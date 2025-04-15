/*
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "SourceBufferList.h"

#include <LibWeb/Bindings/MediaSourcePrototype.h>
#include <LibWeb/DOM/EventTarget.h>

namespace Web::MediaSourceExtensions {

// https://w3c.github.io/media-source/#dom-mediasource
class MediaSource : public DOM::EventTarget {
    WEB_PLATFORM_OBJECT(MediaSource, DOM::EventTarget);
    GC_DECLARE_ALLOCATOR(MediaSource);

public:
    [[nodiscard]] static WebIDL::ExceptionOr<GC::Ref<MediaSource>> construct_impl(JS::Realm&);

    // https://w3c.github.io/media-source/#dom-mediasource-canconstructindedicatedworker
    static bool can_construct_in_dedicated_worker(JS::VM&) { return true; }

    void set_onsourceopen(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onsourceopen();

    void set_onsourceended(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onsourceended();

    void set_onsourceclose(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onsourceclose();

    WebIDL::ExceptionOr<GC::Ref<SourceBuffer>> add_source_buffer(String const&);

    static bool is_type_supported(JS::VM&, String const&);

    Bindings::ReadyState ready_state() const
    {
        return m_ready_state;
    }
    void set_ready_state(Bindings::ReadyState);

    [[nodiscard]]
    bool contains_source_buffer(GC::Ptr<SourceBuffer const>) const;

    GC::Ref<SourceBufferList> source_buffers() const { return *m_source_buffers; }

    struct InternalState final {
        bool m_has_ever_been_attached = false;
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
    MediaSource(JS::Realm&);

    virtual ~MediaSource() override;

    virtual void initialize(JS::Realm&) override;

    virtual GC::Ref<SourceBuffer> create_source_buffer();

private:
    GC::Ptr<SourceBufferList> m_source_buffers;

    Bindings::ReadyState m_ready_state = Bindings::ReadyState::Closed;
    InternalState m_internal_state;
};

}
