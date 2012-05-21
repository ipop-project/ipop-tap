
#include <stdio.h>
#include <stdlib.h>
#include <time.h> // used to generate random seed
#include <sys/socket.h>
#include <arpa/inet.h>
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

static void *
udp_send_thread(void *data)
{
    thread_opts_t *opts = (thread_opts_t *) data;
    int sock4 = opts->sock4;
    int sock6 = opts->sock6;
    int tap = opts->tap;

    int rcount;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

    unsigned char buf[BUFLEN];
    unsigned char enc_buf[BUFLEN];
    unsigned char key[KEY_SIZE] = { 0 };
    unsigned char iv[KEY_SIZE] = { 0 };
    unsigned char p2p_addr[ADDR_SIZE] = { 0 };
    char source_id[ID_SIZE] = { 0 };
    char dest_id[ID_SIZE] = { 0 };
    int idx;

    while (1) {

        idx = 0;
        if ((rcount = read(tap, buf, BUFLEN)) < 0) {
            fprintf(stderr, "tap read failed\n");
            break;
        }

        if (buf[14] == 0x45) { // ipv4 packet
            printf("T >> %d %x %x\n", rcount, buf[32], buf[33]);

            while (get_dest_info((char *)buf + 30, source_id, dest_id, &addr,
                (char *)key, (char *)p2p_addr, &idx) >= 0) {

                translate_packet(buf, NULL, NULL, rcount);

                set_headers(enc_buf, source_id, dest_id, iv);

                if (opts->dtls == 1) {
                    set_headers(enc_buf, source_id, dest_id, p2p_addr);
                    memcpy(enc_buf + BUF_OFFSET, buf, rcount);
                    svpn_dtls_send(enc_buf, rcount + BUF_OFFSET);
                    if (idx++ == -1) break;
                    continue;
                }

                rcount = aes_encrypt(buf, enc_buf + BUF_OFFSET, key, iv,
                    rcount);

                rcount += BUF_OFFSET;

                if (sendto(sock4, enc_buf, rcount, 0, (struct sockaddr*) &addr,
                    addrlen) < 0) {
                    fprintf(stderr, "sendto failed\n");
                }

                printf("S >> %d %x\n", rcount, (unsigned int)addr.sin_addr.s_addr);

                if (idx++ == -1) break;
            }
        } else if (buf[14] == 0x60) { // ipv6 packet
            fprintf(stderr, "We got an IPv6 packet from the tap, but we don't "
                            "know how to send it out through a socket yet.\n");
        } else {
            fprintf(stderr, "Cannot determine packet type to be an IPv4 or 6 "
                            "packet.\n");
        }
    }

    close(sock4);
    // close(sock6);
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

    unsigned char buf[BUFLEN];
    unsigned char dec_buf[BUFLEN];
    unsigned char key[KEY_SIZE] = { 0 };
    unsigned char iv[KEY_SIZE] = { 0 };
    char source_id[KEY_SIZE] = { 0 };
    char dest_id[KEY_SIZE] = { 0 };
    char source[4];
    char dest[4];

    while (1) {

        if ((rcount = recvfrom(sock4, dec_buf, BUFLEN, 0,
               (struct sockaddr*) &addr, &addrlen)) < 0) {
            fprintf(stderr, "upd recv failed\n");
            break;
        }

        printf("S << %d %x\n", rcount, (unsigned int)addr.sin_addr.s_addr);

        if (opts->dtls == 1) {
            svpn_dtls_process(dec_buf, rcount);
            continue;
        }

        get_headers(dec_buf, source_id, dest_id, iv);

        if (get_source_info(source_id, source, dest, (char *)key) < 0) {
            fprintf(stderr, "info not found\n");
            continue;
        }

        rcount -= BUF_OFFSET;
        rcount = aes_decrypt(dec_buf + BUF_OFFSET, buf, key, iv, rcount);

        translate_packet(buf, source, dest, rcount);

        if (translate_headers(buf, source, dest, opts->mac, rcount) < 0) {
            fprintf(stderr, "translate error\n");
            continue;
        }

        if (write(tap, buf, rcount) < 0) {
            fprintf(stderr, "write to tap error\n");
            break;
        }
        printf("T << %d %x %x\n", rcount, buf[32], buf[33]);

    }

    close(sock4);
    // close(sock6);
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
    char source[4];
    char dest[4];
    char key[KEY_SIZE];
    char id[ID_SIZE] = { 0 };

    if (strcmp(inputs[0], "setid") == 0) {
        set_local_peer(inputs[1], opts->local_ip4);
        printf("id = %s ipv4 = %s ipv6 = %s\n",
               inputs[1], opts->local_ip4, opts->local_ip6);
    }
    else if (strcmp(inputs[0], "add") == 0) {
        add_peer(inputs[1], inputs[2], inputs[3], atoi(inputs[3]), inputs[4],
                 inputs[5]);
        strncpy(id, inputs[1], ID_SIZE);
        get_source_info(id, source, dest, key);
        printf("id = %s ip = %s addr = %s\n", id,
            inet_ntoa(*(struct in_addr*)source), inputs[4]);
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
                        "supported");
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
}

int
main(int argc, char *argv[])
{
    char ipv4_addr[] = "172.31.0.100";
    srand(time(NULL)); // set up random number generator
    char ipv6_addr[39];
    generate_ipv6_address("fd50:0dbc:41f2:4a3c", 64, ipv6_addr);
    thread_opts_t opts;

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
    set_local_peer("nobody", ipv4_addr);

    // drop root priviledges and set to nobody
    // I need to add chroot jail in here later
    struct passwd * pwd = getpwnam("nobody");
    if (getuid() == 0) {
        if (setgid(pwd->pw_uid) < 0) {
            fprintf(stderr, "setgid failed\n");
            close(opts.sock4);
            // close(opts.sock6);
            tap_close();
            return -1;
        }
        if (setuid(pwd->pw_gid) < 0) {
            fprintf(stderr, "setuid failed\n");
            close(opts.sock4);
            // close(opts.sock6);
            tap_close();
            return -1;
        }
    }

    pthread_t send_thread, recv_thread, dtls_thread;
    pthread_create(&send_thread, NULL, udp_send_thread, &opts);
    pthread_create(&recv_thread, NULL, udp_recv_thread, &opts);

    char buf[100] = { '0' };
    char * inputs[6];
    int i, j;

    while (fgets(buf, sizeof(buf), stdin) != NULL) {
        printf("fgets %s", buf);

        // trim newline
        buf[strlen(buf)-1] = ' ';

        i = j = 0;
        inputs[j++] = buf + i;

        while (buf[i] != '\0' && i < sizeof(buf)) {
            if (buf[i] == ' ') {
                buf[i] = '\0';
                inputs[j++] = buf + i + 1;

                if (j == 6) break;
            }
            i++;
        }
        process_inputs(&opts, inputs, &dtls_thread);
    }
    return 0;
}
