
#ifndef _ROUTER_H_
#define _ROUTER_H_

#include <arpa/inet.h>

void set_local_ip(const char *local_ip);

int add_route(const char *id, const char *dest_ip, const uint16_t port);

int get_dest_addr(struct sockaddr_in *addr, const char *local_ip,
    int *idx, const int flags, char *dest_id);

int get_source_addr(const char *id, const char *dest_ip, char *source, 
    char *dest);

#endif
