
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

static int sock_ipv4 = -1;
static int sock_ipv6 = -1;
static struct ifreq ifr;

int
open_tap(char *dev, char *mac)
{
    // Opens a tap device on the system, and then returns the file descriptor
    // for it. The device name to be used is given by `dev`, and the mac address
    // for the generated device is written to `mac`.
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

    // create the relevant UDP-based IPv4 and IPv6 sockets
    if ((sock_ipv4 = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        fprintf(stderr, "UDP IPv4 socket construction failed\n");
        close(fd);
        return -1;
    }
    if ((sock_ipv6 = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
        fprintf(stderr, "UDP IPv6 socket construction failed\n");
        close(fd);
        close(sock_ipv4);
        return -1;
    }

    // as the HWaddr/MAC is for the whole device, we can pull it from either one
    // of our sockets
    strcpy(ifr.ifr_name, dev);
    if (ioctl(sock_ipv4, SIOCGIFHWADDR, &ifr) < 0) {
       fprintf(stderr, "get mac failed\n");
        close(fd);
        close(sock_ipv4);
        close(sock_ipv6);
        return -1;
    }

    memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
    return fd;
}

int
configure_tap(int fd, char *ipv4_addr, char *ipv6_addr, int mtu)
{
    // Takes a tap device as a file descriptor, and gives it designated IPv4 and
    // IPv6 addresses and MTU
    struct sockaddr_in sockaddr_ipv4;
    struct sockaddr_in6 sockaddr_ipv6;
    struct in_addr local_ipv4_addr;
    struct in6_addr local_ipv6_addr;

    // convert our passed in ipv4_addr and ipv6_addr strings to in_addr and
    // in6_addr structs
    if (inet_pton(AF_INET, ipv4_addr, &local_ipv4_addr) == 0) {
        fprintf(stderr, "inet_pton failed (Bad IPv4 Address?)\n");
        close(fd);
        close(sock_ipv4);
        close(sock_ipv6);
        return -1;
    }

    if (inet_pton(AF_INET6, ipv6_addr, &local_ipv6_addr) == 0) {
        fprintf(stderr, "inet_pton failed (Bad IPv6 Address?)\n");
        close(fd);
        close(sock_ipv4);
        close(sock_ipv6);
        return -1;
    }

    // Give the device an IPv4 address
    memset(&sockaddr_ipv4, 0, sizeof(sockaddr_ipv4));
    sockaddr_ipv4.sin_addr = local_ipv4_addr;
    sockaddr_ipv4.sin_family = AF_INET;
    memcpy(&ifr.ifr_addr, &sockaddr_ipv4, sizeof(struct sockaddr));
    
    if (ioctl(sock_ipv4, SIOCSIFADDR, &ifr) < 0) {
        fprintf(stderr, "Set address for IPv4 failed\n");
        close(fd);
        close(sock_ipv4);
        close(sock_ipv6);
        return -1;
    }
    
    // Give the device an IPv6 address
    /*
    memset(&sockaddr_ipv6, 0, sizeof(sockaddr_ipv6));
    sockaddr_ipv6.sin6_addr = local_ipv6_addr;
    sockaddr_ipv6.sin6_family = AF_INET6;
    sockaddr_ipv6.sin6_scope_id = 0;
    sockaddr_ipv6.sin6_flowinfo = 0;
    sockaddr_ipv6.sin6_port = 0;
    memcpy(&ifr.ifr_addr, &sockaddr_ipv6, sizeof(struct sockaddr));
    
    if (ioctl(sock_ipv6, SIOCSIFADDR, &ifr) < 0) {
        fprintf(stderr, "Set address for IPv6 failed\n");
        close(fd);
        close(sock_ipv4);
        close(sock_ipv6);
        return -1;
    }*/

    // MTU is per-device, so doing this once does it for IPv4 and 6
    ifr.ifr_mtu = mtu;
    if (ioctl(sock_ipv4, SIOCSIFMTU, &ifr) < 0) {
        fprintf(stderr, "Set MTU failed\n");
        close(fd);
        close(sock_ipv4);
        close(sock_ipv6);
        return -1;
    }

    // Put the device UP
    ifr.ifr_flags |= IFF_UP;
    ifr.ifr_flags |= IFF_RUNNING;

    if (ioctl(sock_ipv4, SIOCSIFFLAGS, &ifr) < 0) {
        fprintf(stderr, "Set flags for IPv4 failed\n");
        close(fd);
        close(sock_ipv4);
        close(sock_ipv6);
        return -1;
    }

    return 0;
}

