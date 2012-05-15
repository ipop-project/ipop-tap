
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <peerlist.h>
#include <svpn.h>

typedef struct peers_state {
    char id[ID_SIZE];
    in_addr_t local_ip;
    in_addr_t dest_ip;
    uint16_t port;
    char key[KEY_SIZE];
    char p2p_addr[ADDR_SIZE];
} peer_state_t;

static peer_state_t table[TABLE_SIZE];
static in_addr_t _base_ip;
static in_addr_t _local_ip;
static char _local_id[ID_SIZE];

static void
clear_table(void)
{
    int i;
    for (i = 0; i < TABLE_SIZE; i++) {
        memset(table[i].id, 0, ID_SIZE);
        memset(table[i].key, 0, KEY_SIZE);
    }
}

void set_local_peer(const char *local_id, const char *local_ip)
{
    memset(_local_id, 0, ID_SIZE);
    strncpy(_local_id, local_id, ID_SIZE);
    _local_ip = inet_addr(local_ip);
    _base_ip = _local_ip;
    unsigned char *ip = (unsigned char *)&_base_ip;
    ip[3] = 101;
    clear_table();
}

int
add_peer(const char *id, const char *dest_ip, const uint16_t port, 
    const char *key, const char *p2p_addr)
{
    // TODO - this is a hack, hash func should be used here
    // this is done because we are takin string input from user
    char tmp_id[ID_SIZE] = { 0 };
    char tmp_key[KEY_SIZE] = { 0 };
    char tmp_addr[ADDR_SIZE] = { 0 };

    strncpy(tmp_id, id, ID_SIZE);
    strncpy(tmp_key, key, KEY_SIZE);
    strncpy(tmp_addr, p2p_addr, ADDR_SIZE);

    int i;
    for (i = 0; i < TABLE_SIZE; i++) {

        if (table[i].id[0] != 0) {
            if (memcmp(tmp_id, table[i].id, ID_SIZE) == 0) {
                memcpy(table[i].key, tmp_key, KEY_SIZE);
                memcpy(table[i].p2p_addr, tmp_addr, ADDR_SIZE);
                table[i].dest_ip = inet_addr(dest_ip);
                table[i].port = port;
                return 0;
            }
            else {
                continue;
            }
        }

        memcpy(table[i].id, tmp_id, ID_SIZE);
        memcpy(table[i].key, tmp_key, KEY_SIZE);
        memcpy(table[i].p2p_addr, tmp_addr, ADDR_SIZE);

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
get_dest_info(const char *local_ip, char *source_id, char *dest_id, 
    struct sockaddr_in *addr, char *key, char *p2p_addr, int *idx)
{
    memcpy(source_id, _local_id, ID_SIZE);

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

            memcpy(dest_id, table[*idx].id, ID_SIZE);
            memcpy(key, table[*idx].key, ID_SIZE);
            memcpy(p2p_addr, table[*idx].p2p_addr, ADDR_SIZE);
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

            memcpy(dest_id, table[i].id, ID_SIZE);
            memcpy(key, table[i].key, KEY_SIZE);
            memcpy(p2p_addr, table[i].p2p_addr, ADDR_SIZE);
            *idx = -1;
            return 0;
        }
    }
    return -1;
}

int
get_source_info(const char *id, char *source, char *dest, char *key)
{
    memcpy(dest, &_local_ip, 4);

    int i;
    for (i = 0; i < TABLE_SIZE; i++) {

        if (table[i].id[0] == 0) {
            continue;
        }

        if (memcmp(id, table[i].id, ID_SIZE) == 0) {
            memcpy(source, &table[i].local_ip, 4);
            memcpy(key, table[i].key, KEY_SIZE);
            return 0;
        }
    }
    return -1;
}

int
get_source_info_by_addr(const char *p2p_addr, char *source, char *dest)
{
    memcpy(dest, &_local_ip, 4);

    int i;
    for (i = 0; i < TABLE_SIZE; i++) {

        if (table[i].id[0] == 0) {
            continue;
        }

        if (memcmp(p2p_addr, table[i].p2p_addr, ADDR_SIZE) == 0) {
            memcpy(source, &table[i].local_ip, 4);
            return 0;
        }
    }
    return -1;
}

