/*
 * Copyright (c) 2025, Tuur Martens <tuurmartens4@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "AK/CircularBuffer.h"

#include <LibMedia/SegmentParsers/ISOBMFFSegParser.h>

namespace Media::SegmentParsers {
#define FOUR_CC(a, b, c, d) \
    ((u32(a) << 24) | (u32(b) << 16) | (u32(c) << 8) | u32(d))

template<typename T>
static T read_value(CircularBuffer const& buffer, size_t offset)
{
    if (offset > buffer.used_space())
        return false;

    auto const data = buffer.peek(offset, sizeof(u32));
    T const value_be = *reinterpret_cast<T const*>(data.data());
    return AK::convert_between_host_and_big_endian(value_be);
}

[[nodiscard]]
static u32 get_segment_type(CircularBuffer const& buffer)
{
    AK::set_debug_enabled(true);
    dbgln("get_segment_type");
    if (buffer.used_space() < 8)
        return false;

    // The first 4 bytes are the size of the box, the next 4 bytes are the type.
    u32 const box_type = read_value<u32>(buffer, 4);
    dbgln("box_type: {}{}{}{}", char((box_type >> 24) & 0xFF), char((box_type >> 16) & 0xFF), char((box_type >> 8) & 0xFF), char(box_type & 0xFF));

    return box_type;
}

bool ISOBMFFSegParser::starts_with_init_segment(CircularBuffer const& buffer) const
{
    return get_segment_type(buffer) == FOUR_CC('f', 't', 'y', 'p');
}

Optional<size_t> ISOBMFFSegParser::init_segment_size(CircularBuffer const& buffer) const
{
    if (!starts_with_init_segment(buffer))
        return {};

    // The first 4 bytes are the size of the box.
    return read_value<u32>(buffer, 0);
}

bool ISOBMFFSegParser::starts_with_media_segment(CircularBuffer const& buffer) const
{
    auto const type = get_segment_type(buffer);
    // First box is either styp or moof.
    return type == FOUR_CC('s', 't', 'y', 'p') || type == FOUR_CC('m', 'o', 'o', 'f');
}

bool ISOBMFFSegParser::contains_full_init_segment(CircularBuffer const& buffer) const
{
    auto const size_bytes = init_segment_size(buffer);
    if (!size_bytes.has_value())
        return false;

    return size_bytes.value() <= buffer.used_space();
}
}
