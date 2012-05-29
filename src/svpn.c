
#include <stdio.h>
#include <stdlib.h>
#include <time.h> // used to generate random seed
#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>

#include <translator.h>
#include <peerlist.h>
#include <tap.h>
#include <encryption.h>
#include <headers.h>
#include <svpn.h>
#include <dtls.h>
#include <socket_utils.h>

/**
 * Reads packet data from the tap device that was locally written, and sends it
 * off through a socket to the relevant peer(s).
 */
static void *
udp_send_thread(void *data)
{
    thread_opts_t *opts = (thread_opts_t *) data;
    int sock4 = opts->sock4;
    int sock6 = opts->sock6;
    int tap = opts->tap;

    int rcount;

    unsigned char buf[BUFLEN];
    unsigned char enc_buf[BUFLEN];
    unsigned char iv[KEY_SIZE] = { 0 };
    struct peer_state *peer = NULL;
    int peercount, is_ipv6;

    while (1) {

        if ((rcount = read(tap, buf, BUFLEN)) < 0) {
            fprintf(stderr, "tap read failed\n");
            break;
        }

        if (buf[14] == 0x45) { // ipv4 packet
            printf("T >> (ipv4) %d %x %x\n", rcount, buf[32], buf[33]);

            struct in_addr local_ipv4_addr = {
                .s_addr = *(unsigned long *)(buf + 30)
            };

            peercount= peerlist_get_by_local_ipv4_addr(&local_ipv4_addr, &peer);
            is_ipv6 = 0;
        } else if (buf[14] == 0x60) { // ipv6 packet
            printf("T >> (ipv6) %d\n", rcount);

            struct in6_addr local_ipv6_addr;
            memcpy(&local_ipv6_addr.s6_addr, buf + 38, 16);

            peercount= peerlist_get_by_local_ipv6_addr(&local_ipv6_addr, &peer);
            is_ipv6 = 1;
        } else {
            fprintf(stderr, "Cannot determine packet type to be an IPv4 or 6 "
                            "packet.\n");
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
            if (!is_ipv6) translate_packet(buf, NULL, NULL, rcount);

            set_headers(enc_buf, peerlist_local.id, peer[i].id, iv);

            if (opts->dtls == 1 && !is_ipv6) {
                // send the data with dtls
                set_headers(enc_buf, peerlist_local.id, peer[i].id,
                            (unsigned char *)peer[i].p2p_addr);
                memcpy(enc_buf + BUF_OFFSET, buf, rcount);
                svpn_dtls_send(enc_buf, rcount + BUF_OFFSET);
                continue;
            }

            // send the data without dtls
            rcount = aes_encrypt(buf, enc_buf + BUF_OFFSET,
                                 (unsigned char *)peer[i].key, iv, rcount);

            rcount += BUF_OFFSET;

            struct sockaddr_in dest_ipv4_addr_sock = {
                .sin_family = AF_INET,
                .sin_port = htons(peer[i].port),
                .sin_addr = peer[i].dest_ipv4_addr,
                .sin_zero = { 0 }
            };
            if (sendto(sock4, enc_buf, rcount, 0,
                       (struct sockaddr *)(&dest_ipv4_addr_sock),
                       sizeof(struct sockaddr_in)) < 0) {
                fprintf(stderr, "sendto failed\n");
            }

            printf("S >> %d %x\n", rcount, peer[i].dest_ipv4_addr.s_addr);
        }
    }

    close(sock4);
    close(sock6);
    tap_close();
    pthread_exit(NULL);
}

static void *
udp_recv_thread(void *data)
{
    thread_opts_t *opts = (thread_opts_t *) data;
    int sock4 = opts->sock4;
    int sock6 = opts->sock6;
    int tap = opts->tap;

    int rcount;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

    char source[4];
    char dest[4];

    unsigned char buf[BUFLEN];
    unsigned char dec_buf[BUFLEN];
    unsigned char iv[KEY_SIZE] = { 0 };
    char source_id[ID_SIZE+1] = { 0 };
    char dest_id[ID_SIZE+1] = { 0 };
    struct peer_state *peer = NULL;

    while (1) {

        if ((rcount = recvfrom(sock4, dec_buf, BUFLEN, 0,
                               (struct sockaddr*) &addr, &addrlen)) < 0) {
            fprintf(stderr, "upd recv failed\n");
            break;
        }

        printf("S << %d %x\n", rcount, addr.sin_addr.s_addr);

        if (opts->dtls == 1) {
            svpn_dtls_process(dec_buf, rcount);
            continue;
        }

        get_headers(dec_buf, source_id, dest_id, iv);

        if (peerlist_get_by_id(source_id, &peer) < 0) {
            fprintf(stderr, "info not found\n");
            continue;
        }
        memcpy(source, &peer->local_ipv4_addr.s_addr, sizeof(source));
        memcpy(dest, &peerlist_local.local_ipv4_addr.s_addr, sizeof(source));

        rcount -= BUF_OFFSET;
        rcount = aes_decrypt(dec_buf + BUF_OFFSET, buf,
                             (unsigned char *)peer->key, iv, rcount);
        // the translated inner packet goes into buf

        translate_packet(buf, source, dest, rcount);

        translate_mac(buf, opts->mac);
        if (buf[14] == 0x45) {
            if (translate_headers(buf, source, dest, rcount) < 0) {
                fprintf(stderr, "translate error\n");
                continue;
            }
        } else if (buf[14] == 0x60) {
            // ipv6: nothing to do!
        } else {
            fprintf(stderr, "Warning: unknown IP packet type: 0x%x\n", buf[14]);
        }

        if (write(tap, buf, rcount) < 0) {
            fprintf(stderr, "write to tap error\n");
            break;
        }
        printf("T << %d %x %x\n", rcount, buf[32], buf[33]);

    }

    close(sock4);
    close(sock6);
    tap_close();
    pthread_exit(NULL);
}

static void *
dtls_recv_thread(void *data)
{
    start_dtls_client(data);
    pthread_exit(NULL);
}

static int
process_inputs(thread_opts_t *opts, char *inputs[], void *data)
{
    char id[ID_SIZE+1] = { 0 };

    if (strcmp(inputs[0], "setid") == 0) {
        peerlist_set_local_p(inputs[1], opts->local_ip4, opts->local_ip6);
        printf("id = %s ipv4 = %s ipv6 = %s\n",
               inputs[1], opts->local_ip4, opts->local_ip6);
    }
    else if (strcmp(inputs[0], "add") == 0) {
        peerlist_add_p(inputs[1], inputs[2], inputs[3], atoi(inputs[4]),
                       inputs[5], inputs[6]);
        strncpy(id, inputs[1], ID_SIZE);
        struct peer_state *peer;
        peerlist_get_by_id(id, &peer);
        printf("id = %s ip = %s addr = %s\n", id,
               inet_ntoa(*(struct in_addr*)(&peer->local_ipv4_addr)),
               inputs[4]);
    }
    else if (strcmp(inputs[0], "dtls") == 0) {
        if (opts->dtls == 0) {
            pthread_t *dtls_thread = (pthread_t *) data;
            strncpy(opts->dtls_ip, inputs[1], 16);
            opts->dtls_port = atoi(inputs[2]);
            opts->dtls = 1;
            pthread_create(dtls_thread, NULL, dtls_recv_thread, opts);
        }
        else {
            fprintf(stderr, "dtls is already active\n");
        }
    }
    return 0;
}

/**
 * Generates a string containing an IPv6 address with the given prefix. Maximum
 * written string length is 39 characters, and is written to `address`.
 */
static int
generate_ipv6_address(char *prefix, unsigned short prefix_len, char *address)
{
    if (prefix_len % 16) {
        fprintf(stderr, "Bad prefix_len value. (only multiples of 16 are "
                        "supported\n");
        return -1;
    }
    unsigned short blocks_left = (128-prefix_len) / 16;
    strcpy(address, prefix);
    for (int i = 0; i < blocks_left; i++) {
        if (blocks_left-i <= 1)
            // ensure the last block is of constant string length
            sprintf(&address[strlen(address)], ":%x",
                    (rand() % (0xEFFF+1)) + 0x1000);
        else
            sprintf(&address[strlen(address)], ":%x", rand() % (0xFFFF+1));
    }
    return 0;
}

static void
get_ipv6_address(char *address) {
    FILE* fd;
    if ((fd = fopen(IPV6_ADDR_FILE, "r")) == NULL) {
        generate_ipv6_address("fd50:0dbc:41f2:4a3c", 64, address);
        if ((fd = fopen(IPV6_ADDR_FILE, "w")) == NULL) {
            fprintf(stderr, "Could not write ip address back to file\n");
        } else {
            fputs(address, fd);
            fclose(fd);
        }
    } else {
        fgets(address, 40, fd);
        if (address[strlen(address)-1] == '\n') {
            address[strlen(address)-1] = '\0'; // strip newline
        }
        fclose(fd);
    }
}

int
main(int argc, char *argv[])
{
    char* ipv4_addr = "172.31.0.100";
    srand(time(NULL)); // set up random number generator
    char ipv6_addr[8*5];
    get_ipv6_address(ipv6_addr);
    thread_opts_t opts;
    peerlist_init(TABLE_SIZE);

    opts.tap = tap_open("svpn0", opts.mac);
    opts.local_ip4 = ipv4_addr;
    opts.local_ip6 = ipv6_addr;
    opts.sock4 = socket_utils_create_ipv4_udp_socket(5800);
    opts.sock6 = socket_utils_create_ipv6_udp_socket(
        5800, if_nametoindex("svpn0")
    );
    opts.dtls = 0;
    init_dtls(&opts);

    // configure the tap device
    tap_set_ipv4_addr(ipv4_addr, 24);
    tap_set_ipv6_addr(ipv6_addr, 64);
    tap_set_mtu(MTU);
    tap_set_base_flags();
    tap_set_up();
    // cleanup_tap();
    peerlist_set_local_p("nobody", ipv4_addr, ipv6_addr);

    // drop root priviledges and set to nobody
    // I need to add chroot jail in here later
    struct passwd * pwd = getpwnam("nobody");
    if (getuid() == 0) {
        if (setgid(pwd->pw_uid) < 0) {
            fprintf(stderr, "setgid failed\n");
            close(opts.sock4);
            close(opts.sock6);
            tap_close();
            return -1;
        }
        if (setuid(pwd->pw_gid) < 0) {
            fprintf(stderr, "setuid failed\n");
            close(opts.sock4);
            close(opts.sock6);
            tap_close();
            return -1;
        }
    }

    pthread_t send_thread, recv_thread, dtls_thread;
    pthread_create(&send_thread, NULL, udp_send_thread, &opts);
    pthread_create(&recv_thread, NULL, udp_recv_thread, &opts);

    char buf[200] = { '0' };
    char * inputs[7];
    int i, j;

    while (fgets(buf, sizeof(buf), stdin) != NULL) {
        printf("fgets %s", buf);

        // trim newline
        buf[strlen(buf)-1] = ' ';

        i = j = 0;
        inputs[j++] = buf + i;

        while (buf[i] != '\0' && i < sizeof(buf)/sizeof(char)) {
            if (buf[i] == ' ') {
                buf[i] = '\0';
                inputs[j++] = buf + i + 1;

                if (j == sizeof(inputs)/sizeof(char *)) break;
            }
            i++;
        }
        process_inputs(&opts, inputs, &dtls_thread);
    }
    return 0;
}
