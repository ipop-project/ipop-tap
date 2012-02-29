
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <router.h>

#define TABLE_SIZE 10
#define ID_SIZE 12

typedef struct peers_state {
    char id[ID_SIZE];
    in_addr_t local_ip;
    in_addr_t dest_ip;
    uint16_t port;
} peer_state_t;

static peer_state_t table[TABLE_SIZE];
in_addr_t _base_ip;
in_addr_t _local_ip;

static void
clear_table(void)
{
    int i;
    for (i = 0; i < TABLE_SIZE; i++) {
        table[i].id[0] = 0;
    }
}

void set_local_ip(const char *local_ip)
{
    _local_ip = inet_addr(local_ip);
    _base_ip = _local_ip;
    unsigned char *ip = (unsigned char *)&_base_ip;
    ip[3] = 100;
    clear_table();
}

int
add_route(const char *id, const char *dest_ip, const uint16_t port)
{

    int i;
    for (i = 0; i < TABLE_SIZE; i++) {

        if (table[i].id[0] != 0) {
            if (strncmp(id, table[i].id, ID_SIZE) == 0) {
                table[i].dest_ip = inet_addr(dest_ip);
                table[i].port = port;
                return 0;
            }
            else {
                continue;
            }
        }

        printf("adding %s %x %s %d\n", id, _base_ip, dest_ip, port);

        strncpy(table[i].id, id, ID_SIZE);
        table[i].local_ip = _base_ip;
        table[i].dest_ip = inet_addr(dest_ip);
        table[i].port = port;
        unsigned char *ip = (unsigned char *)&_base_ip;
        ip[3] += 1;
        return 0;
    }
    return -1;
}

int
get_dest_addr(struct sockaddr_in *addr, const char *local_ip, int *idx, 
    const int flags, char *dest_id)
{
    if ((unsigned char) local_ip[0] >= 224 &&
        (unsigned char) local_ip[0] <= 239) {

        if (*idx >= 0 && *idx < TABLE_SIZE) {
            if (table[*idx].id[0] == 0) {
                *idx = -1;
                return -1;
            }

            memset(addr, 0, sizeof(struct sockaddr_in));
            addr->sin_family = AF_INET;
            addr->sin_port = htons(table[*idx].port);
            addr->sin_addr.s_addr = table[*idx].dest_ip;

            if (flags == 1) {
                memcpy(dest_id, table[*idx].id, ID_SIZE);
            }

            return 0;
        }
    }

    int i;
    for (i = 0; i < TABLE_SIZE; i++) {

        if (table[i].id[0] == 0) {
            continue;
        }

        if (memcmp(&table[i].local_ip, local_ip, 4) == 0) {

            memset(addr, 0, sizeof(struct sockaddr_in));
            addr->sin_family = AF_INET;
            addr->sin_port = htons(table[i].port);
            addr->sin_addr.s_addr = table[i].dest_ip;

            if (flags == 1) {
                memcpy(dest_id, table[i].id, ID_SIZE);
            }

            *idx = -1;
            return 0;
        }
    }

    return -1;
}

int
get_source_addr(const char *id, const char *dest_ip, char *source, char *dest)
{
    if (dest != NULL) {
        memcpy(dest, &_local_ip, 4);
    }

    if ((unsigned char) dest_ip[0] >= 224 &&
        (unsigned char) dest_ip[0] <= 239) {
        return 0;
    }

    int i;
    for (i = 0; i < TABLE_SIZE; i++) {

        if (table[i].id[0] == 0) {
            continue;
        }

        if (strncmp(id, table[i].id, ID_SIZE) == 0) {
            memcpy(source, &table[i].local_ip, 4);
            return 0;
        }
    }

    return -1;
}

