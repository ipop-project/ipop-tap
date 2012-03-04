
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <tap.h>

static int sock = -1;
static struct ifreq ifr;

int
open_tap(char *dev, char *mac)
{
    int fd;

#if DROID_BUILD
    if ((fd = open("/dev/tun", O_RDWR)) < 0) {
#else
    if ((fd = open("/dev/net/tun", O_RDWR)) < 0) {
#endif
        fprintf(stderr, "open failed fd = %d\n", fd);
        return -1;   
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

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        fprintf(stderr, "udp socket failed\n");
        close(fd);
        return -1;
    }

    strcpy(ifr.ifr_name, dev);
    if (ioctl(sock, SIOCGIFHWADDR, &ifr) < 0) {
       fprintf(stderr, "get mac failed\n");
        close(fd);
        close(sock);
        return -1;
    }

    memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
    return fd;
}

int
configure_tap(int fd, char *ip, int mtu)
{
    struct sockaddr_in addr;
    in_addr_t local_ip;

    if (inet_aton(ip, (struct in_addr *) &local_ip) == 0) {
        fprintf(stderr, "inet_aton failed\n");
        close(fd);
        close(sock);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_addr.s_addr = local_ip;
    addr.sin_family = AF_INET;
    memcpy(&ifr.ifr_addr, &addr, sizeof(struct sockaddr));

    if (ioctl(sock, SIOCSIFADDR, &ifr) < 0) {
        fprintf(stderr, "setip failed\n");
        close(fd);
        close(sock);
        return -1;
    }

    ifr.ifr_mtu = mtu;
    if (ioctl(sock, SIOCSIFMTU, &ifr) < 0) {
        fprintf(stderr, "set mtu failed\n");
        close(fd);
        close(sock);
        return -1;
    }

    ifr.ifr_flags |= IFF_UP;
    ifr.ifr_flags |= IFF_RUNNING;

    if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) {
        fprintf(stderr, "set flags failed\n");
        close(fd);
        close(sock);
        return -1;
    }

    return 0;
}

