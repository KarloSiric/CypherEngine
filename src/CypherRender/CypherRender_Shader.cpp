//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_Shader.cpp
//  Purpose: Owns runtime shader handles and copied cooked-program metadata.
//  Details: Pipelines retain shader records. Cooked views are borrowed only
//           during creation; the selected backend owns the linked program.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_Shader.h"
#include "CypherRender_Local.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherCommon/Tier1/CypherCommon_HandleTable.h"
#include "CypherCommon/Tier1/CypherCommon_Unicode.h"

#include <cstring>

namespace cypher::engine::render
{
namespace common = ::cypher::common;

namespace
{

inline constexpr common::usize R_INITIAL_SHADER_CAPACITY = 64u;

struct shader_record_t {
    backend_shader_t native{};
    render_shader_info_t info{};
    common::u32 referenceCount{ 0u };
};

struct shader_system_t {
    common::handle_table_t<shader_record_t> records{};
    bool initialized{ false };
};

shader_system_t shaderSystem{};

render_error_t R_ResolveShader(
    const render_shader_handle_t shader,
    common::handle32_t &tableHandleOut,
    shader_record_t *&recordOut ) noexcept
{
    tableHandleOut = common::CY_HANDLE32_INVALID;
    recordOut = nullptr;
    if ( !common::Cy_Handle64IsValid( shader ) ) return render_error_t::ERR_INVALID_HANDLE;
    if ( common::Cy_Handle64Type( shader ) !=
         static_cast<common::u32>( render_object_type_t::SHADER ) ) {
        return render_error_t::ERR_RESOURCE_TYPE_MISMATCH;
    }
    const common::u32 index = common::Cy_Handle64Index( shader );
    const common::u32 generation = common::Cy_Handle64Generation( shader );
    if ( index > common::CY_HANDLE32_INDEX_MAX || generation == 0u ||
         !common::Cy_Handle32TryMake( index, generation, &tableHandleOut ) ) {
        return render_error_t::ERR_INVALID_HANDLE;
    }
    recordOut = common::HandleTable_Get( &shaderSystem.records, tableHandleOut );
    return recordOut != nullptr ? render_error_t::OK : render_error_t::ERR_STALE_HANDLE;
}

} // namespace

render_error_t R_ShaderSystemInit() noexcept
{
    if ( shaderSystem.initialized ) return render_error_t::ERR_ALREADY_INITIALIZED;
    if ( !common::HandleTable_Init(
            &shaderSystem.records, common::Allocator_GetSystem(), R_INITIAL_SHADER_CAPACITY ) ) {
        return render_error_t::ERR_OUT_OF_MEMORY;
    }
    shaderSystem.initialized = true;
    return render_error_t::OK;
}

render_error_t R_ShaderSystemShutdown() noexcept
{
    if ( !shaderSystem.initialized ) return render_error_t::OK;
    render_error_t firstError = render_error_t::OK;
    if ( tr.backend == nullptr && !common::HandleTable_IsEmpty( &shaderSystem.records ) ) {
        firstError = render_error_t::ERR_INTERNAL_ERROR;
    } else if ( tr.backend != nullptr ) {
        (void)common::HandleTable_ForEach(
            &shaderSystem.records,
            [&]( common::handle32_t, shader_record_t &record ) noexcept {
                const render_error_t result = tr.backend->DestroyShader( record.native, tr.backend->state );
                if ( firstError == render_error_t::OK && result != render_error_t::OK ) firstError = result;
                record.native = R_INVALID_BACKEND_SHADER;
                return common::CY_TRUE;
            } );
    }
    common::HandleTable_Shutdown( &shaderSystem.records );
    shaderSystem.initialized = false;
    return firstError;
}

render_error_t R_ValidateShaderDesc( const render_shader_desc_t &description ) noexcept
{
    if ( !tr.initialized || tr.backend == nullptr || !shaderSystem.initialized ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    if ( description.cookedShader == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    const common::cooked_shader_view_t &cooked = *description.cookedShader;
    if ( cooked.backend != common::render_shader_backend_t::OPENGL ||
         tr.info.backend != render_backend_t::OPENGL ||
         cooked.kind != common::render_shader_program_kind_t::GRAPHICS ||
         cooked.flags != common::COOKED_SHADER_FLAG_NONE ||
         !common::CookedShader_SupportsLanguage( cooked.languageProfile, cooked.nLanguageVersion ) ||
         cooked.nStages != common::CY_COOKED_SHADER_MAX_STAGES ) {
        return render_error_t::ERR_SHADER_DATA_INVALID;
    }

    // The desktop core versions accepted by CYSH correspond to GL 3.3/4.x.
    const common::u32 supportedVersion = static_cast<common::u32>( tr.info.apiMajorVersion ) * 100u +
        static_cast<common::u32>( tr.info.apiMinorVersion ) * 10u;
    if ( cooked.nLanguageVersion > supportedVersion ) {
        return render_error_t::ERR_BACKEND_VERSION_UNSUPPORTED;
    }

    common::u32 stageMask = 0u;
    for ( common::u32 index = 0u; index < cooked.nStages; ++index ) {
        const common::cooked_shader_stage_view_t &stage = cooked.stages[index];
        common::u32 stageBit = 0u;
        switch ( stage.stage ) {
            case common::render_shader_stage_t::VERTEX: stageBit = 1u; break;
            case common::render_shader_stage_t::FRAGMENT: stageBit = 2u; break;
            default: return render_error_t::ERR_SHADER_STAGE_UNSUPPORTED;
        }
        if ( ( stageMask & stageBit ) != 0u ||
             stage.codeFormat != common::render_shader_code_format_t::GLSL_UTF8 ||
             stage.flags != common::COOKED_SHADER_STAGE_FLAG_NONE ||
             stage.code.pData == nullptr || stage.code.cbSize <= 1u ||
             stage.code.cbSize > common::CY_COOKED_SHADER_MAX_CODE_SIZE ) {
            return render_error_t::ERR_SHADER_DATA_INVALID;
        }
        stageMask |= stageBit;
        const common::usize sourceLength = stage.code.cbSize - 1u;
        if ( stage.code.pData[sourceLength] != static_cast<common::byte>( '\0' ) ||
             std::memchr( stage.code.pData, '\0', sourceLength ) != nullptr ||
             common::Unicode_ValidateUtf8( {
                 reinterpret_cast<const char *>( stage.code.pData ), sourceLength
             } ).status != common::unicode_status_t::OK ) {
            return render_error_t::ERR_SHADER_DATA_INVALID;
        }
    }
    return stageMask == 3u ? render_error_t::OK : render_error_t::ERR_SHADER_DATA_INVALID;
}

render_error_t R_CreateShader(
    const render_shader_desc_t &description, render_shader_handle_t *shaderOut ) noexcept
{
    if ( shaderOut == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    *shaderOut = R_INVALID_SHADER;
    const render_error_t validation = R_ValidateShaderDesc( description );
    if ( validation != render_error_t::OK ) return validation;

    shader_record_t record{};
    const render_error_t result = tr.backend->CreateShader( description, record.native, tr.backend->state );
    if ( result != render_error_t::OK ) return result;
    if ( !R_IsBackendShaderValid( record.native ) ) return render_error_t::ERR_INTERNAL_ERROR;
    const common::cooked_shader_view_t &cooked = *description.cookedShader;
    record.info = { cooked.backend, cooked.kind, cooked.languageProfile,
                    cooked.nLanguageVersion, cooked.flags, cooked.sourceHash, cooked.nStages };

    const common::handle32_t tableHandle = common::HandleTable_Insert( &shaderSystem.records, record );
    if ( !common::Cy_Handle32IsValid( tableHandle ) ) {
        (void)tr.backend->DestroyShader( record.native, tr.backend->state );
        return common::HandleTable_Count( &shaderSystem.records ) >= common::CY_HANDLE_TABLE_MAX_CAPACITY
            ? render_error_t::ERR_LIMIT_EXCEEDED : render_error_t::ERR_OUT_OF_MEMORY;
    }
    render_shader_handle_t publicHandle{};
    if ( !common::Cy_Handle64TryMake(
            common::Cy_Handle32Index( tableHandle ), common::Cy_Handle32Generation( tableHandle ),
            static_cast<common::u32>( render_object_type_t::SHADER ), &publicHandle ) ) {
        (void)common::HandleTable_Remove( &shaderSystem.records, tableHandle );
        (void)tr.backend->DestroyShader( record.native, tr.backend->state );
        return render_error_t::ERR_INTERNAL_ERROR;
    }
    *shaderOut = publicHandle;
    return render_error_t::OK;
}

render_error_t R_DestroyShader( const render_shader_handle_t shader ) noexcept
{
    if ( !tr.initialized || tr.backend == nullptr || !shaderSystem.initialized ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    common::handle32_t tableHandle{};
    shader_record_t *record = nullptr;
    const render_error_t resolve = R_ResolveShader( shader, tableHandle, record );
    if ( resolve != render_error_t::OK ) return resolve;
    if ( record->referenceCount != 0u ) return render_error_t::ERR_RESOURCE_BUSY;
    const render_error_t result = tr.backend->DestroyShader( record->native, tr.backend->state );
    if ( result != render_error_t::OK ) return result;
    return common::HandleTable_Remove( &shaderSystem.records, tableHandle )
        ? render_error_t::OK : render_error_t::ERR_INTERNAL_ERROR;
}

bool R_IsShaderValid( const render_shader_handle_t shader ) noexcept
{
    if ( !tr.initialized || !shaderSystem.initialized ) return false;
    common::handle32_t tableHandle{};
    shader_record_t *record = nullptr;
    return R_ResolveShader( shader, tableHandle, record ) == render_error_t::OK;
}

render_error_t R_GetShaderInfo(
    const render_shader_handle_t shader, render_shader_info_t *infoOut ) noexcept
{
    if ( infoOut == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    *infoOut = {};
    if ( !tr.initialized || !shaderSystem.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    common::handle32_t tableHandle{};
    shader_record_t *record = nullptr;
    const render_error_t resolve = R_ResolveShader( shader, tableHandle, record );
    if ( resolve != render_error_t::OK ) return resolve;
    *infoOut = record->info;
    return render_error_t::OK;
}

render_error_t R_ShaderAcquireReference(
    const render_shader_handle_t shader, backend_shader_t *nativeOut ) noexcept
{
    if ( nativeOut == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    *nativeOut = R_INVALID_BACKEND_SHADER;
    if ( !tr.initialized || !shaderSystem.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    common::handle32_t tableHandle{};
    shader_record_t *record = nullptr;
    const render_error_t resolve = R_ResolveShader( shader, tableHandle, record );
    if ( resolve != render_error_t::OK ) return resolve;
    if ( record->referenceCount == common::CY_U32_MAX ) return render_error_t::ERR_LIMIT_EXCEEDED;
    ++record->referenceCount;
    *nativeOut = record->native;
    return render_error_t::OK;
}

render_error_t R_ShaderReleaseReference( const render_shader_handle_t shader ) noexcept
{
    if ( !tr.initialized || !shaderSystem.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    common::handle32_t tableHandle{};
    shader_record_t *record = nullptr;
    const render_error_t resolve = R_ResolveShader( shader, tableHandle, record );
    if ( resolve != render_error_t::OK ) return resolve;
    if ( record->referenceCount == 0u ) return render_error_t::ERR_INVALID_STATE;
    --record->referenceCount;
    return render_error_t::OK;
}

} // namespace cypher::engine::render
