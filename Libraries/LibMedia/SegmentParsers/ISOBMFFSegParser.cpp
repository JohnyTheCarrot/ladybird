/*
 * Copyright (c) 2025, Tuur Martens <tuurmartens4@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "AK/CircularBuffer.h"

#include <LibMedia/SegmentParsers/ISOBMFFSegParser.h>

namespace Media::SegmentParsers {
template<typename T>
static Optional<T> read_value(CircularBuffer const& buffer, size_t offset)
{
    if (offset + sizeof(T) > buffer.used_space())
        return {};

    auto const data = buffer.peek(offset, sizeof(T));

    T const value_be = *reinterpret_cast<T const*>(data.data());
    if constexpr (IsOneOf<T, u8, u16, u32, u64>) {
        return AK::convert_between_host_and_big_endian(value_be);
    } else {
        return value_be;
    }
}

[[nodiscard]]
static u32 get_segment_type(CircularBuffer const& buffer, size_t segment_start_offset = 0)
{
    AK::set_debug_enabled(true);
    dbgln("get_segment_type");
    if (buffer.used_space() < 8)
        return false;

    // The first 4 bytes are the size of the box, the next 4 bytes are the type.
    auto const opt_box_type = read_value<u32>(buffer, segment_start_offset + 4);
    if (!opt_box_type.has_value())
        return {};
    auto const box_type = opt_box_type.value();
    dbgln("box_type: {}{}{}{}", char((box_type >> 24) & 0xFF), char((box_type >> 16) & 0xFF), char((box_type >> 8) & 0xFF), char(box_type & 0xFF));

    return box_type;
}

template<typename B>
requires AK::Concepts::DerivedFrom<B, ISOBMFF::Box>
static Optional<B> read_box(CircularBuffer const& buffer, size_t offset)
{
    auto const opt_box = read_value<B>(buffer, offset);
    if (!opt_box.has_value()) {
        dbgln("ISOBMFFSegParser::read_box: no box at offset {}", offset);
        return {};
    }

    auto box = opt_box.value();
    box.m_type = AK::convert_between_host_and_big_endian(box.m_type);
    box.m_size = AK::convert_between_host_and_big_endian(box.m_size);

    // If we reached this point, the box is probably valid, but we need to check if the type is correct.
    if constexpr (!IsOneOf<B, ISOBMFF::Box, ISOBMFF::FullBox>) {
        if (box.m_type != B::associated_type) {
            dbgln("ISOBMFFSegParser::read_box: box type mismatch");
            dbgln("box.m_type: {}{}{}{}", char((box.m_type >> 24) & 0xFF), char((box.m_type >> 16) & 0xFF), char((box.m_type >> 8) & 0xFF), char(box.m_type & 0xFF));
            dbgln("B::associated_type: {}{}{}{}", char((B::associated_type >> 24) & 0xFF), char((B::associated_type >> 16) & 0xFF), char((B::associated_type >> 8) & 0xFF), char(B::associated_type & 0xFF));
            return {};
        }
    }

    return box;
}

bool ISOBMFFSegParser::starts_with_init_segment(CircularBuffer const& buffer) const
{
    return get_segment_type(buffer) == ISOBMFF_FOUR_CC('f', 't', 'y', 'p');
}

Optional<size_t> ISOBMFFSegParser::init_segment_size(CircularBuffer const& buffer) const
{
    size_t current_offset = 0;
    auto const ftyp_box = read_box<ISOBMFF::FileTypeBox>(buffer, 0);
    if (!ftyp_box.has_value()) {
        dbgln("ISOBMFFSegParser::init_segment_size: no ftyp box");
        return {};
    }

    current_offset += ftyp_box->m_size;
    dbgln("current_offset, buffer.used_space(): {}, {}", current_offset, buffer.used_space());
    while (current_offset < buffer.used_space()) {
        dbgln("ISOBMFFSegParser::init_segment_size: current_offset: {}", current_offset);
        auto const next_box = read_box<ISOBMFF::Box>(buffer, current_offset);
        if (!next_box.has_value()) {
            dbgln("ISOBMFFSegParser::init_segment_size: no next box");
            // We never reached the moov box, so we're trying to get the size of an invalid init segment. Abort.
            return {};
        }

        if (next_box->m_type == ISOBMFF_FOUR_CC('m', 'o', 'o', 'v')) {
            // We found the moov box, we can stop here.
            return current_offset + next_box->m_size;
        }

        // Just in case we start entering garbage data, we check if each box is a valid type.
        // Otherwise, we may enter an infinite loop.
        switch (next_box->m_type) {
        case ISOBMFF_FOUR_CC('p', 'd', 'i', 'n'):
        case ISOBMFF_FOUR_CC('f', 'r', 'e', 'e'):
        case ISOBMFF_FOUR_CC('s', 'i', 'd', 'x'):
            // Ignore these boxes, they are valid, not needed.
            break;
        default:
            dbgln("ISOBMFFSegParser::init_segment_size: invalid box type: {}", next_box->m_type);
            return {};
        }

        current_offset += next_box->m_size;
    }

    dbgln("ISOBMFFSegParser::init_segment_size: no moov box");
    return {};
}

bool ISOBMFFSegParser::starts_with_media_segment(CircularBuffer const& buffer) const
{
    auto const type = get_segment_type(buffer);
    // First box is either styp or moof.
    return type == ISOBMFF_FOUR_CC('s', 't', 'y', 'p') || type == ISOBMFF_FOUR_CC('m', 'o', 'o', 'f');
}

bool ISOBMFFSegParser::contains_full_init_segment(CircularBuffer const& buffer) const
{
    AK::set_debug_enabled(true);
    auto const size_bytes = init_segment_size(buffer);
    if (!size_bytes.has_value()) {
        dbgln("ISOBMFFSegParser::contains_full_init_segment: no size bytes");
        return false;
    }

    dbgln("ISOBMFFSegParser::contains_full_init_segment: yes, size bytes: {}, buffer used space: {}", size_bytes.value(), buffer.used_space());
    return size_bytes.value() <= buffer.used_space();
}

static void parse_ftyp(CircularBuffer const& buffer, size_t& current_offset, InitializationSegment& init_segment)
{
    auto const ftyp_box = read_box<ISOBMFF::FileTypeBox>(buffer, 0);
    ASSERT(ftyp_box.has_value());
    current_offset = sizeof(ISOBMFF::FileTypeBox);
    init_segment.m_raw_data.append(&ftyp_box.value(), sizeof(ISOBMFF::FileTypeBox));

    // The static part of the ftyp box has been read, now we need to read the compatible brands.
    auto const n_compatible_brands = (ftyp_box->m_size - sizeof(ISOBMFF::FileTypeBox)) / sizeof(u32);
    // FIXME: Discard the compatible brands from the buffer for now. Do we need them anyway? Investigate.
    auto const compatible_brands_buffer = buffer.peek(current_offset, n_compatible_brands * sizeof(u32));
    init_segment.m_raw_data.append(compatible_brands_buffer);
    current_offset += n_compatible_brands * sizeof(u32);
}

static Optional<ISOBMFF::Box> skip_to_moov_box(CircularBuffer const& buffer, size_t& current_offset, InitializationSegment& init_segment)
{
    // The spec says the following box is either pdin, free, sidx, or moov. All but moov are to be ignored and discarded.
    // The spec seems to imply these to-be-ignored boxes can appear multiple times:
    //      "Valid top-level boxes such as pdin, free, and sidx are allowed to appear before the moov box."
    while (current_offset < buffer.used_space()) {
        auto const next_box = read_box<ISOBMFF::Box>(buffer, current_offset);
        if (!next_box.has_value()) {
            dbgln("ISOBMFFSegParser::parse_init_segment: no next box");
            return {};
        }

        if (next_box->m_type == ISOBMFF_FOUR_CC('m', 'o', 'o', 'v')) {
            // We found the moov box, we can stop here.
            // Spec says the to-be-ignored boxes may appear *before* the moov box, it says nothing about after.
            init_segment.m_raw_data.append(buffer.peek(current_offset, next_box->m_size));
            current_offset += sizeof(ISOBMFF::Box);
            return next_box;
        }
        current_offset += next_box->m_size;

        switch (next_box->m_type) {
        case ISOBMFF_FOUR_CC('p', 'd', 'i', 'n'):
        case ISOBMFF_FOUR_CC('f', 'r', 'e', 'e'):
        case ISOBMFF_FOUR_CC('s', 'i', 'd', 'x'):
            // Ignore these boxes, they are valid, not needed.
            continue;
        default:
            // Invalid box type, we can't parse this segment.
            dbgln("ISOBMFFSegParser::parse_init_segment: invalid box type: {}", next_box->m_type);
            dbgln("next_box->m_type: {}{}{}{}", char((next_box->m_type >> 24) & 0xFF), char((next_box->m_type >> 16) & 0xFF), char((next_box->m_type >> 8) & 0xFF), char(next_box->m_type & 0xFF));
            return {};
        }
    }

    dbgln("Didn't even start the skip moov box");
    return {};
}

static bool parse_mvhd(CircularBuffer const& buffer, size_t& current_offset, InitializationSegment& init_segment)
{
    auto const mvhd_box = read_box<ISOBMFF::FullBox>(buffer, current_offset);
    if (!mvhd_box.has_value() || (mvhd_box->m_version != 0 && mvhd_box->m_version != 1)) {
        dbgln("ISOBMFFSegParser::parse_mvhd: no mvhd box or invalid version");
        dbgln("mvhd_box->m_type: {}{}{}{}", char((mvhd_box->m_type >> 24) & 0xFF), char((mvhd_box->m_type >> 16) & 0xFF), char((mvhd_box->m_type >> 8) & 0xFF), char(mvhd_box->m_type & 0xFF));
        dbgln("mvhd_box->version: {}", mvhd_box.has_value() ? mvhd_box->m_version : 0);
        return false;
    }

    current_offset += sizeof(ISOBMFF::FullBox);

    u32 timescale = 0;
    u64 duration = 0;
    if (mvhd_box->m_version == 0) {
        // skip creation_time and modification_time
        current_offset += sizeof(u32) * 2;
        if (auto opt_timescale = read_value<u32>(buffer, current_offset); opt_timescale.has_value())
            timescale = opt_timescale.value();
        else {
            dbgln("ISOBMFFSegParser::parse_mvhd: v0 no timescale");
            return false;
        }

        current_offset += sizeof(u32);
        if (auto opt_duration = read_value<u32>(buffer, current_offset); opt_duration.has_value())
            duration = opt_duration.value();
        else {
            dbgln("ISOBMFFSegParser::parse_mvhd: v0 no duration");
            return false;
        }

        current_offset += sizeof(u32);
    } else { // version == 1
        // skip creation_time and modification_time
        current_offset += sizeof(u64) * 2;
        if (auto opt_timescale = read_value<u32>(buffer, current_offset); opt_timescale.has_value())
            timescale = opt_timescale.value();
        else {
            dbgln("ISOBMFFSegParser::parse_mvhd: v1 no timescale");
            return false;
        }

        current_offset += sizeof(u32);
        if (auto opt_duration = read_value<u64>(buffer, current_offset); opt_duration.has_value())
            duration = opt_duration.value();
        else {
            dbgln("ISOBMFFSegParser::parse_mvhd: v1 no duration");
            return false;
        }

        current_offset += sizeof(u64);
    }

    AK::set_debug_enabled(true);
    dbgln("timescale: {}, duration: {}, duration in seconds: {}", timescale, duration, static_cast<double>(duration) / timescale);

    auto const duration_ms = static_cast<double>(duration) / timescale * 1000;
    init_segment.m_duration = AK::Duration::from_seconds(static_cast<i64>(duration_ms));
    // FIXME: skip rate for now, check later if it's necessary
    current_offset += sizeof(i32);
    // FIXME: skip volume for now, check later if it's necessary
    current_offset += sizeof(i16);
    // skip i16 + 2 * u32 reserved
    current_offset += sizeof(i16) + sizeof(u32) * 2;
    // skip matrix
    current_offset += sizeof(i32) * 9;
    // skip predefined
    current_offset += sizeof(i32) * 6;
    // FIXME: skip next track ID for now until we figure out what it's for
    current_offset += sizeof(u32);

    return true;
}

[[nodiscard]]
static bool parse_tkhd(CircularBuffer const& buffer, size_t& current_offset, InitializationSegment&)
{
    // Skip the trak box header.
    current_offset += sizeof(ISOBMFF::Box);

    auto const tkhd_box = read_box<ISOBMFF::FullBox>(buffer, current_offset);
    if (!tkhd_box.has_value() || (tkhd_box->m_version != 0 && tkhd_box->m_version != 1)) {
        dbgln("ISOBMFFSegParser::parse_tkhd: no tkhd box or invalid version");
        dbgln("tkhd_box->m_type: {}{}{}{}", char((tkhd_box->m_type >> 24) & 0xFF), char((tkhd_box->m_type >> 16) & 0xFF), char((tkhd_box->m_type >> 8) & 0xFF), char(tkhd_box->m_type & 0xFF));
        return false;
    }

    current_offset += sizeof(ISOBMFF::FullBox);

    u32 const flags = tkhd_box->m_flags[0] << 16 | tkhd_box->m_flags[1] << 8 | tkhd_box->m_flags[2];
    constexpr u32 isobmff_trak_flag_enabled = 0x000001;
    if ((flags & isobmff_trak_flag_enabled) == 0) {
        // We return true because it's not an error, but we don't want to parse this track.
        return true;
    }

    u32 track_id = 0;
    u64 duration = 0;
    if (tkhd_box->m_version == 0) {
        dbgln("ISOBMFFSegParser::parse_tkhd: v0");
        // skip creation_time and modification_time
        current_offset += sizeof(u32) * 2;
        if (auto opt_track_id = read_value<u32>(buffer, current_offset); opt_track_id.has_value())
            track_id = opt_track_id.value();
        else {
            dbgln("ISOBMFFSegParser::parse_tkhd: v0 no track_id");
            return false;
        }
        current_offset += sizeof(u32); // track_id
        // skip reserved
        current_offset += sizeof(u32);
        if (auto opt_duration = read_value<u32>(buffer, current_offset); opt_duration.has_value())
            duration = opt_duration.value();
        else {
            dbgln("ISOBMFFSegParser::parse_tkhd: v0 no duration");
            return false;
        }
        current_offset += sizeof(u32);
    } else {
        dbgln("ISOBMFFSegParser::parse_tkhd: v1");
        // skip creation_time and modification_time
        current_offset += sizeof(u64) * 2;
        if (auto opt_track_id = read_value<u32>(buffer, current_offset); opt_track_id.has_value())
            track_id = opt_track_id.value();
        else {
            dbgln("ISOBMFFSegParser::parse_tkhd: v1 no track_id");
            return false;
        }
        current_offset += sizeof(u32); // track_id
        // skip reserved
        current_offset += sizeof(u32);
        if (auto opt_duration = read_value<u64>(buffer, current_offset); opt_duration.has_value())
            duration = opt_duration.value();
        else {
            dbgln("ISOBMFFSegParser::parse_tkhd: v1 no duration");
            return false;
        }
        current_offset += sizeof(u64);
    }

    // skip reserved
    current_offset += sizeof(u32) * 2;
    // skip layer and alternate_group
    current_offset += sizeof(u16) * 2;

    // Q8.8
    // auto const volume = read_value<u16>(buffer, current_offset);
    dbgln("volume: {}", read_value<u16>(buffer, current_offset).value());
    current_offset += sizeof(u16);
    // skip reserved
    current_offset += sizeof(u16);
    // skip matrix
    current_offset += sizeof(i32) * 9;
    auto const width = read_value<u32>(buffer, current_offset);
    if (!width.has_value()) {
        dbgln("ISOBMFFSegParser::parse_tkhd: no width");
        return false;
    }
    current_offset += sizeof(u32);
    auto const height = read_value<u32>(buffer, current_offset);
    if (!height.has_value()) {
        dbgln("ISOBMFFSegParser::parse_tkhd: no height");
        return false;
    }
    current_offset += sizeof(u32);
    dbgln("ISOBMFFSegParser::parse_tkhd: track_id: {}, duration: {}, width: {}, height: {}", track_id, duration, width.value(), height.value());
    return true;
}

static void parse_tref(CircularBuffer const& buffer, size_t& current_offset)
{
    auto const potential_tref_box = read_box<ISOBMFF::Box>(buffer, current_offset);
    if (potential_tref_box.has_value())
        dbgln("potential_tref_box->m_type: {}{}{}{}", char((potential_tref_box->m_type >> 24) & 0xFF), char((potential_tref_box->m_type >> 16) & 0xFF), char((potential_tref_box->m_type >> 8) & 0xFF), char(potential_tref_box->m_type & 0xFF));
    if (potential_tref_box.has_value() && potential_tref_box->m_type == ISOBMFF_FOUR_CC('t', 'r', 'e', 'f')) {
        dbgln("FIXME: parse_tref");
        current_offset += potential_tref_box->m_size;
    }
}

[[nodiscard]]
static bool parse_trak(CircularBuffer const& buffer, size_t& current_offset, InitializationSegment& segment)
{
    if (!parse_tkhd(buffer, current_offset, segment))
        return false;

    parse_tref(buffer, current_offset);

    auto next_box = read_box<ISOBMFF::Box>(buffer, current_offset);
    if (!next_box.has_value()) {
        dbgln("ISOBMFFSegParser::parse_trak: no next box");
        return false;
    }

    dbgln("next_box->m_type(1): {} {}{}{}{}", next_box->m_type, char((next_box->m_type >> 24) & 0xFF), char((next_box->m_type >> 16) & 0xFF), char((next_box->m_type >> 8) & 0xFF), char(next_box->m_type & 0xFF));
    if (next_box->m_type == ISOBMFF_FOUR_CC('e', 'd', 't', 's')) {
        dbgln("ISOBMFFSegParser::parse_trak: FIXME: found edts box");
        current_offset += next_box->m_size;
    }

    next_box = read_box<ISOBMFF::Box>(buffer, current_offset);
    if (!next_box.has_value()) {
        dbgln("ISOBMFFSegParser::parse_trak: no next box 2");
        return false;
    }

    dbgln("next_box->m_type: {}{}{}{}", char((next_box->m_type >> 24) & 0xFF), char((next_box->m_type >> 16) & 0xFF), char((next_box->m_type >> 8) & 0xFF), char(next_box->m_type & 0xFF));
    if (next_box->m_type == ISOBMFF_FOUR_CC('m', 'd', 'i', 'a')) {
        dbgln("ISOBMFFSegParser::parse_trak: FIXME: found mdia box");
        current_offset += next_box->m_size;
        return true;
    }

    return true;
}

[[nodiscard]]
static bool parse_moov(CircularBuffer const& buffer, size_t current_offset, InitializationSegment& segment)
{
    auto const moov_box_start_offset = current_offset;
    auto const moov_box = skip_to_moov_box(buffer, current_offset, segment);
    if (!moov_box.has_value()) {
        dbgln("Failed to skip to moov box");
        return false;
    }

    auto const mvhd_box_parse_result = parse_mvhd(buffer, current_offset, segment);
    if (!mvhd_box_parse_result) {
        dbgln("Failed to parse mvhd");
        return false;
    }

    dbgln("moov_box->m_size {}", moov_box->m_size);
    bool has_mvex = false;
    while (current_offset < moov_box_start_offset + moov_box->m_size) {
        dbgln("current_offset: {}, max: {}", current_offset, moov_box_start_offset + moov_box->m_size);
        auto const next_box = read_box<ISOBMFF::Box>(buffer, current_offset);
        if (!next_box.has_value()) {
            dbgln("ISOBMFFSegParser::parse_moov: no next box");
            return false;
        }

        switch (next_box->m_type) {
        case ISOBMFF_FOUR_CC('m', 'v', 'e', 'x'):
            dbgln("ISOBMFFSegParser::parse_moov: mvex encountered");
            has_mvex = true;
            current_offset += next_box->m_size;
            continue;
        case ISOBMFF_FOUR_CC('i', 'p', 'm', 'c'):
            dbgln("FIXME: ISOBMFFSegParser::parse_moov: found ipmc box, what to do?");
            return false;
        case ISOBMFF_FOUR_CC('t', 'r', 'a', 'k'):
            if (!parse_trak(buffer, current_offset, segment)) {
                dbgln("ISOBMFFSegParser::parse_moov: trak parse failed");
                return false;
            }
            break;
        case ISOBMFF_FOUR_CC('u', 'd', 't', 'a'):
            current_offset += next_box->m_size;
            continue;
        default:
            dbgln("ISOBMFFSegParser::parse_moov: invalid box type: {}, {}{}{}{}", next_box->m_type, char((next_box->m_type >> 24) & 0xFF), char((next_box->m_type >> 16) & 0xFF), char((next_box->m_type >> 8) & 0xFF), char(next_box->m_type & 0xFF));
            dbgln("next_box->m_type: {}{}{}{}", char((next_box->m_type >> 24) & 0xFF), char((next_box->m_type >> 16) & 0xFF), char((next_box->m_type >> 8) & 0xFF), char(next_box->m_type & 0xFF));
            return false;
        }
    }

    if (!has_mvex) {
        dbgln("ISOBMFFSegParser::parse_moov: moov is missing mvex");
        return false;
    }

    return true;
}

Optional<InitializationSegment> ISOBMFFSegParser::parse_init_segment(CircularBuffer const& buffer) const
{
    InitializationSegment segment;
    // This function should have never been called if the following is not true:
    ASSERT(starts_with_init_segment(buffer));
    size_t current_offset = 0;
    parse_ftyp(buffer, current_offset, segment);
    if (!parse_moov(buffer, current_offset, segment)) {
        dbgln("ISOBMFFSegParser::parse_init_segment: no moov box");
        return {};
    }

    return segment;
}
}
