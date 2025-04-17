/*
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/Bindings/SourceBufferPrototype.h>
#include <LibWeb/DOM/EventTarget.h>

namespace Media::SegmentParsers {
class SegmentParser;
}
namespace Web::Bindings {
enum class EndOfStreamError : u8;
}
namespace Web::MediaSourceExtensions {

enum class AppendState {
    WaitingForSegment,
    ParsingInitSegment,
    ParsingMediaSegment
};

// https://w3c.github.io/media-source/#dom-sourcebuffer
class SourceBuffer : public DOM::EventTarget {
    WEB_PLATFORM_OBJECT(SourceBuffer, DOM::EventTarget);
    GC_DECLARE_ALLOCATOR(SourceBuffer);

public:
    void set_onupdatestart(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onupdatestart();

    void set_onupdate(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onupdate();

    void set_onupdateend(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onupdateend();

    void set_onerror(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onerror();

    void set_onabort(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onabort();

    Bindings::AppendMode mode() const { return m_mode; }
    WebIDL::ExceptionOr<void> set_mode(Bindings::AppendMode);
    void set_mode_unchecked(Bindings::AppendMode);

    bool updating() const { return m_updating; }

    GC::Ref<HTML::AudioTrackList> audio_tracks() const { return *m_audio_tracks; }

    GC::Ref<HTML::VideoTrackList> video_tracks() const { return *m_video_tracks; }

    WebIDL::ExceptionOr<void> append_buffer(GC::Root<WebIDL::BufferSource>);

    struct InternalState final {
        GC::Ptr<MediaSource> m_parent_source = nullptr;
        HighResolutionTime::DOMHighResTimeStamp m_group_start_timestamp;
        HighResolutionTime::DOMHighResTimeStamp m_group_end_timestamp;
        AppendState m_append_state = AppendState::WaitingForSegment;
        ByteBuffer m_input_buffer;
        GC::Ptr<Media::SegmentParsers::SegmentParser> m_segment_parser = nullptr;
        bool m_buffer_full = false;
        bool m_generate_timestamps_flag = false;
        bool m_first_initialization_segment_received = false;
        bool m_pending_initialization_segment_for_changetype = false;
    };

    [[nodiscard]]
    InternalState& internal_state()
    {
        return m_internal_state;
    }

    [[nodiscard]]
    InternalState const& internal_state() const
    {
        return m_internal_state;
    }

protected:
    SourceBuffer(JS::Realm&, MimeSniff::MimeType const& type);

    virtual ~SourceBuffer() override;

    virtual void initialize(JS::Realm&) override;

private:
    Bindings::AppendMode m_mode = {};
    bool m_updating = false;

    [[nodiscard]]
    bool is_attached_to_parent();

    InternalState m_internal_state = {};
    GC::Ptr<HTML::AudioTrackList> m_audio_tracks;
    GC::Ptr<HTML::VideoTrackList> m_video_tracks;

    HTML::TaskID queue_a_source_buffer_task(Function<void()> steps) const;

    HTML::UniqueTaskSource m_task_source;

    // Algorithms
    /**
     * @return Succeeded, did not abort
     */
    [[nodiscard]]
    bool segment_parser_loop();
    void initialization_segment_received();
    WebIDL::ExceptionOr<void> prepare_append();
    void coded_frame_eviction();
    void buffer_append_algo();
    void append_error();
    void coded_frame_processing();
    void reset_parser_state();

    // Utility
    // Remove any bytes that the byte stream format specifications say MUST be ignored from the start of the [[input buffer]].
    void trim_input_buffer();

    [[nodiscard]]
    bool input_buffer_starts_with_init_seg() const;

    [[nodiscard]]
    bool input_buffer_starts_with_media_seg() const;
};

}
