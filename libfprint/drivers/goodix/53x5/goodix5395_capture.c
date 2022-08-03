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

enum Goodix5395ActivateState {
    POWER_ON,
    FINGER_DOWN_DETECTION,
    WAITING_FINGER_DOWN,
    READING_FINGER_IMAGE,
    FINGER_UP_DETECTION,
    WAITING_FINGER_UP,
    AC_SET_SLEEP_MODE,
    POWER_OFF,
    DEVICE_ACTIVATE_NUM,
};

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

    }

    FpImageDevice *img_dev = FP_IMAGE_DEVICE(dev);
    fpi_image_device_report_finger_status (img_dev, TRUE);
    fpi_ssm_next_state(ssm);
}

static void fpi_goodix_device5395_wait_for_finger_up(FpDevice *dev, FpiSsm *ssm) {
    GError *error = NULL;
    fpi_goodix_device_wait_for_finger(dev, 5000, UP, &error);
    if(error != NULL) {
    }
    FpImageDevice *img_dev = FP_IMAGE_DEVICE(dev);
    fpi_image_device_report_finger_status (img_dev, FALSE);
    fpi_ssm_next_state(ssm);
}

static void fpi_goodix_device5395_read_finger(FpDevice *dev, FpiSsm *ssm) {
    FpImageDevice *img_dev = FP_IMAGE_DEVICE(dev);
    GError *error = NULL;
    GArray *finger_image = fpi_goodix_device_get_image(dev, TRUE, TRUE, 'h', FALSE, TRUE, &error);
    fpi_goodix_protocol_write_pgm(finger_image, SENSOR_WIDTH, SENSOR_HEIGHT, "finger.pgm");
    GArray *background_image = fpi_goodix_device_get_background_image(dev);
    FpImage *img = fpi_goodix_protocol_convert_image(finger_image, background_image, SENSOR_WIDTH, SENSOR_HEIGHT);
    fpi_image_device_image_captured(img_dev, img);
    g_array_free(finger_image, TRUE);
    fpi_ssm_next_state(ssm);
}

static void fpi_goodix5395_ec_control(FpDevice *dev, FpiSsm *ssm, gboolean on, gint timeout_ms) {
    GError *error = NULL;
    if (!fpi_goodix_device_ec_control(dev, TRUE, 200, &error)){
        FAIL_SSM_AND_RETURN(ssm, error);
    }
    fpi_ssm_next_state(ssm);
}

static void fpi_goodix5395_run_activate_state(FpiSsm *ssm, FpDevice *dev) {
    switch (fpi_ssm_get_cur_state(ssm)){
        case POWER_ON:
            fp_info("Activating device...");
            fp_info("Powering on sensor");
            fpi_goodix5395_ec_control(dev, ssm, TRUE, 200);
            break;
        case FINGER_DOWN_DETECTION:
            fp_info("Setting up finger down detection");
            fpi_goodix_device5395_finger_position_detection(dev, DOWN, ssm, 500);
            break;
        case WAITING_FINGER_DOWN:
            fp_info("Waiting for finger down");
            fpi_goodix_device5395_wait_for_finger_down(dev, ssm);
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
        case AC_SET_SLEEP_MODE: {
            fp_info("AC set sleep mode.");

        }
            break;
    }
}

static void goodix5395_capture_complete(FpiSsm *ssm, FpDevice *dev, GError *error) {

}

static void run_capture_state(FpDevice *dev) {
    fp_dbg("Start capture fingerprint.");
    fpi_ssm_start(fpi_ssm_new(dev, fpi_goodix5395_run_activate_state, DEVICE_ACTIVATE_NUM), goodix5395_capture_complete);
}

void fpi_device_goodix5395_change_state(FpImageDevice *img_dev, FpiImageDeviceState state) {
    switch (state) {
        case FPI_IMAGE_DEVICE_STATE_AWAIT_FINGER_ON: {
//            fp_info("Activating device...");
//            fp_info("Powering on sensor");
//            GError *error = NULL;
//            fpi_goodix_device_set_sleep_mode(img_dev, &error);
//            fpi_goodix_device_ec_control(img_dev, TRUE, 200, &error);
//            if (error == NULL) {
//                fpi_goodix_device5395_finger_position_detection(img_dev, DOWN, 500);
//                fpi_goodix_device5395_wait_for_finger_down(img_dev);
//            } else {
//                fp_dbg("Error on finger down detection %s", error->message);
//            }
            run_capture_state((FpDevice *) img_dev);
        }
            break;
//        case FPI_IMAGE_DEVICE_STATE_CAPTURE: {
//            fpi_goodix_device5395_read_finger(img_dev);
//        }
//            break;
//        case FPI_IMAGE_DEVICE_STATE_AWAIT_FINGER_OFF: {
//            fp_dbg("Waiting finger UP");
//            fpi_goodix_device5395_finger_position_detection(img_dev, UP, 500);
//            fpi_goodix_device5395_wait_for_finger_up(img_dev);
//        }
//            break;
//        case FPI_IMAGE_DEVICE_STATE_IDLE: {
//            GError *error = NULL;
//            fp_dbg("Power off device");
////            g_usleep(1000000);
//            fp_dbg("OFF");
//            if (!fpi_goodix_device_set_sleep_mode(img_dev, &error) && !fpi_goodix_device_ec_control(img_dev, FALSE, 200, &error)) {
//                fp_dbg("Error on power off %s", error->message);
//            }
//        }
//            break;
        default:
            break;
    }
}
