// Goodix 5395 driver for libfprint

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

#define FP_COMPONENT "goodix53x5"

#include <glib.h>
#include <string.h>
#include <drivers_api.h>

#include "goodix_device.h"
#include "goodix5395.h"
#include "goodix53x5_init.h"
#include "goodix5395_capture.h"


struct _FpiDeviceGoodix53x5 {
    FpiGoodixDevice parent;
};

G_DECLARE_FINAL_TYPE(FpiDeviceGoodix53x5, fpi_device_goodix53x5, FPI, DEVICE_GOODIX53x5, FpiGoodixDevice);

G_DEFINE_TYPE(FpiDeviceGoodix53x5, fpi_device_goodix53x5, FPI_TYPE_GOODIX_DEVICE)

// ---- DEV SECTION START ----

static void fpi_device_goodix5395_img_open(FpImageDevice *img_dev) {
  FpDevice *dev = FP_DEVICE(img_dev);
  GError *error = NULL;

  fpi_goodix_device_init_device(dev, &error);
  fpi_image_device_open_complete(img_dev, error);
}

static void fpi_device_goodix5395_img_close(FpImageDevice *img_dev) {
  FpDevice *dev = FP_DEVICE(img_dev);
  GError *error = NULL;

  fpi_goodix_device_deinit_device(dev, &error);
  fpi_image_device_close_complete(img_dev, error);
}

static void fpi_device_goodix5395_activate_device(FpImageDevice *img_dev) {
  FpDevice *dev = FP_DEVICE(img_dev);

}

static void fpi_device_goodix5395_deactivate(FpImageDevice *img_dev) {
    fpi_image_device_deactivate_complete(img_dev, NULL);
}

// ---- DEV SECTION END ----

static void fpi_device_goodix53x5_init(FpiDeviceGoodix53x5 *self) {}

static void fpi_device_goodix53x5_class_init(FpiDeviceGoodix53x5Class *class) {
    FpiGoodixDeviceClass *gx_class = FPI_GOODIX_DEVICE_CLASS(class);
    FpDeviceClass *device_class = FP_DEVICE_CLASS(class);
    FpImageDeviceClass *image_device_class = FP_IMAGE_DEVICE_CLASS(class);

    gx_class->interface = GOODIX_5395_INTERFACE;
    gx_class->ep_in = GOODIX_5395_EP_IN;
    gx_class->ep_out = GOODIX_5395_EP_OUT;

    device_class->id = "goodix53x5";
    device_class->full_name = "Goodix 53x5 Fingerprint Sensor";
    device_class->type = FP_DEVICE_TYPE_USB;
    device_class->id_table = id_table;

    device_class->scan_type = FP_SCAN_TYPE_PRESS;

    image_device_class->bz3_threshold = 24;
    image_device_class->img_width = 88;
    image_device_class->img_height = 108;

    image_device_class->img_open = fpi_device_goodix5395_img_open;
    image_device_class->img_close = fpi_device_goodix5395_img_close;
    image_device_class->activate = run_initialize;
    image_device_class->deactivate = fpi_device_goodix5395_deactivate;
    image_device_class->change_state = fpi_device_goodix5395_change_state;

    // TODO fpi_device_class_auto_initialize_features(device_class);
    // device_class->features &= ~FP_DEVICE_FEATURE_VERIFY;
}
