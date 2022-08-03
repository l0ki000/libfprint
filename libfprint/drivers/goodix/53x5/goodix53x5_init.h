// Goodix 53x5 driver for libfprint

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

//const guint8 goodix_5395_psk_0[] = {
//        0xba, 0x1a, 0x86, 0x03, 0x7c, 0x1d, 0x3c, 0x71, 0xc3, 0xaf, 0x34,
//        0x49, 0x55, 0xbd, 0x69, 0xa9, 0xa9, 0x86, 0x1d, 0x9e, 0x91, 0x1f,
//        0xa2, 0x49, 0x85, 0xb6, 0x77, 0xe8, 0xdb, 0xd7, 0x2d, 0x43};

void run_initialize(FpImageDevice *dev);