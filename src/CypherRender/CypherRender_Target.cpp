//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Owns validated offscreen targets and non-nested target-pass state.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_Target.h"
#include "CypherRender_Local.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherCommon/Tier1/CypherCommon_HandleTable.h"
#include <cmath>
#include <limits>

namespace cypher::engine::render
{
namespace common = ::cypher::common;
namespace
{
struct target_record_t {
    backend_render_target_t native{};
    render_target_info_t info{};
    render_texture_format_t colorFormat{ render_texture_format_t::RGBA8 };
};
struct target_system_t {
    common::handle_table_t<target_record_t> records{};
    bool initialized{ false };
};
target_system_t targetSystem{};

render_error_t ResolveTarget( render_target_handle_t target,
    common::handle32_t &slot, target_record_t *&record ) noexcept
{
    slot = common::CY_HANDLE32_INVALID;
    record = nullptr;
    if ( !common::Cy_Handle64IsValid( target ) ) return render_error_t::ERR_INVALID_HANDLE;
    if ( common::Cy_Handle64Type( target ) !=
         static_cast<common::u32>( render_object_type_t::RENDER_TARGET ) ) {
        return render_error_t::ERR_RESOURCE_TYPE_MISMATCH;
    }
    if ( !common::Cy_Handle32TryMake( common::Cy_Handle64Index( target ),
             common::Cy_Handle64Generation( target ), &slot ) ) {
        return render_error_t::ERR_INVALID_HANDLE;
    }
    record = common::HandleTable_Get( &targetSystem.records, slot );
    return record != nullptr ? render_error_t::OK : render_error_t::ERR_STALE_HANDLE;
}

bool ValidPassClear( const render_target_pass_desc_t &description ) noexcept
{
    return ( description.clearFlags & ~( R_CLEAR_COLOR | R_CLEAR_DEPTH ) ) == 0u &&
        std::isfinite( description.clearColor.red ) &&
        std::isfinite( description.clearColor.green ) &&
        std::isfinite( description.clearColor.blue ) &&
        std::isfinite( description.clearColor.alpha ) &&
        std::isfinite( description.clearDepth ) &&
        description.clearDepth >= 0.0f && description.clearDepth <= 1.0f;
}
} // namespace

render_error_t R_TargetSystemInit() noexcept
{
    if ( targetSystem.initialized ) return render_error_t::ERR_ALREADY_INITIALIZED;
    if ( !common::HandleTable_Init( &targetSystem.records, common::Allocator_GetSystem(), 32u ) ) {
        return render_error_t::ERR_OUT_OF_MEMORY;
    }
    targetSystem.initialized = true;
    return render_error_t::OK;
}

render_error_t R_TargetSystemShutdown() noexcept
{
    if ( !targetSystem.initialized ) return render_error_t::OK;
    render_error_t firstError = render_error_t::OK;
    (void)common::HandleTable_ForEach( &targetSystem.records,
        [&]( common::handle32_t, target_record_t &record ) noexcept {
            const render_error_t destroyResult = tr.backend.api != nullptr &&
                tr.backend.api->DestroyRenderTarget != nullptr
                    ? tr.backend.api->DestroyRenderTarget( record.native, tr.backend.state )
                    : render_error_t::ERR_INTERNAL_ERROR;
            const render_error_t releaseResult = record.info.color.value != R_INVALID_TEXTURE.value
                ? R_TextureReleaseReference( record.info.color ) : render_error_t::OK;
            const render_error_t depthReleaseResult = record.info.depth.value != R_INVALID_TEXTURE.value
                ? R_TextureReleaseReference( record.info.depth ) : render_error_t::OK;
            if ( firstError == render_error_t::OK ) {
                firstError = destroyResult != render_error_t::OK ? destroyResult
                    : releaseResult != render_error_t::OK ? releaseResult : depthReleaseResult;
            }
            return common::CY_TRUE;
        } );
    common::HandleTable_Shutdown( &targetSystem.records );
    targetSystem.initialized = false;
    return firstError;
}

render_error_t R_CreateRenderTarget( const render_target_desc_t &description,
    render_target_handle_t *targetOut ) noexcept
{
    if ( targetOut == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    *targetOut = R_INVALID_TARGET;
    if ( !tr.initialized || !targetSystem.initialized || tr.backend.api == nullptr ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    if ( tr.frameActive ) return render_error_t::ERR_COMMAND_STATE_INVALID;
    if ( tr.backend.api->CreateRenderTarget == nullptr ||
         tr.backend.api->DestroyRenderTarget == nullptr ) {
        return render_error_t::ERR_UNSUPPORTED;
    }

    const bool hasColor = description.color.value != R_INVALID_TEXTURE.value;
    const bool hasDepth = description.depth.value != R_INVALID_TEXTURE.value;
    if ( !hasColor && !hasDepth ) return render_error_t::ERR_INVALID_HANDLE;

    backend_render_target_desc_t nativeDescription{};
    render_texture_info_t colorInfo{};
    if ( hasColor ) {
        const render_error_t acquireResult = R_TextureAcquireReference(
            description.color, &nativeDescription.color, &colorInfo );
        if ( acquireResult != render_error_t::OK ) return acquireResult;
        if ( ( colorInfo.format != render_texture_format_t::RGBA8 &&
               colorInfo.format != render_texture_format_t::RGBA16F ) ||
             !colorInfo.renderTarget || colorInfo.sRGB || colorInfo.generateMips ||
             colorInfo.repeat || colorInfo.width == 0u || colorInfo.height == 0u ) {
            (void)R_TextureReleaseReference( description.color );
            return render_error_t::ERR_RENDER_TARGET_INVALID;
        }
        nativeDescription.extent = { colorInfo.width, colorInfo.height };
        nativeDescription.colorFormat = colorInfo.format;
    }
    if ( hasDepth ) {
        render_texture_info_t depthInfo{};
        const render_error_t depthResult = R_TextureAcquireReference(
            description.depth, &nativeDescription.depth, &depthInfo );
        if ( depthResult != render_error_t::OK ) {
            if ( hasColor ) (void)R_TextureReleaseReference( description.color );
            return depthResult;
        }
        if ( depthInfo.format != render_texture_format_t::D24 ||
             !depthInfo.renderTarget || depthInfo.sRGB || depthInfo.generateMips ||
             depthInfo.repeat || depthInfo.width == 0u || depthInfo.height == 0u ||
             ( hasColor && ( depthInfo.width != colorInfo.width ||
                             depthInfo.height != colorInfo.height ) ) ) {
            (void)R_TextureReleaseReference( description.depth );
            if ( hasColor ) (void)R_TextureReleaseReference( description.color );
            return render_error_t::ERR_RENDER_TARGET_INVALID;
        }
        if ( !hasColor ) nativeDescription.extent = { depthInfo.width, depthInfo.height };
    }
    if ( nativeDescription.extent.width > tr.info.limits.maxViewportWidth ||
         nativeDescription.extent.height > tr.info.limits.maxViewportHeight ||
         nativeDescription.extent.width > static_cast<common::u32>( std::numeric_limits<common::i32>::max() ) ||
         nativeDescription.extent.height > static_cast<common::u32>( std::numeric_limits<common::i32>::max() ) ) {
        if ( hasDepth ) (void)R_TextureReleaseReference( description.depth );
        if ( hasColor ) (void)R_TextureReleaseReference( description.color );
        return render_error_t::ERR_RESOURCE_SIZE_UNSUPPORTED;
    }
    target_record_t record{};
    const render_error_t nativeResult = tr.backend.api->CreateRenderTarget(
        nativeDescription, record.native, tr.backend.state );
    if ( nativeResult != render_error_t::OK || record.native.value == 0u ) {
        if ( record.native.value != 0u ) {
            (void)tr.backend.api->DestroyRenderTarget( record.native, tr.backend.state );
        }
        if ( hasDepth ) (void)R_TextureReleaseReference( description.depth );
        if ( hasColor ) (void)R_TextureReleaseReference( description.color );
        return nativeResult != render_error_t::OK ? nativeResult : render_error_t::ERR_INTERNAL_ERROR;
    }
    record.info = { description.color, nativeDescription.extent, description.depth };
    record.colorFormat = nativeDescription.colorFormat;
    const common::handle32_t slot = common::HandleTable_Insert( &targetSystem.records, record );
    if ( !common::Cy_Handle32IsValid( slot ) ) {
        (void)tr.backend.api->DestroyRenderTarget( record.native, tr.backend.state );
        if ( hasDepth ) (void)R_TextureReleaseReference( description.depth );
        if ( hasColor ) (void)R_TextureReleaseReference( description.color );
        return common::HandleTable_Count( &targetSystem.records ) >= common::CY_HANDLE_TABLE_MAX_CAPACITY
            ? render_error_t::ERR_LIMIT_EXCEEDED : render_error_t::ERR_OUT_OF_MEMORY;
    }
    *targetOut = common::Cy_Handle64Make( common::Cy_Handle32Index( slot ),
        common::Cy_Handle32Generation( slot ),
        static_cast<common::u32>( render_object_type_t::RENDER_TARGET ) );
    if ( !common::Cy_Handle64IsValid( *targetOut ) ) {
        (void)common::HandleTable_Remove( &targetSystem.records, slot );
        (void)tr.backend.api->DestroyRenderTarget( record.native, tr.backend.state );
        if ( hasDepth ) (void)R_TextureReleaseReference( description.depth );
        if ( hasColor ) (void)R_TextureReleaseReference( description.color );
        return render_error_t::ERR_INTERNAL_ERROR;
    }
    return render_error_t::OK;
}

render_error_t R_DestroyRenderTarget( render_target_handle_t target ) noexcept
{
    if ( !tr.initialized || !targetSystem.initialized || tr.backend.api == nullptr ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    if ( tr.frameActive ) return render_error_t::ERR_COMMAND_STATE_INVALID;
    common::handle32_t slot{};
    target_record_t *record = nullptr;
    const render_error_t resolved = ResolveTarget( target, slot, record );
    if ( resolved != render_error_t::OK ) return resolved;
    const render_error_t nativeResult = tr.backend.api->DestroyRenderTarget(
        record->native, tr.backend.state );
    if ( nativeResult != render_error_t::OK ) return nativeResult;
    const render_error_t releaseResult = record->info.color.value != R_INVALID_TEXTURE.value
        ? R_TextureReleaseReference( record->info.color ) : render_error_t::OK;
    const render_error_t depthReleaseResult = record->info.depth.value != R_INVALID_TEXTURE.value
        ? R_TextureReleaseReference( record->info.depth ) : render_error_t::OK;
    if ( !common::HandleTable_Remove( &targetSystem.records, slot ) ) {
        return render_error_t::ERR_INTERNAL_ERROR;
    }
    return releaseResult != render_error_t::OK ? releaseResult : depthReleaseResult;
}

bool R_IsRenderTargetValid( render_target_handle_t target ) noexcept
{
    if ( !tr.initialized || !targetSystem.initialized ) return false;
    common::handle32_t slot{};
    target_record_t *record = nullptr;
    return ResolveTarget( target, slot, record ) == render_error_t::OK;
}

render_error_t R_GetRenderTargetInfo( render_target_handle_t target,
    render_target_info_t *infoOut ) noexcept
{
    if ( infoOut == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    *infoOut = {};
    if ( !tr.initialized || !targetSystem.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    common::handle32_t slot{};
    target_record_t *record = nullptr;
    const render_error_t resolved = ResolveTarget( target, slot, record );
    if ( resolved == render_error_t::OK ) *infoOut = record->info;
    return resolved;
}

render_error_t R_BeginTargetPass( const render_target_pass_desc_t &description ) noexcept
{
    if ( !tr.initialized || !targetSystem.initialized || tr.backend.api == nullptr ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    if ( !tr.frameActive ) return render_error_t::ERR_FRAME_NOT_ACTIVE;
    if ( tr.activeTarget.value != R_INVALID_TARGET.value ) {
        return render_error_t::ERR_COMMAND_STATE_INVALID;
    }
    if ( !ValidPassClear( description ) ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( tr.backend.api->BeginTargetPass == nullptr ) return render_error_t::ERR_UNSUPPORTED;
    common::handle32_t slot{};
    target_record_t *record = nullptr;
    const render_error_t resolved = ResolveTarget( description.target, slot, record );
    if ( resolved != render_error_t::OK ) return resolved;
    if ( record->info.color.value == R_INVALID_TEXTURE.value &&
         ( description.clearFlags & R_CLEAR_COLOR ) != 0u ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    const backend_target_pass_desc_t native{ record->native, record->info.extent,
        description.clearFlags, description.clearColor, description.clearDepth };
    const render_error_t result = tr.backend.api->BeginTargetPass( native, tr.backend.state );
    if ( result == render_error_t::OK ) {
        tr.activeTarget = description.target;
        tr.activeTargetColor = record->info.color;
        tr.activeTargetDepth = record->info.depth;
    }
    return result;
}

render_error_t R_EndTargetPass() noexcept
{
    if ( !tr.initialized || !targetSystem.initialized || tr.backend.api == nullptr ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    if ( !tr.frameActive ) return render_error_t::ERR_FRAME_NOT_ACTIVE;
    if ( tr.activeTarget.value == R_INVALID_TARGET.value ) {
        return render_error_t::ERR_COMMAND_STATE_INVALID;
    }
    const render_error_t result = tr.backend.api->EndTargetPass( tr.backend.state );
    // Backends must restore the presentation target even on failure so the
    // caller can still close the frame and report the original error.
    tr.activeTarget = R_INVALID_TARGET;
    tr.activeTargetColor = R_INVALID_TEXTURE;
    tr.activeTargetDepth = R_INVALID_TEXTURE;
    return result;
}

render_error_t R_ReadTargetRGBA8( const render_target_handle_t target,
    const render_readback_rect_t &rect, common::byte *pixelsOut,
    const common::u64 byteCapacity, const common::u32 rowPitchBytes ) noexcept
{
    if ( !tr.initialized || !targetSystem.initialized || tr.backend.api == nullptr ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    if ( !tr.frameActive ) return render_error_t::ERR_FRAME_NOT_ACTIVE;
    if ( tr.activeTarget.value != R_INVALID_TARGET.value ) {
        return render_error_t::ERR_COMMAND_STATE_INVALID;
    }

    common::handle32_t slot{};
    target_record_t *record = nullptr;
    const render_error_t resolved = ResolveTarget( target, slot, record );
    if ( resolved != render_error_t::OK ) return resolved;
    if ( record->info.color.value == R_INVALID_TEXTURE.value ||
         record->colorFormat != render_texture_format_t::RGBA8 ) {
        return render_error_t::ERR_CAPTURE_UNSUPPORTED;
    }

    const render_extent_t extent = record->info.extent;
    if ( pixelsOut == nullptr || rect.width == 0u || rect.height == 0u ||
         rect.x >= extent.width || rect.y >= extent.height ||
         rect.width > extent.width - rect.x ||
         rect.height > extent.height - rect.y ||
         ( rowPitchBytes & 3u ) != 0u ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    constexpr common::u64 bytesPerPixel = 4u;
    const common::u64 rowBytes = static_cast<common::u64>( rect.width ) * bytesPerPixel;
    if ( rowBytes > rowPitchBytes ) return render_error_t::ERR_INVALID_ARGUMENT;
    const common::u64 rowStart = static_cast<common::u64>( rect.height - 1u ) * rowPitchBytes;
    if ( rowStart > std::numeric_limits<common::u64>::max() - rowBytes ||
         rowStart + rowBytes > std::numeric_limits<common::usize>::max() ||
         byteCapacity < rowStart + rowBytes ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( tr.backend.api->ReadTargetRGBA8 == nullptr ) {
        return render_error_t::ERR_CAPTURE_UNSUPPORTED;
    }

    const backend_target_readback_desc_t native{ record->native, rect,
        pixelsOut, byteCapacity, rowPitchBytes };
    return tr.backend.api->ReadTargetRGBA8( native, tr.backend.state );
}
} // namespace cypher::engine::render
