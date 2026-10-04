//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code - Copyright (c) 2026 Karlo Siric.
// Real Quad Slice controls, identity, publication and persistence.
//////////////////////////////////////////////////////////////////////////
#include "CypherMapGui_Workspace.h"
#include "CypherMapGui_ToolPanels.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <memory>

using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::map;
namespace
{
struct session_t {
    gui::editor_gui_t gui{};
    map_workspace_t ws{};
    settings_document_t settings{};
    session_t()
    {
        REQUIRE( gui::EditorGui_Init( &gui, qobject_cast<QApplication *>( QCoreApplication::instance() ), Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
        REQUIRE( MapWorkspace_Init( &ws, &gui ) );
        REQUIRE( MapWorkspace_RegisterCommands( &ws, &gui.commands ) == command_registry_status_t::OK );
        REQUIRE( SettingsDocument_Init( &settings, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
        EditorSettings_SetScope( &gui.settings, settings_scope_t::USER, &settings );
    }
    ~session_t() { EditorSettings_SetScope( &gui.settings, settings_scope_t::USER, nullptr ); MapWorkspace_Shutdown( &ws ); gui::EditorGui_Shutdown( &gui ); }
};
QSpinBox *Cells( QWidget *panel, const char *path )
{
    const auto *control = panel->findChild<QWidget *>( QString::fromLatin1( path ) );
    return control != nullptr ? control->findChild<QSpinBox *>() : nullptr;
}
u64 SelectedTop( map_workspace_t &ws )
{
    map_bounds_t box{}; MapBounds_AddPoint( box, { 0, 0, 0 } ); MapBounds_AddPoint( box, { 128, 96, 64 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
    const u64 id = ws.selection.ids.pData[0]; REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) );
    u64 face = 0;
    for ( usize i = 0; i < ws.wire.faces.nCount; ++i ) { const auto &candidate = ws.wire.faces.pData[i]; if ( candidate.id == id && candidate.normal.z > .99 ) { face = candidate.faceId; } }
    REQUIRE( face != 0 ); MapWorkspace_SelectMeshFace( &ws, id, face ); return face;
}
bool SliceEnabled( session_t &session )
{
    return ( EditorCommands_State( &session.gui.commands, StringView_FromCString( "map.mesh.quad_slice" ) ) & COMMAND_STATE_ENABLED ) != 0;
}
}

TEST_CASE( "Quad Slice sidebar makes an editable grid with retained face selection undo and saved topology", "[map][gui][quad-slice][workflow][persistence]" )
{
    session_t session; auto &ws = session.ws; const u64 face = SelectedTop( ws ), id = ws.selectedMeshFaceObject;
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) ); REQUIRE( panel != nullptr );
    QPointer<QPushButton> apply = panel->findChild<QPushButton *>( QStringLiteral( "map.mesh.quad_slice" ) );
    QPointer<QSpinBox> u = Cells( panel.get(), "editor.map.mesh_slice_u" ), v = Cells( panel.get(), "editor.map.mesh_slice_v" );
    REQUIRE( apply ); REQUIRE( u ); REQUIRE( v ); CHECK( u->value() == 2 ); CHECK( v->value() == 2 );
    CHECK( MapToolProperties_Operations( panel.get() ).count( QStringLiteral( "map.mesh.quad_slice" ) ) == 1 );
    CHECK_FALSE( ( EditorCommands_State( &session.gui.commands, StringView_FromCString( "map.mesh.subdivide" ) ) & COMMAND_STATE_ENABLED ) != 0 );
    u->setValue( 1 ); v->setValue( 1 ); CHECK_FALSE( SliceEnabled( session ) ); CHECK_FALSE( apply->isEnabled() );
    u->setValue( 3 ); v->setValue( 2 ); REQUIRE( SliceEnabled( session ) ); REQUIRE( apply->isEnabled() );
    const usize steps = EditorHistory_StepCount( &ws.history ); const u64 revision = ws.pDocument->geometry.revision;
    apply->click();
    CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1 ); CHECK( ws.pDocument->geometry.revision == revision + 1 );
    CHECK( ws.selectedMeshFaceObject == id ); CHECK( ws.selectedMeshFaceId == face ); CHECK( ws.selection.ids.nCount == 1 );
    CHECK( ws.wire.faces.nCount == 11 ); CHECK( ws.wire.points.nCount == 16 );
    REQUIRE( apply ); CHECK( apply.data() == panel->findChild<QPushButton *>( QStringLiteral( "map.mesh.quad_slice" ) ) );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CHECK( ws.wire.faces.nCount == 6 ); CHECK( ws.wire.points.nCount == 8 ); CHECK( ws.selectedMeshFaceId == face );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CHECK( ws.wire.faces.nCount == 11 ); CHECK( ws.wire.points.nCount == 16 );
    QTemporaryDir destination; REQUIRE( destination.isValid() ); const QString path = destination.filePath( QStringLiteral( "sliced.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
    session_t reloaded; REQUIRE( MapWorkspace_Open( &reloaded.ws, path ).status == map_files_status_t::OK );
    CHECK( reloaded.ws.wire.faces.nCount == 11 ); CHECK( reloaded.ws.wire.points.nCount == 16 );
    MapWorkspace_SelectMeshFace( &reloaded.ws, id, face ); CHECK( reloaded.ws.selectedMeshFaceId == face ); CHECK( SliceEnabled( reloaded ) );
}

TEST_CASE( "Quad Slice rejects nonquad stale preview read-only and wrong-mode selections", "[map][gui][quad-slice][availability]" )
{
    session_t session; auto &ws = session.ws; const u64 face = SelectedTop( ws ), id = ws.selectedMeshFaceObject;
    CHECK( SliceEnabled( session ) );
    ws.pDocument->bReadOnly = CY_TRUE; CHECK_FALSE( SliceEnabled( session ) ); CHECK_FALSE( MapWorkspace_QuadSliceMeshFace( &ws, 2, 2 ) ); ws.pDocument->bReadOnly = CY_FALSE;
    ws.editPreview.bActive = CY_TRUE; CHECK_FALSE( SliceEnabled( session ) ); ws.editPreview.bActive = CY_FALSE;
    ws.selectedMeshFaceId = CY_U64_MAX; CHECK_FALSE( SliceEnabled( session ) ); ws.selectedMeshFaceId = face;
    CHECK_FALSE( MapWorkspace_QuadSliceMeshFace( &ws, 0, 2 ) ); CHECK_FALSE( MapWorkspace_QuadSliceMeshFace( &ws, 65, 2 ) ); CHECK_FALSE( MapWorkspace_QuadSliceMeshFace( &ws, 1, 1 ) );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS ); CHECK_FALSE( SliceEnabled( session ) );
    REQUIRE( MapWorkspace_TriangulateMeshSelection( &ws ) );
    u64 triangle = 0; for ( usize i = 0; i < ws.wire.faces.nCount; ++i ) { if ( ws.wire.faces.pData[i].id == id ) { triangle = ws.wire.faces.pData[i].faceId; break; } }
    REQUIRE( triangle != 0 ); MapWorkspace_SelectMeshFace( &ws, id, triangle ); CHECK_FALSE( SliceEnabled( session ) );
    const auto *document = ws.pDocument; const usize steps = EditorHistory_StepCount( &ws.history );
    CHECK_FALSE( MapWorkspace_QuadSliceMeshFace( &ws, 2, 2 ) ); CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
}
