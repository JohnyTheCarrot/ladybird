/*
 * Copyright (c) 2025, Tuur Martens <tuurmartens4@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Forward.h>

namespace Media::SegmentParsers {
class SegmentParser {
public:
    virtual ~SegmentParser() = default;

    [[nodiscard]]
    virtual bool starts_with_init_segment(CircularBuffer const& buffer) const
        = 0;

    [[nodiscard]]
    virtual Optional<size_t> init_segment_size(CircularBuffer const& buffer) const
        = 0;

    virtual bool contains_full_init_segment(CircularBuffer const& buffer) const = 0;

    [[nodiscard]]
    virtual bool starts_with_media_segment(CircularBuffer const& buffer) const
        = 0;
};
}
