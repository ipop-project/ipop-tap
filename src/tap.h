
#ifndef _TAP_H_
#define _TAP_H_

int open_tap(char *dev, char *mac);

int configure_tap(int fd, char *ipv4_addr, char *ipv6_addr, int mtu);

void cleanup_tap();

#endif
