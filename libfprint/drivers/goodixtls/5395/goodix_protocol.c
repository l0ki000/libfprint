// Goodix Tls driver for libfprint

// Copyright (C) 2021 Alexander Meiler <alex.meiler@protonmail.com>
// Copyright (C) 2021 Matthieu CHARETTE <matthieu.charette@gmail.com>

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

#define FP_COMPONENT "goodix_protocol"

#define GOODIX_NULL_CHECKSUM (0x88)

#include "drivers_api.h"
#include "goodix_protocol.h"
#include <stdio.h>

//G_DEFINE_TYPE(FpiGoodixProtocol, fpi_goodix_protocol, g_object_get_type());

// ----- METHODS -----

gchar *fpi_goodix_protocol_data_to_str(guint8 *data, guint32 length) {
    gchar *string = g_malloc((length * 3) + 1);

    for (guint32 i = 0; i < length; i++) sprintf(string + i * 3, " %02x", data[i]);

    return string;
}

gboolean fpi_goodix_protocol_check_ack(GoodixMessage *message, GError **error) {
    gboolean is_valid_ack = TRUE;
    if (message->category != 0xB) {
        g_set_error(error, FPI_TYPE_IMAGE_DEVICE_STATE, -1, "Not an ack message");
        is_valid_ack = FALSE;
    }
    if (message->command != 0) {
        g_set_error(error, FPI_TYPE_IMAGE_DEVICE_STATE, -1, "ACK should not have commands");
        is_valid_ack = FALSE;
    }

//    if ( != 0) {
//        g_set_error(error, FPI_TYPE_IMAGE_DEVICE_STATE, -1, "ACK should not have commands");
//    }
    return is_valid_ack;
}

static guint8 fpi_goodix_calc_checksum(guint8 *data, guint16 length) {
    guint8 checksum = 0;
    for (guint16 i = 0; i < length; i++) {
        checksum += data[i];
    }
    return 0xAA - checksum & 0xFF;
}

void fpi_goodix_protocol_encode(GoodixMessage *message, gboolean calc_checksum, gboolean pad_data,
                                guint8 **data, guint32 *data_len) {
    guint16 payload_len = message->payload_len;
    GoodixDevicePack *pack;
    *data_len = sizeof(GoodixDevicePack) + payload_len + sizeof(guint8);

    if (pad_data && *data_len % GOODIX_EP_OUT_MAX_BUF_SIZE) {
        *data_len += GOODIX_EP_OUT_MAX_BUF_SIZE - *data_len % GOODIX_EP_OUT_MAX_BUF_SIZE;
    }

    *data = g_malloc0(*data_len);
    pack = (GoodixDevicePack *)*data;

    pack->cmd = message->category << 4 | message->command << 1;
    pack->length = GUINT16_TO_LE(payload_len + sizeof(guint8));

    memcpy(*data + sizeof(GoodixDevicePack), message->payload, payload_len);

    if (calc_checksum) {
        gsize package_with_payload_length = sizeof(GoodixDevicePack) + payload_len;
        (*data)[package_with_payload_length] = fpi_goodix_calc_checksum(*data, package_with_payload_length);
    } else {
        (*data)[sizeof(GoodixDevicePack) + payload_len] = GOODIX_NULL_CHECKSUM;
    }
}

gboolean fpi_goodix_protocol_decode(guint8 *data, GoodixMessage **message, GError **error) {
    GoodixDevicePack *pack = (GoodixDevicePack *) data;
    guint data_length = sizeof(GoodixDevicePack) + pack->length - 1;
    guint8 message_checksum = data[data_length];
    if (message_checksum != 0x88) {
        guint checksum = fpi_goodix_calc_checksum(data, data_length);
        if (message_checksum != checksum) {
            g_set_error(error, 1, "Wrong checksum: expected %d, received %d", checksum, message_checksum);
            return FALSE;
        }
    }

    *message = g_malloc0(sizeof(GoodixMessage));
    (*message)->category = pack->cmd >> 4;
    (*message)->command = (pack->cmd & 0xF) >> 1;
    (*message)->payload = g_malloc0(pack->length - 1);
    (*message)->payload_len = pack->length - 1;
    memcpy((*message)->payload, data + sizeof(GoodixDevicePack), pack->length - 1);

    return TRUE;
}

int fpi_goodix_protocol_decode_u32(guint8 *data, uint length) {
    return data[0] * 0x100 + data[1] + data[2] * 0x1000000 + data[3] * 0x10000;
}


GoodixMessage *fpi_goodix_protocol_create_message(guint8 category, guint8 command, guint8 *payload, guint8 length) {
    GoodixMessage *message = g_malloc0(sizeof(GoodixMessage));
    message->category = category;
    message->command = command;
    message->payload_len = length;
    message->payload = payload;
    return message;
}

static guint8 fpi_goodix_device_compute_otp_hash(const guint8 *otp, guint otp_length, const guint8 otp_hash[]) {
    guint8 checksum = 0;
    for (guint i = 0; i < otp_length; i++) {
        if (i == 25) {
            continue;
        }
        checksum = otp_hash[checksum ^ otp[i]];
    }
    return ~checksum & 0xFF;
}

gboolean fpi_goodix_device_verify_otp_hash(const guint8 *otp, guint otp_length, const guint8 otp_hash[]) {
    guint8 received_hash = otp[25];
    guint8 computed_hash = fpi_goodix_device_compute_otp_hash(otp, otp_length, otp_hash);
    return received_hash == computed_hash;
}