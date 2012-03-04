
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

int main()
{
    set_local_peer("alice", "172.31.0.2");

    char id[20] = { 0 };
    strcpy(id, "aliceid");
    char dest_ip[] = "192.168.5.2";
    uint16_t port = atoi("5800");
    char key[32];

    int ret;
    ret = add_peer(id, dest_ip, port, key);

    char new_id[20] = { 0 };
    strcpy(new_id, "bobid");
    char new_dip[] = "192.168.5.3";
    char new_key[32];
    ret = add_peer(new_id, new_dip, port, new_key);

    struct sockaddr_in addr;
    int idx = 0;
    char dest_id[20];
    char source_id[20];

    char ip1[] = { 172, 31, 0, 101 };
    ret = get_dest_info(ip1, source_id, dest_id, &addr, key, &idx); 
    printf("%s\n", test_int(ret, 0));
    printf("%s\n", test_int(idx, -1));
    printf("%s\n", test_string(dest_id, "aliceid"));

    char ip2[] = { 172, 31, 0, 102};
    ret = get_dest_info(ip2, source_id, dest_id, &addr, key, &idx);
    printf("%s\n", test_int(ret, 0));
    printf("%s\n", test_int(idx, -1));
    printf("%s\n", test_string(dest_id, "bobid"));

    char ip3[] = {224, 0, 0, 1};
    idx = 0;
    ret = get_dest_info(ip3, source_id, dest_id, &addr, key, &idx);
    printf("%s\n", test_int(ret, 0));
    printf("%s\n", test_int(idx++, 1));
    printf("%s\n", test_string(dest_id, "aliceid"));

    ret = get_dest_info(ip3, source_id, dest_id, &addr, key, &idx);
    printf("%s\n", test_int(ret, 0));
    printf("%s\n", test_int(idx++, 1));
    printf("%s\n", test_string(dest_id, "bobid"));
 
    ret = get_dest_info(ip3, source_id, dest_id, &addr, key, &idx);
    printf("%s\n", test_int(ret, -1));
    printf("%s\n", test_int(idx++, -1));
 
}


