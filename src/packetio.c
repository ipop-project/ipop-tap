
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <pthread.h>

#include <peerlist.h>
#include <headers.h>
#include <translator.h>
#include <tap.h>
#include <svpn.h>
#include <packetio.h>

/**
 * Reads packet data from the tap device that was locally written, and sends it
 * off through a socket to the relevant peer(s).
 */
void *
udp_send_thread(void *data)
{
    thread_opts_t *opts = (thread_opts_t *) data;
    int sock4 = opts->sock4;
    int sock6 = opts->sock6;
    int tap = opts->tap;
    struct threadqueue *queue = opts->queue;

    int rcount;

    unsigned char buf[BUFLEN];
    unsigned char enc_buf[BUFLEN];
    struct peer_state *peer = NULL;
    int peercount, is_ipv6;

    while (1) {
        if ((rcount = read(tap, buf, BUFLEN)) < 0) {
            fprintf(stderr, "tap read failed\n");
            break;
        }

        if ((buf[14] >> 4) == 0x04) { // ipv4 packet
#ifdef DEBUG
            printf("T >> (ipv4) %d\n", rcount);
#endif
            struct in_addr local_ipv4_addr = {
                .s_addr = *(unsigned long *)(buf + 30)
            };

            peercount= peerlist_get_by_local_ipv4_addr(&local_ipv4_addr, &peer);
            is_ipv6 = 0;
        } else if ((buf[14] >> 4) == 0x06) { // ipv6 packet
#ifdef DEBUG
            printf("T >> (ipv6) %d\n", rcount);
#endif
            struct in6_addr local_ipv6_addr;
            memcpy(&local_ipv6_addr.s6_addr, buf + 38, 16);

            peercount= peerlist_get_by_local_ipv6_addr(&local_ipv6_addr, &peer);
            is_ipv6 = 1;
        } else {
            fprintf(stderr, "Warning: unknown IP packet type: 0x%x\n", buf[14]);
            continue;
        }

        if (peercount >= 0) {
            if (peercount == 0)
                peercount = 1; // non-multicast, so only one peer
            else if (peercount == 1)
                continue; // multicast, but no peers are connected
            else
                peercount--; // multicast, variable peercount
        } else {
            continue; // non-multicast, no peers found
        }

        // translate and send all the packets
        for(int i = 0; i < peercount; i++) {
            set_headers(enc_buf, peerlist_local.id, peer[i].id);
            if (!is_ipv6) { // IPv4 Packet
                // IPv4 has no security mechanism in place at the moment
                // IPv4 needs in-packet address translation
                translate_packet(buf, NULL, NULL, rcount);
                memcpy(enc_buf + BUF_OFFSET, buf, rcount);
                rcount += BUF_OFFSET;
            } else { // IPv6 Packet
                // IPv6 will typically use IPSec for security
                // (not handled by us)
                // Send the data without encrypting it ourselves:
                memcpy(enc_buf + BUF_OFFSET, buf, rcount);
                rcount += BUF_OFFSET;
            }

            if (queue != NULL) {
                if (thread_queue_put(queue, enc_buf, rcount) < 0) {
                    fprintf(stderr, "thread queue error\n");
                    pthread_exit(NULL);
                }
            }

            struct sockaddr_in dest_ipv4_addr_sock = {
                .sin_family = AF_INET,
                .sin_port = htons(peer[i].port),
                .sin_addr = peer[i].dest_ipv4_addr,
                .sin_zero = { 0 }
            };

            // send our processed packet off
            if (sendto(sock4, enc_buf, rcount, 0,
                       (struct sockaddr *)(&dest_ipv4_addr_sock),
                       sizeof(struct sockaddr_in)) < 0) {
                fprintf(stderr, "sendto failed\n");
                pthread_exit(NULL);
            }
        }
    }

    close(sock4);
    close(sock6);
    tap_close();
    pthread_exit(NULL);
}

/**
 * Reads packet data from the socket that we received from a remote peer, and
 * writes it to the local tap device, making the traffic show up locally.
 */
void *
udp_recv_thread(void *data)
{
    thread_opts_t *opts = (thread_opts_t *) data;
    int sock4 = opts->sock4;
    int sock6 = opts->sock6;
    int tap = opts->tap;
    struct threadqueue *queue = opts->queue;

    int rcount;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

    unsigned char buf[BUFLEN];
    unsigned char dec_buf[BUFLEN];
    char source_id[ID_SIZE+1] = { 0 };
    char dest_id[ID_SIZE+1] = { 0 };
    struct peer_state *peer = NULL;
    struct threadmsg msg;

    while (1) {
        if (queue != NULL) {
            if (thread_queue_get(queue, NULL, &msg) != 0) {
              fprintf(stderr, "threadqueue get failed\n");
              break;
            }
            // msgtype is used to carry message size instead of type
            memcpy(dec_buf, msg.data, msg.msgtype);
        }
        else if ((rcount = recvfrom(sock4, dec_buf, BUFLEN, 0,
                               (struct sockaddr*) &addr, &addrlen)) < 0) {
            fprintf(stderr, "upd recv failed\n");
            break;
        }
        get_headers(dec_buf, source_id, dest_id);

        if (peerlist_get_by_id(source_id, &peer) < 0) {
            fprintf(stderr, "Received data from unknown peer with id: '%s'. "
                            "Ignoring.\n", source_id);
            continue;
        }

        rcount -= BUF_OFFSET;
        memcpy(buf, dec_buf + BUF_OFFSET, rcount);
        if ((buf[14] >> 4) == 0x04) { // IPv4 Packet
#ifdef DEBUG
            printf("R << (ipv4) %d\n", rcount);
#endif
            // no encryption handling yet
            translate_packet(buf, (char *)(&peer->local_ipv4_addr.s_addr),
                             (char *)(&peerlist_local.local_ipv4_addr.s_addr),
                             rcount);
            translate_headers(buf, (char *)(&peer->local_ipv4_addr.s_addr),
                              (char *)(&peerlist_local.local_ipv4_addr.s_addr),
                              rcount);
        } else if ((buf[14] >> 4) == 0x06) { // IPv6 Packet
#ifdef DEBUG
            printf("R << (ipv6) %d\n", rcount);
#endif
        } else {
            fprintf(stderr, "Warning: unknown IP packet type: 0x%x\n", buf[14]);
            continue;
        }
        translate_mac(buf, opts->mac);
        // Write the buffered data back to the TAP device
        if (write(tap, buf, rcount) < 0) {
            fprintf(stderr, "write to tap error\n");
            break;
        }
    }

    close(sock4);
    close(sock6);
    tap_close();
    pthread_exit(NULL);
}

