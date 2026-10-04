//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Outliner_Tests.cpp
//  Purpose: Tests typed mapnodes, authored group projection, filtering,
//           and selection synchronization without a native display.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Panels.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_DocumentSurfaces.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QApplication>
#include <QImage>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <filesystem>
#include <memory>
#include <string>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::map;

namespace
{

struct session_t {
    explicit session_t( bool example = true )
    {
        auto *pApp = qobject_cast<QApplication *>( QCoreApplication::instance() );
        REQUIRE( gui::EditorGui_Init( &gui, pApp, Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
        REQUIRE( MapWorkspace_Init( &workspace, &gui ) );
        if ( example ) {
            const QString path = QString::fromStdString( ( std::filesystem::path( CYPHER_MAP_EXAMPLE_DIR ) / "facility.cymap" ).string() );
            REQUIRE( MapWorkspace_Open( &workspace, path ).status == map_files_status_t::OK );
        }
    }
    gui::editor_gui_t gui{};
    map_workspace_t workspace{};
};

struct user_settings_t {
    explicit user_settings_t( settings_registry_t *pRegistry ) : pRegistry( pRegistry )
    {
        REQUIRE( SettingsDocument_Init( &store, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
        EditorSettings_SetScope( pRegistry, settings_scope_t::USER, &store );
    }
    ~user_settings_t() { EditorSettings_SetScope( pRegistry, settings_scope_t::USER, nullptr ); }
    void SetBool( const char *pPath, bool enabled )
    {
        const setting_descriptor_t *pDescriptor = EditorSettings_Find( pRegistry, StringView_FromCString( pPath ) );
        REQUIRE( pDescriptor != nullptr );
        setting_value_t value{};
        value.type = setting_type_t::BOOL;
        value.bValue = enabled;
        REQUIRE( EditorSettings_Write( pRegistry, settings_scope_t::USER, *pDescriptor, value ) == settings_registry_status_t::OK );
    }
    settings_registry_t *pRegistry;
    settings_document_t store{};
};

QTreeWidget *Tree( QWidget &panel )
{
    auto *pTree = panel.findChild<QTreeWidget *>( QStringLiteral( "MapOutlinerTree" ) );
    REQUIRE( pTree != nullptr );
    return pTree;
}

QTreeWidgetItem *Row( QWidget &panel, u64 id )
{
    for ( QTreeWidgetItemIterator it( Tree( panel ) ); *it != nullptr; ++it ) {
        if ( ( *it )->data( 0, Qt::UserRole ).toULongLong() == id ) { return *it; }
    }
    FAIL( "Mapnode ID absent: " << id );
    return nullptr;
}

bool Visible( const QTreeWidgetItem *pItem )
{
    for ( auto *p = pItem; p != nullptr; p = p->parent() ) { if ( p->isHidden() ) { return false; } }
    return true;
}

QAction *Action( QWidget &panel, const char *pName )
{
    auto *pAction = panel.findChild<QAction *>( QString::fromUtf8( pName ) );
    REQUIRE( pAction != nullptr );
    return pAction;
}

void Show( QWidget &panel )
{
    panel.resize( 250, 650 );
    panel.show();
    QCoreApplication::processEvents();
}

} // namespace

TEST_CASE( "Outliner decorates actual mapnode kinds and authored group membership", "[map][gui][outliner]" )
{
    session_t session;
    std::unique_ptr<QWidget> panel( MapOutliner_Create( nullptr, &session.workspace ) );
    Show( *panel );
    auto *pRoot = Tree( *panel )->topLevelItem( 0 );
    REQUIRE( pRoot != nullptr );
    CHECK( pRoot->text( 0 ) == QStringLiteral( "facility.cymap" ) );
    CHECK_FALSE( pRoot->icon( 0 ).isNull() );
    CHECK_FALSE( pRoot->flags().testFlag( Qt::ItemIsSelectable ) );
    for ( const u64 id : { 1000u, 1200u, 1300u, 140u, 190u } ) {
        CHECK_FALSE( Row( *panel, id )->icon( 0 ).isNull() );
        CHECK_FALSE( Row( *panel, id )->icon( 0 ).pixmap( 16, 16 ).isNull() );
    }
    CHECK( Row( *panel, 1000u )->text( 0 ).contains( QStringLiteral( "floor" ) ) );
    CHECK( Row( *panel, 1200u )->text( 0 ).contains( QStringLiteral( "loading ramp" ) ) );
    CHECK( Row( *panel, 1300u )->text( 0 ).contains( QStringLiteral( "doorway arch" ) ) );
    auto *pGroup = Row( *panel, 190u );
    CHECK( pGroup->text( 0 ).contains( QStringLiteral( "Wave 1" ) ) );
    CHECK_FALSE( pGroup->flags().testFlag( Qt::ItemIsSelectable ) );
    CHECK( Row( *panel, 110u )->parent() == pGroup );
    CHECK( Row( *panel, 120u )->parent() == pGroup );
    CHECK( Row( *panel, 111u )->parent() == Row( *panel, 110u ) );
    CHECK( Row( *panel, 1000u )->icon( 0 ).pixmap( 16, 16 ).toImage() != Row( *panel, 1200u )->icon( 0 ).pixmap( 16, 16 ).toImage() );
}

TEST_CASE( "Outliner filters reveal ancestor chains and restore collapsed branches", "[map][gui][outliner]" )
{
    session_t session;
    std::unique_ptr<QWidget> panel( MapOutliner_Create( nullptr, &session.workspace ) );
    auto *pGroup = Row( *panel, 190u );
    auto *pEntity = Row( *panel, 110u );
    pGroup->setExpanded( false );
    pEntity->setExpanded( false );
    MapOutliner_SetFilter( panel.get(), QStringLiteral( "111" ) );
    CHECK( Visible( Row( *panel, 111u ) ) );
    CHECK( Visible( pEntity ) );
    CHECK( Visible( pGroup ) );
    CHECK( pEntity->isExpanded() );
    CHECK( pGroup->isExpanded() );
    CHECK_FALSE( Visible( Row( *panel, 1000u ) ) );
    MapOutliner_SetFilter( panel.get(), QStringLiteral( "wave 1" ) );
    CHECK( Visible( Row( *panel, 130u ) ) );
    CHECK( Visible( Row( *panel, 111u ) ) );
    MapOutliner_SetFilter( panel.get(), QString() );
    CHECK_FALSE( pGroup->isExpanded() );
    CHECK_FALSE( pEntity->isExpanded() );
    MapOutliner_SetFilter( panel.get(), QStringLiteral( "LOADING RAMP" ) );
    CHECK( Visible( Row( *panel, 1200u ) ) );
    CHECK_FALSE( Visible( Row( *panel, 1000u ) ) );
    CHECK( EditorSelection_Count( &session.workspace.selection ) == 0u );
    CHECK_FALSE( MapWorkspace_IsModified( &session.workspace ) );
}

TEST_CASE( "Outliner type and selected-only filters compose without losing parent context", "[map][gui][outliner]" )
{
    session_t session;
    std::unique_ptr<QWidget> panel( MapOutliner_Create( nullptr, &session.workspace ) );
    Action( *panel, "MapOutlinerKind0" )->setChecked( false );
    CHECK_FALSE( Visible( Row( *panel, 1000u ) ) );
    CHECK_FALSE( Visible( Row( *panel, 111u ) ) );
    CHECK( Visible( Row( *panel, 1200u ) ) );
    Action( *panel, "MapOutlinerSelectedOnly" )->setChecked( true );
    CHECK_FALSE( Visible( Row( *panel, 1200u ) ) );
    MapWorkspace_Select( &session.workspace, 1200u, MAP_SELECT_REPLACE );
    CHECK( Visible( Row( *panel, 1200u ) ) );
    CHECK_FALSE( Visible( Row( *panel, 140u ) ) );
    MapWorkspace_Select( &session.workspace, 111u, MAP_SELECT_REPLACE );
    CHECK_FALSE( Visible( Row( *panel, 111u ) ) );
    Action( *panel, "MapOutlinerKind0" )->setChecked( true );
    CHECK( Visible( Row( *panel, 111u ) ) );
    CHECK( Visible( Row( *panel, 110u ) ) );
    CHECK( Visible( Row( *panel, 190u ) ) );
    CHECK_FALSE( Visible( Row( *panel, 120u ) ) );
    Action( *panel, "MapOutlinerResetFilters" )->trigger();
    CHECK( Visible( Row( *panel, 120u ) ) );
    CHECK_FALSE( Action( *panel, "MapOutlinerSelectedOnly" )->isChecked() );
}

TEST_CASE( "Outliner visibility filter observes session hiding without changing it", "[map][gui][outliner]" )
{
    session_t session;
    REQUIRE( MapWorkspace_RegisterCommands( &session.workspace, &session.gui.commands ) == command_registry_status_t::OK );
    std::unique_ptr<QWidget> panel( MapOutliner_Create( nullptr, &session.workspace ) );
    MapWorkspace_SetVisgroupHidden( &session.workspace, map_visgroup_t::BRUSHES, CY_TRUE );
    CHECK( Visible( Row( *panel, 1000u ) ) );
    CHECK( Row( *panel, 1000u )->font( 0 ).italic() );
    CHECK( Row( *panel, 1000u )->toolTip( 0 ).contains( QStringLiteral( "Hidden in map views" ) ) );
    Action( *panel, "MapOutlinerShowHidden" )->setChecked( false );
    CHECK_FALSE( Visible( Row( *panel, 1000u ) ) );
    CHECK( Visible( Row( *panel, 1200u ) ) );
    MapWorkspace_SetVisgroupHidden( &session.workspace, map_visgroup_t::BRUSHES, CY_FALSE );
    CHECK( Visible( Row( *panel, 1000u ) ) );
    CHECK_FALSE( Row( *panel, 1000u )->font( 0 ).italic() );
    MapWorkspace_Select( &session.workspace, 140u, MAP_SELECT_REPLACE );
    REQUIRE( EditorCommands_ExecuteLine( &session.gui.commands, StringView_FromCString( "map.hide.selected" ) ) == command_result_t::OK );
    CHECK_FALSE( Visible( Row( *panel, 140u ) ) );
    Action( *panel, "MapOutlinerShowHidden" )->setChecked( true );
    CHECK( Visible( Row( *panel, 140u ) ) );
    CHECK( EditorSelection_Contains( &session.workspace.hidden, 140u ) );

    // The fixture's logic relay has no origin and therefore no pickable
    // wire object; its class filters and explicit hiding must still work.
    REQUIRE( MapWireframe_FindObject( session.workspace.wire, 170u ) == nullptr );
    Action( *panel, "MapOutlinerShowHidden" )->setChecked( false );
    MapWorkspace_SetVisgroupHidden( &session.workspace, map_visgroup_t::OTHER_ENTITIES, CY_TRUE );
    CHECK_FALSE( Visible( Row( *panel, 170u ) ) );
    MapWorkspace_SetVisgroupHidden( &session.workspace, map_visgroup_t::OTHER_ENTITIES, CY_FALSE );
    CHECK( Visible( Row( *panel, 170u ) ) );
    MapWorkspace_Select( &session.workspace, 170u, MAP_SELECT_REPLACE );
    REQUIRE( EditorCommands_ExecuteLine( &session.gui.commands, StringView_FromCString( "map.hide.selected" ) ) == command_result_t::OK );
    CHECK_FALSE( Visible( Row( *panel, 170u ) ) );
    Action( *panel, "MapOutlinerShowHidden" )->setChecked( true );
    CHECK( Visible( Row( *panel, 170u ) ) );
    CHECK( Row( *panel, 170u )->font( 0 ).italic() );
}

TEST_CASE( "Outliner keeps selections outside a filter when visible rows are selected", "[map][gui][outliner]" )
{
    session_t session;
    std::unique_ptr<QWidget> panel( MapOutliner_Create( nullptr, &session.workspace ) );
    const u64 ids[]{ 1000u, 1200u };
    MapWorkspace_SetSelection( &session.workspace, ids, 2u );
    MapOutliner_SetFilter( panel.get(), QStringLiteral( "Brush" ) );
    CHECK_FALSE( Visible( Row( *panel, 1200u ) ) );
    CHECK( MapWorkspace_IsSelected( &session.workspace, 1200u ) );
    Tree( *panel )->setCurrentItem( Row( *panel, 1001u ), 0, QItemSelectionModel::ClearAndSelect );
    CHECK( MapWorkspace_IsSelected( &session.workspace, 1001u ) );
    CHECK( MapWorkspace_IsSelected( &session.workspace, 1200u ) );
    CHECK_FALSE( MapWorkspace_IsSelected( &session.workspace, 1000u ) );
    MapOutliner_SetFilter( panel.get(), QString() );
    CHECK( Row( *panel, 1200u )->isSelected() );
    CHECK( Row( *panel, 1001u )->isSelected() );
    CHECK( EditorSelection_Count( &session.workspace.selection ) == 2u );
    MapWorkspace_SetSelection( &session.workspace, nullptr, 0u );
    CHECK( Tree( *panel )->selectedItems().empty() );
    CHECK_FALSE( MapWorkspace_IsModified( &session.workspace ) );
}

TEST_CASE( "Outliner filters survive document rebuilds and empty new maps", "[map][gui][outliner]" )
{
    session_t session;
    std::unique_ptr<QWidget> panel( MapOutliner_Create( nullptr, &session.workspace ) );
    Show( *panel );
    auto *pFilters = panel->findChild<QToolButton *>( QStringLiteral( "MapOutlinerFilters" ) );
    REQUIRE( pFilters != nullptr );
    CHECK( pFilters->popupMode() == QToolButton::InstantPopup );
    REQUIRE( pFilters->menu() != nullptr );
    MapOutliner_SetFilter( panel.get(), QStringLiteral( "arch" ) );
    Action( *panel, "MapOutlinerKind0" )->setChecked( false );
    MapWorkspace_DocumentChanged( &session.workspace );
    CHECK( Visible( Row( *panel, 1300u ) ) );
    CHECK_FALSE( Action( *panel, "MapOutlinerKind0" )->isChecked() );
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    CHECK( Tree( *panel )->topLevelItem( 0 )->text( 0 ) == QStringLiteral( "Untitled" ) );
    CHECK( panel->findChild<QLineEdit *>( QStringLiteral( "MapOutlinerFilter" ) )->text() == QStringLiteral( "arch" ) );
    CHECK( panel->findChild<QLabel *>( QStringLiteral( "MapOutlinerStatus" ) )->text().startsWith( QStringLiteral( "0 / 0" ) ) );
    Action( *panel, "MapOutlinerResetFilters" )->trigger();
    CHECK( MapOutliner_VisibleRowCount( panel.get() ) == 1 ); // Empty default layer is discoverable.
}

TEST_CASE( "Outliner group projection handles cycles duplicate membership and missing members", "[map][gui][outliner]" )
{
    session_t session( false );
    constexpr const char *root = R"(@cykv 1
@schema "cypher.map" 10
{ map_id = "4c1f0a52-7a8e-4f3b-9d61-0b2d3e4f5a6b" name = "Groups" game = "reap"
  next_id = 1000u layers = [ { id = "default" } ] })";
    constexpr const char *chunk = R"(@cykv 1
@schema "cypher.map_chunk" 10
{ map_id = "4c1f0a52-7a8e-4f3b-9d61-0b2d3e4f5a6b" layer = "default" cell = "global"
  entities = [ { id = 1u class = "info_target" name = "member" origin = [ 0, 0, 0 ] }, { id = 2u class = "logic_relay" } ]
  groups = [ { id = 30u name = "Second" members = [ 20u, 1u, 999u ] },
             { id = 20u name = "First" members = [ 30u, 1u, 20u ] } ] })";
    const map_chunk_input_t input{ StringView_FromCString( "default/global.cymapchunk" ), StringView_FromCString( chunk ) };
    MapDocument_Shutdown( session.workspace.pDocument );
    REQUIRE( MapDocument_Load( session.workspace.pDocument, Allocator_GetSystem(), StringView_FromCString( root ), { &input, 1u } ) == map_status_t::OK );
    MapWorkspace_DocumentChanged( &session.workspace );
    std::unique_ptr<QWidget> panel( MapOutliner_Create( nullptr, &session.workspace ) );
    CHECK( Row( *panel, 30u )->parent() == Row( *panel, 20u ) );
    CHECK( Row( *panel, 1u )->parent() == Row( *panel, 20u ) );
    CHECK( Row( *panel, 20u )->parent() == Tree( *panel )->topLevelItem( 0 )->child( 0 ) );
    CHECK( MapOutliner_VisibleRowCount( panel.get() ) == 3 ); // Layer and two actual objects, no duplicated member.
    MapOutliner_SetFilter( panel.get(), QStringLiteral( "member" ) );
    CHECK( Visible( Row( *panel, 20u ) ) );
    CHECK_FALSE( Visible( Row( *panel, 30u ) ) );
    CHECK_FALSE( Visible( Row( *panel, 2u ) ) );
}

TEST_CASE( "Outliner icons follow custom themes and release their style listener", "[map][gui][outliner]" )
{
    session_t session;
    const usize listeners = session.gui.nStyleListeners;
    std::unique_ptr<QWidget> panel( MapOutliner_Create( nullptr, &session.workspace ) );
    CHECK( session.gui.nStyleListeners == listeners + 1u );
    const QImage before = Row( *panel, 1000u )->icon( 0 ).pixmap( 16, 16 ).toImage();
    auto *pApp = qobject_cast<QApplication *>( QCoreApplication::instance() );
    REQUIRE( gui::EditorGui_AddTheme( &session.gui, QStringLiteral(
        "@cykv 1\n@schema \"cypher.theme\" 2\n{ id = \"outliner_test\" name = \"Outliner Test\" base = \"charcoal\" "
        "colors = { \"ui.text.muted\" = \"#33ee55\" } metrics = { \"ui.icon.color_strength\" = 0.0 } }" ) ) == gui::editor_gui_status_t::OK );
    REQUIRE( gui::EditorGui_SelectTheme( &session.gui, pApp, StringView_FromCString( "outliner_test" ) ) == gui::editor_gui_status_t::OK );
    CHECK( Row( *panel, 1000u )->icon( 0 ).pixmap( 16, 16 ).toImage() != before );
    panel.reset();
    CHECK( session.gui.nStyleListeners == listeners );
    REQUIRE( gui::EditorGui_SelectTheme( &session.gui, pApp, StringView_FromCString( "charcoal" ) ) == gui::editor_gui_status_t::OK );
}

TEST_CASE( "Outliner labels prioritize authored names and persist optional mapnode IDs", "[map][gui][outliner]" )
{
    session_t session;
    user_settings_t settings( &session.gui.settings );
    std::unique_ptr<QWidget> panel( MapOutliner_Create( nullptr, &session.workspace ) );
    CHECK( Row( *panel, 1000u )->text( 0 ) == QStringLiteral( "floor" ) );
    CHECK( Row( *panel, 1200u )->text( 0 ) == QStringLiteral( "loading ramp" ) );
    CHECK( Row( *panel, 1000u )->toolTip( 0 ).contains( QStringLiteral( "Brush" ) ) );
    CHECK( Row( *panel, 1000u )->toolTip( 0 ).contains( QStringLiteral( "ID 1000" ) ) );
    const QString unnamed = Row( *panel, 111u )->text( 0 );
    CHECK( unnamed == QStringLiteral( "Brush #111" ) );
    auto *ids = Action( *panel, "MapOutlinerShowIds" );
    CHECK_FALSE( ids->isChecked() );
    ids->setChecked( true );
    CHECK( EditorSettings_Bool( &session.gui.settings, "editor.outliner.show_ids", CY_FALSE ) );
    CHECK( Row( *panel, 1000u )->text( 0 ) == QStringLiteral( "floor  #1000" ) );
    CHECK( Row( *panel, 111u )->text( 0 ) == unnamed );
    MapOutliner_SetFilter( panel.get(), QStringLiteral( "#1000" ) );
    CHECK( Visible( Row( *panel, 1000u ) ) );
    settings.SetBool( "editor.outliner.show_ids", false );
    CHECK_FALSE( ids->isChecked() );
    CHECK( Row( *panel, 1000u )->text( 0 ) == QStringLiteral( "floor" ) );
    CHECK_FALSE( Visible( Row( *panel, 1000u ) ) );
    MapOutliner_SetFilter( panel.get(), QStringLiteral( "1000" ) );
    CHECK( Visible( Row( *panel, 1000u ) ) ); // Stable IDs stay searchable when not drawn.
    settings.SetBool( "editor.outliner.show_ids", true );
    panel.reset( MapOutliner_Create( nullptr, &session.workspace ) );
    CHECK( Action( *panel, "MapOutlinerShowIds" )->isChecked() );
    CHECK( Row( *panel, 1000u )->text( 0 ) == QStringLiteral( "floor  #1000" ) );
    MapWorkspace_DocumentChanged( &session.workspace );
    CHECK( Row( *panel, 1000u )->text( 0 ) == QStringLiteral( "floor  #1000" ) );
    CHECK( EditorSelection_Count( &session.workspace.selection ) == 0u );
    CHECK_FALSE( MapWorkspace_IsModified( &session.workspace ) );
}

TEST_CASE( "Outliner branch lines toggle live without losing hierarchy or expansion", "[map][gui][outliner]" )
{
    session_t session;
    user_settings_t settings( &session.gui.settings );
    std::unique_ptr<QWidget> panel( MapOutliner_Create( nullptr, &session.workspace ) );
    Show( *panel );
    auto *tree = Tree( *panel );
    auto *floor = Row( *panel, 1000u );
    tree->scrollToItem( floor );
    QCoreApplication::processEvents();
    const QRect item = tree->visualItemRect( floor );
    REQUIRE_FALSE( item.isEmpty() );
    REQUIRE( item.left() > 0 );
    const QRect branch( 0, item.top(), item.left(), item.height() );
    const QImage withLines = tree->viewport()->grab( branch ).toImage();
    CHECK( EditorSettings_Bool( &session.gui.settings, "editor.ui.tree_lines", CY_FALSE ) );
    settings.SetBool( "editor.ui.tree_lines", false );
    QCoreApplication::processEvents();
    const QImage withoutLines = tree->viewport()->grab( branch ).toImage();
    CHECK( withLines != withoutLines );
    CHECK( tree->rootIsDecorated() );
    CHECK( tree->itemsExpandable() );
    auto *group = Row( *panel, 190u );
    CHECK( group->isExpanded() );
    tree->collapseItem( group );
    CHECK_FALSE( group->isExpanded() );
    tree->expandItem( group );
    CHECK( group->isExpanded() );
    CHECK( Row( *panel, 110u )->parent() == group );
    settings.SetBool( "editor.ui.tree_lines", true );
    tree->scrollToItem( floor );
    QCoreApplication::processEvents();
    CHECK( tree->viewport()->grab( branch ).toImage() == withLines );
    CHECK( EditorSelection_Count( &session.workspace.selection ) == 0u );
    CHECK_FALSE( MapWorkspace_IsModified( &session.workspace ) );
}

namespace
{
QTreeWidget *PropertyTree( QWidget &panel )
{
    auto *pTree = panel.findChild<QTreeWidget *>( QStringLiteral( "MapPropertiesTree" ) );
    REQUIRE( pTree != nullptr );
    return pTree;
}

QTreeWidgetItem *PropertyGroup( QWidget &panel, const char *name )
{
    auto *pTree = PropertyTree( panel );
    for ( int i = 0; i < pTree->topLevelItemCount(); ++i ) {
        if ( pTree->topLevelItem( i )->text( 0 ) == QString::fromUtf8( name ) ) { return pTree->topLevelItem( i ); }
    }
    FAIL( "Missing property section: " << name );
    return nullptr;
}

QTreeWidgetItem *PropertyChild( QTreeWidgetItem *pParent, const char *name )
{
    for ( int i = 0; i < pParent->childCount(); ++i ) {
        if ( pParent->child( i )->text( 0 ) == QString::fromUtf8( name ) ) { return pParent->child( i ); }
    }
    FAIL( "Missing property: " << name );
    return nullptr;
}
}

TEST_CASE( "Properties inspect live geometry and materials lifted out of source records", "[map][gui][properties]" )
{
    session_t session;
    std::unique_ptr<QWidget> panel( MapProperties_Create( nullptr, &session.workspace ) );
    const auto historyCount = EditorHistory_StepCount( &session.workspace.history );
    const auto revision = session.workspace.pDocument->geometry.revision;
    const auto nextId = session.workspace.pDocument->nextId;
    MapWorkspace_Select( &session.workspace, 1000u, MAP_SELECT_REPLACE );
    auto *pIdentity = PropertyGroup( *panel, "Identity" );
    CHECK( PropertyChild( pIdentity, "Type" )->text( 1 ) == QStringLiteral( "Brush" ) );
    CHECK( PropertyChild( pIdentity, "Name" )->text( 1 ) == QStringLiteral( "floor" ) );
    CHECK( PropertyChild( pIdentity, "ID" )->text( 1 ) == QStringLiteral( "1000" ) );
    CHECK( PropertyChild( PropertyGroup( *panel, "Placement" ), "Layer" )->text( 1 ) == QStringLiteral( "structure" ) );
    CHECK( PropertyChild( PropertyGroup( *panel, "Geometry" ), "Planes" )->text( 1 ) == QStringLiteral( "6" ) );
    CHECK( PropertyChild( PropertyGroup( *panel, "Materials" ), "materials/concrete/floor01.cymat" )->text( 1 ) == QStringLiteral( "6 sides" ) );
    const auto *pSource = MapDocument_FindObject( session.workspace.pDocument, 1000u, nullptr );
    CHECK( KeyValue_Find( pSource, StringView_FromCString( "faces" ) ) == nullptr );
    auto *pData = PropertyGroup( *panel, "Geometry Data" );
    CHECK( pData->childCount() == 0 );
    auto *pFilter = panel->findChild<QLineEdit *>( QStringLiteral( "MapPropertiesFilter" ) );
    REQUIRE( pFilter != nullptr );
    CHECK( pFilter->toolTip().contains( QStringLiteral( "unopened data is not searched" ) ) );
    pFilter->setText( QStringLiteral( "Name" ) );
    CHECK_FALSE( PropertyGroup( *panel, "Identity" )->isHidden() );
    CHECK( pData->isHidden() );
    CHECK( pData->childCount() == 0 ); // Metadata filtering must not describe geometry.
    pFilter->clear();
    pData->setExpanded( true );
    auto *pFaces = PropertyChild( pData, "faces" );
    pFaces->setExpanded( true );
    REQUIRE( pFaces->childCount() == 6 );
    pFaces->child( 0 )->setExpanded( true );
    CHECK( PropertyChild( pFaces->child( 0 ), "material" )->text( 1 ) == QStringLiteral( "materials/concrete/floor01.cymat" ) );
    CHECK_FALSE( PropertyChild( pFaces->child( 0 ), "plane" )->text( 1 ).isEmpty() );
    CHECK_FALSE( PropertyChild( pFaces->child( 0 ), "uv_u" )->text( 1 ).isEmpty() );

    // Document notifications replace the backing snapshot without retaining
    // pointers into the old tree; the expanded inspector remains useful.
    MapWorkspace_DocumentChanged( &session.workspace );
    CHECK( PropertyGroup( *panel, "Geometry Data" )->isExpanded() );
    CHECK( PropertyChild( PropertyGroup( *panel, "Geometry Data" ), "faces" )->isExpanded() );
    CHECK( MapProperties_Text( panel.get() ).contains( QStringLiteral( "uv_u =" ) ) );

    MapWorkspace_Select( &session.workspace, 1200u, MAP_SELECT_REPLACE );
    auto *pMesh = PropertyGroup( *panel, "Geometry" );
    CHECK( PropertyChild( pMesh, "Vertices" )->text( 1 ) == QStringLiteral( "4" ) );
    CHECK( PropertyChild( pMesh, "Edges" )->text( 1 ) == QStringLiteral( "4" ) );
    CHECK( PropertyChild( pMesh, "Faces" )->text( 1 ) == QStringLiteral( "1" ) );
    CHECK( PropertyChild( PropertyGroup( *panel, "Materials" ), "materials/metal/grate01.cymat" )->text( 1 ) == QStringLiteral( "1 face" ) );
    pFilter->setText( QStringLiteral( "grate01" ) );
    CHECK_FALSE( PropertyGroup( *panel, "Materials" )->isHidden() );
    CHECK( PropertyGroup( *panel, "Identity" )->isHidden() );
    // Material usage is already loaded. Unopened geometry is deliberately
    // not serialized or recursively searched by this text filter.
    CHECK( PropertyGroup( *panel, "Geometry Data" )->isHidden() );
    CHECK( PropertyGroup( *panel, "Geometry Data" )->childCount() == 0 );
    pFilter->clear();
    MapWorkspace_Select( &session.workspace, 1300u, MAP_SELECT_REPLACE );
    CHECK( PropertyChild( PropertyGroup( *panel, "Geometry" ), "Control grid" )->text( 1 ) == QStringLiteral( "3 x 3" ) );
    CHECK( PropertyChild( PropertyGroup( *panel, "Geometry" ), "Control points" )->text( 1 ) == QStringLiteral( "9" ) );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == historyCount );
    CHECK( session.workspace.pDocument->geometry.revision == revision );
    CHECK( session.workspace.pDocument->nextId == nextId );
    CHECK( PropertyTree( *panel )->editTriggers() == QAbstractItemView::NoEditTriggers );
    const u64 selection[]{ 1000u, 1200u };
    MapWorkspace_SetSelection( &session.workspace, selection, 2u );
    CHECK( PropertyChild( PropertyGroup( *panel, "Selection" ), "Layer" )->text( 1 ) == QStringLiteral( "Mixed (2 layers)" ) );
    CHECK( PropertyChild( PropertyGroup( *panel, "Selection" ), "Object IDs" )->text( 1 ) == QStringLiteral( "1000, 1200" ) );
}

TEST_CASE( "Properties distinguish entity attachment and owned geometry without invented runtime fields", "[map][gui][properties]" )
{
    session_t session;
    std::unique_ptr<QWidget> panel( MapProperties_Create( nullptr, &session.workspace ) );
    MapWorkspace_Select( &session.workspace, 111u, MAP_SELECT_REPLACE );
    CHECK( PropertyChild( PropertyGroup( *panel, "Placement" ), "Owner entity" )->text( 1 ) == QStringLiteral( "110" ) );
    MapWorkspace_Select( &session.workspace, 170u, MAP_SELECT_REPLACE );
    const auto *pSummary = panel->findChild<QLabel *>( QStringLiteral( "MapSelectionSummary" ) );
    REQUIRE( pSummary != nullptr );
    CHECK( pSummary->text() == QStringLiteral( "1 entity" ) );
    auto *pPlacement = PropertyGroup( *panel, "Placement" );
    CHECK( PropertyChild( pPlacement, "Origin" )->text( 1 ) == QStringLiteral( "Not authored" ) );
    CHECK( PropertyChild( pPlacement, "Parent entity" )->text( 1 ) == QStringLiteral( "None" ) );
    CHECK( MapProperties_Text( panel.get() ).contains( QStringLiteral( "class = logic_relay" ) ) );
    CHECK( MapProperties_Text( panel.get() ).contains( QStringLiteral( "start_disabled = false" ) ) );
    CHECK_FALSE( MapProperties_Text( panel.get() ).contains( QStringLiteral( "(bounds) =" ) ) );
    CHECK_FALSE( MapProperties_Text( panel.get() ).contains( QStringLiteral( "Physics" ) ) );
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    CHECK_FALSE( MapProperties_Text( panel.get() ).contains( QStringLiteral( "logic_relay" ) ) );
}

TEST_CASE( "Properties inspect unsaved geometry and page large terrain arrays", "[map][gui][properties]" )
{
    session_t session( false );
    namespace geo = cypher::editor::geometry;
    auto *pMap = session.workspace.pDocument;
    geo::brush_solid_t box{};
    REQUIRE( geo::BrushGenerator_TryMakeBox( &box, pMap->pAllocator, pMap->geometryPolicy,
        &pMap->geometry.sourceIds.allocator, math::vec3d_t{ 0, 0, 0 }, math::vec3d_t{ 16, 16, 16 } ) == geo::geometry_status_t::OK );
    REQUIRE( geo::GeometryDocument_TryAddBrush( &pMap->geometry, &box ) == geo::geometry_status_t::OK );
    const u64 boxId = box.sourceId.value;
    geo::BrushSolid_Shutdown( &box );
    MapWorkspace_DocumentChanged( &session.workspace );
    REQUIRE( MapDocument_FindObject( pMap, boxId, nullptr ) == nullptr );
    std::unique_ptr<QWidget> panel( MapProperties_Create( nullptr, &session.workspace ) );
    MapWorkspace_Select( &session.workspace, boxId, MAP_SELECT_REPLACE );
    CHECK( PropertyChild( PropertyGroup( *panel, "Geometry" ), "Planes" )->text( 1 ) == QStringLiteral( "6" ) );
    CHECK( PropertyChild( PropertyGroup( *panel, "Materials" ), "Unassigned" )->text( 1 ) == QStringLiteral( "6 sides" ) );
    CHECK( PropertyChild( PropertyGroup( *panel, "Placement" ), "(chunk)" )->text( 1 ) == QStringLiteral( "Assigned when saved" ) );

    const auto allocated = geo::GeometrySourceIdAllocator_Allocate( &pMap->geometry.sourceIds.allocator );
    REQUIRE( allocated.status == geo::geometry_status_t::OK );
    geo::heightfield_t terrain{};
    REQUIRE( geo::HeightField_TryInit( &terrain, pMap->pAllocator, math::vec3d_t{ 0, 0, 0 }, 16.0,
        256u, 8u, 8u, allocated.id, &pMap->geometry.sourceIds.allocator ) == geo::geometry_status_t::OK );
    terrain.heights.pData[200] = 123.25;
    REQUIRE( geo::GeometryDocument_TryAddHeightField( &pMap->geometry, &terrain ) == geo::geometry_status_t::OK );
    geo::HeightField_Shutdown( &terrain );
    MapWorkspace_DocumentChanged( &session.workspace );
    MapWorkspace_Select( &session.workspace, allocated.id.value, MAP_SELECT_REPLACE );
    CHECK( PropertyChild( PropertyGroup( *panel, "Geometry" ), "Height samples" )->text( 1 ) == QStringLiteral( "2313" ) );
    auto *pData = PropertyGroup( *panel, "Geometry Data" );
    CHECK( pData->childCount() == 0 );
    pData->setExpanded( true );
    auto *pHeights = PropertyChild( pData, "heights" );
    pHeights->setExpanded( true );
    REQUIRE( pHeights->childCount() == 9 );
    auto *pFirstRow = pHeights->child( 0 );
    CHECK( pFirstRow->childCount() == 0 );
    pFirstRow->setExpanded( true );
    REQUIRE( pFirstRow->childCount() == 3 );
    CHECK( pFirstRow->child( 0 )->text( 0 ) == QStringLiteral( "[0 ... 127]" ) );
    CHECK( pFirstRow->child( 0 )->childCount() == 0 );
    pFirstRow->child( 0 )->setExpanded( true );
    CHECK( pFirstRow->child( 0 )->childCount() == 128 );
    CHECK( pFirstRow->child( 0 )->child( 0 )->text( 1 ) == QStringLiteral( "0" ) );
    auto *pFilter = panel->findChild<QLineEdit *>( QStringLiteral( "MapPropertiesFilter" ) );
    REQUIRE( pFilter != nullptr );
    auto *pDeferredPage = pFirstRow->child( 1 );
    REQUIRE( pDeferredPage->childCount() == 0 );
    pFilter->setText( QStringLiteral( "123.25" ) );
    CHECK( pData->isHidden() );
    CHECK( pDeferredPage->childCount() == 0 ); // No hidden recursive array search.
    pFilter->clear();
    pDeferredPage->setExpanded( true );
    REQUIRE( pDeferredPage->childCount() == 128 );
    pFilter->setText( QStringLiteral( "123.25" ) );
    CHECK_FALSE( pData->isHidden() );
    CHECK_FALSE( pDeferredPage->isHidden() );
    CHECK_FALSE( PropertyChild( pDeferredPage, "[200]" )->isHidden() );
    pFilter->setText( QStringLiteral( "heights" ) );
    CHECK_FALSE( pHeights->isHidden() ); // Container names stay discoverable.
    pFilter->clear();
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == 0u );
}
