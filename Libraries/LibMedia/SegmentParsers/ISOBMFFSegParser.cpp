/*
 * Copyright (c) 2025, Tuur Martens <tuurmartens4@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "AK/Tuple.h"

#include <AK/String.h>
#include <LibMedia/SegmentParsers/ISOBMFFSegParser.h>

namespace Media::SegmentParsers {
template<typename T>
static Optional<T> read_value(CircularBuffer& buffer, InitializationSegment::OutRawData& bin_out)
{
    if (sizeof(T) > buffer.used_space())
        return {};

    u8 read_buffer[sizeof(T)];
    auto const data = buffer.read({ read_buffer, sizeof(T) });
    bin_out.append(data);
    ASSERT(data.size() != sizeof(T));

    T const value_be = *reinterpret_cast<T const*>(data.data());
    if constexpr (IsOneOf<T, u8, u16, u32, u64>) {
        return AK::convert_between_host_and_big_endian(value_be);
    } else {
        return value_be;
    }
}

template<typename T>
static Optional<T> peek_value(CircularBuffer const& buffer, size_t offset)
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

template<typename B>
requires AK::Concepts::DerivedFrom<B, ISOBMFF::Box>
[[nodiscard]]
static Optional<B> read_box(SegmentParser::Input buffer, InitializationSegment::OutRawData& bin_out)
{
    auto const opt_box = read_value<B>(buffer, bin_out);
    if (!opt_box.has_value()) {
        return {};
    }

    auto box = opt_box.value();
    box.m_type = AK::convert_between_host_and_big_endian(box.m_type);
    box.m_size = AK::convert_between_host_and_big_endian(box.m_size);
    VERIFY(box.m_size >= sizeof(B));

    return box;
}

template<typename B = ISOBMFF::Box>
requires AK::Concepts::DerivedFrom<B, ISOBMFF::Box>
[[nodiscard]]
static Optional<B> peek_box(SegmentParser::Input buffer, size_t offset = 0)
{
    auto const opt_box = peek_value<B>(buffer, offset);
    if (!opt_box.has_value()) {
        return {};
    }

    auto box = opt_box.value();
    box.m_type = AK::convert_between_host_and_big_endian(box.m_type);
    box.m_size = AK::convert_between_host_and_big_endian(box.m_size);
    if (box.m_size == 0) {
        return {};
    }

    return box;
}

bool ISOBMFFSegParser::starts_with_init_segment(Input input) const
{
    auto const box = peek_box(input);
    if (!box.has_value())
        return false;

    return box->m_type == ISOBMFF_FOUR_CC('f', 't', 'y', 'p');
}

// We want to skip `size` bytes for our purposes, but we'll still need them for passing onto the demuxer.
static ErrorOr<void> skip(SegmentParser::Input input, size_t size, InitializationSegment::OutRawData& bin_out)
{
    ByteBuffer buffer = TRY(ByteBuffer::create_uninitialized(size));
    auto const data = input.read(buffer);
    bin_out.append(data);
    return {};
}

#pragma region Init Segment Parsing
[[nodiscard]]
static ErrorOr<size_t> scan_for_moov(SegmentParser::Input input, size_t offset = 0)
{
    // The spec says the box following ftyp is either pdin, free, sidx, or moov. All but moov are to be ignored and discarded.
    // The spec seems to imply these to-be-ignored boxes can appear multiple times:
    //      "Valid top-level boxes such as pdin, free, and sidx are allowed to appear before the moov box."
    size_t current_offset = offset;
    while (input.used_space() - current_offset > 0) {
        auto const next_box = peek_box<ISOBMFF::Box>(input, current_offset);
        if (!next_box.has_value()) {
            return Error::from_string_literal("No next box in search for moov box");
        }
        if (next_box->m_type == ISOBMFF_FOUR_CC('m', 'o', 'o', 'v')) {
            // The moov box contains no useful info on its own
            return current_offset;
        }
        current_offset += next_box->m_size;

        switch (next_box->m_type) {
        case ISOBMFF_FOUR_CC('p', 'd', 'i', 'n'):
        case ISOBMFF_FOUR_CC('f', 'r', 'e', 'e'):
        case ISOBMFF_FOUR_CC('s', 'i', 'd', 'x'): {
            // Ignore these boxes, they are valid, not needed.
            continue;
        }
        default:
            // Invalid box type, we can't parse this segment.
            return Error::from_string_literal("Invalid box type in search for moov box");
        }
    }

    return Error::from_string_literal("not enough data to find moov box");
}

bool ISOBMFFSegParser::contains_full_init_segment(Input input) const
{
    auto const ftyp_box = peek_box(input);
    if (!ftyp_box.has_value())
        return false;
    if (ftyp_box->m_type != ISOBMFF_FOUR_CC('f', 't', 'y', 'p')) {
        return false;
    }
    auto const moov_offset = scan_for_moov(input, ftyp_box->m_size);
    if (moov_offset.is_error())
        return false;

    auto const moov_box = peek_box(input, moov_offset.value());
    ASSERT(moov_box.has_value());

    return moov_box->m_size <= input.used_space();
}

bool ISOBMFFSegParser::starts_with_media_segment(Input) const
{
    TODO();
}

static void parse_ftyp(SegmentParser::Input input, InitializationSegment& init_segment)
{
    auto const ftyp_box = read_box<ISOBMFF::FileTypeBox>(input, init_segment.m_raw_data);
    ASSERT(!ftyp_box.has_value());
    // The static part of the ftyp box has been read, now we need to read the compatible brands.
    auto const n_compatible_brands = (ftyp_box->m_size - sizeof(ISOBMFF::FileTypeBox)) / sizeof(u32);
    // FIXME: Discard the compatible brands from the buffer for now. Do we need them anyway? Investigate.
    VERIFY(!skip(input, n_compatible_brands * sizeof(u32), init_segment.m_raw_data).is_error());
    init_segment.m_raw_data.append(input.peek(0, n_compatible_brands * sizeof(u32)));
}

[[nodiscard]]
static ErrorOr<void> skip_to_moov(SegmentParser::Input input, InitializationSegment& init_segment)
{
    auto const moov_offset = TRY(scan_for_moov(input));

    TRY(skip(input, moov_offset, init_segment.m_raw_data));
    return {};
}

// Read data that follows the following:
// T creation_time;
// T modification_time;
// u32 timescale;
// T duration;
template<typename T>
requires IsOneOf<T, u32, u64>
[[nodiscard]]
static ErrorOr<Tuple<u32, u64>> read_versioned_time_data(SegmentParser::Input input, InitializationSegment& init_segment)
{
    // skip creation_time and modification_time
    TRY(skip(input, sizeof(T) * 2, init_segment.m_raw_data));

    u32 timescale;
    u64 duration;
    if (auto opt_timescale = read_value<u32>(input, init_segment.m_raw_data); opt_timescale.has_value()) {
        timescale = opt_timescale.value();
    } else {
        return Error::from_string_literal("box has no timescale");
    }

    if (auto opt_duration = read_value<T>(input, init_segment.m_raw_data); opt_duration.has_value()) {
        duration = opt_duration.value();
    } else {
        return Error::from_string_literal("box has no duration");
    }

    return Tuple { timescale, duration };
}

[[nodiscard]]
static ErrorOr<u32> parse_mvhd(SegmentParser::Input input, InitializationSegment& init_segment)
{
    auto const mvhd_box = read_box<ISOBMFF::FullBox>(input, init_segment.m_raw_data);
    if (!mvhd_box.has_value()) {
        return Error::from_string_literal("moov box has no mvhd box");
    }
    if (mvhd_box->m_version != 0 && mvhd_box->m_version != 1) {
        return Error::from_string_literal("mvhd box version is invalid");
    }

    u32 timescale;
    u64 duration;
    switch (mvhd_box->m_version) {
    case 0: {
        auto const result = TRY(read_versioned_time_data<u32>(input, init_segment));
        timescale = result.get<u32>();
        duration = result.get<u64>();
        break;
    }
    case 1: {
        auto const result = TRY(read_versioned_time_data<u64>(input, init_segment));
        timescale = result.get<u32>();
        duration = result.get<u64>();
        break;
    }
    default:
        ASSERT_NOT_REACHED();
    }

    auto const duration_ms = static_cast<double>(duration) / timescale * 1000;
    init_segment.m_duration = AK::Duration::from_seconds(static_cast<i64>(duration_ms));

    auto const rate = read_value<i32>(input, init_segment.m_raw_data);
    VERIFY(rate.has_value());
    auto const volume = read_value<i16>(input, init_segment.m_raw_data);
    VERIFY(volume.has_value());
    TRY(skip(input,
        /* reserved */ sizeof(u16) + sizeof(u32) * 2
            + /*  int(32)[9] matrix */ sizeof(i32) * 9
            + /* bit(32)[6] predefined*/ sizeof(i32) * 6
            + /* fixed(32) pre_defined */ sizeof(u32),
        init_segment.m_raw_data));

    return timescale;
}

template<typename T>
requires IsOneOf<T, u32, u64>
[[nodiscard]]
static ErrorOr<Tuple<u32, u64>> read_tkhd_versioned_data(SegmentParser::Input input, InitializationSegment& init_segment)
{
    // skip creation_time and modification_time
    TRY(skip(input, sizeof(T) * 2, init_segment.m_raw_data));

    u32 timescale;
    u64 duration;
    if (auto opt_timescale = read_value<u32>(input, init_segment.m_raw_data); opt_timescale.has_value()) {
        timescale = opt_timescale.value();
    } else {
        return Error::from_string_literal("tkhd box has no timescale");
    }

    // skip reserved
    TRY(skip(input, sizeof(u32), init_segment.m_raw_data));

    if (auto opt_duration = read_value<T>(input, init_segment.m_raw_data); opt_duration.has_value()) {
        duration = opt_duration.value();
    } else {
        return Error::from_string_literal("tkhd box has no duration");
    }

    return Tuple { timescale, duration };
}

namespace MDIA {
namespace MINF {
[[nodiscard]]
static ErrorOr<void> parse_stbl(SegmentParser::Input input, InitializationSegment& init_segment)
{
    auto const current_n_bytes_left = input.used_space();
    auto const stbl_box = read_box<ISOBMFF::Box>(input, init_segment.m_raw_data);
    // we wouldn't have gotten here if the stbl box was not present
    ASSERT(stbl_box.has_value());

    while (current_n_bytes_left - input.used_space() < stbl_box->m_size) {
        auto const next_box = read_box<ISOBMFF::Box>(input, init_segment.m_raw_data);
        if (!next_box.has_value()) {
            return Error::from_string_literal("stbl box has no next box");
        }

        switch (next_box->m_type) {
        case ISOBMFF_FOUR_CC('c', 'o', '6', '4'):
        case ISOBMFF_FOUR_CC('s', 't', 's', 's'):
        case ISOBMFF_FOUR_CC('s', 't', 's', 'h'):
        case ISOBMFF_FOUR_CC('s', 't', 's', 'z'):
        case ISOBMFF_FOUR_CC('p', 'a', 'd', 'b'):
        case ISOBMFF_FOUR_CC('s', 't', 't', 's'): {
            auto const entry_count = read_value<u32>(input, init_segment.m_raw_data);
            if (!entry_count.has_value() || entry_count.value() != 0) {
                return Error::from_string_literal("stbl box may not have any entries");
            }
            TRY(skip(input, next_box->m_size - sizeof(ISOBMFF::Box) - sizeof(entry_count.value()), init_segment.m_raw_data));
            break;
        }
        case ISOBMFF_FOUR_CC('c', 't', 't', 's'):
        case ISOBMFF_FOUR_CC('s', 't', 'c', 'o'):
        case ISOBMFF_FOUR_CC('s', 't', 's', 'c'):
        case ISOBMFF_FOUR_CC('s', 'b', 'g', 'p'):
        case ISOBMFF_FOUR_CC('s', 'g', 'p', 'd'):
        case ISOBMFF_FOUR_CC('s', 'u', 'b', 's'):
        case ISOBMFF_FOUR_CC('s', 't', 'd', 'p'):
        case ISOBMFF_FOUR_CC('s', 'd', 't', 'p'):
        case ISOBMFF_FOUR_CC('s', 't', 'z', '2'):
        case ISOBMFF_FOUR_CC('s', 't', 's', 'd'):
            TRY(skip(input, next_box->m_size - sizeof(ISOBMFF::Box), init_segment.m_raw_data));
            break;
        default:
            return Error::from_string_literal("stbl box has invalid box type");
        }
    }

    return {};
}

[[nodiscard]]
static ErrorOr<void> parse_minf(SegmentParser::Input input, InitializationSegment& init_segment)
{
    auto const current_n_bytes_left = input.used_space();
    auto const minf_box = read_box<ISOBMFF::Box>(input, init_segment.m_raw_data);
    // we wouldn't have gotten here if the minf box was not present
    ASSERT(minf_box.has_value());

    auto has_seen_header_box = false;
    auto has_seen_dinf = false;
    auto has_seen_stbl = false;
    while (current_n_bytes_left - input.used_space() < minf_box->m_size) {
        auto const next_box = peek_box<ISOBMFF::Box>(input);

        switch (next_box->m_type) {
        case ISOBMFF_FOUR_CC('v', 'm', 'h', 'd'): // Video Media Header Box
        case ISOBMFF_FOUR_CC('s', 'm', 'h', 'd'): // Sound Media Header Box
        case ISOBMFF_FOUR_CC('h', 'm', 'h', 'd'): // Hint Media Header Box
        case ISOBMFF_FOUR_CC('n', 'm', 'h', 'd'): // Null Media Header Box
            has_seen_header_box = true;
            TRY(skip(input, next_box->m_size, init_segment.m_raw_data));
            break;
        case ISOBMFF_FOUR_CC('d', 'i', 'n', 'f'): // Data Information Box
            has_seen_dinf = true;
            TRY(skip(input, next_box->m_size, init_segment.m_raw_data));
            break;
        case ISOBMFF_FOUR_CC('s', 't', 'b', 'l'): // Sample Table Box
            has_seen_stbl = true;
            TRY(parse_stbl(input, init_segment));
            break;
        default:
            return Error::from_string_literal("minf box has invalid box type");
        }
    }

    if (has_seen_header_box && has_seen_dinf && has_seen_stbl) {
        return {};
    }

    return Error::from_string_literal("minf box is missing some boxes");
}
}

[[nodiscard]]
static ErrorOr<ISOBMFF::TrakHandlerType> parse_hdlr(SegmentParser::Input input, InitializationSegment& init_segment)
{
    TRY(skip(input, sizeof(ISOBMFF::FullBox) + /* predefined: */ sizeof(u32), init_segment.m_raw_data));
    auto const handler_type_opt = read_value<u32>(input, init_segment.m_raw_data);
    if (!handler_type_opt.has_value()) {
        return Error::from_string_literal("hdlr box has no handler type");
    }
    auto const handler_type = TRY([=] -> ErrorOr<ISOBMFF::TrakHandlerType> {
        switch (handler_type_opt.value()) {
        case ISOBMFF_FOUR_CC('v', 'i', 'd', 'e'):
            return ISOBMFF::TrakHandlerType::Video;
        case ISOBMFF_FOUR_CC('s', 'o', 'u', 'n'):
            return ISOBMFF::TrakHandlerType::Audio;
        case ISOBMFF_FOUR_CC('h', 'i', 'n', 't'):
            return ISOBMFF::TrakHandlerType::Hint;
        default:
            return Error::from_string_literal("hdlr box has invalid handler type");
        }
    }());
    // skip reserved
    TRY(skip(input, sizeof(u32) * 3, init_segment.m_raw_data));
    while (true) {
        if (auto const ch = read_value<u8>(input, init_segment.m_raw_data); !ch.has_value() || ch.value() == '\0') {
            break;
        }
    }

    return handler_type;
}

[[nodiscard]]
static ErrorOr<void> parse_mdhd(SegmentParser::Input input, InitializationSegment& init_segment)
{
    auto const mdhd_box = read_box<ISOBMFF::FullBox>(input, init_segment.m_raw_data);
    ASSERT(mdhd_box.has_value());

    if (mdhd_box->m_version != 0 && mdhd_box->m_version != 1) {
        return Error::from_string_literal("mdhd box version is invalid");
    }

    u32 timescale;
    u64 duration;
    switch (mdhd_box->m_version) {
    case 0: {
        auto const result = TRY(read_versioned_time_data<u32>(input, init_segment));
        timescale = result.get<u32>();
        duration = result.get<u64>();
        break;
    }
    case 1: {
        auto const result = TRY(read_versioned_time_data<u64>(input, init_segment));
        timescale = result.get<u32>();
        duration = result.get<u64>();
        break;
    }
    default:
        ASSERT_NOT_REACHED();
    }
    /*
     * skip:
     * bit(1) pad = 0;
     * unsigned int(5)[3] language; // ISO-639-2/T language code
     */
    // skip predefined
    TRY(skip(input, sizeof(u16) * 2, init_segment.m_raw_data));
    init_segment.m_duration = AK::Duration::from_milliseconds(static_cast<i64>(duration) / timescale);

    return {};
}

[[nodiscard]]
static ErrorOr<ISOBMFF::TrakHandlerType> parse_mdia(SegmentParser::Input input, InitializationSegment& init_segment)
{
    auto const current_n_bytes_left = input.used_space();
    auto const mdia_box = read_box<ISOBMFF::Box>(input, init_segment.m_raw_data);
    // we wouldn't have gotten here if the mdia box was not present
    ASSERT(mdia_box.has_value());
    TRY(parse_mdhd(input, init_segment));

    bool has_seen_hdlr = false;
    bool has_seen_minf = false;
    Optional<ISOBMFF::TrakHandlerType> handler_type;
    while (current_n_bytes_left - input.used_space() < mdia_box->m_size) {
        auto const next_box = peek_box<ISOBMFF::Box>(input);
        if (!next_box.has_value()) {
            break;
        }

        switch (next_box->m_type) {
        case ISOBMFF_FOUR_CC('h', 'd', 'l', 'r'):
            if (has_seen_hdlr) {
                return Error::from_string_literal("mdia box can only contain one hdlr box");
            }
            handler_type = TRY(parse_hdlr(input, init_segment));
            has_seen_hdlr = true;
            break;
        case ISOBMFF_FOUR_CC('m', 'i', 'n', 'f'):
            if (has_seen_minf) {
                return Error::from_string_literal("mdia box can only contain one minf box");
            }
            if (!handler_type.has_value()) {
                return Error::from_string_literal("mdia box has reached minf before hdlr");
            }
            TRY(MINF::parse_minf(input, init_segment));
            has_seen_minf = true;
            break;
        default:
            return Error::from_string_literal("mdia box contains child with invalid box type");
        }
    }

    if (!handler_type.has_value()) {
        return Error::from_string_literal("mdia box has no handler");
    }

    return handler_type.value();
}
}

[[nodiscard]]
static ErrorOr<void> parse_tkhd(SegmentParser::Input input, InitializationSegment& init_segment, Optional<u32> timescale, InitSegTrackBase& out_track)
{
    auto const tkhd_box = read_box<ISOBMFF::FullBox>(input, init_segment.m_raw_data);
    // we wouldn't have gotten here if the tkhd box was not present
    ASSERT(tkhd_box.has_value());

    if (tkhd_box->m_version != 0 && tkhd_box->m_version != 1) {
        return Error::from_string_literal("tkhd box version is invalid");
    }

    u32 track_id;
    u64 duration;
    switch (tkhd_box->m_version) {
    case 0: {
        auto const result = TRY(read_tkhd_versioned_data<u32>(input, init_segment));
        track_id = result.get<u32>();
        duration = result.get<u64>();
        break;
    }
    case 1: {
        auto const result = TRY(read_tkhd_versioned_data<u64>(input, init_segment));
        track_id = result.get<u32>();
        duration = result.get<u64>();
        break;
    }
    default:
        ASSERT_NOT_REACHED();
    }
    out_track.m_track_id = track_id;
    if (timescale.has_value()) {
        out_track.m_duration = AK::Duration::from_milliseconds(static_cast<i64>(duration) / timescale.value());
    } else {
        // "If the duration of this track cannot be determined then duration is set to all 1s(32 - bit maxint)"
        out_track.m_duration = AK::Duration::from_milliseconds(0xFFFF'FFFF);
    }

    // skip reserved
    TRY(skip(input, sizeof(u32) * 2, init_segment.m_raw_data));
    // skip layer and alternate_group
    TRY(skip(input, sizeof(u16) * 2, init_segment.m_raw_data));
    // auto const volume = read_value<i16>(input, init_segment.m_raw_data);
    // FIXME: replace following skip with read above, and populate out_track
    TRY(skip(input, sizeof(i16), init_segment.m_raw_data));

    size_t nb_skips = 0;
    // skip reserved
    nb_skips += sizeof(u16);
    // skip matrix
    nb_skips += sizeof(u32) * 9;
    // skip width and height scales
    nb_skips += sizeof(u32) * 2;
    TRY(skip(input, nb_skips, init_segment.m_raw_data));

    return {};
}

[[nodiscard]]
static ErrorOr<void> parse_trak(SegmentParser::Input input, Optional<u32> timescale, InitializationSegment& init_segment)
{
    auto const current_n_bytes_left = input.used_space();
    auto const trak_box = read_box<ISOBMFF::Box>(input, init_segment.m_raw_data);

    bool has_seen_tkhd = false;
    bool has_seen_edts = false;
    bool has_seen_mdia = false;
    InitSegTrackBase out_track {};
    Optional<ISOBMFF::TrakHandlerType> handler_type;
    while (current_n_bytes_left - input.used_space() < trak_box->m_size) {
        auto const next_box = peek_box<ISOBMFF::Box>(input);
        if (!next_box.has_value()) {
            break;
        }

        switch (next_box->m_type) {
        case ISOBMFF_FOUR_CC('t', 'k', 'h', 'd'):
            if (has_seen_tkhd) {
                return Error::from_string_literal("trak box can only contain one tkhd box");
            }
            TRY(parse_tkhd(input, init_segment, timescale, out_track));
            has_seen_tkhd = true;
            break;
        case ISOBMFF_FOUR_CC('e', 'd', 't', 's'):
            if (has_seen_edts) {
                return Error::from_string_literal("trak box can only contain one edts box");
            }
            TRY(skip(input, next_box->m_size, init_segment.m_raw_data));
            has_seen_edts = true;
            break;
        case ISOBMFF_FOUR_CC('m', 'd', 'i', 'a'):
            if (has_seen_mdia) {
                return Error::from_string_literal("trak box can only contain one mdia box");
            }
            handler_type = TRY(MDIA::parse_mdia(input, init_segment));
            has_seen_mdia = true;
            break;
        case ISOBMFF_FOUR_CC('u', 'd', 't', 'a'):
            TRY(skip(input, next_box->m_size, init_segment.m_raw_data));
            break;
        default:
            return Error::from_string_literal("trak box contains invalid box type");
        }
    }

    // make sure we have the required boxes
    if (has_seen_tkhd && has_seen_mdia) {
        switch (handler_type.value()) {
        case ISOBMFF::TrakHandlerType::Video:
            init_segment.m_tracks.empend(InitSegVideoTrack { out_track });
            return {};
        case ISOBMFF::TrakHandlerType::Audio:
            init_segment.m_tracks.empend(InitSegAudioTrack { out_track });
            return {};
        case ISOBMFF::TrakHandlerType::Hint:
            dbgln("FIXME: Handle hint tracks");
            return {};
        }
    }

    return Error::from_string_literal("trak box is missing required boxes");
}

[[nodiscard]]
static ErrorOr<void> parse_moov(SegmentParser::Input input, InitializationSegment& init_segment)
{
    auto const current_n_bytes_left = input.used_space();
    TRY(skip_to_moov(input, init_segment));
    // skip the moov box header
    auto const moov_box = read_box<ISOBMFF::Box>(input, init_segment.m_raw_data);

    bool has_seen_mvhd = false;
    while (current_n_bytes_left - input.used_space() < moov_box->m_size) {
        auto const next_box = peek_box<ISOBMFF::Box>(input);
        if (!next_box.has_value()) {
            break;
        }

        Optional<u32> timescale;
        switch (next_box->m_type) {
        case ISOBMFF_FOUR_CC('m', 'v', 'h', 'd'):
            if (has_seen_mvhd) {
                return Error::from_string_literal("moov box can only contain one mvhd box");
            }
            timescale = TRY(parse_mvhd(input, init_segment));
            has_seen_mvhd = true;
            break;
        case ISOBMFF_FOUR_CC('t', 'r', 'a', 'k'):
            TRY(parse_trak(input, timescale, init_segment));
            break;
        case ISOBMFF_FOUR_CC('m', 'v', 'e', 'x'):
            TRY(skip(input, next_box->m_size, init_segment.m_raw_data));
            break;
        case ISOBMFF_FOUR_CC('u', 'd', 't', 'a'):
            TRY(skip(input, next_box->m_size, init_segment.m_raw_data));
            break;
        default:
            return Error::from_string_literal("moov box contains invalid box type");
        }
    }

    return {};
}
#pragma endregion

ErrorOr<InitializationSegment> ISOBMFFSegParser::parse_init_segment(Input& input) const
{
    InitializationSegment segment;
    // This function should have never been called if the following is not true:
    ASSERT(starts_with_init_segment(buffer));
    parse_ftyp(input, segment);

    TRY(parse_moov(input, segment));

    return segment;
}
}
