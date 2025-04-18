/*
 * Copyright (c) 2025, Tuur Martens <tuurmartens4@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "AK/BufferedStream.h"
#include "AK/MemoryStream.h"

#include <AK/Forward.h>
#include <AK/Time.h>

namespace Media::SegmentParsers {
struct InitSegTrackBase {
    size_t m_track_id;
};

struct InitSegAudioTrack : InitSegTrackBase {
    // TODO: BCP-47 language tag
};

struct InitSegVideoTrack : InitSegTrackBase { };

using InitSegTrack = Variant<InitSegAudioTrack, InitSegVideoTrack>;

struct InitializationSegment final {
    ByteBuffer m_raw_data;
    Vector<InitSegTrack> m_tracks;
    AK::Duration m_duration;
};

class SegmentParser {
public:
    virtual ~SegmentParser() = default;

    [[nodiscard]]
    virtual bool starts_with_init_segment(CircularBuffer const& buffer) const
        = 0;

    [[nodiscard]]
    virtual Optional<size_t> init_segment_size(CircularBuffer const& buffer) const
        = 0;

    [[nodiscard]]
    virtual bool contains_full_init_segment(CircularBuffer const& buffer) const
        = 0;

    [[nodiscard]]
    virtual bool starts_with_media_segment(CircularBuffer const& buffer) const
        = 0;

    [[nodiscard]]
    virtual Optional<InitializationSegment> parse_init_segment(CircularBuffer const& buffer) const
        = 0;
};
}
