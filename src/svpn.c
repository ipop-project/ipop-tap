
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

#include <jansson.h>

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
main_help(const char *executable)
{
    printf("Usage: %s [-h, --help] [-c|--config path] [-i|--id name]\n"
           "       [-6 ipv6_address] [-p|--port udp_port]\n"
           "       [-t|--tap device_name] [-v|--verbose]\n\n", executable);

    printf("Arguments:\n");
    printf("    -h, --help:    Show this help message.\n");
    printf("    -c, --config:  Give the relative path to the configuration\n"
           "                   json file to use. (default: 'config.json')\n");
    printf("    -i, --id:      The name (id) to give to the local peer.\n"
           "                   (default: 'local')\n");
    printf("    -6:            The virtual IPv6 address to use on the tap\n"
           "                   device. Must begin with 'fd50:0dbc:41f2:4a3c'.\n"
           "                   (default: randomly generated)\n");
    printf("    -p, --port:    The UDP port used by svpn to send and receive\n"
           "                   packet data. (default: 5800)\n");
    printf("    -t, --tap:     The name of the system tap device to use. If\n"
           "                   you're using multiple clients on the same\n"
           "                   machine, device names must be different\n"
           "                   (default: 'svpn0')\n");
    printf("    -v, --verbose: Print out extra information about what's\n"
           "                   happening.\n");
}

static void
main_bad_arg(const char *executable, const char* arg)
{
    fprintf(stderr, "Bad argument: '%s'\n", arg);
    main_help(executable);
}

#define BAD_ARG {main_bad_arg(argv[0], a); return -1;}

int
main(int argc, char *argv[])
{
    srand(time(NULL)); // set up random number generator
    
    // base settings
    char *configuration_file = "config.json";
    char *client_id = NULL;
    char ipv6_addr[8*5]; ipv6_addr[0] = '\0';
    uint16_t port = 0;
    char *tap_device_name = NULL;
    int verbose = 0;

    // read in settings from command line arguments
    for (int i = 1; i < argc; i++) {
        char *a = argv[i];
        if (strcmp(a, "-c") == 0 || strcmp(a, "--config") == 0) {
            if (++i < argc) {
                configuration_file = argv[i];
            } else BAD_ARG
        } else if (strcmp(a, "-i") == 0 || strcmp(a, "--id") == 0) {
            if (++i < argc) {
                client_id = argv[i];
            } else BAD_ARG
        } else if (strcmp(a, "-6") == 0) {
            if (++i < argc && strlen(argv[i]) < sizeof(ipv6_addr)/sizeof(char)){
                strcpy(ipv6_addr, argv[i]);
            } else BAD_ARG
        } else if (strcmp(a, "-p") == 0 || strcmp(a, "--port") == 0) {
            if (++i < argc) {
                port = (uint16_t) atoi(argv[i]);
            } else BAD_ARG
        } else if (strcmp(a, "-t") == 0 || strcmp(a, "--tap") == 0) {
            if (++i < argc) {
                tap_device_name = argv[i];
            } else BAD_ARG
        } else if (strcmp(a, "-v") == 0 || strcmp(a, "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            main_help(argv[0]);
            return 0;
        } else BAD_ARG
    }

    char *ipv4_addr = "172.31.0.100";

    // set uninitialized values to their defaults
    if (client_id == NULL) {
        fprintf(stderr, "Warning: An id was not explicitly set. Falling back "
                        "to 'local'. If no id is set, collisions are likely to "
                        "occur.\n");
        client_id = "local";
    }
    if (ipv6_addr[0] == '\0')
        generate_ipv6_address("fd50:0dbc:41f2:4a3c", 64, ipv6_addr);
    if (port == 0) port = 5800;
    if (tap_device_name == NULL) tap_device_name = "svpn0";

    if (verbose) {
        // pretty-print the client configuration
        printf("Configuration Loaded:\n");
        printf("    Id: '%s'\n", client_id);
        printf("    Virtual IPv6 Address: '%s'\n", ipv6_addr);
        printf("    UDP Socket Port: %d\n", port);
        printf("    TAP Virtual Device Name: '%s'\n", tap_device_name);
    }

    thread_opts_t opts;
    peerlist_init(TABLE_SIZE);

    opts.tap = tap_open(tap_device_name, opts.mac);
    opts.local_ip4 = ipv4_addr;
    opts.local_ip6 = ipv6_addr;
    opts.sock4 = socket_utils_create_ipv4_udp_socket(port);
    opts.sock6 = socket_utils_create_ipv6_udp_socket(
        port, if_nametoindex(tap_device_name)
    );

    // configure the tap device
    tap_set_ipv4_addr(ipv4_addr, 24);
    tap_set_ipv6_addr(ipv6_addr, 64);
    tap_set_mtu(MTU);
    tap_set_base_flags();
    tap_set_up();
    peerlist_set_local_p(client_id, ipv4_addr, ipv6_addr);

    // drop root privileges and set to nobody
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

#undef BAD_ARG
