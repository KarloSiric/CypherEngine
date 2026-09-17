//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Tests cooked preview material decoding without a GPU context.
//////////////////////////////////////////////////////////////////////////
#include "Core/CypherTileMaterialPreview.h"
#include "CypherCommon/Formats/CypherCommon_CookedMaterial.h"
#include "CypherCommon/Formats/CypherCommon_CookedTexture.h"

#include <catch2/catch_test_macros.hpp>
#include <QTemporaryDir>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

using namespace cypher::common;
using namespace cypher::tools::tile_editor;

namespace {
template <usize N>
constexpr string_view_t Text( const char ( &text )[N] ) { return { text, N - 1u }; }

void WriteBytes( const std::filesystem::path &path, const std::vector<byte> &bytes )
{
    std::filesystem::create_directories( path.parent_path() );
    std::ofstream file( path, std::ios::binary | std::ios::trunc );
    REQUIRE( file.good() );
    file.write( reinterpret_cast<const char *>( bytes.data() ),
        static_cast<std::streamsize>( bytes.size() ) );
    REQUIRE( file.good() );
}

struct fixture_t {
    QTemporaryDir directory{};
    std::filesystem::path root{};
    cooked_material_texture_source_t binding{ Text( "base_color" ), Text( "textures/check.cytex" ) };
    cooked_material_parameter_source_t parameters[2]{};
    cooked_material_source_t material{};
    byte base[16]{ 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 64, 128, 192, 255 };
    byte small[4]{ 96, 96, 96, 255 };

    fixture_t()
    {
        REQUIRE( directory.isValid() );
        root = directory.path().toStdString();
        material.shader = Text( "shaders/tile_surface.cyshader" );
        material.textures = { &binding, 1u };
        parameters[0].name = Text( "tint" );
        parameters[0].type = render_material_parameter_type_t::VECTOR;
        parameters[0].nComponents = 4u;
        parameters[0].values[0] = 0.25;
        parameters[0].values[1] = 0.5;
        parameters[0].values[2] = 0.75;
        parameters[0].values[3] = 1.0;
        parameters[1].name = Text( "uv_scale" );
        parameters[1].type = render_material_parameter_type_t::VECTOR;
        parameters[1].nComponents = 2u;
        parameters[1].values[0] = 2.0;
        parameters[1].values[1] = -3.0;
        material.parameters = { parameters, 2u };
        writeTexture();
        writeMaterial();
    }

    void writeMaterial()
    {
        std::vector<byte> bytes( CookedMaterial_RequiredSize( material ) );
        REQUIRE_FALSE( bytes.empty() );
        REQUIRE( CookedMaterial_Succeeded( CookedMaterial_Write(
            material, {}, { bytes.data(), bytes.size() } ) ) );
        WriteBytes( root / "materials/check.cymat_c", bytes );
    }

    void writeTexture( bool linear = false, bool mipmapped = false,
        render_texture_usage_t usage = render_texture_usage_t::COLOR )
    {
        cooked_texture_desc_t desc{};
        desc.nWidth = 2u;
        desc.nHeight = 2u;
        desc.nMipLevels = mipmapped ? 2u : 1u;
        desc.flags = mipmapped ? COOKED_TEXTURE_FLAG_GENERATED_MIPS : COOKED_TEXTURE_FLAG_NONE;
        desc.pixelFormat = linear ? render_texture_pixel_format_t::RGBA8_UNORM : render_texture_pixel_format_t::RGBA8_SRGB;
        desc.colorSpace = linear ? render_texture_color_space_t::LINEAR : render_texture_color_space_t::SRGB;
        desc.usage = usage;
        cooked_texture_mip_source_t mips[2]{
            { 2u, 2u, 1u, 8u, { base, sizeof( base ) } },
            { 1u, 1u, 1u, 4u, { small, sizeof( small ) } }
        };
        const span_t<const cooked_texture_mip_source_t> mipSpan{ mips, desc.nMipLevels };
        std::vector<byte> bytes( CookedTexture_RequiredSize( desc, mipSpan ) );
        REQUIRE_FALSE( bytes.empty() );
        REQUIRE( CookedTexture_Succeeded( CookedTexture_Write(
            desc, mipSpan, {}, { bytes.data(), bytes.size() } ) ) );
        WriteBytes( root / "textures/check.cytex_c", bytes );
    }
};

void CheckReadFailurePreservesSource( fixture_t &fixture, const char *expected )
{
    tile_material_preview_source_t source{};
    source.width = 7u;
    source.height = 9u;
    source.pixels = { 12u, 34u, 56u, 78u };
    source.tint[0] = 0.125f;
    source.uvScale[1] = 7.0f;
    std::string error;
    CHECK_FALSE( CypherTileMaterialPreview_Read( fixture.root,
        "materials/check.cymat", source, error ) );
    CHECK( error.find( expected ) != std::string::npos );
    CHECK( source.width == 7u );
    CHECK( source.height == 9u );
    CHECK( source.pixels == std::vector<byte>{ 12u, 34u, 56u, 78u } );
    CHECK( source.tint[0] == 0.125f );
    CHECK( source.uvScale[1] == 7.0f );
}
} // namespace

TEST_CASE( "Preview reads cooked base color and material transforms", "[TileEditor][MaterialPreview]" )
{
    fixture_t fixture{};
    tile_material_preview_source_t source{};
    std::string error = "stale error";
    REQUIRE( CypherTileMaterialPreview_Read( fixture.root,
        "materials/check.cymat", source, error ) );
    CHECK( error.empty() );
    CHECK( source.width == 2u );
    CHECK( source.height == 2u );
    CHECK( source.sRGB );
    CHECK_FALSE( source.generateMips );
    CHECK( source.pixels == std::vector<byte>( fixture.base, fixture.base + sizeof( fixture.base ) ) );
    CHECK( source.tint[0] == 0.25f );
    CHECK( source.tint[1] == 0.5f );
    CHECK( source.tint[2] == 0.75f );
    CHECK( source.tint[3] == 1.0f );
    CHECK( source.uvScale[0] == 2.0f );
    CHECK( source.uvScale[1] == -3.0f );
}

TEST_CASE( "Preview respects cooked linear encoding and mip policy", "[TileEditor][MaterialPreview]" )
{
    fixture_t fixture{};
    fixture.writeTexture( true, true );
    fixture.material.parameters = {};
    fixture.writeMaterial();
    tile_material_preview_source_t source{};
    std::string error;
    REQUIRE( CypherTileMaterialPreview_Read( fixture.root, "materials/check.cymat", source, error ) );
    CHECK_FALSE( source.sRGB );
    CHECK( source.generateMips );
    CHECK( source.pixels.size() == 16u );
    CHECK( source.tint[0] == 1.0f );
    CHECK( source.uvScale[0] == 1.0f );
}

TEST_CASE( "Preview rejects incompatible shader and sampler contracts atomically", "[TileEditor][MaterialPreview]" )
{
    fixture_t fixture{};
    SECTION( "Shader" ) {
        fixture.material.shader = Text( "shaders/other.cyshader" );
        fixture.writeMaterial();
        CheckReadFailurePreservesSource( fixture, "tile_surface.cyshader" );
    }
    SECTION( "Binding name" ) {
        fixture.binding.binding = Text( "normal_map" );
        fixture.writeMaterial();
        CheckReadFailurePreservesSource( fixture, "base_color" );
    }
    SECTION( "Missing binding" ) {
        fixture.material.textures = {};
        fixture.writeMaterial();
        CheckReadFailurePreservesSource( fixture, "base_color" );
    }
    SECTION( "Non-color texture" ) {
        fixture.writeTexture( true, false, render_texture_usage_t::DATA );
        CheckReadFailurePreservesSource( fixture, "color texture" );
    }
}

TEST_CASE( "Preview rejects unsupported material parameters atomically", "[TileEditor][MaterialPreview]" )
{
    fixture_t fixture{};
    SECTION( "Unknown parameter" ) {
        fixture.parameters[0].name = Text( "roughness" );
        fixture.writeMaterial();
        CheckReadFailurePreservesSource( fixture, "Unsupported" );
    }
    SECTION( "Wrong component count" ) {
        fixture.parameters[0].nComponents = 3u;
        fixture.parameters[0].values[3] = 0.0;
        fixture.writeMaterial();
        CheckReadFailurePreservesSource( fixture, "4 components" );
    }
    SECTION( "Zero UV scale" ) {
        fixture.parameters[1].values[0] = 0.0;
        fixture.writeMaterial();
        CheckReadFailurePreservesSource( fixture, "nonzero" );
    }
    SECTION( "Finite double beyond float range" ) {
        fixture.parameters[1].values[0] = std::numeric_limits<double>::max();
        fixture.writeMaterial();
        CheckReadFailurePreservesSource( fixture, "finite float range" );
    }
}

TEST_CASE( "Preview preserves previous pixels for missing or damaged dependencies", "[TileEditor][MaterialPreview]" )
{
    fixture_t fixture{};
    SECTION( "Missing texture" ) {
        std::filesystem::remove( fixture.root / "textures/check.cytex_c" );
        CheckReadFailurePreservesSource( fixture, "check.cytex_c" );
    }
    SECTION( "Truncated texture" ) {
        WriteBytes( fixture.root / "textures/check.cytex_c", { 0u, 1u, 2u, 3u } );
        CheckReadFailurePreservesSource( fixture, "Invalid cooked texture" );
    }
    SECTION( "Truncated material" ) {
        WriteBytes( fixture.root / "materials/check.cymat_c", { 0u, 1u, 2u, 3u } );
        CheckReadFailurePreservesSource( fixture, "Invalid cooked material" );
    }
}

TEST_CASE( "Preview material paths stay inside the cooked resource root", "[TileEditor][MaterialPreview]" )
{
    fixture_t fixture{};
    tile_material_preview_source_t source{};
    std::string error;
    for ( const char *path : { "../materials/check.cymat", "/materials/check.cymat",
            "materials/../materials/check.cymat", "materials/check.cytex", "materials/check.cymat_c" } ) {
        CAPTURE( path );
        CHECK_FALSE( CypherTileMaterialPreview_Read( fixture.root, path, source, error ) );
        CHECK( error.find( "canonical relative .cymat" ) != std::string::npos );
    }
    CHECK_FALSE( CypherTileMaterialPreview_Read( {}, "materials/check.cymat", source, error ) );
    CHECK( error.find( "asset root" ) != std::string::npos );
}
