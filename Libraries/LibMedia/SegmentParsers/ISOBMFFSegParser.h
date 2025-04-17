/*
 * Copyright (c) 2025, Tuur Martens <tuurmartens4@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Endian.h>
#include <LibMedia/SegmentParsers/SegmentParser.h>

namespace Media::SegmentParsers {

namespace ISOBMFF {
using FourChars = u8[4];

struct [[gnu::packed]] Box {
    u32 m_size;
    FourChars m_type;
};

struct [[gnu::packed]] FileTypeBox final : Box {
    FourChars m_major_brand;
    u32 m_minor_version;
};
}

class ISOBMFFSegParser final : public SegmentParser {
public:
    [[nodiscard]] bool starts_with_init_segment(ReadonlyBytes buffer) override;
    [[nodiscard]] bool starts_with_media_segment(ReadonlyBytes buffer) override;
};
}
