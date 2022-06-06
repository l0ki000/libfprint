// Crypto utils for goodix 5395 driver

// Copyright (C) 2022 Anton Turko <anton.turko@proton.me>
// Copyright (C) 2022 Juri Sacchetta <jurisacchetta@gmail.com>

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
#include "goodix_protocol.h"


#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>

GByteArray *crypto_utils_sha256_hash(guint8 *data, gint len) {
    SHA256_CTX sha256;
    guchar res_buf[SHA256_DIGEST_LENGTH];
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data, len);
    SHA256_Final(&res_buf, &sha256);
    GByteArray *result = g_byte_array_new();
    g_byte_array_append(result, res_buf, SHA256_DIGEST_LENGTH);
    return result;
}

GByteArray *crypto_utils_derive_key(GByteArray *psk, GByteArray *random_data, gsize session_key_lenght) {
    //printf("PSK: %s\n", fpi_goodix_protocol_data_to_str(psk->data, psk->len));
    //printf("random_data_utils: %s\n", fpi_goodix_protocol_data_to_str(random_data->data, random_data->len));
    GByteArray *seed = g_byte_array_new();
    GByteArray *A = g_byte_array_new();
    GByteArray *session_key = g_byte_array_new();
    guint resultlen = 0;
    guint8 *resultbuf = NULL;

    // seed = b"master secret" + random_data
    // session_key = b""
    // A = seed
    char s[] = "master secret";
    g_byte_array_append(seed, s, strlen("master secret"));
    g_byte_array_append(seed, random_data->data, random_data->len);
    g_byte_array_append(A, seed->data, seed->len);


    // while len(session_key) < session_key_length:
    //     A = Crypto.Hash.HMAC.HMAC(psk, A, Crypto.Hash.SHA256).digest()
    //     session_key += Crypto.Hash.HMAC.HMAC(psk, A + seed,
    //                                          Crypto.Hash.SHA256).digest()

    // data: bytes = session_key[:session_key_length]
    // return data
    GByteArray *B, *to_free, *temp;
    int i = 1;
    while (session_key->len < session_key_lenght) {
        //printf("Iterazione %d\n", i);
        to_free = A;
        printf("A: %s\n", fpi_goodix_protocol_data_to_str(A->data, A->len));

        A = crypto_utils_HMAC_SHA256(psk, A);
        printf("A: %s\n", fpi_goodix_protocol_data_to_str(A->data, A->len));

        g_byte_array_free(to_free, TRUE);
        temp = g_byte_array_new();
        g_byte_array_append(temp, A->data, A->len);
        g_byte_array_append(temp, seed->data, seed->len);
        //printf("temp: %s\n", fpi_goodix_protocol_data_to_str(temp->data, temp->len));
        
        B = crypto_utils_HMAC_SHA256(psk, temp);
        //printf("B: %s\n", fpi_goodix_protocol_data_to_str(B->data, B->len));
        
        g_byte_array_free(temp, TRUE);
        g_byte_array_append(session_key, B->data, B->len);
        //printf("session_key: %s\n", fpi_goodix_protocol_data_to_str(session_key->data, session_key->len));
        i++;
    }
    GByteArray *result = g_byte_array_new();
    g_byte_array_append(result, session_key->data, session_key_lenght);
    g_byte_array_free(seed, TRUE);
    g_byte_array_free(session_key, TRUE);
    g_byte_array_free(A, TRUE);
    return result;
}

GByteArray *crypto_utils_HMAC_SHA256(GByteArray *key, GByteArray* data){
    guint resultlen = 0;
    guchar resultbuf[EVP_MAX_MD_SIZE];
    HMAC(EVP_sha256(), key->data, key->len, data->data, data->len, resultbuf, &resultlen);
    GByteArray *result = g_byte_array_new();
    g_byte_array_append(result, resultbuf, resultlen);
    return result;
}