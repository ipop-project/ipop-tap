
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/un.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <semaphore.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>

#undef max
#define max(x,y) ((x) > (y) ? (x) : (y))

#define MTU 1500
#define MAX_CON 10

static int
udp_listen(char *udp_addr, uint16_t port)
{
    int sock, optval;
    struct sockaddr_in addr;

    char buf[MTU];
    ssize_t rcount, wcount;

    sock = socket(PF_INET, SOCK_DGRAM, 0);
    if (sock < 1) {
        fprintf(stderr, "socket failed\n");
        return -1; 
    }

    optval = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    socklen_t addr_len = sizeof(addr);
    memset(&addr, 0, addr_len);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr*) &addr, addr_len) < 0) {
        fprintf(stderr, "bind failed\n");
        close(sock);
        return -1;
    }

    while (1) {
        rcount = recvfrom(sock, buf, MTU, 0, (struct sockaddr*) &addr, 
                          &addr_len);

        if (connect(sock, (struct sockaddr *)&addr, addr_len) < 0) {
            fprintf(stderr, "upd connect failed\n");
        }

    }
    return 0;
}

static int
bt_create_con(const char *bt_addr, const uint16_t psm)
{

    printf("address is %s\n", bt_addr);

    int sock;
    struct sockaddr_l2 addr;
    struct l2cap_options opts;
    socklen_t optlen;

    ssize_t rcount, scount;
    char buf[MTU];

    sock = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    if (sock < 0) {
        fprintf(stderr, "open l2cap failed\n");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.l2_family = AF_BLUETOOTH;
    addr.l2_bdaddr = *BDADDR_ANY;

    memset(&opts, 0, sizeof(opts));
    optlen = sizeof(opts);

    if (getsockopt(sock, SOL_L2CAP, L2CAP_OPTIONS, &opts, &optlen) < 0) {
        fprintf(stderr, "getsockopt l2cap failed\n");
        close(sock);
        return -1;
    }

    opts.imtu = opts.omtu = MTU;

    if (setsockopt(sock, SOL_L2CAP, L2CAP_OPTIONS, &opts, sizeof(opts)) < 0) {
        fprintf(stderr, "getsockopt l2cap failed\n");
        close(sock);
        return -1;
    }

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "bind send l2cap failed\n");
        close(sock);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.l2_family = AF_BLUETOOTH;
    str2ba(bt_addr, &addr.l2_bdaddr);
    addr.l2_psm = htobs(psm);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0 && errno != EINTR) {
        fprintf(stderr, "connect l2cap failed %s\n", strerror(errno));
        close(sock);
        return -1;
    }

    return sock;
}

static int
bt_listen(const char *bt_addr, const uint16_t psm) 
{
    int sock, cli_sock;
    struct sockaddr_l2 addr;
    struct l2cap_options opts;
    socklen_t optlen;

    char buf[MTU];
    ssize_t rcount, wcount;

    fd_set rdfs;
    int sock_fds[MAX_CON] = {-1};
    int nfds;

    sock = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    if (sock < 0) {
        fprintf(stderr, "open l2cap failed\n");
        close(sock);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.l2_family = AF_BLUETOOTH;
    addr.l2_bdaddr = *BDADDR_ANY;
    addr.l2_psm = htobs(psm);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "bind rcv l2cap failed %s\n", strerror(errno));
        close(sock);
        return -1;
    }

    memset(&opts, 0, sizeof(opts));
    optlen = sizeof(opts);

    if (getsockopt(sock, SOL_L2CAP, L2CAP_OPTIONS, &opts, &optlen) < 0) {
        fprintf(stderr, "getsockopt l2cap failed\n");
        close(sock);
        return -1;
    }

    opts.imtu = opts.omtu = MTU;

    if (setsockopt(sock, SOL_L2CAP, L2CAP_OPTIONS, &opts, sizeof(opts)) < 0) {
        fprintf(stderr, "getsockopt l2cap failed\n");
        close(sock);
        return -1;
    }

    if (listen(sock, MAX_CON) < 0) {
        fprintf(stderr, "listen l2cap failed\n");
        return -1;
    }

    FD_ZERO(&rdfs);
    FD_SET(sock, &rdfs);
    nfds = sock;

    while (1) {
        fprintf(stderr, "waiting on select ...\n");
        if (select(nfds + 1, &rdfs, NULL, NULL, NULL) < 0) {
            fprintf(stderr, "select error\n");
            break;
        }

        if (FD_ISSET(sock, &rdfs)) {
            memset(&addr, 0, sizeof(addr));
            cli_sock = accept(sock, (struct sockaddr *)&addr, &optlen);
            if (cli_sock < 0) {
                fprintf(stderr, "accept l2cap failed\n");
                break;
            }

            int i;
            for (i = 0; i < MAX_CON; i++) {
                if (sock_fds[i] == -1) {
                    sock_fds[i] = cli_sock;
                    FD_SET(cli_sock, &rdfs);
                    nfds = max(nfds, cli_sock);
                    break;
                }
            }
        }

        int i;
        for (i = 0; i < MAX_CON; i++) {
            if (sock_fds[i] != -1 && FD_ISSET(sock_fds[i], &rdfs)) {
                rcount = read(sock_fds[i], buf, sizeof(buf));
                if (rcount > 0) {
                    printf("rcv packet of size %d\n", rcount);
                }
                else {
                    fprintf(stderr, "error on fd\n");
                    close(sock_fds[i]);
                    sock_fds[i] = -1;
                }
            }
        }
    }

    close(sock);
    return 0;
}

int main() 
{}

