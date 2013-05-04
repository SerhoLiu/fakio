#include "fcrypt.h"

/*
 *  An implementation of the ARCFOUR algorithm
 *
 *  Copyright (C) 2006-2010, Brainspark B.V.
 *
 *  This code is part of PolarSSL (http://www.polarssl.org)
 *  Lead Maintainer: Paul Bakker <polarssl_maintainer at polarssl.org>
 */

static void rc4_init(rc4_ctx *ctx, const unsigned char *key, unsigned int keylen )
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


int rc4_crypt(rc4_ctx *ctx, size_t length, unsigned char *buffer)
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

void fcrypt_init_ctx(fcrypt_ctx *fctx, const unsigned char *key, unsigned int keylen)
{
    rc4_init(&(fctx->en_ctx), key, keylen);
    rc4_init(&(fctx->de_ctx), key, keylen);
}
