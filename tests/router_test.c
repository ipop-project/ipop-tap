
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
    set_local_ip("172.31.0.2");

    char id[] = "aliceid";
    char dest_ip[] = "192.168.5.2";
    uint16_t port = atoi("5800");

    int ret;
    ret = add_route(id, dest_ip, port);

    char new_id[] = "bobid";
    char new_dip[] = "192.168.5.3";
    ret = add_route(new_id, new_dip, port);

    struct sockaddr_in addr;
    int idx = 0;
    char dest_id[12];

    char ip1[] = { 172, 31, 0, 100 };
    ret = get_dest_addr(&addr, ip1, &idx, 1, dest_id); 
    printf("%s\n", test_int(ret, 0));
    printf("%s\n", test_int(idx, -1));
    printf("%s\n", test_string(dest_id, "aliceid"));

    char ip2[] = { 172, 31, 0, 101};
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


