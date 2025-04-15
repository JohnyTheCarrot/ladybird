/*
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "SourceBuffer.h"

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/MediaSourcePrototype.h>
#include <LibWeb/DOM/Event.h>
#include <LibWeb/MediaSourceExtensions/EventNames.h>
#include <LibWeb/MediaSourceExtensions/MediaSource.h>
#include <LibWeb/MimeSniff/MimeType.h>

namespace Web::MediaSourceExtensions {

GC_DEFINE_ALLOCATOR(MediaSource);

WebIDL::ExceptionOr<GC::Ref<MediaSource>> MediaSource::construct_impl(JS::Realm& realm)
{
    return realm.create<MediaSource>(realm);
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
GC::Ref<SourceBuffer> MediaSource::create_source_buffer()
{
    auto const result = realm().create<SourceBuffer>(realm());
    result->internal_state().m_parent_source = this;
    return result;
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

    if (type.is_empty())
        return vm.throw_completion<JS::TypeError>();

    if (!is_type_supported(vm, type))
        return vm.throw_completion<WebIDL::NotSupportedError>("Unsupported type"_string);

    // FIXME: If the user agent can't handle any more SourceBuffer objects or if creating a SourceBuffer based on type would result in an unsupported SourceBuffer configuration, then throw a QuotaExceededError exception and abort these steps.
    if (m_ready_state != Bindings::ReadyState::Open)
        return vm.throw_completion<WebIDL::InvalidStateError>("MediaSource is not open"_string);

    auto source_buffer = create_source_buffer();
    source_buffer->internal_state().m_generate_timestamps_flag = should_generate_timestamps(type);
    auto const new_buffer_mode = source_buffer->internal_state().m_generate_timestamps_flag ? Bindings::AppendMode::Sequence : Bindings::AppendMode::Segments;
    if (auto const result = source_buffer->set_mode(new_buffer_mode); result.is_error())
        return result.exception();

    m_source_buffers->add_source_buffer(source_buffer);
    for (auto const buffer : m_source_buffers->get_source_buffers()) {
        buffer->dispatch_event(DOM::Event::create(realm(), EventNames::addsourcebuffer));
    }

    return source_buffer;
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

    auto const& parsed_type = mime_type->type();
    auto const& parsed_subtype = mime_type->subtype();

    if (parsed_type == "audio") {
        return parsed_subtype == "webm" || parsed_subtype == "mp4" || parsed_subtype == "mp2t" || parsed_subtype == "mpeg" || parsed_subtype == "aac";
    }

    if (parsed_type == "video") {
        return parsed_subtype == "webm" || parsed_subtype == "mp4" || parsed_subtype == "mp2t";
    }

    return false;

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
    if (m_ready_state == Bindings::ReadyState::Open)
        dispatch_event(DOM::Event::create(realm(), EventNames::sourceopen));
}

bool MediaSource::contains_source_buffer(GC::Ptr<SourceBuffer const>) const
{
    // TODO:
    return true;
}

}
