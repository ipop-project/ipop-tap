
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <stdint.h>
#include <net/if.h>
#include <arpa/inet.h>

#include "socket_utils.h"

/**
 * A convenience function for making an IPv4 UDP (DGRAM) socket. The socket
 * (>=0) is returned on success, -1 otherwise.
 */
int
socket_utils_create_ipv4_udp_socket(const char* ip, uint16_t port)
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
    //addr.sin_addr.s_addr = INADDR_ANY;

    if (!inet_pton(AF_INET, ip, &addr.sin_addr.s_addr)) {
        fprintf(stderr, "Bad IPv4 address format: %s\n", ip);
        return -1;
    }

    if (bind(sock, (struct sockaddr*) &addr, addr_len) < 0) {
        fprintf(stderr, "bind failed\n");
        close(sock);
        return -1;
    }
    return sock;
}

/**
 * A convenience function for making an IPv6 UDP (DGRAM) socket. The socket
 * (>=0) is returned on success, -1 otherwise. `scope_id` is the id of the
 * hardware device to explicitly bind to. This can be found by running
 * `if_nametoindex` on the device name, for example:
 *
 *     u_int32_t scope_id = if_nametoindex("svpn0");
 */
int
socket_utils_create_ipv6_udp_socket(const uint16_t port, u_int32_t scope_id)
{
    int sock, optval = 1;
    struct sockaddr_in6 addr = {
        .sin6_family = AF_INET6,
        .sin6_port = htons(port),
        .sin6_flowinfo = 0,
        .sin6_scope_id = scope_id,
        .sin6_addr = in6addr_any
    };
    socklen_t addr_len = sizeof(addr);

    if ((sock = socket(AF_INET6, SOCK_DGRAM, 0)) < 1) {
        fprintf(stderr, "socket failed\n");
        return -1;
    }

    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    if (bind(sock, (struct sockaddr*) &addr, addr_len) < 0) {
        fprintf(stderr, "bind failed\n");
        close(sock);
        return -1;
    }
    return sock;
}
