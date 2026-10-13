//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Classifies resolved scene draws into ordered pass indices.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RENDER_SCENE_PASSES_H
#define CYPHER_ENGINE_RENDER_SCENE_PASSES_H
#pragma once

#include "CypherRender/CypherRender_Draw.h"

namespace cypher::engine::render
{

enum class render_scene_alpha_t : ::cypher::common::u8 {
    OPAQUE = 0u,
    BLEND,
    ADDITIVE,
    COUNT
};

// Descriptors are already resolved by the caller. The classifier never reads
// renderer state or validates their GPU handles. cameraDistance is a positive
// view-space distance for a camera-visible BLEND item; farther items draw first.
// ADDITIVE items retain input order and do not require cameraDistance.
// A shadow caster need not be visible to the camera. Shadow policy is explicit,
// independent of alpha class, and requires a prepared shadowDraw when consumed.
struct render_scene_item_t {
    render_draw_indexed_desc_t cameraDraw{};
    render_draw_indexed_desc_t shadowDraw{};
    ::cypher::common::f32 cameraDistance{ 0.0f };
    bool cameraVisible{ false };
    bool castsShadow{ false };
    render_scene_alpha_t alpha{ render_scene_alpha_t::OPAQUE };
};

// Caller-owned arrays of indices into the input item span. Only the first
// corresponding *Count elements are written. A null pointer is valid for a
// pass with no items; used array regions must not overlap one another or the
// input item span. Counts and array contents are unchanged on validation failure.
struct render_scene_pass_indices_t {
    ::cypher::common::u32 *shadow{ nullptr };
    ::cypher::common::u32 shadowCapacity{ 0u };
    ::cypher::common::u32 shadowCount{ 0u };
    ::cypher::common::u32 *opaque{ nullptr };
    ::cypher::common::u32 opaqueCapacity{ 0u };
    ::cypher::common::u32 opaqueCount{ 0u };
    ::cypher::common::u32 *blend{ nullptr };
    ::cypher::common::u32 blendCapacity{ 0u };
    ::cypher::common::u32 blendCount{ 0u };
    ::cypher::common::u32 *additive{ nullptr };
    ::cypher::common::u32 additiveCapacity{ 0u };
    ::cypher::common::u32 additiveCount{ 0u };
};

// CPU-only and allocation-free. Opaque, additive, and shadow indices retain
// input order. The caller submits additive draws after opaque and BLEND draws.
// BLEND indices sort by descending distance with input index as the tie-break.
// Output indices borrow the input span: consume/copy its draw records into
// owned draw queues before the input items go out of scope. An empty span with
// items == nullptr is valid and writes zero counts.
CYPHER_NODISCARD render_error_t R_ClassifySceneItems(
    const render_scene_item_t *items,
    ::cypher::common::u32 itemCount,
    render_scene_pass_indices_t *output ) noexcept;

} // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_SCENE_PASSES_H
