/*
 * ipop-tap
 * Copyright 2013, University of Florida
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/**
 * This file uses a lot of ideas from miredo/libtun6/tun6.c, so credit where
 * credit is due.
 */

#if defined(LINUX) || defined(ANDROID)
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <net/route.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "tap.h"

#if defined(LINUX)
struct in6_ifreq {
    struct in6_addr ifr6_addr;
    uint32_t ifr6_prefixlen;
    int ifr6_ifindex;
};
#endif

static struct ifreq ifr;

static int tap_set_flags(short enable, short disable);
static int tap_set_proc_option(const sa_family_t family, const char *option,
                               const char *value);
static void tap_plen_to_ipv4_mask(unsigned int prefix_len,
                                  struct sockaddr* writeback);
// We must "waste" a couple sockets in order to set all the options we want:
static int ipv4_configuration_socket = -1;
static int ipv6_configuration_socket = -1;
// the ifreq structure stores arguments for ioctl calls, we'll just make one in
// open_tap and use it multiple times throughout (see also: 'man netdevice')
static int fd = -1; // The file descriptor used by the current TAP device

// define the path of the tun device (platform specific)
#if defined(ANDROID)
#define TUN_PATH "/dev/tun"
#elif defined(LINUX)
#define TUN_PATH "/dev/net/tun"
#endif

/**
 * Opens a tap device and configures it. `device` is the name that should be
 * given to the device. When we get the MAC address from the OS for the device,
 * we write it back to the array at `mac` (6 bytes are consumed).
 *
 * Returns the file descriptor (>=0) on success, and -1 on failure.
 */
int
tap_open(const char *device, char *mac)
{
    if ((fd = open(TUN_PATH, O_RDWR)) < 0) {
        fprintf(stderr, "Opening %s failed. (Are we not root?)\n", TUN_PATH);
        tap_close(); return -1;
    }

    ifr.ifr_flags = IFF_TAP | IFF_NO_PI; // TAP device, No packet information

    if (strlen(device) >= IFNAMSIZ) {
        fprintf(stderr,
                "Device name '%s' is longer than IFNAMSIZ-1 (%d bytes).\n",
                device, IFNAMSIZ-1);
        tap_close(); return -1;
    }
    strcpy(ifr.ifr_name, device);

    // Tell the system that device is the name of the tunnel interface (creating
    // it in the process)
    if (ioctl(fd, TUNSETIFF, (void *) &ifr) < 0) {
        fprintf(stderr, "Could not set tunnel interface as %s. (Are we not "
                        "root?)\n", device);
        tap_close(); return -1;
    }

    // Create the "throw-away" socket that we'll use to configure the device
    if ((ipv4_configuration_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        fprintf(stderr, "UDP IPv4 socket construction failed.\n");
        tap_close(); return -1;
    }
    if ((ipv6_configuration_socket = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
        fprintf(stderr, "UDP IPv6 socket construction failed.\n");
        tap_close(); return -1;
    }

    // get the hardware/MAC address (gets written back to ifr.ifr_hwaddr)
    if (ioctl(ipv6_configuration_socket, SIOCGIFHWADDR, &ifr) < 0) {
        fprintf(stderr, "Could not read device MAC address.\n");
        tap_close(); return -1;
    }

    memcpy(mac, ifr.ifr_hwaddr.sa_data, 6); // ifr_hwaddr is a sockaddr struct
    return fd;
}

/**
 * Given some flags to enable and disable, reads the current flags for the
 * network device, and then ensures the high bits in enable are also high in
 * ifr_flags, and the high bits in disable are low in ifr_flags. The results are
 * then written back. For a list of valid flags, read the "SIOCGIFFLAGS,
 * SIOCSIFFLAGS" section of the 'man netdevice' page. You can pass `(short)0` if
 * you don't want to enable or disable any flags.
 */
static int
tap_set_flags(short enable, short disable)
{
    // read the current flag states
    if (ioctl(ipv6_configuration_socket, SIOCGIFFLAGS, &ifr) < 0) {
        fprintf(stderr, "Could not read device flags for TAP device. (Device "
                        "not open?)\n");
        tap_close(); return -1;
    }
    // set or unset the right flags
    ifr.ifr_flags |= enable; ifr.ifr_flags &= ~disable;
    // write back the modified flag states
    if (ioctl(ipv6_configuration_socket, SIOCSIFFLAGS, &ifr) < 0) {
        fprintf(stderr, "Could not write back device flags for TAP device. "
                        "(Are we not root?)\n");
        tap_close(); return -1;
    }
    return 0;
}

/**
 * Sets and unsets some common flags used on a TAP device, namely, it sets the
 * IFF_NOARP flag, and unsets IFF_MULTICAST and IFF_BROADCAST. Notably, if
 * IFF_NOARP is not set, when using an IPv6 TAP, applications will have trouble
 * routing their data through the TAP device (Because they'd expect an ARP
 * response, which we aren't really willing to provide).
 */
int
tap_set_base_flags()
{
    //return tap_set_flags(IFF_NOARP, IFF_MULTICAST | IFF_BROADCAST);
    return tap_set_flags(IFF_NOARP, (short)0);
}

int
tap_unset_noarp_flags()
{
    return tap_set_flags((short)0, (short) IFF_NOARP);
}

/**
 * Configures the tap network device to be marked as "UP".
 */
int
tap_set_up()
{
    return tap_set_flags(IFF_UP | IFF_RUNNING, (short)0);
}

/**
 * Configures the tap network device to be marked as "DOWN".
 */
int
tap_down_down()
{
    return tap_set_flags((short)0, IFF_UP | IFF_RUNNING);
}

/**
 * Sets the maximum supported packet size for a device. IPv6 requires a minimum
 * MTU of 1280, so keep that in mind if you plan to use IPv6. Additionally,
 * real-world ethernet connections typically have an MTU of 1500, so it might
 * not make sense to make the MTU above that if you are sending data from the
 * TAP device over ethernet, because then packet fragmentation would be
 * required, degrading performance.
 */
int
tap_set_mtu(int mtu)
{
    ifr.ifr_mtu = mtu;
    if (ioctl(ipv6_configuration_socket, SIOCSIFMTU, &ifr) < 0) {
        fprintf(stderr, "Set MTU failed\n");
        tap_close(); return -1;
    }
    return 0;
}

/**
 * Given an IPv6-like prefix length, convert it to an IPv4 hostmask. This lets
 * us use prefix lengths *everywhere* which IMO is cleaner, and provides a more
 * consistent API.
 */
static void
tap_plen_to_ipv4_mask(unsigned int prefix_len, struct sockaddr *writeback)
{
    uint32_t net_mask_int = ~(0u) << (32-prefix_len);
    // I haven't tested this on a big endian system, but I believe this is an
    // endian-related issue. If this suddenly fails on Android, this line might
    // be why:
    net_mask_int = htonl(net_mask_int); // host format to network byte order
    struct sockaddr_in net_mask = {
        .sin_family = AF_INET,
        .sin_port = 0
    };
    struct in_addr net_mask_addr = {.s_addr = net_mask_int};
    net_mask.sin_addr = net_mask_addr;
    // we have to wrap our sockaddr_in struct into a sockaddr struct
    memcpy(writeback, &net_mask, sizeof(struct sockaddr));
}

/**
 * Sets the IPv4 address for the device, given a string, such as
 * "172.31.0.100". The `prefix_len` specifies how many bits are specifying the
 * subnet, aka the routing prefix or the mask length.
 */
int
tap_set_ipv4_addr(const char *presentation, unsigned int prefix_len, char * my_ip4)
{
    struct sockaddr_in socket_address = {
        .sin_family = AF_INET,
        .sin_port = 0
    }; // sin_addr will get set next:

    // convert the presentation ip address string to a binary format (p-to-n)
    // thus filling in socket_address.sin_addr
    if (inet_pton(AF_INET, presentation, &socket_address.sin_addr) != 1) {
        fprintf(stderr, "inet_pton failed (Bad IPv4 address format?)\n");
        tap_close();
        return -1;
    }
    // we have to wrap our sockaddr_in struct into a sockaddr struct
    memcpy(&ifr.ifr_addr, &socket_address, sizeof(struct sockaddr));

    // Copies IPv4 address to my_ip4. IPv4 address starts at sa_data[2] and
    // terminates at sa_data[5]
    memcpy(my_ip4, ifr.ifr_addr.sa_data+2,4); 

    if (ioctl(ipv4_configuration_socket, SIOCSIFADDR, &ifr) < 0) {
        fprintf(stderr, "Failed to set IPv4 tap device address\n");
        tap_close();
        return -1;
    }

    tap_plen_to_ipv4_mask(prefix_len, &ifr.ifr_netmask);
    if (ioctl(ipv4_configuration_socket, SIOCSIFNETMASK, &ifr) < 0) {
        fprintf(stderr, "Failed to set IPv4 tap device netmask\n");
        tap_close();
        return -1;
    }
    return 0;
}

/**
 * Sets the IPv6 address for the device, given a string, such as
 * "fd50:0dbc:41f2:4a3c:0:0:0:1000". The `prefix_len` specifies how many bits
 * are specifying the subnet, aka the routing prefix (refered to as the mask in
 * IPv4).
 */
int
tap_set_ipv6_addr(const char *presentation, unsigned int prefix_len)
{
    struct sockaddr_in6 socket_address = {
        .sin6_family = AF_INET6,
        .sin6_scope_id = 0,
        .sin6_flowinfo = 0,
        .sin6_port = 0
    }; // sin6_addr will get set next:

    // convert the presentation ip address string to a binary format (p-to-n)
    // thus filling in socket_address.sin6_addr
    if (inet_pton(AF_INET6, presentation,
                  socket_address.sin6_addr.s6_addr) != 1) {
        fprintf(stderr, "inet_pton failed (Bad IPv6 address format?)\n");
        tap_close();
        return -1;
    }

    // for IPv4, we can simply set the ifreq structure's ifr_addr. With IPv6,
    // the ifreq struct is too small. We need to convert things to an in6_ifreq.
    struct in6_ifreq ifr6;
    memset(&ifr6, 0, sizeof(struct in6_ifreq));
    ifr6.ifr6_addr = socket_address.sin6_addr;
    ifr6.ifr6_ifindex = if_nametoindex(ifr.ifr_name);
    ifr6.ifr6_prefixlen = prefix_len;

    if (ioctl(ipv6_configuration_socket, SIOCSIFADDR, &ifr6) < 0) {
        fprintf(stderr, "Failed to set IPv6 tap device address\n");
        tap_close();
        return -1;
    }
    return 0;
}

/**
 * Tells the OS to route IPv4 addresses within the subnet (determined by the
 * `presentation` and `prefix_len` args, see tap_set_ipv4_addr) through us. A
 * priority is given by metric. The Linux kernel's default metric value is 256
 * for subnets and 1024 for gateways.
 */
int
tap_set_ipv4_route(const char *presentation, unsigned short prefix_len,
                   unsigned int metric)
{
    struct rtentry rte = {
        .rt_flags = RTF_UP,
        .rt_dev = ifr.ifr_name,
        .rt_metric = metric
    };
    tap_plen_to_ipv4_mask(prefix_len, &rte.rt_genmask);

    if (inet_pton(AF_INET, presentation, &rte.rt_dst) != 1) {
        fprintf(stderr, "inet_pton failed (Bad IPv4 address format?)\n");
        tap_close();
        return -1;
    }

    // when the mask is the whole address
    if (prefix_len == 32) rte.rt_flags |= RTF_HOST;

    if (ioctl(ipv4_configuration_socket, SIOCADDRT, &rte) < 0) {
        fprintf(stderr, "Could not write back route.\n");
        tap_close();
        return -1;
    }
    return 0;
}

/**
 * Tells the OS to route IPv6 addresses within the subnet (determined by the
 * `presentation` and `prefix_len` args, see tap_set_ipv6_addr) through us. A
 * priority is given by metric. The Linux kernel's default metric value is 256
 * for subnets and 1024 for gateways.
 */
int
tap_set_ipv6_route(const char *presentation, unsigned short prefix_len,
                   unsigned int metric)
{
#if !defined(ANDROID)
    struct in6_rtmsg rtm6 = {
        .rtmsg_flags = RTF_UP,
        .rtmsg_ifindex = if_nametoindex(ifr.ifr_name),
        .rtmsg_dst_len = prefix_len,
        .rtmsg_metric = metric
    };

    if (inet_pton(AF_INET6, presentation, &rtm6.rtmsg_dst) != 1) {
        fprintf(stderr, "inet_pton failed (Bad IPv6 address format?)\n");
        tap_close();
        return -1;
    }

    // when the mask is the whole address
    if (prefix_len == 128) rtm6.rtmsg_flags |= RTF_HOST;

    if (ioctl(ipv6_configuration_socket, SIOCADDRT, &rtm6) < 0) {
        fprintf(stderr, "Could not write back route.\n");
        tap_close();
        return -1;
    }
#endif
    return 0;
}

/**
 * Normally an IPv6 enabled system will try to set up a device with IPv6
 * Stateless autoconfiguration. We're a simple TAP device, and we don't need or
 * want autoconfiguration, so this will disable it for us. IPv4 has no parallel
 * to this, so you don't need to worry about this on IPv4.
 */
int
tap_disable_ipv6_autoconfig()
{
    // Disable router solicitation, as it isn't needed
    if (tap_set_ipv6_proc_option("accept_redirects", "0") < 0) {
        tap_close(); return -1;
    }
    if (tap_set_ipv6_proc_option("accept_ra", "0") < 0) {
        tap_close(); return -1;
    }
    // Disable autoconfiguration in general
    if (tap_set_ipv6_proc_option("autoconf", "0") < 0) {
        tap_close(); return -1;
    }
    return 0;
}

/**
 * Sets an option by writing a value to
 * /proc/sys/net/ipv6/conf/DEVICE_NAME/OPTION, appended with a newline. This is
 * handy for example, to disable those annoying "Router Solicitation" packets.
 * A good list of possible options with their descriptions can be found here:
 * http://tldp.org/HOWTO/Linux+IPv6-HOWTO/proc-sys-net-ipv6..html
 */
int
tap_set_ipv6_proc_option(const char *option, const char *value)
{
    return tap_set_proc_option(AF_INET6, option, value);
}

/**
 * Sets an option by writing a value to
 * /proc/sys/net/ipv4/conf/DEVICE_NAME/OPTION, appended with a newline.
 */
int
tap_set_ipv4_proc_option(const char *option, const char *value)
{
    return tap_set_proc_option(AF_INET, option, value);
}

static int
tap_set_proc_option(const sa_family_t family, const char *option,
                    const char *value)
{
    char path[26 + strlen(ifr.ifr_name) + strlen(option)];
    // path is a C99 variable-length array
    sprintf(path, "/proc/sys/net/ipv%s/conf/%s/%s",
            family==AF_INET?"4":"6", ifr.ifr_name, option);
    FILE *proc_fd;
    if ((proc_fd = fopen(path, "w")) == NULL) {
        fprintf(stderr, "Could not open %s. (Not root or bad path?)\n", path);
        return -1;
    }
    if (fprintf(proc_fd, "%s\n", value) < 0) {
        fprintf(stderr, "Writeback to %s failed.\n", path);
        return -1;
    }
    fclose(proc_fd);
    return 0;
}

/**
 * Closes all the sockets associated with the TAP device, cleaning things up.
 */
void
tap_close()
{
    if (fd >= 0)
        close(fd);
    if (ipv4_configuration_socket >= 0)
        close(ipv4_configuration_socket);
    if (ipv6_configuration_socket >= 0)
        close(ipv6_configuration_socket);
}
#undef TUN_PATH
#endif
