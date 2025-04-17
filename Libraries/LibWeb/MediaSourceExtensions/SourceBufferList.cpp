/*
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/SourceBufferListPrototype.h>
#include <LibWeb/MediaSourceExtensions/EventNames.h>
#include <LibWeb/MediaSourceExtensions/SourceBuffer.h>
#include <LibWeb/MediaSourceExtensions/SourceBufferList.h>

namespace Web::MediaSourceExtensions {

GC_DEFINE_ALLOCATOR(SourceBufferList);

SourceBufferList::SourceBufferList(JS::Realm& realm)
    : DOM::EventTarget(realm)
{
    m_legacy_platform_object_flags = LegacyPlatformObjectFlags { .supports_indexed_properties = true };
}

SourceBufferList::~SourceBufferList() = default;

void SourceBufferList::initialize(JS::Realm& realm)
{
    Base::initialize(realm);
    WEB_SET_PROTOTYPE_FOR_INTERFACE(SourceBufferList);
}

// https://w3c.github.io/media-source/#dom-sourcebufferlist-onaddsourcebuffer
void SourceBufferList::set_onaddsourcebuffer(GC::Ptr<WebIDL::CallbackType> event_handler)
{
    set_event_handler_attribute(EventNames::addsourcebuffer, event_handler);
}

// https://w3c.github.io/media-source/#dom-sourcebufferlist-onaddsourcebuffer
GC::Ptr<WebIDL::CallbackType> SourceBufferList::onaddsourcebuffer()
{
    return event_handler_attribute(EventNames::addsourcebuffer);
}

// https://w3c.github.io/media-source/#dom-sourcebufferlist-onremovesourcebuffer
void SourceBufferList::set_onremovesourcebuffer(GC::Ptr<WebIDL::CallbackType> event_handler)
{
    set_event_handler_attribute(EventNames::removesourcebuffer, event_handler);
}

// https://w3c.github.io/media-source/#dom-sourcebufferlist-onremovesourcebuffer
GC::Ptr<WebIDL::CallbackType> SourceBufferList::onremovesourcebuffer()
{
    return event_handler_attribute(EventNames::removesourcebuffer);
}

Optional<JS::Value> SourceBufferList::item_value(size_t index) const
{
    if (index >= m_source_buffers.size()) {
        return JS::js_undefined();
    }

    return m_source_buffers[index];
}

bool SourceBufferList::contains_source_buffer(GC::Ref<SourceBuffer> source_buffer) const
{
    return m_source_buffers.contains_slow(source_buffer);
}

}
