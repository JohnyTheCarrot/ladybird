/*
 * Copyright (c) 2025, Tuur Martens <tuurmartens4@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibMedia/DASH/DASHDemuxer.h>
#include <LibMedia/FFmpeg/FFmpegIOContext.h>

namespace Media::DASH {
// ErrorOr<NonnullOwnPtr<DASHDemuxer>> DASHDemuxer::create(NonnullOwnPtr<Stream> stream)
// {
//     // auto io_context = TRY(Media::FFmpeg::FFmpegIOContext::create(*stream));
//     // auto demuxer = make<DASHDemuxer>(move(stream), move(io_context));
//     // // Open the container
//     // demuxer->m_format_context = avformat_alloc_context();
//     // if (demuxer->m_format_context == nullptr)
//     //     return Error::from_string_literal("Failed to allocate format context");
//     // demuxer->m_format_context->pb = demuxer->m_io_context->avio_context();
//     // if (avformat_open_input(&demuxer->m_format_context, nullptr, nullptr, nullptr) < 0)
//     //     return Error::from_string_literal("Failed to open input for format parsing");
//     //
//     // // Read stream info; doing this is required for headerless formats like MPEG
//     // if (avformat_find_stream_info(demuxer->m_format_context, nullptr) < 0)
//     //     return Error::from_string_literal("Failed to find stream info");
//     //
//     // demuxer->m_packet = av_packet_alloc();
//     // if (demuxer->m_packet == nullptr)
//     //     return Error::from_string_literal("Failed to allocate packet");
//     //
//     // return demuxer;
// }

DASHDemuxer::DASHDemuxer(NonnullOwnPtr<Stream> stream, NonnullOwnPtr<FFmpeg::FFmpegIOContext> io_context)
    : m_stream { move(stream) }
    , m_io_context { move(io_context) }
{
}
}
