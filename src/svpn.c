
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>

#define MTU 1200
#define BUF_OFFSET 12
#define BUFLEN MTU + BUF_OFFSET

int translate_headers(char *buf, const char *source, const char *dest,
    const char *mac, ssize_t len);
int create_arp_response(char *buf);

int add_route(const char *id, const char *local_ip, const char *dest_ip,
    const uint16_t port);
int get_dest_addr(struct sockaddr_in *addr, const char *local_ip);
int get_source_addr(const char *id, char *source);

typedef struct thread_opts {
    int sock;
    int tap;
    char mac[6];
    char *id;
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

    strncpy(buf, opts->id, BUF_OFFSET-1);
    buf[BUF_OFFSET - 1] = '\0';

    while (1) {

        idx = 0;

        if ((rcount = read(tap, obuf, MTU)) < 0) {
            fprintf(stderr, "tap read failed\n");
            pthread_exit(NULL);
        }

        printf("read from tap %d\n", rcount);

        if (obuf[12] == 0x08 && obuf[13] == 0x06) {
            if (create_arp_response(obuf) == 0) {
                write(tap, obuf, rcount);
            }
        }
        else {

            while (get_dest_addr(&dest, obuf + 30, &idx) >= 0) {

                if (sendto(sock, buf, rcount + BUF_OFFSET, 0, 
                           (struct sockaddr*) &dest, addrlen) < 0) {
                    fprintf(stderr, "sendto failed %x\n", 
                            (unsigned int) dest.sin_addr.s_addr);
                }

                printf("sent to udp %x %d\n", *(unsigned int*) obuf+30, 
                        rcount);

                if (idx++ == -1) {
                    break;
                }
            }
        }
    }
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
    char dest[] = { 172, 31, 0, 2 };

    while (1) {

        if ((rcount = recvfrom(sock, buf, MTU, 0, (struct sockaddr*) &addr, 
                               &addrlen)) < 0) {
            fprintf(stderr, "upd recv failed\n");
            pthread_exit(NULL);
        }

        printf("recv from udp %d\n", rcount);
        printf("from %s\n", buf); 

        if (get_source_addr(buf, source) < 0) {
            fprintf(stderr, "ip not found\n");
            continue;
        }

        if (translate_headers(obuf, source, dest, opts->mac, 
            rcount - BUF_OFFSET) < 0) {
            fprintf(stderr, "translate error\n");
            pthread_exit(NULL);
        }

        if (write(tap, obuf, rcount) < 0) {
            fprintf(stderr, "write to tap error\n");
            close(tap);
            pthread_exit(NULL);
        }
        printf("wrote to tap %d\n", rcount);
    }
    pthread_exit(NULL);
}

int
main(int argc, char *argv[])
{
    thread_opts_t opts;
    opts.id = argv[2];
    opts.sock = create_udp_socket(atoi(argv[1]));
    opts.tap = open_tap("svpn0", "172.31.0.2", opts.mac, MTU);

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

    char buf[16] = { '0' };
    char *line = buf;
    size_t len = sizeof(buf);

    char id[12] = { '\0' };
    char local_ip[16] = { '\0' };
    char dest_ip[16] = { '\0' };
    unsigned short port;
    ssize_t ret;

    while (1) {
        ret = getdelim(&line, &len, ' ',stdin);
        strncpy(id, line, ret-1);
        ret = getdelim(&line, &len, ' ',stdin);
        strncpy(local_ip, line, ret-1);
        ret = getdelim(&line, &len, ' ',stdin);
        strncpy(dest_ip, line,  ret-1);
        ret = getline(&line, &len,stdin);
        port = atoi(line);

        add_route(id, local_ip, dest_ip, port);
    }
}

