/*
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/Bindings/SourceBufferPrototype.h>
#include <LibWeb/DOM/EventTarget.h>

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

    bool updating() const { return m_updating; }

    struct InternalState final {
        GC::Ptr<MediaSource> m_parent_source = nullptr;
        HighResolutionTime::DOMHighResTimeStamp m_group_start_timestamp;
        HighResolutionTime::DOMHighResTimeStamp m_group_end_timestamp;
        AppendState m_append_state = AppendState::WaitingForSegment;
        ByteBuffer m_input_buffer;
        bool m_buffer_full = false;
        bool m_generate_timestamps_flag = false;
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
    SourceBuffer(JS::Realm&);

    virtual ~SourceBuffer() override;

    virtual void initialize(JS::Realm&) override;

private:
    Bindings::AppendMode m_mode;
    bool m_updating = false;

    InternalState m_internal_state;

    void segment_parser_loop();
};

}
