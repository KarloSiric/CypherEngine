//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Verify cached orthographic materials, bounded images, and UV display.
//////////////////////////////////////////////////////////////////////////
#include "Gui/CypherTileOrthoMaterials.h"
#include "Core/CypherTileMapMaterials.h"
#include "CypherCommon/Formats/CypherCommon_CookedMaterial.h"
#include "CypherCommon/Formats/CypherCommon_CookedTexture.h"

#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QDir>
#include <QPainter>
#include <QTemporaryDir>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace cypher::common;
using namespace cypher::tools::tile_editor;

namespace {

template <usize N>
constexpr string_view_t Text( const char ( &text )[N] ) { return { text, N - 1u }; }

void EnsureOrthoMaterialApplication()
{
    if ( QApplication::instance() != nullptr ) return;
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    static int argc = 1;
    static char name[] = "CypherTileOrthoMaterialsTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
}

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
    tile_map_document_t document{};
    cooked_material_texture_source_t texture{ Text( "base_color" ), Text( "textures/check.cytex" ) };
    cooked_material_parameter_source_t parameters[2]{};
    cooked_material_source_t material{};

    fixture_t()
    {
        EnsureOrthoMaterialApplication();
        REQUIRE( directory.isValid() );
        root = directory.path().toStdString();
        REQUIRE( CypherTileMapDocument_Init( &document, Allocator_GetSystem(),
            { 4u, 4u, 2.0f, 3.0f } ) == tile_map_document_status_t::OK );
        material.shader = Text( "shaders/tile_surface.cyshader" );
        material.textures = { &texture, 1u };
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
        parameters[1].values[1] = -1.0;
        material.parameters = { parameters, 2u };
        writeMaterial();
        writeTexture();
    }

    ~fixture_t() { CypherTileMapDocument_Shutdown( &document ); }

    void bind( u16 slot, const char *path = "materials/check.cymat" )
    {
        REQUIRE( CypherTileMapDocument_SetMaterialBinding( &document, slot,
            StringView_FromCString( path ) ) == tile_map_document_status_t::OK );
    }

    void writeMaterial()
    {
        std::vector<byte> bytes( CookedMaterial_RequiredSize( material ) );
        REQUIRE_FALSE( bytes.empty() );
        REQUIRE( CookedMaterial_Succeeded( CookedMaterial_Write(
            material, {}, { bytes.data(), bytes.size() } ) ) );
        WriteBytes( root / "materials/check.cymat_c", bytes );
    }

    void writeTexture( u32 width = 2u, u32 height = 2u, bool linear = false )
    {
        cooked_texture_desc_t desc{};
        desc.nWidth = width;
        desc.nHeight = height;
        desc.nMipLevels = 1u;
        desc.pixelFormat = linear ? render_texture_pixel_format_t::RGBA8_UNORM
            : render_texture_pixel_format_t::RGBA8_SRGB;
        desc.colorSpace = linear ? render_texture_color_space_t::LINEAR
            : render_texture_color_space_t::SRGB;
        desc.usage = render_texture_usage_t::COLOR;
        const byte colors[4][4]{ { 255, 0, 0, 255 }, { 0, 255, 0, 255 },
            { 0, 0, 255, 255 }, { 64, 128, 192, 255 } };
        std::vector<byte> pixels( static_cast<usize>( width ) * height * 4u );
        for ( usize i = 0u; i < pixels.size() / 4u; ++i )
            std::memcpy( pixels.data() + i * 4u, colors[i % 4u], 4u );
        const cooked_texture_mip_source_t mip{ width, height, 1u, width * 4u,
            { pixels.data(), pixels.size() } };
        const span_t<const cooked_texture_mip_source_t> mips{ &mip, 1u };
        std::vector<byte> bytes( CookedTexture_RequiredSize( desc, mips ) );
        REQUIRE_FALSE( bytes.empty() );
        REQUIRE( CookedTexture_Succeeded( CookedTexture_Write(
            desc, mips, {}, { bytes.data(), bytes.size() } ) ) );
        WriteBytes( root / "textures/check.cytex_c", bytes );
    }
};

QImage Paint( const tile_ortho_material_cache_t *cache, u16 slot, qreal opacity = 1.0 )
{
    QImage image( 64, 32, QImage::Format_ARGB32 );
    image.fill( Qt::transparent );
    QPainter painter( &image );
    TileOrthoMaterials_Paint( painter, QRectF( 0.0, 0.0, 64.0, 32.0 ), cache, slot, opacity );
    return image;
}

} // namespace

TEST_CASE( "Orthographic cache decodes tinted material once and shares duplicate slot images",
    "[TileEditor][OrthoMaterials]" )
{
    fixture_t fixture;
    fixture.bind( 8u );
    fixture.bind( 12u );
    tile_ortho_material_cache_t cache{};
    REQUIRE( TileOrthoMaterials_Refresh( cache, fixture.document, fixture.directory.path() ) );
    REQUIRE( cache.entries.size() == 2 );
    const auto *first = TileOrthoMaterials_Find( cache, 8u );
    const auto *second = TileOrthoMaterials_Find( cache, 12u );
    REQUIRE( first != nullptr );
    REQUIRE( second != nullptr );
    CHECK( first->bound );
    CHECK( first->error.isEmpty() );
    REQUIRE( first->image.size() == QSize( 2, 2 ) );
    CHECK( first->image.constBits() == second->image.constBits() );
    CHECK( first->textureBrush.style() == Qt::TexturePattern );
    CHECK( first->image.pixelColor( 0, 0 ) == QColor( 137, 0, 0 ) );
    CHECK( first->image.pixelColor( 1, 0 ) == QColor( 0, 188, 0 ) );
    CHECK( first->image.pixelColor( 0, 1 ) == QColor( 0, 0, 225 ) );
    CHECK( first->textureWidth == 2u );
    CHECK( first->textureHeight == 2u );
    CHECK( first->sRGB );
    CHECK_FALSE( first->generateMips );
    CHECK( first->tint[0] == 0.25f );
    CHECK( first->tint[1] == 0.5f );
    CHECK( first->tint[2] == 0.75f );
    CHECK( first->tint[3] == 1.0f );
    CHECK( first->uvScale[0] == 2.0f );
    CHECK( first->uvScale[1] == -1.0f );
    const QString description = TileOrthoMaterials_Describe( &cache, 8u );
    CHECK( description.contains( "Slot 8" ) );
    CHECK( description.contains( "materials/check.cymat" ) );
}

TEST_CASE( "Cell edits and additional slots reuse orthographic materials without asset reads",
    "[TileEditor][OrthoMaterials]" )
{
    fixture_t fixture;
    fixture.bind( 8u );
    tile_ortho_material_cache_t cache{};
    REQUIRE( TileOrthoMaterials_Refresh( cache, fixture.document, fixture.directory.path() ) );
    const QImage retained = TileOrthoMaterials_Find( cache, 8u )->image;
    std::filesystem::remove_all( fixture.root / "textures" );
    std::filesystem::remove_all( fixture.root / "materials" );
    REQUIRE( CypherTileMapDocument_PaintCell( &fixture.document, { 1, 1 }, {} ) ==
        tile_map_document_status_t::OK );
    CHECK_FALSE( TileOrthoMaterials_Refresh( cache, fixture.document, fixture.directory.path() ) );
    CHECK_FALSE( TileOrthoMaterials_Refresh( cache, fixture.document,
        QDir( fixture.directory.path() ).filePath( "." ) ) );
    fixture.bind( 9u );
    REQUIRE( TileOrthoMaterials_Refresh( cache, fixture.document, fixture.directory.path() ) );
    REQUIRE( TileOrthoMaterials_Find( cache, 9u ) != nullptr );
    CHECK( TileOrthoMaterials_Find( cache, 9u )->image.constBits() == retained.constBits() );
    CHECK( TileOrthoMaterials_Find( cache, 9u )->error.isEmpty() );
    CHECK( Paint( &cache, 8u ).pixelColor( 4, 4 ).blue() > 0 );
}

TEST_CASE( "Failed orthographic material rebinding never displays stale pixels",
    "[TileEditor][OrthoMaterials]" )
{
    fixture_t fixture;
    fixture.bind( 3u );
    tile_ortho_material_cache_t cache{};
    REQUIRE( TileOrthoMaterials_Refresh( cache, fixture.document, fixture.directory.path() ) );
    fixture.bind( 3u, "materials/missing.cymat" );
    REQUIRE( TileOrthoMaterials_Refresh( cache, fixture.document, fixture.directory.path() ) );
    const auto *missing = TileOrthoMaterials_Find( cache, 3u );
    REQUIRE( missing != nullptr );
    CHECK( missing->bound );
    CHECK( missing->image.isNull() );
    CHECK_FALSE( missing->error.isEmpty() );
    CHECK( TileOrthoMaterials_Describe( &cache, 3u ).contains( "Unavailable:" ) );
    const QImage diagnostic = Paint( &cache, 3u );
    CHECK( diagnostic.pixelColor( 2, 2 ) != diagnostic.pixelColor( 10, 2 ) );
    CHECK( diagnostic.pixelColor( 10, 2 ).red() > diagnostic.pixelColor( 10, 2 ).green() );

    fixture.bind( 3u, "" );
    REQUIRE( TileOrthoMaterials_Refresh( cache, fixture.document, fixture.directory.path() ) );
    CHECK( TileOrthoMaterials_Find( cache, 3u ) == nullptr );
    CHECK( TileOrthoMaterials_Describe( &cache, 3u ).contains( "Hazard Yellow" ) );
    CHECK( Paint( &cache, 3u ) == Paint( nullptr, 3u ) );
}

TEST_CASE( "Root changes and forced refresh reload cooked orthographic materials",
    "[TileEditor][OrthoMaterials]" )
{
    fixture_t fixture;
    fixture_t other;
    other.parameters[0].values[0] = 1.0;
    other.writeMaterial();
    fixture.bind( 8u );
    tile_ortho_material_cache_t cache{};
    REQUIRE( TileOrthoMaterials_Refresh( cache, fixture.document, fixture.directory.path() ) );
    REQUIRE( TileOrthoMaterials_Refresh( cache, fixture.document, other.directory.path() ) );
    CHECK( TileOrthoMaterials_Find( cache, 8u )->image.pixelColor( 0, 0 ) == QColor( 255, 0, 0 ) );
    other.parameters[0].values[0] = 0.0;
    other.writeMaterial();
    CHECK_FALSE( TileOrthoMaterials_Refresh( cache, fixture.document, other.directory.path() ) );
    REQUIRE( TileOrthoMaterials_Refresh( cache, fixture.document, other.directory.path(), true ) );
    CHECK( TileOrthoMaterials_Find( cache, 8u )->image.pixelColor( 0, 0 ) == QColor( 0, 0, 0 ) );
    std::filesystem::remove( other.root / "textures/check.cytex_c" );
    REQUIRE( TileOrthoMaterials_Refresh( cache, fixture.document, other.directory.path(), true ) );
    CHECK( TileOrthoMaterials_Find( cache, 8u )->image.isNull() );
    other.writeTexture();
    REQUIRE( TileOrthoMaterials_Refresh( cache, fixture.document, other.directory.path(), true ) );
    CHECK_FALSE( TileOrthoMaterials_Find( cache, 8u )->image.isNull() );
    REQUIRE( TileOrthoMaterials_Refresh( cache, fixture.document, QString() ) );
    CHECK( TileOrthoMaterials_Find( cache, 8u )->error.contains( "asset root" ) );
}

TEST_CASE( "Orthographic thumbnails are bounded and linear textures display with sRGB encoding",
    "[TileEditor][OrthoMaterials]" )
{
    fixture_t fixture;
    fixture.bind( 8u );
    tile_ortho_material_cache_t cache{};
    SECTION( "Bounded large source" ) {
        fixture.writeTexture( 512u, 128u );
        REQUIRE( TileOrthoMaterials_Refresh( cache, fixture.document, fixture.directory.path() ) );
        const auto *material = TileOrthoMaterials_Find( cache, 8u );
        REQUIRE( material != nullptr );
        CHECK( material->image.size() == QSize( 256, 64 ) );
        CHECK( material->image.sizeInBytes() <= 256 * 256 * 4 );
    }
    SECTION( "Linear base image" ) {
        fixture.material.parameters = {};
        fixture.writeMaterial();
        fixture.writeTexture( 2u, 2u, true );
        REQUIRE( TileOrthoMaterials_Refresh( cache, fixture.document, fixture.directory.path() ) );
        const QColor encoded = TileOrthoMaterials_Find( cache, 8u )->image.pixelColor( 1, 1 );
        CHECK( encoded.red() == 137 );
        CHECK( encoded.green() == 188 );
        CHECK( encoded.blue() == 225 );
    }
    SECTION( "Malformed bounded path" ) {
        std::memset( fixture.document.materialBindings.pData[0].path, 'x', TILE_MAP_MATERIAL_PATH_CAPACITY );
        REQUIRE( TileOrthoMaterials_Refresh( cache, fixture.document, fixture.directory.path() ) );
        const auto *material = TileOrthoMaterials_Find( cache, 8u );
        REQUIRE( material != nullptr );
        CHECK( material->image.isNull() );
        CHECK( material->error.contains( "NUL" ) );
    }
}

TEST_CASE( "Orthographic material painting repeats and mirrors UVs without changing painter state",
    "[TileEditor][OrthoMaterials]" )
{
    EnsureOrthoMaterialApplication();
    tile_ortho_material_cache_t cache{};
    tile_ortho_material_t material{};
    material.bound = true;
    material.image = QImage( 2, 2, QImage::Format_ARGB32 );
    material.image.setPixelColor( 0, 0, Qt::red );
    material.image.setPixelColor( 1, 0, Qt::green );
    material.image.setPixelColor( 0, 1, Qt::blue );
    material.image.setPixelColor( 1, 1, Qt::white );
    material.textureBrush = QBrush( material.image );
    material.uvScale[0] = 2.0f;
    cache.entries.insert( 8u, material );
    const QImage repeated = Paint( &cache, 8u );
    CHECK( repeated.pixelColor( 4, 4 ) == QColor( Qt::red ) );
    CHECK( repeated.pixelColor( 20, 4 ) == QColor( Qt::green ) );
    CHECK( repeated.pixelColor( 36, 4 ) == QColor( Qt::red ) );
    CHECK( repeated.pixelColor( 4, 24 ) == QColor( Qt::blue ) );
    cache.entries[8u].uvScale[0] = -2.0f;
    const QImage mirrored = Paint( &cache, 8u );
    CHECK( mirrored.pixelColor( 4, 4 ) == QColor( Qt::green ) );
    CHECK( mirrored.pixelColor( 20, 4 ) == QColor( Qt::red ) );

    QImage transparent( 80, 48, QImage::Format_ARGB32 );
    transparent.fill( Qt::transparent );
    QPainter painter( &transparent );
    painter.setOpacity( 0.5 );
    painter.setPen( Qt::yellow );
    painter.setBrush( Qt::cyan );
    const QTransform transform = painter.transform();
    TileOrthoMaterials_Paint( painter, QRectF( 8.0, 8.0, 64.0, 32.0 ), &cache, 8u, 0.5 );
    CHECK( painter.opacity() == 0.5 );
    CHECK( painter.pen().color() == QColor( Qt::yellow ) );
    CHECK( painter.brush().color() == QColor( Qt::cyan ) );
    CHECK( painter.transform() == transform );
    painter.end();
    CHECK( transparent.pixelColor( 0, 0 ).alpha() == 0 );
    CHECK( std::abs( transparent.pixelColor( 12, 12 ).alpha() - 64 ) <= 1 );
}

TEST_CASE( "Unbound orthographic slots use the shared palette without allocating cache records",
    "[TileEditor][OrthoMaterials]" )
{
    EnsureOrthoMaterialApplication();
    tile_ortho_material_cache_t cache{};
    CHECK( TileOrthoMaterials_Find( cache, 0u ) == nullptr );
    CHECK( TileOrthoMaterials_Describe( nullptr, 6u ).contains( "Cypher Blue" ) );
    CHECK( TileOrthoMaterials_Describe( &cache, 65535u ).contains( "Unknown Material" ) );
    CHECK( Paint( &cache, 6u ).pixelColor( 4, 4 ) != Paint( &cache, 7u ).pixelColor( 4, 4 ) );
    const auto definition = CypherTileMapMaterial_Resolve( 6u );
    CHECK( Paint( &cache, 6u ).pixelColor( 4, 4 ).rgba() ==
        QColor::fromRgbF( definition.colorR, definition.colorG, definition.colorB ).rgba() );
    CHECK( cache.entries.isEmpty() );
}
