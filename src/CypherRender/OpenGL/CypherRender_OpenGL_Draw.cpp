//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Validates linked shader interfaces and submits indexed triangles.
// Details: A native pipeline is an owned state record, not a GL program-pipeline
//          object. The frontend retains its program until this record is freed.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_OpenGL_Draw.h"
#include "CypherRender_OpenGL_Local.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include <cstdint>
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
    common::u32 uniformBlockBytes{ 0u };
    GLint sampledTextureLocation{ -1 };
    bool depthTest{ true };
    bool depthWrite{ true };
    bool cullBackFaces{ true };
    bool frontCounterClockwise{ true };
    bool alphaBlend{ false };
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
    const GLuint program, const backend_pipeline_desc_t &description ) noexcept
{
    GLint blockCount = 0;
    GLint uniformCount = 0;
    glGetProgramiv( program, GL_ACTIVE_UNIFORM_BLOCKS, &blockCount );
    glGetProgramiv( program, GL_ACTIVE_UNIFORMS, &uniformCount );
    render_error_t result = GL_CheckErrors( glState.config.validation );
    if ( result != render_error_t::OK ) return result;
    const bool hasBlock = description.uniformBlockBytes != 0u;
    if ( blockCount != ( hasBlock ? 1 : 0 ) || uniformCount < 0 ) {
        return render_error_t::ERR_SHADER_INTERFACE_MISMATCH;
    }
    const bool hasTexture = description.sampledTextureName != nullptr;
    GLint samplerLocation = -1;
    if ( hasTexture ) {
        samplerLocation = glGetUniformLocation( program, description.sampledTextureName );
        result = GL_CheckErrors( glState.config.validation );
        if ( result != render_error_t::OK ) return result;
        if ( samplerLocation < 0 ) return render_error_t::ERR_SHADER_INTERFACE_MISMATCH;
    }
    unsigned samplerCount = 0u;
    // Exactly one named scalar sampler2D is supported. Reject every other
    // ordinary uniform, sampler type, multi-element array, or undeclared binding.
    for ( GLint i = 0; i < uniformCount; ++i ) {
        const GLuint uniform = static_cast<GLuint>( i );
        GLint blockIndex = -1;
        glGetActiveUniformsiv( program, 1, &uniform, GL_UNIFORM_BLOCK_INDEX, &blockIndex );
        result = GL_CheckErrors( glState.config.validation );
        if ( result != render_error_t::OK ) return result;
        if ( blockIndex < 0 ) {
            if ( !hasTexture ) return render_error_t::ERR_SHADER_INTERFACE_MISMATCH;
            GLint type = 0, size = 0;
            glGetActiveUniformsiv( program, 1, &uniform, GL_UNIFORM_TYPE, &type );
            glGetActiveUniformsiv( program, 1, &uniform, GL_UNIFORM_SIZE, &size );
            GLuint expectedIndex = GL_INVALID_INDEX;
            const GLchar *name = description.sampledTextureName;
            glGetUniformIndices( program, 1, &name, &expectedIndex );
            result = GL_CheckErrors( glState.config.validation );
            if ( result != render_error_t::OK ) return result;
            if ( type != GL_SAMPLER_2D || size != 1 || expectedIndex != uniform ) {
                return render_error_t::ERR_SHADER_INTERFACE_MISMATCH;
            }
            ++samplerCount;
        }
    }
    if ( samplerCount != ( hasTexture ? 1u : 0u ) ) return render_error_t::ERR_SHADER_INTERFACE_MISMATCH;
    if ( hasBlock ) {
        const GLuint block = glGetUniformBlockIndex( program, description.uniformBlockName );
        result = GL_CheckErrors( glState.config.validation );
        if ( result != render_error_t::OK ) return result;
        if ( block == GL_INVALID_INDEX ) return render_error_t::ERR_SHADER_INTERFACE_MISMATCH;
        GLint requiredBytes = 0;
        glGetActiveUniformBlockiv( program, block, GL_UNIFORM_BLOCK_DATA_SIZE, &requiredBytes );
        result = GL_CheckErrors( glState.config.validation );
        if ( result != render_error_t::OK ) return result;
        if ( requiredBytes <= 0 || static_cast<common::u32>( requiredBytes ) != description.uniformBlockBytes ) {
            return render_error_t::ERR_SHADER_INTERFACE_MISMATCH;
        }
        glUniformBlockBinding( program, block, 0u );
        return GL_CheckErrors( glState.config.validation );
    }
    return render_error_t::OK;
}

void GL_SetCapability( const GLenum capability, const bool enabled ) noexcept
{
    if ( enabled ) glEnable( capability );
    else glDisable( capability );
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
    for ( common::u32 i = 0u; i < description.vertexLayout.bindingCount; ++i ) {
        if ( description.vertexLayout.bindings[i].inputRate != render_vertex_input_rate_t::PER_VERTEX ) {
            return render_error_t::ERR_PIPELINE_DESC_INVALID;
        }
    }
    const bool hasBlock = description.uniformBlockName != nullptr;
    if ( hasBlock != ( description.uniformBlockBytes != 0u ) ||
         ( hasBlock && description.uniformBlockName[0] == '\0' ) ) {
        return render_error_t::ERR_PIPELINE_DESC_INVALID;
    }
    if ( description.uniformBlockBytes > glState.limits.maxUniformBlockBytes ||
         ( hasBlock && glState.limits.maxUniformBufferBindings == 0u ) ) {
        return render_error_t::ERR_RESOURCE_SIZE_UNSUPPORTED;
    }
    if ( description.sampledTextureName != nullptr ) {
        if ( description.sampledTextureName[0] == '\0' ) return render_error_t::ERR_PIPELINE_DESC_INVALID;
        if ( glState.limits.maxCombinedTextureUnits == 0u ) return render_error_t::ERR_CAPABILITY_MISSING;
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
    result = GL_ValidatePipelineUniforms( program, description );
    if ( result != render_error_t::OK ) return result;

    void *storage = common::Allocator_Allocate(
        common::Allocator_GetSystem(), sizeof( gl_pipeline_record_t ), alignof( gl_pipeline_record_t ) );
    if ( storage == nullptr ) return render_error_t::ERR_OUT_OF_MEMORY;
    auto *record = new ( storage ) gl_pipeline_record_t{};
    record->program = program;
    record->uniformBlockBytes = description.uniformBlockBytes;
    record->sampledTextureLocation = description.sampledTextureName != nullptr
        ? glGetUniformLocation( program, description.sampledTextureName ) : -1;
    record->depthTest = description.depthTest;
    record->depthWrite = description.depthWrite;
    record->cullBackFaces = description.cullBackFaces;
    record->frontCounterClockwise = description.frontCounterClockwise;
    record->alphaBlend = description.alphaBlend;
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
         description.vertexInput.value > std::numeric_limits<GLuint>::max() ||
         description.uniformBuffer.value > std::numeric_limits<GLuint>::max() ||
         description.sampledTexture.value > std::numeric_limits<GLuint>::max() ) {
        return render_error_t::ERR_INVALID_HANDLE;
    }
    const common::u32 indexSize = R_IndexTypeSize( description.indexType );
    if ( indexSize == 0u || description.indexByteOffset % indexSize != 0u ||
         description.indexByteOffset > std::numeric_limits<std::uintptr_t>::max() ||
         description.indexCount == 0u || description.indexCount % 3u != 0u ||
         description.indexCount > static_cast<common::u32>( std::numeric_limits<GLsizei>::max() ) ||
         description.vertexCount == 0u ) {
        return render_error_t::ERR_DRAW_DESC_INVALID;
    }
    if ( ( description.uniformBuffer.value != 0u ) != ( pipeline->uniformBlockBytes != 0u ) ) {
        return render_error_t::ERR_BINDING_MISMATCH;
    }

    if ( ( description.sampledTexture.value != 0u ) != ( pipeline->sampledTextureLocation >= 0 ) ) {
        return render_error_t::ERR_BINDING_MISMATCH;
    }

    GL_ClearErrors();
    GL_SetCapability( GL_DEPTH_TEST, pipeline->depthTest );
    glDepthMask( pipeline->depthWrite ? GL_TRUE : GL_FALSE );
    glDepthFunc( GL_LESS );
    GL_SetCapability( GL_CULL_FACE, pipeline->cullBackFaces );
    glCullFace( GL_BACK );
    glFrontFace( pipeline->frontCounterClockwise ? GL_CCW : GL_CW );
    GL_SetCapability( GL_BLEND, pipeline->alphaBlend );
    glBlendEquationSeparate( GL_FUNC_ADD, GL_FUNC_ADD );
    glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
    glDisable( GL_STENCIL_TEST );
    glDisable( GL_PRIMITIVE_RESTART );
    glDisable( GL_RASTERIZER_DISCARD );
    glDisable( GL_POLYGON_OFFSET_FILL );
    glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
    glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
    glUseProgram( pipeline->program );
    if ( pipeline->sampledTextureLocation >= 0 ) {
        glActiveTexture( GL_TEXTURE0 );
        glBindSampler( 0u, 0u ); // Host samplers must not override immutable texture policy.
        glBindTexture( GL_TEXTURE_2D, static_cast<GLuint>( description.sampledTexture.value ) );
        glUniform1i( pipeline->sampledTextureLocation, 0 );
    }
    glBindVertexArray( static_cast<GLuint>( description.vertexInput.value ) );
    glBindBufferBase( GL_UNIFORM_BUFFER, 0u, static_cast<GLuint>( description.uniformBuffer.value ) );
    render_error_t result = GL_CheckErrors( glState.config.validation );
    if ( result == render_error_t::OK ) {
        glDrawElements(
            GL_TRIANGLES, static_cast<GLsizei>( description.indexCount ),
            description.indexType == render_index_type_t::UINT16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
            reinterpret_cast<const void *>( static_cast<std::uintptr_t>( description.indexByteOffset ) ) );
        result = GL_CheckErrors( glState.config.validation );
    }
    // No transient native binding remains after an immediate draw. Fixed state
    // is re-established by every pipeline; BeginFrame resets the depth-write mask.
    if ( pipeline->sampledTextureLocation >= 0 ) glBindTexture( GL_TEXTURE_2D, 0u );
    glBindBufferBase( GL_UNIFORM_BUFFER, 0u, 0u );
    glBindVertexArray( 0u );
    glUseProgram( 0u );
    const render_error_t cleanupResult = GL_CheckErrors( glState.config.validation );
    return result != render_error_t::OK ? result : cleanupResult;
}

} // namespace cypher::engine::render
