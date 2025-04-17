/*
 * Copyright (c) 2025, Tuur Martens <tuurmartens4@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibMedia/Demuxer.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

namespace Media::FFmpeg {
class FFmpegIOContext;
}

namespace Media::DASH {
class DASHDemuxer final : public Demuxer {
public:
    // static ErrorOr<NonnullOwnPtr<DASHDemuxer>> create(NonnullOwnPtr<Stream> stream);

    explicit DASHDemuxer(NonnullOwnPtr<Stream> stream, NonnullOwnPtr<FFmpeg::FFmpegIOContext>);

    virtual ~DASHDemuxer() override = default;

    virtual DecoderErrorOr<Vector<Track>> get_tracks_for_type(TrackType type) override;
    virtual DecoderErrorOr<Sample> get_next_sample_for_track(Track track) override;
    virtual DecoderErrorOr<CodecID> get_codec_id_for_track(Track track) override;
    virtual DecoderErrorOr<ReadonlyBytes> get_codec_initialization_data_for_track(Track track) override;
    virtual DecoderErrorOr<Optional<AK::Duration>> seek_to_most_recent_keyframe(Track track, AK::Duration timestamp, Optional<AK::Duration> earliest_available_sample) override;
    virtual DecoderErrorOr<AK::Duration> duration(Track track) override;

private:
    NonnullOwnPtr<Stream> m_stream;
    AVCodecContext* m_codec_context { nullptr };
    AVFormatContext* m_format_context { nullptr };
    NonnullOwnPtr<FFmpeg::FFmpegIOContext> m_io_context;
};
}
