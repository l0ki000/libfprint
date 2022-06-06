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

gboolean fpi_goodix_device_gtls_connection(FpDevice *dev, GError *error)
{
    fp_dbg("fpi_goodix_device_gtls_connection()");
    fp_dbg("Starting GTLS handshake");
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(self);
    priv->gtls_params = fpi_goodix_device_gtls_init_params();
    fpi_goodix_device_gtls_client_hello_step(dev);
    if (!fpi_goodix_device_gtls_server_identity_step(dev, error))
    {
        return FALSE;
    }
    // fpi_goodix_gtls_server_done_step();
    fp_dbg("GTLS handshake successful");
    return TRUE;
}

GoodixGTLSParams *fpi_goodix_device_gtls_init_params(void)
{
    GoodixGTLSParams *params = g_malloc0(sizeof(GoodixGTLSParams));
    params->state = 0;
    params->server_random = g_byte_array_new();
    params->server_identity = g_byte_array_new();
    return params;
}

void fpi_goodix_device_gtls_client_hello_step(FpDevice *dev)
{
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(self);
    priv->gtls_params->client_random = g_byte_array_new();
    GRand *rand = g_rand_new();
    for (int i = 0; i < 8; i++)
    {
        guint32 r = g_rand_int(rand);
        g_byte_array_append(priv->gtls_params->client_random, (guint8 *)&r, 4);
    }

    fp_dbg("client_random: %s", fpi_goodix_protocol_data_to_str(priv->gtls_params->client_random->data, priv->gtls_params->client_random->len));
    guint8 payload[] = {0x01, 0xFF, 0x00, 0x00};
    fpi_goodix_device_send_mcu(dev, payload, sizeof(payload), priv->gtls_params->client_random);
    priv->gtls_params->state = 2;
}

gboolean fpi_goodix_device_gtls_server_identity_step(FpDevice *dev, GError *error)
{
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(self);
    if (priv->gtls_params->state != 2)
    {
        return FALSE;
    }
    GoodixMessage *recv_mcu_ba = NULL;
    if (!fpi_goodix_device_recv_mcu(dev, 0xFF02, &recv_mcu_ba, error))
    {
        return FALSE;
    }
    fp_dbg("len: %#x", recv_mcu_ba->payload_len);
    if (recv_mcu_ba->payload_len != 0x40)
    {
        g_set_error(error, 1, "Wrong length, expected 0x40 - received: %#x", recv_mcu_ba->payload_len);
        return FALSE;
    }
    g_byte_array_append(priv->gtls_params->server_random, recv_mcu_ba->payload, 0x20);
    recv_mcu_ba->payload = &(recv_mcu_ba->payload[0x20]);
    recv_mcu_ba->payload_len = recv_mcu_ba->payload_len - 0x20;
    g_byte_array_append(priv->gtls_params->server_identity, recv_mcu_ba->payload, 0x20);
    g_free(recv_mcu_ba);
    fp_dbg("server_random: %s", fpi_goodix_protocol_data_to_str(priv->gtls_params->server_random->data, priv->gtls_params->server_random->len));
    fp_dbg("server_identity: %s", fpi_goodix_protocol_data_to_str(priv->gtls_params->server_identity->data, priv->gtls_params->server_identity->len));
    return TRUE;
    //TODO to be finish
}

void fpi_goodix_device_send_mcu(FpDevice *dev, const guint8 *data_type, gint lenght_data_type, GByteArray *data)
{
    fp_dbg("fpi_goodix_device_send_mcu");
    GByteArray *payload = g_byte_array_new();
    guint8 t = data->len + 8;
    g_byte_array_append(payload, data_type, lenght_data_type);
    g_byte_array_append(payload, (guint8 *)&t, 4);
    g_byte_array_append(payload, data->data, data->len);
    fp_dbg("mcu: %s", fpi_goodix_protocol_data_to_str(payload->data, payload->len));
    fp_dbg("payload_lenght: %d", payload->len);
    GoodixMessage *message = fpi_goodix_protocol_create_message(0xD, 1, payload->data, payload->len);
    GError *error = NULL;
    fpi_goodix_device_send(dev, message, TRUE, 500, FALSE, &error);
}

gboolean fpi_goodix_device_recv_mcu(FpDevice *dev, guint read_type, GoodixMessage **message, GError *error)
{
    fp_dbg("recv_mcu()");
    *message = g_malloc0(sizeof(GoodixMessage));
    if (!fpi_goodix_device_receive_data(dev, message, &error) || (*message)->category != 0xD || (*message)->command != 1)
    {
        return FALSE;
    }
    guint32 *read_type_recv = (*message)->payload;
    if (read_type != *read_type_recv)
    {
        g_set_error(error, 1, "Wrong read_type, excepted: %s - received: %s", &read_type, fpi_goodix_protocol_data_to_str((*message)->payload, sizeof(read_type)));
        return FALSE;
    }
    // TODO The offset to remove should be 8. But there is an other constrain in the caller function
    // on the len of the payload. To match this costrain I remove one more byte.
    // Now it's only a test, I don't know if it work correctly.
    (*message)->payload = &((*message)->payload[8]);
    (*message)->payload_len = (*message)->payload_len - 9;
    return TRUE;
}