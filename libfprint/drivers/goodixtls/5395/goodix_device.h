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

#pragma once

#include "goodix_protocol.h"

G_DECLARE_DERIVABLE_TYPE(FpiGoodixDevice, fpi_goodix_device, FPI, GOODIX_DEVICE, FpImageDevice);

#define FPI_TYPE_GOODIX_DEVICE (fpi_goodix_device_get_type())


struct _FpiGoodixDeviceClass
{
    FpImageDeviceClass parent;

    gint interface;
    guint8 ep_in;
    guint8 ep_out;
};

typedef void (*GoodixDeviceReceiveCallback)(FpDevice *dev, GoodixMessage *message, GError *error);

gboolean fpi_goodix_device_init_device(FpDevice *dev, GError **error);
gboolean fpi_goodix_device_deinit_device(FpDevice *dev, GError **error);

gboolean fpi_goodix_device_send(FpDevice *dev, GoodixMessage *message, gboolean calc_checksum, gint timeout_ms,
                            gboolean reply, GError **error);
gboolean fpi_goodix_device_receive_data(FpDevice *dev, GoodixMessage **message, GError **error);
void fpi_goodix_device_empty_buffer(FpDevice *dev);