// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/quads/render_pass_draw_quad.h"

#include "base/debug/trace_event_argument.h"
#include "base/values.h"
#include "cc/base/math_util.h"
#include "cc/debug/traced_value.h"
#include "third_party/skia/include/core/SkImageFilter.h"

namespace cc {

RenderPassDrawQuad::RenderPassDrawQuad()
    : render_pass_id(RenderPassId(-1, -1)),
      mask_resource_id(static_cast<ResourceProvider::ResourceId>(-1)) {
}

RenderPassDrawQuad::~RenderPassDrawQuad() {
}

void RenderPassDrawQuad::SetNew(const SharedQuadState* shared_quad_state,
                                const gfx::Rect& rect,
                                const gfx::Rect& visible_rect,
                                RenderPassId render_pass_id,
                                ResourceProvider::ResourceId mask_resource_id,
                                const gfx::Vector2dF& mask_uv_scale,
                                const gfx::Size& mask_texture_size,
                                const FilterOperations& filters,
                                const gfx::Vector2dF& filters_scale,
                                const FilterOperations& background_filters) {
  DCHECK_GT(render_pass_id.layer_id, 0);
  DCHECK_GE(render_pass_id.index, 0);

  gfx::Rect opaque_rect;
  bool needs_blending = false;
  SetAll(shared_quad_state,
         rect,
         opaque_rect,
         visible_rect,
         needs_blending,
         render_pass_id,
         mask_resource_id,
         mask_uv_scale,
         mask_texture_size,
         filters,
         filters_scale,
         background_filters);
}

void RenderPassDrawQuad::SetAll(const SharedQuadState* shared_quad_state,
                                const gfx::Rect& rect,
                                const gfx::Rect& opaque_rect,
                                const gfx::Rect& visible_rect,
                                bool needs_blending,
                                RenderPassId render_pass_id,
                                ResourceProvider::ResourceId mask_resource_id,
                                const gfx::Vector2dF& mask_uv_scale,
                                const gfx::Size& mask_texture_size,
                                const FilterOperations& filters,
                                const gfx::Vector2dF& filters_scale,
                                const FilterOperations& background_filters) {
  DCHECK_GT(render_pass_id.layer_id, 0);
  DCHECK_GE(render_pass_id.index, 0);

  DrawQuad::SetAll(shared_quad_state, DrawQuad::RENDER_PASS, rect, opaque_rect,
                   visible_rect, needs_blending);
  this->render_pass_id = render_pass_id;
  this->mask_resource_id = mask_resource_id;
  this->mask_uv_scale = mask_uv_scale;
  this->mask_texture_size = mask_texture_size;
  this->filters = filters;
  this->filters_scale = filters_scale;
  this->background_filters = background_filters;
}

void RenderPassDrawQuad::IterateResources(
    const ResourceIteratorCallback& callback) {
  if (mask_resource_id)
    mask_resource_id = callback.Run(mask_resource_id);
}

gfx::RectF RenderPassDrawQuad::MaskUVRect() const {
  gfx::RectF mask_uv_rect((mask_uv_scale.x() * rect.x()) / rect.width(),
                          (mask_uv_scale.y() * rect.y()) / rect.height(),
                          mask_uv_scale.x(),
                          mask_uv_scale.y());
  return mask_uv_rect;
}

const RenderPassDrawQuad* RenderPassDrawQuad::MaterialCast(
    const DrawQuad* quad) {
  DCHECK_EQ(quad->material, DrawQuad::RENDER_PASS);
  return static_cast<const RenderPassDrawQuad*>(quad);
}

void RenderPassDrawQuad::ExtendValue(base::debug::TracedValue* value) const {
  TracedValue::SetIDRef(render_pass_id.AsTracingId(), value, "render_pass_id");
  value->SetInteger("mask_resource_id", mask_resource_id);

  value->BeginArray("mask_texture_size");
  MathUtil::AddToTracedValue(mask_texture_size, value);
  value->EndArray();

  value->BeginArray("mask_uv_scale");
  MathUtil::AddToTracedValue(mask_uv_scale, value);
  value->EndArray();

  value->BeginDictionary("filters");
  filters.AsValueInto(value);
  value->EndDictionary();

  value->BeginArray("filters_scale");
  MathUtil::AddToTracedValue(filters_scale, value);
  value->EndArray();

  value->BeginDictionary("background_filters");
  background_filters.AsValueInto(value);
  value->EndDictionary();
}

}  // namespace cc
