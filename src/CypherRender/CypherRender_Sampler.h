//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Declares immutable, reusable sampled-image filtering and addressing.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////
#ifndef CYPHER_ENGINE_RENDER_SAMPLER_H
#define CYPHER_ENGINE_RENDER_SAMPLER_H
#pragma once

#include "CypherRender_Error.h"
#include "CypherRender_Types.h"

namespace cypher::engine::render
{
using render_sampler_handle_t = render_handle_t;
inline constexpr render_sampler_handle_t R_INVALID_SAMPLER{};

enum class render_sampler_filter_t : ::cypher::common::u8 {
    NEAREST = 0,
    LINEAR,
    COUNT
};

enum class render_sampler_mip_filter_t : ::cypher::common::u8 {
    NONE = 0,
    NEAREST,
    LINEAR,
    COUNT
};

enum class render_sampler_address_t : ::cypher::common::u8 {
    REPEAT = 0,
    CLAMP_TO_EDGE,
    COUNT
};

// A default-constructed explicit sampler uses linear min/mag filtering,
// base-level-only sampling, and repeat addressing on both axes. Omit the
// sampler handle on a draw to retain that texture's original sampling policy.
struct render_sampler_desc_t {
    render_sampler_filter_t minFilter{ render_sampler_filter_t::LINEAR };
    render_sampler_filter_t magFilter{ render_sampler_filter_t::LINEAR };
    render_sampler_mip_filter_t mipFilter{ render_sampler_mip_filter_t::NONE };
    render_sampler_address_t addressU{ render_sampler_address_t::REPEAT };
    render_sampler_address_t addressV{ render_sampler_address_t::REPEAT };
    const char *debugName{ nullptr }; // Borrowed only during creation.
};

struct render_sampler_info_t {
    render_sampler_filter_t minFilter{ render_sampler_filter_t::LINEAR };
    render_sampler_filter_t magFilter{ render_sampler_filter_t::LINEAR };
    render_sampler_mip_filter_t mipFilter{ render_sampler_mip_filter_t::NONE };
    render_sampler_address_t addressU{ render_sampler_address_t::REPEAT };
    render_sampler_address_t addressV{ render_sampler_address_t::REPEAT };
};

CYPHER_NODISCARD render_error_t R_CreateSampler(
    const render_sampler_desc_t &description,
    CY_OUT render_sampler_handle_t *samplerOut ) noexcept;
CYPHER_NODISCARD render_error_t R_DestroySampler( render_sampler_handle_t sampler ) noexcept;
CYPHER_NODISCARD bool R_IsSamplerValid( render_sampler_handle_t sampler ) noexcept;
CYPHER_NODISCARD render_error_t R_GetSamplerInfo(
    render_sampler_handle_t sampler, CY_OUT render_sampler_info_t *infoOut ) noexcept;
} // namespace cypher::engine::render
#endif // CYPHER_ENGINE_RENDER_SAMPLER_H
