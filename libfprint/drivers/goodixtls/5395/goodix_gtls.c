// Goodix Tls driver for libfprint

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
#include <stdio.h>

#include "goodix_gtls.h"
#include "goodix_protocol.h"
#include "crypto_utils.h"

GoodixGTLSParams *fpi_goodix_device_gtls_init_params(void) {
    GoodixGTLSParams *params = g_malloc0(sizeof(GoodixGTLSParams));
    params->state = 0;
    params->client_random = g_byte_array_new();
    params->server_random = g_byte_array_new();
    params->server_identity = g_byte_array_new();
    params->symmetric_key = g_byte_array_new();
    params->symmetric_iv = g_byte_array_new();
    params->hmac_key = g_byte_array_new();
    params->psk = g_byte_array_new();
    guchar psk[32] = {0};
    g_byte_array_append(params->psk, psk, 32);
    return params;
}

GByteArray *fpi_goodix_gtls_create_hello_message() {
    GByteArray *res = g_byte_array_new();
    GRand *rand = g_rand_new();
    for (int i = 0; i < 8; i++) {
        guint32 r = g_rand_int(rand);
        g_byte_array_append(res, &r, sizeof(guint32));
    }
    g_rand_free(rand);
    return res;
}

void fpi_goodix_gtls_decode_server_hello(GoodixGTLSParams *params, GByteArray *recv_mcu_payload) {
    g_byte_array_append(params->server_random, recv_mcu_payload->data, 0x20);
    g_byte_array_remove_range(recv_mcu_payload, 0, 0x20);
    g_byte_array_append(params->server_identity, recv_mcu_payload->data, 0x20);
    g_byte_array_free(recv_mcu_payload, recv_mcu_payload->len);
}

gboolean fpi_goodix_gtls_derive_key(GoodixGTLSParams *params) {
    GByteArray *random_data = g_byte_array_new();
    g_byte_array_append(random_data, params->client_random->data, params->client_random->len);
    g_byte_array_append(random_data, params->server_random->data, params->server_random->len);
    GByteArray *session_key = crypto_utils_derive_key(params->psk, random_data, 0x44);
    g_byte_array_free(random_data, TRUE);
    
    g_byte_array_append(params->symmetric_key, session_key->data, 0x10);
    g_byte_array_remove_range(session_key, 0, 0x10);
    
    g_byte_array_append(params->symmetric_iv, session_key->data, 0x10);
    g_byte_array_remove_range(session_key, 0, 0x10);
    
    g_byte_array_append(params->hmac_key, session_key->data, 0x20);
    g_byte_array_remove_range(session_key, 0, 0x20);
    
    params->hmac_client_counter_init = (guint16)(session_key->data[0] | session_key->data[1] << 8);
    g_byte_array_remove_range(session_key, 0, 0x2);
    
    params->hmac_server_counter_init = (guint16)(session_key->data[0] | session_key->data[1] << 8);
    g_byte_array_remove_range(session_key, 0, 0x2);

    g_assert(session_key->len == 0);
    g_byte_array_free(session_key, TRUE);

    GByteArray *random = g_byte_array_new();
    g_byte_array_append(random, params->client_random->data, params->client_random->len);
    g_byte_array_append(random, params->server_random->data, params->server_random->len);
    params->client_identity = crypto_utils_HMAC_SHA256(params->hmac_key, random);

    if (memcmp(params->client_identity->data, params->server_identity->data, params->client_identity->len) != 0
        || params->client_identity->len != params->server_identity->len ) {
        return FALSE;
    }
    return TRUE;
}