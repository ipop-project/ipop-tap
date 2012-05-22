
#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <openssl/ssl.h>
#include <openssl/crypto.h>
#include <openssl/err.h>

#include <translator.h>
#include <peerlist.h>
#include <headers.h>
#include <svpn.h>
#include <dtls.h>

#include "bss_fifo.h"

enum types { CLIENT, SERVER };

typedef struct peer {
    SSL_CTX *ctx;
    SSL *ssl;
    BIO *dbio;
    BIO *inbio;
    int sock; // This is an IPv4 socket. IPv6 security will be handled by IPSec.
} peer_t;

static peer_t _peer;

static int
verify_callback(int ok, X509_STORE_CTX *ctx)
{
    printf("verify called\n");
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
    peer->inbio = BIO_new_fifo(20, 1700);

    // struct timeval timeout;
    // timeout.tv_usec = 250000;
    // timeout.tv_sec = 0;

    //BIO_ctrl(peer->inbio, BIO_CTRL_DGRAM_SET_RECV_TIMEOUT, 0, &timeout);
    SSL_set_bio(peer->ssl, peer->inbio, peer->dbio);

    if (type == CLIENT) {
        SSL_set_connect_state(peer->ssl);
    }
    else {
        SSL_set_accept_state(peer->ssl);
    }

    SSL_set_options(peer->ssl, SSL_OP_NO_QUERY_MTU);
    peer->ssl->d1->mtu = 1500;
    return 0;
}

int
init_dtls(thread_opts_t *opts)
{
    ERR_load_crypto_strings();
    ERR_load_SSL_strings();
    SSL_library_init();

    _peer.sock = opts->sock4;
    init_peer(CLIENT, &_peer);
    return 0;
}

int
start_dtls_client(void *data)
{
    thread_opts_t *opts = (thread_opts_t *) data;
    int tap = opts->tap;

    int rcount;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);

    unsigned char buf[BUFLEN];
    unsigned char dec_buf[BUFLEN];
    // unsigned char key[KEY_SIZE] = { 0 };
    // unsigned char iv[KEY_SIZE] = { 0 };
    unsigned char p2p_addr[ADDR_SIZE] = { 0 };
    char source_id[KEY_SIZE] = { 0 };
    char dest_id[KEY_SIZE] = { 0 };
    char source[4];
    char dest[4];

    memset(&addr, 0, addr_len);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(opts->dtls_port);
    addr.sin_addr.s_addr = inet_addr(opts->dtls_ip);

    BIO_ctrl(_peer.dbio, BIO_CTRL_DGRAM_SET_PEER, 0, &addr);
    SSL_do_handshake(_peer.ssl);

    while (1) {
        if ((rcount = SSL_read(_peer.ssl, dec_buf, BUFLEN)) < 0) {
            fprintf(stderr, "SSL_read failed\n");
            return -1;
        }

        printf("%d %s\n", rcount, dec_buf);

        get_headers(dec_buf, source_id, dest_id, p2p_addr);

        if (get_source_info_by_addr((char *)p2p_addr, source, dest)) {
            fprintf(stderr, "dtls info not found\n");
            continue;
        }

        rcount -= BUF_OFFSET;
        memcpy(buf, dec_buf + BUF_OFFSET, rcount);
        translate_packet(buf, source, dest, rcount);

        if (translate_headers(buf, source, dest, opts->mac, rcount) < 0) {
            fprintf(stderr, "dtls translate error\n");
            continue;
        }

        if (write(tap, buf, rcount) < 0) {
            fprintf(stderr, "dtls write to tap error\n");
            break;
        }
        printf("T << %d %x %x\n", rcount, buf[32], buf[33]);
    }
    return 0;
}

int
svpn_dtls_send(const unsigned char *buf, int len)
{
    return SSL_write(_peer.ssl, buf, len);
}

int
svpn_dtls_process(const unsigned char *buf, int len)
{
    return BIO_write(_peer.inbio, buf, len);
}
