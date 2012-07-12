
#include <svpn.h>

#ifdef USE_IPV6_IPSEC
#ifndef _ANIMAL_CONTROL_H_
#define _ANIMAL_CONTROL_H_

#include <stdlib.h>
const char *animal_control_error;

int animal_control_init(const char* ipv6_addr_p, int prefix,
                        const char *local_privkey_path);
int animal_control_add_peer(const char *addr_p, const char *pubkeyfile_path);
int animal_control_remove_peer(const char *addr_p);
int animal_control_alive();
int animal_control_exit();

#endif // _ANIMAL_CONTROL_H_
#endif // USE_IPV6_IPSEC
