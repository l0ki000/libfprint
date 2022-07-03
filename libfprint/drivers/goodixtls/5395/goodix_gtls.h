// Goodix 5395 driver protocol for libfprint

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

#pragma once

#include "goodix_protocol.h"
#include <glib.h>

enum EstablishConnectionStates {
    CLIENT_HELLO,
    SERVER_IDENTIFY,
    SERVER_DONE,
    ESTABLISH_CONNECTION_STATES_NUM
};

typedef struct __attribute__((__packed__)) __GoodixGTLSParams
{
    enum EstablishConnectionStates state;
    GByteArray *client_random;
    GByteArray *server_random;
    GByteArray *client_identity;
    GByteArray *server_identity;
    GByteArray *symmetric_key;
    GByteArray *symmetric_iv;
    GByteArray *hmac_key;
    guint16 hmac_client_counter_init;
    guint16 hmac_server_counter_init;
    guint hmac_client_counter;
    guint hmac_server_counter;
    GByteArray *psk;
} GoodixGTLSParams;

GoodixGTLSParams *fpi_goodix_device_gtls_init_params(void);
GByteArray *fpi_goodix_gtls_create_hello_message(void);
void fpi_goodix_gtls_decode_server_hello(GoodixGTLSParams *params, GByteArray *recv_mcu_payload);
gboolean fpi_goodix_gtls_derive_key(GoodixGTLSParams *params);
GByteArray *fpi_goodix_gtls_decrypt_sensor_data(GoodixGTLSParams *params, GByteArray *encrypted_message, GError **error);