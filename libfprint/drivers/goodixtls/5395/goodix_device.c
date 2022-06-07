// Goodix 5395 driver protocol for libfprint

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

#define FP_COMPONENT "goodix_device"

#include <gio/gio.h>
#include <glib.h>
#include <gusb.h>
#include <openssl/ssl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "crypto_utils.h"
#include "goodix_gtls.h"

#include "drivers_api.h"
#include "goodix_device.h"

#include "fpi-usb-transfer.h"
#include "fpi-image-device.h"

#define GOODIX_TIMEOUT 2000

typedef struct {
    pthread_t tls_server_thread;
    gint tls_server_sock;
    SSL_CTX *tls_server_ctx;

    GSource *timeout;

    GoodixMessage *message;
    gboolean ack;
    gboolean reply;

    GoodixDeviceReceiveCallback callback;
    gpointer user_data;

    guint8 *data;
    guint32 length;
    GoodixGTLSParams *gtls_params;
    GByteArray *psk;
} FpiGoodixDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FpiGoodixDevice, fpi_goodix_device, FP_TYPE_IMAGE_DEVICE);

// ----- INIT CLASS SECTION ------

static void fpi_goodix_device_init(FpiGoodixDevice *self) {}

static void fpi_goodix_device_class_init(FpiGoodixDeviceClass *class) { }

// ----- CALLBAKS -----

//void fpi_goodix_device_send_done(FpDevice *dev, guint8 *data, guint16 length, GError *error) {
//    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
//    FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(self);
//    GoodixDeviceCmdCallback callback = priv->callback;
//    gpointer user_data = priv->user_data;
//
//    if (!(priv->ack || priv->reply)) return;
//
//    if (priv->timeout) {
//        g_clear_pointer(&priv->timeout, g_source_destroy);
//    }
//    priv->ack = FALSE;
//    priv->reply = FALSE;
//    priv->callback = NULL;
//    priv->user_data = NULL;
//
//    if (!error) {
//        fp_dbg("Completed command: 0x%02x", priv->message->command);
//    }
//
//    if (callback) {
//        callback(dev, data, length, user_data, error);
//    }
//}

// ----- METHODS -----

static gboolean fpi_goodix_device_receive_chunk(FpDevice *dev, GByteArray *data, GError **error) {
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDeviceClass *class = FPI_GOODIX_DEVICE_GET_CLASS(self);
    FpiUsbTransfer *transfer = fpi_usb_transfer_new(dev);

    transfer->short_is_error = FALSE;

    fpi_usb_transfer_fill_bulk(transfer, class->ep_in, GOODIX_EP_IN_MAX_BUF_SIZE);

    gboolean success = fpi_usb_transfer_submit_sync(transfer, GOODIX_TIMEOUT, error);

    while(success && transfer->actual_length == 0) {
        success = fpi_usb_transfer_submit_sync(transfer, GOODIX_TIMEOUT, error);
    }

    if (success) {
        g_byte_array_append(data, transfer->buffer, transfer->actual_length);
        fp_dbg("Received chunk %s", fpi_goodix_protocol_data_to_str(transfer->buffer, transfer->actual_length));
    }
    fpi_usb_transfer_unref(transfer);
    return success;
}

gboolean fpi_goodix_device_receive_data(FpDevice *dev, GoodixMessage **message, GError **error) {
    GByteArray *buffer = g_byte_array_new();
    glong message_length = 0;
    guint8 command_byte;
    if (fpi_goodix_device_receive_chunk(dev, buffer, error)) {
        GoodixDevicePack *pack = (GoodixDevicePack *) buffer->data;
        command_byte = pack->cmd;
        message_length = pack->length;
    } else {
        return FALSE;
    }

    while (buffer->len - 1 < message_length) {
        GByteArray *chuck = g_byte_array_new();
        fpi_goodix_device_receive_chunk(dev, chuck, error);
        guint8 contd_command_byte = chuck->data[0];
         if ((contd_command_byte & 0xFE) != command_byte){
            g_set_error(error, 1, "Wrong contd_command_byte: expected %d, received %d", &command_byte, &contd_command_byte);
         }
        g_byte_array_remove_index(chuck, 0);
        g_byte_array_append(buffer, chuck->data, chuck->len);
        g_byte_array_free(chuck, chuck->len);
    }
    fp_dbg("complete mess: %s", fpi_goodix_protocol_data_to_str(buffer->data, buffer->len));
    gboolean success = fpi_goodix_protocol_decode(buffer->data, message, error);
    g_byte_array_free(buffer, TRUE);
    return success;
}

gboolean fpi_goodix_device_init_device(FpDevice *dev, GError **error) {
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDeviceClass *class = FPI_GOODIX_DEVICE_GET_CLASS(self);
    FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(self);

    priv->timeout = NULL;
    priv->ack = FALSE;
    priv->reply = FALSE;
    priv->callback = NULL;
    priv->user_data = NULL;
    priv->data = NULL;
    priv->gtls_params = NULL;
    priv->length = 0;

    return g_usb_device_claim_interface(fpi_device_get_usb_device(dev), class->interface, G_USB_DEVICE_CLAIM_INTERFACE_NONE, error);
}

gboolean fpi_goodix_device_deinit_device(FpDevice *dev, GError **error) {
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDeviceClass *class = FPI_GOODIX_DEVICE_GET_CLASS(self);
    FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(self);

    if (priv->timeout) {
        g_source_destroy(priv->timeout);
    }
    g_free(priv->data);

    return g_usb_device_release_interface(fpi_device_get_usb_device(dev),
                                          class->interface, 0, error);
}

static gboolean fpi_goodix_device_write(FpDevice *dev, guint8 *data, guint32 length, gint timeout_ms, GError **error) {
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDeviceClass *class = FPI_GOODIX_DEVICE_GET_CLASS(self);
    FpiUsbTransfer *transfer = fpi_usb_transfer_new(dev);

    transfer->short_is_error = TRUE;

    for (guint32 i = 0; i < length; i += GOODIX_EP_OUT_MAX_BUF_SIZE) {
        fp_dbg("Send chunk %s", fpi_goodix_protocol_data_to_str(data, GOODIX_EP_OUT_MAX_BUF_SIZE));
        fpi_usb_transfer_fill_bulk_full(transfer, class->ep_out, data + i,
                                        GOODIX_EP_OUT_MAX_BUF_SIZE, NULL);

        if (!fpi_usb_transfer_submit_sync(transfer, timeout_ms, error)) {
            g_free(data);
            fpi_usb_transfer_unref(transfer);
            return FALSE;
        }
    }

    g_free(data);
    fpi_usb_transfer_unref(transfer);
    return TRUE;
}

gboolean fpi_goodix_device_send(FpDevice *dev, GoodixMessage *message, gboolean calc_checksum,
                            gint timeout_ms, gboolean reply, GError **error) {
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(self);
    guint8 *data;
    guint32 data_len;

    fp_dbg("Running command: 0x%02x", message->command);

    priv->message = message;
    priv->ack = TRUE;
    priv->reply = reply;

    fpi_goodix_protocol_encode(message, calc_checksum, TRUE, &data, &data_len);
    g_free(message);

    gboolean is_success = fpi_goodix_device_write(dev, data, data_len, timeout_ms, error);

    if (is_success) {
        GoodixMessage *ackMessage = NULL;
        is_success = fpi_goodix_device_receive_data(dev, &ackMessage, error);
        if (is_success) {
            is_success = fpi_goodix_protocol_check_ack(ackMessage, error);
        }
        g_free(ackMessage);
    }

    return is_success;
}

void fpi_goodix_device_empty_buffer(FpDevice *dev) {
    while (fpi_goodix_device_receive_chunk(dev, NULL, NULL)) {

    }
}

gboolean fpi_goodix_device_reset(FpDevice *dev, guint8 reset_type, gboolean irq_status) {
    guint16 payload;
    switch (reset_type) {
        case 0:{
            payload = 0x001;
            if (irq_status) {
                payload |= 0x100;
            }
            payload |= 20 << 8;
        }
        break;
        case 1: {
            payload = 0b010;
            payload |= 50 << 8;
        }
        break;
        case 2: {
            payload = 0b011;
        }
        break;
    }
    GoodixMessage *message = g_malloc0(sizeof(GoodixMessage));
    message->category = 0xA;
    message->command = 1;
    message->payload_len = 2;
    message->payload = g_malloc0(message->payload_len);
    message->payload[0] = payload & 0xFF;
    message->payload[1] = payload >> 8;

    GError *error = NULL;
    return fpi_goodix_device_send(dev, message, TRUE, 500, FALSE, &error);
}

gboolean fpi_goodix_device_gtls_connection(FpDevice *dev, GError *error) {
    fp_dbg("fpi_goodix_device_gtls_connection()");
    fp_dbg("Starting GTLS handshake");
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(self);
    priv->gtls_params = fpi_goodix_device_gtls_init_params();

    // Hello phase
    fpi_goodix_gtls_create_hello_message(priv->gtls_params);
    fp_dbg("client_random: %s", fpi_goodix_protocol_data_to_str(priv->gtls_params->client_random->data, priv->gtls_params->client_random->len));
    fp_dbg("client_random_len: %#x", priv->gtls_params->client_random->len);
    fpi_goodix_device_send_mcu(dev, 0xFF01, priv->gtls_params->client_random);
    priv->gtls_params->state = 2;

    // Server identity step
    GByteArray *recv_mcu_payload = fpi_goodix_device_recv_mcu(dev, 0xFF02, error);
    if (recv_mcu_payload == NULL || recv_mcu_payload->len != 0x40) {
        g_set_error(error, 1, "Wrong length, expected 0x40 - received: %#x", recv_mcu_payload->len);
        return FALSE;
    }
    fpi_goodix_gtls_decode_server_hello(priv->gtls_params, recv_mcu_payload);
    fp_dbg("server_random: %s", fpi_goodix_protocol_data_to_str(priv->gtls_params->server_random->data, priv->gtls_params->server_random->len));
    fp_dbg("server_identity: %s", fpi_goodix_protocol_data_to_str(priv->gtls_params->server_identity->data, priv->gtls_params->server_identity->len));

    if(!fpi_goodix_gtls_derive_key(priv->gtls_params)){
        //TODO set error
        fp_dbg("client_identity: %s", fpi_goodix_protocol_data_to_str(priv->gtls_params->client_identity->data, priv->gtls_params->client_identity->len));
        return FALSE;
    }
    fp_dbg("session_key:    %s", fpi_goodix_protocol_data_to_str(priv->gtls_params->symmetric_key->data, priv->gtls_params->symmetric_key->len));
    fp_dbg("session_iv:     %s", fpi_goodix_protocol_data_to_str(priv->gtls_params->symmetric_iv->data, priv->gtls_params->symmetric_iv->len));
    fp_dbg("hmac_key:       %s", fpi_goodix_protocol_data_to_str(priv->gtls_params->hmac_key->data, priv->gtls_params->hmac_key->len));
    fp_dbg("hmac_client_counter_init:    %#x", priv->gtls_params->hmac_client_counter_init);
    fp_dbg("hmac_client_counter_init:    %#x", priv->gtls_params->hmac_server_counter_init);

    GByteArray *temp = g_byte_array_new();
    g_byte_array_append(temp, priv->gtls_params->server_identity->data, priv->gtls_params->server_identity->len);
    guint payload[] = {0xee, 0xee, 0xee, 0xee};
    g_byte_array_append(temp, payload, 4);
    fpi_goodix_device_send_mcu(dev, 0xFF03, temp);
    g_byte_array_free(temp, TRUE);
    priv->gtls_params->state = 4;
    temp = fpi_goodix_device_recv_mcu(dev, 0xFF04, error);
    if (temp->data[0] != 0){
        //TODO set error
        return FALSE;
    }
    priv->gtls_params->hmac_client_counter = priv->gtls_params->hmac_client_counter_init;
    priv->gtls_params->hmac_server_counter = priv->gtls_params->hmac_server_counter_init;
    priv->gtls_params->state = 5;
    fp_dbg("GTLS handshake successful");
    return TRUE;
}

void fpi_goodix_device_send_mcu(FpDevice *dev, const guint32 data_type, GByteArray *data) {
    fp_dbg("fpi_goodix_device_send_mcu()");
    GByteArray *payload = g_byte_array_new();
    guint32 t = data->len + 8;
    g_byte_array_append(payload, &data_type, sizeof(data_type));
    g_byte_array_append(payload, (guint8 *)&t, sizeof(t));
    g_byte_array_append(payload, data->data, data->len);
    fp_dbg("mcu: %s", fpi_goodix_protocol_data_to_str(payload->data, payload->len));
    fp_dbg("payload_lenght: %d", payload->len);
    GoodixMessage *message = fpi_goodix_protocol_create_message(0xD, 1, payload->data, payload->len);
    GError *error = NULL;
    fpi_goodix_device_send(dev, message, TRUE, 500, FALSE, &error);
}

GByteArray *fpi_goodix_device_recv_mcu(FpDevice *dev, guint32 read_type, GError *error) {
    fp_dbg("recv_mcu_payload()");
    GoodixMessage *message = g_malloc0(sizeof(GoodixMessage)); //TODO this must be freed
    if (!fpi_goodix_device_receive_data(dev, &message, &error) || message->category != 0xD || message->command != 1) {
        return FALSE;
    }
    GByteArray *msg_payload = g_byte_array_new();
    g_byte_array_append(msg_payload, message->payload, message->payload_len);
    guint32 read_type_recv = (guint32)(msg_payload->data[0] | msg_payload->data[1] << 8 | msg_payload->data[2] << 16 | msg_payload->data[3] << 32);
    guint32 payload_size_recv  = (guint32)(msg_payload->data[4] | msg_payload->data[5] << 8 | msg_payload->data[6] << 16 | msg_payload->data[7] << 32);
    
    if (read_type != read_type_recv) {
        g_set_error(error, 1, "Wrong read_type, excepted: %#x - received: %s", read_type, fpi_goodix_protocol_data_to_str(message->payload, sizeof(read_type)));
        return FALSE;
    }
    if (payload_size_recv != msg_payload->len) {
        g_set_error(error, 1, "Wrong payload size, excepted: %#x - received: %s", read_type, fpi_goodix_protocol_data_to_str(message->payload, sizeof(payload_size_recv)));
        return FALSE;
    }
    g_byte_array_remove_range(msg_payload, 0, 8);
    return msg_payload;
}