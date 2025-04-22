/*
 * Copyright (c) 2025, Tuur Martens <tuurmartens4@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibMedia/FFmpeg/FFmpegIOContext.h>
#include <LibMedia/SegmentParsers/SegmentParser.h>

namespace Media::SegmentParsers {

namespace ISOBMFF {
#define ISOBMFF_FOUR_CC(a, b, c, d) \
    ((u32(a) << 24) | (u32(b) << 16) | (u32(c) << 8) | u32(d))

struct [[gnu::packed]] Box {
    u32 m_size;
    u32 m_type;
};

struct [[gnu::packed]] FullBox : Box {
    u8 m_version;
    u8 m_flags[3];
};

static_assert(sizeof(Box) == 8);

struct [[gnu::packed]] FileTypeBox final : Box {
    static constexpr u32 associated_type = ISOBMFF_FOUR_CC('f', 't', 'y', 'p');

    u32 m_major_brand;
    u32 m_minor_version;
    // compatible brands is of variable length, so we can't include it here.
};

enum class TrakHandlerType : u8 {
    Video,
    Audio,
    Hint
};
}

class ISOBMFFSegParser final : public SegmentParser {
public:
    [[nodiscard]] bool starts_with_init_segment(Input input) const override;
    [[nodiscard]] bool contains_full_init_segment(Input buffer) const override;
    [[nodiscard]] bool starts_with_media_segment(Input buffer) const override;
    [[nodiscard]] ErrorOr<InitializationSegment> parse_init_segment(Input buffer) const override;
};
}
