
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

enum conn_types {
    BT_CON,
    UDP_CON
};

static int udp_sock = -1;

static int sfds[MAX_CON] = {-1};

static char bt_addr[18] = {'\0'};

static sem_t mutex;

static int bt_create_con(const char *bt_addr, const uint16_t psm);

static int udp_send(const char *data, ssize_t len);

static int
add_conn(struct sockaddr* addr, int sockfd, enum conn_types type)
{
    sem_wait(&mutex);

    //TODO -- Implement properly
    sfds[0] = sfds[0] == -1 ? sockfd : -1;
    
    sem_post(&mutex);

    fprintf(stderr, "conn added fd %d\n", sockfd);

    udp_send("12", 2);
    return 0;
} 

static int
get_conn(char ip[])
{
    //TODO - Implement properly
    return sfds[0];
}

static int
open_tap(char *dev)
{
    int fd;
    struct ifreq ifr;

    fd = open("/dev/net/tun", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "open failed fd = %d\n", fd);
        return fd;   
    }

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;

    if (*dev) {
        strncpy(ifr.ifr_name, dev, IFNAMSIZ);
    }

    if (ioctl(fd, TUNSETIFF, (void *) &ifr) < 0) {
        fprintf(stderr, "TUNSETIFF failed\n");
        close(fd);
        return -1;
    }
    return fd;
}

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

    udp_sock = sock;

    while (1) {
        rcount = recvfrom(sock, buf, MTU, 0, (struct sockaddr*) &addr, 
                          &addr_len);

        if (connect(sock, (struct sockaddr *)&addr, addr_len) < 0) {
            fprintf(stderr, "upd connect failed\n");
        }

        if (rcount == 17) {
            strncpy(bt_addr, buf, 17);
            bt_create_con(bt_addr, 4097);
        }
        else if ( rcount > 18) {
            svpn_send(buf, rcount);
        }
        else if (rcount < 0) {
            fprintf(stderr, "udp rcv failed\n");
        }
    }
    return 0;
}

static int
udp_send(const char *data, ssize_t len)
{
    send(udp_sock, data, len, 0);
}

static int
bt_create_con(const char *bt_addr, const uint16_t psm)
{

    printf("address is %s\n", bt_addr);

    if (get_conn(NULL) > 0) {
      fprintf(stderr, "connection already exists\n");
      return -1;
    }

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

    add_conn((struct sockaddr *)&addr, sock, BT_CON);
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
                    udp_send(buf, rcount);
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

int
svpn_send(const char buf[], ssize_t len)
{
    int sockfd = get_conn(NULL);
    if (sockfd > 2) {
        if (write(sockfd, buf, len) < 0) {
            fprintf(stderr, "error on send \n");
            return -1;
        }
        else {
            udp_send("ACK", 3);
            fprintf(stderr, "sent on fd %d\n", sockfd);
            return 0;
        }
    }
    return -1;
}

static void *
udp_thread_start(void *data)
{
    udp_listen(NULL, 50000);
    pthread_exit(NULL);
}

static void *
bt_thread_start(void *data)
{
    bt_listen(NULL, 4097);
    pthread_exit(NULL);
}


void
svpn_init(int opt)
{
    sem_init(&mutex, 0, 1);
    pthread_t bt_thread, udp_thread;
    pthread_create(&bt_thread, NULL, bt_thread_start, NULL);
    pthread_create(&udp_thread, NULL, udp_thread_start, NULL);
}

int
main(int argc, char *argv[]) {

    /*
    if (argc < 5) {
        fprintf(stderr, "usage: %s lport remoteip rport\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    */

    svpn_init(1);

    size_t len = 0;
    char *line = NULL;
    ssize_t rcount = 0;

    while (1) {
        if (bt_addr[0] != '\0' && get_conn(NULL) == -1) {
            bt_create_con(bt_addr, 4097);
        }
        sleep(5);
    }

    /*
    while (rcount = getline(&line, &len, stdin) != -1) {
        printf("len %d\n", strlen(line));
        if (strlen(line) == 18) {
            strncpy(bt_addr, line, 17);
            bt_create_con(bt_addr, 4097);
        }
        else {
            svpn_send(line, len);
            printf("sent %s\n", line);
        }
    }

    if (line) {
        free(line);
    }
    */

    return 0;
}

