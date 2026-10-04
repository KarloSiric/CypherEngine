//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_EntityProperties_Tests.cpp
//  Purpose: Tests entity key/value editing through the properties panel,
//           including typed validation, tied-brush owners and undo.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Panels.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValue.h"

#include <catch2/catch_test_macros.hpp>

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::map;

namespace
{

struct session_t {
    session_t()
    {
        auto *pApp = qobject_cast<QApplication *>( QCoreApplication::instance() );
        REQUIRE( gui::EditorGui_Init( &gui, pApp, Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
        REQUIRE( MapWorkspace_Init( &workspace, &gui ) );
        const QString path = QString::fromStdString( ( std::filesystem::path( CYPHER_MAP_EXAMPLE_DIR ) / "facility.cymap" ).string() );
        REQUIRE( MapWorkspace_Open( &workspace, path ).status == map_files_status_t::OK );
    }

    ~session_t()
    {
        MapWorkspace_Shutdown( &workspace );
        gui::EditorGui_Shutdown( &gui );
    }

    gui::editor_gui_t gui{};
    map_workspace_t workspace{};
};

struct user_settings_t {
    explicit user_settings_t( settings_registry_t *registry ) : registry( registry )
    {
        REQUIRE( SettingsDocument_Init( &store, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
        EditorSettings_SetScope( registry, settings_scope_t::USER, &store );
    }
    ~user_settings_t() { EditorSettings_SetScope( registry, settings_scope_t::USER, nullptr ); }
    void SetTreeLines( bool enabled )
    {
        const auto *descriptor = EditorSettings_Find( registry, StringView_FromCString( "editor.ui.tree_lines" ) );
        REQUIRE( descriptor != nullptr );
        setting_value_t value{};
        value.type = setting_type_t::BOOL;
        value.bValue = enabled;
        REQUIRE( EditorSettings_Write( registry, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
    }
    settings_registry_t *registry;
    settings_document_t store{};
};

std::unique_ptr<QWidget> Panel( map_workspace_t &workspace )
{
    std::unique_ptr<QWidget> panel( MapProperties_Create( nullptr, &workspace ) );
    REQUIRE( panel != nullptr );
    panel->resize( 420, 720 );
    panel->show();
    QCoreApplication::processEvents();
    return panel;
}

QTreeWidget *Tree( QWidget &panel )
{
    auto *tree = panel.findChild<QTreeWidget *>( QStringLiteral( "MapPropertiesTree" ) );
    REQUIRE( tree != nullptr );
    return tree;
}

QTreeWidgetItem *Group( QWidget &panel, const char *name )
{
    auto *tree = Tree( panel );
    for ( int i = 0; i < tree->topLevelItemCount(); ++i ) {
        auto *item = tree->topLevelItem( i );
        if ( item->text( 0 ) == QString::fromUtf8( name ) ) { return item; }
    }
    FAIL( "Property group is absent: " << name );
    return nullptr;
}

QTreeWidgetItem *Child( QTreeWidgetItem *parent, const char *name )
{
    for ( int i = 0; i < parent->childCount(); ++i ) {
        if ( parent->child( i )->text( 0 ) == QString::fromUtf8( name ) ) { return parent->child( i ); }
    }
    FAIL( "Property child is absent: " << name );
    return nullptr;
}

void CheckDecoration( const QTreeWidgetItem *item, map_property_row_kind_t kind, const char *icon )
{
    CHECK( item->data( 0, MAP_PROPERTY_KIND_ROLE ).toInt() == static_cast<int>( kind ) );
    CHECK( item->data( 0, MAP_PROPERTY_ICON_ROLE ).toString() == QString::fromLatin1( icon ) );
    CHECK_FALSE( item->icon( 0 ).isNull() );
}

QTreeWidgetItem *Keys( QWidget &panel )
{
    auto *tree = Tree( panel );
    for ( int i = 0; i < tree->topLevelItemCount(); ++i ) {
        auto *item = tree->topLevelItem( i );
        if ( item->text( 0 ) == QStringLiteral( "Entity keys" ) ) { return item; }
    }
    FAIL( "Entity keys group is absent" );
    return nullptr;
}

QTreeWidgetItem *Row( QWidget &panel, const char *key )
{
    auto *group = Keys( panel );
    for ( int i = 0; i < group->childCount(); ++i ) {
        if ( group->child( i )->text( 0 ) == QString::fromUtf8( key ) ) { return group->child( i ); }
    }
    FAIL( "Entity key is absent: " << key );
    return nullptr;
}

QTreeWidgetItem *CustomRow( QWidget &panel, const char *key )
{
    auto *group = Keys( panel );
    QTreeWidgetItem *last = nullptr;
    int matches = 0;
    for ( int i = 0; i < group->childCount(); ++i ) {
        if ( group->child( i )->text( 0 ) == QString::fromUtf8( key ) ) { last = group->child( i ); ++matches; }
    }
    REQUIRE( matches == 2 ); // Canonical identity first, identically named custom key second.
    return last;
}

void SelectRow( QWidget &panel, const char *key )
{
    auto *row = Row( panel, key );
    row->parent()->setExpanded( true );
    Tree( panel )->setCurrentItem( row );
}

QAbstractButton *Button( QWidget &panel, const char *name )
{
    auto *button = panel.findChild<QAbstractButton *>( QString::fromUtf8( name ) );
    REQUIRE( button != nullptr );
    return button;
}

const key_value_t *Property( map_workspace_t &workspace, u64 owner, const char *key )
{
    const auto *entity = MapDocument_FindObject( workspace.pDocument, owner, nullptr );
    REQUIRE( entity != nullptr );
    const auto *properties = KeyValue_Find( entity, StringView_FromCString( "properties" ) );
    return KeyValue_Find( properties, StringView_FromCString( key ) );
}

QString String( const key_value_t *value )
{
    string_view_t text{};
    REQUIRE( KeyValue_GetString( value, &text ) );
    return QString::fromUtf8( text.pData, static_cast<qsizetype>( text.cchLength ) );
}

bool Bool( const key_value_t *value )
{
    bool_t result = CY_FALSE;
    REQUIRE( KeyValue_GetBool( value, &result ) );
    return result != CY_FALSE;
}

std::vector<u64> Selection( const map_workspace_t &workspace )
{
    std::vector<u64> ids;
    for ( usize i = 0; i < EditorSelection_Count( &workspace.selection ); ++i ) { ids.push_back( EditorSelection_At( &workspace.selection, i ) ); }
    return ids;
}

struct dialog_widgets_t {
    QDialog *dialog;
    QLineEdit *key;
    QComboBox *type;
    QPlainTextEdit *value;
    QCheckBox *boolean;
    QLabel *error;
    QDialogButtonBox *buttons;
};

// The panel opens an exec() modal. A timer interacts with it from its event
// loop; the watchdog makes a broken connection fail instead of hanging tests.
void UseDialog( QWidget &panel, const std::function<void()> &open, const std::function<void( const dialog_widgets_t & )> &edit )
{
    bool visited = false;
    bool complete = false;
    bool timedOut = false;
    QTimer visit;
    visit.setSingleShot( true );
    QObject::connect( &visit, &QTimer::timeout, &panel, [&]() {
        visited = true;
        auto *dialog = panel.findChild<QDialog *>( QStringLiteral( "MapEntityPropertyDialog" ) );
        CHECK( dialog != nullptr );
        if ( dialog == nullptr ) { return; }
        dialog_widgets_t widgets{
            dialog,
            dialog->findChild<QLineEdit *>( QStringLiteral( "MapEntityPropertyKey" ) ),
            dialog->findChild<QComboBox *>( QStringLiteral( "MapEntityPropertyType" ) ),
            dialog->findChild<QPlainTextEdit *>( QStringLiteral( "MapEntityPropertyValue" ) ),
            dialog->findChild<QCheckBox *>( QStringLiteral( "MapEntityPropertyBool" ) ),
            dialog->findChild<QLabel *>( QStringLiteral( "MapEntityPropertyError" ) ),
            dialog->findChild<QDialogButtonBox *>( QStringLiteral( "MapEntityPropertyButtons" ) )
        };
        complete = widgets.key != nullptr && widgets.type != nullptr && widgets.value != nullptr && widgets.boolean != nullptr &&
                   widgets.error != nullptr && widgets.buttons != nullptr && widgets.buttons->button( QDialogButtonBox::Ok ) != nullptr &&
                   widgets.buttons->button( QDialogButtonBox::Cancel ) != nullptr;
        CHECK( complete );
        if ( !complete ) { dialog->reject(); return; }
        edit( widgets );
        if ( dialog->isVisible() ) { dialog->reject(); }
    } );
    QTimer watchdog;
    watchdog.setSingleShot( true );
    QObject::connect( &watchdog, &QTimer::timeout, &panel, [&]() {
        timedOut = true;
        if ( auto *dialog = qobject_cast<QDialog *>( QApplication::activeModalWidget() ) ) { dialog->reject(); }
    } );
    visit.start( 0 );
    watchdog.start( 2000 );
    open();
    QCoreApplication::processEvents();
    visit.stop();
    watchdog.stop();
    REQUIRE_FALSE( timedOut );
    REQUIRE( visited );
    REQUIRE( complete );
}

void UseButtonDialog( QWidget &panel, const char *buttonName, const std::function<void( const dialog_widgets_t & )> &edit )
{
    auto *button = Button( panel, buttonName );
    REQUIRE( button->isEnabled() );
    UseDialog( panel, [button]() { button->click(); }, edit );
}

bool SetType( const dialog_widgets_t &widgets, key_value_type_t type )
{
    const int index = widgets.type->findData( static_cast<int>( type ) );
    CHECK( index >= 0 );
    if ( index < 0 ) { return false; }
    widgets.type->setCurrentIndex( index );
    return true;
}

} // namespace

TEST_CASE( "Entity properties add raw strings and rename or remove them through undoable panel actions", "[map][gui][entity-properties]" )
{
    session_t session;
    auto &ws = session.workspace;
    MapWorkspace_Select( &ws, 110u, MAP_SELECT_REPLACE );
    auto panel = Panel( ws );
    CHECK( Tree( *panel )->editTriggers() == QAbstractItemView::NoEditTriggers );
    const usize steps = EditorHistory_StepCount( &ws.history );
    const QString text = QStringLiteral( "scripts/arena $wave \"entry\"\nnext" );
    UseButtonDialog( *panel, "MapEntityPropertyAdd", [&]( const dialog_widgets_t &widgets ) {
        CHECK_FALSE( widgets.key->isReadOnly() );
        widgets.key->setText( QStringLiteral( "script_path" ) );
        if ( !SetType( widgets, key_value_type_t::STRING ) ) { return; }
        widgets.value->setPlainText( text );
        widgets.buttons->button( QDialogButtonBox::Ok )->click();
    } );
    REQUIRE( Property( ws, 110u, "script_path" ) != nullptr );
    CHECK( String( Property( ws, 110u, "script_path" ) ) == text );
    CheckDecoration( Row( *panel, "script_path" ), map_property_row_kind_t::VALUE, "entity-logic" );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    CHECK( Property( ws, 110u, "script_path" ) == nullptr );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    CHECK( String( Property( ws, 110u, "script_path" ) ) == text );

    SelectRow( *panel, "script_path" );
    UseButtonDialog( *panel, "MapEntityPropertyRename", [&]( const dialog_widgets_t &widgets ) {
        CHECK_FALSE( widgets.key->isReadOnly() );
        CHECK_FALSE( widgets.type->isVisible() );
        CHECK_FALSE( widgets.value->isVisible() );
        widgets.key->setText( QStringLiteral( "script_asset" ) );
        widgets.buttons->button( QDialogButtonBox::Ok )->click();
    } );
    CHECK( Property( ws, 110u, "script_path" ) == nullptr );
    CHECK( String( Property( ws, 110u, "script_asset" ) ) == text );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    CHECK( Property( ws, 110u, "script_asset" ) == nullptr );
    CHECK( String( Property( ws, 110u, "script_path" ) ) == text );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );

    SelectRow( *panel, "script_asset" );
    auto *remove = Button( *panel, "MapEntityPropertyRemove" );
    REQUIRE( remove->isEnabled() );
    remove->click();
    CHECK( Property( ws, 110u, "script_asset" ) == nullptr );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    CHECK( String( Property( ws, 110u, "script_asset" ) ) == text );
    CHECK( EditorSelection_Count( &ws.selection ) == 1u );
    CHECK( EditorSelection_At( &ws.selection, 0u ) == 110u );
}

TEST_CASE( "A tied-brush property double click edits its entity owner and preserves brush selection", "[map][gui][entity-properties]" )
{
    session_t session;
    auto &ws = session.workspace;
    MapWorkspace_Select( &ws, 111u, MAP_SELECT_REPLACE );
    auto panel = Panel( ws );
    const auto selection = Selection( ws );
    const usize steps = EditorHistory_StepCount( &ws.history );
    auto *row = Row( *panel, "start_disabled" );
    Tree( *panel )->setCurrentItem( row );
    UseDialog( *panel, [&]() { Tree( *panel )->itemDoubleClicked( row, 1 ); }, [&]( const dialog_widgets_t &widgets ) {
        CHECK( widgets.key->isReadOnly() );
        CHECK( widgets.type->currentData().toInt() == static_cast<int>( key_value_type_t::BOOL ) );
        CHECK_FALSE( widgets.boolean->isChecked() );
        widgets.boolean->setChecked( true );
        widgets.buttons->button( QDialogButtonBox::Ok )->click();
    } );
    CHECK( Bool( Property( ws, 110u, "start_disabled" ) ) );
    CHECK( Selection( ws ) == selection );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
    const auto *brush = MapDocument_FindObject( ws.pDocument, 111u, nullptr );
    REQUIRE( brush != nullptr );
    CHECK( KeyValue_Find( brush, StringView_FromCString( "properties" ) ) == nullptr );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    CHECK_FALSE( Bool( Property( ws, 110u, "start_disabled" ) ) );
    CHECK( Selection( ws ) == selection );
}

TEST_CASE( "Invalid numeric property text keeps the dialog open without changing the map or history", "[map][gui][entity-properties]" )
{
    session_t session;
    auto &ws = session.workspace;
    MapWorkspace_Select( &ws, 110u, MAP_SELECT_REPLACE );
    auto panel = Panel( ws );
    const usize steps = EditorHistory_StepCount( &ws.history );
    auto *document = ws.pDocument;
    REQUIRE_FALSE( MapWorkspace_IsModified( &ws ) );
    UseButtonDialog( *panel, "MapEntityPropertyAdd", [&]( const dialog_widgets_t &widgets ) {
        widgets.key->setText( QStringLiteral( "spawn_count" ) );
        if ( !SetType( widgets, key_value_type_t::I64 ) ) { return; }
        widgets.value->setPlainText( QStringLiteral( "3.5" ) );
        widgets.buttons->button( QDialogButtonBox::Ok )->click();
        CHECK( widgets.dialog->isVisible() );
        CHECK_FALSE( widgets.error->text().isEmpty() );
        CHECK( ws.pDocument == document );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps );
        CHECK_FALSE( MapWorkspace_IsModified( &ws ) );
        CHECK( Property( ws, 110u, "spawn_count" ) == nullptr );
        widgets.buttons->button( QDialogButtonBox::Cancel )->click();
    } );
    CHECK( ws.pDocument == document );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    CHECK( Property( ws, 110u, "spawn_count" ) == nullptr );
}

TEST_CASE( "The entity key union distinguishes mixed and partially present empty values and edits all owners once", "[map][gui][entity-properties]" )
{
    session_t session;
    auto &ws = session.workspace;
    const u64 ids[]{ 110u, 131u };
    MapWorkspace_SetSelection( &ws, ids, 2u );
    auto panel = Panel( ws );
    auto *mixed = Row( *panel, "start_disabled" );
    auto *partial = Row( *panel, "filter" );
    CHECK( mixed->text( 1 ) == QStringLiteral( "Multiple values" ) );
    CHECK_FALSE( partial->toolTip( 0 ).isEmpty() );
    CHECK( partial->toolTip( 0 ) != mixed->toolTip( 0 ) );
    const auto selection = Selection( ws );
    const usize steps = EditorHistory_StepCount( &ws.history );
    SelectRow( *panel, "start_disabled" );
    UseButtonDialog( *panel, "MapEntityPropertyEdit", [&]( const dialog_widgets_t &widgets ) {
        if ( !SetType( widgets, key_value_type_t::BOOL ) ) { return; }
        widgets.boolean->setChecked( false );
        widgets.buttons->button( QDialogButtonBox::Ok )->click();
    } );
    CHECK_FALSE( Bool( Property( ws, 110u, "start_disabled" ) ) );
    CHECK_FALSE( Bool( Property( ws, 131u, "start_disabled" ) ) );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
    CHECK( Selection( ws ) == selection );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    CHECK_FALSE( Bool( Property( ws, 110u, "start_disabled" ) ) );
    CHECK( Bool( Property( ws, 131u, "start_disabled" ) ) );
    CHECK( Row( *panel, "start_disabled" )->text( 1 ) == QStringLiteral( "Multiple values" ) );

    const u64 emptyIds[]{ 110u, 150u };
    MapWorkspace_SetSelection( &ws, emptyIds, 2u );
    auto *empty = Row( *panel, "open_sound" );
    CHECK( empty->text( 1 ) != QStringLiteral( "Multiple values" ) );
    CHECK_FALSE( empty->toolTip( 0 ).isEmpty() );
    CHECK( String( Property( ws, 150u, "open_sound" ) ).isEmpty() );
    CHECK( Property( ws, 110u, "open_sound" ) == nullptr );
}

TEST_CASE( "Entity identity value editing protects class and name from key removal or renaming", "[map][gui][entity-properties]" )
{
    session_t session;
    auto &ws = session.workspace;
    MapWorkspace_Select( &ws, 110u, MAP_SELECT_REPLACE );
    auto panel = Panel( ws );
    for ( const char *identity : { "class", "name" } ) {
        SelectRow( *panel, identity );
        CHECK( Button( *panel, "MapEntityPropertyEdit" )->isEnabled() );
        CHECK_FALSE( Button( *panel, "MapEntityPropertyRename" )->isEnabled() );
        CHECK_FALSE( Button( *panel, "MapEntityPropertyRemove" )->isEnabled() );
    }
    SelectRow( *panel, "name" );
    UseButtonDialog( *panel, "MapEntityPropertyEdit", [&]( const dialog_widgets_t &widgets ) {
        CHECK( widgets.key->isReadOnly() );
        widgets.value->setPlainText( QStringLiteral( "arena_trigger" ) );
        widgets.buttons->button( QDialogButtonBox::Ok )->click();
    } );
    const auto *entity = MapDocument_FindObject( ws.pDocument, 110u, nullptr );
    REQUIRE( entity != nullptr );
    CHECK( String( KeyValue_Find( entity, StringView_FromCString( "name" ) ) ) == QStringLiteral( "arena_trigger" ) );
    CHECK( Property( ws, 110u, "name" ) == nullptr );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    entity = MapDocument_FindObject( ws.pDocument, 110u, nullptr );
    CHECK( String( KeyValue_Find( entity, StringView_FromCString( "name" ) ) ) == QStringLiteral( "wave1_trigger" ) );
}

TEST_CASE( "Entity property actions refuse mixed world geometry and read-only maps", "[map][gui][entity-properties]" )
{
    session_t session;
    auto &ws = session.workspace;
    MapWorkspace_Select( &ws, 110u, MAP_SELECT_REPLACE );
    auto panel = Panel( ws );
    SECTION( "Mixed world and entity selections" )
    {
        const u64 ids[]{ 110u, 1000u };
        MapWorkspace_SetSelection( &ws, ids, 2u );
    }
    SECTION( "Read-only document" )
    {
        ws.pDocument->bReadOnly = CY_TRUE;
        MapWorkspace_Notify( &ws, MAP_CHANGE_DOCUMENT );
        SelectRow( *panel, "filter" );
    }
    for ( const char *name : { "MapEntityPropertyAdd", "MapEntityPropertyEdit", "MapEntityPropertyRename", "MapEntityPropertyRemove" } ) {
        CHECK_FALSE( Button( *panel, name )->isEnabled() );
    }
    CHECK_FALSE( MapWorkspace_IsModified( &ws ) );
}

TEST_CASE( "Editing a malformed numeric entity name to empty text repairs its type and can be undone", "[map][gui][entity-properties]" )
{
    session_t session;
    auto &ws = session.workspace;
    map_chunk_t *chunk = nullptr;
    auto *entity = MapDocument_FindObject( ws.pDocument, 110u, &chunk );
    REQUIRE( entity != nullptr );
    REQUIRE( chunk != nullptr );
    auto *name = KeyValue_Find( entity, StringView_FromCString( "name" ) );
    REQUIRE( name != nullptr );
    REQUIRE( KeyValue_SetI64( chunk->store.pDocument, name, 7 ) );
    MapWorkspace_DocumentChanged( &ws );
    MapWorkspace_Select( &ws, 110u, MAP_SELECT_REPLACE );
    auto panel = Panel( ws );
    SelectRow( *panel, "name" );
    const usize steps = EditorHistory_StepCount( &ws.history );
    UseButtonDialog( *panel, "MapEntityPropertyEdit", [&]( const dialog_widgets_t &widgets ) {
        CHECK( widgets.key->isReadOnly() );
        CHECK( widgets.type->currentData().toInt() == static_cast<int>( key_value_type_t::STRING ) );
        widgets.value->clear();
        widgets.buttons->button( QDialogButtonBox::Ok )->click();
    } );
    entity = MapDocument_FindObject( ws.pDocument, 110u, nullptr );
    REQUIRE( entity != nullptr );
    name = KeyValue_Find( entity, StringView_FromCString( "name" ) );
    CHECK( KeyValue_Type( name ) == key_value_type_t::STRING );
    CHECK( String( name ).isEmpty() );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    entity = MapDocument_FindObject( ws.pDocument, 110u, nullptr );
    i64 number = 0;
    REQUIRE( KeyValue_GetI64( KeyValue_Find( entity, StringView_FromCString( "name" ) ), &number ) );
    CHECK( number == 7 );
}

TEST_CASE( "Custom class and name keys retain their selected row and do not edit canonical identity", "[map][gui][entity-properties]" )
{
    for ( const char *key : { "class", "name" } ) {
        session_t session;
        auto &ws = session.workspace;
        map_chunk_t *chunk = nullptr;
        auto *entity = MapDocument_FindObject( ws.pDocument, 110u, &chunk );
        REQUIRE( entity != nullptr );
        REQUIRE( chunk != nullptr );
        auto *properties = KeyValue_Find( entity, StringView_FromCString( "properties" ) );
        REQUIRE( properties != nullptr );
        auto *custom = KeyValue_ObjectInsert( chunk->store.pDocument, properties, StringView_FromCString( key ), key_value_type_t::STRING );
        REQUIRE( custom != nullptr );
        REQUIRE( KeyValue_SetString( chunk->store.pDocument, custom, StringView_FromCString( "extension value" ) ) );
        const QString canonical = String( KeyValue_Find( entity, StringView_FromCString( key ) ) );
        MapWorkspace_DocumentChanged( &ws );
        MapWorkspace_Select( &ws, 110u, MAP_SELECT_REPLACE );
        auto panel = Panel( ws );
        auto *customRow = CustomRow( *panel, key );
        customRow->parent()->setExpanded( true );
        Tree( *panel )->setCurrentItem( customRow );
        REQUIRE( Button( *panel, "MapEntityPropertyRename" )->isEnabled() );
        const usize steps = EditorHistory_StepCount( &ws.history );
        UseButtonDialog( *panel, "MapEntityPropertyEdit", [&]( const dialog_widgets_t &widgets ) {
            CHECK( widgets.value->toPlainText() == QStringLiteral( "extension value" ) );
            widgets.value->setPlainText( QStringLiteral( "updated extension" ) );
            widgets.buttons->button( QDialogButtonBox::Ok )->click();
        } );
        CHECK( String( Property( ws, 110u, key ) ) == QStringLiteral( "updated extension" ) );
        entity = MapDocument_FindObject( ws.pDocument, 110u, nullptr );
        CHECK( String( KeyValue_Find( entity, StringView_FromCString( key ) ) ) == canonical );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
        CHECK( Tree( *panel )->currentItem() == CustomRow( *panel, key ) );
        CHECK( Button( *panel, "MapEntityPropertyRename" )->isEnabled() );
        CHECK( Button( *panel, "MapEntityPropertyRemove" )->isEnabled() );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
        CHECK( String( Property( ws, 110u, key ) ) == QStringLiteral( "extension value" ) );
        CHECK( Tree( *panel )->currentItem() == CustomRow( *panel, key ) );
    }
}

TEST_CASE( "Structured property dialogs preserve numeric kinds and reject duplicate keys before a corrected retry", "[map][gui][entity-properties]" )
{
    session_t session;
    auto &ws = session.workspace;
    MapWorkspace_Select( &ws, 110u, MAP_SELECT_REPLACE );
    auto panel = Panel( ws );
    UseButtonDialog( *panel, "MapEntityPropertyAdd", [&]( const dialog_widgets_t &widgets ) {
        widgets.key->setText( QStringLiteral( "settings" ) );
        if ( !SetType( widgets, key_value_type_t::OBJECT ) ) { return; }
        widgets.value->setPlainText( QStringLiteral( "{ count = 7u gain = 1.0 nested = [ 2u, 3.0 ] }" ) );
        widgets.buttons->button( QDialogButtonBox::Ok )->click();
        INFO( widgets.error->text().toStdString() );
        CHECK( widgets.error->text().isEmpty() );
    } );
    const auto *settings = Property( ws, 110u, "settings" );
    REQUIRE( KeyValue_Type( settings ) == key_value_type_t::OBJECT );
    CHECK( KeyValue_Type( KeyValue_Find( settings, StringView_FromCString( "count" ) ) ) == key_value_type_t::U64 );
    CHECK( KeyValue_Type( KeyValue_Find( settings, StringView_FromCString( "gain" ) ) ) == key_value_type_t::F64 );
    const auto *nested = KeyValue_Find( settings, StringView_FromCString( "nested" ) );
    CHECK( KeyValue_Type( KeyValue_ChildAt( nested, 0u ) ) == key_value_type_t::U64 );
    CHECK( KeyValue_Type( KeyValue_ChildAt( nested, 1u ) ) == key_value_type_t::F64 );

    // Writing an existing object to text and reading it back must retain 1u
    // versus 1.0. Applying the unmodified text should create no history step.
    SelectRow( *panel, "settings" );
    const usize objectSteps = EditorHistory_StepCount( &ws.history );
    UseButtonDialog( *panel, "MapEntityPropertyEdit", [&]( const dialog_widgets_t &widgets ) {
        CHECK( widgets.type->currentData().toInt() == static_cast<int>( key_value_type_t::OBJECT ) );
        widgets.buttons->button( QDialogButtonBox::Ok )->click();
    } );
    CHECK( EditorHistory_StepCount( &ws.history ) == objectSteps );
    CHECK( KeyValue_Type( KeyValue_Find( Property( ws, 110u, "settings" ), StringView_FromCString( "gain" ) ) ) == key_value_type_t::F64 );

    SelectRow( *panel, "settings" );
    auto *document = ws.pDocument;
    UseButtonDialog( *panel, "MapEntityPropertyEdit", [&]( const dialog_widgets_t &widgets ) {
        widgets.value->setPlainText( QStringLiteral( "{ count = 7u count = 8u }" ) );
        widgets.buttons->button( QDialogButtonBox::Ok )->click();
        CHECK( widgets.dialog->isVisible() );
        CHECK_FALSE( widgets.error->text().isEmpty() );
        CHECK( ws.pDocument == document );
        CHECK( EditorHistory_StepCount( &ws.history ) == objectSteps );
        u64 original = 0u;
        CHECK( KeyValue_GetU64( KeyValue_Find( Property( ws, 110u, "settings" ), StringView_FromCString( "count" ) ), &original ) );
        CHECK( original == 7u );
        widgets.value->setPlainText( QStringLiteral( "{ count = 9u gain = 2.0 nested = [ 4u, 5.0 ] }" ) );
        widgets.buttons->button( QDialogButtonBox::Ok )->click();
    } );
    CHECK( EditorHistory_StepCount( &ws.history ) == objectSteps + 1u );
    settings = Property( ws, 110u, "settings" );
    u64 count = 0u;
    REQUIRE( KeyValue_GetU64( KeyValue_Find( settings, StringView_FromCString( "count" ) ), &count ) );
    CHECK( count == 9u );
    CHECK( KeyValue_Type( KeyValue_Find( settings, StringView_FromCString( "gain" ) ) ) == key_value_type_t::F64 );

    SelectRow( *panel, "settings" );
    UseButtonDialog( *panel, "MapEntityPropertyEdit", [&]( const dialog_widgets_t &widgets ) {
        if ( !SetType( widgets, key_value_type_t::ARRAY ) ) { return; }
        widgets.value->setPlainText( QStringLiteral( "[ { count = 18446744073709551615u gain = 1.0 }, 2u, 3.0 ]" ) );
        widgets.buttons->button( QDialogButtonBox::Ok )->click();
    } );
    settings = Property( ws, 110u, "settings" );
    REQUIRE( KeyValue_Type( settings ) == key_value_type_t::ARRAY );
    CHECK( KeyValue_ChildCount( settings ) == 3u );
    const auto *first = KeyValue_ChildAt( settings, 0u );
    REQUIRE( KeyValue_GetU64( KeyValue_Find( first, StringView_FromCString( "count" ) ), &count ) );
    CHECK( count == std::numeric_limits<u64>::max() );
    CHECK( KeyValue_Type( KeyValue_Find( first, StringView_FromCString( "gain" ) ) ) == key_value_type_t::F64 );
    CHECK( KeyValue_Type( KeyValue_ChildAt( settings, 1u ) ) == key_value_type_t::U64 );
    CHECK( KeyValue_Type( KeyValue_ChildAt( settings, 2u ) ) == key_value_type_t::F64 );
    const usize arraySteps = EditorHistory_StepCount( &ws.history );
    SelectRow( *panel, "settings" );
    UseButtonDialog( *panel, "MapEntityPropertyEdit", [&]( const dialog_widgets_t &widgets ) {
        CHECK( widgets.type->currentData().toInt() == static_cast<int>( key_value_type_t::ARRAY ) );
        widgets.buttons->button( QDialogButtonBox::Ok )->click();
    } );
    CHECK( EditorHistory_StepCount( &ws.history ) == arraySteps );
}

TEST_CASE( "An entity property dialog rejects changes to its selection or document while it is open", "[map][gui][entity-properties]" )
{
    session_t session;
    auto &ws = session.workspace;
    MapWorkspace_Select( &ws, 110u, MAP_SELECT_REPLACE );
    auto panel = Panel( ws );
    SelectRow( *panel, "filter" );
    const usize steps = EditorHistory_StepCount( &ws.history );
    auto *document = ws.pDocument;
    bool changeSelection = false;
    SECTION( "Selection switches to another entity" ) { changeSelection = true; }
    SECTION( "The existing document changes in place" ) { changeSelection = false; }
    UseButtonDialog( *panel, "MapEntityPropertyEdit", [&]( const dialog_widgets_t &widgets ) {
        widgets.value->setPlainText( QStringLiteral( "stale dialog value" ) );
        if ( changeSelection ) {
            MapWorkspace_Select( &ws, 120u, MAP_SELECT_REPLACE );
        } else {
            map_chunk_t *chunk = nullptr;
            auto *entity = MapDocument_FindObject( ws.pDocument, 110u, &chunk );
            CHECK( entity != nullptr );
            CHECK( chunk != nullptr );
            if ( entity == nullptr || chunk == nullptr ) { return; }
            auto *filter = KeyValue_Find( KeyValue_Find( entity, StringView_FromCString( "properties" ) ), StringView_FromCString( "filter" ) );
            CHECK( KeyValue_SetString( chunk->store.pDocument, filter, StringView_FromCString( "external update" ) ) );
            MapWorkspace_DocumentChanged( &ws );
        }
        widgets.buttons->button( QDialogButtonBox::Ok )->click();
        CHECK( widgets.dialog->isVisible() );
        CHECK_FALSE( widgets.error->text().isEmpty() );
        CHECK( ws.pDocument == document );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps );
        CHECK( String( Property( ws, 110u, "filter" ) ) == ( changeSelection ? QStringLiteral( "players" ) : QStringLiteral( "external update" ) ) );
        CHECK( Property( ws, 120u, "filter" ) == nullptr );
        widgets.buttons->button( QDialogButtonBox::Cancel )->click();
    } );
    CHECK( ws.pDocument == document );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    CHECK( EditorSelection_Count( &ws.selection ) == 1u );
    CHECK( EditorSelection_At( &ws.selection, 0u ) == ( changeSelection ? 120u : 110u ) );
}

TEST_CASE( "Object Properties paints hierarchy connections and keeps disclosure arrows when lines are disabled", "[map][gui][entity-properties][properties-tree]" )
{
    session_t session;
    auto &ws = session.workspace;
    user_settings_t settings( &session.gui.settings );
    MapWorkspace_Select( &ws, 110u, MAP_SELECT_REPLACE );
    auto panel = Panel( ws );
    auto *tree = Tree( *panel );
    auto *keys = Keys( *panel );
    auto *filter = Row( *panel, "filter" );
    const usize steps = EditorHistory_StepCount( &ws.history );
    tree->scrollToItem( filter );
    QCoreApplication::processEvents();
    const QRect rowRect = tree->visualItemRect( filter );
    REQUIRE_FALSE( rowRect.isEmpty() );
    REQUIRE( rowRect.left() > 0 );
    const QRect branch( 0, rowRect.top(), rowRect.left(), rowRect.height() );
    const QImage withLines = tree->viewport()->grab( branch ).toImage();
    settings.SetTreeLines( false );
    QCoreApplication::processEvents();
    const QImage withoutLines = tree->viewport()->grab( branch ).toImage();
    CHECK( withLines != withoutLines );
    CHECK( tree->rootIsDecorated() );
    CHECK( tree->itemsExpandable() );
    REQUIRE( keys->isExpanded() );
    const QRect groupRect = tree->visualItemRect( keys );
    REQUIRE_FALSE( groupRect.isEmpty() );
    const QRect disclosure( 0, groupRect.top(), tree->indentation(), groupRect.height() );
    const QImage openArrow = tree->viewport()->grab( disclosure ).toImage();
    tree->collapseItem( keys );
    QCoreApplication::processEvents();
    CHECK_FALSE( keys->isExpanded() );
    CHECK( tree->viewport()->grab( disclosure ).toImage() != openArrow );
    tree->expandItem( keys );
    settings.SetTreeLines( true );
    tree->scrollToItem( filter );
    QCoreApplication::processEvents();
    CHECK( tree->viewport()->grab( branch ).toImage() == withLines );
    CHECK( filter->parent() == keys );
    CHECK( Selection( ws ) == std::vector<u64>{ 110u } );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    CHECK_FALSE( MapWorkspace_IsModified( &ws ) );
}

TEST_CASE( "Object Properties decorates semantic groups and lazily populated geometry", "[map][gui][entity-properties][properties-tree]" )
{
    session_t session;
    auto &ws = session.workspace;
    MapWorkspace_Select( &ws, 111u, MAP_SELECT_REPLACE );
    auto panel = Panel( ws );
    auto *tree = Tree( *panel );
    const usize steps = EditorHistory_StepCount( &ws.history );
    CheckDecoration( Keys( *panel ), map_property_row_kind_t::GROUP, "entity-trigger" );
    CheckDecoration( Row( *panel, "class" ), map_property_row_kind_t::VALUE, "entity-trigger" );
    CheckDecoration( Row( *panel, "filter" ), map_property_row_kind_t::VALUE, "keymap" );
    CheckDecoration( Group( *panel, "Identity" ), map_property_row_kind_t::GROUP, "tool-block" );
    CheckDecoration( Group( *panel, "Placement" ), map_property_row_kind_t::GROUP, "tool-translate" );
    CheckDecoration( Child( Group( *panel, "Placement" ), "Owner entity" ), map_property_row_kind_t::VALUE, "group-create" );
    CheckDecoration( Group( *panel, "Geometry" ), map_property_row_kind_t::GROUP, "tool-block" );
    CheckDecoration( Child( Group( *panel, "Geometry" ), "Planes" ), map_property_row_kind_t::VALUE, "select-faces" );
    auto *materials = Group( *panel, "Materials" );
    CheckDecoration( materials, map_property_row_kind_t::GROUP, "asset-material" );
    REQUIRE( materials->childCount() > 0 );
    CheckDecoration( materials->child( 0 ), map_property_row_kind_t::VALUE, "asset-material" );
    auto *data = Group( *panel, "Geometry Data" );
    CheckDecoration( data, map_property_row_kind_t::GROUP, "tool-block" );
    REQUIRE( data->childCount() == 0 );
    CHECK( data->childIndicatorPolicy() == QTreeWidgetItem::ShowIndicator );
    tree->expandItem( data );
    REQUIRE( data->childCount() > 0 );
    auto *faces = Child( data, "faces" );
    CheckDecoration( faces, map_property_row_kind_t::ARRAY, "view-properties" );
    REQUIRE( faces->childCount() == 0 );
    tree->expandItem( faces );
    REQUIRE( faces->childCount() == 6 );
    auto *face = faces->child( 0 );
    CheckDecoration( face, map_property_row_kind_t::OBJECT, "asset-folder" );
    REQUIRE( face->childCount() == 0 );
    tree->expandItem( face );
    CheckDecoration( Child( face, "material" ), map_property_row_kind_t::VALUE, "asset-material" );
    for ( QTreeWidgetItemIterator it( tree ); *it != nullptr; ++it ) {
        CHECK_FALSE( ( *it )->icon( 0 ).isNull() );
        CHECK( ( *it )->data( 0, MAP_PROPERTY_KIND_ROLE ).isValid() );
    }
    CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    CHECK( Selection( ws ) == std::vector<u64>{ 111u } );
    CHECK_FALSE( MapWorkspace_IsModified( &ws ) );

    // Decorations remain presentation only; the existing typed edit path still
    // targets the owning entity and restores the decorated row after publishing.
    SelectRow( *panel, "start_disabled" );
    UseButtonDialog( *panel, "MapEntityPropertyEdit", [&]( const dialog_widgets_t &widgets ) {
        CHECK( widgets.type->currentData().toInt() == static_cast<int>( key_value_type_t::BOOL ) );
        widgets.boolean->setChecked( true );
        widgets.buttons->button( QDialogButtonBox::Ok )->click();
    } );
    CHECK( Bool( Property( ws, 110u, "start_disabled" ) ) );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
    CHECK( Tree( *panel )->currentItem() == Row( *panel, "start_disabled" ) );
    CheckDecoration( Row( *panel, "start_disabled" ), map_property_row_kind_t::VALUE, "keymap" );
    CHECK( Group( *panel, "Geometry Data" )->isExpanded() );
    CHECK( Selection( ws ) == std::vector<u64>{ 111u } );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    CHECK_FALSE( Bool( Property( ws, 110u, "start_disabled" ) ) );
    CheckDecoration( Row( *panel, "start_disabled" ), map_property_row_kind_t::VALUE, "keymap" );
}

TEST_CASE( "Object Properties decorates source objects and array pages before lazy population", "[map][gui][entity-properties][properties-tree]" )
{
    session_t session;
    auto &ws = session.workspace;
    MapWorkspace_Select( &ws, 110u, MAP_SELECT_REPLACE );
    key_value_document_desc_t desc{};
    desc.pAllocator = Allocator_GetSystem();
    std::unique_ptr<key_value_document_t, decltype( &KeyValue_DestroyDocument )> values( KeyValue_CreateDocument( desc ), &KeyValue_DestroyDocument );
    REQUIRE( values != nullptr );
    REQUIRE( KeyValue_SetRootType( values.get(), key_value_type_t::ARRAY ) );
    for ( u64 i = 0u; i < 130u; ++i ) {
        auto *value = KeyValue_ArrayAppend( values.get(), KeyValue_Root( values.get() ), key_value_type_t::U64 );
        REQUIRE( value != nullptr );
        REQUIRE( KeyValue_SetU64( values.get(), value, i ) );
    }
    REQUIRE( MapWorkspace_SetEntityProperty( &ws, StringView_FromCString( "samples" ), KeyValue_Root( values.get() ) ) );
    auto panel = Panel( ws );
    auto *tree = Tree( *panel );
    const usize steps = EditorHistory_StepCount( &ws.history );
    CheckDecoration( Row( *panel, "samples" ), map_property_row_kind_t::ARRAY, "view-properties" );
    auto *source = Group( *panel, "Source Fields" );
    CheckDecoration( source, map_property_row_kind_t::GROUP, "asset-folder" );
    auto *properties = Child( source, "properties" );
    CheckDecoration( properties, map_property_row_kind_t::OBJECT, "asset-folder" );
    auto *samples = Child( properties, "samples" );
    CheckDecoration( samples, map_property_row_kind_t::ARRAY, "view-properties" );
    REQUIRE( samples->childCount() == 0 );
    tree->expandItem( samples );
    REQUIRE( samples->childCount() == 2 );
    auto *firstPage = samples->child( 0 );
    CheckDecoration( firstPage, map_property_row_kind_t::ARRAY_PAGE, "view-properties" );
    CheckDecoration( samples->child( 1 ), map_property_row_kind_t::ARRAY_PAGE, "view-properties" );
    REQUIRE( firstPage->childCount() == 0 );
    tree->expandItem( firstPage );
    REQUIRE( firstPage->childCount() == 128 );
    CheckDecoration( firstPage->child( 0 ), map_property_row_kind_t::VALUE, "keymap" );
    CHECK( firstPage->child( 0 )->text( 1 ) == QStringLiteral( "0" ) );
    CHECK( samples->child( 1 )->childCount() == 0 );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    CHECK( Selection( ws ) == std::vector<u64>{ 110u } );
    ws.pDocument->bReadOnly = true;
    MapWorkspace_Notify( &ws, MAP_CHANGE_VIEW );
    SelectRow( *panel, "samples" );
    CHECK_FALSE( Button( *panel, "MapEntityPropertyEdit" )->isEnabled() );
    CheckDecoration( Row( *panel, "samples" ), map_property_row_kind_t::ARRAY, "view-properties" );
}

TEST_CASE( "Object Properties keeps light icons and selection through style refresh and releases its listeners", "[map][gui][entity-properties][properties-tree]" )
{
    session_t session;
    auto &ws = session.workspace;
    user_settings_t settings( &session.gui.settings );
    const usize styleListeners = session.gui.nStyleListeners;
    const usize settingListeners = session.gui.settings.nListeners;
    MapWorkspace_Select( &ws, 140u, MAP_SELECT_REPLACE );
    auto panel = Panel( ws );
    CHECK( session.gui.nStyleListeners == styleListeners + 1u );
    CHECK( session.gui.settings.nListeners == settingListeners + 1u );
    CheckDecoration( Keys( *panel ), map_property_row_kind_t::GROUP, "entity-light" );
    CheckDecoration( Group( *panel, "Identity" ), map_property_row_kind_t::GROUP, "entity-light" );
    CheckDecoration( Row( *panel, "class" ), map_property_row_kind_t::VALUE, "entity-light" );
    CheckDecoration( Row( *panel, "color" ), map_property_row_kind_t::VALUE, "entity-light" );
    CheckDecoration( Row( *panel, "intensity" ), map_property_row_kind_t::VALUE, "entity-light" );
    CheckDecoration( Row( *panel, "range" ), map_property_row_kind_t::VALUE, "entity-light" );
    SelectRow( *panel, "cast_shadows" );
    auto *current = Tree( *panel )->currentItem();
    const usize steps = EditorHistory_StepCount( &ws.history );
    REQUIRE( gui::EditorGui_ApplyStyle( &session.gui, qobject_cast<QApplication *>( QCoreApplication::instance() ) ) == gui::editor_gui_status_t::OK );
    CHECK( Tree( *panel )->currentItem() == current );
    CHECK( Keys( *panel )->isExpanded() );
    CheckDecoration( current, map_property_row_kind_t::VALUE, "entity-light" );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    CHECK( Selection( ws ) == std::vector<u64>{ 140u } );
    panel.reset();
    CHECK( session.gui.nStyleListeners == styleListeners );
    CHECK( session.gui.settings.nListeners == settingListeners );
    // Notify after destruction: callbacks must not retain the departed tree.
    settings.SetTreeLines( false );
    REQUIRE( gui::EditorGui_ApplyStyle( &session.gui, qobject_cast<QApplication *>( QCoreApplication::instance() ) ) == gui::editor_gui_status_t::OK );
}
