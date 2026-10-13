//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Prepares shadow, opaque, and far-to-near blended draw order.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_ScenePasses.h"

#include <cmath>
#include <cstdint>
#include <limits>

namespace cypher::engine::render
{
namespace common = ::cypher::common;
namespace
{

// Used output prefixes must be disjoint. Comparing integer address intervals
// also catches an interval whose end would wrap the address space.
bool OverlapOrOverflow( const void *first, std::uint64_t firstBytes,
    const void *second, std::uint64_t secondBytes ) noexcept
{
    if ( firstBytes == 0u || secondBytes == 0u ) return false;
    const auto firstBegin = reinterpret_cast<std::uintptr_t>( first );
    const auto secondBegin = reinterpret_cast<std::uintptr_t>( second );
    constexpr auto maxAddress = std::numeric_limits<std::uintptr_t>::max();
    if ( firstBytes > maxAddress - firstBegin ||
         secondBytes > maxAddress - secondBegin ) return true;
    const auto firstEnd = firstBegin + static_cast<std::uintptr_t>( firstBytes );
    const auto secondEnd = secondBegin + static_cast<std::uintptr_t>( secondBytes );
    return firstBegin < secondEnd && secondBegin < firstEnd;
}

// This is a strict order: a larger distance goes first, and equal distances
// preserve input order through the index tie-break. NaN and infinity have
// already been rejected before this comparison.
bool BlendBefore( const render_scene_item_t *items,
    common::u32 left, common::u32 right ) noexcept
{
    const float a = items[left].cameraDistance;
    const float b = items[right].cameraDistance;
    return a > b || ( a == b && left < right );
}

void SiftBlendHeap( const render_scene_item_t *items,
    common::u32 *indices, common::u32 count, common::u32 root ) noexcept
{
    while ( count > 1u && root <= ( count - 2u ) / 2u ) {
        common::u32 child = root * 2u + 1u;
        if ( child + 1u < count &&
             BlendBefore( items, indices[child], indices[child + 1u] ) ) {
            ++child;
        }
        if ( !BlendBefore( items, indices[root], indices[child] ) ) return;
        const common::u32 temp = indices[root];
        indices[root] = indices[child];
        indices[child] = temp;
        root = child;
    }
}

void SortBlend( const render_scene_item_t *items,
    common::u32 *indices, common::u32 count ) noexcept
{
    for ( common::u32 parent = count / 2u; parent > 0u; --parent ) {
        SiftBlendHeap( items, indices, count, parent - 1u );
    }
    for ( common::u32 remaining = count; remaining > 1u; --remaining ) {
        const common::u32 temp = indices[0];
        indices[0] = indices[remaining - 1u];
        indices[remaining - 1u] = temp;
        SiftBlendHeap( items, indices, remaining - 1u, 0u );
    }
}

} // namespace

render_error_t R_ClassifySceneItems( const render_scene_item_t *items,
    common::u32 itemCount, render_scene_pass_indices_t *output ) noexcept
{
    if ( output == nullptr || ( itemCount != 0u && items == nullptr ) ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }

    common::u32 shadowCount = 0u;
    common::u32 opaqueCount = 0u;
    common::u32 blendCount = 0u;
    for ( common::u32 i = 0u; i < itemCount; ++i ) {
        const render_scene_item_t &item = items[i];
        if ( item.alpha != render_scene_alpha_t::OPAQUE &&
             item.alpha != render_scene_alpha_t::BLEND ) {
            return render_error_t::ERR_INVALID_ARGUMENT;
        }
        if ( item.castsShadow ) ++shadowCount;
        if ( !item.cameraVisible ) continue;
        if ( item.alpha == render_scene_alpha_t::BLEND ) {
            if ( !std::isfinite( item.cameraDistance ) ||
                 item.cameraDistance <= 0.0f ) {
                return render_error_t::ERR_INVALID_ARGUMENT;
            }
            ++blendCount;
        } else {
            ++opaqueCount;
        }
    }

    if ( ( shadowCount != 0u && output->shadow == nullptr ) ||
         ( opaqueCount != 0u && output->opaque == nullptr ) ||
         ( blendCount != 0u && output->blend == nullptr ) ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( output->shadowCapacity < shadowCount ||
         output->opaqueCapacity < opaqueCount ||
         output->blendCapacity < blendCount ) {
        return render_error_t::ERR_COMMAND_CAPACITY_EXCEEDED;
    }
    const auto inputBytes = static_cast<std::uint64_t>( itemCount ) *
        sizeof( render_scene_item_t );
    const auto shadowBytes = static_cast<std::uint64_t>( shadowCount ) *
        sizeof( common::u32 );
    const auto opaqueBytes = static_cast<std::uint64_t>( opaqueCount ) *
        sizeof( common::u32 );
    const auto blendBytes = static_cast<std::uint64_t>( blendCount ) *
        sizeof( common::u32 );
    if ( OverlapOrOverflow( output->shadow, shadowBytes,
             output->opaque, opaqueBytes ) ||
         OverlapOrOverflow( output->shadow, shadowBytes,
             output->blend, blendBytes ) ||
         OverlapOrOverflow( output->opaque, opaqueBytes,
             output->blend, blendBytes ) ||
         OverlapOrOverflow( items, inputBytes, output->shadow, shadowBytes ) ||
         OverlapOrOverflow( items, inputBytes, output->opaque, opaqueBytes ) ||
         OverlapOrOverflow( items, inputBytes, output->blend, blendBytes ) ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }

    common::u32 shadowAt = 0u;
    common::u32 opaqueAt = 0u;
    common::u32 blendAt = 0u;
    for ( common::u32 i = 0u; i < itemCount; ++i ) {
        const render_scene_item_t &item = items[i];
        if ( item.castsShadow ) output->shadow[shadowAt++] = i;
        if ( !item.cameraVisible ) continue;
        if ( item.alpha == render_scene_alpha_t::BLEND ) {
            output->blend[blendAt++] = i;
        } else {
            output->opaque[opaqueAt++] = i;
        }
    }
    SortBlend( items, output->blend, blendCount );
    output->shadowCount = shadowCount;
    output->opaqueCount = opaqueCount;
    output->blendCount = blendCount;
    return render_error_t::OK;
}

} // namespace cypher::engine::render
