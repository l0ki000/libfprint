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
    SHA256_Final(res_buf, &sha256);
    GByteArray *result = g_byte_array_new();
    g_byte_array_append(result, res_buf, SHA256_DIGEST_LENGTH);
    return result;
}

GByteArray *crypto_utils_derive_key(GByteArray *psk, GByteArray *random_data, gsize session_key_lenght) {
    GByteArray *seed = g_byte_array_new();
    GByteArray *A = g_byte_array_new();
    GByteArray *session_key = g_byte_array_new();

    char s[] = "master secret";
    g_byte_array_append(seed, s, strlen(s));
    g_byte_array_append(seed, random_data->data, random_data->len);
    g_byte_array_append(A, seed->data, seed->len);

    GByteArray *B, *to_free, *temp;
    int i = 1;
    while (session_key->len < session_key_lenght) {
        to_free = A;

        A = crypto_utils_HMAC_SHA256(psk, A);

        g_byte_array_free(to_free, TRUE);
        temp = g_byte_array_new();
        g_byte_array_append(temp, A->data, A->len);
        g_byte_array_append(temp, seed->data, seed->len);
        
        B = crypto_utils_HMAC_SHA256(psk, temp);
        
        g_byte_array_free(temp, TRUE);
        g_byte_array_append(session_key, B->data, B->len);
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

GByteArray *crypo_utils_AES_128_cbc_decrypt(GByteArray *ciphertext, GByteArray *key, GByteArray *iv, GError **error) {
    EVP_CIPHER_CTX *ctx;
    int len = 0, plaintext_len = 0;
    if(key->len != 16 || iv->len != 16) {
        return NULL;
    }
    guint8 *plaintext = malloc(ciphertext->len * 3);

    if(!(ctx = EVP_CIPHER_CTX_new())){
        //TODO handleErrors
        return NULL;
    }
    if (1 != EVP_DecryptInit(ctx, EVP_aes_128_cbc(), key->data, iv->data)) {
        ERR_print_errors_fp(stderr);
        //TODO handleErrors
        return NULL;
    }
    EVP_CIPHER_CTX_set_padding(ctx, 0);
    if(1 != EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext->data, ciphertext->len)) {
        ERR_print_errors_fp(stderr);
        //TODO handleErrors
        return NULL;
    }
    plaintext_len = len;
    if (1 != EVP_DecryptFinal(ctx, plaintext + len, &len)) {
        ERR_print_errors_fp(stderr);
        //TODO handleErrors
        return NULL;
    }
    plaintext_len += len;
    GByteArray *plain_buf = g_byte_array_new();
    g_byte_array_append(plain_buf, plaintext, plaintext_len);

    /* Clean up */
    EVP_CIPHER_CTX_free(ctx);
    g_free(plaintext);

    return plain_buf;
}


GByteArray *ctypto_utils_gea_decrypt(guint32 key, GByteArray *encrypted_data){
    guint32 uVar1, uVar2, uVar3;
    GByteArray *decrypt_data = g_byte_array_new();
    for (int index = 0; index < encrypted_data->len; index += 2) {
        uVar3 = (key >> 1 ^ key) & 0xFFFFFFFF;
        uVar2 = (
            (
                (
                    (
                        (
                            (
                                (
                                    (key >> 0xF & 0x2000 | key & 0x1000000) >> 1
                                    | key & 0x20000
                                )
                                >> 2
                                | key & 0x1000
                            )
                            >> 3
                            | (key >> 7 ^ key) & 0x80000
                        )
                        >> 1
                        | (key >> 0xF ^ key) & 0x4000
                    )
                    >> 2
                    | key & 0x2000
                )
                >> 2
                | uVar3 & 0x40
                | key & 0x20
            )
            >> 1
            | (key >> 9 ^ key << 8) & 0x800
            | (key >> 0x14 ^ key * 2) & 4
            | (key * 8 ^ key >> 0x10) & 0x4000
            | (key >> 2 ^ key >> 0x10) & 0x80
            | (key << 6 ^ key >> 7) & 0x100
            | (key & 0x100) << 7);
        uVar2 = uVar2 & 0xFFFFFFFF;
        uVar1 = key & 0xFFFF;
        key = ((key ^ (uVar3 >> 0x14 ^ key) >> 10) << 0x1F | key >> 1) & 0xFFFFFFFF;
        guint16 input_element = encrypted_data->data[index] | encrypted_data->data[index + 1] << 8;
        guint16 stream_val = ((uVar2 >> 8) & 0xFFFF) + (uVar2 & 0xFF | uVar1 & 1) * 0x100;
        guint16 temp = input_element ^ stream_val;
        // temp = GINT16_TO_BE(temp);
        guint8 buffer[] = {temp & 0xFF, temp >> 8};
        g_byte_array_append(decrypt_data, buffer, 2);
    }

    g_assert(encrypted_data->len == decrypt_data->len);
    return decrypt_data;
}

guint crypto_utils_crc32_mpeg2_calc(unsigned char *message, size_t l){
   size_t i, j;
   unsigned int crc, msb;

   crc = 0xFFFFFFFF;
   for(i = 0; i < l; i++) {
      // xor next byte to upper bits of crc
      crc ^= (((unsigned int)message[i])<<24);
      for (j = 0; j < 8; j++) {    // Do eight times.
            msb = crc>>31;
            crc <<= 1;
            crc ^= (0 - msb) & 0x04C11DB7;
      }
   }
   return crc;         // don't complement crc on output
}
