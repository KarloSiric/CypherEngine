//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Owns validated immutable sampler objects and generational handles.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////
#include "CypherRender_Sampler.h"
#include "CypherRender_Local.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherCommon/Tier1/CypherCommon_HandleTable.h"

namespace cypher::engine::render
{
namespace common = ::cypher::common;
namespace
{
struct sampler_record_t {
    backend_sampler_t native{};
    render_sampler_info_t info{};
};
struct sampler_system_t {
    common::handle_table_t<sampler_record_t> records{};
    bool initialized{ false };
};
sampler_system_t samplerSystem{};

render_error_t ResolveSampler( render_sampler_handle_t sampler,
    common::handle32_t &tableHandle, sampler_record_t *&record ) noexcept
{
    tableHandle = common::CY_HANDLE32_INVALID;
    record = nullptr;
    if ( !common::Cy_Handle64IsValid( sampler ) ) return render_error_t::ERR_INVALID_HANDLE;
    if ( common::Cy_Handle64Type( sampler ) != static_cast<common::u32>( render_object_type_t::SAMPLER ) ) {
        return render_error_t::ERR_RESOURCE_TYPE_MISMATCH;
    }
    if ( !common::Cy_Handle32TryMake( common::Cy_Handle64Index( sampler ),
             common::Cy_Handle64Generation( sampler ), &tableHandle ) ) {
        return render_error_t::ERR_INVALID_HANDLE;
    }
    record = common::HandleTable_Get( &samplerSystem.records, tableHandle );
    return record != nullptr ? render_error_t::OK : render_error_t::ERR_STALE_HANDLE;
}

bool ValidSamplerDescription( const render_sampler_desc_t &description ) noexcept
{
    return description.minFilter < render_sampler_filter_t::COUNT &&
        description.magFilter < render_sampler_filter_t::COUNT &&
        description.mipFilter < render_sampler_mip_filter_t::COUNT &&
        description.addressU < render_sampler_address_t::COUNT &&
        description.addressV < render_sampler_address_t::COUNT;
}
} // namespace

render_error_t R_SamplerSystemInit() noexcept
{
    if ( samplerSystem.initialized ) return render_error_t::ERR_ALREADY_INITIALIZED;
    if ( !common::HandleTable_Init( &samplerSystem.records, common::Allocator_GetSystem(), 64u ) ) {
        return render_error_t::ERR_OUT_OF_MEMORY;
    }
    samplerSystem.initialized = true;
    return render_error_t::OK;
}

render_error_t R_SamplerSystemShutdown() noexcept
{
    if ( !samplerSystem.initialized ) return render_error_t::OK;
    render_error_t firstError = render_error_t::OK;
    (void)common::HandleTable_ForEach( &samplerSystem.records,
        [&]( common::handle32_t, sampler_record_t &record ) noexcept {
            const auto result = tr.backend.api != nullptr && tr.backend.api->DestroySampler != nullptr
                ? tr.backend.api->DestroySampler( record.native, tr.backend.state )
                : render_error_t::ERR_INTERNAL_ERROR;
            if ( firstError == render_error_t::OK ) firstError = result;
            return common::CY_TRUE;
        } );
    common::HandleTable_Shutdown( &samplerSystem.records );
    samplerSystem.initialized = false;
    return firstError;
}

render_error_t R_CreateSampler( const render_sampler_desc_t &description,
    render_sampler_handle_t *samplerOut ) noexcept
{
    if ( samplerOut == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    *samplerOut = R_INVALID_SAMPLER;
    if ( !tr.initialized || !samplerSystem.initialized || tr.backend.api == nullptr ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    if ( tr.backend.api->CreateSampler == nullptr || tr.backend.api->DestroySampler == nullptr ) {
        return render_error_t::ERR_UNSUPPORTED;
    }
    if ( !ValidSamplerDescription( description ) ) return render_error_t::ERR_INVALID_ARGUMENT;

    sampler_record_t record{};
    const auto result = tr.backend.api->CreateSampler( description, record.native, tr.backend.state );
    if ( result != render_error_t::OK || record.native.value == 0u ) {
        if ( record.native.value != 0u ) {
            (void)tr.backend.api->DestroySampler( record.native, tr.backend.state );
        }
        return result != render_error_t::OK ? result : render_error_t::ERR_INTERNAL_ERROR;
    }
    record.info = { description.minFilter, description.magFilter, description.mipFilter,
        description.addressU, description.addressV };
    const auto slot = common::HandleTable_Insert( &samplerSystem.records, record );
    if ( !common::Cy_Handle32IsValid( slot ) ) {
        (void)tr.backend.api->DestroySampler( record.native, tr.backend.state );
        return common::HandleTable_Count( &samplerSystem.records ) >= common::CY_HANDLE_TABLE_MAX_CAPACITY
            ? render_error_t::ERR_LIMIT_EXCEEDED : render_error_t::ERR_OUT_OF_MEMORY;
    }
    *samplerOut = common::Cy_Handle64Make( common::Cy_Handle32Index( slot ),
        common::Cy_Handle32Generation( slot ), static_cast<common::u32>( render_object_type_t::SAMPLER ) );
    return render_error_t::OK;
}

render_error_t R_DestroySampler( render_sampler_handle_t sampler ) noexcept
{
    if ( !tr.initialized || !samplerSystem.initialized || tr.backend.api == nullptr ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    common::handle32_t slot{};
    sampler_record_t *record = nullptr;
    const auto resolved = ResolveSampler( sampler, slot, record );
    if ( resolved != render_error_t::OK ) return resolved;
    const auto result = tr.backend.api->DestroySampler( record->native, tr.backend.state );
    if ( result != render_error_t::OK ) return result;
    return common::HandleTable_Remove( &samplerSystem.records, slot )
        ? render_error_t::OK : render_error_t::ERR_INTERNAL_ERROR;
}

bool R_IsSamplerValid( render_sampler_handle_t sampler ) noexcept
{
    if ( !tr.initialized || !samplerSystem.initialized ) return false;
    common::handle32_t slot{};
    sampler_record_t *record = nullptr;
    return ResolveSampler( sampler, slot, record ) == render_error_t::OK;
}

render_error_t R_GetSamplerInfo( render_sampler_handle_t sampler,
    render_sampler_info_t *infoOut ) noexcept
{
    if ( infoOut == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    *infoOut = {};
    if ( !tr.initialized || !samplerSystem.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    common::handle32_t slot{};
    sampler_record_t *record = nullptr;
    const auto result = ResolveSampler( sampler, slot, record );
    if ( result == render_error_t::OK ) *infoOut = record->info;
    return result;
}

render_error_t R_SamplerResolveForDraw( render_sampler_handle_t sampler,
    backend_sampler_t *nativeOut, render_sampler_info_t *infoOut ) noexcept
{
    if ( nativeOut == nullptr || infoOut == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    *nativeOut = {};
    *infoOut = {};
    if ( !tr.initialized || !samplerSystem.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    common::handle32_t slot{};
    sampler_record_t *record = nullptr;
    const auto result = ResolveSampler( sampler, slot, record );
    if ( result != render_error_t::OK ) return result;
    *nativeOut = record->native;
    *infoOut = record->info;
    return render_error_t::OK;
}
} // namespace cypher::engine::render
