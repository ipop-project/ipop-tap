
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#define MAXBUF 1024

static int
udp_reflect(uint16_t port)
{
    int sock, optval, n;
    struct sockaddr_in s_addr, c_addr;
    socklen_t len;
    char buf[MAXBUF];

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 1) {
        fprintf(stderr, "socket failed\n");
        return -1; 
    }

    optval = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    len = sizeof(s_addr);
    memset(&s_addr, 0, len);
    s_addr.sin_family = AF_INET;
    s_addr.sin_port = htons(port);
    s_addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(sock, (struct sockaddr*) &s_addr, len) < 0) {
        fprintf(stderr, "bind failed\n");
        close(sock);
        return -1;
    }

    while (1) {
        len = sizeof(c_addr);
        n = recvfrom(sock, buf, MAXBUF, 0, (struct sockaddr*) &c_addr, &len);
        if (n < 0) {
            fprintf(stderr, "read failed\n");
            return -1;
        }
        n = sprintf(buf, "%s:%d", inet_ntoa(c_addr.sin_addr), 
                    ntohs(c_addr.sin_port));
        if (n < 0) {
            fprintf(stderr, "sprintf error\n");
        }
        sendto(sock, buf, n, 0, (struct sockaddr*) &c_addr, len);
        fprintf(stdout, "%s\n", buf);
    }
}

int main(int argc, char *argv[])
{
    uint16_t port = atoi(argv[1]);
    udp_reflect(port);
    return 0;
}

