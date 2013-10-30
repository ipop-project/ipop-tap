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

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#if defined(LINUX) || defined(ANDROID)
#include <sys/socket.h>
#include <net/if.h>
#include <arpa/inet.h>
#elif defined(WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "socket_utils.h"

/**
 * A convenience function for making an IPv4 UDP (DGRAM) socket. The socket
 * (>=0) is returned on success, -1 otherwise.
 */
int
socket_utils_create_ipv4_udp_socket(const char* ip, uint16_t port)
{
    int sock;
    char optval[4] = { 0 };
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 1) {
        fprintf(stderr, "socket failed\n");
        return -1;
    }

    optval[3] = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, optval, sizeof(optval));

    memset(&addr, 0, addr_len);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    //addr.sin_addr.s_addr = INADDR_ANY;

#if defined(LINUX) || defined(ANDROID)
    if (!inet_pton(AF_INET, ip, &addr.sin_addr.s_addr)) {
#elif defined(WIN32)
    CHAR* Term;
    LONG err = RtlIpv4StringToAddress(ip, TRUE, &Term,
                                      (IN_ADDR *) &addr.sin_addr.s_addr);
    if (err != NO_ERROR) {
#endif
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
 *     u_int32_t scope_id = if_nametoindex("ipop0");
 */
int
socket_utils_create_ipv6_udp_socket(const uint16_t port, uint32_t scope_id)
{
    int sock;
    char optval[4] = { 0 };
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

    optval[3] = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, optval, sizeof(optval));

    if (bind(sock, (struct sockaddr*) &addr, addr_len) < 0) {
        fprintf(stderr, "bind failed\n");
        close(sock);
        return -1;
    }
    return sock;
}
