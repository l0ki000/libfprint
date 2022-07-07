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

enum Goodix5395ActivateState {
    POWER_ON,
    FINGER_DOWN_DETECTION,
    WAITING_FINGER,
    FINGER_UP_DETECTION,
    AC_SET_SLEEP_MODE,
    POWER_OFF,
    DEVICE_ACTIVATE_END,
};

static void fpi_goodix5395_ec_control(FpDevice *dev, FpiSsm *ssm, gboolean on, gint timeout_ms){
    GError *error = NULL;
    fpi_goodix_device_ec_control(dev, TRUE, 200, &error);
    if (error){
        FAIL_SSM_AND_RETURN(ssm, FPI_GOODIX_DEVICE_ERROR(fpi_ssm_get_cur_state(ssm), "Error ec control", NULL));
    }
    fpi_ssm_next_state(ssm);
}

static void fpi_goodix_device5395_finger_position_detection(FpDevice *dev, FpiSsm *ssm, enum FingerDetectionOperation posix, gint timeout_ms){
    GError *error = NULL;
    fpi_goodix_device_setup_finger_position_detection(dev, posix, timeout_ms, &error);
    if(error) {
        FAIL_SSM_AND_RETURN(ssm, error);
    }
    fpi_ssm_next_state(ssm);
}

static void fpi_goodix_device5395_wait_for_finger_up(FpDevice *dev, FpiSsm *ssm) {
    GError *error = NULL;
    fpi_goodix_device_wait_for_finger_down(dev, 1000000, &error);
    if(error) {
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
            fpi_goodix_device5395_finger_position_detection(dev, ssm, DOWN, 500);
            break;
        case WAITING_FINGER:
            fp_info("Waiting for finger up");
            fpi_goodix_device5395_wait_for_finger_up(dev, ssm);
            break;
        case FINGER_UP_DETECTION:
            fp_info("Setting up finger up detection...");
            break;
    }
}

static void goodix5395_capture_complete(FpiSsm *ssm, FpDevice *dev, GError *error) {

}

void run_capture_state(FpDevice *dev) {
    fp_dbg("Start capture fingerprint.");
    fpi_ssm_start(fpi_ssm_new(dev, fpi_goodix5395_run_activate_state, DEVICE_ACTIVATE_END), goodix5395_capture_complete);
}
