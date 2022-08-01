// Goodix 5395 driver protocol for libfprint

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


#define TCODE_TAG 0x5C
#define DAC_L_TAG 0x220
#define DELTA_DOWN_TAG 0x82
#define FDT_BASE_LEN 24

#define SENSOR_WIDTH 108
#define SENSOR_HEIGHT 88

typedef struct {
    GoodixMessage *message;
    gboolean reply;

    GoodixDeviceReceiveCallback callback;
    gpointer user_data;

    GoodixGTLSParams *gtls_params;
    GByteArray *psk;
    GoodixCalibrationParam *calibration_params;
} FpiGoodixDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FpiGoodixDevice, fpi_goodix_device, FP_TYPE_IMAGE_DEVICE);

// ----- INIT CLASS SECTION ------

static void fpi_goodix_device_init(FpiGoodixDevice *self) {
    FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(self);
    priv->reply = FALSE;
    priv->callback = NULL;
    priv->user_data = NULL;
    priv->gtls_params = NULL;
    priv->calibration_params = NULL;
}

static void fpi_goodix_device_class_init(FpiGoodixDeviceClass *class) {}

// ----- CALLBAKS -----

// ===================================== START PRIVATE METHOD ===================================== //

// TODO: When a message is sent it's automatically freed, so for now it isn't possible use this function 
//static gboolean fpi_goodix_device_check_receive_data(GoodixMessage *send_message, GoodixMessage *receive_message, GError **error) {
//     gboolean is_success_reply = send_message->category == receive_message->category 
//                                     && send_message->command == receive_message->command;

//     if (!is_success_reply) {
//         *error = FPI_GOODIX_DEVICE_ERROR(1, "Category and command are different for send and receive message. \n Send message category %02x, command %02x. \n Receive message category %02x, command %02x", send_message->category, send_message->command, receive_message->category, receive_message->command);
//     }
//     return is_success_reply; 
// }
static void fpi_goodix5395_replace_value_in_section(GByteArray *config, guint8 section_num, guint tag, guint16 value) {
    guint8 *section_table = config->data;
    guint section_base = section_table[section_num + 1];
    guint section_size = section_table[section_num + 2];
    fp_dbg("Section base %d", section_base);
    guint entry_base = section_base;
    while(entry_base <= section_base + section_size) {
        guint entry_tag = config->data[entry_base] | config->data[entry_base + 1] << 8;
        if (entry_tag == tag) {
            config->data[entry_base + 2] = value & 0xff;
            config->data[entry_base + 3] = value >> 8;
        }
        entry_base += 4;
    }
}

static gboolean fpi_goodix_device_receive_chunk(FpDevice *dev, GByteArray *data, guint timeout_ms, GError **error) {
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDeviceClass *class = FPI_GOODIX_DEVICE_GET_CLASS(self);
    FpiUsbTransfer *transfer = fpi_usb_transfer_new(dev);

    transfer->short_is_error = FALSE;

    fpi_usb_transfer_fill_bulk(transfer, class->ep_in, GOODIX_EP_IN_MAX_BUF_SIZE);

    gboolean success = fpi_usb_transfer_submit_sync(transfer, timeout_ms, error);

    while(success && transfer->actual_length == 0) {
        success = fpi_usb_transfer_submit_sync(transfer, timeout_ms, error);
    }

    if (success) {
        g_byte_array_append(data, transfer->buffer, transfer->actual_length);
        // fpi_goodix_protocol_debug_data("Received chunk %s", transfer->buffer, transfer->actual_length);
    }
    fpi_usb_transfer_unref(transfer);
    return success;
}

static GoodixFtdEvent* fpi_goodix_device_get_finger_detection_data(FpDevice *dev, enum FingerDetectionOperation fdt_op, guint timeout_ms, GError **error){
    g_autofree GoodixMessage *reply = NULL;
    if (!fpi_goodix_device_receive(dev, &reply, timeout_ms, error) || reply->category != 0x3 || reply->command != fdt_op) {
        //TODO raise Exception("Not a finger detection reply")

        return FALSE;
    }
    if(reply->payload->len != 28) {
        //TODO raise Exception("Finger detection payload wrong length")
        return FALSE;
    }
    // TODO check if g_autofree works correctly otherwise create a copy
    // GByteArray *payload = g_byte_array_new();
    // g_byte_array_append(payload, reply->payload->data, reply->payload->len);
    // fpi_goodix_protocol_free_message(reply);

    guint16 irq_status = reply->payload->data[0] | reply->payload->data[1] << 8;
    fp_dbg("IRQ status: %#02x", irq_status);

    guint16 touch_flag = reply->payload->data[3] | reply->payload->data[4] << 8;
    fp_dbg("Touch flag: %#02x", touch_flag);

    g_byte_array_remove_range(reply->payload, 0, 4);

    GoodixFtdEvent *event = g_malloc(sizeof(GoodixFtdEvent));
    event->ftd_data = g_byte_array_new();
    g_byte_array_append(event->ftd_data, reply->payload->data, reply->payload->len);
    event->touch_flag = touch_flag;

    return event;
}


static GByteArray *fpi_goodix_device_execute_fdt_operation(FpDevice *dev, enum FingerDetectionOperation fdt_op, GByteArray *fdt_base, gint timeout_ms, GError **error){
    guint8 op_code = 0xD;
    switch (fdt_op) {
        case DOWN:
            g_assert(fdt_base->len == 24);
            op_code = 0xC;
            break;
        case UP:
            g_assert(fdt_base->len == 24);
            op_code = 0xE;
            break;
        case MANUAL:
            g_assert(fdt_base->len == 25);
            op_code = fdt_base->data[0];
            g_byte_array_remove_index(fdt_base, 0);
            timeout_ms = 500;
            break;
    }
    GByteArray *payload = g_byte_array_new();
    guint8 one = 0x1;
    g_byte_array_append(payload, &op_code, 1);
    g_byte_array_append(payload, &one, 1);
    g_byte_array_append(payload, fdt_base->data, fdt_base->len);
    GoodixMessage *message = fpi_goodix_protocol_create_message_byte_array(0x3, fdt_op, payload);
    if (!fpi_goodix_device_send(dev, message, TRUE, timeout_ms, FALSE, error)) {
        return FALSE;
    }
    if (fdt_op != MANUAL) {
        return NULL;
    }
    GoodixFtdEvent *fdt_event = fpi_goodix_device_get_finger_detection_data(dev, fdt_op, timeout_ms, error);
    return fdt_event->ftd_data;
}

static GByteArray *fpi_goodix_device_generate_fdt_up_base(GoodixFtdEvent *event, guint8 delta_down, guint8 delta_up){
    guint16 fdt_val;
    const gint buffer_size = event->ftd_data->len / 2;
    guint16 *buffer = g_malloc0(buffer_size);
    for (gint i = 0; i < buffer_size; i++) {
        buffer[i] = event->ftd_data->data[2 * i] | event->ftd_data->data[2 * i + 1] << 8;
    }
    
    for(gint i = 0; i < buffer_size; i++) {
        fdt_val = (buffer[i] >> 1) + delta_down;
        fdt_val = fdt_val * 0x100 | fdt_val;
        buffer[i] = fdt_val;
    }

    for(gint i = 0; i < 0xc; i++){
        if(((event->touch_flag >> i) & 1) == 0) {
            buffer[i] = delta_up * 0x100 | delta_up;
        }
    }

    GByteArray *fdt_base_up_vals_update = g_byte_array_new();
    guint8 encode_buffer[2] = {};
    for (gint i = 0; i < buffer_size; i++) {
        encode_buffer[0] = buffer[i] & 0xFF;
        encode_buffer[1] = buffer[i] >> 8;
        g_byte_array_append(fdt_base_up_vals_update, encode_buffer, 2);
    }
    g_free(buffer);
    return fdt_base_up_vals_update;
}

static GoodixFtdEvent *fpi_goodix_device_wait_fdt_event(FpDevice *dev, enum FingerDetectionOperation fdo, guint timeout_ms, GError **error) {
    return fpi_goodix_device_get_finger_detection_data(dev, fdo, timeout_ms, error);
}

static GByteArray *fpi_goodix_device_get_and_decrypt_image(FpDevice *dev, GByteArray *request, gint timeout_ms, GError **error) {
    g_assert(request->len == 4);
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(self);
    GoodixMessage *message = fpi_goodix_protocol_create_message_byte_array(0x2, 0, request);
    if (!fpi_goodix_device_send(dev, message, TRUE, timeout_ms, FALSE, error)) {
        return NULL;
    }
    message = NULL;
    if (!fpi_goodix_device_receive(dev, &message, timeout_ms, error) || message->category != 0x2 || message->command != 0) {
//      TODO  raise Exception("Not an image message")
        return NULL;
    }

//  TODO       if self.gtls_context is None or not self.gtls_context.is_connected():
//             raise Exception("Invalid GTLS connection state")
    return fpi_goodix_gtls_decrypt_sensor_data(priv->gtls_params, message->payload, error);
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
        // fpi_goodix_protocol_debug_data("Chunk sent %s", buffer->data, GOODIX_EP_OUT_MAX_BUF_SIZE);
        g_byte_array_free(buffer, FALSE);
        fpi_usb_transfer_unref(transfer);
    }

    g_free(data);
    return TRUE;
}

static void fpi_goodix_device_empty_buffer(FpDevice *dev) {
    while (fpi_goodix_device_receive_chunk(dev, NULL, 200, NULL)) {

    }
}

static void fpi_goodix5395_fix_config_checksum(GByteArray *config) {
    guint checksum = 0xA5A5;
    for (guint short_index = 0; short_index < config->len - 2; short_index += 2) {
        guint s = config->data[short_index] | config->data[short_index + 1] << 8;
        checksum += s;
        checksum &= 0xFFFF;
    }
    checksum = 0x10000 - checksum;
    config->data[config->len - 2] = checksum & 0xff;
    config->data[config->len - 1] = checksum >> 8;
}

// ===================================== END PRIVATE METHOD ===================================== //

// ===================================== START PUBLIC METHOD ===================================== //

gboolean fpi_goodix_device_init_device(FpDevice *dev, GError **error) {
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDeviceClass *class = FPI_GOODIX_DEVICE_GET_CLASS(self);
    return g_usb_device_claim_interface(fpi_device_get_usb_device(dev), class->interface, G_USB_DEVICE_CLAIM_INTERFACE_NONE, error);
}

gboolean fpi_goodix_device_deinit_device(FpDevice *dev, GError **error) {
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDeviceClass *class = FPI_GOODIX_DEVICE_GET_CLASS(self);
    // TODO FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(self);

    return g_usb_device_release_interface(fpi_device_get_usb_device(dev),
                                          class->interface, 0, error);
}

gboolean fpi_goodix_device_ping(FpDevice *dev, GError **error) {
    fpi_goodix_device_empty_buffer(dev);
    guint8 payload[] = {0, 0};
    return fpi_goodix_device_send(dev, fpi_goodix_protocol_create_message(0, 0x00, payload, 2), TRUE, 500, FALSE, error);
}

gboolean fpi_goodix_device_receive(FpDevice *dev, GoodixMessage **message, guint timeout_ms, GError **error) {
    GByteArray *buffer = g_byte_array_new();
    glong message_length = 0;
    guint8 command_byte;
    if (fpi_goodix_device_receive_chunk(dev, buffer, timeout_ms, error)) {
        GoodixDevicePack *pack = (GoodixDevicePack *) buffer->data;
        command_byte = pack->cmd;
        message_length = pack->length;
    } else {
        return FALSE;
    }

    while (buffer->len - 1 < message_length) {
        GByteArray *chunk_buffer = g_byte_array_new();
        fpi_goodix_device_receive_chunk(dev, chunk_buffer, timeout_ms, error);
        guint8 contd_command_byte = chunk_buffer->data[0];
        if ((contd_command_byte & 0xFE) != command_byte){
            g_set_error(error, 1, 1,"Wrong contd_command_byte: expected %02x, received %02x", command_byte, contd_command_byte);
        }
        g_byte_array_remove_index(chunk_buffer, 0);
        g_byte_array_append(buffer, chunk_buffer->data, chunk_buffer->len);
        g_byte_array_free(chunk_buffer, TRUE);
    }
    gboolean success = fpi_goodix_protocol_decode(buffer->data, message, error);

    g_byte_array_free(buffer, TRUE);
    return success;
}

gboolean fpi_goodix_device_send(FpDevice *dev, GoodixMessage *message, gboolean calc_checksum,
                            guint timeout_ms, gboolean reply, GError **error) {
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(self);
    guint8 *data;
    guint32 data_len;

    fp_dbg("Running command: 0x%02x", message->command);

    priv->message = message;
    priv->reply = reply;

    fpi_goodix_protocol_encode(message, calc_checksum, TRUE, &data, &data_len);
    fpi_goodix_protocol_free_message(message);

    gboolean is_success = fpi_goodix_device_write(dev, data, data_len, timeout_ms, error);

    if (is_success) {
        GoodixMessage *ackMessage = NULL;
        is_success = fpi_goodix_device_receive(dev, &ackMessage, timeout_ms, error);
        if (is_success) {
            is_success = fpi_goodix_protocol_check_ack(ackMessage, error);
            fpi_goodix_protocol_free_message(ackMessage);
        }
    }

    return is_success;
}

gboolean fpi_goodix_device_reset(FpDevice *dev, guint8 reset_type, gboolean irq_status) {
    g_assert(reset_type < 3);
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

void fpi_goodix_device_send_mcu(FpDevice *dev, const guint32 data_type, GByteArray *data) {
    //TODO FIXME: error management
    fp_dbg("fpi_goodix_device_send_mcu()");
    GByteArray *payload = g_byte_array_new();
    guint32 t = data->len + 8;
    g_byte_array_append(payload, (guint8 *) &data_type, sizeof(data_type));
    g_byte_array_append(payload, (guint8 *) &t, sizeof(t));
    g_byte_array_append(payload, data->data, data->len);
    fpi_goodix_protocol_debug_data("mcu: %s", payload->data, payload->len);
    fp_dbg("payload_lenght: %d", payload->len);
    GoodixMessage *message = fpi_goodix_protocol_create_message(0xD, 1, payload->data, payload->len);
    GError *error = NULL;
    fpi_goodix_device_send(dev, message, TRUE, 500, FALSE, &error);
}

GByteArray *fpi_goodix_device_recv_mcu(FpDevice *dev, guint32 read_type, GError *error) {
    fp_dbg("recv_mcu_payload()");
    GoodixMessage *message = NULL;
    if (!fpi_goodix_device_receive(dev, &message, 2000, &error)
        || message->category != 0xD || message->command != 1) {
        return FALSE;
    }
    GByteArray *msg_payload = g_byte_array_new();
    g_byte_array_append(msg_payload, message->payload->data, message->payload->len);
    fpi_goodix_protocol_free_message(message);
    guint32 read_type_recv = (guint32)(msg_payload->data[0] | msg_payload->data[1] << 8 | msg_payload->data[2] << 16 | msg_payload->data[3] << 24);
    guint32 payload_size_recv  = (guint32)(msg_payload->data[4] | msg_payload->data[5] << 8 | msg_payload->data[6] << 16 | msg_payload->data[7] << 24);
    
    if (read_type != read_type_recv) {
        g_autofree gchar *data_string = fpi_goodix_protocol_data_to_str(message->payload->data, sizeof(read_type));
        g_set_error(&error, 1, 1, "Wrong read_type, excepted: %02x - received: %s", read_type, data_string);
        return FALSE;
    }
    if (payload_size_recv != msg_payload->len) {
        g_autofree gchar *data_string = fpi_goodix_protocol_data_to_str(message->payload->data, sizeof(payload_size_recv));
        g_set_error(&error, 1, 1,"Wrong payload size, excepted: %02x - received: %s", read_type, data_string);
        return FALSE;
    }
    g_byte_array_remove_range(msg_payload, 0, 8);
    return msg_payload;
}

gboolean fpi_goodix_device_upload_config(FpDevice *dev, GByteArray *config, gint timeout_ms, GError **error) {
    GoodixMessage *message = fpi_goodix_protocol_create_message_byte_array(0x9, 0, config);
    if (!fpi_goodix_device_send(dev, message, TRUE, timeout_ms, FALSE, error)) {
        return FALSE;
    }

    GoodixMessage *receive_message = NULL;
    if (!fpi_goodix_device_receive(dev, &receive_message, 500, error)) {
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

void fpi_goodix_device_prepare_config(FpDevice *dev, GByteArray *config) {
    FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(FPI_GOODIX_DEVICE(dev));
    
    guint8 tcode = priv->calibration_params->tcode;
    guint8 dac_l = priv->calibration_params->dac_l;
    guint8 delta_down = priv->calibration_params->delta_down;
    fp_dbg("tcode is %02x", tcode);
    fpi_goodix5395_replace_value_in_section(config, 4, TCODE_TAG, tcode);
    fpi_goodix5395_replace_value_in_section(config, 6, TCODE_TAG, tcode);
    fpi_goodix5395_replace_value_in_section(config, 8, TCODE_TAG, tcode);

    fpi_goodix5395_replace_value_in_section(config, 4, DAC_L_TAG, dac_l << 4 | 8);
    fpi_goodix5395_replace_value_in_section(config, 6, DAC_L_TAG, dac_l << 4 | 8);
    fpi_goodix5395_replace_value_in_section(config, 4, DELTA_DOWN_TAG, delta_down << 8 | 0x80);
    fpi_goodix5395_fix_config_checksum(config);
}

void fpi_goodix_device_set_calibration_params(FpDevice* dev, GByteArray* payload) {
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(self);

    guint8 *otp = payload->data;
    guint8 diff = otp[17] >> 1 & 0x1F;
    fp_dbg("[0x11]:%02x, diff[5:1]=%02x", otp[0x11], diff);
    guint16 tcode = otp[23] != 0 ? otp[23] + 1 : 0;

    GoodixCalibrationParam *params = g_malloc0(sizeof(GoodixCalibrationParam));

    params->tcode = tcode;
    params->delta_fdt = 0;
    params->delta_down = 0xD;
    params->delta_up = 0xB;
    params->delta_img = 0xC8;
    params->delta_nav = 0x28;

    params->dac_h = (otp[17] << 8 ^ otp[22]) & 0x1FF;
    params->dac_l = (otp[17] & 0x40) << 2 ^ otp[31];

    if (diff != 0) {
        guint8 tmp = diff + 5;
        guint8 tmp2 = (tmp * 0x32) >> 4;

        params->delta_fdt = tmp2 / 5;
        params->delta_down = tmp2 / 3;
        params->delta_up = params->delta_down - 2;
        params->delta_img = 0xC8;
        params->delta_nav = tmp * 4;
    }

    if (otp[17] == 0 || otp[22] == 0 || otp[31] == 0) {
        params->dac_h = 0x97;
        params->dac_l = 0xD0;
    }

    fp_dbg("tcode:%02x delta down:%02x", tcode, params->delta_down);
    fp_dbg("delta up:%02x delta img:%02x", params->delta_up, params->delta_img);
    fp_dbg("delta nav:%02x dac_h:%02x dac_l:%02x", params->delta_nav, params->dac_h, params->dac_l);

    params->dac_delta = 0xC83 / tcode;
    fp_dbg("sensor broken dac_delta=%02x", params->dac_delta);

    //TODO: maybe it doesn't need to allocate different GByteArray for all variables
    guint8 t[FDT_BASE_LEN] = {0};
    params->fdt_base_down = g_byte_array_new();
    g_byte_array_append(params->fdt_base_down, t, FDT_BASE_LEN);

    params->fdt_base_up = g_byte_array_new();
    g_byte_array_append(params->fdt_base_up, t, FDT_BASE_LEN);

    params->fdt_base_manual = g_byte_array_new();
    g_byte_array_append(params->fdt_base_manual, t, FDT_BASE_LEN);


    priv->calibration_params = params;
}

gboolean fpi_goodix_device_set_sleep_mode(FpDevice *dev, GError **error) {
    guint8 payload[] = {0x01, 0x00};
    GoodixMessage *message = fpi_goodix_protocol_create_message(0x6, 0, payload, sizeof(payload));
    return fpi_goodix_device_send(dev, message, TRUE, 200, FALSE, error);
}

gboolean fpi_goodix_device_ec_control(FpDevice *dev, gboolean is_enable, gint timeout_ms, GError **error) {
    guint8 control_val = is_enable ? 1 : 0;
    guint8 payload[] = {control_val, control_val, 0x00};

    GoodixMessage *message = fpi_goodix_protocol_create_message(0xA, 7, payload, sizeof(payload));
    if(!fpi_goodix_device_send(dev, message, TRUE, timeout_ms, FALSE, error)) {
        return FALSE;
    }

    GoodixMessage *receive_message = NULL;
    if(!fpi_goodix_device_receive(dev, &receive_message, 500, error)) {
        return FALSE;
    }

    // TODO See comment above fpi_goodix_device_check_receive_data
    //if (!fpi_goodix_device_check_receive_data(message, receive_message, error)) {
    //     return FALSE;
    // }

    gboolean is_ec_control_success = receive_message->payload->data[0] != 1;

    if (is_ec_control_success) {
        *error = FPI_GOODIX_DEVICE_ERROR(1, "EC control failed for state %d", is_enable);
        return FALSE;
    }

    return TRUE;
}

GByteArray *fpi_goodix_device_get_fdt_base_with_tx(FpDevice *dev, gboolean tx_enable, GError **error){
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(self);

    GByteArray *payload = g_byte_array_new();
    guint8 op_code = 0xD;
    if(!tx_enable) {
        op_code |= 0x80;
    }
    g_byte_array_append(payload, &op_code, 1);
    g_byte_array_append(payload, priv->calibration_params->fdt_base_manual->data, priv->calibration_params->fdt_base_manual->len);
    fpi_goodix_protocol_debug_data("FDT manual %s", priv->calibration_params->fdt_base_manual->data, priv->calibration_params->fdt_base_manual->len);
    GByteArray *fdt_base = fpi_goodix_device_execute_fdt_operation(dev, MANUAL, payload, 500, error);
    if(fdt_base->len == 0) {
        //TODO error
    }
    return fdt_base;
}

GArray *fpi_goodix_device_get_image(FpDevice *dev, gboolean tx_enable, gboolean hv_enable, gchar use_dac, gboolean adjust_dac, gboolean is_finger, GError **error){
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(self);
    guint8 op_code;
    guint8 hv_value;
    if (tx_enable) {
        op_code = 0x1;
    } else {
        op_code = 0x81;
    }
    if(is_finger){
        op_code = op_code | (guint8)0x40;
    }
    if (hv_enable){
        hv_value = 0x6;
    } else {
        hv_value = 0x10;
    }
    guint16 dac;
    if (use_dac == 'h') {
        dac = priv->calibration_params->dac_h;
    } else if(use_dac == 'l'){
        dac = priv->calibration_params->dac_l;
    } else {
//   TODO      raise Exception("Invalid DAC type")
    }
    GByteArray *request = g_byte_array_new();
    g_byte_array_append(request, &op_code, 1);
    g_byte_array_append(request, &hv_value, 1);
    g_byte_array_append(request, (guint8 *)&dac, 2);
    return fpi_goodix_protocol_decode_image(fpi_goodix_device_get_and_decrypt_image(dev, request, 500, error));
}

// TODO GoodixCalibrationParam *fpi_goodix_device_get_calibration_params(FpDevice *dev) {
//     FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
//     FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(self);
//     return priv->calibration_params;
// }

void fpi_goodix_device_update_bases(FpDevice *dev, GByteArray *fdt_base) {
    //TODO error management
    g_assert(fdt_base->len == FDT_BASE_LEN);
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(self);
    priv->calibration_params->fdt_base_down = fdt_base;
    priv->calibration_params->fdt_base_up = fdt_base;
    priv->calibration_params->fdt_base_manual = fdt_base;
}

gboolean fpi_goodix_device_is_fdt_base_valid(FpDevice *dev, GByteArray *fdt_data_1, GByteArray *fdt_data_2) {
    g_assert(fdt_data_1->len == fdt_data_2->len);
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(self);
    guint16 fdt_val_1, fdt_val_2;
    gint16 delta;
    fp_dbg("Checking FDT data, max delta: %d", priv->calibration_params->delta_fdt);
    for (int i = 0; i < fdt_data_1->len; i = i + 2) {
        fdt_val_1 = fdt_data_1->data[i] | fdt_data_1->data[i + 1] << 8;
        fdt_val_2 = fdt_data_2->data[i] | fdt_data_2->data[i + 1] << 8;

        delta = abs((fdt_val_2 >> 1) - (fdt_val_1 >> 1));
        if (delta > priv->calibration_params->delta_fdt) return FALSE;
    }
    return TRUE;
}

gboolean fpi_goodix_device_validate_base_img(FpDevice *dev, GArray *base_image_1, GArray *base_image_2) {
    //TODO error management
    g_assert(base_image_1->len == SENSOR_WIDTH * SENSOR_HEIGHT);
    g_assert(base_image_2->len == SENSOR_WIDTH * SENSOR_HEIGHT);
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(self);
    guint8 image_val_1, image_val_2;
    gint offset, diff_sum = 0;
    const guint8 image_threshold = priv->calibration_params->delta_img;
    for (gint row_idx = 2; row_idx < SENSOR_HEIGHT - 2; row_idx += 2) {
        for (gint col_idx = 2; col_idx < SENSOR_HEIGHT - 2; col_idx += 2) {
            offset = row_idx * SENSOR_WIDTH + col_idx;
            image_val_1 = base_image_1->data[offset];
            image_val_2 = base_image_2->data[offset];
            diff_sum += abs(image_val_2 - image_val_1);
        }
    }
    gdouble avg = diff_sum / ((SENSOR_HEIGHT - 4) * (SENSOR_WIDTH - 4));
    fp_dbg("Checking image data, avg: %.2f, threshold: %d", avg, image_threshold);
    if (avg > image_threshold){
        return FALSE;
    }
    return TRUE;
}

GByteArray *fpi_goodix_device_generate_fdt_base(GByteArray *fdt_data){
    GByteArray *fdt_base = g_byte_array_new();
    guint16 fdt_val, fdt_base_val;
    for (int idx = 0; idx < fdt_data->len; idx += 2) {
        fdt_val = fdt_data->data[idx] | fdt_data->data[idx + 1] << 8;
        fdt_base_val = (fdt_val & 0xFFFE) * 0x80 | fdt_val >> 1;
        g_byte_array_append(fdt_base, (guint8*)(&fdt_base_val), 2);
    }
    fp_dbg("Generated fdt base %d", fdt_base->len);
    return fdt_base;
}

void fpi_device_update_fdt_bases(FpDevice *dev, GByteArray *fdt_base){
    //TODO error management
    g_assert(fdt_base->len == FDT_BASE_LEN);
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(self);
    if(priv->calibration_params->fdt_base_down != NULL) {
        g_byte_array_free(priv->calibration_params->fdt_base_down, TRUE);
    }
    if(priv->calibration_params->fdt_base_manual != NULL) {
        g_byte_array_free(priv->calibration_params->fdt_base_manual, TRUE);
    }
    if(priv->calibration_params->fdt_base_up != NULL) {
        g_byte_array_free(priv->calibration_params->fdt_base_up, TRUE);
    }
    priv->calibration_params->fdt_base_down = g_byte_array_new_take(fdt_base->data, fdt_base->len);
    priv->calibration_params->fdt_base_manual = g_byte_array_new_take(fdt_base->data, fdt_base->len);
    priv->calibration_params->fdt_base_up = g_byte_array_new_take(fdt_base->data, fdt_base->len);
    fpi_goodix_protocol_debug_data("FDT manual base: %s", priv->calibration_params->fdt_base_manual->data, priv->calibration_params->fdt_base_manual->len);
}

void fpi_device_update_calibration_image(FpDevice *dev, GArray *calib_image) {
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(self);
    if(priv->calibration_params->calib_image != NULL) {
        g_byte_array_free(priv->calibration_params->calib_image, TRUE);
    }
    priv->calibration_params->calib_image = calib_image;
}

void fpi_goodix_device_setup_finger_position_detection(FpDevice *dev, enum FingerDetectionOperation fdo, gint timeout_ms, GError **error){
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(self);
    GByteArray *fdt_base = fdo == DOWN ? priv->calibration_params->fdt_base_down : priv->calibration_params->fdt_base_up;
    fpi_goodix_device_execute_fdt_operation(dev, fdo, fdt_base, timeout_ms, error);
}

GByteArray *fpi_goodix_device_wait_for_finger(FpDevice *dev, guint timeout_ms, enum FingerDetectionOperation fdo, GError **error) {
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(self);
    //TODO check if it's freed also the memory area returned
    g_autofree GoodixFtdEvent *event = fpi_goodix_device_wait_fdt_event(dev, fdo, timeout_ms, error);
    if (fdo == UP) {
        g_byte_array_free(priv->calibration_params->fdt_base_down, TRUE);
        priv->calibration_params->fdt_base_down = fpi_goodix_device_generate_fdt_base(event->ftd_data);
    } else {
        g_byte_array_free(priv->calibration_params->fdt_base_up, TRUE);
        priv->calibration_params->fdt_base_up = fpi_goodix_device_generate_fdt_up_base(event, priv->calibration_params->delta_down, priv->calibration_params->delta_up);   
    }
    return event->ftd_data;
}

GArray *fpi_goodix_device_get_background_image(FpDevice *dev) {
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(self);
    return priv->calibration_params->calib_image;
}

// ----- GOODIX GTLS CONNECTION ------
void fpi_goodix_device_gtls_connection_handle(FpiSsm *ssm, FpDevice* dev) {
    GError *error = NULL;
    FpiGoodixDevice *self = FPI_GOODIX_DEVICE(dev);
    FpiGoodixDevicePrivate *priv = fpi_goodix_device_get_instance_private(self);

    switch (fpi_ssm_get_cur_state(ssm)) {
        case CLIENT_HELLO: {
            priv->gtls_params = fpi_goodix_device_gtls_init_params();
            priv->gtls_params->client_random = fpi_goodix_gtls_create_hello_message();
            fpi_goodix_protocol_debug_data("client_random: %s", priv->gtls_params->client_random->data, priv->gtls_params->client_random->len);
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
            fpi_goodix_protocol_debug_data("server_random: %s", priv->gtls_params->server_random->data, priv->gtls_params->server_random->len);
            fpi_goodix_protocol_debug_data("server_identity: %s", priv->gtls_params->server_identity->data, priv->gtls_params->server_identity->len);

            if(!fpi_goodix_gtls_derive_key(priv->gtls_params)) {
                fpi_goodix_protocol_debug_data("client_identity: %s", priv->gtls_params->client_identity->data, priv->gtls_params->client_identity->len);
                g_autofree gchar *client_identity_string = fpi_goodix_protocol_data_to_str(priv->gtls_params->client_identity->data, priv->gtls_params->client_identity->len);
                g_autofree gchar *server_identity_string = fpi_goodix_protocol_data_to_str(priv->gtls_params->server_identity->data, priv->gtls_params->server_identity->len);
                FAIL_SSM_AND_RETURN(ssm, FPI_GOODIX_DEVICE_ERROR(SERVER_IDENTIFY, "Client and server identity don't match. client identity: %s, server identity: %s ",
                                                                 client_identity_string,
                                                                 server_identity_string))
            }
            fpi_goodix_protocol_debug_data("session_key:    %s", priv->gtls_params->symmetric_key->data, priv->gtls_params->symmetric_key->len);
            fpi_goodix_protocol_debug_data("session_iv:     %s", priv->gtls_params->symmetric_iv->data, priv->gtls_params->symmetric_iv->len);
            fpi_goodix_protocol_debug_data("hmac_key:       %s", priv->gtls_params->hmac_key->data, priv->gtls_params->hmac_key->len);
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
                g_autofree gchar *data_string = fpi_goodix_protocol_data_to_str(receive_mcu->data, receive_mcu->len);
                FAIL_SSM_AND_RETURN(ssm, FPI_GOODIX_DEVICE_ERROR(SERVER_DONE, "Receive mcu error: mcu %s", data_string))
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
// ----- GOODIX GTLS CONNECTION END ------
