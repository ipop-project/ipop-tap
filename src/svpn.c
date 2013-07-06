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
#include <linux/limits.h>
#include <pwd.h>

#include <jansson.h>

#include "translator.h"
#include "peerlist.h"
#include "tap.h"
#include "headers.h"
#include "socket_utils.h"
#include "packetio.h"
#include "svpn.h"

static int process_inputs(thread_opts_t *opts, char *inputs[]);
static int generate_ipv6_address(char *prefix, unsigned short prefix_len,
                                 char *address);
static int add_peer_json(json_t *peer_json);
static void main_help(const char *executable);
static void main_bad_arg(const char *executable, const char *arg);
int main(int argc, const char **argv);

static int
process_inputs(thread_opts_t *opts, char **inputs)
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
        printf("id = %s ip = %s port = %s\n", id,
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

static int
add_peer_json(json_t* peer_json) {
    json_t *id_json = json_object_get(peer_json, "id");
    json_t *ipv4_json = json_object_get(peer_json, "ipv4_addr");
    json_t *ipv6_json = json_object_get(peer_json, "ipv6_addr");
    json_t *port_json = json_object_get(peer_json, "port");

    if (id_json == NULL || ipv4_json == NULL || ipv6_json == NULL ||
                                                            port_json == NULL) {
        return -1;
    }

    const char *id = json_string_value(id_json);
    const char *dest_ipv4 = json_string_value(ipv4_json);

    // get the ipv6 address, or generate one randomly if there isn't one
    const char *dest_ipv6;
    char dest_ipv6_m[40];
    if (ipv6_json != NULL) dest_ipv6 = json_string_value(ipv6_json);
    else { // if none is supplied, generate one randomly
        generate_ipv6_address("fd50:0dbc:41f2:4a3c", 64, dest_ipv6_m);
        dest_ipv6 = dest_ipv6_m; // lets us make dest_ipv6 a const
    }

    uint16_t port = json_integer_value(port_json);

    if (id == NULL || dest_ipv4 == NULL || dest_ipv6 == NULL || port == 0)
        return -1;

#ifdef DEBUG
    printf("Added peer with id: %s\n", id);
#endif

    // peerlist_add does the memcpy of everything for us
    peerlist_add_p(id, dest_ipv4, dest_ipv6, port);
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
    printf("    -i, --id:      The name (id) to give to the local peer. Max\n"
           "                   length of %d characters. (default: 'local')\n",
                               ID_SIZE-1);
    printf("    -4:            The virtual IPv4 address to use on the tap.\n"
           "                   This represents the localhost, and all other\n"
           "                   peers are given an IP derived by adding to\n"
           "                   this. The last block must be a 3-digit number,\n"
           "                   such as '100'. (default: '172.31.0.100')\n");
    printf("    -6:            The virtual IPv6 address to use on the tap\n"
           "                   device. Must begin with 'fd50:0dbc:41f2:4a3c'.\n"
           "                   (default: randomly generated)\n");
    printf("    -p, --port:    The UDP port used by svpn to send and receive\n"
           "                   packet data. (default: 5800)\n");
    printf("    -t, --tap:     The name of the system tap device to use. If\n"
           "                   you're using multiple clients on the same\n"
           "                   machine, device names must be different. Max\n"
           "                   length of %d characters. (default: 'svpn0')\n",
                               IFNAMSIZ-1);
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

/**
 * Performs (in order) argument processing, configuration file processing, sets
 * default arguments for any settings left unset, spawns the threads for sending
 * and receiving, and then reads control commands from stdin until exit.
 */
int
main(int argc, const char *argv[])
{
    srand(time(NULL)); // set up the random number generator

    // mark the various configurable options as unset or as their defaults
    // (dependent on how the option must be manipulated)
    char config_file[PATH_MAX]; strcpy(config_file, "config.json");
    char client_id[ID_SIZE]; client_id[0] = '\0';
    char ipv4_addr[4*4]; ipv4_addr[0] = '\0';
    char ipv6_addr[8*5]; ipv6_addr[0] = '\0';
    uint16_t port = 0;
    char tap_device_name[IFNAMSIZ]; tap_device_name[0] = '\0';
    int verbose = 0;

    // Ideally we'd define defaults first, then configuration file stuff, then
    // command-line arguments, but unfortunately the configuration file can be
    // set in the arguments, so they must be parsed first.

    // read in settings from command line arguments
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-c") == 0 || strcmp(a, "--config") == 0) {
            if (++i < argc && strlen(argv[i]) < sizeof(config_file)) {
                strcpy(config_file, argv[i]);
            } else BAD_ARG
        } else if (strcmp(a, "-i") == 0 || strcmp(a, "--id") == 0) {
            if (++i < argc && strlen(argv[i]) < sizeof(client_id)) {
                strcpy(client_id, argv[i]);
            } else BAD_ARG
        } else if (strcmp(a, "-4") == 0) {
            if (++i < argc && strlen(argv[i]) < sizeof(ipv4_addr)) {
                strcpy(ipv4_addr, argv[i]);
            } else BAD_ARG
        } else if (strcmp(a, "-6") == 0) {
            if (++i < argc && strlen(argv[i]) < sizeof(ipv6_addr)) {
                strcpy(ipv6_addr, argv[i]);
            } else BAD_ARG
        } else if (strcmp(a, "-p") == 0 || strcmp(a, "--port") == 0) {
            if (++i < argc) {
                port = (uint16_t) atoi(argv[i]);
            } else BAD_ARG
        } else if (strcmp(a, "-t") == 0 || strcmp(a, "--tap") == 0) {
            if (++i < argc && strlen(argv[i]) < sizeof(tap_device_name)) {
                strcpy(tap_device_name, argv[i]);
            } else BAD_ARG
        } else if (strcmp(a, "-v") == 0 || strcmp(a, "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            main_help(argv[0]);
            return 0;
        } else BAD_ARG
    }


    json_t *config_json = NULL;
    // read in settings from the json config file
    if (access(config_file, R_OK) == 0) {
        json_error_t config_json_err;
        config_json = json_load_file(config_file, 0, &config_json_err);
        if (config_json == NULL) {
            fprintf(stderr, "JSON Error: %s (%d, %d) %s: %s\n", config_file,
                    config_json_err.line, config_json_err.column,
                    config_json_err.source, config_json_err.text);
            return -1;
        } else {
            if (client_id[0] == '\0') {
                const char *str =
                    json_string_value(json_object_get(config_json, "id"));
                if (str != NULL) {
                    strncpy(client_id, str, sizeof(client_id)-1);
                    client_id[sizeof(client_id)-1] = '\0';
                }
            }
            if (ipv4_addr[0] == '\0') {
                const char *str = json_string_value(
                    json_object_get(config_json, "ipv4_addr"));
                if (str != NULL) {
                    strncpy(ipv4_addr, str, sizeof(ipv4_addr)-1);
                    ipv4_addr[sizeof(ipv4_addr)-1] = '\0';
                }
            }
            if (ipv6_addr[0] == '\0') {
                const char *str = json_string_value(
                    json_object_get(config_json, "ipv6_addr"));
                if (str != NULL) {
                    strncpy(ipv6_addr, str, sizeof(ipv6_addr)-1);
                    ipv6_addr[sizeof(ipv6_addr)-1] = '\0';
                }
            }
            if (port == 0) {
                json_t *port_json = json_object_get(config_json, "port");
                if (port_json != NULL) {
                    port = (uint16_t) json_integer_value(port_json);
                    // gives 0 on error, which (fortunately) is what we want
                }
            }
            if (tap_device_name[0] == '\0') {
                const char *str =
                    json_string_value(json_object_get(config_json, "tap_name"));
                if (str != NULL) {
                    strncpy(tap_device_name, str, sizeof(tap_device_name));
                    tap_device_name[sizeof(tap_device_name)-1] = '\0';
                }
            }
        }
    } else if (verbose) {
        fprintf(stderr,
                "Warning: Configuration file '%s' not found or not openable.\n",
                config_file);
    }

    // set uninitialized values to their defaults
    if (client_id[0] == '\0') {
        fprintf(stderr, "Warning: An id was not explicitly set. Falling back "
                        "to 'local'. If no id is set, collisions are likely to "
                        "occur.\n");
        strcpy(client_id, "local");
    }
    if (ipv6_addr[0] == '\0')
        generate_ipv6_address("fd50:0dbc:41f2:4a3c", 64, ipv6_addr);
    if (ipv4_addr[0] == '\0') strcpy(ipv4_addr, "172.31.0.100");
    if (port == 0) port = 5800;
    if (tap_device_name[0] == '\0') strcpy(tap_device_name, "svpn0");

    if (verbose) {
        // pretty-print the client configuration
        printf("Configuration Loaded:\n");
        printf("    Id: '%s'\n", client_id);
        printf("    Virtual IPv6 Address: '%s'\n", ipv6_addr);
        printf("    UDP Socket Port: %d\n", port);
        printf("    TAP Virtual Device Name: '%s'\n", tap_device_name);
    }

    // Initialize the peerlist for possible peers we might add
    // This can only be done after we're sure we resolved the ipv4 and ipv6
    // addresses, but it must be done before we add any peers
    peerlist_init(TABLE_SIZE);
    peerlist_set_local_p(client_id, ipv4_addr, ipv6_addr);

    if (json_is_object(config_json)) {
        json_t *peerlist_json = json_object_get(config_json, "peers");
        if (json_is_array(peerlist_json)) {
            for (int i = 0; i < json_array_size(peerlist_json); i++) {
                json_t *peer_json = json_array_get(peerlist_json, i);
                add_peer_json(peer_json);
            }
        }
    }

    // we're completely done with the json, so we can free it
    if (config_json != NULL) {
        json_decref(config_json);
    }

    // write out the threading options to be passed to the runner threads
    thread_opts_t opts;
    opts.tap = tap_open(tap_device_name, opts.mac);
    opts.local_ip4 = ipv4_addr;
    opts.local_ip6 = ipv6_addr;
    opts.sock4 = socket_utils_create_ipv4_udp_socket("0.0.0.0", port);
    opts.sock6 = socket_utils_create_ipv6_udp_socket(
        port, if_nametoindex(tap_device_name)
    );
    opts.send_queue = NULL;
    opts.rcv_queue = NULL;

    // configure the tap device
    tap_set_ipv4_addr(ipv4_addr, 24);
    tap_set_ipv6_addr(ipv6_addr, 64);
    tap_set_mtu(MTU);
    tap_set_base_flags();
    tap_set_up();

    // drop root privileges and set to nobody
    struct passwd * pwd = getpwnam("nobody");
    if (getuid() == 0) {
        if (setgid(pwd->pw_uid) < 0) {
            fprintf(stderr, "setgid failed\n");
            close(opts.sock4); close(opts.sock6);
            tap_close();
            return -1;
        }
        if (setuid(pwd->pw_gid) < 0) {
            fprintf(stderr, "setuid failed\n");
            close(opts.sock4); close(opts.sock6);
            tap_close();
            return -1;
        }
    }

    pthread_t send_thread, recv_thread;
    pthread_create(&send_thread, NULL, udp_send_thread, &opts);
    pthread_create(&recv_thread, NULL, udp_recv_thread, &opts);
#ifndef EN_INPUT
    pthread_join(recv_thread, NULL);
#else
    // sockets do not work after nobody on Android
    int input_socket;
    uint16_t iport = port - 1;
    if ((input_socket = 
             socket_utils_create_ipv4_udp_socket("127.0.0.1", iport)) < 0) {
      fprintf(stderr, "socket failed at %d port\n", iport);
      return -1;
    }

    struct sockaddr_in s_addr;
    socklen_t len = sizeof(s_addr);
    char buf[MAXBUF] = {'0'};;
    char* inputs[5];
    int i, j, n;

    while (1) {
        n = recvfrom(input_socket, buf, sizeof(buf), 0, 
                     (struct sockaddr*) &s_addr, &len);
        if (n < 0) {
            fprintf(stderr, "read failed\n");
            return -1;
        }

        // overwrites last character
        buf[n-1] = ' ';
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
#endif
    return 0;
}

#undef BAD_ARG
