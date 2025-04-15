/*
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/DOM/EventTarget.h>

namespace Web::MediaSourceExtensions {

// https://w3c.github.io/media-source/#dom-sourcebufferlist
class SourceBufferList : public DOM::EventTarget {
    WEB_PLATFORM_OBJECT(SourceBufferList, DOM::EventTarget);
    GC_DECLARE_ALLOCATOR(SourceBufferList);

    using SourceBuffers = Vector<GC::Ref<SourceBuffer>>;

public:
    void set_onaddsourcebuffer(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onaddsourcebuffer();

    void set_onremovesourcebuffer(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onremovesourcebuffer();

    unsigned long length() const { return m_source_buffers.size(); }

    SourceBuffer* item(size_t index)
    {
        if (index >= m_source_buffers.size())
            return nullptr;
        return m_source_buffers[index];
    }

    SourceBuffer const* item(size_t index) const
    {
        if (index >= m_source_buffers.size())
            return nullptr;
        return m_source_buffers[index];
    }

    virtual Optional<JS::Value> item_value(size_t) const override;

    void add_source_buffer(GC::Ref<SourceBuffer> source_buffer)
    {
        m_source_buffers.append(source_buffer);
    }

    [[nodiscard]]
    SourceBuffers const& get_source_buffers() const
    {
        return m_source_buffers;
    }

private:
    SourceBuffers m_source_buffers;

    SourceBufferList(JS::Realm&);

    virtual ~SourceBufferList() override;

    virtual void initialize(JS::Realm&) override;
};

}
