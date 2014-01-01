#include "fcrypt.h"

/*
 *  An implementation of the ARCFOUR algorithm
 *
 *  Copyright (C) 2006-2010, Brainspark B.V.
 *
 *  This code is part of PolarSSL (http://www.polarssl.org)
 *  Lead Maintainer: Paul Bakker <polarssl_maintainer at polarssl.org>
 */

#define SWAP_BYTE(a, b) do {unsigned char t; t = (b); (b) = (a); (a) = t; } while (0)

typedef struct {
    int x, y;
    unsigned char m[256];
} arc4_ctx;

static void arc4_init(arc4_ctx *ctx, const unsigned char *key, unsigned int keylen )
{
    int i, j, a;
    unsigned int k;
    unsigned char *m;

    ctx->x = 0;
    ctx->y = 0;
    m = ctx->m;

    for(i = 0; i < 256; i++) {
        m[i] = (unsigned char)i;
    }

    j = k = 0;
    for(i = 0; i < 256; i++, k++) {
        if( k >= keylen ) {
            k = 0;
        }
        a = m[i];
        j = (j + a + key[k]) & 0xFF;
        m[i] = m[j];
        m[j] = (unsigned char)a;
    }
}


static int arc4_crypt(arc4_ctx *ctx, size_t length, unsigned char *buffer)
{
    int x, y, a, b;
    size_t i;
    unsigned char *m;

    x = ctx->x;
    y = ctx->y;
    m = ctx->m;

    for(i = 0; i < length; i++) {
        x = (x + 1) & 0xFF; a = m[x];
        y = (y + a) & 0xFF; b = m[y];

        m[x] = (unsigned char) b;
        m[y] = (unsigned char) a;

        buffer[i] = (unsigned char)(buffer[i] ^ m[(unsigned char)( a + b )]);
    }

    ctx->x = x;
    ctx->y = y;
    return 0;
}

/* 不能保证使用不同的 key 生成打得加密解密转换表一定不同 */
void fcrypt_init_ctx(fcrypt_ctx *fctx, const unsigned char *key, unsigned int keylen)
{
    arc4_ctx ctx;
    arc4_init(&ctx, key, keylen);
    
    unsigned int i, j;
    unsigned char table[256];

    for(i = 0; i < 256; i++) {
        table[i] = i;
        fctx->en_table[i] = i;
    }

    for (j = 0; j < keylen; j++) {
        arc4_crypt(&ctx, 256, table);
        for(i = 0; i < 256; i++) {
            if (table[i] < fctx->en_table[i]) {
                SWAP_BYTE(fctx->en_table[i], fctx->en_table[table[i]]);
            }
        }    
    }
    
    for (i = 0; i < 256; i++) {
        fctx->de_table[fctx->en_table[i]] = i;
    }
}


void fcrypt_encrypt(fcrypt_ctx *fctx, unsigned char *buf, int len)
{
    unsigned char *end = buf + len;
    while (buf < end) {
        *buf = fctx->en_table[*buf];
        buf++;
    }
}

void fcrypt_decrypt(fcrypt_ctx *fctx, unsigned char *buf, int len)
{
    unsigned char *end = buf + len;
    while (buf < end) {
        *buf = fctx->de_table[*buf];
        buf++;
    }
}
