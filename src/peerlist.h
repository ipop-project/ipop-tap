
#ifndef _PEERLIST_H_
#define _PEERLIST_H_

#include <arpa/inet.h>

void set_local_peer(const char *local_id, const char *local_ip);

int add_peer(const char *id, const char *dest_ipv4, const char *dest_ipv6,
    const uint16_t port, const char *key, const char *p2p_addr);

int get_dest_info(const char *local_ip, char *source_id, char *dest_id,
    struct sockaddr_in *addr, char *key, char *p2p_addr, int *idx);

int get_source_info(const char *id, char *source, char *dest, char *key);

int get_source_info_by_addr(const char *p2p_addr, char *source, char *dest);

#endif
