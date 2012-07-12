/**
 * Contains a set of utilities for forking and controlling an instance of the
 * animal_control daemon.
 */

#include <svpn.h>

#ifdef USE_IPV6_IPSEC

#include <animal_control.h>

#ifndef ANIMAL_CONTROL_BIN_NAME
#define ANIMAL_CONTROL_BIN_NAME "animal_control"
#endif
#ifndef RACOON_CONF_PATH
#define RACOON_CONF_PATH "/etc/racoon/racoon.conf"
#endif

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <libgen.h> // gives us the dirname function
#include <linux/limits.h> // gives us PATH_MAX
#include <sys/types.h>
#include <signal.h>

#define ANIMAL_CONTROL_READ_BUFLEN 1024

static int read_pipe[] = {-1, -1};  // parent reads from, child writes to
static FILE *fread_pipe;
static FILE *fwrite_pipe;
static int write_pipe[] = {-1, -1}; // parent writes to, child reads from
static pid_t animal_control_pid = 0;
static char animal_control_read_buffer[ANIMAL_CONTROL_READ_BUFLEN];

#define AC_IS_PASS(line)  (memcmp("pass", (line), 4) == 0)
#define AC_IS_FAIL(line)  (memcmp("fail", (line), 4) == 0)
#define AC_FAIL_MSG(line) (line[4] != '\0' ? line+5 : line+4)

static char *animal_control_get_path();

/**
 * Generates the path to the animal_control binary by assuming it is in the same
 * directory as the current (svpn) binary. Alternatively, if
 * ANIMAL_CONTROL_BIN_PATH is defined, that is simply returned instead.
 *
 * Returns: The path string to animal_control, or NULL if we encountered an
 * error. The returned string is allocated with `malloc` and should be `free`d.
 */
static char *
animal_control_get_path()
{
#ifdef ANIMAL_CONTROL_BIN_PATH
    return strdup(ANIMAL_CONTROL_BIN_PATH);
#else
    char buffer[PATH_MAX+1];
    int len = readlink("/proc/self/exe", buffer,
                       PATH_MAX - strlen(ANIMAL_CONTROL_BIN_NAME) - 1);
    if (len < 0) return NULL;
    // ensure null termination
    buffer[len] = '\0';
    return strdup(
        strcat(strcat(dirname(buffer), "/"), ANIMAL_CONTROL_BIN_NAME)
    );
#endif
}

/**
 * The only function here that needs to be called as root. Starts the
 * animal_control daemon. Returns 0 on success, -1 on failure.
 */
int
animal_control_init(const char* ipv6_addr_p, int prefix,
                    const char *local_privkey_path)
{
    char *animal_control_path = animal_control_get_path();
    if (animal_control_path == NULL) return -1;
    if (pipe(read_pipe) < 0 || pipe(write_pipe) < 0) return -1;
    // pid_t parent_pid = getpid();
    pid_t child_pid = fork(); // We could probably use vfork, but fork works
    if (child_pid == 0) { // child (daemon) process
        dup2(read_pipe[1], STDOUT_FILENO);
        dup2(write_pipe[0], STDIN_FILENO);
        close(read_pipe[0]); //close(read_pipe[1]);
        close(write_pipe[1]); //close(write_pipe[1]);
        char *prefix_str; asprintf(&prefix_str, "%d", prefix);
        // don't worry about free: execl will replace our process image anyways
        execl(animal_control_path, "animal_control", ipv6_addr_p, prefix_str,
              RACOON_CONF_PATH, local_privkey_path, NULL);
        exit(-1); // we should never get to this point
                  // (unless something goes wrong)
    }
    // still the parent process
    close(read_pipe[1]); close(write_pipe[0]);
    if (child_pid == -1) { // fork failed
        close(read_pipe[0]); close(write_pipe[0]);
        return -1;
    }
    // convert our side of the pipe to FILE pointers
    fread_pipe = fdopen(read_pipe[0], "r");
    fwrite_pipe = fdopen(write_pipe[1], "w");

    animal_control_pid = child_pid;
    return animal_control_alive();
}

/**
 * Waits for a response from the animal_control daemon, discarding debug and
 * junk output, accepting only lines starting with "pass" or "fail", and then
 * returning the full line.
 *
 * Returns a value stored in a statically allocated buffer that gets rewritten
 * on subsequent calls. Returns NULL on an error. Returned value will have the
 * ending newline stripped, but is guarenteed to be null-terminated.
 */
static const char *
animal_control_output()
{
    while (1) {
        char *line = fgets(animal_control_read_buffer,
                           ANIMAL_CONTROL_READ_BUFLEN, fread_pipe);
        if (line == NULL) return NULL;
        if (AC_IS_PASS(line) || AC_IS_FAIL(line)) {
            line[strlen(line)-1] = '\0'; // strip trailing newline
            return line;
        }
    }
}

static int
animal_control_send_command(const char *command_line)
{
    if (fprintf(fwrite_pipe, "%s\n", command_line) < 0) {
        strcpy(animal_control_read_buffer,
               "animal_control must have been killed or has crashed");
        animal_control_error = animal_control_read_buffer;
        return -1;
    }
    fflush(fwrite_pipe);
    const char* output = animal_control_output();
    if (output == NULL) return -1;
    if (AC_IS_FAIL(output)) {
        animal_control_error = AC_FAIL_MSG(output);
        return -1;
    }
    return 0;
}

int
animal_control_add_peer(const char *addr_p, const char *pubkeyfile_path)
{
    char *line;
    asprintf(&line, "add_peer %s %s", addr_p, pubkeyfile_path);
    int val = animal_control_send_command(line);
    free(line);
    return val;
}

int
animal_control_remove_peer(const char *addr_p)
{
    char *line;
    asprintf(&line, "remove_peer %s", addr_p);
    int val = animal_control_send_command(line);
    free(line);
    return val;
}

int
animal_control_alive()
{
    return animal_control_send_command("alive");
}

int
animal_control_exit()
{
    if (animal_control_pid == 0) {
        return -1;
    }
    fclose(fread_pipe); fclose(fwrite_pipe);
    animal_control_pid = 0;
    return kill(animal_control_pid, 2); // SIGINT
}

#undef AC_IS_PASS
#undef AC_IS_FAIL
#undef AC_FAIL_MSG
#endif // ifdef USE_IPV6_IPSEC
