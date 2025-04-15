/*
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "MediaSource.h"

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/SourceBufferPrototype.h>
#include <LibWeb/MediaSourceExtensions/EventNames.h>
#include <LibWeb/MediaSourceExtensions/SourceBuffer.h>

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
}

void SourceBuffer::segment_parser_loop()
{
    // 1. Loop Top: If the [[input buffer]] is empty, then jump to the need more data step below.
    if (m_internal_state.m_input_buffer.is_empty()) {
        // 7.Need more data : Return control to the calling algorithm.
        return;
    }

    // 2. If the [[input buffer]] contains bytes that violate the SourceBuffer byte stream format specification, then run the append error algorithm and abort this algorithm.
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
    if (!m_internal_state.m_parent_source->contains_source_buffer(this)) {
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

}
