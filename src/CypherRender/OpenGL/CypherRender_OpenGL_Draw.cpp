//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Validates linked shader interfaces and submits indexed geometry.
// Details: A native pipeline is an owned state record, not a GL program-pipeline
//          object. The frontend retains its program until this record is freed.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_OpenGL_Draw.h"
#include "CypherRender_OpenGL_Local.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>

namespace cypher::engine::render
{
namespace common = ::cypher::common;
namespace
{
struct gl_pipeline_record_t {
    gl_pipeline_record_t *next{ nullptr };
    GLuint program{ 0u };
    common::u32 uniformBlockCount{ 0u };
    common::u32 uniformBlockBytes[R_MAX_UNIFORM_BLOCK_BINDINGS]{};
    GLuint uniformBlockIndices[R_MAX_UNIFORM_BLOCK_BINDINGS]{};
    common::u32 sampledTextureCount{ 0u };
    GLint sampledTextureLocations[R_MAX_SAMPLED_TEXTURE_BINDINGS]{};
    bool depthTest{ true };
    bool depthWrite{ true };
    GLenum depthCompare{ GL_LESS };
    bool colorWrite{ true };
    bool cullBackFaces{ true };
    bool frontCounterClockwise{ true };
    bool alphaBlend{ false };
    render_primitive_topology_t topology{ render_primitive_topology_t::TRIANGLE_LIST };
    GLenum polygonMode{ GL_FILL };
};

// Tokens are backend-private addresses. Look them up before dereferencing so a
// malformed token cannot become an unchecked pointer. Public generations live
// in the frontend table; this list owns the native records only.
gl_pipeline_record_t *glPipelines = nullptr;

common::u64 GL_PipelineToken( const gl_pipeline_record_t *record ) noexcept
{
    return static_cast<common::u64>( reinterpret_cast<std::uintptr_t>( record ) );
}

gl_pipeline_record_t *GL_FindPipeline( const backend_pipeline_t pipeline ) noexcept
{
    for ( gl_pipeline_record_t *record = glPipelines; record != nullptr; record = record->next ) {
        if ( GL_PipelineToken( record ) == pipeline.value ) return record;
    }
    return nullptr;
}

bool GL_AttributeType(
    const GLenum type, render_vertex_value_t &valueOut, common::u8 &componentsOut ) noexcept
{
    valueOut = render_vertex_value_t::FLOAT;
    switch ( type ) {
        case GL_FLOAT: componentsOut = 1u; return true;
        case GL_FLOAT_VEC2: componentsOut = 2u; return true;
        case GL_FLOAT_VEC3: componentsOut = 3u; return true;
        case GL_FLOAT_VEC4: componentsOut = 4u; return true;
        case GL_INT: valueOut = render_vertex_value_t::SINT; componentsOut = 1u; return true;
        case GL_INT_VEC2: valueOut = render_vertex_value_t::SINT; componentsOut = 2u; return true;
        case GL_INT_VEC3: valueOut = render_vertex_value_t::SINT; componentsOut = 3u; return true;
        case GL_INT_VEC4: valueOut = render_vertex_value_t::SINT; componentsOut = 4u; return true;
        case GL_UNSIGNED_INT: valueOut = render_vertex_value_t::UINT; componentsOut = 1u; return true;
        case GL_UNSIGNED_INT_VEC2: valueOut = render_vertex_value_t::UINT; componentsOut = 2u; return true;
        case GL_UNSIGNED_INT_VEC3: valueOut = render_vertex_value_t::UINT; componentsOut = 3u; return true;
        case GL_UNSIGNED_INT_VEC4: valueOut = render_vertex_value_t::UINT; componentsOut = 4u; return true;
        default: return false; // Matrices, arrays, and double attributes are outside this contract.
    }
}

GLenum GL_DepthCompare( const render_depth_compare_t compare ) noexcept
{
    switch ( compare ) {
        case render_depth_compare_t::NEVER: return GL_NEVER;
        case render_depth_compare_t::LESS: return GL_LESS;
        case render_depth_compare_t::EQUAL: return GL_EQUAL;
        case render_depth_compare_t::LESS_EQUAL: return GL_LEQUAL;
        case render_depth_compare_t::GREATER: return GL_GREATER;
        case render_depth_compare_t::NOT_EQUAL: return GL_NOTEQUAL;
        case render_depth_compare_t::GREATER_EQUAL: return GL_GEQUAL;
        case render_depth_compare_t::ALWAYS: return GL_ALWAYS;
        default: return 0u;
    }
}

GLenum GL_PolygonMode( const render_polygon_mode_t mode ) noexcept
{
    switch ( mode ) {
        case render_polygon_mode_t::FILL: return GL_FILL;
        case render_polygon_mode_t::LINE: return GL_LINE;
        default: return 0u;
    }
}

struct attribute_name_storage_t {
    char *bytes{ nullptr };
    common::usize size{ 0u };
    ~attribute_name_storage_t()
    {
        common::Allocator_Free( common::Allocator_GetSystem(), bytes, size, alignof( char ) );
    }
};

render_error_t GL_ValidatePipelineAttributes(
    const GLuint program, const render_vertex_layout_t &layout ) noexcept
{
    GLint attributeCount = 0;
    GLint nameCapacity = 0;
    glGetProgramiv( program, GL_ACTIVE_ATTRIBUTES, &attributeCount );
    glGetProgramiv( program, GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &nameCapacity );
    render_error_t result = GL_CheckErrors( glState.config.validation );
    if ( result != render_error_t::OK ) return result;
    if ( attributeCount == 0 ) return render_error_t::OK;
    if ( attributeCount < 0 || nameCapacity <= 0 ) return render_error_t::ERR_SHADER_INTERFACE_MISMATCH;
    attribute_name_storage_t name{};
    name.size = static_cast<common::usize>( nameCapacity );
    name.bytes = static_cast<char *>( common::Allocator_Allocate(
        common::Allocator_GetSystem(), name.size, alignof( char ) ) );
    if ( name.bytes == nullptr ) return render_error_t::ERR_OUT_OF_MEMORY;

    for ( GLint i = 0; i < attributeCount; ++i ) {
        GLenum type = 0u;
        GLint arraySize = 0;
        GLsizei nameLength = 0;
        glGetActiveAttrib( program, static_cast<GLuint>( i ), nameCapacity,
                           &nameLength, &arraySize, &type, name.bytes );
        result = GL_CheckErrors( glState.config.validation );
        if ( result != render_error_t::OK ) return result;
        if ( nameLength <= 0 || nameLength >= nameCapacity || arraySize != 1 ) {
            return render_error_t::ERR_SHADER_INTERFACE_MISMATCH;
        }
        const GLint location = glGetAttribLocation( program, name.bytes );
        result = GL_CheckErrors( glState.config.validation );
        if ( result != render_error_t::OK ) return result;
        render_vertex_value_t value{};
        common::u8 components = 0u;
        if ( location < 0 || !GL_AttributeType( type, value, components ) ) {
            return render_error_t::ERR_SHADER_INTERFACE_MISMATCH;
        }
        const render_vertex_attribute_desc_t *attribute = nullptr;
        for ( common::u32 j = 0u; j < layout.attributeCount; ++j ) {
            if ( layout.attributes[j].location == location ) attribute = &layout.attributes[j];
        }
        render_vertex_format_info_t format{};
        if ( attribute == nullptr || !R_GetVertexFormatInfo( attribute->format, &format ) ||
             format.valueType != value || format.componentCount != components ) {
            return render_error_t::ERR_SHADER_INTERFACE_MISMATCH;
        }
    }
    return render_error_t::OK;
}

render_error_t GL_ValidatePipelineUniforms(
    const GLuint program, const backend_pipeline_desc_t &description,
    GLuint ( &blockIndicesOut )[R_MAX_UNIFORM_BLOCK_BINDINGS],
    GLint ( &samplerLocationsOut )[R_MAX_SAMPLED_TEXTURE_BINDINGS] ) noexcept
{
    GLint blockCount = 0;
    GLint uniformCount = 0;
    glGetProgramiv( program, GL_ACTIVE_UNIFORM_BLOCKS, &blockCount );
    glGetProgramiv( program, GL_ACTIVE_UNIFORMS, &uniformCount );
    render_error_t result = GL_CheckErrors( glState.config.validation );
    if ( result != render_error_t::OK ) return result;
    if ( blockCount != static_cast<GLint>( description.uniformBlockCount ) || uniformCount < 0 ) {
        return render_error_t::ERR_SHADER_INTERFACE_MISMATCH;
    }
    for ( common::u32 slot = 0u; slot < description.sampledTextureCount; ++slot ) {
        samplerLocationsOut[slot] = glGetUniformLocation(
            program, description.sampledTextures[slot].name );
        result = GL_CheckErrors( glState.config.validation );
        if ( result != render_error_t::OK ) return result;
        if ( samplerLocationsOut[slot] < 0 ) return render_error_t::ERR_SHADER_INTERFACE_MISMATCH;
    }
    common::u32 samplerCount = 0u;
    // Only declared scalar sampler2D or sampler2DShadow uniforms are supported.
    // Reject every other ordinary uniform, array, or undeclared binding.
    for ( GLint i = 0; i < uniformCount; ++i ) {
        const GLuint uniform = static_cast<GLuint>( i );
        GLint blockIndex = -1;
        glGetActiveUniformsiv( program, 1, &uniform, GL_UNIFORM_BLOCK_INDEX, &blockIndex );
        result = GL_CheckErrors( glState.config.validation );
        if ( result != render_error_t::OK ) return result;
        if ( blockIndex < 0 ) {
            GLint type = 0, size = 0;
            glGetActiveUniformsiv( program, 1, &uniform, GL_UNIFORM_TYPE, &type );
            glGetActiveUniformsiv( program, 1, &uniform, GL_UNIFORM_SIZE, &size );
            result = GL_CheckErrors( glState.config.validation );
            if ( result != render_error_t::OK ) return result;
            if ( size != 1 ) return render_error_t::ERR_SHADER_INTERFACE_MISMATCH;
            bool declared = false;
            for ( common::u32 slot = 0u; slot < description.sampledTextureCount; ++slot ) {
                GLuint expectedIndex = GL_INVALID_INDEX;
                const GLchar *name = description.sampledTextures[slot].name;
                glGetUniformIndices( program, 1, &name, &expectedIndex );
                result = GL_CheckErrors( glState.config.validation );
                if ( result != render_error_t::OK ) return result;
                if ( expectedIndex == uniform ) {
                    const GLenum expectedType = description.sampledTextures[slot].kind ==
                            render_sampled_texture_kind_t::DEPTH_COMPARE_2D
                        ? GL_SAMPLER_2D_SHADOW : GL_SAMPLER_2D;
                    if ( type != static_cast<GLint>( expectedType ) ) {
                        return render_error_t::ERR_SHADER_INTERFACE_MISMATCH;
                    }
                    declared = true;
                }
            }
            if ( !declared ) return render_error_t::ERR_SHADER_INTERFACE_MISMATCH;
            ++samplerCount;
        }
    }
    if ( samplerCount != description.sampledTextureCount ) {
        return render_error_t::ERR_SHADER_INTERFACE_MISMATCH;
    }
    for ( common::u32 slot = 0u; slot < description.uniformBlockCount; ++slot ) {
        const auto &binding = description.uniformBlocks[slot];
        const GLuint block = glGetUniformBlockIndex( program, binding.name );
        result = GL_CheckErrors( glState.config.validation );
        if ( result != render_error_t::OK ) return result;
        if ( block == GL_INVALID_INDEX ) return render_error_t::ERR_SHADER_INTERFACE_MISMATCH;
        GLint requiredBytes = 0;
        glGetActiveUniformBlockiv( program, block, GL_UNIFORM_BLOCK_DATA_SIZE, &requiredBytes );
        result = GL_CheckErrors( glState.config.validation );
        if ( result != render_error_t::OK ) return result;
        if ( requiredBytes <= 0 || static_cast<common::u32>( requiredBytes ) != binding.byteSize ) {
            return render_error_t::ERR_SHADER_INTERFACE_MISMATCH;
        }
        blockIndicesOut[slot] = block;
    }
    return render_error_t::OK;
}

void GL_SetCapability( const GLenum capability, const bool enabled ) noexcept
{
    if ( enabled ) glEnable( capability );
    else glDisable( capability );
}

bool GL_RectFitsExtent( const common::u32 x, const common::u32 y,
    const common::u32 width, const common::u32 height,
    const render_extent_t extent ) noexcept
{
    const auto maximumCount = static_cast<common::u32>( std::numeric_limits<GLint>::max() );
    return width != 0u && height != 0u && x <= maximumCount && y <= maximumCount &&
        width <= maximumCount && height <= maximumCount &&
        x < extent.width && y < extent.height &&
        width <= extent.width - x && height <= extent.height - y;
}
} // namespace

render_error_t GL_CreateGraphicsPipeline(
    const backend_pipeline_desc_t &description,
    backend_pipeline_t &pipelineOut, void *backendState ) noexcept
{
    pipelineOut = R_INVALID_BACKEND_PIPELINE;
    if ( backendState != &glState ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( !glState.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    if ( !R_IsBackendShaderValid( description.shader ) ||
         description.shader.value > std::numeric_limits<GLuint>::max() ) {
        return render_error_t::ERR_INVALID_HANDLE;
    }
    if ( R_ValidateVertexLayout( description.vertexLayout ) != render_error_t::OK ) {
        return render_error_t::ERR_VERTEX_LAYOUT_INVALID;
    }
    if ( description.topology != render_primitive_topology_t::TRIANGLE_LIST &&
         description.topology != render_primitive_topology_t::LINE_LIST ) {
        return render_error_t::ERR_PIPELINE_DESC_INVALID;
    }
    const GLenum polygonMode = GL_PolygonMode( description.polygonMode );
    if ( polygonMode == 0u ||
         ( description.topology == render_primitive_topology_t::LINE_LIST &&
           description.polygonMode != render_polygon_mode_t::FILL ) ) {
        return render_error_t::ERR_PIPELINE_DESC_INVALID;
    }
    const GLenum depthCompare = GL_DepthCompare( description.depthCompare );
    if ( depthCompare == 0u ) return render_error_t::ERR_PIPELINE_DESC_INVALID;
    if ( description.uniformBlockCount > R_MAX_UNIFORM_BLOCK_BINDINGS ||
         description.sampledTextureCount > R_MAX_SAMPLED_TEXTURE_BINDINGS ) {
        return render_error_t::ERR_PIPELINE_DESC_INVALID;
    }
    if ( description.uniformBlockCount > glState.limits.maxUniformBufferBindings ) {
        return render_error_t::ERR_RESOURCE_SIZE_UNSUPPORTED;
    }
    if ( description.sampledTextureCount > glState.limits.maxCombinedTextureUnits ) {
        return render_error_t::ERR_CAPABILITY_MISSING;
    }
    for ( common::u32 slot = 0u; slot < R_MAX_UNIFORM_BLOCK_BINDINGS; ++slot ) {
        const auto &block = description.uniformBlocks[slot];
        if ( slot < description.uniformBlockCount ) {
            if ( block.name == nullptr || block.name[0] == '\0' || block.byteSize == 0u ) {
                return render_error_t::ERR_PIPELINE_DESC_INVALID;
            }
            if ( block.byteSize > glState.limits.maxUniformBlockBytes ) {
                return render_error_t::ERR_RESOURCE_SIZE_UNSUPPORTED;
            }
            for ( common::u32 previous = 0u; previous < slot; ++previous ) {
                if ( std::strcmp( block.name, description.uniformBlocks[previous].name ) == 0 ) {
                    return render_error_t::ERR_PIPELINE_DESC_INVALID;
                }
            }
        } else if ( block.name != nullptr || block.byteSize != 0u ) {
            return render_error_t::ERR_PIPELINE_DESC_INVALID;
        }
    }
    for ( common::u32 slot = 0u; slot < R_MAX_SAMPLED_TEXTURE_BINDINGS; ++slot ) {
        const auto &texture = description.sampledTextures[slot];
        if ( slot < description.sampledTextureCount ) {
            if ( texture.name == nullptr || texture.name[0] == '\0' ||
                 texture.kind >= render_sampled_texture_kind_t::COUNT ) {
                return render_error_t::ERR_PIPELINE_DESC_INVALID;
            }
            for ( common::u32 previous = 0u; previous < slot; ++previous ) {
                if ( std::strcmp( texture.name, description.sampledTextures[previous].name ) == 0 ) {
                    return render_error_t::ERR_PIPELINE_DESC_INVALID;
                }
            }
        } else if ( texture.name != nullptr ||
                    texture.kind != render_sampled_texture_kind_t::SCALAR_2D ) {
            return render_error_t::ERR_PIPELINE_DESC_INVALID;
        }
    }
    const GLuint program = static_cast<GLuint>( description.shader.value );
    GL_ClearErrors();
    GLint linked = GL_FALSE;
    glGetProgramiv( program, GL_LINK_STATUS, &linked );
    render_error_t result = GL_CheckErrors( glState.config.validation );
    if ( result != render_error_t::OK ) return result;
    if ( linked != GL_TRUE ) return render_error_t::ERR_SHADER_LINK_FAILED;
    result = GL_ValidatePipelineAttributes( program, description.vertexLayout );
    if ( result != render_error_t::OK ) return result;
    GLuint blockIndices[R_MAX_UNIFORM_BLOCK_BINDINGS]{};
    GLint samplerLocations[R_MAX_SAMPLED_TEXTURE_BINDINGS]{};
    result = GL_ValidatePipelineUniforms(
        program, description, blockIndices, samplerLocations );
    if ( result != render_error_t::OK ) return result;

    void *storage = common::Allocator_Allocate(
        common::Allocator_GetSystem(), sizeof( gl_pipeline_record_t ), alignof( gl_pipeline_record_t ) );
    if ( storage == nullptr ) return render_error_t::ERR_OUT_OF_MEMORY;
    auto *record = new ( storage ) gl_pipeline_record_t{};
    record->program = program;
    record->uniformBlockCount = description.uniformBlockCount;
    record->sampledTextureCount = description.sampledTextureCount;
    for ( common::u32 slot = 0u; slot < R_MAX_UNIFORM_BLOCK_BINDINGS; ++slot ) {
        record->uniformBlockBytes[slot] = description.uniformBlocks[slot].byteSize;
        record->uniformBlockIndices[slot] = blockIndices[slot];
    }
    for ( common::u32 slot = 0u; slot < R_MAX_SAMPLED_TEXTURE_BINDINGS; ++slot ) {
        record->sampledTextureLocations[slot] = samplerLocations[slot];
    }
    record->depthTest = description.depthTest;
    record->depthWrite = description.depthWrite;
    record->depthCompare = depthCompare;
    record->colorWrite = description.colorWrite;
    record->cullBackFaces = description.cullBackFaces;
    record->frontCounterClockwise = description.frontCounterClockwise;
    record->alphaBlend = description.alphaBlend;
    record->topology = description.topology;
    record->polygonMode = polygonMode;
    record->next = glPipelines;
    glPipelines = record;
    pipelineOut.value = GL_PipelineToken( record );
    return render_error_t::OK;
}

render_error_t GL_DestroyGraphicsPipeline(
    const backend_pipeline_t pipeline, void *backendState ) noexcept
{
    if ( backendState != &glState ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( !glState.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    gl_pipeline_record_t **link = &glPipelines;
    while ( *link != nullptr && GL_PipelineToken( *link ) != pipeline.value ) link = &( *link )->next;
    if ( *link == nullptr ) return render_error_t::ERR_INVALID_HANDLE;
    gl_pipeline_record_t *record = *link;
    *link = record->next;
    record->~gl_pipeline_record_t();
    common::Allocator_Free(
        common::Allocator_GetSystem(), record, sizeof( gl_pipeline_record_t ), alignof( gl_pipeline_record_t ) );
    return render_error_t::OK;
}

render_error_t GL_DrawIndexed(
    const backend_draw_indexed_desc_t &description, void *backendState ) noexcept
{
    if ( backendState != &glState ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( !glState.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    if ( !glState.frameActive ) return render_error_t::ERR_FRAME_NOT_ACTIVE;
    const gl_pipeline_record_t *pipeline = GL_FindPipeline( description.pipeline );
    if ( pipeline == nullptr || !R_IsBackendVertexInputValid( description.vertexInput ) ||
         description.vertexInput.value > std::numeric_limits<GLuint>::max() ) {
        return render_error_t::ERR_INVALID_HANDLE;
    }
    for ( common::u32 slot = 0u; slot < R_MAX_UNIFORM_BLOCK_BINDINGS; ++slot ) {
        if ( description.uniformBuffers[slot].value > std::numeric_limits<GLuint>::max() ) {
            return render_error_t::ERR_INVALID_HANDLE;
        }
        const bool required = slot < pipeline->uniformBlockCount;
        if ( ( description.uniformBuffers[slot].value != 0u ) != required ) {
            return render_error_t::ERR_BINDING_MISMATCH;
        }
        const auto range = description.uniformRanges[slot];
        const bool explicitRange = range.offset != 0u || range.byteSize != 0u;
        if ( !required && explicitRange ) return render_error_t::ERR_BINDING_MISMATCH;
        if ( required && explicitRange ) {
            const auto nativeMax = static_cast<common::u64>(
                std::numeric_limits<GLintptr>::max() );
            if ( glState.limits.uniformBufferOffsetAlignment == 0u ||
                 range.byteSize < pipeline->uniformBlockBytes[slot] ||
                 range.offset % glState.limits.uniformBufferOffsetAlignment != 0u ||
                 range.offset > nativeMax || range.byteSize > nativeMax ||
                 range.byteSize > nativeMax - range.offset ) {
                return render_error_t::ERR_BINDING_MISMATCH;
            }
        }
    }
    for ( common::u32 slot = 0u; slot < R_MAX_SAMPLED_TEXTURE_BINDINGS; ++slot ) {
        if ( description.sampledTextures[slot].value > std::numeric_limits<GLuint>::max() ||
             description.samplers[slot].value > std::numeric_limits<GLuint>::max() ) {
            return render_error_t::ERR_INVALID_HANDLE;
        }
        if ( ( description.sampledTextures[slot].value != 0u ) !=
             ( slot < pipeline->sampledTextureCount ) ) {
            return render_error_t::ERR_BINDING_MISMATCH;
        }
        if ( slot >= pipeline->sampledTextureCount && description.samplers[slot].value != 0u ) {
            return render_error_t::ERR_BINDING_MISMATCH;
        }
    }
    const common::u32 indexSize = R_IndexTypeSize( description.indexType );
    const common::u32 primitiveIndexCount =
        pipeline->topology == render_primitive_topology_t::LINE_LIST ? 2u : 3u;
    if ( indexSize == 0u || description.indexByteOffset % indexSize != 0u ||
         description.indexByteOffset > std::numeric_limits<std::uintptr_t>::max() ||
         description.indexCount == 0u || description.indexCount % primitiveIndexCount != 0u ||
         description.indexCount > static_cast<common::u32>( std::numeric_limits<GLsizei>::max() ) ||
         description.vertexCount == 0u || description.instanceCount == 0u ||
         description.instanceCount > static_cast<common::u32>( std::numeric_limits<GLsizei>::max() ) ) {
        return render_error_t::ERR_DRAW_DESC_INVALID;
    }
    if ( description.scissor.enabled || description.viewport.enabled ) {
        const render_extent_t extent = glState.targetPassActive
            ? glState.targetPassExtent : glState.drawableExtent;
        if ( ( description.scissor.enabled &&
               !GL_RectFitsExtent( description.scissor.x, description.scissor.y,
                   description.scissor.width, description.scissor.height, extent ) ) ||
             ( description.viewport.enabled &&
               !GL_RectFitsExtent( description.viewport.x, description.viewport.y,
                   description.viewport.width, description.viewport.height, extent ) ) ) {
            return render_error_t::ERR_DRAW_DESC_INVALID;
        }
    }
    GL_ClearErrors();
    // Set viewport zero on every draw so a custom region never leaks into the
    // next full-frame draw. Hosted contexts may own other viewport indices.
    const render_extent_t extent = glState.targetPassActive
        ? glState.targetPassExtent : glState.drawableExtent;
    const GLint viewportX = description.viewport.enabled
        ? static_cast<GLint>( description.viewport.x ) : 0;
    const GLint viewportY = description.viewport.enabled
        ? static_cast<GLint>( description.viewport.y ) : 0;
    const GLsizei viewportWidth = static_cast<GLsizei>( description.viewport.enabled
        ? description.viewport.width : extent.width );
    const GLsizei viewportHeight = static_cast<GLsizei>( description.viewport.enabled
        ? description.viewport.height : extent.height );
    if ( glState.usesHostSurface ) {
        glViewportIndexedf( 0u, static_cast<GLfloat>( viewportX ),
            static_cast<GLfloat>( viewportY ),
            static_cast<GLfloat>( viewportWidth ),
            static_cast<GLfloat>( viewportHeight ) );
    } else {
        glViewport( viewportX, viewportY, viewportWidth, viewportHeight );
    }
    // Set scissor slot zero on every draw too, so clipped draws do not leak.
    if ( description.scissor.enabled ) {
        glScissorIndexed( 0u, static_cast<GLint>( description.scissor.x ),
            static_cast<GLint>( description.scissor.y ),
            static_cast<GLsizei>( description.scissor.width ),
            static_cast<GLsizei>( description.scissor.height ) );
        glEnablei( GL_SCISSOR_TEST, 0u );
    } else {
        glDisablei( GL_SCISSOR_TEST, 0u );
    }
    GL_SetCapability( GL_DEPTH_TEST, pipeline->depthTest );
    glDepthMask( pipeline->depthWrite ? GL_TRUE : GL_FALSE );
    glDepthFunc( pipeline->depthCompare );
    GL_SetCapability( GL_CULL_FACE, pipeline->cullBackFaces );
    glCullFace( GL_BACK );
    glFrontFace( pipeline->frontCounterClockwise ? GL_CCW : GL_CW );
    // A host may use other MRT attachments with independent blend and write
    // state. Our current pipeline writes only color output zero, so hosted
    // draws change only that indexed slot.
    if ( glState.usesHostSurface ) {
        if ( pipeline->alphaBlend ) glEnablei( GL_BLEND, 0u );
        else glDisablei( GL_BLEND, 0u );
    } else {
        GL_SetCapability( GL_BLEND, pipeline->alphaBlend );
    }
    glLineWidth( 1.0f );
    if ( glState.usesHostSurface ) {
        glBlendEquationSeparatei( 0u, GL_FUNC_ADD, GL_FUNC_ADD );
        glBlendFuncSeparatei( 0u, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
            GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
    } else {
        glBlendEquationSeparate( GL_FUNC_ADD, GL_FUNC_ADD );
        glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
            GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
    }
    glDisable( GL_STENCIL_TEST );
    glDisable( GL_PRIMITIVE_RESTART );
    glDisable( GL_RASTERIZER_DISCARD );
    glDisable( GL_POLYGON_OFFSET_FILL );
    glPolygonMode( GL_FRONT_AND_BACK, pipeline->polygonMode );
    const GLboolean colorMask = pipeline->colorWrite ? GL_TRUE : GL_FALSE;
    if ( glState.usesHostSurface ) glColorMaski( 0u, colorMask, colorMask, colorMask, colorMask );
    else glColorMask( colorMask, colorMask, colorMask, colorMask );
    glUseProgram( pipeline->program );
    for ( common::u32 slot = 0u; slot < pipeline->sampledTextureCount; ++slot ) {
        glActiveTexture( GL_TEXTURE0 + slot );
        // A null explicit sampler preserves the texture-owned legacy policy.
        glBindSampler( slot, static_cast<GLuint>( description.samplers[slot].value ) );
        glBindTexture( GL_TEXTURE_2D, static_cast<GLuint>( description.sampledTextures[slot].value ) );
        glUniform1i( pipeline->sampledTextureLocations[slot], static_cast<GLint>( slot ) );
    }
    glBindVertexArray( static_cast<GLuint>( description.vertexInput.value ) );
    for ( common::u32 slot = 0u; slot < pipeline->uniformBlockCount; ++slot ) {
        glUniformBlockBinding( pipeline->program, pipeline->uniformBlockIndices[slot], slot );
        const auto range = description.uniformRanges[slot];
        if ( range.offset == 0u && range.byteSize == 0u ) {
            glBindBufferBase( GL_UNIFORM_BUFFER, slot,
                static_cast<GLuint>( description.uniformBuffers[slot].value ) );
        } else {
            glBindBufferRange( GL_UNIFORM_BUFFER, slot,
                static_cast<GLuint>( description.uniformBuffers[slot].value ),
                static_cast<GLintptr>( range.offset ),
                static_cast<GLsizeiptr>( range.byteSize ) );
        }
    }
    render_error_t result = GL_CheckErrors( glState.config.validation );
    if ( result == render_error_t::OK ) {
        glDrawElementsInstanced(
            pipeline->topology == render_primitive_topology_t::LINE_LIST ? GL_LINES : GL_TRIANGLES,
            static_cast<GLsizei>( description.indexCount ),
            description.indexType == render_index_type_t::UINT16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
            reinterpret_cast<const void *>( static_cast<std::uintptr_t>( description.indexByteOffset ) ),
            static_cast<GLsizei>( description.instanceCount ) );
        result = GL_CheckErrors( glState.config.validation );
    }
    // No transient native binding remains after an immediate draw. Fixed state
    // is re-established by every pipeline; BeginFrame resets the depth-write mask.
    for ( common::u32 slot = 0u; slot < pipeline->sampledTextureCount; ++slot ) {
        glActiveTexture( GL_TEXTURE0 + slot );
        glBindSampler( slot, 0u );
        glBindTexture( GL_TEXTURE_2D, 0u );
    }
    glActiveTexture( GL_TEXTURE0 );
    for ( common::u32 slot = 0u; slot < pipeline->uniformBlockCount; ++slot ) {
        glBindBufferBase( GL_UNIFORM_BUFFER, slot, 0u );
    }
    glBindVertexArray( 0u );
    glUseProgram( 0u );
    const render_error_t cleanupResult = GL_CheckErrors( glState.config.validation );
    return result != render_error_t::OK ? result : cleanupResult;
}

} // namespace cypher::engine::render
