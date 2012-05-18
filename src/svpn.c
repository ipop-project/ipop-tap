
#include <stdio.h>
#include <stdlib.h>
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

static void *
udp_send_thread(void *data)
{
    thread_opts_t *opts = (thread_opts_t *) data;
    int sock = opts->sock;
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

        printf("T >> %d %x %x\n", rcount, buf[32], buf[33]);

        if (buf[12] == 0x08 && buf[13] == 0x06 && create_arp_response(buf)) {
            if (write(tap, buf, rcount) < 0) {
                fprintf(stderr, "tap write failed\n");
                break;
            }
            continue;
        }

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

            if (sendto(sock, enc_buf, rcount, 0, (struct sockaddr*) &addr, 
                addrlen) < 0) {
                fprintf(stderr, "sendto failed\n");
            }

            printf("S >> %d %x\n", rcount, (unsigned int)addr.sin_addr.s_addr);

            if (idx++ == -1) break;
        }
    }

    close(sock);
    close(tap);
    pthread_exit(NULL);
}

static void *
udp_recv_thread(void *data)
{
    thread_opts_t *opts = (thread_opts_t *) data;
    int sock = opts->sock;
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

        if ((rcount = recvfrom(sock, dec_buf, BUFLEN, 0, 
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

    close(tap);
    close(sock);
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
        set_local_peer(inputs[1], opts->local_ip);
        printf("id = %s ip = %s\n", inputs[1], opts->local_ip);
    }
    else if (strcmp(inputs[0], "add") == 0) {
        add_peer(inputs[1], inputs[2], atoi(inputs[3]), inputs[4], inputs[5]);
        strncpy(id, inputs[1], ID_SIZE);
        get_source_info(id, source, dest, key);
        printf("id = %s ip = %s addr = %s\n", id, 
            inet_ntoa(*(struct in_addr*)source), inputs[5]);
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

int
main(int argc, char *argv[])
{
    char ip[] = "172.31.0.100";
    thread_opts_t opts;
    opts.sock = create_udp_socket(5800);
    opts.tap = open_tap("svpn0", opts.mac);
    opts.local_ip = ip;
    opts.dtls = 0;
    init_dtls(&opts);
    configure_tap(opts.tap, ip, MTU);
    set_local_peer("nobody", ip);

    // drop root priviledges and set to nobody
    // I need to add chroot jail in here later
    struct passwd * pwd = getpwnam("nobody");
    if (getuid() == 0) {
        if (setgid(pwd->pw_uid) < 0) {
            fprintf(stderr, "setgid failed\n");
            close(opts.tap);
            close(opts.sock);
            return -1;
        }
        if (setuid(pwd->pw_gid) < 0) {
            fprintf(stderr, "setuid failed\n");
            close(opts.tap);
            close(opts.sock);
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

