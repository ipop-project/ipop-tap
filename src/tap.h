
#ifndef _TAP_H_
#define _TAP_H_

int tap_open(const char *device, char *mac);
int tap_set_base_flags();
int tap_set_up();
int tap_set_down();
int tap_set_mtu(int mtu);
int tap_set_ipv4_addr(const char *presentation, unsigned int prefix_len);
int tap_set_ipv6_addr(const char *presentation, unsigned int prefix_len);
int tap_set_ipv4_route(const char *presentation, unsigned short prefix_len,
                       unsigned int metric);
int tap_set_ipv6_route(const char *presentation, unsigned short prefix_len,
                       unsigned int metric);
int tap_disable_ipv6_autoconfig();
int tap_set_ipv4_proc_option(const char *option, const char *value);
int tap_set_ipv6_proc_option(const char *option, const char *value);
void tap_close();

#endif
