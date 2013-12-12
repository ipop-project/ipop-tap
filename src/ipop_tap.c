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
#include <time.h> // used to generate random seed
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <getopt.h>

#if defined(LINUX) || defined(ANDROID)
#include <limits.h>
#include <pwd.h>
#include <net/if.h>
#endif

#include <jansson.h>

#include "translator.h"
#include "peerlist.h"
#include "tap.h"
#include "headers.h"
#include "socket_utils.h"
#include "packetio.h"
#include "ipop_tap.h"
#include "utils.h"

#if defined(WIN32)
#include "win32_tap.h"
#endif

static int generate_ipv6_address(char *prefix, unsigned short prefix_len,
                                 char *address);
static int add_peer_json(json_t *peer_json);
static void main_help(const char *executable);
int main(int argc, const char **argv);

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
add_peer_json(json_t* peer_json)
{
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

    json_t *route_json = json_object_get(peer_json, "ipv4_route");
    if (route_json != NULL) {
        const char *route_ipv4 = json_string_value(route_json);
        override_base_ipv4_addr_p(route_ipv4);
    }

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
    printf("    -p, --port:    The UDP port used by ipop to send and receive\n"
           "                   packet data. (default: 5800)\n");
    printf("    -t, --tap:     The name of the system tap device to use. If\n"
           "                   you're using multiple clients on the same\n"
           "                   machine, device names must be different. Max\n"
           "                   length of %d characters. (default: 'ipop0')\n",
#if defined(LINUX) || defined(ANDROID)
                               IFNAMSIZ-1);
#elif defined(WIN32)
                               99);
#endif
    printf("    -v, --verbose: Print out extra information about what's\n"
           "                   happening.\n");
}

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
    char client_id[2*ID_SIZE + 1] = { 0 };
    char ipv4_addr[4*4] = { 0 };
    char ipv6_addr[8*5] = { 0 };
    uint16_t port = 0;
#if defined(LINUX) || defined(ANDROID)
    char tap_device_name[IFNAMSIZ] = { 0 };
#elif defined(WIN32)
    char tap_device_name[100] = { 0 };
#endif
    int verbose = 0;

    // Ideally we'd define defaults first, then configuration file stuff, then
    // command-line arguments, but unfortunately the configuration file can be
    // set in the arguments, so they must be parsed first.

    // read in settings from command line arguments
    char* short_options = "c:i:4:6:p:t:vh";

    static const struct option long_options[] = {
        {"config", required_argument, 0, 'c'},
        {"id", required_argument, 0, 'i'},
        {"ipv4", required_argument, 0, '4'},
        {"ipv6", required_argument, 0, '6'},
        {"port", required_argument, 0, 'p'},
        {"tap", required_argument, 0, 't'},
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int current;
    int option_index = 0;

    while ((current = getopt_long(argc,
                                  (char* const *)argv,     
                                  short_options,
                                  long_options,
                                  &option_index)) != -1) {
        switch (current) {
            case 'c':
                strcpy(config_file, optarg);
                break;
                
            case 'i':
                strcpy(client_id, optarg);
                break;
            
            case '4':
                strcpy(ipv4_addr, optarg);
                break;
                
            case '6':
                strcpy(ipv6_addr, optarg);
                break;
                
            case 'p':
                port = (uint16_t)atoi(optarg);
                break;
                
            case 't':
                strcpy(tap_device_name, optarg);
                break;
                
            case 'v':
                verbose = 1;
                break;
                
            case 'h':
                main_help(argv[0]);
                return EXIT_SUCCESS;
                break;
                
            default:
                main_help(argv[0]);
                return EXIT_FAILURE;
        }
        
    }
    
    // consume any stray arguments as unrecognized options
    if (optind < argc) {
        while (optind < argc)
            fprintf(stderr, "%s: unrecognized option: '%s'\n", 
                            argv[0], argv[optind++]);
            
        main_help(argv[0]);
        
        return EXIT_FAILURE;
    }

    // parse the configuration file
    json_t *config_json = NULL;
    if (access(config_file, R_OK) == 0) {
        json_error_t config_json_err;
        config_json = json_load_file(config_file, 0, &config_json_err);
        if (config_json == NULL) {
            fprintf(stderr, "JSON Error: %s (%d, %d) %s: %s\n", config_file,
                    config_json_err.line, config_json_err.column,
                    config_json_err.source, config_json_err.text);
            return EXIT_FAILURE;
        } else {
            if (client_id[0] == '\0') {
                const char *str =
                    json_string_value(json_object_get(config_json, "id"));
                if (str != NULL) {
                    strlcpy(client_id, str, (sizeof client_id)-1);
                }
            }
            
            if (ipv4_addr[0] == '\0') {
                const char *str = json_string_value(
                    json_object_get(config_json, "ipv4_addr"));
                if (str != NULL) {
                    strlcpy(ipv4_addr, str, (sizeof ipv4_addr)-1);
                }
            }
            
            if (ipv6_addr[0] == '\0') {
                const char *str = json_string_value(
                    json_object_get(config_json, "ipv6_addr"));
                if (str != NULL) {
                    strlcpy(ipv6_addr, str, (sizeof ipv6_addr)-1);
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
                    strlcpy(tap_device_name, str, (sizeof tap_device_name)-1);
                }
            }
        }
        
    } else {
        if (verbose)
            fprintf(stderr,
                    "Warning: Cannot read configuration file: '%s'\n",
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
    if (tap_device_name[0] == '\0') strcpy(tap_device_name, "ipop0");

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
    peerlist_init();
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

#if defined(WIN32)
    int iResult;
    WSADATA wsaData;
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != NO_ERROR) {
        fprintf(stderr, "WSAStartup failed with error: %d\n", iResult);
        return -1;
    }
#endif

    // write out the threading options to be passed to the runner threads
    thread_opts_t opts;
#if defined(LINUX) || defined(ANDROID)
    opts.tap = tap_open(tap_device_name, opts.mac);
#elif defined(WIN32)
    opts.win32_tap = open_tap(tap_device_name, opts.mac);
#endif
    opts.local_ip4 = ipv4_addr;
    opts.local_ip6 = ipv6_addr;
    opts.sock4 = socket_utils_create_ipv4_udp_socket("0.0.0.0", port);
#if defined(LINUX) || defined(ANDROID)
    opts.sock6 = socket_utils_create_ipv6_udp_socket(
        port, if_nametoindex(tap_device_name)
    );
#endif
    opts.translate = 1;
    opts.send_func = NULL;
    opts.recv_func = NULL;

#if defined(LINUX) || defined(ANDROID)
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
            close(opts.sock4);
            close(opts.sock6);
            tap_close();
            
            return EXIT_FAILURE;
        }
        if (setuid(pwd->pw_gid) < 0) {
            fprintf(stderr, "setuid failed\n");
            close(opts.sock4);
            close(opts.sock6);
            tap_close();
            
            return EXIT_FAILURE;
        }
    }
    pthread_t send_thread, recv_thread;
    pthread_create(&send_thread, NULL, ipop_send_thread, &opts);
    pthread_create(&recv_thread, NULL, ipop_recv_thread, &opts);
    pthread_join(recv_thread, NULL);
#endif
    return EXIT_SUCCESS;
}
