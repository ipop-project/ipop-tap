/*
 * ipop-tap
 * Copyright 2013, University of Florida
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#if defined(LINUX) || defined(ANDROID)
#include <sys/socket.h>
#include <net/if.h>
#include <arpa/inet.h>
#elif defined(WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdint.h>
#include <win32_tap.h>
#endif

#include "peerlist.h"
#include "headers.h"
#include "translator.h"
#include "tap.h"
#include "ipop_tap.h"
#include "packetio.h"

/**
 * Reads packet data from the tap device that was locally written, and sends it
 * off through a socket to the relevant peer(s).
 */
void *
ipop_send_thread(void *data)
{
    // thread_opts data structure is shared by both send and recv threads
    // so do not modify its contents only read
    thread_opts_t *opts = (thread_opts_t *) data;
    int sock4 = opts->sock4;
    int sock6 = opts->sock6;
#if defined(LINUX) || defined(ANDROID)
    int tap = opts->tap;
#elif defined(WIN32)
    windows_tap *win32_tap = opts->win32_tap;
#endif

    int rcount, ncount;

    // ipop_buf will contain 40-byte header + ethernet frame
    unsigned char ipop_buf[BUFLEN];

    // BUF_OFFSET leaves 40-bytes for header
    unsigned char *buf = ipop_buf + BUF_OFFSET ;
    struct in_addr local_ipv4_addr;
    struct in6_addr local_ipv6_addr;
    struct peer_state *peer = NULL;
    int result, is_ipv4;

    while (1) {

        int arp = 0;

#if defined(LINUX) || defined(ANDROID)
        if ((rcount = read(tap, buf, BUFLEN-BUF_OFFSET)) < 0) {
#elif defined(WIN32)
        if ((rcount = read_tap(win32_tap, (char *)buf, BUFLEN-BUF_OFFSET)) < 0) {
#endif
            fprintf(stderr, "tap read failed\n");
            break;
        }

        ncount = rcount + BUF_OFFSET;

        /*---------------------------------------------------------------------
        Switchmode
        ---------------------------------------------------------------------*/
        if (opts->switchmode) {

            // Check whether target of ARP request message is tap itself
            if (is_arp_req(buf) &&
                is_my_ip4(buf, (const unsigned char *) opts->my_ip4)) {
                create_arp_response_sw(buf, (unsigned char * ) opts->mac, 
                                       (unsigned char *) opts->my_ip4);
                // Write back ARP reply to tap device
#if defined(LINUX) || defined(ANDROID)
                int r = write(tap, buf, rcount);
#elif defined(WIN32)
                int r = write_tap(win32_tap, (char *)buf, rcount);
#endif
                if (r < 0) {
                    fprintf(stderr, "write to tap failed\n");
                }
            }

            /* If the frame is broadcast message, it sends the frame to
               every TinCan links as physical switch does */
            if (is_nonunicast(buf)) {
                reset_id_table();
                while( !is_id_table_end() ) {
                    if ( is_id_exist() )  {
                        /* TODO It may be better to retrieve the iterator rather
                           than key string itself.  */
                        peer = retrieve_peer();
                        set_headers(ipop_buf, peerlist_local.id, peer->id);
                        if (opts->send_func != NULL) {
                            if (opts->send_func((const char*)ipop_buf, ncount) < 0) {
                                fprintf(stderr, "send_func failed\n");
                            }
                        }
                    }
                    increase_id_table_itr();
                }
                continue;
            }

            /* If the MAC address is in the table, we forward the frame to
               destined TinCan link */
            peerlist_get_by_mac_addr(buf, &peer);
            set_headers(ipop_buf, peerlist_local.id, peer->id);
            if (opts->send_func != NULL) {
                if (opts->send_func((const char*)ipop_buf, ncount) < 0) {
                    fprintf(stderr, "send_func failed\n");
                }
            }
            continue;
        }

        /*---------------------------------------------------------------------
        Conventional IPOP Tap (non-switchmode)
        ---------------------------------------------------------------------*/

        // checks to see if this is an ARP request, if so, send response
        if (buf[12] == 0x08 && buf[13] == 0x06 && buf[21] == 0x01
            && !opts->switchmode) {
            if (create_arp_response(buf) == 0) {
#if defined(LINUX) || defined(ANDROID)
                int r = write(tap, buf, rcount);
#elif defined(WIN32)
                int r = write_tap(win32_tap, (char *)buf, rcount);
#endif
                // This doesn't handle partial writes yet, we need a loop to
                // guarantee a full write.
                if (r < 0) {
                    fprintf(stderr, "tap write failed\n");
                    break;
                }
            }
            continue;
        }


        if ((buf[14] >> 4) == 0x04) { // ipv4 packet
            memcpy(&local_ipv4_addr.s_addr, buf + 30, 4);
            is_ipv4 = 1;
        } else if ((buf[14] >> 4) == 0x06) { // ipv6 packet
            memcpy(&local_ipv6_addr.s6_addr, buf + 38, 16);
            is_ipv4 = 0;
        } else if (buf[12] == 0x08 && buf[13] == 0x06 && opts->switchmode) {
            arp = 1;
            is_ipv4 = 0;
        } else {
            fprintf(stderr, "unknown IP packet type: 0x%x\n", buf[14] >> 4);
            continue;
        }

        // we need to update the size of packet to account for ipop header
        ncount = rcount + BUF_OFFSET;

        // we need to initialize peerlist
        peerlist_reset_iterators();
        while (1) {
            if (arp || is_ipv4) {
                result = peerlist_get_by_local_ipv4_addr(&local_ipv4_addr,
                                                         &peer);
            } else {
                result = peerlist_get_by_local_ipv6_addr(&local_ipv6_addr,
                                                         &peer);
            }

            // -1 means something went wrong, should not happen
            if (result == -1) break;

            if (arp) {
                // ARP message should not be forwarded to peers but to 
                // controller only
                set_headers(ipop_buf, peerlist_local.id, null_peer.id);
            } else {
                // we set ipop header by copying local peer uid as first
                // 20-bytes and then dest peer uid as the next 20-bytes. That is
                // necessary for routing by upper layers
                set_headers(ipop_buf, peerlist_local.id, peer->id);
            }

            // we only translate if we have IPv4 packet and translate is on
            if (!arp && is_ipv4 && opts->translate) {
                translate_packet(buf, NULL, NULL, rcount);
            }

            // If the send_func function pointer is set then we use that to
            // send packet to upper layers, in IPOP-Tincan this function just
            // adds to a send blocking queue. If this is not set, then we
            // send to the IP/port stored in the peerlist when the node was
            // added to the network
            if (opts->send_func != NULL) {
                if (opts->send_func((const char*)ipop_buf, ncount) < 0) {
                    fprintf(stderr, "send_func failed\n");
                }
            }
            else {
                // this is portion of the code allows ipop-tap nodes to
                // communicate directly among themselves if necessary but
                // there is no encryption provided with this approach
                struct sockaddr_in dest_ipv4_addr_sock = {
                    .sin_family = AF_INET,
                    .sin_port = htons(peer->port),
                    .sin_addr = peer->dest_ipv4_addr,
                    .sin_zero = { 0 }
                };
                // send our processed packet off
                if (sendto(sock4, (const char *)ipop_buf, ncount, 0,
                           (struct sockaddr *)(&dest_ipv4_addr_sock),
                           sizeof(struct sockaddr_in)) < 0) {
                    fprintf(stderr, "sendto failed\n");
                }
            }
            if (result == 0) break;
        }
    }

    close(sock4);
    close(sock6);
#if defined(LINUX) || defined(ANDROID)
    tap_close();
#elif defined(WIN32)
    // TODO - Add close socket for tap
    WSACleanup();
#endif
    pthread_exit(NULL);
    return NULL;
}

/**
 * Reads packet data from the socket that we received from a remote peer, and
 * writes it to the local tap device, making the traffic show up locally.
 */
void *
ipop_recv_thread(void *data)
{
    // thread_opts data structure is shared by both send and recv threads
    // so do not modify its contents only read
    thread_opts_t *opts = (thread_opts_t *) data;
    int sock4 = opts->sock4;
    int sock6 = opts->sock6;
#if defined(LINUX) || defined(ANDROID)
    int tap = opts->tap;
#elif defined(WIN32)
    windows_tap *win32_tap = opts->win32_tap;
#endif

    int rcount;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

    // ipop_buf will contain 40-byte header + ethernet frame
    unsigned char ipop_buf[BUFLEN];

    // ipop_buf will contain 40-byte header + ethernet frame
    unsigned char *buf = ipop_buf + BUF_OFFSET;
    char source_id[ID_SIZE] = { 0 };
    char dest_id[ID_SIZE] = { 0 };
    struct peer_state *peer = NULL;

    while (1) {
        // if recv function pointer is set then use that to get packets
        // in IPOP-Tincan, this just reads from a recv blocking queue.
        // Otherwise, just read from the UDP socket
        if (opts->recv_func != NULL) {
            // read from ipop-tincan
            if ((rcount = opts->recv_func((char *)ipop_buf, BUFLEN)) < 0) {
              fprintf(stderr, "recv_func failed\n");
              break;
            }
        }
        else if ((rcount = recvfrom(sock4, (char *)ipop_buf, BUFLEN, 0,
                               (struct sockaddr*) &addr, &addrlen)) < 0) {
            // read from UDP socket (useful for testing)
            fprintf(stderr, "udp recv failed\n");
            break;
        }

        /* ICC message use certain MAC address value (00-69-70-6f-70-0?) to
           identify itself as ICC message. Generally, in this receiving thread,
           we receive the message from TinCan link and put to tap device. But,
           this ICC message need to go to the TinCan manager and then
           controller.*/
        if (is_icc(ipop_buf)) {
            if (opts->send_func != NULL) {
                /* Set destination and source uid field all NULL that tincan pass
                   this message to the controller */
                memset(ipop_buf+ID_SIZE, 0x00, ID_SIZE);
                if (opts->send_func((const char*)ipop_buf, rcount)  < 0) {
                   fprintf(stderr, "send_func failed\n");
                }
            }
            continue;
        }

        // update packet size to remove 40-byte header size, this is
        // important to have correct size when writing packet to VNIC
        rcount -= BUF_OFFSET;

        // read the 20-byte source and dest uids from the ipop header
        get_headers(ipop_buf, source_id, dest_id);

        // ARP request target the tap of myself. It create ARP reply and sends
        // back the message back to the IPOP link it comes from.
        if (is_arp_req(buf) && (opts->switchmode == 1) &&
            is_my_ip4(buf, (const unsigned char *) opts->my_ip4)) {

            // Swaps source and destination UID (IPOP link identifier)
            // so that the ARP reply message goes back to source
            char temp[ID_SIZE];
            memcpy(temp, ipop_buf, ID_SIZE);
            memcpy(ipop_buf, ipop_buf+ID_SIZE, ID_SIZE);
            memcpy(ipop_buf + ID_SIZE, temp, ID_SIZE);

            create_arp_response_sw(buf, (unsigned char *) opts->mac,
                                   (unsigned char *) opts->my_ip4);

            if (opts->send_func != NULL) {
                if (opts->send_func((const char*)ipop_buf,
                    rcount + BUF_OFFSET) < 0) {
                   fprintf(stderr, "send_func failed\n");
                }
            }
            // Do not need to go further
            continue;
        }

        /* L2 broadcasting is forwarded from TinCan link. 
           To make switchmode working, TinCan requires mac learning. Checking
           all ethernet frame may be overkill. So it check only L2 broadcast
           (for BOOTP/DHCP) and ARP for mac learning process.  */
        if (ipop_buf[52] == 0x08 && ipop_buf[53] == 0x06 && 
            (ipop_buf[61] == 0x02 || ipop_buf[61] == 0x01) && 
            opts->switchmode == 1) {
            /* ARP message is forwarded from TinCan links. Add the mac to the
               table  */
            arp_sha_mac_add((const unsigned char *) &ipop_buf);
        }

        if (ipop_buf[40] == 0xff && ipop_buf[41] == 0xff && 
            ipop_buf[42] == 0xff && ipop_buf[43] == 0xff &&
            ipop_buf[44] == 0xff && ipop_buf[45] == 0xff && 
            opts->switchmode == 1) {
            /* L2 Broadcast is forwarded from TinCan links. Add source mac to
               the table  */
            source_mac_add((const unsigned char *) &ipop_buf);
        }

        // perform translation if IPv4 and translate is enabled
        if ((buf[14] >> 4) == 0x04 && opts->translate) {
            int peer_found = peerlist_get_by_id(source_id, &peer);
            // -1 indicates that no peer was found in the list so translation
            // cannot be performed, it is important to keep in mind that the
            // packet will get written to OS even if it is not translated
            // this is necessary for multicast and broadcast packets.
            // TODO - Do not allow untranslated packets to go to OS in svpn
            if (peer_found != -1) {
                // this call updates IP packet payload for MDNS and UPNP
                translate_packet(buf, (char *)(&peer->local_ipv4_addr.s_addr),
                               (char *)(&peerlist_local.local_ipv4_addr.s_addr),
                               rcount);
                // this call updates the IPv4 header with locally assign source
                // and destination ip addresses obtained from the peerlist
                translate_headers(buf, (char *)(&peer->local_ipv4_addr.s_addr),
                               (char *)(&peerlist_local.local_ipv4_addr.s_addr),
                               rcount);
            }
        }

        // it is important to make sure Eternet frame has the correct dest mac
        // address for OS to accept the packet. Since ipop tap mac address is
        // only known locally, this is a mandatory step
        // When we need to broadcast arp request message it should be destined
        // to every nodes in l2 network, so we do not update mac of destination.

        // In switchmode, the destination mac address should be the mac address
        // of final destination mac address but not the ipop mac address. Here, 
        // if the source mac address (buf[6:12]) is the same with the
        // destination  mac address (buf[0:6]), we regard this frame from the
        // remote host. If it is different, we think this frame comes from the
        // container. 
        // More accurate implementation would be tap device
        // keeping ARP table or query O/S whether certain mac address is in 
        // network. 
        if ( opts->switchmode == 0 ||
             (memcmp(buf, buf+6, 6) == 0 && opts->switchmode == 1)) {
            update_mac(buf, opts->mac);
        }
#if defined(LINUX) || defined(ANDROID)
        if (write(tap, buf, rcount) < 0) {
#elif defined(WIN32)
        if (write_tap(win32_tap, (char *)buf, rcount) < 0) {
#endif
            fprintf(stderr, "write to tap error\n");
            break;
        }
    }

    close(sock4);
    close(sock6);
#if defined(LINUX) || defined(ANDROID)
    tap_close();
#elif defined(WIN32)
    // TODO - Add close for windows tap
    WSACleanup();
#endif
    pthread_exit(NULL);
    return NULL;
}

