/*
 * Copyright (c) 2018-2023, Andreas Kling <andreas@ladybird.org>
 * Copyright (c) 2021, Tobias Christiansen <tobyase@serenityos.org>
 * Copyright (c) 2021-2025, Sam Atkins <sam@ladybird.org>
 * Copyright (c) 2022-2023, MacDue <macdue@dueutil.tech>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/CSS/ComputedValues.h>
#include <LibWeb/CSS/Fetch.h>
#include <LibWeb/CSS/StyleValues/ImageStyleValue.h>
#include <LibWeb/CSS/ToGfxConversions.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/HTML/DecodedImageData.h>
#include <LibWeb/HTML/PotentialCORSRequest.h>
#include <LibWeb/HTML/SharedResourceRequest.h>
#include <LibWeb/Painting/DisplayListRecorder.h>
#include <LibWeb/Painting/PaintContext.h>
#include <LibWeb/Platform/Timer.h>

namespace Web::CSS {

ValueComparingNonnullRefPtr<ImageStyleValue const> ImageStyleValue::create(URL const& url)
{
    return adopt_ref(*new (nothrow) ImageStyleValue(url));
}

ValueComparingNonnullRefPtr<ImageStyleValue const> ImageStyleValue::create(::URL::URL const& url)
{
    return adopt_ref(*new (nothrow) ImageStyleValue(URL { url.to_string() }));
}

ImageStyleValue::ImageStyleValue(URL const& url)
    : AbstractImageStyleValue(Type::Image)
    , m_url(url)
{
}

ImageStyleValue::~ImageStyleValue() = default;

void ImageStyleValue::visit_edges(JS::Cell::Visitor& visitor) const
{
    Base::visit_edges(visitor);
    // FIXME: visit_edges in non-GC allocated classes is confusing pattern.
    //        Consider making CSSStyleValue to be GC allocated instead.
    visitor.visit(m_resource_request);
    visitor.visit(m_style_sheet);
    visitor.visit(m_timer);
}

void ImageStyleValue::load_any_resources(DOM::Document& document)
{
    if (m_resource_request)
        return;
    m_document = &document;

    if (m_style_sheet) {
        m_resource_request = fetch_an_external_image_for_a_stylesheet(m_url, { *m_style_sheet });
    } else {
        m_resource_request = fetch_an_external_image_for_a_stylesheet(m_url, { document });
    }
    if (m_resource_request) {
        m_resource_request->add_callbacks(
            [this, weak_this = make_weak_ptr()] {
                if (!weak_this || !m_document)
                    return;

                auto image_data = m_resource_request->image_data();
                if (image_data->is_animated() && image_data->frame_count() > 1) {
                    m_timer = Platform::Timer::create(m_document->heap());
                    m_timer->set_interval(image_data->frame_duration(0));
                    m_timer->on_timeout = GC::create_function(m_document->heap(), [this] { animate(); });
                    m_timer->start();
                }
            },
            nullptr);
    }
}

void ImageStyleValue::animate()
{
    if (!m_resource_request)
        return;
    auto image_data = m_resource_request->image_data();
    if (!image_data)
        return;

    m_current_frame_index = (m_current_frame_index + 1) % image_data->frame_count();
    auto current_frame_duration = image_data->frame_duration(m_current_frame_index);

    if (current_frame_duration != m_timer->interval())
        m_timer->restart(current_frame_duration);

    if (m_current_frame_index == image_data->frame_count() - 1) {
        ++m_loops_completed;
        if (m_loops_completed > 0 && m_loops_completed == image_data->loop_count())
            m_timer->stop();
    }

    if (on_animate)
        on_animate();
}

bool ImageStyleValue::is_paintable() const
{
    return image_data();
}

Gfx::ImmutableBitmap const* ImageStyleValue::bitmap(size_t frame_index, Gfx::IntSize size) const
{
    if (auto image_data = this->image_data())
        return image_data->bitmap(frame_index, size);
    return nullptr;
}

String ImageStyleValue::to_string(SerializationMode) const
{
    return m_url.to_string();
}

bool ImageStyleValue::equals(CSSStyleValue const& other) const
{
    if (type() != other.type())
        return false;
    return m_url == other.as_image().m_url;
}

Optional<CSSPixels> ImageStyleValue::natural_width() const
{
    if (auto const image_data = this->image_data())
        return image_data->intrinsic_width(Gfx::ImageOrientation::FromExif);

    return {};
}

Optional<CSSPixels> ImageStyleValue::natural_height() const
{
    if (auto const image_data = this->image_data())
        return image_data->intrinsic_height(Gfx::ImageOrientation::FromExif);

    return {};
}

Optional<CSSPixelFraction> ImageStyleValue::natural_aspect_ratio() const
{
    if (auto const image_data = this->image_data())
        return image_data->intrinsic_aspect_ratio(Gfx::ImageOrientation::FromExif);

    return {};
}
Optional<CSSPixels> ImageStyleValue::intrinsic_width(Gfx::ImageOrientation image_orientation) const
{
    if (auto const image_data = this->image_data())
        return image_data->intrinsic_width(image_orientation);

    return {};
}
Optional<CSSPixels> ImageStyleValue::intrinsic_height(Gfx::ImageOrientation image_orientation) const
{
    if (auto const image_data = this->image_data())
        return image_data->intrinsic_height(image_orientation);

    return {};
}
Optional<CSSPixelFraction> ImageStyleValue::intrinsic_aspect_ratio(Gfx::ImageOrientation image_orientation) const
{
    if (auto const image_data = this->image_data())
        return image_data->intrinsic_aspect_ratio(image_orientation);

    return {};
}

void ImageStyleValue::paint(PaintContext& context, DevicePixelRect const& dest_rect, CSS::ImageRendering image_rendering, Gfx::ImageOrientation image_orientation) const
{
    if (auto const* b = bitmap(m_current_frame_index, dest_rect.size().to_type<int>()); b != nullptr) {
        auto scaling_mode = to_gfx_scaling_mode(image_rendering, b->rect(image_orientation), dest_rect.to_type<int>());
        auto dest_int_rect = dest_rect.to_type<int>();
        context.display_list_recorder().draw_scaled_immutable_bitmap(dest_int_rect, dest_int_rect, *b, scaling_mode, image_orientation);
    }
}

Gfx::ImmutableBitmap const* ImageStyleValue::current_frame_bitmap(DevicePixelRect const& dest_rect) const
{
    return bitmap(m_current_frame_index, dest_rect.size().to_type<int>());
}

GC::Ptr<HTML::DecodedImageData> ImageStyleValue::image_data() const
{
    if (!m_resource_request)
        return nullptr;
    return m_resource_request->image_data();
}

Optional<Gfx::Color> ImageStyleValue::color_if_single_pixel_bitmap() const
{
    if (auto const* b = bitmap(m_current_frame_index)) {
        if (b->width(Gfx::ImageOrientation::FromDecoded) == 1 && b->height(Gfx::ImageOrientation::FromDecoded) == 1)
            return b->get_pixel(0, 0);
    }
    return {};
}

void ImageStyleValue::set_style_sheet(GC::Ptr<CSSStyleSheet> style_sheet)
{
    Base::set_style_sheet(style_sheet);
    m_style_sheet = style_sheet;
}

}
