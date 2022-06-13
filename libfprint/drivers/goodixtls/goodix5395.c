// Goodix 5395 driver for libfprint

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

#define FP_COMPONENT "goodixtls5395"

#include <glib.h>
#include <string.h>
#include <drivers_api.h>

#include "5395/goodix_device.h"
#include "goodix5395.h"
#include "5395/crypto_utils.h"

#define FIRMWARE_VERSION_1 "GF5288_HTSEC_APP_10011"
#define FIRMWARE_VERSION_2 "GF5288_HTSEC_APP_10020"

#define FDT_BASE_LEN 24

const guint8 goodix_5395_otp_hash[] = {
        0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15, 0x38, 0x3f, 0x36, 0x31, 0x24, 0x23, 0x2a, 0x2d,
        0x70, 0x77, 0x7e, 0x79, 0x6c, 0x6b, 0x62, 0x65, 0x48, 0x4f, 0x46, 0x41, 0x54, 0x53, 0x5a, 0x5d,
        0xe0, 0xe7, 0xee, 0xe9, 0xfc, 0xfb, 0xf2, 0xf5, 0xd8, 0xdf, 0xd6, 0xd1, 0xc4, 0xc3, 0xca, 0xcd,
        0x90, 0x97, 0x9e, 0x99, 0x8c, 0x8b, 0x82, 0x85, 0xa8, 0xaf, 0xa6, 0xa1, 0xb4, 0xb3, 0xba, 0xbd,
        0xc7, 0xc0, 0xc9, 0xce, 0xdb, 0xdc, 0xd5, 0xd2, 0xff, 0xf8, 0xf1, 0xf6, 0xe3, 0xe4, 0xed, 0xea,
        0xb7, 0xb0, 0xb9, 0xbe, 0xab, 0xac, 0xa5, 0xa2, 0x8f, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9d, 0x9a,
        0x27, 0x20, 0x29, 0x2e, 0x3b, 0x3c, 0x35, 0x32, 0x1f, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0d, 0x0a,
        0x57, 0x50, 0x59, 0x5e, 0x4b, 0x4c, 0x45, 0x42, 0x6f, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7d, 0x7a,
        0x89, 0x8e, 0x87, 0x80, 0x95, 0x92, 0x9b, 0x9c, 0xb1, 0xb6, 0xbf, 0xb8, 0xad, 0xaa, 0xa3, 0xa4,
        0xf9, 0xfe, 0xf7, 0xf0, 0xe5, 0xe2, 0xeb, 0xec, 0xc1, 0xc6, 0xcf, 0xc8, 0xdd, 0xda, 0xd3, 0xd4,
        0x69, 0x6e, 0x67, 0x60, 0x75, 0x72, 0x7b, 0x7c, 0x51, 0x56, 0x5f, 0x58, 0x4d, 0x4a, 0x43, 0x44,
        0x19, 0x1e, 0x17, 0x10, 0x05, 0x02, 0x0b, 0x0c, 0x21, 0x26, 0x2f, 0x28, 0x3d, 0x3a, 0x33, 0x34,
        0x4e, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5c, 0x5b, 0x76, 0x71, 0x78, 0x7f, 0x6a, 0x6d, 0x64, 0x63,
        0x3e, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2c, 0x2b, 0x06, 0x01, 0x08, 0x0f, 0x1a, 0x1d, 0x14, 0x13,
        0xae, 0xa9, 0xa0, 0xa7, 0xb2, 0xb5, 0xbc, 0xbb, 0x96, 0x91, 0x98, 0x9f, 0x8a, 0x8d, 0x84, 0x83,
        0xde, 0xd9, 0xd0, 0xd7, 0xc2, 0xc5, 0xcc, 0xcb, 0xe6, 0xe1, 0xe8, 0xef, 0xfa, 0xfd, 0xf4, 0xf3
};

const guint8 goodix5395_psk_white_box[] = {
        0xec, 0x35, 0xae, 0x3a, 0xbb, 0x45, 0xed, 0x3f, 0x12, 0xc4, 0x75, 0x1f, 0x1e, 0x5c, 0x2c, 0xc0, 0x5b, 0x3c, 0x54, 0x52, 0xe9, 0x10, 0x4d, 0x9f, 0x2a, 0x31, 0x18, 0x64, 0x4f, 0x37, 0xa0, 0x4b,
        0x6f, 0xd6, 0x6b, 0x1d, 0x97, 0xcf, 0x80, 0xf1, 0x34, 0x5f, 0x76, 0xc8, 0x4f, 0x03, 0xff, 0x30, 0xbb, 0x51, 0xbf, 0x30, 0x8f, 0x2a, 0x98, 0x75, 0xc4, 0x1e, 0x65, 0x92, 0xcd, 0x2a, 0x2f, 0x9e,
        0x60, 0x80, 0x9b, 0x17, 0xb5, 0x31, 0x60, 0x37, 0xb6, 0x9b, 0xb2, 0xfa, 0x5d, 0x4c, 0x8a, 0xc3, 0x1e, 0xdb, 0x33, 0x94, 0x04, 0x6e, 0xc0, 0x6b, 0xbd, 0xac, 0xc5, 0x7d, 0xa6, 0xa7, 0x56, 0xc5
};


struct _FpiDeviceGoodixTls5395 {
    FpiGoodixDevice parent;
};

G_DECLARE_FINAL_TYPE(FpiDeviceGoodixTls5395, fpi_device_goodixtls5395, FPI, DEVICE_GOODIXTLS5395, FpiGoodixDevice);

G_DEFINE_TYPE(FpiDeviceGoodixTls5395, fpi_device_goodixtls5395, FPI_TYPE_GOODIX_DEVICE)

// ---- ACTIVE SECTION START ----

enum Goodix5395InitState {
  INIT_DEVICE,
  CHECK_FIRMWARE,
  DEVICE_ENABLE,
  CHECK_SENSOR,
  CHECK_PSK,
  WRITE_PSK,
  ESTABLISH_GTS_CONNECTION,
  UPDATE_ALL_BASE,
  SET_SLEEP_MODE,
  DEVICE_INIT_NUM_STATES,
};

static void fpi_goodix_5395_activate_complete(FpiSsm *ssm, FpDevice *dev, GError *error) {
    FpImageDevice *image_dev = FP_IMAGE_DEVICE(dev);

    fpi_image_device_activate_complete(image_dev, error);

    if (!error) {
//        fpi_ssm_start(fpi_ssm_new(dev, goodix_tls_run_state, TLS_NUM_STATES), goodix_tls_complete);
    }
}

static void fpi_device_goodixtls5395_ping(FpDevice *dev, FpiSsm *ssm) {
    fpi_goodix_device_empty_buffer(dev);
    guint8 payload[] = {0, 0};
    GoodixMessage *message = fpi_goodix_protocol_create_message(0, 0x00, payload, 2);
    GError *error = NULL;
    if (fpi_goodix_device_send(dev, message, TRUE, 500, FALSE, &error)) {
        fpi_ssm_next_state(ssm);
    } else {
        fpi_ssm_mark_failed(ssm, error);
    }
}

static void fpi_device_goodixtls5395_device_enable(FpDevice *dev, FpiSsm *ssm) {
    fpi_goodix_device_reset(dev, 0, FALSE);
    guint8 payload[] = {0, 0, 0, 4};
    GoodixMessage *enable_message = fpi_goodix_protocol_create_message(0x8, 0x1, payload, 4);

    GError *error = NULL;
    if (!fpi_goodix_device_send(dev, enable_message, TRUE, 500, FALSE, &error)) {
        FAIL_SSM_AND_RETURN(ssm, error)
    }

    GoodixMessage *receive_message = NULL;
    if (!fpi_goodix_device_receive_data(dev, &receive_message, &error)) {
        FAIL_SSM_AND_RETURN(ssm, error)
    }

    if (receive_message->category == 0x8 && receive_message->command == 0x1) {
        int chip_id = fpi_goodix_protocol_decode_u32(receive_message->payload->data, receive_message->payload->len);
        if (chip_id >> 8 != 0x220C) {
            fpi_ssm_mark_failed(ssm, FPI_GOODIX_DEVICE_ERROR(DEVICE_ENABLE, "Unsupported chip ID %x", chip_id));
        } else {
            fpi_ssm_next_state(ssm);
        }
    } else {
        fpi_ssm_mark_failed(ssm, FPI_GOODIX_DEVICE_ERROR(DEVICE_ENABLE, "Not a register read message for command %02x", receive_message->command));
    }
    g_free(receive_message);

}

static void fpi_device_goodixtls5395_check_firmware_version(FpDevice *dev, FpiSsm *ssm) {
    fp_dbg("Check Firmware");
    guint8 payload[] = {0x0, 0x0};
    GoodixMessage *message = fpi_goodix_protocol_create_message(0xA, 4, payload, 2);
    GError *error = NULL;

    if(!fpi_goodix_device_send(dev, message, TRUE, 500, FALSE, &error)) {
        FAIL_SSM_AND_RETURN(ssm, error)
    }

    GoodixMessage *receive_message = NULL;
    if (!fpi_goodix_device_receive_data(dev, &receive_message, &error)) {
        FAIL_SSM_AND_RETURN(ssm, error)
    }

    if (receive_message->category == 0xA && receive_message->command == 4) {
        g_autofree gchar *fw_version = g_strndup(receive_message->payload->data, receive_message->payload->len);
        if (g_strcmp0(fw_version, FIRMWARE_VERSION_1) == 0 || g_strcmp0(fw_version, FIRMWARE_VERSION_2) == 0) {
            fpi_ssm_next_state(ssm);
        } else {
            fpi_ssm_mark_failed(ssm, FPI_GOODIX_DEVICE_ERROR(CHECK_FIRMWARE, "Firmware %s version is not supported.", fw_version));
        }
    } else {
        fpi_ssm_mark_failed(ssm, error);
    }

    g_free(receive_message);
}

static void fpi_device_goodixtls5395_check_sensor(FpDevice *dev, FpiSsm *ssm) {
    FpiGoodixDeviceClass *self = FPI_GOODIX_DEVICE_GET_CLASS(dev);
    fp_dbg("Check sensor");
    guint8 payload[] = {0x0, 0x0};
    GoodixMessage *check_message = fpi_goodix_protocol_create_message(0xA, 0x3, payload, 2);

    GError *error = NULL;
    if (!fpi_goodix_device_send(dev, check_message, TRUE, 500, FALSE, &error)) {
        FAIL_SSM_AND_RETURN(ssm, error)
    }

    GoodixMessage *receive_message = NULL;
    if (!fpi_goodix_device_receive_data(dev, &receive_message, &error)) {
        FAIL_SSM_AND_RETURN(ssm, error)
    }

    if (receive_message->category == 0xA && receive_message->command == 0x3) {
        fp_dbg("OTP: %s", fpi_goodix_protocol_data_to_str(receive_message->payload->data, receive_message->payload->len));

        guint8 *otp = receive_message->payload->data;
        guint otp_length = receive_message->payload->len;
        if(!fpi_goodix_device_verify_otp_hash(otp, otp_length, goodix_5395_otp_hash)) {
            FAIL_SSM_AND_RETURN(ssm, FPI_GOODIX_DEVICE_ERROR(CHECK_SENSOR, "OTP hash incorrect %s",
                                                             fpi_goodix_protocol_data_to_str(otp, otp_length)))
        }
        guint8 diff = otp[17] >> 1 & 0x1F;
        fp_dbg("[0x11]:%02x, diff[5:1]=%02x", otp[0x11], diff);
        guint8 tcode = otp[23] != 0 ? otp[23] + 1 : 0;

        GoodixCalibrationParam *params = g_malloc0(sizeof(GoodixCalibrationParam));

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

        //TODO: maybe it needs to allocate fdt_base for all variable
        guint8 *fdt_base = g_malloc0(FDT_BASE_LEN);

        params->fdt_base_down = fdt_base;
        params->fdt_base_up = fdt_base;
        params->fdt_base_manual = fdt_base;

        self->calibration_params = params;

        fpi_ssm_next_state(ssm);
    } else {
        fpi_ssm_mark_failed(ssm, FPI_GOODIX_DEVICE_ERROR(CHECK_SENSOR, "Not a register read message for command %02x", receive_message->command));
    }
    g_free(receive_message);
}

static void fpi_device_goodixtls5395_check_psk(FpDevice *dev, FpiSsm *ssm) {
    fp_dbg("Check PSK");
    FpiGoodixDeviceClass *class = FPI_GOODIX_DEVICE_GET_CLASS(dev);
    guint8 payload[] = {0x03, 0xb0, 0x00, 0x00};
    GoodixMessage *check_psk_message = fpi_goodix_protocol_create_message(0xE, 2, payload, 4);

    GError *error = NULL;
    if (!fpi_goodix_device_send(dev, check_psk_message, TRUE, 500, FALSE, &error)) {
        FAIL_SSM_AND_RETURN(ssm, error)
    }

    GoodixMessage *receive_message = NULL;
    if (!fpi_goodix_device_receive_data(dev, &receive_message, &error)) {
        FAIL_SSM_AND_RETURN(ssm, error)
    }

    if (receive_message->category == 0xE && receive_message->command == 2) {

        GoodixProductionRead *read_structure = (GoodixProductionRead *) receive_message->payload->data;

        if (read_structure->status != 0x00) {
            FAIL_SSM_AND_RETURN(ssm, FPI_GOODIX_DEVICE_ERROR(CHECK_PSK, "Not a production read reply for command %02x", receive_message->command))
        }

        if (read_structure->message_read_type != 0xb003) {
            FAIL_SSM_AND_RETURN(ssm, FPI_GOODIX_DEVICE_ERROR(CHECK_PSK, "Wrong read type in reply, expected: %02x, received: %02x", 0xb003, read_structure->message_read_type))
        }

        if (read_structure->payload_size != receive_message->payload->len - sizeof(GoodixProductionRead)) {
            FAIL_SSM_AND_RETURN(ssm, FPI_GOODIX_DEVICE_ERROR(CHECK_PSK, "Payload does not match reported size: %lu != %d", receive_message->payload->len - sizeof(GoodixProductionRead), read_structure->payload_size))
        }

        guint8 *received_psk = receive_message->payload->data + sizeof(GoodixProductionRead);
        fp_dbg("psk is %s", fpi_goodix_protocol_data_to_str(received_psk, read_structure->payload_size));

        
        guint8 *psk = g_malloc0(32);
        GByteArray *calculate_sha256 = crypto_utils_sha256_hash(psk, 32);
        fp_dbg("Calculated psk: %s", fpi_goodix_protocol_data_to_str(calculate_sha256->data, calculate_sha256->len));
        class->is_psk_valid = memcmp(received_psk, calculate_sha256->data, calculate_sha256->len) == 0;
        fpi_ssm_next_state(ssm);
    } else {
        fpi_ssm_mark_failed(ssm, FPI_GOODIX_DEVICE_ERROR(CHECK_PSK, "Not read reply for command %02x", receive_message->command));
    }
}

static void fpi_device_goodixtls5395_write_psk(FpDevice *dev, FpiSsm *ssm) {
    FpiGoodixDeviceClass *class = FPI_GOODIX_DEVICE_GET_CLASS(dev);
    if (class->is_psk_valid) {
        fp_dbg("PSKs are%s equal", class->is_psk_valid ? "": " not");
        fpi_ssm_next_state(ssm);
        return;
    }
    fp_dbg("Write PSK");
    const gint psk_white_box_length = sizeof(goodix5395_psk_white_box);
    guint8 payload[5 + psk_white_box_length];
    payload[0] = 0x02;
    payload[1] = 0xb0;
    payload[4] = psk_white_box_length;

    memcpy(payload + 5, goodix5395_psk_white_box, psk_white_box_length);
    GoodixMessage *write_psk_message = fpi_goodix_protocol_create_message(0xE, 1, payload, sizeof(payload));

    GError *error = NULL;
    if (!fpi_goodix_device_send(dev, write_psk_message, TRUE, 500, FALSE, &error)) {
        FAIL_SSM_AND_RETURN(ssm, error)
    }

    GoodixMessage *receive_message = NULL;
    if (!fpi_goodix_device_receive_data(dev, &receive_message, &error)) {
        FAIL_SSM_AND_RETURN(ssm, error)
    }

    if (receive_message->category != 0xE || receive_message->command != 1) {
        FAIL_SSM_AND_RETURN(ssm, FPI_GOODIX_DEVICE_ERROR(WRITE_PSK, "Not a production write reply command: 0%02x", receive_message->command))
    }

    if (receive_message->payload->data[0] != 0) {
        FAIL_SSM_AND_RETURN(ssm, FPI_GOODIX_DEVICE_ERROR(WRITE_PSK, "Production write MCU failed. Command: 0%02x", receive_message->command))
    } else {
        fpi_ssm_next_state(ssm);
    }
}

static void fpi_goodix_5395_activate_run_state(FpiSsm *ssm, FpDevice *dev) {
  switch (fpi_ssm_get_cur_state(ssm)) {
      case INIT_DEVICE:
          fpi_device_goodixtls5395_ping(dev, ssm);
          break;

      case CHECK_FIRMWARE:
          fpi_device_goodixtls5395_check_firmware_version(dev, ssm);
          break;

      case DEVICE_ENABLE:
          fpi_device_goodixtls5395_device_enable(dev, ssm);
          break;

      case CHECK_SENSOR:
          fpi_device_goodixtls5395_check_sensor(dev, ssm);
          break;

      case CHECK_PSK:
          fpi_device_goodixtls5395_check_psk(dev, ssm);
          break;

      case WRITE_PSK:
          fpi_device_goodixtls5395_write_psk(dev, ssm);
          break;
      case ESTABLISH_GTS_CONNECTION:
          fpi_goodix_device_gtls_connection(dev, ssm);
          break;
      case UPDATE_ALL_BASE:
          fp_dbg("Update all base");
          break;
      case SET_SLEEP_MODE:
          fp_dbg("Set sleep mode.");
          break;
  }
}

// ---- ACTIVE SECTION END ----

// -----------------------------------------------------------------------------

// ---- DEV SECTION START ----

static void fpi_device_goodixtls5395_init_device(FpImageDevice *img_dev) {
  FpDevice *dev = FP_DEVICE(img_dev);
  GError *error = NULL;

  if (fpi_goodix_device_init_device(dev, &error)) {
    fpi_image_device_open_complete(img_dev, error);
    return;
  }

  fpi_image_device_open_complete(img_dev, NULL);
}

static void fpi_device_goodixtls5395_deinit_device(FpImageDevice *img_dev) {
  FpDevice *dev = FP_DEVICE(img_dev);
  GError *error = NULL;

  if (fpi_goodix_device_deinit_device(dev, &error)) {
    fpi_image_device_close_complete(img_dev, error);
    return;
  }

  fpi_image_device_close_complete(img_dev, NULL);
}

static void fpi_device_goodixtls5395_activate_device(FpImageDevice *img_dev) {
  FpDevice *dev = FP_DEVICE(img_dev);

  fpi_ssm_start(fpi_ssm_new(dev, fpi_goodix_5395_activate_run_state, DEVICE_INIT_NUM_STATES),
                fpi_goodix_5395_activate_complete);
}

static void fpi_device_goodixtls5395_change_state(FpImageDevice *img_dev, FpiImageDeviceState state) {}

static void fpi_device_goodixtls5395_deactivate_device(FpImageDevice *img_dev) {
  fpi_image_device_deactivate_complete(img_dev, NULL);
}

// ---- DEV SECTION END ----

static void fpi_device_goodixtls5395_init(FpiDeviceGoodixTls5395 *self) {
}

static void fpi_device_goodixtls5395_class_init(FpiDeviceGoodixTls5395Class *class) {
  FpiGoodixDeviceClass *gx_class = FPI_GOODIX_DEVICE_CLASS(class);
  FpDeviceClass *device_class = FP_DEVICE_CLASS(class);
  FpImageDeviceClass *image_device_class = FP_IMAGE_DEVICE_CLASS(class);

    gx_class->interface = GOODIX_5395_INTERFACE;
    gx_class->ep_in = GOODIX_5395_EP_IN;
    gx_class->ep_out = GOODIX_5395_EP_OUT;

    device_class->id = "goodixtls5395";
    device_class->full_name = "Goodix TLS Fingerprint Sensor 5395";
    device_class->type = FP_DEVICE_TYPE_USB;
    device_class->id_table = id_table;

    device_class->scan_type = FP_SCAN_TYPE_PRESS;

  // TODO
    image_device_class->bz3_threshold = 24;
    image_device_class->img_width = 88;
    image_device_class->img_height = 108;

    image_device_class->img_open = fpi_device_goodixtls5395_init_device;
    image_device_class->img_close = fpi_device_goodixtls5395_deinit_device;
    image_device_class->activate = fpi_device_goodixtls5395_activate_device;
    image_device_class->change_state = fpi_device_goodixtls5395_change_state;
    image_device_class->deactivate = fpi_device_goodixtls5395_deactivate_device;

    fpi_device_class_auto_initialize_features(device_class);
    device_class->features &= ~FP_DEVICE_FEATURE_VERIFY;
}
