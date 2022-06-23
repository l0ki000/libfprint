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

#pragma once

#include "goodix_protocol.h"
#include "goodix_gtls.h"

#define GOODIX_DEVICE_ERROR_DOMAIN 1
#define FAIL_SSM_AND_RETURN(ssm, error) fpi_ssm_mark_failed(ssm, error); return;
#define FPI_GOODIX_DEVICE_ERROR(code, format, ...)  g_error_new(GOODIX_DEVICE_ERROR_DOMAIN, code, format, __VA_ARGS__)

typedef struct __attribute__((__packed__)) _GoodixCalibrationParam{
    guint16 tcode;
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


} GoodixCalibrationParam;

G_DECLARE_DERIVABLE_TYPE(FpiGoodixDevice, fpi_goodix_device, FPI, GOODIX_DEVICE, FpImageDevice);

#define FPI_TYPE_GOODIX_DEVICE (fpi_goodix_device_get_type())


struct _FpiGoodixDeviceClass
{
    FpImageDeviceClass parent;

    gint interface;
    guint8 ep_in;
    guint8 ep_out;
    gboolean is_psk_valid;
};

enum FingerDetectionOperation {
    DOWN,
    UP,
    MANUAL
};

typedef void (*GoodixDeviceReceiveCallback)(FpDevice *dev, GoodixMessage *message, GError *error);

gboolean fpi_goodix_device_init_device(FpDevice *dev, GError **error);
gboolean fpi_goodix_device_deinit_device(FpDevice *dev, GError **error);

gboolean fpi_goodix_device_send(FpDevice *dev, GoodixMessage *message, gboolean calc_checksum, gint timeout_ms,
                            gboolean reply, GError **error);
gboolean fpi_goodix_device_receive_data(FpDevice *dev, GoodixMessage **message, GError **error);
void fpi_goodix_device_empty_buffer(FpDevice *dev);
gboolean fpi_goodix_device_reset(FpDevice *dev, guint8 reset_type, gboolean irq_status);
void fpi_goodix_device_gtls_connection(FpDevice *dev, FpiSsm *parent_ssm);
void fpi_goodix_device_send_mcu(FpDevice *dev, const guint32 data_type, GByteArray *data);
GByteArray *fpi_goodix_device_recv_mcu(FpDevice *dev, guint read_type, GError *error);
gboolean fpi_goodix_device_fdt_execute_operation(FpDevice *dev, enum FingerDetectionOperation operation, GByteArray *fdt_base, gint timeout_ms, GError **error);
gboolean fpi_goodix_device_upload_config(FpDevice *dev, GByteArray *config, gint timeout_ms, GError **error);
void fpi_goodix_device_prepare_config(FpDevice *dev, GByteArray *config);
void fpi_goodix_device_set_calibration_params(FpDevice *dev, GByteArray* otp);