
#include <stdio.h>
#include <stdlib.h>
#include <time.h> // used to generate random seed
#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>

#include <translator.h>
#include <peerlist.h>
#include <tap.h>
#include <headers.h>
#include <socket_utils.h>
#include <packetio.h>
#include <svpn.h>

static int
process_inputs(thread_opts_t *opts, char *inputs[])
{
    char id[ID_SIZE+1] = { 0 };

    if (strcmp(inputs[0], "setid") == 0) {
        peerlist_set_local_p(inputs[1], opts->local_ip4, opts->local_ip6);
        printf("id = %s ipv4 = %s ipv6 = %s\n",
               inputs[1], opts->local_ip4, opts->local_ip6);
    }
    else if (strcmp(inputs[0], "add") == 0) {
        peerlist_add_p(inputs[1], inputs[2], inputs[3], atoi(inputs[4]));
        strncpy(id, inputs[1], ID_SIZE);
        struct peer_state *peer;
        peerlist_get_by_id(id, &peer);
        printf("id = %s ip = %s addr = %s\n", id,
               inet_ntoa(*(struct in_addr*)(&peer->local_ipv4_addr)),
               inputs[4]);
    } else {
        fprintf(stderr, "Unrecognized Command: '%s'\n", inputs[0]);
    }
    return 0;
}

/**
 * Generates a string containing an IPv6 address with the given prefix. Maximum
 * written string length is 39 characters, and is written to `address`.
 */
static int
generate_ipv6_address(char *prefix, unsigned short prefix_len, char *address)
{
    if (prefix_len % 16) {
        fprintf(stderr, "Bad prefix_len value. (only multiples of 16 are "
                        "supported\n");
        return -1;
    }
    unsigned short blocks_left = (128-prefix_len) / 16;
    strcpy(address, prefix);
    for (int i = 0; i < blocks_left; i++) {
        if (blocks_left-i <= 1)
            // ensure the last block is of constant string length
            sprintf(&address[strlen(address)], ":%x",
                    (rand() % (0xEFFF+1)) + 0x1000);
        else
            sprintf(&address[strlen(address)], ":%x", rand() % (0xFFFF+1));
    }
    return 0;
}

static void
get_ipv6_address(char *address) {
    FILE* fd;
    if ((fd = fopen(IPV6_ADDR_FILE, "r")) == NULL) {
        generate_ipv6_address("fd50:0dbc:41f2:4a3c", 64, address);
        if ((fd = fopen(IPV6_ADDR_FILE, "w")) == NULL) {
            fprintf(stderr, "Could not write ip address back to file\n");
        } else {
            fputs(address, fd);
            fclose(fd);
        }
    } else {
        fgets(address, 40, fd);
        if (address[strlen(address)-1] == '\n') {
            address[strlen(address)-1] = '\0'; // strip newline
        }
        fclose(fd);
    }
}

int
main(int argc, char *argv[])
{
    char* ipv4_addr = "172.31.0.100";
    srand(time(NULL)); // set up random number generator
    char ipv6_addr[8*5];
    get_ipv6_address(ipv6_addr);
    thread_opts_t opts;
    peerlist_init(TABLE_SIZE);

    opts.tap = tap_open("svpn0", opts.mac);
    opts.local_ip4 = ipv4_addr;
    opts.local_ip6 = ipv6_addr;
    opts.sock4 = socket_utils_create_ipv4_udp_socket(5800);
    opts.sock6 = socket_utils_create_ipv6_udp_socket(
        5800, if_nametoindex("svpn0")
    );

    // configure the tap device
    tap_set_ipv4_addr(ipv4_addr, 24);
    tap_set_ipv6_addr(ipv6_addr, 64);
    tap_set_mtu(MTU);
    tap_set_base_flags();
    tap_set_up();
    peerlist_set_local_p("nobody", ipv4_addr, ipv6_addr);

    // drop root priviledges and set to nobody
    // I need to add chroot jail in here later
    struct passwd * pwd = getpwnam("nobody");
    if (getuid() == 0) {
        if (setgid(pwd->pw_uid) < 0) {
            fprintf(stderr, "setgid failed\n");
            close(opts.sock4);
            close(opts.sock6);
            tap_close();
            return -1;
        }
        if (setuid(pwd->pw_gid) < 0) {
            fprintf(stderr, "setuid failed\n");
            close(opts.sock4);
            close(opts.sock6);
            tap_close();
            return -1;
        }
    }

    pthread_t send_thread, recv_thread;
    pthread_create(&send_thread, NULL, udp_send_thread, &opts);
    pthread_create(&recv_thread, NULL, udp_recv_thread, &opts);

    char buf[200] = { '0' };
    char * inputs[5];
    int i, j;

    while (fgets(buf, sizeof(buf), stdin) != NULL) {
        // trim newline
        buf[strlen(buf)-1] = ' ';

        i = j = 0;
        inputs[j++] = buf + i;

        while (buf[i] != '\0' && i < sizeof(buf)/sizeof(char)) {
            if (buf[i] == ' ') {
                buf[i] = '\0';
                inputs[j++] = buf + i + 1;

                if (j == sizeof(inputs)/sizeof(char *)) break;
            }
            i++;
        }
        process_inputs(&opts, inputs);
    }
    return 0;
}
