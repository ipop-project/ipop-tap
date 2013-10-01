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

#ifndef _PEERLIST_H_
#define _PEERLIST_H_

#include <arpa/inet.h>

// normally these values are defined in ipop_tap.h:
#define ID_SIZE 20
#define ADDR_SIZE 32

#ifdef __cplusplus
extern "C" {
#endif

struct peer_state {
    char id[ID_SIZE]; // 160bit unique identifier
    struct in_addr local_ipv4_addr; // the virtual IPv4 address that we see
    struct in6_addr local_ipv6_addr; // the virtual IPv6 address that we see
    struct in_addr dest_ipv4_addr;  // the actual address to send data to
    uint16_t port; // The open port on the client that we're connected to
};

extern struct peer_state peerlist_local; // used to publicly expose the local
                                         // peer info

int peerlist_init(size_t _table_length);
int peerlist_set_local(const char *_local_id,
                       const struct in_addr *_local_ipv4_addr,
                       const struct in6_addr *_local_ipv6_addr);
int peerlist_set_local_p(const char *_local_id, const char *_local_ipv4_addr_p,
                         const char *_local_ipv6_addr_p);
int peerlist_add(const char *id, const struct in_addr *dest_ipv4,
                 const struct in6_addr *dest_ipv6, const uint16_t port);
int peerlist_add_p(const char *id, const char *dest_ipv4, const char *dest_ipv6,
                   const uint16_t port);
int peerlist_get_by_id(const char *id, struct peer_state **peer);
int peerlist_get_by_local_ipv4_addr(const struct in_addr *_local_ipv4_addr,
                                    struct peer_state **peer);
int peerlist_get_by_local_ipv4_addr_p(const char *_local_ipv4_addr,
                                      struct peer_state **peer);
int peerlist_get_by_local_ipv6_addr(const struct in6_addr *_local_ipv6_addr,
                                    struct peer_state **peer);
int peerlist_get_by_local_ipv6_addr_p(const char *_local_ipv6_addr,
                                      struct peer_state **peer);

int override_base_ipv4_addr_p(const char *ipv4);
#ifdef __cplusplus
}
#endif

#endif
