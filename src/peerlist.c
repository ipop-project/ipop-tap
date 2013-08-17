/*
 * svpn-core
 * Copyright 2013, University of Florida
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#define _GNU_SOURCE // needed for search.h
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "../lib/hsearch/search.h" // hash tables!
#include "peerlist.h"

static size_t table_length;
static struct hsearch_data *id_table;
static struct hsearch_data *ipv4_addr_table;
static struct hsearch_data *ipv6_addr_table;
// We can't iterate a hashtable, so we have to keep track of entries separately:
static struct peer_state **table_entries;
static struct peer_state *sequential_table_entries; // used with multicast
// `sequential_table_entries` is just table_entries, but with one less level of
// indirection. `table_entries` is useful if we decide we later want to free the
// data within the hashtables, as we need the right pointers.
// `sequential_table_entries` is needed when we return after being given a
// multicast address, because peerlist_get_by_local_ipv4_addr's peer argument is
// only a double pointer (**peer), so we can't write back a double pointer, only
// a single (*).
static int table_entries_length = 0;
static char local_id[ID_SIZE+1]; // +1 for \0
static struct in_addr local_ipv4_addr; // Our virtual IPv4 address
static struct in6_addr local_ipv6_addr; // Our virtual IPv6 address
// With IPv4 addresses, each peer is assigned sequentially, as to prevent
// collisions. This causes some disparity between how we see the peer, and how
// the peer sees itself, but that can (usually) be solved with some translation.
// With IPv6 addresses, each peer gets a (typically random) address that is not
// sequential. We get informed by the user when that peer is added what the IPv6
// address is. Since the address space is so large, we can just assume there
// will be no collisions, and thus there will be no disparity or translation!
static struct in_addr base_ipv4_addr; // iterated when adding a peer, is
                                      // assigned to peer
struct peer_state peerlist_local; // used to publicly expose the local peer info

/**
 * Returns 0 on success, -1 on failure.
 */
int
peerlist_init(size_t _table_length)
{
    table_length = _table_length;
    int hsds = sizeof(struct hsearch_data);
    // allocate space for all our needed tables
    if ((id_table =                 calloc(table_length, hsds)) == NULL ||
        (ipv4_addr_table =          calloc(table_length, hsds)) == NULL ||
        (ipv6_addr_table =          calloc(table_length, hsds)) == NULL ||
        (table_entries =            malloc(table_length *
                                        sizeof(struct peer_state *))) == NULL ||
        (sequential_table_entries = malloc(table_length *
                                        sizeof(struct peer_state))) == NULL)
    {
        fprintf(stderr, "Could not allocate memory for peerlist tables.\n");
        return -1;
    }
    // initialize the tables now that we allocated the space
    if (!hcreate_r(table_length, id_table) ||
        !hcreate_r(table_length, ipv4_addr_table) ||
        !hcreate_r(table_length, ipv6_addr_table))
    {
        fprintf(stderr, "Could not initialize peerlist tables.\n");
        return -1;
    }
    return 0;
}

/**
 * To ensure each client gets a unique local (virtual) ipv4 address, we keep a
 * counter, and increment it, giving each client a sequentially assigned
 * address.
 */
static inline void
increment_base_ipv4_addr()
{
    unsigned char *ip = (unsigned char *)&base_ipv4_addr.s_addr;
    ip[3]++;
}

/**
 * Adds this local machine to the peerlist, given a (human-readable) string
 * indentifier and a local IPv4/6 address pair.
 */
int
peerlist_set_local(const char *_local_id,
                   const struct in_addr *_local_ipv4_addr,
                   const struct in6_addr *_local_ipv6_addr)
{
    if (strlen(local_id) > ID_SIZE) {
        fprintf(stderr, "Bad local_id. Too long.\n"); return -1;
    }
    strcpy(local_id, _local_id);
    memcpy(&local_ipv4_addr, _local_ipv4_addr, sizeof(struct in_addr));
    memcpy(&base_ipv4_addr,  _local_ipv4_addr, sizeof(struct in_addr));
    increment_base_ipv4_addr();
    memcpy(&local_ipv6_addr, _local_ipv6_addr, sizeof(struct in6_addr));
    struct in_addr dest_ipv4_addr;
    char ip[] = "127.0.0.1";
    if (!inet_pton(AF_INET, ip, &dest_ipv4_addr.s_addr)) {
        fprintf(stderr, "Bad IPv4 address format: %s\n", ip);
        return -1;
    }

    // initialize the local peer struct
    strcpy(peerlist_local.id, local_id);
    peerlist_local.local_ipv4_addr = local_ipv4_addr;
    peerlist_local.local_ipv6_addr = local_ipv6_addr;
    peerlist_local.dest_ipv4_addr = dest_ipv4_addr;
    peerlist_local.port = 0;

    return 0;
}

/**
 * Adds this local machine to the peerlist, given a (human-readable) string
 * indentifier and a local IPv4/6 address pair. Addresses are given as strings.
 */
int
peerlist_set_local_p(const char *_local_id, const char *_local_ipv4_addr_p,
                     const char *_local_ipv6_addr_p)
{
    struct in_addr local_ipv4_addr_n;
    struct in6_addr local_ipv6_addr_n;
    if (!inet_pton(AF_INET, _local_ipv4_addr_p, &local_ipv4_addr_n)) {
        fprintf(stderr, "Bad IPv4 address format: %s\n", _local_ipv4_addr_p);
        return -1;
    }
    if (!inet_pton(AF_INET6, _local_ipv6_addr_p, &local_ipv6_addr_n)) {
        fprintf(stderr, "Bad IPv6 address format: %s\n", _local_ipv6_addr_p);
        return -1;
    }
    return peerlist_set_local(_local_id, &local_ipv4_addr_n,
                                         &local_ipv6_addr_n);
}

/**
 * id --        Some string used as an identifier name for the client.
 * dest_ipv4 -- The IPv4 Address to actually send the data to (must be directly
 *              accessible). All traffic ends up getting sent to this address.
 * dest_ipv6 -- The virtual IPv6 address assigned to the client (usually
 *              random). We should never get collisions between these addresses.
 *              This address is simply needed by the peerlist so that when we
 *              intercept a packet, we can look up where to send it by the IPv6
 *              address.
 * port --      The port to communicate with the client peer over.
 */
int
peerlist_add(const char *id, const struct in_addr *dest_ipv4,
             const struct in6_addr *dest_ipv6, const uint16_t port)
{
    if (strlen(id) > ID_SIZE) {
        fprintf(stderr, "Bad id. Too long.\n"); return -1;
    }

    // create and populate a peer structure
    struct peer_state *peer = malloc(sizeof(struct peer_state));
    if (peer == NULL) {
        fprintf(stderr, "Not enough memory to allocate peer.\n");
    }
    strcpy(peer->id, id);
    memcpy(&peer->local_ipv4_addr, &base_ipv4_addr, sizeof(struct in_addr));
    memcpy(&peer->local_ipv6_addr, dest_ipv6, sizeof(struct in6_addr));
    memcpy(&peer->dest_ipv4_addr, dest_ipv4, sizeof(struct in_addr));
    peer->port = port;

    ENTRY table_entry = {
        .data = peer
    };
    ENTRY* retval;

    // Allocate space for our keys:
    // hsearch requires our keys to be null-terminated strings, so we convert
    // in_addr and in6_addr values with inet_ntop first, but we need to allocate
    // space for the keys.
    const int ipv4_key_length = 4*4;
    const int ipv6_key_length = 8*5;
    char *ipv4_key = malloc(ipv4_key_length * sizeof(char));
    char *ipv6_key = malloc(ipv6_key_length * sizeof(char));

    // Enter everything into our tables:
    // id_table
    table_entry.key = peer->id;
    if (!hsearch_r(table_entry, ENTER, &retval, id_table)) {
        fprintf(stderr, "Ran out of space in peerlist table.\n"); return -1;
    }

    // ipv4_addr_table:
    inet_ntop(AF_INET, &peer->local_ipv4_addr, ipv4_key, ipv4_key_length);
    table_entry.key = ipv4_key;
    hsearch_r(table_entry, ENTER, &retval, ipv4_addr_table);

    // ipv6_addr_table:
    inet_ntop(AF_INET6, &peer->local_ipv6_addr, ipv6_key, ipv6_key_length);
    table_entry.key = ipv6_key;
    hsearch_r(table_entry, ENTER, &retval, ipv6_addr_table);

    table_entries[table_entries_length] = peer;
    sequential_table_entries[table_entries_length] = *peer;
    table_entries_length++;

    increment_base_ipv4_addr(); // only actually increment on success
    return 0;
}

/**
 * A convenience form of `peerlist_add`, allowing one to use strings to define
 * IP addresses instead of `in_addr` and `in6_addr` structs. Conversion is done
 * with `inet_pton`.
 */
int
peerlist_add_p(const char *id, const char *dest_ipv4, const char *dest_ipv6,
               const uint16_t port)
{
    struct in_addr dest_ipv4_n;
    struct in6_addr dest_ipv6_n;
    if (!inet_pton(AF_INET, dest_ipv4, &dest_ipv4_n)) {
        fprintf(stderr, "Bad IPv4 address format: %s\n", dest_ipv4);
        return -1;
    }
    if (!inet_pton(AF_INET6, dest_ipv6, &dest_ipv6_n)) {
        fprintf(stderr, "Bad IPv6 address format: %s\n", dest_ipv6);
        return -1;
    }
    return peerlist_add(id, &dest_ipv4_n, &dest_ipv6_n, port);
}

/**
 * Given a string human-readable identifier, gives back a pointer to the
 * internal `peer_state` struct representation of the related peer. The
 * underlining struct should be treated as immutable, as changing it could
 * interfere with the hash-table mechanism used internally. Returns 0 on
 * success, -1 on failure (if a client with the given id cannot be found).
 */
int
peerlist_get_by_id(const char *id, struct peer_state **peer)
{
    ENTRY *result, query;
    query.key = malloc((strlen(id)+1) * sizeof(char));
    strcpy(query.key, id);
    if (!hsearch_r(query, FIND, &result, id_table)) {
        free(query.key);
        return -1;
    }
    *peer = result->data;
    free(query.key);
    return 0;
}

/**
 * Returns -1 on failure, 0 on a normal return, length+1 on multicast, where
 * length is the length of the array written back to `peer`. No underlying data
 * written back to `peer` should be modified, to preserve the internal state of
 * the peerlist. length+1 must be returned in the case that length is 0,
 * preventing a case of ambiguity.
 */
int
peerlist_get_by_local_ipv4_addr(const struct in_addr *_local_ipv4_addr,
                                struct peer_state **peer)
{
    unsigned char start_byte =
        ((unsigned char *)(&_local_ipv4_addr->s_addr))[0];
    unsigned char end_byte =
        ((unsigned char *)(&_local_ipv4_addr->s_addr))[3];
    if ((start_byte >= 224 && start_byte <= 239) || end_byte == 0xFF) {
        *peer = sequential_table_entries;
        return table_entries_length + 1;
    }
    ENTRY *result;
    char key[4*4];
    inet_ntop(AF_INET, _local_ipv4_addr, key, sizeof(key)/sizeof(char));
    ENTRY query = { .key = key };
    if (!hsearch_r(query, FIND, &result, ipv4_addr_table)) return -1;
    *peer = result->data;
    return 0;
}

int
peerlist_get_by_local_ipv4_addr_p(const char *_local_ipv4_addr,
                                  struct peer_state **peer)
{
    struct in_addr _local_ipv4_addr_n;
    if (!inet_pton(AF_INET, _local_ipv4_addr, &_local_ipv4_addr_n)) {
        fprintf(stderr, "Bad IPv4 address format: %s\n", _local_ipv4_addr);
        return -1;
    }
    return peerlist_get_by_local_ipv4_addr(&_local_ipv4_addr_n, peer);
}

int
peerlist_get_by_local_ipv6_addr(const struct in6_addr *_local_ipv6_addr,
                                struct peer_state **peer)
{
    unsigned char* bytes =
        ((unsigned char *)(&_local_ipv6_addr->s6_addr));
    unsigned char type = bytes[1] & 0x0F;
    if (bytes[0] == 0xFF && (type == 0x05 || type == 0x08 || type == 0x0e)) {
        // if it is an IPv6 multicast address by the rules given by
        // https://en.wikipedia.org/wiki/Multicast_address#IPv6
        *peer = sequential_table_entries;
        return table_entries_length + 1;
    }
    ENTRY *result;
    char key[5*8];
    inet_ntop(AF_INET6, _local_ipv6_addr, key, sizeof(key)/sizeof(char));
    ENTRY query = { .key = key };
    if (!hsearch_r(query, FIND, &result, ipv6_addr_table)) return -1;
    *peer = result->data;
    return 0;
}

int
peerlist_get_by_local_ipv6_addr_p(const char *_local_ipv6_addr,
                                  struct peer_state **peer)
{
    struct in6_addr _local_ipv6_addr_n;
    if (!inet_pton(AF_INET6, _local_ipv6_addr, &_local_ipv6_addr_n)) {
        fprintf(stderr, "Bad IPv6 address format: %s\n", _local_ipv6_addr);
        return -1;
    }
    return peerlist_get_by_local_ipv6_addr(&_local_ipv6_addr_n, peer);
}

int
override_base_ipv4_addr_p(const char *ipv4)
{
   struct in_addr ipv4_n;
    if (!inet_pton(AF_INET, ipv4, &ipv4_n)) {
        fprintf(stderr, "Bad IPv4 address format: %s\n", ipv4);
        return -1;
    }
    memcpy(&base_ipv4_addr, &ipv4_n, sizeof(struct in_addr));
    return 0;
}

