// Crypto utils for goodix 5395 driver

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

GByteArray *crypto_utils_sha256_hash(guint8 *data, gint len);
GByteArray *crypto_utils_derive_key(GByteArray *psk, GByteArray *random_data, gsize session_key_lenght);
GByteArray *crypto_utils_HMAC_SHA256(GByteArray *key, GByteArray *data);