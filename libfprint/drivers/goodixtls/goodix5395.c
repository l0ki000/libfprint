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

struct _FpiDeviceGoodixTls5395 {
    FpiGoodixDevice parent;
};

G_DECLARE_FINAL_TYPE(FpiDeviceGoodixTls5395, fpi_device_goodixtls5395, FPI, DEVICE_GOODIXTLS5395, FpiGoodixDevice);

G_DEFINE_TYPE(FpiDeviceGoodixTls5395, fpi_device_goodixtls5395, FPI_TYPE_GOODIX_DEVICE)

// ---- ACTIVE SECTION START ----

enum activate_states {
  INIT_DEVICE,
  CHECK_FIRMWARE,
  POWER_ON_SENSOR,
  SETUP_FINGER_DOWN_DETECTION,
  WAITING_FOR_FINGER_DOWN,
  READING_FINGER,
  WAITING_FOR_FINGER_UP,
  SET_UP_SLEEP_MODE,
  POWER_OFF_SENSOR,
  ACTIVATE_NUM_STATES,
};

static void ping_handler(FpDevice *dev, guint8 *data, guint16 length,gpointer user_data, GError *error) {

}

static void activate_complete(FpiSsm *ssm, FpDevice *dev, GError *error) {
    FpImageDevice *image_dev = FP_IMAGE_DEVICE(dev);

    fpi_image_device_activate_complete(image_dev, error);

    if (!error) {
//        fpi_ssm_start(fpi_ssm_new(dev, goodix_tls_run_state, TLS_NUM_STATES), goodix_tls_complete);
    }
}

static void ping(FpDevice *device, FpiSsm *ssm) {
    //TODO: memory managment
    fpi_goodix_device_empty_buffer(device);
    GoodixMessage *message = malloc(sizeof(GoodixMessage));
    message->command = 0x00;
    message->category = 0;
    message->payload_len = 2;
    message->payload = g_malloc0(message->payload_len);
    GError *error = NULL;
    if (fpi_goodix_device_send(device, message, TRUE, 500, FALSE, &error)) {
        fpi_ssm_next_state(ssm);
    } else {
        fpi_ssm_mark_failed(ssm, error);
    }
}

static void fpi_goodix_device_check_firmware_version(FpDevice *dev, FpiSsm *ssm) {
    fp_dbg("Check Firmware");
    GoodixMessage *message = malloc(sizeof(GoodixMessage));
    message->command = 4;
    message->category = 0xA;
    message->payload_len = 2;
    message->payload = g_malloc0(message->payload_len);
    GError *error = NULL;

    if(fpi_goodix_device_send(dev, message, TRUE, 500, FALSE, &error)) {
        GoodixMessage *receive_message = NULL;
        if (fpi_goodix_device_receive_data(dev, &receive_message, &error)) {
            if (receive_message->category != 0xA || receive_message->command != 4) {
                fpi_ssm_mark_failed(ssm, error);
            }
            g_autofree gchar *fw_version = g_strndup(receive_message->payload, receive_message->payload_len);
            if (g_strcmp0(fw_version, FIRMWARE_VERSION_1) == 0 || g_strcmp0(fw_version, FIRMWARE_VERSION_2) == 0) {
                fpi_ssm_next_state(ssm);
            } else {
                fpi_ssm_mark_failed(ssm, g_error_new(11, 10, "Firmware version is not supported."));
            }
        } else {
            fpi_ssm_mark_failed(ssm, error);
        }
    }

}

static void activate_run_state(FpiSsm *ssm, FpDevice *dev) {
  GError *error = NULL;

  switch (fpi_ssm_get_cur_state(ssm)) {
    case INIT_DEVICE:

        ping(dev, ssm);
      break;

      case CHECK_FIRMWARE:
          fpi_goodix_device_check_firmware_version(dev, ssm);
          break;

      case POWER_ON_SENSOR:
//          goodix_send_enable_chip(dev, TRUE, check_none, ssm);
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

static void init_device(FpImageDevice *img_dev) {
  FpDevice *dev = FP_DEVICE(img_dev);
  GError *error = NULL;

  if (fpi_goodix_device_init_device(dev, &error)) {
    fpi_image_device_open_complete(img_dev, error);
    return;
  }

  fpi_image_device_open_complete(img_dev, NULL);
}

static void deinit_device(FpImageDevice *img_dev) {
  FpDevice *dev = FP_DEVICE(img_dev);
  GError *error = NULL;

  if (fpi_goodix_device_deinit_device(dev, &error)) {
    fpi_image_device_close_complete(img_dev, error);
    return;
  }

  fpi_image_device_close_complete(img_dev, NULL);
}

static void activate_device(FpImageDevice *img_dev) {
  FpDevice *dev = FP_DEVICE(img_dev);

  fpi_ssm_start(fpi_ssm_new(dev, activate_run_state, ACTIVATE_NUM_STATES),
                activate_complete);
}

static void dev_change_state(FpImageDevice *img_dev, FpiImageDeviceState state) {}

static void deactivate_device(FpImageDevice *img_dev) {
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

    image_device_class->img_open = init_device;
    image_device_class->img_close = deinit_device;
    image_device_class->activate = activate_device;
    image_device_class->change_state = dev_change_state;
    image_device_class->deactivate = deactivate_device;

    fpi_device_class_auto_initialize_features(device_class);
    device_class->features &= ~FP_DEVICE_FEATURE_VERIFY;
}
