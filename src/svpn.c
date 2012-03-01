
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>

#include <translator.h>
#include <router.h>
#include <tap.h>

#define MTU 1200
#define BUF_OFFSET 12
#define BUFLEN 2048
#define ID_SIZE 12

typedef struct thread_opts {
    int sock;
    int tap;
    char mac[6];
    char id[ID_SIZE];
    char s_ip[16];
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

    ssize_t rcount;
    struct sockaddr_in dest;
    socklen_t addrlen = sizeof(dest);
    char buf[BUFLEN];
    char *obuf = buf + BUF_OFFSET;
    int idx;


    while (1) {

        idx = 0;

        if ((rcount = read(tap, obuf, BUFLEN)) < 0) {
            fprintf(stderr, "tap read failed\n");
            break;
        }

        printf("read from tap %d\n", rcount);

        if (obuf[12] == 0x08 && obuf[13] == 0x06) {
            if (create_arp_response(obuf) == 0) {
                write(tap, obuf, rcount);
            }
        }
        else {

            rcount += BUF_OFFSET;
            while (get_dest_addr(&dest, obuf + 30, &idx, 1, obuf) >= 0) {

                memcpy(buf, opts->id, ID_SIZE);
                translate_packet(obuf, NULL, NULL, rcount - BUF_OFFSET);

                if (sendto(sock, buf, rcount, 0, (struct sockaddr*) &dest, 
                    addrlen) < 0) {
                    fprintf(stderr, "sendto failed\n");
                }

                printf("sent to udp %x %d\n", *(unsigned int*) obuf+30, rcount);

                if (idx++ == -1) {
                    break;
                }
            }
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

    ssize_t rcount;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    char buf[BUFLEN];
    char *obuf = buf + BUF_OFFSET;
    char source[4];
    char dest[4];

    while (1) {

        if ((rcount = recvfrom(sock, buf, BUFLEN, 0, (struct sockaddr*) &addr, 
                               &addrlen)) < 0) {
            fprintf(stderr, "upd recv failed\n");
            break;
        }

        printf("recv from udp %d\n", rcount);

        if (get_source_addr(buf, obuf + 30, source, dest) < 0) {
            fprintf(stderr, "ip not found\n");
            continue;
        }

        rcount -= BUF_OFFSET;
        translate_packet(obuf, source, dest, rcount);

        if (translate_headers(obuf, source, dest, opts->mac, rcount) < 0) {
            fprintf(stderr, "translate error\n");
            continue;
        }

        if (write(tap, obuf, rcount) < 0) {
            fprintf(stderr, "write to tap error\n");
            break;
        }
        printf("wrote to tap %d\n", rcount);
    }

    close(tap);
    close(sock);
    pthread_exit(NULL);
}

static int
process_inputs(thread_opts_t *opts, char *inputs[])
{
    if (strcmp(inputs[0], "setid") == 0) {
        strncpy(opts->id, inputs[1], ID_SIZE);
        printf("id = %s\n", opts->id);
    }
    else if (strcmp(inputs[0], "add") == 0) {
        add_route(inputs[1], inputs[2], atoi(inputs[3]));
    }
    return 0;
}

int
main(int argc, char *argv[])
{
    thread_opts_t opts;
    opts.sock = create_udp_socket(5800);
    opts.tap = open_tap("svpn0", opts.mac);
    strncpy(opts.s_ip, "172.31.0.100", 16);
    configure_tap(opts.tap, opts.s_ip, MTU);
    set_local_ip(opts.s_ip);

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

