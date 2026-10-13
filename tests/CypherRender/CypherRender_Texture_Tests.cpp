//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Tests texture ownership, bounds and sampled draw validation.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////
#include "CypherRender/CypherRender_Texture.h"
#include "CypherRender/CypherRender_Sampler.h"
#include "CypherRender/CypherRender_Local.h"
#include "CypherRender/CypherRender_Draw.h"
#include "CypherCommon/Tier0/CypherCommon_MemoryOps.h"
#include "CypherCommon/Tier1/CypherCommon_ByteWriter.h"
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <limits>
#include <vector>

namespace render = ::cypher::engine::render;
namespace common = ::cypher::common;
namespace
{
using texture_error_t = render::render_error_t;
struct texture_fixture_t {
    render::backend_api_t backend{};
    common::u64 next{ 1u };
    unsigned creates{ 0u }, destroys{ 0u }, draws{ 0u };
    unsigned samplerCreates{ 0u }, samplerDestroys{ 0u };
    texture_error_t createResult{ texture_error_t::OK }, destroyResult{ texture_error_t::OK };
    texture_error_t samplerCreateResult{ texture_error_t::OK }, samplerDestroyResult{ texture_error_t::OK };
    texture_error_t drawResult{ texture_error_t::OK };
    bool tokenOnCreateFailure{ false };
    bool samplerTokenOnCreateFailure{ false };
    render::render_texture_desc_t captured{};
    common::byte capturedPixels[24]{};
    common::u64 capturedPixelBytes{ 0u };
    common::u32 capturedMipCount{ 0u };
    common::u32 capturedMipWidths[3]{}, capturedMipHeights[3]{};
    common::u64 capturedMipBytes[3]{};
    common::byte capturedMipPixels[3][32]{};
    render::render_sampler_desc_t capturedSampler{};
    render::backend_draw_indexed_desc_t capturedDraw{};
    common::byte pixels[24]{};
    common::byte mappedBytes[256]{};

    void Init()
    {
        backend.CreateTexture2D = []( const render::render_texture_desc_t &desc,
            const render::render_texture_data_t &data, render::backend_texture_t &out, void *state ) noexcept {
            auto &self = *static_cast<texture_fixture_t *>( state );
            ++self.creates;
            self.captured = desc;
            self.capturedPixelBytes = data.byteSize;
            if ( data.bytes != nullptr && data.byteSize <= sizeof( self.capturedPixels ) ) {
                std::memcpy( self.capturedPixels, data.bytes, static_cast<std::size_t>( data.byteSize ) );
            }
            out = {};
            if ( self.createResult == texture_error_t::OK || self.tokenOnCreateFailure ) {
                out.value = self.next++;
            }
            return self.createResult;
        };
        backend.CreateTexture2DLevels = []( const render::render_texture_desc_t &desc,
            const render::render_texture_mip_data_t *levels, common::u32 levelCount,
            render::backend_texture_t &out, void *state ) noexcept {
            auto &self = *static_cast<texture_fixture_t *>( state );
            ++self.creates;
            self.captured = desc;
            self.capturedMipCount = levelCount;
            if ( levels != nullptr && levelCount <= 3u ) {
                for ( common::u32 level = 0u; level < levelCount; ++level ) {
                    self.capturedMipWidths[level] = levels[level].width;
                    self.capturedMipHeights[level] = levels[level].height;
                    self.capturedMipBytes[level] = levels[level].data.byteSize;
                    if ( levels[level].data.bytes != nullptr &&
                         levels[level].data.byteSize <= sizeof( self.capturedMipPixels[level] ) ) {
                        std::memcpy( self.capturedMipPixels[level], levels[level].data.bytes,
                            static_cast<std::size_t>( levels[level].data.byteSize ) );
                    }
                }
            }
            out = {};
            if ( self.createResult == texture_error_t::OK || self.tokenOnCreateFailure ) {
                out.value = self.next++;
            }
            return self.createResult;
        };
        backend.DestroyTexture = []( render::backend_texture_t, void *state ) noexcept {
            auto &self = *static_cast<texture_fixture_t *>( state );
            ++self.destroys;
            return self.destroyResult;
        };
        backend.CreateSampler = []( const render::render_sampler_desc_t &desc,
            render::backend_sampler_t &out, void *state ) noexcept {
            auto &self = *static_cast<texture_fixture_t *>( state );
            ++self.samplerCreates;
            self.capturedSampler = desc;
            out = {};
            if ( self.samplerCreateResult == texture_error_t::OK ||
                 self.samplerTokenOnCreateFailure ) {
                out.value = self.next++;
            }
            return self.samplerCreateResult;
        };
        backend.DestroySampler = []( render::backend_sampler_t, void *state ) noexcept {
            auto &self = *static_cast<texture_fixture_t *>( state );
            ++self.samplerDestroys;
            return self.samplerDestroyResult;
        };
        backend.CreateBuffer = []( const render::render_buffer_desc_t &,
            const render::render_buffer_data_t *, render::backend_buffer_t &out, void *state ) noexcept {
            out.value = static_cast<texture_fixture_t *>( state )->next++;
            return texture_error_t::OK;
        };
        backend.DestroyBuffer = []( render::backend_buffer_t, void * ) noexcept { return texture_error_t::OK; };
        backend.MapBuffer = []( render::backend_buffer_t,
            const render::render_buffer_range_t &range,
            render::render_buffer_map_flags_t,
            render::render_buffer_mapping_t &out, void *state ) noexcept {
            out = { static_cast<texture_fixture_t *>( state )->mappedBytes, range.byteSize };
            return texture_error_t::OK;
        };
        backend.UnmapBuffer = []( render::backend_buffer_t, void * ) noexcept { return texture_error_t::OK; };
        backend.CreateVertexInput = []( const render::backend_vertex_input_desc_t &,
            render::backend_vertex_input_t &out, void *state ) noexcept {
            out.value = static_cast<texture_fixture_t *>( state )->next++;
            return texture_error_t::OK;
        };
        backend.DestroyVertexInput = []( render::backend_vertex_input_t, void * ) noexcept { return texture_error_t::OK; };
        backend.CreateShader = []( const render::render_shader_desc_t &,
            render::backend_shader_t &out, void *state ) noexcept {
            out.value = static_cast<texture_fixture_t *>( state )->next++;
            return texture_error_t::OK;
        };
        backend.DestroyShader = []( render::backend_shader_t, void * ) noexcept { return texture_error_t::OK; };
        backend.CreateGraphicsPipeline = []( const render::backend_pipeline_desc_t &,
            render::backend_pipeline_t &out, void *state ) noexcept {
            out.value = static_cast<texture_fixture_t *>( state )->next++;
            return texture_error_t::OK;
        };
        backend.DestroyGraphicsPipeline = []( render::backend_pipeline_t, void * ) noexcept { return texture_error_t::OK; };
        backend.DrawIndexed = []( const render::backend_draw_indexed_desc_t &desc, void *state ) noexcept {
            auto &self = *static_cast<texture_fixture_t *>( state );
            ++self.draws;
            self.capturedDraw = desc;
            return self.drawResult;
        };
        render::tr = {};
        render::tr.backend = { &backend, this };
        render::tr.initialized = true;
        render::tr.info.backend = render::render_backend_t::OPENGL;
        render::tr.info.apiMajorVersion = 4u;
        render::tr.info.apiMinorVersion = 1u;
        render::tr.info.limits.maxTexture2DSize = 4096u;
        render::tr.info.limits.maxCombinedTextureUnits = 16u;
        render::tr.info.limits.maxUniformBlockBytes = 16384u;
        render::tr.info.limits.maxUniformBufferBindings = 16u;
        REQUIRE( render::R_TextureSystemInit() == texture_error_t::OK );
        REQUIRE( render::R_SamplerSystemInit() == texture_error_t::OK );
        REQUIRE( render::R_BufferSystemInit() == texture_error_t::OK );
        REQUIRE( render::R_ShaderSystemInit() == texture_error_t::OK );
        REQUIRE( render::R_VertexInputSystemInit() == texture_error_t::OK );
        REQUIRE( render::R_PipelineSystemInit() == texture_error_t::OK );
    }
    render::render_texture_desc_t Description() const { return { 3u, 2u, true, true, true, "Test texture" }; }
    render::render_texture_data_t Data() const { return { pixels, sizeof( pixels ) }; }
    render::render_texture_handle_t Create()
    {
        render::render_texture_handle_t texture{};
        REQUIRE( render::R_CreateTexture2D( Description(), Data(), &texture ) == texture_error_t::OK );
        return texture;
    }
    render::render_sampler_handle_t Sampler( render::render_sampler_mip_filter_t mip =
                                                render::render_sampler_mip_filter_t::NONE )
    {
        render::render_sampler_desc_t desc{};
        desc.minFilter = render::render_sampler_filter_t::NEAREST;
        desc.magFilter = render::render_sampler_filter_t::LINEAR;
        desc.mipFilter = mip;
        desc.addressU = render::render_sampler_address_t::CLAMP_TO_EDGE;
        desc.addressV = render::render_sampler_address_t::REPEAT;
        desc.debugName = "Fixture sampler";
        render::render_sampler_handle_t sampler{};
        REQUIRE( render::R_CreateSampler( desc, &sampler ) == texture_error_t::OK );
        return sampler;
    }
    render::render_buffer_handle_t Uniform( common::u64 bytes )
    {
        render::render_buffer_desc_t desc{};
        desc.byteSize = bytes;
        desc.usage = render::R_BUFFER_USAGE_UNIFORM;
        desc.memory = render::render_buffer_memory_t::UPLOAD;
        desc.updatePolicy = render::render_buffer_update_t::DYNAMIC;
        render::render_buffer_handle_t result{};
        REQUIRE( render::R_CreateBuffer( desc, nullptr, &result ) == texture_error_t::OK );
        return result;
    }
    render::render_draw_indexed_desc_t MakeDraw( bool textured, bool twoBindings = false,
        render::render_sampled_texture_kind_t kind = render::render_sampled_texture_kind_t::SCALAR_2D )
    {
        constexpr char source[] = "#version 410 core\nvoid main() {}\n";
        common::cooked_shader_view_t cooked{};
        cooked.nLanguageVersion = 410u;
        cooked.nStages = 2u;
        cooked.stages[0].stage = common::render_shader_stage_t::VERTEX;
        cooked.stages[1].stage = common::render_shader_stage_t::FRAGMENT;
        for ( auto &stage : cooked.stages ) stage.code = { reinterpret_cast<const common::byte *>( source ), sizeof( source ) };
        render::render_shader_handle_t shader{};
        REQUIRE( render::R_CreateShader( { &cooked, nullptr }, &shader ) == texture_error_t::OK );
        render::render_vertex_layout_t layout{};
        layout.bindingCount = 1u;
        layout.bindings[0].stride = 12u;
        layout.attributeCount = 1u;
        layout.attributes[0] = { render::render_format_t::RGB32_FLOAT, 0u, 0u, 0u };
        render::render_pipeline_desc_t pipeline{};
        pipeline.shader = shader;
        pipeline.vertexLayout = layout;
        pipeline.sampledTextureCount = textured ? ( twoBindings ? 2u : 1u ) : 0u;
        if ( textured ) {
            pipeline.sampledTextures[0].name = "base_color";
            pipeline.sampledTextures[0].kind = kind;
        }
        if ( twoBindings ) {
            pipeline.sampledTextures[1].name = "detail";
            pipeline.uniformBlockCount = 2u;
            pipeline.uniformBlocks[0] = { "Transform", 64u };
            pipeline.uniformBlocks[1] = { "Material", 32u };
        }
        render::render_draw_indexed_desc_t draw{};
        REQUIRE( render::R_CreateGraphicsPipeline( pipeline, &draw.pipeline ) == texture_error_t::OK );
        render::render_pipeline_info_t pipelineInfo{};
        REQUIRE( render::R_GetGraphicsPipelineInfo( draw.pipeline, &pipelineInfo ) == texture_error_t::OK );
        CHECK( pipelineInfo.sampledTextureCount == pipeline.sampledTextureCount );
        CHECK( pipelineInfo.uniformBlockCount == pipeline.uniformBlockCount );
        if ( textured ) CHECK( pipelineInfo.sampledTextureKinds[0] == kind );
        auto makeBuffer = []( common::u64 bytes, render::render_buffer_usage_flags_t usage ) {
            render::render_buffer_desc_t desc{};
            desc.byteSize = bytes;
            desc.usage = usage;
            desc.memory = render::render_buffer_memory_t::UPLOAD;
            desc.updatePolicy = render::render_buffer_update_t::DYNAMIC;
            render::render_buffer_handle_t result{};
            REQUIRE( render::R_CreateBuffer( desc, nullptr, &result ) == texture_error_t::OK );
            return result;
        };
        render::render_vertex_input_desc_t input{};
        input.layout = layout;
        input.vertexBufferCount = 1u;
        input.vertexBuffers[0] = { makeBuffer( 36u, render::R_BUFFER_USAGE_VERTEX ), 0u, 0u };
        input.indexBuffer = { makeBuffer( 6u, render::R_BUFFER_USAGE_INDEX ), 0u, render::render_index_type_t::UINT16 };
        REQUIRE( render::R_CreateVertexInput( input, &draw.vertexInput ) == texture_error_t::OK );
        draw.indexCount = draw.vertexCount = 3u;
        render::tr.frameActive = true;
        return draw;
    }
    ~texture_fixture_t()
    {
        render::tr.frameActive = false;
        destroyResult = texture_error_t::OK;
        (void)render::R_PipelineSystemShutdown();
        samplerDestroyResult = texture_error_t::OK;
        (void)render::R_SamplerSystemShutdown();
        (void)render::R_TextureSystemShutdown();
        (void)render::R_VertexInputSystemShutdown();
        (void)render::R_ShaderSystemShutdown();
        (void)render::R_BufferSystemShutdown();
        render::tr = {};
    }
};

void WriteCookedTexture( const common::cooked_texture_desc_t &desc,
    const common::cooked_texture_mip_source_t *mips, common::u32 mipCount,
    std::vector<common::byte> &file, common::cooked_texture_view_t &view )
{
    const auto source = common::span_t<const common::cooked_texture_mip_source_t>{ mips, mipCount };
    const auto required = common::CookedTexture_RequiredSize( desc, source );
    REQUIRE( required > 0u );
    file.assign( required, 0u );
    constexpr char path[] = "textures/renderer-adoption.cytex";
    const auto hash = common::ContentHash_String( { path, sizeof( path ) - 1u } );
    const auto written = common::CookedTexture_Write( desc, source, hash,
        { file.data(), file.size() } );
    REQUIRE( common::CookedTexture_Succeeded( written ) );
    REQUIRE( written.cbWritten == file.size() );
    const auto read = common::CookedTexture_Read( { file.data(), file.size() }, &view );
    REQUIRE( common::CookedTexture_Succeeded( read ) );
}

void WriteCookedSubresources( const common::cooked_texture_desc_t &desc,
    const common::cooked_texture_subresource_source_t *subresources,
    common::u32 count, std::vector<common::byte> &file,
    common::cooked_texture_view_t &view )
{
    const auto source = common::span_t<const common::cooked_texture_subresource_source_t>{
        subresources, count };
    const auto required = common::CookedTexture_RequiredSizeSubresources( desc, source );
    REQUIRE( required > 0u );
    file.assign( required, 0u );
    const auto written = common::CookedTexture_WriteSubresources( desc, source, {},
        { file.data(), file.size() } );
    REQUIRE( common::CookedTexture_Succeeded( written ) );
    REQUIRE( written.cbWritten == file.size() );
    const auto read = common::CookedTexture_Read( { file.data(), file.size() }, &view );
    REQUIRE( common::CookedTexture_Succeeded( read ) );
}

std::vector<common::byte> BuildLegacyCookedTexture()
{
    constexpr common::byte pixels[]{ 0x10u, 0x20u, 0x30u, 0x40u };
    constexpr auto metadataSize = common::CY_COOKED_TEXTURE_METADATA_HEADER_SIZE_V1 +
                                  common::CY_COOKED_TEXTURE_MIP_RECORD_SIZE_V1;
    common::cooked_chunk_desc_t chunks[2]{};
    common::usize offset = common::CookedResource_PrefixSize( 2u );
    if ( !common::Cy_AlignUpChecked( offset, common::CY_COOKED_TEXTURE_METADATA_ALIGNMENT, offset ) ) {
        return {};
    }
    chunks[0].chunkType = common::CY_COOKED_TEXTURE_METADATA_CHUNK;
    chunks[0].flags = common::COOKED_CHUNK_FLAG_HAS_CONTENT_HASH;
    chunks[0].nAlignment = common::CY_COOKED_TEXTURE_METADATA_ALIGNMENT;
    chunks[0].iOffset = offset;
    chunks[0].cbStored = chunks[0].cbDecoded = metadataSize;
    offset += metadataSize;
    if ( !common::Cy_AlignUpChecked( offset, common::CY_COOKED_TEXTURE_DATA_ALIGNMENT, offset ) ) {
        return {};
    }
    chunks[1].chunkType = common::CY_COOKED_TEXTURE_DATA_CHUNK;
    chunks[1].flags = common::COOKED_CHUNK_FLAG_HAS_CONTENT_HASH;
    chunks[1].nAlignment = common::CY_COOKED_TEXTURE_DATA_ALIGNMENT;
    chunks[1].iOffset = offset;
    chunks[1].cbStored = chunks[1].cbDecoded = sizeof( pixels );
    chunks[1].contentHash = common::ContentHash_Data( { pixels, sizeof( pixels ) } );

    std::vector<common::byte> file( offset + sizeof( pixels ), 0u );
    // The v1 metadata header is 64 bytes, followed by one 32-byte mip record.
    // Keep this fixture as a compatibility input; the current writer emits v2.
    constexpr common::u32 fields[]{
        common::CY_COOKED_TEXTURE_METADATA_MAGIC,
        common::CY_COOKED_TEXTURE_METADATA_VERSION_V1,
        static_cast<common::u32>( common::CY_COOKED_TEXTURE_METADATA_HEADER_SIZE_V1 ),
        static_cast<common::u32>( common::render_texture_dimension_t::TEXTURE_2D ),
        static_cast<common::u32>( common::render_texture_pixel_format_t::RGBA8_SRGB ),
        static_cast<common::u32>( common::render_texture_usage_t::COLOR ),
        static_cast<common::u32>( common::render_texture_color_space_t::SRGB ),
        common::COOKED_TEXTURE_FLAG_NONE,
        1u, 1u, 1u, 1u, 1u, 1u, 0u, 0u, 0u, 1u, 1u, 1u, 4u, 1u
    };
    common::byte_writer_t writer{};
    if ( !common::ByteWriter_Init( &writer,
             { file.data() + chunks[0].iOffset, metadataSize },
             common::data_byte_order_t::LITTLE ) ) {
        return {};
    }
    for ( const auto field : fields ) {
        if ( !common::ByteWriter_WriteU32( &writer, field ) ) return {};
    }
    if ( !common::ByteWriter_WriteU64( &writer, sizeof( pixels ) ) ) return {};
    chunks[0].contentHash = common::ContentHash_Data(
        { file.data() + chunks[0].iOffset, metadataSize } );
    std::memcpy( file.data() + chunks[1].iOffset, pixels, sizeof( pixels ) );

    common::cooked_resource_header_t header{};
    header.resourceType = common::CY_RENDER_TEXTURE_RESOURCE_TYPE;
    header.nResourceVersion = common::CY_COOKED_TEXTURE_RESOURCE_VERSION_V1;
    header.nChunks = 2u;
    header.cbFile = file.size();
    if ( !common::CookedResource_Succeeded( common::CookedResource_WriteLayout(
             header, { chunks, 2u }, { file.data(), file.size() } ) ) ) {
        return {};
    }
    header.flags = common::COOKED_RESOURCE_FLAG_HAS_CONTENT_HASH;
    header.contentHash = common::CookedResource_ComputeContentHash(
        { file.data(), file.size() } );
    if ( !common::CookedResource_Succeeded( common::CookedResource_WriteLayout(
             header, { chunks, 2u }, { file.data(), file.size() } ) ) ) {
        return {};
    }
    return file;
}
}

TEST_CASE( "cooked RGBA8 images upload their exact pixels and retain no borrowed file storage",
    "[CypherRender][Texture][Cooked]" )
{
    texture_fixture_t fixture;
    fixture.Init();

    bool linear = false;
    SECTION( "linear" ) { linear = true; }
    SECTION( "sRGB" ) {}
    const common::byte pixels[16]{
        0x15u, 0x2Au, 0x3Fu, 0x80u, 0xE1u, 0xC2u, 0xA3u, 0xFFu,
        0x01u, 0x23u, 0x45u, 0x67u, 0x89u, 0xABu, 0xCDu, 0xEFu
    };
    const common::u32 height = linear ? 1u : 2u;
    const common::u64 pixelBytes = 8u * height;
    common::cooked_texture_desc_t desc{};
    desc.pixelFormat = linear ? common::render_texture_pixel_format_t::RGBA8_UNORM
                              : common::render_texture_pixel_format_t::RGBA8_SRGB;
    desc.storageFormat = linear ? common::render_format_t::RGBA8_UNORM
                                : common::render_format_t::RGBA8_SRGB;
    desc.colorSpace = linear ? common::render_texture_color_space_t::LINEAR
                             : common::render_texture_color_space_t::SRGB;
    desc.nWidth = 2u;
    desc.nHeight = height;
    desc.nMipLevels = 1u;
    const common::cooked_texture_mip_source_t mip{ 2u, height, 1u, 8u,
        { pixels, pixelBytes } };
    std::vector<common::byte> file;
    common::cooked_texture_view_t view{};
    WriteCookedTexture( desc, &mip, 1u, file, view );

    render::render_texture_handle_t texture{ 99u };
    REQUIRE( render::R_CreateTexture2DFromCooked( view, &texture ) == texture_error_t::OK );
    REQUIRE( render::R_IsTextureValid( texture ) );
    CHECK( fixture.creates == 1u );
    CHECK( fixture.captured.width == 2u );
    CHECK( fixture.captured.height == height );
    CHECK( fixture.captured.sRGB == !linear );
    CHECK( fixture.captured.format == render::render_texture_format_t::RGBA8 );
    CHECK_FALSE( fixture.captured.generateMips );
    CHECK_FALSE( fixture.captured.renderTarget );
    REQUIRE( fixture.capturedPixelBytes == pixelBytes );
    CHECK( std::memcmp( fixture.capturedPixels, pixels,
        static_cast<std::size_t>( pixelBytes ) ) == 0 );

    for ( auto &byte : file ) byte = 0u;
    file.clear();
    render::render_texture_info_t info{};
    REQUIRE( render::R_GetTextureInfo( texture, &info ) == texture_error_t::OK );
    CHECK( info.width == 2u );
    CHECK( info.height == height );
    CHECK( info.sRGB == !linear );
    CHECK( info.mipLevels == 1u );
    CHECK( std::memcmp( fixture.capturedPixels, pixels,
        static_cast<std::size_t>( pixelBytes ) ) == 0 );
    REQUIRE( render::R_DestroyTexture( texture ) == texture_error_t::OK );
}

TEST_CASE( "cooked texture adoption refuses unsupported color-image layouts",
    "[CypherRender][Texture][Cooked]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    common::cooked_texture_desc_t desc{};
    desc.nWidth = 2u;
    desc.nHeight = 2u;
    desc.nMipLevels = 1u;
    common::byte rgba[32]{};
    common::cooked_texture_mip_source_t mips[2]{
        { 2u, 2u, 1u, 8u, { rgba, 16u } },
        { 1u, 1u, 1u, 4u, { rgba + 16u, 4u } }
    };
    std::vector<common::byte> file;
    common::cooked_texture_view_t view{};
    render::render_texture_handle_t texture{ 99u };

    SECTION( "incomplete authored chain" ) {
        common::byte partial[40]{};
        desc.nWidth = 4u;
        desc.nMipLevels = 2u;
        const common::cooked_texture_mip_source_t partialMips[2]{
            { 4u, 2u, 1u, 16u, { partial, 32u } },
            { 2u, 1u, 1u, 8u, { partial + 32u, 8u } }
        };
        WriteCookedTexture( desc, partialMips, 2u, file, view );
    }
    SECTION( "streamed mip tail" ) {
        desc.nMipLevels = 2u;
        desc.residency = common::cooked_texture_residency_t::MIP_STREAMED;
        desc.nResidentMipLevels = 1u;
        WriteCookedTexture( desc, mips, 2u, file, view );
    }
    SECTION( "floating-point storage" ) {
        desc.nWidth = desc.nHeight = 1u;
        desc.pixelFormat = common::render_texture_pixel_format_t::RGBA32_FLOAT;
        desc.storageFormat = common::render_format_t::RGBA32_FLOAT;
        desc.colorSpace = common::render_texture_color_space_t::LINEAR;
        mips[0] = { 1u, 1u, 1u, 16u, { rgba, 16u } };
        WriteCookedTexture( desc, mips, 1u, file, view );
    }
    SECTION( "block-compressed storage" ) {
        common::byte blocks[64]{};
        desc.storageFormat = common::render_format_t::BC7_SRGB;
        desc.alphaMode = common::cooked_texture_alpha_mode_t::MASK;
        desc.alphaCutoff = 0.375f;
        desc.target = common::cooked_texture_target_t::DESKTOP;
        desc.nWidth = desc.nHeight = 5u;
        const common::cooked_texture_subresource_source_t compressed{
            0u, 0u, 0u, 0u, 5u, 5u, 1u, 32u, 64u,
            { blocks, sizeof( blocks ) }
        };
        WriteCookedSubresources( desc, &compressed, 1u, file, view );
    }
    SECTION( "non-color data" ) {
        desc.pixelFormat = common::render_texture_pixel_format_t::RGBA8_UNORM;
        desc.storageFormat = common::render_format_t::RGBA8_UNORM;
        desc.colorSpace = common::render_texture_color_space_t::LINEAR;
        desc.usage = common::render_texture_usage_t::DATA;
        desc.alphaMode = common::cooked_texture_alpha_mode_t::DATA;
        WriteCookedTexture( desc, mips, 1u, file, view );
    }
    SECTION( "volume texture" ) {
        desc.dimension = common::render_texture_dimension_t::TEXTURE_3D;
        desc.pixelFormat = common::render_texture_pixel_format_t::RGBA8_UNORM;
        desc.storageFormat = common::render_format_t::RGBA8_UNORM;
        desc.colorSpace = common::render_texture_color_space_t::LINEAR;
        desc.nDepth = 2u;
        const common::cooked_texture_subresource_source_t volume{
            0u, 0u, 0u, 0u, 2u, 2u, 2u, 8u, 16u,
            { rgba, sizeof( rgba ) }
        };
        WriteCookedSubresources( desc, &volume, 1u, file, view );
    }
    CHECK( render::R_CreateTexture2DFromCooked( view, &texture ) ==
           texture_error_t::ERR_RESOURCE_FORMAT_UNSUPPORTED );
    CHECK( texture.value == 0u );
    CHECK( fixture.creates == 0u );
}

TEST_CASE( "complete authored RGBA8 mips upload each level without regenerating pixels",
    "[CypherRender][Texture][Cooked][Mips]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    common::byte pixels[44]{};
    for ( common::u32 i = 0u; i < sizeof( pixels ); ++i ) pixels[i] = static_cast<common::byte>( i + 1u );
    common::cooked_texture_desc_t desc{};
    desc.nWidth = 4u;
    desc.nHeight = 2u;
    desc.nMipLevels = 3u;
    const common::cooked_texture_mip_source_t mips[3]{
        { 4u, 2u, 1u, 16u, { pixels, 32u } },
        { 2u, 1u, 1u, 8u, { pixels + 32u, 8u } },
        { 1u, 1u, 1u, 4u, { pixels + 40u, 4u } }
    };
    std::vector<common::byte> file;
    common::cooked_texture_view_t view{};
    WriteCookedTexture( desc, mips, 3u, file, view );

    render::render_texture_handle_t texture{};
    REQUIRE( render::R_CreateTexture2DFromCooked( view, &texture ) == texture_error_t::OK );
    CHECK( fixture.creates == 1u );
    CHECK( fixture.capturedMipCount == 3u );
    CHECK_FALSE( fixture.captured.generateMips );
    CHECK( fixture.capturedMipWidths[0] == 4u );
    CHECK( fixture.capturedMipHeights[0] == 2u );
    CHECK( fixture.capturedMipWidths[1] == 2u );
    CHECK( fixture.capturedMipHeights[1] == 1u );
    CHECK( fixture.capturedMipWidths[2] == 1u );
    CHECK( fixture.capturedMipHeights[2] == 1u );
    CHECK( fixture.capturedMipBytes[0] == 32u );
    CHECK( fixture.capturedMipBytes[1] == 8u );
    CHECK( fixture.capturedMipBytes[2] == 4u );
    CHECK( std::memcmp( fixture.capturedMipPixels[0], pixels, 32u ) == 0 );
    CHECK( std::memcmp( fixture.capturedMipPixels[1], pixels + 32u, 8u ) == 0 );
    CHECK( std::memcmp( fixture.capturedMipPixels[2], pixels + 40u, 4u ) == 0 );

    for ( auto &byte : file ) byte = 0u;
    file.clear();
    render::render_texture_info_t info{};
    REQUIRE( render::R_GetTextureInfo( texture, &info ) == texture_error_t::OK );
    CHECK( info.mipLevels == 3u );
    CHECK( info.completeMipChain );
    CHECK_FALSE( info.generateMips );
    CHECK( std::memcmp( fixture.capturedMipPixels[2], pixels + 40u, 4u ) == 0 );
    REQUIRE( render::R_DestroyTexture( texture ) == texture_error_t::OK );
}

TEST_CASE( "authored mip upload validates all levels and cleans backend failure",
    "[CypherRender][Texture][Cooked][Mips]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    const common::byte pixels[40]{};
    common::cooked_texture_desc_t desc{};
    desc.nWidth = desc.nHeight = 2u;
    desc.nMipLevels = 2u;
    const common::cooked_texture_mip_source_t mips[2]{
        { 2u, 2u, 1u, 8u, { pixels, 16u } },
        { 1u, 1u, 1u, 4u, { pixels + 16u, 4u } }
    };
    std::vector<common::byte> file;
    common::cooked_texture_view_t view{};
    WriteCookedTexture( desc, mips, 2u, file, view );
    render::render_texture_handle_t texture{ 99u };

    auto changed = view;
    changed.mips[1].cbRowPitch = 8u;
    CHECK( render::R_CreateTexture2DFromCooked( changed, &texture ) ==
           texture_error_t::ERR_INVALID_ARGUMENT );
    CHECK( texture.value == 0u );
    CHECK( fixture.creates == 0u );

    const auto createLevels = fixture.backend.CreateTexture2DLevels;
    fixture.backend.CreateTexture2DLevels = nullptr;
    texture = { 99u };
    CHECK( render::R_CreateTexture2DFromCooked( view, &texture ) ==
           texture_error_t::ERR_UNSUPPORTED );
    CHECK( texture.value == 0u );
    fixture.backend.CreateTexture2DLevels = createLevels;

    fixture.createResult = texture_error_t::ERR_OUT_OF_MEMORY;
    fixture.tokenOnCreateFailure = true;
    texture = { 99u };
    CHECK( render::R_CreateTexture2DFromCooked( view, &texture ) ==
           texture_error_t::ERR_OUT_OF_MEMORY );
    CHECK( texture.value == 0u );
    CHECK( fixture.creates == 1u );
    CHECK( fixture.destroys == 1u );
}

TEST_CASE( "cooked texture adoption rejects absent or changed borrowed file data",
    "[CypherRender][Texture][Cooked]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    const common::byte pixels[16]{ 2u, 3u, 5u, 7u, 11u, 13u, 17u, 19u,
        23u, 29u, 31u, 37u, 41u, 43u, 47u, 53u };
    common::cooked_texture_desc_t desc{};
    desc.nWidth = desc.nHeight = 2u;
    desc.nMipLevels = 1u;
    const common::cooked_texture_mip_source_t mip{ 2u, 2u, 1u, 8u,
        { pixels, sizeof( pixels ) } };
    std::vector<common::byte> file;
    common::cooked_texture_view_t view{};
    WriteCookedTexture( desc, &mip, 1u, file, view );

    render::render_texture_handle_t texture{ 99u };
    CHECK( render::R_CreateTexture2DFromCooked( view, nullptr ) ==
           texture_error_t::ERR_INVALID_ARGUMENT );
    auto absent = view;
    absent.fileBytes = {};
    CHECK( render::R_CreateTexture2DFromCooked( absent, &texture ) ==
           texture_error_t::ERR_INVALID_ARGUMENT );
    CHECK( texture.value == 0u );
    auto changedView = view;
    changedView.desc.nWidth = 1u;
    texture = { 99u };
    CHECK( render::R_CreateTexture2DFromCooked( changedView, &texture ) ==
           texture_error_t::ERR_INVALID_ARGUMENT );
    CHECK( texture.value == 0u );

    changedView = view;
    changedView.mips[0].cbRowPitch = 12u;
    texture = { 99u };
    CHECK( render::R_CreateTexture2DFromCooked( changedView, &texture ) ==
           texture_error_t::ERR_INVALID_ARGUMENT );
    CHECK( texture.value == 0u );

    auto *pixelInFile = const_cast<common::byte *>( view.mips[0].pixels.pData );
    REQUIRE( pixelInFile != nullptr );
    *pixelInFile ^= 0x80u;
    texture = { 99u };
    CHECK( render::R_CreateTexture2DFromCooked( view, &texture ) ==
           texture_error_t::ERR_INVALID_ARGUMENT );
    CHECK( texture.value == 0u );
    CHECK( fixture.creates == 0u );
}

TEST_CASE( "cooked texture adoption preserves backend creation failures and no handle escapes",
    "[CypherRender][Texture][Cooked]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    const common::byte pixels[4]{ 7u, 11u, 13u, 17u };
    common::cooked_texture_desc_t desc{};
    desc.nWidth = desc.nHeight = desc.nMipLevels = 1u;
    const common::cooked_texture_mip_source_t mip{ 1u, 1u, 1u, 4u,
        { pixels, sizeof( pixels ) } };
    std::vector<common::byte> file;
    common::cooked_texture_view_t view{};
    WriteCookedTexture( desc, &mip, 1u, file, view );

    fixture.createResult = texture_error_t::ERR_OUT_OF_MEMORY;
    render::render_texture_handle_t texture{ 99u };
    CHECK( render::R_CreateTexture2DFromCooked( view, &texture ) ==
           texture_error_t::ERR_OUT_OF_MEMORY );
    CHECK( texture.value == 0u );
    CHECK( fixture.creates == 1u );
    CHECK( fixture.destroys == 0u );
    fixture.tokenOnCreateFailure = true;
    texture = { 99u };
    CHECK( render::R_CreateTexture2DFromCooked( view, &texture ) ==
           texture_error_t::ERR_OUT_OF_MEMORY );
    CHECK( texture.value == 0u );
    CHECK( fixture.creates == 2u );
    CHECK( fixture.destroys == 1u );
}

TEST_CASE( "cooked texture adoption reads legacy CYTX v1 through the validated view",
    "[CypherRender][Texture][Cooked][Compatibility]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    auto file = BuildLegacyCookedTexture();
    REQUIRE_FALSE( file.empty() );
    common::cooked_texture_view_t view{};
    REQUIRE( common::CookedTexture_Succeeded( common::CookedTexture_Read(
        { file.data(), file.size() }, &view ) ) );
    REQUIRE( view.nResourceVersion == common::CY_COOKED_TEXTURE_RESOURCE_VERSION_V1 );

    render::render_texture_handle_t texture{};
    REQUIRE( render::R_CreateTexture2DFromCooked( view, &texture ) == texture_error_t::OK );
    CHECK( fixture.captured.width == 1u );
    CHECK( fixture.captured.height == 1u );
    CHECK( fixture.captured.sRGB );
    REQUIRE( fixture.capturedPixelBytes == 4u );
    const common::byte expected[]{ 0x10u, 0x20u, 0x30u, 0x40u };
    CHECK( std::memcmp( fixture.capturedPixels, expected, sizeof( expected ) ) == 0 );
    REQUIRE( render::R_DestroyTexture( texture ) == texture_error_t::OK );
}

TEST_CASE( "textures reject invalid sizes and incomplete pixels before backend upload", "[CypherRender][Texture]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    auto desc = fixture.Description();
    render::render_texture_handle_t out{ 99u };
    desc.width = 0u;
    CHECK( render::R_CreateTexture2D( desc, fixture.Data(), &out ) == texture_error_t::ERR_INVALID_ARGUMENT );
    CHECK( out.value == 0u );
    desc.width = 4097u;
    CHECK( render::R_CreateTexture2D( desc, fixture.Data(), &out ) == texture_error_t::ERR_RESOURCE_SIZE_UNSUPPORTED );
    desc.width = std::numeric_limits<common::u32>::max();
    desc.height = desc.width;
    CHECK( render::R_CreateTexture2D( desc, fixture.Data(), &out ) == texture_error_t::ERR_RESOURCE_SIZE_UNSUPPORTED );
    desc = fixture.Description();
    CHECK( render::R_CreateTexture2D( desc, { fixture.pixels, 23u }, &out ) == texture_error_t::ERR_INVALID_ARGUMENT );
    CHECK( render::R_CreateTexture2D( desc, { fixture.pixels, 25u }, &out ) == texture_error_t::ERR_INVALID_ARGUMENT );
    CHECK( render::R_CreateTexture2D( desc, { nullptr, 24u }, &out ) == texture_error_t::ERR_INVALID_ARGUMENT );
    CHECK( fixture.creates == 0u );
}
TEST_CASE( "D24 textures require GPU-only depth attachment storage", "[CypherRender][Texture][Depth]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    auto desc = fixture.Description();
    desc.width = 8u;
    desc.height = 4u;
    desc.sRGB = false;
    desc.generateMips = false;
    desc.repeat = false;
    desc.renderTarget = true;
    desc.format = render::render_texture_format_t::D24;
    render::render_texture_handle_t depth{ 99u };

    auto invalid = desc;
    invalid.renderTarget = false;
    CHECK( render::R_CreateTexture2D( invalid, {}, &depth ) == texture_error_t::ERR_INVALID_ARGUMENT );
    CHECK( depth.value == 0u );
    invalid = desc;
    invalid.sRGB = true;
    CHECK( render::R_CreateTexture2D( invalid, {}, &depth ) == texture_error_t::ERR_INVALID_ARGUMENT );
    invalid = desc;
    invalid.generateMips = true;
    CHECK( render::R_CreateTexture2D( invalid, {}, &depth ) == texture_error_t::ERR_INVALID_ARGUMENT );
    invalid = desc;
    invalid.repeat = true;
    CHECK( render::R_CreateTexture2D( invalid, {}, &depth ) == texture_error_t::ERR_INVALID_ARGUMENT );
    invalid = desc;
    invalid.format = static_cast<render::render_texture_format_t>( 255u );
    CHECK( render::R_CreateTexture2D( invalid, {}, &depth ) == texture_error_t::ERR_INVALID_ARGUMENT );
    CHECK( render::R_CreateTexture2D( desc, { fixture.pixels, sizeof( fixture.pixels ) }, &depth ) ==
           texture_error_t::ERR_INVALID_ARGUMENT );
    CHECK( render::R_CreateTexture2D( desc, { nullptr, 1u }, &depth ) ==
           texture_error_t::ERR_INVALID_ARGUMENT );
    CHECK( fixture.creates == 0u );

    REQUIRE( render::R_CreateTexture2D( desc, {}, &depth ) == texture_error_t::OK );
    CHECK( fixture.captured.format == render::render_texture_format_t::D24 );
    render::render_texture_info_t info{};
    REQUIRE( render::R_GetTextureInfo( depth, &info ) == texture_error_t::OK );
    CHECK( info.format == render::render_texture_format_t::D24 );
    CHECK( info.renderTarget );
    CHECK_FALSE( info.sRGB );
    CHECK_FALSE( info.generateMips );
    CHECK_FALSE( info.repeat );
    CHECK( info.mipLevels == 1u );
    REQUIRE( render::R_DestroyTexture( depth ) == texture_error_t::OK );
    CHECK_FALSE( render::R_IsTextureValid( depth ) );
}
TEST_CASE( "RGBA16F textures require a supported GPU-only linear target",
           "[CypherRender][Texture][HDR]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    render::render_texture_desc_t desc{};
    desc.width = 8u;
    desc.height = 4u;
    desc.sRGB = false;
    desc.generateMips = false;
    desc.repeat = false;
    desc.renderTarget = true;
    desc.format = render::render_texture_format_t::RGBA16F;
    render::render_texture_handle_t texture{ 99u };

    CHECK( render::R_CreateTexture2D( desc, {}, &texture ) ==
        texture_error_t::ERR_CAPABILITY_MISSING );
    CHECK( texture.value == 0u );
    CHECK( fixture.creates == 0u );
    render::tr.info.capabilities |= render::R_CAPABILITY_FLOAT_RENDER_TARGETS;

    auto invalid = desc;
    invalid.renderTarget = false;
    CHECK( render::R_CreateTexture2D( invalid, {}, &texture ) ==
        texture_error_t::ERR_INVALID_ARGUMENT );
    invalid = desc;
    invalid.sRGB = true;
    CHECK( render::R_CreateTexture2D( invalid, {}, &texture ) ==
        texture_error_t::ERR_INVALID_ARGUMENT );
    invalid = desc;
    invalid.generateMips = true;
    CHECK( render::R_CreateTexture2D( invalid, {}, &texture ) ==
        texture_error_t::ERR_INVALID_ARGUMENT );
    invalid = desc;
    invalid.repeat = true;
    CHECK( render::R_CreateTexture2D( invalid, {}, &texture ) ==
        texture_error_t::ERR_INVALID_ARGUMENT );
    CHECK( render::R_CreateTexture2D( desc, { fixture.pixels, sizeof( fixture.pixels ) },
        &texture ) == texture_error_t::ERR_INVALID_ARGUMENT );
    CHECK( render::R_CreateTexture2D( desc, { nullptr, 1u }, &texture ) ==
        texture_error_t::ERR_INVALID_ARGUMENT );
    CHECK( fixture.creates == 0u );

    REQUIRE( render::R_CreateTexture2D( desc, {}, &texture ) == texture_error_t::OK );
    CHECK( fixture.creates == 1u );
    CHECK( fixture.captured.format == render::render_texture_format_t::RGBA16F );
    CHECK( fixture.capturedPixelBytes == 0u );
    render::render_texture_info_t info{};
    REQUIRE( render::R_GetTextureInfo( texture, &info ) == texture_error_t::OK );
    CHECK( info.format == render::render_texture_format_t::RGBA16F );
    CHECK( info.renderTarget );
    CHECK_FALSE( info.sRGB );
    CHECK( info.mipLevels == 1u );
    REQUIRE( render::R_DestroyTexture( texture ) == texture_error_t::OK );
}
TEST_CASE( "textures retain immutable metadata and invalidate recycled generations", "[CypherRender][Texture]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    auto first = fixture.Create();
    render::render_texture_info_t info{};
    REQUIRE( render::R_GetTextureInfo( first, &info ) == texture_error_t::OK );
    CHECK( info.width == 3u );
    CHECK( info.height == 2u );
    CHECK( info.sRGB );
    CHECK( info.generateMips );
    CHECK( info.repeat );
    CHECK( info.mipLevels == 2u );
    REQUIRE( render::R_DestroyTexture( first ) == texture_error_t::OK );
    const auto second = fixture.Create();
    CHECK( second.value != first.value );
    CHECK_FALSE( render::R_IsTextureValid( first ) );
    CHECK( render::R_DestroyTexture( first ) == texture_error_t::ERR_STALE_HANDLE );
    CHECK( render::R_IsTextureValid( second ) );
    auto wrongKind = common::Cy_Handle64Make( common::Cy_Handle64Index( second ),
        common::Cy_Handle64Generation( second ), static_cast<common::u32>( render::render_object_type_t::BUFFER ) );
    CHECK( render::R_DestroyTexture( wrongKind ) == texture_error_t::ERR_RESOURCE_TYPE_MISMATCH );
    CHECK( fixture.destroys == 1u );
}
TEST_CASE( "texture failures publish no handle and failed destruction preserves ownership", "[CypherRender][Texture]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    fixture.createResult = texture_error_t::ERR_OUT_OF_MEMORY;
    render::render_texture_handle_t out{ 999u };
    CHECK( render::R_CreateTexture2D( fixture.Description(), fixture.Data(), &out ) == texture_error_t::ERR_OUT_OF_MEMORY );
    CHECK( out.value == 0u );
    fixture.tokenOnCreateFailure = true;
    CHECK( render::R_CreateTexture2D( fixture.Description(), fixture.Data(), &out ) == texture_error_t::ERR_OUT_OF_MEMORY );
    CHECK( out.value == 0u );
    CHECK( fixture.destroys == 1u );
    fixture.tokenOnCreateFailure = false;
    fixture.createResult = texture_error_t::OK;
    out = fixture.Create();
    fixture.destroyResult = texture_error_t::ERR_DEVICE_LOST;
    CHECK( render::R_DestroyTexture( out ) == texture_error_t::ERR_DEVICE_LOST );
    CHECK( render::R_IsTextureValid( out ) );
    fixture.destroyResult = texture_error_t::OK;
    CHECK( render::R_TextureSystemShutdown() == texture_error_t::OK );
    CHECK_FALSE( render::R_IsTextureValid( out ) );
    CHECK( fixture.destroys == 3u );
}
TEST_CASE( "texture-less backend reports unsupported without changing existing resources", "[CypherRender][Texture]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    fixture.backend.CreateTexture2D = nullptr;
    fixture.backend.DestroyTexture = nullptr;
    render::render_texture_handle_t texture{};
    CHECK( render::R_CreateTexture2D( fixture.Description(), fixture.Data(), &texture ) == texture_error_t::ERR_UNSUPPORTED );
    CHECK( texture.value == 0u );
    const auto draw = fixture.MakeDraw( false );
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::OK );
}
TEST_CASE( "sampled draws require a live texture of the right type", "[CypherRender][Texture][Draw]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    auto draw = fixture.MakeDraw( true );
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::ERR_BINDING_MISMATCH );
    draw.sampledTextures[0] = draw.pipeline;
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::ERR_RESOURCE_TYPE_MISMATCH );
    draw.sampledTextures[0] = fixture.Create();
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::OK );
    CHECK( fixture.capturedDraw.sampledTextures[0].value != 0u );
    REQUIRE( render::R_DestroyTexture( draw.sampledTextures[0] ) == texture_error_t::OK );
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::ERR_STALE_HANDLE );
    CHECK( fixture.draws == 1u );
}
TEST_CASE( "active depth attachment cannot be sampled until its pass ends",
           "[CypherRender][Texture][Depth][Draw]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    auto draw = fixture.MakeDraw( true );
    render::render_texture_desc_t desc{ 4u, 4u, false, false, false,
        "Sampleable depth", true, render::render_texture_format_t::D24 };
    render::render_texture_handle_t depth{};
    REQUIRE( render::R_CreateTexture2D( desc, {}, &depth ) == texture_error_t::OK );
    draw.sampledTextures[0] = depth;
    render::tr.activeTargetDepth = depth;
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::ERR_COMMAND_STATE_INVALID );
    CHECK( fixture.draws == 0u );
    render::tr.activeTargetDepth = {};
    REQUIRE( render::R_DrawIndexed( draw ) == texture_error_t::OK );
    CHECK( fixture.draws == 1u );
    CHECK( fixture.capturedDraw.sampledTextures[0].value != 0u );
    REQUIRE( render::R_DestroyTexture( depth ) == texture_error_t::OK );
}

TEST_CASE( "shadow draws require depth storage and an explicit comparison sampler",
           "[CypherRender][Texture][Depth][Sampler][Draw]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    auto shadow = fixture.MakeDraw( true, false,
        render::render_sampled_texture_kind_t::DEPTH_COMPARE_2D );
    auto scalar = fixture.MakeDraw( true );
    const auto color = fixture.Create();
    render::render_texture_desc_t depthDesc{ 4u, 4u, false, false, false,
        "Shadow depth", true, render::render_texture_format_t::D24 };
    render::render_texture_handle_t depth{};
    REQUIRE( render::R_CreateTexture2D( depthDesc, {}, &depth ) == texture_error_t::OK );
    render::render_sampler_desc_t compareDesc{};
    compareDesc.compare = render::render_sampler_compare_t::LESS_EQUAL;
    render::render_sampler_handle_t compare{};
    REQUIRE( render::R_CreateSampler( compareDesc, &compare ) == texture_error_t::OK );
    const auto raw = fixture.Sampler();

    shadow.sampledTextures[0] = color;
    shadow.samplers[0] = compare;
    CHECK( render::R_DrawIndexed( shadow ) == texture_error_t::ERR_BINDING_MISMATCH );
    shadow.sampledTextures[0] = depth;
    shadow.samplers[0] = {};
    CHECK( render::R_DrawIndexed( shadow ) == texture_error_t::ERR_BINDING_MISMATCH );
    shadow.samplers[0] = raw;
    CHECK( render::R_DrawIndexed( shadow ) == texture_error_t::ERR_BINDING_MISMATCH );
    shadow.samplers[0] = compare;
    REQUIRE( render::R_DrawIndexed( shadow ) == texture_error_t::OK );

    scalar.sampledTextures[0] = depth;
    scalar.samplers[0] = compare;
    CHECK( render::R_DrawIndexed( scalar ) == texture_error_t::ERR_BINDING_MISMATCH );
    scalar.samplers[0] = raw;
    REQUIRE( render::R_DrawIndexed( scalar ) == texture_error_t::OK );
    CHECK( fixture.draws == 2u );
}

TEST_CASE( "untextured pipelines reject an undeclared sampled binding", "[CypherRender][Texture][Draw]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    auto draw = fixture.MakeDraw( false );
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::OK );
    draw.sampledTextures[0] = fixture.Create();
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::ERR_BINDING_MISMATCH );
    CHECK( fixture.draws == 1u );
}

TEST_CASE( "two sampled textures and two uniform blocks bind in stable slots",
           "[CypherRender][Texture][Draw]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    auto draw = fixture.MakeDraw( true, true );
    const auto baseColor = fixture.Create();
    const auto detail = fixture.Create();
    const auto transform = fixture.Uniform( 64u );
    const auto material = fixture.Uniform( 32u );
    draw.sampledTextures[0] = baseColor;
    draw.sampledTextures[1] = detail;
    draw.uniformBuffers[0] = transform;
    draw.uniformBuffers[1] = material;

    REQUIRE( render::R_DrawIndexed( draw ) == texture_error_t::OK );
    CHECK( fixture.draws == 1u );
    CHECK( fixture.capturedDraw.sampledTextures[0].value != 0u );
    CHECK( fixture.capturedDraw.sampledTextures[1].value != 0u );
    CHECK( fixture.capturedDraw.sampledTextures[0].value !=
           fixture.capturedDraw.sampledTextures[1].value );
    CHECK( fixture.capturedDraw.uniformBuffers[0].value != 0u );
    CHECK( fixture.capturedDraw.uniformBuffers[1].value != 0u );
    CHECK( fixture.capturedDraw.uniformBuffers[0].value !=
           fixture.capturedDraw.uniformBuffers[1].value );

    auto invalid = draw;
    invalid.sampledTextures[1] = {};
    CHECK( render::R_DrawIndexed( invalid ) == texture_error_t::ERR_BINDING_MISMATCH );
    invalid = draw;
    invalid.uniformBuffers[1] = {};
    CHECK( render::R_DrawIndexed( invalid ) == texture_error_t::ERR_BINDING_MISMATCH );
    invalid = draw;
    invalid.sampledTextures[1] = draw.pipeline;
    CHECK( render::R_DrawIndexed( invalid ) == texture_error_t::ERR_RESOURCE_TYPE_MISMATCH );
    invalid = draw;
    invalid.uniformBuffers[1] = baseColor;
    CHECK( render::R_DrawIndexed( invalid ) == texture_error_t::ERR_RESOURCE_TYPE_MISMATCH );

    render::tr.activeTargetColor = detail;
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::ERR_COMMAND_STATE_INVALID );
    render::tr.activeTargetColor = {};

    const auto tooShort = fixture.Uniform( 31u );
    invalid = draw;
    invalid.uniformBuffers[1] = tooShort;
    CHECK( render::R_DrawIndexed( invalid ) == texture_error_t::ERR_BINDING_MISMATCH );
    CHECK( render::R_DestroyBuffer( tooShort ) == texture_error_t::OK );

    render::render_buffer_mapping_t mapped{};
    REQUIRE( render::R_MapBuffer( material, { 0u, 32u },
        render::R_BUFFER_MAP_WRITE, &mapped ) == texture_error_t::OK );
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::ERR_RESOURCE_BUSY );
    REQUIRE( render::R_UnmapBuffer( material ) == texture_error_t::OK );
    CHECK( fixture.draws == 1u ); // Validation never reached backend.

    fixture.drawResult = texture_error_t::ERR_SUBMISSION_FAILED;
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::ERR_SUBMISSION_FAILED );
    CHECK( fixture.draws == 2u );
    // Temporary draw references, including those from earlier slots, must be
    // released even when a later binding or the backend submission fails.
    CHECK( render::R_DestroyBuffer( transform ) == texture_error_t::OK );
    CHECK( render::R_DestroyBuffer( material ) == texture_error_t::OK );
    CHECK( render::R_DestroyTexture( baseColor ) == texture_error_t::OK );
    CHECK( render::R_DestroyTexture( detail ) == texture_error_t::OK );
}

TEST_CASE( "one-slot pipelines reject a resource in an undeclared second slot",
           "[CypherRender][Texture][Draw]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    auto draw = fixture.MakeDraw( true );
    draw.sampledTextures[0] = fixture.Create();
    draw.sampledTextures[1] = fixture.Create();
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::ERR_BINDING_MISMATCH );
    CHECK( fixture.draws == 0u );
}

TEST_CASE( "immutable samplers validate policy and retain inspected metadata",
           "[CypherRender][Sampler]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    render::render_sampler_desc_t desc{};
    CHECK( desc.minFilter == render::render_sampler_filter_t::LINEAR );
    CHECK( desc.magFilter == render::render_sampler_filter_t::LINEAR );
    CHECK( desc.mipFilter == render::render_sampler_mip_filter_t::NONE );
    CHECK( desc.compare == render::render_sampler_compare_t::NONE );

    render::render_sampler_handle_t out{ 99u };
    for ( const auto invalidFilter : {
              static_cast<render::render_sampler_filter_t>( 2u ),
              static_cast<render::render_sampler_filter_t>( 255u ) } ) {
        desc.minFilter = invalidFilter;
        CHECK( render::R_CreateSampler( desc, &out ) == texture_error_t::ERR_INVALID_ARGUMENT );
        CHECK( out.value == 0u );
    }
    desc.minFilter = render::render_sampler_filter_t::NEAREST;
    desc.magFilter = static_cast<render::render_sampler_filter_t>( 255u );
    CHECK( render::R_CreateSampler( desc, &out ) == texture_error_t::ERR_INVALID_ARGUMENT );
    desc.magFilter = render::render_sampler_filter_t::LINEAR;
    desc.mipFilter = static_cast<render::render_sampler_mip_filter_t>( 255u );
    CHECK( render::R_CreateSampler( desc, &out ) == texture_error_t::ERR_INVALID_ARGUMENT );
    desc.mipFilter = render::render_sampler_mip_filter_t::NEAREST;
    desc.addressU = static_cast<render::render_sampler_address_t>( 255u );
    CHECK( render::R_CreateSampler( desc, &out ) == texture_error_t::ERR_INVALID_ARGUMENT );
    desc.addressU = render::render_sampler_address_t::CLAMP_TO_EDGE;
    desc.addressV = static_cast<render::render_sampler_address_t>( 255u );
    CHECK( render::R_CreateSampler( desc, &out ) == texture_error_t::ERR_INVALID_ARGUMENT );
    desc.addressV = render::render_sampler_address_t::REPEAT;
    desc.compare = static_cast<render::render_sampler_compare_t>( 255u );
    CHECK( render::R_CreateSampler( desc, &out ) == texture_error_t::ERR_INVALID_ARGUMENT );
    CHECK( out.value == 0u );
    CHECK( fixture.samplerCreates == 0u );
    CHECK( render::R_CreateSampler( desc, nullptr ) == texture_error_t::ERR_INVALID_ARGUMENT );

    desc.compare = render::render_sampler_compare_t::LESS_EQUAL;
    desc.debugName = "Immutable sampler";
    REQUIRE( render::R_CreateSampler( desc, &out ) == texture_error_t::OK );
    CHECK( render::R_IsSamplerValid( out ) );
    CHECK( fixture.samplerCreates == 1u );
    CHECK( fixture.capturedSampler.minFilter == desc.minFilter );
    CHECK( fixture.capturedSampler.mipFilter == desc.mipFilter );
    CHECK( fixture.capturedSampler.compare == render::render_sampler_compare_t::LESS_EQUAL );
    render::render_sampler_info_t info{};
    REQUIRE( render::R_GetSamplerInfo( out, &info ) == texture_error_t::OK );
    CHECK( info.minFilter == desc.minFilter );
    CHECK( info.magFilter == desc.magFilter );
    CHECK( info.mipFilter == desc.mipFilter );
    CHECK( info.addressU == desc.addressU );
    CHECK( info.addressV == desc.addressV );
    CHECK( info.compare == desc.compare );
    CHECK( render::R_GetSamplerInfo( out, nullptr ) == texture_error_t::ERR_INVALID_ARGUMENT );

    const auto wrongKind = common::Cy_Handle64Make( common::Cy_Handle64Index( out ),
        common::Cy_Handle64Generation( out ), static_cast<common::u32>( render::render_object_type_t::TEXTURE ) );
    CHECK( render::R_DestroySampler( wrongKind ) == texture_error_t::ERR_RESOURCE_TYPE_MISMATCH );
    REQUIRE( render::R_DestroySampler( out ) == texture_error_t::OK );
    CHECK_FALSE( render::R_IsSamplerValid( out ) );
    CHECK( render::R_DestroySampler( out ) == texture_error_t::ERR_STALE_HANDLE );
    render::render_sampler_handle_t replacement{};
    REQUIRE( render::R_CreateSampler( desc, &replacement ) == texture_error_t::OK );
    CHECK( replacement.value != out.value );
    CHECK( render::R_GetSamplerInfo( out, &info ) == texture_error_t::ERR_STALE_HANDLE );
}

TEST_CASE( "sampler backend failure publishes no handle and failed destruction preserves ownership",
           "[CypherRender][Sampler]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    render::render_sampler_desc_t desc{};
    render::render_sampler_handle_t sampler{ 99u };
    fixture.samplerCreateResult = texture_error_t::ERR_OUT_OF_MEMORY;
    CHECK( render::R_CreateSampler( desc, &sampler ) == texture_error_t::ERR_OUT_OF_MEMORY );
    CHECK( sampler.value == 0u );
    CHECK( fixture.samplerDestroys == 0u );
    fixture.samplerTokenOnCreateFailure = true;
    CHECK( render::R_CreateSampler( desc, &sampler ) == texture_error_t::ERR_OUT_OF_MEMORY );
    CHECK( sampler.value == 0u );
    CHECK( fixture.samplerDestroys == 1u );
    fixture.samplerTokenOnCreateFailure = false;
    fixture.samplerCreateResult = texture_error_t::OK;
    sampler = fixture.Sampler();
    fixture.samplerDestroyResult = texture_error_t::ERR_DEVICE_LOST;
    CHECK( render::R_DestroySampler( sampler ) == texture_error_t::ERR_DEVICE_LOST );
    CHECK( render::R_IsSamplerValid( sampler ) );
    fixture.samplerDestroyResult = texture_error_t::OK;
    REQUIRE( render::R_DestroySampler( sampler ) == texture_error_t::OK );
    CHECK_FALSE( render::R_IsSamplerValid( sampler ) );
    CHECK( fixture.samplerDestroys == 3u );
}

TEST_CASE( "sampler-less backends report unsupported without affecting texture draws",
           "[CypherRender][Sampler]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    fixture.backend.CreateSampler = nullptr;
    fixture.backend.DestroySampler = nullptr;
    render::render_sampler_handle_t sampler{ 99u };
    CHECK( render::R_CreateSampler( {}, &sampler ) == texture_error_t::ERR_UNSUPPORTED );
    CHECK( sampler.value == 0u );
    auto draw = fixture.MakeDraw( true );
    draw.sampledTextures[0] = fixture.Create();
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::OK );
}

TEST_CASE( "draw samplers are optional but must match live textures and declared slots",
           "[CypherRender][Sampler][Draw]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    auto draw = fixture.MakeDraw( true, true );
    auto texture = fixture.Create();
    auto mipSampler = fixture.Sampler( render::render_sampler_mip_filter_t::LINEAR );
    auto nearestSampler = fixture.Sampler();
    draw.sampledTextures[0] = texture;
    draw.sampledTextures[1] = texture;
    draw.uniformBuffers[0] = fixture.Uniform( 64u );
    draw.uniformBuffers[1] = fixture.Uniform( 32u );
    draw.samplers[0] = nearestSampler;
    draw.samplers[1] = mipSampler;
    REQUIRE( render::R_DrawIndexed( draw ) == texture_error_t::OK );
    CHECK( fixture.capturedDraw.samplers[0].value != 0u );
    CHECK( fixture.capturedDraw.samplers[1].value != 0u );
    CHECK( fixture.capturedDraw.samplers[0].value != fixture.capturedDraw.samplers[1].value );

    auto invalid = draw;
    invalid.samplers[0] = draw.pipeline;
    CHECK( render::R_DrawIndexed( invalid ) == texture_error_t::ERR_RESOURCE_TYPE_MISMATCH );
    REQUIRE( render::R_DestroySampler( nearestSampler ) == texture_error_t::OK );
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::ERR_STALE_HANDLE );
    draw.samplers[0] = {}; // Legacy texture policy remains usable without a sampler.
    REQUIRE( render::R_DrawIndexed( draw ) == texture_error_t::OK );

    auto noMips = fixture.Description();
    noMips.generateMips = false;
    render::render_texture_handle_t singleLevel{};
    REQUIRE( render::R_CreateTexture2D( noMips, fixture.Data(), &singleLevel ) == texture_error_t::OK );
    invalid = draw;
    invalid.sampledTextures[1] = singleLevel;
    CHECK( render::R_DrawIndexed( invalid ) == texture_error_t::ERR_BINDING_MISMATCH );
    invalid.samplers[1] = {};
    REQUIRE( render::R_DrawIndexed( invalid ) == texture_error_t::OK );
    CHECK( render::R_DestroySampler( mipSampler ) == texture_error_t::OK );

    auto oneSlot = fixture.MakeDraw( true );
    oneSlot.sampledTextures[0] = texture;
    oneSlot.samplers[1] = fixture.Sampler();
    CHECK( render::R_DrawIndexed( oneSlot ) == texture_error_t::ERR_BINDING_MISMATCH );
    auto noTexture = fixture.MakeDraw( false );
    noTexture.samplers[0] = oneSlot.samplers[1];
    CHECK( render::R_DrawIndexed( noTexture ) == texture_error_t::ERR_BINDING_MISMATCH );
}
