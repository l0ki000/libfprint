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

#include <glib.h>
#include <string.h>
#include <drivers_api.h>

#include "goodix5395_capture.h"
#include "goodix_device.h"

#define SENSOR_WIDTH 108
#define SENSOR_HEIGHT 88

static void fpi_goodix_device5395_finger_position_detection(FpDevice *dev, enum FingerDetectionOperation posix, gint timeout_ms){
    GError *error = NULL;
    fpi_goodix_device_setup_finger_position_detection(dev, posix, timeout_ms, &error);
    if(error) {
        //TODO: Handle error
    }
}

static void fpi_goodix_device5395_wait_for_finger_down(FpDevice *dev) {
    GError *error = NULL;
    GByteArray *fdt_base = fpi_goodix_device_wait_for_finger(dev, 1000000, DOWN, &error);
    if(error != NULL) {
        //TODO: Handle error
    }

    GByteArray *manual_fdt_base = fpi_goodix_device_get_fdt_base_with_tx(dev, FALSE, &error);
    if (error != NULL) {
    
    }

    if (fpi_goodix_device_is_fdt_base_valid(dev, fdt_base, manual_fdt_base)) {

    }

    fpi_image_device_report_finger_status (dev, TRUE);
}

static void fpi_goodix_device5395_wait_for_finger_up(FpDevice *dev) {
    GError *error = NULL;
    GByteArray *fdt_base = fpi_goodix_device_wait_for_finger(dev, 5000, UP, &error);
    if(error != NULL) {
    }
    fpi_image_device_report_finger_status (dev, FALSE);
}

static void fpi_goodix_device5395_read_finger(FpDevice *dev) {
    GError *error = NULL;
    GArray *finger_image = fpi_goodix_device_get_image(dev, TRUE, TRUE, 'h', FALSE, TRUE, &error);
    fpi_goodix_protocol_write_pgm(finger_image, SENSOR_WIDTH, SENSOR_HEIGHT, "finger.pgm");
    FpImage *img = fpi_goodix_protocol_convert_image(finger_image, SENSOR_WIDTH, SENSOR_HEIGHT);
    fpi_image_device_image_captured(dev, img);
}

void fpi_device_goodix5395_change_state(FpImageDevice *img_dev, FpiImageDeviceState state) {
    switch (state) {
        case FPI_IMAGE_DEVICE_STATE_AWAIT_FINGER_ON: {
            fpi_goodix_device5395_finger_position_detection(img_dev, DOWN, 500);
            fpi_goodix_device5395_wait_for_finger_down(img_dev);
        }
            break;
        case FPI_IMAGE_DEVICE_STATE_CAPTURE: {
            fpi_goodix_device5395_read_finger(img_dev);
        }
            break;
        case FPI_IMAGE_DEVICE_STATE_AWAIT_FINGER_OFF: {
            fp_dbg("Waiting finger UP");
            fpi_goodix_device5395_finger_position_detection(img_dev, UP, 500);
            fpi_goodix_device5395_wait_for_finger_up(img_dev);
        }
            break;
    }
}
