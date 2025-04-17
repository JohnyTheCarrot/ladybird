/*
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "SourceBuffer.h"

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/MediaSourcePrototype.h>
#include <LibWeb/DOM/Event.h>
#include <LibWeb/HTML/HTMLMediaElement.h>
#include <LibWeb/MediaSourceExtensions/EventNames.h>
#include <LibWeb/MediaSourceExtensions/MediaSource.h>
#include <LibWeb/MimeSniff/MimeType.h>

namespace Web::MediaSourceExtensions {

GC_DEFINE_ALLOCATOR(MediaSource);

WebIDL::ExceptionOr<GC::Ref<MediaSource>> MediaSource::construct_impl(JS::Realm& realm)
{
    return realm.create<MediaSource>(realm);
}

// https://www.w3.org/TR/media-source-2/#end-of-stream-algorithm
void MediaSource::end_of_stream_algo(Optional<Bindings::EndOfStreamError> error)
{
    // 1. Change the readyState attribute value to "ended".
    m_ready_state = Bindings::ReadyState::Ended;

    // 2. Queue a task to fire an event named sourceended at the MediaSource.
    queue_a_media_source_task([this] {
        dispatch_event(DOM::Event::create(realm(), EventNames::sourceended));
    });

    // 3. If error is not set:
    if (!error.has_value()) {
        // FIXME: 1. Run the duration change algorithm with new duration set to the largest track buffer ranges end time across all the track buffers across all SourceBuffer objects in sourceBuffers.
        // FIXME: 2. Notify the media element that it now has all of the media data.
        return;
    }

    // 3. If error is set to "network"
    switch (error.value()) {
    case Bindings::EndOfStreamError::Network:
        // Use the mirror if necessary algorithm to run the following steps in Window:
        mirror_if_necessary([this] {
            switch (m_internal_state.m_media_element->ready_state()) {
            case HTML::HTMLMediaElement::ReadyState::HaveNothing:
                // If the HTMLMediaElement's readyState attribute equals HAVE_NOTHING
                // Run the "If the media data cannot be fetched at all, due to network errors, causing the user agent to give up trying to fetch the resource" steps of the resource fetch algorithm's media data processing steps list.
                // AD HOC: Unclear spec, this is a best guess.
                m_internal_state.m_media_element->failed_with_media_provider("FIXME: figure out a good error message for this"_string);
                break;
            default:
                // If the HTMLMediaElement's readyState attribute is greater than HAVE_NOTHING
                // (everything else is greater than HAVE_NOTHING)
                // Run the "If the connection is interrupted after some media data has been received, causing the user agent to give up trying to fetch the resource" steps
                // AD HOC: Unclear spec, this is a best guess.
                m_internal_state.m_media_element->connection_interrupted_failure();
                break;
            }
        });
        break;
    case Bindings::EndOfStreamError::Decode:
        mirror_if_necessary([this] {
            switch (m_internal_state.m_media_element->ready_state()) {
            case HTML::HTMLMediaElement::ReadyState::HaveNothing:
                // If the HTMLMediaElement's readyState attribute equals HAVE_NOTHING
                // Run the "If the media data can be fetched but is found by inspection to be in an unsupported format, or can otherwise not be rendered at all" steps of the resource fetch algorithm's media data processing steps list.
                // AD HOC: Unclear spec, this is a best guess.
                m_internal_state.m_media_element->failed_with_media_provider("FIXME: figure out a good error message for this"_string);
                break;
            default:
                // If the HTMLMediaElement's readyState attribute is greater than HAVE_NOTHING
                // (everything else is greater than HAVE_NOTHING)
                // Run the media data is corrupted steps of the resource fetch algorithm's media data processing steps list.
                // AD HOC: Unclear spec, this is a best guess.
                m_internal_state.m_media_element->media_data_corrupted_failure();
                break;
            }
        });
        break;
    }
}

MediaSource::MediaSource(JS::Realm& realm)
    : DOM::EventTarget(realm)
{
}

MediaSource::~MediaSource() = default;

void MediaSource::initialize(JS::Realm& realm)
{
    Base::initialize(realm);
    WEB_SET_PROTOTYPE_FOR_INTERFACE(MediaSource);
    m_source_buffers = realm.create<SourceBufferList>(realm);
}

GC::Ref<SourceBuffer> MediaSource::create_source_buffer(MimeSniff::MimeType const& type)
{
    auto const result = realm().create<SourceBuffer>(realm(), type);
    result->internal_state().m_parent_source = this;
    return result;
}

HTML::TaskID MediaSource::queue_a_media_source_task(Function<void()> steps)
{
    return HTML::queue_a_task(m_task_source.source, nullptr, nullptr, GC::create_function(heap(), move(steps)));
}

// https://www.w3.org/TR/media-source-2/#dfn-mirror-if-necessary
void MediaSource::mirror_if_necessary(Function<void()> const& steps)
{
    // FIXME: If the MediaSource was constructed in a DedicatedWorkerGlobalScope:
    // Post an internal mirror on window message to [[port to main]] whose implicit handler in Window will run steps. Return control to the caller without awaiting that handler's receipt of the message.

    // Otherwise, run steps
    steps();
}

// https://w3c.github.io/media-source/#dom-mediasource-onsourceopen
void MediaSource::set_onsourceopen(GC::Ptr<WebIDL::CallbackType> event_handler)
{
    set_event_handler_attribute(EventNames::sourceopen, event_handler);
}

// https://w3c.github.io/media-source/#dom-mediasource-onsourceopen
GC::Ptr<WebIDL::CallbackType> MediaSource::onsourceopen()
{
    return event_handler_attribute(EventNames::sourceopen);
}

// https://w3c.github.io/media-source/#dom-mediasource-onsourceended
void MediaSource::set_onsourceended(GC::Ptr<WebIDL::CallbackType> event_handler)
{
    set_event_handler_attribute(EventNames::sourceended, event_handler);
}

// https://w3c.github.io/media-source/#dom-mediasource-onsourceended
GC::Ptr<WebIDL::CallbackType> MediaSource::onsourceended()
{
    return event_handler_attribute(EventNames::sourceended);
}

// https://w3c.github.io/media-source/#dom-mediasource-onsourceclose
void MediaSource::set_onsourceclose(GC::Ptr<WebIDL::CallbackType> event_handler)
{
    set_event_handler_attribute(EventNames::sourceclose, event_handler);
}

// https://w3c.github.io/media-source/#dom-mediasource-onsourceclose
GC::Ptr<WebIDL::CallbackType> MediaSource::onsourceclose()
{
    return event_handler_attribute(EventNames::sourceclose);
}

[[nodiscard]] static bool should_generate_timestamps(String const& type)
{
    auto mime_type = MimeSniff::MimeType::parse(type);
    VERIFY(mime_type.has_value());

    auto const& parsed_type = mime_type->type();
    auto const& parsed_subtype = mime_type->subtype();

    if (parsed_subtype == "webm" || parsed_subtype == "mp4" || parsed_subtype == "mp2t")
        return false;

    if (parsed_type == "audio" && (parsed_subtype == "aac" || parsed_subtype == "mpeg"))
        return true;

    VERIFY_NOT_REACHED();
}

WebIDL::ExceptionOr<GC::Ref<SourceBuffer>> MediaSource::add_source_buffer(String const& type)
{
    AK::set_debug_enabled(true);
    auto& vm = this->vm();
    dbgln("add_source_buffer: {}", type);

    if (type.is_empty()) {
        dbgln("cancel 1");
        return vm.throw_completion<JS::TypeError>();
    }

    auto mime_type = MimeSniff::MimeType::parse(type);
    if (!mime_type.has_value() || !is_type_supported(mime_type.value())) {
        dbgln("cancel 2");
        return vm.throw_completion<WebIDL::NotSupportedError>("Unsupported type"_string);
    }

    // FIXME: If the user agent can't handle any more SourceBuffer objects or if creating a SourceBuffer based on type would result in an unsupported SourceBuffer configuration, then throw a QuotaExceededError exception and abort these steps.
    if (m_ready_state != Bindings::ReadyState::Open) {
        dbgln("cancel 3");
        return vm.throw_completion<WebIDL::InvalidStateError>("MediaSource is not open"_string);
    }

    auto source_buffer = create_source_buffer(mime_type.value());
    source_buffer->internal_state().m_generate_timestamps_flag = should_generate_timestamps(type);
    auto const new_buffer_mode = source_buffer->internal_state().m_generate_timestamps_flag ? Bindings::AppendMode::Sequence : Bindings::AppendMode::Segments;
    source_buffer->set_mode_unchecked(new_buffer_mode);

    dbgln("Adding buffer");
    m_source_buffers->add_source_buffer(source_buffer);
    for (auto const buffer : m_source_buffers->get_source_buffers()) {
        buffer->dispatch_event(DOM::Event::create(realm(), EventNames::addsourcebuffer));
    }

    return source_buffer;
}

bool MediaSource::is_type_supported(MimeSniff::MimeType const& type)
{
    // auto const& parsed_type = type.type();
    auto const& parsed_subtype = type.subtype();

    return parsed_subtype == "mp4";

    //
    // if (parsed_type == "audio") {
    //     return parsed_subtype == "webm" || parsed_subtype == "mp4" || parsed_subtype == "mp2t" || parsed_subtype == "mpeg" || parsed_subtype == "aac";
    // }
    //
    // if (parsed_type == "video") {
    //     return parsed_subtype == "webm" || parsed_subtype == "mp4" || parsed_subtype == "mp2t";
    // }

    // return false;
}

// https://w3c.github.io/media-source/#dom-mediasource-istypesupported
bool MediaSource::is_type_supported(JS::VM&, String const& type)
{
    // 1. If type is an empty string, then return false.
    if (type.is_empty())
        return false;

    // 2. If type does not contain a valid MIME type string, then return false.
    auto mime_type = MimeSniff::MimeType::parse(type);
    if (!mime_type.has_value())
        return false;

    return is_type_supported(mime_type.value());
    // FIXME: 3. If type contains a media type or media subtype that the MediaSource does not support, then
    //    return false.

    // FIXME: 4. If type contains a codec that the MediaSource does not support, then return false.

    // FIXME: 5. If the MediaSource does not support the specified combination of media type, media
    //    subtype, and codecs then return false.

    // 6. Return true.
}

void MediaSource::set_ready_state(Bindings::ReadyState state)
{
    m_ready_state = state;
    if (m_ready_state == Bindings::ReadyState::Open) {
        queue_a_media_source_task([this] {
            dispatch_event(DOM::Event::create(realm(), EventNames::sourceopen));
        });
    }
}

bool MediaSource::contains_source_buffer(GC::Ref<SourceBuffer> source_buffer) const
{
    return m_source_buffers->contains_source_buffer(source_buffer);
}

// https://www.w3.org/TR/media-source-2/#endofstream-method
WebIDL::ExceptionOr<void> MediaSource::end_of_stream(Optional<Bindings::EndOfStreamError> error)
{
    // 1. If the readyState attribute is not in the "open" state then throw an InvalidStateError exception and abort these steps.
    if (m_ready_state != Bindings::ReadyState::Open) {
        return vm().throw_completion<WebIDL::InvalidStateError>("MediaSource is not open"_string);
    }

    // 2. If the updating attribute equals true on any SourceBuffer in sourceBuffers, then throw an InvalidStateError exception and abort these steps.
    for (auto const& source_buffer : m_source_buffers->get_source_buffers()) {
        if (source_buffer->updating()) {
            return vm().throw_completion<WebIDL::InvalidStateError>("SourceBuffer is updating"_string);
        }
    }

    // 3. Run the end of stream algorithm with the error parameter set to error.
    end_of_stream_algo(error);
    return {};
}
}
