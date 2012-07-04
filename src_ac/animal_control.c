
/**
 * A tool for controlling racoon that runs as root, but can be controlled by
 * non-root users, configuring _only_ the IPv6 subnet assigned to SocialVPN.
 * Since this tool can potentially be accessed by non-root users the code is
 * kept to an absolute minimum: security, reability, and simplicity are treated
 * as far more important than performance.
 *
 * Note that this tool will allow any malicious or crafty user on your system to
 * configure SocialVPN addresses in a (limited) way.
 *
 * Some documentation on how to do what we are trying to do can be found here:
 *     http://www.netbsd.org/docs/network/ipsec/rasvpn.html
 *     http://qnx.com/developers/docs/6.4.1/neutrino/utilities/r/racoon.conf
 *     http://www.kame.net/racoon/racoon.conf.5
 * 
 * In addtion to the racoon.conf file, we also use a combination of setkey and
 * racoonctl (forked into subprocesses) to do some of our dirty work for us.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h> // used to handle ctrl+c
#include <sys/file.h> // used by flock()
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <endian.h>
#include <linux/limits.h> // provides PATH_MAX

struct in6_addr base_addr;
char *base_addr_p, *racoon_conf_path;
int base_prefix_len;
char *privkeyfile_path; // our private key

#define RACOON_CONF_WARNING "\n" \
    "# WARNING: The block below is automatically generated for use with\n" \
    "#          SocialVPN by the animal_control daemon. It will be removed\n" \
    "#          when SocialVPN quits. Do not attempt to edit this warning\n" \
    "#          or anything below it in this file.\n\n"
#define RACOON_CERTS_DIR_PATH "/etc/racoon/svpn_certs"
#define RACOON_PORT 500
#define MAX_PEERS 500
#define MAX_INPUT_LEN 300
#define MAX_INPUT_ARGS 20

struct racoon_peer {
    char address_p[8*5];
    struct in6_addr address;
    char *pubkeyfile_path; // Their public key
};

struct racoon_peer racoon_peerlist[MAX_PEERS];
int racoon_peerlist_len = 0;

// function prototypes go here:
int main(int argc, const char **argv);
void command_add_peer(const char *addr_p, const char *pubkeyfile_path);
int verify_ipv6_addr(const char *input);
int verify_path(const char *input);
int setkey_associate(const char *addr_p);
int racoon_init();
int racoon_cleanup();
int racoon_ctl(const char *command);
int racoon_update();
void main_signal_handler(int signal);

/**
 * We can assume all arguments received here are safe, as we receive them from
 * root. Furthermore, as svpn is passing them to us (not a user), we don't need
 * to do any real error checking with the arguments.
 *
 * Data from stdin, however, is not to be considered safe. That data could be
 * coming from just about anyone on the system.
 *
 * Note: When using this with svpn, one should set the forwarding option in
 * /proc for IPv6 on the network device. This can be done with:
 *     tap_set_ipv6_proc_option("forwarding", "1");
 * from <tap.h>.
 */
int
main(int argc, const char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "Error: Bad arguments\n");
        printf("Usage: %s IPV6_ADDR PREFIX RACOON_CONF_PATH LOCAL_PRIV_KEY\n",
               argv[0]);
        return -1;
    }
    // argv[1] should be our assigned local IPv6 address
    inet_pton(AF_INET6, argv[1], &(base_addr));
    base_addr_p = malloc(strlen(argv[1]) + 1);
    strcpy(base_addr_p, argv[1]);
    // argv[2] should be the prefix length in bits
    base_prefix_len = atoi(argv[2]);
    // argv[3] should be the location of racoon.conf
    // (usually /etc/racoon/racoon.conf)
    racoon_conf_path = malloc(strlen(argv[3]) + 1);
    strcpy(racoon_conf_path, argv[3]);
    racoon_cleanup(); // we don't really care if initial cleanup fails
    racoon_init();
    // argv[4] should be the location of our private keyfile
    privkeyfile_path = malloc(PATH_MAX + 1);
    realpath(argv[4], privkeyfile_path);

    // Catch a ctrl+c at least letting us clean up racoon.conf
    struct sigaction signal_handler_action;
    signal_handler_action.sa_handler = main_signal_handler;
    sigemptyset(&signal_handler_action.sa_mask);
    signal_handler_action.sa_flags = 0;
    sigaction(SIGINT, &signal_handler_action, NULL);


    // Run the REPL. This is a pretty dumb parser, based on Pierre's original
    // svpn REPL, but it should be easy to understand and probably isn't
    // vulnerable to stuff like buffer overflows. Example usage:
    //    command_name optional_arg optional_arg optional_arg
    // As the parser is really dumb, multiple spaces will create empty
    // arguments. Also, there is no way of escaping spaces, sorry!
    // 
    // Every command should give out one line of output, beginning with "pass"
    // or "fail". The text after "fail" will give an human-readable
    // explaination. The text after pass (if there is any) will be space
    // separated result information. To ensure you read the proper results back
    // in the proper order, make sure you only control this daemon from one
    // process. For simplicity, this data all gets written to stdout. stderr
    // will  not be used by this daemon (except when there is an issue with
    // initializing arguments)
    char buffer[MAX_INPUT_LEN];
    char* command;
    char* command_argv[MAX_INPUT_ARGS];
    int ready = 1; // is 0 if the last line did not end with newline (avoids io
                   // sync issues if we accidentally gave too long of an input)
    while (fgets(buffer, MAX_INPUT_LEN, stdin) != NULL) {
        // fgets guarentees null termination except on error or EOF (given by a
        // NULL return value).
        int i = 0; int command_argc = 0;
        command = buffer;
        while (buffer[i] != '\0' && buffer[i] != '\n' &&
                                                command_argc < MAX_INPUT_ARGS) {
            if (buffer[i] == ' ' || buffer[i] == '\t') {
                buffer[i] = '\0';
                command_argv[command_argc++] = buffer + i + 1;
            }
            i++;
        }
        int was_ready = ready;
        ready = (buffer[i] == '\n');
        if (!was_ready) continue;
        buffer[i] = '\0'; // replace the ending newline
        if (strcmp(command, "add_peer") == 0 && command_argc == 2)
            command_add_peer(command_argv[0], command_argv[1]);
        else
            printf("fail Bad command or argument count for '%s' (%d args)\n",
                   command, command_argc);
    }
    
#ifdef DEBUG
    printf("debug Got EOF or error. Exiting.\n");
#endif

    // cleanup and exit
    racoon_cleanup();
    for (int i = 0; i < racoon_peerlist_len; i++) {
        free(racoon_peerlist[i].pubkeyfile_path);
    }
    free(base_addr_p);
    free(racoon_conf_path);
    free(privkeyfile_path);
    return 0;
}

void
main_signal_handler(int signal) {
#ifdef DEBUG
    printf("debug Caught signal %d. Exiting.\n", signal);
#endif
    racoon_cleanup();
    exit(1);
}

void
command_add_peer(const char *addr_p, const char *pubkeyfile_path)
{
    // check that we have room
    if (racoon_peerlist_len == MAX_PEERS) {
        printf("fail Internal peerlist is already full.\n");
        return;
    }
    // check that it is a valid address within our subnet
    if (!verify_ipv6_addr(addr_p)) {
        printf("fail Bad IPv6 address format: '%s'\n", addr_p);
        return;
    }
    // check that the rsa public key path follows our restrictions
    if (!verify_path(pubkeyfile_path)) {
        printf("fail Bad RSA public key file path '%s' for peer.\n",
               pubkeyfile_path);
    }
    // check that it isn't already in our peerlist
    struct in6_addr addr;
    inet_pton(AF_INET6, addr_p, &addr);
    for (int i = 0; i < racoon_peerlist_len; i++) {
        if (memcmp(&addr, &(racoon_peerlist[i].address),
                                                sizeof(struct in6_addr)) == 0) {
            printf("fail Peer already exists in internal peerlist.\n");
            return;
        }
    }

#ifdef DEBUG
    printf("debug Adding peer '%s' to peerlist.\n", addr_p);
#endif
    // add the peer to our peerlist
    struct racoon_peer peer;
    strcpy(peer.address_p, addr_p);
    memcpy(&(peer.address), &addr, sizeof(struct in6_addr));
    
    // peer.pubkeyfile_path = malloc(strlen(pubkeyfile_path) + 1);
    peer.pubkeyfile_path = malloc(PATH_MAX + 1);
    if (realpath(pubkeyfile_path, peer.pubkeyfile_path) == NULL) {
        free(peer.pubkeyfile_path);
        printf("fail Could not expand relative public key path.");
        return;
    }
    
    racoon_peerlist[racoon_peerlist_len++] = peer;
    
    // write the changes to the peerlist out
    if (racoon_update() != 0) {
        printf("fail Update of racoon configuration file failed.\n");
        return;
    }

#ifdef DEBUG
    printf("debug Associating with setkey.\n");
#endif
    // set us up with setkey
    if (setkey_associate(addr_p) != 0) {
        printf("fail Call to setkey failed.\n");
        return;
    }
    printf("pass\n");
}

/**
 * Returns 0 if the verification fails, 1 if it is valid.
 */
int
verify_path(const char *input)
{
#ifdef DEBUG
    printf("debug Verifying file path '%s'\n", input);
#endif
    if (strlen(input) >= 255) // path too long
        return 0;
    // As we don't have a good way of escaping racoon.conf input, we'll (sadly)
    // just have to greatly limit the character set
    int i = -1;
    while (input[++i] != '\0') {
        if (input[i] >= 'a' && input[i] <= 'z') continue;
        if (input[i] >= 'A' && input[i] <= 'Z') continue;
        if (input[i] >= '0' && input[i] <= '9') continue;
        if (input[i] == '_' || input[i] == '-' || input[i] == ' ') continue;
                                         // we quote strings, so spaces are safe
        if (input[i] == '/' || input[i] == '.') continue;
        return 0;
    }
    return 1;
}

/**
 * Takes a completely untrusted string input (assuming only that it is properly
 * null-terminated) and tells us if it is a valid IPv6 address within our
 * controlled subnet.
 *
 * Returns 0 if the verification fails, 1 if it is valid.
 */
int
verify_ipv6_addr(const char *input)
{
#ifdef DEBUG
    printf("debug Verifying IPv6 Address: '%s'\n", input);
#endif
    struct in6_addr addr;
    if (strlen(input) > 39) {
        return 0; // to long to be a valid IPv6 address: we fail!
    }
    if (inet_pton(AF_INET6, input, &addr) != 1) { // is 1 on success
        return 0; // Not a valid IPv6 address: we fail!
    }
    // check the prefix
    // we have to perform some funky handling for byte order here
    int flip_endian = 0; if (ntohs((short)1) != 1) flip_endian = 1;

    // check the address prefix bit-by-bit for base_prefix_len bits
    for (int i = 0; i < base_prefix_len; i++) {
        int byte_index = i/8;
        unsigned char byte_mask = 0x01 << flip_endian ? 8-(i%8) : i%8;
        if ((base_addr.s6_addr[byte_index] & byte_mask) !=
                                       (addr.s6_addr[byte_index] & byte_mask)) {
#ifdef DEBUG
            printf("debug Failure on bit %d\n", i);
#endif
            return 0; // invalid prefix bit: we fail!
        }
    }
    return 1; // no tests failed: we should be good.
}

int
setkey_associate(const char *addr_p)
{
    // forks setkey, writes commands to stdin, kills setkey
    FILE *setkey_pipe = popen("setkey -c > /dev/null", "w");
#ifdef DEBUG
    printf("debug Called popen with setkey -c\n");
#endif
    if (setkey_pipe == NULL) return -1;
    fprintf(setkey_pipe,
            "spdadd %s %s any -P out ipsec esp/transport//require;\n",
            base_addr_p, addr_p);
    fprintf(setkey_pipe,
            "spdadd %s %s any -P in ipsec esp/transport//require;\n",
            addr_p, base_addr_p);
#ifdef DEBUG
    printf("debug Wrote commands to setkey.\n");
#endif
    pclose(setkey_pipe);
    return 0;
}

/**
 * Opens the racoon.conf file, and appends RACOON_CONF_WARNING. The warning can
 * then (assuming the user actually follows the warning message)
 */
int
racoon_init()
{
#ifdef DEBUG
    printf("debug Initializing racoon.conf file.\n");
#endif
    FILE *racoon_conf = fopen(racoon_conf_path, "a");
    if (racoon_conf == NULL) return -1;
    if (fputs(RACOON_CONF_WARNING, racoon_conf) == EOF) return -1;
    if (fclose(racoon_conf) != 0) return -1;
    return 0;
}

int
racoon_cleanup()
{
#ifdef DEBUG
    printf("debug Cleaning the racoon.conf file.\n");
#endif
    // use a buffered FILE for finding where our RACOON_CONF_WARNING is
    FILE *racoon_conf = fopen(racoon_conf_path, "r");
    size_t block_size = strlen(RACOON_CONF_WARNING);
    char *buffer[block_size]; // not null terminated
    int found = 0; int index = 0;
    while (fread(buffer, sizeof(char), block_size, racoon_conf) == block_size) {
        // fread does not give null-terminated strings, but memcmp is safe
        if (memcmp(buffer, RACOON_CONF_WARNING, block_size) == 0) {
            found = 1;
            break;
        }
        index++;
        fseek(racoon_conf, index, SEEK_SET);
    }
    // we need a raw file descriptor instead now to use ftruncate
    fclose(racoon_conf);
    if (!found) {
        return -1;
    }
    int raccon_conf_fd = open(racoon_conf_path, O_WRONLY);
    ftruncate(raccon_conf_fd, index);
    close(raccon_conf_fd);
    return 0;
}

int
racoon_ctl(const char *command)
{
    // TODO: Actually open the unix socket connection ourselves and do this
    char command_system[strlen(command) + strlen("racoonctl ") + 1];
    sprintf(command_system, "racoonctl %s", command);
    system(command_system);
    return 0; // normally we'd return the system return code, but the version of
              // racoonctl in debian squeeze seems to give back 1 no matter what
}

int
racoon_update()
{
    // Aquire an advisory lock on the file. May block.
    if (racoon_cleanup() != 0) return -1;
    if (racoon_init() != 0) return -1;

    FILE *racoon_conf = fopen(racoon_conf_path, "a");
    if (racoon_conf == NULL) return -1;
    
    // actual configuration writing would happen here
    for (int i = 0; i < racoon_peerlist_len; i++) {
        struct racoon_peer *peer = &racoon_peerlist[i];
        fprintf(racoon_conf,
            "remote %s {\n"
            "    # remote_address %s;\n"
            "    nat_traversal off;\n" // svpn does this for us
            "    exchange_mode aggressive;\n" // The documentation is vague here
            "    my_identifier address;\n"    // Use the address for their id
            "    peers_identifier address;\n" // Do a simple id check first
            "    verify_identifier on;\n"     // same as above
            "    certificate_type plain_rsa \"%s\";\n" // path to our privkey
            "    peers_certfile plain_rsa \"%s\";\n" // path to their pubkey
            "    generate_policy on;\n" // used when responding (???)
            "    proposal {\n"
            "        encryption_algorithm aes;\n"
            "        hash_algorithm sha256;\n"
            "        authentication_method rsasig;\n" // Used with plain_rsa
            "        dh_group 2;\n" // not really sure what this is doing?
            "    }\n"
            "}\n\n", peer->address_p, peer->address_p, privkeyfile_path,
                     peer->pubkeyfile_path
        );
        // write sainfo here
        fprintf(racoon_conf,
            "sainfo (address %s[%d] any address %s[%d] any) {\n"
            "    lifetime time 1 hour;\n" // reauthenticate each hour
            "    authentication_algorithm hmac_sha1;\n"
            "    encryption_algorithm aes;\n"
            "    compression_algorithm deflate;\n" // yes please!
            "}\n\n", base_addr_p, RACOON_PORT, peer->address_p, RACOON_PORT
        );
    }
    
    fclose(racoon_conf);
#ifdef DEBUG
    printf("debug Reloading configuration into racoon.\n");
#endif
    if (racoon_ctl("reload-config") != 0) return -1;
    return 0;
}
