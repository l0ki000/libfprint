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

#include <glib.h>
#include <string.h>
#include <drivers_api.h>

#include "goodix5395_capture.h"


enum Goodix5395CaptureState {
    WAKE_UP_DEVICE,
    SETUP_FINGER_DOWN_DETECTION,
    WAITING_FOR_FINGER,
    READING_FINGER,
    SETUP_FINGER_UP_DETECTION,
    POWER_OFF_DEVICE,
    CAPTURE_NUM_STATES
};

static void goodix5395_capture_complete(FpiSsm *ssm, FpDevice *dev, GError *error) {

}


static void goodix5395_capture_run_state(FpiSsm *ssm, FpDevice *dev) {

}

void run_capture_state(FpDevice *dev) {
	fp_dbg("Start capture fingerprint.");
	fpi_ssm_start(fpi_ssm_new(dev, goodix5395_capture_run_state, CAPTURE_NUM_STATES), goodix5395_capture_complete);
}

