// Goodix 53x5 driver for libfprint

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

#pragma once

#include "goodix_protocol.h"
#include "goodix_gtls.h"

#define GOODIX_DEVICE_ERROR_DOMAIN 1
#define FAIL_SSM_AND_RETURN(ssm, error) fpi_ssm_mark_failed(ssm, error); return;
#define FPI_GOODIX_DEVICE_ERROR(code, format, ...)  g_error_new(GOODIX_DEVICE_ERROR_DOMAIN, code, format)
#define FPI_GOODIX_DEVICE_ERROR_WITH_PARAMS(code, format, ...)  g_error_new(GOODIX_DEVICE_ERROR_DOMAIN, code, format, __VA_ARGS__)

G_DECLARE_DERIVABLE_TYPE(FpiGoodixDevice, fpi_goodix_device, FPI, GOODIX_DEVICE, FpImageDevice);

#define FPI_TYPE_GOODIX_DEVICE (fpi_goodix_device_get_type())

enum FingerDetectionOperation {
    DOWN = 1,
    UP = 2,
    MANUAL = 3
};

typedef struct __attribute__((__packed__)) _GoodixCalibrationParam{
    guint16 tcode;
    guint8 delta_fdt;
    guint8 delta_down;
    guint8 delta_up;
    guint8 delta_img;
    guint8 delta_nav;
    guint16 dac_h;
    guint16 dac_l;
    guint8 dac_delta;
    GByteArray *fdt_base_down;
    GByteArray *fdt_base_up;
    GByteArray *fdt_base_manual;
    GArray *calib_image;
} GoodixCalibrationParam;

typedef struct _GoodixFtdEvent{
    GByteArray *ftd_data;
    guint16 touch_flag;
} GoodixFtdEvent;

struct _FpiGoodixDeviceClass {
    FpImageDeviceClass parent;
    gint interface;
    guint8 ep_in;
    guint8 ep_out;
    gboolean is_psk_valid;
};

typedef void (*GoodixDeviceReceiveCallback)(FpDevice *dev, GoodixMessage *message, GError *error);

gboolean fpi_goodix_device_ping(FpDevice *dev, GError **error);
gboolean fpi_goodix_device_init_device(FpDevice *dev, GError **error);
gboolean fpi_goodix_device_deinit_device(FpDevice *dev, GError **error);

gboolean fpi_goodix_device_send(FpDevice *dev, GoodixMessage *message, gboolean calc_checksum, guint timeout_ms, GError **error);
gboolean fpi_goodix_device_receive(FpDevice *dev, GoodixMessage **message, guint timeout_ms, GError **error);
gboolean fpi_goodix_device_reset(FpDevice *dev, guint8 reset_type, gboolean irq_status);
void fpi_goodix_device_send_mcu(FpDevice *dev, const guint32 data_type, GByteArray *data);
GByteArray *fpi_goodix_device_recv_mcu(FpDevice *dev, guint read_type, GError *error);
gboolean fpi_goodix_device_fdt_execute_operation(FpDevice *dev, enum FingerDetectionOperation operation, GByteArray *fdt_base, gint timeout_ms, GError **error);
gboolean fpi_goodix_device_upload_config(FpDevice *dev, GByteArray *config, gint timeout_ms, GError **error);
void fpi_goodix_device_prepare_config(FpDevice *dev, GByteArray *config);
void fpi_goodix_device_set_calibration_params(FpDevice *dev, GByteArray* otp);
gboolean fpi_goodix_device_set_sleep_mode(FpDevice *dev, GError **error);
GByteArray *fpi_goodix_device_get_fdt_base_with_tx(FpDevice *dev, gboolean tx_enable, GError **error);
GArray *fpi_goodix_device_get_image(FpDevice *dev, gboolean tx_enable, gboolean hv_enable, gchar use_dac, gboolean adjust_dac, gboolean is_finger, GError **error);
gboolean fpi_goodix_device_is_receive_data_valid(guint8 category, guint8 command, GoodixMessage *receive_message, GError **error);
gboolean fpi_goodix_device_validate_base_img(FpDevice *dev, GArray *base_image_1, GArray *base_image_2);
void fpi_device_update_fdt_bases(FpDevice *dev, GByteArray *fdt_base);
gboolean fpi_goodix_device_is_fdt_base_valid(FpDevice *dev, GByteArray *fdt_data_1, GByteArray *fdt_data_2);
GByteArray *fpi_goodix_device_generate_fdt_base(GByteArray *fdt_data);
void fpi_device_update_calibration_image(FpDevice *dev, GArray *calib_image);
gboolean fpi_goodix_device_ec_control(FpDevice *dev, gboolean is_enable, gint timeout_ms, GError **error);
void fpi_goodix_device_setup_finger_position_detection(FpDevice *dev, enum FingerDetectionOperation posix, gint timeout_ms, GError **error);
GByteArray *fpi_goodix_device_wait_for_finger(FpDevice *dev, guint timeout_ms, enum FingerDetectionOperation fdo, GError **error);
void fpi_goodix_device_gtls_connection_handle(FpiSsm *ssm, FpDevice *dev);
GArray *fpi_goodix_device_get_background_image(FpDevice *dev);
void fpi_goodix_device_add_image(FpDevice *dev, GArray *image);
GSList *fpi_goodix_device_get_finger_images(FpDevice *dev);
void fpi_goodix_device_clear_finger_images(FpDevice *dev);
