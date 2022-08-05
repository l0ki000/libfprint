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

enum Goodix5395CaptureState {
    POWER_ON,
    FINGER_DOWN_DETECTION,
    WAITING_FINGER_DOWN,
    READING_FINGER_IMAGE,
    FINGER_UP_DETECTION,
    WAITING_FINGER_UP,
    POWER_OFF,
    DEVICE_CAPTURE_NUM,
};

void run_capture_state(FpDevice *dev);

static void fpi_goodix_device53x5_report_finger(FpDevice *dev, gboolean is_present) {
    FpImageDevice *img_dev = FP_IMAGE_DEVICE(dev);
    fpi_image_device_report_finger_status (img_dev, is_present);
}

static void fpi_goodix_device5395_finger_position_detection(FpDevice *dev, enum FingerDetectionOperation posix, FpiSsm *ssm, gint timeout_ms){
    GError *error = NULL;
    fpi_goodix_device_setup_finger_position_detection(dev, posix, timeout_ms, &error);
    if (error != NULL) {
        FAIL_SSM_AND_RETURN(ssm, error)
    }
    fpi_ssm_next_state(ssm);
}

static void fpi_goodix_device5395_wait_for_finger_down(FpDevice *dev, FpiSsm *ssm) {
    GError *error = NULL;
    GByteArray *fdt_base = fpi_goodix_device_wait_for_finger(dev, 1000000, DOWN, &error);
    if(error != NULL) {
        FAIL_SSM_AND_RETURN(ssm, error)
    }

    GByteArray *manual_fdt_base = fpi_goodix_device_get_fdt_base_with_tx(dev, FALSE, &error);
    if (error != NULL) {
        FAIL_SSM_AND_RETURN(ssm, error)
    }

    if (fpi_goodix_device_is_fdt_base_valid(dev, fdt_base, manual_fdt_base)) {
        FAIL_SSM_AND_RETURN(ssm, FPI_GOODIX_DEVICE_ERROR(1, "FDT base is not valid."))
    }

    fpi_goodix_device53x5_report_finger(dev, TRUE);
    fpi_ssm_next_state(ssm);
}

static void fpi_goodix_device5395_wait_for_finger_up(FpDevice *dev, FpiSsm *ssm) {
    GError *error = NULL;
    fpi_goodix_device_wait_for_finger(dev, 100000, UP, &error);
    if(error != NULL) {
        FAIL_SSM_AND_RETURN(ssm, error)
    }
    fpi_ssm_next_state(ssm);
}

static void fpi_goodix_device5395_read_finger(FpDevice *dev, FpiSsm *ssm) {
    FpImageDevice *img_dev = FP_IMAGE_DEVICE(dev);
    GError *error = NULL;
    GArray *finger_image = fpi_goodix_device_get_image(dev, TRUE, TRUE, 'h', FALSE, TRUE, &error);
    fpi_goodix_device_add_image(dev, finger_image);
//    g_array_free(finger_image, TRUE);
    fpi_ssm_next_state(ssm);
}

static void fpi_goodix5395_switch_power_device(FpDevice *dev, FpiSsm *ssm, gboolean is_on) {
    GError *error = NULL;
    if (!fpi_goodix_device_set_sleep_mode(dev, &error) && !fpi_goodix_device_ec_control(dev, is_on, 200, &error)){
        FAIL_SSM_AND_RETURN(ssm, error);
    }
}

static void fpi_goodix5395_run_activate_state(FpiSsm *ssm, FpDevice *dev) {
    switch (fpi_ssm_get_cur_state(ssm)){
        case POWER_ON: {
            g_usleep(3000000);
            fp_info("Activating device...");
            fp_info("Powering on sensor");
            fpi_goodix5395_switch_power_device(dev, ssm, TRUE);
            fpi_ssm_next_state(ssm);
        }
            break;
        case FINGER_DOWN_DETECTION: {
            fp_info("Setting up finger down detection");
            fpi_goodix_device5395_finger_position_detection(dev, DOWN, ssm, 500);
        }
            break;
        case WAITING_FINGER_DOWN: {
            fp_info("Waiting for finger down");
            fpi_goodix_device5395_wait_for_finger_down(dev, ssm);
        }
            break;
        case READING_FINGER_IMAGE: {
            fp_info("Reading finger image.");
            fpi_goodix_device5395_read_finger(dev, ssm);
        }
            break;
        case FINGER_UP_DETECTION: {
            fp_info("Setting up finger up detection...");
            fpi_goodix_device5395_finger_position_detection(dev, UP, ssm, 500);
        }
            break;
        case WAITING_FINGER_UP: {
            fp_info("Waiting for finger up");
            fpi_goodix_device5395_wait_for_finger_up(dev, ssm);
        }
            break;
        case POWER_OFF: {
            fp_info("Power off.");
            fpi_goodix5395_switch_power_device(dev, ssm, FALSE);
            fpi_ssm_mark_completed(ssm);
        }
            break;
    }
}

static void goodix5395_capture_complete(FpiSsm *ssm, FpDevice *dev, GError *error) {
    FpImageDevice *img_dev = FP_IMAGE_DEVICE(dev);
    fp_dbg("Complete capture.");
    if (error != NULL) {
        fp_dbg("Error on capture finger %s", error->message);
        fpi_goodix5395_switch_power_device(dev, ssm, FALSE);
        fpi_image_device_session_error(img_dev, error);
    } else {
        GArray *background_image = fpi_goodix_device_get_background_image(dev);
        GArray *finger_image = g_list_first(fpi_goodix_device_get_finger_images(dev))->data;
        FpImage *img = fpi_goodix_protocol_convert_image(finger_image, background_image, SENSOR_WIDTH, SENSOR_HEIGHT);
        fpi_image_device_image_captured(img_dev, img);
        fpi_goodix_device53x5_report_finger(dev, FALSE);
        fpi_goodix_device_clear_finger_images(dev);
    }
}

void run_capture_state(FpDevice *dev) {
    fp_dbg("Start capture fingerprint.");
    fpi_ssm_start(fpi_ssm_new(dev, fpi_goodix5395_run_activate_state, DEVICE_CAPTURE_NUM), goodix5395_capture_complete);
}

void fpi_device_goodix5395_change_state(FpImageDevice *img_dev, FpiImageDeviceState state) {
    switch (state) {
        case FPI_IMAGE_DEVICE_STATE_AWAIT_FINGER_ON: {
            run_capture_state((FpDevice *) img_dev);
        }
            break;
        default:
            break;
    }
}
