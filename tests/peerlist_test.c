
#include <stdio.h>
#include <string.h>
#include <peerlist.h>

#include <minunit.h>

static char *test_string(char *first, char *second)
{
    printf("%s = %s\n", first, second);
    mu_assert("MISMATCH", strncmp(first, second, 12) == 0);
    return "MATCH";
}

static char *test_int(int first, int second)
{
    printf("%d = %d\n", first, second);
    mu_assert("MISMATCH", first == second);
    return "MATCH";
}

int main(int argc, char *argv[])
{
    int ret;
    ret = peerlist_init(10);
    printf("%s\n", test_int(ret, 0));
    ret = peerlist_set_local_p("localid", "172.31.0.100",
                               "fd50:dbc:41f2:4a3c:8b41:299c:8481:f88c");
    printf("%s\n", test_int(ret, 0));

    char *id = "aliceid";
    char dest_ipv4[] = "192.168.5.2";
    char dest_ipv6[] = "fd50:dbc:41f2:4a3c:aa8e:11f8:7841:dd68";
    uint16_t port = 5800;
    char *key = "alice_key";
    char *p2p_addr = "ABCabcABCabcABCabcABCabcABCabcAB"; // ADDR_SIZE is 32

    ret = peerlist_add_p(id, dest_ipv4, dest_ipv6, port, key, p2p_addr);
    printf("%s\n", test_int(ret, 0));

    char *new_id = "bobid";
    char *new_dip4 = "192.168.5.3";
    char *new_dip6 = "fd50:dbc:41f2:4a3c:91dd:a81b:6418:7be1";
    char *new_key = "bob_key";
    char *new_p2p_addr = "CBAabcABCabcABCabcABCabcABCabcAB";
    ret = peerlist_add_p(new_id, new_dip4, new_dip6, port, new_key,
                         new_p2p_addr);
    printf("%s\n", test_int(ret, 0));

    struct peer_state *peer;

    // test lookups by IPv4 address
    // for alice
    ret = peerlist_get_by_local_ipv4_addr_p("172.31.0.101", &peer);
    printf("%s\n", test_int(ret, 0));
    printf("%s\n", test_int(peer->port, port));
    printf("%s\n", test_string(peer->key, key));
    printf("%s\n", test_string(peer->p2p_addr, p2p_addr));
    printf("%s\n", test_string(peer->id, "aliceid"));
    // and for bob now
    ret = peerlist_get_by_local_ipv4_addr_p("172.31.0.102", &peer);
    printf("%s\n", test_int(ret, 0));
    printf("%s\n", test_string(peer->id, "bobid"));
    // and for failure
    ret = peerlist_get_by_local_ipv4_addr_p("172.31.0.103", &peer);
    printf("%s\n", test_int(ret, -1));

    // test lookups by IPv6 address
    // for alice
    ret = peerlist_get_by_local_ipv6_addr_p(dest_ipv6, &peer);
    printf("%s\n", test_int(ret, 0));
    printf("%s\n", test_string(peer->id, "aliceid"));
    // and for failure
    ret = peerlist_get_by_local_ipv6_addr_p("fd50::1", &peer);
    printf("%s\n", test_int(ret, -1));

    // test multicast and broadcast support
    ret = peerlist_get_by_local_ipv4_addr_p("172.31.0.255", &peer);
    printf("%s\n", test_int(ret, 2+1));
    printf("%s\n", test_string(peer[0].id, "aliceid"));
    printf("%s\n", test_string(peer[1].id, "bobid"));
}
