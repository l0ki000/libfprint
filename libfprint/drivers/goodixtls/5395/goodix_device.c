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
        GByteArray *chunk = g_byte_array_new();
        fpi_goodix_device_receive_chunk(dev, chunk, error);
        guint8 contd_command_byte = chunk->data[0];
         if ((contd_command_byte & 0xFE) != command_byte){
            g_set_error(error, 1, 1,"Wrong contd_command_byte: expected %02x, received %02x", command_byte, contd_command_byte);
         }
        g_byte_array_remove_index(chunk, 0);
        g_byte_array_append(buffer, chunk->data, chunk->len);
        g_byte_array_free(chunk, TRUE);
    }
    gboolean success = fpi_goodix_protocol_decode(buffer->data, message, error);
    g_byte_array_free(buffer, TRUE);
    return success;
}

gboolean fpi_goodix_device_init_device(FpDevice *dev, GError **error) {
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDeviceClass *class = FPI_GOODIX_DEVICE_GET_CLASS(self);
    FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(self);

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

    g_free(priv->data);

    return g_usb_device_release_interface(fpi_device_get_usb_device(dev),
                                          class->interface, 0, error);
}

static gboolean fpi_goodix_device_write(FpDevice *dev, guint8 *data, guint32 length, gint timeout_ms, GError **error) {
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDeviceClass *class = FPI_GOODIX_DEVICE_GET_CLASS(self);

    guint32 sent_length = 0;
    while(sent_length < length) {
        FpiUsbTransfer *transfer = fpi_usb_transfer_new(dev);

        transfer->short_is_error = FALSE;

        GByteArray *buffer = g_byte_array_new();
        if (sent_length == 0) {
            g_byte_array_append(buffer, data, GOODIX_EP_OUT_MAX_BUF_SIZE);
            sent_length += GOODIX_EP_OUT_MAX_BUF_SIZE;
        } else {
            guint8 start_byte = data[0] | 1;
            g_byte_array_append(buffer, &start_byte, 1);
            g_byte_array_append(buffer, data + sent_length, GOODIX_EP_OUT_MAX_BUF_SIZE - 1);
            sent_length += GOODIX_EP_OUT_MAX_BUF_SIZE - 1;
        }
        fpi_usb_transfer_fill_bulk_full(transfer, class->ep_out, buffer->data, GOODIX_EP_OUT_MAX_BUF_SIZE, NULL);
        if (!fpi_usb_transfer_submit_sync(transfer, timeout_ms, error)) {
            g_free(data);
            g_byte_array_free(buffer, FALSE);
            fpi_usb_transfer_unref(transfer);
            return FALSE;
        }
        fp_dbg("Chunk sent %s", fpi_goodix_protocol_data_to_str(buffer->data, GOODIX_EP_OUT_MAX_BUF_SIZE));
        g_byte_array_free(buffer, FALSE);
        fpi_usb_transfer_unref(transfer);
    }

    g_free(data);
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
    guint8 message_payload[] = {payload & 0xFF, payload >> 8};
    GoodixMessage *message = fpi_goodix_protocol_create_message(0xA, 1, message_payload, 2);

    GError *error = NULL;
    return fpi_goodix_device_send(dev, message, TRUE, 500, FALSE, &error);
}


// ----- GOODIX GTLS CONNECTION ------
static void fpi_goodix_device_gtls_connection_handle(FpiSsm *ssm, FpDevice* dev) {
    GError *error = NULL;
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(self);

    switch (fpi_ssm_get_cur_state(ssm)) {
        case CLIENT_HELLO: {
            priv->gtls_params = fpi_goodix_device_gtls_init_params();
            priv->gtls_params->client_random = fpi_goodix_gtls_create_hello_message();
            fp_dbg("client_random: %s", fpi_goodix_protocol_data_to_str(priv->gtls_params->client_random->data, priv->gtls_params->client_random->len));
            fp_dbg("client_random_len: %02x", priv->gtls_params->client_random->len);
            fpi_goodix_device_send_mcu(dev, 0xFF01, priv->gtls_params->client_random);
            priv->gtls_params->state = CLIENT_HELLO;
            fpi_ssm_next_state(ssm);
        }
            break;
        case SERVER_IDENTIFY: {
            GByteArray *recv_mcu_payload = fpi_goodix_device_recv_mcu(dev, 0xFF02, error);
            if (recv_mcu_payload == NULL || recv_mcu_payload->len != 0x40) {
                FAIL_SSM_AND_RETURN(ssm, FPI_GOODIX_DEVICE_ERROR(SERVER_IDENTIFY, "Wrong length, expected 0x40 - received: %02x", recv_mcu_payload->len))
            }
            fpi_goodix_gtls_decode_server_hello(priv->gtls_params, recv_mcu_payload);
            fp_dbg("server_random: %s", fpi_goodix_protocol_data_to_str(priv->gtls_params->server_random->data, priv->gtls_params->server_random->len));
            fp_dbg("server_identity: %s", fpi_goodix_protocol_data_to_str(priv->gtls_params->server_identity->data, priv->gtls_params->server_identity->len));

            if(!fpi_goodix_gtls_derive_key(priv->gtls_params)) {
                fp_dbg("client_identity: %s", fpi_goodix_protocol_data_to_str(priv->gtls_params->client_identity->data, priv->gtls_params->client_identity->len));
                FAIL_SSM_AND_RETURN(ssm, FPI_GOODIX_DEVICE_ERROR(SERVER_IDENTIFY, "Client and server identity don't match. client identity: %s, server identity: %s ",
                                                                 fpi_goodix_protocol_data_to_str(priv->gtls_params->client_identity->data, priv->gtls_params->client_identity->len),
                                                                 fpi_goodix_protocol_data_to_str(priv->gtls_params->server_identity->data, priv->gtls_params->server_identity->len)))
            }
            fp_dbg("session_key:    %s", fpi_goodix_protocol_data_to_str(priv->gtls_params->symmetric_key->data, priv->gtls_params->symmetric_key->len));
            fp_dbg("session_iv:     %s", fpi_goodix_protocol_data_to_str(priv->gtls_params->symmetric_iv->data, priv->gtls_params->symmetric_iv->len));
            fp_dbg("hmac_key:       %s", fpi_goodix_protocol_data_to_str(priv->gtls_params->hmac_key->data, priv->gtls_params->hmac_key->len));
            fp_dbg("hmac_client_counter_init:    %02x", priv->gtls_params->hmac_client_counter_init);
            fp_dbg("hmac_client_counter_init:    %02x", priv->gtls_params->hmac_server_counter_init);

            GByteArray *temp = g_byte_array_new();
            g_byte_array_append(temp, priv->gtls_params->server_identity->data, priv->gtls_params->server_identity->len);
            guint8 payload[] = {0xee, 0xee, 0xee, 0xee};
            g_byte_array_append(temp, payload, 4);
            fpi_goodix_device_send_mcu(dev, 0xFF03, temp);
            g_byte_array_free(temp, TRUE);
            priv->gtls_params->state = SERVER_IDENTIFY;
            fpi_ssm_next_state(ssm);
        }
            break;
        case SERVER_DONE: {
            GByteArray *receive_mcu = fpi_goodix_device_recv_mcu(dev, 0xFF04, error);
            if (receive_mcu->data[0] != 0){
                FAIL_SSM_AND_RETURN(ssm, FPI_GOODIX_DEVICE_ERROR(SERVER_DONE, "Receive mcu error: mcu %s",
                                                                 fpi_goodix_protocol_data_to_str(receive_mcu->data, receive_mcu->len)))
            }
            priv->gtls_params->hmac_client_counter = priv->gtls_params->hmac_client_counter_init;
            priv->gtls_params->hmac_server_counter = priv->gtls_params->hmac_server_counter_init;
            priv->gtls_params->state = SERVER_DONE;
            fp_dbg("GTLS handshake successful");
            fpi_ssm_next_state(ssm);
        }
            break;
    }
}

void fpi_goodix_device_gtls_connection(FpDevice *dev, FpiSsm *parent_ssm) {
    fpi_ssm_start_subsm(parent_ssm, fpi_ssm_new(dev, fpi_goodix_device_gtls_connection_handle, ESTABLISH_CONNECTION_STATES_NUM));
}

void fpi_goodix_device_send_mcu(FpDevice *dev, const guint32 data_type, GByteArray *data) {
    fp_dbg("fpi_goodix_device_send_mcu()");
    GByteArray *payload = g_byte_array_new();
    guint32 t = data->len + 8;
    g_byte_array_append(payload, (guint8 *) &data_type, sizeof(data_type));
    g_byte_array_append(payload, (guint8 *) &t, sizeof(t));
    g_byte_array_append(payload, data->data, data->len);
    fp_dbg("mcu: %s", fpi_goodix_protocol_data_to_str(payload->data, payload->len));
    fp_dbg("payload_lenght: %d", payload->len);
    GoodixMessage *message = fpi_goodix_protocol_create_message(0xD, 1, payload->data, payload->len);
    GError *error = NULL;
    fpi_goodix_device_send(dev, message, TRUE, 500, FALSE, &error);
}

GByteArray *fpi_goodix_device_recv_mcu(FpDevice *dev, guint32 read_type, GError *error) {
    fp_dbg("recv_mcu_payload()");
    GoodixMessage *message = NULL;
    if (!fpi_goodix_device_receive_data(dev, &message, &error)
        || message->category != 0xD || message->command != 1) {
        return FALSE;
    }
    GByteArray *msg_payload = g_byte_array_new();
    g_byte_array_append(msg_payload, message->payload->data, message->payload->len);
    guint32 read_type_recv = (guint32)(msg_payload->data[0] | msg_payload->data[1] << 8 | msg_payload->data[2] << 16 | msg_payload->data[3] << 32);
    guint32 payload_size_recv  = (guint32)(msg_payload->data[4] | msg_payload->data[5] << 8 | msg_payload->data[6] << 16 | msg_payload->data[7] << 32);
    
    if (read_type != read_type_recv) {
        g_set_error(&error, 1, 1, "Wrong read_type, excepted: %02x - received: %s", read_type, fpi_goodix_protocol_data_to_str(message->payload->data, sizeof(read_type)));
        return FALSE;
    }
    if (payload_size_recv != msg_payload->len) {
        g_set_error(&error, 1, 1,"Wrong payload size, excepted: %02x - received: %s", read_type, fpi_goodix_protocol_data_to_str(message->payload->data, sizeof(payload_size_recv)));
        return FALSE;
    }
    g_byte_array_remove_range(msg_payload, 0, 8);
    g_free(message);
    return msg_payload;
}

gboolean fpi_goodix_device_fdt_execute_operation(FpDevice *dev, enum FingerDetectionOperation operation, GByteArray *fdt_base, gint timeout_ms, GError **error) {

    guint8 op_code;
    switch(operation) {

        case DOWN:
            break;
        case UP:
            break;
        case MANUAL:
            break;
    }

    GByteArray *payload = g_byte_array_new();
    g_byte_array_append(payload, &op_code, 1);
    g_byte_array_append(payload, 1, 1);
    g_byte_array_append(payload, fdt_base->data, fdt_base->len);

    GoodixMessage *message = fpi_goodix_protocol_create_message(0x3, operation, payload->data, payload->len);
    if (!fpi_goodix_device_send(dev, message, TRUE, timeout_ms, FALSE, error)) {
        return FALSE;
    }

    if (operation != MANUAL) {
        return FALSE;
    }

    return TRUE;

}

gboolean fpi_goodix_device_finger_detection_data(FpDevice *dev, enum FingerDetectionOperation operation, GByteArray *fdt_base, GError *error) {
    GoodixMessage *receive_message = NULL;
    if (!fpi_goodix_device_receive_data(dev, &receive_message, &error)) {
        return FALSE;
    }

    if (receive_message->category != 0x3 || receive_message->command != operation) {
        g_set_error(&error, 1, 1, "Not a finger detection reply. Command %02x", receive_message->command);
        return FALSE;
    }

    if (receive_message->payload->len != 28) {
        g_set_error(&error, 1, 1, "Finger detection payload wrong length. Command %02x", receive_message->command);
        return FALSE;
    }

    guint8 irq_status = receive_message->payload->data[2];

    return TRUE;
}

gboolean fpi_goodix_device_upload_config(FpDevice *dev, GByteArray *config, gint timeout_ms, GError **error) {
    GoodixMessage *message = fpi_goodix_protocol_create_message_byte_array(0x9, 0, config);
    fp_dbg("Config after %s data len %d", fpi_goodix_protocol_data_to_str(message->payload->data, message->payload->len), message->payload->len);
    if (!fpi_goodix_device_send(dev, message, TRUE, timeout_ms, FALSE, error)) {
        return FALSE;
    }

    GoodixMessage *receive_message = NULL;
    if (!fpi_goodix_device_receive_data(dev, &receive_message, error)) {
        return FALSE;
    }
    if (receive_message->category != 0x9 && receive_message->command != 0) {
        *error = FPI_GOODIX_DEVICE_ERROR(1, "Not a config message. Expected category %02x command %02x, received category %02x and command %02x", 
            0x9, 0, receive_message->category, receive_message->command); 
        return FALSE;
    }
    if (receive_message->payload->data[0] != 1) {
        *error = FPI_GOODIX_DEVICE_ERROR(1, "Upload configuration failed. Category %02x command %02x", receive_message->category, receive_message->command); 
        return FALSE;
    }
    return TRUE;
}