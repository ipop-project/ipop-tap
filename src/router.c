
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

void
clear_table(void)
{
    int i;
    for (i = 0; i < TABLE_SIZE; i++) {
        table[i].id[0] = 0;
    }
}

int
add_route(const char *id, const char *local_ip, const char *dest_ip, 
    const uint16_t port)
{
    printf("adding %s %s %s %d\n", id, local_ip, dest_ip, port);

    int i;
    for (i = 0; i < TABLE_SIZE; i++) {

        if (table[i].id[0] != 0) {
            if (strncmp(id, table[i].id, ID_SIZE) != 0) {
              continue;
            }
        }

        strncpy(table[i].id, id, ID_SIZE);
        table[i].local_ip = inet_addr(local_ip);
        table[i].dest_ip = inet_addr(dest_ip);
        table[i].port = port;
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

        // this is inefficient, need to figure out byte order
        char *tmp_ip = (char *) &table[i].local_ip;

        if (tmp_ip[3] == local_ip[3] && tmp_ip[2] == local_ip[2] && 
            tmp_ip[1] == local_ip[1] && tmp_ip[0] == local_ip[0]) {

            memset(addr, 0, sizeof(struct sockaddr_in));
            addr->sin_family = AF_INET;
            addr->sin_port = htons(table[i].port);
            addr->sin_addr.s_addr = table[i].dest_ip;

            if (flags == 1) {
                memcpy(dest_id, table[i].id, ID_SIZE);
            }

            unsigned char *tmp = (unsigned char *)(&addr->sin_addr.s_addr);
            printf("get dest %x %x %x %x\n", tmp[0], tmp[1], tmp[2], tmp[3]);

            *idx = -1;
            return 0;
        }
    }

    return -1;
}

int
get_source_addr(const char *id, char *source)
{
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

