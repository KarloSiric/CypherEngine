//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_Backend.cpp
//  Purpose: Validates backend tables and resolves configured implementations.
//  Details: Backend selection is centralized so Host and the public frontend
//           never accumulate backend-specific conditionals.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_Backend.h"

#include "OpenGL/CypherRender_OpenGL.h"

namespace cypher::engine::render
{

bool R_IsBackendValid( const backend_api_t *backend ) noexcept
{
    if ( backend == nullptr ||
         backend->apiVersion != R_BACKEND_API_VERSION ||
         backend->structSize != R_BACKEND_API_SIZE ||
         backend->backend == render_backend_t::AUTO ||
         backend->backend >= render_backend_t::COUNT ||
         backend->name == nullptr || backend->name[0] == '\0' ) {
        return false;
    }

    // Every callback below is part of the current private contract. A backend is
    // rejected as one unit rather than failing later through a null call.
    return backend->ConfigureWindow != nullptr &&
        backend->Init != nullptr &&
        backend->Shutdown != nullptr &&
        backend->BeginFrame != nullptr &&
        backend->Resize != nullptr &&
        backend->EndFrame != nullptr &&
        backend->SetPresentMode != nullptr &&
        backend->WaitIdle != nullptr &&
        backend->CreateBuffer != nullptr &&
        backend->UpdateBuffer != nullptr &&
        backend->MapBuffer != nullptr &&
        backend->FlushMappedBuffer != nullptr &&
        backend->InvalidateMappedBuffer != nullptr &&
        backend->UnmapBuffer != nullptr &&
        backend->DestroyBuffer != nullptr &&
        backend->state != nullptr;
}

render_error_t R_SelectBackend(
    const render_config_t &config,
    const backend_api_t **backendOut ) noexcept
{
    if ( backendOut == nullptr ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    *backendOut = nullptr;

    const backend_api_t *backend = nullptr;
    switch ( config.backend ) {
        case render_backend_t::AUTO:
        case render_backend_t::OPENGL:
            backend = GL_GetBackendAPI();
            break;

        case render_backend_t::SOFTWARE:
        case render_backend_t::VULKAN:
            return render_error_t::ERR_BACKEND_NOT_BUILT;

        case render_backend_t::COUNT:
        default:
            return render_error_t::ERR_INVALID_ARGUMENT;
    }

    if ( !R_IsBackendValid( backend ) ) {
        return render_error_t::ERR_API_VERSION_MISMATCH;
    }

    *backendOut = backend;
    return render_error_t::OK;
}

} // namespace cypher::engine::render
