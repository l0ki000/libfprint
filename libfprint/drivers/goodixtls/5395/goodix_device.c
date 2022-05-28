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

#define FP_COMPONENT "goodix_device"

#include <gio/gio.h>
#include <glib.h>
#include <gusb.h>
#include <openssl/ssl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

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

static gboolean fpi_goodix_device_receive_chunk(FpDevice *dev, guint8 **data, glong *length, GError **error) {
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
        memcpy(*data, transfer->buffer, transfer->actual_length);
        *length = transfer->actual_length;
        fp_dbg("Received chunk %s", fpi_goodix_protocol_data_to_str(*data, *length));
    }
    fpi_usb_transfer_unref(transfer);
    return success;
}

gboolean fpi_goodix_device_receive_data(FpDevice *dev, GoodixMessage **message, GError **error) {
    guint8 *data = g_malloc0( GOODIX_EP_IN_MAX_BUF_SIZE);
    glong length = 0;
    glong message_length = 0;
    guint chunk_count = 1;

    if (fpi_goodix_device_receive_chunk(dev, &data, &length, error)) {
        GoodixDevicePack *pack = (GoodixDevicePack *) data;
        message_length = pack->length;
    } else {
        return FALSE;
    }

    while (length - 1 < message_length) {
        chunk_count++;
        data = g_realloc(data, GOODIX_EP_IN_MAX_BUF_SIZE * chunk_count);
        fpi_goodix_device_receive_chunk(dev, (&data + (chunk_count - 1) * GOODIX_EP_IN_MAX_BUF_SIZE), &length, error);
    }
    gboolean success = fpi_goodix_protocol_decode(data, message, error);
    g_free(data);
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
    while (fpi_goodix_device_receive_chunk(dev, NULL, NULL, NULL)) {

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
