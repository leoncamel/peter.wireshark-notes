/*
 * Dumps master keys for OpenSSL clients to file. The format is documented at
 * https://developer.mozilla.org/en-US/docs/Mozilla/Projects/NSS/Key_Log_Format
 *
 * Copyright (C) 2014 Peter Wu <peter@lekensteyn.nl>
 * Licensed under the terms of GPLv3 (or any later version) at your choice.
 *
 * Usage:
 *  cc sslkeylog.c -shared -o libsslkeylog.so -fPIC -ldl
 *  SSLKEYLOGFILE=premaster.txt LD_PRELOAD=./libsslkeylog.so openssl ...
 */
#define _GNU_SOURCE /* for RTLD_NEXT */
#include <dlfcn.h>
#include <openssl/ssl.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define PREFIX      "CLIENT_RANDOM "
#define PREFIX_LEN  (sizeof(PREFIX) - 1)
#define FIRSTLINE   "# SSL key logfile generated by sslkeylog.c\n"
#define FIRSTLINE_LEN (sizeof(FIRSTLINE) - 1)

static int (*_SSL_connect)(SSL *ssl);
static int key_logfile_fd = -1;

static inline void put_hex(char *buffer, int pos, char c)
{
    unsigned char c1 = ((unsigned char) c) >> 4;
    unsigned char c2 = c & 0xF;
    buffer[pos] = c1 < 10 ? '0' + c1 : 'A' + c1 - 10;
    buffer[pos+1] = c2 < 10 ? '0' + c2 : 'A' + c2 - 10;
}

static void dump_to_fd(SSL *ssl, int fd)
{
    int pos, i;
    char line[PREFIX_LEN + 2 * SSL3_RANDOM_SIZE + 1 +
              2 * SSL_MAX_MASTER_KEY_LENGTH + 1];

    memcpy(line, PREFIX, PREFIX_LEN);
    pos = PREFIX_LEN;
    /* Client Random for SSLv3/TLS */
    for (i = 0; i < SSL3_RANDOM_SIZE; i++) {
        put_hex(line, pos, ssl->s3->client_random[i]);
        pos += 2;
    }
    line[pos++] = ' ';
    /* Master Secret (size is at most SSL_MAX_MASTER_KEY_LENGTH) */
    for (i = 0; i < ssl->session->master_key_length; i++) {
        put_hex(line, pos, ssl->session->master_key[i]);
        pos += 2;
    }
    line[pos++] = '\n';
    /* Write at once rather than using buffered I/O. Perhaps there is concurrent
     * write access so do not write hex values one by one. */
    write(fd, line, pos);
}

static void init_key_logfile(void)
{
    if (key_logfile_fd >= 0)
        return;

    const char *filename = getenv("SSLKEYLOGFILE");
    if (filename) {
        key_logfile_fd = open(filename, O_WRONLY | O_APPEND | O_CREAT, 0644);
        if (key_logfile_fd >= 0 && lseek(key_logfile_fd, 0, SEEK_END) == 0) {
            /* file is opened successfully and there is no data (pos == 0) */
            write(key_logfile_fd, FIRSTLINE, FIRSTLINE_LEN);
        }
    }
}

int SSL_connect(SSL *ssl)
{
    if (!_SSL_connect) {
        _SSL_connect = (int (*)(SSL *ssl)) dlsym(RTLD_NEXT, "SSL_connect");
    }
    int ret = _SSL_connect(ssl);
    if (ret >= 0) {
        init_key_logfile();
        if (key_logfile_fd >= 0) {
            dump_to_fd(ssl, key_logfile_fd);
        }
    }
    return ret;
}