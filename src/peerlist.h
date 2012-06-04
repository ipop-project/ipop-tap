
#ifndef _PEERLIST_H_
#define _PEERLIST_H_

#include <arpa/inet.h>

// normally these values are defined in svpn.h:
#define ID_SIZE 20
#define ADDR_SIZE 32

struct peer_state {
    char id[ID_SIZE+1]; // Human readable string identifier
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

#endif
