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

#define FP_COMPONENT "goodixtls5395"

#include <glib.h>
#include <string.h>
#include <drivers_api.h>

#include "5395/goodix_device.h"
#include "goodix5395.h"

#define FIRMWARE_VERSION_1 "GF5288_HTSEC_APP_10011"
#define FIRMWARE_VERSION_2 "GF5288_HTSEC_APP_10020"

#define GOODIX_DEVICE_ERROR_DOMAIN 1

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

struct _FpiDeviceGoodixTls5395 {
    FpiGoodixDevice parent;
};

typedef struct __attribute__((__packed__)) _CalibrationParam{
    guint8 tcode;
    guint8 delta_fdt;
    guint8 delta_down;
    guint8 delta_up;
    guint8 delta_img;
    guint8 delta_nav;
    guint8 dac_h;
    guint8 dac_l;
    guint8 dac_delta;
    guint8 *fdt_base_down;
    guint8 *fdt_base_up;
    guint8 *fdt_base_manual;

    guint8 *calib_image;


} CalibrationParam;

G_DECLARE_FINAL_TYPE(FpiDeviceGoodixTls5395, fpi_device_goodixtls5395, FPI, DEVICE_GOODIXTLS5395, FpiGoodixDevice);

G_DEFINE_TYPE(FpiDeviceGoodixTls5395, fpi_device_goodixtls5395, FPI_TYPE_GOODIX_DEVICE)

// ---- ACTIVE SECTION START ----

enum activate_states {
  INIT_DEVICE,
  CHECK_FIRMWARE,
  DEVICE_ENABLE,
  CHECK_SENSOR,
  CHECK_PSK,
  SETUP_FINGER_DOWN_DETECTION,
  WAITING_FOR_FINGER_DOWN,
  READING_FINGER,
  WAITING_FOR_FINGER_UP,
  SET_UP_SLEEP_MODE,
  POWER_OFF_SENSOR,
  ACTIVATE_NUM_STATES,
};

static void activate_complete(FpiSsm *ssm, FpDevice *dev, GError *error) {
    FpImageDevice *image_dev = FP_IMAGE_DEVICE(dev);

    fpi_image_device_activate_complete(image_dev, error);

    if (!error) {
//        fpi_ssm_start(fpi_ssm_new(dev, goodix_tls_run_state, TLS_NUM_STATES), goodix_tls_complete);
    }
}

#define FPI_GOODIX_DEVICE_ERROR(code, format, ...)  g_error_new(GOODIX_DEVICE_ERROR_DOMAIN, code, format, __VA_ARGS__)
#define FAIL_SSM_WITH_RETURN(ssm, error) fpi_ssm_mark_failed(ssm, error); return;

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
        FAIL_SSM_WITH_RETURN(ssm, error)
    }

    GoodixMessage *receive_message = NULL;
    if (!fpi_goodix_device_receive_data(dev, &receive_message, &error)) {
        FAIL_SSM_WITH_RETURN(ssm, error)
    }

    if (receive_message->category == 0x8 && receive_message->command == 0x1) {
        int chip_id = fpi_goodix_protocol_decode_u32(receive_message->payload, receive_message->payload_len);
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
        FAIL_SSM_WITH_RETURN(ssm, error)
    }

    GoodixMessage *receive_message = NULL;
    if (!fpi_goodix_device_receive_data(dev, &receive_message, &error)) {
        FAIL_SSM_WITH_RETURN(ssm, error)
    }

    if (receive_message->category == 0xA && receive_message->command == 4) {
        g_autofree gchar *fw_version = g_strndup(receive_message->payload, receive_message->payload_len);
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
    fp_dbg("Check sensor");
    guint8 payload[] = {0x0, 0x0};
    GoodixMessage *check_message = fpi_goodix_protocol_create_message(0xA, 0x3, payload, 2);

    GError *error = NULL;
    if (!fpi_goodix_device_send(dev, check_message, TRUE, 500, FALSE, &error)) {
        FAIL_SSM_WITH_RETURN(ssm, error)
    }

    GoodixMessage *receive_message = NULL;
    if (!fpi_goodix_device_receive_data(dev, &receive_message, &error)) {
        FAIL_SSM_WITH_RETURN(ssm, error)
    }

    if (receive_message->category == 0xA && receive_message->command == 0x3) {
        fp_dbg("OTP: %s", fpi_goodix_protocol_data_to_str(receive_message->payload, receive_message->payload_len));

        guint8 *otp = receive_message->payload;
        guint otp_length = receive_message->payload_len;
        if(!fpi_goodix_device_verify_otp_hash(otp, otp_length, goodix_5395_otp_hash)) {
            FAIL_SSM_WITH_RETURN(ssm, FPI_GOODIX_DEVICE_ERROR(CHECK_SENSOR, "OTP hash incorrect %s",
                                                              fpi_goodix_protocol_data_to_str(otp, otp_length)))
        }
        guint8 diff = otp[17] >> 1 & 0x1F;
        fp_dbg("[0x11]:%02x, diff[5:1]=%02x", otp[0x11], diff);
        guint8 tcode = otp[23] != 0 ? otp[23] + 1 : 0;

        guint8 delta_fdt = 0;
        guint8 delta_down = 0xD;
        guint8 delta_up = 0xB;
        guint8 delta_img = 0xC8;
        guint8 delta_nav = 0x28;

        guint8 dac_h = (otp[17] << 8 ^ otp[22]) & 0x1FF;
        guint8 dac_l = (otp[17] & 0x40) << 2 ^ otp[31];

        if (diff != 0) {
            guint8 tmp = diff + 5;
            guint8 tmp2 = (tmp * 0x32) >> 4;

            delta_fdt = tmp2 / 5;
            delta_down = tmp2 / 3;
            delta_up = delta_down - 2;
            delta_img = 0xC8;
            delta_nav = tmp * 4;
        }

        if (otp[17] == 0 || otp[22] == 0 || otp[31] == 0) {
            dac_h = 0x97;
            dac_l = 0xD0;
        }

        fp_dbg("tcode:%02x delta down:%02x", tcode, delta_down);
        fp_dbg("delta up:%02x delta img:%02x", delta_up, delta_img);
        fp_dbg("delta nav:%02x dac_h:%02x dac_l:%02x", delta_nav, dac_h, dac_l);

        guint8 dac_delta = 0xC83 / tcode;
        fp_dbg("sensor broken dac_delta=%02x", dac_delta);
        //TODO: prepare CalibrationParam

        fpi_ssm_next_state(ssm);
    } else {
        fpi_ssm_mark_failed(ssm, FPI_GOODIX_DEVICE_ERROR(CHECK_SENSOR, "Not a register read message for command %02x", receive_message->command));
    }
    g_free(receive_message);
}

static void fpi_device_goodixtls5395_check_psk(FpDevice *dev, FpiSsm *ssm) {
    fp_dbg("Check PSK");
}

static void activate_run_state(FpiSsm *ssm, FpDevice *dev) {
  GError *error = NULL;

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

//    case ACTIVATE_NOP:
//      goodix_send_nop(dev, check_none, ssm);
//      break;
//
//    case ACTIVATE_CHECK_FW_VER:
//      goodix_send_firmware_version(dev, check_firmware_version, ssm);
//      break;
//
//    case ACTIVATE_CHECK_PSK:
//      goodix_send_preset_psk_read(dev, GOODIX_5395_PSK_FLAGS, 0,
//                                  check_preset_psk_read, ssm);
//      break;
//
//    case ACTIVATE_RESET:
//      goodix_send_reset(dev, TRUE, 20, check_reset, ssm);
//      break;
//
//    case BREAK:
//      g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Break");
//      fpi_ssm_mark_failed(ssm, error);
//      break;
  }
}

//static void activate_complete(FpiSsm *ssm, FpDevice *dev, GError *error) {
//  FpImageDevice *image_dev = FP_IMAGE_DEVICE(dev);
//
//  fpi_image_device_activate_complete(image_dev, error);
//
//  if (!error) goodix_tls(dev);
//}

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

  fpi_ssm_start(fpi_ssm_new(dev, activate_run_state, ACTIVATE_NUM_STATES),
                activate_complete);
}

static void fpi_device_goodixtls5395_change_state(FpImageDevice *img_dev, FpiImageDeviceState state) {}

static void fpi_device_goodixtls5395_deactivate_device(FpImageDevice *img_dev) {
  fpi_image_device_deactivate_complete(img_dev, NULL);
}

// ---- DEV SECTION END ----

static void fpi_device_goodixtls5395_init(FpiDeviceGoodixTls5395 *self) {
}

static void fpi_device_goodixtls5395_class_init(
    FpiDeviceGoodixTls5395Class *class) {
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
