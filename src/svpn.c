
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

typedef struct thread_opts {
    int sock;
    int tap;
    char mac[6];
    char *local_ip;
} thread_opts_t;

static int
create_udp_socket(uint16_t port)
{
    int sock, optval = 1;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 1) {
        fprintf(stderr, "socket failed\n");
        return -1;
    }

    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    memset(&addr, 0, addr_len);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr*) &addr, addr_len) < 0) {
        fprintf(stderr, "bind failed\n");
        close(sock);
        return -1;
    }
    return sock;
}

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
            write(tap, buf, rcount);
            continue;
        }

        while (get_dest_info((char *)buf + 30, source_id, dest_id, &addr, 
            (char *)key, &idx) >= 0) {

            translate_packet(buf, NULL, NULL, rcount);

            set_headers(enc_buf, source_id, dest_id, iv);
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

static int
process_inputs(thread_opts_t *opts, char *inputs[])
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
        add_peer(inputs[1], inputs[2], atoi(inputs[3]), inputs[4]);
        strncpy(id, inputs[1], ID_SIZE);
        get_source_info(id, source, dest, key);
        printf("id = %s ip = %s\n", id, inet_ntoa(*(struct in_addr*)source));
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
    configure_tap(opts.tap, ip, MTU);
    set_local_peer("nobody", ip);

    // drop root priviledges and set to nobody
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

    pthread_t send_thread, recv_thread;
    pthread_create(&send_thread, NULL, udp_send_thread, &opts);
    pthread_create(&recv_thread, NULL, udp_recv_thread, &opts);

    char buf[50] = { '0' };
    char * inputs[5];
    int i, j;

    while (1) {
        fgets(buf, sizeof(buf), stdin);
        printf("fgets %s", buf);

        // trim newline
        buf[strlen(buf)-1] = ' ';

        i = j = 0;
        inputs[j++] = buf + i;

        while (buf[i] != '\0' && i < sizeof(buf)) {
            if (buf[i] == ' ') {
                buf[i] = '\0';
                inputs[j++] = buf + i + 1;

                if (j == 5) break;
            }
            i++;
        }
        process_inputs(&opts, inputs);
    }
}

