
#ifndef _TAP_H_
#define _TAP_H_

int open_tap(char *dev, char *mac);

int configure_tap(int fd, char *ip, int mtu);

#endif

