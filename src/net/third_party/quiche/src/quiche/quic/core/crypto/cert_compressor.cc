// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/crypto/cert_compressor.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "zlib.h"

namespace quic {

namespace {

// kCommonCertSubstrings contains ~1500 bytes of common certificate substrings
// in order to help zlib. This was generated via a fairly dumb algorithm from
// the Alexa Top 5000 set - we could probably do better.
static const unsigned char kCommonCertSubstrings[] = {
    0x04, 0x02, 0x30, 0x00, 0x30, 0x1d, 0x06, 0x03, 0x55, 0x1d, 0x25, 0x04,
    0x16, 0x30, 0x14, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03,
    0x01, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x02, 0x30,
    0x5f, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x86, 0xf8, 0x42, 0x04, 0x01,
    0x06, 0x06, 0x0b, 0x60, 0x86, 0x48, 0x01, 0x86, 0xfd, 0x6d, 0x01, 0x07,
    0x17, 0x01, 0x30, 0x33, 0x20, 0x45, 0x78, 0x74, 0x65, 0x6e, 0x64, 0x65,
    0x64, 0x20, 0x56, 0x61, 0x6c, 0x69, 0x64, 0x61, 0x74, 0x69, 0x6f, 0x6e,
    0x20, 0x53, 0x20, 0x4c, 0x69, 0x6d, 0x69, 0x74, 0x65, 0x64, 0x31, 0x34,
    0x20, 0x53, 0x53, 0x4c, 0x20, 0x43, 0x41, 0x30, 0x1e, 0x17, 0x0d, 0x31,
    0x32, 0x20, 0x53, 0x65, 0x63, 0x75, 0x72, 0x65, 0x20, 0x53, 0x65, 0x72,
    0x76, 0x65, 0x72, 0x20, 0x43, 0x41, 0x30, 0x2d, 0x61, 0x69, 0x61, 0x2e,
    0x76, 0x65, 0x72, 0x69, 0x73, 0x69, 0x67, 0x6e, 0x2e, 0x63, 0x6f, 0x6d,
    0x2f, 0x45, 0x2d, 0x63, 0x72, 0x6c, 0x2e, 0x76, 0x65, 0x72, 0x69, 0x73,
    0x69, 0x67, 0x6e, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x45, 0x2e, 0x63, 0x65,
    0x72, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01,
    0x01, 0x05, 0x05, 0x00, 0x03, 0x82, 0x01, 0x01, 0x00, 0x4a, 0x2e, 0x63,
    0x6f, 0x6d, 0x2f, 0x72, 0x65, 0x73, 0x6f, 0x75, 0x72, 0x63, 0x65, 0x73,
    0x2f, 0x63, 0x70, 0x73, 0x20, 0x28, 0x63, 0x29, 0x30, 0x30, 0x09, 0x06,
    0x03, 0x55, 0x1d, 0x13, 0x04, 0x02, 0x30, 0x00, 0x30, 0x1d, 0x30, 0x0d,
    0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05, 0x05,
    0x00, 0x03, 0x82, 0x01, 0x01, 0x00, 0x7b, 0x30, 0x1d, 0x06, 0x03, 0x55,
    0x1d, 0x0e, 0x30, 0x82, 0x01, 0x22, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86,
    0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00, 0x03, 0x82, 0x01,
    0x0f, 0x00, 0x30, 0x82, 0x01, 0x0a, 0x02, 0x82, 0x01, 0x01, 0x00, 0xd2,
    0x6f, 0x64, 0x6f, 0x63, 0x61, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x43, 0x2e,
    0x63, 0x72, 0x6c, 0x30, 0x1d, 0x06, 0x03, 0x55, 0x1d, 0x0e, 0x04, 0x16,
    0x04, 0x14, 0xb4, 0x2e, 0x67, 0x6c, 0x6f, 0x62, 0x61, 0x6c, 0x73, 0x69,
    0x67, 0x6e, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x72, 0x30, 0x0b, 0x06, 0x03,
    0x55, 0x1d, 0x0f, 0x04, 0x04, 0x03, 0x02, 0x01, 0x30, 0x0d, 0x06, 0x09,
    0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05, 0x05, 0x00, 0x30,
    0x81, 0xca, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13,
    0x02, 0x55, 0x53, 0x31, 0x10, 0x30, 0x0e, 0x06, 0x03, 0x55, 0x04, 0x08,
    0x13, 0x07, 0x41, 0x72, 0x69, 0x7a, 0x6f, 0x6e, 0x61, 0x31, 0x13, 0x30,
    0x11, 0x06, 0x03, 0x55, 0x04, 0x07, 0x13, 0x0a, 0x53, 0x63, 0x6f, 0x74,
    0x74, 0x73, 0x64, 0x61, 0x6c, 0x65, 0x31, 0x1a, 0x30, 0x18, 0x06, 0x03,
    0x55, 0x04, 0x0a, 0x13, 0x11, 0x47, 0x6f, 0x44, 0x61, 0x64, 0x64, 0x79,
    0x2e, 0x63, 0x6f, 0x6d, 0x2c, 0x20, 0x49, 0x6e, 0x63, 0x2e, 0x31, 0x33,
    0x30, 0x31, 0x06, 0x03, 0x55, 0x04, 0x0b, 0x13, 0x2a, 0x68, 0x74, 0x74,
    0x70, 0x3a, 0x2f, 0x2f, 0x63, 0x65, 0x72, 0x74, 0x69, 0x66, 0x69, 0x63,
    0x61, 0x74, 0x65, 0x73, 0x2e, 0x67, 0x6f, 0x64, 0x61, 0x64, 0x64, 0x79,
    0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x72, 0x65, 0x70, 0x6f, 0x73, 0x69, 0x74,
    0x6f, 0x72, 0x79, 0x31, 0x30, 0x30, 0x2e, 0x06, 0x03, 0x55, 0x04, 0x03,
    0x13, 0x27, 0x47, 0x6f, 0x20, 0x44, 0x61, 0x64, 0x64, 0x79, 0x20, 0x53,
    0x65, 0x63, 0x75, 0x72, 0x65, 0x20, 0x43, 0x65, 0x72, 0x74, 0x69, 0x66,
    0x69, 0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x20, 0x41, 0x75, 0x74, 0x68,
    0x6f, 0x72, 0x69, 0x74, 0x79, 0x31, 0x11, 0x30, 0x0f, 0x06, 0x03, 0x55,
    0x04, 0x05, 0x13, 0x08, 0x30, 0x37, 0x39, 0x36, 0x39, 0x32, 0x38, 0x37,
    0x30, 0x1e, 0x17, 0x0d, 0x31, 0x31, 0x30, 0x0e, 0x06, 0x03, 0x55, 0x1d,
    0x0f, 0x01, 0x01, 0xff, 0x04, 0x04, 0x03, 0x02, 0x05, 0xa0, 0x30, 0x0c,
    0x06, 0x03, 0x55, 0x1d, 0x13, 0x01, 0x01, 0xff, 0x04, 0x02, 0x30, 0x00,
    0x30, 0x1d, 0x30, 0x0f, 0x06, 0x03, 0x55, 0x1d, 0x13, 0x01, 0x01, 0xff,
    0x04, 0x05, 0x30, 0x03, 0x01, 0x01, 0x00, 0x30, 0x1d, 0x06, 0x03, 0x55,
    0x1d, 0x25, 0x04, 0x16, 0x30, 0x14, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05,
    0x05, 0x07, 0x03, 0x01, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07,
    0x03, 0x02, 0x30, 0x0e, 0x06, 0x03, 0x55, 0x1d, 0x0f, 0x01, 0x01, 0xff,
    0x04, 0x04, 0x03, 0x02, 0x05, 0xa0, 0x30, 0x33, 0x06, 0x03, 0x55, 0x1d,
    0x1f, 0x04, 0x2c, 0x30, 0x2a, 0x30, 0x28, 0xa0, 0x26, 0xa0, 0x24, 0x86,
    0x22, 0x68, 0x74, 0x74, 0x70, 0x3a, 0x2f, 0x2f, 0x63, 0x72, 0x6c, 0x2e,
    0x67, 0x6f, 0x64, 0x61, 0x64, 0x64, 0x79, 0x2e, 0x63, 0x6f, 0x6d, 0x2f,
    0x67, 0x64, 0x73, 0x31, 0x2d, 0x32, 0x30, 0x2a, 0x30, 0x28, 0x06, 0x08,
    0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x02, 0x01, 0x16, 0x1c, 0x68, 0x74,
    0x74, 0x70, 0x73, 0x3a, 0x2f, 0x2f, 0x77, 0x77, 0x77, 0x2e, 0x76, 0x65,
    0x72, 0x69, 0x73, 0x69, 0x67, 0x6e, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x63,
    0x70, 0x73, 0x30, 0x34, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x5a, 0x17,
    0x0d, 0x31, 0x33, 0x30, 0x35, 0x30, 0x39, 0x06, 0x08, 0x2b, 0x06, 0x01,
    0x05, 0x05, 0x07, 0x30, 0x02, 0x86, 0x2d, 0x68, 0x74, 0x74, 0x70, 0x3a,
    0x2f, 0x2f, 0x73, 0x30, 0x39, 0x30, 0x37, 0x06, 0x08, 0x2b, 0x06, 0x01,
    0x05, 0x05, 0x07, 0x02, 0x30, 0x44, 0x06, 0x03, 0x55, 0x1d, 0x20, 0x04,
    0x3d, 0x30, 0x3b, 0x30, 0x39, 0x06, 0x0b, 0x60, 0x86, 0x48, 0x01, 0x86,
    0xf8, 0x45, 0x01, 0x07, 0x17, 0x06, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03,
    0x55, 0x04, 0x06, 0x13, 0x02, 0x47, 0x42, 0x31, 0x1b, 0x53, 0x31, 0x17,
    0x30, 0x15, 0x06, 0x03, 0x55, 0x04, 0x0a, 0x13, 0x0e, 0x56, 0x65, 0x72,
    0x69, 0x53, 0x69, 0x67, 0x6e, 0x2c, 0x20, 0x49, 0x6e, 0x63, 0x2e, 0x31,
    0x1f, 0x30, 0x1d, 0x06, 0x03, 0x55, 0x04, 0x0b, 0x13, 0x16, 0x56, 0x65,
    0x72, 0x69, 0x53, 0x69, 0x67, 0x6e, 0x20, 0x54, 0x72, 0x75, 0x73, 0x74,
    0x20, 0x4e, 0x65, 0x74, 0x77, 0x6f, 0x72, 0x6b, 0x31, 0x3b, 0x30, 0x39,
    0x06, 0x03, 0x55, 0x04, 0x0b, 0x13, 0x32, 0x54, 0x65, 0x72, 0x6d, 0x73,
    0x20, 0x6f, 0x66, 0x20, 0x75, 0x73, 0x65, 0x20, 0x61, 0x74, 0x20, 0x68,
    0x74, 0x74, 0x70, 0x73, 0x3a, 0x2f, 0x2f, 0x77, 0x77, 0x77, 0x2e, 0x76,
    0x65, 0x72, 0x69, 0x73, 0x69, 0x67, 0x6e, 0x2e, 0x63, 0x6f, 0x6d, 0x2f,
    0x72, 0x70, 0x61, 0x20, 0x28, 0x63, 0x29, 0x30, 0x31, 0x10, 0x30, 0x0e,
    0x06, 0x03, 0x55, 0x04, 0x07, 0x13, 0x07, 0x53, 0x31, 0x13, 0x30, 0x11,
    0x06, 0x03, 0x55, 0x04, 0x0b, 0x13, 0x0a, 0x47, 0x31, 0x13, 0x30, 0x11,
    0x06, 0x0b, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x3c, 0x02, 0x01,
    0x03, 0x13, 0x02, 0x55, 0x31, 0x16, 0x30, 0x14, 0x06, 0x03, 0x55, 0x04,
    0x03, 0x14, 0x31, 0x19, 0x30, 0x17, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13,
    0x31, 0x1d, 0x30, 0x1b, 0x06, 0x03, 0x55, 0x04, 0x0f, 0x13, 0x14, 0x50,
    0x72, 0x69, 0x76, 0x61, 0x74, 0x65, 0x20, 0x4f, 0x72, 0x67, 0x61, 0x6e,
    0x69, 0x7a, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x31, 0x12, 0x31, 0x21, 0x30,
    0x1f, 0x06, 0x03, 0x55, 0x04, 0x0b, 0x13, 0x18, 0x44, 0x6f, 0x6d, 0x61,
    0x69, 0x6e, 0x20, 0x43, 0x6f, 0x6e, 0x74, 0x72, 0x6f, 0x6c, 0x20, 0x56,
    0x61, 0x6c, 0x69, 0x64, 0x61, 0x74, 0x65, 0x64, 0x31, 0x14, 0x31, 0x31,
    0x30, 0x2f, 0x06, 0x03, 0x55, 0x04, 0x0b, 0x13, 0x28, 0x53, 0x65, 0x65,
    0x20, 0x77, 0x77, 0x77, 0x2e, 0x72, 0x3a, 0x2f, 0x2f, 0x73, 0x65, 0x63,
    0x75, 0x72, 0x65, 0x2e, 0x67, 0x47, 0x6c, 0x6f, 0x62, 0x61, 0x6c, 0x53,
    0x69, 0x67, 0x6e, 0x31, 0x53, 0x65, 0x72, 0x76, 0x65, 0x72, 0x43, 0x41,
    0x2e, 0x63, 0x72, 0x6c, 0x56, 0x65, 0x72, 0x69, 0x53, 0x69, 0x67, 0x6e,
    0x20, 0x43, 0x6c, 0x61, 0x73, 0x73, 0x20, 0x33, 0x20, 0x45, 0x63, 0x72,
    0x6c, 0x2e, 0x67, 0x65, 0x6f, 0x74, 0x72, 0x75, 0x73, 0x74, 0x2e, 0x63,
    0x6f, 0x6d, 0x2f, 0x63, 0x72, 0x6c, 0x73, 0x2f, 0x73, 0x64, 0x31, 0x1a,
    0x30, 0x18, 0x06, 0x03, 0x55, 0x04, 0x0a, 0x68, 0x74, 0x74, 0x70, 0x3a,
    0x2f, 0x2f, 0x45, 0x56, 0x49, 0x6e, 0x74, 0x6c, 0x2d, 0x63, 0x63, 0x72,
    0x74, 0x2e, 0x67, 0x77, 0x77, 0x77, 0x2e, 0x67, 0x69, 0x63, 0x65, 0x72,
    0x74, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x31, 0x6f, 0x63, 0x73, 0x70, 0x2e,
    0x76, 0x65, 0x72, 0x69, 0x73, 0x69, 0x67, 0x6e, 0x2e, 0x63, 0x6f, 0x6d,
    0x30, 0x39, 0x72, 0x61, 0x70, 0x69, 0x64, 0x73, 0x73, 0x6c, 0x2e, 0x63,
    0x6f, 0x73, 0x2e, 0x67, 0x6f, 0x64, 0x61, 0x64, 0x64, 0x79, 0x2e, 0x63,
    0x6f, 0x6d, 0x2f, 0x72, 0x65, 0x70, 0x6f, 0x73, 0x69, 0x74, 0x6f, 0x72,
    0x79, 0x2f, 0x30, 0x81, 0x80, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05,
    0x07, 0x01, 0x01, 0x04, 0x74, 0x30, 0x72, 0x30, 0x24, 0x06, 0x08, 0x2b,
    0x06, 0x01, 0x05, 0x05, 0x07, 0x30, 0x01, 0x86, 0x18, 0x68, 0x74, 0x74,
    0x70, 0x3a, 0x2f, 0x2f, 0x6f, 0x63, 0x73, 0x70, 0x2e, 0x67, 0x6f, 0x64,
    0x61, 0x64, 0x64, 0x79, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x30, 0x4a, 0x06,
    0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x30, 0x02, 0x86, 0x3e, 0x68,
    0x74, 0x74, 0x70, 0x3a, 0x2f, 0x2f, 0x63, 0x65, 0x72, 0x74, 0x69, 0x66,
    0x69, 0x63, 0x61, 0x74, 0x65, 0x73, 0x2e, 0x67, 0x6f, 0x64, 0x61, 0x64,
    0x64, 0x79, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x72, 0x65, 0x70, 0x6f, 0x73,
    0x69, 0x74, 0x6f, 0x72, 0x79, 0x2f, 0x67, 0x64, 0x5f, 0x69, 0x6e, 0x74,
    0x65, 0x72, 0x6d, 0x65, 0x64, 0x69, 0x61, 0x74, 0x65, 0x2e, 0x63, 0x72,
    0x74, 0x30, 0x1f, 0x06, 0x03, 0x55, 0x1d, 0x23, 0x04, 0x18, 0x30, 0x16,
    0x80, 0x14, 0xfd, 0xac, 0x61, 0x32, 0x93, 0x6c, 0x45, 0xd6, 0xe2, 0xee,
    0x85, 0x5f, 0x9a, 0xba, 0xe7, 0x76, 0x99, 0x68, 0xcc, 0xe7, 0x30, 0x27,
    0x86, 0x29, 0x68, 0x74, 0x74, 0x70, 0x3a, 0x2f, 0x2f, 0x63, 0x86, 0x30,
    0x68, 0x74, 0x74, 0x70, 0x3a, 0x2f, 0x2f, 0x73,
};

// CertEntry represents a certificate in compressed form. Each entry is one of
// the three types enumerated in |Type|.
struct CertEntry {
 public:
  enum Type {
    // Type 0 is reserved to mean "end of list" in the wire format.

    // COMPRESSED means that the certificate is included in the trailing zlib
    // data.
    COMPRESSED = 1,
    // CACHED means that the certificate is already known to the peer and will
    // be replaced by its 64-bit hash (in |hash|).
    CACHED = 2,
  };

  Type type;
  uint64_t hash;
  uint64_t set_hash;
  uint32_t index;
};

// MatchCerts returns a vector of CertEntries describing how to most
// efficiently represent |certs| to a peer who has cached the certificates
// with the 64-bit, FNV-1a hashes in |client_cached_cert_hashes|.
std::vector<CertEntry> MatchCerts(const std::vector<std::string>& certs,
                                  absl::string_view client_cached_cert_hashes) {
  std::vector<CertEntry> entries;
  entries.reserve(certs.size());

  const bool cached_valid =
      client_cached_cert_hashes.size() % sizeof(uint64_t) == 0 &&
      !client_cached_cert_hashes.empty();

  for (auto i = certs.begin(); i != certs.end(); ++i) {
    CertEntry entry;

    if (cached_valid) {
      bool cached = false;

      uint64_t hash = QuicUtils::FNV1a_64_Hash(*i);
      // This assumes that the machine is little-endian.
      for (size_t j = 0; j < client_cached_cert_hashes.size();
           j += sizeof(uint64_t)) {
        uint64_t cached_hash;
        memcpy(&cached_hash, client_cached_cert_hashes.data() + j,
               sizeof(uint64_t));
        if (hash != cached_hash) {
          continue;
        }

        entry.type = CertEntry::CACHED;
        entry.hash = hash;
        entries.push_back(entry);
        cached = true;
        break;
      }

      if (cached) {
        continue;
      }
    }

    entry.type = CertEntry::COMPRESSED;
    entries.push_back(entry);
  }

  return entries;
}

// CertEntriesSize returns the size, in bytes, of the serialised form of
// |entries|.
size_t CertEntriesSize(const std::vector<CertEntry>& entries) {
  size_t entries_size = 0;

  for (auto i = entries.begin(); i != entries.end(); ++i) {
    entries_size++;
    switch (i->type) {
      case CertEntry::COMPRESSED:
        break;
      case CertEntry::CACHED:
        entries_size += sizeof(uint64_t);
        break;
    }
  }

  entries_size++;  // for end marker

  return entries_size;
}

// SerializeCertEntries serialises |entries| to |out|, which must have enough
// space to contain them.
void SerializeCertEntries(uint8_t* out, const std::vector<CertEntry>& entries) {
  for (auto i = entries.begin(); i != entries.end(); ++i) {
    *out++ = static_cast<uint8_t>(i->type);
    switch (i->type) {
      case CertEntry::COMPRESSED:
        break;
      case CertEntry::CACHED:
        memcpy(out, &i->hash, sizeof(i->hash));
        out += sizeof(uint64_t);
        break;
    }
  }

  *out++ = 0;  // end marker
}

// ZlibDictForEntries returns a string that contains the zlib pre-shared
// dictionary to use in order to decompress a zlib block following |entries|.
// |certs| is one-to-one with |entries| and contains the certificates for those
// entries that are CACHED.
std::string ZlibDictForEntries(const std::vector<CertEntry>& entries,
                               const std::vector<std::string>& certs) {
  std::string zlib_dict;

  // The dictionary starts with the cached certs in reverse order.
  size_t zlib_dict_size = 0;
  for (size_t i = certs.size() - 1; i < certs.size(); i--) {
    if (entries[i].type != CertEntry::COMPRESSED) {
      zlib_dict_size += certs[i].size();
    }
  }

  // At the end of the dictionary is a block of common certificate substrings.
  zlib_dict_size += sizeof(kCommonCertSubstrings);

  zlib_dict.reserve(zlib_dict_size);

  for (size_t i = certs.size() - 1; i < certs.size(); i--) {
    if (entries[i].type != CertEntry::COMPRESSED) {
      zlib_dict += certs[i];
    }
  }

  zlib_dict += std::string(reinterpret_cast<const char*>(kCommonCertSubstrings),
                           sizeof(kCommonCertSubstrings));

  QUICHE_DCHECK_EQ(zlib_dict.size(), zlib_dict_size);

  return zlib_dict;
}

// HashCerts returns the FNV-1a hashes of |certs|.
std::vector<uint64_t> HashCerts(const std::vector<std::string>& certs) {
  std::vector<uint64_t> ret;
  ret.reserve(certs.size());

  for (auto i = certs.begin(); i != certs.end(); ++i) {
    ret.push_back(QuicUtils::FNV1a_64_Hash(*i));
  }

  return ret;
}

// ParseEntries parses the serialised form of a vector of CertEntries from
// |in_out| and writes them to |out_entries|. CACHED entries are resolved using
// |cached_certs| and written to |out_certs|. |in_out| is updated to contain
// the trailing data.
bool ParseEntries(absl::string_view* in_out,
                  const std::vector<std::string>& cached_certs,
                  std::vector<CertEntry>* out_entries,
                  std::vector<std::string>* out_certs) {
  absl::string_view in = *in_out;
  std::vector<uint64_t> cached_hashes;

  out_entries->clear();
  out_certs->clear();

  for (;;) {
    if (in.empty()) {
      return false;
    }
    CertEntry entry;
    const uint8_t type_byte = in[0];
    in.remove_prefix(1);

    if (type_byte == 0) {
      break;
    }

    entry.type = static_cast<CertEntry::Type>(type_byte);

    switch (entry.type) {
      case CertEntry::COMPRESSED:
        out_certs->push_back(std::string());
        break;
      case CertEntry::CACHED: {
        if (in.size() < sizeof(uint64_t)) {
          return false;
        }
        memcpy(&entry.hash, in.data(), sizeof(uint64_t));
        in.remove_prefix(sizeof(uint64_t));

        if (cached_hashes.size() != cached_certs.size()) {
          cached_hashes = HashCerts(cached_certs);
        }
        bool found = false;
        for (size_t i = 0; i < cached_hashes.size(); i++) {
          if (cached_hashes[i] == entry.hash) {
            out_certs->push_back(cached_certs[i]);
            found = true;
            break;
          }
        }
        if (!found) {
          return false;
        }
        break;
      }

      default:
        return false;
    }
    out_entries->push_back(entry);
  }

  *in_out = in;
  return true;
}

// ScopedZLib deals with the automatic destruction of a zlib context.
class ScopedZLib {
 public:
  enum Type {
    INFLATE,
    DEFLATE,
  };

  explicit ScopedZLib(Type type) : z_(nullptr), type_(type) {}

  void reset(z_stream* z) {
    Clear();
    z_ = z;
  }

  ~ScopedZLib() { Clear(); }

 private:
  void Clear() {
    if (!z_) {
      return;
    }

    if (type_ == DEFLATE) {
      deflateEnd(z_);
    } else {
      inflateEnd(z_);
    }
    z_ = nullptr;
  }

  z_stream* z_;
  const Type type_;
};

}  // anonymous namespace

// static
std::string CertCompressor::CompressChain(
    const std::vector<std::string>& certs,
    absl::string_view client_cached_cert_hashes) {
  const std::vector<CertEntry> entries =
      MatchCerts(certs, client_cached_cert_hashes);
  QUICHE_DCHECK_EQ(entries.size(), certs.size());

  size_t uncompressed_size = 0;
  for (size_t i = 0; i < entries.size(); i++) {
    if (entries[i].type == CertEntry::COMPRESSED) {
      uncompressed_size += 4 /* uint32_t length */ + certs[i].size();
    }
  }

  size_t compressed_size = 0;
  z_stream z;
  ScopedZLib scoped_z(ScopedZLib::DEFLATE);

  if (uncompressed_size > 0) {
    memset(&z, 0, sizeof(z));
    int rv = deflateInit(&z, Z_DEFAULT_COMPRESSION);
    QUICHE_DCHECK_EQ(Z_OK, rv);
    if (rv != Z_OK) {
      return "";
    }
    scoped_z.reset(&z);

    std::string zlib_dict = ZlibDictForEntries(entries, certs);

    rv = deflateSetDictionary(
        &z, reinterpret_cast<const uint8_t*>(&zlib_dict[0]), zlib_dict.size());
    QUICHE_DCHECK_EQ(Z_OK, rv);
    if (rv != Z_OK) {
      return "";
    }

    compressed_size = deflateBound(&z, uncompressed_size);
  }

  const size_t entries_size = CertEntriesSize(entries);

  std::string result;
  result.resize(entries_size + (uncompressed_size > 0 ? 4 : 0) +
                compressed_size);

  uint8_t* j = reinterpret_cast<uint8_t*>(&result[0]);
  SerializeCertEntries(j, entries);
  j += entries_size;

  if (uncompressed_size == 0) {
    return result;
  }

  uint32_t uncompressed_size_32 = uncompressed_size;
  memcpy(j, &uncompressed_size_32, sizeof(uint32_t));
  j += sizeof(uint32_t);

  int rv;

  z.next_out = j;
  z.avail_out = compressed_size;

  for (size_t i = 0; i < certs.size(); i++) {
    if (entries[i].type != CertEntry::COMPRESSED) {
      continue;
    }

    uint32_t length32 = certs[i].size();
    z.next_in = reinterpret_cast<uint8_t*>(&length32);
    z.avail_in = sizeof(length32);
    rv = deflate(&z, Z_NO_FLUSH);
    QUICHE_DCHECK_EQ(Z_OK, rv);
    QUICHE_DCHECK_EQ(0u, z.avail_in);
    if (rv != Z_OK || z.avail_in) {
      return "";
    }

    z.next_in =
        const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(certs[i].data()));
    z.avail_in = certs[i].size();
    rv = deflate(&z, Z_NO_FLUSH);
    QUICHE_DCHECK_EQ(Z_OK, rv);
    QUICHE_DCHECK_EQ(0u, z.avail_in);
    if (rv != Z_OK || z.avail_in) {
      return "";
    }
  }

  z.avail_in = 0;
  rv = deflate(&z, Z_FINISH);
  QUICHE_DCHECK_EQ(Z_STREAM_END, rv);
  if (rv != Z_STREAM_END) {
    return "";
  }

  result.resize(result.size() - z.avail_out);
  return result;
}

// static
bool CertCompressor::DecompressChain(
    absl::string_view in, const std::vector<std::string>& cached_certs,
    std::vector<std::string>* out_certs) {
  std::vector<CertEntry> entries;
  if (!ParseEntries(&in, cached_certs, &entries, out_certs)) {
    return false;
  }
  QUICHE_DCHECK_EQ(entries.size(), out_certs->size());

  std::unique_ptr<uint8_t[]> uncompressed_data;
  absl::string_view uncompressed;

  if (!in.empty()) {
    if (in.size() < sizeof(uint32_t)) {
      return false;
    }

    uint32_t uncompressed_size;
    memcpy(&uncompressed_size, in.data(), sizeof(uncompressed_size));
    in.remove_prefix(sizeof(uint32_t));

    if (uncompressed_size > 128 * 1024) {
      return false;
    }

    uncompressed_data = std::make_unique<uint8_t[]>(uncompressed_size);
    z_stream z;
    ScopedZLib scoped_z(ScopedZLib::INFLATE);

    memset(&z, 0, sizeof(z));
    z.next_out = uncompressed_data.get();
    z.avail_out = uncompressed_size;
    z.next_in =
        const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(in.data()));
    z.avail_in = in.size();

    if (Z_OK != inflateInit(&z)) {
      return false;
    }
    scoped_z.reset(&z);

    int rv = inflate(&z, Z_FINISH);
    if (rv == Z_NEED_DICT) {
      std::string zlib_dict = ZlibDictForEntries(entries, *out_certs);
      const uint8_t* dict = reinterpret_cast<const uint8_t*>(zlib_dict.data());
      if (Z_OK != inflateSetDictionary(&z, dict, zlib_dict.size())) {
        return false;
      }
      rv = inflate(&z, Z_FINISH);
    }

    if (Z_STREAM_END != rv || z.avail_out > 0 || z.avail_in > 0) {
      return false;
    }

    uncompressed = absl::string_view(
        reinterpret_cast<char*>(uncompressed_data.get()), uncompressed_size);
  }

  for (size_t i = 0; i < entries.size(); i++) {
    switch (entries[i].type) {
      case CertEntry::COMPRESSED:
        if (uncompressed.size() < sizeof(uint32_t)) {
          return false;
        }
        uint32_t cert_len;
        memcpy(&cert_len, uncompressed.data(), sizeof(cert_len));
        uncompressed.remove_prefix(sizeof(uint32_t));
        if (uncompressed.size() < cert_len) {
          return false;
        }
        (*out_certs)[i] = std::string(uncompressed.substr(0, cert_len));
        uncompressed.remove_prefix(cert_len);
        break;
      case CertEntry::CACHED:
        break;
    }
  }

  if (!uncompressed.empty()) {
    return false;
  }

  return true;
}

}  // namespace quic
