//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_PropertyEditors_Tests.cpp
//  Purpose: Tests the typed property editors Object Properties shows for an
//           entity class the game profile defines: what each type looks
//           like, defaults, limits, mixed selections, and that every change
//           is one undo step on the entity.
//
//  History:
//  - Created by Karlo Siric on 2026-10-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_EntityLook.h"
#include "CypherMapGui_Game.h"
#include "CypherMapGui_Hotspots.h"
#include "CypherMapGui_Panels.h"
#include "CypherMapGui_PropertyEditors.h"
#include "CypherMap_EntityEdit.h"
#include "CypherMap_ObjectCreate.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValue.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <functional>
#include <memory>
#include <string>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::map;

namespace
{

constexpr const char *kProfile = R"cykv(@cykv 1
@schema "cypher.game" 1
{
    id = "reap"
    name = "REAP"
    version = 1u
    classes = [
        { name = "targetable" kind = "abstract" properties = [ { key = "start_disabled" type = "bool" default = false label = "Start Disabled" group = "Logic" } ] },
        { name = "lamp" kind = "point" base = [ "targetable" ] label = "Lamp" editor = { color_from = "color" }
          properties = [
            { key = "color" type = "color" default = "#ffffff" label = "Colour" },
            { key = "intensity" type = "real" default = 300.0 min = 0.0 max = 1000.0 unit = "candela" label = "Intensity" },
            { key = "count" type = "integer" default = 1 min = 1 max = 8 label = "Count" },
            { key = "cast_shadows" type = "bool" default = true label = "Cast Shadows" },
            { key = "team" type = "choice" default = "any" choices = [ "any", "red", "blue" ] label = "Team" },
            { key = "flags" type = "flags" default = [ "silent" ] choices = [ "silent", "boss" ] label = "Flags" },
            { key = "aim" type = "angles" default = [ 0.0, 90.0, 0.0 ] label = "Aim" },
            { key = "sound" type = "asset" default = "" extensions = [ ".cysnd" ] label = "Sound" },
            { key = "follow" type = "target" default = "" label = "Follow" },
            { key = "note" type = "string" default = "" label = "Note" },
            { key = "secret" type = "string" default = "" hidden = true }
          ] },
        { name = "beacon" kind = "point" properties = [ { key = "range" type = "real" default = 64.0 } ] },
        { name = "marker" kind = "point"
          editor = { size = [ -16, -16, 0, 16, 16, 72 ] color = "#40c040" helpers = [ { type = "arrow" }, { type = "radius" property = "range" }, { type = "line_to" property = "follow" } ] }
          properties = [ { key = "range" type = "real" default = 100.0 }, { key = "follow" type = "target" default = "" } ] },
        { name = "door" kind = "solid" editor = { helpers = [ { type = "move_direction" property = "move_dir" distance = "lip" } ] }
          properties = [ { key = "move_dir" type = "angles" default = [ 0.0, 90.0, 0.0 ] }, { key = "lip" type = "real" default = 8.0 } ] }
    ]
}
)cykv";

struct session_t {
    session_t()
    {
        auto *pApp = qobject_cast<QApplication *>( QCoreApplication::instance() );
        REQUIRE( gui::EditorGui_Init( &gui, pApp, Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
        REQUIRE( MapWorkspace_Init( &workspace, &gui ) );
        REQUIRE( content.isValid() );
        REQUIRE( QDir( content.path() ).mkpath( QStringLiteral( "game" ) ) );
        REQUIRE( QDir( content.path() ).mkpath( QStringLiteral( "sounds" ) ) );
        Write( QStringLiteral( "game/reap.cygame" ), kProfile );
        Write( QStringLiteral( "sounds/hum.cysnd" ), "" );
        MapHotspots_SetResolver( &workspace, [this]( const QString &path ) {
            const QString file = QDir( content.path() ).filePath( path );
            return QFileInfo::exists( file ) ? file : QString();
        } );
        MapAssets_SetLister( &workspace, []( const QStringList & ) { return QStringList{ QStringLiteral( "sounds/hum.cysnd" ) }; } );
        REQUIRE( MapGame_Profile( &workspace ) != nullptr );
    }

    ~session_t()
    {
        panel.reset();
        MapAssets_SetLister( &workspace, {} );
        MapHotspots_SetResolver( &workspace, {} );
        MapGame_Forget( &workspace );
        MapWorkspace_Shutdown( &workspace );
        gui::EditorGui_Shutdown( &gui );
    }

    void Write( const QString &path, const char *text )
    {
        QFile file( QDir( content.path() ).filePath( path ) );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( text );
    }

    u64 Entity( const char *className, math::vec3d_t at )
    {
        u64 id = 0;
        REQUIRE( MapDocument_AddEntity( workspace.pDocument, StringView_FromCString( workspace.pDocument->layers.pData[0].id ), StringView_FromCString( className ), at, &id ) == map_status_t::OK );
        MapWorkspace_DocumentChanged( &workspace );
        return id;
    }

    void Select( std::initializer_list<u64> ids )
    {
        const std::vector<u64> list( ids );
        MapWorkspace_SetSelection( &workspace, list.data(), list.size() );
        QCoreApplication::processEvents();
    }

    void Set( const char *key, const map_property_value_t &value )
    {
        REQUIRE( MapWorkspace_SetEntityProperty( &workspace, StringView_FromCString( key ), KeyValue_Root( value.get() ) ) );
    }

    QWidget *Panel()
    {
        if ( panel == nullptr ) {
            panel.reset( MapProperties_Create( nullptr, &workspace ) );
            panel->resize( 480, 900 );
            panel->show();
            QCoreApplication::processEvents();
        }
        return panel.get();
    }

    QTreeWidget *Tree() { return Panel()->findChild<QTreeWidget *>( QStringLiteral( "MapPropertiesTree" ) ); }

    // A row of the Entity keys group (sections included) by its label.
    QTreeWidgetItem *Row( const char *label )
    {
        QTreeWidgetItem *keys = nullptr;
        for ( int i = 0; i < Tree()->topLevelItemCount(); ++i ) {
            if ( Tree()->topLevelItem( i )->text( 0 ) == QStringLiteral( "Entity keys" ) ) { keys = Tree()->topLevelItem( i ); }
        }
        REQUIRE( keys != nullptr );
        const std::function<QTreeWidgetItem *( QTreeWidgetItem * )> find = [&]( QTreeWidgetItem *parent ) -> QTreeWidgetItem * {
            for ( int i = 0; i < parent->childCount(); ++i ) {
                QTreeWidgetItem *child = parent->child( i );
                if ( child->text( 0 ) == QString::fromUtf8( label ) ) { return child; }
                if ( QTreeWidgetItem *inner = find( child ) ) { return inner; }
            }
            return nullptr;
        };
        return find( keys );
    }

    template <typename Widget> Widget *Editor( const char *label )
    {
        QTreeWidgetItem *row = Row( label );
        REQUIRE( row != nullptr );
        QWidget *cell = Tree()->itemWidget( row, 1 );
        REQUIRE( cell != nullptr );
        if ( auto *widget = qobject_cast<Widget *>( cell ) ) { return widget; }
        auto *widget = cell->findChild<Widget *>();
        REQUIRE( widget != nullptr );
        return widget;
    }

    // Commits are queued: let them run.
    static void Settle()
    {
        for ( int i = 0; i < 3; ++i ) { QCoreApplication::processEvents(); }
    }

    const key_value_t *Value( u64 entity, const char *key )
    {
        return KeyValue_Find( KeyValue_Find( MapEntityEdit_FindEntity( workspace.pDocument, entity ), StringView_FromCString( "properties" ) ), StringView_FromCString( key ) );
    }

    usize Steps() { return EditorHistory_StepCount( &workspace.history ); }

    gui::editor_gui_t gui{};
    map_workspace_t workspace{};
    QTemporaryDir content;
    std::unique_ptr<QWidget> panel;
};

QString Text( const key_value_t *value )
{
    string_view_t text{};
    return KeyValue_GetString( value, &text ) ? QString::fromUtf8( text.pData, static_cast<qsizetype>( text.cchLength ) ) : QString();
}

f64 Number( const key_value_t *value )
{
    f64 number = -1.0;
    CHECK( MapPropertyValue_Number( value, &number ) );
    return number;
}

void Finish( QLineEdit *edit, const QString &text )
{
    edit->setFocus();
    edit->setText( text );
    Q_EMIT edit->textEdited( text );
    Q_EMIT edit->editingFinished();
}

} // namespace

TEST_CASE( "Object Properties lists the class's properties with an editor for each type", "[map][gui][properties][property-editors]" )
{
    session_t s;
    const u64 lamp = s.Entity( "lamp", { 0, 0, 64 } );
    s.Select( { lamp } );
    s.Set( "color", MapPropertyValue_String( QStringLiteral( "#ff0000" ) ) );
    s.Set( "custom_key", MapPropertyValue_String( QStringLiteral( "kept" ) ) );

    // Labels, not keys; the profile's group is a section; hidden keys stay out.
    REQUIRE( s.Row( "Colour" ) != nullptr );
    CHECK( s.Row( "Colour" )->toolTip( 0 ).contains( QStringLiteral( "Key: color" ) ) );
    REQUIRE( s.Row( "Start Disabled" ) != nullptr );
    CHECK( s.Row( "Start Disabled" )->parent()->text( 0 ) == QStringLiteral( "Logic" ) );
    CHECK( s.Row( "secret" ) == nullptr );
    REQUIRE( s.Row( "custom_key" ) != nullptr ); // Not in the profile: listed as written.
    CHECK( s.Row( "custom_key" )->text( 1 ) == QStringLiteral( "kept" ) );

    // Each type's editor shows the value, or the default.
    CHECK( s.Editor<QLineEdit>( "Colour" )->text() == QStringLiteral( "#ff0000" ) );
    CHECK( s.Editor<QLineEdit>( "Intensity" )->text() == QStringLiteral( "300" ) );
    CHECK( s.Editor<QCheckBox>( "Cast Shadows" )->isChecked() );
    CHECK( s.Editor<QComboBox>( "Team" )->currentText() == QStringLiteral( "any" ) );
    CHECK( s.Editor<QPushButton>( "Flags" )->text() == QStringLiteral( "silent" ) );
    CHECK( s.Tree()->itemWidget( s.Row( "Aim" ), 1 )->objectName() == QStringLiteral( "MapPropertyAngles" ) );
    CHECK( s.Editor<QComboBox>( "Sound" )->isEditable() );
    CHECK( s.Editor<QComboBox>( "Follow" )->isEditable() );
    CHECK( s.Tree()->itemWidget( s.Row( "class" ), 1 )->objectName() == QStringLiteral( "MapPropertyClass" ) );

    // Unwritten values are greyed and italic; written ones are not.
    CHECK( s.Row( "Intensity" )->font( 0 ).italic() );
    CHECK_FALSE( s.Row( "Colour" )->font( 0 ).italic() );
    // The text view still reads every value.
    const QString text = MapProperties_Text( s.Panel() );
    CHECK( text.contains( QStringLiteral( "Colour = #ff0000" ) ) );
    CHECK( text.contains( QStringLiteral( "Intensity = 300" ) ) );
}

TEST_CASE( "Each property editor writes one undo step with the property's type", "[map][gui][properties][property-editors]" )
{
    session_t s;
    const u64 lamp = s.Entity( "lamp", { 0, 0, 64 } );
    s.Select( { lamp } );
    s.Panel();
    usize steps = s.Steps();

    // A checkbox: the default was true, so unticking writes false.
    s.Editor<QCheckBox>( "Cast Shadows" )->click();
    s.Settle();
    bool_t shadows = CY_TRUE;
    REQUIRE( KeyValue_GetBool( s.Value( lamp, "cast_shadows" ), &shadows ) );
    CHECK( shadows == CY_FALSE );
    CHECK( s.Steps() == ++steps );
    CHECK_FALSE( s.Row( "Cast Shadows" )->font( 0 ).italic() ); // Written now.

    // A real, limited to its maximum.
    Finish( s.Editor<QLineEdit>( "Intensity" ), QStringLiteral( "250.5" ) );
    s.Settle();
    CHECK( KeyValue_Type( s.Value( lamp, "intensity" ) ) == key_value_type_t::F64 );
    CHECK( Number( s.Value( lamp, "intensity" ) ) == 250.5 );
    CHECK( s.Steps() == ++steps );
    Finish( s.Editor<QLineEdit>( "Intensity" ), QStringLiteral( "5000" ) );
    s.Settle();
    CHECK( Number( s.Value( lamp, "intensity" ) ) == 1000.0 );
    CHECK( s.Steps() == ++steps );

    // An integer stays an integer.
    Finish( s.Editor<QLineEdit>( "Count" ), QStringLiteral( "3" ) );
    s.Settle();
    CHECK( KeyValue_Type( s.Value( lamp, "count" ) ) == key_value_type_t::I64 );
    CHECK( Number( s.Value( lamp, "count" ) ) == 3.0 );
    CHECK( s.Steps() == ++steps );

    // Leaving a field without changing it writes nothing.
    Q_EMIT s.Editor<QLineEdit>( "Count" )->editingFinished();
    s.Settle();
    CHECK( s.Steps() == steps );

    // A dropdown.
    auto *team = s.Editor<QComboBox>( "Team" );
    const int blue = team->findText( QStringLiteral( "blue" ) );
    REQUIRE( blue >= 0 );
    team->setCurrentIndex( blue );
    Q_EMIT team->activated( blue );
    s.Settle();
    CHECK( Text( s.Value( lamp, "team" ) ) == QStringLiteral( "blue" ) );
    CHECK( s.Steps() == ++steps );

    // A colour, normalised to #rrggbb.
    Finish( s.Editor<QLineEdit>( "Colour" ), QStringLiteral( "00FF80" ) );
    s.Settle();
    CHECK( Text( s.Value( lamp, "color" ) ) == QStringLiteral( "#00ff80" ) );
    CHECK( s.Steps() == ++steps );

    // Angles: three numbers.
    Finish( s.Tree()->itemWidget( s.Row( "Aim" ), 1 )->findChild<QLineEdit *>( QStringLiteral( "MapPropertyAxis1" ) ), QStringLiteral( "45" ) );
    s.Settle();
    const key_value_t *aim = s.Value( lamp, "aim" );
    REQUIRE( KeyValue_ChildCount( aim ) == 3u );
    CHECK( Number( KeyValue_ChildAt( aim, 0 ) ) == 0.0 );
    CHECK( Number( KeyValue_ChildAt( aim, 1 ) ) == 45.0 );
    CHECK( s.Steps() == ++steps );

    // An asset picker lists the content files of its type.
    auto *sound = s.Editor<QComboBox>( "Sound" );
    sound->showPopup();
    sound->hidePopup();
    REQUIRE( sound->findText( QStringLiteral( "sounds/hum.cysnd" ) ) >= 0 );
    Finish( sound->lineEdit(), QStringLiteral( "sounds/hum.cysnd" ) );
    s.Settle();
    CHECK( Text( s.Value( lamp, "sound" ) ) == QStringLiteral( "sounds/hum.cysnd" ) );
    CHECK( s.Steps() == ++steps );

    // Undo takes the last change back, and the editor follows.
    REQUIRE( MapWorkspace_Undo( &s.workspace ) == editor_history_status_t::OK );
    QCoreApplication::processEvents();
    CHECK( s.Value( lamp, "sound" ) == nullptr );
    CHECK( s.Editor<QComboBox>( "Sound" )->currentText().isEmpty() );
}

TEST_CASE( "Removing a profile key returns it to the default; it cannot be renamed", "[map][gui][properties][property-editors]" )
{
    session_t s;
    const u64 lamp = s.Entity( "lamp", { 0, 0, 64 } );
    s.Select( { lamp } );
    s.Set( "intensity", MapPropertyValue_Real( 42.0 ) );
    auto *remove = s.Panel()->findChild<QAbstractButton *>( QStringLiteral( "MapEntityPropertyRemove" ) );
    auto *rename = s.Panel()->findChild<QAbstractButton *>( QStringLiteral( "MapEntityPropertyRename" ) );
    REQUIRE( remove != nullptr );
    REQUIRE( rename != nullptr );
    s.Tree()->setCurrentItem( s.Row( "Count" ) ); // Not written.
    CHECK_FALSE( remove->isEnabled() );
    CHECK_FALSE( rename->isEnabled() );
    s.Tree()->setCurrentItem( s.Row( "Intensity" ) );
    CHECK( remove->isEnabled() );
    CHECK_FALSE( rename->isEnabled() );
    remove->click();
    QCoreApplication::processEvents();
    CHECK( s.Value( lamp, "intensity" ) == nullptr );
    CHECK( s.Editor<QLineEdit>( "Intensity" )->text() == QStringLiteral( "300" ) ); // The default again.
}

TEST_CASE( "Property editors show a mixed selection empty and write one value to all", "[map][gui][properties][property-editors]" )
{
    session_t s;
    const u64 a = s.Entity( "lamp", { 0, 0, 64 } );
    const u64 b = s.Entity( "lamp", { 128, 0, 64 } );
    s.Select( { a } );
    s.Set( "intensity", MapPropertyValue_Real( 10.0 ) );
    s.Select( { b } );
    s.Set( "intensity", MapPropertyValue_Real( 20.0 ) );
    s.Select( { a, b } );
    auto *intensity = s.Editor<QLineEdit>( "Intensity" );
    CHECK( intensity->text().isEmpty() );
    CHECK( intensity->placeholderText() == QStringLiteral( "Multiple values" ) );
    CHECK( s.Editor<QCheckBox>( "Cast Shadows" )->checkState() == Qt::Checked ); // Neither has it: both use the default.
    const usize steps = s.Steps();
    Q_EMIT intensity->editingFinished(); // Left without typing: nothing written.
    s.Settle();
    CHECK( s.Steps() == steps );
    Finish( s.Editor<QLineEdit>( "Intensity" ), QStringLiteral( "15" ) );
    s.Settle();
    CHECK( Number( s.Value( a, "intensity" ) ) == 15.0 );
    CHECK( Number( s.Value( b, "intensity" ) ) == 15.0 );
    CHECK( s.Steps() == steps + 1u );
}

TEST_CASE( "The class dropdown offers the profile's classes and changes the class", "[map][gui][properties][property-editors]" )
{
    session_t s;
    const u64 lamp = s.Entity( "lamp", { 0, 0, 64 } );
    s.Select( { lamp } );
    auto *classes = qobject_cast<QComboBox *>( s.Tree()->itemWidget( s.Row( "class" ), 1 ) );
    REQUIRE( classes != nullptr );
    CHECK( classes->currentText() == QStringLiteral( "lamp" ) );
    classes->showPopup();
    classes->hidePopup();
    CHECK( classes->findText( QStringLiteral( "beacon" ) ) >= 0 );
    CHECK( classes->findText( QStringLiteral( "targetable" ) ) < 0 ); // Abstract: never placed.
    Finish( classes->lineEdit(), QStringLiteral( "beacon" ) );
    s.Settle();
    string_view_t className{};
    REQUIRE( KeyValue_GetString( KeyValue_Find( MapEntityEdit_FindEntity( s.workspace.pDocument, lamp ), StringView_FromCString( "class" ) ), &className ) );
    CHECK( std::string( className.pData, className.cchLength ) == "beacon" );
    // The rows follow the new class.
    CHECK( s.Row( "range" ) != nullptr );
    CHECK( s.Row( "Intensity" ) == nullptr );
}

TEST_CASE( "Entities take their box, colour and helpers from the game profile", "[map][gui][entity-look]" )
{
    session_t s;
    const u64 marker = s.Entity( "marker", { 100, 0, 0 } );
    const u64 lamp = s.Entity( "lamp", { 0, 0, 64 } );
    const u64 goal = s.Entity( "beacon", { 300, 0, 0 } );
    s.Select( { goal } );
    REQUIRE( MapWorkspace_SetEntityIdentityField( &s.workspace, StringView_FromCString( "name" ), StringView_FromCString( "goal" ) ) );
    s.Select( { marker } );
    s.Set( "follow", MapPropertyValue_String( QStringLiteral( "goal" ) ) );
    s.Select( { lamp } );
    s.Set( "color", MapPropertyValue_String( QStringLiteral( "#ff0000" ) ) );

    // The class box, placed on the origin; a class without one keeps the cube.
    const map_wire_object_t *box = MapWireframe_FindObject( s.workspace.wire, marker );
    REQUIRE( box != nullptr );
    CHECK( box->bounds.box.minimum.x == 84.0 );
    CHECK( box->bounds.box.minimum.z == 0.0 );
    CHECK( box->bounds.box.maximum.y == 16.0 );
    CHECK( box->bounds.box.maximum.z == 72.0 );
    const map_wire_object_t *cube = MapWireframe_FindObject( s.workspace.wire, goal );
    REQUIRE( cube != nullptr );
    CHECK( cube->bounds.box.maximum.x - cube->bounds.box.minimum.x == 2.0 * MAP_WIRE_POINT_ENTITY_HALF );
    // Picking follows the box: a click at its top hits the marker.
    CHECK( box->bounds.box.maximum.z > 64.0 );

    // Colours: the class colour, a colour property, none.
    CHECK( MapEntityLook_Colour( &s.workspace, marker ) == QColor( QStringLiteral( "#40c040" ) ) );
    CHECK( MapEntityLook_Colour( &s.workspace, lamp ) == QColor( QStringLiteral( "#ff0000" ) ) );
    CHECK_FALSE( MapEntityLook_Colour( &s.workspace, goal ).isValid() );

    // Helpers: an arrow (3 lines) along +X, a radius (3 circles), a line to "goal".
    std::vector<map_helper_line_t> lines;
    MapEntityLook_Helpers( &s.workspace, marker, lines );
    REQUIRE( lines.size() == 3u + 3u * 48u + 1u );
    CHECK( lines[0].a.x == 100.0 );
    CHECK( lines[0].b.x == Catch::Approx( 148.0 ) ); // 1.5 x the 32-unit box.
    CHECK( lines[0].b.y == Catch::Approx( 0.0 ) );
    CHECK( lines[3].a.x == Catch::Approx( 200.0 ) ); // The radius circle starts range (100) along +X.
    CHECK( lines.back().b.x == 300.0 );

    // A door's travel: its brush's depth along the direction (+Y), less the lip.
    map_bounds_t slab{}; MapBounds_AddPoint( slab, { 0, 200, 0 } ); MapBounds_AddPoint( slab, { 128, 216, 96 } );
    REQUIRE( MapWorkspace_CreateBox( &s.workspace, slab ) );
    const u64 brush = EditorSelection_At( &s.workspace.selection, 0 );
    u64 door = 0;
    REQUIRE( MapCreate_TieToEntity( s.workspace.pDocument, { &brush, 1 }, 0, StringView_FromCString( "door" ), &door ) == map_status_t::OK );
    MapWorkspace_DocumentChanged( &s.workspace );
    lines.clear();
    MapEntityLook_Helpers( &s.workspace, door, lines );
    REQUIRE( lines.size() == 3u + 12u );
    CHECK( lines[0].b.y - lines[0].a.y == Catch::Approx( 16.0 - 8.0 ) );
}

TEST_CASE( "Reloading the game profile resizes entity boxes", "[map][gui][entity-look]" )
{
    session_t s;
    const u64 marker = s.Entity( "marker", { 0, 0, 0 } );
    CHECK( MapWireframe_FindObject( s.workspace.wire, marker )->bounds.box.maximum.z == 72.0 );
    // The class loses its size: the default cube comes back.
    std::string edited( kProfile );
    const std::string size = "size = [ -16, -16, 0, 16, 16, 72 ] ";
    edited.erase( edited.find( size ), size.size() );
    s.Write( QStringLiteral( "game/reap.cygame" ), edited.c_str() );
    REQUIRE( MapGame_Reload( &s.workspace ) == map_status_t::OK );
    MapEntityLook_Refresh( &s.workspace );
    CHECK( MapWireframe_FindObject( s.workspace.wire, marker )->bounds.box.maximum.z == MAP_WIRE_POINT_ENTITY_HALF );
}
