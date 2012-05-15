
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <openssl/ssl.h>
#include <openssl/crypto.h>
#include <openssl/err.h>

#include <bss_fifo.h>

enum types { CLIENT, SERVER };

typedef struct peer {
    SSL_CTX *ctx;
    SSL *ssl;
    BIO *dbio;
    BIO *inbio;
    int sock;
} peer_t;

static int
create_udp_socket(uint16_t port)
{
    int sock, optval = 1;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 1) {
        fprintf(stderr, "socket failed\n");
        return -1;
    }

    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    memset(&addr, 0, addr_len);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr*) &addr, addr_len) < 0) {
        fprintf(stderr, "bind failed\n");
        close(sock);
        return -1;
    }
    return sock;
}

static int
verify_callback(int ok, X509_STORE_CTX *ctx)
{
    fprintf(stderr, "verify called\n");
    return 1;
}

static SSL_CTX *
create_dtls_context(int type, const char *key_file)
{
    SSL_CTX *ctx;

    if (type == CLIENT) {
        ctx = SSL_CTX_new(DTLSv1_client_method());
    }
    else {
        ctx = SSL_CTX_new(DTLSv1_server_method());
    }

    if (!SSL_CTX_use_certificate_file(ctx, key_file,SSL_FILETYPE_PEM)
        || !SSL_CTX_use_PrivateKey_file(ctx, key_file,SSL_FILETYPE_PEM)
        || !SSL_CTX_check_private_key(ctx)) {
        fprintf(stderr, "Error setting up SSL_CTX\n");
        ERR_print_errors_fp(stderr);
        return NULL;
    }

    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, verify_callback);
    SSL_CTX_set_read_ahead(ctx, 1);
    return ctx;
}

static int
init_peer(int type, peer_t *peer)
{
    if (type == CLIENT) {
        peer->ctx = create_dtls_context(CLIENT, "peer1.pem");
    }
    else {
        peer->ctx = create_dtls_context(SERVER, "peer2.pem");
    }

    peer->ssl = SSL_new(peer->ctx);
    peer->dbio = BIO_new_dgram(peer->sock, BIO_NOCLOSE);
    peer->inbio = BIO_new_fifo(20, 1400);

    struct timeval timeout;
    timeout.tv_usec = 250000;
    timeout.tv_sec = 0;

    //BIO_ctrl(peer->dbio, BIO_CTRL_DGRAM_SET_RECV_TIMEOUT, 0, &timeout);
    SSL_set_bio(peer->ssl, peer->dbio, peer->dbio);

    if (type == CLIENT) {
        SSL_set_connect_state(peer->ssl);
    }
    else {
        SSL_set_accept_state(peer->ssl);
    }

    SSL_set_options(peer->ssl, SSL_OP_NO_QUERY_MTU);
    peer->ssl->d1->mtu = 1200;
    return 0;
}

static void *
send_thread(void *data)
{
    peer_t *peer = (peer_t *) data;

    char buf[2048];
    int r;

    while (1) {
        if ((r = read(0, buf, sizeof(buf))) < 0) {
            fprintf(stderr, "read failed\n");
            break;
        }

        fprintf(stderr, "%d %s\n", r, buf);

        if ((r = SSL_write(peer->ssl, buf, r)) < 0) {
            fprintf(stderr, "ssl write failed\n");
            break;
        }
    }
    pthread_exit(NULL);
}

int
start_server()
{
    int r = 0;
    char buf[2048];
    peer_t peer;

    peer.sock = create_udp_socket(12345);
    init_peer(SERVER, &peer);

    SSL_do_handshake(peer.ssl);
    
    pthread_t s_thread;
    pthread_create(&s_thread, NULL, send_thread, &peer);

    while (1) {
        if ((r = SSL_read(peer.ssl, buf, sizeof(buf))) < 0) {
            fprintf(stderr, "ssl read error\n");
            exit(1);
        }

        if ((r = write(1, buf, r)) < 0) {
            fprintf(stderr, "write failed\n");
            exit(1);
        }
    }
}

int
main(int argc, char *argv[])
{
    ERR_load_crypto_strings();
    ERR_load_SSL_strings();
    SSL_library_init();

    start_server();
    return 0;
}
