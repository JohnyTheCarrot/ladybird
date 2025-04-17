/*
 * Copyright (c) 2025, Tuur Martens <tuurmartens4@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibMedia/SegmentParsers/ISOBMFFSegParser.h>

namespace Media::SegmentParsers {
#define FOUR_CC(a, b, c, d) \
    ((u32(a) << 24) | (u32(b) << 16) | (u32(c) << 8) | u32(d))

[[nodiscard]]
static u32 get_segment_type(ReadonlyBytes const& buffer)
{
    if (buffer.size() < 8)
        return false;

    // The first 4 bytes are the size of the box, the next 4 bytes are the type.
    u32 const box_type = *reinterpret_cast<u32 const*>(buffer.data() + 4);
    return box_type;
}

bool ISOBMFFSegParser::starts_with_init_segment(ReadonlyBytes buffer)
{
    return get_segment_type(buffer) == FOUR_CC('f', 't', 'y', 'p');
}

bool ISOBMFFSegParser::starts_with_media_segment(ReadonlyBytes buffer)
{
    auto const type = get_segment_type(buffer);
    // First box is either styp or moof.
    return type == FOUR_CC('s', 't', 'y', 'p') || type == FOUR_CC('m', 'o', 'o', 'f');
}
}
