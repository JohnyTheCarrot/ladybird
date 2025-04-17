/*
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "SourceBuffer.h"

#include <LibJS/Runtime/ArrayBuffer.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/SourceBufferPrototype.h>
#include <LibWeb/DOM/Event.h>
#include <LibWeb/HTML/HTMLMediaElement.h>
#include <LibWeb/MediaSourceExtensions/EventNames.h>
#include <LibWeb/MediaSourceExtensions/MediaSource.h>
#include <LibWeb/WebIDL/Buffers.h>
#include <LibWeb/HTML/AudioTrackList.h>
#include <LibWeb/HTML/VideoTrackList.h>

namespace Web::MediaSourceExtensions {

GC_DEFINE_ALLOCATOR(SourceBuffer);

SourceBuffer::SourceBuffer(JS::Realm& realm)
    : DOM::EventTarget(realm)
{
}

SourceBuffer::~SourceBuffer() = default;

void SourceBuffer::initialize(JS::Realm& realm)
{
    Base::initialize(realm);
    WEB_SET_PROTOTYPE_FOR_INTERFACE(SourceBuffer);
    m_video_tracks = realm.create<HTML::VideoTrackList>(realm);
    m_audio_tracks = realm.create<HTML::AudioTrackList>(realm);
}

WebIDL::ExceptionOr<void> SourceBuffer::prepare_append()
{
    // 1. If the SourceBuffer has been removed from the sourceBuffers attribute of the parent media source then throw an InvalidStateError exception and abort these steps.
    if (!is_attached_to_parent()) {
        return vm().throw_completion<WebIDL::InvalidStateError>("SourceBuffer is not attached to a MediaSource"_string);
    }

    // 2. If the updating attribute equals true, then throw an InvalidStateError exception and abort these steps.
    if (m_updating) {
        return vm().throw_completion<WebIDL::InvalidStateError>("Cannot append while updating"_string);
    }

    // 3. Let recent element error be determined as follows:
    auto recent_element_error = false;

    // If the MediaSource was constructed in a Window:
    recent_element_error = m_internal_state.m_parent_source->internal_state().m_media_element->error() != nullptr;

    // FIXME: Otherwise: Let recent element error be the value resulting from the steps for the Window case, but run on the Window HTMLMediaElement on any change to its error attribute and communicated by using [[port to worker]] implicit messages. If such a message has not yet been received, then let recent element error be false.

    // 4. If recent element error is true, then throw an InvalidStateError exception and abort these steps.
    if (recent_element_error) {
        return vm().throw_completion<WebIDL::InvalidStateError>("MediaSource has an error"_string);
    }

    // 5. If the readyState attribute of the parent media source is in the "ended" state then run the following steps:
    if (m_internal_state.m_parent_source->ready_state() == Bindings::ReadyState::Ended) {
        // 1. Set the readyState attribute of the parent media source to "open".
        // 2. Queue a task to fire an event named sourceopen at the parent media source. (handled by set_ready_state)
        queue_a_source_buffer_task([this] {
            m_internal_state.m_parent_source->set_ready_state(Bindings::ReadyState::Open);
        });
    }

    // 6. Run the coded frame eviction algorithm.
    coded_frame_eviction();

    // 7. If the [[buffer full flag]] equals true, then throw a QuotaExceededError exception and abort these steps.
    if (m_internal_state.m_buffer_full) {
        return vm().throw_completion<WebIDL::QuotaExceededError>("Buffer is full"_string);
    }

    return {};
}

void SourceBuffer::coded_frame_eviction()
{
    // 1. Let new data equal the data that is about to be appended to this SourceBuffer.

    // FIXME: Implementations MAY decide to set [[buffer full flag]] true here if it predicts that processing new data in addition to any existing bytes in [[input buffer]] would exceed the capacity of the SourceBuffer.
    // 2. If the [[buffer full flag]] equals false, then abort these steps.
    if (!m_internal_state.m_buffer_full) {
        return;
    }

    // FIXME: 3. Let removal ranges equal a list of presentation time ranges that can be evicted from the presentation to make room for the new data.
    // FIXME: 4. For each range in removal ranges, run the coded frame removal algorithm with start and end equal to the removal range start and end timestamp respectively.
}

void SourceBuffer::buffer_append_algo()
{
    // 1. Run the segment parser loop algorithm.
    auto const parsing_succeeded = segment_parser_loop();

    // 2. If the segment parser loop algorithm in the previous step was aborted, then abort this algorithm.
    if (!parsing_succeeded) {
        return;
    }

    // 3. Set the updating attribute to false.
    m_updating = false;

    // 4. Queue a task to fire an event named update at this SourceBuffer object.
    queue_a_source_buffer_task([this] {
        dispatch_event(DOM::Event::create(realm(), EventNames::update));
    });

    // 5. Queue a task to fire an event named updateend at this SourceBuffer object.
    queue_a_source_buffer_task([this] {
        dispatch_event(DOM::Event::create(realm(), EventNames::updateend));
    });
}

// https://www.w3.org/TR/media-source-2/#sourcebuffer-append-error
void SourceBuffer::append_error()
{
    // 1. Run the reset parser state algorithm.
    reset_parser_state();

    // 2. Set the updating attribute to false.
    m_updating = false;

    // 3. Queue a task to fire an event named error at this SourceBuffer object.
    queue_a_source_buffer_task([this] {
        dispatch_event(DOM::Event::create(realm(), EventNames::error));
    });

    // 4. Queue a task to fire an event named updateend at this SourceBuffer object.
    queue_a_source_buffer_task([this] {
        dispatch_event(DOM::Event::create(realm(), EventNames::updateend));
    });

    // 5. Run the end of stream algorithm with the error parameter set to "decode".
    m_internal_state.m_parent_source->end_of_stream_algo(Bindings::EndOfStreamError::Decode);
}

// https://www.w3.org/TR/media-source-2/#dfn-coded-frame-processing
void SourceBuffer::coded_frame_processing()
{
}

// https://www.w3.org/TR/media-source-2/#dfn-reset-parser-state
void SourceBuffer::reset_parser_state()
{
}

bool SourceBuffer::is_attached_to_parent()
{
    return m_internal_state.m_parent_source->contains_source_buffer(*this);
}

HTML::TaskID SourceBuffer::queue_a_source_buffer_task(Function<void()> steps) const
{
    return HTML::queue_a_task(m_task_source.source, nullptr, nullptr, GC::create_function(heap(), move(steps)));
}

// https://www.w3.org/TR/media-source-2/#dfn-segment-parser-loop
bool SourceBuffer::segment_parser_loop()
{
    // 1. Loop Top: If the [[input buffer]] is empty, then jump to the need more data step below.
    while (!m_internal_state.m_input_buffer.is_empty()) {
        // 2. If the [[input buffer]] contains bytes that violate the SourceBuffer byte stream format specification, then run the append error algorithm and abort this algorithm.
        // FIXME: Implement this check.

        // 3. Remove any bytes that the byte stream format specifications say MUST be ignored from the start of the [[input buffer]].
        // FIXME: do the thing

        switch (m_internal_state.m_append_state) {
        // 4. If the [[append state]] equals WAITING_FOR_SEGMENT, then run the following steps:
        case AppendState::WaitingForSegment:
            // 4.1. If the beginning of the [[input buffer]] indicates the start of an initialization segment, set the [[append state]] to PARSING_INIT_SEGMENT.
            // FIXME: Implement this check.

            // 4.2. If the beginning of the [[input buffer]] indicates the start of a media segment, set [[append state]] to PARSING_MEDIA_SEGMENT.
            // FIXME: Implement this check.

            // 4.3. Jump to the loop top step above.
            continue;

        // 5. If the [[append state]] equals PARSING_INIT_SEGMENT, then run the following steps:
        case AppendState::ParsingInitSegment:
            // 5.1. If the [[input buffer]] does not contain a complete initialization segment yet, then jump to the need more data step below.
            // FIXME: Implement this check.

            // 5.2. Run the initialization segment received algorithm.
            initialization_segment_received();

            // 5.3. Remove the initialization segment bytes from the beginning of the[[input buffer]].
            // FIXME: do this

            // 5.4. Set [[append state]] to WAITING_FOR_SEGMENT.
            m_internal_state.m_append_state = AppendState::WaitingForSegment;

            // 5.5. Jump to the loop top step above.
            continue;

        // 6. If the [[append state]] equals PARSING_MEDIA_SEGMENT, then run the following steps:
        case AppendState::ParsingMediaSegment:
            // 6.1. If the [[first initialization segment received flag]] is false or the [[pending initialization segment for changeType flag]] is true, then run the append error algorithm and abort this algorithm.
            if (!m_internal_state.m_first_initialization_segment_received || m_internal_state.m_pending_initialization_segment_for_changetype) {
                append_error();
                return false;
            }

            // 6.2. If the [[input buffer]] contains one or more complete coded frames, then run the coded frame processing algorithm.
            // FIXME: check if input buffer contains one or more complete coded frames, don't just unconditionally run the algorithm
            coded_frame_processing();

            // 6.3. If this SourceBuffer is full and cannot accept more media data, then set the [[buffer full flag]] to true.
            // FIXME: what does that even mean?

            // 6.4. If the [[input buffer]] does not contain a complete media segment, then jump to the need more data step below.
            // FIXME: Implement this check.

            // 6.5. Remove the media segment bytes from the beginning of the [[input buffer]].
            // FIXME: do this

            // 6.6. Set [[append state]] to WAITING_FOR_SEGMENT.
            m_internal_state.m_append_state = AppendState::WaitingForSegment;

            // 6.7. Jump to the loop top step above.
            continue;
        }
    }

    // 7.Need more data : Return control to the calling algorithm.
    return true;
}

// https://www.w3.org/TR/media-source-2/#dfn-initialization-segment-received
void SourceBuffer::initialization_segment_received()
{
    // FIXME: implement this
}

// https://w3c.github.io/media-source/#dom-sourcebuffer-onupdatestart
void SourceBuffer::set_onupdatestart(GC::Ptr<WebIDL::CallbackType> event_handler)
{
    set_event_handler_attribute(EventNames::updatestart, event_handler);
}

// https://w3c.github.io/media-source/#dom-sourcebuffer-onupdatestart
GC::Ptr<WebIDL::CallbackType> SourceBuffer::onupdatestart()
{
    return event_handler_attribute(EventNames::updatestart);
}

// https://w3c.github.io/media-source/#dom-sourcebuffer-onupdate
void SourceBuffer::set_onupdate(GC::Ptr<WebIDL::CallbackType> event_handler)
{
    set_event_handler_attribute(EventNames::update, event_handler);
}

// https://w3c.github.io/media-source/#dom-sourcebuffer-onupdate
GC::Ptr<WebIDL::CallbackType> SourceBuffer::onupdate()
{
    return event_handler_attribute(EventNames::update);
}

// https://w3c.github.io/media-source/#dom-sourcebuffer-onupdateend
void SourceBuffer::set_onupdateend(GC::Ptr<WebIDL::CallbackType> event_handler)
{
    set_event_handler_attribute(EventNames::updateend, event_handler);
}

// https://w3c.github.io/media-source/#dom-sourcebuffer-onupdateend
GC::Ptr<WebIDL::CallbackType> SourceBuffer::onupdateend()
{
    return event_handler_attribute(EventNames::updateend);
}

// https://w3c.github.io/media-source/#dom-sourcebuffer-onerror
void SourceBuffer::set_onerror(GC::Ptr<WebIDL::CallbackType> event_handler)
{
    set_event_handler_attribute(EventNames::error, event_handler);
}

// https://w3c.github.io/media-source/#dom-sourcebuffer-onerror
GC::Ptr<WebIDL::CallbackType> SourceBuffer::onerror()
{
    return event_handler_attribute(EventNames::error);
}

// https://w3c.github.io/media-source/#dom-sourcebuffer-onabort
void SourceBuffer::set_onabort(GC::Ptr<WebIDL::CallbackType> event_handler)
{
    set_event_handler_attribute(EventNames::abort, event_handler);
}

// https://w3c.github.io/media-source/#dom-sourcebuffer-onabort
GC::Ptr<WebIDL::CallbackType> SourceBuffer::onabort()
{
    return event_handler_attribute(EventNames::abort);
}

WebIDL::ExceptionOr<void> SourceBuffer::set_mode(Bindings::AppendMode new_mode)
{
    if (!is_attached_to_parent()) {
        return vm().throw_completion<WebIDL::InvalidStateError>("SourceBuffer is not attached to a MediaSource"_string);
    }

    if (m_updating) {
        return vm().throw_completion<WebIDL::InvalidStateError>("Cannot change mode while updating"_string);
    }

    if (m_internal_state.m_generate_timestamps_flag && new_mode == Bindings::AppendMode::Segments) {
        return vm().throw_completion<WebIDL::InvalidStateError>("Cannot change mode to segments while generate_timestamps_flag is true"_string);
    }

    if (m_internal_state.m_parent_source->ready_state() == Bindings::ReadyState::Ended) {
        m_internal_state.m_parent_source->set_ready_state(Bindings::ReadyState::Open);
    }

    if (m_internal_state.m_append_state == AppendState::ParsingMediaSegment) {
        return vm().throw_completion<WebIDL::InvalidStateError>("Cannot change mode while parsing media segment"_string);
    }

    if (new_mode == Bindings::AppendMode::Sequence) {
        m_internal_state.m_group_start_timestamp = m_internal_state.m_group_end_timestamp;
    }

    m_mode = new_mode;
    return {};
}

WebIDL::ExceptionOr<void> SourceBuffer::append_buffer(GC::Root<WebIDL::BufferSource> buffer_source)
{
    // 1. Run the prepare append algorithm.
    TRY(prepare_append());

    // 2. Add data to the end of [[input buffer]].
    m_internal_state.m_input_buffer.append(buffer_source->viewed_array_buffer()->buffer());

    // 3. Set the updating attribute to true.
    m_updating = true;

    // 4. Queue a task to fire an event named updatestart at this SourceBuffer object.
    queue_a_source_buffer_task([this] {
        dispatch_event(DOM::Event::create(realm(), EventNames::updatestart));
    });

    // 5. Asynchronously run the buffer append algorithm.
    // FIXME: Make it actually asynchronous.
    buffer_append_algo();
    return {};
}

}
