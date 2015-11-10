/*
 * ipop-tap
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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(LINUX) || defined(ANDROID)
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#include "peerlist.h"

#include "../lib/klib/khash.h"

KHASH_MAP_INIT_STR(pmap, struct peer_state*)
/* KHASH only use a integer or string as a key
   We convert 48bit MAC address to 64bit integer as a key */
KHASH_MAP_INIT_INT64(64, struct peer_state*)

static khash_t(pmap) *id_table;
static khash_t(pmap) *ipv4_addr_table;
static khash_t(pmap) *ipv6_addr_table;
static khash_t(64) *mac_table;
static khint_t id_iterator;
static khint_t ipv4_iterator;
static khint_t ipv6_iterator;

static char local_id[ID_SIZE]; // +1 for \0
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


// Stores the local subnet mask 
static struct in_addr subnet_mask = { .s_addr = ~(0u) };

// Stores the subnet mask for router mode
static struct in_addr router_subnet_mask = { .s_addr = ~(0u) };

struct peer_state null_peer = { .id = {0} };
struct peer_state peerlist_local; // used to publicly expose the local peer info

static int
convert_to_hex_string(const char *source, int source_len,
                      char *dest, int dest_len)
{
    if (dest_len <= source_len * 2) {
        fprintf(stderr, "dest_len %d has to be greater 2x source_len %d\n",
                dest_len, source_len);
        return -1;
    }
    int i;
    for (i = 0; i < source_len; i++) {
        sprintf(dest + (2*i), "%02x", *(source + i) & 0xFF);
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
 * Returns 0 on success, -1 on failure.
 */
int
peerlist_init()
{
    // init hash table
    id_table = kh_init(pmap);
    ipv4_addr_table = kh_init(pmap);
    ipv6_addr_table = kh_init(pmap);
    mac_table = kh_init(64);
    return 0;
}

int
peerlist_reset_iterators()
{
    // the klib library requires that we initialize the tables
    ipv4_iterator = kh_begin(ipv4_addr_table);
    ipv6_iterator = kh_begin(ipv6_addr_table);
    return 0;
}

/**
 * Adds this local machine to the peerlist, given a unique 160-bit id
 * identifier and a local IPv4/6 address pair.
 */
int
peerlist_set_local(const char *_local_id,
                   const struct in_addr *_local_ipv4_addr,
                   const struct in6_addr *_local_ipv6_addr)
{
    memcpy(local_id, _local_id, ID_SIZE);
    memcpy(&local_ipv4_addr, _local_ipv4_addr, sizeof(struct in_addr));
    memcpy(&base_ipv4_addr,  _local_ipv4_addr, sizeof(struct in_addr));
    increment_base_ipv4_addr();
    memcpy(&local_ipv6_addr, _local_ipv6_addr, sizeof(struct in6_addr));
    struct in_addr dest_ipv4_addr;
    char ip[] = "127.0.0.1";
#if defined(LINUX) || defined(ANDROID)
    if (!inet_pton(AF_INET, ip, &dest_ipv4_addr.s_addr)) {
#elif defined(WIN32)
    CHAR *Term;
    LONG err = RtlIpv4StringToAddress(ip, TRUE, &Term,
                                      (IN_ADDR *)&dest_ipv4_addr.s_addr);
    if (err != NO_ERROR) {
#endif
        fprintf(stderr, "Bad IPv4 address format: %s\n", ip);
        return -1;
    }

    // initialize the local peer struct
    memcpy(peerlist_local.id, local_id, ID_SIZE);
    peerlist_local.local_ipv4_addr = local_ipv4_addr;
    peerlist_local.local_ipv6_addr = local_ipv6_addr;
    peerlist_local.dest_ipv4_addr = dest_ipv4_addr;
    peerlist_local.port = 0;

    return 0;
}

/**
 * Adds this local machine to the peerlist, given a unique 160-bit id
 * identifier and a local IPv4/6 address pair. Addresses are given as strings.
 */
int
peerlist_set_local_p(const char *_local_id, const char *_local_ipv4_addr_p,
                     const char *_local_ipv6_addr_p)
{
    struct in_addr local_ipv4_addr_n;
    struct in6_addr local_ipv6_addr_n;
#if defined(LINUX) || defined(ANDROID)
    if (!inet_pton(AF_INET, _local_ipv4_addr_p, &local_ipv4_addr_n)) {
#elif defined(WIN32)
    CHAR* Term;
    LONG err = RtlIpv4StringToAddress(_local_ipv4_addr_p, TRUE, &Term,
                                      &local_ipv4_addr_n);
    if (err != NO_ERROR) {
#endif
        fprintf(stderr, "Bad IPv4 address format: %s\n", _local_ipv4_addr_p);
        return -1;
    }
#if defined(LINUX) || defined(ANDROID)
    if (!inet_pton(AF_INET6, _local_ipv6_addr_p, &local_ipv6_addr_n)) {
#elif defined(WIN32)
    ULONG ScopeId;
    USHORT Port;
    err = RtlIpv6StringToAddressEx(_local_ipv6_addr_p, 
                                   &local_ipv6_addr_n, &ScopeId, &Port);
    if (err != NO_ERROR) {
#endif
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
    // create and populate a peer structure
    struct peer_state *peer = malloc(sizeof(struct peer_state));
    if (peer == NULL) {
        fprintf(stderr, "Not enough memory to allocate peer.\n");
    }
    memcpy(peer->id, id, ID_SIZE);
    memcpy(&peer->local_ipv4_addr, &base_ipv4_addr, sizeof(struct in_addr));
    memcpy(&peer->local_ipv6_addr, dest_ipv6, sizeof(struct in6_addr));
    memcpy(&peer->dest_ipv4_addr, dest_ipv4, sizeof(struct in_addr));
    peer->port = port;

    // Allocate space for our keys:
    // hsearch requires our keys to be null-terminated strings, so we convert
    // in_addr and in6_addr values with inet_ntop first, but we need to allocate
    // space for the keys.
    const int ipv4_key_length = 4*4;
    const int ipv6_key_length = 8*5;
    const int id_key_length = ID_SIZE * 2 + 1;
    char *ipv4_key = malloc(ipv4_key_length * sizeof(char));
    char *ipv6_key = malloc(ipv6_key_length * sizeof(char));
    char *id_key = malloc(id_key_length * sizeof(char));

    int ret;
    khint_t k;

    // id_table
    convert_to_hex_string(peer->id, ID_SIZE, id_key, id_key_length);
    k = kh_put(pmap, id_table, id_key, &ret);
    if (ret == -1) {
        fprintf(stderr, "put failed for id_table.\n"); return -1;
    }
    else if (!ret) {
        free(&kh_key(id_table, k));
        free(kh_value(id_table, k));
        kh_del(pmap, id_table, k);
    }
    kh_value(id_table, k) = peer;

    // Router mode support
    peer->local_ipv4_addr.s_addr &= router_subnet_mask.s_addr;

    // ipv4_addr_table
#if defined(LINUX) || defined(ANDROID)
    inet_ntop(AF_INET, &peer->local_ipv4_addr, ipv4_key, ipv4_key_length);
#elif defined(WIN32)
    RtlIpv4AddressToString(&peer->local_ipv4_addr, ipv4_key);
#endif
    k = kh_put(pmap, ipv4_addr_table, ipv4_key, &ret);
    if (ret == -1) {
        fprintf(stderr, "put failed for ipv4_table.\n"); return -1;
    }
    else if (!ret) {
        free(&kh_key(ipv4_addr_table, k));
        kh_del(pmap, ipv4_addr_table, k);
    }
    kh_value(ipv4_addr_table, k) = peer;

    // ipv6_addr_table:
#if defined(LINUX) || defined(ANDROID)
    inet_ntop(AF_INET6, &peer->local_ipv6_addr, ipv6_key, ipv6_key_length);
#elif defined(WIN32)
    RtlIpv6AddressToString(&peer->local_ipv6_addr, ipv6_key);
#endif
    k = kh_put(pmap, ipv6_addr_table, ipv6_key, &ret);
    if (ret == -1) {
        fprintf(stderr, "put failed for ipv6_table.\n"); return -1;
    }
    else if (!ret) {
        free(&kh_key(ipv6_addr_table, k));
        kh_del(pmap, ipv6_addr_table, k);
    }
    kh_value(ipv6_addr_table, k) = peer;


    increment_base_ipv4_addr(); // only actually increment on success
    return 0;
}

// Create peer with given uid and make index by uid
int
peerlist_add_by_uid(const char *id)
{
    // create and populate a peer structure
    struct peer_state *peer = malloc(sizeof(struct peer_state));
    if (peer == NULL) {
        fprintf(stderr, "Not enough memory to allocate peer.\n");
    }
    memcpy(peer->id, id, ID_SIZE);

    // Allocate space for our keys:
    // hsearch requires our keys to be null-terminated strings, so we convert
    // in_addr and in6_addr values with inet_ntop first, but we need to allocate
    // space for the keys.
    const int id_key_length = ID_SIZE * 2 + 1;
    char *id_key = malloc(id_key_length * sizeof(char));

    int ret;
    khint_t k;

    // id_table
    convert_to_hex_string(peer->id, ID_SIZE, id_key, id_key_length);
    k = kh_put(pmap, id_table, id_key, &ret);
    if (ret == -1) {
        fprintf(stderr, "put failed for id_table.\n"); return -1;
    }
    else if (!ret) {
        free(&kh_key(id_table, k));
        free(kh_value(id_table, k));
        kh_del(pmap, id_table, k);
    }
    kh_value(id_table, k) = peer;
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
#if defined(LINUX) || defined(ANDROID)
    if (!inet_pton(AF_INET, dest_ipv4, &dest_ipv4_n)) {
#elif defined(WIN32)
    CHAR* Term;
    LONG err = RtlIpv4StringToAddress(dest_ipv4, TRUE, &Term, &dest_ipv4_n);
    if (err != NO_ERROR) {
#endif
        fprintf(stderr, "Bad IPv4 address format: %s\n", dest_ipv4);
        return -1;
    }
#if defined(LINUX) || defined(ANDROID)
    if (!inet_pton(AF_INET6, dest_ipv6, &dest_ipv6_n)) {
#elif defined(WIN32)
    ULONG ScopeId;
    USHORT Port;
    err = RtlIpv6StringToAddressEx(dest_ipv6, &dest_ipv6_n, &ScopeId, &Port);
    if (err != NO_ERROR) {
#endif
        fprintf(stderr, "Bad IPv6 address format: %s\n", dest_ipv6);
        return -1;
    }
    return peerlist_add(id, &dest_ipv4_n, &dest_ipv6_n, port);
}

// Associate mac address with TinCan peer.
// Fill up MAC address in peer and make index for mac as key and peer as value
int
mac_add(const unsigned char * ipop_buf, int mac_offset)
{
    int id_key_length = ID_SIZE*2+1;
    char id_key [id_key_length];
    int ret;
    convert_to_hex_string((const char *) ipop_buf, ID_SIZE, id_key,
                          id_key_length);
    struct peer_state *peer = NULL;
    peerlist_get_by_ids(id_key, &peer);
    if (peer == NULL) {
        fprintf(stderr, "Unable to find the peer with given key.\n"); return -1;
    }
    int i;
    long long key = 0;
    for(i=0;i<6;i++) {
        *(peer->mac)=*(ipop_buf+mac_offset+i);
        key += (long long) *(ipop_buf+mac_offset+i) << 8*i;
    }
    khint_t k = kh_put(64, mac_table, key, &ret);
    if (ret == -1) {
        fprintf(stderr, "put failed for mac_table.\n"); return -1;
    }
    kh_value(mac_table, k) = peer;
    return 0;
}

// Associate TinCan link with Mac address of sender hareward address of ARP
int
arp_sha_mac_add(const unsigned char * ipop_buf) {
    return mac_add(ipop_buf, 62);
}

// Associate TinCan link with Mac address of sender hareward address of 
// Ethernet frame
int
source_mac_add(const unsigned char * ipop_buf) {
    return mac_add(ipop_buf, 46);
}

/**
 * Given a unique 160-bit identifier, gives back a pointer to the
 * internal `peer_state` struct representation of the related peer. The
 * underlining struct should be treated as immutable, as changing it could
 * interfere with the hash-table mechanism used internally. Returns 0 on
 * success, -1 on failure (if a client with the given id cannot be found).
 */
int
peerlist_get_by_id(const char *id, struct peer_state **peer)
{
    const int id_key_length = ID_SIZE * 2 + 1;
    char key[id_key_length];
    convert_to_hex_string(id, ID_SIZE, key, id_key_length);
    khint_t k = kh_get(pmap, id_table, key);
    if (k == kh_end(id_table)) return -1;
    *peer = kh_value(id_table, k);
    return 0;
}

//argument id is give as string
int
peerlist_get_by_ids(const char *id, struct peer_state **peer)
{
    khint_t k = kh_get(pmap, id_table, id);
    if (k == kh_end(id_table)) return -1;
    *peer = kh_value(id_table, k);
    return 0;
}

int
peerlist_get_by_local_ipv4_addr(struct in_addr *_local_ipv4_addr,
                                struct peer_state **peer)
{
    unsigned char start_byte =
        ((unsigned char *)(&_local_ipv4_addr->s_addr))[0];
    unsigned char end_byte =
        ((unsigned char *)(&_local_ipv4_addr->s_addr))[3];
    if ((start_byte >= 224 && start_byte <= 239) || end_byte == 0xFF) {
        for (; ipv4_iterator < kh_end(ipv4_addr_table); ++ipv4_iterator) {
            if (kh_exist(ipv4_addr_table, ipv4_iterator)) {
                *peer = kh_value(ipv4_addr_table, ipv4_iterator++);
                return 1;
            }
        }
        return -1;
    }

    // Router mode support
    _local_ipv4_addr->s_addr &= router_subnet_mask.s_addr;

    char key[4*4];
#if defined(LINUX) || defined(ANDROID)
    inet_ntop(AF_INET, _local_ipv4_addr, key, sizeof(key)/sizeof(char));
#elif defined(WIN32)
    RtlIpv4AddressToString(_local_ipv4_addr, key);
#endif
    khint_t k = kh_get(pmap, ipv4_addr_table, key);
    if (k != kh_end(ipv4_addr_table) && kh_exist(ipv4_addr_table, k)) {
        *peer = kh_value(ipv4_addr_table, k);
    }
    else { *peer = &null_peer; }
    return 0;
}

int
peerlist_get_by_local_ipv4_addr_p(const char *_local_ipv4_addr,
                                  struct peer_state **peer)
{
    struct in_addr _local_ipv4_addr_n;
#if defined(LINUX) || defined(ANDROID)
    if (!inet_pton(AF_INET, _local_ipv4_addr, &_local_ipv4_addr_n)) {
#elif defined(WIN32)
    CHAR* Term;
    if(!RtlIpv4StringToAddress(_local_ipv4_addr, TRUE, &Term, 
                               &_local_ipv4_addr_n)) { 
#endif
        fprintf(stderr, "Bad IPv4 address format: %s\n", _local_ipv4_addr);
        return -1;
    }
    return peerlist_get_by_local_ipv4_addr(&_local_ipv4_addr_n, peer);
}

int
peerlist_get_by_local_ipv6_addr(struct in6_addr *_local_ipv6_addr,
                                struct peer_state **peer)
{
    unsigned char* bytes =
        ((unsigned char *)(&_local_ipv6_addr->s6_addr));
    unsigned char type = bytes[1] & 0x0F;
    if (bytes[0] == 0xFF && (type == 0x05 || type == 0x08 || type == 0x0e)) {
        // if it is an IPv6 multicast address by the rules given by
        // https://en.wikipedia.org/wiki/Multicast_address#IPv6
        for (; ipv6_iterator != kh_end(ipv6_addr_table); ++ipv6_iterator) {
            if (kh_exist(ipv6_addr_table, ipv6_iterator)) {
                *peer = kh_value(ipv6_addr_table, ipv6_iterator++);
                return 1;
            }
        }
        return -1;
    }
    char key[5*8];
#if defined(LINUX) || defined(ANDROID)
    inet_ntop(AF_INET6, _local_ipv6_addr, key, sizeof(key)/sizeof(char));
#elif defined(WIN32)
    RtlIpv6AddressToString(_local_ipv6_addr, key);
#endif
    khint_t k = kh_get(pmap, ipv6_addr_table, key);
    if (k != kh_end(ipv6_addr_table) && kh_exist(ipv6_addr_table, k)) {
        *peer = kh_value(ipv6_addr_table, k);
    }
    else { *peer = &null_peer; }
    return 0;
}

int
peerlist_get_by_local_ipv6_addr_p(const char *_local_ipv6_addr,
                                  struct peer_state **peer)
{
    struct in6_addr _local_ipv6_addr_n;
#if defined(LINUX) || defined(ANDROID)
    if (!inet_pton(AF_INET6, _local_ipv6_addr, &_local_ipv6_addr_n)) {
#elif defined(WIN32)
    ULONG ScopeId;
    USHORT Port;
    LONG err = RtlIpv6StringToAddressEx(_local_ipv6_addr, 
                                        &_local_ipv6_addr_n, &ScopeId, &Port);
    if (err != NO_ERROR) {
#endif
        fprintf(stderr, "Bad IPv6 address format: %s\n", _local_ipv6_addr);
        return -1;
    }
    return peerlist_get_by_local_ipv6_addr(&_local_ipv6_addr_n, peer);
}

int
peerlist_get_by_mac_addr(const unsigned char * buf, struct peer_state **peer)
{
    long long key = 0;
    int i;
    for(i=0;i<6;i++) {
        key += (long long) *(buf+i) << 8*i;
    }
    khint_t k = kh_get(64, mac_table, key);
    if (k != kh_end(mac_table) && kh_exist(mac_table, k)) {
        *peer = kh_value(mac_table, k);
    }
    else { *peer = &null_peer; }
    return 0;
}

int
override_base_ipv4_addr_p(const char *_local_ipv4_addr_p)
{
    struct in_addr local_ipv4_addr_n;
#if defined(LINUX) || defined(ANDROID)
    if (!inet_pton(AF_INET, _local_ipv4_addr_p, &local_ipv4_addr_n)) {
#elif defined(WIN32)
    CHAR* Term;
    LONG err = RtlIpv4StringToAddress(_local_ipv4_addr_p, TRUE, &Term,
                                      &local_ipv4_addr_n);
    if (err != NO_ERROR) {
#endif
        fprintf(stderr, "Bad IPv4 address format: %s\n", _local_ipv4_addr_p);
        return -1;
    }
    memcpy(&base_ipv4_addr, &local_ipv4_addr_n, sizeof(struct in_addr));
    return 0;
}

int
set_subnet_mask(unsigned int mask_len, unsigned int router_mask_len)
{
    subnet_mask.s_addr = htonl(~(0u) << (32 - mask_len));
    router_subnet_mask.s_addr = htonl(~(0u) << (32 - router_mask_len));
    return 0;
}

int
check_network_range(struct in_addr ip_addr)
{
    struct in_addr tmp_addr = local_ipv4_addr;
    if (ip_addr.s_addr == tmp_addr.s_addr) return -1;

    tmp_addr.s_addr &= subnet_mask.s_addr;
    ip_addr.s_addr &= subnet_mask.s_addr;

    if (tmp_addr.s_addr == ip_addr.s_addr) return 1;
    return 0;
}

int
reset_id_table()
{
  id_iterator = kh_begin(id_table);
  return 0;
}

int
is_id_table_end()
{
  return (id_iterator == kh_end(id_table));
}

void
increase_id_table_itr()
{
  ++id_iterator;
}


int
is_id_exist() {
  return kh_exist(id_table, id_iterator);
}

void
retrieve_id(const char ** key)
{
   *key = kh_key(id_table, id_iterator);
}

struct peer_state *
retrieve_peer()
{
    struct peer_state *peer;
    peer = kh_value(id_table, id_iterator);
    return peer;
}

void
iterate_id_table()
{
    int i=0;
    for(i=0; i<kh_end(id_table) ; i++) {
      if (kh_exist(id_table, i)) {
         printf("i:%d, key:%s\n", i, kh_key(id_table, i));
      }
    }
}
