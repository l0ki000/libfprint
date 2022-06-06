// Crypto utils for goodix 5395 driver

// Copyright (C) 2022 Anton Turko <anton.turko@proton.me>

// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#include <glib.h>
#include "crypto_utils.h"


#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>

void crypto_utils_sha256_hash(const guint8 *data, const guint length, guint8 *result_hash, guint result_length) {
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data, length);
    SHA256_Final(result_hash, &sha256);
    result_length = SHA256_DIGEST_LENGTH;
}

GByteArray *crypto_utils_derive_key(GByteArray *psk, GByteArray *random_data, gsize session_key_lenght) {
    GByteArray *seed = g_byte_array_new();
    g_byte_array_append(seed, "master secret", sizeof("master secret"));
    g_byte_array_append(seed, random_data->data, random_data->len);
    GByteArray *temp_session_key = g_byte_array_new();
    GByteArray *A = g_byte_array_new();
    g_byte_array_append(A, seed->data, seed->len);

    guint resultlen = 0;
    guchar resultbuf[EVP_MAX_MD_SIZE];
    while (temp_session_key->len < session_key_lenght) {
        HMAC(EVP_sha256(), psk->data, psk->len, A->data, A->len, resultbuf, &resultlen);
        g_byte_array_free(A, TRUE);
        
        A = g_byte_array_new();
        g_byte_array_append(A, resultbuf, resultlen);
        g_byte_array_append(A, seed->data, seed->len);
        
        HMAC(EVP_sha256(), psk->data, psk->len, A->data, A->len, resultbuf, &resultlen);
        g_byte_array_append(temp_session_key, resultbuf, resultlen);
    }
    GByteArray *session_key = g_byte_array_new();
    g_byte_array_append(session_key, temp_session_key->data, session_key_lenght);
    g_byte_array_free(seed, TRUE);
    g_byte_array_free(temp_session_key, TRUE);
    g_byte_array_free(A, TRUE);
    return session_key;
}

GByteArray *crypto_utils_HMAC_SHA256(GByteArray *buf1, GByteArray* buf2){
    guint resultlen = 0;
    guchar resultbuf[EVP_MAX_MD_SIZE];
    HMAC(EVP_sha256(), buf1->data, buf1->len, buf2->data, buf2->len, resultbuf, &resultlen);
    GByteArray *result = g_byte_array_new();
    g_byte_array_append(result, resultbuf, resultlen);
    return result;
}