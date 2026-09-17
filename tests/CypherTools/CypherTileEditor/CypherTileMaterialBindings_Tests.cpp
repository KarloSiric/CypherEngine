//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code
// Copyright (c) 2026 Karlo Siric. All rights reserved.
// File: CypherTileMaterialBindings_Tests.cpp
// Purpose: Verifies material path ownership, atomic history, and map persistence.
//////////////////////////////////////////////////////////////////////////

#include "CypherTileMapDocument.h"
#include "CypherTileMapSerialization.h"
#include "CypherCommon_Allocator.h"
#include "CypherCommon_TextBuffer.h"

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <string>

using namespace cypher::common;
using namespace cypher::tools::tile_editor;

namespace
{
struct binding_fixture_t {
    tile_map_document_t document{};
    explicit binding_fixture_t( const allocator_t *allocator = Allocator_GetSystem() )
    {
        REQUIRE( CypherTileMapDocument_Init( &document, allocator, { 4u, 4u, 2.0f, 3.0f } ) ==
                 tile_map_document_status_t::OK );
    }
    ~binding_fixture_t() { CypherTileMapDocument_Shutdown( &document ); }
    tile_map_document_status_t bind( u16 slot, const char *path )
    {
        return CypherTileMapDocument_SetMaterialBinding( &document, slot, StringView_FromCString( path ) );
    }
    std::string path( u16 slot ) const
    {
        const auto *binding = CypherTileMapDocument_FindMaterialBinding( &document, slot );
        return binding != nullptr ? binding->path : "";
    }
};

std::string MaterialMapText( u32 version, const std::string &materials = "" )
{
    return "@cykv 1\n@schema \"cypher.map\" " + std::to_string( version ) + "\n{\n"
        "map_id = \"00112233-4455-4677-8899-aabbccddeeff\"\n"
        "dimensions = { width = 4u height = 4u }\n"
        "metrics = { cell_size = 2.0 level_height = 3.0 }\n"
        "cells = []\nmarkers = []\n" + materials + "\n}\n";
}

tile_map_serialization_result_t Load( const std::string &source, tile_map_document_t &document,
                                      const allocator_t *allocator = Allocator_GetSystem() )
{
    return CypherTileMapSerialization_LoadFromText( { source.data(), source.size() }, allocator, &document );
}

struct allocation_state_t { usize remaining{ CY_USIZE_MAX }; usize outstanding{ 0u }; };
void *BindingAllocate( void *user, usize bytes, usize alignment ) noexcept
{
    auto &state = *static_cast<allocation_state_t *>( user );
    if ( state.remaining == 0u ) return nullptr;
    if ( state.remaining != CY_USIZE_MAX ) --state.remaining;
    auto *memory = Allocator_Allocate( Allocator_GetSystem(), bytes, alignment );
    if ( memory != nullptr ) ++state.outstanding;
    return memory;
}
void BindingFree( void *user, void *memory, usize bytes, usize alignment ) noexcept
{
    if ( memory != nullptr ) --static_cast<allocation_state_t *>( user )->outstanding;
    Allocator_Free( Allocator_GetSystem(), memory, bytes, alignment );
}
} // namespace

TEST_CASE( "Material binding owns its path and participates in mixed undo history",
           "[CypherTools][TileMap][MaterialBindings]" )
{
    binding_fixture_t fixture;
    auto &document = fixture.document;
    CypherTileMapDocument_MarkSaved( &document );
    char source[] = "materials/stone.cymat";
    REQUIRE( fixture.bind( 65535u, source ) == tile_map_document_status_t::OK );
    source[10] = 'x';
    REQUIRE( fixture.path( 65535u ) == "materials/stone.cymat" );
    REQUIRE( CypherTileMapDocument_IsDirty( &document ) );
    REQUIRE( CypherTileMapDocument_PaintCell( &document, { 1, 1 }, { 0, 1u, 65535u } ) ==
             tile_map_document_status_t::OK );
    REQUIRE( fixture.bind( 65535u, "materials/metal.cymat" ) == tile_map_document_status_t::OK );
    REQUIRE( fixture.bind( 65535u, "" ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_FindMaterialBinding( &document, 65535u ) == nullptr );
    REQUIRE( CypherTileMapDocument_CellAt( &document, { 1, 1 } )->nMaterialSlot == 65535u );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    REQUIRE( fixture.path( 65535u ) == "materials/metal.cymat" );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    REQUIRE( fixture.path( 65535u ) == "materials/stone.cymat" );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    REQUIRE_FALSE( CypherTileMapDocument_CellHasFloor( &document, { 1, 1 } ) );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    REQUIRE_FALSE( CypherTileMapDocument_IsDirty( &document ) );
    REQUIRE( fixture.path( 65535u ).empty() );
    for ( int i = 0; i < 4; ++i ) REQUIRE( CypherTileMapDocument_Redo( &document ) == tile_map_document_status_t::OK );
    REQUIRE( fixture.path( 65535u ).empty() );
    REQUIRE( CypherTileMapDocument_CellHasFloor( &document, { 1, 1 } ) );
}

TEST_CASE( "Material no-ops and rejected paths preserve revision and redo",
           "[CypherTools][TileMap][MaterialBindings]" )
{
    binding_fixture_t fixture;
    auto &document = fixture.document;
    REQUIRE( fixture.bind( 1u, "materials/stone.cymat" ) == tile_map_document_status_t::OK );
    REQUIRE( fixture.bind( 2u, "materials/metal.cymat" ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    const auto revision = document.nCurrentRevision;
    const auto history = CypherTileMapDocument_HistoryCount( &document );
    REQUIRE( fixture.bind( 1u, "materials/stone.cymat" ) == tile_map_document_status_t::OK );
    REQUIRE( fixture.bind( 8u, "" ) == tile_map_document_status_t::OK );
    for ( const char *path : { "../stone.cymat", "materials/../stone.cymat", "/stone.cymat",
                              "Materials/stone.cymat", "materials//stone.cymat", "materials/./stone.cymat",
                              "materials\\stone.cymat", "materials/stone.cytex", "materials/stone.cymat_c" } ) {
        INFO( path );
        REQUIRE( fixture.bind( 1u, path ) == tile_map_document_status_t::INVALID_MATERIAL_PATH );
    }
    const std::string tooLong( TILE_MAP_MATERIAL_PATH_CAPACITY, 'a' );
    REQUIRE( fixture.bind( 1u, ( tooLong + ".cymat" ).c_str() ) == tile_map_document_status_t::INVALID_MATERIAL_PATH );
    REQUIRE( document.nCurrentRevision == revision );
    REQUIRE( CypherTileMapDocument_HistoryCount( &document ) == history );
    REQUIRE( CypherTileMapDocument_CanRedo( &document ) );
    REQUIRE( CypherTileMapDocument_BeginEditGroup( &document, StringView_FromCString( "Painting" ) ) ==
             tile_map_document_status_t::OK );
    REQUIRE( fixture.bind( 1u, "materials/new.cymat" ) == tile_map_document_status_t::INVALID_STATE );
    REQUIRE( CypherTileMapDocument_IsEditGroupOpen( &document ) );
    CypherTileMapDocument_CancelEditGroup( &document );
    REQUIRE( fixture.bind( 3u, "materials/new.cymat" ) == tile_map_document_status_t::OK );
    REQUIRE_FALSE( CypherTileMapDocument_CanRedo( &document ) );
}

TEST_CASE( "Material tables enforce binding count while allowing replacement and undo at capacity",
           "[CypherTools][TileMap][MaterialBindings]" )
{
    binding_fixture_t fixture;
    auto &document = fixture.document;
    for ( u16 slot = 0u; slot < TILE_MAP_MAX_MATERIAL_BINDINGS; ++slot )
        REQUIRE( fixture.bind( slot, "materials/stone.cymat" ) == tile_map_document_status_t::OK );
    REQUIRE( fixture.bind( 500u, "materials/stone.cymat" ) == tile_map_document_status_t::MATERIAL_BINDING_LIMIT_REACHED );
    REQUIRE( fixture.bind( 100u, "materials/metal.cymat" ) == tile_map_document_status_t::OK );
    REQUIRE( fixture.bind( 100u, "" ) == tile_map_document_status_t::OK );
    REQUIRE( Vector_Count( &document.materialBindings ) == 255u );
    REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
    REQUIRE( fixture.path( 100u ) == "materials/metal.cymat" );
    REQUIRE( Vector_Count( &document.materialBindings ) == 256u );
    REQUIRE( CypherTileMapDocument_HistoryCount( &document ) == TILE_MAP_HISTORY_MAX_ENTRIES );
}

TEST_CASE( "Material allocation failure preserves bindings and redo without leaks",
           "[CypherTools][TileMap][MaterialBindings]" )
{
    for ( usize failure = 0u; failure < 2u; ++failure ) {
        allocation_state_t state{};
        const allocator_t allocator{ BindingAllocate, nullptr, BindingFree, &state };
        {
            binding_fixture_t fixture( &allocator );
            auto &document = fixture.document;
            REQUIRE( CypherTileMapDocument_PaintCell( &document, { 1, 1 }, {} ) == tile_map_document_status_t::OK );
            REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
            const auto revision = document.nCurrentRevision;
            state.remaining = failure;
            REQUIRE( fixture.bind( 3u, "materials/stone.cymat" ) == tile_map_document_status_t::ALLOCATION_FAILED );
            REQUIRE( document.materialBindings.nCount == 0u );
            REQUIRE( document.nCurrentRevision == revision );
            REQUIRE( CypherTileMapDocument_CanRedo( &document ) );
            state.remaining = CY_USIZE_MAX;
            REQUIRE( fixture.bind( 3u, "materials/stone.cymat" ) == tile_map_document_status_t::OK );
            REQUIRE( fixture.bind( 3u, "" ) == tile_map_document_status_t::OK );
            state.remaining = 0u;
            REQUIRE( CypherTileMapDocument_Undo( &document ) == tile_map_document_status_t::OK );
            REQUIRE( fixture.path( 3u ) == "materials/stone.cymat" );
            REQUIRE( CypherTileMapDocument_Redo( &document ) == tile_map_document_status_t::OK );
        }
        REQUIRE( state.outstanding == 0u );
    }
}

TEST_CASE( "Map material bindings round-trip deterministically with clean history",
           "[CypherTools][TileMap][MaterialBindings][Serialization]" )
{
    binding_fixture_t fixture;
    REQUIRE( fixture.bind( 42u, "materials/metal.cymat" ) == tile_map_document_status_t::OK );
    REQUIRE( fixture.bind( 3u, "materials/stone.cymat" ) == tile_map_document_status_t::OK );
    text_buffer_t first{}, second{};
    REQUIRE( TextBuffer_Init( &first, Allocator_GetSystem() ) );
    REQUIRE( TextBuffer_Init( &second, Allocator_GetSystem() ) );
    REQUIRE( CypherTileMapSerialization_SaveToText( &fixture.document, &first ).status == tile_map_serialization_status_t::OK );
    const auto view = TextBuffer_View( &first );
    const std::string encoded( view.pData, view.cchLength );
    REQUIRE( encoded.find( "@schema \"cypher.map\" 3" ) != std::string::npos );
    REQUIRE( encoded.find( "materials/stone.cymat" ) < encoded.find( "materials/metal.cymat" ) );
    tile_map_document_t decoded{};
    REQUIRE( Load( encoded, decoded ).status == tile_map_serialization_status_t::OK );
    REQUIRE( decoded.materialBindings.nCount == 2u );
    REQUIRE( std::string( CypherTileMapDocument_FindMaterialBinding( &decoded, 42u )->path ) == "materials/metal.cymat" );
    REQUIRE_FALSE( CypherTileMapDocument_IsDirty( &decoded ) );
    REQUIRE_FALSE( CypherTileMapDocument_CanUndo( &decoded ) );
    REQUIRE( CypherTileMapSerialization_SaveToText( &decoded, &second ).status == tile_map_serialization_status_t::OK );
    REQUIRE( StringView_Equals( TextBuffer_View( &first ), TextBuffer_View( &second ) ) );
    CypherTileMapDocument_Shutdown( &decoded );
}

TEST_CASE( "Material map reader preserves v1 and v2 compatibility and rejects malformed v3 bindings",
           "[CypherTools][TileMap][MaterialBindings][Serialization]" )
{
    for ( u32 version : { 1u, 2u, 3u } ) {
        tile_map_document_t document{};
        REQUIRE( Load( MaterialMapText( version ), document ).status == tile_map_serialization_status_t::OK );
        REQUIRE( document.materialBindings.nCount == 0u );
        CypherTileMapDocument_Shutdown( &document );
    }
    using status_t = tile_map_serialization_status_t;
    const struct { const char *field; status_t status; } invalid[] = {
        { "materials = [{ slot = 1u path = \"../bad.cymat\" }]", status_t::INVALID_MATERIAL_PATH },
        { "materials = [{ slot = 1u path = \"materials/bad.cytex\" }]", status_t::INVALID_MATERIAL_PATH },
        { "materials = [{ slot = 1u path = \"\" }]", status_t::INVALID_MATERIAL_PATH },
        { "materials = [{ slot = 1u path = \"Materials/bad.cymat\" }]", status_t::INVALID_MATERIAL_PATH },
        { "materials = [{ slot = 1u path = \"materials/a.cymat\" }, { slot = 1u path = \"materials/b.cymat\" }]", status_t::DUPLICATE_MATERIAL_SLOT },
        { "materials = [{ slot = 65536u path = \"materials/a.cymat\" }]", status_t::VALUE_OUT_OF_RANGE },
        { "materials = [{ slot = 1u }]", status_t::MISSING_FIELD },
        { "materials = [{ slot = 1u path = \"materials/a.cymat\" extra = true }]", status_t::UNKNOWN_FIELD },
        { "materials = {}", status_t::TYPE_MISMATCH }
    };
    for ( const auto &test : invalid ) {
        INFO( test.field );
        tile_map_document_t document{};
        const auto result = Load( MaterialMapText( 3u, test.field ), document );
        REQUIRE( result.parseStatus == key_value_parse_status_t::OK );
        REQUIRE( result.status == test.status );
        REQUIRE( document.pAllocator == nullptr );
        REQUIRE( document.materialBindings.pData == nullptr );
    }
    for ( u32 version : { 1u, 2u } ) {
        tile_map_document_t document{};
        REQUIRE( Load( MaterialMapText( version, "materials = []" ), document ).status ==
                 tile_map_serialization_status_t::UNKNOWN_FIELD );
    }
    std::string overLimit = "materials = [";
    for ( usize i = 0u; i <= TILE_MAP_MAX_MATERIAL_BINDINGS; ++i ) {
        if ( i != 0u ) overLimit += ", ";
        overLimit += "{ slot = " + std::to_string( i ) + "u path = \"materials/a.cymat\" }";
    }
    overLimit += "]";
    tile_map_document_t document{};
    const auto limitResult = Load( MaterialMapText( 3u, overLimit ), document );
    REQUIRE( limitResult.parseStatus == key_value_parse_status_t::OK );
    REQUIRE( limitResult.status == tile_map_serialization_status_t::LIMIT_EXCEEDED );
    REQUIRE( std::string( limitResult.field ) == "materials" );
}

TEST_CASE( "Material path validation bounds byte views and rejects corrupted persisted tables",
           "[CypherTools][TileMap][MaterialBindings][Serialization]" )
{
    binding_fixture_t fixture;
    const std::string longest = std::string( TILE_MAP_MATERIAL_PATH_CAPACITY - 1u - 6u, 'a' ) + ".cymat";
    REQUIRE( longest.size() == TILE_MAP_MATERIAL_PATH_CAPACITY - 1u );
    REQUIRE( fixture.bind( 1u, longest.c_str() ) == tile_map_document_status_t::OK );
    const char embeddedNul[]{ 'a', '\0', '.', 'c', 'y', 'm', 'a', 't' };
    REQUIRE( CypherTileMapDocument_SetMaterialBinding( &fixture.document, 1u,
                 { embeddedNul, sizeof( embeddedNul ) } ) == tile_map_document_status_t::INVALID_MATERIAL_PATH );
    REQUIRE( CypherTileMapDocument_SetMaterialBinding( &fixture.document, 1u,
                 { nullptr, 1u } ) == tile_map_document_status_t::INVALID_MATERIAL_PATH );
    text_buffer_t output{};
    REQUIRE( TextBuffer_Init( &output, Allocator_GetSystem() ) );
    REQUIRE( TextBuffer_Assign( &output, StringView_FromCString( "unchanged" ) ) );
    auto &binding = fixture.document.materialBindings.pData[0];
    std::memset( binding.path, 'a', sizeof( binding.path ) );
    REQUIRE( CypherTileMapSerialization_SaveToText( &fixture.document, &output ).status ==
             tile_map_serialization_status_t::INVALID_MATERIAL_PATH );
    REQUIRE( StringView_Equals( TextBuffer_View( &output ), StringView_FromCString( "unchanged" ) ) );
    tile_map_validation_report_t report{};
    REQUIRE( CypherTileMapValidationReport_Init( &report, Allocator_GetSystem() ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_Validate( &fixture.document, &report ) == tile_map_document_status_t::OK );
    bool found = false;
    for ( usize i = 0u; i < report.diagnostics.nCount; ++i )
        found |= report.diagnostics.pData[i].code == tile_map_validation_code_t::INVALID_MATERIAL_BINDING;
    REQUIRE( found );
    CypherTileMapValidationReport_Shutdown( &report );
}

TEST_CASE( "Material bindings retain meaning across copied and rotated tile regions",
           "[CypherTools][TileMap][MaterialBindings]" )
{
    binding_fixture_t fixture;
    auto &document = fixture.document;
    REQUIRE( fixture.bind( 8u, "materials/stone.cymat" ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_PaintCell( &document, { 0, 0 }, { 0, 1u, 8u } ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_CopyRegion( &document, { 0, 0, 1u, 1u }, 2, 0 ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_RotateRegionClockwise( &document, { 2, 0, 1u, 1u } ) == tile_map_document_status_t::OK );
    REQUIRE( CypherTileMapDocument_CellAt( &document, { 2, 0 } )->nMaterialSlot == 8u );
    REQUIRE( fixture.path( 8u ) == "materials/stone.cymat" );
    REQUIRE( document.materialBindings.nCount == 1u );
}

TEST_CASE( "Material map decode allocation failures leave fresh output and release temporary storage",
           "[CypherTools][TileMap][MaterialBindings][Serialization]" )
{
    const auto source = MaterialMapText( 3u,
        "materials = [{ slot = 0u path = \"materials/a.cymat\" }, { slot = 1u path = \"materials/b.cymat\" }]" );
    // Establish that the fixture is valid before testing every allocation site.
    // Otherwise a syntax failure could be mistaken for allocator handling.
    {
        tile_map_document_t baseline{};
        REQUIRE( Load( source, baseline ).status == tile_map_serialization_status_t::OK );
        REQUIRE( baseline.materialBindings.nCount == 2u );
        CypherTileMapDocument_Shutdown( &baseline );
    }
    bool succeeded = false;
    usize failed = 0u;
    for ( usize budget = 0u; budget < 128u; ++budget ) {
        CAPTURE( budget );
        allocation_state_t state{ budget, 0u };
        const allocator_t allocator{ BindingAllocate, nullptr, BindingFree, &state };
        {
            tile_map_document_t document{};
            const auto result = Load( source, document, &allocator );
            CAPTURE( result.parseStatus );
            if ( result.status == tile_map_serialization_status_t::OK ) {
                succeeded = true;
                REQUIRE( document.materialBindings.nCount == 2u );
                CypherTileMapDocument_Shutdown( &document );
            } else {
                ++failed;
                REQUIRE( result.status == tile_map_serialization_status_t::OUT_OF_MEMORY );
                REQUIRE( document.pAllocator == nullptr );
                REQUIRE( document.materialBindings.pData == nullptr );
                REQUIRE( document.pHistoryState == nullptr );
            }
        }
        REQUIRE( state.outstanding == 0u );
        if ( succeeded ) break;
    }
    REQUIRE( succeeded );
    REQUIRE( failed >= 5u );
}
