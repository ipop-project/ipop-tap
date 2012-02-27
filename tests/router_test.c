
#include <stdio.h>
#include <router.h>

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
    char id[] = "aliceid";
    char local_ip[] = "172.31.0.3";
    char dest_ip[] = "192.168.5.2";
    uint16_t port = atoi("5800");

    int ret;
    clear_table();
    ret = add_route(id, local_ip, dest_ip, port);

    char new_id[] = "bobid";
    char new_lip[] = "172.31.0.4";
    char new_dip[] = "192.168.5.3";
    ret = add_route(new_id, new_lip, new_dip, port);

    struct sockaddr_in addr;
    int idx = 0;
    char dest_id[12];

    char ip1[] = { 172, 31, 0, 3 };
    ret = get_dest_addr(&addr, ip1, &idx, 1, dest_id);
    printf("%s\n", test_int(ret, 0));
    printf("%s\n", test_int(idx, -1));
    printf("%s\n", test_string(dest_id, "aliceid"));

    char ip2[] = { 172, 31, 0, 4};
    ret = get_dest_addr(&addr, ip2, &idx, 1, dest_id);
    printf("%s\n", test_int(ret, 0));
    printf("%s\n", test_int(idx, -1));
    printf("%s\n", test_string(dest_id, "bobid"));

    char ip3[] = {224, 0, 0, 1};
    idx = 0;
    ret = get_dest_addr(&addr, ip3, &idx, 1, dest_id);
    printf("%s\n", test_int(ret, 0));
    printf("%s\n", test_int(idx++, 1));
    printf("%s\n", test_string(dest_id, "aliceid"));

    ret = get_dest_addr(&addr, ip3, &idx, 1, dest_id);
    printf("%s\n", test_int(ret, 0));
    printf("%s\n", test_int(idx++, 1));
    printf("%s\n", test_string(dest_id, "bobid"));
 
    ret = get_dest_addr(&addr, ip3, &idx, 1, dest_id);
    printf("%s\n", test_int(ret, -1));
    printf("%s\n", test_int(idx++, -1));
 
}


