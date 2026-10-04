//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Views_Tests.cpp
//  Purpose: Contract tests for the Map views and panels, on the offscreen
//           platform: framing, drawing, picking, zooming, the outliner, and
//           the properties panel.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Panels.h"
#include "CypherMapGui_Input.h"
#include "CypherMapGui_ToolPanels.h"
#include "CypherMapGui_Views.h"
#include "CypherEditorGui_Style.h"
#include "CypherGeometry_RaycastQueries.h"
#include "CypherEditor/Geometry/Document/CypherGeometry_DocumentBrushAttributes.h"
#include "CypherEditor/Geometry/Document/CypherGeometry_DocumentMeshes.h"
#include "CypherEditor_Keymap.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDir>
#include <QEnterEvent>
#include <QEventLoop>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QLabel>
#include <QMouseEvent>
#include <QMenu>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPolygonF>
#include <QSet>
#include <QSplitter>
#include <QToolButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QWheelEvent>
#include <QWidget>

#include <cmath>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <limits>
#include <utility>
#include <vector>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::map;

namespace
{

QApplication *App()
{
    return qobject_cast<QApplication *>( QCoreApplication::instance() );
}

// A session with the documented example map open.
struct session_t {
    session_t()
    {
        REQUIRE( gui::EditorGui_Init( &gui, App(), Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
        REQUIRE( MapWorkspace_Init( &workspace, &gui ) );
        const QString root = QString::fromStdString( ( std::filesystem::path( CYPHER_MAP_EXAMPLE_DIR ) / "facility.cymap" ).string() );
        REQUIRE( MapWorkspace_Open( &workspace, root ).status == map_files_status_t::OK );
    }
    gui::editor_gui_t gui{};
    map_workspace_t workspace{};
};

// Shows a widget at a fixed size and lets pending layout and paint run.
void ShowAt( QWidget *pWidget, int width, int height )
{
    pWidget->resize( width, height );
    pWidget->show();
    QCoreApplication::processEvents();
}

int DistinctColors( QWidget *pWidget )
{
    const QImage image = pWidget->grab().toImage();
    QSet<QRgb> colors;
    for ( int y = 0; y < image.height(); y += 3 ) {
        for ( int x = 0; x < image.width(); x += 3 ) { colors.insert( image.pixel( x, y ) ); }
    }
    return static_cast<int>( colors.size() );
}

void Click( QWidget *pWidget, QPointF position, Qt::KeyboardModifiers modifiers = Qt::NoModifier )
{
    QMouseEvent press( QEvent::MouseButtonPress, position, pWidget->mapToGlobal( position ), Qt::LeftButton, Qt::LeftButton, modifiers );
    QCoreApplication::sendEvent( pWidget, &press );
    QMouseEvent release( QEvent::MouseButtonRelease, position, pWidget->mapToGlobal( position ), Qt::LeftButton, Qt::NoButton, modifiers );
    QCoreApplication::sendEvent( pWidget, &release );
}

void DragMouse( QWidget *widget, QEvent::Type type, QPointF position, Qt::KeyboardModifiers modifiers = Qt::NoModifier )
{
    const Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    const Qt::MouseButtons held = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent event( type, position, widget->mapToGlobal( position ), button, held, modifiers );
    QCoreApplication::sendEvent( widget, &event );
}

void DragButton( QWidget *widget, QEvent::Type type, QPointF position, Qt::MouseButton button, Qt::KeyboardModifiers modifiers = Qt::NoModifier )
{
    QMouseEvent event( type, position, widget->mapToGlobal( position ), type == QEvent::MouseMove ? Qt::NoButton : button,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::MouseButtons( button ), modifiers );
    QCoreApplication::sendEvent( widget, &event );
}

f64 TestDistance( math::vec3d_t a, math::vec3d_t b )
{
    return std::sqrt( ( a.x - b.x ) * ( a.x - b.x ) + ( a.y - b.y ) * ( a.y - b.y ) + ( a.z - b.z ) * ( a.z - b.z ) );
}

void DepthWheel( QWidget *widget, QPointF position, int angle, Qt::KeyboardModifiers modifiers = Qt::ShiftModifier )
{
    QWheelEvent event( position, widget->mapToGlobal( position ), QPoint(), QPoint( 0, angle ), Qt::LeftButton, modifiers,
                       Qt::NoScrollPhase, false );
    QCoreApplication::sendEvent( widget, &event );
}

f64 TestCoordinate( math::vec3d_t point, u32 axis )
{
    return axis == 0 ? point.x : axis == 1 ? point.y : point.z;
}
void SetTestCoordinate( math::vec3d_t &point, u32 axis, f64 value )
{
    if ( axis == 0 ) { point.x = value; } else if ( axis == 1 ) { point.y = value; } else { point.z = value; }
}
void CheckPointClose( math::vec3d_t actual, math::vec3d_t expected )
{
    CHECK( std::abs( actual.x - expected.x ) < 1e-6 );
    CHECK( std::abs( actual.y - expected.y ) < 1e-6 );
    CHECK( std::abs( actual.z - expected.z ) < 1e-6 );
}

u64 BrushSideInDirection( const map_workspace_t &ws, u64 object, u32 axis, int sign );

f64 CameraGizmoLength( QWidget *camera, math::vec3d_t pivot, const settings_registry_t &settings )
{
    const auto position = MapCameraView_Position( camera ), forward = MapCameraView_Forward( camera );
    const f64 depth = ( pivot.x - position.x ) * forward.x + ( pivot.y - position.y ) * forward.y + ( pivot.z - position.z ) * forward.z;
    const f64 fov = EditorSettings_Real( &settings, "editor.camera.fov", 75.0 );
    const f64 focal = camera->width() * 0.5 / std::tan( fov * 3.14159265358979323846 / 360.0 );
    return std::max( depth, 1.0 ) * 72.0 * EditorSettings_Real( &settings, "editor.viewport.gizmo_scale", 1.0 ) / focal;
}

// Use the ordinary look gesture to face +X, keeping the framed position.
// Tests can then place an authored edge across the near plane without a
// test-only camera setter or relying on the initial framing distance.
void FacePositiveX( QWidget *camera )
{
    const auto forward = MapCameraView_Forward( camera );
    constexpr f64 radiansToDegrees = 180.0 / 3.14159265358979323846;
    const QPointF start( 400, 300 );
    const QPointF end = start + QPointF( std::atan2( forward.y, forward.x ) * radiansToDegrees,
                                         std::asin( forward.z ) * radiansToDegrees );
    QMouseEvent press( QEvent::MouseButtonPress, start, camera->mapToGlobal( start ), Qt::RightButton, Qt::RightButton, Qt::NoModifier );
    QCoreApplication::sendEvent( camera, &press );
    QMouseEvent move( QEvent::MouseMove, end, camera->mapToGlobal( end ), Qt::NoButton, Qt::RightButton, Qt::NoModifier );
    QCoreApplication::sendEvent( camera, &move );
    QMouseEvent release( QEvent::MouseButtonRelease, end, camera->mapToGlobal( end ), Qt::RightButton, Qt::NoButton, Qt::NoModifier );
    QCoreApplication::sendEvent( camera, &release );
    CHECK( std::abs( MapCameraView_Forward( camera ).x - 1.0 ) < 1e-10 );
    CHECK( std::abs( MapCameraView_Forward( camera ).y ) < 1e-10 );
    CHECK( std::abs( MapCameraView_Forward( camera ).z ) < 1e-10 );
}

int ChangedPixelsNear( const QImage &before, const QImage &after, QPointF logicalPoint, int logicalRadius = 3 )
{
    REQUIRE( before.size() == after.size() );
    const qreal ratio = after.devicePixelRatio();
    const QPoint center( qRound( logicalPoint.x() * ratio ), qRound( logicalPoint.y() * ratio ) );
    const int radius = static_cast<int>( std::ceil( logicalRadius * ratio ) );
    const QRect region = QRect( center - QPoint( radius, radius ), QSize( radius * 2 + 1, radius * 2 + 1 ) ).intersected( after.rect() );
    int changed = 0;
    for ( int y = region.top(); y <= region.bottom(); ++y ) {
        for ( int x = region.left(); x <= region.right(); ++x ) { changed += before.pixel( x, y ) != after.pixel( x, y ) ? 1 : 0; }
    }
    return changed;
}

std::vector<math::vec3d_t> ObjectLineVertices( const map_wireframe_t &wire, u64 id )
{
    const auto *object = MapWireframe_FindObject( wire, id ); REQUIRE( object != nullptr );
    std::vector<math::vec3d_t> result;
    for ( u32 i = 0; i < object->nLines; ++i ) {
        const auto &edge = wire.lines.pData[object->iFirstLine + i];
        for ( const u32 index : { edge.iA, edge.iB } ) {
            const auto point = wire.points.pData[index];
            if ( std::none_of( result.begin(), result.end(), [&]( math::vec3d_t other ) { return TestDistance( point, other ) < 1e-6; } ) ) { result.push_back( point ); }
        }
    }
    return result;
}

map_bounds_t ObjectWireBounds( const map_wireframe_t &wire, u64 id )
{
    const auto *object = MapWireframe_FindObject( wire, id ); REQUIRE( object != nullptr );
    return object->bounds;
}

void CheckObjectVertices( const map_wireframe_t &wire, u64 id, const std::vector<math::vec3d_t> &expected )
{
    const auto actual = ObjectLineVertices( wire, id ); REQUIRE( actual.size() == expected.size() );
    for ( const auto point : expected ) {
        CHECK( std::any_of( actual.begin(), actual.end(), [&]( math::vec3d_t other ) { return TestDistance( point, other ) < 1e-5; } ) );
    }
}

void DoubleClick( QWidget *widget, QPointF position )
{
    QMouseEvent event( QEvent::MouseButtonDblClick, position, widget->mapToGlobal( position ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( widget, &event );
}

void OnlyVisible( map_workspace_t &workspace, std::initializer_list<u64> ids )
{
    ( void )EditorSelection_Clear( &workspace.hidden );
    for ( usize i = 0; i < workspace.wire.objects.nCount; ++i ) {
        const u64 id = workspace.wire.objects.pData[i].id;
        if ( std::find( ids.begin(), ids.end(), id ) == ids.end() ) { ( void )EditorSelection_Apply( &workspace.hidden, id, EDITOR_SELECT_ADD ); }
    }
    MapWorkspace_Notify( &workspace, MAP_CHANGE_VIEW );
}

bool OpenMenuWithLeftClick( QToolButton *button )
{
    bool opened = false;
    QTimer::singleShot( 0, button, [&]() {
        opened = button->menu()->isVisible();
        button->menu()->hide();
    } );
    Click( button, button->rect().center() );
    QCoreApplication::processEvents();
    return opened;
}

void Enter( QWidget *pWidget )
{
    const QPointF local( 30.0, 40.0 );
    QEnterEvent enter( local, pWidget->mapTo( pWidget->window(), local ), pWidget->mapToGlobal( local ) );
    QCoreApplication::sendEvent( pWidget, &enter );
}

void Wheel( QWidget *pWidget )
{
    const QPointF local( 100.0, 100.0 );
    QWheelEvent wheel( local, pWidget->mapToGlobal( local ), QPoint(), QPoint( 0, 240 ), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false );
    QCoreApplication::sendEvent( pWidget, &wheel );
}

struct view_settings_t {
    explicit view_settings_t( settings_registry_t *pRegistry ) : registry( pRegistry )
    {
        REQUIRE( SettingsDocument_Init( &store, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
        EditorSettings_SetScope( registry, settings_scope_t::USER, &store );
    }
    ~view_settings_t() { EditorSettings_SetScope( registry, settings_scope_t::USER, nullptr ); }
    void Set( const char *path, bool enabled )
    {
        const setting_descriptor_t *descriptor = EditorSettings_Find( registry, StringView_FromCString( path ) );
        REQUIRE( descriptor != nullptr );
        setting_value_t value{};
        value.type = setting_type_t::BOOL;
        value.bValue = enabled;
        REQUIRE( EditorSettings_Write( registry, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
        QCoreApplication::processEvents();
    }
    void Choice( const char *path, const char *text )
    {
        const setting_descriptor_t *descriptor = EditorSettings_Find( registry, StringView_FromCString( path ) );
        REQUIRE( descriptor != nullptr );
        setting_value_t value{};
        value.type = descriptor->type;
        value.text = StringView_FromCString( text );
        REQUIRE( EditorSettings_Write( registry, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
        QCoreApplication::processEvents();
    }
    void Real( const char *path, f64 number )
    {
        const setting_descriptor_t *descriptor = EditorSettings_Find( registry, StringView_FromCString( path ) );
        REQUIRE( descriptor != nullptr );
        setting_value_t value{};
        value.type = setting_type_t::REAL;
        value.flValue = number;
        REQUIRE( EditorSettings_Write( registry, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
        QCoreApplication::processEvents();
    }
    void Integer( const char *path, i64 number )
    {
        const setting_descriptor_t *descriptor = EditorSettings_Find( registry, StringView_FromCString( path ) );
        REQUIRE( descriptor != nullptr );
        setting_value_t value{};
        value.type = setting_type_t::INTEGER;
        value.nValue = number;
        REQUIRE( EditorSettings_Write( registry, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
        QCoreApplication::processEvents();
    }
    settings_registry_t *registry;
    settings_document_t store{};
};

int ChangedPixelCount( const QImage &before, const QImage &after )
{
    REQUIRE( before.size() == after.size() );
    int changed = 0;
    for ( int y = 0; y < before.height(); ++y ) {
        for ( int x = 0; x < before.width(); ++x ) { changed += before.pixel( x, y ) != after.pixel( x, y ) ? 1 : 0; }
    }
    return changed;
}

// Isolate pixels changed by the actual name drawing policy. Geometry,
// selection controls and grids remain in both captures; no label geometry
// or private renderer state is manufactured by the test.
QImage EntityNamePixels( QWidget *view, view_settings_t &settings, bool perspective, const char *mode = "always", QImage *pGeometryOut = nullptr )
{
    const char *path = perspective ? "editor.viewport.perspective.entity_names" : "editor.viewport.entity_names";
    settings.Choice( path, "never" );
    const QImage plain = view->grab().toImage();
    if ( pGeometryOut != nullptr ) { *pGeometryOut = plain; }
    settings.Choice( path, mode );
    const QImage named = view->grab().toImage();
    REQUIRE( plain.size() == named.size() );
    QImage result( named.size(), QImage::Format_ARGB32 );
    result.setDevicePixelRatio( named.devicePixelRatio() );
    result.fill( Qt::transparent );
    for ( int y = 0; y < named.height(); ++y ) {
        for ( int x = 0; x < named.width(); ++x ) {
            if ( plain.pixel( x, y ) != named.pixel( x, y ) ) { result.setPixel( x, y, named.pixel( x, y ) ); }
        }
    }
    return result;
}

QRect PaintedNameBounds( const QImage &image )
{
    QRect bounds;
    for ( int y = 0; y < image.height(); ++y ) {
        for ( int x = 0; x < image.width(); ++x ) {
            if ( qAlpha( image.pixel( x, y ) ) != 0 ) { bounds = bounds.united( QRect( x, y, 1, 1 ) ); }
        }
    }
    return bounds;
}

u64 AddNamedViewEntity( map_workspace_t &workspace, math::vec3d_t origin, const char *name )
{
    u64 id = 0;
    // session_t opens facility.cymap, whose authored entity layer is gameplay.
    REQUIRE( MapDocument_AddEntity( workspace.pDocument, StringView_FromCString( "gameplay" ),
        StringView_FromCString( "info_player_start" ), origin, &id ) == map_status_t::OK );
    MapWorkspace_DocumentChanged( &workspace );
    MapWorkspace_Select( &workspace, id, MAP_SELECT_REPLACE );
    REQUIRE( MapWorkspace_SetEntityIdentityField( &workspace, StringView_FromCString( "name" ), StringView_FromCString( name ) ) );
    return id;
}

void DisableNameTestAids( view_settings_t &settings )
{
    for ( const char *path : { "editor.grid.show", "editor.grid.show_3d", "editor.grid.show_surface_3d",
        "editor.viewport.active_border", "editor.viewport.show_axes", "editor.viewport.show_rulers", "editor.viewport.show_metrics",
        "editor.viewport.show_selection_bounds", "editor.viewport.show_selection_dimensions", "editor.viewport.show_selection_vertices",
        "editor.viewport.perspective.show_axes", "editor.viewport.perspective.show_metrics", "editor.viewport.perspective.show_selection_bounds",
        "editor.viewport.perspective.show_selection_dimensions", "editor.viewport.perspective.show_selection_vertices" } ) { settings.Set( path, false ); }
    settings.Choice( "editor.viewport.io_lines", "never" );
    settings.Choice( "editor.viewport.perspective.io_lines", "never" );
}

void DisableSurfaceGridTestAids( view_settings_t &settings )
{
    settings.Set( "editor.viewport.active_border", false );
    settings.Set( "editor.grid.show_3d", false );
    settings.Set( "editor.viewport.perspective.show_axes", false );
    settings.Set( "editor.viewport.perspective.center_axes", false );
    settings.Set( "editor.viewport.perspective.show_selection_dimensions", false );
    settings.Set( "editor.viewport.perspective.show_selection_bounds", false );
    settings.Set( "editor.viewport.perspective.show_selection_vertices", false );
}

// A known presentation polygon lets raster tests distinguish the exact
// silhouette from its AABB without adding a test-only camera/geometry API.
void AppendSurfaceGridTestFace( map_workspace_t &ws, u64 id, const std::vector<math::vec3d_t> &points )
{
    REQUIRE( points.size() >= 3u );
    auto &wire = ws.wire;
    map_wire_object_t object{};
    object.id = id; object.kind = map_wire_kind_t::MESH;
    object.iFirstPoint = static_cast<u32>( wire.points.nCount );
    object.nPoints = static_cast<u32>( points.size() );
    map_wire_face_t face{};
    face.id = id; face.bTwoSided = CY_TRUE;
    face.iFirstIndex = static_cast<u32>( wire.faceIndices.nCount );
    face.nIndices = object.nPoints;
    const auto a = points[1], b = points[0], c = points[2];
    const math::vec3d_t u{ a.x - b.x, a.y - b.y, a.z - b.z }, v{ c.x - b.x, c.y - b.y, c.z - b.z };
    face.normal = { u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x };
    const f64 length = TestDistance( face.normal, {} ); REQUIRE( length > 0.0 );
    face.normal = { face.normal.x / length, face.normal.y / length, face.normal.z / length };
    for ( const auto point : points ) {
        REQUIRE( Vector_PushBack( &wire.faceIndices, static_cast<u32>( wire.points.nCount ) ) );
        REQUIRE( Vector_PushBack( &wire.points, point ) );
        REQUIRE( Vector_PushBack( &wire.pointSourceIds, u64{ 0 } ) );
        MapBounds_AddPoint( object.bounds, point );
    }
    REQUIRE( Vector_PushBack( &wire.objects, object ) );
    REQUIRE( Vector_PushBack( &wire.faces, face ) );
    MapBounds_AddBounds( wire.bounds, object.bounds );
    MapWorkspace_Notify( &ws, MAP_CHANGE_VIEW );
}

} // namespace

TEST_CASE( "The view tab lists only views on a left click; options are the right-click menu, in 3D too", "[map][gui][views][view-menus]" )
{
    session_t session;
    view_settings_t settings( &session.gui.settings );
    std::unique_ptr<QWidget> views( MapViews_Create( nullptr, &session.workspace ) );
    ShowAt( views.get(), 1200, 900 );
    // Hammer's view tab: the view's name; its menu holds views and nothing else.
    auto *tab = views->findChild<QToolButton *>( QStringLiteral( "EditorViewChoose1" ) );
    REQUIRE( tab != nullptr );
    REQUIRE( tab->menu() != nullptr );
    CHECK( tab->menu()->objectName() == QStringLiteral( "EditorViewTypeMenu1" ) );
    CHECK( tab->arrowType() == Qt::DownArrow );
    CHECK( tab->text() == QStringLiteral( "Top" ) );
    CHECK( tab->parentWidget()->findChildren<QToolButton *>().size() == 1 );
    int nViews = 0;
    for ( QAction *action : tab->menu()->actions() ) {
        if ( action->isSeparator() ) { continue; }
        ++nViews;
        CHECK( action->isCheckable() );
        CHECK( action->menu() == nullptr ); // No submenus, no drawing toggles.
        const QString name = action->objectName();
        CHECK( ( name.startsWith( QStringLiteral( "EditorViewProjection1_" ) ) || name.startsWith( QStringLiteral( "EditorViewRender1_" ) ) ||
                 name.startsWith( QStringLiteral( "EditorViewContent1_" ) ) ) );
    }
    CHECK( nViews == 10 ); // Three 2D projections, four 3D modes, three editor panels.
    QWidget *top = MapViews_PaneView( views.get(), 1 );
    QWidget *camera = MapViews_PaneView( views.get(), 0 );
    Wheel( top );
    Wheel( camera );
    MapWorkspace_Select( &session.workspace, 1000u, MAP_SELECT_REPLACE );
    const QPointF topBefore = MapOrthoView_ViewToWorld( top, QPointF() );
    const math::vec3d_t cameraBefore = MapCameraView_Position( camera );
    const auto menus = views->findChildren<QMenu *>().size();
    for ( int i = 0; i < 3; ++i ) { CHECK( OpenMenuWithLeftClick( tab ) ); }
    CHECK( views->findChildren<QMenu *>().size() == menus );
    CHECK( MapViews_PaneView( views.get(), 1 ) == top );
    CHECK( MapOrthoView_ViewToWorld( top, QPointF() ) == topBefore );
    CHECK( MapCameraView_Position( camera ).x == cameraBefore.x );
    CHECK( MapCameraView_Position( camera ).z == cameraBefore.z );
    CHECK( MapWorkspace_IsSelected( &session.workspace, 1000u ) );
    // Right-click on the tab opens the options menu.
    auto *topOptions = views->findChild<QMenu *>( QStringLiteral( "EditorViewOptionsMenu1" ) );
    REQUIRE( topOptions != nullptr );
    QWidget *header = tab->parentWidget();
    const QPoint point( 5, 5 );
    QContextMenuEvent context( QContextMenuEvent::Mouse, point, header->mapToGlobal( point ) );
    QCoreApplication::sendEvent( header, &context );
    QCoreApplication::processEvents();
    CHECK( topOptions->isVisible() );
    CHECK_FALSE( tab->menu()->isVisible() );
    topOptions->hide();
    // 3D: a right drag looks around; a right click without dragging opens the options.
    auto *cameraOptions = views->findChild<QMenu *>( QStringLiteral( "EditorViewOptionsMenu0" ) );
    REQUIRE( cameraOptions != nullptr );
    const QPointF centre( camera->width() / 2.0, camera->height() / 2.0 );
    const auto rightButton = [&]( QEvent::Type type, QPointF at, Qt::MouseButtons buttons ) {
        QMouseEvent event( type, at, camera->mapToGlobal( at ), type == QEvent::MouseMove ? Qt::NoButton : Qt::RightButton, buttons, Qt::NoModifier );
        QCoreApplication::sendEvent( camera, &event );
    };
    rightButton( QEvent::MouseButtonPress, centre, Qt::RightButton );
    rightButton( QEvent::MouseMove, centre + QPointF( 40.0, 0.0 ), Qt::RightButton );
    rightButton( QEvent::MouseButtonRelease, centre + QPointF( 40.0, 0.0 ), Qt::NoButton );
    QCoreApplication::processEvents();
    CHECK_FALSE( cameraOptions->isVisible() );
    rightButton( QEvent::MouseButtonPress, centre, Qt::RightButton );
    rightButton( QEvent::MouseButtonRelease, centre, Qt::NoButton );
    QCoreApplication::processEvents();
    CHECK( cameraOptions->isVisible() );
    cameraOptions->hide();
    // The window system's own right-click event stays camera look.
    QContextMenuEvent mouseContext( QContextMenuEvent::Mouse, point, camera->mapToGlobal( point ) );
    QCoreApplication::sendEvent( camera, &mouseContext );
    CHECK_FALSE( cameraOptions->isVisible() );
    QContextMenuEvent keyboardContext( QContextMenuEvent::Keyboard, point, camera->mapToGlobal( point ) );
    QCoreApplication::sendEvent( camera, &keyboardContext );
    CHECK( cameraOptions->isVisible() );
    cameraOptions->hide();
}

TEST_CASE( "An unbound camera look still offers a plain right click menu without owning right drags", "[map][gui][views][camera][view-menus][navigation-gestures]" )
{
    session_t session; auto &ws = session.workspace;
    settings_document_t keys{};
    REQUIRE( SettingsDocument_Init( &keys, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &keys, StringView_FromCString( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "unbound_camera_look" mouse = { "map.viewport.3d" = {
  "map.camera.look" = []
  "map.camera.dolly" = [ "Alt+RightDrag" ]
} } }
)cykv" ) ).status == settings_document_status_t::OK );
    session.gui.keymapChain[0] = SettingsDocument_Root( &keys ); session.gui.nKeymapChain = 1u;
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
    // Intercept the view's posted request so this standalone view can test
    // click semantics without opening a popup or entering its event loop.
    struct context_requests_t : QObject {
        int count = 0;
        bool eventFilter( QObject *, QEvent *event ) override {
            if ( event->type() != QEvent::ContextMenu ) { return false; }
            if ( static_cast<QContextMenuEvent *>( event )->reason() == QContextMenuEvent::Other ) { ++count; }
            return true;
        }
    } requests;
    camera->installEventFilter( &requests );
    const auto deliver = [&]() { QCoreApplication::sendPostedEvents( camera.get(), QEvent::ContextMenu ); QCoreApplication::processEvents(); };
    const QPointF start( 400, 300 );
    const auto position = MapCameraView_Position( camera.get() ), forward = MapCameraView_Forward( camera.get() );
    const auto *document = ws.pDocument; const auto revision = document->geometry.revision, selectionRevision = ws.selection.revision;
    const usize steps = EditorHistory_StepCount( &ws.history );
    DragButton( camera.get(), QEvent::MouseButtonPress, start, Qt::RightButton ); deliver(); CHECK( requests.count == 0 );
    DragButton( camera.get(), QEvent::MouseMove, start + QPointF( 2, 0 ), Qt::RightButton );
    DragButton( camera.get(), QEvent::MouseButtonRelease, start + QPointF( 2, 0 ), Qt::RightButton ); deliver();
    CHECK( requests.count == 1 ); CheckPointClose( MapCameraView_Position( camera.get() ), position ); CheckPointClose( MapCameraView_Forward( camera.get() ), forward );
    DragButton( camera.get(), QEvent::MouseButtonPress, start, Qt::RightButton );
    DragButton( camera.get(), QEvent::MouseMove, start + QPointF( 40, 0 ), Qt::RightButton );
    DragButton( camera.get(), QEvent::MouseButtonRelease, start + QPointF( 40, 0 ), Qt::RightButton ); deliver();
    CHECK( requests.count == 1 ); CheckPointClose( MapCameraView_Position( camera.get() ), position ); CheckPointClose( MapCameraView_Forward( camera.get() ), forward );
    DragButton( camera.get(), QEvent::MouseButtonPress, start, Qt::RightButton, Qt::AltModifier );
    DragButton( camera.get(), QEvent::MouseMove, start + QPointF( 0, 30 ), Qt::RightButton, Qt::AltModifier );
    DragButton( camera.get(), QEvent::MouseButtonRelease, start + QPointF( 0, 30 ), Qt::RightButton, Qt::AltModifier ); deliver();
    CHECK( requests.count == 1 ); CHECK( TestDistance( MapCameraView_Position( camera.get() ), position ) > 0.1 );
    CheckPointClose( MapCameraView_Forward( camera.get() ), forward );
    DragButton( camera.get(), QEvent::MouseButtonPress, start, Qt::RightButton );
    QFocusEvent out( QEvent::FocusOut, Qt::OtherFocusReason ); QCoreApplication::sendEvent( camera.get(), &out );
    DragButton( camera.get(), QEvent::MouseButtonRelease, start, Qt::RightButton ); deliver(); CHECK( requests.count == 1 );
    DragButton( camera.get(), QEvent::MouseButtonPress, start, Qt::RightButton, Qt::ShiftModifier );
    DragButton( camera.get(), QEvent::MouseButtonRelease, start, Qt::RightButton, Qt::ShiftModifier ); deliver(); CHECK( requests.count == 1 );
    CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.selection.revision == selectionRevision );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK_FALSE( ws.editPreview.bActive );
}

TEST_CASE( "The 3D modes draw shaded faces and are remembered with the arrangement", "[map][gui][views][render]" )
{
    session_t session;
    view_settings_t settings( &session.gui.settings );
    settings.Set( "editor.viewport.active_border", false );
    REQUIRE( session.workspace.wire.faces.nCount > 0u ); // Brush sides, mesh faces, patch and terrain cells.
    std::unique_ptr<QWidget> views( MapViews_Create( nullptr, &session.workspace ) );
    ShowAt( views.get(), 1200, 900 );
    QWidget *camera = MapViews_PaneView( views.get(), 0 );
    CHECK( MapCameraView_RenderMode( camera ) == map_render_mode_t::WIREFRAME );
    const QImage wireImage = camera->grab().toImage().convertToFormat( QImage::Format_ARGB32 );
    // Choosing "3d Shaded" fills face interiors. Colour-count comparisons
    // are misleading because antialiased grid lines already have many shades.
    auto *shaded = views->findChild<QAction *>( QStringLiteral( "EditorViewRender0_%1" ).arg( static_cast<int>( map_render_mode_t::SHADED ) ) );
    REQUIRE( shaded != nullptr );
    shaded->trigger();
    CHECK( MapViews_PaneRenderMode( views.get(), 0 ) == map_render_mode_t::SHADED );
    CHECK( MapCameraView_RenderMode( MapViews_PaneView( views.get(), 0 ) ) == map_render_mode_t::SHADED );
    CHECK( shaded->isChecked() );
    const QImage shadedImage = MapViews_PaneView( views.get(), 0 )->grab().toImage().convertToFormat( QImage::Format_ARGB32 );
    REQUIRE( shadedImage.size() == wireImage.size() );
    int changedPixels = 0;
    for ( int y = 0; y < wireImage.height(); ++y ) {
        const auto *before = reinterpret_cast<const QRgb *>( wireImage.constScanLine( y ) );
        const auto *after = reinterpret_cast<const QRgb *>( shadedImage.constScanLine( y ) );
        for ( int x = 0; x < wireImage.width(); ++x ) { changedPixels += before[x] != after[x]; }
    }
    CHECK( changedPixels > wireImage.width() * wireImage.height() / 20 );
    // A 2D pane switched to a 3D mode becomes the 3D view in that mode.
    MapViews_SetPaneView( views.get(), 1, map_view_type_t::CAMERA, map_render_mode_t::NORMALS );
    CHECK( MapViews_PaneType( views.get(), 1 ) == map_view_type_t::CAMERA );
    CHECK( MapViews_PaneRenderMode( views.get(), 1 ) == map_render_mode_t::NORMALS );
    // Overlays and modes survive saving and restoring the arrangement.
    MapViews_SetPaneMeshEdges( views.get(), 0, false );
    MapViews_SetPaneWireOverlay( views.get(), 0, true );
    const QByteArray state = MapViews_SavePresentation( views.get() );
    MapViews_SetPaneView( views.get(), 0, map_view_type_t::CAMERA, map_render_mode_t::WIREFRAME );
    MapViews_SetPaneMeshEdges( views.get(), 0, true );
    MapViews_SetPaneWireOverlay( views.get(), 0, false );
    REQUIRE( MapViews_RestorePresentation( views.get(), state ) );
    CHECK( MapViews_PaneRenderMode( views.get(), 0 ) == map_render_mode_t::SHADED );
    CHECK_FALSE( MapViews_PaneMeshEdges( views.get(), 0 ) );
    CHECK( MapViews_PaneWireOverlay( views.get(), 0 ) );
    CHECK( MapViews_PaneRenderMode( views.get(), 1 ) == map_render_mode_t::NORMALS );
    // An older state without modes still restores, with the defaults.
    QJsonObject old = QJsonDocument::fromJson( state ).object();
    QJsonArray panes = old.value( QStringLiteral( "panes" ) ).toArray();
    for ( int i = 0; i < panes.size(); ++i ) {
        QJsonObject pane = panes[i].toObject();
        pane.remove( QStringLiteral( "render" ) );
        pane.remove( QStringLiteral( "mesh_edges" ) );
        pane.remove( QStringLiteral( "wire_overlay" ) );
        panes[i] = pane;
    }
    old.insert( QStringLiteral( "panes" ), panes );
    REQUIRE( MapViews_RestorePresentation( views.get(), QJsonDocument( old ).toJson() ) );
    CHECK( MapViews_PaneRenderMode( views.get(), 0 ) == map_render_mode_t::WIREFRAME );
    CHECK( MapViews_PaneMeshEdges( views.get(), 0 ) );
    // A bad mode is refused like any other corrupt value.
    QJsonObject bad = QJsonDocument::fromJson( state ).object();
    QJsonArray badPanes = bad.value( QStringLiteral( "panes" ) ).toArray();
    QJsonObject first = badPanes[0].toObject();
    first.insert( QStringLiteral( "render" ), 99 );
    badPanes[0] = first;
    bad.insert( QStringLiteral( "panes" ), badPanes );
    CHECK_FALSE( MapViews_ValidatePresentation( QJsonDocument( bad ).toJson() ) );
}

TEST_CASE( "Filled camera views draw a surface grid on every geometry kind", "[map][gui][views][render][surface-grid]" )
{
    for ( const auto kind : { map_wire_kind_t::BRUSH, map_wire_kind_t::MESH, map_wire_kind_t::PATCH, map_wire_kind_t::TERRAIN } ) {
        CAPTURE( static_cast<int>( kind ) );
        session_t session; auto &ws = session.workspace;
        view_settings_t settings( &session.gui.settings );
        DisableSurfaceGridTestAids( settings );
        MapWorkspace_SetGridVisible( &ws, CY_FALSE );
        CHECK( EditorSettings_Bool( &session.gui.settings, "editor.grid.show_surface_3d", CY_FALSE ) );
        const map_wire_object_t *object = nullptr;
        for ( usize i = 0; i < ws.wire.objects.nCount; ++i ) {
            if ( ws.wire.objects.pData[i].kind == kind ) { object = &ws.wire.objects.pData[i]; break; }
        }
        REQUIRE( object != nullptr ); REQUIRE( object->bounds.bHas );
        const u64 id = object->id; CAPTURE( id );
        ws.frameBounds = object->bounds;
        OnlyVisible( ws, { id } );
        std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) );
        ShowAt( camera.get(), 800, 600 );
        for ( const auto mode : { map_render_mode_t::SHADED, map_render_mode_t::FULLBRIGHT } ) {
            CAPTURE( static_cast<int>( mode ) );
            MapCameraView_SetRenderMode( camera.get(), mode );
            settings.Set( "editor.grid.show_surface_3d", false );
            const QImage plain = camera->grab().toImage();
            settings.Set( "editor.grid.show_surface_3d", true );
            const QImage gridded = camera->grab().toImage();
            CHECK( ChangedPixelCount( plain, gridded ) > 64 );
            CHECK( MapCameraView_GridInfo( camera.get() ).nCandidateSegments == 0u );
            settings.Set( "editor.grid.show_surface_3d", false );
            CHECK( camera->grab().toImage() == plain );
        }
    }
}

TEST_CASE( "Surface grids follow world spacing and stay inside a sloped polygon without changing authored state", "[map][gui][views][render][surface-grid][clipping]" )
{
    session_t session; auto &ws = session.workspace;
    view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    DisableSurfaceGridTestAids( settings );
    settings.Real( "editor.camera.look_sensitivity", 1.0 );
    settings.Set( "editor.camera.invert_y", false );
    settings.Set( "editor.grid.adaptive_3d", false );
    MapWorkspace_SetGridVisible( &ws, CY_FALSE );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) );
    ShowAt( camera.get(), 800, 600 ); FacePositiveX( camera.get() );
    MapCameraView_SetRenderMode( camera.get(), map_render_mode_t::SHADED );
    const auto p = MapCameraView_Position( camera.get() );
    const std::vector<math::vec3d_t> vertices{
        { p.x + 448, p.y - 160, p.z - 120 },
        { p.x + 512, p.y + 160, p.z - 120 },
        { p.x + 512, p.y - 160, p.z + 120 }
    };
    AppendSurfaceGridTestFace( ws, 1u, vertices );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
    ShowAt( top.get(), 800, 600 );
    const QImage topBefore = top->grab().toImage();
    const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
    const usize steps = EditorHistory_StepCount( &ws.history ); const auto selectionRevision = ws.selection.revision;
    const auto *points = ws.wire.points.pData; const auto *faces = ws.wire.faces.pData;
    const usize pointCount = ws.wire.points.nCount, faceCount = ws.wire.faces.nCount;
    settings.Set( "editor.grid.show_surface_3d", false );
    const QImage plain = camera->grab().toImage();
    settings.Set( "editor.grid.show_surface_3d", true );
    const QImage gridded = camera->grab().toImage();
    CHECK( ChangedPixelCount( plain, gridded ) > 64 );
    CHECK( top->grab().toImage() == topBefore );
    // Allow only the antialiased edge margin around the true triangle.
    // Its rectangular bounds have a large empty corner that must stay clear.
    QPolygonF polygon;
    for ( const auto point : vertices ) {
        QPointF screen; REQUIRE( MapCameraView_WorldToView( camera.get(), point, &screen ) ); polygon << screen;
    }
    QPainterPath silhouette; silhouette.addPolygon( polygon ); silhouette.closeSubpath();
    QPainterPathStroker margin; margin.setWidth( 4.0 );
    const QPainterPath allowed = silhouette.united( margin.createStroke( silhouette ) );
    int outside = 0;
    const qreal ratio = gridded.devicePixelRatio();
    for ( int y = 0; y < gridded.height(); ++y ) {
        for ( int x = 0; x < gridded.width(); ++x ) {
            if ( plain.pixel( x, y ) != gridded.pixel( x, y ) && !allowed.contains( QPointF( ( x + 0.5 ) / ratio, ( y + 0.5 ) / ratio ) ) ) { ++outside; }
        }
    }
    CHECK( outside == 0 );
    MapWorkspace_SetGridSize( &ws, 64.0 );
    CHECK( camera->grab().toImage() != gridded );
    CHECK( ws.gridSize == 64.0 );
    settings.Set( "editor.grid.show_surface_3d", false );
    CHECK( camera->grab().toImage() == plain );
    CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( ws.selection.revision == selectionRevision );
    CHECK( ws.wire.points.pData == points ); CHECK( ws.wire.faces.pData == faces );
    CHECK( ws.wire.points.nCount == pointCount ); CHECK( ws.wire.faces.nCount == faceCount );
    for ( usize i = 0; i < vertices.size(); ++i ) { CheckPointClose( ws.wire.points.pData[i], vertices[i] ); }
    CHECK_FALSE( ws.bGridVisible ); CHECK_FALSE( EditorSettings_Bool( &session.gui.settings, "editor.grid.show_3d", CY_TRUE ) );
    // The edge-only mode never substitutes a filled surface grid.
    MapCameraView_SetRenderMode( camera.get(), map_render_mode_t::WIREFRAME );
    const QImage wire = camera->grab().toImage();
    settings.Set( "editor.grid.show_surface_3d", true );
    CHECK( camera->grab().toImage() == wire );
}

TEST_CASE( "Nearer face fills hide the complete grid of a farther surface", "[map][gui][views][render][surface-grid][occlusion]" )
{
    session_t session; auto &ws = session.workspace;
    view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    DisableSurfaceGridTestAids( settings );
    settings.Real( "editor.camera.look_sensitivity", 1.0 );
    settings.Set( "editor.camera.invert_y", false );
    MapWorkspace_SetGridVisible( &ws, CY_FALSE );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) );
    ShowAt( camera.get(), 800, 600 ); FacePositiveX( camera.get() );
    MapCameraView_SetRenderMode( camera.get(), map_render_mode_t::FULLBRIGHT );
    const auto p = MapCameraView_Position( camera.get() );
    AppendSurfaceGridTestFace( ws, 1u, {
        { p.x + 400, p.y - 200, p.z - 150 }, { p.x + 400, p.y + 200, p.z - 150 },
        { p.x + 400, p.y + 200, p.z + 150 }, { p.x + 400, p.y - 200, p.z + 150 }
    } );
    AppendSurfaceGridTestFace( ws, 2u, {
        { p.x + 600, p.y - 120, p.z - 90 }, { p.x + 650, p.y + 120, p.z - 90 },
        { p.x + 620, p.y - 120, p.z + 90 }
    } );
    const QImage both = camera->grab().toImage();
    OnlyVisible( ws, { 1u } );
    CHECK( camera->grab().toImage() == both );
    settings.Set( "editor.grid.show_surface_3d", false );
    CHECK( ChangedPixelCount( both, camera->grab().toImage() ) > 64 );
}

TEST_CASE( "Moved and cloned brush previews keep their grid on the world lattice", "[map][gui][views][render][surface-grid][transform-preview]" )
{
    for ( const bool clone : { false, true } ) {
        CAPTURE( clone );
        session_t session; auto &ws = session.workspace;
        view_settings_t settings( &session.gui.settings );
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        DisableSurfaceGridTestAids( settings );
        settings.Real( "editor.camera.look_sensitivity", 1.0 ); settings.Set( "editor.camera.invert_y", false );
        settings.Set( "editor.grid.adaptive_3d", false );
        MapWorkspace_SetGridVisible( &ws, CY_FALSE );
        std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) );
        ShowAt( camera.get(), 800, 600 ); FacePositiveX( camera.get() );
        MapCameraView_SetRenderMode( camera.get(), map_render_mode_t::FULLBRIGHT );
        const auto p = MapCameraView_Position( camera.get() );
        const f64 yLow = std::floor( ( p.y - 80 ) / 16.0 ) * 16.0 + 3.0;
        const f64 zLow = std::floor( ( p.z - 80 ) / 16.0 ) * 16.0 + 5.0;
        map_bounds_t box{}; MapBounds_AddPoint( box, { p.x + 400, yLow, zLow } );
        MapBounds_AddPoint( box, { p.x + 464, yLow + 160, zLow + 160 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
        const u64 id = EditorSelection_At( &ws.selection, 0 );
        const auto original = ObjectLineVertices( ws.wire, id );
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
        const usize steps = EditorHistory_StepCount( &ws.history );
        const auto *points = ws.wire.points.pData; const auto *faces = ws.wire.faces.pData;
        const QImage source = camera->grab().toImage();
        map_transform_preview_t transform{}; transform.kind = map_transform_preview_kind_t::TRANSLATE;
        transform.pivot = MapBounds_Center( box ); transform.delta = { 0, 151, 5 }; transform.bClone = clone;
        MapWorkspace_SetTransformPreview( &ws, transform ); REQUIRE( ws.editPreview.bActive );
        // Translation puts both object-local origins at coordinate % 16 ==
        // 10. The visible lattice must still cross world coordinate % 16 == 0.
        const f64 yLine = std::ceil( ( yLow + 151 + 96 ) / 16.0 ) * 16.0;
        const f64 zMiddle = std::ceil( ( zLow + 5 + 104 ) / 16.0 ) * 16.0 + 8.0;
        QPointF onWorldLine, inWorldCell;
        REQUIRE( MapCameraView_WorldToView( camera.get(), { box.box.minimum.x, yLine, zMiddle }, &onWorldLine ) );
        REQUIRE( MapCameraView_WorldToView( camera.get(), { box.box.minimum.x, yLine + 8, zMiddle }, &inWorldCell ) );
        REQUIRE( camera->rect().adjusted( 8, 8, -8, -8 ).contains( onWorldLine.toPoint() ) );
        REQUIRE( camera->rect().adjusted( 8, 8, -8, -8 ).contains( inWorldCell.toPoint() ) );
        settings.Set( "editor.grid.show_surface_3d", false ); const QImage plain = camera->grab().toImage();
        settings.Set( "editor.grid.show_surface_3d", true ); const QImage gridded = camera->grab().toImage();
        CHECK( ChangedPixelsNear( plain, gridded, onWorldLine, 1 ) > 0 );
        CHECK( ChangedPixelsNear( plain, gridded, inWorldCell, 1 ) == 0 );
        CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
        CHECK( ws.wire.points.pData == points ); CHECK( ws.wire.faces.pData == faces );
        CheckObjectVertices( ws.wire, id, original ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
        MapWorkspace_ClearEditPreview( &ws ); CHECK_FALSE( ws.editPreview.bActive );
        CHECK( camera->grab().toImage() == source );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( document->geometry.brushes.nCount == 1u );
        CheckObjectVertices( ws.wire, id, original );
    }
}

TEST_CASE( "Staged block construction shows its surface grid and cancellation discards only the preview", "[map][gui][views][render][surface-grid][block-preview]" )
{
    session_t session; auto &ws = session.workspace;
    view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    DisableSurfaceGridTestAids( settings );
    settings.Real( "editor.camera.look_sensitivity", 1.0 ); settings.Set( "editor.camera.invert_y", false );
    MapWorkspace_SetGridVisible( &ws, CY_FALSE ); MapWorkspace_SetTool( &ws, map_tool_t::BLOCK );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) );
    ShowAt( camera.get(), 800, 600 ); FacePositiveX( camera.get() );
    MapCameraView_SetRenderMode( camera.get(), map_render_mode_t::SHADED );
    const auto p = MapCameraView_Position( camera.get() ); const QImage empty = camera->grab().toImage();
    const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
    const usize steps = EditorHistory_StepCount( &ws.history );
    map_bounds_t box{}; MapBounds_AddPoint( box, { p.x + 400, p.y - 96, p.z - 96 } );
    MapBounds_AddPoint( box, { p.x + 528, p.y + 96, p.z + 96 } );
    MapWorkspace_SetEditPreview( &ws, box ); REQUIRE( ws.editPreview.status == map_status_t::OK );
    REQUIRE( MapWorkspace_StageBlockPreview( &ws ) ); REQUIRE( MapWorkspace_HasBlockPreview( &ws ) );
    const auto *previewPoints = ws.editPreviewWire.points.pData; const usize previewCount = ws.editPreviewWire.points.nCount;
    REQUIRE( previewCount > 0u );
    settings.Set( "editor.grid.show_surface_3d", false ); const QImage plain = camera->grab().toImage();
    settings.Set( "editor.grid.show_surface_3d", true ); const QImage gridded = camera->grab().toImage();
    CHECK( ChangedPixelCount( plain, gridded ) > 64 );
    CHECK( ws.editPreviewWire.points.pData == previewPoints ); CHECK( ws.editPreviewWire.points.nCount == previewCount );
    CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
    CHECK( document->geometry.brushes.nCount == 0u ); CHECK( ws.wire.faces.nCount == 0u );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    QKeyEvent cancel( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &cancel );
    CHECK_FALSE( ws.editPreview.bActive ); CHECK_FALSE( MapWorkspace_HasBlockPreview( &ws ) );
    CHECK( ws.editPreviewWire.points.nCount == 0u ); CHECK( camera->grab().toImage() == empty );
    CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
    CHECK( document->geometry.brushes.nCount == 0u ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
}

TEST_CASE( "Extreme close-up and grazing near-clipped surfaces keep a finite visible grid", "[map][gui][views][render][surface-grid][near-clip]" )
{
    for ( const bool grazing : { false, true } ) {
        CAPTURE( grazing );
        session_t session; auto &ws = session.workspace;
        view_settings_t settings( &session.gui.settings );
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        DisableSurfaceGridTestAids( settings );
        settings.Real( "editor.camera.look_sensitivity", 1.0 ); settings.Set( "editor.camera.invert_y", false );
        MapWorkspace_SetGridVisible( &ws, CY_FALSE );
        std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) );
        ShowAt( camera.get(), 800, 600 ); FacePositiveX( camera.get() );
        MapCameraView_SetRenderMode( camera.get(), map_render_mode_t::FULLBRIGHT );
        const auto p = MapCameraView_Position( camera.get() );
        if ( grazing ) {
            AppendSurfaceGridTestFace( ws, 1u, {
                { p.x - 16, p.y + 16, p.z - 256 }, { p.x + 1024, p.y + 16, p.z - 256 },
                { p.x + 1024, p.y + 16, p.z + 256 }, { p.x - 16, p.y + 16, p.z + 256 }
            } );
            QPointF behind; CHECK_FALSE( MapCameraView_WorldToView( camera.get(), ws.wire.points.pData[0], &behind ) );
            const auto normal = ws.wire.faces.pData[0].normal, forward = MapCameraView_Forward( camera.get() );
            CHECK( std::abs( normal.x * forward.x + normal.y * forward.y + normal.z * forward.z ) < 1e-10 );
        } else {
            // Its original projected bounds exceed the raster coordinate
            // range by many orders of magnitude, while its visible world
            // extent is only a few grid cells at this close camera distance.
            AppendSurfaceGridTestFace( ws, 1u, {
                { p.x + 16, p.y - 1e9, p.z - 1e9 }, { p.x + 16, p.y + 1e9, p.z - 1e9 },
                { p.x + 16, p.y + 1e9, p.z + 1e9 }, { p.x + 16, p.y - 1e9, p.z + 1e9 }
            } );
        }
        QPointF probe;
        REQUIRE( MapCameraView_WorldToView( camera.get(), grazing ? math::vec3d_t{ p.x + 512, p.y + 16, p.z } : math::vec3d_t{ p.x + 16, p.y, p.z }, &probe ) );
        CHECK( std::isfinite( probe.x() ) ); CHECK( std::isfinite( probe.y() ) );
        REQUIRE( camera->rect().contains( probe.toPoint() ) );
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
        settings.Set( "editor.grid.show_surface_3d", false ); const QImage plain = camera->grab().toImage();
        settings.Set( "editor.grid.show_surface_3d", true ); const QImage gridded = camera->grab().toImage();
        REQUIRE_FALSE( gridded.isNull() ); CHECK( gridded.size() == plain.size() );
        CHECK( ChangedPixelCount( plain, gridded ) > 64 );
        CHECK( camera->grab().toImage() == gridded );
        CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
        CHECK( EditorHistory_StepCount( &ws.history ) == 0u );
    }
}

TEST_CASE( "A large warped quad grid matches its exact presentation triangles", "[map][gui][views][render][surface-grid][warped-quad]" )
{
    session_t session; auto &ws = session.workspace;
    view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    DisableSurfaceGridTestAids( settings );
    settings.Real( "editor.camera.look_sensitivity", 1.0 ); settings.Set( "editor.camera.invert_y", false );
    MapWorkspace_SetGridVisible( &ws, CY_FALSE );
    std::unique_ptr<QWidget> views( MapViews_Create( nullptr, &ws ) );
    ShowAt( views.get(), 1200, 900 ); MapViews_SetMaximized( views.get(), 0 );
    MapViews_SetPaneView( views.get(), 0, map_view_type_t::CAMERA, map_render_mode_t::FULLBRIGHT );
    MapViews_SetPaneMeshEdges( views.get(), 0, false );
    QWidget *camera = MapViews_PaneView( views.get(), 0 ); FacePositiveX( camera );
    const auto p = MapCameraView_Position( camera );
    const std::vector<math::vec3d_t> vertices{
        { p.x + 16, p.y - 1e9, p.z - 1e9 }, { p.x + 16, p.y + 80, p.z - 80 },
        { p.x + 16, p.y + 80, p.z + 80 }, { p.x + 24, p.y - 80, p.z + 80 }
    };
    const auto normalFor = []( const std::vector<math::vec3d_t> &ring ) {
        math::vec3d_t normal{};
        for ( usize i = 0; i < ring.size(); ++i ) {
            const auto a = ring[i], b = ring[( i + 1u ) % ring.size()];
            normal.x += ( a.y - b.y ) * ( a.z + b.z );
            normal.y += ( a.z - b.z ) * ( a.x + b.x );
            normal.z += ( a.x - b.x ) * ( a.y + b.y );
        }
        const f64 length = TestDistance( normal, {} ); REQUIRE( length > 0.0 );
        return math::vec3d_t{ normal.x / length, normal.y / length, normal.z / length };
    };
    AppendSurfaceGridTestFace( ws, 1u, vertices );
    ws.wire.faces.pData[0].normal = normalFor( vertices );
    settings.Set( "editor.grid.show_surface_3d", false ); const QImage plain = camera->grab().toImage();
    settings.Set( "editor.grid.show_surface_3d", true ); const QImage quad = camera->grab().toImage();
    CHECK( ChangedPixelCount( plain, quad ) > 64 );
    // At the distant corner a relative planarity tolerance can swallow the
    // eight-unit warp. The quad must render the same lattice as the two real
    // planes, with no face-outline seam influencing the image comparison.
    auto first = ws.wire.faces.pData[0], second = first;
    first.iFirstIndex = 0; first.nIndices = 3; first.normal = normalFor( { vertices[0], vertices[1], vertices[2] } );
    second.iFirstIndex = 3; second.nIndices = 3; second.normal = normalFor( { vertices[0], vertices[2], vertices[3] } );
    Vector_Clear( &ws.wire.faceIndices ); Vector_Clear( &ws.wire.faces );
    for ( const u32 index : { 0u, 1u, 2u, 0u, 2u, 3u } ) { REQUIRE( Vector_PushBack( &ws.wire.faceIndices, index ) ); }
    REQUIRE( Vector_PushBack( &ws.wire.faces, first ) ); REQUIRE( Vector_PushBack( &ws.wire.faces, second ) );
    MapWorkspace_Notify( &ws, MAP_CHANGE_VIEW );
    const QImage triangles = camera->grab().toImage();
    // Independent face fills may cover a boundary pixel in a different
    // painter order. Compare the actual surface interiors away from that
    // two-pixel seam; a wrong average plane shifts whole grid lines there.
    QPointF a, c; REQUIRE( MapCameraView_WorldToView( camera, vertices[0], &a ) );
    REQUIRE( MapCameraView_WorldToView( camera, vertices[2], &c ) );
    QPainterPath diagonal; diagonal.moveTo( a ); diagonal.lineTo( c );
    QPainterPathStroker margin; margin.setWidth( 4.0 ); const auto seam = margin.createStroke( diagonal );
    const qreal ratio = triangles.devicePixelRatio(); int outside = 0;
    for ( int y = 0; y < triangles.height(); ++y ) {
        for ( int x = 0; x < triangles.width(); ++x ) {
            if ( triangles.pixel( x, y ) != quad.pixel( x, y ) && !seam.contains( QPointF( ( x + 0.5 ) / ratio, ( y + 0.5 ) / ratio ) ) ) { ++outside; }
        }
    }
    if ( outside != 0 ) {
        CHECK( plain.save( QStringLiteral( "/Users/karlosiric/Documents/MyProjects/CYPHER/artifacts/mason_surface_grid_warp_plain.png" ) ) );
        CHECK( quad.save( QStringLiteral( "/Users/karlosiric/Documents/MyProjects/CYPHER/artifacts/mason_surface_grid_warp_quad.png" ) ) );
        CHECK( triangles.save( QStringLiteral( "/Users/karlosiric/Documents/MyProjects/CYPHER/artifacts/mason_surface_grid_warp_triangles.png" ) ) );
        INFO( "Camera: " << p.x << ", " << p.y << ", " << p.z );
        INFO( "Triangle normal: " << second.normal.x << ", " << second.normal.y << ", " << second.normal.z );
        INFO( "Grid pixels in quad: " << ChangedPixelCount( plain, quad ) << "; triangles: " << ChangedPixelCount( plain, triangles ) );
        CHECK( outside == 0 );
    }
    CHECK( ws.pDocument->geometry.brushes.nCount == 0u ); CHECK( EditorHistory_StepCount( &ws.history ) == 0u );
}

TEST_CASE( "Viewport drawing menu uses live settings and the registered grid command", "[map][gui][views][view-menus]" )
{
    session_t session;
    view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_RegisterCommands( &session.workspace, &session.gui.commands ) == command_registry_status_t::OK );
    int gridExecutions = 0;
    EditorCommands_SetObserver( &session.gui.commands,
        []( void *context, const command_desc_t &command, const command_args_t &, command_result_t result ) {
            if ( StringView_Equals( StringView_FromCString( command.pId ), StringView_FromCString( "map.grid.show" ) ) && result == command_result_t::OK ) {
                ++*static_cast<int *>( context );
            }
        }, &gridExecutions );
    std::unique_ptr<QWidget> views( MapViews_Create( nullptr, &session.workspace ) );
    ShowAt( views.get(), 1200, 900 );
    auto *axes = views->findChild<QAction *>( QStringLiteral( "EditorViewSetting_editor.viewport.show_axes" ) );
    auto *dimensions = views->findChild<QAction *>( QStringLiteral( "EditorViewSetting_editor.viewport.show_selection_dimensions" ) );
    auto *always = views->findChild<QAction *>( QStringLiteral( "EditorViewSetting_editor.viewport.io_lines.always" ) );
    auto *selected = views->findChild<QAction *>( QStringLiteral( "EditorViewSetting_editor.viewport.io_lines.selected" ) );
    auto *grid = views->findChild<QAction *>( QStringLiteral( "EditorViewShowGrid" ) );
    REQUIRE( axes != nullptr );
    REQUIRE( dimensions != nullptr );
    REQUIRE( always != nullptr );
    REQUIRE( selected != nullptr );
    REQUIRE( grid != nullptr );
    CHECK( axes->isChecked() );
    axes->trigger();
    CHECK_FALSE( EditorSettings_Bool( &session.gui.settings, "editor.viewport.show_axes", CY_TRUE ) );
    CHECK_FALSE( axes->isChecked() );
    settings.Set( "editor.viewport.show_axes", true );
    CHECK( axes->isChecked() );
    for ( const char *path : { "editor.grid.show_3d", "editor.grid.show_surface_3d", "editor.viewport.show_rulers" } ) {
        auto *action = views->findChild<QAction *>( QStringLiteral( "EditorViewSetting_%1" ).arg( QString::fromLatin1( path ) ) );
        REQUIRE( action != nullptr );
        CHECK( action->isChecked() );
        action->trigger();
        CHECK_FALSE( EditorSettings_Bool( &session.gui.settings, path, CY_TRUE ) );
        CHECK_FALSE( action->isChecked() );
        settings.Set( path, true );
        CHECK( action->isChecked() );
    }
    dimensions->trigger();
    CHECK_FALSE( EditorSettings_Bool( &session.gui.settings, "editor.viewport.show_selection_dimensions", CY_TRUE ) );
    CHECK( selected->isChecked() );
    always->trigger();
    CHECK( StringView_Equals( EditorSettings_Text( &session.gui.settings, "editor.viewport.io_lines", {} ), StringView_FromCString( "always" ) ) );
    CHECK( always->isChecked() );
    CHECK_FALSE( selected->isChecked() );
    settings.Choice( "editor.viewport.io_lines", "selected" );
    CHECK( selected->isChecked() );
    CHECK_FALSE( always->isChecked() );
    const bool gridBefore = session.workspace.bGridVisible;
    grid->trigger();
    CHECK( gridExecutions == 1 );
    CHECK( session.workspace.bGridVisible != gridBefore );
    CHECK( grid->isChecked() == session.workspace.bGridVisible );
    // Orthographic panes share one live menu; perspective has its own menu.
    const auto aids = views->findChildren<QMenu *>( QStringLiteral( "EditorViewDrawingAids" ) );
    REQUIRE( aids.size() == 1 );
    for ( int i = 0; i < 4; ++i ) {
        auto *options = views->findChild<QMenu *>( QStringLiteral( "EditorViewOptionsMenu%1" ).arg( i ) );
        REQUIRE( options != nullptr );
        bool found = false;
        for ( QAction *action : options->actions() ) {
            if ( action->menu() == aids[0] ) { found = true; CHECK( action->isVisible() == ( i != 0 ) ); }
        }
        CHECK( found );
    }
    EditorCommands_SetObserver( &session.gui.commands, nullptr, nullptr );
}

TEST_CASE( "Clean View applies only its viewport family after an explicit menu choice", "[map][gui][views][view-menus][clean-view]" )
{
    session_t session;
    view_settings_t settings( &session.gui.settings );
    for ( const char *path : { "editor.viewport.show_rulers", "editor.viewport.show_selection_bounds", "editor.viewport.show_selection_vertices",
        "editor.viewport.show_metrics", "editor.viewport.perspective.show_selection_bounds", "editor.viewport.perspective.show_selection_vertices",
        "editor.viewport.perspective.show_metrics" } ) { settings.Set( path, true ); }
    settings.Set( "editor.viewport.show_selection_dimensions", true );
    settings.Set( "editor.viewport.perspective.show_selection_dimensions", false );
    settings.Set( "editor.viewport.center_axes", false );
    settings.Set( "editor.viewport.perspective.center_axes", false );
    settings.Set( "editor.grid.adaptive", false );
    settings.Integer( "editor.grid.min_spacing_px", 23 );
    settings.Choice( "editor.viewport.entity_names", "never" );
    settings.Choice( "editor.viewport.perspective.entity_names", "always" );
    settings.Choice( "editor.viewport.io_lines", "always" );
    settings.Choice( "editor.viewport.perspective.io_lines", "always" );
    std::unique_ptr<QWidget> views( MapViews_Create( nullptr, &session.workspace ) );
    ShowAt( views.get(), 1200, 900 );
    QAction *clean2D = views->findChild<QAction *>( QStringLiteral( "EditorViewClean2D" ) );
    QAction *clean3D = views->findChild<QAction *>( QStringLiteral( "EditorViewClean3D" ) );
    REQUIRE( clean2D != nullptr ); REQUIRE( clean3D != nullptr );
    CHECK( EditorSettings_Bool( &session.gui.settings, "editor.viewport.show_rulers", CY_FALSE ) );
    CHECK( StringView_Equals( EditorSettings_Text( &session.gui.settings, "editor.viewport.entity_names", {} ), StringView_FromCString( "never" ) ) );
    QWidget *camera = MapViews_PaneView( views.get(), 0 );
    const auto position = MapCameraView_Position( camera );
    const f64 snap = session.workspace.gridSize;
    clean2D->trigger();
    for ( const char *path : { "editor.viewport.show_rulers", "editor.viewport.show_selection_bounds", "editor.viewport.show_selection_vertices", "editor.viewport.show_metrics" } ) {
        CHECK_FALSE( EditorSettings_Bool( &session.gui.settings, path, CY_TRUE ) );
    }
    CHECK( StringView_Equals( EditorSettings_Text( &session.gui.settings, "editor.viewport.entity_names", {} ), StringView_FromCString( "always" ) ) );
    CHECK( StringView_Equals( EditorSettings_Text( &session.gui.settings, "editor.viewport.io_lines", {} ), StringView_FromCString( "selected" ) ) );
    CHECK( EditorSettings_Bool( &session.gui.settings, "editor.viewport.perspective.show_selection_bounds", CY_FALSE ) );
    CHECK( StringView_Equals( EditorSettings_Text( &session.gui.settings, "editor.viewport.perspective.entity_names", {} ), StringView_FromCString( "always" ) ) );
    clean3D->trigger();
    for ( const char *path : { "editor.viewport.perspective.show_selection_bounds", "editor.viewport.perspective.show_selection_vertices", "editor.viewport.perspective.show_metrics" } ) {
        CHECK_FALSE( EditorSettings_Bool( &session.gui.settings, path, CY_TRUE ) );
    }
    CHECK( StringView_Equals( EditorSettings_Text( &session.gui.settings, "editor.viewport.perspective.entity_names", {} ), StringView_FromCString( "selected" ) ) );
    CHECK( StringView_Equals( EditorSettings_Text( &session.gui.settings, "editor.viewport.perspective.io_lines", {} ), StringView_FromCString( "selected" ) ) );
    CHECK( EditorSettings_Bool( &session.gui.settings, "editor.viewport.show_selection_dimensions", CY_FALSE ) );
    CHECK_FALSE( EditorSettings_Bool( &session.gui.settings, "editor.viewport.perspective.show_selection_dimensions", CY_TRUE ) );
    CHECK_FALSE( EditorSettings_Bool( &session.gui.settings, "editor.viewport.center_axes", CY_TRUE ) );
    CHECK_FALSE( EditorSettings_Bool( &session.gui.settings, "editor.viewport.perspective.center_axes", CY_TRUE ) );
    CHECK_FALSE( EditorSettings_Bool( &session.gui.settings, "editor.grid.adaptive", CY_TRUE ) );
    CHECK( EditorSettings_Integer( &session.gui.settings, "editor.grid.min_spacing_px", 0 ) == 23 );
    CHECK( session.workspace.gridSize == snap );
    CheckPointClose( MapCameraView_Position( camera ), position );
    CHECK( MapViews_PaneView( views.get(), 0 ) == camera );
    EditorSettings_SetScope( &session.gui.settings, settings_scope_t::USER, nullptr );
    CHECK_FALSE( clean2D->isEnabled() ); CHECK_FALSE( clean3D->isEnabled() );
}

TEST_CASE( "Selected entity names reserve readable space regardless of authored order", "[map][gui][views][render][entity-labels]" )
{
    for ( const bool perspective : { false, true } ) {
        CAPTURE( perspective ); session_t session; auto &ws = session.workspace;
        view_settings_t settings( &session.gui.settings ); DisableNameTestAids( settings );
        const auto origin = MapBounds_Center( ws.wire.bounds );
        const u64 first = AddNamedViewEntity( ws, origin, "ordinary" );
        const u64 second = AddNamedViewEntity( ws, origin, "priority" );
        OnlyVisible( ws, { first, second } );
        std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &ws ) : MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
        ShowAt( view.get(), 800, 600 );
        const QImage selectedLast = EntityNamePixels( view.get(), settings, perspective );
        REQUIRE_FALSE( PaintedNameBounds( selectedLast ).isEmpty() );
        const QImage selectedOnly = EntityNamePixels( view.get(), settings, perspective, "selected" );
        const QRect selectedBounds = PaintedNameBounds( selectedOnly );
        REQUIRE_FALSE( selectedBounds.isEmpty() );
        CHECK( selectedLast != selectedOnly ); // A free alternate anchor still shows the ordinary name.
        CHECK( selectedLast.copy( selectedBounds ) == selectedOnly.copy( selectedBounds ) );
        const QFontMetrics metrics( gui::EditorStyle_Font( session.gui.style, "viewport.labels" ) );
        CHECK( selectedBounds.width() / selectedOnly.devicePixelRatio() <= metrics.horizontalAdvance( QStringLiteral( "priority" ) ) + 8 );
        // Keep IDs and the wire's required sorted order intact. Exchange the
        // authored names and selected owner through the normal edit API.
        MapWorkspace_Select( &ws, first, MAP_SELECT_REPLACE );
        REQUIRE( MapWorkspace_SetEntityIdentityField( &ws, StringView_FromCString( "name" ), StringView_FromCString( "priority" ) ) );
        MapWorkspace_Select( &ws, second, MAP_SELECT_REPLACE );
        REQUIRE( MapWorkspace_SetEntityIdentityField( &ws, StringView_FromCString( "name" ), StringView_FromCString( "ordinary" ) ) );
        MapWorkspace_Select( &ws, first, MAP_SELECT_REPLACE );
        CHECK( EntityNamePixels( view.get(), settings, perspective ) == selectedLast );
        CHECK( EntityNamePixels( view.get(), settings, perspective, "selected" ) == selectedOnly );
    }
}

TEST_CASE( "Dense entity labels omit exhausted positions instead of covering existing names", "[map][gui][views][render][entity-labels]" )
{
    for ( const bool perspective : { false, true } ) {
        CAPTURE( perspective ); session_t session; auto &ws = session.workspace;
        view_settings_t settings( &session.gui.settings ); DisableNameTestAids( settings );
        const auto origin = MapBounds_Center( ws.wire.bounds );
        u64 ids[8]{};
        for ( u64 &id : ids ) { id = AddNamedViewEntity( ws, origin, "crowded" ); }
        OnlyVisible( ws, { ids[0], ids[1], ids[2], ids[3], ids[4], ids[5], ids[6], ids[7] } );
        MapWorkspace_SetSelection( &ws, ids, 4 );
        std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &ws ) : MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
        ShowAt( view.get(), 800, 600 );
        QImage fourGeometry;
        const QImage four = EntityNamePixels( view.get(), settings, perspective, "selected", &fourGeometry );
        REQUIRE_FALSE( PaintedNameBounds( four ).isEmpty() );
        MapWorkspace_SetSelection( &ws, ids, 8 );
        QImage eightGeometry;
        const QImage eight = EntityNamePixels( view.get(), settings, perspective, "selected", &eightGeometry );
        REQUIRE( four.size() == eight.size() );
        CHECK( PaintedNameBounds( eight ) == PaintedNameBounds( four ) );
        // Selecting more coincident entities changes their antialiased wires.
        // The 3D card is translucent, so its RGB can change above those wires
        // even when placement and text are identical. Everywhere with the
        // same underlying geometry must still match exactly: this catches
        // extra positions and repeated cards/text in free viewport space.
        int differentNamesOnStableGeometry = 0;
        int stablePaintedPixels = 0;
        int changedGeometryPixels = 0;
        for ( int y = 0; y < four.height(); ++y ) {
            for ( int x = 0; x < four.width(); ++x ) {
                if ( fourGeometry.pixel( x, y ) != eightGeometry.pixel( x, y ) ) { ++changedGeometryPixels; continue; }
                stablePaintedPixels += qAlpha( four.pixel( x, y ) ) != 0 ? 1 : 0;
                differentNamesOnStableGeometry += four.pixel( x, y ) != eight.pixel( x, y ) ? 1 : 0;
            }
        }
        INFO( "Pixels changed by coincident selection wires: " << changedGeometryPixels );
        REQUIRE( stablePaintedPixels > 0 );
        CHECK( differentNamesOnStableGeometry == 0 );
        OnlyVisible( ws, {} );
        CHECK( PaintedNameBounds( EntityNamePixels( view.get(), settings, perspective ) ).isEmpty() );
    }
}

TEST_CASE( "Long entity names remain inside cropped and minimum-sized viewports", "[map][gui][views][render][entity-labels]" )
{
    session_t session; auto &ws = session.workspace;
    view_settings_t settings( &session.gui.settings ); DisableNameTestAids( settings );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
    ShowAt( top.get(), 180, 100 );
    const QPointF world = MapOrthoView_ViewToWorld( top.get(), QPointF( 179, 45 ) );
    const u64 id = AddNamedViewEntity( ws, { world.x(), world.y(), 0 },
        "edge_name_with_long_description_that_should_be_elided_inside_the_visible_viewport" );
    OnlyVisible( ws, { id } );
    const QImage label = EntityNamePixels( top.get(), settings, false, "selected" );
    const QRect bounds = PaintedNameBounds( label );
    REQUIRE_FALSE( bounds.isEmpty() );
    CHECK( bounds.left() >= 2 * label.devicePixelRatio() );
    CHECK( bounds.right() < label.width() - 2 * label.devicePixelRatio() );
    CHECK( bounds.top() >= 2 * label.devicePixelRatio() );
    CHECK( bounds.bottom() < label.height() - 2 * label.devicePixelRatio() );
    const QPointF outside = MapOrthoView_ViewToWorld( top.get(), QPointF( 500, 400 ) );
    const u64 offscreen = AddNamedViewEntity( ws, { outside.x(), outside.y(), 0 }, "outside" );
    OnlyVisible( ws, { offscreen } );
    CHECK( PaintedNameBounds( EntityNamePixels( top.get(), settings, false ) ).isEmpty() );
    // Orthographic widgets enforce a 64px minimum, so exercise the actual
    // smallest pane instead of requesting a size QWidget will reject.
    ShowAt( top.get(), 64, 64 );
    REQUIRE( top->size() == QSize( 64, 64 ) );
    const QPointF tinyCenter = MapOrthoView_ViewToWorld( top.get(), QPointF( 32, 32 ) );
    const u64 tiny = AddNamedViewEntity( ws, { tinyCenter.x(), tinyCenter.y(), 0 }, "minimum_pane_name_is_elided" );
    OnlyVisible( ws, { tiny } );
    const QImage minimum = EntityNamePixels( top.get(), settings, false );
    const QRect minimumBounds = PaintedNameBounds( minimum );
    REQUIRE_FALSE( minimumBounds.isEmpty() );
    CHECK( minimumBounds.left() >= 2 * minimum.devicePixelRatio() );
    CHECK( minimumBounds.right() < minimum.width() - 2 * minimum.devicePixelRatio() );
    CHECK( minimumBounds.top() >= 2 * minimum.devicePixelRatio() );
    CHECK( minimumBounds.bottom() < minimum.height() - 2 * minimum.devicePixelRatio() );
}

TEST_CASE( "The view tab's menu lists projections with their keys, then the pane controls", "[map][gui][views][view-menus]" )
{
    session_t session;
    std::unique_ptr<QWidget> views( MapViews_Create( nullptr, &session.workspace ) );
    ShowAt( views.get(), 1200, 900 );
    auto *tab = views->findChild<QToolButton *>( QStringLiteral( "EditorViewChoose0" ) );
    REQUIRE( tab != nullptr );
    QMenu *menu = tab->menu();
    REQUIRE( menu != nullptr );
    // No frame, maximize, or close buttons on the view itself.
    for ( const char *name : { "EditorViewOptions0", "EditorViewFrame0", "EditorViewMaximize0", "EditorViewClose0" } ) {
        CHECK( views->findChild<QToolButton *>( QString::fromLatin1( name ) ) == nullptr );
    }
    const QWidget *header = tab->parentWidget();
    const QWidget *pane = header->parentWidget();
    CHECK( header->geometry().left() == 0 ); // Top-left, as in Hammer.
    CHECK( header->geometry().top() == 0 );
    CHECK( header->width() < pane->width() / 2 );
    CHECK( MapViews_PaneView( views.get(), 0 )->size() == pane->size() );
    CHECK( views->findChild<QAction *>( QStringLiteral( "EditorViewSetting_editor.grid.bands" ) ) == nullptr );
    // The 2D projections come first, each with the key the keymap gives it,
    // then the 3D modes; the 3D view in wireframe is what pane 0 shows.
    emit menu->aboutToShow();
    QList<QAction *> items;
    for ( QAction *action : menu->actions() ) {
        if ( !action->isSeparator() ) { items.append( action ); }
    }
    REQUIRE( items.size() == 10 );
    CHECK( items[0]->objectName() == QStringLiteral( "EditorViewProjection0_1" ) );
    CHECK( items[1]->objectName() == QStringLiteral( "EditorViewProjection0_2" ) );
    CHECK( items[2]->objectName() == QStringLiteral( "EditorViewProjection0_3" ) );
    CHECK( items[3]->objectName() == QStringLiteral( "EditorViewProjection0_0" ) ); // 3d Wireframe.
    CHECK( items[3]->isChecked() );
    for ( int i = 7; i < 10; ++i ) {
        CHECK( items[i]->objectName() == QStringLiteral( "EditorViewContent0_%1" ).arg( i - 3 ) );
        CHECK_FALSE( items[i]->isEnabled() ); // The standalone map GUI has no asset providers.
        CHECK_FALSE( items[i]->isChecked() );
    }
    CHECK( items[0]->text().startsWith( QStringLiteral( "2d Top\t" ) ) );
    CHECK( items[0]->text().section( QLatin1Char( '\t' ), 1 ) == QKeySequence( Qt::Key_F2 ).toString( QKeySequence::NativeText ) );
    CHECK( items[1]->text().endsWith( QKeySequence( Qt::Key_F3 ).toString( QKeySequence::NativeText ) ) );
    CHECK( items[2]->text().endsWith( QKeySequence( Qt::Key_F4 ).toString( QKeySequence::NativeText ) ) );
    QMenu *options = views->findChild<QMenu *>( QStringLiteral( "EditorViewOptionsMenu0" ) );
    REQUIRE( options != nullptr );
    const QList<QAction *> optionItems = options->actions();
    // The pane controls are menu items.
    auto *frame = views->findChild<QAction *>( QStringLiteral( "EditorViewFrameAction0" ) );
    auto *maximize = views->findChild<QAction *>( QStringLiteral( "EditorViewMaximizeAction0" ) );
    auto *close = views->findChild<QAction *>( QStringLiteral( "EditorViewCloseAction0" ) );
    auto *showAll = views->findChild<QAction *>( QStringLiteral( "EditorViewShowAll" ) );
    REQUIRE( frame != nullptr );
    REQUIRE( maximize != nullptr );
    REQUIRE( close != nullptr );
    REQUIRE( showAll != nullptr );
    for ( QAction *action : { frame, maximize, close, showAll } ) { CHECK( optionItems.contains( action ) ); }
    CHECK( maximize->isCheckable() );
    CHECK_FALSE( maximize->isChecked() );
    // Choosing a projection changes the view and the tab's name.
    items[0]->trigger();
    CHECK( MapViews_PaneType( views.get(), 0 ) == map_view_type_t::TOP );
    CHECK( tab->text() == QStringLiteral( "Top" ) );
    CHECK( items[0]->isChecked() );
    CHECK_FALSE( items[3]->isChecked() );
    QWidget *top = MapViews_PaneView( views.get(), 0 );
    items[0]->trigger();
    CHECK( MapViews_PaneView( views.get(), 0 ) == top );
    maximize->trigger();
    CHECK( MapViews_MaximizedPane( views.get() ) == 0 );
    CHECK( maximize->isChecked() );
    CHECK( maximize->text() == QStringLiteral( "Restore Views" ) );
    maximize->trigger();
    CHECK( MapViews_MaximizedPane( views.get() ) == -1 );
    CHECK_FALSE( maximize->isChecked() );
    MapViews_SetArrangement( views.get(), map_view_arrangement_t::PERSPECTIVE );
    CHECK_FALSE( close->isEnabled() );
    showAll->trigger();
    CHECK( close->isEnabled() );
    close->trigger();
    CHECK_FALSE( MapViews_IsPaneVisible( views.get(), 0 ) );
}

TEST_CASE( "The view menu reaches the application's windows only when their commands exist", "[map][gui][views][view-menus]" )
{
    session_t session;
    std::unique_ptr<QWidget> views( MapViews_Create( nullptr, &session.workspace ) );
    ShowAt( views.get(), 1200, 900 );
    auto *menu = views->findChild<QMenu *>( QStringLiteral( "EditorViewOptionsMenu0" ) );
    auto *properties = views->findChild<QAction *>( QStringLiteral( "EditorViewCommand_view.properties_0" ) );
    REQUIRE( menu != nullptr );
    REQUIRE( properties != nullptr );
    emit menu->aboutToShow();
    CHECK_FALSE( properties->isVisible() ); // Nothing registered it.
    int nRuns = 0;
    command_desc_t open{};
    open.pId = "view.properties";
    open.pLabel = "Object Properties";
    open.pfnExecute = []( void *context, const command_args_t & ) { ++*static_cast<int *>( context ); return command_result_t::OK; };
    open.pContext = &nRuns;
    REQUIRE( EditorCommands_Register( &session.gui.commands, &open, 1u ) == command_registry_status_t::OK );
    emit menu->aboutToShow();
    CHECK( properties->isVisible() );
    properties->trigger();
    CHECK( nRuns == 1 );
}

TEST_CASE( "Camera floor grid follows distant navigation with bounded finite segments", "[map][gui][views][camera-grid]" )
{
    session_t session;
    view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    settings.Set( "editor.viewport.perspective.show_axes", false );
    settings.Set( "editor.viewport.perspective.center_axes", false );
    session.workspace.frameBounds = {};
    MapBounds_AddPoint( session.workspace.frameBounds, math::Vec3d_Make( 100000.0, -250000.0, 0.0 ) );
    MapBounds_AddPoint( session.workspace.frameBounds, math::Vec3d_Make( 101024.0, -248976.0, 400.0 ) );
    std::unique_ptr<QWidget> view( MapCameraView_Create( nullptr, &session.workspace ) );
    ShowAt( view.get(), 800, 600 );
    const auto checkGrid = [&]() {
        ( void )view->grab();
        const map_camera_grid_info_t info = MapCameraView_GridInfo( view.get() );
        const math::vec3d_t camera = MapCameraView_Position( view.get() );
        CHECK( info.center.x == camera.x );
        CHECK( info.center.y == camera.y );
        CHECK( info.center.z == 0.0 );
        CHECK( std::isfinite( info.step ) );
        CHECK( std::isfinite( info.halfExtent ) );
        CHECK( info.step >= session.workspace.gridSize );
        CHECK( info.halfExtent >= std::abs( camera.z ) * 4.0 );
        CHECK( info.nCandidateSegments == MAP_CAMERA_GRID_MAX_SEGMENTS );
        CHECK( info.nVisibleSegments > 10u );
        CHECK( info.nVisibleSegments <= info.nCandidateSegments );
        CHECK( info.screenBounds.left() >= -1e-5 );
        CHECK( info.screenBounds.top() >= -1e-5 );
        CHECK( info.screenBounds.right() <= view->width() + 1e-5 );
        CHECK( info.screenBounds.bottom() <= view->height() + 1e-5 );
        return info;
    };
    const map_camera_grid_info_t initial = checkGrid();
    const QImage grid = view->grab().toImage();
    settings.Set( "editor.grid.show_3d", false );
    CHECK( view->grab().toImage() != grid );
    CHECK( MapCameraView_GridInfo( view.get() ).nCandidateSegments == 0u );
    settings.Set( "editor.grid.show_3d", true );
    const QPointF anchor( 400.0, 300.0 );
    QWheelEvent farMove( anchor, view->mapToGlobal( anchor ), QPoint(), QPoint( 0, -120000 ), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false );
    QCoreApplication::sendEvent( view.get(), &farMove );
    const map_camera_grid_info_t distant = checkGrid();
    CHECK( distant.center.x != initial.center.x );
    CHECK( distant.center.y != initial.center.y );
    CHECK( distant.step > initial.step );
    CHECK( session.workspace.gridSize == 16.0 );
}

TEST_CASE( "Every projection displays all three orientation axes without a grid", "[map][gui][views][camera-grid]" )
{
    session_t session;
    view_settings_t settings( &session.gui.settings );
    settings.Set( "editor.viewport.active_border", false ); // Pixel counts below exclude the focused pane's red outline.
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    MapWorkspace_SetGridVisible( &session.workspace, CY_FALSE );
    settings.Set( "editor.viewport.show_rulers", false );
    settings.Set( "editor.grid.show_3d", false );
    const gui::editor_style_color_t tokens[]{ gui::STYLE_COLOR_AXIS_X, gui::STYLE_COLOR_AXIS_Y, gui::STYLE_COLOR_AXIS_Z };
    const auto countColor = []( const QImage &image, QRect logicalRect, QColor color ) {
        const qreal ratio = image.devicePixelRatio();
        const QRect area( qRound( logicalRect.x() * ratio ), qRound( logicalRect.y() * ratio ),
                          qRound( logicalRect.width() * ratio ), qRound( logicalRect.height() * ratio ) );
        int count = 0;
        for ( int y = area.top(); y <= area.bottom(); ++y ) {
            for ( int x = area.left(); x <= area.right(); ++x ) {
                const QColor pixel = image.pixelColor( x, y );
                // Thin orientation arrows are antialiased against the
                // viewport, so accept their nearly opaque interior pixels.
                count += std::abs( pixel.red() - color.red() ) < 36 && std::abs( pixel.green() - color.green() ) < 36 &&
                         std::abs( pixel.blue() - color.blue() ) < 36 ? 1 : 0;
            }
        }
        return count;
    };
    for ( int projection = 0; projection < 4; ++projection ) {
        CAPTURE( projection );
        std::unique_ptr<QWidget> view( projection == 3 ? MapCameraView_Create( nullptr, &session.workspace )
            : MapOrthoView_Create( nullptr, &session.workspace, static_cast<map_ortho_axes_t>( projection ) ) );
        ShowAt( view.get(), 800, 600 );
        settings.Set( projection == 3 ? "editor.viewport.perspective.show_axes" : "editor.viewport.show_axes", true );
        settings.Set( projection == 3 ? "editor.viewport.perspective.center_axes" : "editor.viewport.center_axes", true );
        const QImage axes = view->grab().toImage();
        const QRect triad( 710, 0, 90, 90 );
        for ( const auto token : tokens ) {
            const QColor color = gui::EditorStyle_Color( session.gui.style, token );
            CHECK( countColor( axes, triad, color ) > 0 );
            if ( projection == 3 ) { CHECK( countColor( axes, QRect( 0, 90, 800, 510 ), color ) > 4 ); }
        }
        settings.Set( projection == 3 ? "editor.viewport.perspective.show_axes" : "editor.viewport.show_axes", false );
        settings.Set( projection == 3 ? "editor.viewport.perspective.center_axes" : "editor.viewport.center_axes", false );
        const QImage plain = view->grab().toImage();
        for ( const auto token : tokens ) { CHECK( countColor( plain, triad, gui::EditorStyle_Color( session.gui.style, token ) ) == 0 ); }
    }
}

TEST_CASE( "View menus offer navigation for their current projection and synchronize maximize", "[map][gui][views][view-menus][camera-grid]" )
{
    session_t session;
    std::unique_ptr<QWidget> views( MapViews_Create( nullptr, &session.workspace ) );
    ShowAt( views.get(), 1200, 900 );
    auto *cameraMenu = views->findChild<QMenu *>( QStringLiteral( "EditorViewCameraControls0" ) );
    auto *orthoMenu = views->findChild<QMenu *>( QStringLiteral( "EditorViewOrthoControls0" ) );
    REQUIRE( cameraMenu != nullptr );
    REQUIRE( orthoMenu != nullptr );
    CHECK( cameraMenu->menuAction()->isVisible() );
    CHECK_FALSE( orthoMenu->menuAction()->isVisible() );
    QWidget *camera = MapViews_PaneView( views.get(), 0 );
    QWidget *top = MapViews_PaneView( views.get(), 1 );
    const math::vec3d_t before = MapCameraView_Position( camera );
    const math::vec3d_t forward = MapCameraView_Forward( camera );
    auto *move = views->findChild<QAction *>( QStringLiteral( "EditorViewCameraForward_0" ) );
    REQUIRE( move != nullptr );
    move->trigger();
    const math::vec3d_t after = MapCameraView_Position( camera );
    CHECK( std::abs( ( after.x - before.x ) * forward.x + ( after.y - before.y ) * forward.y + ( after.z - before.z ) * forward.z - 64.0 ) < 1e-6 );
    const f64 zoom = MapOrthoView_Zoom( top );
    auto *zoomIn = views->findChild<QAction *>( QStringLiteral( "EditorViewOrthoZoomIn_1" ) );
    REQUIRE( zoomIn != nullptr );
    zoomIn->trigger();
    CHECK( MapOrthoView_Zoom( top ) > zoom );
    CHECK( MapCameraView_Position( camera ).x == after.x );
    auto *level = views->findChild<QAction *>( QStringLiteral( "EditorViewCameraLevel_0" ) );
    REQUIRE( level != nullptr );
    level->trigger();
    CHECK( MapCameraView_Forward( camera ).z == 0.0 );
    ( void )camera->grab();
    const map_camera_grid_info_t horizon = MapCameraView_GridInfo( camera );
    CHECK( horizon.nVisibleSegments > 0u );
    CHECK( horizon.screenBounds.top() >= -1e-5 );
    CHECK( horizon.screenBounds.bottom() <= camera->height() + 1e-5 );
    MapViews_SetPaneType( views.get(), 0, map_view_type_t::TOP );
    CHECK_FALSE( cameraMenu->menuAction()->isVisible() );
    CHECK( orthoMenu->menuAction()->isVisible() );
    auto *maximize = views->findChild<QAction *>( QStringLiteral( "EditorViewMaximizeAction0" ) );
    REQUIRE( maximize != nullptr );
    MapViews_SetMaximized( views.get(), 0 );
    CHECK( maximize->isChecked() );
    const QByteArray presentation = MapViews_SavePresentation( views.get() );
    MapViews_SetArrangement( views.get(), map_view_arrangement_t::FOUR );
    CHECK_FALSE( maximize->isChecked() );
    REQUIRE( MapViews_RestorePresentation( views.get(), presentation ) );
    CHECK( maximize->isChecked() );
    MapViews_SetPaneVisible( views.get(), 0, false );
    CHECK_FALSE( maximize->isChecked() );
}

TEST_CASE( "Grid density follows live display settings without changing snap spacing", "[map][gui][views][visualization]" )
{
    session_t session;
    view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    std::unique_ptr<QWidget> view( MapOrthoView_Create( nullptr, &session.workspace, map_ortho_axes_t::TOP ) );
    ShowAt( view.get(), 800, 600 );
    const f64 snap = session.workspace.gridSize;
    const f64 zoom = MapOrthoView_Zoom( view.get() );
    CHECK( MapOrthoView_GridStep( view.get() ) * zoom >= MAP_VIEW_GRID_MIN_PIXELS );
    const QImage normal = view->grab().toImage();
    settings.Integer( "editor.grid.min_spacing_px", 64 );
    CHECK( MapOrthoView_GridStep( view.get() ) * zoom >= 64.0 );
    CHECK( MapOrthoView_GridStep( view.get() ) * zoom < 128.0 );
    CHECK( view->grab().toImage() != normal );
    CHECK( session.workspace.gridSize == snap );
    settings.Set( "editor.grid.adaptive", false );
    CHECK( MapOrthoView_GridStep( view.get() ) == snap );
    settings.Integer( "editor.grid.min_spacing_px", 128 );
    CHECK( MapOrthoView_GridStep( view.get() ) == snap );
    // At pathological zoom the literal grid is bounded in rendering work,
    // while the actual modeling/snap grid remains one world unit.
    MapWorkspace_SetGridSize( &session.workspace, 1.0 );
    const QPointF local( 400.0, 300.0 );
    QWheelEvent wheel( local, view->mapToGlobal( local ), QPoint(), QPoint( 0, -120000 ), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false );
    QCoreApplication::sendEvent( view.get(), &wheel );
    CHECK( MapOrthoView_Zoom( view.get() ) == MAP_VIEW_ZOOM_MIN );
    CHECK( view->width() / ( MapOrthoView_GridStep( view.get() ) * MapOrthoView_Zoom( view.get() ) ) + 2.0 <= 4096.0 );
    CHECK( session.workspace.gridSize == 1.0 );
}

TEST_CASE( "Hiding viewport axes retains neutral origin grid lines", "[map][gui][views][visualization]" )
{
    session_t session;
    view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    for ( int projection = 0; projection < 4; ++projection ) {
        CAPTURE( projection );
        std::unique_ptr<QWidget> view( projection == 3 ? MapCameraView_Create( nullptr, &session.workspace )
            : MapOrthoView_Create( nullptr, &session.workspace, static_cast<map_ortho_axes_t>( projection ) ) );
        ShowAt( view.get(), 800, 600 );
        settings.Set( projection == 3 ? "editor.viewport.perspective.show_axes" : "editor.viewport.show_axes", true );
        settings.Set( projection == 3 ? "editor.viewport.perspective.center_axes" : "editor.viewport.center_axes", true );
        const QImage axes = view->grab().toImage();
        settings.Set( projection == 3 ? "editor.viewport.perspective.show_axes" : "editor.viewport.show_axes", false );
        settings.Set( projection == 3 ? "editor.viewport.perspective.center_axes" : "editor.viewport.center_axes", false );
        const QImage neutral = view->grab().toImage();
        CHECK( neutral != axes );
        if ( projection != 3 ) {
            const QPointF origin = MapOrthoView_WorldToView( view.get(), QPointF( 0.0, 0.0 ) );
            const qreal ratio = neutral.devicePixelRatio();
            const QColor pixel = neutral.pixelColor( qRound( origin.x() * ratio ), qRound( origin.y() * ratio ) );
            CHECK( pixel == gui::EditorStyle_Color( session.gui.style, gui::STYLE_COLOR_GRID_MAJOR ) );
        }
    }
}

TEST_CASE( "Changing authored grid size updates visible density and construction hierarchy", "[map][gui][views][grid-density]" )
{
    session_t session;
    view_settings_t settings( &session.gui.settings );
    settings.Set( "editor.viewport.active_border", false ); // Pixel counts below exclude the focused pane's red outline.
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    settings.Set( "editor.viewport.show_axes", false );
    settings.Set( "editor.viewport.center_axes", false );
    settings.Set( "editor.viewport.show_rulers", false );
    settings.Set( "editor.viewport.show_metrics", false );
    settings.Integer( "editor.grid.min_spacing_px", 4 );
    session.workspace.frameBounds = {};
    MapBounds_AddPoint( session.workspace.frameBounds, math::Vec3d_Make( -512.0, -512.0, -512.0 ) );
    MapBounds_AddPoint( session.workspace.frameBounds, math::Vec3d_Make( 512.0, 512.0, 512.0 ) );
    for ( const map_ortho_axes_t axes : { map_ortho_axes_t::TOP, map_ortho_axes_t::FRONT, map_ortho_axes_t::SIDE } ) {
        settings.Integer( "editor.grid.min_spacing_px", 4 );
        MapWorkspace_SetGridSize( &session.workspace, 16.0 );
        std::unique_ptr<QWidget> view( MapOrthoView_Create( nullptr, &session.workspace, axes ) );
        ShowAt( view.get(), 1000, 1000 );
        const f64 zoom = MapOrthoView_Zoom( view.get() );
        const QPointF corner = MapOrthoView_ViewToWorld( view.get(), QPointF() );
        const QImage coarse = view->grab().toImage();
        const f64 coarseStep = MapOrthoView_GridStep( view.get() );
        REQUIRE( coarseStep == 16.0 );
        MapWorkspace_SetGridSize( &session.workspace, 4.0 );
        CHECK( session.workspace.gridSize == 4.0 );
        CHECK( EditorSettings_Integer( &session.gui.settings, "editor.grid.size", 0 ) == 4 );
        CHECK( MapOrthoView_GridStep( view.get() ) < coarseStep );
        CHECK( MapOrthoView_GridStep( view.get() ) * zoom >= 4.0 );
        CHECK( view->grab().toImage() != coarse );

        // Reproduce the original failure: adaptive display can coarsen both
        // choices to one minor step, but must not erase their major hierarchy.
        settings.Integer( "editor.grid.min_spacing_px", 12 );
        MapWorkspace_SetGridSize( &session.workspace, 16.0 );
        const QImage large = view->grab().toImage();
        const f64 largeStep = MapOrthoView_GridStep( view.get() );
        MapWorkspace_SetGridSize( &session.workspace, 4.0 );
        REQUIRE( MapOrthoView_GridStep( view.get() ) == largeStep );
        const QImage small = view->grab().toImage();
        CHECK( small != large );
        const QColor minor = gui::EditorStyle_Color( session.gui.style, gui::STYLE_COLOR_GRID_MINOR );
        const QColor major = gui::EditorStyle_Color( session.gui.style, gui::STYLE_COLOR_GRID_MAJOR );
        const QColor background = gui::EditorStyle_Color( session.gui.style, gui::STYLE_COLOR_VIEWPORT_2D );
        QSet<QRgb> colors;
        for ( int y = 0; y < small.height(); y += 3 ) {
            for ( int x = 0; x < small.width(); x += 3 ) { colors.insert( small.pixel( x, y ) ); }
        }
        CHECK( colors.contains( minor.rgba() ) ); // Coarsening must not make every line major.
        CHECK( colors.contains( major.rgba() ) );
        CHECK( colors.contains( background.rgba() ) );
        CHECK( colors.size() == 3 ); // No orange bands or new contrasting overlay.
        CHECK( MapOrthoView_Zoom( view.get() ) == zoom );
        CHECK( MapOrthoView_ViewToWorld( view.get(), QPointF() ) == corner );
    }
}

TEST_CASE( "Changing authored grid size updates the perspective floor at normal framing distance", "[map][gui][views][grid-density][camera-grid]" )
{
    session_t session;
    view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    settings.Set( "editor.viewport.perspective.show_axes", false );
    settings.Set( "editor.viewport.perspective.center_axes", false );
    settings.Set( "editor.viewport.perspective.show_metrics", false );
    settings.Set( "editor.grid.show_3d", true );
    settings.Set( "editor.grid.adaptive_3d", true );
    settings.Integer( "editor.grid.min_spacing_px", 4 );
    session.workspace.frameBounds = {};
    MapBounds_AddPoint( session.workspace.frameBounds, math::Vec3d_Make( -800.0, -800.0, -64.0 ) );
    MapBounds_AddPoint( session.workspace.frameBounds, math::Vec3d_Make( 800.0, 800.0, 64.0 ) );
    MapWorkspace_SetGridSize( &session.workspace, 16.0 );
    std::unique_ptr<QWidget> view( MapCameraView_Create( nullptr, &session.workspace ) );
    ShowAt( view.get(), 800, 600 );
    const QImage coarseImage = view->grab().toImage();
    const map_camera_grid_info_t coarse = MapCameraView_GridInfo( view.get() );
    const math::vec3d_t camera = MapCameraView_Position( view.get() );
    const math::vec3d_t forward = MapCameraView_Forward( view.get() );
    REQUIRE( camera.z > 500.0 );
    REQUIRE( camera.z < 1500.0 );
    MapWorkspace_SetGridSize( &session.workspace, 4.0 );
    const QImage fineImage = view->grab().toImage();
    const map_camera_grid_info_t fine = MapCameraView_GridInfo( view.get() );
    CHECK( fineImage != coarseImage );
    CHECK( fine.step < coarse.step );
    CHECK( fine.step >= 4.0 );
    CHECK( fine.step / 4.0 == std::floor( fine.step / 4.0 ) );
    CHECK( fine.halfExtent >= std::abs( camera.z ) * 4.0 );
    CHECK( fine.nCandidateSegments == MAP_CAMERA_GRID_MAX_SEGMENTS );
    CHECK( fine.nCandidateSegments == coarse.nCandidateSegments );
    CHECK( fine.nVisibleSegments > 10u );
    // Distant projected spacing must fade before it becomes a bright moire
    // field. Compare equal areas in the empty perspective view, excluding
    // the outer edge fade, against the same viewport background color.
    const QColor background = gui::EditorStyle_Color( session.gui.style, gui::STYLE_COLOR_VIEWPORT_3D );
    const auto contrast = [&]( int top, int bottom ) {
        const qreal ratio = fineImage.devicePixelRatio();
        f64 total = 0.0;
        for ( int y = qRound( top * ratio ); y < qRound( bottom * ratio ); ++y ) {
            for ( int x = qRound( 80 * ratio ); x < qRound( 720 * ratio ); ++x ) {
                const QColor pixel = fineImage.pixelColor( x, y );
                total += std::abs( pixel.red() - background.red() ) + std::abs( pixel.green() - background.green() ) +
                         std::abs( pixel.blue() - background.blue() );
            }
        }
        return total;
    };
    const f64 nearContrast = contrast( 440, 580 );
    CHECK( nearContrast > 0.0 );
    CHECK( contrast( 20, 160 ) < nearContrast );
    CHECK( session.workspace.gridSize == 4.0 );
    CHECK( EditorSettings_Integer( &session.gui.settings, "editor.grid.size", 0 ) == 4 );
    CHECK( MapCameraView_Position( view.get() ).x == camera.x );
    CHECK( MapCameraView_Position( view.get() ).y == camera.y );
    CHECK( MapCameraView_Position( view.get() ).z == camera.z );
    CHECK( MapCameraView_Forward( view.get() ).x == forward.x );
    CHECK( MapCameraView_Forward( view.get() ).y == forward.y );
    CHECK( MapCameraView_Forward( view.get() ).z == forward.z );
}

TEST_CASE( "Camera floor visibility preserves a single bounded grid and camera state", "[map][gui][views][grid-density][camera-grid]" )
{
    session_t session;
    view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    settings.Set( "editor.viewport.perspective.show_axes", false );
    settings.Set( "editor.viewport.perspective.center_axes", false );
    settings.Set( "editor.grid.show_3d", true );
    std::unique_ptr<QWidget> view( MapCameraView_Create( nullptr, &session.workspace ) );
    ShowAt( view.get(), 800, 600 );
    const QImage plain = view->grab().toImage();
    const map_camera_grid_info_t baseline = MapCameraView_GridInfo( view.get() );
    const math::vec3d_t camera = MapCameraView_Position( view.get() );
    const f64 snap = session.workspace.gridSize;
    REQUIRE( baseline.nCandidateSegments == MAP_CAMERA_GRID_MAX_SEGMENTS );
    CHECK( baseline.nCandidateSegments == 1026u ); // One lattice, two directions, 513 lines each.
    settings.Set( "editor.grid.show_3d", false );
    const QImage hidden = view->grab().toImage();
    CHECK( hidden != plain );
    CHECK( MapCameraView_GridInfo( view.get() ).nCandidateSegments == 0u );
    // Floor visibility is independent from world axes and their orientation marker.
    settings.Set( "editor.viewport.perspective.show_axes", true );
    CHECK( view->grab().toImage() != hidden );
    CHECK( MapCameraView_Position( view.get() ).x == camera.x );
    CHECK( MapCameraView_Position( view.get() ).y == camera.y );
    CHECK( MapCameraView_Position( view.get() ).z == camera.z );
    CHECK( session.workspace.gridSize == snap );
}

TEST_CASE( "Coordinate rulers stay at viewport edges without reserving map space", "[map][gui][views][grid-density][rulers]" )
{
    session_t session;
    view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    MapWorkspace_SetGridVisible( &session.workspace, false );
    settings.Set( "editor.viewport.show_axes", false );
    settings.Set( "editor.viewport.center_axes", false );
    settings.Set( "editor.viewport.show_metrics", false );
    for ( const map_ortho_axes_t axes : { map_ortho_axes_t::TOP, map_ortho_axes_t::FRONT, map_ortho_axes_t::SIDE } ) {
        settings.Set( "editor.viewport.show_rulers", false );
        std::unique_ptr<QWidget> view( MapOrthoView_Create( nullptr, &session.workspace, axes ) );
        ShowAt( view.get(), 800, 600 );
        const QImage blank = view->grab().toImage();
        const f64 zoom = MapOrthoView_Zoom( view.get() );
        const QPointF corner = MapOrthoView_ViewToWorld( view.get(), QPointF() );
        settings.Set( "editor.viewport.show_rulers", true );
        const QImage ruler = view->grab().toImage();
        const qreal ratio = ruler.devicePixelRatio();
        const auto physical = [ratio]( QRectF logical ) {
            return QRect( qRound( logical.x() * ratio ), qRound( logical.y() * ratio ),
                          qRound( logical.width() * ratio ), qRound( logical.height() * ratio ) );
        };
        CHECK( ruler.copy( physical( QRectF( 0, 0, 800, 28 ) ) ) != blank.copy( physical( QRectF( 0, 0, 800, 28 ) ) ) );
        CHECK( ruler.copy( physical( QRectF( 0, 30, 90, 570 ) ) ) != blank.copy( physical( QRectF( 0, 30, 90, 570 ) ) ) );
        const QRect interior = physical( QRectF( 110, 35, 690, 565 ) );
        CHECK( ruler.copy( interior ) == blank.copy( interior ) );
        CHECK( MapOrthoView_Zoom( view.get() ) == zoom );
        CHECK( MapOrthoView_ViewToWorld( view.get(), QPointF() ) == corner );
        settings.Set( "editor.viewport.show_rulers", false );
        CHECK( view->grab().toImage() == blank );
    }
}

TEST_CASE( "Camera framing fits actual pane aspect and all bounds corners", "[map][gui][views][visualization]" )
{
    session_t session;
    for ( const QSize size : { QSize( 240, 800 ), QSize( 1200, 240 ), QSize( 640, 480 ) } ) {
        CAPTURE( size.width(), size.height() );
        for ( const math::vec3d_t extent : { math::Vec3d_Make( 1200.0, 800.0, 32.0 ), math::Vec3d_Make( 64.0, 64.0, 2400.0 ) } ) {
            session.workspace.frameBounds = {};
            const math::vec3d_t lo = math::Vec3d_Make( 300.0, -700.0, 140.0 );
            const math::vec3d_t hi = math::Vec3d_Make( lo.x + extent.x, lo.y + extent.y, lo.z + extent.z );
            MapBounds_AddPoint( session.workspace.frameBounds, lo );
            MapBounds_AddPoint( session.workspace.frameBounds, hi );
            std::unique_ptr<QWidget> view( MapCameraView_Create( nullptr, &session.workspace ) );
            ShowAt( view.get(), size.width(), size.height() );
            const math::vec3d_t center = MapBounds_Center( session.workspace.frameBounds );
            CHECK( MapCameraView_Position( view.get() ).z > center.z );
            CHECK( MapCameraView_Forward( view.get() ).z < -0.5 );
            QPointF screen;
            REQUIRE( MapCameraView_WorldToView( view.get(), center, &screen ) );
            CHECK( std::abs( screen.x() - size.width() * 0.5 ) < 1e-6 );
            CHECK( std::abs( screen.y() - size.height() * 0.5 ) < 1e-6 );
            for ( u32 i = 0u; i < 8u; ++i ) {
                const math::vec3d_t corner = math::Vec3d_Make( ( i & 1u ) ? hi.x : lo.x, ( i & 2u ) ? hi.y : lo.y, ( i & 4u ) ? hi.z : lo.z );
                REQUIRE( MapCameraView_WorldToView( view.get(), corner, &screen ) );
                CHECK( screen.x() >= size.width() * 0.04 );
                CHECK( screen.x() <= size.width() * 0.96 );
                CHECK( screen.y() >= size.height() * 0.04 );
                CHECK( screen.y() <= size.height() * 0.96 );
            }
            // Near-clipped or behind-camera points never become overlay labels.
            const math::vec3d_t position = MapCameraView_Position( view.get() );
            REQUIRE_FALSE( MapCameraView_WorldToView( view.get(), position, &screen ) );
            Wheel( view.get() );
            const math::vec3d_t moved = MapCameraView_Position( view.get() );
            view->resize( size.width() + 20, size.height() + 20 );
            QCoreApplication::processEvents();
            CHECK( MapCameraView_Position( view.get() ).x == moved.x );
            CHECK( MapCameraView_Position( view.get() ).z == moved.z );
        }
    }
}

TEST_CASE( "Selection measurements use combined visible authored bounds", "[map][gui][views][visualization]" )
{
    session_t session;
    CHECK_FALSE( MapViews_SelectionGeometryBounds( &session.workspace ).bHas );
    const u64 selection[]{ 1000u, 1001u, 110u };
    MapWorkspace_SetSelection( &session.workspace, selection, 3u );
    const map_bounds_t bounds = MapViews_SelectionGeometryBounds( &session.workspace );
    REQUIRE( bounds.bHas );
    CHECK( bounds.box.maximum.x - bounds.box.minimum.x == 1024.0 );
    CHECK( bounds.box.maximum.y - bounds.box.minimum.y == 1024.0 );
    CHECK( bounds.box.maximum.z - bounds.box.minimum.z == 352.0 );
    MapWorkspace_SetVisgroupHidden( &session.workspace, map_visgroup_t::BRUSHES, CY_TRUE );
    CHECK_FALSE( MapViews_SelectionGeometryBounds( &session.workspace ).bHas );
    MapWorkspace_SetVisgroupHidden( &session.workspace, map_visgroup_t::BRUSHES, CY_FALSE );
    MapWorkspace_Select( &session.workspace, 110u, MAP_SELECT_REPLACE );
    CHECK_FALSE( MapViews_SelectionGeometryBounds( &session.workspace ).bHas );
}

TEST_CASE( "Selection overlays can be disabled live in every projection", "[map][gui][views][visualization]" )
{
    session_t session;
    view_settings_t settings( &session.gui.settings );
    for ( int projection = 0; projection < 4; ++projection ) {
        CAPTURE( projection );
        std::unique_ptr<QWidget> view( projection == 3 ? MapCameraView_Create( nullptr, &session.workspace )
            : MapOrthoView_Create( nullptr, &session.workspace, static_cast<map_ortho_axes_t>( projection ) ) );
        MapWorkspace_Select( &session.workspace, 1000u, MAP_SELECT_REPLACE );
        MapWorkspace_Frame( &session.workspace, CY_TRUE );
        ShowAt( view.get(), 800, 600 );
        settings.Set( projection == 3 ? "editor.viewport.perspective.show_selection_bounds" : "editor.viewport.show_selection_bounds", false );
        settings.Set( projection == 3 ? "editor.viewport.perspective.show_selection_dimensions" : "editor.viewport.show_selection_dimensions", false );
        settings.Set( projection == 3 ? "editor.viewport.perspective.show_selection_vertices" : "editor.viewport.show_selection_vertices", false );
        const QImage plain = view->grab().toImage();
        settings.Set( projection == 3 ? "editor.viewport.perspective.show_selection_dimensions" : "editor.viewport.show_selection_dimensions", true );
        CHECK( view->grab().toImage() != plain );
        settings.Set( projection == 3 ? "editor.viewport.perspective.show_selection_dimensions" : "editor.viewport.show_selection_dimensions", false );
        CHECK( view->grab().toImage() == plain );
        settings.Set( projection == 3 ? "editor.viewport.perspective.show_selection_vertices" : "editor.viewport.show_selection_vertices", true );
        CHECK( view->grab().toImage() != plain );
        settings.Set( projection == 3 ? "editor.viewport.perspective.show_selection_vertices" : "editor.viewport.show_selection_vertices", false );
        MapWorkspace_Select( &session.workspace, 110u, MAP_SELECT_REPLACE );
        const QImage entity = view->grab().toImage();
        settings.Set( projection == 3 ? "editor.viewport.perspective.show_selection_dimensions" : "editor.viewport.show_selection_dimensions", true );
        CHECK( view->grab().toImage() == entity );
    }
}

TEST_CASE( "Entity output overlays follow authored endpoints and display policy", "[map][gui][views][visualization]" )
{
    session_t session;
    view_settings_t settings( &session.gui.settings );
    std::unique_ptr<QWidget> view( MapOrthoView_Create( nullptr, &session.workspace, map_ortho_axes_t::TOP ) );
    ShowAt( view.get(), 800, 600 );
    REQUIRE( session.workspace.wire.connections.nCount >= 4u );
    settings.Choice( "editor.viewport.io_lines", "never" );
    const QImage empty = view->grab().toImage();
    settings.Choice( "editor.viewport.io_lines", "selected" );
    CHECK( view->grab().toImage() == empty );
    settings.Choice( "editor.viewport.io_lines", "always" );
    CHECK( view->grab().toImage() != empty );
    MapWorkspace_Select( &session.workspace, 110u, MAP_SELECT_REPLACE );
    settings.Choice( "editor.viewport.io_lines", "never" );
    const QImage selected = view->grab().toImage();
    settings.Choice( "editor.viewport.io_lines", "selected" );
    CHECK( view->grab().toImage() != selected );
    // 101 targets an originless logic entity. No invented line to (0,0,0).
    MapWorkspace_Select( &session.workspace, 101u, MAP_SELECT_REPLACE );
    const QImage originless = view->grab().toImage();
    settings.Choice( "editor.viewport.io_lines", "never" );
    CHECK( view->grab().toImage() == originless );
}

TEST_CASE( "A 2D view frames the map and draws it", "[map][gui][views]" )
{
    session_t session;
    QWidget *pView = MapOrthoView_Create( nullptr, &session.workspace, map_ortho_axes_t::TOP );
    ShowAt( pView, 800, 600 );

    const map_bounds_t &bounds = session.workspace.wire.bounds;
    REQUIRE( bounds.bHas );
    const QPointF center = MapOrthoView_WorldToView(
        pView, QPointF( ( bounds.box.minimum.x + bounds.box.maximum.x ) * 0.5, ( bounds.box.minimum.y + bounds.box.maximum.y ) * 0.5 ) );
    CHECK( std::fabs( center.x() - 400.0 ) < 1.0 );
    CHECK( std::fabs( center.y() - 300.0 ) < 1.0 );
    // The whole map fits.
    const QPointF lo = MapOrthoView_WorldToView( pView, QPointF( bounds.box.minimum.x, bounds.box.minimum.y ) );
    const QPointF hi = MapOrthoView_WorldToView( pView, QPointF( bounds.box.maximum.x, bounds.box.maximum.y ) );
    CHECK( lo.x() >= 0.0 );
    CHECK( hi.x() <= 800.0 );
    CHECK( hi.y() >= 0.0 );
    CHECK( lo.y() <= 600.0 );
    // Background, grid, axes, and several object colours at least.
    CHECK( DistinctColors( pView ) > 5 );
    delete pView;
}

TEST_CASE( "Zooming keeps the point under the cursor fixed", "[map][gui][views]" )
{
    session_t session;
    QWidget *pView = MapOrthoView_Create( nullptr, &session.workspace, map_ortho_axes_t::FRONT );
    ShowAt( pView, 640, 480 );
    const QPointF anchor( 200.0, 150.0 );
    const QPointF before = MapOrthoView_ViewToWorld( pView, anchor );
    const f64 zoomBefore = MapOrthoView_Zoom( pView );
    QWheelEvent wheel( anchor, pView->mapToGlobal( anchor ), QPoint(), QPoint( 0, 240 ), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false );
    QCoreApplication::sendEvent( pView, &wheel );
    CHECK( MapOrthoView_Zoom( pView ) > zoomBefore );
    const QPointF after = MapOrthoView_ViewToWorld( pView, anchor );
    CHECK( std::fabs( after.x() - before.x() ) < 1e-6 );
    CHECK( std::fabs( after.y() - before.y() ) < 1e-6 );
    delete pView;
}

TEST_CASE( "Clicking an edge in a 2D view selects what the wireframe picks", "[map][gui][views]" )
{
    session_t session;
    QWidget *pView = MapOrthoView_Create( nullptr, &session.workspace, map_ortho_axes_t::TOP );
    ShowAt( pView, 800, 600 );

    // Probe an actual long authored outline a quarter of the way along it.
    // Bounds corners and side midpoints now belong to explicit resize handles.
    const map_wire_object_t *pBrush = nullptr;
    for ( usize i = 0u; i < session.workspace.wire.objects.nCount && pBrush == nullptr; ++i ) {
        const map_wire_object_t &object = session.workspace.wire.objects.pData[i];
        if ( object.kind == map_wire_kind_t::BRUSH && object.owner == 0u ) { pBrush = &object; }
    }
    REQUIRE( pBrush != nullptr );
    const map_wire_line_t *pEdge = nullptr; f64 longestSquared = 0;
    for ( u32 i = 0; i < pBrush->nLines; ++i ) {
        const auto &edge = session.workspace.wire.lines.pData[pBrush->iFirstLine + i];
        const auto a = session.workspace.wire.points.pData[edge.iA], b = session.workspace.wire.points.pData[edge.iB];
        const f64 lengthSquared = ( b.x - a.x ) * ( b.x - a.x ) + ( b.y - a.y ) * ( b.y - a.y );
        if ( lengthSquared > longestSquared ) { longestSquared = lengthSquared; pEdge = &edge; }
    }
    REQUIRE( pEdge != nullptr );
    const auto a = session.workspace.wire.points.pData[pEdge->iA], b = session.workspace.wire.points.pData[pEdge->iB];
    const QPointF world( a.x + ( b.x - a.x ) * 0.25, a.y + ( b.y - a.y ) * 0.25 );
    const QPointF position = MapOrthoView_WorldToView( pView, world );
    const u64 expected = MapWireframe_Pick2D( session.workspace.wire, 0u, 1u, world.x(), world.y(), MAP_VIEW_PICK_PIXELS / MapOrthoView_Zoom( pView ) );
    REQUIRE( expected != 0u );

    Click( pView, position );
    REQUIRE( EditorSelection_Count( &session.workspace.selection ) == 1u );
    CHECK( EditorSelection_At( &session.workspace.selection, 0 ) == expected );
    // Ctrl+click on the same spot toggles it off; a click on empty space clears.
    Click( pView, position, Qt::ControlModifier );
    CHECK( EditorSelection_Count( &session.workspace.selection ) == 0u );
    Click( pView, position );
    Click( pView, QPointF( 2.0, 2.0 ) );
    CHECK( EditorSelection_Count( &session.workspace.selection ) == 0u );
    delete pView;
}

TEST_CASE( "Perspective picking hits authored surfaces and filters hidden nearer geometry", "[map][gui][views][picking]" )
{
    session_t session;
    OnlyVisible( session.workspace, { 1000u, 1001u } );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &session.workspace ) );
    ShowAt( camera.get(), 800, 600 );
    QPointF point;
    REQUIRE( MapCameraView_WorldToView( camera.get(), math::Vec3d_Make( 0, 0, 0 ), &point ) );
    CHECK( MapCameraView_Pick( camera.get(), point ) == 1001u ); // Ceiling is before the floor.
    OnlyVisible( session.workspace, { 1000u } );
    CHECK( MapCameraView_Pick( camera.get(), point ) == 1000u ); // Face center, far from any wire edge.
    Click( camera.get(), point );
    CHECK( MapWorkspace_IsSelected( &session.workspace, 1000u ) );
    // Select now exposes movement controls at the selection center. Toggle
    // the authored surface away from them, keeping this a body-picking test.
    QPointF togglePoint;
    REQUIRE( MapCameraView_WorldToView( camera.get(), math::Vec3d_Make( -256, -256, 0 ), &togglePoint ) );
    REQUIRE( camera->rect().contains( togglePoint.toPoint() ) );
    REQUIRE( MapCameraView_Pick( camera.get(), togglePoint ) == 1000u );
    Click( camera.get(), togglePoint, Qt::ControlModifier );
    CHECK_FALSE( MapWorkspace_IsSelected( &session.workspace, 1000u ) );
    OnlyVisible( session.workspace, { 1200u } );
    REQUIRE( MapCameraView_WorldToView( camera.get(), math::Vec3d_Make( 320, 320, 24 ), &point ) );
    REQUIRE( camera->rect().contains( point.toPoint() ) );
    const auto *ramp = MapWireframe_FindObject( session.workspace.wire, 1200u );
    REQUIRE( ramp != nullptr );
    REQUIRE( MapWorkspace_IsVisible( &session.workspace, *ramp ) );
    // Exercise the authored mesh query independently of screen conversion,
    // making integration failures distinguishable from missed surface picks.
    namespace geo = geometry;
    REQUIRE( session.workspace.pDocument->geometry.meshes.nCount > 0u );
    const auto *mesh = session.workspace.pDocument->geometry.meshes.pData[0];
    REQUIRE( mesh->sourceId.value == 1200u );
    usize bytes = 0u;
    REQUIRE( geo::MeshQueries_TryGetRaycastScratchSize( &mesh->mesh, session.workspace.pDocument->geometryPolicy, &bytes ) == geo::geometry_status_t::OK );
    geo::geometry_scratch_t scratch{};
    geo::geometry_scratch_desc_t desc{};
    desc.pFallbackAllocator = Allocator_GetSystem();
    desc.cbCapacity = bytes;
    desc.cbBudget = 64u * CY_MIB;
    REQUIRE( geo::GeometryScratch_Acquire( &scratch, desc ) == geo::geometry_status_t::OK );
    const auto origin = MapCameraView_Position( camera.get() );
    auto direction = math::Vec3d_Make( 320 - origin.x, 320 - origin.y, 24 - origin.z );
    const f64 length = std::sqrt( direction.x * direction.x + direction.y * direction.y + direction.z * direction.z );
    direction = math::Vec3d_Make( direction.x / length, direction.y / length, direction.z / length );
    geo::mesh_raycast_hit_t hit{};
    const auto query = geo::MeshQueries_TryRaycast( &mesh->mesh, mesh->sourceId, { origin, direction }, {},
        session.workspace.pDocument->geometryPolicy, &scratch, &hit );
    CHECK( geo::GeometryScratch_Release( &scratch ) == geo::geometry_status_t::OK );
    REQUIRE( query == geo::geometry_status_t::OK );
    REQUIRE( hit.bHit );
    CHECK( MapCameraView_Pick( camera.get(), point ) == 1200u ); // Interior of an open sloped quad mesh.
    OnlyVisible( session.workspace, { 110u } );
    const map_wire_entity_t *entity = MapWireframe_FindEntity( session.workspace.wire, 110u );
    REQUIRE( entity != nullptr );
    REQUIRE( MapCameraView_WorldToView( camera.get(), entity->origin, &point ) );
    CHECK( MapCameraView_Pick( camera.get(), point ) == 110u );
    OnlyVisible( session.workspace, {} );
    CHECK( MapCameraView_Pick( camera.get(), point ) == 0u );
}

TEST_CASE( "The object under the pointer draws in the hover colour until the pointer leaves or a button is held", "[map][gui][views][hover]" )
{
    session_t session;
    // Isolate object hover ink from antialiased world-axis pixels. A muted
    // green palette can legitimately blend through the same RGB tolerance.
    REQUIRE( gui::EditorGui_AddTheme( &session.gui, QStringLiteral(
        "@cykv 1\n@schema \"cypher.theme\" 2\n{ id = \"hover_probe\" name = \"Hover probe\" base = \"charcoal\" "
        "colors = { \"viewport.hover\" = \"#ef42ee\" } }" ) ) == gui::editor_gui_status_t::OK );
    REQUIRE( gui::EditorGui_SelectTheme( &session.gui, App(), StringView_FromCString( "hover_probe" ) ) == gui::editor_gui_status_t::OK );
    view_settings_t settings( &session.gui.settings );
    settings.Set( "editor.viewport.active_border", false );
    // Inspect object coloring independently of Select's green control and
    // operation label. Idle Block has no manipulator or construction stage.
    MapWorkspace_SetTool( &session.workspace, map_tool_t::BLOCK );
    OnlyVisible( session.workspace, { 1000u } );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &session.workspace ) );
    ShowAt( camera.get(), 800, 600 );
    QPointF point;
    REQUIRE( MapCameraView_WorldToView( camera.get(), math::Vec3d_Make( 0, 0, 0 ), &point ) );
    const auto move = [&]( Qt::MouseButtons buttons ) {
        QMouseEvent event( QEvent::MouseMove, point, camera->mapToGlobal( point ), Qt::NoButton, buttons, Qt::NoModifier );
        QCoreApplication::sendEvent( camera.get(), &event );
        // The pick is coalesced on a short timer; let it fire.
        QEventLoop wait;
        QTimer::singleShot( 80, &wait, &QEventLoop::quit );
        wait.exec();
    };
    const QColor hover = gui::EditorStyle_TokenColor( session.gui.style, "viewport.hover" );
    const auto hoverPixels = [&]() {
        const QImage image = camera->grab().toImage();
        int count = 0;
        for ( int y = 0; y < image.height(); y += 2 ) {
            for ( int x = 0; x < image.width(); x += 2 ) {
                const QColor pixel = image.pixelColor( x, y );
                count += std::abs( pixel.red() - hover.red() ) < 24 && std::abs( pixel.green() - hover.green() ) < 24 &&
                         std::abs( pixel.blue() - hover.blue() ) < 24 ? 1 : 0;
            }
        }
        return count;
    };
    CHECK( MapView_HoveredObject( camera.get() ) == 0u );
    CHECK( hoverPixels() == 0 );
    move( Qt::NoButton );
    CHECK( MapView_HoveredObject( camera.get() ) == 1000u );
    CHECK( hoverPixels() > 0 );
    // Selection wins: a selected object stays in the selection colour.
    REQUIRE( EditorSelection_Apply( &session.workspace.selection, 1000u, EDITOR_SELECT_REPLACE ) );
    MapWorkspace_Notify( &session.workspace, MAP_CHANGE_SELECTION );
    CHECK( MapView_HoveredObject( camera.get() ) == 1000u );
    CHECK( hoverPixels() == 0 );
    ( void )EditorSelection_Clear( &session.workspace.selection );
    MapWorkspace_Notify( &session.workspace, MAP_CHANGE_SELECTION );
    // Leaving, dragging, and turning the setting off all clear it.
    QEvent leave( QEvent::Leave );
    QCoreApplication::sendEvent( camera.get(), &leave );
    CHECK( MapView_HoveredObject( camera.get() ) == 0u );
    move( Qt::NoButton );
    REQUIRE( MapView_HoveredObject( camera.get() ) == 1000u );
    move( Qt::LeftButton );
    CHECK( MapView_HoveredObject( camera.get() ) == 0u );
    settings.Set( "editor.viewport.hover_highlight", false );
    move( Qt::NoButton );
    CHECK( MapView_HoveredObject( camera.get() ) == 0u );

    // The 2D views highlight the same way.
    settings.Set( "editor.viewport.hover_highlight", true );
    ( void )EditorSelection_Clear( &session.workspace.hidden );
    MapWorkspace_Notify( &session.workspace, MAP_CHANGE_VIEW );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &session.workspace, map_ortho_axes_t::TOP ) );
    ShowAt( top.get(), 800, 600 );
    const map_wire_object_t *pBrush = nullptr;
    for ( usize i = 0u; i < session.workspace.wire.objects.nCount && pBrush == nullptr; ++i ) {
        const map_wire_object_t &object = session.workspace.wire.objects.pData[i];
        if ( object.kind == map_wire_kind_t::BRUSH && object.bounds.bHas ) { pBrush = &object; }
    }
    REQUIRE( pBrush != nullptr );
    const QPointF corner = MapOrthoView_WorldToView( top.get(), QPointF( pBrush->bounds.box.minimum.x, pBrush->bounds.box.minimum.y ) );
    const u64 expected = MapOrthoView_Pick( top.get(), corner );
    REQUIRE( expected != 0u );
    QMouseEvent event( QEvent::MouseMove, corner, top->mapToGlobal( corner ), Qt::NoButton, Qt::NoButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top.get(), &event );
    QEventLoop wait;
    QTimer::singleShot( 80, &wait, &QEventLoop::quit );
    wait.exec();
    CHECK( MapView_HoveredObject( top.get() ) == expected );
}

TEST_CASE( "Camera FOV is shared by projection picking and aspect-aware framing", "[map][gui][views][camera]" )
{
    session_t session;
    view_settings_t settings( &session.gui.settings );
    OnlyVisible( session.workspace, { 1000u } );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &session.workspace ) );
    ShowAt( camera.get(), 800, 600 );
    const math::vec3d_t original = MapCameraView_Position( camera.get() );
    f64 previousRadius = 1.0e9;
    for ( const f64 fov : { 45.0, 75.0, 110.0 } ) {
        CAPTURE( fov );
        settings.Real( "editor.camera.fov", fov );
        QPointF point;
        REQUIRE( MapCameraView_WorldToView( camera.get(), math::Vec3d_Make( -200, 100, 0 ), &point ) );
        REQUIRE( camera->rect().contains( point.toPoint() ) );
        const f64 radius = QLineF( QPointF( 400, 300 ), point ).length();
        CHECK( radius < previousRadius );
        previousRadius = radius;
        CHECK( MapCameraView_Pick( camera.get(), point ) == 1000u );
        CHECK( MapCameraView_Position( camera.get() ).x == original.x );
        CHECK( MapCameraView_Position( camera.get() ).z == original.z );
    }
    for ( const f64 fov : { 25.0, 110.0 } ) {
        settings.Real( "editor.camera.fov", fov );
        for ( const QSize size : { QSize( 300, 800 ), QSize( 1000, 260 ) } ) {
            std::unique_ptr<QWidget> framed( MapCameraView_Create( nullptr, &session.workspace ) );
            ShowAt( framed.get(), size.width(), size.height() );
            const auto &box = session.workspace.frameBounds.box;
            for ( unsigned i = 0; i < 8; ++i ) {
                QPointF point;
                const auto corner = math::Vec3d_Make( ( i & 1 ) ? box.maximum.x : box.minimum.x,
                    ( i & 2 ) ? box.maximum.y : box.minimum.y, ( i & 4 ) ? box.maximum.z : box.minimum.z );
                REQUIRE( MapCameraView_WorldToView( framed.get(), corner, &point ) );
                CHECK( point.x() >= size.width() * 0.04 );
                CHECK( point.x() <= size.width() * 0.96 );
                CHECK( point.y() >= size.height() * 0.04 );
                CHECK( point.y() <= size.height() * 0.96 );
            }
        }
    }
}

TEST_CASE( "Camera flight uses configured velocity and never consumes command chords", "[map][gui][views][camera]" )
{
    session_t session;
    view_settings_t settings( &session.gui.settings );
    MapWorkspace_SetTool( &session.workspace, map_tool_t::CAMERA );
    settings.Real( "editor.camera.move_speed", 350.0 );
    settings.Real( "editor.camera.fast_multiplier", 3.0 );
    settings.Real( "editor.camera.slow_multiplier", 0.2 );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &session.workspace ) );
    ShowAt( camera.get(), 800, 600 );
    const auto key = [&]( int code, bool down, Qt::KeyboardModifiers modifiers = Qt::NoModifier ) {
        QKeyEvent event( down ? QEvent::KeyPress : QEvent::KeyRelease, code, modifiers );
        QCoreApplication::sendEvent( camera.get(), &event );
    };
    const auto speed = [&]( Qt::KeyboardModifiers modifiers = Qt::NoModifier ) {
        const auto v = MapCameraView_NavigationVelocity( camera.get(), modifiers );
        return std::sqrt( v.x * v.x + v.y * v.y + v.z * v.z );
    };
    key( Qt::Key_W, true );
    CHECK( std::abs( speed() - 350.0 ) < 1e-8 );
    CHECK( std::abs( speed( Qt::ShiftModifier ) - 1050.0 ) < 1e-8 );
    CHECK( std::abs( speed( Qt::AltModifier ) - 70.0 ) < 1e-8 );
    CHECK( speed( Qt::ControlModifier ) == 0.0 );
    CHECK( speed( Qt::MetaModifier ) == 0.0 );
    key( Qt::Key_D, true );
    CHECK( std::abs( speed() - 350.0 ) < 1e-8 ); // Diagonals do not accelerate.
    key( Qt::Key_W, false );
    key( Qt::Key_D, false );
    key( Qt::Key_Control, true, Qt::ControlModifier );
    CHECK( speed() == 0.0 ); // Ctrl is never descend.
    key( Qt::Key_S, true, Qt::ControlModifier );
    key( Qt::Key_S, true, Qt::MetaModifier );
    CHECK( speed() == 0.0 );
    key( Qt::Key_Q, true );
    CHECK( MapCameraView_NavigationVelocity( camera.get() ).z == -350.0 );
    key( Qt::Key_Q, false );
    key( Qt::Key_PageUp, true );
    CHECK( MapCameraView_NavigationVelocity( camera.get() ).z == 350.0 );
    key( Qt::Key_PageUp, false );
    key( Qt::Key_Up, true );
    CHECK( std::abs( speed() - 350.0 ) < 1e-8 );
    key( Qt::Key_Up, false );
}

TEST_CASE( "Camera look and both viewport wheels honor live navigation settings", "[map][gui][views][camera]" )
{
    session_t session;
    view_settings_t settings( &session.gui.settings );
    settings.Real( "editor.camera.zoom_sensitivity", 2.0 );
    settings.Set( "editor.camera.zoom_to_cursor", false );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &session.workspace ) );
    ShowAt( camera.get(), 800, 600 );
    const auto before = MapCameraView_Position( camera.get() );
    const auto forward = MapCameraView_Forward( camera.get() );
    Wheel( camera.get() );
    const auto after = MapCameraView_Position( camera.get() );
    CHECK( std::abs( after.x - before.x - forward.x * 256.0 ) < 1e-8 );
    CHECK( std::abs( after.y - before.y - forward.y * 256.0 ) < 1e-8 );
    CHECK( std::abs( after.z - before.z - forward.z * 256.0 ) < 1e-8 );
    settings.Set( "editor.camera.invert_wheel", true );
    Wheel( camera.get() );
    CHECK( std::abs( MapCameraView_Position( camera.get() ).x - before.x ) < 1e-8 );
    settings.Set( "editor.camera.invert_wheel", false );
    settings.Set( "editor.camera.zoom_to_cursor", true );
    Wheel( camera.get() );
    const auto cursorMove = MapCameraView_Position( camera.get() );
    CHECK( std::abs( cursorMove.x - after.x ) > 1.0 );
    settings.Real( "editor.camera.look_sensitivity", 1.0 );
    const QPointF anchor( 300, 250 );
    const auto look = [&]( int dy ) {
        QMouseEvent press( QEvent::MouseButtonPress, anchor, camera->mapToGlobal( anchor ), Qt::RightButton, Qt::RightButton, Qt::NoModifier );
        QCoreApplication::sendEvent( camera.get(), &press );
        const QPointF end = anchor + QPointF( 0, dy );
        QMouseEvent move( QEvent::MouseMove, end, camera->mapToGlobal( end ), Qt::NoButton, Qt::RightButton, Qt::NoModifier );
        QCoreApplication::sendEvent( camera.get(), &move );
        QMouseEvent release( QEvent::MouseButtonRelease, end, camera->mapToGlobal( end ), Qt::RightButton, Qt::NoButton, Qt::NoModifier );
        QCoreApplication::sendEvent( camera.get(), &release );
    };
    const auto facing = MapCameraView_Forward( camera.get() );
    look( 10 );
    CHECK( MapCameraView_Forward( camera.get() ).z < facing.z );
    settings.Set( "editor.camera.invert_y", true );
    look( 10 );
    CHECK( std::abs( MapCameraView_Forward( camera.get() ).z - facing.z ) < 1e-12 );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &session.workspace, map_ortho_axes_t::TOP ) );
    ShowAt( top.get(), 800, 600 );
    settings.Set( "editor.camera.zoom_to_cursor", false );
    const QPointF center = MapOrthoView_ViewToWorld( top.get(), QPointF( 400, 300 ) );
    const f64 initialZoom = MapOrthoView_Zoom( top.get() );
    Wheel( top.get() );
    CHECK( QLineF( center, MapOrthoView_ViewToWorld( top.get(), QPointF( 400, 300 ) ) ).length() < 1e-8 );
    CHECK( MapOrthoView_Zoom( top.get() ) > initialZoom );
    settings.Set( "editor.camera.invert_wheel", true );
    Wheel( top.get() );
    CHECK( std::abs( MapOrthoView_Zoom( top.get() ) - initialZoom ) < 1e-10 );
}

TEST_CASE( "Default camera orbit pan dolly and look navigate without editing geometry or selection", "[map][gui][views][camera][navigation-gestures]" )
{
    enum class action_t { ORBIT, PAN, DOLLY, LOOK };
    const struct { Qt::MouseButton button; Qt::KeyboardModifiers modifiers; bool space; action_t action; } gestures[]{
        { Qt::LeftButton, Qt::AltModifier, false, action_t::ORBIT },
        { Qt::MiddleButton, Qt::NoModifier, false, action_t::PAN },
        { Qt::LeftButton, Qt::NoModifier, true, action_t::PAN },
        { Qt::RightButton, Qt::AltModifier, false, action_t::DOLLY },
        { Qt::MiddleButton, Qt::AltModifier, false, action_t::LOOK },
        { Qt::RightButton, Qt::NoModifier, false, action_t::LOOK },
    };
    for ( const auto &gesture : gestures ) {
        CAPTURE( static_cast<int>( gesture.button ), static_cast<int>( gesture.modifiers ), gesture.space );
        session_t session; auto &ws = session.workspace;
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { 64, -128, 0 } ); MapBounds_AddPoint( box, { 192, -64, 64 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
        const u64 id = EditorSelection_At( &ws.selection, 0 );
        std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
        MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        const math::vec3d_t pivot = MapBounds_Center( box );
        const auto position = MapCameraView_Position( camera.get() ), forward = MapCameraView_Forward( camera.get() );
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision, selectionRevision = ws.selection.revision;
        const usize steps = EditorHistory_StepCount( &ws.history );
        const QPointF start( 400, 300 ), end( 458, 326 );
        if ( gesture.space ) { QKeyEvent event( QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &event ); }
        DragButton( camera.get(), QEvent::MouseButtonPress, start, gesture.button, gesture.modifiers );
        DragButton( camera.get(), QEvent::MouseMove, end, gesture.button, gesture.modifiers );
        DragButton( camera.get(), QEvent::MouseButtonRelease, end, gesture.button, gesture.modifiers );
        if ( gesture.space ) { QKeyEvent event( QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &event ); }
        const auto moved = MapCameraView_Position( camera.get() ), facing = MapCameraView_Forward( camera.get() );
        const math::vec3d_t delta{ moved.x - position.x, moved.y - position.y, moved.z - position.z };
        const f64 forwardDistance = delta.x * forward.x + delta.y * forward.y + delta.z * forward.z;
        if ( gesture.action == action_t::LOOK ) {
            CheckPointClose( moved, position ); CHECK( TestDistance( facing, forward ) > 0.01 );
        } else if ( gesture.action == action_t::PAN ) {
            CheckPointClose( facing, forward ); CHECK( TestDistance( moved, position ) > 0.1 ); CHECK( std::abs( forwardDistance ) < 1e-6 );
        } else if ( gesture.action == action_t::DOLLY ) {
            CheckPointClose( facing, forward ); CHECK( std::abs( forwardDistance ) > 0.1 );
            CheckPointClose( delta, { forward.x * forwardDistance, forward.y * forwardDistance, forward.z * forwardDistance } );
        } else {
            CHECK( TestDistance( moved, position ) > 0.1 ); CHECK( TestDistance( facing, forward ) > 0.01 );
            CHECK( std::abs( TestDistance( moved, pivot ) - TestDistance( position, pivot ) ) < 1e-6 );
            QPointF projected; REQUIRE( MapCameraView_WorldToView( camera.get(), pivot, &projected ) );
            CHECK( QLineF( projected, QPointF( 400, 300 ) ).length() < 1e-6 );
        }
        CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision );
        CHECK( ws.selection.revision == selectionRevision ); CHECK( EditorSelection_Count( &ws.selection ) == 1u ); CHECK( EditorSelection_At( &ws.selection, 0 ) == id );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.tool == map_tool_t::SELECT );
    }
}

TEST_CASE( "Camera orbit retains the selected point entity as its pivot in every editing tool", "[map][gui][views][camera][navigation-gestures][entity-pivot]" )
{
    for ( const auto tool : { map_tool_t::CAMERA, map_tool_t::ROTATE, map_tool_t::CLIP } ) {
        CAPTURE( static_cast<int>( tool ) );
        session_t session; auto &ws = session.workspace;
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        // A distant brush makes the scene center different from the selected
        // entity. Falling back to world bounds would orbit the wrong point.
        map_bounds_t box{}; MapBounds_AddPoint( box, { -768, -512, -64 } ); MapBounds_AddPoint( box, { -640, -384, 64 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
        const math::vec3d_t origin{ 350, -220, 140 };
        u64 id = 0;
        REQUIRE( MapDocument_AddEntity( ws.pDocument, StringView_FromCString( "default" ),
            StringView_FromCString( "info_player_start" ), origin, &id ) == map_status_t::OK );
        MapWorkspace_DocumentChanged( &ws ); MapWorkspace_Select( &ws, id, MAP_SELECT_REPLACE );
        MapWorkspace_SetTool( &ws, tool );
        REQUIRE( ws.tool == tool );
        REQUIRE( EditorSelection_Count( &ws.selection ) == 1u );
        std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
        MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        QPointF projected;
        REQUIRE( MapCameraView_WorldToView( camera.get(), origin, &projected ) );
        REQUIRE( QLineF( projected, QPointF( 400, 300 ) ).length() < 1e-6 );
        const auto position = MapCameraView_Position( camera.get() ), forward = MapCameraView_Forward( camera.get() );
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision, selectionRevision = ws.selection.revision;
        const usize steps = EditorHistory_StepCount( &ws.history );
        const QPointF start( 400, 300 ), end( 458, 326 );
        DragButton( camera.get(), QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::AltModifier );
        DragButton( camera.get(), QEvent::MouseMove, end, Qt::LeftButton, Qt::AltModifier );
        DragButton( camera.get(), QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::AltModifier );
        const auto moved = MapCameraView_Position( camera.get() );
        CHECK( TestDistance( moved, position ) > 0.1 );
        CHECK( TestDistance( MapCameraView_Forward( camera.get() ), forward ) > 0.01 );
        CHECK( std::abs( TestDistance( moved, origin ) - TestDistance( position, origin ) ) < 1e-6 );
        REQUIRE( MapCameraView_WorldToView( camera.get(), origin, &projected ) );
        CHECK( QLineF( projected, QPointF( 400, 300 ) ).length() < 1e-6 );
        CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision );
        CHECK( ws.selection.revision == selectionRevision ); CHECK( EditorSelection_Count( &ws.selection ) == 1u );
        CHECK( EditorSelection_At( &ws.selection, 0 ) == id ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
        CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.tool == tool );
        const auto *entity = MapWireframe_FindEntity( ws.wire, id ); REQUIRE( entity != nullptr ); CheckPointClose( entity->origin, origin );
    }
}

TEST_CASE( "Camera focus loss ends navigation drags and clears held flight and Space state", "[map][gui][views][camera][navigation-gestures][focus]" )
{
    session_t session; auto &ws = session.workspace;
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, 0 } ); MapBounds_AddPoint( box, { 64, 64, 96 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
    const u64 id = EditorSelection_At( &ws.selection, 0 ); const usize steps = EditorHistory_StepCount( &ws.history );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
    const QPointF start( 250, 200 ), end( 290, 230 );
    DragButton( camera.get(), QEvent::MouseButtonPress, start, Qt::RightButton );
    QKeyEvent down( QEvent::KeyPress, Qt::Key_W, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &down );
    REQUIRE( TestDistance( MapCameraView_NavigationVelocity( camera.get() ), {} ) > 0.0 );
    QFocusEvent out( QEvent::FocusOut, Qt::OtherFocusReason ); QCoreApplication::sendEvent( camera.get(), &out );
    CheckPointClose( MapCameraView_NavigationVelocity( camera.get() ), {} );
    const auto position = MapCameraView_Position( camera.get() ), forward = MapCameraView_Forward( camera.get() );
    DragButton( camera.get(), QEvent::MouseMove, end, Qt::RightButton ); DragButton( camera.get(), QEvent::MouseButtonRelease, end, Qt::RightButton );
    CheckPointClose( MapCameraView_Position( camera.get() ), position ); CheckPointClose( MapCameraView_Forward( camera.get() ), forward );
    QKeyEvent space( QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &space );
    DragButton( camera.get(), QEvent::MouseButtonPress, start, Qt::LeftButton );
    QCoreApplication::sendEvent( camera.get(), &out );
    DragButton( camera.get(), QEvent::MouseMove, end, Qt::LeftButton ); DragButton( camera.get(), QEvent::MouseButtonRelease, end, Qt::LeftButton );
    CheckPointClose( MapCameraView_Position( camera.get() ), position ); CheckPointClose( MapCameraView_Forward( camera.get() ), forward );
    // No physical Space release follows focus loss. A fresh plain press in
    // empty space must select normally rather than revive the old pan state.
    Click( camera.get(), QPointF( 12, camera->height() - 12 ) );
    CHECK( EditorSelection_Count( &ws.selection ) == 0u );
    CHECK_FALSE( ws.editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( ws.tool == map_tool_t::SELECT );
    CHECK( MapWireframe_FindObject( ws.wire, id ) != nullptr );
}

TEST_CASE( "Double click inspects objects and opens options only on empty viewport space", "[map][gui][views][picking]" )
{
    session_t session;
    OnlyVisible( session.workspace, { 1000u } );
    int inspections = 0;
    command_desc_t inspect{};
    inspect.pId = "view.properties.open";
    inspect.pLabel = "Inspect Object";
    inspect.pfnExecute = []( void *context, const command_args_t & ) { ++*static_cast<int *>( context ); return command_result_t::OK; };
    inspect.pContext = &inspections;
    REQUIRE( EditorCommands_Register( &session.gui.commands, &inspect, 1u ) == command_registry_status_t::OK );
    std::unique_ptr<QWidget> views( MapViews_Create( nullptr, &session.workspace ) );
    ShowAt( views.get(), 1200, 900 );
    for ( int pane = 0; pane < 4; ++pane ) {
        QWidget *view = MapViews_PaneView( views.get(), pane );
        QPointF point;
        if ( pane == 0 ) { REQUIRE( MapCameraView_WorldToView( view, math::Vec3d_Make( 0, 0, 0 ), &point ) ); }
        else { point = MapOrthoView_WorldToView( view, pane == 1 ? QPointF( 0, -512 ) : QPointF( 0, 0 ) ); }
        DoubleClick( view, point );
        CHECK( inspections == pane + 1 );
        CHECK( MapWorkspace_IsSelected( &session.workspace, 1000u ) );
        auto *menu = views->findChild<QMenu *>( QStringLiteral( "EditorViewOptionsMenu%1" ).arg( pane ) );
        REQUIRE( menu != nullptr );
        CHECK_FALSE( menu->isVisible() );
        DoubleClick( view, QPointF( 5, 5 ) );
        CHECK( menu->isVisible() );
        CHECK( inspections == pane + 1 );
        menu->hide();
    }
}

TEST_CASE( "Orthographic and perspective drawing aids update independently", "[map][gui][views][visualization]" )
{
    session_t session;
    view_settings_t settings( &session.gui.settings );
    OnlyVisible( session.workspace, { 1000u } );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &session.workspace ) );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &session.workspace, map_ortho_axes_t::TOP ) );
    ShowAt( camera.get(), 800, 600 );
    ShowAt( top.get(), 800, 600 );
    const QImage initialCamera = camera->grab().toImage();
    const QImage initialTop = top->grab().toImage();
    MapWorkspace_SetGridVisible( &session.workspace, CY_FALSE );
    settings.Set( "editor.viewport.show_axes", false );
    settings.Set( "editor.viewport.center_axes", false );
    CHECK( camera->grab().toImage() == initialCamera );
    CHECK( top->grab().toImage() != initialTop );
    const QImage alteredTop = top->grab().toImage();
    settings.Set( "editor.grid.show_3d", false );
    settings.Set( "editor.viewport.perspective.show_axes", false );
    settings.Set( "editor.viewport.perspective.center_axes", false );
    CHECK( top->grab().toImage() == alteredTop );
    CHECK( camera->grab().toImage() != initialCamera );
    OnlyVisible( session.workspace, {} );
    const QImage hidden = camera->grab().toImage();
    const QImage topHidden = top->grab().toImage();
    settings.Set( "editor.viewport.perspective.ghost_hidden", true );
    CHECK( camera->grab().toImage() != hidden );
    CHECK( top->grab().toImage() == topHidden );
    QPointF center;
    REQUIRE( MapCameraView_WorldToView( camera.get(), math::Vec3d_Make( 0, 0, 0 ), &center ) );
    CHECK( MapCameraView_Pick( camera.get(), center ) == 0u ); // Ghosts are not selectable.
}

TEST_CASE( "The camera view frames the map in front of it", "[map][gui][views]" )
{
    session_t session;
    QWidget *pView = MapCameraView_Create( nullptr, &session.workspace );
    ShowAt( pView, 640, 480 );
    const math::vec3d_t center = MapBounds_Center( session.workspace.wire.bounds );
    const math::vec3d_t position = MapCameraView_Position( pView );
    const math::vec3d_t forward = MapCameraView_Forward( pView );
    const f64 ahead = ( center.x - position.x ) * forward.x + ( center.y - position.y ) * forward.y + ( center.z - position.z ) * forward.z;
    CHECK( ahead > 0.0 );
    CHECK( DistinctColors( pView ) > 3 );
    delete pView;
}

TEST_CASE( "The four-way arrangement holds the camera and three 2D views", "[map][gui][views]" )
{
    session_t session;
    QWidget *pViews = MapViews_Create( nullptr, &session.workspace );
    ShowAt( pViews, 1200, 800 );
    CHECK( pViews->findChild<QWidget *>( QStringLiteral( "mapCameraView" ) ) != nullptr );
    CHECK( pViews->findChild<QWidget *>( QStringLiteral( "mapTopView" ) ) != nullptr );
    CHECK( pViews->findChild<QWidget *>( QStringLiteral( "mapFrontView" ) ) != nullptr );
    CHECK( pViews->findChild<QWidget *>( QStringLiteral( "mapSideView" ) ) != nullptr );
    delete pViews;
    // Views unsubscribed on destruction; notifying must not touch them.
    MapWorkspace_Notify( &session.workspace, MAP_CHANGE_DOCUMENT );
    CHECK( session.workspace.nListeners == 0u );
}

TEST_CASE( "Closing panes always preserves a recoverable view", "[map][gui][views]" )
{
    session_t session;
    QWidget *pViews = MapViews_Create( nullptr, &session.workspace );
    ShowAt( pViews, 1200, 800 );
    for ( int i = 0; i < MAP_VIEW_PANE_COUNT; ++i ) { MapViews_SetPaneVisible( pViews, i, false ); }
    CHECK( MapViews_IsPaneVisible( pViews, 3 ) );
    CHECK( MapViews_PaneView( pViews, 3 )->isVisibleTo( pViews ) );
    auto *pClose = pViews->findChild<QAction *>( QStringLiteral( "EditorViewCloseAction3" ) );
    REQUIRE( pClose != nullptr );
    CHECK_FALSE( pClose->isEnabled() );

    // Maximizing an explicitly closed pane reopens it, so restoring cannot
    // make the view disappear or leave its header state contradictory.
    MapViews_SetMaximized( pViews, 0 );
    CHECK( MapViews_IsPaneVisible( pViews, 0 ) );
    CHECK( MapViews_PaneView( pViews, 0 )->isVisibleTo( pViews ) );
    MapViews_SetPaneVisible( pViews, 0, false );
    CHECK( MapViews_MaximizedPane( pViews ) == -1 );
    CHECK( MapViews_PaneView( pViews, 3 )->isVisibleTo( pViews ) );
    MapViews_ShowAllPanes( pViews );
    for ( int i = 0; i < MAP_VIEW_PANE_COUNT; ++i ) { CHECK( MapViews_IsPaneVisible( pViews, i ) ); }
    CHECK( pClose->isEnabled() );
    delete pViews;
}

TEST_CASE( "Every view layout of one to four panes places panes as its icon shows", "[map][gui][views][presentation]" )
{
    session_t session;
    std::unique_ptr<QWidget> views( MapViews_Create( nullptr, &session.workspace ) );
    QWidget *pViews = views.get();
    ShowAt( pViews, 1200, 900 );
    QWidget *original[MAP_VIEW_PANE_COUNT]{};
    for ( int i = 0; i < MAP_VIEW_PANE_COUNT; ++i ) { original[i] = MapViews_PaneView( pViews, i ); }
    const auto rect = [&]( int i ) { return QRect( original[i]->mapTo( pViews, QPoint() ), original[i]->size() ); };
    usize nPicker = 0u;
    const map_view_arrangement_t *pPicker = MapViews_PickerArrangements( &nPicker );
    REQUIRE( nPicker == static_cast<usize>( map_view_arrangement_t::COUNT ) );
    for ( usize k = 0u; k < nPicker; ++k ) {
        const map_view_arrangement_t arrangement = pPicker[k];
        INFO( MapViews_ArrangementTitle( arrangement ).toStdString() );
        MapViews_SetArrangement( pViews, arrangement );
        QCoreApplication::processEvents();
        const map_view_arrangement_shape_t shape = MapViews_ArrangementShape( arrangement );
        const int nPanes = MapViews_ArrangementPaneCount( arrangement );
        // Exactly the first nPanes panes show, and they are the same views.
        for ( int i = 0; i < MAP_VIEW_PANE_COUNT; ++i ) {
            CHECK( MapViews_PaneView( pViews, i ) == original[i] );
            CHECK( MapViews_IsPaneVisible( pViews, i ) == ( i < nPanes ) );
        }
        // Panes within a group run across a row (or down a column); groups
        // follow each other the other way.
        for ( int group = 0, first = 0; group < 2; first += shape.counts[group], ++group ) {
            for ( int i = first + 1; i < first + shape.counts[group]; ++i ) {
                if ( shape.bColumns ) {
                    CHECK( rect( i ).top() > rect( i - 1 ).bottom() );
                    CHECK( std::abs( rect( i ).left() - rect( i - 1 ).left() ) <= 2 );
                } else {
                    CHECK( rect( i ).left() > rect( i - 1 ).right() );
                    CHECK( std::abs( rect( i ).top() - rect( i - 1 ).top() ) <= 2 );
                }
            }
        }
        if ( shape.counts[1] != 0 ) {
            const int iSecond = shape.counts[0];
            if ( shape.bColumns ) {
                CHECK( rect( iSecond ).left() > rect( 0 ).right() );
            } else {
                CHECK( rect( iSecond ).top() > rect( 0 ).bottom() );
            }
        }
        // A single pane beside a group is the main view and the larger one.
        if ( shape.counts[0] == 1 && shape.counts[1] > 1 ) {
            CHECK( rect( 0 ).width() * rect( 0 ).height() > rect( 1 ).width() * rect( 1 ).height() * 1.5 );
        }
        CHECK( MapViews_ArrangementIcon( arrangement ) != nullptr );
    }
}

TEST_CASE( "A 2D pane traces over a background image kept per projection", "[map][gui][views][background]" )
{
    session_t session;
    view_settings_t settings( &session.gui.settings );
    settings.Set( "editor.viewport.active_border", false );
    std::unique_ptr<QWidget> views( MapViews_Create( nullptr, &session.workspace ) );
    ShowAt( views.get(), 1200, 900 );
    REQUIRE( MapViews_PaneType( views.get(), 1 ) == map_view_type_t::TOP );
    const auto redPixels = [&]() {
        const QImage image = MapViews_PaneView( views.get(), 1 )->grab().toImage();
        int count = 0;
        for ( int y = 0; y < image.height(); y += 4 ) {
            for ( int x = 0; x < image.width(); x += 4 ) {
                const QColor pixel = image.pixelColor( x, y );
                // The plan's pure red, not the antialiased edge of the red X axis.
                count += pixel.red() > 90 && pixel.green() * 4 < pixel.red() && pixel.blue() * 4 < pixel.red() ? 1 : 0;
            }
        }
        return count;
    };
    CHECK( redPixels() == 0 );
    QImage plan( 64, 64, QImage::Format_RGB32 );
    plan.fill( QColor( 0xFF, 0x00, 0x00 ) );
    // Big enough to cover the framed view whatever its zoom.
    MapViews_SetPaneBackground( views.get(), 1, plan, QRectF( -100000.0, -100000.0, 200000.0, 200000.0 ), 0.5 );
    QCoreApplication::processEvents();
    CHECK( MapViews_PaneHasBackground( views.get(), 1 ) );
    CHECK( redPixels() > 100 );
    // Another projection has its own (none); coming back restores the plan.
    MapViews_SetPaneType( views.get(), 1, map_view_type_t::FRONT );
    QCoreApplication::processEvents();
    CHECK_FALSE( MapViews_PaneHasBackground( views.get(), 1 ) );
    CHECK( redPixels() == 0 );
    MapViews_SetPaneType( views.get(), 1, map_view_type_t::TOP );
    QCoreApplication::processEvents();
    CHECK( MapViews_PaneHasBackground( views.get(), 1 ) );
    CHECK( redPixels() > 100 );
    MapViews_ClearPaneBackground( views.get(), 1 );
    QCoreApplication::processEvents();
    CHECK( redPixels() == 0 );
    // The 3D pane has no background slot.
    MapViews_SetPaneBackground( views.get(), 0, plan, QRectF( 0.0, 0.0, 64.0, 64.0 ) );
    CHECK_FALSE( MapViews_PaneHasBackground( views.get(), 0 ) );
}

TEST_CASE( "Viewport presets change geometry without recreating cameras", "[map][gui][views][presentation]" )
{
    session_t session;
    std::unique_ptr<QWidget> views( MapViews_Create( nullptr, &session.workspace ) );
    QWidget *pViews = views.get();
    ShowAt( pViews, 1200, 900 );
    QWidget *original[MAP_VIEW_PANE_COUNT]{};
    for ( int i = 0; i < MAP_VIEW_PANE_COUNT; ++i ) { original[i] = MapViews_PaneView( pViews, i ); }
    Wheel( original[0] );
    Wheel( original[1] );
    const math::vec3d_t position = MapCameraView_Position( original[0] );
    const f64 zoom = MapOrthoView_Zoom( original[1] );

    // The layout button opens a grid of every arrangement, as icons.
    auto *pPreset = pViews->findChild<QToolButton *>( QStringLiteral( "masonLayoutPreset" ) );
    REQUIRE( pPreset != nullptr );
    int nLayoutButtons = 0;
    for ( const QToolButton *pButton : pViews->findChildren<QToolButton *>() ) {
        nLayoutButtons += pButton->objectName().startsWith( QStringLiteral( "masonLayout_" ) ) ? 1 : 0;
    }
    CHECK( nLayoutButtons == static_cast<int>( map_view_arrangement_t::COUNT ) );
    CHECK( pViews->findChild<QWidget *>( QStringLiteral( "masonSessionToolbar" ) ) != nullptr );
    CHECK( pViews->findChild<QToolButton *>( QStringLiteral( "masonSessionOptions" ) ) != nullptr );
    auto *pHammer = pViews->findChild<QToolButton *>( QStringLiteral( "masonLayout_layout-one-top-two-bottom" ) );
    REQUIRE( pHammer != nullptr );
    pHammer->click();
    QCoreApplication::processEvents();
    CHECK( MapViews_Arrangement( pViews ) == map_view_arrangement_t::HAMMER );
    CHECK( original[0]->width() > pViews->width() * 0.9 );
    CHECK( original[0]->height() > original[1]->height() * 1.7 );
    CHECK( original[1]->mapTo( pViews, QPoint() ).y() == original[2]->mapTo( pViews, QPoint() ).y() );
    CHECK_FALSE( MapViews_IsPaneVisible( pViews, 3 ) );

    MapViews_SetArrangement( pViews, map_view_arrangement_t::PERSPECTIVE );
    QCoreApplication::processEvents();
    CHECK( pViews->findChild<QToolButton *>( QStringLiteral( "masonLayout_layout-single" ) )->isChecked() );
    CHECK_FALSE( pHammer->isChecked() );
    CHECK( original[0]->width() > pViews->width() * 0.9 );
    CHECK( original[0]->height() > pViews->height() * 0.9 );
    MapViews_SetPaneVisible( pViews, 0, false );
    CHECK( MapViews_IsPaneVisible( pViews, 0 ) );

    MapViews_SetArrangement( pViews, map_view_arrangement_t::TWO );
    QCoreApplication::processEvents();
    CHECK( std::abs( original[0]->width() - original[1]->width() ) <= 2 );
    CHECK( original[0]->height() > pViews->height() * 0.9 );
    CHECK_FALSE( MapViews_IsPaneVisible( pViews, 2 ) );
    CHECK_FALSE( MapViews_IsPaneVisible( pViews, 3 ) );

    MapViews_SetArrangement( pViews, map_view_arrangement_t::FOUR );
    QCoreApplication::processEvents();
    CHECK( std::abs( original[0]->width() - original[1]->width() ) <= 2 );
    CHECK( std::abs( original[0]->height() - original[2]->height() ) <= 2 );
    for ( int i = 0; i < MAP_VIEW_PANE_COUNT; ++i ) {
        CHECK( MapViews_PaneView( pViews, i ) == original[i] );
        CHECK( MapViews_IsPaneVisible( pViews, i ) );
    }
    const math::vec3d_t after = MapCameraView_Position( original[0] );
    CHECK( after.x == position.x );
    CHECK( after.y == position.y );
    CHECK( after.z == position.z );
    CHECK( MapOrthoView_Zoom( original[1] ) == zoom );
}

TEST_CASE( "Viewport presentation round-trips types dividers and hidden panes", "[map][gui][views][presentation]" )
{
    session_t session;
    std::unique_ptr<QWidget> views( MapViews_Create( nullptr, &session.workspace ) );
    QWidget *pViews = views.get();
    ShowAt( pViews, 1200, 900 );
    MapViews_SetArrangement( pViews, map_view_arrangement_t::HAMMER );
    MapViews_SetPaneType( pViews, 2, map_view_type_t::SIDE );
    QCoreApplication::processEvents();
    auto *pRows = pViews->findChild<QSplitter *>( QStringLiteral( "EditorViewRows" ) );
    REQUIRE( pRows != nullptr );
    pRows->setSizes( { 600, 300 } );
    auto *pBottom = qobject_cast<QSplitter *>( pRows->widget( 1 ) );
    REQUIRE( pBottom != nullptr );
    pBottom->setSizes( { 300, 900 } );
    QCoreApplication::processEvents();
    const double originalRowRatio = static_cast<double>( pRows->sizes()[0] ) / pRows->sizes()[1];
    const double originalColumnRatio = static_cast<double>( pBottom->sizes()[0] ) / pBottom->sizes()[1];
    QWidget *pCamera = MapViews_PaneView( pViews, 0 );
    Wheel( pCamera );
    const math::vec3d_t camera = MapCameraView_Position( pCamera );
    MapViews_SetMaximized( pViews, 1 );
    const QByteArray state = MapViews_SavePresentation( pViews );
    REQUIRE_FALSE( state.isEmpty() );

    MapViews_SetArrangement( pViews, map_view_arrangement_t::FOUR );
    REQUIRE( MapViews_RestorePresentation( pViews, state ) );
    QCoreApplication::processEvents();
    CHECK( MapViews_Arrangement( pViews ) == map_view_arrangement_t::HAMMER );
    CHECK( MapViews_MaximizedPane( pViews ) == 1 );
    CHECK( MapViews_PaneType( pViews, 2 ) == map_view_type_t::SIDE );
    CHECK_FALSE( MapViews_IsPaneVisible( pViews, 3 ) );
    CHECK( MapViews_PaneView( pViews, 0 ) == pCamera );
    CHECK( MapCameraView_Position( pCamera ).x == camera.x );
    CHECK( MapCameraView_Position( pCamera ).y == camera.y );
    CHECK( MapCameraView_Position( pCamera ).z == camera.z );
    MapViews_SetMaximized( pViews, -1 );
    QCoreApplication::processEvents();
    CHECK( std::fabs( static_cast<double>( pRows->sizes()[0] ) / pRows->sizes()[1] - originalRowRatio ) < 0.03 );
    CHECK( std::fabs( static_cast<double>( pBottom->sizes()[0] ) / pBottom->sizes()[1] - originalColumnRatio ) < 0.03 );

    MapViews_SetPaneVisible( pViews, 2, false );
    const QByteArray closed = MapViews_SavePresentation( pViews );
    MapViews_ShowAllPanes( pViews );
    REQUIRE( MapViews_RestorePresentation( pViews, closed ) );
    CHECK_FALSE( MapViews_IsPaneVisible( pViews, 2 ) );
    CHECK_FALSE( MapViews_IsPaneVisible( pViews, 3 ) );
    // Explicitly reopening a pane not in this preset expands the layout,
    // while keeping all live view objects and manual close flags intact.
    MapViews_SetPaneVisible( pViews, 3, true );
    CHECK( MapViews_Arrangement( pViews ) == map_view_arrangement_t::FOUR );
    CHECK( MapViews_IsPaneVisible( pViews, 3 ) );
    CHECK_FALSE( MapViews_IsPaneVisible( pViews, 2 ) );
}

TEST_CASE( "Invalid viewport presentation leaves the live layout untouched", "[map][gui][views][presentation]" )
{
    session_t session;
    std::unique_ptr<QWidget> views( MapViews_Create( nullptr, &session.workspace ) );
    QWidget *pViews = views.get();
    ShowAt( pViews, 1200, 900 );
    MapViews_SetArrangement( pViews, map_view_arrangement_t::HAMMER );
    QCoreApplication::processEvents();
    const QByteArray before = MapViews_SavePresentation( pViews );
    const QJsonObject valid = QJsonDocument::fromJson( before ).object();
    QList<QByteArray> invalid{ QByteArray(), QByteArray( "{broken" ), QByteArray( 16385, 'x' ) };
    for ( int version : { 0, 3 } ) {
        QJsonObject object = valid;
        object[QStringLiteral( "version" )] = version;
        invalid.append( QJsonDocument( object ).toJson() );
    }
    {
        QJsonObject object = valid;
        object[QStringLiteral( "arrangement" )] = 1.5;
        invalid.append( QJsonDocument( object ).toJson() );
        object = valid;
        object[QStringLiteral( "maximized" )] = 3; // Excluded from Hammer preset.
        invalid.append( QJsonDocument( object ).toJson() );
    }
    {
        QJsonObject object = valid;
        QJsonArray panes = object[QStringLiteral( "panes" )].toArray();
        for ( int i = 0; i < panes.size(); ++i ) {
            QJsonObject pane = panes[i].toObject();
            pane[QStringLiteral( "visible" )] = false;
            panes[i] = pane;
        }
        object[QStringLiteral( "panes" )] = panes;
        invalid.append( QJsonDocument( object ).toJson() );
        object = valid;
        QJsonArray sizes = object[QStringLiteral( "splitters" )].toArray();
        sizes[0] = QJsonArray{ 0, 10 };
        object[QStringLiteral( "splitters" )] = sizes;
        invalid.append( QJsonDocument( object ).toJson() );
    }
    for ( const QByteArray &state : invalid ) {
        CHECK_FALSE( MapViews_RestorePresentation( pViews, state ) );
        CHECK( MapViews_SavePresentation( pViews ) == before );
    }
}

TEST_CASE( "A pane's frame control preserves all other view positions", "[map][gui][views]" )
{
    session_t session;
    QWidget *pViews = MapViews_Create( nullptr, &session.workspace );
    ShowAt( pViews, 1200, 800 );
    QWidget *pCamera = MapViews_PaneView( pViews, 0 );
    QWidget *pTop = MapViews_PaneView( pViews, 1 );
    QWidget *pFront = MapViews_PaneView( pViews, 2 );
    Wheel( pCamera );
    Wheel( pTop );
    Wheel( pFront );
    const math::vec3d_t cameraBefore = MapCameraView_Position( pCamera );
    const QPointF frontBefore = MapOrthoView_ViewToWorld( pFront, QPointF( 0.0, 0.0 ) );
    const f64 frontZoom = MapOrthoView_Zoom( pFront );
    const f64 topZoom = MapOrthoView_Zoom( pTop );
    const map_wire_object_t *pSelected = nullptr;
    for ( usize i = 0u; i < session.workspace.wire.objects.nCount && pSelected == nullptr; ++i ) {
        if ( session.workspace.wire.objects.pData[i].bounds.bHas ) { pSelected = &session.workspace.wire.objects.pData[i]; }
    }
    REQUIRE( pSelected != nullptr );
    MapWorkspace_Select( &session.workspace, pSelected->id, MAP_SELECT_REPLACE );
    auto *pFrame = pViews->findChild<QAction *>( QStringLiteral( "EditorViewFrameAction1" ) );
    REQUIRE( pFrame != nullptr );
    pFrame->trigger();
    QCoreApplication::processEvents();
    const math::vec3d_t center = MapBounds_Center( pSelected->bounds );
    const QPointF topCenter = MapOrthoView_WorldToView( pTop, QPointF( center.x, center.y ) );
    CHECK( std::fabs( topCenter.x() - pTop->width() * 0.5 ) < 1e-6 );
    CHECK( std::fabs( topCenter.y() - pTop->height() * 0.5 ) < 1e-6 );
    CHECK( MapOrthoView_Zoom( pTop ) != topZoom );
    CHECK( MapOrthoView_ViewToWorld( pFront, QPointF( 0.0, 0.0 ) ) == frontBefore );
    CHECK( MapOrthoView_Zoom( pFront ) == frontZoom );
    const math::vec3d_t cameraAfter = MapCameraView_Position( pCamera );
    CHECK( cameraAfter.x == cameraBefore.x );
    CHECK( cameraAfter.y == cameraBefore.y );
    CHECK( cameraAfter.z == cameraBefore.z );
    const QPointF topBeforeCameraFrame = MapOrthoView_ViewToWorld( pTop, QPointF( 0.0, 0.0 ) );
    MapViews_FramePane( pViews, 0 );
    QCoreApplication::processEvents();
    CHECK( MapOrthoView_ViewToWorld( pTop, QPointF( 0.0, 0.0 ) ) == topBeforeCameraFrame );
    CHECK( MapOrthoView_ViewToWorld( pFront, QPointF( 0.0, 0.0 ) ) == frontBefore );
    delete pViews;
}

TEST_CASE( "An owner's pane frame includes its visible children and preserves other panes", "[map][gui][views][presentation][owner-frame][shared-transform]" )
{
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -384, -96, 0 } ); MapBounds_AddPoint( box, { -64, 96, 192 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 brush = EditorSelection_At( &ws.selection, 0 );
    u64 owner = 0, unrelated = 0;
    REQUIRE( MapDocument_AddEntity( ws.pDocument, StringView_FromCString( "default" ), StringView_FromCString( "func_door" ), { 512, 256, 0 }, &owner ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryOwner( ws.pDocument, brush, owner ) == map_status_t::OK );
    REQUIRE( MapDocument_AddEntity( ws.pDocument, StringView_FromCString( "default" ), StringView_FromCString( "info_player_start" ), { 4096, -4096, 0 }, &unrelated ) == map_status_t::OK );
    MapWorkspace_DocumentChanged( &ws ); MapWorkspace_Select( &ws, owner, MAP_SELECT_REPLACE );
    const auto *entity = MapWireframe_FindObject( ws.wire, owner ); REQUIRE( entity != nullptr );
    map_bounds_t expected = entity->bounds; MapBounds_AddBounds( expected, box ); const auto center = MapBounds_Center( expected );
    std::unique_ptr<QWidget> views( MapViews_Create( nullptr, &ws ) ); ShowAt( views.get(), 1200, 800 );
    auto *camera = MapViews_PaneView( views.get(), 0 ), *top = MapViews_PaneView( views.get(), 1 ), *front = MapViews_PaneView( views.get(), 2 );
    Wheel( camera ); Wheel( top ); Wheel( front );
    const auto cameraBefore = MapCameraView_Position( camera ); const auto frontBefore = MapOrthoView_ViewToWorld( front, {} );
    const f64 frontZoom = MapOrthoView_Zoom( front ); const auto revision = ws.pDocument->geometry.revision, selectionRevision = ws.selection.revision;
    const usize steps = EditorHistory_StepCount( &ws.history );
    MapViews_FramePane( views.get(), 1 ); QCoreApplication::processEvents();
    const QPointF topCenter = MapOrthoView_WorldToView( top, { center.x, center.y } );
    CHECK( std::abs( topCenter.x() - top->width() * 0.5 ) < 1e-6 ); CHECK( std::abs( topCenter.y() - top->height() * 0.5 ) < 1e-6 );
    CHECK( MapOrthoView_ViewToWorld( front, {} ) == frontBefore ); CHECK( MapOrthoView_Zoom( front ) == frontZoom );
    CheckPointClose( MapCameraView_Position( camera ), cameraBefore );
    for ( const QPointF point : { QPointF( box.box.minimum.x, box.box.minimum.y ), QPointF( box.box.maximum.x, box.box.maximum.y ) } ) {
        CHECK( top->rect().adjusted( 2, 2, -2, -2 ).contains( MapOrthoView_WorldToView( top, point ).toPoint() ) );
    }
    const auto topBefore = MapOrthoView_ViewToWorld( top, {} ); const f64 topZoom = MapOrthoView_Zoom( top );
    MapViews_FramePane( views.get(), 0 ); QCoreApplication::processEvents();
    CHECK( MapOrthoView_ViewToWorld( top, {} ) == topBefore ); CHECK( MapOrthoView_Zoom( top ) == topZoom );
    CHECK( MapOrthoView_ViewToWorld( front, {} ) == frontBefore ); CHECK( MapOrthoView_Zoom( front ) == frontZoom );
    for ( const auto point : ObjectLineVertices( ws.wire, brush ) ) {
        QPointF screen; REQUIRE( MapCameraView_WorldToView( camera, point, &screen ) );
        CHECK( camera->rect().adjusted( 2, 2, -2, -2 ).contains( screen.toPoint() ) );
    }
    CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.selection.revision == selectionRevision ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    CHECK( EditorSelection_Count( &ws.selection ) == 1u ); CHECK( EditorSelection_At( &ws.selection, 0 ) == owner );
    CHECK_FALSE( MapWorkspace_IsSelected( &ws, brush ) ); CHECK_FALSE( MapWorkspace_IsSelected( &ws, unrelated ) );
}

TEST_CASE( "Hover activates orthographic Space panning without moving other views", "[map][gui][views]" )
{
    session_t session;
    QWidget window;
    auto *pLayout = new QHBoxLayout( &window );
    QWidget *pFirst = MapOrthoView_Create( &window, &session.workspace, map_ortho_axes_t::TOP );
    QWidget *pSecond = MapOrthoView_Create( &window, &session.workspace, map_ortho_axes_t::FRONT );
    pLayout->addWidget( pFirst );
    pLayout->addWidget( pSecond );
    ShowAt( &window, 1000, 600 );
    window.activateWindow();
    pFirst->setFocus();
    QCoreApplication::processEvents();
    REQUIRE( pFirst->hasFocus() );
    const QPointF firstBefore = MapOrthoView_ViewToWorld( pFirst, QPointF( 0.0, 0.0 ) );
    const QPointF secondBefore = MapOrthoView_ViewToWorld( pSecond, QPointF( 0.0, 0.0 ) );
    Enter( pSecond );
    REQUIRE( pSecond->hasFocus() );
    QKeyEvent spaceDown( QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier );
    QCoreApplication::sendEvent( QApplication::focusWidget(), &spaceDown );
    const QPointF start( 100.0, 100.0 );
    const QPointF end( 180.0, 145.0 );
    QMouseEvent press( QEvent::MouseButtonPress, start, pSecond->mapToGlobal( start ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( pSecond, &press );
    QMouseEvent move( QEvent::MouseMove, end, pSecond->mapToGlobal( end ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( pSecond, &move );
    QMouseEvent release( QEvent::MouseButtonRelease, end, pSecond->mapToGlobal( end ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
    QCoreApplication::sendEvent( pSecond, &release );
    QKeyEvent spaceUp( QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier );
    QCoreApplication::sendEvent( pSecond, &spaceUp );
    CHECK( MapOrthoView_ViewToWorld( pSecond, QPointF( 0.0, 0.0 ) ) != secondBefore );
    CHECK( MapOrthoView_ViewToWorld( pFirst, QPointF( 0.0, 0.0 ) ) == firstBefore );
    CHECK( EditorSelection_Count( &session.workspace.selection ) == 0u );
}

TEST_CASE( "Hover activates the camera tool and reserves navigation keys without taking a drag", "[map][gui][views][hover]" )
{
    session_t session;
    MapWorkspace_SetTool( &session.workspace, map_tool_t::CAMERA );
    view_settings_t settings( &session.gui.settings );
    settings.Set( "editor.viewport.activate_on_hover", true );
    std::unique_ptr<QWidget> views( MapViews_Create( nullptr, &session.workspace ) );
    ShowAt( views.get(), 1200, 900 );
    views->activateWindow();
    QWidget *camera = MapViews_PaneView( views.get(), 0 );
    QWidget *top = MapViews_PaneView( views.get(), 1 );
    top->setFocus();
    QCoreApplication::processEvents();
    Enter( camera );
    REQUIRE( camera->hasFocus() );
    auto *header = camera->parentWidget()->findChild<QWidget *>( QStringLiteral( "EditorViewHeader" ), Qt::FindDirectChildrenOnly );
    REQUIRE( header != nullptr );
    CHECK( header->property( "active" ).toBool() );
    QKeyEvent shortcut( QEvent::ShortcutOverride, Qt::Key_W, Qt::NoModifier );
    shortcut.ignore();
    QCoreApplication::sendEvent( camera, &shortcut );
    CHECK( shortcut.isAccepted() );
    const math::vec3d_t before = MapCameraView_Position( camera );
    QKeyEvent down( QEvent::KeyPress, Qt::Key_W, Qt::NoModifier );
    QCoreApplication::sendEvent( QApplication::focusWidget(), &down );
    QEventLoop movement;
    QTimer::singleShot( 45, &movement, &QEventLoop::quit );
    movement.exec();
    QKeyEvent up( QEvent::KeyRelease, Qt::Key_W, Qt::NoModifier );
    QCoreApplication::sendEvent( camera, &up );
    CHECK( MapCameraView_Position( camera ).x != before.x );
    const QPointF position( 30, 40 );
    QMouseEvent dragOver( QEvent::MouseMove, position, top->mapToGlobal( position ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &dragOver );
    CHECK( camera->hasFocus() );
    // Retry activation on movement: entering while a popup/drag was active
    // must not require a second exit/re-entry or a click to acquire focus.
    QMouseEvent hover( QEvent::MouseMove, position, top->mapToGlobal( position ), Qt::NoButton, Qt::NoButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &hover );
    CHECK( top->hasFocus() );
    CHECK_FALSE( header->property( "active" ).toBool() );
}

TEST_CASE( "Hover navigation respects text editors and modal dialogs", "[map][gui][views]" )
{
    session_t session;
    QWidget window;
    auto *pLayout = new QHBoxLayout( &window );
    QWidget *pView = MapOrthoView_Create( &window, &session.workspace, map_ortho_axes_t::TOP );
    auto *pInput = new QLineEdit( &window );
    pLayout->addWidget( pView );
    pLayout->addWidget( pInput );
    ShowAt( &window, 1000, 600 );
    window.activateWindow();
    pInput->setFocus();
    QCoreApplication::processEvents();
    REQUIRE( pInput->hasFocus() );
    Enter( pView );
    CHECK( pInput->hasFocus() );
    CHECK_FALSE( pView->hasFocus() );
    QDialog dialog( &window );
    dialog.setWindowModality( Qt::ApplicationModal );
    dialog.show();
    QCoreApplication::processEvents();
    REQUIRE( QApplication::activeModalWidget() == &dialog );
    Enter( pView );
    CHECK_FALSE( pView->hasFocus() );
    dialog.close();
}

TEST_CASE( "The outliner lists every object and follows the selection", "[map][gui][panels]" )
{
    session_t session;
    QWidget *pOutliner = MapOutliner_Create( nullptr, &session.workspace );
    ShowAt( pOutliner, 320, 600 );
    const map_wireframe_t &wire = session.workspace.wire;
    usize nGeometry = 0u;
    for ( usize i = 0u; i < wire.objects.nCount; ++i ) { nGeometry += wire.objects.pData[i].kind != map_wire_kind_t::ENTITY ? 1u : 0u; }
    const int nExpected = static_cast<int>( session.workspace.pDocument->layers.nCount + wire.entities.nCount + nGeometry );
    CHECK( MapOutliner_VisibleRowCount( pOutliner ) == nExpected );

    MapOutliner_SetFilter( pOutliner, QStringLiteral( "info_player_start" ) );
    const int nFiltered = MapOutliner_VisibleRowCount( pOutliner );
    CHECK( nFiltered > 0 );
    CHECK( nFiltered < nExpected );
    MapOutliner_SetFilter( pOutliner, QString() );
    CHECK( MapOutliner_VisibleRowCount( pOutliner ) == nExpected );
    delete pOutliner;
}

TEST_CASE( "Viewport icons follow live theme changes without resetting pane state", "[map][gui][views][view-menus][theme]" )
{
    session_t session;
    const usize originalListeners = session.gui.nStyleListeners;
    auto views = std::unique_ptr<QWidget>( MapViews_Create( nullptr, &session.workspace ) );
    ShowAt( views.get(), 1000, 700 );
    CHECK( session.gui.nStyleListeners == originalListeners + 1u );
    MapViews_SetMaximized( views.get(), 0 );
    QCoreApplication::processEvents();
    QWidget *camera = MapViews_PaneView( views.get(), 0 );
    const math::vec3d_t position = MapCameraView_Position( camera );
    const math::vec3d_t direction = MapCameraView_Forward( camera );
    auto *maximize = views->findChild<QAction *>( QStringLiteral( "EditorViewMaximizeAction0" ) );
    auto *grid = views->findChild<QAction *>( QStringLiteral( "EditorViewShowGrid" ) );
    auto *showAll = views->findChild<QAction *>( QStringLiteral( "EditorViewShowAll" ) );
    auto *frame = views->findChild<QAction *>( QStringLiteral( "EditorViewFrameAction0" ) );
    auto *projection = views->findChild<QAction *>( QStringLiteral( "EditorViewProjection0_0" ) );
    REQUIRE( maximize != nullptr );
    REQUIRE( grid != nullptr );
    REQUIRE( showAll != nullptr );
    REQUIRE( frame != nullptr );
    REQUIRE( projection != nullptr );
    REQUIRE( maximize->isChecked() );
    const auto pixels = []( QAction *action, QIcon::State state = QIcon::Off ) {
        return action->icon().pixmap( QSize( 24, 24 ), QIcon::Normal, state ).toImage();
    };
    const QImage beforeMax = pixels( maximize, QIcon::On );
    const QImage beforeGrid = pixels( grid );
    const QImage beforeAll = pixels( showAll );
    const QImage beforeFrame = pixels( frame );
    const QImage beforeProjection = pixels( projection );
    REQUIRE( gui::EditorGui_AddTheme( &session.gui, QStringLiteral(
        "@cykv 1\n@schema \"cypher.theme\" 2\n{ id = \"view_test\" name = \"View test\" base = \"charcoal\"\n"
        "colors = { \"ui.accent\" = \"#00ffff\" \"ui.text.muted\" = \"#ef349e\" }\n"
        "metrics = { \"ui.icon.color_strength\" = 0.0 } }\n" ) ) == gui::editor_gui_status_t::OK ); // Neutral icons: art that follows the theme.
    REQUIRE( gui::EditorGui_SelectTheme( &session.gui, App(), StringView_FromCString( "view_test" ) ) == gui::editor_gui_status_t::OK );
    CHECK( pixels( maximize, QIcon::On ) != beforeMax );
    CHECK( pixels( grid ) != beforeGrid );
    CHECK( pixels( showAll ) != beforeAll );
    CHECK( pixels( frame ) != beforeFrame );
    CHECK( pixels( projection ) != beforeProjection );
    CHECK( maximize->isChecked() );
    CHECK( MapViews_MaximizedPane( views.get() ) == 0 );
    CHECK( MapViews_PaneView( views.get(), 0 ) == camera );
    const math::vec3d_t afterPosition = MapCameraView_Position( camera );
    const math::vec3d_t afterDirection = MapCameraView_Forward( camera );
    CHECK( afterPosition.x == position.x );
    CHECK( afterPosition.y == position.y );
    CHECK( afterPosition.z == position.z );
    CHECK( afterDirection.x == direction.x );
    CHECK( afterDirection.y == direction.y );
    CHECK( afterDirection.z == direction.z );
    views.reset();
    CHECK( session.gui.nStyleListeners == originalListeners );
    // A later theme change cannot call back into a deleted view workspace.
    REQUIRE( gui::EditorGui_SelectTheme( &session.gui, App(), StringView_FromCString( "charcoal" ) ) == gui::editor_gui_status_t::OK );
}

TEST_CASE( "The properties panel shows the map, or the selected object's record", "[map][gui][panels]" )
{
    session_t session;
    QWidget *pProperties = MapProperties_Create( nullptr, &session.workspace );
    ShowAt( pProperties, 320, 600 );
    CHECK( MapProperties_Text( pProperties ).startsWith( QStringLiteral( "Map: facility.cymap" ) ) );
    CHECK( MapProperties_Text( pProperties ).contains( QStringLiteral( "game = reap" ) ) );

    REQUIRE( session.workspace.wire.entities.nCount != 0u );
    const map_wire_entity_t &entity = session.workspace.wire.entities.pData[0];
    MapWorkspace_Select( &session.workspace, entity.id, MAP_SELECT_REPLACE );
    const QString text = MapProperties_Text( pProperties );
    CHECK( text.contains( QStringLiteral( "class = %1" ).arg( QString::fromUtf8( entity.className ) ) ) );
    CHECK( text.contains( QStringLiteral( "(chunk) = " ) ) );

    u64 other = 0u;
    for ( usize i = 0u; i < session.workspace.wire.objects.nCount && other == 0u; ++i ) {
        if ( session.workspace.wire.objects.pData[i].id != entity.id ) { other = session.workspace.wire.objects.pData[i].id; }
    }
    const u64 two[]{ entity.id, other };
    MapWorkspace_SetSelection( &session.workspace, two, 2u );
    CHECK( MapProperties_Text( pProperties ).startsWith( QStringLiteral( "2 objects selected" ) ) );
    delete pProperties;
}

TEST_CASE( "Selection summaries measure geometry across multiple objects", "[map][gui][panels][selection-summary]" )
{
    session_t session;
    std::unique_ptr<QWidget> properties( MapProperties_Create( nullptr, &session.workspace ) );
    QLabel *pSummary = properties->findChild<QLabel *>( QStringLiteral( "MapSelectionSummary" ) );
    REQUIRE( pSummary != nullptr );
    CHECK( pSummary->isHidden() );

    // The example's floor spans (-512,-512,-32) to (512,512,0).
    REQUIRE( MapWireframe_FindObject( session.workspace.wire, 1000u ) != nullptr );
    MapWorkspace_Select( &session.workspace, 1000u, MAP_SELECT_REPLACE );
    CHECK_FALSE( pSummary->isHidden() );
    CHECK( pSummary->text().startsWith( QStringLiteral( "1 brush\n" ) ) );
    CHECK( pSummary->text().contains( QStringLiteral( "Size  X 1024 \u00B7 Y 1024 \u00B7 Z 32 u" ) ) );
    CHECK( pSummary->toolTip().contains( QStringLiteral( "[-512, -512, -32] to [512, 512, 0]" ) ) );
    // The CYKV metadata stays visible. Geometry-owned fields (including
    // face materials) are lifted out of the record when the map is loaded.
    CHECK( MapProperties_Text( properties.get() ).contains( QStringLiteral( "name = floor" ) ) );
    CHECK( MapProperties_Text( properties.get() ).contains( QStringLiteral( "id = 1000" ) ) );

    // The ceiling raises the combined maximum to 320; the readout must
    // describe the enclosing bounds, not sum the two slab thicknesses.
    const u64 slabs[]{ 1000u, 1001u };
    MapWorkspace_SetSelection( &session.workspace, slabs, 2u );
    CHECK( MapProperties_Text( properties.get() ).startsWith( QStringLiteral( "2 objects selected" ) ) );
    CHECK( pSummary->text().startsWith( QStringLiteral( "2 brushes\n" ) ) );
    CHECK( pSummary->text().contains( QStringLiteral( "Z 352 u" ) ) );
    CHECK( MapProperties_Text( properties.get() ).contains( QStringLiteral( "(bounds) = [-512, -512, -32] to [512, 512, 320]" ) ) );

    // Internal record rows can be hidden without losing the modeling readout.
    QAction *pInternal = nullptr;
    for ( QAction *pAction : properties->findChildren<QAction *>() ) {
        if ( pAction->text().startsWith( QStringLiteral( "Show Editor Rows" ) ) ) { pInternal = pAction; }
    }
    REQUIRE( pInternal != nullptr );
    pInternal->setChecked( false );
    CHECK_FALSE( MapProperties_Text( properties.get() ).contains( QStringLiteral( "(bounds) =" ) ) );
    CHECK( pSummary->text().contains( QStringLiteral( "Z 352 u" ) ) );

    // A mixed geometry selection gives a useful kind breakdown.
    const u64 mixed[]{ 1000u, 1200u };
    REQUIRE( MapWireframe_FindObject( session.workspace.wire, 1200u ) != nullptr );
    MapWorkspace_SetSelection( &session.workspace, mixed, 2u );
    CHECK( pSummary->text().startsWith( QStringLiteral( "1 brush \u00B7 1 mesh\n" ) ) );
    CHECK( pSummary->text().contains( QStringLiteral( "Z 80 u" ) ) );
}

TEST_CASE( "Selection summaries exclude entity helper boxes and clear stale dimensions", "[map][gui][panels][selection-summary]" )
{
    session_t session;
    std::unique_ptr<QWidget> properties( MapProperties_Create( nullptr, &session.workspace ) );
    QLabel *pSummary = properties->findChild<QLabel *>( QStringLiteral( "MapSelectionSummary" ) );
    REQUIRE( pSummary != nullptr );
    REQUIRE( session.workspace.wire.entities.nCount != 0u );
    const u64 entity = session.workspace.wire.entities.pData[0].id;
    const u64 mixed[]{ 1000u, entity };
    MapWorkspace_SetSelection( &session.workspace, mixed, 2u );
    CHECK( pSummary->text().startsWith( QStringLiteral( "1 brush \u00B7 1 entity\n" ) ) );
    CHECK( pSummary->text().contains( QStringLiteral( "Z 32 u" ) ) );
    CHECK( pSummary->toolTip().contains( QStringLiteral( "[-512, -512, -32] to [512, 512, 0]" ) ) );

    MapWorkspace_Select( &session.workspace, entity, MAP_SELECT_REPLACE );
    CHECK( pSummary->text() == QStringLiteral( "1 entity" ) );
    CHECK( pSummary->toolTip().isEmpty() );
    CHECK_FALSE( MapProperties_Text( properties.get() ).contains( QStringLiteral( "Size  X" ) ) );
    CHECK( MapProperties_Text( properties.get() ).contains( QStringLiteral( "class = " ) ) );

    MapWorkspace_SetSelection( &session.workspace, nullptr, 0u );
    CHECK( pSummary->isHidden() );
    CHECK( pSummary->text().isEmpty() );
    CHECK( MapProperties_Text( properties.get() ).startsWith( QStringLiteral( "Map: facility.cymap" ) ) );

    // Document replacement must clear the previous selection readout too.
    MapWorkspace_Select( &session.workspace, 1000u, MAP_SELECT_REPLACE );
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    CHECK( pSummary->isHidden() );
    CHECK( pSummary->text().isEmpty() );
    CHECK( pSummary->toolTip().isEmpty() );
}

TEST_CASE( "Box drag previews without mutating and commits one undoable snapped brush", "[map][gui][views][geometry-edit]" )
{
    session_t session;
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    MapWorkspace_SetGridSize( &session.workspace, 16 );
    MapWorkspace_SetTool( &session.workspace, map_tool_t::BLOCK );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &session.workspace, map_ortho_axes_t::TOP ) );
    ShowAt( top.get(), 800, 600 );
    const QPointF start = MapOrthoView_WorldToView( top.get(), { -64, -64 } );
    const QPointF end = MapOrthoView_WorldToView( top.get(), { 64, 64 } );
    QMouseEvent press( QEvent::MouseButtonPress, start, top->mapToGlobal( start ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top.get(), &press );
    QMouseEvent move( QEvent::MouseMove, end, top->mapToGlobal( end ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top.get(), &move );
    CHECK( session.workspace.editPreview.bActive );
    CHECK( session.workspace.pDocument->geometry.brushes.nCount == 0 );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == 0 );
    QMouseEvent release( QEvent::MouseButtonRelease, end, top->mapToGlobal( end ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top.get(), &release );
    REQUIRE( MapWorkspace_HasBlockPreview( &session.workspace ) );
    CHECK( session.workspace.pDocument->geometry.brushes.nCount == 0 );
    QKeyEvent confirm( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier ); QCoreApplication::sendEvent( top.get(), &confirm );
    REQUIRE( session.workspace.pDocument->geometry.brushes.nCount == 1 );
    CHECK_FALSE( session.workspace.editPreview.bActive );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == 1 );
    const auto bounds = MapViews_SelectionGeometryBounds( &session.workspace );
    CHECK( bounds.box.minimum.x == -64 ); CHECK( bounds.box.maximum.y == 64 ); CHECK( bounds.box.maximum.z == 64 );
    const auto selected = EditorSelection_At( &session.workspace.selection, 0 );
    REQUIRE( MapWorkspace_Undo( &session.workspace ) == editor_history_status_t::OK );
    CHECK( session.workspace.pDocument->geometry.brushes.nCount == 0 );
    CHECK_FALSE( MapWorkspace_IsModified( &session.workspace ) );
    REQUIRE( MapWorkspace_Redo( &session.workspace ) == editor_history_status_t::OK );
    CHECK( EditorSelection_At( &session.workspace.selection, 0 ) == selected );

    // Cancel an in-progress creation, including its later release event.
    QCoreApplication::sendEvent( top.get(), &press ); QCoreApplication::sendEvent( top.get(), &move );
    QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( top.get(), &escape );
    QCoreApplication::sendEvent( top.get(), &release );
    CHECK( session.workspace.pDocument->geometry.brushes.nCount == 1 );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == 1 );
}

TEST_CASE( "Viewport translation uses a private preview and Shift drag clones with undo", "[map][gui][views][geometry-edit]" )
{
    session_t session;
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, 0 } ); MapBounds_AddPoint( box, { 64, 64, 64 } );
    REQUIRE( MapWorkspace_CreateBox( &session.workspace, box ) );
    MapWorkspace_SetTool( &session.workspace, map_tool_t::TRANSLATE );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &session.workspace, map_ortho_axes_t::TOP ) ); ShowAt( top.get(), 800, 600 );
    const QPointF start = MapOrthoView_WorldToView( top.get(), { -64, -64 } );
    const QPointF end = MapOrthoView_WorldToView( top.get(), { -32, -16 } );
    const auto original = EditorSelection_At( &session.workspace.selection, 0 );
    const auto drag = [&]( bool clone ) {
        const auto mods = clone ? Qt::ShiftModifier : Qt::NoModifier;
        QMouseEvent press( QEvent::MouseButtonPress, start, top->mapToGlobal( start ), Qt::LeftButton, Qt::LeftButton, mods ); QCoreApplication::sendEvent( top.get(), &press );
        QMouseEvent move( QEvent::MouseMove, end, top->mapToGlobal( end ), Qt::NoButton, Qt::LeftButton, mods ); QCoreApplication::sendEvent( top.get(), &move );
        CHECK( session.workspace.editPreview.bActive );
        CHECK( EditorHistory_StepCount( &session.workspace.history ) == 1 );
        QMouseEvent release( QEvent::MouseButtonRelease, end, top->mapToGlobal( end ), Qt::LeftButton, Qt::NoButton, mods ); QCoreApplication::sendEvent( top.get(), &release );
    };
    drag( true );
    REQUIRE( session.workspace.pDocument->geometry.brushes.nCount == 2 );
    CHECK( EditorSelection_At( &session.workspace.selection, 0 ) != original );
    CHECK( MapViews_SelectionGeometryBounds( &session.workspace ).box.minimum.x == -32 );
    REQUIRE( MapWorkspace_Undo( &session.workspace ) == editor_history_status_t::OK );
    CHECK( EditorSelection_At( &session.workspace.selection, 0 ) == original );
    CHECK( session.workspace.pDocument->geometry.brushes.nCount == 1 );
}

TEST_CASE( "Ctrl bypasses translation snapping without changing the grid setting", "[map][gui][views][geometry-edit][snap]" )
{
    session_t session; REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, 0 } ); MapBounds_AddPoint( box, { 64, 64, 64 } );
    REQUIRE( MapWorkspace_CreateBox( &session.workspace, box ) );
    MapWorkspace_SetGridSize( &session.workspace, 16 ); MapWorkspace_SetTool( &session.workspace, map_tool_t::TRANSLATE );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &session.workspace, map_ortho_axes_t::TOP ) ); ShowAt( top.get(), 800, 600 );
    const QPointF start = MapOrthoView_WorldToView( top.get(), { -64, -64 } ), end = MapOrthoView_WorldToView( top.get(), { -43, -35 } );
    QMouseEvent press( QEvent::MouseButtonPress, start, top->mapToGlobal( start ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier ); QCoreApplication::sendEvent( top.get(), &press );
    QMouseEvent move( QEvent::MouseMove, end, top->mapToGlobal( end ), Qt::NoButton, Qt::LeftButton, Qt::ControlModifier ); QCoreApplication::sendEvent( top.get(), &move );
    QMouseEvent release( QEvent::MouseButtonRelease, end, top->mapToGlobal( end ), Qt::LeftButton, Qt::NoButton, Qt::ControlModifier ); QCoreApplication::sendEvent( top.get(), &release );
    const auto bounds = MapViews_SelectionGeometryBounds( &session.workspace );
    CHECK( std::abs( bounds.box.minimum.x + 43 ) < 1e-6 ); CHECK( std::abs( bounds.box.minimum.y + 35 ) < 1e-6 );
    CHECK( session.workspace.bSnapToGrid ); CHECK( session.workspace.gridSize == 16 );
    REQUIRE( MapWorkspace_Undo( &session.workspace ) == editor_history_status_t::OK );
    CHECK( MapViews_SelectionGeometryBounds( &session.workspace ).box.minimum.x == -64 );
}

TEST_CASE( "Select move gizmos constrain snapped axis endcap center and planar drags in Objects and Groups", "[map][gui][views][geometry-edit][gizmo][select-move]" )
{
    for ( const auto mode : { map_element_mode_t::OBJECTS, map_element_mode_t::GROUPS } ) {
        for ( int projection : { -1, 0, 1, 2 } ) {
            CAPTURE( static_cast<int>( mode ), projection );
            session_t session; auto &ws = session.workspace;
            REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
            // A cube keeps the selection-bound resize controls separated
            // from the constant-screen-size central movement handles.
            map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, 0 } ); MapBounds_AddPoint( box, { 64, 64, 128 } );
            REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
            MapWorkspace_SetGridSize( &ws, 16 ); MapWorkspace_SetElementMode( &ws, mode );
            REQUIRE( ws.tool == map_tool_t::SELECT ); REQUIRE( ws.elementMode == mode );
            const bool perspective = projection < 0;
            const auto axes = static_cast<map_ortho_axes_t>( std::max( 0, projection ) );
            const u32 axisU = axes == map_ortho_axes_t::FRONT ? 1u : 0u;
            const u32 axisV = axes == map_ortho_axes_t::TOP ? 1u : 2u;
            std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &ws ) : MapOrthoView_Create( nullptr, &ws, axes ) );
            ShowAt( view.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
            const math::vec3d_t pivot{ 0, 0, 64 };
            const auto project = [&]( math::vec3d_t p ) {
                QPointF result;
                if ( perspective ) { REQUIRE( MapCameraView_WorldToView( view.get(), p, &result ) ); }
                else { result = MapOrthoView_WorldToView( view.get(), { TestCoordinate( p, axisU ), TestCoordinate( p, axisV ) } ); }
                return result;
            };
            const usize initialSteps = EditorHistory_StepCount( &ws.history );
            // Negative means center; 0..2 mean shafts, 3..5 their endcaps,
            // and 6..8 mean the plane perpendicular to the named axis.
            std::vector<int> handles;
            if ( perspective ) { handles = { 0, 1, 2, 3, 4, 5, -1, 6, 7, 8 }; }
            else { handles = { static_cast<int>( axisU ), static_cast<int>( axisV ), static_cast<int>( axisU ) + 3, -1, 6 + static_cast<int>( 3u - axisU - axisV ) }; }
            for ( const int handle : handles ) {
                CAPTURE( handle );
                const f64 length = perspective ? CameraGizmoLength( view.get(), pivot, session.gui.settings ) : 64.0 / MapOrthoView_Zoom( view.get() );
                QPointF start, end; math::vec3d_t expected{};
                bool exactExpected = true;
                if ( handle < 0 ) {
                    start = project( pivot );
                    if ( perspective ) { end = start + QPointF( 43, -27 ); exactExpected = false; }
                    else {
                        auto target = pivot; SetTestCoordinate( target, axisU, TestCoordinate( target, axisU ) + 21 );
                        SetTestCoordinate( target, axisV, TestCoordinate( target, axisV ) + 29 ); end = project( target );
                        SetTestCoordinate( expected, axisU, 16 ); SetTestCoordinate( expected, axisV, 32 );
                    }
                } else if ( handle < 6 ) {
                    const u32 axis = static_cast<u32>( handle % 3 ); auto tip = pivot;
                    SetTestCoordinate( tip, axis, TestCoordinate( tip, axis ) + length );
                    const QPointF center = project( pivot ), tipScreen = project( tip );
                    start = center + ( tipScreen - center ) * ( handle < 3 ? 0.70 : 1.0 );
                    end = start + ( tipScreen - center ) * ( 21.0 / length );
                    SetTestCoordinate( expected, axis, 16 );
                } else {
                    const u32 omitted = static_cast<u32>( handle - 6 );
                    const u32 u = ( omitted + 1u ) % 3u, v = ( omitted + 2u ) % 3u;
                    auto pad = pivot; SetTestCoordinate( pad, u, TestCoordinate( pad, u ) + length * 0.31 );
                    SetTestCoordinate( pad, v, TestCoordinate( pad, v ) + length * 0.31 ); start = project( pad );
                    SetTestCoordinate( pad, u, TestCoordinate( pad, u ) + 21 ); SetTestCoordinate( pad, v, TestCoordinate( pad, v ) + 29 ); end = project( pad );
                    SetTestCoordinate( expected, u, 16 ); SetTestCoordinate( expected, v, 32 );
                }
                const auto *document = ws.pDocument;
                DragMouse( view.get(), QEvent::MouseButtonPress, start ); DragMouse( view.get(), QEvent::MouseMove, end );
                REQUIRE( ws.editPreview.bActive ); CHECK( ws.pDocument == document );
                CHECK( MapViews_SelectionGeometryBounds( &ws ).box.minimum.x == -64 );
                const auto future = ws.editPreview.bounds;
                math::vec3d_t actual{ future.box.minimum.x + 64, future.box.minimum.y + 64, future.box.minimum.z };
                if ( exactExpected ) { CheckPointClose( actual, expected ); }
                else {
                    CHECK( TestDistance( actual, {} ) > 0.0 );
                    for ( u32 axis = 0; axis < 3; ++axis ) { const f64 value = TestCoordinate( actual, axis ); CHECK( std::abs( value / 16.0 - std::round( value / 16.0 ) ) < 1e-6 ); }
                }
                CHECK( ws.tool == map_tool_t::SELECT ); CHECK( ws.elementMode == mode );
                DragMouse( view.get(), QEvent::MouseButtonRelease, end );
                CHECK_FALSE( ws.editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws.history ) == initialSteps + 1u );
                const auto moved = MapViews_SelectionGeometryBounds( &ws );
                CheckPointClose( moved.box.minimum, future.box.minimum ); CheckPointClose( moved.box.maximum, future.box.maximum );
                CHECK( EditorSelection_At( &ws.selection, 0 ) == id );
                REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
                const auto original = MapViews_SelectionGeometryBounds( &ws ); CheckPointClose( original.box.minimum, box.box.minimum ); CheckPointClose( original.box.maximum, box.box.maximum );
            }
        }
    }
}

TEST_CASE( "Select group bounds sides and corners resize in every orthographic projection around a fixed opposite anchor", "[map][gui][views][geometry-edit][select-resize][gizmo]" )
{
    for ( int projection = 0; projection < 3; ++projection ) {
        for ( int handle = 0; handle < 3; ++handle ) {
            CAPTURE( projection, handle );
            session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
            REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK ); settings.Set( "editor.viewport.show_selection_bounds", true );
            settings.Choice( "editor.map.resize_mode", "Selection bounds" );
            map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -48, 16 } ); MapBounds_AddPoint( box, { 96, 80, 144 } );
            std::vector<u64> ids;
            if ( handle == 2 ) {
                auto left = box, right = box; left.box.maximum.x = 0; right.box.minimum.x = 32;
                REQUIRE( MapWorkspace_CreateBox( &ws, left ) ); ids.push_back( EditorSelection_At( &ws.selection, 0 ) );
                REQUIRE( MapWorkspace_CreateBox( &ws, right ) ); ids.push_back( EditorSelection_At( &ws.selection, 0 ) );
                MapWorkspace_SetSelection( &ws, ids.data(), ids.size() );
            } else { REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); ids.push_back( EditorSelection_At( &ws.selection, 0 ) ); }
            std::vector<std::vector<math::vec3d_t>> originals;
            for ( const u64 id : ids ) { originals.push_back( ObjectLineVertices( ws.wire, id ) ); }
            const auto mode = handle == 1 ? map_element_mode_t::GROUPS : map_element_mode_t::OBJECTS;
            MapWorkspace_SetElementMode( &ws, mode ); MapWorkspace_SetGridSize( &ws, 16 ); MapWorkspace_SetScaleSnap( &ws, 0.25 );
            const auto axes = static_cast<map_ortho_axes_t>( projection ); const u32 u = axes == map_ortho_axes_t::FRONT ? 1u : 0u, v = axes == map_ortho_axes_t::TOP ? 1u : 2u;
            std::unique_ptr<QWidget> view( MapOrthoView_Create( nullptr, &ws, axes ) ); ShowAt( view.get(), 800, 600 );
            MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
            auto startPoint = MapBounds_Center( box ), pivot = startPoint, factors = math::vec3d_t{ 1, 1, 1 }; auto expected = box;
            if ( handle != 1 ) {
                SetTestCoordinate( startPoint, u, TestCoordinate( box.box.maximum, u ) ); SetTestCoordinate( pivot, u, TestCoordinate( box.box.minimum, u ) );
                SetTestCoordinate( expected.box.maximum, u, TestCoordinate( expected.box.maximum, u ) + 16 );
                SetTestCoordinate( factors, u, 1.0 + 16.0 / ( TestCoordinate( box.box.maximum, u ) - TestCoordinate( box.box.minimum, u ) ) );
            }
            if ( handle != 0 ) {
                SetTestCoordinate( startPoint, v, TestCoordinate( box.box.minimum, v ) ); SetTestCoordinate( pivot, v, TestCoordinate( box.box.maximum, v ) );
                SetTestCoordinate( expected.box.minimum, v, TestCoordinate( expected.box.minimum, v ) - 32 );
                SetTestCoordinate( factors, v, 1.0 + 32.0 / ( TestCoordinate( box.box.maximum, v ) - TestCoordinate( box.box.minimum, v ) ) );
            }
            auto endPoint = startPoint;
            SetTestCoordinate( endPoint, u, TestCoordinate( endPoint, u ) + ( handle == 1 ? 13 : 21 ) );
            SetTestCoordinate( endPoint, v, TestCoordinate( endPoint, v ) + ( handle == 0 ? 13 : -27 ) );
            const auto project = [&]( math::vec3d_t point ) { return MapOrthoView_WorldToView( view.get(), { TestCoordinate( point, u ), TestCoordinate( point, v ) } ); };
            const auto *document = ws.pDocument; const auto revision = document->geometry.revision, selectionRevision = ws.selection.revision; const usize steps = EditorHistory_StepCount( &ws.history );
            DragMouse( view.get(), QEvent::MouseButtonPress, project( startPoint ) ); DragMouse( view.get(), QEvent::MouseMove, project( endPoint ) );
            REQUIRE( ws.editPreview.bActive ); CHECK( ws.editPreview.transform.kind == map_transform_preview_kind_t::SCALE ); CHECK( ws.editPreview.transform.bResize ); CHECK_FALSE( ws.editPreview.transform.bClone );
            CheckPointClose( ws.editPreview.transform.pivot, pivot ); CheckPointClose( ws.editPreview.transform.factors, factors );
            CheckPointClose( MapWorkspace_TransformPreviewPoint( ws.editPreview.transform, pivot ), pivot );
            CheckPointClose( ws.editPreview.bounds.box.minimum, expected.box.minimum ); CheckPointClose( ws.editPreview.bounds.box.maximum, expected.box.maximum );
            CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.selection.revision == selectionRevision ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
            CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, box.box.minimum ); CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, box.box.maximum );
            for ( usize object = 0; object < ids.size(); ++object ) { CheckObjectVertices( ws.wire, ids[object], originals[object] ); }
            DragMouse( view.get(), QEvent::MouseButtonRelease, project( endPoint ) ); DragMouse( view.get(), QEvent::MouseButtonRelease, project( endPoint ) );
            CHECK_FALSE( ws.editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u ); CHECK( ws.tool == map_tool_t::SELECT ); CHECK( ws.elementMode == mode );
            CHECK( EditorSelection_Count( &ws.selection ) == ids.size() );
            CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, expected.box.minimum ); CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, expected.box.maximum );
            for ( usize object = 0; object < ids.size(); ++object ) {
                REQUIRE( MapWorkspace_IsSelected( &ws, ids[object] ) ); auto moved = originals[object];
                for ( auto &point : moved ) { for ( u32 axis = 0; axis < 3; ++axis ) { SetTestCoordinate( point, axis, TestCoordinate( pivot, axis ) + ( TestCoordinate( point, axis ) - TestCoordinate( pivot, axis ) ) * TestCoordinate( factors, axis ) ); } }
                CheckObjectVertices( ws.wire, ids[object], moved );
            }
            REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
            for ( usize object = 0; object < ids.size(); ++object ) { CheckObjectVertices( ws.wire, ids[object], originals[object] ); }
            REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
            CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, expected.box.minimum ); CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, expected.box.maximum );
        }
    }
}

TEST_CASE( "Select camera face handles resize each signed axis while its opposite face stays fixed", "[map][gui][views][geometry-edit][select-resize][gizmo][camera]" )
{
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -48, 16 } ); MapBounds_AddPoint( box, { 96, 80, 144 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
    MapWorkspace_SetGridSize( &ws, 16 ); MapWorkspace_SetScaleSnap( &ws, 0.25 );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE );
    QCoreApplication::processEvents(); const auto cameraPosition = MapCameraView_Position( camera.get() ); const usize initialSteps = EditorHistory_StepCount( &ws.history );
    const auto center = MapBounds_Center( box ); const auto original = ObjectLineVertices( ws.wire, id );
    for ( u32 axis = 0; axis < 3; ++axis ) { for ( const bool maximum : { false, true } ) { for ( const f64 distance : { 21.0, 137.0 } ) {
        CAPTURE( axis, maximum, distance ); auto point = center, pivot = center, factors = math::vec3d_t{ 1, 1, 1 }; auto expected = box;
        const bool bypass = distance > 100; const Qt::KeyboardModifiers modifiers = bypass ? Qt::ControlModifier : Qt::NoModifier;
        const f64 boundaryDelta = bypass ? distance : 16;
        const f64 sign = maximum ? 1 : -1, extent = TestCoordinate( box.box.maximum, axis ) - TestCoordinate( box.box.minimum, axis );
        SetTestCoordinate( point, axis, TestCoordinate( maximum ? box.box.maximum : box.box.minimum, axis ) );
        SetTestCoordinate( pivot, axis, TestCoordinate( maximum ? box.box.minimum : box.box.maximum, axis ) );
        SetTestCoordinate( factors, axis, 1.0 + boundaryDelta / extent );
        SetTestCoordinate( maximum ? expected.box.maximum : expected.box.minimum, axis, TestCoordinate( point, axis ) + sign * boundaryDelta );
        // Project the intended world boundary itself. The long Ctrl drag
        // changes camera depth and exposes a screen-derivative approximation.
        auto endPoint = point; SetTestCoordinate( endPoint, axis, TestCoordinate( endPoint, axis ) + sign * distance ); QPointF start, end;
        REQUIRE( MapCameraView_WorldToView( camera.get(), point, &start ) ); REQUIRE( MapCameraView_WorldToView( camera.get(), endPoint, &end ) );
        REQUIRE( camera->rect().adjusted( 8, 8, -8, -8 ).contains( start.toPoint() ) );
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision, selectionRevision = ws.selection.revision;
        DragMouse( camera.get(), QEvent::MouseButtonPress, start, modifiers ); DragMouse( camera.get(), QEvent::MouseMove, end, modifiers );
        REQUIRE( ws.editPreview.bActive ); CHECK( ws.editPreview.transform.kind == map_transform_preview_kind_t::SCALE ); CHECK( ws.editPreview.transform.bResize );
        CheckPointClose( ws.editPreview.transform.pivot, pivot ); CheckPointClose( ws.editPreview.transform.factors, factors );
        CheckPointClose( ws.editPreview.bounds.box.minimum, expected.box.minimum ); CheckPointClose( ws.editPreview.bounds.box.maximum, expected.box.maximum );
        CheckPointClose( MapWorkspace_TransformPreviewPoint( ws.editPreview.transform, pivot ), pivot ); CheckPointClose( MapCameraView_Position( camera.get() ), cameraPosition );
        CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.selection.revision == selectionRevision ); CheckObjectVertices( ws.wire, id, original );
        DragMouse( camera.get(), QEvent::MouseButtonRelease, end, modifiers ); CHECK_FALSE( ws.editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws.history ) == initialSteps + 1u );
        CHECK( ws.tool == map_tool_t::SELECT ); CHECK( EditorSelection_At( &ws.selection, 0 ) == id );
        CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, expected.box.minimum ); CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, expected.box.maximum );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CheckObjectVertices( ws.wire, id, original );
        REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
        CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, expected.box.minimum ); CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, expected.box.maximum );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    } } }
}

TEST_CASE( "Select resize preserves the grab offset and supports snap bypass positive extent clamping and cancellation", "[map][gui][views][geometry-edit][select-resize][snap]" )
{
    session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK ); settings.Set( "editor.viewport.show_selection_bounds", true );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -48, 16 } ); MapBounds_AddPoint( box, { 96, 80, 144 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 ); MapWorkspace_SetGridSize( &ws, 16 ); MapWorkspace_SetScaleSnap( &ws, 0.25 );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) ); ShowAt( top.get(), 800, 600 );
    MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
    const QPointF grabOffset( 3, -2 ), corner = MapOrthoView_WorldToView( top.get(), { 96, -48 } ), start = corner + grabOffset;
    const QPointF end = MapOrthoView_WorldToView( top.get(), { 117, -75 } ) + grabOffset; const usize initialSteps = EditorHistory_StepCount( &ws.history );
    for ( const Qt::KeyboardModifiers modifier : { Qt::KeyboardModifiers( Qt::ControlModifier ), Qt::KeyboardModifiers( Qt::MetaModifier ) } ) {
        CAPTURE( static_cast<int>( modifier ) ); const auto *document = ws.pDocument; const auto selectionRevision = ws.selection.revision;
        DragMouse( top.get(), QEvent::MouseButtonPress, start, modifier ); DragMouse( top.get(), QEvent::MouseMove, end, modifier );
        REQUIRE( ws.editPreview.bActive ); CHECK( ws.editPreview.transform.bResize );
        CheckPointClose( ws.editPreview.bounds.box.minimum, { -64, -75, 16 } ); CheckPointClose( ws.editPreview.bounds.box.maximum, { 117, 80, 144 } );
        CheckPointClose( ws.editPreview.transform.pivot, { -64, 80, 80 } ); CHECK( ws.pDocument == document ); CHECK( ws.selection.revision == selectionRevision );
        DragMouse( top.get(), QEvent::MouseButtonRelease, end, modifier ); CHECK( EditorHistory_StepCount( &ws.history ) == initialSteps + 1u );
        CHECK( EditorSelection_At( &ws.selection, 0 ) == id ); CHECK( ws.bSnapToGrid ); CHECK( ws.gridSize == 16 ); CHECK( ws.tool == map_tool_t::SELECT );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
        CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, box.box.minimum ); CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, box.box.maximum );
    }
    const QPointF crossing = MapOrthoView_WorldToView( top.get(), { -96, 112 } ) + grabOffset;
    DragMouse( top.get(), QEvent::MouseButtonPress, start ); DragMouse( top.get(), QEvent::MouseMove, crossing );
    REQUIRE( ws.editPreview.bActive ); CHECK( ws.editPreview.transform.bResize );
    CheckPointClose( ws.editPreview.bounds.box.minimum, { -64, 79, 16 } ); CheckPointClose( ws.editPreview.bounds.box.maximum, { -63, 80, 144 } );
    CHECK( ws.editPreview.transform.factors.x > 0 ); CHECK( ws.editPreview.transform.factors.y > 0 );
    DragMouse( top.get(), QEvent::MouseButtonRelease, crossing ); CHECK( EditorHistory_StepCount( &ws.history ) == initialSteps + 1u );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    const auto *document = ws.pDocument; const usize steps = EditorHistory_StepCount( &ws.history );
    DragMouse( top.get(), QEvent::MouseButtonPress, start ); DragMouse( top.get(), QEvent::MouseMove, end ); REQUIRE( ws.editPreview.bActive );
    QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( top.get(), &escape );
    DragMouse( top.get(), QEvent::MouseButtonRelease, end ); CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, box.box.minimum ); CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, box.box.maximum );
    Click( top.get(), start ); CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( EditorSelection_At( &ws.selection, 0 ) == id );
}

TEST_CASE( "Flat mesh zero extent corners do not mask its valid side resize handle", "[map][gui][views][geometry-edit][select-resize][mesh]" )
{
    namespace geo = geometry;
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    const auto nextId = [&]() {
        const auto result = geo::GeometrySourceIdAllocator_Allocate( &ws.pDocument->geometry.sourceIds.allocator );
        REQUIRE( result.status == geo::geometry_status_t::OK ); return result.id;
    };
    const auto meshId = nextId(); geo::mesh_source_description_t description{};
    REQUIRE( geo::MeshSourceDescription_Init( &description, ws.pDocument->pAllocator, meshId ) == geo::geometry_status_t::OK );
    const math::vec3d_t corners[]{ { -64, -48, 0 }, { 96, -48, 0 }, { 96, 80, 0 }, { -64, 80, 0 } };
    for ( const auto &point : corners ) { REQUIRE( geo::MeshSourceDescription_TryAddVertex( &description, point, nextId(), nullptr ) == geo::geometry_status_t::OK ); }
    const u32 indices[]{ 0, 1, 2, 3 };
    REQUIRE( geo::MeshSourceDescription_TryAddFace( &description, { indices, 4 }, nextId(), {}, nullptr ) == geo::geometry_status_t::OK );
    geo::mesh_source_t mesh{}; REQUIRE( geo::MeshSource_TryBuild( &description, ws.pDocument->pAllocator, &mesh ) == geo::geometry_status_t::OK );
    REQUIRE( geo::GeometryDocument_TryAddMesh( &ws.pDocument->geometry, &mesh ) == geo::geometry_status_t::OK );
    geo::MeshSource_Shutdown( &mesh ); geo::MeshSourceDescription_Shutdown( &description );
    REQUIRE( MapDocument_SetGeometryLayer( ws.pDocument, meshId.value, StringView_FromCString( "default" ) ) == map_status_t::OK );
    MapWorkspace_DocumentChanged( &ws ); MapWorkspace_Select( &ws, meshId.value, MAP_SELECT_REPLACE ); MapWorkspace_SetGridSize( &ws, 16 );
    REQUIRE( MapWorkspace_CanEditSelection( &ws ) );
    std::unique_ptr<QWidget> side( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::SIDE ) ); ShowAt( side.get(), 800, 600 );
    MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
    // The X maximum side and both X maximum/Z corners project to this same
    // point. Invalid Z resize corners must not capture the valid X side.
    const QPointF start = MapOrthoView_WorldToView( side.get(), { 96, 0 } ), end = MapOrthoView_WorldToView( side.get(), { 117, 0 } );
    const auto original = ObjectLineVertices( ws.wire, meshId.value ); const auto *document = ws.pDocument;
    const auto revision = document->geometry.revision; const usize steps = EditorHistory_StepCount( &ws.history );
    DragMouse( side.get(), QEvent::MouseButtonPress, start ); DragMouse( side.get(), QEvent::MouseMove, end );
    REQUIRE( ws.editPreview.bActive ); CHECK( ws.editPreview.transform.kind == map_transform_preview_kind_t::SCALE ); CHECK( ws.editPreview.transform.bResize );
    CheckPointClose( ws.editPreview.transform.pivot, { -64, 16, 0 } ); CheckPointClose( ws.editPreview.transform.factors, { 1.1, 1, 1 } );
    CheckPointClose( ws.editPreview.bounds.box.minimum, { -64, -48, 0 } ); CheckPointClose( ws.editPreview.bounds.box.maximum, { 112, 80, 0 } );
    CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CheckObjectVertices( ws.wire, meshId.value, original );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    DragMouse( side.get(), QEvent::MouseButtonRelease, end ); CHECK_FALSE( ws.editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
    CHECK( ws.tool == map_tool_t::SELECT ); CHECK( EditorSelection_At( &ws.selection, 0 ) == meshId.value );
    CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, { -64, -48, 0 } ); CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, { 112, 80, 0 } );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CheckObjectVertices( ws.wire, meshId.value, original );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, { 112, 80, 0 } );
}

TEST_CASE( "Select body drag preserves multiselection while modified body presses only select", "[map][gui][views][geometry-edit][select-move][selection]" )
{
    for ( bool perspective : { false, true } ) {
        CAPTURE( perspective );
        session_t session; auto &ws = session.workspace;
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t first{}; MapBounds_AddPoint( first, { -160, -64, 0 } ); MapBounds_AddPoint( first, { -64, 64, 96 } );
        map_bounds_t second{}; MapBounds_AddPoint( second, { 64, -64, 0 } ); MapBounds_AddPoint( second, { 160, 64, 96 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, first ) ); const u64 a = EditorSelection_At( &ws.selection, 0 );
        REQUIRE( MapWorkspace_CreateBox( &ws, second ) ); const u64 b = EditorSelection_At( &ws.selection, 0 );
        const u64 pair[]{ a, b }; MapWorkspace_SetSelection( &ws, pair, 2 ); MapWorkspace_SetGridSize( &ws, 16 );
        std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &ws ) : MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
        ShowAt( view.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        QPointF start, end;
        if ( perspective ) {
            REQUIRE( MapCameraView_WorldToView( view.get(), { -152, -56, 96 }, &start ) );
            REQUIRE( MapCameraView_Pick( view.get(), start ) == a ); end = start + QPointF( 43, -27 );
        } else {
            // Selection-bound corners and side midpoints resize. Use an
            // ordinary brush edge away from those controls for body motion.
            start = MapOrthoView_WorldToView( view.get(), { -160, -24 } ); end = MapOrthoView_WorldToView( view.get(), { -139, 5 } );
        }
        const auto *document = ws.pDocument; const auto selectionRevision = ws.selection.revision;
        const usize steps = EditorHistory_StepCount( &ws.history );
        DragMouse( view.get(), QEvent::MouseButtonPress, start );
        CHECK( ws.selection.revision == selectionRevision ); CHECK( EditorSelection_Count( &ws.selection ) == 2u );
        DragMouse( view.get(), QEvent::MouseMove, end ); REQUIRE( ws.editPreview.bActive ); CHECK( ws.pDocument == document );
        DragMouse( view.get(), QEvent::MouseButtonRelease, end );
        CHECK( EditorSelection_Count( &ws.selection ) == 2u ); CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
        const auto *movedA = MapWireframe_FindObject( ws.wire, a ), *movedB = MapWireframe_FindObject( ws.wire, b );
        REQUIRE( movedA != nullptr ); REQUIRE( movedB != nullptr );
        const math::vec3d_t deltaA{ movedA->bounds.box.minimum.x + 160, movedA->bounds.box.minimum.y + 64, movedA->bounds.box.minimum.z };
        const math::vec3d_t deltaB{ movedB->bounds.box.minimum.x - 64, movedB->bounds.box.minimum.y + 64, movedB->bounds.box.minimum.z };
        CheckPointClose( deltaA, deltaB ); CHECK( TestDistance( deltaA, {} ) > 0.0 );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
        for ( const Qt::KeyboardModifiers modifiers : { Qt::KeyboardModifiers( Qt::ControlModifier ), Qt::KeyboardModifiers( Qt::MetaModifier ), Qt::KeyboardModifiers( Qt::ShiftModifier ) } ) {
            CAPTURE( static_cast<int>( modifiers ) );
            if ( modifiers == Qt::ShiftModifier ) { MapWorkspace_Select( &ws, b, MAP_SELECT_REPLACE ); }
            else { MapWorkspace_SetSelection( &ws, pair, 2 ); }
            const auto *unchanged = ws.pDocument;
            DragMouse( view.get(), QEvent::MouseButtonPress, start, modifiers ); DragMouse( view.get(), QEvent::MouseMove, end, modifiers );
            CHECK_FALSE( ws.editPreview.bActive ); DragMouse( view.get(), QEvent::MouseButtonRelease, end, modifiers );
            CHECK( ws.pDocument == unchanged ); CHECK( ws.tool == map_tool_t::SELECT );
            CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u ); // Only the earlier, undone move exists.
            CHECK( MapWorkspace_IsSelected( &ws, b ) );
            CHECK( MapWorkspace_IsSelected( &ws, a ) == ( modifiers == Qt::ShiftModifier ) );
        }
    }
}

TEST_CASE( "Selected projected interiors move the whole selection while other authored outlines retain pick priority", "[map][gui][views][geometry-edit][select-move][selected-interior]" )
{
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_bounds_t first{}, second{}, inset{};
    MapBounds_AddPoint( first, { -160, -64, 0 } ); MapBounds_AddPoint( first, { -64, 64, 96 } );
    MapBounds_AddPoint( second, { 64, -64, 0 } ); MapBounds_AddPoint( second, { 160, 64, 96 } );
    MapBounds_AddPoint( inset, { -120, -32, 16 } ); MapBounds_AddPoint( inset, { -104, -16, 80 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, first ) ); const u64 a = EditorSelection_At( &ws.selection, 0 );
    REQUIRE( MapWorkspace_CreateBox( &ws, second ) ); const u64 b = EditorSelection_At( &ws.selection, 0 );
    REQUIRE( MapWorkspace_CreateBox( &ws, inset ) ); const u64 c = EditorSelection_At( &ws.selection, 0 );
    const u64 pair[]{ a, b }; MapWorkspace_SetSelection( &ws, pair, 2 ); MapWorkspace_SetGridSize( &ws, 16 );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) ); ShowAt( top.get(), 800, 600 );
    MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
    const QPointF start = MapOrthoView_WorldToView( top.get(), { -112, 24 } ), end = MapOrthoView_WorldToView( top.get(), { -91, 53 } );
    REQUIRE( MapOrthoView_Pick( top.get(), start ) == a );
    CHECK( MapOrthoView_Pick( top.get(), MapOrthoView_WorldToView( top.get(), { -120, -24 } ) ) == c );
    const auto *document = ws.pDocument; const auto selectionRevision = ws.selection.revision, revision = document->geometry.revision; const usize steps = EditorHistory_StepCount( &ws.history );
    DragMouse( top.get(), QEvent::MouseButtonPress, start ); DragMouse( top.get(), QEvent::MouseMove, end );
    REQUIRE( ws.editPreview.bActive ); CHECK( ws.editPreview.transform.kind == map_transform_preview_kind_t::TRANSLATE ); CHECK_FALSE( ws.editPreview.transform.bResize );
    CheckPointClose( ws.editPreview.transform.delta, { 16, 32, 0 } ); CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision );
    CHECK( ws.selection.revision == selectionRevision ); CHECK( EditorSelection_Count( &ws.selection ) == 2u ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    DragMouse( top.get(), QEvent::MouseButtonRelease, end ); CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
    for ( const auto &[id, original] : { std::pair{ a, first }, std::pair{ b, second }, std::pair{ c, inset } } ) {
        const auto *object = MapWireframe_FindObject( ws.wire, id ); REQUIRE( object != nullptr ); const auto delta = id == c ? math::vec3d_t{} : math::vec3d_t{ 16, 32, 0 };
        CheckPointClose( object->bounds.box.minimum, math::Vec3d_Add( original.box.minimum, delta ) ); CheckPointClose( object->bounds.box.maximum, math::Vec3d_Add( original.box.maximum, delta ) );
    }
    CHECK( MapWorkspace_IsSelected( &ws, a ) ); CHECK( MapWorkspace_IsSelected( &ws, b ) ); CHECK_FALSE( MapWorkspace_IsSelected( &ws, c ) );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, { -160, -64, 0 } );
}

TEST_CASE( "Selected owned geometry preserves owner multiselection and ignores small body jitter before movement", "[map][gui][views][geometry-edit][select-move][selected-interior][owner]" )
{
    for ( const bool perspective : { false, true } ) {
        CAPTURE( perspective ); session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t first{}, second{}; MapBounds_AddPoint( first, { -160, -64, 0 } ); MapBounds_AddPoint( first, { -64, 64, 96 } );
        MapBounds_AddPoint( second, { 320, -64, 0 } ); MapBounds_AddPoint( second, { 416, 64, 96 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, first ) ); const u64 a = EditorSelection_At( &ws.selection, 0 );
        REQUIRE( MapWorkspace_CreateBox( &ws, second ) ); const u64 b = EditorSelection_At( &ws.selection, 0 );
        u64 owner = 0; const math::vec3d_t origin{ 256, 64, 32 };
        REQUIRE( MapDocument_AddEntity( ws.pDocument, StringView_FromCString( "default" ), StringView_FromCString( "func_door" ), origin, &owner ) == map_status_t::OK );
        REQUIRE( MapDocument_SetGeometryOwner( ws.pDocument, a, owner ) == map_status_t::OK ); MapWorkspace_DocumentChanged( &ws );
        const u64 selection[]{ owner, b }; MapWorkspace_SetSelection( &ws, selection, 2 ); MapWorkspace_SetGridSize( &ws, 16 );
        std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &ws ) : MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
        ShowAt( view.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        QPointF start, end;
        if ( perspective ) {
            REQUIRE( MapCameraView_WorldToView( view.get(), { -112, 24, 96 }, &start ) ); REQUIRE( MapCameraView_Pick( view.get(), start ) == a ); end = start + QPointF( 43, -27 );
        } else {
            start = MapOrthoView_WorldToView( view.get(), { -112, 24 } ); end = MapOrthoView_WorldToView( view.get(), { -91, 53 } ); REQUIRE( MapOrthoView_Pick( view.get(), start ) == a );
        }
        const auto *document = ws.pDocument; const auto selectionRevision = ws.selection.revision, revision = document->geometry.revision; const usize steps = EditorHistory_StepCount( &ws.history );
        DragMouse( view.get(), QEvent::MouseButtonPress, start ); DragMouse( view.get(), QEvent::MouseMove, start + QPointF( 1, 1 ) );
        CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.selection.revision == selectionRevision );
        DragMouse( view.get(), QEvent::MouseButtonRelease, start + QPointF( 1, 1 ) ); CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
        DragMouse( view.get(), QEvent::MouseButtonPress, start ); DragMouse( view.get(), QEvent::MouseMove, end );
        REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.transform.kind == map_transform_preview_kind_t::TRANSLATE );
        const auto delta = ws.editPreview.transform.delta; CHECK( TestDistance( delta, {} ) > 0 ); if ( !perspective ) { CheckPointClose( delta, { 16, 32, 0 } ); }
        CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.selection.revision == selectionRevision ); CHECK( EditorSelection_Count( &ws.selection ) == 2u );
        CHECK( MapWorkspace_IsSelected( &ws, owner ) ); CHECK( MapWorkspace_IsSelected( &ws, b ) ); CHECK_FALSE( MapWorkspace_IsSelected( &ws, a ) );
        DragMouse( view.get(), QEvent::MouseButtonRelease, end ); CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
        const auto *owned = MapWireframe_FindObject( ws.wire, a ), *other = MapWireframe_FindObject( ws.wire, b ); const auto *entity = MapWireframe_FindEntity( ws.wire, owner );
        REQUIRE( owned != nullptr ); REQUIRE( other != nullptr ); REQUIRE( entity != nullptr );
        CheckPointClose( owned->bounds.box.minimum, math::Vec3d_Add( first.box.minimum, delta ) ); CheckPointClose( other->bounds.box.minimum, math::Vec3d_Add( second.box.minimum, delta ) );
        CheckPointClose( entity->origin, math::Vec3d_Add( origin, delta ) ); CHECK( EditorSelection_Count( &ws.selection ) == 2u ); CHECK( MapWorkspace_IsSelected( &ws, owner ) ); CHECK_FALSE( MapWorkspace_IsSelected( &ws, a ) );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CheckPointClose( MapWireframe_FindObject( ws.wire, a )->bounds.box.minimum, first.box.minimum );
        CheckPointClose( MapWireframe_FindEntity( ws.wire, owner )->origin, origin );
    }
}

TEST_CASE( "Selected primitive interior picking follows its projected polygon rather than the AABB", "[map][gui][views][selection][selected-interior]" )
{
    for ( const auto kind : { map_primitive_kind_t::WEDGE, map_primitive_kind_t::CYLINDER } ) {
        CAPTURE( static_cast<int>( kind ) ); session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_primitive_desc_t primitive{}; primitive.kind = kind; primitive.bounds = { { -96, -64, 0 }, { 96, 64, 128 } }; primitive.nSides = 8;
        REQUIRE( MapWorkspace_CreatePrimitive( &ws, primitive ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
        const auto axes = kind == map_primitive_kind_t::WEDGE ? map_ortho_axes_t::SIDE : map_ortho_axes_t::TOP;
        std::unique_ptr<QWidget> view( MapOrthoView_Create( nullptr, &ws, axes ) ); ShowAt( view.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE );
        const QPointF inside = kind == map_primitive_kind_t::WEDGE ? QPointF( -32, 32 ) : QPointF( 32, 16 );
        const QPointF outside = kind == map_primitive_kind_t::WEDGE ? QPointF( 64, 112 ) : QPointF( 80, 52 );
        CHECK( MapOrthoView_Pick( view.get(), MapOrthoView_WorldToView( view.get(), inside ) ) == id );
        CHECK( MapOrthoView_Pick( view.get(), MapOrthoView_WorldToView( view.get(), outside ) ) == 0u );
        MapWorkspace_SetTool( &ws, map_tool_t::ROTATE ); CHECK( MapOrthoView_Pick( view.get(), MapOrthoView_WorldToView( view.get(), inside ) ) == 0u );
        MapWorkspace_SetTool( &ws, map_tool_t::SELECT ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
        // Faces mode now picks the actual projected polygon interior. It
        // still rejects the empty corner of the enclosing bounds.
        CHECK( MapOrthoView_Pick( view.get(), MapOrthoView_WorldToView( view.get(), inside ) ) == id );
        CHECK( MapOrthoView_Pick( view.get(), MapOrthoView_WorldToView( view.get(), outside ) ) == 0u );
        Click( view.get(), MapOrthoView_WorldToView( view.get(), inside ) );
        CHECK( MapWorkspace_HasBrushFace( &ws ) ); CHECK( ws.selectedBrushFaceObject == id );
    }
}

TEST_CASE( "Select in Faces mode never starts a whole-object convenience transform", "[map][gui][views][geometry-edit][select-move][face]" )
{
    for ( bool perspective : { false, true } ) {
        CAPTURE( perspective );
        session_t session; auto &ws = session.workspace;
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, 0 } ); MapBounds_AddPoint( box, { 64, 64, 96 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
        std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &ws ) : MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
        ShowAt( view.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        const auto *document = ws.pDocument; const usize steps = EditorHistory_StepCount( &ws.history );
        QPointF body, center;
        if ( perspective ) { REQUIRE( MapCameraView_WorldToView( view.get(), { 0, 0, 96 }, &body ) ); REQUIRE( MapCameraView_WorldToView( view.get(), { 0, 0, 48 }, &center ) ); }
        else { body = MapOrthoView_WorldToView( view.get(), { -64, -64 } ); center = MapOrthoView_WorldToView( view.get(), { 0, 0 } ); }
        for ( const QPointF start : { body, center } ) {
            DragMouse( view.get(), QEvent::MouseButtonPress, start ); DragMouse( view.get(), QEvent::MouseMove, start + QPointF( 48, -32 ) );
            CHECK_FALSE( ws.editPreview.bActive ); DragMouse( view.get(), QEvent::MouseButtonRelease, start + QPointF( 48, -32 ) );
            CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
            CheckPointClose( ws.wire.bounds.box.minimum, box.box.minimum ); CheckPointClose( ws.wire.bounds.box.maximum, box.box.maximum );
            CHECK( ws.tool == map_tool_t::SELECT ); CHECK( ws.elementMode == map_element_mode_t::FACES );
        }
        if ( perspective ) { CHECK( MapWorkspace_HasBrushFace( &ws ) ); }
    }
}

TEST_CASE( "Idle cancel enters navigation without discarding selection and a second cancel clears it", "[map][gui][views][input][cancel][neutral-navigation]" )
{
    for ( const bool perspective : { false, true } ) {
        CAPTURE( perspective ); session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, 0 } ); MapBounds_AddPoint( box, { 64, 64, 128 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
        std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &ws ) : MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) ); ShowAt( view.get(), 800, 600 );
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision; const usize steps = EditorHistory_StepCount( &ws.history );
        const auto cancel = [&]() { QKeyEvent event( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( view.get(), &event ); };
        for ( u32 value = 0; value < static_cast<u32>( map_tool_t::COUNT ); ++value ) {
            const auto tool = static_cast<map_tool_t>( value );
            if ( tool == map_tool_t::NONE || !MapWorkspace_IsToolAvailable( tool ) ) { continue; }
            CAPTURE( static_cast<int>( tool ) ); MapWorkspace_SetTool( &ws, tool ); cancel();
            CHECK( ws.tool == map_tool_t::NONE ); CHECK( EditorSelection_Count( &ws.selection ) == 1u ); CHECK( EditorSelection_At( &ws.selection, 0 ) == id ); CHECK_FALSE( ws.editPreview.bActive );
        }
        u64 side = 0;
        for ( usize i = 0; i < ws.wire.faces.nCount; ++i ) { const auto &face = ws.wire.faces.pData[i]; if ( face.id == id && face.normal.z > 0.99 ) { side = face.sideId; } }
        REQUIRE( side != 0 ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES ); MapWorkspace_SelectBrushFace( &ws, id, side );
        MapWorkspace_SetTool( &ws, map_tool_t::SELECT );
        REQUIRE( MapWorkspace_HasBrushFace( &ws ) ); cancel(); CHECK( MapWorkspace_HasBrushFace( &ws ) );
        CHECK( ws.tool == map_tool_t::NONE ); CHECK( ws.selectedBrushFaceObject == id ); CHECK( ws.selectedBrushFaceSide == side );
        CHECK( EditorSelection_Count( &ws.selection ) == 1u ); CHECK( EditorSelection_At( &ws.selection, 0 ) == id ); CHECK( ws.elementMode == map_element_mode_t::FACES );
        cancel(); CHECK_FALSE( MapWorkspace_HasBrushFace( &ws ) ); CHECK( EditorSelection_Count( &ws.selection ) == 0u );
        cancel(); CHECK( EditorSelection_Count( &ws.selection ) == 0u ); CHECK( ws.tool == map_tool_t::NONE );
        CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
        CheckPointClose( ws.wire.bounds.box.minimum, box.box.minimum ); CheckPointClose( ws.wire.bounds.box.maximum, box.box.maximum );

        settings_document_t keys{};
        REQUIRE( SettingsDocument_Init( &keys, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
        const char *remapped = R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "idle_cancel" bindings = { "map.viewport" = { "map.tool.cancel" = [ "Ctrl+G" ] } } })cykv";
        const char *unbound = R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "idle_cancel" bindings = { "map.viewport" = { "map.tool.cancel" = [] } } })cykv";
        const auto alternateCancel = [&]() { QKeyEvent event( QEvent::KeyPress, Qt::Key_G, Qt::ControlModifier ); QCoreApplication::sendEvent( view.get(), &event ); };
        REQUIRE( SettingsDocument_Load( &keys, StringView_FromCString( remapped ) ).status == settings_document_status_t::OK );
        session.gui.keymapChain[0] = SettingsDocument_Root( &keys ); session.gui.nKeymapChain = 1;
        MapWorkspace_Select( &ws, id, MAP_SELECT_REPLACE ); MapWorkspace_SetTool( &ws, map_tool_t::BLOCK );
        cancel(); CHECK( ws.tool == map_tool_t::BLOCK ); CHECK( MapWorkspace_IsSelected( &ws, id ) );
        alternateCancel(); CHECK( ws.tool == map_tool_t::NONE ); CHECK( MapWorkspace_IsSelected( &ws, id ) );
        alternateCancel(); CHECK( EditorSelection_Count( &ws.selection ) == 0u );
        REQUIRE( SettingsDocument_Load( &keys, StringView_FromCString( unbound ) ).status == settings_document_status_t::OK );
        session.gui.keymapChain[0] = SettingsDocument_Root( &keys );
        MapWorkspace_Select( &ws, id, MAP_SELECT_REPLACE );
        for ( const auto tool : { map_tool_t::BLOCK, map_tool_t::SELECT, map_tool_t::NONE } ) {
            MapWorkspace_SetTool( &ws, tool ); cancel(); alternateCancel(); CHECK( ws.tool == tool ); CHECK( MapWorkspace_IsSelected( &ws, id ) );
        }
        CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == steps ); session.gui.nKeymapChain = 0;
    }
}

TEST_CASE( "Escape cancels a live or staged creation before exiting its tool and ignores cancellation repeats", "[map][gui][views][input][cancel][neutral-navigation][late-release]" )
{
    for ( const bool perspective : { false, true } ) { for ( const bool clip : { false, true } ) { for ( const bool staged : { false, true } ) {
        CAPTURE( perspective, clip, staged ); session_t session; auto &ws = session.workspace;
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, 0 } ); MapBounds_AddPoint( box, { 64, 64, 128 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
        const auto original = ObjectLineVertices( ws.wire, id ); const auto *document = ws.pDocument;
        const auto token = UndoRedo_StateToken( ws.history.pUndo ); const auto revision = document->geometry.revision;
        const usize steps = EditorHistory_StepCount( &ws.history );
        const auto tool = clip ? map_tool_t::CLIP : map_tool_t::BLOCK; MapWorkspace_SetTool( &ws, tool );
        std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &ws ) : MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
        ShowAt( view.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        const math::vec3d_t a = clip ? math::vec3d_t{ 0, -96, 64 } : math::vec3d_t{ -64, -64, 0 };
        const math::vec3d_t b = clip ? math::vec3d_t{ 0, 96, 64 } : math::vec3d_t{ 64, 64, 0 };
        QPointF start, end;
        if ( perspective ) { REQUIRE( MapCameraView_WorldToView( view.get(), a, &start ) ); REQUIRE( MapCameraView_WorldToView( view.get(), b, &end ) ); }
        else { start = MapOrthoView_WorldToView( view.get(), { a.x, a.y } ); end = MapOrthoView_WorldToView( view.get(), { b.x, b.y } ); }
        DragMouse( view.get(), QEvent::MouseButtonPress, start ); DragMouse( view.get(), QEvent::MouseMove, end );
        REQUIRE( ws.editPreview.bActive );
        if ( staged ) {
            DragMouse( view.get(), QEvent::MouseButtonRelease, end ); REQUIRE( ws.editPreview.bActive );
            CHECK( ( clip ? bool( ws.editPreview.bClip ) : bool( ws.editPreview.bStagedBlock ) ) );
        }
        QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( view.get(), &escape );
        CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.tool == tool ); CHECK( MapWorkspace_IsSelected( &ws, id ) );
        QKeyEvent repeat( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier, QString(), true );
        for ( int i = 0; i < 3; ++i ) { QCoreApplication::sendEvent( view.get(), &repeat ); }
        CHECK( ws.tool == tool ); CHECK( MapWorkspace_IsSelected( &ws, id ) );
        // An already-held button may deliver more motion and a duplicate
        // release after Escape. Neither is a new construction gesture.
        DragMouse( view.get(), QEvent::MouseMove, end + QPointF( 16, 8 ) );
        DragMouse( view.get(), QEvent::MouseButtonRelease, end + QPointF( 16, 8 ) );
        DragMouse( view.get(), QEvent::MouseButtonRelease, end );
        CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.editPreviewWire.points.nCount == 0u );
        CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
        CheckObjectVertices( ws.wire, id, original );
        QCoreApplication::sendEvent( view.get(), &escape ); CHECK( ws.tool == map_tool_t::NONE ); CHECK( MapWorkspace_IsSelected( &ws, id ) );
        QCoreApplication::sendEvent( view.get(), &repeat ); CHECK( MapWorkspace_IsSelected( &ws, id ) );
        QCoreApplication::sendEvent( view.get(), &escape ); CHECK( EditorSelection_Count( &ws.selection ) == 0u );
    } } }
}

TEST_CASE( "Escape stops a camera or orthographic pan before exiting the idle editing tool", "[map][gui][views][input][cancel][neutral-navigation][pan-priority]" )
{
    for ( const bool perspective : { false, true } ) { for ( const bool spacePan : { false, true } ) {
        CAPTURE( perspective, spacePan ); session_t session; auto &ws = session.workspace;
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, 0 } ); MapBounds_AddPoint( box, { 64, 64, 128 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
        const auto original = ObjectLineVertices( ws.wire, id ); const usize steps = EditorHistory_StepCount( &ws.history );
        std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &ws ) : MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
        ShowAt( view.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        const Qt::MouseButton button = spacePan ? Qt::LeftButton : perspective ? Qt::RightButton : Qt::MiddleButton;
        QKeyEvent space( QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier );
        if ( spacePan ) { QCoreApplication::sendEvent( view.get(), &space ); }
        const QPointF start( 300, 220 ), moved = start + QPointF( 30, 20 );
        DragButton( view.get(), QEvent::MouseButtonPress, start, button ); DragButton( view.get(), QEvent::MouseMove, moved, button );
        const auto position = perspective ? MapCameraView_Position( view.get() ) : math::vec3d_t{};
        const auto forward = perspective ? MapCameraView_Forward( view.get() ) : math::vec3d_t{};
        const QPointF center = perspective ? QPointF() : MapOrthoView_ViewToWorld( view.get(), {} );
        QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( view.get(), &escape );
        CHECK( ws.tool == map_tool_t::SELECT ); CHECK( MapWorkspace_IsSelected( &ws, id ) );
        DragButton( view.get(), QEvent::MouseMove, moved + QPointF( 80, 40 ), button );
        DragButton( view.get(), QEvent::MouseButtonRelease, moved + QPointF( 80, 40 ), button );
        if ( perspective ) { CheckPointClose( MapCameraView_Position( view.get() ), position ); CheckPointClose( MapCameraView_Forward( view.get() ), forward ); }
        else { CHECK( MapOrthoView_ViewToWorld( view.get(), {} ) == center ); }
        QKeyEvent releaseSpace( QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier );
        if ( spacePan ) { QCoreApplication::sendEvent( view.get(), &releaseSpace ); }
        CHECK_FALSE( ws.editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CheckObjectVertices( ws.wire, id, original );
        QCoreApplication::sendEvent( view.get(), &escape ); CHECK( ws.tool == map_tool_t::NONE ); CHECK( MapWorkspace_IsSelected( &ws, id ) );
    } }
}

TEST_CASE( "Escape discards a live Select transform before entering navigation and late releases cannot commit it", "[map][gui][views][input][cancel][neutral-navigation][late-release][select-move]" )
{
    for ( const bool perspective : { false, true } ) {
        CAPTURE( perspective ); session_t session; auto &ws = session.workspace;
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, 0 } ); MapBounds_AddPoint( box, { 64, 64, 96 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
        const auto original = ObjectLineVertices( ws.wire, id ); const auto *document = ws.pDocument;
        const auto token = UndoRedo_StateToken( ws.history.pUndo ); const auto revision = document->geometry.revision;
        const usize steps = EditorHistory_StepCount( &ws.history );
        std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &ws ) : MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
        ShowAt( view.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        QPointF start, end;
        if ( perspective ) {
            const f64 length = CameraGizmoLength( view.get(), { 0, 0, 48 }, session.gui.settings );
            REQUIRE( MapCameraView_WorldToView( view.get(), { length * 0.7, 0, 48 }, &start ) );
            REQUIRE( MapCameraView_WorldToView( view.get(), { length * 0.7 + 32, 0, 48 }, &end ) );
        } else {
            const f64 length = 64.0 / MapOrthoView_Zoom( view.get() );
            start = MapOrthoView_WorldToView( view.get(), { length * 0.7, 0 } );
            end = MapOrthoView_WorldToView( view.get(), { length * 0.7 + 32, 0 } );
        }
        DragMouse( view.get(), QEvent::MouseButtonPress, start ); DragMouse( view.get(), QEvent::MouseMove, end );
        REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.transform.kind == map_transform_preview_kind_t::TRANSLATE );
        CHECK( ws.editPreview.transform.delta.x == Catch::Approx( 32.0 ) );
        QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( view.get(), &escape );
        CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.tool == map_tool_t::SELECT ); CHECK( MapWorkspace_IsSelected( &ws, id ) );
        QKeyEvent repeat( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier, QString(), true ); QCoreApplication::sendEvent( view.get(), &repeat );
        CHECK( ws.tool == map_tool_t::SELECT );
        DragMouse( view.get(), QEvent::MouseMove, end + QPointF( 20, 10 ) );
        DragMouse( view.get(), QEvent::MouseButtonRelease, end + QPointF( 20, 10 ) ); DragMouse( view.get(), QEvent::MouseButtonRelease, end );
        CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.editPreviewWire.points.nCount == 0u );
        CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
        CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) ); CheckObjectVertices( ws.wire, id, original );
        QCoreApplication::sendEvent( view.get(), &escape ); CHECK( ws.tool == map_tool_t::NONE ); CHECK( MapWorkspace_IsSelected( &ws, id ) );
    }
}

TEST_CASE( "Neutral navigation hides editing controls retains selection and allows explicit tool re-entry", "[map][gui][views][input][cancel][neutral-navigation][reenter-tool]" )
{
    for ( const bool perspective : { false, true } ) {
        CAPTURE( perspective ); session_t session; auto &ws = session.workspace;
        REQUIRE( MapWorkspace_RegisterCommands( &ws, &session.gui.commands ) == command_registry_status_t::OK );
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, 0 } ); MapBounds_AddPoint( box, { 64, 64, 128 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
        const auto original = ObjectLineVertices( ws.wire, id ); const auto *document = ws.pDocument;
        const auto revision = document->geometry.revision, selection = ws.selection.revision; const usize steps = EditorHistory_StepCount( &ws.history );
        std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &ws ) : MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
        ShowAt( view.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        QPointF center;
        if ( perspective ) { REQUIRE( MapCameraView_WorldToView( view.get(), { 0, 0, 64 }, &center ) ); }
        else { center = MapOrthoView_WorldToView( view.get(), {} ); }
        const QImage editing = view->grab().toImage();
        QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( view.get(), &escape );
        REQUIRE( ws.tool == map_tool_t::NONE );
        CHECK( ChangedPixelsNear( editing, view->grab().toImage(), center, 8 ) > 0 );
        for ( const char *command : { "map.tool.select", "map.tool.translate", "map.tool.rotate", "map.tool.scale", "map.tool.block", "map.tool.clip" } ) {
            CHECK( ( EditorCommands_State( &session.gui.commands, StringView_FromCString( command ) ) & COMMAND_STATE_CHECKED ) == 0u );
        }
        CHECK( ( EditorCommands_State( &session.gui.commands, StringView_FromCString( "map.tool.navigation" ) ) & COMMAND_STATE_CHECKED ) != 0u );
        const QPointF beforePan = perspective ? QPointF() : MapOrthoView_ViewToWorld( view.get(), {} );
        DragMouse( view.get(), QEvent::MouseButtonPress, center ); DragMouse( view.get(), QEvent::MouseMove, center + QPointF( 30, 20 ) );
        DragMouse( view.get(), QEvent::MouseButtonRelease, center + QPointF( 30, 20 ) );
        if ( !perspective ) { CHECK( MapOrthoView_ViewToWorld( view.get(), {} ) != beforePan ); }
        CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
        CHECK( ws.selection.revision == selection ); CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CheckObjectVertices( ws.wire, id, original );
        if ( perspective ) {
            QKeyEvent forward( QEvent::KeyPress, Qt::Key_W, Qt::NoModifier ); QCoreApplication::sendEvent( view.get(), &forward );
            CHECK( TestDistance( MapCameraView_NavigationVelocity( view.get() ), {} ) > 0 );
            QKeyEvent release( QEvent::KeyRelease, Qt::Key_W, Qt::NoModifier ); QCoreApplication::sendEvent( view.get(), &release );
            CheckPointClose( MapCameraView_NavigationVelocity( view.get() ), {} );
        } else {
            QKeyEvent right( QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier ); QCoreApplication::sendEvent( view.get(), &right );
            CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CheckObjectVertices( ws.wire, id, original );
        }
        // Shift+S is the ordinary Select shortcut and overlaps held S flight.
        // An explicit tool chord must still win in neutral navigation.
        QKeyEvent chooseSelect( QEvent::KeyPress, Qt::Key_S, Qt::ShiftModifier ); QCoreApplication::sendEvent( view.get(), &chooseSelect );
        CHECK( ws.tool == map_tool_t::SELECT ); CHECK( MapWorkspace_IsSelected( &ws, id ) );
        if ( perspective ) { CheckPointClose( MapCameraView_NavigationVelocity( view.get() ), {} ); }
        QKeyEvent releaseSelect( QEvent::KeyRelease, Qt::Key_S, Qt::ShiftModifier ); QCoreApplication::sendEvent( view.get(), &releaseSelect );
        REQUIRE( EditorCommands_Execute( &session.gui.commands, StringView_FromCString( "map.tool.navigation" ), {} ) == command_result_t::OK );
        CHECK( ws.tool == map_tool_t::NONE ); CHECK( MapWorkspace_IsSelected( &ws, id ) );
        REQUIRE( EditorCommands_Execute( &session.gui.commands, StringView_FromCString( "map.tool.select" ), {} ) == command_result_t::OK );
        CHECK( ws.tool == map_tool_t::SELECT ); CHECK( MapWorkspace_IsSelected( &ws, id ) );
    }
}

TEST_CASE( "Select handle modifiers clone or bypass snapping without toggling body selection", "[map][gui][views][geometry-edit][select-move][gizmo][snap]" )
{
    for ( bool perspective : { false, true } ) {
        for ( const Qt::KeyboardModifiers modifiers : { Qt::KeyboardModifiers( Qt::ShiftModifier ), Qt::KeyboardModifiers( Qt::ControlModifier ) } ) {
            CAPTURE( perspective, static_cast<int>( modifiers ) );
            session_t session; auto &ws = session.workspace;
            REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
            map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, 0 } ); MapBounds_AddPoint( box, { 64, 64, 96 } );
            REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 original = EditorSelection_At( &ws.selection, 0 );
            MapWorkspace_SetGridSize( &ws, 16 );
            std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &ws ) : MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
            ShowAt( view.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
            QPointF start, end;
            if ( perspective ) {
                const math::vec3d_t pivot{ 0, 0, 48 }; const f64 length = CameraGizmoLength( view.get(), pivot, session.gui.settings );
                REQUIRE( MapCameraView_WorldToView( view.get(), { length * 0.7, 0, 48 }, &start ) );
                REQUIRE( MapCameraView_WorldToView( view.get(), { length * 0.7 + 21, 0, 48 }, &end ) );
            } else {
                const f64 length = 64.0 / MapOrthoView_Zoom( view.get() );
                start = MapOrthoView_WorldToView( view.get(), { length * 0.7, 0 } ); end = MapOrthoView_WorldToView( view.get(), { length * 0.7 + 21, 0 } );
            }
            const usize steps = EditorHistory_StepCount( &ws.history );
            DragMouse( view.get(), QEvent::MouseButtonPress, start, modifiers ); DragMouse( view.get(), QEvent::MouseMove, end, modifiers );
            REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.wire.objects.nCount == 1u ); CHECK( EditorSelection_At( &ws.selection, 0 ) == original );
            DragMouse( view.get(), QEvent::MouseButtonRelease, end, modifiers );
            const bool clone = modifiers == Qt::ShiftModifier;
            CHECK( ws.wire.objects.nCount == ( clone ? 2u : 1u ) ); CHECK( ws.tool == map_tool_t::SELECT );
            CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
            REQUIRE( EditorSelection_Count( &ws.selection ) == 1u ); CHECK( ( EditorSelection_At( &ws.selection, 0 ) != original ) == clone );
            CHECK( std::abs( MapViews_SelectionGeometryBounds( &ws ).box.minimum.x - ( clone ? -48.0 : -43.0 ) ) < 1e-6 );
            CHECK( ws.bSnapToGrid ); CHECK( ws.gridSize == 16.0 );
            REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
            CHECK( ws.wire.objects.nCount == 1u ); CHECK( EditorSelection_At( &ws.selection, 0 ) == original );
            CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, box.box.minimum );
        }
    }
}

TEST_CASE( "Viewport drags cancel when editing context or the view projection changes", "[map][gui][views][geometry-edit][gesture-context]" )
{
    const auto exercise = []( bool perspective, const QString &change ) {
        CAPTURE( perspective, change );
        session_t session;
        REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
        map_bounds_t first{}; MapBounds_AddPoint( first, { -64, -64, 0 } ); MapBounds_AddPoint( first, { 64, 64, 64 } );
        map_bounds_t second{}; MapBounds_AddPoint( second, { 256, -64, 0 } ); MapBounds_AddPoint( second, { 384, 64, 64 } );
        REQUIRE( MapWorkspace_CreateBox( &session.workspace, first ) );
        const u64 firstId = EditorSelection_At( &session.workspace.selection, 0 );
        REQUIRE( MapWorkspace_CreateBox( &session.workspace, second ) );
        const u64 secondId = EditorSelection_At( &session.workspace.selection, 0 );
        MapWorkspace_Select( &session.workspace, firstId, MAP_SELECT_REPLACE );
        MapWorkspace_SetTool( &session.workspace, map_tool_t::TRANSLATE );
        std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &session.workspace ) :
                                                   MapOrthoView_Create( nullptr, &session.workspace, map_ortho_axes_t::TOP ) );
        ShowAt( view.get(), 800, 600 );
        MapWorkspace_Frame( &session.workspace, CY_TRUE ); QCoreApplication::processEvents();
        QPointF start;
        if ( perspective ) { REQUIRE( MapCameraView_WorldToView( view.get(), { 0, 0, 64 }, &start ) ); }
        else { start = MapOrthoView_WorldToView( view.get(), { -64, -64 } ); }
        const QPointF end = start + QPointF( 40, -32 );
        QMouseEvent press( QEvent::MouseButtonPress, start, view->mapToGlobal( start ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
        QCoreApplication::sendEvent( view.get(), &press );
        QMouseEvent move( QEvent::MouseMove, end, view->mapToGlobal( end ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier );
        QCoreApplication::sendEvent( view.get(), &move );
        REQUIRE( session.workspace.editPreview.bActive );
        const usize steps = EditorHistory_StepCount( &session.workspace.history );
        if ( change == QStringLiteral( "selection" ) ) { MapWorkspace_Select( &session.workspace, secondId, MAP_SELECT_REPLACE ); }
        else if ( change == QStringLiteral( "tool" ) ) { MapWorkspace_SetTool( &session.workspace, map_tool_t::SCALE ); }
        else if ( change == QStringLiteral( "mode" ) ) { MapWorkspace_SetElementMode( &session.workspace, map_element_mode_t::GROUPS ); }
        else if ( change == QStringLiteral( "document" ) ) { REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK ); }
        else if ( change == QStringLiteral( "wheel" ) ) { Wheel( view.get() ); }
        else if ( change == QStringLiteral( "frame" ) ) { MapWorkspace_Frame( &session.workspace, CY_TRUE ); }
        else if ( change == QStringLiteral( "resize" ) ) { view->resize( 820, 620 ); }
        else if ( change == QStringLiteral( "pan" ) || change == QStringLiteral( "look" ) ) {
            const Qt::MouseButton navigationButton = perspective ? Qt::RightButton : Qt::MiddleButton;
            QMouseEvent navigation( QEvent::MouseButtonPress, end, view->mapToGlobal( end ), navigationButton,
                                    Qt::LeftButton | navigationButton, Qt::NoModifier );
            QCoreApplication::sendEvent( view.get(), &navigation );
        }
        else if ( change == QStringLiteral( "render" ) ) { MapCameraView_SetRenderMode( view.get(), map_render_mode_t::SHADED ); }
        else if ( change == QStringLiteral( "fov" ) ) { view_settings_t settings( &session.gui.settings ); settings.Real( "editor.camera.fov", 90.0 ); }
        else { QFocusEvent focusOut( QEvent::FocusOut ); QCoreApplication::sendEvent( view.get(), &focusOut ); }
        CHECK_FALSE( session.workspace.editPreview.bActive );
        // A stale release must not apply the old delta to a replacement selection.
        QMouseEvent release( QEvent::MouseButtonRelease, end, view->mapToGlobal( end ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
        QCoreApplication::sendEvent( view.get(), &release );
        CHECK_FALSE( session.workspace.editPreview.bActive );
        CHECK( EditorHistory_StepCount( &session.workspace.history ) == ( change == QStringLiteral( "document" ) ? 0u : steps ) );
        if ( change == QStringLiteral( "document" ) ) { CHECK( session.workspace.wire.objects.nCount == 0u ); }
        else {
            const auto *firstObject = MapWireframe_FindObject( session.workspace.wire, firstId );
            const auto *secondObject = MapWireframe_FindObject( session.workspace.wire, secondId );
            REQUIRE( firstObject != nullptr ); REQUIRE( secondObject != nullptr );
            CHECK( firstObject->bounds.box.minimum.x == -64 ); CHECK( firstObject->bounds.box.maximum.z == 64 );
            CHECK( secondObject->bounds.box.minimum.x == 256 ); CHECK( secondObject->bounds.box.maximum.z == 64 );
        }
    };
    SECTION( "Orthographic" ) {
        for ( const QString &change : { QStringLiteral( "selection" ), QStringLiteral( "tool" ), QStringLiteral( "mode" ), QStringLiteral( "document" ), QStringLiteral( "focus" ),
                                      QStringLiteral( "pan" ), QStringLiteral( "wheel" ), QStringLiteral( "frame" ), QStringLiteral( "resize" ) } ) {
            exercise( false, change );
        }
    }
    SECTION( "Perspective" ) {
        for ( const QString &change : { QStringLiteral( "selection" ), QStringLiteral( "tool" ), QStringLiteral( "mode" ), QStringLiteral( "document" ), QStringLiteral( "focus" ),
                                      QStringLiteral( "look" ), QStringLiteral( "wheel" ), QStringLiteral( "frame" ), QStringLiteral( "resize" ), QStringLiteral( "render" ), QStringLiteral( "fov" ) } ) {
            exercise( true, change );
        }
    }
}

TEST_CASE( "Press-time object selection establishes the transform drag context", "[map][gui][views][geometry-edit][gesture-context]" )
{
    session_t session; REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, 0 } ); MapBounds_AddPoint( box, { 64, 64, 64 } );
    REQUIRE( MapWorkspace_CreateBox( &session.workspace, box ) );
    const u64 id = EditorSelection_At( &session.workspace.selection, 0 );
    MapWorkspace_Select( &session.workspace, 0u, MAP_SELECT_REPLACE );
    MapWorkspace_SetTool( &session.workspace, map_tool_t::TRANSLATE );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &session.workspace, map_ortho_axes_t::TOP ) ); ShowAt( top.get(), 800, 600 );
    const QPointF start = MapOrthoView_WorldToView( top.get(), { -64, -64 } );
    const QPointF end = MapOrthoView_WorldToView( top.get(), { -32, -16 } );
    QMouseEvent press( QEvent::MouseButtonPress, start, top->mapToGlobal( start ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier ); QCoreApplication::sendEvent( top.get(), &press );
    REQUIRE( EditorSelection_At( &session.workspace.selection, 0 ) == id );
    QMouseEvent move( QEvent::MouseMove, end, top->mapToGlobal( end ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier ); QCoreApplication::sendEvent( top.get(), &move );
    REQUIRE( session.workspace.editPreview.bActive );
    QMouseEvent release( QEvent::MouseButtonRelease, end, top->mapToGlobal( end ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier ); QCoreApplication::sendEvent( top.get(), &release );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == 2u );
    CHECK( MapViews_SelectionGeometryBounds( &session.workspace ).box.minimum.x == -32 );
}

TEST_CASE( "Replacing a pane projection discards its geometry preview", "[map][gui][views][geometry-edit][gesture-context]" )
{
    session_t session; REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, 0 } ); MapBounds_AddPoint( box, { 64, 64, 64 } );
    REQUIRE( MapWorkspace_CreateBox( &session.workspace, box ) );
    MapWorkspace_SetTool( &session.workspace, map_tool_t::TRANSLATE );
    std::unique_ptr<QWidget> views( MapViews_Create( nullptr, &session.workspace ) ); ShowAt( views.get(), 1200, 900 );
    QWidget *top = MapViews_PaneView( views.get(), 1 );
    REQUIRE( top != nullptr );
    const QPointF start = MapOrthoView_WorldToView( top, { -64, -64 } );
    const QPointF end = MapOrthoView_WorldToView( top, { -32, -16 } );
    QMouseEvent press( QEvent::MouseButtonPress, start, top->mapToGlobal( start ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier ); QCoreApplication::sendEvent( top, &press );
    QMouseEvent move( QEvent::MouseMove, end, top->mapToGlobal( end ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier ); QCoreApplication::sendEvent( top, &move );
    REQUIRE( session.workspace.editPreview.bActive );
    MapViews_SetPaneView( views.get(), 1, map_view_type_t::CAMERA, map_render_mode_t::WIREFRAME );
    CHECK_FALSE( session.workspace.editPreview.bActive );
    QWidget *replacement = MapViews_PaneView( views.get(), 1 ); REQUIRE( replacement != nullptr );
    QMouseEvent release( QEvent::MouseButtonRelease, end, replacement->mapToGlobal( end ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier ); QCoreApplication::sendEvent( replacement, &release );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == 1u );
    CHECK( MapViews_SelectionGeometryBounds( &session.workspace ).box.minimum.x == -64 );
}

TEST_CASE( "Faces mode cannot move the brush root through an object translate handle over its surface", "[map][gui][views][geometry-edit][face][gesture-context][component-transform-safety]" )
{
    session_t session; REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, 0 } ); MapBounds_AddPoint( box, { 64, 64, 64 } );
    REQUIRE( MapWorkspace_CreateBox( &session.workspace, box ) );
    const u64 id = EditorSelection_At( &session.workspace.selection, 0 );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &session.workspace ) ); ShowAt( camera.get(), 800, 600 );
    MapWorkspace_Frame( &session.workspace, CY_TRUE ); QCoreApplication::processEvents();
    MapWorkspace_SetElementMode( &session.workspace, map_element_mode_t::FACES );
    QPointF top; REQUIRE( MapCameraView_WorldToView( camera.get(), { 0, 0, 64 }, &top ) );
    Click( camera.get(), top ); REQUIRE( MapWorkspace_HasBrushFace( &session.workspace ) );
    const u64 side = session.workspace.selectedBrushFaceSide;
    MapWorkspace_SetTool( &session.workspace, map_tool_t::TRANSLATE );
    const math::vec3d_t pivot{ 0, 0, 32 };
    const auto position = MapCameraView_Position( camera.get() ), forward = MapCameraView_Forward( camera.get() );
    const f64 depth = ( pivot.x - position.x ) * forward.x + ( pivot.y - position.y ) * forward.y + ( pivot.z - position.z ) * forward.z;
    const f64 fov = EditorSettings_Real( &session.gui.settings, "editor.camera.fov", 75.0 );
    const f64 focal = camera->width() * 0.5 / std::tan( fov * 3.14159265358979323846 / 360.0 );
    const f64 length = std::max( depth, 1.0 ) * 72.0 / focal;
    QPointF start, end;
    // The former whole-object X handle lies over the brush's visible surface.
    // Faces mode must not reinterpret a component gesture as a root edit while
    // a real component Translate adapter is unavailable.
    REQUIRE( MapCameraView_WorldToView( camera.get(), { length * 0.5, 0, 32 }, &start ) );
    REQUIRE( MapCameraView_WorldToView( camera.get(), { length * 0.5 + 32, 0, 32 }, &end ) );
    REQUIRE( MapCameraView_Pick( camera.get(), start ) == id );
    const auto *document = session.workspace.pDocument;
    const auto revision = document->geometry.revision;
    const auto original = MapWireframe_FindObject( session.workspace.wire, id )->bounds;
    const auto beforePoints = ObjectLineVertices( session.workspace.wire, id );
    QMouseEvent press( QEvent::MouseButtonPress, start, camera->mapToGlobal( start ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &press );
    QMouseEvent move( QEvent::MouseMove, end, camera->mapToGlobal( end ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &move );
    CHECK_FALSE( session.workspace.editPreview.bActive );
    QMouseEvent release( QEvent::MouseButtonRelease, end, camera->mapToGlobal( end ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &release );
    CHECK( session.workspace.pDocument == document );
    CHECK( document->geometry.revision == revision );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == 1u );
    const auto retained = MapWireframe_FindObject( session.workspace.wire, id )->bounds;
    CheckPointClose( retained.box.minimum, original.box.minimum ); CheckPointClose( retained.box.maximum, original.box.maximum );
    CheckObjectVertices( session.workspace.wire, id, beforePoints );
    CHECK( session.workspace.selectedBrushFaceSide == side );
    // The identical surface-covered handle still performs a whole-root move
    // after an explicit switch to Objects. The component selection is cleared;
    // authored face identity and undo remain intact.
    MapWorkspace_SetElementMode( &session.workspace, map_element_mode_t::OBJECTS );
    DragMouse( camera.get(), QEvent::MouseButtonPress, start );
    DragMouse( camera.get(), QEvent::MouseMove, end );
    REQUIRE( session.workspace.editPreview.bActive );
    DragMouse( camera.get(), QEvent::MouseButtonRelease, end );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == 2u );
    const auto result = MapViews_SelectionGeometryBounds( &session.workspace );
    CHECK( result.box.minimum.x > -64 ); CHECK( result.box.minimum.y == -64 ); CHECK( result.box.minimum.z == 0 );
    CHECK( session.workspace.selectedBrushFaceSide == 0u );
    CHECK( BrushSideInDirection( session.workspace, id, 2, 1 ) == side );
    REQUIRE( MapWorkspace_Undo( &session.workspace ) == editor_history_status_t::OK );
    CHECK( MapViews_SelectionGeometryBounds( &session.workspace ).box.minimum.x == -64 );
    CHECK( session.workspace.selectedBrushFaceSide == 0u );
    CHECK( BrushSideInDirection( session.workspace, id, 2, 1 ) == side );
}

TEST_CASE( "3D face picking pushes the selected authored plane and retains face identity on undo", "[map][gui][views][geometry-edit][face]" )
{
    session_t session; REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, 0 } ); MapBounds_AddPoint( box, { 64, 64, 64 } );
    REQUIRE( MapWorkspace_CreateBox( &session.workspace, box ) );
    const auto id = EditorSelection_At( &session.workspace.selection, 0 );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &session.workspace ) ); ShowAt( camera.get(), 800, 600 );
    MapWorkspace_Frame( &session.workspace, CY_TRUE ); QCoreApplication::processEvents();
    MapWorkspace_SetElementMode( &session.workspace, map_element_mode_t::FACES );
    QPointF top; REQUIRE( MapCameraView_WorldToView( camera.get(), { 0, 0, 64 }, &top ) );
    Click( camera.get(), top );
    REQUIRE( MapWorkspace_HasBrushFace( &session.workspace ) );
    CHECK( session.workspace.selectedBrushFaceObject == id );
    const auto side = session.workspace.selectedBrushFaceSide;
    REQUIRE( MapWorkspace_PushPullFace( &session.workspace, 32 ) );
    CHECK( MapViews_SelectionGeometryBounds( &session.workspace ).box.maximum.z == 96 );
    CHECK( session.workspace.selectedBrushFaceSide == side );
    REQUIRE( MapWorkspace_Undo( &session.workspace ) == editor_history_status_t::OK );
    CHECK( MapViews_SelectionGeometryBounds( &session.workspace ).box.maximum.z == 64 );
    CHECK( session.workspace.selectedBrushFaceSide == side );
    const auto steps = EditorHistory_StepCount( &session.workspace.history );
    CHECK_FALSE( MapWorkspace_PushPullFace( &session.workspace, -1000 ) );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == steps );
    CHECK( MapViews_SelectionGeometryBounds( &session.workspace ).box.maximum.z == 64 );
}

TEST_CASE( "The visible push pull handle edits its selected face outside the brush silhouette", "[map][gui][views][geometry-edit][face][gizmo]" )
{
    session_t session; REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    map_bounds_t first{}; MapBounds_AddPoint( first, { -16, -16, 0 } ); MapBounds_AddPoint( first, { 16, 16, 32 } );
    map_bounds_t second{}; MapBounds_AddPoint( second, { 256, -16, 0 } ); MapBounds_AddPoint( second, { 288, 16, 32 } );
    REQUIRE( MapWorkspace_CreateBox( &session.workspace, first ) );
    const u64 firstId = EditorSelection_At( &session.workspace.selection, 0 );
    REQUIRE( MapWorkspace_CreateBox( &session.workspace, second ) );
    const u64 secondId = EditorSelection_At( &session.workspace.selection, 0 );
    const auto *brush = geometry::GeometryDocument_FindBrush( &session.workspace.pDocument->geometry, { firstId } ); REQUIRE( brush != nullptr );
    u64 topSide = 0u;
    for ( usize i = 0; i < brush->sides.nCount; ++i ) {
        if ( brush->sides.pData[i].plane.normal.z > 0.99 ) { topSide = brush->sides.pData[i].sourceId.value; }
    }
    REQUIRE( topSide != 0u );
    MapWorkspace_SelectBrushFace( &session.workspace, firstId, topSide );
    MapWorkspace_SetTool( &session.workspace, map_tool_t::EXTRUDE );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &session.workspace ) ); ShowAt( camera.get(), 800, 600 );
    MapWorkspace_Frame( &session.workspace, CY_TRUE ); QCoreApplication::processEvents();
    // Move back until the constant-size handle extends beyond the small brush.
    const QPointF center( camera->width() * 0.5, camera->height() * 0.5 );
    QWheelEvent zoomOut( center, camera->mapToGlobal( center ), QPoint(), QPoint( 0, -1200 ), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false );
    QCoreApplication::sendEvent( camera.get(), &zoomOut );
    const auto position = MapCameraView_Position( camera.get() ), forward = MapCameraView_Forward( camera.get() );
    const math::vec3d_t pivot{ 0, 0, 16 };
    const f64 depth = ( pivot.x - position.x ) * forward.x + ( pivot.y - position.y ) * forward.y + ( pivot.z - position.z ) * forward.z;
    const f64 fov = EditorSettings_Real( &session.gui.settings, "editor.camera.fov", 75.0 );
    const f64 focal = camera->width() * 0.5 / std::tan( fov * 3.14159265358979323846 / 360.0 );
    const f64 length = std::max( depth, 1.0 ) * 72.0 / focal;
    QPointF origin, tip;
    REQUIRE( MapCameraView_WorldToView( camera.get(), { 0, 0, 32 }, &origin ) );
    REQUIRE( MapCameraView_WorldToView( camera.get(), { 0, 0, 32 + length }, &tip ) );
    QPointF start = tip;
    SECTION( "End cap" ) { start = tip; }
    SECTION( "Outer shaft" ) { start = origin + ( tip - origin ) * 0.75; }
    REQUIRE( camera->rect().contains( start.toPoint() ) );
    REQUIRE( MapCameraView_Pick( camera.get(), start ) == 0u );
    const QPointF end = start + ( tip - origin ) * 0.5;
    QMouseEvent press( QEvent::MouseButtonPress, start, camera->mapToGlobal( start ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &press );
    QMouseEvent move( QEvent::MouseMove, end, camera->mapToGlobal( end ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &move );
    REQUIRE( session.workspace.editPreview.bActive );
    CHECK( session.workspace.selectedBrushFaceObject == firstId ); CHECK( session.workspace.selectedBrushFaceSide == topSide );
    CHECK( MapViews_SelectionGeometryBounds( &session.workspace ).box.maximum.z == 32 );
    QMouseEvent release( QEvent::MouseButtonRelease, end, camera->mapToGlobal( end ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &release );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == 3u );
    CHECK( MapViews_SelectionGeometryBounds( &session.workspace ).box.maximum.z > 32 );
    CHECK( session.workspace.selectedBrushFaceSide == topSide );
    REQUIRE( MapWorkspace_Undo( &session.workspace ) == editor_history_status_t::OK );
    CHECK( MapViews_SelectionGeometryBounds( &session.workspace ).box.maximum.z == 32 );
    CHECK( session.workspace.selectedBrushFaceSide == topSide );

    // A different face away from this gizmo still uses ordinary ray picking.
    QPointF otherTop; REQUIRE( MapCameraView_WorldToView( camera.get(), { 272, 0, 32 }, &otherTop ) );
    REQUIRE( MapCameraView_Pick( camera.get(), otherTop ) == secondId );
    Click( camera.get(), otherTop );
    CHECK( session.workspace.selectedBrushFaceObject == secondId );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == 3u );
}

namespace
{
u64 BrushSideInDirection( const map_workspace_t &ws, u64 object, u32 axis, int sign )
{
    const auto *brush = geometry::GeometryDocument_FindBrush( &ws.pDocument->geometry, { object } ); REQUIRE( brush != nullptr );
    for ( usize i = 0; i < brush->sides.nCount; ++i ) {
        const auto &side = brush->sides.pData[i];
        if ( TestCoordinate( side.plane.normal, axis ) * sign > 0.99 ) { return side.sourceId.value; }
    }
    FAIL( "The authored box must have a side in the requested signed axis" );
    return 0;
}
}

TEST_CASE( "Camera push pull follows exact grid 64 world travel in both face directions through a multistep perspective drag", "[map][gui][views][geometry-edit][face][gizmo][grid64][push-pull]" )
{
    for ( const bool offGrid : { false, true } ) { for ( u32 axis = 0; axis < 3; ++axis ) {
        for ( const int sign : { -1, 1 } ) { for ( const int travelSign : { -1, 1 } ) {
            CAPTURE( offGrid, axis, sign, travelSign );
            session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
            map_bounds_t box{};
            MapBounds_AddPoint( box, offGrid ? math::vec3d_t{ -183, -171, -157 } : math::vec3d_t{ -192, -192, -192 } );
            MapBounds_AddPoint( box, offGrid ? math::vec3d_t{ 201, 213, 227 } : math::vec3d_t{ 192, 192, 192 } );
            REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
            const auto original = ObjectLineVertices( ws.wire, id ); const u64 side = BrushSideInDirection( ws, id, axis, sign );
            // Display coarsening must never become a different authoring step.
            view_settings_t settings( &session.gui.settings ); settings.Integer( "editor.grid.size", 64 ); settings.Set( "editor.grid.snap", true );
            settings.Set( "editor.grid.adaptive_3d", true );
            settings.Integer( "editor.grid.min_spacing_px", 64 );
            std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
            MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
            MapWorkspace_SelectBrushFace( &ws, id, side ); MapWorkspace_SetTool( &ws, map_tool_t::EXTRUDE );
            auto origin = MapBounds_Center( box );
            SetTestCoordinate( origin, axis, TestCoordinate( sign > 0 ? box.box.maximum : box.box.minimum, axis ) );
            QPointF start; REQUIRE( MapCameraView_WorldToView( camera.get(), origin, &start ) );
            REQUIRE( camera->rect().adjusted( 8, 8, -8, -8 ).contains( start.toPoint() ) );
            const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
            const usize applied = EditorHistory_AppliedStepCount( &ws.history ); const usize steps = EditorHistory_StepCount( &ws.history );
            const auto cameraPosition = MapCameraView_Position( camera.get() );
            DragMouse( camera.get(), QEvent::MouseButtonPress, start ); CHECK_FALSE( ws.editPreview.bActive );
            map_bounds_t expected = box; QPointF end;
            for ( const f64 cells : { 1.0, 2.0, 3.0 } ) {
                const f64 distance = travelSign * cells * 64.0;
                // A partial cell also distinguishes the actual 64-unit step
                // from a stale 16-unit step, even when both divide the final
                // whole-cell dimensions.
                const f64 pointerDistance = travelSign * ( cells * 64.0 - 13.0 );
                auto target = origin; SetTestCoordinate( target, axis, TestCoordinate( origin, axis ) + sign * pointerDistance );
                REQUIRE( MapCameraView_WorldToView( camera.get(), target, &end ) );
                // Project the true world target rather than scaling a screen
                // arrow. Three cells vary camera depth enough to expose the
                // old linear pixels-per-unit approximation.
                DragMouse( camera.get(), QEvent::MouseMove, end );
                expected = box;
                SetTestCoordinate( sign > 0 ? expected.box.maximum : expected.box.minimum, axis, TestCoordinate( origin, axis ) + sign * distance );
                REQUIRE( ws.editPreview.bActive ); CHECK( ws.editPreview.transform.kind == map_transform_preview_kind_t::NONE );
                CheckPointClose( ws.editPreview.bounds.box.minimum, expected.box.minimum );
                CheckPointClose( ws.editPreview.bounds.box.maximum, expected.box.maximum );
                CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
                CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( EditorHistory_AppliedStepCount( &ws.history ) == applied );
                CheckObjectVertices( ws.wire, id, original ); CheckPointClose( MapCameraView_Position( camera.get() ), cameraPosition );
                CHECK( ws.selectedBrushFaceObject == id ); CHECK( ws.selectedBrushFaceSide == side ); CHECK( ws.gridSize == 64 );
            }
            DragMouse( camera.get(), QEvent::MouseButtonRelease, end ); CHECK_FALSE( ws.editPreview.bActive );
            CHECK( EditorHistory_StepCount( &ws.history ) == applied + 1u ); CHECK( EditorHistory_AppliedStepCount( &ws.history ) == applied + 1u );
            // Faces mode reports the selected face's dimensions. Confirm the
            // authored solid independently so its opposite plane is checked.
            CheckPointClose( ObjectWireBounds( ws.wire, id ).box.minimum, expected.box.minimum );
            CheckPointClose( ObjectWireBounds( ws.wire, id ).box.maximum, expected.box.maximum );
            CHECK( ws.selectedBrushFaceObject == id ); CHECK( ws.selectedBrushFaceSide == side ); CHECK( ws.tool == map_tool_t::EXTRUDE );
            const auto committed = ObjectLineVertices( ws.wire, id );
            REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CheckObjectVertices( ws.wire, id, original );
            CHECK( ws.selectedBrushFaceObject == id ); CHECK( ws.selectedBrushFaceSide == side );
            REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CheckObjectVertices( ws.wire, id, committed );
            CheckPointClose( ObjectWireBounds( ws.wire, id ).box.minimum, expected.box.minimum );
            CheckPointClose( ObjectWireBounds( ws.wire, id ).box.maximum, expected.box.maximum );
            if ( offGrid && axis == 2u && sign == -1 && travelSign == 1 ) {
                QTemporaryDir folder; REQUIRE( folder.isValid() ); const QString path = folder.filePath( QStringLiteral( "push_pull_grid64.cymap" ) );
                REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
                map_document_t loaded{}; REQUIRE( MapFiles_Load( &loaded, Allocator_GetSystem(), path.toUtf8().constData() ).status == map_files_status_t::OK );
                map_wireframe_t loadedWire{}; REQUIRE( MapWireframe_Init( &loadedWire, Allocator_GetSystem() ) );
                REQUIRE( MapWireframe_Build( &loadedWire, loaded ) == map_status_t::OK ); CheckObjectVertices( loadedWire, id, committed );
                CheckPointClose( loadedWire.bounds.box.minimum, expected.box.minimum ); CheckPointClose( loadedWire.bounds.box.maximum, expected.box.maximum );
            }
        } }
    } }
}

TEST_CASE( "Camera face push pull uses live snap bypass and clears zero motion without publishing an undo step", "[map][gui][views][geometry-edit][face][grid64][push-pull][snap]" )
{
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -183, -171, -157 } ); MapBounds_AddPoint( box, { 201, 213, 227 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
    view_settings_t settings( &session.gui.settings ); settings.Integer( "editor.grid.size", 64 ); settings.Set( "editor.grid.snap", true );
    const u64 side = BrushSideInDirection( ws, id, 2u, 1 ); const auto original = ObjectLineVertices( ws.wire, id );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE );
    MapWorkspace_SelectBrushFace( &ws, id, side ); MapWorkspace_SetTool( &ws, map_tool_t::EXTRUDE );
    auto origin = MapBounds_Center( box ); origin.z = box.box.maximum.z;
    QPointF start, end; REQUIRE( MapCameraView_WorldToView( camera.get(), origin, &start ) );
    auto target = origin; target.z += 69.25; REQUIRE( MapCameraView_WorldToView( camera.get(), target, &end ) );
    const auto *document = ws.pDocument; const usize steps = EditorHistory_StepCount( &ws.history );
    DragMouse( camera.get(), QEvent::MouseButtonPress, start ); DragMouse( camera.get(), QEvent::MouseMove, end );
    REQUIRE( ws.editPreview.bActive ); CHECK( std::abs( ws.editPreview.bounds.box.maximum.z - ( box.box.maximum.z + 64 ) ) < 1e-6 );
    for ( const Qt::KeyboardModifiers modifier : { Qt::KeyboardModifiers( Qt::ControlModifier ), Qt::KeyboardModifiers( Qt::MetaModifier ) } ) {
        CAPTURE( static_cast<int>( modifier ) );
        DragMouse( camera.get(), QEvent::MouseMove, end, modifier ); REQUIRE( ws.editPreview.bActive );
        CHECK( std::abs( ws.editPreview.bounds.box.maximum.z - target.z ) < 1e-6 );
        DragMouse( camera.get(), QEvent::MouseMove, end ); REQUIRE( ws.editPreview.bActive );
        CHECK( std::abs( ws.editPreview.bounds.box.maximum.z - ( box.box.maximum.z + 64 ) ) < 1e-6 );
    }
    DragMouse( camera.get(), QEvent::MouseMove, start ); CHECK_FALSE( ws.editPreview.bActive );
    DragMouse( camera.get(), QEvent::MouseButtonRelease, start ); CHECK_FALSE( ws.editPreview.bActive );
    CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CheckObjectVertices( ws.wire, id, original );
    CHECK( ws.bSnapToGrid ); CHECK( ws.gridSize == 64 ); CHECK( ws.selectedBrushFaceSide == side );
}

TEST_CASE( "An invalid perspective face constraint cancels the last valid push pull before a late release", "[map][gui][views][geometry-edit][face][grid64][push-pull][invalid]" )
{
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -192, -192, -192 } ); MapBounds_AddPoint( box, { 192, 192, 192 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
    view_settings_t settings( &session.gui.settings ); settings.Integer( "editor.grid.size", 64 ); settings.Set( "editor.grid.snap", true );
    const u64 side = BrushSideInDirection( ws, id, 2u, 1 ); const auto original = ObjectLineVertices( ws.wire, id );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE );
    MapWorkspace_SelectBrushFace( &ws, id, side ); MapWorkspace_SetTool( &ws, map_tool_t::EXTRUDE );
    const math::vec3d_t origin{ 0, 0, 192 }; QPointF start, end;
    REQUIRE( MapCameraView_WorldToView( camera.get(), origin, &start ) ); REQUIRE( MapCameraView_WorldToView( camera.get(), { 0, 0, 256 }, &end ) );
    const auto position = MapCameraView_Position( camera.get() ), forward = MapCameraView_Forward( camera.get() );
    const f64 horizontal = std::hypot( forward.x, forward.y ); REQUIRE( horizontal > 0 );
    const math::vec3d_t right{ forward.y / horizontal, -forward.x / horizontal, 0 };
    const auto up = math::Vec3d_Cross( right, forward );
    // The face travels vertically. Its constraint plane contains that axis
    // and the pickup point; its horizon is a ray parallel to this plane.
    const math::vec3d_t constraint{ origin.x - position.x, origin.y - position.y, 0 };
    const auto dot = []( math::vec3d_t a, math::vec3d_t b ) { return a.x * b.x + a.y * b.y + a.z * b.z; };
    const f64 denominator = dot( up, constraint ); REQUIRE( std::abs( denominator ) > 1e-6 );
    const f64 fov = EditorSettings_Real( &session.gui.settings, "editor.camera.fov", 75 );
    const f64 focal = camera->width() * 0.5 / std::tan( fov * 3.14159265358979323846 / 360.0 );
    const QPointF parallel( camera->width() * 0.5, camera->height() * 0.5 + focal * dot( forward, constraint ) / denominator );
    const auto *document = ws.pDocument; const auto token = UndoRedo_StateToken( ws.history.pUndo ); const usize steps = EditorHistory_StepCount( &ws.history );
    DragMouse( camera.get(), QEvent::MouseButtonPress, start ); DragMouse( camera.get(), QEvent::MouseMove, end );
    REQUIRE( ws.editPreview.bActive ); CHECK( std::abs( ws.editPreview.bounds.box.maximum.z - 256 ) < 1e-6 );
    DragMouse( camera.get(), QEvent::MouseMove, parallel ); CHECK_FALSE( ws.editPreview.bActive );
    DragMouse( camera.get(), QEvent::MouseButtonRelease, end ); CHECK_FALSE( ws.editPreview.bActive );
    CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) ); CheckObjectVertices( ws.wire, id, original );
    CHECK( ws.selectedBrushFaceObject == id ); CHECK( ws.selectedBrushFaceSide == side );
}

TEST_CASE( "Compact world gizmos and grid 64 drag measurements capture", "[.gizmo-grid64-screenshot]" )
{
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -192, -128, 0 } ); MapBounds_AddPoint( box, { 192, 128, 192 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
    view_settings_t settings( &session.gui.settings );
    settings.Integer( "editor.grid.size", 64 ); settings.Set( "editor.grid.snap", true );
    settings.Set( "editor.viewport.perspective.show_selection_bounds", true ); settings.Set( "editor.viewport.perspective.show_selection_dimensions", true );
    settings.Set( "editor.viewport.show_selection_bounds", true ); settings.Set( "editor.viewport.show_selection_dimensions", true );
    settings.Set( "editor.grid.show_surface_3d", true );
    std::unique_ptr<QWidget> views( MapViews_Create( nullptr, &ws ) ); ShowAt( views.get(), 1500, 950 );
    MapViews_SetArrangement( views.get(), map_view_arrangement_t::HAMMER );
    MapViews_SetPaneView( views.get(), 0, map_view_type_t::CAMERA, map_render_mode_t::FULLBRIGHT );
    MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
    QWidget *camera = MapViews_PaneView( views.get(), 0 ); REQUIRE( camera != nullptr );
    // Leave room for the two-cell face extension and its following arrow.
    const QPointF center = camera->rect().center();
    QWheelEvent zoomOut( center, camera->mapToGlobal( center ), QPoint(), QPoint( 0, -480 ), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false );
    QCoreApplication::sendEvent( camera, &zoomOut ); QCoreApplication::processEvents();
    MapWorkspace_SetTool( &ws, map_tool_t::SELECT );
    std::error_code error; std::filesystem::create_directories( "artifacts", error ); REQUIRE_FALSE( error );
    REQUIRE( views->grab().save( QStringLiteral( "artifacts/mason_grid64_gizmos.png" ) ) );
    QPointF start, end;
    REQUIRE( MapCameraView_WorldToView( camera, { 192, 0, 96 }, &start ) ); REQUIRE( MapCameraView_WorldToView( camera, { 320, 0, 96 }, &end ) );
    DragMouse( camera, QEvent::MouseButtonPress, start ); DragMouse( camera, QEvent::MouseMove, end );
    REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.transform.bResize );
    CHECK( std::abs( ws.editPreview.bounds.box.maximum.x - 320 ) < 1e-6 );
    REQUIRE( views->grab().save( QStringLiteral( "artifacts/mason_grid64_resize.png" ) ) );
    QKeyEvent cancelResize( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( camera, &cancelResize );
    DragMouse( camera, QEvent::MouseButtonRelease, end ); CHECK_FALSE( ws.editPreview.bActive );
    MapWorkspace_SelectBrushFace( &ws, id, BrushSideInDirection( ws, id, 2u, 1 ) ); MapWorkspace_SetTool( &ws, map_tool_t::EXTRUDE );
    REQUIRE( MapCameraView_WorldToView( camera, { 0, 0, 192 }, &start ) ); REQUIRE( MapCameraView_WorldToView( camera, { 0, 0, 320 }, &end ) );
    DragMouse( camera, QEvent::MouseButtonPress, start ); DragMouse( camera, QEvent::MouseMove, end );
    REQUIRE( ws.editPreview.bActive ); CHECK( std::abs( ws.editPreview.bounds.box.maximum.z - 320 ) < 1e-6 );
    REQUIRE( views->grab().save( QStringLiteral( "artifacts/mason_grid64_push_pull.png" ) ) );
    QKeyEvent cancelFace( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( camera, &cancelFace );
    DragMouse( camera, QEvent::MouseButtonRelease, end ); CHECK_FALSE( ws.editPreview.bActive );
    CHECK( EditorHistory_StepCount( &ws.history ) == 1u );
    CheckPointClose( ObjectWireBounds( ws.wire, id ).box.minimum, box.box.minimum );
    CheckPointClose( ObjectWireBounds( ws.wire, id ).box.maximum, box.box.maximum );
}

TEST_CASE( "Perspective block creation previews ground-plane dimensions and publishes once", "[map][gui][views][geometry-edit][block]" )
{
    session_t session;
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    MapWorkspace_SetGridSize( &session.workspace, 16 );
    MapWorkspace_SetTool( &session.workspace, map_tool_t::BLOCK );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &session.workspace ) );
    ShowAt( camera.get(), 800, 600 );
    MapWorkspace_Frame( &session.workspace, CY_FALSE );
    QCoreApplication::processEvents();
    QPointF start, end;
    REQUIRE( MapCameraView_WorldToView( camera.get(), { 128, 96, 0 }, &start ) );
    REQUIRE( MapCameraView_WorldToView( camera.get(), { -64, -32, 0 }, &end ) );
    REQUIRE( camera->rect().contains( start.toPoint() ) );
    REQUIRE( camera->rect().contains( end.toPoint() ) );
    DragMouse( camera.get(), QEvent::MouseButtonPress, start );
    DragMouse( camera.get(), QEvent::MouseMove, end );
    REQUIRE( session.workspace.editPreview.bActive );
    const auto preview = session.workspace.editPreview.bounds;
    CHECK( preview.box.minimum.x == -64 ); CHECK( preview.box.maximum.x == 128 );
    CHECK( preview.box.minimum.y == -32 ); CHECK( preview.box.maximum.y == 96 );
    CHECK( preview.box.minimum.z == 0 ); CHECK( preview.box.maximum.z == 64 );
    CHECK( session.workspace.pDocument->geometry.brushes.nCount == 0 );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == 0 );

    SECTION( "Release stages, duplicate release and confirmation cannot commit twice" ) {
        DragMouse( camera.get(), QEvent::MouseButtonRelease, end );
        DragMouse( camera.get(), QEvent::MouseButtonRelease, end );
        REQUIRE( MapWorkspace_HasBlockPreview( &session.workspace ) );
        CHECK( session.workspace.pDocument->geometry.brushes.nCount == 0 ); CHECK( EditorHistory_StepCount( &session.workspace.history ) == 0 );
        QKeyEvent confirm( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &confirm );
        QCoreApplication::sendEvent( camera.get(), &confirm );
        CHECK_FALSE( session.workspace.editPreview.bActive );
        REQUIRE( session.workspace.pDocument->geometry.brushes.nCount == 1 );
        CHECK( EditorHistory_StepCount( &session.workspace.history ) == 1 );
        const auto bounds = MapViews_SelectionGeometryBounds( &session.workspace );
        CHECK( bounds.box.minimum.x == preview.box.minimum.x );
        CHECK( bounds.box.maximum.y == preview.box.maximum.y );
        CHECK( bounds.box.maximum.z == preview.box.maximum.z );
        REQUIRE( MapWorkspace_Undo( &session.workspace ) == editor_history_status_t::OK );
        CHECK( session.workspace.pDocument->geometry.brushes.nCount == 0 );
        REQUIRE( MapWorkspace_Redo( &session.workspace ) == editor_history_status_t::OK );
        CHECK( MapViews_SelectionGeometryBounds( &session.workspace ).box.maximum.z == 64 );
    }
    SECTION( "Escape leaves an empty document and absorbs the later release" ) {
        QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
        QCoreApplication::sendEvent( camera.get(), &escape );
        DragMouse( camera.get(), QEvent::MouseButtonRelease, end );
        CHECK_FALSE( session.workspace.editPreview.bActive );
        CHECK( session.workspace.pDocument->geometry.brushes.nCount == 0 );
        CHECK( EditorHistory_StepCount( &session.workspace.history ) == 0 );
        CHECK_FALSE( MapWorkspace_IsModified( &session.workspace ) );
    }
}

TEST_CASE( "Active block depth is local to the gesture in every projection", "[map][gui][views][geometry-edit][block][depth]" )
{
    for ( int projection = 0; projection < 4; ++projection ) {
        CAPTURE( projection );
        session_t session;
        view_settings_t settings( &session.gui.settings );
        REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
        settings.Real( "editor.map.block_depth", 64 );
        MapWorkspace_SetGridSize( &session.workspace, 16 );
        MapWorkspace_SetTool( &session.workspace, map_tool_t::BLOCK );
        std::unique_ptr<QWidget> view( projection == 3 ? MapCameraView_Create( nullptr, &session.workspace ) :
            MapOrthoView_Create( nullptr, &session.workspace, static_cast<map_ortho_axes_t>( projection ) ) );
        ShowAt( view.get(), 800, 600 );
        QPointF start, end;
        if ( projection == 3 ) {
            MapWorkspace_Frame( &session.workspace, CY_FALSE );
            REQUIRE( MapCameraView_WorldToView( view.get(), { -64, -64, 0 }, &start ) );
            REQUIRE( MapCameraView_WorldToView( view.get(), { 64, 64, 0 }, &end ) );
        } else {
            start = MapOrthoView_WorldToView( view.get(), { -64, -64 } );
            end = MapOrthoView_WorldToView( view.get(), { 64, 64 } );
        }
        const auto omittedExtent = [&]( const map_bounds_t &bounds ) {
            return projection == 1 ? bounds.box.maximum.x - bounds.box.minimum.x :
                projection == 2 ? bounds.box.maximum.y - bounds.box.minimum.y : bounds.box.maximum.z - bounds.box.minimum.z;
        };
        DragMouse( view.get(), QEvent::MouseButtonPress, start );
        DragMouse( view.get(), QEvent::MouseMove, end );
        REQUIRE( session.workspace.editPreview.bActive );
        CHECK( omittedExtent( session.workspace.editPreview.bounds ) == 64 );
        // Changing a default must not resize a block the user already started.
        settings.Real( "editor.map.block_depth", 192 );
        DragMouse( view.get(), QEvent::MouseMove, end );
        CHECK( omittedExtent( session.workspace.editPreview.bounds ) == 64 );
        const auto cameraPosition = projection == 3 ? MapCameraView_Position( view.get() ) : math::vec3d_t{};
        const f64 zoom = projection == 3 ? 0 : MapOrthoView_Zoom( view.get() );
        DepthWheel( view.get(), end, 240 );
        REQUIRE( session.workspace.editPreview.bActive );
        CHECK( omittedExtent( session.workspace.editPreview.bounds ) == 96 );
        DepthWheel( view.get(), end, 120, Qt::ControlModifier | Qt::ShiftModifier );
        CHECK( omittedExtent( session.workspace.editPreview.bounds ) == 97 );
        CHECK( EditorSettings_Real( &session.gui.settings, "editor.map.block_depth", 0 ) == 192 );
        CHECK( session.workspace.pDocument->geometry.brushes.nCount == 0 );
        CHECK( EditorHistory_StepCount( &session.workspace.history ) == 0 );
        if ( projection == 3 ) {
            const auto after = MapCameraView_Position( view.get() );
            CHECK( after.x == cameraPosition.x ); CHECK( after.y == cameraPosition.y ); CHECK( after.z == cameraPosition.z );
        } else { CHECK( MapOrthoView_Zoom( view.get() ) == zoom ); }
        DragMouse( view.get(), QEvent::MouseButtonRelease, end );
        REQUIRE( MapWorkspace_HasBlockPreview( &session.workspace ) );
        CHECK( session.workspace.pDocument->geometry.brushes.nCount == 0 ); CHECK( EditorHistory_StepCount( &session.workspace.history ) == 0 );
        QKeyEvent confirm( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier ); QCoreApplication::sendEvent( view.get(), &confirm );
        CHECK_FALSE( session.workspace.editPreview.bActive );
        CHECK( session.workspace.pDocument->geometry.brushes.nCount == 1 );
        CHECK( omittedExtent( MapViews_SelectionGeometryBounds( &session.workspace ) ) == 97 );
        CHECK( EditorHistory_StepCount( &session.workspace.history ) == 1 );
        REQUIRE( MapWorkspace_Undo( &session.workspace ) == editor_history_status_t::OK );
        CHECK( session.workspace.pDocument->geometry.brushes.nCount == 0 );
        REQUIRE( MapWorkspace_Redo( &session.workspace ) == editor_history_status_t::OK );
        CHECK( omittedExtent( MapViews_SelectionGeometryBounds( &session.workspace ) ) == 97 );
    }
}

TEST_CASE( "Block footprint follows current grid and snap policy without publishing an intermediate brush", "[map][gui][views][geometry-edit][block][snap]" )
{
    session_t session;
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    MapWorkspace_SetGridSize( &session.workspace, 16 );
    MapWorkspace_SetTool( &session.workspace, map_tool_t::BLOCK );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &session.workspace, map_ortho_axes_t::TOP ) );
    ShowAt( top.get(), 800, 600 );
    const QPointF start = MapOrthoView_WorldToView( top.get(), { -57, -25 } );
    const QPointF end = MapOrthoView_WorldToView( top.get(), { 73, 89 } );
    DragMouse( top.get(), QEvent::MouseButtonPress, start );
    DragMouse( top.get(), QEvent::MouseMove, end );
    REQUIRE( session.workspace.editPreview.bActive );
    CHECK( session.workspace.editPreview.bounds.box.minimum.x == -64 );
    CHECK( session.workspace.editPreview.bounds.box.minimum.y == -32 );
    CHECK( session.workspace.editPreview.bounds.box.maximum.x == 80 );
    CHECK( session.workspace.editPreview.bounds.box.maximum.y == 96 );
    MapWorkspace_SetGridSize( &session.workspace, 64 );
    DragMouse( top.get(), QEvent::MouseMove, end );
    REQUIRE( session.workspace.editPreview.bActive );
    CHECK( session.workspace.editPreview.bounds.box.minimum.x == -64 );
    CHECK( session.workspace.editPreview.bounds.box.minimum.y == 0 );
    CHECK( session.workspace.editPreview.bounds.box.maximum.x == 64 );
    CHECK( session.workspace.editPreview.bounds.box.maximum.y == 64 );
    MapWorkspace_SetSnapToGrid( &session.workspace, CY_FALSE );
    DragMouse( top.get(), QEvent::MouseMove, end );
    CHECK( std::abs( session.workspace.editPreview.bounds.box.minimum.x + 57 ) < 1e-6 );
    CHECK( std::abs( session.workspace.editPreview.bounds.box.maximum.y - 89 ) < 1e-6 );
    MapWorkspace_SetSnapToGrid( &session.workspace, CY_TRUE );
    DragMouse( top.get(), QEvent::MouseMove, end, Qt::ControlModifier );
    CHECK( std::abs( session.workspace.editPreview.bounds.box.minimum.x + 57 ) < 1e-6 );
    CHECK( std::abs( session.workspace.editPreview.bounds.box.maximum.y - 89 ) < 1e-6 );
    CHECK( session.workspace.pDocument->geometry.brushes.nCount == 0 );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == 0 );
    DragMouse( top.get(), QEvent::MouseButtonRelease, end, Qt::ControlModifier );
    REQUIRE( MapWorkspace_HasBlockPreview( &session.workspace ) );
    CHECK( session.workspace.pDocument->geometry.brushes.nCount == 0 );
    QKeyEvent confirm( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier ); QCoreApplication::sendEvent( top.get(), &confirm );
    REQUIRE( session.workspace.pDocument->geometry.brushes.nCount == 1 );
    const auto bounds = MapViews_SelectionGeometryBounds( &session.workspace );
    CHECK( std::abs( bounds.box.maximum.x - bounds.box.minimum.x - 130 ) < 1e-6 );
    CHECK( std::abs( bounds.box.maximum.y - bounds.box.minimum.y - 114 ) < 1e-6 );
    CHECK( session.workspace.bSnapToGrid ); CHECK( session.workspace.gridSize == 64 );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == 1 );
}

TEST_CASE( "Depth scrolling preserves a legal block while ordinary scrolling cancels it", "[map][gui][views][geometry-edit][block][depth]" )
{
    for ( bool perspective : { false, true } ) {
        CAPTURE( perspective );
        session_t session;
        view_settings_t settings( &session.gui.settings );
        REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
        settings.Real( "editor.map.block_depth", 64 );
        MapWorkspace_SetGridSize( &session.workspace, 16 );
        MapWorkspace_SetTool( &session.workspace, map_tool_t::BLOCK );
        std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &session.workspace ) :
            MapOrthoView_Create( nullptr, &session.workspace, map_ortho_axes_t::TOP ) );
        ShowAt( view.get(), 800, 600 );
        QPointF start, end;
        if ( perspective ) {
            MapWorkspace_Frame( &session.workspace, CY_FALSE );
            REQUIRE( MapCameraView_WorldToView( view.get(), { -64, -64, 0 }, &start ) );
            REQUIRE( MapCameraView_WorldToView( view.get(), { 64, 64, 0 }, &end ) );
        } else {
            start = MapOrthoView_WorldToView( view.get(), { -64, -64 } );
            end = MapOrthoView_WorldToView( view.get(), { 64, 64 } );
        }
        DragMouse( view.get(), QEvent::MouseButtonPress, start );
        DragMouse( view.get(), QEvent::MouseMove, end );
        REQUIRE( session.workspace.editPreview.bActive );
        DepthWheel( view.get(), end, -12000 );
        REQUIRE( session.workspace.editPreview.bActive );
        CHECK( session.workspace.editPreview.bounds.box.maximum.z == 1 );
        DepthWheel( view.get(), end, 120 );
        CHECK( session.workspace.editPreview.bounds.box.maximum.z == 17 );
        DepthWheel( view.get(), end, 1200000 );
        CHECK( session.workspace.editPreview.bounds.box.maximum.z == 65536 );
        CHECK( EditorSettings_Real( &session.gui.settings, "editor.map.block_depth", 0 ) == 64 );
        DepthWheel( view.get(), end, 120, Qt::NoModifier );
        CHECK_FALSE( session.workspace.editPreview.bActive );
        // The release after a dolly/zoom must never publish stale coordinates.
        DragMouse( view.get(), QEvent::MouseButtonRelease, end );
        CHECK( session.workspace.pDocument->geometry.brushes.nCount == 0 );
        CHECK( EditorHistory_StepCount( &session.workspace.history ) == 0 );
    }
}

TEST_CASE( "Live block measurements can be hidden without cancelling geometry authoring", "[map][gui][views][geometry-edit][block][visualization]" )
{
    for ( int projection = 0; projection < 4; ++projection ) {
        CAPTURE( projection );
        session_t session;
        view_settings_t settings( &session.gui.settings );
        REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
        MapWorkspace_SetTool( &session.workspace, map_tool_t::BLOCK );
        std::unique_ptr<QWidget> view( projection == 3 ? MapCameraView_Create( nullptr, &session.workspace ) :
            MapOrthoView_Create( nullptr, &session.workspace, static_cast<map_ortho_axes_t>( projection ) ) );
        ShowAt( view.get(), 800, 600 );
        QPointF start, end;
        if ( projection == 3 ) {
            MapWorkspace_Frame( &session.workspace, CY_FALSE );
            REQUIRE( MapCameraView_WorldToView( view.get(), { -128, -128, 0 }, &start ) );
            REQUIRE( MapCameraView_WorldToView( view.get(), { 128, 128, 0 }, &end ) );
        } else {
            start = MapOrthoView_WorldToView( view.get(), { -128, -128 } );
            end = MapOrthoView_WorldToView( view.get(), { 128, 128 } );
        }
        DragMouse( view.get(), QEvent::MouseButtonPress, start );
        DragMouse( view.get(), QEvent::MouseMove, end );
        REQUIRE( session.workspace.editPreview.bActive );
        const char *path = projection == 3 ? "editor.viewport.perspective.show_selection_dimensions" : "editor.viewport.show_selection_dimensions";
        settings.Set( path, false );
        const QImage plain = view->grab().toImage();
        settings.Set( path, true );
        CHECK( view->grab().toImage() != plain );
        settings.Set( path, false );
        CHECK( view->grab().toImage() == plain );
        CHECK( session.workspace.editPreview.bActive );
        CHECK( session.workspace.pDocument->geometry.brushes.nCount == 0 );
        DragMouse( view.get(), QEvent::MouseButtonRelease, end );
        REQUIRE( MapWorkspace_HasBlockPreview( &session.workspace ) ); CHECK( session.workspace.pDocument->geometry.brushes.nCount == 0 );
        QKeyEvent confirm( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier ); QCoreApplication::sendEvent( view.get(), &confirm );
        CHECK( session.workspace.pDocument->geometry.brushes.nCount == 1 );
        CHECK( EditorHistory_StepCount( &session.workspace.history ) == 1 );
    }
}

TEST_CASE( "Constrained scale handles preview the future bounds in 2D and 3D", "[map][gui][views][geometry-edit][gizmo][scale]" )
{
    for ( bool perspective : { false, true } ) {
        CAPTURE( perspective );
        session_t session;
        REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -32, 0 } ); MapBounds_AddPoint( box, { 64, 32, 96 } );
        REQUIRE( MapWorkspace_CreateBox( &session.workspace, box ) );
        const u64 id = EditorSelection_At( &session.workspace.selection, 0 );
        MapWorkspace_SetGridSize( &session.workspace, 16 );
        MapWorkspace_SetScaleSnap( &session.workspace, 0.25 );
        MapWorkspace_SetTool( &session.workspace, map_tool_t::SCALE );
        std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &session.workspace ) :
            MapOrthoView_Create( nullptr, &session.workspace, map_ortho_axes_t::TOP ) );
        ShowAt( view.get(), 800, 600 );
        QPointF start, end;
        if ( perspective ) {
            MapWorkspace_Frame( &session.workspace, CY_TRUE );
            const math::vec3d_t pivot{ 0, 0, 48 };
            const f64 length = CameraGizmoLength( view.get(), pivot, session.gui.settings );
            QPointF center, tip;
            REQUIRE( MapCameraView_WorldToView( view.get(), pivot, &center ) );
            REQUIRE( MapCameraView_WorldToView( view.get(), { length, 0, 48 }, &tip ) );
            start = center + ( tip - center ) * 0.7;
            end = start + ( tip - center ) * ( 32.0 / length );
        } else {
            const f64 length = 64.0 / MapOrthoView_Zoom( view.get() );
            start = MapOrthoView_WorldToView( view.get(), { length * 0.7, 0 } );
            end = MapOrthoView_WorldToView( view.get(), { length * 0.7 + 32, 0 } );
        }
        DragMouse( view.get(), QEvent::MouseButtonPress, start );
        DragMouse( view.get(), QEvent::MouseMove, end );
        REQUIRE( session.workspace.editPreview.bActive );
        const auto preview = session.workspace.editPreview.bounds;
        CHECK( std::abs( preview.box.minimum.x + 96 ) < 1e-6 );
        CHECK( std::abs( preview.box.maximum.x - 96 ) < 1e-6 );
        CHECK( preview.box.minimum.y == -32 ); CHECK( preview.box.maximum.y == 32 );
        CHECK( preview.box.minimum.z == 0 ); CHECK( preview.box.maximum.z == 96 );
        CHECK( MapViews_SelectionGeometryBounds( &session.workspace ).box.minimum.x == -64 );
        CHECK( EditorHistory_StepCount( &session.workspace.history ) == 1 );
        DragMouse( view.get(), QEvent::MouseButtonRelease, end );
        CHECK_FALSE( session.workspace.editPreview.bActive );
        CHECK( EditorSelection_At( &session.workspace.selection, 0 ) == id );
        CHECK( MapViews_SelectionGeometryBounds( &session.workspace ).box.minimum.x == -96 );
        CHECK( EditorHistory_StepCount( &session.workspace.history ) == 2 );
        REQUIRE( MapWorkspace_Undo( &session.workspace ) == editor_history_status_t::OK );
        CHECK( MapViews_SelectionGeometryBounds( &session.workspace ).box.minimum.x == -64 );
        REQUIRE( MapWorkspace_Redo( &session.workspace ) == editor_history_status_t::OK );
        CHECK( MapViews_SelectionGeometryBounds( &session.workspace ).box.minimum.x == -96 );
    }
}

TEST_CASE( "Scale planar pads affect exactly two axes and the center scales all axes uniformly", "[map][gui][views][geometry-edit][gizmo][scale][planar-scale]" )
{
    for ( int projection : { -1, 0, 1, 2 } ) {
        CAPTURE( projection );
        session_t session; auto &ws = session.workspace;
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -32, 0 } ); MapBounds_AddPoint( box, { 64, 32, 96 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
        MapWorkspace_SetGridSize( &ws, 8 ); MapWorkspace_SetScaleSnap( &ws, 0.25 ); MapWorkspace_SetTool( &ws, map_tool_t::SCALE );
        const bool perspective = projection < 0;
        const auto axes = static_cast<map_ortho_axes_t>( std::max( 0, projection ) );
        const u32 axisU = axes == map_ortho_axes_t::FRONT ? 1u : 0u, axisV = axes == map_ortho_axes_t::TOP ? 1u : 2u;
        std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &ws ) : MapOrthoView_Create( nullptr, &ws, axes ) );
        ShowAt( view.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        const math::vec3d_t pivot{ 0, 0, 48 }, half{ 64, 32, 48 };
        const auto project = [&]( math::vec3d_t p ) {
            QPointF point;
            if ( perspective ) { REQUIRE( MapCameraView_WorldToView( view.get(), p, &point ) ); }
            else { point = MapOrthoView_WorldToView( view.get(), { TestCoordinate( p, axisU ), TestCoordinate( p, axisV ) } ); }
            return point;
        };
        const std::vector<int> pads = perspective ? std::vector<int>{ 0, 1, 2, -1 } : std::vector<int>{ static_cast<int>( 3u - axisU - axisV ), -1 };
        const usize steps = EditorHistory_StepCount( &ws.history );
        for ( const int omitted : pads ) {
            CAPTURE( omitted );
            const f64 length = perspective ? CameraGizmoLength( view.get(), pivot, session.gui.settings ) : 64.0 / MapOrthoView_Zoom( view.get() );
            auto anchor = pivot; QPointF start, end;
            if ( omitted < 0 ) { start = project( pivot ); end = start + QPointF( 35, -15 ); }
            else {
                for ( u32 axis = 0; axis < 3; ++axis ) { if ( axis != static_cast<u32>( omitted ) ) { SetTestCoordinate( anchor, axis, TestCoordinate( anchor, axis ) + length * 0.31 ); } }
                start = project( anchor );
                for ( u32 axis = 0; axis < 3; ++axis ) { if ( axis != static_cast<u32>( omitted ) ) { SetTestCoordinate( anchor, axis, TestCoordinate( anchor, axis ) + TestCoordinate( half, axis ) * 0.5 ); } }
                end = project( anchor );
            }
            auto expected = box;
            for ( u32 axis = 0; axis < 3; ++axis ) {
                const f64 factor = static_cast<int>( axis ) == omitted ? 1.0 : 1.5;
                SetTestCoordinate( expected.box.minimum, axis, TestCoordinate( pivot, axis ) - TestCoordinate( half, axis ) * factor );
                SetTestCoordinate( expected.box.maximum, axis, TestCoordinate( pivot, axis ) + TestCoordinate( half, axis ) * factor );
            }
            const auto *document = ws.pDocument;
            DragMouse( view.get(), QEvent::MouseButtonPress, start ); DragMouse( view.get(), QEvent::MouseMove, end );
            REQUIRE( ws.editPreview.bActive ); CHECK( ws.pDocument == document );
            CheckPointClose( ws.editPreview.bounds.box.minimum, expected.box.minimum ); CheckPointClose( ws.editPreview.bounds.box.maximum, expected.box.maximum );
            DragMouse( view.get(), QEvent::MouseButtonRelease, end );
            CHECK_FALSE( ws.editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
            CHECK( ws.tool == map_tool_t::SCALE ); CHECK( EditorSelection_At( &ws.selection, 0 ) == id );
            const auto result = MapViews_SelectionGeometryBounds( &ws ); CheckPointClose( result.box.minimum, expected.box.minimum ); CheckPointClose( result.box.maximum, expected.box.maximum );
            REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
            CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, box.box.minimum ); CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, box.box.maximum );
        }
    }
}

TEST_CASE( "Orthographic rotation rings rotate in the visible plane and preview its dimensions", "[map][gui][views][geometry-edit][gizmo][rotate]" )
{
    session_t session;
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -32, 0 } ); MapBounds_AddPoint( box, { 64, 32, 96 } );
    REQUIRE( MapWorkspace_CreateBox( &session.workspace, box ) );
    MapWorkspace_SetAngleSnap( &session.workspace, 15 );
    MapWorkspace_SetTool( &session.workspace, map_tool_t::ROTATE );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &session.workspace, map_ortho_axes_t::TOP ) );
    ShowAt( top.get(), 800, 600 );
    const f64 length = 64.0 / MapOrthoView_Zoom( top.get() );
    const QPointF start = MapOrthoView_WorldToView( top.get(), { length, 0 } );
    const QPointF end = MapOrthoView_WorldToView( top.get(), { 0, length } );
    DragMouse( top.get(), QEvent::MouseButtonPress, start );
    DragMouse( top.get(), QEvent::MouseMove, end );
    REQUIRE( session.workspace.editPreview.bActive );
    const auto preview = session.workspace.editPreview.bounds;
    CHECK( std::abs( preview.box.minimum.x + 32 ) < 1e-6 ); CHECK( std::abs( preview.box.maximum.x - 32 ) < 1e-6 );
    CHECK( std::abs( preview.box.minimum.y + 64 ) < 1e-6 ); CHECK( std::abs( preview.box.maximum.y - 64 ) < 1e-6 );
    CHECK( preview.box.minimum.z == 0 ); CHECK( preview.box.maximum.z == 96 );
    CHECK( MapViews_SelectionGeometryBounds( &session.workspace ).box.minimum.x == -64 );
    DragMouse( top.get(), QEvent::MouseButtonRelease, end );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == 2 );
    CHECK( std::abs( MapViews_SelectionGeometryBounds( &session.workspace ).box.minimum.x + 32 ) < 1e-6 );
    REQUIRE( MapWorkspace_Undo( &session.workspace ) == editor_history_status_t::OK );
    CHECK( MapViews_SelectionGeometryBounds( &session.workspace ).box.minimum.x == -64 );
}

TEST_CASE( "Transform preview points apply world translation and pivot scale and rotation", "[map][gui][views][geometry-edit][shared-transform]" )
{
    map_transform_preview_t transform{};
    const math::vec3d_t point{ 7, 8, 9 }; transform.pivot = { 2, 3, 4 };
    CheckPointClose( MapWorkspace_TransformPreviewPoint( transform, point ), point );
    transform.kind = map_transform_preview_kind_t::TRANSLATE; transform.delta = { 10, -4, 3 };
    CheckPointClose( MapWorkspace_TransformPreviewPoint( transform, point ), { 17, 4, 12 } );
    transform.kind = map_transform_preview_kind_t::SCALE; transform.factors = { 2, 0.5, 3 };
    CheckPointClose( MapWorkspace_TransformPreviewPoint( transform, point ), { 12, 5.5, 19 } );
    transform.kind = map_transform_preview_kind_t::ROTATE;
    transform.degrees = { 90, 0, 0 }; CheckPointClose( MapWorkspace_TransformPreviewPoint( transform, point ), { 7, -2, 9 } );
    transform.degrees = { 0, 90, 0 }; CheckPointClose( MapWorkspace_TransformPreviewPoint( transform, point ), { 7, 8, -1 } );
    transform.degrees = { 0, 0, 450 }; CheckPointClose( MapWorkspace_TransformPreviewPoint( transform, point ), { -3, 8, 9 } );
    // X precedes Y: reversing these two turns would return this point to
    // itself, rather than take its offset (5,5,5) to (5,-5,-5).
    transform.degrees = { 90, 90, 0 }; CheckPointClose( MapWorkspace_TransformPreviewPoint( transform, point ), { 7, -2, -1 } );
    CheckPointClose( MapWorkspace_TransformPreviewPoint( transform, transform.pivot ), transform.pivot );
}

TEST_CASE( "Programmatic shared transforms clear when their captured editing context becomes stale", "[map][gui][views][geometry-edit][shared-transform][gesture-context]" )
{
    for ( int change = 0; change < 9; ++change ) {
        CAPTURE( change );
        session_t session; auto &ws = session.workspace;
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -32, 0 } ); MapBounds_AddPoint( box, { 64, 32, 96 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
        MapWorkspace_SetTool( &ws, map_tool_t::ROTATE );
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
        const usize steps = EditorHistory_StepCount( &ws.history );
        map_transform_preview_t transform{}; transform.kind = map_transform_preview_kind_t::SCALE;
        transform.pivot = { 0, 0, 48 }; transform.factors = { 2, 0.5, 1.5 };
        MapWorkspace_SetTransformPreview( &ws, transform );
        REQUIRE( ws.editPreview.bActive ); CHECK( ws.editPreview.transform.kind == map_transform_preview_kind_t::SCALE );
        CheckPointClose( ws.editPreview.bounds.box.minimum, { -128, -16, -24 } ); CheckPointClose( ws.editPreview.bounds.box.maximum, { 128, 16, 120 } );
        CHECK( ws.editPreview.documentRevision == revision ); CHECK( ws.editPreview.selectionRevision == ws.selection.revision );
        CHECK( ws.editPreview.tool == map_tool_t::ROTATE ); CHECK( ws.editPreview.mode == map_element_mode_t::OBJECTS );
        CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
        if ( change == 0 ) { MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE ); }
        else if ( change == 1 ) { MapWorkspace_SetTool( &ws, map_tool_t::SELECT ); }
        else if ( change == 2 ) { MapWorkspace_SetElementMode( &ws, map_element_mode_t::GROUPS ); }
        else if ( change == 3 ) { REQUIRE( MapWorkspace_TranslateSelection( &ws, { 16, 0, 0 } ) ); }
        else if ( change == 4 ) { MapWorkspace_ClearEditPreview( &ws ); }
        else if ( change == 5 ) { MapWorkspace_SetTransformPreview( &ws, {} ); }
        else if ( change == 6 ) { transform.pivot.x = std::numeric_limits<f64>::infinity(); MapWorkspace_SetTransformPreview( &ws, transform ); }
        // Neither change hides the selected brush: these exercise captured
        // visibility invalidation rather than edit eligibility alone.
        else if ( change == 7 ) { MapWorkspace_SetVisgroupHidden( &ws, map_visgroup_t::LIGHTS, CY_TRUE ); }
        else {
            map_bounds_t cordon{}; MapBounds_AddPoint( cordon, { -128, -128, -128 } ); MapBounds_AddPoint( cordon, { 128, 128, 256 } );
            MapWorkspace_SetCordon( &ws, cordon );
        }
        CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.editPreview.transform.kind == map_transform_preview_kind_t::NONE );
        CHECK( ws.editPreviewWire.points.nCount == 0u );
        if ( change != 3 ) { CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( EditorHistory_StepCount( &ws.history ) == steps ); }
        else { CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u ); }
    }
}

TEST_CASE( "Visibility changes cancel a shared move before a stale release can publish it", "[map][gui][views][geometry-edit][shared-transform][gesture-context][visibility]" )
{
    for ( int change = 0; change < 3; ++change ) {
        CAPTURE( change );
        session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t a{}; MapBounds_AddPoint( a, { -64, -32, 0 } ); MapBounds_AddPoint( a, { 64, 32, 96 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, a ) ); const u64 first = EditorSelection_At( &ws.selection, 0 );
        map_bounds_t b{}; MapBounds_AddPoint( b, { 256, -32, 0 } ); MapBounds_AddPoint( b, { 384, 32, 96 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, b ) ); const u64 second = EditorSelection_At( &ws.selection, 0 );
        MapWorkspace_Select( &ws, first, MAP_SELECT_REPLACE ); MapWorkspace_SetGridSize( &ws, 16 );
        std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) ); ShowAt( top.get(), 800, 600 );
        MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision, selectionRevision = ws.selection.revision;
        const usize steps = EditorHistory_StepCount( &ws.history );
        const f64 length = 64.0 / MapOrthoView_Zoom( top.get() );
        const QPointF start = MapOrthoView_WorldToView( top.get(), { length * 0.7, 0 } );
        const QPointF end = MapOrthoView_WorldToView( top.get(), { length * 0.7 + 21, 0 } );
        DragMouse( top.get(), QEvent::MouseButtonPress, start ); DragMouse( top.get(), QEvent::MouseMove, end );
        REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.transform.kind == map_transform_preview_kind_t::TRANSLATE );
        CheckPointClose( ws.editPreview.transform.delta, { 16, 0, 0 } );
        if ( change == 0 ) { REQUIRE( EditorSelection_Apply( &ws.hidden, second, EDITOR_SELECT_ADD ) ); MapWorkspace_Notify( &ws, MAP_CHANGE_VIEW ); }
        else if ( change == 1 ) { MapWorkspace_SetVisgroupHidden( &ws, map_visgroup_t::LIGHTS, CY_TRUE ); }
        else {
            map_bounds_t cordon{}; MapBounds_AddPoint( cordon, { -128, -128, -128 } ); MapBounds_AddPoint( cordon, { 512, 128, 256 } );
            MapWorkspace_SetCordon( &ws, cordon );
        }
        REQUIRE( MapWorkspace_CanMoveSelection( &ws ) ); CHECK_FALSE( ws.editPreview.bActive );
        CHECK( ws.editPreview.transform.kind == map_transform_preview_kind_t::NONE );
        DragMouse( top.get(), QEvent::MouseMove, end + QPointF( 40, 0 ) ); DragMouse( top.get(), QEvent::MouseButtonRelease, end + QPointF( 40, 0 ) );
        CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision );
        CHECK( ws.selection.revision == selectionRevision ); CHECK( EditorSelection_At( &ws.selection, 0 ) == first ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
        const auto *original = MapWireframe_FindObject( ws.wire, first ); REQUIRE( original != nullptr );
        CheckPointClose( original->bounds.box.minimum, a.box.minimum ); CheckPointClose( original->bounds.box.maximum, a.box.maximum );
    }
}

TEST_CASE( "A Top rotation shares exact cylinder edges and bounds with inactive Front and camera views", "[map][gui][views][geometry-edit][gizmo][rotate][shared-transform]" )
{
    session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    for ( const char *key : { "editor.viewport.active_border", "editor.viewport.show_axes", "editor.viewport.center_axes",
        "editor.viewport.show_selection_bounds", "editor.viewport.show_selection_dimensions", "editor.viewport.show_selection_vertices",
        "editor.viewport.perspective.show_axes", "editor.viewport.perspective.center_axes", "editor.viewport.perspective.show_selection_bounds",
        "editor.viewport.perspective.show_selection_dimensions", "editor.viewport.perspective.show_selection_vertices" } ) { settings.Set( key, false ); }
    MapWorkspace_SetGridVisible( &ws, CY_FALSE );
    map_primitive_desc_t cylinder{}; cylinder.kind = map_primitive_kind_t::CYLINDER; cylinder.axis = 2; cylinder.nSides = 5;
    cylinder.bounds = { { -96, -48, 0 }, { 96, 48, 256 } };
    REQUIRE( MapWorkspace_CreatePrimitive( &ws, cylinder ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
    const auto original = ObjectLineVertices( ws.wire, id ); REQUIRE( original.size() == 10u );
    const auto bounds = MapViews_SelectionGeometryBounds( &ws ); const auto pivot = MapBounds_Center( bounds );
    const auto rotated = [&]( math::vec3d_t p ) {
        constexpr f64 diagonal = 0.7071067811865475244;
        return math::vec3d_t{ pivot.x + diagonal * ( p.x - pivot.x - p.y + pivot.y ),
            pivot.y + diagonal * ( p.x - pivot.x + p.y - pivot.y ), p.z };
    };
    map_bounds_t exact{}, envelope{}; std::vector<math::vec3d_t> expected;
    for ( const auto point : original ) { expected.push_back( rotated( point ) ); MapBounds_AddPoint( exact, expected.back() ); }
    for ( u32 c = 0; c < 8; ++c ) { MapBounds_AddPoint( envelope, rotated( { c & 1 ? bounds.box.maximum.x : bounds.box.minimum.x,
        c & 2 ? bounds.box.maximum.y : bounds.box.minimum.y, c & 4 ? bounds.box.maximum.z : bounds.box.minimum.z } ) ); }
    REQUIRE( TestDistance( exact.box.minimum, envelope.box.minimum ) > 5 );
    MapWorkspace_SetAngleSnap( &ws, 15 ); MapWorkspace_SetTool( &ws, map_tool_t::ROTATE );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
    std::unique_ptr<QWidget> front( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::FRONT ) );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) );
    ShowAt( top.get(), 800, 600 ); ShowAt( front.get(), 800, 600 ); ShowAt( camera.get(), 800, 600 );
    MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
    const auto project = [&]( bool perspective, math::vec3d_t point ) {
        QPointF result;
        if ( perspective ) { REQUIRE( MapCameraView_WorldToView( camera.get(), point, &result ) ); }
        else { result = MapOrthoView_WorldToView( front.get(), { point.y, point.z } ); }
        return result;
    };
    const auto distance = []( QPointF p, QLineF edge ) {
        const QPointF d = edge.p2() - edge.p1(); const f64 squared = QPointF::dotProduct( d, d );
        const f64 t = squared > 0 ? std::clamp( QPointF::dotProduct( p - edge.p1(), d ) / squared, 0.0, 1.0 ) : 0.0;
        return QLineF( p, edge.p1() + d * t ).length();
    };
    QPointF probes[2];
    for ( const bool perspective : { false, true } ) {
        CAPTURE( perspective ); bool found = false;
        std::vector<QLineF> guides;
        for ( u32 c = 0; c < 8; ++c ) { for ( u32 axis = 0; axis < 3; ++axis ) {
            if ( c & ( 1u << axis ) ) { continue; }
            const auto corner = [&]( u32 n ) { return math::vec3d_t{ n & 1 ? exact.box.maximum.x : exact.box.minimum.x,
                n & 2 ? exact.box.maximum.y : exact.box.minimum.y, n & 4 ? exact.box.maximum.z : exact.box.minimum.z }; };
            guides.emplace_back( project( perspective, corner( c ) ), project( perspective, corner( c | ( 1u << axis ) ) ) );
        } }
        for ( usize i = 0; i < ws.wire.lines.nCount && !found; ++i ) {
            const auto &edge = ws.wire.lines.pData[i]; const auto a = ws.wire.points.pData[edge.iA], b = ws.wire.points.pData[edge.iB];
            if ( std::abs( a.z - b.z ) < 250 ) { continue; }
            for ( const f64 t : { 0.16, 0.24, 0.76, 0.84 } ) {
                const auto point = rotated( { a.x + ( b.x - a.x ) * t, a.y + ( b.y - a.y ) * t, a.z + ( b.z - a.z ) * t } );
                const QPointF probe = project( perspective, point );
                if ( !( perspective ? camera : front )->rect().adjusted( 12, 12, -12, -12 ).contains( probe.toPoint() ) ||
                     QLineF( probe, project( perspective, pivot ) ).length() < 88 ) { continue; }
                bool clear = std::all_of( guides.begin(), guides.end(), [&]( QLineF guide ) { return distance( probe, guide ) > 9; } );
                for ( usize line = 0; line < ws.wire.lines.nCount && clear; ++line ) {
                    const auto &old = ws.wire.lines.pData[line];
                    clear = distance( probe, QLineF( project( perspective, ws.wire.points.pData[old.iA] ), project( perspective, ws.wire.points.pData[old.iB] ) ) ) > 9;
                }
                if ( clear ) { probes[perspective ? 1 : 0] = probe; found = true; break; }
            }
        }
        REQUIRE( found ); // These probes cannot be satisfied by an AABB or a gizmo.
    }
    const QImage frontBefore = front->grab().toImage(), cameraBefore = camera->grab().toImage();
    const QColor selectedColor = gui::EditorStyle_TokenColor( session.gui.style, "viewport.selection" );
    const auto selectedPixelsNear = [&]( const QImage &image, QPointF point ) {
        const qreal ratio = image.devicePixelRatio(); const QPoint center( qRound( point.x() * ratio ), qRound( point.y() * ratio ) );
        const int radius = static_cast<int>( std::ceil( 3 * ratio ) );
        const QRect area = QRect( center - QPoint( radius, radius ), QSize( radius * 2 + 1, radius * 2 + 1 ) ).intersected( image.rect() );
        int count = 0;
        for ( int y = area.top(); y <= area.bottom(); ++y ) { for ( int x = area.left(); x <= area.right(); ++x ) {
            const QColor pixel = image.pixelColor( x, y );
            count += std::abs( pixel.red() - selectedColor.red() ) < 24 && std::abs( pixel.green() - selectedColor.green() ) < 24 &&
                std::abs( pixel.blue() - selectedColor.blue() ) < 24 ? 1 : 0;
        } }
        return count;
    };
    REQUIRE( selectedPixelsNear( frontBefore, probes[0] ) == 0 ); REQUIRE( selectedPixelsNear( cameraBefore, probes[1] ) == 0 );
    const auto cameraPosition = MapCameraView_Position( camera.get() ); const f64 frontZoom = MapOrthoView_Zoom( front.get() );
    const auto *document = ws.pDocument; const auto revision = document->geometry.revision, selectionRevision = ws.selection.revision;
    const usize steps = EditorHistory_StepCount( &ws.history );
    const f64 length = 64.0 / MapOrthoView_Zoom( top.get() );
    const QPointF start = MapOrthoView_WorldToView( top.get(), { pivot.x + length, pivot.y } );
    const QPointF end = MapOrthoView_WorldToView( top.get(), { pivot.x + length / std::sqrt( 2.0 ), pivot.y + length / std::sqrt( 2.0 ) } );
    for ( const bool commit : { false, true } ) {
        DragMouse( top.get(), QEvent::MouseButtonPress, start ); DragMouse( top.get(), QEvent::MouseMove, end );
        REQUIRE( ws.editPreview.bActive ); CHECK( ws.editPreview.transform.kind == map_transform_preview_kind_t::ROTATE );
        CheckPointClose( ws.editPreview.transform.pivot, pivot ); CheckPointClose( ws.editPreview.transform.degrees, { 0, 0, 45 } );
        CHECK_FALSE( ws.editPreview.transform.bClone ); CheckPointClose( ws.editPreview.bounds.box.minimum, exact.box.minimum ); CheckPointClose( ws.editPreview.bounds.box.maximum, exact.box.maximum );
        CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.selection.revision == selectionRevision );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( ws.editPreviewWire.points.nCount == 0u ); CheckObjectVertices( ws.wire, id, original );
        const QImage frontPreview = front->grab().toImage(), cameraPreview = camera->grab().toImage();
        CHECK( ChangedPixelsNear( frontBefore, frontPreview, probes[0] ) > 3 );
        CHECK( ChangedPixelsNear( cameraBefore, cameraPreview, probes[1] ) > 3 );
        // The future authored edge carries selection yellow; hover green
        // belongs to picking and controls, rather than the moving geometry.
        CHECK( selectedPixelsNear( frontPreview, probes[0] ) > 2 ); CHECK( selectedPixelsNear( cameraPreview, probes[1] ) > 2 );
        CheckPointClose( MapCameraView_Position( camera.get() ), cameraPosition ); CHECK( MapOrthoView_Zoom( front.get() ) == frontZoom );
        if ( !commit ) { QKeyEvent cancel( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( top.get(), &cancel ); }
        DragMouse( top.get(), QEvent::MouseButtonRelease, end );
        CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.editPreview.transform.kind == map_transform_preview_kind_t::NONE );
        if ( !commit ) {
            CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CheckObjectVertices( ws.wire, id, original );
            CHECK( ChangedPixelsNear( frontBefore, front->grab().toImage(), probes[0] ) == 0 ); CHECK( ChangedPixelsNear( cameraBefore, camera->grab().toImage(), probes[1] ) == 0 );
        }
    }
    CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u ); CHECK( EditorSelection_At( &ws.selection, 0 ) == id ); CheckObjectVertices( ws.wire, id, expected );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CheckObjectVertices( ws.wire, id, original );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CheckObjectVertices( ws.wire, id, expected );
    QTemporaryDir folder; REQUIRE( folder.isValid() ); const QString path = folder.filePath( QStringLiteral( "shared_rotation.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
    map_document_t loaded{}; REQUIRE( MapFiles_Load( &loaded, Allocator_GetSystem(), path.toUtf8().constData() ).status == map_files_status_t::OK );
    map_wireframe_t loadedWire{}; REQUIRE( MapWireframe_Init( &loadedWire, Allocator_GetSystem() ) ); REQUIRE( MapWireframe_Build( &loadedWire, loaded ) == map_status_t::OK );
    CheckObjectVertices( loadedWire, id, expected ); CheckPointClose( loadedWire.bounds.box.minimum, exact.box.minimum ); CheckPointClose( loadedWire.bounds.box.maximum, exact.box.maximum );
}

TEST_CASE( "Rendered transform previews retain the committed face position and true normal colour", "[map][gui][views][geometry-edit][shared-transform][rendered-face]" )
{
    for ( int operation = 0; operation < 4; ++operation ) {
        CAPTURE( operation );
        session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        for ( const char *key : { "editor.viewport.active_border", "editor.viewport.perspective.show_axes", "editor.viewport.perspective.center_axes",
            "editor.viewport.perspective.show_selection_bounds", "editor.viewport.perspective.show_selection_dimensions",
            "editor.viewport.perspective.show_selection_vertices" } ) { settings.Set( key, false ); }
        // This regression samples the exact base normal color. Whole-object
        // selection is conveyed by edges instead of altering the face normal.
        // Surface-overlay alignment has its own world-lattice regressions.
        settings.Set( "editor.grid.show_surface_3d", false );
        MapWorkspace_SetGridVisible( &ws, CY_FALSE ); MapWorkspace_SetTool( &ws, map_tool_t::CAMERA );
        map_primitive_desc_t wedge{}; wedge.kind = map_primitive_kind_t::WEDGE; wedge.bounds = { { -96, -64, 0 }, { 96, 64, 128 } };
        REQUIRE( MapWorkspace_CreatePrimitive( &ws, wedge ) ); const u64 brush = EditorSelection_At( &ws.selection, 0 );
        u64 owner = 0;
        if ( operation == 2 ) {
            REQUIRE( MapDocument_AddEntity( ws.pDocument, StringView_FromCString( "default" ), StringView_FromCString( "func_door" ),
                { 128, 96, 0 }, &owner ) == map_status_t::OK );
            REQUIRE( MapDocument_SetGeometryOwner( ws.pDocument, brush, owner ) == map_status_t::OK );
            MapWorkspace_DocumentChanged( &ws ); MapWorkspace_Select( &ws, owner, MAP_SELECT_REPLACE );
            REQUIRE_FALSE( MapWorkspace_IsSelected( &ws, brush ) ); // Only the owning entity is selected.
        }
        map_transform_preview_t transform{}; transform.pivot = { 0, 0, 64 };
        if ( operation == 0 ) { transform.kind = map_transform_preview_kind_t::SCALE; transform.factors = { 1.7, 0.8, 0.6 }; }
        else if ( operation == 1 ) { transform.kind = map_transform_preview_kind_t::ROTATE; transform.degrees = { 15, -25, -35 }; }
        else { transform.kind = map_transform_preview_kind_t::TRANSLATE; transform.delta = operation == 2 ? math::vec3d_t{ 48, -32, 24 } : math::vec3d_t{ 0, -256, 0 }; }
        transform.bClone = operation == 3 ? CY_TRUE : CY_FALSE;
        std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
        // Owner selection must frame its authored children as well as the
        // entity helper, without adding the children to the selection.
        MapWorkspace_Frame( &ws, CY_TRUE );
        if ( owner != 0 ) {
            REQUIRE( ws.frameBounds.bHas );
            for ( u32 axis = 0; axis < 3; ++axis ) {
                CHECK( TestCoordinate( ws.frameBounds.box.minimum, axis ) <= TestCoordinate( wedge.bounds.minimum, axis ) );
                CHECK( TestCoordinate( ws.frameBounds.box.maximum, axis ) >= TestCoordinate( wedge.bounds.maximum, axis ) );
            }
            CHECK( EditorSelection_Count( &ws.selection ) == 1u ); CHECK( EditorSelection_At( &ws.selection, 0 ) == owner );
        }
        if ( transform.bClone ) {
            // Separate the retained source and future clone on screen while
            // keeping both in the same fixed camera throughout the edit.
            MapBounds_AddPoint( ws.frameBounds, math::Vec3d_Add( wedge.bounds.minimum, transform.delta ) );
            MapBounds_AddPoint( ws.frameBounds, math::Vec3d_Add( wedge.bounds.maximum, transform.delta ) );
            MapWorkspace_Notify( &ws, MAP_CHANGE_FRAME );
        }
        QCoreApplication::processEvents();
        const auto cameraPosition = MapCameraView_Position( camera.get() );
        const auto project = [&]( math::vec3d_t point ) {
            CAPTURE( point.x, point.y, point.z, cameraPosition.x, cameraPosition.y, cameraPosition.z );
            QPointF screen; REQUIRE( MapCameraView_WorldToView( camera.get(), point, &screen ) ); return screen;
        };
        const auto distance = []( QPointF point, QLineF edge ) {
            const QPointF d = edge.p2() - edge.p1(); const f64 squared = QPointF::dotProduct( d, d );
            const f64 t = squared > 0 ? std::clamp( QPointF::dotProduct( point - edge.p1(), d ) / squared, 0.0, 1.0 ) : 0.0;
            return QLineF( point, edge.p1() + d * t ).length();
        };
        std::vector<QLineF> edges;
        for ( usize i = 0; i < ws.wire.lines.nCount; ++i ) {
            const auto &edge = ws.wire.lines.pData[i]; const auto a = ws.wire.points.pData[edge.iA], b = ws.wire.points.pData[edge.iB];
            edges.emplace_back( project( a ), project( b ) );
            edges.emplace_back( project( MapWorkspace_TransformPreviewPoint( transform, a ) ), project( MapWorkspace_TransformPreviewPoint( transform, b ) ) );
        }
        if ( owner != 0 ) {
            const auto *entity = MapWireframe_FindObject( ws.wire, owner ); REQUIRE( entity != nullptr );
            const auto corner = [&]( u32 c ) { return math::vec3d_t{ c & 1 ? entity->bounds.box.maximum.x : entity->bounds.box.minimum.x,
                c & 2 ? entity->bounds.box.maximum.y : entity->bounds.box.minimum.y, c & 4 ? entity->bounds.box.maximum.z : entity->bounds.box.minimum.z }; };
            for ( u32 c = 0; c < 8; ++c ) { for ( u32 axis = 0; axis < 3; ++axis ) {
                if ( c & ( 1u << axis ) ) { continue; }
                const auto a = corner( c ), b = corner( c | ( 1u << axis ) );
                edges.emplace_back( project( a ), project( b ) );
                edges.emplace_back( project( MapWorkspace_TransformPreviewPoint( transform, a ) ), project( MapWorkspace_TransformPreviewPoint( transform, b ) ) );
            } }
        }
        std::vector<QRectF> overlays;
        if ( transform.kind == map_transform_preview_kind_t::TRANSLATE ) {
            const QLineF travel( project( transform.pivot ), project( math::Vec3d_Add( transform.pivot, transform.delta ) ) );
            edges.push_back( travel ); // The travel line and start/end markers are intentional overlays.
            const QFontMetrics metrics( gui::EditorStyle_Font( session.gui.style, "viewport.labels" ) );
            overlays.emplace_back( travel.p1() + QPointF( 8, 8 ), QSizeF( metrics.horizontalAdvance( transform.bClone ? QStringLiteral( "Source" ) : QStringLiteral( "Start" ) ) + 10, metrics.height() + 4 ) );
            if ( travel.length() >= 90 ) {
                // Reserve only the small distance-label area, not the face.
                overlays.emplace_back( travel.center() + QPointF( 8, 8 ), QSizeF( 90, metrics.height() + 4 ) );
            }
        }
        const auto unobstructed = [&]( QPointF screen ) {
            return camera->rect().adjusted( 12, 12, -12, -12 ).contains( screen.toPoint() ) &&
                std::all_of( edges.begin(), edges.end(), [&]( QLineF edge ) { return distance( screen, edge ) > 10; } ) &&
                std::none_of( overlays.begin(), overlays.end(), [&]( QRectF area ) { return area.adjusted( -4, -4, 4, 4 ).contains( screen ); } );
        };
        QPointF probe, sourceProbe; math::vec3d_t futureNormal{}, sourceNormal{}; bool found = false, sourceFound = false;
        for ( usize i = 0; i < ws.wire.faces.nCount && !found; ++i ) {
            const auto &face = ws.wire.faces.pData[i];
            const int components = ( std::abs( face.normal.x ) > 0.01 ) + ( std::abs( face.normal.y ) > 0.01 ) + ( std::abs( face.normal.z ) > 0.01 );
            if ( face.id != brush || components < 2 || face.nIndices < 3 ) { continue; }
            sourceNormal = face.normal;
            std::vector<math::vec3d_t> vertices; math::vec3d_t center{};
            for ( u32 k = 0; k < face.nIndices; ++k ) {
                const auto point = MapWorkspace_TransformPreviewPoint( transform, ws.wire.points.pData[ws.wire.faceIndices.pData[face.iFirstIndex + k]] );
                vertices.push_back( point ); center = math::Vec3d_Add( center, point );
            }
            center = math::Vec3d_Scale( center, 1.0 / vertices.size() );
            // Reconstruct the normal from future edges, independently of the
            // preview renderer's inverse-transpose normal calculation.
            futureNormal = math::Vec3d_Cross( math::Vec3d_Subtract( vertices[1], vertices[0] ), math::Vec3d_Subtract( vertices[2], vertices[0] ) );
            futureNormal = math::Vec3d_Scale( futureNormal, 1.0 / std::sqrt( math::Vec3d_LengthSquared( futureNormal ) ) );
            REQUIRE( math::Vec3d_Dot( futureNormal, math::Vec3d_Subtract( cameraPosition, center ) ) > 0 );
            std::vector<math::vec3d_t> candidates{ center };
            for ( const auto vertex : vertices ) { candidates.push_back( math::Vec3d_Add( math::Vec3d_Scale( center, 0.75 ), math::Vec3d_Scale( vertex, 0.25 ) ) ); }
            if ( vertices.size() == 4u ) {
                const auto u = math::Vec3d_Subtract( vertices[1], vertices[0] ), v = math::Vec3d_Subtract( vertices[3], vertices[0] );
                for ( int row = 1; row < 10; ++row ) { for ( int column = 1; column < 10; ++column ) {
                    candidates.push_back( math::Vec3d_Add( vertices[0], math::Vec3d_Add( math::Vec3d_Scale( u, column / 10.0 ), math::Vec3d_Scale( v, row / 10.0 ) ) ) );
                } }
            }
            for ( const auto point : candidates ) {
                const QPointF screen = project( point );
                if ( unobstructed( screen ) ) { probe = screen; found = true; break; }
            }
            if ( transform.bClone ) {
                for ( const auto future : candidates ) {
                    const auto source = math::Vec3d_Subtract( future, transform.delta );
                    REQUIRE( math::Vec3d_Dot( sourceNormal, math::Vec3d_Subtract( cameraPosition, source ) ) > 0 );
                    const QPointF screen = project( source );
                    if ( unobstructed( screen ) ) { sourceProbe = screen; sourceFound = true; break; }
                }
            }
        }
        REQUIRE( found ); // Face interiors cannot be supplied by outlines or source ghost edges.
        if ( transform.bClone ) { REQUIRE( sourceFound ); }
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision, selectionRevision = ws.selection.revision;
        const usize steps = EditorHistory_StepCount( &ws.history );
        MapCameraView_SetRenderMode( camera.get(), map_render_mode_t::NORMALS ); const QImage before = camera->grab().toImage();
        MapWorkspace_SetTransformPreview( &ws, transform ); REQUIRE( ws.editPreview.bActive );
        const map_render_mode_t modes[]{ map_render_mode_t::SHADED, map_render_mode_t::FULLBRIGHT, map_render_mode_t::NORMALS };
        QImage previews[3];
        for ( usize mode = 0; mode < 3; ++mode ) {
            MapCameraView_SetRenderMode( camera.get(), modes[mode] ); previews[mode] = camera->grab().toImage(); REQUIRE( ws.editPreview.bActive );
        }
        CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.selection.revision == selectionRevision );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CheckPointClose( MapCameraView_Position( camera.get() ), cameraPosition );
        if ( operation < 2 ) { CHECK( ChangedPixelsNear( before, previews[2], probe ) > 3 ); }
        const QColor expected = QColor::fromRgbF( static_cast<float>( futureNormal.x * 0.5 + 0.5 ),
            static_cast<float>( futureNormal.y * 0.5 + 0.5 ), static_cast<float>( futureNormal.z * 0.5 + 0.5 ) );
        const auto centerPixel = []( const QImage &image, QPointF point ) { return image.pixelColor( qRound( point.x() * image.devicePixelRatio() ), qRound( point.y() * image.devicePixelRatio() ) ); };
        const QColor previewNormal = centerPixel( previews[2], probe );
        CHECK( std::abs( previewNormal.red() - expected.red() ) <= 1 ); CHECK( std::abs( previewNormal.green() - expected.green() ) <= 1 ); CHECK( std::abs( previewNormal.blue() - expected.blue() ) <= 1 );
        CHECK( centerPixel( previews[0], probe ) != centerPixel( previews[1], probe ) ); CHECK( centerPixel( previews[1], probe ) != previewNormal );
        if ( transform.bClone ) {
            const QColor expectedSource = QColor::fromRgbF( static_cast<float>( sourceNormal.x * 0.5 + 0.5 ), static_cast<float>( sourceNormal.y * 0.5 + 0.5 ), static_cast<float>( sourceNormal.z * 0.5 + 0.5 ) );
            const QColor retained = centerPixel( previews[2], sourceProbe );
            CHECK( std::abs( retained.red() - expectedSource.red() ) <= 1 ); CHECK( std::abs( retained.green() - expectedSource.green() ) <= 1 ); CHECK( std::abs( retained.blue() - expectedSource.blue() ) <= 1 );
        }
        if ( operation == 0 ) { REQUIRE( MapWorkspace_ScaleSelection( &ws, transform.factors, transform.pivot ) ); }
        else if ( operation == 1 ) { REQUIRE( MapWorkspace_RotateSelection( &ws, transform.degrees, transform.pivot ) ); }
        else { REQUIRE( MapWorkspace_TranslateSelection( &ws, transform.delta, transform.bClone != CY_FALSE ) ); }
        CHECK_FALSE( ws.editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
        if ( transform.bClone ) {
            CHECK( EditorSelection_At( &ws.selection, 0 ) != brush ); REQUIRE( MapWireframe_FindObject( ws.wire, brush ) != nullptr );
            CHECK( ws.pDocument->geometry.brushes.nCount == 2u );
        } else { CHECK( EditorSelection_At( &ws.selection, 0 ) == ( owner != 0 ? owner : brush ) ); }
        for ( usize mode = 0; mode < 3; ++mode ) {
            CAPTURE( static_cast<int>( modes[mode] ) );
            MapCameraView_SetRenderMode( camera.get(), modes[mode] ); const QImage committed = camera->grab().toImage();
            const QPointF probes[]{ probe, sourceProbe };
            for ( usize sample = 0; sample < ( transform.bClone ? 2u : 1u ); ++sample ) {
                const QPointF point = probes[sample];
                const qreal ratio = committed.devicePixelRatio(); const QPoint center( qRound( point.x() * ratio ), qRound( point.y() * ratio ) );
                const int radius = static_cast<int>( std::ceil( 2 * ratio ) ); int maximumDifference = 0;
                for ( int y = center.y() - radius; y <= center.y() + radius; ++y ) { for ( int x = center.x() - radius; x <= center.x() + radius; ++x ) {
                    const QColor a = previews[mode].pixelColor( x, y ), b = committed.pixelColor( x, y );
                    maximumDifference = std::max( { maximumDifference, std::abs( a.red() - b.red() ), std::abs( a.green() - b.green() ), std::abs( a.blue() - b.blue() ) } );
                } }
                CHECK( maximumDifference <= 1 );
            }
        }
        CheckPointClose( MapCameraView_Position( camera.get() ), cameraPosition );
    }
}

TEST_CASE( "Perspective rotation uses its chosen world-axis plane and previews the rotated bounds", "[map][gui][views][geometry-edit][gizmo][rotate]" )
{
    session_t session;
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -32, 0 } ); MapBounds_AddPoint( box, { 64, 32, 96 } );
    REQUIRE( MapWorkspace_CreateBox( &session.workspace, box ) );
    MapWorkspace_SetAngleSnap( &session.workspace, 15 );
    MapWorkspace_SetTool( &session.workspace, map_tool_t::ROTATE );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &session.workspace ) );
    ShowAt( camera.get(), 800, 600 );
    MapWorkspace_Frame( &session.workspace, CY_TRUE );
    const math::vec3d_t pivot{ 0, 0, 48 };
    const f64 length = CameraGizmoLength( camera.get(), pivot, session.gui.settings );
    constexpr f64 startAngle = 3.14159265358979323846 / 8.0;
    constexpr f64 endAngle = startAngle + 3.14159265358979323846 / 2.0;
    QPointF start, end;
    // This Z ring vertex is away from the intersections with X and Y rings.
    REQUIRE( MapCameraView_WorldToView( camera.get(), { length * std::cos( startAngle ), length * std::sin( startAngle ), 48 }, &start ) );
    REQUIRE( MapCameraView_WorldToView( camera.get(), { length * std::cos( endAngle ), length * std::sin( endAngle ), 48 }, &end ) );
    REQUIRE( camera->rect().contains( start.toPoint() ) );
    REQUIRE( camera->rect().contains( end.toPoint() ) );
    DragMouse( camera.get(), QEvent::MouseButtonPress, start );
    DragMouse( camera.get(), QEvent::MouseMove, end );
    REQUIRE( session.workspace.editPreview.bActive );
    const auto preview = session.workspace.editPreview.bounds;
    CHECK( std::abs( preview.box.minimum.x + 32 ) < 1e-6 ); CHECK( std::abs( preview.box.maximum.x - 32 ) < 1e-6 );
    CHECK( std::abs( preview.box.minimum.y + 64 ) < 1e-6 ); CHECK( std::abs( preview.box.maximum.y - 64 ) < 1e-6 );
    CHECK( preview.box.minimum.z == 0 ); CHECK( preview.box.maximum.z == 96 );
    CHECK( MapViews_SelectionGeometryBounds( &session.workspace ).box.minimum.x == -64 );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == 1 );
    DragMouse( camera.get(), QEvent::MouseButtonRelease, end );
    CHECK_FALSE( session.workspace.editPreview.bActive );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == 2 );
    CHECK( std::abs( MapViews_SelectionGeometryBounds( &session.workspace ).box.minimum.x + 32 ) < 1e-6 );
    REQUIRE( MapWorkspace_Undo( &session.workspace ) == editor_history_status_t::OK );
    CHECK( MapViews_SelectionGeometryBounds( &session.workspace ).box.minimum.x == -64 );
    REQUIRE( MapWorkspace_Redo( &session.workspace ) == editor_history_status_t::OK );
    CHECK( std::abs( MapViews_SelectionGeometryBounds( &session.workspace ).box.minimum.y + 64 ) < 1e-6 );
}

TEST_CASE( "Camera axis move and scale solve long projected world drags from the captured grab", "[map][gui][views][geometry-edit][gizmo][axis-plane][precision]" )
{
    for ( const auto tool : { map_tool_t::TRANSLATE, map_tool_t::SCALE } ) {
        for ( u32 axis = 0; axis < 3; ++axis ) {
            for ( f64 sign : { -1.0, 1.0 } ) {
                for ( bool bypass : { false, true } ) {
                    CAPTURE( static_cast<int>( tool ), axis, sign, bypass );
                    session_t session; auto &ws = session.workspace;
                    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
                    map_bounds_t box{}; MapBounds_AddPoint( box, { -192, -192, 0 } ); MapBounds_AddPoint( box, { 192, 192, 384 } );
                    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
                    MapWorkspace_SetGridSize( &ws, 16 ); MapWorkspace_SetScaleSnap( &ws, 0.25 ); MapWorkspace_SetTool( &ws, tool );
                    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
                    MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
                    const auto pivot = MapBounds_Center( box );
                    const auto cameraPosition = MapCameraView_Position( camera.get() );
                    const f64 length = CameraGizmoLength( camera.get(), pivot, session.gui.settings );
                    // Two shaft grabs, away from the origin and plane pads,
                    // must keep their initial offset throughout the drag.
                    auto grab = pivot; SetTestCoordinate( grab, axis, TestCoordinate( grab, axis ) + length * ( bypass ? 0.90 : 0.60 ) );
                    auto target = grab; SetTestCoordinate( target, axis, TestCoordinate( target, axis ) + sign * 137.0 );
                    QPointF start, end;
                    REQUIRE( MapCameraView_WorldToView( camera.get(), grab, &start ) );
                    REQUIRE( MapCameraView_WorldToView( camera.get(), target, &end ) );
                    REQUIRE( camera->rect().adjusted( 8, 8, -8, -8 ).contains( start.toPoint() ) );
                    REQUIRE( QLineF( start, end ).length() > 3 );
                    const auto modifiers = bypass ? Qt::ControlModifier : Qt::NoModifier;
                    const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
                    const auto selectionRevision = ws.selection.revision; const usize steps = EditorHistory_StepCount( &ws.history );
                    const auto original = ObjectLineVertices( ws.wire, id );
                    DragMouse( camera.get(), QEvent::MouseButtonPress, start, modifiers );
                    CHECK_FALSE( ws.editPreview.bActive );
                    const auto checkPreview = [&]() {
                        REQUIRE( ws.editPreview.bActive );
                        const f64 delta = sign * ( bypass ? 137.0 : 144.0 );
                        auto expected = box;
                        if ( tool == map_tool_t::TRANSLATE ) {
                            CHECK( ws.editPreview.transform.kind == map_transform_preview_kind_t::TRANSLATE );
                            math::vec3d_t offset{}; SetTestCoordinate( offset, axis, delta );
                            CheckPointClose( ws.editPreview.transform.delta, offset );
                            expected.box.minimum = math::Vec3d_Add( box.box.minimum, offset ); expected.box.maximum = math::Vec3d_Add( box.box.maximum, offset );
                        } else {
                            CHECK( ws.editPreview.transform.kind == map_transform_preview_kind_t::SCALE );
                            auto factors = math::vec3d_t{ 1, 1, 1 }; SetTestCoordinate( factors, axis, 1.0 + delta / 192.0 );
                            CheckPointClose( ws.editPreview.transform.factors, factors );
                            SetTestCoordinate( expected.box.minimum, axis, TestCoordinate( pivot, axis ) - 192.0 * TestCoordinate( factors, axis ) );
                            SetTestCoordinate( expected.box.maximum, axis, TestCoordinate( pivot, axis ) + 192.0 * TestCoordinate( factors, axis ) );
                        }
                        CheckPointClose( ws.editPreview.bounds.box.minimum, expected.box.minimum ); CheckPointClose( ws.editPreview.bounds.box.maximum, expected.box.maximum );
                        CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision ); CHECK( ws.selection.revision == selectionRevision );
                        CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CheckObjectVertices( ws.wire, id, original );
                        CheckPointClose( MapCameraView_Position( camera.get() ), cameraPosition );
                    };
                    DragMouse( camera.get(), QEvent::MouseMove, end, modifiers ); checkPreview();
                    // Returning to the captured grab removes the preview;
                    // another motion solves from that same original anchor.
                    DragMouse( camera.get(), QEvent::MouseMove, start, modifiers ); CHECK_FALSE( ws.editPreview.bActive );
                    DragMouse( camera.get(), QEvent::MouseMove, end, modifiers ); checkPreview();
                    const auto shown = ws.editPreview.bounds;
                    DragMouse( camera.get(), QEvent::MouseButtonRelease, end, modifiers );
                    CHECK_FALSE( ws.editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
                    CHECK( EditorSelection_At( &ws.selection, 0 ) == id );
                    CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, shown.box.minimum ); CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, shown.box.maximum );
                    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CheckObjectVertices( ws.wire, id, original );
                    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
                    CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, shown.box.minimum ); CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, shown.box.maximum );
                }
            }
        }
    }
}

TEST_CASE( "Camera constrained axis rays crossing the near plane cannot publish the previous ghost", "[map][gui][views][geometry-edit][gizmo][axis-plane][near-plane][cancel]" )
{
    for ( const auto tool : { map_tool_t::TRANSLATE, map_tool_t::SCALE } ) {
        for ( f64 invalidDepth : { 0.5, -8.0 } ) {
            CAPTURE( static_cast<int>( tool ), invalidDepth );
            session_t session; auto &ws = session.workspace;
            REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
            map_bounds_t box{}; MapBounds_AddPoint( box, { -192, -192, 0 } ); MapBounds_AddPoint( box, { 192, 192, 384 } );
            REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
            MapWorkspace_SetTool( &ws, tool );
            std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
            MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
            const auto pivot = MapBounds_Center( box );
            const f64 length = CameraGizmoLength( camera.get(), pivot, session.gui.settings );
            auto grab = pivot; grab.x += length * 0.70;
            auto target = grab; target.x += 137;
            QPointF start, end;
            REQUIRE( MapCameraView_WorldToView( camera.get(), grab, &start ) ); REQUIRE( MapCameraView_WorldToView( camera.get(), target, &end ) );
            const auto position = MapCameraView_Position( camera.get() ), forward = MapCameraView_Forward( camera.get() );
            REQUIRE( std::abs( forward.x ) > 0.2 );
            auto invalid = grab;
            const auto relative = math::Vec3d_Subtract( grab, position );
            invalid.x += ( invalidDepth - math::Vec3d_Dot( relative, forward ) ) / forward.x;
            QPointF rejected; REQUIRE_FALSE( MapCameraView_WorldToView( camera.get(), invalid, &rejected ) );
            // Project the rejected world point algebraically so the event
            // still carries a finite cursor ray intersecting this same axis
            // before the near plane or behind the camera.
            const f64 horizontal = std::hypot( forward.x, forward.y );
            const math::vec3d_t right{ forward.y / horizontal, -forward.x / horizontal, 0 };
            const math::vec3d_t up = math::Vec3d_Cross( right, forward );
            const auto invalidRelative = math::Vec3d_Subtract( invalid, position );
            const f64 fov = EditorSettings_Real( &session.gui.settings, "editor.camera.fov", 75.0 );
            const f64 focal = camera->width() * 0.5 / std::tan( fov * 3.14159265358979323846 / 360.0 );
            const QPointF invalidScreen( camera->width() * 0.5 + math::Vec3d_Dot( invalidRelative, right ) * focal / invalidDepth,
                                        camera->height() * 0.5 - math::Vec3d_Dot( invalidRelative, up ) * focal / invalidDepth );
            REQUIRE( std::isfinite( invalidScreen.x() ) ); REQUIRE( std::isfinite( invalidScreen.y() ) );
            const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
            const auto selectionRevision = ws.selection.revision; const usize steps = EditorHistory_StepCount( &ws.history );
            const auto original = ObjectLineVertices( ws.wire, id );
            DragMouse( camera.get(), QEvent::MouseButtonPress, start, Qt::ControlModifier );
            DragMouse( camera.get(), QEvent::MouseMove, end, Qt::ControlModifier ); REQUIRE( ws.editPreview.bActive );
            DragMouse( camera.get(), QEvent::MouseMove, invalidScreen, Qt::ControlModifier ); CHECK_FALSE( ws.editPreview.bActive );
            DragMouse( camera.get(), QEvent::MouseButtonRelease, end, Qt::ControlModifier );
            QKeyEvent confirm( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &confirm );
            CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
            CHECK( ws.selection.revision == selectionRevision ); CHECK( EditorSelection_At( &ws.selection, 0 ) == id );
            CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CheckObjectVertices( ws.wire, id, original );
        }
    }
}

TEST_CASE( "Active creation honors remapped gesture confirm and cancel in either viewport family", "[map][gui][views][geometry-edit][block][input]" )
{
    for ( int scenario = 0; scenario < 4; ++scenario ) {
        const bool perspective = ( scenario & 1 ) != 0;
        const bool confirm = ( scenario & 2 ) != 0;
        CAPTURE( perspective, confirm );
        session_t session;
        REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
        settings_document_t keys{};
        REQUIRE( SettingsDocument_Init( &keys, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
        const char *text = R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "view_gestures" bindings = { "map.tool.block" = {
    "map.tool.confirm" = [ "V" ]
    "map.tool.cancel" = [ "C" ]
} } })cykv";
        REQUIRE( SettingsDocument_Load( &keys, StringView_FromCString( text ) ).status == settings_document_status_t::OK );
        session.gui.keymapChain[0] = SettingsDocument_Root( &keys ); session.gui.nKeymapChain = 1;
        MapWorkspace_SetTool( &session.workspace, map_tool_t::BLOCK );
        std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &session.workspace ) :
            MapOrthoView_Create( nullptr, &session.workspace, map_ortho_axes_t::TOP ) );
        ShowAt( view.get(), 800, 600 );
        QPointF start, end;
        if ( perspective ) {
            MapWorkspace_Frame( &session.workspace, CY_FALSE );
            REQUIRE( MapCameraView_WorldToView( view.get(), { -64, -64, 0 }, &start ) );
            REQUIRE( MapCameraView_WorldToView( view.get(), { 64, 64, 0 }, &end ) );
        } else {
            start = MapOrthoView_WorldToView( view.get(), { -64, -64 } );
            end = MapOrthoView_WorldToView( view.get(), { 64, 64 } );
        }
        DragMouse( view.get(), QEvent::MouseButtonPress, start );
        DragMouse( view.get(), QEvent::MouseMove, end );
        REQUIRE( session.workspace.editPreview.bActive );
        QKeyEvent oldCancel( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
        QKeyEvent oldConfirm( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier );
        QCoreApplication::sendEvent( view.get(), &oldCancel );
        QCoreApplication::sendEvent( view.get(), &oldConfirm );
        CHECK( session.workspace.editPreview.bActive );
        CHECK( session.workspace.pDocument->geometry.brushes.nCount == 0 );
        CHECK( EditorHistory_StepCount( &session.workspace.history ) == 0 );

        const int key = confirm ? Qt::Key_V : Qt::Key_C;
        QKeyEvent preflight( QEvent::ShortcutOverride, key, Qt::NoModifier );
        preflight.ignore();
        QCoreApplication::sendEvent( view.get(), &preflight );
        CHECK( preflight.isAccepted() );
        CHECK( session.workspace.editPreview.bActive );
        CHECK( EditorHistory_StepCount( &session.workspace.history ) == 0 );
        QKeyEvent action( QEvent::KeyPress, key, Qt::NoModifier );
        QCoreApplication::sendEvent( view.get(), &action );
        CHECK_FALSE( session.workspace.editPreview.bActive );
        DragMouse( view.get(), QEvent::MouseButtonRelease, end );
        CHECK( session.workspace.pDocument->geometry.brushes.nCount == ( confirm ? 1u : 0u ) );
        CHECK( EditorHistory_StepCount( &session.workspace.history ) == ( confirm ? 1u : 0u ) );
        if ( confirm ) {
            CHECK( MapViews_SelectionGeometryBounds( &session.workspace ).box.maximum.z == 64 );
            REQUIRE( MapWorkspace_Undo( &session.workspace ) == editor_history_status_t::OK );
            CHECK( session.workspace.pDocument->geometry.brushes.nCount == 0 );
        } else { CHECK_FALSE( MapWorkspace_IsModified( &session.workspace ) ); }
        session.gui.nKeymapChain = 0;
    }
}

TEST_CASE( "Keyboard confirmation commits the primitive that was last previewed", "[map][gui][views][geometry-edit][block][input][primitive]" )
{
    for ( bool perspective : { false, true } ) {
        CAPTURE( perspective );
        session_t session;
        view_settings_t settings( &session.gui.settings );
        REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
        settings.Choice( "editor.map.new_brush_shape", "cylinder" );
        settings.Choice( "editor.map.primitive_axis", "X" );
        settings.Integer( "editor.map.cylinder_sides", 12 );
        settings.Real( "editor.map.default_texture_scale", 0.5 );
        MapWorkspace_SetTool( &session.workspace, map_tool_t::BLOCK );
        std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &session.workspace ) :
            MapOrthoView_Create( nullptr, &session.workspace, map_ortho_axes_t::TOP ) );
        ShowAt( view.get(), 800, 600 );
        QPointF start, end;
        if ( perspective ) {
            MapWorkspace_Frame( &session.workspace, CY_FALSE );
            REQUIRE( MapCameraView_WorldToView( view.get(), { -64, -64, 0 }, &start ) );
            REQUIRE( MapCameraView_WorldToView( view.get(), { 64, 64, 0 }, &end ) );
        } else {
            start = MapOrthoView_WorldToView( view.get(), { -64, -64 } );
            end = MapOrthoView_WorldToView( view.get(), { 64, 64 } );
        }
        DragMouse( view.get(), QEvent::MouseButtonPress, start );
        DragMouse( view.get(), QEvent::MouseMove, end );
        REQUIRE( session.workspace.editPreview.bActive );
        REQUIRE( session.workspace.editPreview.status == map_status_t::OK );
        REQUIRE( session.workspace.editPreviewWire.faces.nCount == 14 );
        const auto shown = session.workspace.editPreview.primitive;
        settings.Choice( "editor.map.new_brush_shape", "sphere" );
        settings.Choice( "editor.map.primitive_axis", "Z" );
        settings.Integer( "editor.map.cylinder_sides", 24 );
        settings.Real( "editor.map.default_texture_scale", 0.25 );
        // Settings do not mutate a preview until its next update. Confirming
        // now must publish the visible cylinder, rather than a new sphere.
        CHECK( session.workspace.editPreview.primitive.kind == shown.kind );
        CHECK( session.workspace.editPreviewWire.faces.nCount == 14 );
        QKeyEvent confirm( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier );
        QCoreApplication::sendEvent( view.get(), &confirm );
        CHECK_FALSE( session.workspace.editPreview.bActive );
        REQUIRE( session.workspace.pDocument->geometry.brushes.nCount == 1 );
        const auto *brush = session.workspace.pDocument->geometry.brushes.pData[0];
        const auto *attributes = geometry::GeometryDocument_FindBrushAttributes( &session.workspace.pDocument->geometry, brush->sourceId );
        REQUIRE( attributes != nullptr );
        CHECK( brush->sides.nCount == 14 );
        int xCaps = 0;
        for ( usize side = 0; side < brush->sides.nCount; ++side ) {
            xCaps += std::abs( brush->sides.pData[side].plane.normal.x ) > 0.99 ? 1 : 0;
            const auto &uv = attributes->records.pData[brush->sides.pData[side].iAttributeIndex].uvProjection;
            CHECK( uv.worldUnitsPerUv.x == shown.worldUnitsPerUv );
            CHECK( uv.worldUnitsPerUv.y == shown.worldUnitsPerUv );
        }
        CHECK( xCaps == 2 );
        CHECK( EditorHistory_StepCount( &session.workspace.history ) == 1 );
        DragMouse( view.get(), QEvent::MouseButtonRelease, end );
        CHECK( session.workspace.pDocument->geometry.brushes.nCount == 1 );
        CHECK( EditorHistory_StepCount( &session.workspace.history ) == 1 );
        REQUIRE( MapWorkspace_Undo( &session.workspace ) == editor_history_status_t::OK );
        CHECK( session.workspace.pDocument->geometry.brushes.nCount == 0 );
        REQUIRE( MapWorkspace_Redo( &session.workspace ) == editor_history_status_t::OK );
        CHECK( session.workspace.pDocument->geometry.brushes.pData[0]->sides.nCount == 14 );
    }
}

TEST_CASE( "Construction edges and translucent faces survive camera near-plane crossing", "[map][gui][views][geometry-edit][block][near-clip]" )
{
    session_t session;
    view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    settings.Real( "editor.camera.look_sensitivity", 1.0 );
    settings.Set( "editor.camera.invert_y", false );
    settings.Set( "editor.viewport.perspective.show_axes", false );
    settings.Set( "editor.viewport.perspective.center_axes", false );
    settings.Set( "editor.viewport.perspective.show_selection_dimensions", false );
    MapWorkspace_SetGridVisible( &session.workspace, CY_FALSE );
    MapWorkspace_SetTool( &session.workspace, map_tool_t::BLOCK );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &session.workspace ) );
    ShowAt( camera.get(), 800, 600 );
    FacePositiveX( camera.get() );
    const auto position = MapCameraView_Position( camera.get() );
    map_bounds_t box{};
    MapBounds_AddPoint( box, { position.x - 16, position.y + 16, position.z - 32 } );
    MapBounds_AddPoint( box, { position.x + 256, position.y + 64, position.z + 32 } );
    QPointF rejected, edgeProbe, faceProbe;
    REQUIRE_FALSE( MapCameraView_WorldToView( camera.get(), box.box.minimum, &rejected ) );
    REQUIRE( MapCameraView_WorldToView( camera.get(), { position.x + 128, position.y + 16, position.z + 32 }, &edgeProbe ) );
    REQUIRE( MapCameraView_WorldToView( camera.get(), { position.x + 128, position.y + 40, position.z + 32 }, &faceProbe ) );
    REQUIRE( camera->rect().adjusted( 8, 8, -8, -8 ).contains( edgeProbe.toPoint() ) );
    REQUIRE( camera->rect().adjusted( 8, 8, -8, -8 ).contains( faceProbe.toPoint() ) );
    const QImage before = camera->grab().toImage();
    MapWorkspace_SetEditPreview( &session.workspace, box );
    REQUIRE( session.workspace.editPreview.status == map_status_t::OK );
    const QImage preview = camera->grab().toImage();
    // These probes are beyond the entirely visible front face. The first
    // lies on an edge whose other endpoint is behind the camera; the second
    // is inside a face with two behind-camera vertices, away from its edges.
    CHECK( ChangedPixelsNear( before, preview, edgeProbe ) > 3 );
    CHECK( ChangedPixelsNear( before, preview, faceProbe ) > 3 );
    CHECK( session.workspace.pDocument->geometry.brushes.nCount == 0 );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == 0 );
    MapWorkspace_ClearEditPreview( &session.workspace );
    const QImage cleared = camera->grab().toImage();
    CHECK( ChangedPixelsNear( before, cleared, edgeProbe ) == 0 );
    CHECK( ChangedPixelsNear( before, cleared, faceProbe ) == 0 );
}

TEST_CASE( "A selected brush face keeps its clipped fill in a wireframe camera", "[map][gui][views][face][near-clip]" )
{
    session_t session;
    view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    settings.Real( "editor.camera.look_sensitivity", 1.0 );
    settings.Set( "editor.camera.invert_y", false );
    settings.Set( "editor.viewport.perspective.show_axes", false );
    settings.Set( "editor.viewport.perspective.center_axes", false );
    settings.Set( "editor.viewport.perspective.show_selection_bounds", false );
    settings.Set( "editor.viewport.perspective.show_selection_dimensions", false );
    settings.Set( "editor.viewport.perspective.show_selection_vertices", false );
    MapWorkspace_SetGridVisible( &session.workspace, CY_FALSE );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &session.workspace ) );
    ShowAt( camera.get(), 800, 600 );
    MapCameraView_SetRenderMode( camera.get(), map_render_mode_t::WIREFRAME );
    FacePositiveX( camera.get() );
    const auto position = MapCameraView_Position( camera.get() );
    map_bounds_t box{};
    MapBounds_AddPoint( box, { position.x - 16, position.y + 16, position.z - 32 } );
    MapBounds_AddPoint( box, { position.x + 256, position.y + 64, position.z + 32 } );
    REQUIRE( MapWorkspace_CreateBox( &session.workspace, box ) );
    const auto *brush = session.workspace.pDocument->geometry.brushes.pData[0];
    const u64 object = brush->sourceId.value;
    u64 topSide = 0;
    for ( usize side = 0; side < brush->sides.nCount; ++side ) {
        if ( brush->sides.pData[side].plane.normal.z > 0.99 ) { topSide = brush->sides.pData[side].sourceId.value; }
    }
    REQUIRE( topSide != 0 );
    int visibleVertices = 0, clippedVertices = 0;
    for ( usize i = 0; i < session.workspace.wire.faces.nCount; ++i ) {
        const auto &face = session.workspace.wire.faces.pData[i];
        if ( face.id != object || face.sideId != topSide ) { continue; }
        REQUIRE( face.nIndices == 4 );
        for ( u32 vertex = 0; vertex < face.nIndices; ++vertex ) {
            const auto point = session.workspace.wire.points.pData[session.workspace.wire.faceIndices.pData[face.iFirstIndex + vertex]];
            QPointF screen;
            if ( MapCameraView_WorldToView( camera.get(), point, &screen ) ) { ++visibleVertices; }
            else { ++clippedVertices; }
        }
    }
    REQUIRE( visibleVertices == 2 );
    REQUIRE( clippedVertices == 2 );
    QPointF faceProbe;
    REQUIRE( MapCameraView_WorldToView( camera.get(), { position.x + 128, position.y + 40, position.z + 32 }, &faceProbe ) );
    REQUIRE( camera->rect().adjusted( 8, 8, -8, -8 ).contains( faceProbe.toPoint() ) );
    MapWorkspace_SetElementMode( &session.workspace, map_element_mode_t::FACES );
    const QImage before = camera->grab().toImage();
    MapWorkspace_SelectBrushFace( &session.workspace, object, topSide );
    REQUIRE( MapWorkspace_HasBrushFace( &session.workspace ) );
    CHECK( session.workspace.selectedBrushFaceSide == topSide );
    CHECK( MapCameraView_RenderMode( camera.get() ) == map_render_mode_t::WIREFRAME );
    // An interior probe beyond the front cap isolates selected-face fill:
    // committed wire edges and the ordinary shaded-face path cannot paint it.
    CHECK( ChangedPixelsNear( before, camera->grab().toImage(), faceProbe ) > 3 );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == 1 );
    MapWorkspace_ClearBrushFace( &session.workspace );
    CHECK_FALSE( MapWorkspace_HasBrushFace( &session.workspace ) );
    CHECK( ChangedPixelsNear( before, camera->grab().toImage(), faceProbe ) == 0 );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == 1 );
}

TEST_CASE( "A transformed cylinder keeps its near-clipped authored edges visible", "[map][gui][views][geometry-edit][gizmo][near-clip]" )
{
    session_t session;
    view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    settings.Real( "editor.camera.look_sensitivity", 1.0 );
    settings.Set( "editor.camera.invert_y", false );
    settings.Set( "editor.viewport.perspective.show_axes", false );
    settings.Set( "editor.viewport.perspective.center_axes", false );
    settings.Set( "editor.viewport.perspective.show_selection_bounds", false );
    settings.Set( "editor.viewport.perspective.show_selection_dimensions", false );
    settings.Set( "editor.viewport.perspective.show_selection_vertices", false );
    settings.Choice( "editor.map.new_brush_shape", "cylinder" );
    settings.Choice( "editor.map.primitive_axis", "X" );
    settings.Integer( "editor.map.cylinder_sides", 12 );
    MapWorkspace_SetGridVisible( &session.workspace, CY_FALSE );
    MapWorkspace_SetGridSize( &session.workspace, 16 );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &session.workspace ) );
    ShowAt( camera.get(), 800, 600 );
    FacePositiveX( camera.get() );
    const auto position = MapCameraView_Position( camera.get() );
    map_bounds_t box{};
    MapBounds_AddPoint( box, { position.x - 16, position.y + 16, position.z - 32 } );
    MapBounds_AddPoint( box, { position.x + 256, position.y + 64, position.z + 32 } );
    REQUIRE( MapWorkspace_CreatePrimitive( &session.workspace, box ) );
    MapWorkspace_SetTool( &session.workspace, map_tool_t::TRANSLATE );
    const math::vec3d_t pivot{ position.x + 120, position.y + 40, position.z };
    const f64 length = CameraGizmoLength( camera.get(), pivot, session.gui.settings );
    QPointF center, tip;
    REQUIRE( MapCameraView_WorldToView( camera.get(), pivot, &center ) );
    REQUIRE( MapCameraView_WorldToView( camera.get(), { pivot.x, pivot.y, pivot.z + length }, &tip ) );
    const QPointF start = center + ( tip - center ) * 0.7;
    const QPointF end = start + ( tip - center ) * ( 16.0 / length );
    // Find the top longitudinal edge from the generated topology. Unlike a
    // box corner, its Y position is inside the bounds envelope, so the green
    // bounds guide cannot accidentally satisfy this authored-edge check.
    math::vec3d_t edgeA{}, edgeB{};
    bool found = false;
    for ( usize i = 0; i < session.workspace.wire.lines.nCount; ++i ) {
        const auto &line = session.workspace.wire.lines.pData[i];
        const auto a = session.workspace.wire.points.pData[line.iA], b = session.workspace.wire.points.pData[line.iB];
        if ( std::abs( a.x - b.x ) > 200 && std::abs( a.y - pivot.y ) < 1e-6 && std::abs( a.z - box.box.maximum.z ) < 1e-6 &&
             std::abs( a.y - b.y ) < 1e-6 && std::abs( a.z - b.z ) < 1e-6 ) {
            edgeA = a.x < b.x ? a : b; edgeB = a.x < b.x ? b : a; found = true; break;
        }
    }
    REQUIRE( found );
    QPointF behind, edgeProbe;
    REQUIRE_FALSE( MapCameraView_WorldToView( camera.get(), { edgeA.x, edgeA.y, edgeA.z + 16 }, &behind ) );
    REQUIRE( MapCameraView_WorldToView( camera.get(), { position.x + 128, edgeB.y, edgeB.z + 16 }, &edgeProbe ) );
    REQUIRE( camera->rect().adjusted( 8, 8, -8, -8 ).contains( edgeProbe.toPoint() ) );
    DragMouse( camera.get(), QEvent::MouseButtonPress, start );
    const QImage before = camera->grab().toImage();
    DragMouse( camera.get(), QEvent::MouseMove, end );
    REQUIRE( session.workspace.editPreview.bActive );
    CHECK( session.workspace.editPreview.transform.kind == map_transform_preview_kind_t::TRANSLATE );
    CheckPointClose( session.workspace.editPreview.transform.delta, { 0, 0, 16 } );
    CHECK( session.workspace.editPreviewWire.points.nCount == 0u );
    CHECK( std::abs( session.workspace.editPreview.bounds.box.maximum.z - box.box.maximum.z - 16 ) < 1e-6 );
    const QImage preview = camera->grab().toImage();
    CHECK( ChangedPixelsNear( before, preview, edgeProbe ) > 3 );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == 1 );
    QKeyEvent cancel( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
    QCoreApplication::sendEvent( camera.get(), &cancel );
    CHECK_FALSE( session.workspace.editPreview.bActive );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == 1 );
    const QImage cleared = camera->grab().toImage();
    CHECK( ChangedPixelsNear( before, cleared, edgeProbe ) == 0 );
}

TEST_CASE( "Orthographic clipping stages its retained solid until keyboard confirmation", "[map][gui][views][geometry-edit][clip]" )
{
    for ( int projection = 0; projection < 3; ++projection ) {
        CAPTURE( projection );
        session_t session;
        view_settings_t settings( &session.gui.settings );
        REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, -64 } ); MapBounds_AddPoint( box, { 64, 64, 64 } );
        REQUIRE( MapWorkspace_CreateBox( &session.workspace, box ) );
        const u64 object = EditorSelection_At( &session.workspace.selection, 0 );
        const auto *document = session.workspace.pDocument;
        const auto revision = document->geometry.revision;
        settings.Choice( "editor.map.clip_mode", "back" );
        MapWorkspace_SetGridSize( &session.workspace, 16 );
        MapWorkspace_SetTool( &session.workspace, map_tool_t::CLIP );
        std::unique_ptr<QWidget> view( MapOrthoView_Create( nullptr, &session.workspace, static_cast<map_ortho_axes_t>( projection ) ) );
        ShowAt( view.get(), 800, 600 );
        const QPointF start = MapOrthoView_WorldToView( view.get(), { 16, -96 } );
        const QPointF end = MapOrthoView_WorldToView( view.get(), { 16, 96 } );
        DragMouse( view.get(), QEvent::MouseButtonPress, start );
        DragMouse( view.get(), QEvent::MouseMove, end );
        REQUIRE( session.workspace.editPreview.bActive );
        REQUIRE( session.workspace.editPreview.bClip );
        REQUIRE( session.workspace.editPreview.status == map_status_t::OK );
        REQUIRE( session.workspace.editPreviewWire.objects.nCount == 1 );
        const auto plane = session.workspace.editPreview.clipPlane;
        const f64 normal[]{ plane.normal.x, plane.normal.y, plane.normal.z };
        const int u = projection == 1 ? 1 : 0;
        const int omitted = projection == 0 ? 2 : projection == 1 ? 0 : 1;
        CHECK( std::abs( normal[u] - 1 ) < 1e-6 );
        CHECK( std::abs( normal[omitted] ) < 1e-6 );
        CHECK( std::abs( plane.d + 16 ) < 1e-6 );
        const auto retained = session.workspace.editPreviewWire.bounds;
        const f64 maximum[]{ retained.box.maximum.x, retained.box.maximum.y, retained.box.maximum.z };
        CHECK( std::abs( maximum[u] - 16 ) < 1e-6 );
        CHECK( std::abs( maximum[omitted] - 64 ) < 1e-6 );
        DragMouse( view.get(), QEvent::MouseButtonRelease, end );
        REQUIRE( session.workspace.editPreview.bActive );
        CHECK( session.workspace.pDocument == document );
        CHECK( session.workspace.pDocument->geometry.revision == revision );
        CHECK( session.workspace.pDocument->geometry.brushes.nCount == 1 );
        CHECK( EditorHistory_StepCount( &session.workspace.history ) == 1 );
        // After release, an ordinary hover must not silently move the plane.
        const QPointF hover = end + QPointF( 70, 40 );
        QMouseEvent move( QEvent::MouseMove, hover, view->mapToGlobal( hover ), Qt::NoButton, Qt::NoButton, Qt::NoModifier );
        QCoreApplication::sendEvent( view.get(), &move );
        CHECK( session.workspace.editPreview.clipPlane.d == plane.d );
        CHECK( session.workspace.editPreview.clipMode == map_brush_clip_mode_t::BACK );
        QKeyEvent confirm( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier );
        QCoreApplication::sendEvent( view.get(), &confirm );
        CHECK_FALSE( session.workspace.editPreview.bActive );
        CHECK( EditorHistory_StepCount( &session.workspace.history ) == 2 );
        REQUIRE( session.workspace.pDocument->geometry.brushes.nCount == 1 );
        CHECK( EditorSelection_At( &session.workspace.selection, 0 ) == object );
        const auto committed = MapViews_SelectionGeometryBounds( &session.workspace );
        const f64 committedMaximum[]{ committed.box.maximum.x, committed.box.maximum.y, committed.box.maximum.z };
        CHECK( std::abs( committedMaximum[u] - 16 ) < 1e-6 );
        REQUIRE( MapWorkspace_Undo( &session.workspace ) == editor_history_status_t::OK );
        const auto undone = MapViews_SelectionGeometryBounds( &session.workspace );
        CHECK( undone.box.minimum.x == -64 ); CHECK( undone.box.maximum.x == 64 );
        CHECK( undone.box.minimum.y == -64 ); CHECK( undone.box.maximum.y == 64 );
        CHECK( undone.box.minimum.z == -64 ); CHECK( undone.box.maximum.z == 64 );
        REQUIRE( MapWorkspace_Redo( &session.workspace ) == editor_history_status_t::OK );
        CHECK( EditorSelection_At( &session.workspace.selection, 0 ) == object );
        const auto redone = MapViews_SelectionGeometryBounds( &session.workspace );
        const f64 redoneMaximum[]{ redone.box.maximum.x, redone.box.maximum.y, redone.box.maximum.z };
        CHECK( std::abs( redoneMaximum[u] - 16 ) < 1e-6 );
    }
}

TEST_CASE( "Repeated clipping tool commands cycle the staged sides without replacing its plane", "[map][gui][views][geometry-edit][clip][input]" )
{
    session_t session;
    REQUIRE( MapWorkspace_RegisterCommands( &session.workspace, &session.gui.commands ) == command_registry_status_t::OK );
    view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, -64 } ); MapBounds_AddPoint( box, { 64, 64, 64 } );
    REQUIRE( MapWorkspace_CreateBox( &session.workspace, box ) );
    const auto *document = session.workspace.pDocument;
    settings.Choice( "editor.map.clip_mode", "back" );
    settings.Choice( "editor.map.default_material", "materials/dev/clip_preview.cymat" );
    REQUIRE( EditorCommands_ExecuteLine( &session.gui.commands, StringView_FromCString( "map.tool.clip" ) ) == command_result_t::OK );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &session.workspace, map_ortho_axes_t::TOP ) );
    ShowAt( top.get(), 800, 600 );
    const QPointF start = MapOrthoView_WorldToView( top.get(), { 0, -96 } );
    const QPointF end = MapOrthoView_WorldToView( top.get(), { 0, 96 } );
    DragMouse( top.get(), QEvent::MouseButtonPress, start ); DragMouse( top.get(), QEvent::MouseMove, end );
    DragMouse( top.get(), QEvent::MouseButtonRelease, end );
    REQUIRE( session.workspace.editPreview.bClip );
    const auto plane = session.workspace.editPreview.clipPlane;
    CHECK( MapWorkspace_ClipMode( &session.workspace ) == map_brush_clip_mode_t::BACK );
    CHECK( session.workspace.editPreviewWire.bounds.box.maximum.x == 0 );
    REQUIRE( EditorCommands_ExecuteLine( &session.gui.commands, StringView_FromCString( "map.tool.clip" ) ) == command_result_t::OK );
    REQUIRE( session.workspace.editPreview.bActive );
    CHECK( session.workspace.editPreview.clipMode == map_brush_clip_mode_t::FRONT );
    CHECK( session.workspace.editPreviewWire.bounds.box.minimum.x == 0 );
    CHECK( session.workspace.editPreviewWire.bounds.box.maximum.x == 64 );
    REQUIRE( EditorCommands_ExecuteLine( &session.gui.commands, StringView_FromCString( "map.tool.clip" ) ) == command_result_t::OK );
    REQUIRE( session.workspace.editPreview.bActive );
    CHECK( session.workspace.editPreview.clipMode == map_brush_clip_mode_t::BOTH );
    CHECK( session.workspace.editPreviewWire.objects.nCount == 2 );
    CHECK( session.workspace.editPreviewWire.bounds.box.minimum.x == -64 );
    CHECK( session.workspace.editPreviewWire.bounds.box.maximum.x == 64 );
    CHECK( session.workspace.editPreview.clipPlane.normal.x == plane.normal.x );
    CHECK( session.workspace.editPreview.clipPlane.normal.y == plane.normal.y );
    CHECK( session.workspace.editPreview.clipPlane.normal.z == plane.normal.z );
    CHECK( session.workspace.editPreview.clipPlane.d == plane.d );
    CHECK( session.workspace.pDocument == document );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == 1 );
    // Confirm the displayed BOTH result, including the cut material that
    // produced it, even after an unrelated material default changes.
    settings.Choice( "editor.map.default_material", "materials/dev/clip_after.cymat" );
    QKeyEvent confirm( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier );
    QCoreApplication::sendEvent( top.get(), &confirm );
    CHECK_FALSE( session.workspace.editPreview.bActive );
    REQUIRE( session.workspace.pDocument->geometry.brushes.nCount == 2 );
    CHECK( session.workspace.selection.ids.nCount == 2 );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == 2 );
    int cutFaces = 0;
    for ( usize i = 0; i < session.workspace.pDocument->geometry.brushes.nCount; ++i ) {
        const auto *brush = session.workspace.pDocument->geometry.brushes.pData[i];
        const auto *attributes = geometry::GeometryDocument_FindBrushAttributes( &session.workspace.pDocument->geometry, brush->sourceId );
        REQUIRE( attributes != nullptr );
        for ( usize j = 0; j < brush->sides.nCount; ++j ) {
            const auto &side = brush->sides.pData[j];
            if ( std::abs( side.plane.normal.x ) < 0.99 || std::abs( side.plane.d ) > 1e-6 ) { continue; }
            ++cutFaces;
            const auto material = attributes->records.pData[side.iAttributeIndex].material;
            CHECK( StringView_Equals( MapMaterials_Path( &session.workspace.pDocument->materials, material.value ),
                StringView_FromCString( "materials/dev/clip_preview.cymat" ) ) );
        }
    }
    CHECK( cutFaces == 2 );
    REQUIRE( MapWorkspace_Undo( &session.workspace ) == editor_history_status_t::OK );
    CHECK( session.workspace.pDocument->geometry.brushes.nCount == 1 );
    REQUIRE( MapWorkspace_Redo( &session.workspace ) == editor_history_status_t::OK );
    CHECK( session.workspace.pDocument->geometry.brushes.nCount == 2 );
}

TEST_CASE( "A staged clipping plane is cancelled without geometry edits or stale confirmation", "[map][gui][views][geometry-edit][clip][gesture-context]" )
{
    for ( bool documentChange : { false, true } ) {
        CAPTURE( documentChange );
        session_t session;
        REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, -64 } ); MapBounds_AddPoint( box, { 64, 64, 64 } );
        REQUIRE( MapWorkspace_CreateBox( &session.workspace, box ) );
        MapWorkspace_SetTool( &session.workspace, map_tool_t::CLIP );
        std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &session.workspace, map_ortho_axes_t::TOP ) );
        ShowAt( top.get(), 800, 600 );
        const QPointF start = MapOrthoView_WorldToView( top.get(), { 0, -96 } );
        const QPointF end = MapOrthoView_WorldToView( top.get(), { 0, 96 } );
        DragMouse( top.get(), QEvent::MouseButtonPress, start ); DragMouse( top.get(), QEvent::MouseMove, end );
        DragMouse( top.get(), QEvent::MouseButtonRelease, end );
        REQUIRE( session.workspace.editPreview.bClip );
        if ( documentChange ) { REQUIRE( MapWorkspace_Undo( &session.workspace ) == editor_history_status_t::OK ); }
        else {
            QKeyEvent cancel( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
            QCoreApplication::sendEvent( top.get(), &cancel );
        }
        CHECK_FALSE( session.workspace.editPreview.bActive );
        CHECK( session.workspace.editPreviewWire.points.nCount == 0 );
        const auto token = UndoRedo_StateToken( session.workspace.history.pUndo );
        QKeyEvent confirm( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier );
        QCoreApplication::sendEvent( top.get(), &confirm );
        DragMouse( top.get(), QEvent::MouseButtonRelease, end );
        CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( session.workspace.history.pUndo ) ) );
        CHECK( EditorHistory_StepCount( &session.workspace.history ) == 1 );
        CHECK( session.workspace.pDocument->geometry.brushes.nCount == ( documentChange ? 0u : 1u ) );
        if ( !documentChange ) { CHECK( MapViews_SelectionGeometryBounds( &session.workspace ).box.maximum.x == 64 ); }
    }
}

TEST_CASE( "A released clipping plane survives pane focus changes and accepts shared confirmation", "[map][gui][views][geometry-edit][clip][focus]" )
{
    for ( int scenario = 0; scenario < 3; ++scenario ) {
        CAPTURE( scenario );
        const bool releaseBeforeFocus = scenario != 0;
        const bool confirm = scenario == 1;
        session_t session;
        REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, -64 } ); MapBounds_AddPoint( box, { 64, 64, 64 } );
        REQUIRE( MapWorkspace_CreateBox( &session.workspace, box ) );
        MapWorkspace_SetTool( &session.workspace, map_tool_t::CLIP );
        std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &session.workspace, map_ortho_axes_t::TOP ) );
        std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &session.workspace ) );
        ShowAt( camera.get(), 800, 600 ); ShowAt( top.get(), 800, 600 );
        const QPointF start = MapOrthoView_WorldToView( top.get(), { 0, -96 } );
        const QPointF end = MapOrthoView_WorldToView( top.get(), { 0, 96 } );
        DragMouse( top.get(), QEvent::MouseButtonPress, start ); DragMouse( top.get(), QEvent::MouseMove, end );
        REQUIRE( session.workspace.editPreview.bClip );
        if ( releaseBeforeFocus ) { DragMouse( top.get(), QEvent::MouseButtonRelease, end ); }
        QFocusEvent lost( QEvent::FocusOut, Qt::OtherFocusReason );
        QCoreApplication::sendEvent( top.get(), &lost );
        CHECK( static_cast<bool>( session.workspace.editPreview.bActive ) == releaseBeforeFocus );
        Enter( camera.get() );
        if ( releaseBeforeFocus ) {
            REQUIRE( session.workspace.editPreview.bClip );
            REQUIRE( session.workspace.editPreviewWire.points.nCount != 0 );
        }
        QKeyEvent finish( QEvent::KeyPress, confirm ? Qt::Key_Return : Qt::Key_Escape, Qt::NoModifier );
        QCoreApplication::sendEvent( camera.get(), &finish );
        CHECK_FALSE( session.workspace.editPreview.bActive );
        CHECK( session.workspace.editPreviewWire.points.nCount == 0 );
        CHECK( session.workspace.pDocument->geometry.brushes.nCount == ( confirm ? 2u : 1u ) );
        CHECK( EditorHistory_StepCount( &session.workspace.history ) == ( confirm ? 2u : 1u ) );
        // The originating pane must not retain a committable local drag after
        // a second pane consumes the shared preview.
        const auto token = UndoRedo_StateToken( session.workspace.history.pUndo );
        QKeyEvent stale( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier );
        QCoreApplication::sendEvent( top.get(), &stale );
        DragMouse( top.get(), QEvent::MouseButtonRelease, end );
        CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( session.workspace.history.pUndo ) ) );
        CHECK_FALSE( session.workspace.editPreview.bActive );
    }
}

TEST_CASE( "Camera navigation inspects a released orthographic clip without discarding its shared guide", "[map][gui][views][geometry-edit][clip][navigation-gestures]" )
{
    session_t session; auto &ws = session.workspace;
    view_settings_t settings( &session.gui.settings ); settings.Choice( "editor.map.clip_mode", "back" );
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, 0 } ); MapBounds_AddPoint( box, { 64, 64, 96 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 selected = EditorSelection_At( &ws.selection, 0 );
    MapWorkspace_SetGridSize( &ws, 16 ); MapWorkspace_SetTool( &ws, map_tool_t::CLIP );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ), top( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
    ShowAt( camera.get(), 800, 600 ); ShowAt( top.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
    const QPointF first = MapOrthoView_WorldToView( top.get(), { 16, -96 } ), second = MapOrthoView_WorldToView( top.get(), { 16, 96 } );
    DragMouse( top.get(), QEvent::MouseButtonPress, first ); DragMouse( top.get(), QEvent::MouseMove, second ); DragMouse( top.get(), QEvent::MouseButtonRelease, second );
    REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.clipGuide.bHas ); REQUIRE( ws.editPreview.status == map_status_t::OK );
    const auto guide = ws.editPreview.clipGuide; const auto plane = ws.editPreview.clipPlane;
    const auto *document = ws.pDocument; const auto revision = document->geometry.revision; const usize steps = EditorHistory_StepCount( &ws.history );
    const auto *ghostPoints = ws.editPreviewWire.points.pData;
    const auto unchanged = [&]() {
        REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.clipGuide.bHas ); CHECK( ws.editPreview.status == map_status_t::OK );
        CheckPointClose( ws.editPreview.clipGuide.points[0], guide.points[0] ); CheckPointClose( ws.editPreview.clipGuide.points[1], guide.points[1] );
        CheckPointClose( ws.editPreview.clipPlane.normal, plane.normal ); CHECK( ws.editPreview.clipPlane.d == plane.d );
        CHECK( ws.editPreviewWire.points.pData == ghostPoints ); CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( EditorSelection_At( &ws.selection, 0 ) == selected ); CHECK( ws.tool == map_tool_t::CLIP );
    };
    const struct { Qt::MouseButton button; Qt::KeyboardModifiers modifiers; } gestures[]{
        { Qt::RightButton, Qt::NoModifier }, { Qt::LeftButton, Qt::AltModifier },
        { Qt::MiddleButton, Qt::NoModifier }, { Qt::RightButton, Qt::AltModifier },
    };
    for ( const auto &gesture : gestures ) {
        CAPTURE( static_cast<int>( gesture.button ), static_cast<int>( gesture.modifiers ) );
        const QPointF start( 400, 300 ), end( 414, 309 );
        DragButton( camera.get(), QEvent::MouseButtonPress, start, gesture.button, gesture.modifiers );
        REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.clipGuide.bHas );
        DragButton( camera.get(), QEvent::MouseMove, end, gesture.button, gesture.modifiers );
        DragButton( camera.get(), QEvent::MouseButtonRelease, end, gesture.button, gesture.modifiers ); unchanged();
    }
    Wheel( camera.get() ); unchanged();
    QKeyEvent confirm( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &confirm );
    CHECK_FALSE( ws.editPreview.bActive ); REQUIRE( ws.wire.objects.nCount == 1u ); CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
    const auto clipped = MapViews_SelectionGeometryBounds( &ws );
    CheckPointClose( clipped.box.minimum, box.box.minimum ); CheckPointClose( clipped.box.maximum, { 16, 64, 96 } );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, box.box.minimum ); CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, box.box.maximum );
    CHECK( EditorSelection_At( &ws.selection, 0 ) == selected );
}

TEST_CASE( "Hiding clipping targets cancels an unreleased drag and requires a fresh line", "[map][gui][views][geometry-edit][clip][visibility]" )
{
    session_t session;
    view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    auto &ws = session.workspace;
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, -64 } ); MapBounds_AddPoint( box, { 64, 64, 64 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
    const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
    const u64 selected = EditorSelection_At( &ws.selection, 0 );
    const auto token = UndoRedo_StateToken( ws.history.pUndo );
    settings.Choice( "editor.map.clip_mode", "back" );
    MapWorkspace_SetGridSize( &ws, 16 ); MapWorkspace_SetTool( &ws, map_tool_t::CLIP );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
    ShowAt( top.get(), 800, 600 );
    const QPointF start = MapOrthoView_WorldToView( top.get(), { 0, -96 } );
    const QPointF end = MapOrthoView_WorldToView( top.get(), { 0, 96 } );
    DragMouse( top.get(), QEvent::MouseButtonPress, start ); DragMouse( top.get(), QEvent::MouseMove, end );
    REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.bClip );
    REQUIRE( ws.editPreview.status == map_status_t::OK );
    MapWorkspace_SetVisgroupHidden( &ws, map_visgroup_t::BRUSHES, CY_TRUE );
    CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.editPreviewWire.points.nCount == 0 );
    MapWorkspace_SetVisgroupHidden( &ws, map_visgroup_t::BRUSHES, CY_FALSE );
    DragMouse( top.get(), QEvent::MouseButtonRelease, end );
    QKeyEvent stale( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier );
    QCoreApplication::sendEvent( top.get(), &stale );
    CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.editPreviewWire.points.nCount == 0 );
    CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision );
    CHECK( ws.selection.ids.nCount == 1 ); CHECK( EditorSelection_At( &ws.selection, 0 ) == selected );
    CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
    CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    CHECK( MapViews_SelectionGeometryBounds( &ws ).box.maximum.x == 64 );

    const QPointF freshStart = MapOrthoView_WorldToView( top.get(), { 16, -96 } );
    const QPointF freshEnd = MapOrthoView_WorldToView( top.get(), { 16, 96 } );
    DragMouse( top.get(), QEvent::MouseButtonPress, freshStart ); DragMouse( top.get(), QEvent::MouseMove, freshEnd );
    DragMouse( top.get(), QEvent::MouseButtonRelease, freshEnd );
    REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.status == map_status_t::OK );
    CHECK( std::abs( ws.editPreview.clipPlane.d + 16 ) < 1e-6 );
    QKeyEvent confirm( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier );
    QCoreApplication::sendEvent( top.get(), &confirm );
    CHECK_FALSE( ws.editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws.history ) == 2 );
    CHECK( ws.pDocument->geometry.brushes.nCount == 1 ); CHECK( EditorSelection_At( &ws.selection, 0 ) == selected );
    CHECK( std::abs( MapViews_SelectionGeometryBounds( &ws ).box.maximum.x - 16 ) < 1e-6 );
}

TEST_CASE( "Clipping gestures snap their line unless the held modifier bypasses the grid", "[map][gui][views][geometry-edit][clip][snap]" )
{
    for ( auto modifiers : { Qt::KeyboardModifiers( Qt::NoModifier ), Qt::KeyboardModifiers( Qt::ControlModifier ), Qt::KeyboardModifiers( Qt::MetaModifier ) } ) {
        CAPTURE( static_cast<int>( modifiers ) );
        session_t session;
        REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, -64 } ); MapBounds_AddPoint( box, { 64, 64, 64 } );
        REQUIRE( MapWorkspace_CreateBox( &session.workspace, box ) );
        MapWorkspace_SetGridSize( &session.workspace, 16 );
        MapWorkspace_SetTool( &session.workspace, map_tool_t::CLIP );
        std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &session.workspace, map_ortho_axes_t::TOP ) );
        ShowAt( top.get(), 800, 600 );
        const QPointF start = MapOrthoView_WorldToView( top.get(), { 7, -91 } );
        const QPointF end = MapOrthoView_WorldToView( top.get(), { 7, 87 } );
        DragMouse( top.get(), QEvent::MouseButtonPress, start, modifiers ); DragMouse( top.get(), QEvent::MouseMove, end, modifiers );
        DragMouse( top.get(), QEvent::MouseButtonRelease, end, modifiers );
        REQUIRE( session.workspace.editPreview.bClip );
        CHECK( std::abs( session.workspace.editPreview.clipPlane.d + ( modifiers == Qt::NoModifier ? 0.0 : 7.0 ) ) < 1e-6 );
        CHECK( EditorHistory_StepCount( &session.workspace.history ) == 1 );
        // Starting another line clears the old staged clip. A collapsed line
        // must not let Enter reuse the prior successful plane.
        // Choose empty space away from the new editable endpoint markers.
        const QPointF collapsed = MapOrthoView_WorldToView( top.get(), { -96, -96 } );
        DragMouse( top.get(), QEvent::MouseButtonPress, collapsed );
        DragMouse( top.get(), QEvent::MouseButtonRelease, collapsed );
        CHECK( session.workspace.editPreviewWire.points.nCount == 0 );
        QKeyEvent confirm( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier );
        QCoreApplication::sendEvent( top.get(), &confirm );
        CHECK( session.workspace.pDocument->geometry.brushes.nCount == 1 );
        CHECK( EditorHistory_StepCount( &session.workspace.history ) == 1 );
        CHECK( MapViews_SelectionGeometryBounds( &session.workspace ).box.maximum.x == 64 );
    }
}

TEST_CASE( "Clipping endpoints edit across compatible orthographic panes without changing the extrusion coordinate", "[map][gui][views][geometry-edit][clip][endpoints]" )
{
    for ( auto modifiers : { Qt::KeyboardModifiers( Qt::NoModifier ), Qt::KeyboardModifiers( Qt::ControlModifier ), Qt::KeyboardModifiers( Qt::MetaModifier ) } ) {
        CAPTURE( static_cast<int>( modifiers ) );
        session_t session; auto &ws = session.workspace;
        view_settings_t settings( &session.gui.settings );
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, -16 } ); MapBounds_AddPoint( box, { 64, 64, 112 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
        const auto *document = ws.pDocument; const auto token = UndoRedo_StateToken( ws.history.pUndo );
        settings.Choice( "editor.map.clip_mode", "back" ); MapWorkspace_SetGridSize( &ws, 16 );
        MapWorkspace_SetTool( &ws, map_tool_t::CLIP );
        std::unique_ptr<QWidget> origin( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
        std::unique_ptr<QWidget> editor( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
        ShowAt( origin.get(), 800, 600 );
        const QPointF start = MapOrthoView_WorldToView( origin.get(), { 16, -96 } );
        const QPointF end = MapOrthoView_WorldToView( origin.get(), { 16, 96 } );
        DragMouse( origin.get(), QEvent::MouseButtonPress, start ); DragMouse( origin.get(), QEvent::MouseMove, end );
        DragMouse( origin.get(), QEvent::MouseButtonRelease, end );
        REQUIRE( ws.editPreview.clipGuide.bHas ); CHECK( ws.editPreview.clipGuide.extrusionAxis == 2 );
        CheckPointClose( ws.editPreview.clipGuide.points[0], { 16, -96, 48 } );
        CheckPointClose( ws.editPreview.clipGuide.points[1], { 16, 96, 48 } );
        const auto originalPlane = ws.editPreview.clipPlane;
        const auto fixed = ws.editPreview.clipGuide.points[1];
        ShowAt( editor.get(), 800, 600 );
        const QPointF marker = MapOrthoView_WorldToView( editor.get(), { 16, -96 } );
        const QPointF destination = MapOrthoView_WorldToView( editor.get(), { 29, -73 } );
        // Start slightly off the marker to exercise hit tolerance. The anchor
        // must not jump to the mouse position before an actual movement.
        DragMouse( editor.get(), QEvent::MouseButtonPress, marker + QPointF( 4, 0 ), modifiers );
        REQUIRE( ws.editPreview.clipGuide.bHas );
        CheckPointClose( ws.editPreview.clipGuide.points[0], { 16, -96, 48 } );
        DragMouse( editor.get(), QEvent::MouseButtonRelease, marker + QPointF( 4, 0 ), modifiers );
        CheckPointClose( ws.editPreview.clipGuide.points[0], { 16, -96, 48 } );
        DragMouse( editor.get(), QEvent::MouseButtonPress, marker, modifiers );
        DragMouse( editor.get(), QEvent::MouseMove, destination, modifiers );
        REQUIRE( ws.editPreview.status == map_status_t::OK );
        DragMouse( editor.get(), QEvent::MouseMove, marker, modifiers );
        DragMouse( editor.get(), QEvent::MouseButtonRelease, marker, modifiers );
        CheckPointClose( ws.editPreview.clipGuide.points[0], { 16, -96, 48 } );
        CheckPointClose( ws.editPreview.clipGuide.points[1], fixed );
        CheckPointClose( ws.editPreview.clipPlane.normal, originalPlane.normal );
        CHECK( std::abs( ws.editPreview.clipPlane.d - originalPlane.d ) < 1e-6 );
        CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
        DragMouse( editor.get(), QEvent::MouseButtonPress, marker, modifiers );
        DragMouse( editor.get(), QEvent::MouseMove, destination, modifiers );
        DragMouse( editor.get(), QEvent::MouseButtonRelease, destination, modifiers );
        REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.status == map_status_t::OK );
        const bool bypass = modifiers != Qt::NoModifier;
        const math::vec3d_t expected{ bypass ? 29.0 : 32.0, bypass ? -73.0 : -80.0, 48 };
        CheckPointClose( ws.editPreview.clipGuide.points[0], expected );
        CheckPointClose( ws.editPreview.clipGuide.points[1], fixed );
        CHECK( ws.editPreview.clipGuide.extrusionAxis == 2 );
        const QPointF hover = destination + QPointF( 60, 45 );
        QMouseEvent move( QEvent::MouseMove, hover, editor->mapToGlobal( hover ), Qt::NoButton, Qt::NoButton, Qt::NoModifier );
        QCoreApplication::sendEvent( editor.get(), &move );
        CheckPointClose( ws.editPreview.clipGuide.points[0], expected );
        CheckPointClose( ws.editPreview.clipGuide.points[1], fixed );
        CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
        CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
        QKeyEvent cancel( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
        QCoreApplication::sendEvent( origin.get(), &cancel );
        CHECK_FALSE( ws.editPreview.bActive ); CHECK_FALSE( ws.editPreview.clipGuide.bHas );
        CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
    }
}

TEST_CASE( "Camera clipping placement intersects the selected construction plane for every extrusion axis", "[map][gui][views][geometry-edit][clip][3d-placement]" )
{
    for ( u32 axis = 0; axis < 3; ++axis ) {
        CAPTURE( axis );
        session_t session; auto &ws = session.workspace;
        view_settings_t settings( &session.gui.settings );
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -40, -72, -16 } ); MapBounds_AddPoint( box, { 88, 56, 112 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
        const auto *document = ws.pDocument; const u64 selected = EditorSelection_At( &ws.selection, 0 );
        settings.Choice( "editor.map.clip_mode", "back" );
        settings.Choice( "editor.map.clip_axis", axis == 0 ? "x" : axis == 1 ? "y" : "z" );
        CHECK( MapWorkspace_ClipAxis( &ws ) == axis );
        MapWorkspace_SetGridSize( &ws, 16 ); MapWorkspace_SetTool( &ws, map_tool_t::CLIP );
        std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
        MapWorkspace_Frame( &ws, CY_TRUE, map_frame_target_t::PERSPECTIVE ); QCoreApplication::processEvents();
        const math::vec3d_t center = MapBounds_Center( box );
        const u32 u = axis == 0 ? 1 : 0, v = axis == 2 ? 1 : 2;
        auto a = center, b = center;
        SetTestCoordinate( a, u, 16 ); SetTestCoordinate( b, u, 16 );
        SetTestCoordinate( a, v, v == 1 ? -48 : 0 );
        SetTestCoordinate( b, v, v == 1 ? 48 : 96 );
        QPointF start, end;
        REQUIRE( MapCameraView_WorldToView( camera.get(), a, &start ) );
        REQUIRE( MapCameraView_WorldToView( camera.get(), b, &end ) );
        REQUIRE( camera->rect().adjusted( 10, 10, -10, -10 ).contains( start.toPoint() ) );
        REQUIRE( camera->rect().adjusted( 10, 10, -10, -10 ).contains( end.toPoint() ) );
        DragMouse( camera.get(), QEvent::MouseButtonPress, start ); DragMouse( camera.get(), QEvent::MouseMove, end );
        DragMouse( camera.get(), QEvent::MouseButtonRelease, end );
        REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.status == map_status_t::OK );
        REQUIRE( ws.editPreview.clipGuide.bHas ); CHECK( ws.editPreview.clipGuide.extrusionAxis == axis );
        auto expectedA = a, expectedB = b;
        for ( u32 visible : { u, v } ) {
            SetTestCoordinate( expectedA, visible, std::round( TestCoordinate( a, visible ) / 16 ) * 16 );
            SetTestCoordinate( expectedB, visible, std::round( TestCoordinate( b, visible ) / 16 ) * 16 );
        }
        CheckPointClose( ws.editPreview.clipGuide.points[0], expectedA );
        CheckPointClose( ws.editPreview.clipGuide.points[1], expectedB );
        const auto plane = ws.editPreview.clipPlane;
        CHECK( std::abs( TestCoordinate( plane.normal, axis ) ) < 1e-6 );
        CHECK( std::abs( plane.normal.x * plane.normal.x + plane.normal.y * plane.normal.y + plane.normal.z * plane.normal.z - 1 ) < 1e-6 );
        const f64 retainedU = 16;
        CHECK( std::abs( TestCoordinate( ws.editPreview.bounds.box.maximum, u ) - retainedU ) < 1e-6 );
        CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
        QKeyEvent confirm( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &confirm );
        CHECK_FALSE( ws.editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws.history ) == 2 );
        CHECK( EditorSelection_At( &ws.selection, 0 ) == selected );
        CHECK( std::abs( TestCoordinate( MapViews_SelectionGeometryBounds( &ws ).box.maximum, u ) - retainedU ) < 1e-6 );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
        CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, box.box.minimum );
        CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, box.box.maximum );
        REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
        CHECK( std::abs( TestCoordinate( MapViews_SelectionGeometryBounds( &ws ).box.maximum, u ) - retainedU ) < 1e-6 );
    }
}

TEST_CASE( "A camera pane can edit a staged orthographic clipping endpoint before shared confirmation", "[map][gui][views][geometry-edit][clip][endpoints][3d-placement]" )
{
    session_t session; auto &ws = session.workspace;
    view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, -16 } ); MapBounds_AddPoint( box, { 64, 64, 112 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
    const auto *document = ws.pDocument;
    settings.Choice( "editor.map.clip_mode", "back" ); MapWorkspace_SetGridSize( &ws, 16 ); MapWorkspace_SetTool( &ws, map_tool_t::CLIP );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) );
    ShowAt( camera.get(), 800, 600 ); ShowAt( top.get(), 800, 600 );
    MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
    const QPointF start = MapOrthoView_WorldToView( top.get(), { 16, -48 } );
    const QPointF end = MapOrthoView_WorldToView( top.get(), { 16, 48 } );
    DragMouse( top.get(), QEvent::MouseButtonPress, start ); DragMouse( top.get(), QEvent::MouseMove, end );
    DragMouse( top.get(), QEvent::MouseButtonRelease, end );
    REQUIRE( ws.editPreview.clipGuide.bHas );
    const auto fixed = ws.editPreview.clipGuide.points[1];
    QPointF marker, destination;
    REQUIRE( MapCameraView_WorldToView( camera.get(), ws.editPreview.clipGuide.points[0], &marker ) );
    REQUIRE( MapCameraView_WorldToView( camera.get(), { 32, -32, 48 }, &destination ) );
    Enter( camera.get() );
    DragMouse( camera.get(), QEvent::MouseButtonPress, marker );
    CheckPointClose( ws.editPreview.clipGuide.points[0], { 16, -48, 48 } );
    DragMouse( camera.get(), QEvent::MouseMove, destination ); DragMouse( camera.get(), QEvent::MouseButtonRelease, destination );
    REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.status == map_status_t::OK );
    CheckPointClose( ws.editPreview.clipGuide.points[0], { 32, -32, 48 } ); CheckPointClose( ws.editPreview.clipGuide.points[1], fixed );
    CHECK( ws.editPreview.clipGuide.extrusionAxis == 2 );
    const auto retained = ws.editPreview.bounds;
    const auto plane = ws.editPreview.clipPlane;
    CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
    QKeyEvent confirm( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier ); QCoreApplication::sendEvent( top.get(), &confirm );
    CHECK_FALSE( ws.editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws.history ) == 2 );
    const auto committed = MapViews_SelectionGeometryBounds( &ws );
    CheckPointClose( committed.box.minimum, retained.box.minimum ); CheckPointClose( committed.box.maximum, retained.box.maximum );
    bool cutFace = false;
    const auto &sides = ws.pDocument->geometry.brushes.pData[0]->sides;
    for ( usize i = 0; i < sides.nCount; ++i ) {
        const auto &side = sides.pData[i];
        if ( std::abs( side.plane.normal.x - plane.normal.x ) < 1e-6 && std::abs( side.plane.normal.y - plane.normal.y ) < 1e-6 &&
             std::abs( side.plane.normal.z - plane.normal.z ) < 1e-6 && std::abs( side.plane.d - plane.d ) < 1e-6 ) { cutFace = true; }
    }
    CHECK( cutFace );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, box.box.maximum );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, retained.box.minimum );
}

TEST_CASE( "Camera clipping rejects parallel construction rays and collapsed lines without an edit", "[map][gui][views][geometry-edit][clip][3d-placement][invalid]" )
{
    session_t session; auto &ws = session.workspace;
    view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, -64 } ); MapBounds_AddPoint( box, { 64, 64, 64 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
    const auto *document = ws.pDocument; const auto token = UndoRedo_StateToken( ws.history.pUndo );
    settings.Choice( "editor.map.clip_axis", "z" );
    settings.Real( "editor.camera.look_sensitivity", 1.0 ); settings.Set( "editor.camera.invert_y", false );
    MapWorkspace_SetTool( &ws, map_tool_t::CLIP );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
    MapWorkspace_Frame( &ws, CY_TRUE, map_frame_target_t::PERSPECTIVE ); QCoreApplication::processEvents();
    FacePositiveX( camera.get() );
    const QPointF center( camera->width() * 0.5, camera->height() * 0.5 );
    // The center ray has zero Z component. Moving the second point cannot
    // repair an unprojectable first point or introduce a raw-ray fallback.
    DragMouse( camera.get(), QEvent::MouseButtonPress, center );
    DragMouse( camera.get(), QEvent::MouseMove, center + QPointF( 60, 50 ) );
    DragMouse( camera.get(), QEvent::MouseButtonRelease, center + QPointF( 60, 50 ) );
    CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.editPreviewWire.points.nCount == 0 );
    QKeyEvent stale( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &stale );
    CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
    CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    settings.Choice( "editor.map.clip_axis", "x" );
    // This workplane is projectable, but a press/release at one point still
    // cannot define a cut or accidentally use an earlier plane.
    DragMouse( camera.get(), QEvent::MouseButtonPress, center ); DragMouse( camera.get(), QEvent::MouseButtonRelease, center );
    CHECK( ws.editPreviewWire.points.nCount == 0 );
    CHECK_FALSE( MapWorkspace_CommitClipPreview( &ws ) );
    QKeyEvent confirm( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &confirm );
    CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
    CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    QKeyEvent cancel( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &cancel );
    CHECK_FALSE( ws.editPreview.bActive );
    settings.Choice( "editor.map.clip_axis", "z" );
    settings.Choice( "editor.map.clip_mode", "both" );
    const QPointF validStart = center + QPointF( 0, 100 ), validEnd = center + QPointF( 60, 130 );
    DragMouse( camera.get(), QEvent::MouseButtonPress, validStart ); DragMouse( camera.get(), QEvent::MouseMove, validEnd );
    REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.status == map_status_t::OK );
    REQUIRE( ws.editPreviewWire.points.nCount != 0 );
    DragMouse( camera.get(), QEvent::MouseMove, center );
    CHECK( ws.editPreview.status == map_status_t::INVALID_ARGUMENT ); CHECK( ws.editPreviewWire.points.nCount == 0 );
    DragMouse( camera.get(), QEvent::MouseMove, validEnd );
    REQUIRE( ws.editPreview.status == map_status_t::OK ); REQUIRE( ws.editPreviewWire.points.nCount != 0 );
    const QPointF behind = center - QPointF( 0, 100 );
    DragMouse( camera.get(), QEvent::MouseMove, behind );
    CHECK( ws.editPreview.status == map_status_t::INVALID_ARGUMENT ); CHECK( ws.editPreviewWire.points.nCount == 0 );
    DragMouse( camera.get(), QEvent::MouseButtonRelease, behind );
    CHECK_FALSE( MapWorkspace_CommitClipPreview( &ws ) );
    CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
    CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    QKeyEvent clear( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &clear );
    CHECK_FALSE( ws.editPreview.bActive );
}

TEST_CASE( "Changing the 3D clipping axis cancels fresh active and released viewport lines", "[map][gui][views][geometry-edit][clip][gesture-context]" )
{
    for ( int stage = 0; stage < 3; ++stage ) {
        CAPTURE( stage );
        session_t session; auto &ws = session.workspace;
        view_settings_t settings( &session.gui.settings );
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, -64 } ); MapBounds_AddPoint( box, { 64, 64, 64 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
        const auto *document = ws.pDocument; const auto token = UndoRedo_StateToken( ws.history.pUndo );
        settings.Choice( "editor.map.clip_axis", "z" ); MapWorkspace_SetTool( &ws, map_tool_t::CLIP );
        std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) ); ShowAt( top.get(), 800, 600 );
        const QPointF start = MapOrthoView_WorldToView( top.get(), { 0, -96 } );
        const QPointF end = MapOrthoView_WorldToView( top.get(), { 0, 96 } );
        DragMouse( top.get(), QEvent::MouseButtonPress, start );
        if ( stage != 0 ) { DragMouse( top.get(), QEvent::MouseMove, end ); REQUIRE( ws.editPreview.clipGuide.bHas ); }
        if ( stage == 2 ) { DragMouse( top.get(), QEvent::MouseButtonRelease, end ); }
        settings.Choice( "editor.map.clip_axis", "x" );
        CHECK_FALSE( ws.editPreview.bActive ); CHECK_FALSE( ws.editPreview.clipGuide.bHas ); CHECK( ws.editPreviewWire.points.nCount == 0 );
        DragMouse( top.get(), QEvent::MouseButtonRelease, end );
        QKeyEvent confirm( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier ); QCoreApplication::sendEvent( top.get(), &confirm );
        CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
        CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    }
}

TEST_CASE( "Clipping mode changes keep world anchors and a different numeric plane removes them", "[map][gui][views][geometry-edit][clip][endpoints][settings]" )
{
    session_t session; auto &ws = session.workspace;
    view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, -16 } ); MapBounds_AddPoint( box, { 64, 64, 112 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
    settings.Choice( "editor.map.clip_mode", "back" ); MapWorkspace_SetTool( &ws, map_tool_t::CLIP );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) ); ShowAt( top.get(), 800, 600 );
    const QPointF start = MapOrthoView_WorldToView( top.get(), { 16, -96 } );
    const QPointF end = MapOrthoView_WorldToView( top.get(), { 16, 96 } );
    DragMouse( top.get(), QEvent::MouseButtonPress, start ); DragMouse( top.get(), QEvent::MouseMove, end );
    DragMouse( top.get(), QEvent::MouseButtonRelease, end );
    REQUIRE( ws.editPreview.clipGuide.bHas );
    const auto guide = ws.editPreview.clipGuide;
    settings.Choice( "editor.map.clip_mode", "front" );
    REQUIRE( ws.editPreview.bActive ); CHECK( ws.editPreview.clipMode == map_brush_clip_mode_t::FRONT );
    REQUIRE( ws.editPreview.clipGuide.bHas );
    CheckPointClose( ws.editPreview.clipGuide.points[0], guide.points[0] ); CheckPointClose( ws.editPreview.clipGuide.points[1], guide.points[1] );
    CHECK( ws.editPreview.clipGuide.extrusionAxis == guide.extrusionAxis );
    MapWorkspace_SetClipPreview( &ws, { { 1, 0, 0 }, -32 } );
    REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.status == map_status_t::OK );
    CHECK_FALSE( ws.editPreview.clipGuide.bHas ); CHECK( ws.editPreview.clipPlane.d == -32 );
    CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
    QKeyEvent cancel( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( top.get(), &cancel );
    CHECK_FALSE( ws.editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
}

TEST_CASE( "Off-center camera clipping rays cannot place a guide behind the forward near plane", "[map][gui][views][geometry-edit][clip][3d-placement][near-plane]" )
{
    session_t session; auto &ws = session.workspace;
    view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    settings.Choice( "editor.map.clip_axis", "x" );
    settings.Real( "editor.camera.look_sensitivity", 1.0 ); settings.Set( "editor.camera.invert_y", false );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
    FacePositiveX( camera.get() );
    const auto position = MapCameraView_Position( camera.get() );
    const math::vec3d_t center{ position.x + 0.8, position.y, position.z };
    map_bounds_t box{};
    MapBounds_AddPoint( box, { center.x - 32, center.y - 64, center.z - 64 } );
    MapBounds_AddPoint( box, { center.x + 32, center.y + 64, center.z + 64 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
    const auto *document = ws.pDocument; const auto token = UndoRedo_StateToken( ws.history.pUndo );
    CheckPointClose( MapCameraView_Position( camera.get() ), position );
    QPointF hidden;
    REQUIRE_FALSE( MapCameraView_WorldToView( camera.get(), center, &hidden ) );
    MapWorkspace_SetTool( &ws, map_tool_t::CLIP );
    const QPointF start( 780, 570 ), end( 780, 30 );
    const f64 fov = EditorSettings_Real( &session.gui.settings, "editor.camera.fov", 75.0 );
    const f64 focal = camera->width() * 0.5 / std::tan( fov * 3.14159265358979323846 / 360.0 );
    const f64 horizontal = ( start.x() - camera->width() * 0.5 ) / focal;
    const f64 vertical = ( camera->height() * 0.5 - start.y() ) / focal;
    // The old radial-distance check accepted this ray even though its
    // forward depth is only 0.8. Bypass snapping so rejection cannot be
    // explained by the small world-space line collapsing onto a grid cell.
    REQUIRE( 0.8 * std::sqrt( 1 + horizontal * horizontal + vertical * vertical ) > 1.0 );
    DragMouse( camera.get(), QEvent::MouseButtonPress, start, Qt::ControlModifier );
    DragMouse( camera.get(), QEvent::MouseMove, end, Qt::ControlModifier );
    DragMouse( camera.get(), QEvent::MouseButtonRelease, end, Qt::ControlModifier );
    CHECK_FALSE( ws.editPreview.bActive ); CHECK_FALSE( ws.editPreview.clipGuide.bHas );
    CHECK( ws.editPreviewWire.points.nCount == 0 ); CHECK_FALSE( MapWorkspace_CommitClipPreview( &ws ) );
    QKeyEvent confirm( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &confirm );
    CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.brushes.nCount == 1 );
    CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
    CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
}

TEST_CASE( "Returning a camera clipping endpoint to its original marker repairs an invalid ray preview", "[map][gui][views][geometry-edit][clip][endpoints][3d-placement][invalid]" )
{
    session_t session; auto &ws = session.workspace;
    view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    settings.Real( "editor.camera.look_sensitivity", 1.0 ); settings.Set( "editor.camera.invert_y", false );
    settings.Choice( "editor.map.clip_axis", "z" ); settings.Choice( "editor.map.clip_mode", "back" );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
    FacePositiveX( camera.get() );
    const auto position = MapCameraView_Position( camera.get() );
    map_bounds_t box{};
    MapBounds_AddPoint( box, { position.x + 32, position.y - 64, position.z - 64 } );
    MapBounds_AddPoint( box, { position.x + 256, position.y + 64, position.z } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
    MapWorkspace_SetGridSize( &ws, 16 ); MapWorkspace_SetTool( &ws, map_tool_t::CLIP );
    const auto *document = ws.pDocument; const auto token = UndoRedo_StateToken( ws.history.pUndo );
    map_clip_guide_t guide{}; guide.bHas = CY_TRUE; guide.extrusionAxis = 2;
    guide.points[0] = { position.x + 128, position.y - 32, position.z - 32 };
    guide.points[1] = { position.x + 128, position.y + 32, position.z - 32 };
    MapWorkspace_SetClipGuide( &ws, guide );
    REQUIRE( ws.editPreview.status == map_status_t::OK ); REQUIRE( ws.editPreviewWire.points.nCount != 0 );
    const auto originalPlane = ws.editPreview.clipPlane;
    QPointF marker, destination;
    REQUIRE( MapCameraView_WorldToView( camera.get(), guide.points[0], &marker ) );
    REQUIRE( MapCameraView_WorldToView( camera.get(), { position.x + 160, position.y - 16, position.z - 32 }, &destination ) );
    REQUIRE( camera->rect().contains( marker.toPoint() ) );
    DragMouse( camera.get(), QEvent::MouseButtonPress, marker, Qt::ControlModifier );
    DragMouse( camera.get(), QEvent::MouseMove, destination, Qt::ControlModifier );
    REQUIRE( ws.editPreview.status == map_status_t::OK );
    CHECK( std::abs( ws.editPreview.clipGuide.points[0].x - guide.points[0].x ) > 16 );
    const QPointF parallel( camera->width() * 0.5, camera->height() * 0.5 );
    DragMouse( camera.get(), QEvent::MouseMove, parallel, Qt::ControlModifier );
    CHECK( ws.editPreview.status == map_status_t::INVALID_ARGUMENT ); CHECK( ws.editPreviewWire.points.nCount == 0 );
    DragMouse( camera.get(), QEvent::MouseMove, marker, Qt::ControlModifier );
    DragMouse( camera.get(), QEvent::MouseButtonRelease, marker, Qt::ControlModifier );
    REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.status == map_status_t::OK );
    REQUIRE( ws.editPreview.clipGuide.bHas ); REQUIRE( ws.editPreviewWire.points.nCount != 0 );
    CheckPointClose( ws.editPreview.clipGuide.points[0], guide.points[0] ); CheckPointClose( ws.editPreview.clipGuide.points[1], guide.points[1] );
    CheckPointClose( ws.editPreview.clipPlane.normal, originalPlane.normal );
    CHECK( std::abs( ws.editPreview.clipPlane.d - originalPlane.d ) < 1e-6 );
    CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
    CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    QKeyEvent cancel( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &cancel );
    CHECK_FALSE( ws.editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
}

TEST_CASE( "Geometry gizmo view capture", "[.geometry-screenshot]" )
{
    session_t session; REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -128, -128, 0 } ); MapBounds_AddPoint( box, { 128, 128, 128 } );
    REQUIRE( MapWorkspace_CreateBox( &session.workspace, box ) );
    std::unique_ptr<QWidget> views( MapViews_Create( nullptr, &session.workspace ) ); ShowAt( views.get(), 1500, 950 );
    MapViews_SetArrangement( views.get(), map_view_arrangement_t::HAMMER );
    MapViews_SetPaneView( views.get(), 0, map_view_type_t::CAMERA, map_render_mode_t::SHADED );
    MapWorkspace_Frame( &session.workspace, CY_TRUE );
    MapWorkspace_SetTool( &session.workspace, map_tool_t::TRANSLATE );
    QCoreApplication::processEvents();
    REQUIRE( views->grab().save( QStringLiteral( "artifacts/mason_geometry_views.png" ) ) );
    const auto *brush = session.workspace.pDocument->geometry.brushes.pData[0];
    MapWorkspace_SelectBrushFace( &session.workspace, brush->sourceId.value, brush->sides.pData[0].sourceId.value );
    MapWorkspace_SetTool( &session.workspace, map_tool_t::EXTRUDE ); QCoreApplication::processEvents();
    REQUIRE( views->grab().save( QStringLiteral( "artifacts/mason_geometry_face.png" ) ) );
}

TEST_CASE( "Camera flight honors remapped held actions and releases changed-modifier chords", "[map][gui][views][input]" )
{
    session_t session;
    settings_document_t keys{};
    REQUIRE( SettingsDocument_Init( &keys, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
    const char *text = R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "view_navigation" held = { "map.viewport.3d" = {
    "map.camera.forward" = [ "X", "Ctrl+Y" ]
    "map.camera.fast" = [ "F" ]
    "map.camera.slow" = [ "V" ]
} } })cykv";
    REQUIRE( SettingsDocument_Load( &keys, StringView_FromCString( text ) ).status == settings_document_status_t::OK );
    session.gui.keymapChain[0] = SettingsDocument_Root( &keys ); session.gui.nKeymapChain = 1;
    MapWorkspace_SetTool( &session.workspace, map_tool_t::CAMERA );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &session.workspace ) ); ShowAt( camera.get(), 800, 600 );
    const auto magnitude = [&]() {
        const auto v = MapCameraView_NavigationVelocity( camera.get() ); return std::sqrt( v.x*v.x + v.y*v.y + v.z*v.z );
    };
    const auto key = [&]( int code, bool down, Qt::KeyboardModifiers modifiers = Qt::NoModifier ) {
        QKeyEvent event( down ? QEvent::KeyPress : QEvent::KeyRelease, code, modifiers ); QCoreApplication::sendEvent( camera.get(), &event );
    };
    key( Qt::Key_W, true ); CHECK( magnitude() == 0 ); key( Qt::Key_W, false );
    key( Qt::Key_X, true ); CHECK( std::abs( magnitude() - 1000 ) < 1e-6 );
    key( Qt::Key_F, true ); CHECK( std::abs( magnitude() - 4000 ) < 1e-6 );
    key( Qt::Key_V, true ); CHECK( std::abs( magnitude() - 250 ) < 1e-6 );
    key( Qt::Key_V, false ); key( Qt::Key_F, false );
    key( Qt::Key_X, false, Qt::ShiftModifier ); CHECK( magnitude() == 0 );
    key( Qt::Key_Y, true, Qt::ControlModifier );
    const auto explicitChord = MapCameraView_NavigationVelocity( camera.get(), Qt::ControlModifier );
    CHECK( std::sqrt( explicitChord.x*explicitChord.x + explicitChord.y*explicitChord.y + explicitChord.z*explicitChord.z ) > 999 );
    key( Qt::Key_Y, false ); CHECK( magnitude() == 0 );
    session.gui.nKeymapChain = 0;
}

TEST_CASE( "Camera look accepts a previously held direction across ordinary right drags", "[map][gui][views][camera][input][held-order]" )
{
    session_t session; auto &ws = session.workspace;
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
    const auto *document = ws.pDocument; const auto revision = document->geometry.revision, selectionRevision = ws.selection.revision;
    const usize steps = EditorHistory_StepCount( &ws.history );
    const auto key = [&]( QEvent::Type type, Qt::KeyboardModifiers modifiers = Qt::NoModifier, bool repeat = false ) {
        QKeyEvent event( type, Qt::Key_W, modifiers, QString(), repeat ); QCoreApplication::sendEvent( camera.get(), &event );
    };
    const auto speed = [&]() { return TestDistance( MapCameraView_NavigationVelocity( camera.get() ), {} ); };
    const QPointF point( 300, 220 );
    key( QEvent::KeyPress ); CHECK( speed() == 0.0 ); // Idle authoring never flies.
    for ( int gesture = 0; gesture < 2; ++gesture ) {
        CAPTURE( gesture );
        DragButton( camera.get(), QEvent::MouseButtonPress, point, Qt::RightButton );
        CHECK( std::abs( speed() - 1000.0 ) < 1e-6 );
        // Qt may send a release/press pair for auto-repeat. Neither releases
        // nor recreates the physical press which this pane already observed.
        key( QEvent::KeyRelease, Qt::NoModifier, true ); CHECK( speed() > 999.0 );
        key( QEvent::KeyPress, Qt::NoModifier, true ); CHECK( speed() > 999.0 );
        DragButton( camera.get(), QEvent::MouseMove, point + QPointF( 8, 0 ), Qt::RightButton );
        DragButton( camera.get(), QEvent::MouseButtonRelease, point + QPointF( 8, 0 ), Qt::RightButton );
        CHECK( speed() == 0.0 );
    }
    key( QEvent::KeyRelease, Qt::ShiftModifier );
    DragButton( camera.get(), QEvent::MouseButtonPress, point, Qt::RightButton ); CHECK( speed() == 0.0 );
    DragButton( camera.get(), QEvent::MouseButtonRelease, point, Qt::RightButton );
    CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision ); CHECK( ws.selection.revision == selectionRevision );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( ws.tool == map_tool_t::SELECT );
}

TEST_CASE( "Held navigation starts in the current context while idle authoring commands keep precedence", "[map][gui][views][camera][input][held-order][keymap]" )
{
    settings_document_t keys{};
    REQUIRE( SettingsDocument_Init( &keys, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &keys, StringView_FromCString( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "held_authoring_precedence"
  bindings = { "map.viewport" = { "map.tool.scale" = [ "E" ] } }
  held = { "map.viewport.3d" = { "map.camera.up" = [ "E" ] "map.camera.forward" = [ "X" ] } }
  mouse = { "map.viewport.3d" = { "map.camera.look" = [ "RightDrag" ] } }
})cykv" ) ).status == settings_document_status_t::OK );
    session_t session; auto &ws = session.workspace;
    REQUIRE( MapWorkspace_RegisterCommands( &ws, &session.gui.commands ) == command_registry_status_t::OK );
    session.gui.keymapChain[0] = SettingsDocument_Root( &keys ); session.gui.nKeymapChain = 1;
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
    const auto key = [&]( int code, bool down ) {
        QKeyEvent event( down ? QEvent::KeyPress : QEvent::KeyRelease, code, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &event );
    };
    key( Qt::Key_E, true ); CHECK( ws.tool == map_tool_t::SCALE );
    CheckPointClose( MapCameraView_NavigationVelocity( camera.get() ), {} );
    const QPointF point( 300, 220 ); DragButton( camera.get(), QEvent::MouseButtonPress, point, Qt::RightButton );
    CheckPointClose( MapCameraView_NavigationVelocity( camera.get() ), { 0, 0, 1000 } );
    CHECK( ws.tool == map_tool_t::SCALE ); key( Qt::Key_E, false );
    key( Qt::Key_W, true ); CheckPointClose( MapCameraView_NavigationVelocity( camera.get() ), {} ); key( Qt::Key_W, false );
    key( Qt::Key_X, true ); CHECK( TestDistance( MapCameraView_NavigationVelocity( camera.get() ), {} ) > 999.0 ); key( Qt::Key_X, false );
    DragButton( camera.get(), QEvent::MouseButtonRelease, point, Qt::RightButton );
    key( Qt::Key_X, true ); CheckPointClose( MapCameraView_NavigationVelocity( camera.get() ), {} );
    DragButton( camera.get(), QEvent::MouseButtonPress, point, Qt::RightButton );
    CHECK( TestDistance( MapCameraView_NavigationVelocity( camera.get() ), {} ) > 999.0 ); key( Qt::Key_X, false );
}

TEST_CASE( "Live held navigation unbinding and remapping cannot retain an old velocity", "[map][gui][views][camera][input][held-order][keymap]" )
{
    settings_document_t initial{}, unbound{}, remapped{};
    for ( auto *keys : { &initial, &unbound, &remapped } ) {
        REQUIRE( SettingsDocument_Init( keys, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
    }
    REQUIRE( SettingsDocument_Load( &initial, StringView_FromCString( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "initial_held" held = { "map.viewport.3d" = { "map.camera.forward" = [ "X" ] } }
  mouse = { "map.viewport.3d" = { "map.camera.look" = [ "RightDrag" ] } } })cykv" ) ).status == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &unbound, StringView_FromCString( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "unbound_held" held = { "map.viewport.3d" = { "map.camera.forward" = [] } } })cykv" ) ).status == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &remapped, StringView_FromCString( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "remapped_held" held = { "map.viewport.3d" = { "map.camera.forward" = [] "map.camera.back" = [ "X" ] } } })cykv" ) ).status == settings_document_status_t::OK );
    session_t session;
    session.gui.keymapChain[0] = SettingsDocument_Root( &initial ); session.gui.nKeymapChain = 1;
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &session.workspace ) ); ShowAt( camera.get(), 800, 600 );
    QKeyEvent down( QEvent::KeyPress, Qt::Key_X, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &down );
    const QPointF point( 300, 220 ); DragButton( camera.get(), QEvent::MouseButtonPress, point, Qt::RightButton );
    const auto forward = MapCameraView_Forward( camera.get() );
    CheckPointClose( MapCameraView_NavigationVelocity( camera.get() ), math::Vec3d_Scale( forward, 1000 ) );
    session.gui.keymapChain[1] = SettingsDocument_Root( &initial ); session.gui.nKeymapChain = 2;
    session.gui.keymapChain[0] = SettingsDocument_Root( &unbound );
    CheckPointClose( MapCameraView_NavigationVelocity( camera.get() ), {} );
    session.gui.keymapChain[0] = SettingsDocument_Root( &remapped );
    CheckPointClose( MapCameraView_NavigationVelocity( camera.get() ), math::Vec3d_Scale( forward, -1000 ) );
    QKeyEvent release( QEvent::KeyRelease, Qt::Key_X, Qt::AltModifier ); QCoreApplication::sendEvent( camera.get(), &release );
    CheckPointClose( MapCameraView_NavigationVelocity( camera.get() ), {} );
    session.gui.keymapChain[0] = SettingsDocument_Root( &initial ); session.gui.nKeymapChain = 1;
    CheckPointClose( MapCameraView_NavigationVelocity( camera.get() ), {} );
}

TEST_CASE( "Camera navigation resets require a fresh physical press rather than auto-repeat", "[map][gui][views][camera][input][held-order][focus]" )
{
    for ( const char *reset : { "focus", "cancel", "document", "projection" } ) {
        CAPTURE( reset );
        session_t session; view_settings_t settings( &session.gui.settings );
        std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &session.workspace ) ); ShowAt( camera.get(), 800, 600 );
        const QPointF point( 300, 220 );
        DragButton( camera.get(), QEvent::MouseButtonPress, point, Qt::RightButton );
        QKeyEvent down( QEvent::KeyPress, Qt::Key_W, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &down );
        REQUIRE( TestDistance( MapCameraView_NavigationVelocity( camera.get() ), {} ) > 999.0 );
        if ( QString::fromUtf8( reset ) == QStringLiteral( "focus" ) ) {
            QFocusEvent event( QEvent::FocusOut, Qt::OtherFocusReason ); QCoreApplication::sendEvent( camera.get(), &event );
        } else if ( QString::fromUtf8( reset ) == QStringLiteral( "cancel" ) ) {
            QKeyEvent event( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &event );
        } else if ( QString::fromUtf8( reset ) == QStringLiteral( "document" ) ) {
            REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
        } else { settings.Real( "editor.camera.fov", 90.0 ); }
        DragButton( camera.get(), QEvent::MouseButtonPress, point, Qt::RightButton );
        CheckPointClose( MapCameraView_NavigationVelocity( camera.get() ), {} );
        QKeyEvent repeat( QEvent::KeyPress, Qt::Key_W, Qt::NoModifier, QString(), true ); QCoreApplication::sendEvent( camera.get(), &repeat );
        CheckPointClose( MapCameraView_NavigationVelocity( camera.get() ), {} );
        QCoreApplication::sendEvent( camera.get(), &down ); CHECK( TestDistance( MapCameraView_NavigationVelocity( camera.get() ), {} ) > 999.0 );
        QKeyEvent release( QEvent::KeyRelease, Qt::Key_W, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &release );
        CheckPointClose( MapCameraView_NavigationVelocity( camera.get() ), {} );
    }
}

TEST_CASE( "Handle hover follows the pointer when object highlighting is disabled", "[map][gui][views][hover][gizmos][render]" )
{
    for ( const bool perspective : { false, true } ) {
        CAPTURE( perspective );
        session_t session; auto &ws = session.workspace;
        view_settings_t settings( &session.gui.settings );
        settings.Set( "editor.viewport.hover_highlight", false );
        settings.Set( "editor.viewport.active_border", false );
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t bounds{}; MapBounds_AddPoint( bounds, { -64, -64, -64 } ); MapBounds_AddPoint( bounds, { 64, 64, 64 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, bounds ) );
        MapWorkspace_Frame( &ws, CY_TRUE );
        std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &ws ) :
                                                  MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
        ShowAt( view.get(), 800, 600 );
        // Offscreen tests have no native pointer dispatch. Set only the
        // QWidget presence flag used by painting; all positions still travel
        // through the ordinary mouse event and hover paths.
        view->setAttribute( Qt::WA_UnderMouse, true );
        const auto project = [&]( math::vec3d_t world ) {
            QPointF point;
            if ( perspective ) { REQUIRE( MapCameraView_WorldToView( view.get(), world, &point ) ); }
            else { point = MapOrthoView_WorldToView( view.get(), { world.x, world.y } ); }
            REQUIRE( view->rect().adjusted( 10, 10, -10, -10 ).contains( point.toPoint() ) );
            return point;
        };
        const QPointF center = project( { 0, 0, 0 } ), resize = project( { 64, 0, 0 } );
        const auto move = [&]( QPointF point ) {
            QMouseEvent event( QEvent::MouseMove, point, view->mapToGlobal( point ), Qt::NoButton, Qt::NoButton, Qt::NoModifier );
            QCoreApplication::sendEvent( view.get(), &event );
        };
        const QColor hover = gui::EditorStyle_TokenColor( session.gui.style, "viewport.hover" );
        const auto greenNear = [&]( const QImage &image, QPointF point ) {
            const qreal ratio = image.devicePixelRatio();
            const QRect region = QRect( qRound( ( point.x() - 8 ) * ratio ), qRound( ( point.y() - 8 ) * ratio ),
                                        qRound( 16 * ratio ), qRound( 16 * ratio ) ).intersected( image.rect() );
            int count = 0;
            for ( int y = region.top(); y <= region.bottom(); ++y ) { for ( int x = region.left(); x <= region.right(); ++x ) {
                const QColor color = image.pixelColor( x, y );
                if ( std::abs( color.red() - hover.red() ) < 24 && std::abs( color.green() - hover.green() ) < 24 &&
                     std::abs( color.blue() - hover.blue() ) < 24 ) { ++count; }
            } }
            return count;
        };
        const auto *document = ws.pDocument;
        const auto revision = document->geometry.revision, selection = ws.selection.revision;
        const usize history = EditorHistory_StepCount( &ws.history );
        for ( const QPointF point : { center, resize } ) {
            CAPTURE( point.x(), point.y() );
            move( { 20, 20 } ); const QImage plain = view->grab().toImage();
            move( point ); const QImage highlighted = view->grab().toImage();
            CHECK( ChangedPixelsNear( plain, highlighted, point, 8 ) > 0 );
            CHECK( greenNear( highlighted, point ) > greenNear( plain, point ) );
            CHECK( MapView_HoveredObject( view.get() ) == 0u );
            move( { 20, 20 } ); const QImage restored = view->grab().toImage();
            CHECK( greenNear( restored, point ) == greenNear( plain, point ) );
        }
        // The disabled object pick must not reappear after its former delay.
        QEventLoop wait; QTimer::singleShot( 80, &wait, &QEventLoop::quit ); wait.exec();
        CHECK( MapView_HoveredObject( view.get() ) == 0u );
        CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision ); CHECK( ws.selection.revision == selection );
        CHECK( EditorHistory_StepCount( &ws.history ) == history ); CHECK( ws.tool == map_tool_t::SELECT );
    }
}

namespace
{
int HandleColorPixelsNear( const QImage &image, QPointF point, QColor expected, int radius = 6 )
{
    const qreal ratio = image.devicePixelRatio();
    const QRect region = QRect( qRound( ( point.x() - radius ) * ratio ), qRound( ( point.y() - radius ) * ratio ),
                                qRound( ( radius * 2 + 1 ) * ratio ), qRound( ( radius * 2 + 1 ) * ratio ) ).intersected( image.rect() );
    int count = 0;
    for ( int y = region.top(); y <= region.bottom(); ++y ) { for ( int x = region.left(); x <= region.right(); ++x ) {
        const QColor color = image.pixelColor( x, y );
        if ( std::abs( color.red() - expected.red() ) < 24 && std::abs( color.green() - expected.green() ) < 24 &&
             std::abs( color.blue() - expected.blue() ) < 24 ) { ++count; }
    } }
    return count;
}

void HoverMouse( QWidget *view, QPointF point )
{
    QMouseEvent event( QEvent::MouseMove, point, view->mapToGlobal( point ), Qt::NoButton, Qt::NoButton, Qt::NoModifier );
    QCoreApplication::sendEvent( view, &event );
}
}

TEST_CASE( "Tiny selected geometry retains visible resize controls without moving its world anchor", "[map][gui][views][geometry-edit][select-resize][gizmos][render][tiny-handles]" )
{
    for ( int variant = 0; variant < 3; ++variant ) {
        const bool perspective = variant != 0, offCenterPickup = variant == 2;
        CAPTURE( perspective, offCenterPickup );
        session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
        settings.Set( "editor.viewport.hover_highlight", false ); settings.Set( "editor.viewport.active_border", false );
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        // Keep the comfortable empty-map overview. Framing the tiny selection
        // would conceal the problem by making its actual bounds fill the pane.
        MapWorkspace_Frame( &ws, CY_FALSE );
        std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &ws ) :
                                                  MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
        ShowAt( view.get(), 800, 600 ); view->setAttribute( Qt::WA_UnderMouse, true );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -8, -8, -8 } ); MapBounds_AddPoint( box, { 8, 8, 8 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); MapWorkspace_SetGridSize( &ws, 64 );
        const u64 id = EditorSelection_At( &ws.selection, 0 );
        const auto project = [&]( math::vec3d_t world ) {
            QPointF point;
            if ( perspective ) { REQUIRE( MapCameraView_WorldToView( view.get(), world, &point ) ); }
            else { point = MapOrthoView_WorldToView( view.get(), { world.x, world.y } ); }
            return point;
        };
        const QPointF center = project( {} ), actualSide = project( { 8, 0, 0 } );
        const f64 actualDistance = QLineF( center, actualSide ).length();
        REQUIRE( actualDistance > 0.0 ); REQUIRE( actualDistance < 14.0 );
        const QPointF direction = perspective ? ( actualSide - center ) / actualDistance : QPointF( 1, 0 );
        f64 minimumRadius = 24.0;
        if ( perspective ) {
            const f64 moveLength = CameraGizmoLength( view.get(), {}, session.gui.settings );
            for ( u32 axis = 0; axis < 3; ++axis ) {
                math::vec3d_t end{}; SetTestCoordinate( end, axis, moveLength );
                minimumRadius = std::max( minimumRadius, QLineF( center, project( end ) ).length() + 18.0 );
            }
        } else { minimumRadius = 88.0; }
        const QPointF side = center + direction * minimumRadius;
        const QPointF press = side + ( offCenterPickup ? QPointF( -direction.y(), direction.x() ) * 5.0 : QPointF() );
        const QColor hover = gui::EditorStyle_TokenColor( session.gui.style, "viewport.hover" );
        std::vector<QPointF> controls;
        if ( perspective ) {
            for ( u32 axis = 0; axis < 3; ++axis ) { for ( const f64 sign : { -1.0, 1.0 } ) {
                math::vec3d_t anchor{}; SetTestCoordinate( anchor, axis, sign * 8 );
                const QPointF offset = project( anchor ) - center; const f64 length = std::hypot( offset.x(), offset.y() );
                REQUIRE( length > 0.0 ); REQUIRE( length < minimumRadius ); controls.push_back( center + offset * ( minimumRadius / length ) );
            } }
        } else {
            for ( const QPointF offset : { QPointF( -1, 0 ), QPointF( 1, 0 ), QPointF( 0, -1 ), QPointF( 0, 1 ),
                                          QPointF( -1, -1 ), QPointF( 1, -1 ), QPointF( -1, 1 ), QPointF( 1, 1 ) } ) {
                controls.push_back( center + offset * minimumRadius );
            }
        }
        for ( const QPointF control : controls ) {
            CAPTURE( control.x(), control.y() );
            REQUIRE( view->rect().adjusted( 8, 8, -8, -8 ).contains( control.toPoint() ) );
            HoverMouse( view.get(), { 20, 20 } ); const QImage idle = view->grab().toImage();
            HoverMouse( view.get(), control ); const QImage highlighted = view->grab().toImage();
            CHECK( ChangedPixelsNear( idle, highlighted, control, 7 ) > 0 );
            CHECK( HandleColorPixelsNear( highlighted, control, hover ) > HandleColorPixelsNear( idle, control, hover ) );
            CHECK( view->cursor().shape() != Qt::ArrowCursor );
        }
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision, selectionRevision = ws.selection.revision;
        const usize steps = EditorHistory_StepCount( &ws.history ); const auto original = ObjectLineVertices( ws.wire, id );
        const auto cameraPosition = perspective ? MapCameraView_Position( view.get() ) : math::vec3d_t{};
        // A visual leader does not turn its screen offset into a world delta.
        DragMouse( view.get(), QEvent::MouseButtonPress, press, Qt::ControlModifier );
        DragMouse( view.get(), QEvent::MouseMove, press, Qt::ControlModifier );
        CHECK_FALSE( ws.editPreview.bActive ); CheckObjectVertices( ws.wire, id, original );
        DragMouse( view.get(), QEvent::MouseButtonRelease, press, Qt::ControlModifier );
        CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
        CHECK( ws.selection.revision == selectionRevision ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
        // In Camera, the leader and off-center pickup offset accompany the
        // projection of the real boundary throughout the gesture.
        const QPointF end = perspective ? project( { 72, 0, 0 } ) + ( press - actualSide ) : press + direction * 12.0;
        REQUIRE( QLineF( press, end ).length() > 3.0 );
        DragMouse( view.get(), QEvent::MouseButtonPress, press, Qt::ControlModifier );
        DragMouse( view.get(), QEvent::MouseMove, end, Qt::ControlModifier );
        REQUIRE( ws.editPreview.bActive ); CHECK( ws.editPreview.transform.kind == map_transform_preview_kind_t::SCALE );
        REQUIRE( ws.editPreview.transform.bResize ); CHECK_FALSE( ws.editPreview.transform.bClone );
        const auto expected = ws.editPreview.bounds;
        CheckPointClose( expected.box.minimum, box.box.minimum );
        CHECK( expected.box.maximum.x > box.box.maximum.x ); CHECK( expected.box.maximum.y == box.box.maximum.y ); CHECK( expected.box.maximum.z == box.box.maximum.z );
        CheckPointClose( ws.editPreview.transform.pivot, { -8, 0, 0 } );
        if ( !perspective ) {
            // Ctrl bypasses the deliberately coarse grid; twelve logical
            // pixels correspond to exactly twelve/zoom world units in Top.
            CHECK( std::abs( expected.box.maximum.x - ( 8.0 + 12.0 / MapOrthoView_Zoom( view.get() ) ) ) < 1e-6 );
        } else {
            CHECK( std::abs( expected.box.maximum.x - 72.0 ) < 1e-6 );
            CheckPointClose( MapCameraView_Position( view.get() ), cameraPosition );
        }
        CHECK( document->geometry.revision == revision ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
        CheckObjectVertices( ws.wire, id, original );
        DragMouse( view.get(), QEvent::MouseButtonRelease, end, Qt::ControlModifier );
        CHECK_FALSE( ws.editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
        CHECK( EditorSelection_At( &ws.selection, 0 ) == id ); CHECK( ws.tool == map_tool_t::SELECT );
        CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, expected.box.minimum );
        CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, expected.box.maximum );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CheckObjectVertices( ws.wire, id, original );
        REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
        CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, expected.box.minimum );
        CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, expected.box.maximum );
    }
}

TEST_CASE( "Orthographic move and bounds controls have separate visible pickups across zoom and gizmo scales", "[map][gui][views][geometry-edit][select-resize][gizmos][render][handle-priority][ortho-handle-separation]" )
{
    for ( int projection = 0; projection < 3; ++projection ) { for ( const f64 scale : { 0.5, 1.0, 2.0 } ) { for ( const int wheelAngle : { -480, 480 } ) {
        const auto axes = static_cast<map_ortho_axes_t>( projection );
        const u32 u = axes == map_ortho_axes_t::FRONT ? 1u : 0u, v = axes == map_ortho_axes_t::TOP ? 1u : 2u;
        CAPTURE( projection, scale, wheelAngle );
        session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
        settings.Real( "editor.viewport.gizmo_scale", scale );
        settings.Set( "editor.viewport.hover_highlight", false ); settings.Set( "editor.viewport.active_border", false );
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK ); MapWorkspace_Frame( &ws, CY_FALSE );
        std::unique_ptr<QWidget> view( MapOrthoView_Create( nullptr, &ws, axes ) ); ShowAt( view.get(), 800, 600 );
        view->setAttribute( Qt::WA_UnderMouse, true );
        const f64 initialZoom = MapOrthoView_Zoom( view.get() );
        const QPointF wheelAt( view->width() * 0.5, view->height() * 0.5 );
        QWheelEvent wheel( wheelAt, view->mapToGlobal( wheelAt ), QPoint(), QPoint( 0, wheelAngle ), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false );
        QCoreApplication::sendEvent( view.get(), &wheel );
        CHECK( ( MapOrthoView_Zoom( view.get() ) > initialZoom ) == ( wheelAngle > 0 ) );
        const f64 zoom = MapOrthoView_Zoom( view.get() ), movePixels = 64.0 * scale;
        const QPointF center = MapOrthoView_WorldToView( view.get(), {} );
        // Match the reported failure: a real boundary sits exactly at the
        // move endpoint. Tiny bounds and wider bounds exercise both sides of
        // the presentation spacing floor without changing world dimensions.
        for ( const f64 halfPixels : { 8.0, movePixels, movePixels + 64.0 } ) {
            CAPTURE( halfPixels );
            map_bounds_t box{}; const f64 extent = halfPixels / zoom;
            MapBounds_AddPoint( box, { -extent, -extent, -extent } ); MapBounds_AddPoint( box, { extent, extent, extent } );
            REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
            const u64 id = EditorSelection_At( &ws.selection, 0 );
            MapWorkspace_SetGridSize( &ws, 16 );
            const f64 resizePixels = std::max( halfPixels, movePixels + 24.0 );
            const QColor axisU = gui::EditorStyle_TokenColor( session.gui.style, u == 0 ? "viewport.axis.x" : "viewport.axis.y" );
            const QColor axisV = gui::EditorStyle_TokenColor( session.gui.style, v == 1 ? "viewport.axis.y" : "viewport.axis.z" );
            const QColor hover = gui::EditorStyle_TokenColor( session.gui.style, "viewport.hover" );
            for ( int target = 0; target < 4; ++target ) {
                const bool resize = target >= 2; const bool vertical = ( target & 1 ) != 0;
                const u32 axis = vertical ? v : u;
                const QPointF direction = vertical ? QPointF( 0, -1 ) : QPointF( 1, 0 );
                const QPointF move = center + direction * movePixels, side = center + direction * resizePixels;
                const QPointF pointer = resize ? side : move;
                const QColor axisColor = vertical ? axisV : axisU;
                CAPTURE( target );
                REQUIRE( QLineF( move, side ).length() >= 24.0 );
                HoverMouse( view.get(), { 20, 20 } ); const QImage idle = view->grab().toImage();
                CHECK( HandleColorPixelsNear( idle, move, axisColor, 3 ) > 0 );
                CHECK( HandleColorPixelsNear( idle, side, axisColor, 5 ) > 0 );
                HoverMouse( view.get(), pointer ); const QImage highlighted = view->grab().toImage();
                CHECK( view->cursor().shape() == ( resize ? vertical ? Qt::SizeVerCursor : Qt::SizeHorCursor : Qt::SizeAllCursor ) );
                CHECK( HandleColorPixelsNear( highlighted, pointer, hover, 5 ) > HandleColorPixelsNear( idle, pointer, hover, 5 ) );
                const QPointF other = resize ? move : side;
                // An antialiased Y-axis pixel can resemble the green hover
                // token. The other target's pixels must remain unchanged.
                CHECK( ChangedPixelsNear( idle, highlighted, other, 5 ) == 0 );
                CHECK( HandleColorPixelsNear( highlighted, other, axisColor, 5 ) > 0 );
                const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
                const usize steps = EditorHistory_StepCount( &ws.history ), applied = EditorHistory_AppliedStepCount( &ws.history );
                // A stationary pickup must never commit its visual leader as travel.
                DragMouse( view.get(), QEvent::MouseButtonPress, pointer );
                DragMouse( view.get(), QEvent::MouseButtonRelease, pointer );
                CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.pDocument == document );
                CHECK( document->geometry.revision == revision ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
                const QPointF end = pointer + direction * ( 21.0 * zoom );
                auto expected = box;
                SetTestCoordinate( expected.box.maximum, axis, resize ? std::round( ( extent + 21.0 ) / 16.0 ) * 16.0 : extent + 16.0 );
                if ( !resize ) { SetTestCoordinate( expected.box.minimum, axis, -extent + 16.0 ); }
                DragMouse( view.get(), QEvent::MouseButtonPress, pointer ); DragMouse( view.get(), QEvent::MouseMove, end );
                REQUIRE( ws.editPreview.bActive ); CHECK( bool( ws.editPreview.transform.bResize ) == resize );
                CHECK( ws.editPreview.transform.kind == ( resize ? map_transform_preview_kind_t::SCALE : map_transform_preview_kind_t::TRANSLATE ) );
                CheckPointClose( ws.editPreview.bounds.box.minimum, expected.box.minimum );
                CheckPointClose( ws.editPreview.bounds.box.maximum, expected.box.maximum );
                CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
                CHECK( EditorHistory_StepCount( &ws.history ) == steps );
                DragMouse( view.get(), QEvent::MouseButtonRelease, end );
                CHECK_FALSE( ws.editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws.history ) == applied + 1u ); CHECK( EditorHistory_AppliedStepCount( &ws.history ) == applied + 1u );
                CHECK( ws.tool == map_tool_t::SELECT ); CHECK( EditorSelection_At( &ws.selection, 0 ) == id );
                CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, expected.box.minimum );
                CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, expected.box.maximum );
                REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
                CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, box.box.minimum );
                CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, box.box.maximum );
                REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
                CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, expected.box.minimum );
                CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, expected.box.maximum );
                REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
                if ( projection == 0 && scale == 1.0 && halfPixels == movePixels ) {
                    const auto *beforeCancel = ws.pDocument; const auto cancelRevision = beforeCancel->geometry.revision;
                    const usize cancelSteps = EditorHistory_StepCount( &ws.history );
                    DragMouse( view.get(), QEvent::MouseButtonPress, pointer ); DragMouse( view.get(), QEvent::MouseMove, end );
                    REQUIRE( ws.editPreview.bActive );
                    QKeyEvent cancel( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( view.get(), &cancel );
                    CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.tool == map_tool_t::SELECT );
                    DragMouse( view.get(), QEvent::MouseButtonRelease, end );
                    CHECK( ws.pDocument == beforeCancel ); CHECK( beforeCancel->geometry.revision == cancelRevision );
                    CHECK( EditorHistory_StepCount( &ws.history ) == cancelSteps ); CHECK( EditorSelection_At( &ws.selection, 0 ) == id );
                    CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, box.box.minimum );
                    CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, box.box.maximum );
                }
            }
            REQUIRE( MapWorkspace_DeleteSelection( &ws ) );
        }
    } } }
}

TEST_CASE( "Captured resize keeps its own control highlighted when the pointer crosses another control", "[map][gui][views][geometry-edit][select-resize][gizmos][render][captured-handle]" )
{
    session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
    settings.Set( "editor.viewport.hover_highlight", false ); settings.Set( "editor.viewport.active_border", false );
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK ); MapWorkspace_Frame( &ws, CY_FALSE );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) ); ShowAt( top.get(), 800, 600 );
    top->setAttribute( Qt::WA_UnderMouse, true );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, -64 } ); MapBounds_AddPoint( box, { 64, 64, 64 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
    MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
    const f64 zoom = MapOrthoView_Zoom( top.get() );
    const QPointF start = MapOrthoView_WorldToView( top.get(), { 64, 0 } );
    const QPointF corner = MapOrthoView_WorldToView( top.get(), { 64, 64 } );
    REQUIRE( QLineF( start, corner ).length() > 24.0 );
    const QPointF end = corner + QPointF( 6, 0 );
    const usize steps = EditorHistory_StepCount( &ws.history );
    DragMouse( top.get(), QEvent::MouseButtonPress, start, Qt::ControlModifier );
    DragMouse( top.get(), QEvent::MouseMove, end, Qt::ControlModifier );
    REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.transform.bResize );
    auto expected = box; expected.box.maximum.x += 6.0 / zoom;
    CheckPointClose( ws.editPreview.bounds.box.minimum, expected.box.minimum ); CheckPointClose( ws.editPreview.bounds.box.maximum, expected.box.maximum );
    CheckPointClose( ws.editPreview.transform.pivot, { -64, 0, 0 } );
    CHECK( top->cursor().shape() == Qt::SizeHorCursor );
    const QPointF captured = MapOrthoView_WorldToView( top.get(), { expected.box.maximum.x, 0 } );
    const QPointF crossed = MapOrthoView_WorldToView( top.get(), { expected.box.maximum.x, 64 } );
    REQUIRE( QLineF( crossed, end ).length() < 1e-6 );
    const QColor hover = gui::EditorStyle_TokenColor( session.gui.style, "viewport.hover" );
    const QColor selected = gui::EditorStyle_TokenColor( session.gui.style, "viewport.selection" );
    const QImage image = top->grab().toImage();
    CHECK( HandleColorPixelsNear( image, captured, hover ) > 0 );
    CHECK( HandleColorPixelsNear( image, crossed, hover ) == 0 );
    CHECK( HandleColorPixelsNear( image, crossed, selected ) > 0 );
    // Each control's label begins below and to its right. A second label
    // must not appear at the crossed corner during the captured side drag.
    CHECK( HandleColorPixelsNear( image, captured + QPointF( 40, 22 ), hover, 8 ) > 0 );
    CHECK( HandleColorPixelsNear( image, crossed + QPointF( 40, 22 ), hover, 8 ) == 0 );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    DragMouse( top.get(), QEvent::MouseButtonRelease, end, Qt::ControlModifier );
    CHECK_FALSE( ws.editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
    CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, expected.box.minimum );
    CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, expected.box.maximum );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, box.box.minimum );
    CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, box.box.maximum );
}

TEST_CASE( "Short narrow panes retain staged block dimensions and invalid construction feedback", "[map][gui][views][geometry-edit][block][render][compact-readout]" )
{
    for ( const bool perspective : { false, true } ) {
        CAPTURE( perspective );
        session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
        settings.Set( "editor.viewport.hover_highlight", false ); settings.Set( "editor.viewport.active_border", false );
        settings.Set( "editor.viewport.show_rulers", false ); settings.Set( "editor.viewport.show_axes", false );
        settings.Set( "editor.viewport.perspective.show_axes", false );
        settings.Set( "editor.viewport.show_metrics", false ); settings.Set( "editor.viewport.perspective.show_metrics", false );
        const char *dimensions = perspective ? "editor.viewport.perspective.show_selection_dimensions" : "editor.viewport.show_selection_dimensions";
        settings.Set( dimensions, true );
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK ); MapWorkspace_SetGridVisible( &ws, CY_FALSE );
        MapWorkspace_SetTool( &ws, map_tool_t::BLOCK ); MapWorkspace_Frame( &ws, CY_FALSE );
        std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &ws ) :
                                                  MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
        ShowAt( view.get(), 180, 100 ); view->setAttribute( Qt::WA_UnderMouse, false );
        const QImage empty = view->grab().toImage();
        // These extents need separate rows at this width unless the readout
        // falls back to its compact operation + XYZ presentation.
        map_bounds_t bounds{}; MapBounds_AddPoint( bounds, { 0, 0, 0 } ); MapBounds_AddPoint( bounds, { 1234, 2345, 3456 } );
        MapWorkspace_SetEditPreview( &ws, bounds ); REQUIRE( ws.editPreview.status == map_status_t::OK );
        REQUIRE( MapWorkspace_StageBlockPreview( &ws ) );
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision; const usize steps = EditorHistory_StepCount( &ws.history );
        const QImage ready = view->grab().toImage();
        CHECK( ChangedPixelsNear( empty, ready, { 90, 78 }, 20 ) > 0 );
        const QColor selected = gui::EditorStyle_TokenColor( session.gui.style, "viewport.selection" );
        CHECK( HandleColorPixelsNear( ready, { 90, 80 }, selected, 12 ) > 0 );
        settings.Set( dimensions, false ); const QImage withoutDimensions = view->grab().toImage();
        CHECK( HandleColorPixelsNear( withoutDimensions, { 90, 80 }, selected, 12 ) == 0 );
        settings.Set( dimensions, true );
        auto invalid = bounds; invalid.box.maximum.x = invalid.box.minimum.x;
        CHECK_FALSE( MapWorkspace_SetBlockPreviewBounds( &ws, invalid ) );
        REQUIRE( MapWorkspace_HasBlockPreview( &ws ) ); REQUIRE( ws.editPreview.status != map_status_t::OK );
        const QImage failed = view->grab().toImage();
        // The status row changes while the last valid dimensions remain
        // readable, ready for correction rather than disappearing on failure.
        CHECK( ChangedPixelsNear( ready, failed, { 90, 64 }, 12 ) > 0 );
        CHECK( HandleColorPixelsNear( failed, { 90, 80 }, selected, 12 ) > 0 );
        CheckPointClose( ws.editPreview.bounds.box.minimum, bounds.box.minimum ); CheckPointClose( ws.editPreview.bounds.box.maximum, bounds.box.maximum );
        CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision ); CHECK( document->geometry.brushes.nCount == 0u );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    }
}

TEST_CASE( "Camera resize does not advertise near-parallel controls that its capture plane cannot solve", "[map][gui][views][geometry-edit][block][select-resize][gizmos][render][near-parallel-handles]" )
{
    session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
    settings.Real( "editor.camera.look_sensitivity", 1.0 );
    settings.Set( "editor.viewport.hover_highlight", false ); settings.Set( "editor.viewport.active_border", false );
    settings.Set( "editor.viewport.perspective.show_axes", false ); settings.Set( "editor.viewport.perspective.show_metrics", false );
    settings.Set( "editor.viewport.perspective.show_selection_dimensions", false );
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK ); MapWorkspace_SetGridVisible( &ws, CY_FALSE ); MapWorkspace_Frame( &ws, CY_FALSE );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
    FacePositiveX( camera.get() ); MapWorkspace_SetTool( &ws, map_tool_t::BLOCK ); camera->setAttribute( Qt::WA_UnderMouse, true );
    const auto position = MapCameraView_Position( camera.get() );
    map_bounds_t bounds{};
    MapBounds_AddPoint( bounds, { position.x + 1.01, position.y + 1e-6 - 1, position.z + 1e-6 - 1 } );
    MapBounds_AddPoint( bounds, { position.x + 1.03, position.y + 1e-6 + 1, position.z + 1e-6 + 1 } );
    const auto project = [&]( math::vec3d_t world ) {
        QPointF point; REQUIRE( MapCameraView_WorldToView( camera.get(), world, &point ) ); return point;
    };
    const auto worldCenter = MapBounds_Center( bounds ); const QPointF center = project( worldCenter );
    std::vector<QPointF> candidates;
    for ( const f64 x : { bounds.box.minimum.x, bounds.box.maximum.x } ) {
        const auto anchor = math::vec3d_t{ x, worldCenter.y, worldCenter.z }; const QPointF actual = project( anchor );
        const QPointF offset = actual - center; const f64 distance = std::hypot( offset.x(), offset.y() );
        // A finite axis projection is insufficient: the ray is still almost
        // parallel to an axis-containing construction plane. Previously the
        // minimum spacing inflated this tiny offset into a visible red dot.
        REQUIRE( distance > 1e-6 ); REQUIRE( distance < 1e-3 );
        const QPointF candidate = center + offset * ( 24.0 / distance );
        REQUIRE( camera->rect().adjusted( 8, 8, -8, -8 ).contains( candidate.toPoint() ) ); candidates.push_back( candidate );
    }
    HoverMouse( camera.get(), { 20, 20 } ); const QImage empty = camera->grab().toImage();
    MapWorkspace_SetEditPreview( &ws, bounds ); REQUIRE( ws.editPreview.status == map_status_t::OK );
    REQUIRE( MapWorkspace_StageBlockPreview( &ws ) );
    const QColor axisX = gui::EditorStyle_TokenColor( session.gui.style, "viewport.axis.x" );
    const QColor hover = gui::EditorStyle_TokenColor( session.gui.style, "viewport.hover" );
    const auto *document = ws.pDocument; const auto revision = document->geometry.revision, selectionRevision = ws.selection.revision;
    const usize steps = EditorHistory_StepCount( &ws.history );
    for ( const QPointF point : candidates ) {
        CAPTURE( point.x(), point.y() );
        HoverMouse( camera.get(), { 20, 20 } ); const QImage idle = camera->grab().toImage();
        CHECK( HandleColorPixelsNear( idle, point, axisX, 7 ) == HandleColorPixelsNear( empty, point, axisX, 7 ) );
        CHECK( HandleColorPixelsNear( idle, point, axisX, 7 ) == 0 );
        HoverMouse( camera.get(), point ); const QImage highlighted = camera->grab().toImage();
        CHECK( HandleColorPixelsNear( highlighted, point, hover, 7 ) == 0 ); CHECK( camera->cursor().shape() == Qt::ArrowCursor );
        Click( camera.get(), point, Qt::ControlModifier );
        REQUIRE( MapWorkspace_HasBlockPreview( &ws ) ); CHECK( ws.editPreview.transform.kind == map_transform_preview_kind_t::NONE );
        CheckPointClose( ws.editPreview.bounds.box.minimum, bounds.box.minimum ); CheckPointClose( ws.editPreview.bounds.box.maximum, bounds.box.maximum );
        CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision ); CHECK( ws.selection.revision == selectionRevision );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( document->geometry.brushes.nCount == 0u );
    }
    QKeyEvent cancel( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &cancel );
    CHECK_FALSE( MapWorkspace_HasBlockPreview( &ws ) ); CHECK_FALSE( ws.editPreview.bActive );
    CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    CHECK( document->geometry.brushes.nCount == 0u );
}

TEST_CASE( "Shift resize mirrors all orthographic sides and corners around the captured off-grid center", "[map][gui][views][geometry-edit][select-resize][centered-resize]" )
{
    // Explicit expected endpoints exercise absolute boundary snapping rather
    // than snapping the center or rounding the resulting scale factor.
    const f64 expandedMin[3][2]{ { -96, -94 }, { -80, -82 }, { 0, 2 } };
    const f64 expandedMax[3][2]{ { 114, 112 }, { 110, 112 }, { 162, 160 } };
    const int signs[8][2]{ { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }, { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } };
    for ( const auto axes : { map_ortho_axes_t::TOP, map_ortho_axes_t::FRONT, map_ortho_axes_t::SIDE } ) {
        CAPTURE( static_cast<int>( axes ) );
        session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -71, -53, 17 } ); MapBounds_AddPoint( box, { 89, 83, 145 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
        const auto original = ObjectLineVertices( ws.wire, id ); const auto center = MapBounds_Center( box );
        MapWorkspace_SetGridSize( &ws, 16 );
        std::unique_ptr<QWidget> view( MapOrthoView_Create( nullptr, &ws, axes ) ); ShowAt( view.get(), 800, 600 );
        MapWorkspace_Frame( &ws, CY_TRUE );
        const u32 u = axes == map_ortho_axes_t::FRONT ? 1u : 0u, v = axes == map_ortho_axes_t::TOP ? 1u : 2u;
        const auto project = [&]( math::vec3d_t point ) { return MapOrthoView_WorldToView( view.get(), { TestCoordinate( point, u ), TestCoordinate( point, v ) } ); };
        for ( int handle = 0; handle < 8; ++handle ) {
            CAPTURE( handle );
            MapWorkspace_SetElementMode( &ws, handle & 1 ? map_element_mode_t::GROUPS : map_element_mode_t::OBJECTS );
            auto startPoint = center, endPoint = center; auto expected = box;
            for ( int dimension = 0; dimension < 2; ++dimension ) {
                const int sign = signs[handle][dimension]; if ( sign == 0 ) { continue; }
                const u32 axis = dimension == 0 ? u : v;
                const f64 boundary = TestCoordinate( sign > 0 ? box.box.maximum : box.box.minimum, axis );
                SetTestCoordinate( startPoint, axis, boundary ); SetTestCoordinate( endPoint, axis, boundary + sign * 22.0 );
                SetTestCoordinate( expected.box.minimum, axis, expandedMin[axis][sign > 0 ? 1 : 0] );
                SetTestCoordinate( expected.box.maximum, axis, expandedMax[axis][sign > 0 ? 1 : 0] );
            }
            const QPointF start = project( startPoint ), end = project( endPoint );
            const auto *document = ws.pDocument; const auto revision = document->geometry.revision; const usize steps = EditorHistory_StepCount( &ws.history );
            const usize applied = EditorHistory_AppliedStepCount( &ws.history );
            DragMouse( view.get(), QEvent::MouseButtonPress, start, Qt::ShiftModifier ); CHECK_FALSE( ws.editPreview.bActive );
            // Releasing Shift after pickup must keep the captured anchor.
            DragMouse( view.get(), QEvent::MouseMove, end );
            REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.transform.bResize ); CHECK( ws.editPreview.transform.bResizeFromCenter );
            CHECK_FALSE( ws.editPreview.transform.bClone ); CheckPointClose( ws.editPreview.transform.pivot, center );
            CheckPointClose( MapBounds_Center( ws.editPreview.bounds ), center );
            CheckPointClose( ws.editPreview.bounds.box.minimum, expected.box.minimum ); CheckPointClose( ws.editPreview.bounds.box.maximum, expected.box.maximum );
            CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
            CheckObjectVertices( ws.wire, id, original );
            DragMouse( view.get(), QEvent::MouseButtonRelease, end );
            CHECK_FALSE( ws.editPreview.bActive ); CHECK( EditorHistory_AppliedStepCount( &ws.history ) == applied + 1u );
            CHECK( EditorHistory_StepCount( &ws.history ) == applied + 1u ); CHECK( ws.pDocument->geometry.brushes.nCount == 1u );
            CHECK( EditorSelection_At( &ws.selection, 0 ) == id ); CHECK( ws.tool == map_tool_t::SELECT );
            CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, expected.box.minimum );
            CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, expected.box.maximum );
            REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CheckObjectVertices( ws.wire, id, original );
            if ( handle == 0 ) {
                REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
                CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, expected.box.minimum );
                CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, expected.box.maximum );
                REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
            }
        }
    }
}

TEST_CASE( "Shift camera resize keeps the off-grid center fixed on all six signed world axes", "[map][gui][views][geometry-edit][select-resize][centered-resize][camera]" )
{
    const f64 expandedMin[3][2]{ { -96, -94 }, { -80, -82 }, { 0, 2 } };
    const f64 expandedMax[3][2]{ { 114, 112 }, { 110, 112 }, { 162, 160 } };
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -71, -53, 17 } ); MapBounds_AddPoint( box, { 89, 83, 145 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
    const auto original = ObjectLineVertices( ws.wire, id ); const auto center = MapBounds_Center( box ); MapWorkspace_SetGridSize( &ws, 16 );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE );
    const auto position = MapCameraView_Position( camera.get() );
    for ( u32 axis = 0; axis < 3; ++axis ) { for ( const int sign : { -1, 1 } ) {
        CAPTURE( axis, sign ); auto startPoint = center, endPoint = center; auto expected = box;
        const f64 boundary = TestCoordinate( sign > 0 ? box.box.maximum : box.box.minimum, axis );
        SetTestCoordinate( startPoint, axis, boundary ); SetTestCoordinate( endPoint, axis, boundary + sign * 22 );
        SetTestCoordinate( expected.box.minimum, axis, expandedMin[axis][sign > 0 ? 1 : 0] );
        SetTestCoordinate( expected.box.maximum, axis, expandedMax[axis][sign > 0 ? 1 : 0] );
        QPointF start, end; REQUIRE( MapCameraView_WorldToView( camera.get(), startPoint, &start ) ); REQUIRE( MapCameraView_WorldToView( camera.get(), endPoint, &end ) );
        REQUIRE( camera->rect().adjusted( 8, 8, -8, -8 ).contains( start.toPoint() ) );
        const auto revision = ws.pDocument->geometry.revision; const usize steps = EditorHistory_StepCount( &ws.history );
        const usize applied = EditorHistory_AppliedStepCount( &ws.history );
        DragMouse( camera.get(), QEvent::MouseButtonPress, start, Qt::ShiftModifier );
        DragMouse( camera.get(), QEvent::MouseMove, end );
        REQUIRE( ws.editPreview.bActive ); CHECK( ws.editPreview.transform.bResize ); CHECK( ws.editPreview.transform.bResizeFromCenter ); CHECK_FALSE( ws.editPreview.transform.bClone );
        CheckPointClose( ws.editPreview.transform.pivot, center ); CheckPointClose( MapBounds_Center( ws.editPreview.bounds ), center );
        CheckPointClose( ws.editPreview.bounds.box.minimum, expected.box.minimum ); CheckPointClose( ws.editPreview.bounds.box.maximum, expected.box.maximum );
        CheckPointClose( MapCameraView_Position( camera.get() ), position ); CHECK( ws.pDocument->geometry.revision == revision );
        CheckObjectVertices( ws.wire, id, original ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
        DragMouse( camera.get(), QEvent::MouseButtonRelease, end );
        CHECK_FALSE( ws.editPreview.bActive ); CHECK( EditorHistory_AppliedStepCount( &ws.history ) == applied + 1u );
        CHECK( EditorHistory_StepCount( &ws.history ) == applied + 1u ); CHECK( ws.pDocument->geometry.brushes.nCount == 1u );
        CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, expected.box.minimum ); CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, expected.box.maximum );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CheckObjectVertices( ws.wire, id, original );
    } }
}

TEST_CASE( "Resize captures Shift once while snap bypass remains live and center crossing keeps a positive solid", "[map][gui][views][geometry-edit][select-resize][centered-resize][snap]" )
{
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -71, -53, 17 } ); MapBounds_AddPoint( box, { 89, 83, 145 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 ); MapWorkspace_SetGridSize( &ws, 16 );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) ); ShowAt( top.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE );
    const QPointF start = MapOrthoView_WorldToView( top.get(), { 89, 15 } ), end = MapOrthoView_WorldToView( top.get(), { 110, 15 } );
    const usize steps = EditorHistory_StepCount( &ws.history );
    DragMouse( top.get(), QEvent::MouseButtonPress, start, Qt::ShiftModifier );
    for ( const Qt::KeyboardModifiers modifiers : { Qt::KeyboardModifiers( Qt::NoModifier ), Qt::KeyboardModifiers( Qt::ControlModifier ), Qt::KeyboardModifiers( Qt::MetaModifier ), Qt::KeyboardModifiers( Qt::NoModifier ) } ) {
        CAPTURE( static_cast<int>( modifiers ) ); DragMouse( top.get(), QEvent::MouseMove, end, modifiers );
        REQUIRE( ws.editPreview.bActive ); CHECK( ws.editPreview.transform.bResizeFromCenter ); CHECK_FALSE( ws.editPreview.transform.bClone );
        const bool bypass = modifiers != Qt::NoModifier;
        CheckPointClose( ws.editPreview.bounds.box.minimum, { bypass ? -92.0 : -94.0, -53, 17 } );
        CheckPointClose( ws.editPreview.bounds.box.maximum, { bypass ? 110.0 : 112.0, 83, 145 } );
        CheckPointClose( ws.editPreview.transform.pivot, { 9, 15, 81 } );
    }
    const QPointF crossing = MapOrthoView_WorldToView( top.get(), { -11, 15 } );
    DragMouse( top.get(), QEvent::MouseMove, crossing, Qt::ControlModifier ); REQUIRE( ws.editPreview.bActive );
    CheckPointClose( ws.editPreview.bounds.box.minimum, { 8.5, -53, 17 } ); CheckPointClose( ws.editPreview.bounds.box.maximum, { 9.5, 83, 145 } );
    CHECK( ws.editPreview.transform.factors.x > 0 ); CHECK( ws.bSnapToGrid ); CHECK( ws.gridSize == 16 );
    QKeyEvent cancel( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( top.get(), &cancel );
    DragMouse( top.get(), QEvent::MouseButtonRelease, crossing, Qt::ControlModifier );
    CHECK_FALSE( ws.editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( EditorSelection_At( &ws.selection, 0 ) == id );
    CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, box.box.minimum ); CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, box.box.maximum );
    // Pressing Shift after an ordinary pickup does not re-anchor or clone.
    DragMouse( top.get(), QEvent::MouseButtonPress, start ); DragMouse( top.get(), QEvent::MouseMove, end, Qt::ShiftModifier | Qt::ControlModifier );
    REQUIRE( ws.editPreview.bActive ); CHECK_FALSE( ws.editPreview.transform.bResizeFromCenter ); CHECK_FALSE( ws.editPreview.transform.bClone );
    CheckPointClose( ws.editPreview.transform.pivot, { -71, 15, 81 } );
    CheckPointClose( ws.editPreview.bounds.box.minimum, box.box.minimum ); CheckPointClose( ws.editPreview.bounds.box.maximum, { 110, 83, 145 } );
    DragMouse( top.get(), QEvent::MouseButtonRelease, end, Qt::ShiftModifier | Qt::ControlModifier );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u ); CHECK( ws.pDocument->geometry.brushes.nCount == 1u );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, box.box.maximum );
}

TEST_CASE( "Centered resize cancels on a stale mode or document before its release can publish", "[map][gui][views][geometry-edit][select-resize][centered-resize][gesture-context]" )
{
    for ( const bool replaceDocument : { false, true } ) {
        CAPTURE( replaceDocument ); session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -71, -53, 17 } ); MapBounds_AddPoint( box, { 89, 83, 145 } ); REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
        std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) ); ShowAt( top.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE );
        const QPointF start = MapOrthoView_WorldToView( top.get(), { 89, 15 } ), end = MapOrthoView_WorldToView( top.get(), { 110, 15 } );
        const usize steps = EditorHistory_StepCount( &ws.history );
        DragMouse( top.get(), QEvent::MouseButtonPress, start, Qt::ShiftModifier ); DragMouse( top.get(), QEvent::MouseMove, end );
        REQUIRE( ws.editPreview.bActive ); CHECK( ws.editPreview.transform.bResizeFromCenter );
        if ( replaceDocument ) { REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK ); }
        else { MapWorkspace_SetElementMode( &ws, map_element_mode_t::GROUPS ); }
        CHECK_FALSE( ws.editPreview.bActive ); DragMouse( top.get(), QEvent::MouseButtonRelease, end );
        CHECK_FALSE( ws.editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws.history ) == ( replaceDocument ? 0u : steps ) );
        if ( replaceDocument ) { CHECK( ws.pDocument->geometry.brushes.nCount == 0u ); }
        else { CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, box.box.minimum ); CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, box.box.maximum ); }
    }
}

TEST_CASE( "Centered staged block resize mirrors its footprint and creates one solid only on confirmation", "[map][gui][views][geometry-edit][block][centered-resize]" )
{
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK ); MapWorkspace_SetTool( &ws, map_tool_t::BLOCK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -71, -53, 17 } ); MapBounds_AddPoint( box, { 89, 83, 145 } );
    MapWorkspace_SetEditPreview( &ws, box ); REQUIRE( ws.editPreview.status == map_status_t::OK ); REQUIRE( MapWorkspace_StageBlockPreview( &ws ) );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) ); ShowAt( top.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_FALSE );
    const QPointF start = MapOrthoView_WorldToView( top.get(), { 89, 83 } ), end = MapOrthoView_WorldToView( top.get(), { 102, 100 } );
    DragMouse( top.get(), QEvent::MouseButtonPress, start, Qt::ShiftModifier | Qt::ControlModifier );
    DragMouse( top.get(), QEvent::MouseMove, end, Qt::ControlModifier );
    REQUIRE( MapWorkspace_HasBlockPreview( &ws ) ); CheckPointClose( MapBounds_Center( ws.editPreview.bounds ), { 9, 15, 81 } );
    CheckPointClose( ws.editPreview.bounds.box.minimum, { -84, -70, 17 } ); CheckPointClose( ws.editPreview.bounds.box.maximum, { 102, 100, 145 } );
    CHECK( ws.pDocument->geometry.brushes.nCount == 0u ); CHECK( EditorHistory_StepCount( &ws.history ) == 0u );
    DragMouse( top.get(), QEvent::MouseButtonRelease, end, Qt::ControlModifier ); REQUIRE( MapWorkspace_HasBlockPreview( &ws ) );
    CHECK( ws.pDocument->geometry.brushes.nCount == 0u ); CHECK( EditorHistory_StepCount( &ws.history ) == 0u );
    QKeyEvent confirm( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier ); QCoreApplication::sendEvent( top.get(), &confirm ); QCoreApplication::sendEvent( top.get(), &confirm );
    CHECK_FALSE( ws.editPreview.bActive ); REQUIRE( ws.pDocument->geometry.brushes.nCount == 1u ); CHECK( EditorHistory_StepCount( &ws.history ) == 1u );
    CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, { -84, -70, 17 } ); CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, { 102, 100, 145 } );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CHECK( ws.pDocument->geometry.brushes.nCount == 0u );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); REQUIRE( ws.pDocument->geometry.brushes.nCount == 1u );
    CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, { -84, -70, 17 } ); CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, { 102, 100, 145 } );
}

TEST_CASE( "The persisted resize anchor default is captured once without changing an active gesture", "[map][gui][views][geometry-edit][select-resize][centered-resize][settings]" )
{
    session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    CHECK_FALSE( EditorSettings_Bool( &session.gui.settings, "editor.map.resize_from_center", CY_FALSE ) );
    settings.Set( "editor.map.resize_from_center", true );
    text_buffer_t text{}; REQUIRE( TextBuffer_Init( &text, Allocator_GetSystem() ) );
    REQUIRE( SettingsDocument_Write( &settings.store, &text ) == settings_document_status_t::OK );
    settings_document_t restored{}; REQUIRE( SettingsDocument_Init( &restored, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &restored, TextBuffer_View( &text ) ).status == settings_document_status_t::OK );
    settings.Set( "editor.map.resize_from_center", false );
    EditorSettings_SetScope( &session.gui.settings, settings_scope_t::USER, &restored );
    REQUIRE( EditorSettings_Bool( &session.gui.settings, "editor.map.resize_from_center", CY_FALSE ) );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -71, -53, 17 } ); MapBounds_AddPoint( box, { 89, 83, 145 } ); REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) ); ShowAt( top.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE );
    const QPointF start = MapOrthoView_WorldToView( top.get(), { 89, 15 } ), end = MapOrthoView_WorldToView( top.get(), { 110, 15 } );
    const usize steps = EditorHistory_StepCount( &ws.history );
    DragMouse( top.get(), QEvent::MouseButtonPress, start );
    // Restore the now-false setting document after pickup, before any motion.
    EditorSettings_SetScope( &session.gui.settings, settings_scope_t::USER, &settings.store );
    REQUIRE_FALSE( EditorSettings_Bool( &session.gui.settings, "editor.map.resize_from_center", CY_TRUE ) );
    DragMouse( top.get(), QEvent::MouseMove, end, Qt::ControlModifier ); REQUIRE( ws.editPreview.bActive );
    CHECK( ws.editPreview.transform.bResizeFromCenter ); CheckPointClose( ws.editPreview.transform.pivot, { 9, 15, 81 } );
    CheckPointClose( ws.editPreview.bounds.box.minimum, { -92, -53, 17 } ); CheckPointClose( ws.editPreview.bounds.box.maximum, { 110, 83, 145 } );
    QKeyEvent cancel( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( top.get(), &cancel );
    DragMouse( top.get(), QEvent::MouseButtonRelease, end, Qt::ControlModifier ); CHECK_FALSE( ws.editPreview.bActive );
    DragMouse( top.get(), QEvent::MouseButtonPress, start ); DragMouse( top.get(), QEvent::MouseMove, end, Qt::ControlModifier );
    REQUIRE( ws.editPreview.bActive ); CHECK_FALSE( ws.editPreview.transform.bResizeFromCenter );
    CheckPointClose( ws.editPreview.bounds.box.minimum, box.box.minimum ); CheckPointClose( ws.editPreview.bounds.box.maximum, { 110, 83, 145 } );
    QCoreApplication::sendEvent( top.get(), &cancel ); DragMouse( top.get(), QEvent::MouseButtonRelease, end, Qt::ControlModifier );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( ws.pDocument->geometry.brushes.nCount == 1u );
}

TEST_CASE( "Move source outlines and travel feedback appear in every pane and end with the gesture", "[map][gui][views][geometry-edit][shared-transform][source-ghost][render]" )
{
    for ( const bool clone : { false, true } ) {
        CAPTURE( clone ); session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
        for ( const char *key : { "editor.viewport.active_border", "editor.viewport.hover_highlight", "editor.viewport.show_axes",
            "editor.viewport.show_rulers", "editor.viewport.show_metrics", "editor.viewport.perspective.show_axes",
            "editor.viewport.perspective.show_metrics", "editor.viewport.show_selection_dimensions", "editor.viewport.perspective.show_selection_dimensions" } ) { settings.Set( key, false ); }
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK ); MapWorkspace_SetGridVisible( &ws, CY_FALSE ); MapWorkspace_Frame( &ws, CY_FALSE );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, 0 } ); MapBounds_AddPoint( box, { 64, 64, 128 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
        const auto original = ObjectLineVertices( ws.wire, id ); MapWorkspace_SetTool( &ws, map_tool_t::TRANSLATE );
        std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
        std::unique_ptr<QWidget> front( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::FRONT ) );
        std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) );
        for ( auto *view : { top.get(), front.get(), camera.get() } ) { ShowAt( view, 800, 600 ); view->setAttribute( Qt::WA_UnderMouse, false ); }
        top->setAttribute( Qt::WA_UnderMouse, true ); MapCameraView_SetRenderMode( camera.get(), map_render_mode_t::SHADED );
        QWidget *panes[]{ top.get(), front.get(), camera.get() };
        QPointF probes[]{ MapOrthoView_WorldToView( top.get(), { -64, 0 } ), MapOrthoView_WorldToView( front.get(), { -64, 64 } ), {} };
        REQUIRE( MapCameraView_WorldToView( camera.get(), { -64, -64, 64 }, &probes[2] ) );
        const QColor source = gui::EditorStyle_TokenColor( session.gui.style, "viewport.transform.source" ); REQUIRE( source.isValid() );
        QImage before[3];
        for ( usize i = 0; i < 3; ++i ) { before[i] = panes[i]->grab().toImage(); CHECK( HandleColorPixelsNear( before[i], probes[i], source, 9 ) == 0 ); }
        const QPointF start = MapOrthoView_WorldToView( top.get(), {} ), end = MapOrthoView_WorldToView( top.get(), { 192, 128 } );
        const QPointF travel = MapOrthoView_WorldToView( top.get(), { 96, 64 } ), label = start + QPointF( 24, 16 );
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision; const usize steps = EditorHistory_StepCount( &ws.history );
        const auto begin = [&]() {
            DragMouse( top.get(), QEvent::MouseButtonPress, start, clone ? Qt::ShiftModifier | Qt::ControlModifier : Qt::ControlModifier );
            DragMouse( top.get(), QEvent::MouseMove, end, Qt::ControlModifier );
            REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.transform.kind == map_transform_preview_kind_t::TRANSLATE );
            CHECK( bool( ws.editPreview.transform.bClone ) == clone ); CheckPointClose( ws.editPreview.transform.delta, { 192, 128, 0 } );
        };
        const auto checkSource = [&]() {
            for ( usize i = 0; i < 3; ++i ) { CAPTURE( i ); const QImage preview = panes[i]->grab().toImage();
                if ( i == 2 && HandleColorPixelsNear( preview, probes[i], source, 9 ) == 0 &&
                     !qEnvironmentVariableIsEmpty( "CYPHER_SOURCE_GHOST_CAPTURE" ) ) {
                    const QString path = qEnvironmentVariable( "CYPHER_SOURCE_GHOST_CAPTURE" );
                    CHECK( preview.save( path ) );
                    INFO( "Camera source probe: " << probes[i].x() << ", " << probes[i].y() << "; DPR " << preview.devicePixelRatio() );
                }
                CHECK( ChangedPixelsNear( before[i], preview, probes[i], 9 ) > 0 ); CHECK( HandleColorPixelsNear( preview, probes[i], source, 9 ) > 0 );
            }
            const QImage active = top->grab().toImage();
            CHECK( HandleColorPixelsNear( active, travel, source, 6 ) > 0 ); CHECK( HandleColorPixelsNear( active, label, source, 10 ) > 0 );
            CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
            CheckObjectVertices( ws.wire, id, original );
        };
        begin(); checkSource();
        QKeyEvent cancel( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( top.get(), &cancel );
        DragMouse( top.get(), QEvent::MouseButtonRelease, end, Qt::ControlModifier ); CHECK_FALSE( ws.editPreview.bActive );
        for ( usize i = 0; i < 3; ++i ) { CHECK( HandleColorPixelsNear( panes[i]->grab().toImage(), probes[i], source, 9 ) == 0 ); }
        CHECK( HandleColorPixelsNear( top->grab().toImage(), label, source, 10 ) == 0 ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
        map_transform_preview_t identity{}; identity.kind = map_transform_preview_kind_t::TRANSLATE; identity.pivot = { 0, 0, 64 };
        MapWorkspace_SetTransformPreview( &ws, identity ); REQUIRE( ws.editPreview.bActive );
        for ( usize i = 0; i < 3; ++i ) { CHECK( HandleColorPixelsNear( panes[i]->grab().toImage(), probes[i], source, 9 ) == 0 ); }
        MapWorkspace_ClearEditPreview( &ws );
        begin(); checkSource(); DragMouse( top.get(), QEvent::MouseButtonRelease, end, Qt::ControlModifier );
        CHECK_FALSE( ws.editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
        for ( usize i = 0; i < 3; ++i ) { CHECK( HandleColorPixelsNear( panes[i]->grab().toImage(), probes[i], source, 9 ) == 0 ); }
        CHECK( HandleColorPixelsNear( top->grab().toImage(), label, source, 10 ) == 0 );
        REQUIRE( ws.pDocument->geometry.brushes.nCount == ( clone ? 2u : 1u ) );
        if ( clone ) { CheckObjectVertices( ws.wire, id, original ); CHECK( EditorSelection_At( &ws.selection, 0 ) != id ); }
        else { CHECK( EditorSelection_At( &ws.selection, 0 ) == id ); }
        CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, { 128, 64, 0 } );
        CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, { 256, 192, 128 } );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CheckObjectVertices( ws.wire, id, original );
    }
}

TEST_CASE( "Move travel distance follows each viewport family's dimension display policy", "[map][gui][views][geometry-edit][shared-transform][source-ghost][display-policy][render]" )
{
    session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
    for ( const char *key : { "editor.viewport.active_border", "editor.viewport.hover_highlight", "editor.viewport.show_axes",
        "editor.viewport.show_rulers", "editor.viewport.show_metrics", "editor.viewport.perspective.show_axes",
        "editor.viewport.perspective.show_metrics", "editor.viewport.show_selection_dimensions", "editor.viewport.perspective.show_selection_dimensions" } ) { settings.Set( key, false ); }
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK ); MapWorkspace_SetGridVisible( &ws, CY_FALSE ); MapWorkspace_Frame( &ws, CY_FALSE );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, 0 } ); MapBounds_AddPoint( box, { 64, 64, 128 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); MapWorkspace_SetTool( &ws, map_tool_t::TRANSLATE );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) );
    for ( auto *view : { top.get(), camera.get() } ) { ShowAt( view, 800, 600 ); view->setAttribute( Qt::WA_UnderMouse, false ); }
    map_transform_preview_t transform{}; transform.kind = map_transform_preview_kind_t::TRANSLATE;
    transform.pivot = { 0, 0, 64 }; transform.delta = { 512, 0, 0 }; MapWorkspace_SetTransformPreview( &ws, transform );
    REQUIRE( ws.editPreview.bActive );
    QWidget *panes[]{ top.get(), camera.get() };
    QPointF starts[]{ MapOrthoView_WorldToView( top.get(), {} ), {} };
    QPointF ends[]{ MapOrthoView_WorldToView( top.get(), { 512, 0 } ), {} };
    REQUIRE( MapCameraView_WorldToView( camera.get(), transform.pivot, &starts[1] ) );
    REQUIRE( MapCameraView_WorldToView( camera.get(), { 512, 0, 64 }, &ends[1] ) );
    const QFontMetrics metrics( gui::EditorStyle_Font( session.gui.style, "viewport.labels" ) );
    const QSizeF distanceSize( metrics.horizontalAdvance( QStringLiteral( "512 u" ) ) + 10, metrics.height() + 4 );
    const QSizeF startSize( metrics.horizontalAdvance( QStringLiteral( "Start" ) ) + 10, metrics.height() + 4 );
    QRectF distanceAreas[2], startAreas[2];
    for ( usize i = 0; i < 2; ++i ) {
        REQUIRE( QLineF( starts[i], ends[i] ).length() >= 90 );
        distanceAreas[i] = QRectF( ( starts[i] + ends[i] ) * 0.5 + QPointF( 8, 8 ), distanceSize );
        startAreas[i] = QRectF( starts[i] + QPointF( 8, 8 ), startSize );
        REQUIRE( QRectF( panes[i]->rect() ).adjusted( 4, 4, -4, -4 ).contains( distanceAreas[i] ) );
    }
    const QColor source = gui::EditorStyle_TokenColor( session.gui.style, "viewport.transform.source" );
    const auto pixels = [&]( const QImage &image, QRectF area ) {
        const qreal ratio = image.devicePixelRatio();
        const QRect region = QRectF( area.topLeft() * ratio, area.size() * ratio ).toAlignedRect().intersected( image.rect() );
        int count = 0;
        for ( int y = region.top(); y <= region.bottom(); ++y ) { for ( int x = region.left(); x <= region.right(); ++x ) {
            const QColor color = image.pixelColor( x, y );
            if ( std::abs( color.red() - source.red() ) < 24 && std::abs( color.green() - source.green() ) < 24 &&
                 std::abs( color.blue() - source.blue() ) < 24 ) { ++count; }
        } }
        return count;
    };
    const QImage hidden[]{ top->grab().toImage(), camera->grab().toImage() };
    // Hiding dimensions retains the source marker and dashed motion guide.
    for ( usize i = 0; i < 2; ++i ) {
        CHECK( pixels( hidden[i], startAreas[i] ) > 0 );
        CHECK( HandleColorPixelsNear( hidden[i], ( starts[i] + ends[i] ) * 0.5, source, 6 ) > 0 );
    }
    settings.Set( "editor.viewport.show_selection_dimensions", true );
    const QImage topDimensions = top->grab().toImage();
    CHECK( pixels( topDimensions, distanceAreas[0] ) > pixels( hidden[0], distanceAreas[0] ) );
    CHECK( camera->grab().toImage() == hidden[1] );
    settings.Set( "editor.viewport.show_selection_dimensions", false );
    CHECK( top->grab().toImage() == hidden[0] );
    settings.Set( "editor.viewport.perspective.show_selection_dimensions", true );
    const QImage cameraDimensions = camera->grab().toImage();
    CHECK( pixels( cameraDimensions, distanceAreas[1] ) > pixels( hidden[1], distanceAreas[1] ) );
    CHECK( top->grab().toImage() == hidden[0] );
    settings.Set( "editor.viewport.perspective.show_selection_dimensions", false );
    CHECK( camera->grab().toImage() == hidden[1] );
    REQUIRE( ws.editPreview.bActive ); CheckPointClose( ws.editPreview.transform.delta, transform.delta );
    CHECK( ws.pDocument->geometry.brushes.nCount == 1u );
}

namespace
{
u64 CreateMeshFaceTestCube( map_workspace_t &ws )
{
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_bounds_t bounds{}; MapBounds_AddPoint( bounds, { -64, -64, 0 } ); MapBounds_AddPoint( bounds, { 64, 64, 128 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, bounds ) );
    const u64 id = EditorSelection_At( &ws.selection, 0 );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) );
    REQUIRE( geometry::GeometryDocument_FindMesh( &ws.pDocument->geometry, { id } ) != nullptr );
    return id;
}

u64 MeshFaceInDirection( const map_workspace_t &ws, u64 object, u32 axis )
{
    for ( usize i = 0; i < ws.wire.faces.nCount; ++i ) {
        const auto &face = ws.wire.faces.pData[i];
        if ( face.id == object && face.faceId != 0 && TestCoordinate( face.normal, axis ) > 0.99 ) { return face.faceId; }
    }
    FAIL( "The authored cube must have a face in the requested positive axis" );
    return 0;
}

void HoverMeshFaceTestPoint( QWidget *view, QPointF point )
{
    QMouseEvent event( QEvent::MouseMove, point, view->mapToGlobal( point ), Qt::NoButton, Qt::NoButton, Qt::NoModifier );
    QCoreApplication::sendEvent( view, &event );
    QEventLoop wait; QTimer::singleShot( 80, &wait, &QEventLoop::quit ); wait.exec();
}

struct mesh_face_allocation_failure_t { usize calls{}, failOn{}, live{}; };
void *MeshFaceTestAllocate( void *context, usize bytes, usize alignment ) noexcept
{
    auto &audit = *static_cast<mesh_face_allocation_failure_t *>( context );
    if ( ++audit.calls == audit.failOn ) { return nullptr; }
    void *memory = Allocator_Allocate( Allocator_GetSystem(), bytes, alignment );
    if ( memory != nullptr ) { ++audit.live; }
    return memory;
}
void MeshFaceTestFree( void *context, void *memory, usize bytes, usize alignment ) noexcept
{
    if ( memory != nullptr ) { --static_cast<mesh_face_allocation_failure_t *>( context )->live; }
    Allocator_Free( Allocator_GetSystem(), memory, bytes, alignment );
}
}

TEST_CASE( "Neutral navigation retains a selected mesh face until the next physical Cancel", "[map][gui][views][mesh-face][input][cancel][neutral-navigation]" )
{
    for ( const bool perspective : { false, true } ) {
        CAPTURE( perspective ); session_t session; auto &ws = session.workspace;
        const u64 id = CreateMeshFaceTestCube( ws ), face = MeshFaceInDirection( ws, id, 2u );
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES ); MapWorkspace_SelectMeshFace( &ws, id, face );
        REQUIRE( MapWorkspace_HasMeshFace( &ws ) );
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision, selection = ws.selection.revision;
        const usize steps = EditorHistory_StepCount( &ws.history ); const auto token = UndoRedo_StateToken( ws.history.pUndo );
        const auto original = ObjectLineVertices( ws.wire, id );
        std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &ws ) : MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
        ShowAt( view.get(), 800, 600 );
        QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( view.get(), &escape );
        CHECK( ws.tool == map_tool_t::NONE ); CHECK( MapWorkspace_IsSelected( &ws, id ) ); CHECK( ws.selection.revision == selection );
        CHECK( MapWorkspace_HasMeshFace( &ws ) ); CHECK( ws.selectedMeshFaceObject == id ); CHECK( ws.selectedMeshFaceId == face );
        CHECK( ws.elementMode == map_element_mode_t::FACES );
        QKeyEvent repeat( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier, QString(), true ); QCoreApplication::sendEvent( view.get(), &repeat );
        CHECK( MapWorkspace_HasMeshFace( &ws ) ); CHECK( MapWorkspace_IsSelected( &ws, id ) );
        QCoreApplication::sendEvent( view.get(), &escape ); CHECK_FALSE( MapWorkspace_HasMeshFace( &ws ) ); CHECK( EditorSelection_Count( &ws.selection ) == 0u );
        CHECK( ws.tool == map_tool_t::NONE ); CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
        CheckObjectVertices( ws.wire, id, original );
    }
}

TEST_CASE( "Faces mode picks exact authored mesh faces in the camera and every orthographic pane", "[map][gui][views][mesh-face][picking]" )
{
    for ( int projection : { -1, 0, 1, 2 } ) {
        CAPTURE( projection ); session_t session; auto &ws = session.workspace;
        const u64 object = CreateMeshFaceTestCube( ws );
        const bool camera = projection < 0;
        const auto axes = static_cast<map_ortho_axes_t>( std::max( 0, projection ) );
        const u32 u = axes == map_ortho_axes_t::FRONT ? 1u : 0u, v = axes == map_ortho_axes_t::TOP ? 1u : 2u;
        const u32 depth = camera ? 2u : 3u - u - v;
        const u64 face = MeshFaceInDirection( ws, object, depth );
        std::unique_ptr<QWidget> view( camera ? MapCameraView_Create( nullptr, &ws ) : MapOrthoView_Create( nullptr, &ws, axes ) );
        ShowAt( view.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        math::vec3d_t point{ 16, 8, 80 }; SetTestCoordinate( point, depth, depth == 2u ? 128.0 : 64.0 );
        QPointF screen;
        if ( camera ) { REQUIRE( MapCameraView_WorldToView( view.get(), point, &screen ) ); }
        else { screen = MapOrthoView_WorldToView( view.get(), { TestCoordinate( point, u ), TestCoordinate( point, v ) } ); }
        REQUIRE( view->rect().contains( screen.toPoint() ) );
        // A face interior on an unselected root must be pickable directly.
        MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
        const usize steps = EditorHistory_StepCount( &ws.history ); const auto revision = ws.pDocument->geometry.revision;
        Click( view.get(), screen ); REQUIRE( MapWorkspace_HasMeshFace( &ws ) );
        CHECK( ws.selectedMeshFaceObject == object ); CHECK( ws.selectedMeshFaceId == face );
        CHECK_FALSE( MapWorkspace_HasBrushFace( &ws ) ); CHECK( ws.selection.ids.nCount == 1u );
        Click( view.get(), screen, Qt::ControlModifier ); CHECK_FALSE( MapWorkspace_HasMeshFace( &ws ) );
        CHECK( ws.selectedMeshFaceId == 0 );
        Click( view.get(), screen ); REQUIRE( MapWorkspace_HasMeshFace( &ws ) );
        Click( view.get(), screen, Qt::MetaModifier ); CHECK_FALSE( MapWorkspace_HasMeshFace( &ws ) );
        Click( view.get(), screen ); REQUIRE( MapWorkspace_HasMeshFace( &ws ) );
        // Select-body drags cannot deform or move the root in component mode.
        DragMouse( view.get(), QEvent::MouseButtonPress, screen );
        DragMouse( view.get(), QEvent::MouseMove, screen + QPointF( 48, -32 ) ); CHECK_FALSE( ws.editPreview.bActive );
        DragMouse( view.get(), QEvent::MouseButtonRelease, screen + QPointF( 48, -32 ) );
        CHECK( ws.selectedMeshFaceObject == object ); CHECK( ws.selectedMeshFaceId == face );
        CHECK( ws.pDocument->geometry.revision == revision ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
        Click( view.get(), { 3, 3 } ); CHECK_FALSE( MapWorkspace_HasMeshFace( &ws ) );
        CHECK( ws.selectedMeshFaceObject == 0 ); CHECK( ws.selectedMeshFaceId == 0 );
    }
}

TEST_CASE( "Orthographic authored mesh face hover and picking respect the primitive silhouette", "[map][gui][views][mesh-face][picking][hover]" )
{
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_primitive_desc_t wedge{}; wedge.kind = map_primitive_kind_t::WEDGE; wedge.bounds = { { -96, -64, 0 }, { 96, 64, 128 } };
    REQUIRE( MapWorkspace_CreatePrimitive( &ws, wedge ) ); const u64 object = EditorSelection_At( &ws.selection, 0 );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) );
    std::unique_ptr<QWidget> side( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::SIDE ) );
    ShowAt( side.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
    MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
    const QPointF inside = MapOrthoView_WorldToView( side.get(), { -32, 32 } );
    const QPointF outside = MapOrthoView_WorldToView( side.get(), { 64, 112 } );
    HoverMeshFaceTestPoint( side.get(), inside ); CHECK( MapView_HoveredObject( side.get() ) == object );
    Click( side.get(), inside ); REQUIRE( MapWorkspace_HasMeshFace( &ws ) ); const u64 face = ws.selectedMeshFaceId;
    REQUIRE( face != 0 );
    HoverMeshFaceTestPoint( side.get(), outside ); CHECK( MapView_HoveredObject( side.get() ) == 0 );
    Click( side.get(), outside ); CHECK_FALSE( MapWorkspace_HasMeshFace( &ws ) ); CHECK( ws.selectedMeshFaceId == 0 );
    CHECK( EditorSelection_Count( &ws.selection ) == 0u );
}

TEST_CASE( "A visible nearer brush blocks camera mesh-face picking until it is hidden", "[map][gui][views][mesh-face][picking][occlusion]" )
{
    session_t session; auto &ws = session.workspace; const u64 mesh = CreateMeshFaceTestCube( ws );
    const u64 face = MeshFaceInDirection( ws, mesh, 2u );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) );
    ShowAt( camera.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
    const math::vec3d_t target{ 16, 8, 128 }, position = MapCameraView_Position( camera.get() );
    const f64 length = TestDistance( position, target ); REQUIRE( length > 64 );
    const math::vec3d_t blockerCenter{ target.x + ( position.x - target.x ) * 48 / length,
        target.y + ( position.y - target.y ) * 48 / length, target.z + ( position.z - target.z ) * 48 / length };
    map_bounds_t bounds{}; MapBounds_AddPoint( bounds, { blockerCenter.x - 8, blockerCenter.y - 8, blockerCenter.z - 8 } );
    MapBounds_AddPoint( bounds, { blockerCenter.x + 8, blockerCenter.y + 8, blockerCenter.z + 8 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, bounds ) ); const u64 blocker = EditorSelection_At( &ws.selection, 0 );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES ); MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
    QPointF screen; REQUIRE( MapCameraView_WorldToView( camera.get(), target, &screen ) );
    REQUIRE( MapCameraView_Pick( camera.get(), screen ) == blocker );
    HoverMeshFaceTestPoint( camera.get(), screen ); CHECK( MapView_HoveredObject( camera.get() ) == blocker );
    Click( camera.get(), screen ); CHECK_FALSE( MapWorkspace_HasMeshFace( &ws ) ); REQUIRE( MapWorkspace_HasBrushFace( &ws ) );
    MapWorkspace_SetVisgroupHidden( &ws, map_visgroup_t::BRUSHES, CY_TRUE );
    REQUIRE( MapCameraView_Pick( camera.get(), screen ) == mesh );
    HoverMeshFaceTestPoint( camera.get(), screen ); CHECK( MapView_HoveredObject( camera.get() ) == mesh );
    Click( camera.get(), screen ); REQUIRE( MapWorkspace_HasMeshFace( &ws ) );
    CHECK( ws.selectedMeshFaceObject == mesh ); CHECK( ws.selectedMeshFaceId == face ); CHECK_FALSE( MapWorkspace_HasBrushFace( &ws ) );
}

TEST_CASE( "Mesh face modeling retains the cap or inner identity through undo redo and persistence", "[map][gui][views][mesh-face][modeling][persistence]" )
{
    for ( bool extrude : { false, true } ) {
        CAPTURE( extrude ); session_t session; auto &ws = session.workspace; const u64 object = CreateMeshFaceTestCube( ws );
        const u64 face = MeshFaceInDirection( ws, object, 2u ); MapWorkspace_SelectMeshFace( &ws, object, face );
        REQUIRE( MapWorkspace_CanEditMeshFace( &ws ) ); const usize steps = EditorHistory_StepCount( &ws.history );
        const auto original = MapViews_SelectionGeometryBounds( &ws );
        const bool modeled = extrude ? MapWorkspace_ExtrudeMeshFace( &ws, 32 ) : MapWorkspace_InsetMeshFace( &ws, 16 );
        REQUIRE( modeled );
        REQUIRE( MapWorkspace_HasMeshFace( &ws ) ); CHECK( ws.selectedMeshFaceObject == object ); CHECK( ws.selectedMeshFaceId == face );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
        const auto changed = MapViews_SelectionGeometryBounds( &ws );
        CHECK( changed.box.maximum.z == ( extrude ? 160 : 128 ) );
        const auto *mesh = geometry::GeometryDocument_FindMesh( &ws.pDocument->geometry, { object } ); REQUIRE( mesh != nullptr );
        geometry::geometry_mesh_face_handle_t handle{}; REQUIRE( geometry::MeshSource_TryFindFace( mesh, { face }, &handle ) );
        CHECK( geometry::EditableMesh_FaceCount( &mesh->mesh ) == 10u );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); REQUIRE( MapWorkspace_HasMeshFace( &ws ) );
        CHECK( ws.selectedMeshFaceId == face ); CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, original.box.maximum );
        REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); REQUIRE( MapWorkspace_HasMeshFace( &ws ) );
        CHECK( ws.selectedMeshFaceId == face ); CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, changed.box.maximum );
        QTemporaryDir folder; REQUIRE( folder.isValid() ); const QString path = folder.filePath( QStringLiteral( "mesh-face.cymap" ) );
        REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK ); CHECK( ws.selectedMeshFaceId == face );
        REQUIRE( MapWorkspace_Open( &ws, path ).status == map_files_status_t::OK );
        CHECK( ws.selectedMeshFaceObject == 0 ); CHECK( ws.selectedMeshFaceId == 0 );
        MapWorkspace_SelectMeshFace( &ws, object, face ); REQUIRE( MapWorkspace_HasMeshFace( &ws ) );
        CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, changed.box.maximum );
        CHECK( geometry::EditableMesh_FaceCount( &geometry::GeometryDocument_FindMesh( &ws.pDocument->geometry, { object } )->mesh ) == 10u );
    }
}

TEST_CASE( "Mesh face operations enforce contextual eligibility and clear stale face selection", "[map][gui][views][mesh-face][eligibility]" )
{
    session_t session; auto &ws = session.workspace; const u64 object = CreateMeshFaceTestCube( ws );
    const u64 face = MeshFaceInDirection( ws, object, 2u ); MapWorkspace_SelectMeshFace( &ws, object, face );
    REQUIRE( MapWorkspace_RegisterCommands( &ws, &session.gui.commands ) == command_registry_status_t::OK );
    const auto enabled = [&]( const char *id ) { return ( EditorCommands_State( &session.gui.commands, StringView_FromCString( id ) ) & COMMAND_STATE_ENABLED ) != 0; };
    REQUIRE( enabled( "map.mesh.extrude" ) ); REQUIRE( enabled( "map.mesh.inset" ) );
    const auto *document = ws.pDocument; const auto revision = document->geometry.revision; const usize steps = EditorHistory_StepCount( &ws.history );
    for ( f64 invalid : { 0.0, -1.0, std::numeric_limits<f64>::infinity(), std::numeric_limits<f64>::quiet_NaN() } ) {
        CHECK_FALSE( MapWorkspace_ExtrudeMeshFace( &ws, invalid ) ); CHECK_FALSE( MapWorkspace_InsetMeshFace( &ws, invalid ) );
    }
    CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    ws.pDocument->bReadOnly = CY_TRUE; CHECK_FALSE( enabled( "map.mesh.extrude" ) ); CHECK_FALSE( MapWorkspace_CanEditMeshFace( &ws ) );
    CHECK_FALSE( MapWorkspace_ExtrudeMeshFace( &ws, 16 ) ); ws.pDocument->bReadOnly = CY_FALSE;
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS ); CHECK_FALSE( enabled( "map.mesh.inset" ) );
    CHECK_FALSE( MapWorkspace_InsetMeshFace( &ws, 8 ) ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
    // A mode switch deliberately retires the prior component pick; merely
    // returning to Faces must not invent a newly selected face.
    CHECK( ws.selectedMeshFaceId == 0 ); MapWorkspace_SelectMeshFace( &ws, object, face );
    REQUIRE( MapWorkspace_CanEditMeshFace( &ws ) );
    MapWorkspace_SetVisgroupHidden( &ws, map_visgroup_t::MESHES, CY_TRUE ); CHECK_FALSE( enabled( "map.mesh.extrude" ) );
    MapWorkspace_SetVisgroupHidden( &ws, map_visgroup_t::MESHES, CY_FALSE ); REQUIRE( MapWorkspace_CanEditMeshFace( &ws ) );
    ws.editPreview.bActive = CY_TRUE; CHECK_FALSE( MapWorkspace_CanEditMeshFace( &ws ) );
    CHECK_FALSE( MapWorkspace_ExtrudeMeshFace( &ws, 16 ) ); ws.editPreview = {};
    MapWorkspace_Select( &ws, object, MAP_SELECT_REPLACE ); CHECK( ws.selectedMeshFaceId == 0 ); CHECK_FALSE( enabled( "map.mesh.inset" ) );
    MapWorkspace_SelectMeshFace( &ws, object, face ); REQUIRE( MapWorkspace_HasMeshFace( &ws ) );
    MapWorkspace_SetSelection( &ws, nullptr, 0 ); CHECK( ws.selectedMeshFaceObject == 0 ); CHECK( ws.selectedMeshFaceId == 0 );
    MapWorkspace_SelectMeshFace( &ws, object, face ); REQUIRE( MapWorkspace_HasMeshFace( &ws ) );
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK ); CHECK( ws.selectedMeshFaceObject == 0 ); CHECK( ws.selectedMeshFaceId == 0 );
}

TEST_CASE( "Mesh face publication failures retain exact live state and allow retry", "[map][gui][views][mesh-face][allocation][atomic]" )
{
    for ( bool extrude : { false, true } ) {
        CAPTURE( extrude ); mesh_face_allocation_failure_t audit{};
        const allocator_t allocator{ &MeshFaceTestAllocate, nullptr, &MeshFaceTestFree, &audit };
        const auto prepare = []( session_t &session ) {
            auto &ws = session.workspace; const u64 object = CreateMeshFaceTestCube( ws );
            MapWorkspace_SelectMeshFace( &ws, object, MeshFaceInDirection( ws, object, 2u ) ); REQUIRE( MapWorkspace_HasMeshFace( &ws ) );
        };
        const auto edit = [&]( map_workspace_t &ws ) { return extrude ? MapWorkspace_ExtrudeMeshFace( &ws, 32 ) : MapWorkspace_InsetMeshFace( &ws, 16 ); };
        usize successfulCalls = 0;
        {
            session_t session; prepare( session ); const auto *original = session.gui.pAllocator; session.gui.pAllocator = &allocator;
            const bool changed = edit( session.workspace ); session.gui.pAllocator = original;
            REQUIRE( changed ); successfulCalls = audit.calls; REQUIRE( successfulCalls > 2 );
        }
        REQUIRE( audit.live == 0 );
        for ( usize failOn : { usize{ 1 }, usize{ 2 }, successfulCalls } ) {
            CAPTURE( failOn ); session_t session; prepare( session ); auto &ws = session.workspace;
            const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
            const auto *points = ws.wire.points.pData;
            const auto *sourceIds = ws.wire.pointSourceIds.pData;
            const auto *selection = ws.selection.ids.pData; const auto selectionRevision = ws.selection.revision;
            const u64 object = ws.selectedMeshFaceObject, face = ws.selectedMeshFaceId, nextId = ws.pDocument->nextId;
            const usize steps = EditorHistory_StepCount( &ws.history ); const auto token = UndoRedo_StateToken( ws.history.pUndo );
            audit.calls = 0; audit.failOn = failOn; const auto *original = session.gui.pAllocator; session.gui.pAllocator = &allocator;
            const bool changed = edit( ws ); session.gui.pAllocator = original;
            CHECK_FALSE( changed ); CHECK( audit.calls >= failOn ); CHECK( audit.live == 0 );
            CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.pDocument->nextId == nextId );
            CHECK( ws.wire.points.pData == points ); CHECK( ws.wire.pointSourceIds.pData == sourceIds );
            CHECK( ws.selection.ids.pData == selection ); CHECK( ws.selection.revision == selectionRevision );
            CHECK( ws.selectedMeshFaceObject == object ); CHECK( ws.selectedMeshFaceId == face ); CHECK( MapWorkspace_HasMeshFace( &ws ) );
            CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
            audit.calls = 0; audit.failOn = 0; REQUIRE( edit( ws ) ); CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
            REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CHECK( ws.selectedMeshFaceId == face );
            REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CHECK( ws.selectedMeshFaceId == face );
        }
        CHECK( audit.live == 0 );
    }
}

TEST_CASE( "History restores mesh face selection only while the current mode accepts faces", "[map][gui][views][mesh-face][history][mode]" )
{
    session_t session; auto &ws = session.workspace; const u64 object = CreateMeshFaceTestCube( ws );
    const u64 face = MeshFaceInDirection( ws, object, 2u ); MapWorkspace_SelectMeshFace( &ws, object, face );
    REQUIRE( MapWorkspace_ExtrudeMeshFace( &ws, 32 ) ); REQUIRE( MapWorkspace_HasMeshFace( &ws ) );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS );
    const auto checkObjectSelection = [&]() {
        CHECK( ws.elementMode == map_element_mode_t::OBJECTS ); CHECK( EditorSelection_Count( &ws.selection ) == 1u );
        CHECK( EditorSelection_At( &ws.selection, 0 ) == object );
        CHECK( ws.selectedMeshFaceObject == 0 ); CHECK( ws.selectedMeshFaceId == 0 );
        CHECK_FALSE( MapWorkspace_HasMeshFace( &ws ) ); CHECK_FALSE( MapWorkspace_HasBrushFace( &ws ) );
    };
    checkObjectSelection();
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); checkObjectSelection();
    CHECK( MapViews_SelectionGeometryBounds( &ws ).box.maximum.z == 128 );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); checkObjectSelection();
    CHECK( MapViews_SelectionGeometryBounds( &ws ).box.maximum.z == 160 );
    // The snapshots retain authored component identities. Returning to
    // Faces mode permits a subsequent history replay to restore that cap.
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); REQUIRE( MapWorkspace_HasMeshFace( &ws ) );
    CHECK( ws.selectedMeshFaceObject == object ); CHECK( ws.selectedMeshFaceId == face );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); REQUIRE( MapWorkspace_HasMeshFace( &ws ) );
    CHECK( ws.selectedMeshFaceObject == object ); CHECK( ws.selectedMeshFaceId == face );
}

TEST_CASE( "The mesh face inspector distinguishes the selected surface from its root and follows history", "[map][gui][panels][mesh-face][history]" )
{
    session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
    const QString material = QStringLiteral( "materials/dev/mesh_face_inspector.cymat" );
    settings.Choice( "editor.map.default_material", "materials/dev/mesh_face_inspector.cymat" );
    const u64 object = CreateMeshFaceTestCube( ws ), face = MeshFaceInDirection( ws, object, 2u );
    QString opened;
    command_desc_t database{}; database.pId = "assets.database"; database.pLabel = "Database View"; database.pContext = &opened;
    database.pfnExecute = []( void *context, const command_args_t &args ) {
        if ( args.nArgs != 1u ) { return command_result_t::FAILED; }
        *static_cast<QString *>( context ) = QString::fromUtf8( args.pArgs[0].pData, static_cast<qsizetype>( args.pArgs[0].cchLength ) );
        return command_result_t::OK;
    };
    REQUIRE( EditorCommands_Register( &session.gui.commands, &database, 1u ) == command_registry_status_t::OK );
    std::unique_ptr<QWidget> properties( MapProperties_Create( nullptr, &ws ) );
    auto *tree = properties->findChild<QTreeWidget *>( QStringLiteral( "MapPropertiesTree" ) );
    auto *summary = properties->findChild<QLabel *>( QStringLiteral( "MapSelectionSummary" ) );
    REQUIRE( tree != nullptr ); REQUIRE( summary != nullptr );
    const auto group = [&]() -> QTreeWidgetItem * {
        for ( int i = 0; i < tree->topLevelItemCount(); ++i ) {
            auto *item = tree->topLevelItem( i ); if ( item->text( 0 ) == QStringLiteral( "Selected mesh face" ) ) { return item; }
        }
        return nullptr;
    };
    const auto row = [&]( const char *name ) -> QTreeWidgetItem * {
        auto *selected = group(); if ( selected == nullptr ) { return nullptr; }
        for ( int i = 0; i < selected->childCount(); ++i ) {
            auto *item = selected->child( i ); if ( item->text( 0 ) == QString::fromLatin1( name ) ) { return item; }
        }
        return nullptr;
    };
    // Read canonical authored IDs independently of the viewport cache: the
    // inspector must expose persistent corner identities, not pool slots.
    const auto canonicalVertexIds = [&]() {
        const auto *mesh = geometry::GeometryDocument_FindMesh( &ws.pDocument->geometry, { object } ); REQUIRE( mesh != nullptr );
        geometry::mesh_source_description_t description{};
        REQUIRE( geometry::MeshSourceDescription_Init( &description, Allocator_GetSystem(), { object } ) == geometry::geometry_status_t::OK );
        REQUIRE( geometry::MeshSource_TryDescribe( mesh, &description ) == geometry::geometry_status_t::OK );
        QStringList ids;
        for ( usize i = 0; i < description.faces.nCount; ++i ) {
            const auto &surface = description.faces.pData[i]; if ( surface.sourceId.value != face ) { continue; }
            for ( usize j = 0; j < surface.cCorners; ++j ) {
                const auto vertex = description.corners.pData[surface.iFirstCorner + j].iVertex;
                ids.append( QString::number( description.vertices.pData[vertex].sourceId.value ) );
            }
        }
        geometry::MeshSourceDescription_Shutdown( &description ); REQUIRE( ids.size() == 4 );
        return ids.join( QStringLiteral( ", " ) );
    };
    CHECK( group() == nullptr ); MapWorkspace_SelectMeshFace( &ws, object, face ); REQUIRE( group() != nullptr );
    const QString originalIds = canonicalVertexIds();
    const auto inspect = [&]( const QString &vertexIds, const QString &bounds, int height ) {
        REQUIRE( row( "Face ID" ) != nullptr ); CHECK( row( "Face ID" )->text( 1 ) == QString::number( face ) );
        REQUIRE( row( "Vertex IDs" ) != nullptr ); CHECK( row( "Vertex IDs" )->text( 1 ) == vertexIds );
        REQUIRE( row( "Corners" ) != nullptr ); CHECK( row( "Corners" )->text( 1 ) == QStringLiteral( "4" ) );
        REQUIRE( row( "Normal" ) != nullptr ); CHECK( row( "Normal" )->text( 1 ) == QStringLiteral( "[0, 0, 1]" ) );
        REQUIRE( row( "Face bounds" ) != nullptr ); CHECK( row( "Face bounds" )->text( 1 ) == bounds );
        REQUIRE( row( "Smoothing groups" ) != nullptr ); CHECK( row( "Smoothing groups" )->text( 1 ) == QStringLiteral( "0" ) );
        REQUIRE( row( "Material" ) != nullptr ); CHECK( row( "Material" )->text( 1 ) == material );
        CHECK( summary->text().contains( QStringLiteral( "1 face selected" ) ) );
        CHECK( summary->text().contains( QStringLiteral( "Mesh size  X 128 \u00B7 Y 128 \u00B7 Z %1 u" ).arg( height ) ) );
        auto *cell = tree->itemWidget( row( "Material" ), 1 ); REQUIRE( cell != nullptr );
        auto *open = cell->findChild<QToolButton *>( QStringLiteral( "MapPropertiesOpen" ) ); REQUIRE( open != nullptr );
        CHECK( open->property( "databaseTarget" ).toString() == material ); open->click(); CHECK( opened == material );
    };
    inspect( originalIds, QStringLiteral( "[-64, -64, 128] to [64, 64, 128]" ), 128 );
    REQUIRE( MapWorkspace_ExtrudeMeshFace( &ws, 32 ) );
    const QString capIds = canonicalVertexIds(); CHECK( capIds != originalIds );
    CHECK( properties->findChild<QTreeWidget *>( QStringLiteral( "MapPropertiesTree" ) ) == tree );
    inspect( capIds, QStringLiteral( "[-64, -64, 160] to [64, 64, 160]" ), 160 );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    inspect( originalIds, QStringLiteral( "[-64, -64, 128] to [64, 64, 128]" ), 128 );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS ); CHECK( group() == nullptr );
    CHECK_FALSE( summary->text().contains( QStringLiteral( "face selected" ) ) );
    CHECK_FALSE( summary->text().contains( QStringLiteral( "Mesh size" ) ) );
}

TEST_CASE( "Face tools pick authored mesh surfaces directly from Objects mode in every pane", "[map][gui][views][mesh-face][picking][tool-parity]" )
{
    for ( const auto tool : { map_tool_t::EXTRUDE, map_tool_t::TEXTURE } ) { for ( int projection : { -1, 0, 1, 2 } ) {
        CAPTURE( static_cast<int>( tool ), projection ); session_t session; auto &ws = session.workspace;
        const u64 object = CreateMeshFaceTestCube( ws ); const bool camera = projection < 0;
        const auto axes = static_cast<map_ortho_axes_t>( std::max( 0, projection ) );
        const u32 u = axes == map_ortho_axes_t::FRONT ? 1u : 0u, v = axes == map_ortho_axes_t::TOP ? 1u : 2u;
        const u32 depth = camera ? 2u : 3u - u - v; const u64 face = MeshFaceInDirection( ws, object, depth );
        std::unique_ptr<QWidget> view( camera ? MapCameraView_Create( nullptr, &ws ) : MapOrthoView_Create( nullptr, &ws, axes ) );
        ShowAt( view.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        math::vec3d_t point{ 16, 8, 80 }; SetTestCoordinate( point, depth, depth == 2u ? 128.0 : 64.0 );
        QPointF screen;
        if ( camera ) { REQUIRE( MapCameraView_WorldToView( view.get(), point, &screen ) ); }
        else { screen = MapOrthoView_WorldToView( view.get(), { TestCoordinate( point, u ), TestCoordinate( point, v ) } ); }
        MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS );
        MapWorkspace_SetTool( &ws, tool ); REQUIRE( ws.elementMode == map_element_mode_t::OBJECTS );
        const auto revision = ws.pDocument->geometry.revision; const usize steps = EditorHistory_StepCount( &ws.history );
        HoverMeshFaceTestPoint( view.get(), screen ); CHECK( MapView_HoveredObject( view.get() ) == object );
        Click( view.get(), screen ); REQUIRE( MapWorkspace_HasMeshFace( &ws ) );
        CHECK( ws.elementMode == map_element_mode_t::FACES ); CHECK( ws.tool == tool );
        CHECK( ws.selectedMeshFaceObject == object ); CHECK( ws.selectedMeshFaceId == face );
        CHECK_FALSE( MapWorkspace_HasBrushFace( &ws ) ); CHECK_FALSE( ws.editPreview.bActive );
        Click( view.get(), { 3, 3 } ); CHECK_FALSE( MapWorkspace_HasMeshFace( &ws ) ); CHECK( ws.selectedMeshFaceId == 0 );
        CHECK( EditorSelection_Count( &ws.selection ) == 0u );
        CHECK( ws.pDocument->geometry.revision == revision ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    } }
}

TEST_CASE( "Unavailable component transforms never move selected brush or mesh roots in any viewport", "[map][gui][views][geometry-edit][component-transform-safety]" )
{
    for ( const bool mesh : { false, true } ) { for ( int projection : { -1, 0, 1, 2 } ) {
        CAPTURE( mesh, projection );
        session_t session; auto &ws = session.workspace;
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t bounds{};
        MapBounds_AddPoint( bounds, { -64, -64, 0 } ); MapBounds_AddPoint( bounds, { 64, 64, 128 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, bounds ) );
        const u64 id = EditorSelection_At( &ws.selection, 0 );
        if ( mesh ) { REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) ); }
        const bool camera = projection < 0;
        const auto axes = static_cast<map_ortho_axes_t>( std::max( 0, projection ) );
        const u32 u = axes == map_ortho_axes_t::FRONT ? 1u : 0u;
        const u32 v = axes == map_ortho_axes_t::TOP ? 1u : 2u;
        std::unique_ptr<QWidget> view( camera ? MapCameraView_Create( nullptr, &ws ) : MapOrthoView_Create( nullptr, &ws, axes ) );
        ShowAt( view.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        const u32 depthAxis = camera ? 2u : 3u - u - v;
        const u64 face = mesh ? MeshFaceInDirection( ws, id, depthAxis ) : BrushSideInDirection( ws, id, depthAxis, 1 );
        if ( mesh ) { MapWorkspace_SelectMeshFace( &ws, id, face ); }
        else { MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES ); MapWorkspace_SelectBrushFace( &ws, id, face ); }
        REQUIRE( ws.elementMode == map_element_mode_t::FACES );
        REQUIRE( ( mesh ? MapWorkspace_HasMeshFace( &ws ) : MapWorkspace_HasBrushFace( &ws ) ) );
        const auto *document = ws.pDocument;
        const auto revision = document->geometry.revision;
        const usize steps = EditorHistory_StepCount( &ws.history );
        const auto original = ObjectLineVertices( ws.wire, id );
        const auto originalBounds = MapWireframe_FindObject( ws.wire, id )->bounds;
        const auto selectionRevision = ws.selection.revision;
        const math::vec3d_t pivot{ 0, 0, 64 };
        f64 length{};
        if ( camera ) {
            const auto position = MapCameraView_Position( view.get() ), forward = MapCameraView_Forward( view.get() );
            const f64 depth = ( pivot.x - position.x ) * forward.x + ( pivot.y - position.y ) * forward.y + ( pivot.z - position.z ) * forward.z;
            const f64 fov = EditorSettings_Real( &session.gui.settings, "editor.camera.fov", 75.0 );
            const f64 focal = view->width() * 0.5 / std::tan( fov * 3.14159265358979323846 / 360.0 );
            length = std::max( depth, 1.0 ) * 72.0 / focal;
        } else { length = 64.0 / MapOrthoView_Zoom( view.get() ); }
        auto startWorld = pivot, endWorld = pivot;
        const u32 axis = camera ? 0u : u;
        SetTestCoordinate( startWorld, axis, length * 0.5 ); SetTestCoordinate( endWorld, axis, length * 0.5 + 32.0 );
        QPointF start, end;
        if ( camera ) {
            REQUIRE( MapCameraView_WorldToView( view.get(), startWorld, &start ) );
            REQUIRE( MapCameraView_WorldToView( view.get(), endWorld, &end ) );
            REQUIRE( MapCameraView_Pick( view.get(), start ) == id );
        } else {
            start = MapOrthoView_WorldToView( view.get(), { TestCoordinate( startWorld, u ), TestCoordinate( startWorld, v ) } );
            end = MapOrthoView_WorldToView( view.get(), { TestCoordinate( endWorld, u ), TestCoordinate( endWorld, v ) } );
        }
        REQUIRE( view->rect().contains( start.toPoint() ) ); REQUIRE( QLineF( start, end ).length() >= 3.0 );
        // All three transform tools formerly used the whole-object path in
        // Faces mode. Guard the real mouse path, including a surface-covered
        // location where a cached root gizmo used to be reachable.
        for ( const auto tool : { map_tool_t::TRANSLATE, map_tool_t::SCALE, map_tool_t::ROTATE } ) {
            CAPTURE( static_cast<int>( tool ) );
            MapWorkspace_SetTool( &ws, tool );
            DragMouse( view.get(), QEvent::MouseButtonPress, start );
            DragMouse( view.get(), QEvent::MouseMove, end );
            CHECK_FALSE( ws.editPreview.bActive );
            CHECK( ws.editPreview.transform.kind == map_transform_preview_kind_t::NONE );
            DragMouse( view.get(), QEvent::MouseButtonRelease, end );
            CHECK_FALSE( ws.editPreview.bActive );
            CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
            CHECK( EditorHistory_StepCount( &ws.history ) == steps );
            CHECK( ws.selection.revision == selectionRevision );
            CHECK( EditorSelection_At( &ws.selection, 0 ) == id );
            CHECK( ( mesh ? ws.selectedMeshFaceId == face : ws.selectedBrushFaceSide == face ) );
            CheckObjectVertices( ws.wire, id, original );
            const auto retained = MapWireframe_FindObject( ws.wire, id )->bounds;
            CheckPointClose( retained.box.minimum, originalBounds.box.minimum ); CheckPointClose( retained.box.maximum, originalBounds.box.maximum );
        }
    } }
}

TEST_CASE( "A mesh with one warped quad cannot publish a face pick from an otherwise planar surface", "[map][gui][views][mesh-face][picking][non-planar]" )
{
    session_t session; auto &ws = session.workspace; const u64 object = CreateMeshFaceTestCube( ws );
    const u64 topFace = MeshFaceInDirection( ws, object, 2u );
    const auto *source = geometry::GeometryDocument_FindMesh( &ws.pDocument->geometry, { object } ); REQUIRE( source != nullptr );
    geometry::mesh_source_description_t description{};
    REQUIRE( geometry::MeshSourceDescription_Init( &description, Allocator_GetSystem(), { object } ) == geometry::geometry_status_t::OK );
    REQUIRE( geometry::MeshSource_TryDescribe( source, &description ) == geometry::geometry_status_t::OK );
    bool warped = false;
    for ( usize i = 0; i < description.vertices.nCount; ++i ) {
        auto &vertex = description.vertices.pData[i];
        if ( vertex.position.x == -64 && vertex.position.y == -64 && vertex.position.z == 0 ) {
            vertex.position.z = 16; warped = true; break;
        }
    }
    REQUIRE( warped );
    geometry::mesh_source_t imported{};
    REQUIRE( geometry::MeshSource_TryBuild( &description, Allocator_GetSystem(), &imported ) == geometry::geometry_status_t::OK );
    REQUIRE( geometry::GeometryDocument_TryReplaceMesh( &ws.pDocument->geometry, &imported ) == geometry::geometry_status_t::OK );
    geometry::MeshSource_Shutdown( &imported ); geometry::MeshSourceDescription_Shutdown( &description );
    MapWorkspace_DocumentChanged( &ws );
    // The top face is still a valid planar quad. The query contract rejects
    // the entire untriangulatable mesh instead of returning a partial hit.
    const map_wire_face_t *top = nullptr;
    for ( usize i = 0; i < ws.wire.faces.nCount; ++i ) {
        const auto &face = ws.wire.faces.pData[i]; if ( face.id == object && face.faceId == topFace ) { top = &face; break; }
    }
    REQUIRE( top != nullptr ); REQUIRE( top->nIndices == 4u );
    for ( usize i = 0; i < top->nIndices; ++i ) { CHECK( ws.wire.points.pData[ws.wire.faceIndices.pData[top->iFirstIndex + i]].z == 128 ); }
    for ( bool camera : { false, true } ) {
        CAPTURE( camera ); MapWorkspace_Select( &ws, object, MAP_SELECT_REPLACE );
        std::unique_ptr<QWidget> view( camera ? MapCameraView_Create( nullptr, &ws ) : MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
        ShowAt( view.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        QPointF screen;
        if ( camera ) { REQUIRE( MapCameraView_WorldToView( view.get(), { 16, 8, 128 }, &screen ) ); }
        else { screen = MapOrthoView_WorldToView( view.get(), { 16, 8 } ); }
        MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE ); MapWorkspace_SetTool( &ws, map_tool_t::SELECT );
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
        CHECK( ( camera ? MapCameraView_Pick( view.get(), screen ) : MapOrthoView_Pick( view.get(), screen ) ) == 0 );
        HoverMeshFaceTestPoint( view.get(), screen ); CHECK( MapView_HoveredObject( view.get() ) == 0 );
        Click( view.get(), screen ); CHECK_FALSE( MapWorkspace_HasMeshFace( &ws ) ); CHECK_FALSE( MapWorkspace_HasBrushFace( &ws ) );
        CHECK( ws.selectedMeshFaceObject == 0 ); CHECK( ws.selectedMeshFaceId == 0 ); CHECK( EditorSelection_Count( &ws.selection ) == 0u );
    }
}


TEST_CASE( "Pan sensitivity applies to every view and stays captured until release", "[map][gui][views][camera][workflow-settings][pan-sensitivity]" )
{
    for ( int kind = 0; kind < 4; ++kind ) {
        CAPTURE( kind );
        session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, -64 } ); MapBounds_AddPoint( box, { 64, 64, 64 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
        const bool camera = kind == 3;
        std::unique_ptr<QWidget> view( camera ? MapCameraView_Create( nullptr, &ws ) :
            MapOrthoView_Create( nullptr, &ws, static_cast<map_ortho_axes_t>( kind ) ) );
        ShowAt( view.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        const auto center = [&]() -> math::vec3d_t {
            if ( camera ) { return MapCameraView_Position( view.get() ); }
            const auto p = MapOrthoView_ViewToWorld( view.get(), { 400, 300 } ); return { p.x(), p.y(), 0 };
        };
        const auto delta = []( math::vec3d_t a, math::vec3d_t b ) { return math::vec3d_t{ a.x - b.x, a.y - b.y, a.z - b.z }; };
        const auto twice = []( math::vec3d_t p ) { return math::vec3d_t{ p.x * 2, p.y * 2, p.z * 2 }; };
        const auto forward = camera ? MapCameraView_Forward( view.get() ) : math::vec3d_t{};
        const usize history = EditorHistory_StepCount( &ws.history ); const auto revision = ws.pDocument->geometry.revision;
        settings.Real( "editor.camera.pan_sensitivity", 1.0 );
        const QPointF start( 300, 200 ), end = start + QPointF( 50, 25 ), farther = start + QPointF( 100, 50 );
        const auto origin = center();
        DragButton( view.get(), QEvent::MouseButtonPress, start, Qt::MiddleButton );
        DragButton( view.get(), QEvent::MouseMove, end, Qt::MiddleButton );
        const auto single = delta( center(), origin ); REQUIRE( TestDistance( single, {} ) > 0.1 );
        settings.Real( "editor.camera.pan_sensitivity", 2.0 );
        CheckPointClose( delta( center(), origin ), single );
        DragButton( view.get(), QEvent::MouseMove, farther, Qt::MiddleButton );
        CheckPointClose( delta( center(), origin ), twice( single ) );
        DragButton( view.get(), QEvent::MouseButtonRelease, farther, Qt::MiddleButton );
        const auto nextOrigin = center();
        DragButton( view.get(), QEvent::MouseButtonPress, start, Qt::MiddleButton );
        DragButton( view.get(), QEvent::MouseMove, end, Qt::MiddleButton );
        CheckPointClose( delta( center(), nextOrigin ), twice( single ) );
        DragButton( view.get(), QEvent::MouseButtonRelease, end, Qt::MiddleButton );
        if ( camera ) { CheckPointClose( MapCameraView_Forward( view.get() ), forward ); }
        CHECK( ws.pDocument->geometry.revision == revision ); CHECK( EditorHistory_StepCount( &ws.history ) == history );
        CHECK_FALSE( ws.editPreview.bActive );
    }
}

TEST_CASE( "Scaled gizmos draw and pick the same endpoints and cancel changed captured controls", "[map][gui][views][geometry-edit][gizmos][workflow-settings][gizmo-scale]" )
{
    for ( const bool camera : { false, true } ) { for ( const f64 scale : { 0.5, 2.0 } ) {
        CAPTURE( camera, scale );
        session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
        settings.Real( "editor.viewport.gizmo_scale", scale );
        settings.Set( "editor.viewport.hover_highlight", false ); settings.Set( "editor.viewport.active_border", false );
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, -64 } ); MapBounds_AddPoint( box, { 64, 64, 64 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); MapWorkspace_SetSnapToGrid( &ws, CY_FALSE ); MapWorkspace_SetTool( &ws, map_tool_t::TRANSLATE );
        std::unique_ptr<QWidget> view( camera ? MapCameraView_Create( nullptr, &ws ) : MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
        ShowAt( view.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        view->setAttribute( Qt::WA_UnderMouse, true );
        const f64 length = camera ? CameraGizmoLength( view.get(), {}, session.gui.settings ) : 64.0 * scale / MapOrthoView_Zoom( view.get() );
        QPointF center, endpoint;
        if ( camera ) {
            REQUIRE( MapCameraView_WorldToView( view.get(), {}, &center ) );
            REQUIRE( MapCameraView_WorldToView( view.get(), { length, 0, 0 }, &endpoint ) );
        } else {
            center = MapOrthoView_WorldToView( view.get(), {} ); endpoint = center + QPointF( 64.0 * scale, 0 );
        }
        REQUIRE( view->rect().adjusted( 12, 12, -12, -12 ).contains( endpoint.toPoint() ) );
        HoverMouse( view.get(), { 20, 20 } ); const QImage idle = view->grab().toImage();
        const QColor axis = gui::EditorStyle_TokenColor( session.gui.style, "viewport.axis.x" );
        CHECK( HandleColorPixelsNear( idle, endpoint, axis, 3 ) > 0 );
        HoverMouse( view.get(), endpoint ); const QImage hovered = view->grab().toImage();
        const QColor hover = gui::EditorStyle_TokenColor( session.gui.style, "viewport.hover" );
        CHECK( HandleColorPixelsNear( hovered, endpoint, hover, 3 ) > HandleColorPixelsNear( idle, endpoint, hover, 3 ) );
        CHECK( view->cursor().shape() == Qt::SizeAllCursor );
        const QPointF direction = ( endpoint - center ) / QLineF( center, endpoint ).length();
        const QPointF end = endpoint + direction * 20;
        const usize history = EditorHistory_StepCount( &ws.history );
        DragMouse( view.get(), QEvent::MouseButtonPress, endpoint ); DragMouse( view.get(), QEvent::MouseMove, end );
        REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.transform.kind == map_transform_preview_kind_t::TRANSLATE );
        CHECK( std::abs( ws.editPreview.transform.delta.x ) > 0.01 ); CHECK( ws.editPreview.transform.delta.y == 0 ); CHECK( ws.editPreview.transform.delta.z == 0 );
        DragMouse( view.get(), QEvent::MouseButtonRelease, end );
        REQUIRE( EditorHistory_StepCount( &ws.history ) == history + 1 ); REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
        // The same endpoint starts a new drag; changing its projected size
        // discards the private preview, and release must not mutate geometry.
        DragMouse( view.get(), QEvent::MouseButtonPress, endpoint ); DragMouse( view.get(), QEvent::MouseMove, end );
        REQUIRE( ws.editPreview.bActive ); const auto revision = ws.pDocument->geometry.revision;
        const auto capturedDelta = ws.editPreview.transform.delta;
        settings.Real( "editor.viewport.gizmo_scale", scale ); // Same effective value still sends a notification.
        REQUIRE( ws.editPreview.bActive ); CheckPointClose( ws.editPreview.transform.delta, capturedDelta );
        if ( !camera ) {
            text_buffer_t text{}; REQUIRE( TextBuffer_Init( &text, Allocator_GetSystem() ) );
            REQUIRE( SettingsDocument_Write( &settings.store, &text ) == settings_document_status_t::OK );
            settings_document_t same{}, changed{};
            for ( auto *document : { &same, &changed } ) {
                REQUIRE( SettingsDocument_Init( document, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
                REQUIRE( SettingsDocument_Load( document, TextBuffer_View( &text ) ).status == settings_document_status_t::OK );
            }
            const auto *descriptor = EditorSettings_Find( &session.gui.settings, StringView_FromCString( "editor.viewport.gizmo_scale" ) );
            REQUIRE( descriptor != nullptr ); setting_value_t value{}; value.type = setting_type_t::REAL; value.flValue = scale == 0.5 ? 2.0 : 0.5;
            REQUIRE( Setting_WriteOverride( &changed, *descriptor, value, Setting_Default( *descriptor ) ) == settings_document_status_t::OK );
            EditorSettings_SetScope( &session.gui.settings, settings_scope_t::USER, &same );
            REQUIRE( ws.editPreview.bActive ); CheckPointClose( ws.editPreview.transform.delta, capturedDelta );
            DragMouse( view.get(), QEvent::MouseMove, end + direction * 10 ); REQUIRE( ws.editPreview.bActive );
            CHECK( std::abs( ws.editPreview.transform.delta.x ) > std::abs( capturedDelta.x ) );
            EditorSettings_SetScope( &session.gui.settings, settings_scope_t::USER, &changed );
            CHECK_FALSE( ws.editPreview.bActive );
            EditorSettings_SetScope( &session.gui.settings, settings_scope_t::USER, &settings.store );
        } else { settings.Real( "editor.viewport.gizmo_scale", scale == 0.5 ? 2.0 : 0.5 ); }
        CHECK_FALSE( ws.editPreview.bActive );
        DragMouse( view.get(), QEvent::MouseButtonRelease, end );
        CHECK( ws.pDocument->geometry.revision == revision ); CHECK( EditorHistory_StepCount( &ws.history ) == history + 1 );
        const auto restored = MapViews_SelectionGeometryBounds( &ws );
        CheckPointClose( restored.box.minimum, box.box.minimum ); CheckPointClose( restored.box.maximum, box.box.maximum );
    } }
}

TEST_CASE( "Frame margin adds breathing room without moving views until framing is requested", "[map][gui][views][camera][workflow-settings][frame-margin]" )
{
    for ( int kind = 0; kind < 4; ++kind ) {
        CAPTURE( kind );
        session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -200, -100, -150 } ); MapBounds_AddPoint( box, { 200, 100, 150 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
        const bool camera = kind == 3;
        std::unique_ptr<QWidget> view( camera ? MapCameraView_Create( nullptr, &ws ) :
            MapOrthoView_Create( nullptr, &ws, static_cast<map_ortho_axes_t>( kind ) ) );
        ShowAt( view.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        const auto coverage = [&]() {
            QRectF occupied; bool first = true;
            for ( int corner = 0; corner < 8; ++corner ) {
                const math::vec3d_t world{ corner & 1 ? 200.0 : -200.0, corner & 2 ? 100.0 : -100.0, corner & 4 ? 150.0 : -150.0 };
                QPointF point;
                if ( camera ) { REQUIRE( MapCameraView_WorldToView( view.get(), world, &point ) ); }
                else { const u32 u = kind == 1 ? 1u : 0u, v = kind == 0 ? 1u : 2u;
                    point = MapOrthoView_WorldToView( view.get(), { TestCoordinate( world, u ), TestCoordinate( world, v ) } ); }
                if ( first ) { occupied = QRectF( point, QSizeF() ); first = false; }
                else { occupied.setLeft( std::min( occupied.left(), point.x() ) ); occupied.setRight( std::max( occupied.right(), point.x() ) );
                    occupied.setTop( std::min( occupied.top(), point.y() ) ); occupied.setBottom( std::max( occupied.bottom(), point.y() ) ); }
            }
            return occupied;
        };
        const QRectF before = coverage(); const auto position = camera ? MapCameraView_Position( view.get() ) : math::vec3d_t{};
        const f64 zoom = camera ? 0.0 : MapOrthoView_Zoom( view.get() );
        settings.Real( "editor.camera.frame_margin", 2.0 ); CHECK( coverage() == before );
        if ( camera ) { CheckPointClose( MapCameraView_Position( view.get() ), position ); }
        else { CHECK( MapOrthoView_Zoom( view.get() ) == zoom ); }
        MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents(); const QRectF after = coverage();
        CHECK( after.width() < before.width() ); CHECK( after.height() < before.height() );
        CHECK( QRectF( view->rect() ).contains( after ) ); CHECK_FALSE( ws.editPreview.bActive );
    }
}

TEST_CASE( "Camera resize spheres remain outside move controls and preserve signed grid travel", "[map][gui][views][geometry-edit][select-resize][gizmos][camera][render][resize-move-separation]" )
{
    for ( const f64 scale : { 0.5, 1.0, 2.0 } ) { for ( const bool tiny : { true, false } ) {
        CAPTURE( scale, tiny );
        session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
        settings.Real( "editor.viewport.gizmo_scale", scale );
        settings.Set( "editor.viewport.hover_highlight", false ); settings.Set( "editor.viewport.active_border", false );
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK ); MapWorkspace_Frame( &ws, CY_FALSE );
        std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 1000, 760 );
        camera->setAttribute( Qt::WA_UnderMouse, true );
        const f64 half = tiny ? 8.0 : 256.0;
        map_bounds_t box{}; MapBounds_AddPoint( box, { -half, -half, -half } ); MapBounds_AddPoint( box, { half, half, half } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); MapWorkspace_SetGridSize( &ws, 64 );
        if ( !tiny ) { MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents(); }
        const u64 id = EditorSelection_At( &ws.selection, 0 );
        const usize initialAppliedSteps = EditorHistory_AppliedStepCount( &ws.history );
        const auto original = ObjectLineVertices( ws.wire, id ); const auto cameraPosition = MapCameraView_Position( camera.get() );
        const auto project = [&]( math::vec3d_t point ) {
            QPointF screen; REQUIRE( MapCameraView_WorldToView( camera.get(), point, &screen ) ); return screen;
        };
        const QPointF center = project( {} ); const f64 length = CameraGizmoLength( camera.get(), {}, session.gui.settings );
        f64 clearance = 24.0;
        for ( u32 axis = 0; axis < 3; ++axis ) {
            math::vec3d_t tip{}; SetTestCoordinate( tip, axis, length );
            clearance = std::max( clearance, QLineF( center, project( tip ) ).length() + 18.0 );
        }
        const QColor hover = gui::EditorStyle_TokenColor( session.gui.style, "viewport.hover" );
        for ( u32 axis = 0; axis < 3; ++axis ) {
            CAPTURE( axis );
            // Foreground movement remains available at every visible arrow,
            // including when the selection itself occupies only a few pixels.
            math::vec3d_t tip{}; SetTestCoordinate( tip, axis, length );
            const QPointF move = project( tip ); REQUIRE( QLineF( center, move ).length() >= 14.0 );
            REQUIRE( camera->rect().adjusted( 12, 12, -12, -12 ).contains( move.toPoint() ) );
            HoverMouse( camera.get(), { 20, 20 } ); const QImage idle = camera->grab().toImage();
            HoverMouse( camera.get(), move ); const QImage highlighted = camera->grab().toImage();
            CHECK( ChangedPixelsNear( idle, highlighted, move, 7 ) > 0 ); CHECK( camera->cursor().shape() == Qt::SizeAllCursor );
            auto movedTip = tip; SetTestCoordinate( movedTip, axis, TestCoordinate( movedTip, axis ) + 64.0 );
            const QPointF end = project( movedTip );
            const auto *moveDocument = ws.pDocument; const auto moveRevision = moveDocument->geometry.revision;
            DragMouse( camera.get(), QEvent::MouseButtonPress, move ); DragMouse( camera.get(), QEvent::MouseMove, end );
            REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.transform.kind == map_transform_preview_kind_t::TRANSLATE );
            CHECK_FALSE( ws.editPreview.transform.bResize );
            math::vec3d_t delta{}; SetTestCoordinate( delta, axis, 64.0 ); CheckPointClose( ws.editPreview.transform.delta, delta );
            CHECK( ws.pDocument == moveDocument ); CHECK( moveDocument->geometry.revision == moveRevision );
            CHECK( EditorHistory_AppliedStepCount( &ws.history ) == initialAppliedSteps );
            DragMouse( camera.get(), QEvent::MouseButtonRelease, end );
            CHECK( EditorHistory_AppliedStepCount( &ws.history ) == initialAppliedSteps + 1u );
            REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CheckObjectVertices( ws.wire, id, original );

            for ( const f64 sign : { -1.0, 1.0 } ) {
                CAPTURE( sign );
                math::vec3d_t anchor{}; SetTestCoordinate( anchor, axis, sign * half );
                const QPointF actual = project( anchor ); const QPointF offset = actual - center;
                const f64 distance = std::hypot( offset.x(), offset.y() ); REQUIRE( distance > 1e-6 );
                const QPointF direction = offset / distance;
                const QPointF sphere = center + direction * std::max( distance, clearance );
                REQUIRE( camera->rect().adjusted( 12, 12, -12, -12 ).contains( sphere.toPoint() ) );
                CHECK( QLineF( center, sphere ).length() >= clearance - 1e-6 );
                if ( distance >= clearance ) { CHECK( QLineF( actual, sphere ).length() < 1e-6 ); }
                HoverMouse( camera.get(), { 20, 20 } ); const QImage sphereIdle = camera->grab().toImage();
                HoverMouse( camera.get(), sphere ); const QImage sphereHovered = camera->grab().toImage();
                CHECK( ChangedPixelsNear( sphereIdle, sphereHovered, sphere, 7 ) > 0 );
                CHECK( HandleColorPixelsNear( sphereHovered, sphere, hover, 7 ) > HandleColorPixelsNear( sphereIdle, sphere, hover, 7 ) );
                CHECK( camera->cursor().shape() == Qt::SizeAllCursor );
                // The whole pickup offset belongs to the gesture. A three
                // pixel off-center press must neither move the boundary nor
                // be interpreted as movement of the central arrow.
                const QPointF press = sphere + QPointF( -direction.y(), direction.x() ) * 3.0;
                const usize steps = EditorHistory_StepCount( &ws.history );
                const auto *resizeDocument = ws.pDocument; const auto beforeRevision = resizeDocument->geometry.revision;
                DragMouse( camera.get(), QEvent::MouseButtonPress, press ); DragMouse( camera.get(), QEvent::MouseMove, press );
                CHECK_FALSE( ws.editPreview.bActive ); CheckObjectVertices( ws.wire, id, original );
                DragMouse( camera.get(), QEvent::MouseButtonRelease, press );
                CHECK( ws.pDocument == resizeDocument ); CHECK( resizeDocument->geometry.revision == beforeRevision );
                CHECK( EditorHistory_StepCount( &ws.history ) == steps );
                auto boundary = anchor; SetTestCoordinate( boundary, axis, sign * ( half + 64.0 ) );
                const QPointF resized = project( boundary ) + ( press - actual );
                auto expected = box;
                // The on-grid box grows by exactly 64. The tiny off-grid
                // boundary lands on the first outward 64-unit grid line.
                const f64 snappedBoundary = tiny ? 64.0 : 320.0;
                if ( sign < 0 ) { SetTestCoordinate( expected.box.minimum, axis, -snappedBoundary ); }
                else { SetTestCoordinate( expected.box.maximum, axis, snappedBoundary ); }
                DragMouse( camera.get(), QEvent::MouseButtonPress, press ); DragMouse( camera.get(), QEvent::MouseMove, resized );
                REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.transform.bResize );
                CHECK( ws.editPreview.transform.kind == map_transform_preview_kind_t::SCALE );
                CheckPointClose( ws.editPreview.bounds.box.minimum, expected.box.minimum ); CheckPointClose( ws.editPreview.bounds.box.maximum, expected.box.maximum );
                CHECK( ws.pDocument == resizeDocument ); CHECK( resizeDocument->geometry.revision == beforeRevision );
                CHECK( EditorHistory_StepCount( &ws.history ) == steps );
                CheckPointClose( MapCameraView_Position( camera.get() ), cameraPosition ); CheckObjectVertices( ws.wire, id, original );
                DragMouse( camera.get(), QEvent::MouseButtonRelease, resized );
                CHECK_FALSE( ws.editPreview.bActive ); CHECK( EditorHistory_AppliedStepCount( &ws.history ) == initialAppliedSteps + 1u );
                CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, expected.box.minimum );
                CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, expected.box.maximum );
                REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CheckObjectVertices( ws.wire, id, original );
                REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
                CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.minimum, expected.box.minimum );
                CheckPointClose( MapViews_SelectionGeometryBounds( &ws ).box.maximum, expected.box.maximum );
                REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CheckObjectVertices( ws.wire, id, original );
                CHECK( ws.tool == map_tool_t::SELECT ); CHECK( EditorSelection_At( &ws.selection, 0 ) == id );
            }
        }
    } }
}

TEST_CASE( "Thin brush and mesh outlines remain selectable at grazing camera angles", "[map][gui][views][selection][picking][grazing-edge]" )
{
    for ( const bool mesh : { false, true } ) { for ( const QPointF turn : { QPointF(), QPointF( 12, 8 ), QPointF( -12, -8 ) } ) {
        CAPTURE( mesh, turn.x(), turn.y() );
        session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        settings.Real( "editor.camera.look_sensitivity", 1.0 ); settings.Set( "editor.camera.invert_y", false );
        std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
        FacePositiveX( camera.get() ); const auto position = MapCameraView_Position( camera.get() );
        map_bounds_t box{}; MapBounds_AddPoint( box, { position.x + 512, position.y - 96, position.z - 1 } );
        MapBounds_AddPoint( box, { position.x + 768, position.y + 96, position.z + 1 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
        if ( mesh ) { REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) ); }
        MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
        if ( !turn.isNull() ) {
            const QPointF start( 400, 300 );
            DragButton( camera.get(), QEvent::MouseButtonPress, start, Qt::RightButton );
            DragButton( camera.get(), QEvent::MouseMove, start + turn, Qt::RightButton );
            DragButton( camera.get(), QEvent::MouseButtonRelease, start + turn, Qt::RightButton );
        }
        const auto *object = MapWireframe_FindObject( ws.wire, id ); REQUIRE( object != nullptr );
        QPointF top; bool first = true;
        for ( u32 i = 0; i < object->nPoints; ++i ) {
            QPointF point; REQUIRE( MapCameraView_WorldToView( camera.get(), ws.wire.points.pData[object->iFirstPoint + i], &point ) );
            if ( first || point.y() < top.y() ) { top = point; first = false; }
        }
        // Two pixels above the actual silhouette is outside every authored
        // triangle, but still inside the visible outline's click tolerance.
        const QPointF probe = top + QPointF( 0, -2 );
        REQUIRE( camera->rect().adjusted( 8, 8, -8, -8 ).contains( probe.toPoint() ) );
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
        CHECK( MapCameraView_Pick( camera.get(), probe ) == 0u );
        Click( camera.get(), probe ); CHECK( EditorSelection_Count( &ws.selection ) == 0 );
        for ( const auto mode : { map_element_mode_t::OBJECTS, map_element_mode_t::MESHES, map_element_mode_t::GROUPS } ) {
            CAPTURE( static_cast<int>( mode ) ); MapWorkspace_SetElementMode( &ws, mode );
            REQUIRE( MapCameraView_Pick( camera.get(), probe ) == id );
            const auto revision = ws.pDocument->geometry.revision; const usize steps = EditorHistory_StepCount( &ws.history );
            HoverMeshFaceTestPoint( camera.get(), probe ); CHECK( MapView_HoveredObject( camera.get() ) == id );
            Click( camera.get(), probe ); CHECK( MapWorkspace_IsSelected( &ws, id ) ); CHECK_FALSE( ws.editPreview.bActive );
            CHECK( ws.pDocument->geometry.revision == revision ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
            MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
        }
        MapWorkspace_SetTool( &ws, map_tool_t::TEXTURE ); CHECK( MapCameraView_Pick( camera.get(), probe ) == 0u );
        MapWorkspace_SetTool( &ws, map_tool_t::EXTRUDE ); CHECK( MapCameraView_Pick( camera.get(), probe ) == 0u );
        MapWorkspace_SetTool( &ws, map_tool_t::SELECT );
        MapWorkspace_SetVisgroupHidden( &ws, mesh ? map_visgroup_t::MESHES : map_visgroup_t::BRUSHES, CY_TRUE );
        CHECK( MapCameraView_Pick( camera.get(), probe ) == 0u ); HoverMeshFaceTestPoint( camera.get(), probe );
        CHECK( MapView_HoveredObject( camera.get() ) == 0u );
        MapWorkspace_SetVisgroupHidden( &ws, mesh ? map_visgroup_t::MESHES : map_visgroup_t::BRUSHES, CY_FALSE );
        map_bounds_t cordon{}; MapBounds_AddPoint( cordon, { position.x - 64, position.y - 64, position.z - 64 } );
        MapBounds_AddPoint( cordon, { position.x + 64, position.y + 64, position.z + 64 } );
        MapWorkspace_SetCordon( &ws, cordon ); MapWorkspace_SetCordonActive( &ws, CY_TRUE );
        CHECK( MapCameraView_Pick( camera.get(), probe ) == 0u );
    } }
}

TEST_CASE( "Vertex and Edge profiles never drag parent objects in any pane", "[map][gui][views][selection-mode]" )
{
    for ( const bool mesh : { false, true } ) {
        session_t session; auto &ws = session.workspace;
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, -64 } ); MapBounds_AddPoint( box, { 64, 64, 64 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
        if ( mesh ) { REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) ); }
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
        const usize steps = EditorHistory_StepCount( &ws.history );
        for ( int pane = 0; pane < 4; ++pane ) {
            CAPTURE( mesh, pane );
            std::unique_ptr<QWidget> view( pane == 3 ? MapCameraView_Create( nullptr, &ws ) :
                MapOrthoView_Create( nullptr, &ws, static_cast<map_ortho_axes_t>( pane ) ) );
            ShowAt( view.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
            const QPointF point = pane == 3 ? QPointF( 400, 300 ) : MapOrthoView_WorldToView( view.get(), { 64, 0 } );
            MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS );
            REQUIRE( ( pane == 3 ? MapCameraView_Pick( view.get(), point ) : MapOrthoView_Pick( view.get(), point ) ) == id );
            HoverMeshFaceTestPoint( view.get(), point ); REQUIRE( MapView_HoveredObject( view.get() ) == id );
            for ( const auto mode : { map_element_mode_t::VERTICES, map_element_mode_t::EDGES } ) {
                CAPTURE( static_cast<int>( mode ) ); MapWorkspace_SetElementMode( &ws, mode );
                map_mesh_edge_hit_t edgeHit{};
                const bool hasEdge = mode == map_element_mode_t::EDGES && mesh && ( pane == 3 ?
                    MapCameraView_PickMeshEdge( view.get(), point, &edgeHit ) : MapOrthoView_PickMeshEdge( view.get(), point, &edgeHit ) );
                map_mesh_vertex_hit_t vertexHit{};
                const bool hasVertex = mode == map_element_mode_t::VERTICES && mesh && ( pane == 3 ?
                    MapCameraView_PickMeshVertex( view.get(), point, &vertexHit ) : MapOrthoView_PickMeshVertex( view.get(), point, &vertexHit ) );
                CHECK( MapView_HoveredObject( view.get() ) == 0u );
                CHECK( ( pane == 3 ? MapCameraView_Pick( view.get(), point ) : MapOrthoView_Pick( view.get(), point ) ) == 0u );
                Click( view.get(), point ); HoverMeshFaceTestPoint( view.get(), point );
                CHECK( MapView_HoveredObject( view.get() ) == ( hasEdge || hasVertex ? id : 0u ) );
                DragMouse( view.get(), QEvent::MouseButtonPress, point );
                DragMouse( view.get(), QEvent::MouseMove, point + QPointF( 90, 35 ) );
                DragMouse( view.get(), QEvent::MouseButtonRelease, point + QPointF( 90, 35 ) );
                DragMouse( view.get(), QEvent::MouseButtonPress, { 10, 10 } );
                DragMouse( view.get(), QEvent::MouseMove, { 110, 110 } );
                DragMouse( view.get(), QEvent::MouseButtonRelease, { 110, 110 } );
                CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
                CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK_FALSE( ws.editPreview.bActive );
                CHECK( EditorSelection_Count( &ws.selection ) == 1u ); CHECK( MapWorkspace_IsSelected( &ws, id ) );
                MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS );
                HoverMeshFaceTestPoint( view.get(), point ); REQUIRE( MapView_HoveredObject( view.get() ) == id );
            }
        }
    }
}

TEST_CASE( "Meshes keeps direct move handles and cancels a drag when switching to Edges", "[map][gui][views][selection-mode]" )
{
    for ( const bool mesh : { false, true } ) {
        session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        settings.Integer( "editor.grid.size", 64 ); settings.Set( "editor.grid.snap", true );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, 0 } ); MapBounds_AddPoint( box, { 64, 64, 128 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
        if ( mesh ) { REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) ); }
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::MESHES );
        std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) ); ShowAt( top.get(), 800, 600 );
        MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        const QPointF start = MapOrthoView_WorldToView( top.get(), { 0, 0 } ) + QPointF( 52, 0 );
        const QPointF end = start + QPointF( 64 * MapOrthoView_Zoom( top.get() ), 0 );
        const usize steps = EditorHistory_AppliedStepCount( &ws.history );
        DragMouse( top.get(), QEvent::MouseButtonPress, start ); DragMouse( top.get(), QEvent::MouseMove, end );
        REQUIRE( ws.editPreview.bActive ); CHECK( ws.editPreview.transform.kind == map_transform_preview_kind_t::TRANSLATE );
        CheckPointClose( ws.editPreview.transform.delta, { 64, 0, 0 } );
        DragMouse( top.get(), QEvent::MouseButtonRelease, end );
        CHECK( EditorHistory_AppliedStepCount( &ws.history ) == steps + 1u );
        CheckPointClose( MapWireframe_FindObject( ws.wire, id )->bounds.box.minimum, { 0, -64, 0 } );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
        CheckPointClose( MapWireframe_FindObject( ws.wire, id )->bounds.box.minimum, box.box.minimum );
        REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
        DragMouse( top.get(), QEvent::MouseButtonPress, start ); DragMouse( top.get(), QEvent::MouseMove, end );
        REQUIRE( ws.editPreview.bActive ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES );
        CHECK_FALSE( ws.editPreview.bActive );
        DragMouse( top.get(), QEvent::MouseButtonRelease, end );
        CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
        CHECK( EditorHistory_AppliedStepCount( &ws.history ) == steps );
        CheckPointClose( MapWireframe_FindObject( ws.wire, id )->bounds.box.minimum, box.box.minimum );
    }
}

TEST_CASE( "Meshes excludes point entity picks and additive marquee carryover in all panes", "[map][gui][views][selection-mode]" )
{
    session_t session; auto &ws = session.workspace;
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, -64 } ); MapBounds_AddPoint( box, { 64, 64, 64 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 brush = EditorSelection_At( &ws.selection, 0 );
    u64 entity = 0;
    const math::vec3d_t origin{ 384, 384, 384 };
    REQUIRE( MapDocument_AddEntity( ws.pDocument, StringView_FromCString( "default" ),
        StringView_FromCString( "info_player_start" ), origin, &entity ) == map_status_t::OK );
    MapWorkspace_DocumentChanged( &ws );
    for ( int pane = 0; pane < 4; ++pane ) {
        CAPTURE( pane );
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS ); MapWorkspace_Select( &ws, entity, MAP_SELECT_REPLACE );
        std::unique_ptr<QWidget> view( pane == 3 ? MapCameraView_Create( nullptr, &ws ) :
            MapOrthoView_Create( nullptr, &ws, static_cast<map_ortho_axes_t>( pane ) ) );
        ShowAt( view.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_FALSE ); QCoreApplication::processEvents();
        QPointF point;
        if ( pane == 3 ) { REQUIRE( MapCameraView_WorldToView( view.get(), origin, &point ) ); }
        else { point = MapOrthoView_WorldToView( view.get(), { 384, 384 } ); }
        REQUIRE( ( pane == 3 ? MapCameraView_Pick( view.get(), point ) : MapOrthoView_Pick( view.get(), point ) ) == entity );
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::MESHES );
        CHECK( ( pane == 3 ? MapCameraView_Pick( view.get(), point ) : MapOrthoView_Pick( view.get(), point ) ) == 0u );
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
        Click( view.get(), point ); CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.pDocument == document );
        CHECK( document->geometry.revision == revision ); CHECK_FALSE( MapWorkspace_IsSelected( &ws, entity ) );
        MapWorkspace_Select( &ws, entity, MAP_SELECT_REPLACE );
        DragMouse( view.get(), QEvent::MouseButtonPress, { 5, 5 }, Qt::ShiftModifier );
        DragMouse( view.get(), QEvent::MouseMove, { 795, 595 }, Qt::ShiftModifier );
        DragMouse( view.get(), QEvent::MouseButtonRelease, { 795, 595 }, Qt::ShiftModifier );
        CHECK_FALSE( MapWorkspace_IsSelected( &ws, entity ) ); CHECK( MapWorkspace_IsSelected( &ws, brush ) );
        CHECK( EditorSelection_Count( &ws.selection ) == 1u );
    }
}

TEST_CASE( "Camera edge tolerance retains frontmost ordering and exact surface priority", "[map][gui][views][selection][picking][grazing-edge][occlusion]" )
{
    for ( const bool nearMesh : { false, true } ) {
        CAPTURE( nearMesh ); session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        settings.Real( "editor.camera.look_sensitivity", 1.0 ); settings.Set( "editor.camera.invert_y", false );
        std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 ); FacePositiveX( camera.get() );
        const auto p = MapCameraView_Position( camera.get() );
        const auto create = [&]( f64 scale, bool mesh ) {
            map_bounds_t box{}; MapBounds_AddPoint( box, { p.x + 512 * scale, p.y - 96 * scale, p.z - scale } );
            MapBounds_AddPoint( box, { p.x + 768 * scale, p.y + 96 * scale, p.z + scale } );
            REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
            if ( mesh ) { REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) ); }
            return id;
        };
        // Identical projected outlines, but different authored depth and
        // kinds. The nearer edge wins independent of the geometry list order.
        const u64 far = create( 2, !nearMesh ), near = create( 1, nearMesh );
        MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
        QPointF edge; REQUIRE( MapCameraView_WorldToView( camera.get(), { p.x + 512, p.y, p.z + 1 }, &edge ) );
        const QPointF probe = edge + QPointF( 0, -2 );
        REQUIRE( MapCameraView_Pick( camera.get(), probe ) == near );
        HoverMeshFaceTestPoint( camera.get(), probe ); CHECK( MapView_HoveredObject( camera.get() ) == near );
        Click( camera.get(), probe ); CHECK( MapWorkspace_IsSelected( &ws, near ) );
        MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE ); OnlyVisible( ws, { far } );
        CHECK( MapCameraView_Pick( camera.get(), probe ) == far );
        HoverMeshFaceTestPoint( camera.get(), probe ); CHECK( MapView_HoveredObject( camera.get() ) == far );
        Click( camera.get(), probe ); CHECK( MapWorkspace_IsSelected( &ws, far ) );
        MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE ); OnlyVisible( ws, { near, far } );
        map_bounds_t wall{}; MapBounds_AddPoint( wall, { p.x + 1800, p.y - 500, p.z - 64 } );
        MapBounds_AddPoint( wall, { p.x + 1816, p.y + 500, p.z + 64 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, wall ) ); const u64 exact = EditorSelection_At( &ws.selection, 0 );
        MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
        // The cursor actually intersects this farther wall. Nearby outlines
        // must not steal a deliberate surface-interior selection.
        CHECK( MapCameraView_Pick( camera.get(), probe ) == exact );
        HoverMeshFaceTestPoint( camera.get(), probe ); CHECK( MapView_HoveredObject( camera.get() ) == exact );
        Click( camera.get(), probe ); CHECK( MapWorkspace_IsSelected( &ws, exact ) );
    }
}

TEST_CASE( "Camera edge tolerance clips at the near plane and never selects empty primitive bounds", "[map][gui][views][selection][picking][grazing-edge][near-clip][silhouette]" )
{
    session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    settings.Real( "editor.camera.look_sensitivity", 1.0 ); settings.Set( "editor.camera.invert_y", false );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 ); FacePositiveX( camera.get() );
    const auto p = MapCameraView_Position( camera.get() );
    map_bounds_t clipped{}; MapBounds_AddPoint( clipped, { p.x - 16, p.y + 16, p.z - 32 } );
    MapBounds_AddPoint( clipped, { p.x + 256, p.y + 64, p.z + 32 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, clipped ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
    MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
    QPointF edge; REQUIRE( MapCameraView_WorldToView( camera.get(), { p.x + 128, p.y + 16, p.z + 32 }, &edge ) );
    const QPointF probe = edge + QPointF( 0, -2 );
    REQUIRE( camera->rect().adjusted( 8, 8, -8, -8 ).contains( probe.toPoint() ) );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES ); CHECK( MapCameraView_Pick( camera.get(), probe ) == 0u );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS ); CHECK( MapCameraView_Pick( camera.get(), probe ) == id );
    HoverMeshFaceTestPoint( camera.get(), probe ); CHECK( MapView_HoveredObject( camera.get() ) == id );
    Click( camera.get(), probe ); CHECK( MapWorkspace_IsSelected( &ws, id ) );
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    FacePositiveX( camera.get() ); const auto wedgePosition = MapCameraView_Position( camera.get() );
    map_primitive_desc_t wedge{}; wedge.kind = map_primitive_kind_t::WEDGE;
    wedge.wedgeCutAxis = 1; wedge.wedgeSlopeAxis = 2; // Triangular Y/Z cross-section, viewed along its X extrusion.
    wedge.bounds = { { wedgePosition.x + 512, wedgePosition.y - 64, wedgePosition.z },
        { wedgePosition.x + 704, wedgePosition.y + 64, wedgePosition.z + 128 } };
    REQUIRE( MapWorkspace_CreatePrimitive( &ws, wedge ) ); const u64 wedgeId = EditorSelection_At( &ws.selection, 0 );
    MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
    QPointF empty; REQUIRE( MapCameraView_WorldToView( camera.get(), { wedgePosition.x + 512, wedgePosition.y + 50, wedgePosition.z + 112 }, &empty ) );
    const auto *wedgeObject = MapWireframe_FindObject( ws.wire, wedgeId ); REQUIRE( wedgeObject != nullptr );
    QRectF projectedBounds; bool firstCorner = true;
    for ( u32 corner = 0; corner < 8; ++corner ) {
        const auto &lo = wedgeObject->bounds.box.minimum, &hi = wedgeObject->bounds.box.maximum;
        QPointF point; REQUIRE( MapCameraView_WorldToView( camera.get(), { corner & 1 ? hi.x : lo.x,
            corner & 2 ? hi.y : lo.y, corner & 4 ? hi.z : lo.z }, &point ) );
        if ( firstCorner ) { projectedBounds = QRectF( point, QSizeF() ); firstCorner = false; }
        else {
            projectedBounds.setLeft( std::min( projectedBounds.left(), point.x() ) ); projectedBounds.setRight( std::max( projectedBounds.right(), point.x() ) );
            projectedBounds.setTop( std::min( projectedBounds.top(), point.y() ) ); projectedBounds.setBottom( std::max( projectedBounds.bottom(), point.y() ) );
        }
    }
    REQUIRE( projectedBounds.contains( empty ) );
    f64 nearestWire = 1.0e6;
    for ( u32 edgeIndex = 0; edgeIndex < wedgeObject->nLines; ++edgeIndex ) {
        const auto &indices = ws.wire.lines.pData[wedgeObject->iFirstLine + edgeIndex];
        QPointF a, b; REQUIRE( MapCameraView_WorldToView( camera.get(), ws.wire.points.pData[indices.iA], &a ) );
        REQUIRE( MapCameraView_WorldToView( camera.get(), ws.wire.points.pData[indices.iB], &b ) );
        const QPointF direction = b - a; const f64 length2 = QPointF::dotProduct( direction, direction );
        const f64 along = length2 > 0 ? std::clamp( QPointF::dotProduct( empty - a, direction ) / length2, 0.0, 1.0 ) : 0.0;
        nearestWire = std::min( nearestWire, QLineF( empty, a + direction * along ).length() );
    }
    REQUIRE( nearestWire > MAP_VIEW_PICK_PIXELS );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES ); REQUIRE( MapCameraView_Pick( camera.get(), empty ) == 0u );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS );
    CHECK( MapCameraView_Pick( camera.get(), empty ) == 0u );
    HoverMeshFaceTestPoint( camera.get(), empty ); CHECK( MapView_HoveredObject( camera.get() ) == 0u );
    Click( camera.get(), empty ); CHECK( EditorSelection_Count( &ws.selection ) == 0u );
    map_bounds_t behind{}; MapBounds_AddPoint( behind, { wedgePosition.x - 256, wedgePosition.y - 64, wedgePosition.z - 32 } );
    MapBounds_AddPoint( behind, { wedgePosition.x - 128, wedgePosition.y + 64, wedgePosition.z + 32 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, behind ) ); const u64 back = EditorSelection_At( &ws.selection, 0 );
    MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE ); OnlyVisible( ws, { back } );
    CHECK( MapCameraView_Pick( camera.get(), { 400, 300 } ) == 0u );
}

TEST_CASE( "A grazing foreground edge is not occluded by a farther narrow surface", "[map][gui][views][selection][picking][grazing-edge][edge-occlusion-depth]" )
{
    session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    settings.Real( "editor.camera.look_sensitivity", 1.0 ); settings.Set( "editor.camera.invert_y", false );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 ); FacePositiveX( camera.get() );
    const auto p = MapCameraView_Position( camera.get() );
    map_bounds_t background{}; MapBounds_AddPoint( background, { p.x + 1024, p.y - 256, p.z - 1 } );
    MapBounds_AddPoint( background, { p.x + 1040, p.y + 256, p.z + 1 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, background ) ); const u64 back = EditorSelection_At( &ws.selection, 0 );
    map_primitive_desc_t quad{}; quad.kind = map_primitive_kind_t::QUAD; quad.axis = 2;
    quad.bounds = { { p.x + 256, p.y - 96, p.z }, { p.x + 512, p.y + 96, p.z } };
    REQUIRE( MapWorkspace_CreatePrimitive( &ws, quad ) ); const u64 front = EditorSelection_At( &ws.selection, 0 );
    MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
    QPointF edge; REQUIRE( MapCameraView_WorldToView( camera.get(), { p.x + 256, p.y, p.z }, &edge ) );
    const QPointF probe = edge + QPointF( 0, -2 );
    // The quad is coplanar with the edge ray, so exact triangle picking
    // misses it and reaches the background. The nearby cursor misses both.
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
    REQUIRE( MapCameraView_Pick( camera.get(), edge ) == back );
    REQUIRE( MapCameraView_Pick( camera.get(), probe ) == 0u );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS );
    REQUIRE( MapCameraView_Pick( camera.get(), probe ) == front );
    const auto revision = ws.pDocument->geometry.revision; const usize steps = EditorHistory_StepCount( &ws.history );
    HoverMeshFaceTestPoint( camera.get(), probe ); CHECK( MapView_HoveredObject( camera.get() ) == front );
    Click( camera.get(), probe ); CHECK( MapWorkspace_IsSelected( &ws, front ) ); CHECK_FALSE( ws.editPreview.bActive );
    CHECK( ws.pDocument->geometry.revision == revision ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
    // A genuinely nearer narrow face does hide that edge. Its own nearby
    // outline remains selectable, and hiding it restores the foreground quad.
    map_bounds_t blocker{}; MapBounds_AddPoint( blocker, { p.x + 128, p.y - 32, p.z - 0.25 } );
    MapBounds_AddPoint( blocker, { p.x + 144, p.y + 32, p.z + 0.25 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, blocker ) ); const u64 blockedBy = EditorSelection_At( &ws.selection, 0 );
    MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
    REQUIRE( MapCameraView_Pick( camera.get(), probe ) == 0u );
    REQUIRE( MapCameraView_Pick( camera.get(), edge ) == blockedBy );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS );
    CHECK( MapCameraView_Pick( camera.get(), probe ) == blockedBy );
    HoverMeshFaceTestPoint( camera.get(), probe ); CHECK( MapView_HoveredObject( camera.get() ) == blockedBy );
    Click( camera.get(), probe ); CHECK( MapWorkspace_IsSelected( &ws, blockedBy ) );
    MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE ); OnlyVisible( ws, { front, back } );
    CHECK( MapCameraView_Pick( camera.get(), probe ) == front );
}

TEST_CASE( "Multi-selection bounds gestures extend every root equally and capture the resize mode", "[map][gui][views][geometry-edit][individual-resize][resize-gesture]" )
{
    for ( const bool perspective : { false, true } ) { for ( const bool maximum : { false, true } ) { for ( const bool centered : { false, true } ) {
        CAPTURE( perspective, maximum, centered ); session_t session; auto &ws = session.workspace;
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK ); view_settings_t settings( &session.gui.settings );
        settings.Choice( "editor.map.resize_mode", "Each object" ); settings.Set( "editor.map.resize_from_center", false );
        u64 ids[4]{}; map_bounds_t boxes[4]{};
        for ( int i = 0; i < 4; ++i ) {
            boxes[i].bHas = CY_TRUE;
            boxes[i].box = { { 7.0 + i * 192, 11.0 + i * 32, 17 }, { 103.0 + i * 224, 139.0 + i * 32, 145.0 + i * 16 } };
            REQUIRE( MapWorkspace_CreateBox( &ws, boxes[i] ) ); ids[i] = EditorSelection_At( &ws.selection, 0 );
            if ( i >= 2 ) { REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) ); }
        }
        MapWorkspace_SetSelection( &ws, ids, 4 ); MapWorkspace_SetTool( &ws, map_tool_t::SELECT );
        MapWorkspace_SetGridSize( &ws, 64 ); MapWorkspace_SetSnapToGrid( &ws, CY_TRUE );
        std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &ws ) : MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
        ShowAt( view.get(), 1100, 800 ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        const auto bounds = MapViews_SelectionGeometryBounds( &ws ); auto boundary = MapBounds_Center( bounds );
        boundary.x = maximum ? bounds.box.maximum.x : bounds.box.minimum.x;
        auto destination = boundary; destination.x += maximum ? 91 : -91;
        QPointF start, end;
        if ( perspective ) {
            REQUIRE( MapCameraView_WorldToView( view.get(), boundary, &start ) );
            REQUIRE( MapCameraView_WorldToView( view.get(), destination, &end ) );
        } else {
            start = MapOrthoView_WorldToView( view.get(), { boundary.x, boundary.y } );
            end = MapOrthoView_WorldToView( view.get(), { destination.x, destination.y } );
        }
        REQUIRE( view->rect().adjusted( 8, 8, -8, -8 ).contains( start.toPoint() ) );
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
        const usize steps = EditorHistory_AppliedStepCount( &ws.history );
        const auto modifiers = centered ? Qt::ShiftModifier : Qt::NoModifier;
        DragMouse( view.get(), QEvent::MouseButtonPress, start, modifiers );
        CHECK_FALSE( ws.editPreview.bActive );
        // Preference changes affect the next gesture, never the captured one.
        settings.Choice( "editor.map.resize_mode", "Selection bounds" );
        DragMouse( view.get(), QEvent::MouseMove, end, modifiers );
        REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.transform.bResizeIndividually );
        CHECK( bool( ws.editPreview.transform.bResizeFromCenter ) == centered );
        CheckPointClose( ws.editPreview.transform.delta, { maximum ? 64.0 : -64.0, 0, 0 } );
        CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
        CHECK( EditorHistory_AppliedStepCount( &ws.history ) == steps );
        const auto checkResult = [&]() {
            for ( int i = 0; i < 4; ++i ) {
                const auto *object = MapWireframe_FindObject( ws.wire, ids[i] ); REQUIRE( object != nullptr );
                auto expected = boxes[i];
                if ( centered || !maximum ) { expected.box.minimum.x -= 64; }
                if ( centered || maximum ) { expected.box.maximum.x += 64; }
                CheckPointClose( object->bounds.box.minimum, expected.box.minimum );
                CheckPointClose( object->bounds.box.maximum, expected.box.maximum );
                CHECK( MapWorkspace_IsSelected( &ws, ids[i] ) );
            }
        };
        if ( qEnvironmentVariableIsSet( "CYPHER_QOL_CAPTURE" ) && maximum && !centered ) {
            view->grab().save( QDir::currentPath() + ( perspective ? QStringLiteral( "/artifacts/mason_qol_each_resize_camera.png" ) :
                QStringLiteral( "/artifacts/mason_qol_each_resize_drag.png" ) ) );
        }
        DragMouse( view.get(), QEvent::MouseButtonRelease, end, modifiers ); CHECK_FALSE( ws.editPreview.bActive );
        CHECK( EditorHistory_AppliedStepCount( &ws.history ) == steps + 1 ); checkResult();
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
        for ( int i = 0; i < 4; ++i ) {
            const auto *object = MapWireframe_FindObject( ws.wire, ids[i] ); REQUIRE( object != nullptr );
            CheckPointClose( object->bounds.box.minimum, boxes[i].box.minimum ); CheckPointClose( object->bounds.box.maximum, boxes[i].box.maximum );
        }
        REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); checkResult();
    } } }
}

TEST_CASE( "Invalid camera constraints cannot commit stale individual resize travel and can be repaired", "[map][gui][views][geometry-edit][individual-resize][resize-gesture][invalid-ray]" )
{
    for ( const bool centered : { false, true } ) { for ( const bool repair : { false, true } ) {
        CAPTURE( centered, repair ); session_t session; auto &ws = session.workspace;
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK ); view_settings_t settings( &session.gui.settings );
        settings.Choice( "editor.map.resize_mode", "Each object" ); settings.Set( "editor.map.resize_from_center", centered );
        settings.Integer( "editor.grid.size", 64 ); settings.Set( "editor.grid.snap", true );
        u64 ids[2]{}; std::vector<std::vector<math::vec3d_t>> originals;
        for ( int i = 0; i < 2; ++i ) {
            map_bounds_t box{}; MapBounds_AddPoint( box, { i == 0 ? -192.0 : 32.0, -192, -192 } );
            MapBounds_AddPoint( box, { i == 0 ? -32.0 : 192.0, 192, 192 } );
            REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); ids[i] = EditorSelection_At( &ws.selection, 0 );
            if ( i == 1 ) { REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) ); }
            originals.push_back( ObjectLineVertices( ws.wire, ids[i] ) );
        }
        MapWorkspace_SetSelection( &ws, ids, 2 ); MapWorkspace_SetTool( &ws, map_tool_t::SELECT );
        std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
        MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        const math::vec3d_t origin{ 0, 0, 192 }; QPointF start, end;
        REQUIRE( MapCameraView_WorldToView( camera.get(), origin, &start ) );
        REQUIRE( MapCameraView_WorldToView( camera.get(), { 0, 0, 256 }, &end ) );
        const auto position = MapCameraView_Position( camera.get() ), forward = MapCameraView_Forward( camera.get() );
        const f64 horizontal = std::hypot( forward.x, forward.y ); REQUIRE( horizontal > 0 );
        const math::vec3d_t right{ forward.y / horizontal, -forward.x / horizontal, 0 };
        const auto up = math::Vec3d_Cross( right, forward );
        const math::vec3d_t constraint{ origin.x - position.x, origin.y - position.y, 0 };
        const auto dot = []( math::vec3d_t a, math::vec3d_t b ) { return a.x * b.x + a.y * b.y + a.z * b.z; };
        const f64 denominator = dot( up, constraint ); REQUIRE( std::abs( denominator ) > 1e-6 );
        const f64 fov = EditorSettings_Real( &session.gui.settings, "editor.camera.fov", 75 );
        const f64 focal = camera->width() * 0.5 / std::tan( fov * 3.14159265358979323846 / 360.0 );
        const QPointF parallel( camera->width() * 0.5, camera->height() * 0.5 + focal * dot( forward, constraint ) / denominator );
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
        const auto token = UndoRedo_StateToken( ws.history.pUndo ); const usize steps = EditorHistory_AppliedStepCount( &ws.history );
        DragMouse( camera.get(), QEvent::MouseButtonPress, start ); DragMouse( camera.get(), QEvent::MouseMove, end );
        REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.transform.bResizeIndividually );
        CheckPointClose( ws.editPreview.transform.resizeSides, { 0, 0, 1 } );
        CheckPointClose( ws.editPreview.transform.delta, { 0, 0, 64 } );
        DragMouse( camera.get(), QEvent::MouseMove, parallel ); CHECK_FALSE( ws.editPreview.bActive );
        CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision );
        CHECK( EditorHistory_AppliedStepCount( &ws.history ) == steps );
        for ( int i = 0; i < 2; ++i ) { CheckObjectVertices( ws.wire, ids[i], originals[i] ); }
        if ( repair ) {
            DragMouse( camera.get(), QEvent::MouseMove, end ); REQUIRE( ws.editPreview.bActive );
            CheckPointClose( ws.editPreview.transform.delta, { 0, 0, 64 } );
        }
        // Releasing on the invalid ray must not publish hidden stale travel;
        // an explicit valid move above can repair the gesture before release.
        DragMouse( camera.get(), QEvent::MouseButtonRelease, repair ? end : parallel ); CHECK_FALSE( ws.editPreview.bActive );
        CHECK( EditorHistory_AppliedStepCount( &ws.history ) == steps + ( repair ? 1 : 0 ) );
        if ( repair ) { REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); }
        else {
            CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision );
            CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
        }
        for ( int i = 0; i < 2; ++i ) { CHECK( MapWorkspace_IsSelected( &ws, ids[i] ) ); CheckObjectVertices( ws.wire, ids[i], originals[i] ); }
    } }
}

TEST_CASE( "Individual inward resizing stops on a grid increment before the smallest object collapses", "[map][gui][views][geometry-edit][individual-resize][resize-gesture][snap]" )
{
    for ( const bool centered : { false, true } ) { for ( const bool bypass : { false, true } ) {
        CAPTURE( centered, bypass ); session_t session; auto &ws = session.workspace;
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK ); view_settings_t settings( &session.gui.settings );
        settings.Choice( "editor.map.resize_mode", "Each object" ); settings.Set( "editor.map.resize_from_center", centered );
        u64 ids[2]{};
        for ( int i = 0; i < 2; ++i ) {
            map_bounds_t box{}; MapBounds_AddPoint( box, { i * 128.0, 0, 0 } ); MapBounds_AddPoint( box, { i * 128.0 + 32 + i * 64, 128, 128 } );
            REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); ids[i] = EditorSelection_At( &ws.selection, 0 );
        }
        MapWorkspace_SetSelection( &ws, ids, 2 ); MapWorkspace_SetGridSize( &ws, 64 ); MapWorkspace_SetSnapToGrid( &ws, CY_TRUE );
        std::unique_ptr<QWidget> view( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) ); ShowAt( view.get(), 1000, 700 );
        MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        const auto start = MapOrthoView_WorldToView( view.get(), { 224, 64 } );
        const auto end = MapOrthoView_WorldToView( view.get(), { 124, 64 } );
        const auto modifiers = bypass ? Qt::ControlModifier : Qt::NoModifier;
        const usize steps = EditorHistory_AppliedStepCount( &ws.history );
        DragMouse( view.get(), QEvent::MouseButtonPress, start, modifiers ); DragMouse( view.get(), QEvent::MouseMove, end, modifiers );
        REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.transform.bResizeIndividually );
        const f64 travel = bypass ? ( centered ? -15.5 : -31.0 ) : 0.0;
        CheckPointClose( ws.editPreview.transform.delta, { travel, 0, 0 } );
        DragMouse( view.get(), QEvent::MouseButtonRelease, end, modifiers ); CHECK_FALSE( ws.editPreview.bActive );
        CHECK( EditorHistory_AppliedStepCount( &ws.history ) == steps + ( bypass ? 1 : 0 ) );
        for ( int i = 0; i < 2; ++i ) {
            const auto *object = MapWireframe_FindObject( ws.wire, ids[i] ); REQUIRE( object != nullptr );
            CHECK( object->bounds.box.minimum.x == Catch::Approx( i * 128.0 - ( centered ? travel : 0 ) ) );
            CHECK( object->bounds.box.maximum.x == Catch::Approx( i * 128.0 + 32 + i * 64 + travel ) );
        }
    } }
}

TEST_CASE( "Selection bounds mode retains collective proportional resizing", "[map][gui][views][geometry-edit][individual-resize][resize-gesture][group-resize]" )
{
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    view_settings_t settings( &session.gui.settings ); settings.Choice( "editor.map.resize_mode", "Selection bounds" );
    u64 ids[2]{};
    for ( int i = 0; i < 2; ++i ) {
        map_bounds_t box{}; MapBounds_AddPoint( box, { i * 128.0, 0, 0 } ); MapBounds_AddPoint( box, { i * 128.0 + 64, 64, 64 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); ids[i] = EditorSelection_At( &ws.selection, 0 );
    }
    MapWorkspace_SetSelection( &ws, ids, 2 ); MapWorkspace_SetGridSize( &ws, 64 );
    std::unique_ptr<QWidget> view( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) ); ShowAt( view.get(), 1000, 700 );
    MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
    const auto start = MapOrthoView_WorldToView( view.get(), { 192, 32 } ), end = MapOrthoView_WorldToView( view.get(), { 256, 32 } );
    DragMouse( view.get(), QEvent::MouseButtonPress, start ); DragMouse( view.get(), QEvent::MouseMove, end );
    REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.transform.bResize ); CHECK_FALSE( ws.editPreview.transform.bResizeIndividually );
    CheckPointClose( ws.editPreview.transform.factors, { 4.0 / 3.0, 1, 1 } );
    DragMouse( view.get(), QEvent::MouseButtonRelease, end );
    for ( int i = 0; i < 2; ++i ) {
        const auto *object = MapWireframe_FindObject( ws.wire, ids[i] ); REQUIRE( object != nullptr );
        CheckPointClose( object->bounds.box.minimum, { i * 128.0 * 4.0 / 3.0, 0, 0 } );
        CheckPointClose( object->bounds.box.maximum, { ( i * 128.0 + 64 ) * 4.0 / 3.0, 64, 64 } );
    }
}

TEST_CASE( "Four-object equal-distance resize workflow capture", "[.qol-individual-resize-screenshot]" )
{
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    view_settings_t settings( &session.gui.settings ); settings.Choice( "editor.map.resize_mode", "Each object" );
    settings.Integer( "editor.grid.size", 64 ); settings.Set( "editor.grid.snap", true );
    settings.Set( "editor.viewport.perspective.show_selection_bounds", true ); settings.Set( "editor.viewport.perspective.show_selection_dimensions", true );
    settings.Set( "editor.viewport.show_selection_bounds", true ); settings.Set( "editor.viewport.show_selection_dimensions", true );
    settings.Set( "editor.grid.show_surface_3d", true );
    u64 ids[4]{}; map_bounds_t boxes[4]{};
    for ( int i = 0; i < 4; ++i ) {
        boxes[i].bHas = CY_TRUE;
        boxes[i].box = { { ( i % 2 ) * 320.0, ( i / 2 ) * 256.0, 0 },
            { ( i % 2 ) * 320.0 + 192, ( i / 2 ) * 256.0 + 160, 128.0 + i * 16 } };
        REQUIRE( MapWorkspace_CreateBox( &ws, boxes[i] ) ); ids[i] = EditorSelection_At( &ws.selection, 0 );
        if ( i >= 2 ) { REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) ); }
    }
    MapWorkspace_SetSelection( &ws, ids, 4 );
    QWidget window; auto *layout = new QHBoxLayout( &window ); layout->setContentsMargins( 0, 0, 0, 0 ); layout->setSpacing( 4 );
    auto *properties = MapToolProperties_Create( &window, &ws ); properties->setFixedWidth( 320 ); layout->addWidget( properties );
    auto *views = MapViews_Create( &window, &ws ); layout->addWidget( views, 1 );
    ShowAt( &window, 1650, 1050 ); MapViews_SetArrangement( views, map_view_arrangement_t::HAMMER );
    MapViews_SetPaneView( views, 0, map_view_type_t::CAMERA, map_render_mode_t::FULLBRIGHT );
    MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
    std::error_code error; std::filesystem::create_directories( "artifacts", error ); REQUIRE_FALSE( error );
    REQUIRE( window.grab().save( QStringLiteral( "artifacts/mason_qol_each_resize_workspace.png" ) ) );
    auto *top = MapViews_PaneView( views, 1 ); REQUIRE( top != nullptr );
    const auto start = MapOrthoView_WorldToView( top, { 512, 208 } ), end = MapOrthoView_WorldToView( top, { 576, 208 } );
    DragMouse( top, QEvent::MouseButtonPress, start ); DragMouse( top, QEvent::MouseMove, end );
    REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.transform.bResizeIndividually );
    CheckPointClose( ws.editPreview.transform.delta, { 64, 0, 0 } );
    REQUIRE( window.grab().save( QStringLiteral( "artifacts/mason_qol_each_resize_workspace_drag.png" ) ) );
    DragMouse( top, QEvent::MouseButtonRelease, end );
    for ( int i = 0; i < 4; ++i ) {
        const auto *object = MapWireframe_FindObject( ws.wire, ids[i] ); REQUIRE( object != nullptr );
        CheckPointClose( object->bounds.box.minimum, boxes[i].box.minimum );
        auto maximum = boxes[i].box.maximum; maximum.x += 64; CheckPointClose( object->bounds.box.maximum, maximum );
    }
}

namespace
{
void UseClarityProbeTheme( session_t &session, const char *id, int dimensionSize = 12, int labelSize = 9 )
{
    // Deliberately distinguish a dark outline from ordinary antialiasing:
    // overlay backgrounds are magenta, while authoring canvases are white.
    // Any surviving geometry/text backdrop therefore leaves identifiable ink.
    const QString source = QStringLiteral(
        "@cykv 1\n@schema \"cypher.theme\" 2\n{ id = \"%1\" name = \"Clarity probe\" base = \"charcoal\" "
        "colors = { \"viewport.background.2d\" = \"#ffffff\" \"viewport.background.3d\" = \"#ffffff\" "
        "\"viewport.overlay.background\" = \"#c800c8\" \"viewport.face.world\" = \"#707070\" "
        "\"viewport.axis.x\" = \"#d04040\" \"viewport.axis.y\" = \"#40b040\" \"viewport.axis.z\" = \"#4070d0\" } "
        "fonts = { \"viewport.dimensions\" = { family = \"system-mono\" size = %2 weight = 500 } "
        "\"viewport.labels\" = { family = \"system-mono\" size = %3 weight = 500 } } }" )
        .arg( QString::fromUtf8( id ) ).arg( dimensionSize ).arg( labelSize );
    REQUIRE( gui::EditorGui_AddTheme( &session.gui, source ) == gui::editor_gui_status_t::OK );
    REQUIRE( gui::EditorGui_SelectTheme( &session.gui, App(), StringView_FromCString( id ) ) == gui::editor_gui_status_t::OK );
    MapWorkspace_Notify( &session.workspace, MAP_CHANGE_VIEW );
}

int BackdropProbePixels( const QImage &image, QRect region )
{
    region = region.intersected( image.rect() );
    int count = 0;
    for ( int y = region.top(); y <= region.bottom(); ++y ) { for ( int x = region.left(); x <= region.right(); ++x ) {
        const QColor color = image.pixelColor( x, y );
        // Include the antialiased edge of the probe rather than relying on
        // exact opaque RGB values; normal RGB controls cannot match this hue.
        count += color.red() - color.green() > 80 && color.blue() - color.green() > 80 ? 1 : 0;
    } }
    return count;
}

QImage SelectionDimensionPixels( QWidget *view, view_settings_t &settings, bool perspective )
{
    const char *path = perspective ? "editor.viewport.perspective.show_selection_dimensions" : "editor.viewport.show_selection_dimensions";
    settings.Set( path, false ); const QImage plain = view->grab().toImage();
    settings.Set( path, true ); const QImage labelled = view->grab().toImage();
    REQUIRE( plain.size() == labelled.size() );
    QImage changed( labelled.size(), QImage::Format_ARGB32 ); changed.setDevicePixelRatio( labelled.devicePixelRatio() );
    changed.fill( Qt::transparent );
    for ( int y = 0; y < labelled.height(); ++y ) { for ( int x = 0; x < labelled.width(); ++x ) {
        if ( labelled.pixel( x, y ) != plain.pixel( x, y ) ) { changed.setPixel( x, y, labelled.pixel( x, y ) ); }
    } }
    return changed;
}

int PaintedPixelCount( const QImage &image )
{
    int count = 0;
    for ( int y = 0; y < image.height(); ++y ) { for ( int x = 0; x < image.width(); ++x ) {
        count += qAlpha( image.pixel( x, y ) ) != 0 ? 1 : 0;
    } }
    return count;
}

int ExclusivePaintedPixelCount( const QImage &image, const QImage &other )
{
    REQUIRE( image.size() == other.size() );
    int count = 0;
    for ( int y = 0; y < image.height(); ++y ) { for ( int x = 0; x < image.width(); ++x ) {
        count += qAlpha( image.pixel( x, y ) ) != 0 && image.pixel( x, y ) != other.pixel( x, y ) ? 1 : 0;
    } }
    return count;
}

int LargestColorChangeNear( const QImage &before, const QImage &after, QPointF point )
{
    REQUIRE( before.size() == after.size() );
    const qreal ratio = before.devicePixelRatio();
    const QRect region( qRound( ( point.x() - 2 ) * ratio ), qRound( ( point.y() - 2 ) * ratio ), qRound( 5 * ratio ), qRound( 5 * ratio ) );
    REQUIRE( before.rect().contains( region ) );
    int difference = 0;
    for ( int y = region.top(); y <= region.bottom(); ++y ) { for ( int x = region.left(); x <= region.right(); ++x ) {
        const QColor a = before.pixelColor( x, y ), b = after.pixelColor( x, y );
        difference = std::max( { difference, std::abs( a.red() - b.red() ), std::abs( a.green() - b.green() ), std::abs( a.blue() - b.blue() ) } );
    } }
    return difference;
}
}

TEST_CASE( "Dimension glyphs use their own readable theme font without a dark outline", "[map][gui][views][render][selection-clarity][dimension-font]" )
{
    for ( const bool perspective : { false, true } ) {
        CAPTURE( perspective );
        session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -128, 0 } ); MapBounds_AddPoint( box, { 64, 128, 384 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
        DisableNameTestAids( settings ); settings.Set( "editor.viewport.perspective.center_axes", false );
        // Idle Block keeps the measurement captures free of control text.
        MapWorkspace_SetTool( &ws, map_tool_t::BLOCK );
        std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &ws ) : MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::FRONT ) );
        ShowAt( view.get(), 1000, 760 ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        const QFont readable = gui::EditorStyle_Font( session.gui.style, "viewport.dimensions" );
        CHECK( readable.pointSizeF() >= 12.0 );
        CHECK( readable.pointSizeF() > gui::EditorStyle_Font( session.gui.style, "viewport.labels" ).pointSizeF() );
        UseClarityProbeTheme( session, "dimensions_small", 12, 9 );
        const QImage small = SelectionDimensionPixels( view.get(), settings, perspective );
        const int smallInk = PaintedPixelCount( small ); REQUIRE( smallInk > 50 );
        CHECK( BackdropProbePixels( small, small.rect() ) == 0 );
        UseClarityProbeTheme( session, "dimensions_large", 24, 9 );
        const QImage large = SelectionDimensionPixels( view.get(), settings, perspective );
        // Both captures also recolor the same real measured side. Excluding
        // their common ink isolates the glyph growth from that fixed edge.
        const int smallFontInk = ExclusivePaintedPixelCount( small, large ); REQUIRE( smallFontInk > 20 );
        CHECK( ExclusivePaintedPixelCount( large, small ) > smallFontInk * 2 );
        CHECK( BackdropProbePixels( large, large.rect() ) == 0 );
        UseClarityProbeTheme( session, "labels_large_only", 12, 24 );
        const QImage labelsOnly = SelectionDimensionPixels( view.get(), settings, perspective );
        CHECK( labelsOnly == small );
        CHECK( EditorHistory_StepCount( &ws.history ) == 1u );
    }
}

TEST_CASE( "Selected wire and RGB controls draw without backdrop rims", "[map][gui][views][render][selection-clarity][gizmo-ink]" )
{
    for ( const bool perspective : { false, true } ) {
        CAPTURE( perspective );
        session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -128, -128, -128 } ); MapBounds_AddPoint( box, { 128, 128, 128 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
        DisableNameTestAids( settings ); settings.Set( "editor.viewport.perspective.center_axes", false );
        UseClarityProbeTheme( session, "wire_and_gizmo_probe" );
        std::unique_ptr<QWidget> view( perspective ? MapCameraView_Create( nullptr, &ws ) : MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
        ShowAt( view.get(), 1000, 760 ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        if ( perspective ) { MapCameraView_SetRenderMode( view.get(), map_render_mode_t::WIREFRAME ); }
        const QImage selected = view->grab().toImage();
        const qreal ratio = selected.devicePixelRatio();
        CHECK( BackdropProbePixels( selected, QRect( qRound( 32 * ratio ), qRound( 32 * ratio ),
            qRound( ( view->width() - 64 ) * ratio ), qRound( ( view->height() - 64 ) * ratio ) ) ) == 0 );
        QPointF center;
        if ( perspective ) { REQUIRE( MapCameraView_WorldToView( view.get(), {}, &center ) ); }
        else { center = MapOrthoView_WorldToView( view.get(), {} ); }
        const f64 length = perspective ? CameraGizmoLength( view.get(), {}, session.gui.settings ) : 0.0;
        for ( u32 axis = 0; axis < ( perspective ? 3u : 2u ); ++axis ) {
            CAPTURE( axis );
            QPointF tip;
            if ( perspective ) {
                math::vec3d_t world{}; SetTestCoordinate( world, axis, length ); REQUIRE( MapCameraView_WorldToView( view.get(), world, &tip ) );
            } else { tip = center + ( axis == 0 ? QPointF( 72, 0 ) : QPointF( 0, -72 ) ); }
            const QColor color = gui::EditorStyle_Color( session.gui.style, static_cast<gui::editor_style_color_t>( gui::STYLE_COLOR_AXIS_X + axis ) );
            CHECK( HandleColorPixelsNear( selected, tip, color, 8 ) > 0 );
        }
    }
}

TEST_CASE( "Whole object Fullbright selection keeps neutral bodies while face selection marks only its face", "[map][gui][views][render][selection-clarity][face-fill]" )
{
    session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -128, -128, 0 } ); MapBounds_AddPoint( box, { 128, 128, 256 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
    DisableNameTestAids( settings ); settings.Set( "editor.viewport.perspective.center_axes", false );
    UseClarityProbeTheme( session, "neutral_body_probe" ); MapWorkspace_SetTool( &ws, map_tool_t::BLOCK );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 1000, 760 );
    MapCameraView_SetRenderMode( camera.get(), map_render_mode_t::FULLBRIGHT ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
    const auto position = MapCameraView_Position( camera.get() );
    // The framed camera faces equally along X and Y. A top-face probe with
    // x == y lies on its projected vertical resize guide, although its world
    // position is away from the face center. This asymmetric interior point
    // stays clear of the projected control shafts and signed-axis guides.
    const math::vec3d_t topPoint{ -80, 64, 256 }, sidePoint{ position.x < 0 ? -128.0 : 128.0, 80, 80 };
    QPointF top, side; REQUIRE( MapCameraView_WorldToView( camera.get(), topPoint, &top ) ); REQUIRE( MapCameraView_WorldToView( camera.get(), sidePoint, &side ) );
    REQUIRE( MapCameraView_Pick( camera.get(), top ) == id ); REQUIRE( MapCameraView_Pick( camera.get(), side ) == id );
    MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE ); const QImage ordinary = camera->grab().toImage();
    MapWorkspace_Select( &ws, id, MAP_SELECT_REPLACE ); const QImage selected = camera->grab().toImage();
    // Small theme-driven emphasis remains permitted, but the previous 42%
    // yellow wash changes these neutral interiors by more than 40 channels.
    CHECK( LargestColorChangeNear( ordinary, selected, top ) <= 12 );
    CHECK( LargestColorChangeNear( ordinary, selected, side ) <= 12 );
    const u64 topSide = BrushSideInDirection( ws, id, 2u, 1 );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES ); MapWorkspace_SelectBrushFace( &ws, id, topSide );
    const QImage face = camera->grab().toImage();
    CHECK( LargestColorChangeNear( ordinary, face, top ) > LargestColorChangeNear( ordinary, selected, top ) );
    CHECK( LargestColorChangeNear( ordinary, face, top ) <= 65 );
    CHECK( LargestColorChangeNear( ordinary, face, side ) <= 12 );
    CHECK( EditorHistory_StepCount( &ws.history ) == 1u );
}

TEST_CASE( "Faces Select normal handle performs snapped push pull without switching tools", "[map][gui][views][geometry-edit][face][gizmo][selection-clarity][face-select-push-pull]" )
{
    for ( u32 axis = 0; axis < 3; ++axis ) { for ( const int sign : { -1, 1 } ) {
        CAPTURE( axis, sign );
        session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -183, -171, -157 } ); MapBounds_AddPoint( box, { 201, 213, 227 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
        const u64 side = BrushSideInDirection( ws, id, axis, sign ); const auto original = ObjectLineVertices( ws.wire, id );
        settings.Integer( "editor.grid.size", 64 ); settings.Set( "editor.grid.snap", true );
        std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 1000, 760 );
        MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES ); MapWorkspace_SelectBrushFace( &ws, id, side );
        REQUIRE( ws.tool == map_tool_t::SELECT );
        auto origin = MapBounds_Center( box ); SetTestCoordinate( origin, axis, TestCoordinate( sign > 0 ? box.box.maximum : box.box.minimum, axis ) );
        const auto measurement = MapViews_SelectionGeometryBounds( &ws ); REQUIRE( measurement.bHas );
        CHECK( TestCoordinate( measurement.box.minimum, axis ) == TestCoordinate( origin, axis ) );
        CHECK( TestCoordinate( measurement.box.maximum, axis ) == TestCoordinate( origin, axis ) );
        // The ordinary Select handle keeps the same forgiving shaft pickup
        // as Push/Pull. Preserve an off-center grab while two world grid cells
        // change perspective depth, rather than manufacturing screen travel.
        auto pickup = origin; SetTestCoordinate( pickup, axis, TestCoordinate( pickup, axis ) + sign * CameraGizmoLength( camera.get(), MapBounds_Center( box ), session.gui.settings ) * 0.6 );
        auto target = pickup; SetTestCoordinate( target, axis, TestCoordinate( target, axis ) + sign * 115.0 );
        QPointF start, end; REQUIRE( MapCameraView_WorldToView( camera.get(), pickup, &start ) ); REQUIRE( MapCameraView_WorldToView( camera.get(), target, &end ) );
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
        const usize applied = EditorHistory_AppliedStepCount( &ws.history );
        DragMouse( camera.get(), QEvent::MouseButtonPress, start ); DragMouse( camera.get(), QEvent::MouseMove, start );
        CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.pDocument == document );
        DragMouse( camera.get(), QEvent::MouseMove, end ); REQUIRE( ws.editPreview.bActive );
        CHECK( ws.editPreview.transform.kind == map_transform_preview_kind_t::NONE );
        map_bounds_t expected = box; SetTestCoordinate( sign > 0 ? expected.box.maximum : expected.box.minimum, axis,
            TestCoordinate( origin, axis ) + sign * 128.0 );
        CheckPointClose( ws.editPreview.bounds.box.minimum, expected.box.minimum ); CheckPointClose( ws.editPreview.bounds.box.maximum, expected.box.maximum );
        CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision ); CheckObjectVertices( ws.wire, id, original );
        CHECK( EditorHistory_AppliedStepCount( &ws.history ) == applied );
        DragMouse( camera.get(), QEvent::MouseButtonRelease, end ); CHECK_FALSE( ws.editPreview.bActive );
        CHECK( EditorHistory_AppliedStepCount( &ws.history ) == applied + 1u ); CHECK( ws.tool == map_tool_t::SELECT );
        CHECK( ws.selectedBrushFaceObject == id ); CHECK( ws.selectedBrushFaceSide == side );
        const auto *object = MapWireframe_FindObject( ws.wire, id ); REQUIRE( object != nullptr );
        CheckPointClose( object->bounds.box.minimum, expected.box.minimum ); CheckPointClose( object->bounds.box.maximum, expected.box.maximum );
        const auto committed = ObjectLineVertices( ws.wire, id );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CheckObjectVertices( ws.wire, id, original );
        CHECK( ws.selectedBrushFaceObject == id ); CHECK( ws.selectedBrushFaceSide == side );
        REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CheckObjectVertices( ws.wire, id, committed );
        CHECK( ws.tool == map_tool_t::SELECT ); CHECK( ws.selectedBrushFaceSide == side ); CHECK( ws.gridSize == 64 ); CHECK( ws.bSnapToGrid );
    } }
}

TEST_CASE( "RGB measurements recolor actual sides without drawing a multi-object envelope through empty space", "[map][gui][views][render][selection-clarity][measured-edges]" )
{
    SECTION( "All three box measurements coincide with authored geometry edges" ) {
        session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -128, -128, 0 } ); MapBounds_AddPoint( box, { 128, 128, 256 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
        DisableNameTestAids( settings ); settings.Set( "editor.viewport.perspective.center_axes", false );
        // Camera has no authoring manipulator. Changes on a real edge below
        // therefore come from its measurement rather than a coincident shaft.
        MapWorkspace_SetTool( &ws, map_tool_t::CAMERA );
        std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 1000, 760 );
        MapCameraView_SetRenderMode( camera.get(), map_render_mode_t::WIREFRAME ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        const QImage plain = camera->grab().toImage();
        settings.Set( "editor.viewport.perspective.show_selection_dimensions", true ); const QImage measured = camera->grab().toImage();
        const auto *object = MapWireframe_FindObject( ws.wire, id ); REQUIRE( object != nullptr );
        for ( u32 axis = 0; axis < 3; ++axis ) {
            CAPTURE( axis ); int matchingEdgePixels = 0;
            const QColor axisColor = gui::EditorStyle_Color( session.gui.style, static_cast<gui::editor_style_color_t>( gui::STYLE_COLOR_AXIS_X + axis ) );
            for ( u32 k = 0; k < object->nLines; ++k ) {
                const auto &line = ws.wire.lines.pData[object->iFirstLine + k]; const auto a = ws.wire.points.pData[line.iA], b = ws.wire.points.pData[line.iB];
                bool aligned = std::abs( TestCoordinate( a, axis ) - TestCoordinate( b, axis ) ) > 1;
                for ( u32 other = 0; other < 3; ++other ) {
                    if ( other != axis && std::abs( TestCoordinate( a, other ) - TestCoordinate( b, other ) ) > 1e-6 ) { aligned = false; }
                }
                if ( !aligned ) { continue; }
                for ( const f64 fraction : { 0.25, 0.75 } ) {
                    const auto point = math::Vec3d_Add( a, math::Vec3d_Scale( math::Vec3d_Subtract( b, a ), fraction ) );
                    QPointF screen; REQUIRE( MapCameraView_WorldToView( camera.get(), point, &screen ) );
                    if ( ChangedPixelsNear( plain, measured, screen, 2 ) > 0 ) {
                        matchingEdgePixels += HandleColorPixelsNear( measured, screen, axisColor, 2 );
                    }
                }
            }
            CHECK( matchingEdgePixels > 0 );
        }
    }
    SECTION( "Separated selections show numbers without inventing their combined X side" ) {
        session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t left{}, right{};
        MapBounds_AddPoint( left, { -192, -128, 0 } ); MapBounds_AddPoint( left, { -64, 128, 128 } );
        MapBounds_AddPoint( right, { 64, -128, 0 } ); MapBounds_AddPoint( right, { 192, 128, 128 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, left ) ); const u64 a = EditorSelection_At( &ws.selection, 0 );
        REQUIRE( MapWorkspace_CreateBox( &ws, right ) ); const u64 b = EditorSelection_At( &ws.selection, 0 );
        const u64 ids[]{ a, b }; MapWorkspace_SetSelection( &ws, ids, 2 );
        DisableNameTestAids( settings ); MapWorkspace_SetTool( &ws, map_tool_t::CAMERA );
        std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
        ShowAt( top.get(), 1000, 760 ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        const QImage plain = top->grab().toImage();
        settings.Set( "editor.viewport.show_selection_dimensions", true ); const QImage measured = top->grab().toImage();
        CHECK( measured != plain ); // The selection still has useful numbers.
        for ( const f64 y : { -128.0, 128.0 } ) {
            const QPointF gap = MapOrthoView_WorldToView( top.get(), { 0, y } );
            CHECK( MapOrthoView_Pick( top.get(), gap ) == 0u );
            CHECK( ChangedPixelsNear( plain, measured, gap, 3 ) == 0 );
        }
        CHECK( EditorSelection_Count( &ws.selection ) == 2u ); CHECK( EditorHistory_StepCount( &ws.history ) == 2u );
    }
}

TEST_CASE( "Mesh edges select persistent components in every orthographic pane without transforming roots", "[map][gui][views][mesh-edge]" )
{
    for ( int pane = 0; pane < 3; ++pane ) {
        CAPTURE( pane ); session_t session; auto &ws = session.workspace;
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, -64 } ); MapBounds_AddPoint( box, { 64, 64, 64 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
        REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) );
        std::unique_ptr<QWidget> view( MapOrthoView_Create( nullptr, &ws, static_cast<map_ortho_axes_t>( pane ) ) );
        ShowAt( view.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES );
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
        const usize steps = EditorHistory_StepCount( &ws.history ); const auto original = ObjectLineVertices( ws.wire, id );
        const QPointF right = MapOrthoView_WorldToView( view.get(), { 64, 0 } );
        const QPointF left = MapOrthoView_WorldToView( view.get(), { -64, 0 } );
        map_mesh_edge_hit_t a{}, b{};
        REQUIRE( MapOrthoView_PickMeshEdge( view.get(), right, &a ) ); REQUIRE( a.object == id );
        REQUIRE( MapOrthoView_PickMeshEdge( view.get(), left, &b ) ); REQUIRE( b.object == id );
        CHECK( a.edge.a.value < a.edge.b.value ); CHECK( b.edge.a.value < b.edge.b.value );
        CHECK( ( a.edge.a.value != b.edge.a.value || a.edge.b.value != b.edge.b.value ) );
        CHECK( MapOrthoView_Pick( view.get(), right ) == 0u );
        map_mesh_edge_hit_t nearby{};
        REQUIRE( MapOrthoView_PickMeshEdge( view.get(), right + QPointF( 2, 0 ), &nearby ) );
        CHECK( nearby.edge.a.value == a.edge.a.value ); CHECK( nearby.edge.b.value == a.edge.b.value );
        CHECK_FALSE( MapOrthoView_PickMeshEdge( view.get(), right + QPointF( 8, 0 ), &nearby ) );
        CHECK_FALSE( MapOrthoView_PickMeshEdge( view.get(), { -1, -1 }, &nearby ) );
        const auto *source = geometry::GeometryDocument_FindMesh( &document->geometry, { id } ); REQUIRE( source != nullptr );
        for ( const auto endpoint : { a.edge.a, a.edge.b } ) {
            geometry::geometry_mesh_vertex_handle_t vertex{};
            REQUIRE( geometry::MeshSource_TryFindVertex( source, endpoint, &vertex ) );
            const auto *record = GenerationPool_Get( &source->mesh.vertices, vertex ); REQUIRE( record != nullptr );
            CHECK( std::abs( record->position.x ) == 64 ); CHECK( std::abs( record->position.y ) == 64 ); CHECK( std::abs( record->position.z ) == 64 );
        }
        HoverMeshFaceTestPoint( view.get(), right ); CHECK( MapView_HoveredObject( view.get() ) == id );
        Click( view.get(), right ); REQUIRE( ws.meshSelection.edges.nCount == 1u );
        CHECK( geometry::MeshSelection_HasEdge( &ws.meshSelection, a.edge ) );
        Click( view.get(), left, Qt::ShiftModifier ); REQUIRE( ws.meshSelection.edges.nCount == 2u );
        CHECK( geometry::MeshSelection_HasEdge( &ws.meshSelection, b.edge ) );
        Click( view.get(), right, Qt::ControlModifier ); REQUIRE( ws.meshSelection.edges.nCount == 1u );
        CHECK_FALSE( geometry::MeshSelection_HasEdge( &ws.meshSelection, a.edge ) );
        DragMouse( view.get(), QEvent::MouseButtonPress, left );
        DragMouse( view.get(), QEvent::MouseMove, left + QPointF( 100, 40 ) );
        DragMouse( view.get(), QEvent::MouseButtonRelease, left + QPointF( 100, 40 ) );
        CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CheckObjectVertices( ws.wire, id, original );
        QKeyEvent cancel( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( view.get(), &cancel );
        CHECK( ws.tool == map_tool_t::NONE ); CHECK( ws.meshSelection.edges.nCount == 1u );
        CHECK_FALSE( MapOrthoView_PickMeshEdge( view.get(), left, &nearby ) );
        QKeyEvent clear( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( view.get(), &clear );
        CHECK( ws.meshSelection.edges.nCount == 0u ); CHECK( EditorSelection_Count( &ws.selection ) == 0u );
        CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    }
}

TEST_CASE( "Camera mesh edges respect physical occlusion in every preview mode", "[map][gui][views][mesh-edge][occlusion]" )
{
    for ( const auto mode : { map_render_mode_t::WIREFRAME, map_render_mode_t::SHADED, map_render_mode_t::FULLBRIGHT, map_render_mode_t::NORMALS } ) {
        CAPTURE( static_cast<int>( mode ) ); session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        settings.Real( "editor.camera.look_sensitivity", 1.0 ); settings.Set( "editor.camera.invert_y", false );
        std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 ); FacePositiveX( camera.get() );
        MapCameraView_SetRenderMode( camera.get(), mode ); const auto p = MapCameraView_Position( camera.get() );
        const auto make = [&]( f64 scale, bool convert ) {
            map_bounds_t box{}; MapBounds_AddPoint( box, { p.x + 512 * scale, p.y - 96 * scale, p.z - 96 * scale } );
            MapBounds_AddPoint( box, { p.x + 768 * scale, p.y + 96 * scale, p.z + 96 * scale } );
            REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
            if ( convert ) { REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) ); } return id;
        };
        const u64 far = make( 2, true ), near = make( 1, true );
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES );
        QPointF edge; REQUIRE( MapCameraView_WorldToView( camera.get(), { p.x + 512, p.y, p.z + 96 }, &edge ) );
        const QPointF point = edge + QPointF( 0, -2 ); map_mesh_edge_hit_t hit{};
        REQUIRE( MapCameraView_PickMeshEdge( camera.get(), point, &hit ) ); CHECK( hit.object == near );
        CHECK( MapCameraView_Pick( camera.get(), point ) == 0u );
        HoverMeshFaceTestPoint( camera.get(), point ); CHECK( MapView_HoveredObject( camera.get() ) == near );
        Click( camera.get(), point ); REQUIRE( ws.meshSelection.edges.nCount == 1u );
        CHECK( geometry::MeshSelection_HasEdge( &ws.meshSelection, hit.edge ) );
        OnlyVisible( ws, { far } ); REQUIRE( MapCameraView_PickMeshEdge( camera.get(), point, &hit ) ); CHECK( hit.object == far );
        OnlyVisible( ws, { near, far } ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS );
        // An ordinary brush is not an Edge target, but still physically blocks
        // source-mesh edges. Category filtering must not erase occlusion.
        map_bounds_t wall{}; MapBounds_AddPoint( wall, { p.x + 256, p.y - 200, p.z - 200 } );
        MapBounds_AddPoint( wall, { p.x + 272, p.y + 200, p.z + 200 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, wall ) ); const u64 blocker = EditorSelection_At( &ws.selection, 0 );
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES );
        CHECK_FALSE( MapCameraView_PickMeshEdge( camera.get(), point, &hit ) );
        HoverMeshFaceTestPoint( camera.get(), point ); CHECK( MapView_HoveredObject( camera.get() ) == 0u );
        OnlyVisible( ws, { near, far } ); REQUIRE( MapCameraView_PickMeshEdge( camera.get(), point, &hit ) ); CHECK( hit.object == near );
        OnlyVisible( ws, { blocker } ); CHECK_FALSE( MapCameraView_PickMeshEdge( camera.get(), point, &hit ) );
    }
}

TEST_CASE( "Camera mesh edge picking clips crossing edges and excludes geometry behind the camera", "[map][gui][views][mesh-edge][near-clip]" )
{
    session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    settings.Real( "editor.camera.look_sensitivity", 1.0 ); settings.Set( "editor.camera.invert_y", false );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 ); FacePositiveX( camera.get() );
    const auto p = MapCameraView_Position( camera.get() );
    map_bounds_t box{}; MapBounds_AddPoint( box, { p.x - 16, p.y + 16, p.z - 32 } ); MapBounds_AddPoint( box, { p.x + 256, p.y + 64, p.z + 32 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES );
    QPointF edge; REQUIRE( MapCameraView_WorldToView( camera.get(), { p.x + 128, p.y + 16, p.z + 32 }, &edge ) );
    map_mesh_edge_hit_t hit{}; REQUIRE( MapCameraView_PickMeshEdge( camera.get(), edge + QPointF( 0, -2 ), &hit ) ); CHECK( hit.object == id );
    OnlyVisible( ws, {} ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS );
    map_bounds_t behind{}; MapBounds_AddPoint( behind, { p.x - 128, p.y - 32, p.z - 32 } ); MapBounds_AddPoint( behind, { p.x - 64, p.y + 32, p.z + 32 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, behind ) ); REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES );
    CHECK_FALSE( MapCameraView_PickMeshEdge( camera.get(), { 400, 300 }, &hit ) );
}

TEST_CASE( "A failed camera edge occlusion query preserves the previous component selection", "[map][gui][views][mesh-edge][allocation][atomic]" )
{
    session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    settings.Real( "editor.camera.look_sensitivity", 1.0 ); settings.Set( "editor.camera.invert_y", false );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 ); FacePositiveX( camera.get() );
    const auto p = MapCameraView_Position( camera.get() );
    map_bounds_t box{}; MapBounds_AddPoint( box, { p.x + 512, p.y - 96, p.z - 96 } ); MapBounds_AddPoint( box, { p.x + 768, p.y + 96, p.z + 96 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES );
    QPointF point; REQUIRE( MapCameraView_WorldToView( camera.get(), { p.x + 512, p.y, p.z + 96 }, &point ) );
    map_mesh_edge_hit_t hit{}; REQUIRE( MapCameraView_PickMeshEdge( camera.get(), point, &hit ) );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, hit.object, hit.edge ) );
    const auto *edges = ws.meshSelection.edges.pData; const auto selectionRevision = ws.selection.revision;
    const usize steps = EditorHistory_StepCount( &ws.history ); const auto *document = ws.pDocument;
    mesh_face_allocation_failure_t audit{ 0, 1, 0 };
    const allocator_t allocator{ &MeshFaceTestAllocate, nullptr, &MeshFaceTestFree, &audit };
    const auto *original = ws.pDocument->pAllocator;
    ws.pDocument->pAllocator = &allocator;
    Click( camera.get(), point );
    ws.pDocument->pAllocator = original;
    CHECK( audit.calls > 0u ); CHECK( audit.live == 0u );
    CHECK( ws.meshSelection.edges.pData == edges ); CHECK( ws.meshSelection.edges.nCount == 1u );
    CHECK( geometry::MeshSelection_HasEdge( &ws.meshSelection, hit.edge ) ); CHECK( ws.selection.revision == selectionRevision );
    CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK_FALSE( ws.editPreview.bActive );
}

TEST_CASE( "Mesh vertices select authored components in every orthographic pane without editing roots", "[map][gui][views][mesh-vertex]" )
{
    for ( int pane = 0; pane < 3; ++pane ) {
        CAPTURE( pane ); session_t session; auto &ws = session.workspace;
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, -64 } ); MapBounds_AddPoint( box, { 64, 64, 64 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
        REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) );
        std::unique_ptr<QWidget> view( MapOrthoView_Create( nullptr, &ws, static_cast<map_ortho_axes_t>( pane ) ) );
        ShowAt( view.get(), 800, 600 ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES );
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
        const usize steps = EditorHistory_StepCount( &ws.history ); const auto original = ObjectLineVertices( ws.wire, id );
        const QPointF right = MapOrthoView_WorldToView( view.get(), { 64, 64 } );
        const QPointF left = MapOrthoView_WorldToView( view.get(), { -64, 64 } );
        map_mesh_vertex_hit_t a{}, b{}, nearby{};
        REQUIRE( MapOrthoView_PickMeshVertex( view.get(), right, &a ) ); REQUIRE( a.object == id );
        REQUIRE( MapOrthoView_PickMeshVertex( view.get(), left, &b ) ); REQUIRE( b.object == id );
        CHECK( a.vertex.value != b.vertex.value ); CHECK( MapOrthoView_Pick( view.get(), right ) == 0u );
        REQUIRE( MapOrthoView_PickMeshVertex( view.get(), right + QPointF( 2, 0 ), &nearby ) );
        CHECK( nearby.vertex.value == a.vertex.value );
        CHECK_FALSE( MapOrthoView_PickMeshVertex( view.get(), right + QPointF( 8, 0 ), &nearby ) );
        CHECK_FALSE( MapOrthoView_PickMeshVertex( view.get(), { -1, -1 }, &nearby ) );
        CHECK_FALSE( MapOrthoView_PickMeshVertex( view.get(), { std::numeric_limits<f64>::quiet_NaN(), 0 }, &nearby ) );
        const auto *source = geometry::GeometryDocument_FindMesh( &document->geometry, { id } ); REQUIRE( source != nullptr );
        const u32 depthAxis = pane == 0 ? 2u : pane == 1 ? 0u : 1u;
        for ( const auto endpoint : { a.vertex, b.vertex } ) {
            geometry::geometry_mesh_vertex_handle_t vertex{};
            REQUIRE( geometry::MeshSource_TryFindVertex( source, endpoint, &vertex ) );
            const auto *record = GenerationPool_Get( &source->mesh.vertices, vertex ); REQUIRE( record != nullptr );
            CHECK( TestCoordinate( record->position, depthAxis ) == 64 );
        }
        HoverMeshFaceTestPoint( view.get(), right ); CHECK( MapView_HoveredObject( view.get() ) == id );
        Click( view.get(), right ); REQUIRE( ws.meshSelection.vertices.nCount == 1u );
        CHECK( geometry::MeshSelection_HasVertex( &ws.meshSelection, a.vertex ) );
        Click( view.get(), left, Qt::ShiftModifier ); REQUIRE( ws.meshSelection.vertices.nCount == 2u );
        Click( view.get(), right, Qt::ControlModifier ); REQUIRE( ws.meshSelection.vertices.nCount == 1u );
        CHECK_FALSE( geometry::MeshSelection_HasVertex( &ws.meshSelection, a.vertex ) );
        CHECK( geometry::MeshSelection_HasVertex( &ws.meshSelection, b.vertex ) );
        DragMouse( view.get(), QEvent::MouseButtonPress, left );
        DragMouse( view.get(), QEvent::MouseMove, left + QPointF( 100, 40 ) );
        DragMouse( view.get(), QEvent::MouseButtonRelease, left + QPointF( 100, 40 ) );
        CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CheckObjectVertices( ws.wire, id, original );
        QKeyEvent cancel( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( view.get(), &cancel );
        CHECK( ws.tool == map_tool_t::NONE ); CHECK( ws.meshSelection.vertices.nCount == 1u );
        CHECK_FALSE( MapOrthoView_PickMeshVertex( view.get(), left, &nearby ) );
        QKeyEvent clear( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( view.get(), &clear );
        CHECK( ws.meshSelection.vertices.nCount == 0u ); CHECK( EditorSelection_Count( &ws.selection ) == 0u );
        CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    }
}

TEST_CASE( "Camera mesh vertices respect own faces and other physical occluders in every preview mode", "[map][gui][views][mesh-vertex][occlusion]" )
{
    for ( const auto mode : { map_render_mode_t::WIREFRAME, map_render_mode_t::SHADED, map_render_mode_t::FULLBRIGHT, map_render_mode_t::NORMALS } ) {
        CAPTURE( static_cast<int>( mode ) ); session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        settings.Real( "editor.camera.look_sensitivity", 1.0 ); settings.Set( "editor.camera.invert_y", false );
        std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 ); FacePositiveX( camera.get() );
        MapCameraView_SetRenderMode( camera.get(), mode ); const auto p = MapCameraView_Position( camera.get() );
        const auto make = [&]( f64 scale ) {
            map_bounds_t box{}; MapBounds_AddPoint( box, { p.x + 512 * scale, p.y - 96 * scale, p.z - 96 * scale } );
            MapBounds_AddPoint( box, { p.x + 768 * scale, p.y + 96 * scale, p.z + 96 * scale } );
            REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
            REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) ); return id;
        };
        const u64 far = make( 2 ), near = make( 1 ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES );
        QPointF corner, rear; REQUIRE( MapCameraView_WorldToView( camera.get(), { p.x + 512, p.y + 96, p.z + 96 }, &corner ) );
        REQUIRE( MapCameraView_WorldToView( camera.get(), { p.x + 768, p.y + 96, p.z + 96 }, &rear ) );
        map_mesh_vertex_hit_t hit{}; const QPointF point = corner + QPointF( 2, 0 );
        REQUIRE( MapCameraView_PickMeshVertex( camera.get(), point, &hit ) ); CHECK( hit.object == near );
        CHECK( MapCameraView_Pick( camera.get(), point ) == 0u );
        CHECK_FALSE( MapCameraView_PickMeshVertex( camera.get(), rear, &hit ) );
        HoverMeshFaceTestPoint( camera.get(), point ); CHECK( MapView_HoveredObject( camera.get() ) == near );
        Click( camera.get(), point ); REQUIRE( ws.meshSelection.vertices.nCount == 1u );
        OnlyVisible( ws, { far } ); REQUIRE( MapCameraView_PickMeshVertex( camera.get(), point, &hit ) ); CHECK( hit.object == far );
        OnlyVisible( ws, { near, far } ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS );
        map_bounds_t wall{}; MapBounds_AddPoint( wall, { p.x + 256, p.y - 200, p.z - 200 } );
        MapBounds_AddPoint( wall, { p.x + 272, p.y + 200, p.z + 200 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, wall ) ); const u64 blocker = EditorSelection_At( &ws.selection, 0 );
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES );
        CHECK_FALSE( MapCameraView_PickMeshVertex( camera.get(), point, &hit ) );
        HoverMeshFaceTestPoint( camera.get(), point ); CHECK( MapView_HoveredObject( camera.get() ) == 0u );
        OnlyVisible( ws, { near, far } ); REQUIRE( MapCameraView_PickMeshVertex( camera.get(), point, &hit ) ); CHECK( hit.object == near );
        OnlyVisible( ws, { blocker } ); CHECK_FALSE( MapCameraView_PickMeshVertex( camera.get(), point, &hit ) );
    }
}

TEST_CASE( "Camera vertex picking never synthesizes vertices at a clipped edge", "[map][gui][views][mesh-vertex][near-clip]" )
{
    session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    settings.Real( "editor.camera.look_sensitivity", 1.0 ); settings.Set( "editor.camera.invert_y", false );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 ); FacePositiveX( camera.get() );
    const auto p = MapCameraView_Position( camera.get() );
    map_bounds_t box{}; MapBounds_AddPoint( box, { p.x + 0.5, p.y + 16, p.z - 32 } ); MapBounds_AddPoint( box, { p.x + 256, p.y + 64, p.z + 32 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES );
    QPointF corner, middle, tooNear;
    REQUIRE( MapCameraView_WorldToView( camera.get(), { p.x + 256, p.y + 16, p.z + 32 }, &corner ) );
    REQUIRE( MapCameraView_WorldToView( camera.get(), { p.x + 128, p.y + 16, p.z + 32 }, &middle ) );
    CHECK_FALSE( MapCameraView_WorldToView( camera.get(), { p.x + 0.5, p.y + 16, p.z + 32 }, &tooNear ) );
    map_mesh_vertex_hit_t hit{}; REQUIRE( MapCameraView_PickMeshVertex( camera.get(), corner, &hit ) ); CHECK( hit.object == id );
    CHECK_FALSE( MapCameraView_PickMeshVertex( camera.get(), middle, &hit ) );
    OnlyVisible( ws, {} ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS );
    map_bounds_t behind{}; MapBounds_AddPoint( behind, { p.x - 128, p.y - 32, p.z - 32 } ); MapBounds_AddPoint( behind, { p.x - 64, p.y + 32, p.z + 32 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, behind ) ); REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES );
    CHECK_FALSE( MapCameraView_PickMeshVertex( camera.get(), { 400, 300 }, &hit ) );
    CHECK_FALSE( MapCameraView_PickMeshVertex( camera.get(), { 400, std::numeric_limits<f64>::quiet_NaN() }, &hit ) );
}

TEST_CASE( "A failed camera vertex occlusion query preserves the previous authored selection", "[map][gui][views][mesh-vertex][allocation][atomic]" )
{
    session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    settings.Real( "editor.camera.look_sensitivity", 1.0 ); settings.Set( "editor.camera.invert_y", false );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 ); FacePositiveX( camera.get() );
    const auto p = MapCameraView_Position( camera.get() );
    map_bounds_t box{}; MapBounds_AddPoint( box, { p.x + 512, p.y - 96, p.z - 96 } ); MapBounds_AddPoint( box, { p.x + 768, p.y + 96, p.z + 96 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES );
    QPointF point; REQUIRE( MapCameraView_WorldToView( camera.get(), { p.x + 512, p.y + 96, p.z + 96 }, &point ) );
    map_mesh_vertex_hit_t hit{}; REQUIRE( MapCameraView_PickMeshVertex( camera.get(), point, &hit ) );
    REQUIRE( MapWorkspace_SelectMeshVertex( &ws, hit.object, hit.vertex ) );
    const auto *vertices = ws.meshSelection.vertices.pData; const auto selectionRevision = ws.selection.revision;
    const usize steps = EditorHistory_StepCount( &ws.history ); const auto *document = ws.pDocument;
    mesh_face_allocation_failure_t audit{ 0, 1, 0 };
    const allocator_t allocator{ &MeshFaceTestAllocate, nullptr, &MeshFaceTestFree, &audit };
    const auto *original = ws.pDocument->pAllocator; ws.pDocument->pAllocator = &allocator;
    Click( camera.get(), point ); ws.pDocument->pAllocator = original;
    CHECK( audit.calls > 0u ); CHECK( audit.live == 0u );
    CHECK( ws.meshSelection.vertices.pData == vertices ); CHECK( ws.meshSelection.vertices.nCount == 1u );
    CHECK( geometry::MeshSelection_HasVertex( &ws.meshSelection, hit.vertex ) ); CHECK( ws.selection.revision == selectionRevision );
    CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK_FALSE( ws.editPreview.bActive );
}

TEST_CASE( "Vertex overlays stay compact and keep mesh face interiors neutral", "[map][gui][views][mesh-vertex][render][selection-clarity]" )
{
    session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -128, -128, 0 } ); MapBounds_AddPoint( box, { 128, 128, 256 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) );
    DisableNameTestAids( settings ); settings.Set( "editor.viewport.perspective.center_axes", false );
    UseClarityProbeTheme( session, "mesh_vertex_clarity" );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 1000, 760 );
    MapCameraView_SetRenderMode( camera.get(), map_render_mode_t::FULLBRIGHT ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
    MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES );
    const auto p = MapCameraView_Position( camera.get() );
    const math::vec3d_t cornerPoint{ p.x < 0 ? -128.0 : 128.0, p.y < 0 ? -128.0 : 128.0, 256 };
    QPointF corner, top; REQUIRE( MapCameraView_WorldToView( camera.get(), cornerPoint, &corner ) );
    REQUIRE( MapCameraView_WorldToView( camera.get(), { -80, 64, 256 }, &top ) );
    map_mesh_vertex_hit_t hit{}; REQUIRE( MapCameraView_PickMeshVertex( camera.get(), corner, &hit ) ); REQUIRE( hit.object == id );
    const QImage neutral = camera->grab().toImage();
    Click( camera.get(), corner ); REQUIRE( ws.meshSelection.vertices.nCount == 1u );
    const QImage selected = camera->grab().toImage();
    CHECK( ChangedPixelsNear( neutral, selected, corner, 5 ) > 0 );
    CHECK( LargestColorChangeNear( neutral, selected, top ) <= 12 );
    const QColor selection = gui::EditorStyle_Color( session.gui.style, gui::STYLE_COLOR_SELECTION );
    CHECK( HandleColorPixelsNear( selected, corner, selection, 5 ) > 0 );
    // Selection-bounds/dimension preferences must not reinstate parent-root
    // geometry overlays while working with authored components.
    settings.Set( "editor.viewport.perspective.show_selection_bounds", false ); settings.Set( "editor.viewport.perspective.show_selection_dimensions", false );
    const QImage noParentAids = camera->grab().toImage();
    settings.Set( "editor.viewport.perspective.show_selection_bounds", true ); settings.Set( "editor.viewport.perspective.show_selection_dimensions", true );
    CHECK( camera->grab().toImage() == noParentAids );
    std::unique_ptr<QWidget> topView( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) ); ShowAt( topView.get(), 800, 600 );
    MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents(); MapWorkspace_ClearMeshVertices( &ws );
    const QPointF sourceCorner = MapOrthoView_WorldToView( topView.get(), { 128, 128 } );
    const QImage candidates = topView->grab().toImage();
    MapWorkspace_SetTool( &ws, map_tool_t::NONE ); const QImage navigation = topView->grab().toImage();
    CHECK( ChangedPixelsNear( navigation, candidates, sourceCorner, 3 ) > 0 );
}

TEST_CASE( "Double click on a vertex opens its mesh inspector without discarding component context", "[map][gui][views][mesh-vertex][picking]" )
{
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, -64 } ); MapBounds_AddPoint( box, { 64, 64, 64 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 ); REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) );
    int inspections = 0; command_desc_t inspect{}; inspect.pId = "view.properties.open"; inspect.pLabel = "Inspect Object";
    inspect.pfnExecute = []( void *context, const command_args_t & ) { ++*static_cast<int *>( context ); return command_result_t::OK; };
    inspect.pContext = &inspections;
    REQUIRE( EditorCommands_Register( &session.gui.commands, &inspect, 1u ) == command_registry_status_t::OK );
    std::unique_ptr<QWidget> views( MapViews_Create( nullptr, &ws ) ); ShowAt( views.get(), 1200, 900 );
    MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents(); MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES );
    for ( int pane = 0; pane < 4; ++pane ) {
        CAPTURE( pane ); QWidget *view = MapViews_PaneView( views.get(), pane ); QPointF point; map_mesh_vertex_hit_t hit{};
        if ( pane == 0 ) {
            bool found = false;
            for ( const f64 x : { -64.0, 64.0 } ) { for ( const f64 y : { -64.0, 64.0 } ) { for ( const f64 z : { -64.0, 64.0 } ) {
                QPointF candidate;
                if ( !found && MapCameraView_WorldToView( view, { x, y, z }, &candidate ) && MapCameraView_PickMeshVertex( view, candidate, &hit ) && hit.object == id ) {
                    point = candidate; found = true;
                }
            } } }
            REQUIRE( found );
        } else {
            point = MapOrthoView_WorldToView( view, { 64, 64 } ); REQUIRE( MapOrthoView_PickMeshVertex( view, point, &hit ) );
        }
        DoubleClick( view, point ); CHECK( inspections == pane + 1 );
        REQUIRE( ws.meshSelection.vertices.nCount == 1u ); CHECK( geometry::MeshSelection_HasVertex( &ws.meshSelection, hit.vertex ) );
        CHECK( MapWorkspace_IsSelected( &ws, id ) );
        auto *menu = views->findChild<QMenu *>( QStringLiteral( "EditorViewOptionsMenu%1" ).arg( pane ) ); REQUIRE( menu != nullptr );
        CHECK_FALSE( menu->isVisible() ); DoubleClick( view, { 5, 5 } ); CHECK( menu->isVisible() ); CHECK( inspections == pane + 1 ); menu->hide();
    }
}

namespace
{
void CameraSpeedTestKeys( session_t &session, settings_document_t &keys )
{
    REQUIRE( SettingsDocument_Init( &keys, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &keys, StringView_FromCString( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "camera_speed_events"
  bindings = { "map.viewport.3d" = {
    "map.camera.speed_increase" = [ "Equal", "Plus", "Shift+Plus", "NumAdd" ]
    "map.camera.speed_decrease" = [ "Minus", "Shift+Minus", "NumSubtract" ]
    "map.camera.speed_reset" = [ "0", "Num0" ]
  } }
  held = { "map.viewport.3d" = { "map.camera.forward" = [ "W" ] "map.camera.fast" = [ "Shift" ] } }
  mouse = { "map.viewport.3d" = { "map.camera.look" = [ "RightDrag" ] } }
})cykv" ) ).status == settings_document_status_t::OK );
    REQUIRE( MapWorkspace_RegisterCommands( &session.workspace, &session.gui.commands ) == command_registry_status_t::OK );
    session.gui.keymapChain[0] = SettingsDocument_Root( &keys ); session.gui.nKeymapChain = 1u;
}

void CameraSpeedTestPress( QWidget *view, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier )
{
    QKeyEvent preflight( QEvent::ShortcutOverride, key, modifiers ); preflight.ignore();
    QCoreApplication::sendEvent( view, &preflight ); REQUIRE( preflight.isAccepted() );
    QKeyEvent press( QEvent::KeyPress, key, modifiers ); QCoreApplication::sendEvent( view, &press );
    CHECK( press.isAccepted() );
    QKeyEvent release( QEvent::KeyRelease, key, modifiers ); QCoreApplication::sendEvent( view, &release );
}
}

TEST_CASE( "Camera speed key commands preserve held flight and the active look pickup", "[map][gui][views][camera-speed][keymap][navigation]" )
{
    session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
    settings.Real( "editor.camera.move_speed", 1000.0 ); settings.Real( "editor.camera.fast_multiplier", 3.0 );
    settings_document_t keys{}; CameraSpeedTestKeys( session, keys );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
    const auto *document = ws.pDocument; const auto geometry = document->geometry.revision, selection = ws.selection.revision;
    const auto token = UndoRedo_StateToken( ws.history.pUndo ); const usize steps = EditorHistory_StepCount( &ws.history );
    QKeyEvent forward( QEvent::KeyPress, Qt::Key_W, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &forward );
    CHECK( TestDistance( MapCameraView_NavigationVelocity( camera.get() ), {} ) == 0.0 );
    const QPointF point( 300, 220 ); DragButton( camera.get(), QEvent::MouseButtonPress, point, Qt::RightButton );
    const auto originalForward = MapCameraView_Forward( camera.get() );
    const auto checkSpeed = [&]( f64 base, Qt::KeyboardModifiers modifiers = Qt::NoModifier ) {
        CHECK( EditorSettings_Real( &session.gui.settings, "editor.camera.move_speed", 0.0 ) == base );
        CheckPointClose( MapCameraView_NavigationVelocity( camera.get(), modifiers ), math::Vec3d_Scale( originalForward, base * ( modifiers.testFlag( Qt::ShiftModifier ) ? 3.0 : 1.0 ) ) );
        CheckPointClose( MapCameraView_Forward( camera.get() ), originalForward );
        CHECK( ws.tool == map_tool_t::SELECT );
    };
    checkSpeed( 1000.0 );
    QKeyEvent preflight( QEvent::ShortcutOverride, Qt::Key_Equal, Qt::NoModifier ); preflight.ignore();
    QCoreApplication::sendEvent( camera.get(), &preflight ); REQUIRE( preflight.isAccepted() ); checkSpeed( 1000.0 );
    CameraSpeedTestPress( camera.get(), Qt::Key_Equal ); checkSpeed( 2000.0 );
    CameraSpeedTestPress( camera.get(), Qt::Key_Minus ); checkSpeed( 1000.0 );
    CameraSpeedTestPress( camera.get(), Qt::Key_Plus, Qt::ShiftModifier ); checkSpeed( 2000.0, Qt::ShiftModifier );
    CameraSpeedTestPress( camera.get(), Qt::Key_0 ); checkSpeed( 1000.0 );
    CameraSpeedTestPress( camera.get(), Qt::Key_Plus, Qt::KeypadModifier ); checkSpeed( 2000.0 );
    CameraSpeedTestPress( camera.get(), Qt::Key_Minus, Qt::KeypadModifier ); checkSpeed( 1000.0 );
    CameraSpeedTestPress( camera.get(), Qt::Key_Equal ); checkSpeed( 2000.0 );
    CameraSpeedTestPress( camera.get(), Qt::Key_0, Qt::KeypadModifier ); checkSpeed( 1000.0 );
    const auto position = MapCameraView_Position( camera.get() );
    DragButton( camera.get(), QEvent::MouseMove, point + QPointF( 20, 0 ), Qt::RightButton );
    CHECK( TestDistance( MapCameraView_Forward( camera.get() ), originalForward ) > 0.01 );
    CheckPointClose( MapCameraView_Position( camera.get() ), position );
    CHECK( TestDistance( MapCameraView_NavigationVelocity( camera.get() ), {} ) == Catch::Approx( 1000.0 ) );
    QKeyEvent up( QEvent::KeyRelease, Qt::Key_W, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &up );
    CheckPointClose( MapCameraView_NavigationVelocity( camera.get() ), {} );
    DragButton( camera.get(), QEvent::MouseButtonRelease, point + QPointF( 20, 0 ), Qt::RightButton );
    CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == geometry ); CHECK( ws.selection.revision == selection );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
}

TEST_CASE( "Camera speed bindings stay remappable and never consume 2D or text editing keys", "[map][gui][views][camera-speed][keymap][focus]" )
{
    session_t session; auto &ws = session.workspace; view_settings_t settings( &session.gui.settings );
    settings.Real( "editor.camera.move_speed", 1000.0 );
    settings_document_t keys{}, remapped{}; CameraSpeedTestKeys( session, keys );
    REQUIRE( SettingsDocument_Init( &remapped, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &remapped, StringView_FromCString( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "camera_speed_remapped" bindings = { "map.viewport.3d" = {
  "map.camera.speed_increase" = [ "F6" ] "map.camera.speed_decrease" = [ "F7" ] "map.camera.speed_reset" = [ "F9" ]
} } })cykv" ) ).status == settings_document_status_t::OK );
    session.gui.keymapChain[0] = SettingsDocument_Root( &remapped ); session.gui.keymapChain[1] = SettingsDocument_Root( &keys ); session.gui.nKeymapChain = 2u;
    QWidget window; auto *layout = new QHBoxLayout( &window );
    auto *camera = MapCameraView_Create( &window, &ws ); auto *top = MapOrthoView_Create( &window, &ws, map_ortho_axes_t::TOP ); auto *input = new QLineEdit( &window );
    layout->addWidget( camera ); layout->addWidget( top ); layout->addWidget( input ); ShowAt( &window, 1200, 600 );
    CameraSpeedTestPress( camera, Qt::Key_F6 ); CHECK( EditorSettings_Real( &session.gui.settings, "editor.camera.move_speed", 0.0 ) == 2000.0 );
    CameraSpeedTestPress( camera, Qt::Key_F7 ); CHECK( EditorSettings_Real( &session.gui.settings, "editor.camera.move_speed", 0.0 ) == 1000.0 );
    CameraSpeedTestPress( camera, Qt::Key_F6 ); CameraSpeedTestPress( camera, Qt::Key_F9 );
    CHECK( EditorSettings_Real( &session.gui.settings, "editor.camera.move_speed", 0.0 ) == 1000.0 );
    for ( const int key : { Qt::Key_Equal, Qt::Key_Minus, Qt::Key_0 } ) {
        QKeyEvent press( QEvent::KeyPress, key, Qt::NoModifier ); QCoreApplication::sendEvent( camera, &press );
        CHECK( EditorSettings_Real( &session.gui.settings, "editor.camera.move_speed", 0.0 ) == 1000.0 );
    }
    for ( const int key : { Qt::Key_F6, Qt::Key_F7, Qt::Key_F9, Qt::Key_Equal, Qt::Key_Minus, Qt::Key_0 } ) {
        QKeyEvent preflight( QEvent::ShortcutOverride, key, Qt::NoModifier ); preflight.ignore(); QCoreApplication::sendEvent( top, &preflight );
        CHECK_FALSE( preflight.isAccepted() );
        QKeyEvent press( QEvent::KeyPress, key, Qt::NoModifier ); QCoreApplication::sendEvent( top, &press );
        CHECK( EditorSettings_Real( &session.gui.settings, "editor.camera.move_speed", 0.0 ) == 1000.0 );
    }
    session.gui.keymapChain[0] = SettingsDocument_Root( &keys ); session.gui.nKeymapChain = 1u;
    window.activateWindow(); input->setFocus(); QCoreApplication::processEvents(); REQUIRE( input->hasFocus() );
    Enter( camera ); REQUIRE( input->hasFocus() );
    for ( const auto &stroke : { std::pair{ Qt::Key_Equal, QStringLiteral( "=" ) }, std::pair{ Qt::Key_Minus, QStringLiteral( "-" ) }, std::pair{ Qt::Key_0, QStringLiteral( "0" ) } } ) {
        QKeyEvent preflight( QEvent::ShortcutOverride, stroke.first, Qt::NoModifier ); preflight.ignore(); QCoreApplication::sendEvent( input, &preflight );
        CHECK( preflight.isAccepted() );
        QKeyEvent press( QEvent::KeyPress, stroke.first, Qt::NoModifier, stroke.second ); QCoreApplication::sendEvent( input, &press );
    }
    CHECK( input->text() == QStringLiteral( "=-0" ) ); CHECK( input->hasFocus() );
    CHECK( EditorSettings_Real( &session.gui.settings, "editor.camera.move_speed", 0.0 ) == 1000.0 );
}

TEST_CASE( "Effective camera speed changes briefly render feedback while metrics remain hidden", "[map][gui][views][camera-speed][render][settings]" )
{
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    view_settings_t settings( &session.gui.settings ); DisableNameTestAids( settings );
    settings.Real( "editor.camera.move_speed", 1000.0 );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
    const QImage baseline = camera->grab().toImage();
    settings.Real( "editor.camera.move_speed", 2000.0 ); const QImage feedback = camera->grab().toImage();
    CHECK( ChangedPixelCount( baseline, feedback ) > 100 );
    CHECK_FALSE( EditorSettings_Bool( &session.gui.settings, "editor.viewport.perspective.show_metrics", CY_TRUE ) );
    const qreal dpr = feedback.devicePixelRatio();
    CHECK( ChangedPixelCount( baseline.copy( 0, 0, baseline.width(), static_cast<int>( 500 * dpr ) ),
                              feedback.copy( 0, 0, feedback.width(), static_cast<int>( 500 * dpr ) ) ) == 0 );
    QEventLoop expiry; QTimer::singleShot( 2250, &expiry, &QEventLoop::quit ); expiry.exec();
    CHECK( ChangedPixelCount( baseline, camera->grab().toImage() ) == 0 );
    settings.Real( "editor.camera.move_speed", 2000.0 ); // A same-value notification must not restart feedback.
    CHECK( ChangedPixelCount( baseline, camera->grab().toImage() ) == 0 );
    settings.Real( "editor.camera.fast_multiplier", 4.0 );
    CHECK( ChangedPixelCount( baseline, camera->grab().toImage() ) == 0 );
    settings.Real( "editor.camera.move_speed", 2500.0 );
    CHECK( ChangedPixelCount( baseline, camera->grab().toImage() ) > 100 );
}

TEST_CASE( "Failed camera speed settings publication preserves flight and does not announce a change", "[map][gui][views][camera-speed][allocation][atomic]" )
{
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    view_settings_t settings( &session.gui.settings ); DisableNameTestAids( settings );
    settings.Real( "editor.camera.move_speed", 1000.0 );
    settings_document_t keys{}; CameraSpeedTestKeys( session, keys );
    mesh_face_allocation_failure_t audit{};
    const allocator_t allocator{ &MeshFaceTestAllocate, nullptr, &MeshFaceTestFree, &audit };
    settings_document_t scope{}; REQUIRE( SettingsDocument_Init( &scope, &allocator, EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
    EditorSettings_SetScope( &session.gui.settings, settings_scope_t::WORKSPACE, &scope );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
    const QPointF point( 300, 220 ); DragButton( camera.get(), QEvent::MouseButtonPress, point, Qt::RightButton );
    DragButton( camera.get(), QEvent::MouseMove, point + QPointF( 8, 0 ), Qt::RightButton );
    QKeyEvent forward( QEvent::KeyPress, Qt::Key_W, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &forward );
    const auto direction = MapCameraView_Forward( camera.get() ); const auto position = MapCameraView_Position( camera.get() );
    const auto *document = ws.pDocument; const auto geometry = document->geometry.revision, selection = ws.selection.revision;
    const auto token = UndoRedo_StateToken( ws.history.pUndo ); const usize steps = EditorHistory_StepCount( &ws.history );
    const auto *settingsTree = scope.pDocument; const usize live = audit.live;
    const QImage baseline = camera->grab().toImage();
    audit.calls = 0u; audit.failOn = 1u; CameraSpeedTestPress( camera.get(), Qt::Key_Equal ); audit.failOn = 0u;
    CHECK( audit.calls > 0u ); CHECK( audit.live == live ); CHECK( scope.pDocument == settingsTree );
    CHECK( EditorSettings_Real( &session.gui.settings, "editor.camera.move_speed", 0.0 ) == 1000.0 );
    CheckPointClose( MapCameraView_NavigationVelocity( camera.get() ), math::Vec3d_Scale( direction, 1000.0 ) );
    CheckPointClose( MapCameraView_Position( camera.get() ), position ); CheckPointClose( MapCameraView_Forward( camera.get() ), direction );
    CHECK( ChangedPixelCount( baseline, camera->grab().toImage() ) == 0 );
    CameraSpeedTestPress( camera.get(), Qt::Key_Equal );
    CHECK( EditorSettings_Real( &session.gui.settings, "editor.camera.move_speed", 0.0 ) == 2000.0 );
    CheckPointClose( MapCameraView_NavigationVelocity( camera.get() ), math::Vec3d_Scale( direction, 2000.0 ) );
    CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == geometry ); CHECK( ws.selection.revision == selection );
    CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    QKeyEvent up( QEvent::KeyRelease, Qt::Key_W, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &up );
    DragButton( camera.get(), QEvent::MouseButtonRelease, point + QPointF( 8, 0 ), Qt::RightButton );
    camera.reset(); EditorSettings_SetScope( &session.gui.settings, settings_scope_t::WORKSPACE, nullptr );
    SettingsDocument_Shutdown( &scope ); CHECK( audit.live == 0u );
}

namespace
{
void CameraShiftTestKey( QWidget *view, QEvent::Type type, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                         bool repeat = false, bool expectAccepted = true )
{
    // QKeyEvent::modifiers() toggles a modifier key's own bit. The caller
    // supplies the post-event state, as ordinary native events expose it.
    Qt::KeyboardModifiers constructorModifiers = modifiers;
    if ( key == Qt::Key_Shift ) { constructorModifiers ^= Qt::ShiftModifier; }
    else if ( key == Qt::Key_Control ) { constructorModifiers ^= Qt::ControlModifier; }
    else if ( key == Qt::Key_Alt ) { constructorModifiers ^= Qt::AltModifier; }
    else if ( key == Qt::Key_Meta ) { constructorModifiers ^= Qt::MetaModifier; }
    if ( type == QEvent::KeyPress ) {
        QKeyEvent preflight( QEvent::ShortcutOverride, key, constructorModifiers, QString(), repeat ); preflight.ignore();
        QCoreApplication::sendEvent( view, &preflight );
        if ( expectAccepted ) { REQUIRE( preflight.isAccepted() ); }
    }
    QKeyEvent event( type, key, constructorModifiers, QString(), repeat );
    REQUIRE( event.modifiers() == modifiers ); event.ignore(); QCoreApplication::sendEvent( view, &event );
    if ( type == QEvent::KeyPress && expectAccepted ) { CHECK( event.isAccepted() ); }
}
}

TEST_CASE( "Shift movement owns camera input across captured look and neutral flight modifier orders", "[map][gui][views][camera-shift][navigation-ownership]" )
{
    // Select+Look, Navigation+Look, Camera tool, and Navigation idle flight.
    for ( int context = 0; context < 4; ++context ) { for ( const bool shiftFirst : { false, true } ) {
        CAPTURE( context, shiftFirst ); session_t session; auto &ws = session.workspace;
        REQUIRE( MapWorkspace_RegisterCommands( &ws, &session.gui.commands ) == command_registry_status_t::OK );
        view_settings_t settings( &session.gui.settings ); settings.Real( "editor.camera.move_speed", 1000.0 );
        settings.Real( "editor.camera.fast_multiplier", 4.0 );
        const auto tool = context == 0 ? map_tool_t::SELECT : context == 2 ? map_tool_t::CAMERA : map_tool_t::NONE;
        MapWorkspace_SetTool( &ws, tool );
        std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision, selection = ws.selection.revision;
        const auto token = UndoRedo_StateToken( ws.history.pUndo ); const usize steps = EditorHistory_StepCount( &ws.history );
        const bool captured = context < 2, anchor = !shiftFirst || context == 3;
        const QPointF point( 300, 220 );
        if ( shiftFirst ) { CameraShiftTestKey( camera.get(), QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier, false, context != 0 ); }
        if ( captured ) { DragButton( camera.get(), QEvent::MouseButtonPress, point, Qt::RightButton, shiftFirst ? Qt::ShiftModifier : Qt::NoModifier ); }
        if ( anchor ) { CameraShiftTestKey( camera.get(), QEvent::KeyPress, Qt::Key_W, shiftFirst ? Qt::ShiftModifier : Qt::NoModifier ); }
        if ( !shiftFirst ) {
            CameraShiftTestKey( camera.get(), QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier );
            CameraShiftTestKey( camera.get(), QEvent::KeyPress, Qt::Key_W, Qt::ShiftModifier, true );
            CHECK( TestDistance( MapCameraView_NavigationVelocity( camera.get(), Qt::ShiftModifier ), {} ) == Catch::Approx( 4000.0 ) );
        }
        for ( const int key : { Qt::Key_S, Qt::Key_D, Qt::Key_E } ) {
            CAPTURE( key ); CameraShiftTestKey( camera.get(), QEvent::KeyPress, key, Qt::ShiftModifier );
            CHECK( ws.tool == tool );
            const auto velocity = MapCameraView_NavigationVelocity( camera.get(), Qt::ShiftModifier );
            CHECK( TestDistance( velocity, {} ) == Catch::Approx( anchor && key == Qt::Key_S ? 0.0 : 4000.0 ) );
            if ( !anchor && key == Qt::Key_E ) { CHECK( velocity.z == Catch::Approx( 4000.0 ) ); }
            // Modifier changes on release still release the observed physical key.
            CameraShiftTestKey( camera.get(), QEvent::KeyRelease, key );
            CHECK( TestDistance( MapCameraView_NavigationVelocity( camera.get(), Qt::ShiftModifier ), {} ) == Catch::Approx( anchor ? 4000.0 : 0.0 ) );
        }
        if ( anchor ) { CameraShiftTestKey( camera.get(), QEvent::KeyRelease, Qt::Key_W ); }
        CameraShiftTestKey( camera.get(), QEvent::KeyRelease, Qt::Key_Shift );
        CheckPointClose( MapCameraView_NavigationVelocity( camera.get() ), {} );
        if ( captured ) {
            const auto forward = MapCameraView_Forward( camera.get() );
            DragButton( camera.get(), QEvent::MouseMove, point + QPointF( 8, 0 ), Qt::RightButton );
            CHECK( TestDistance( MapCameraView_Forward( camera.get() ), forward ) > 0.01 );
            DragButton( camera.get(), QEvent::MouseButtonRelease, point + QPointF( 8, 0 ), Qt::RightButton );
        }
        CHECK( ws.tool == tool ); CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
        CHECK( ws.selection.revision == selection ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
        CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    } }
}

TEST_CASE( "Neutral camera idle tool exits return after release or focus loss without stale repeats", "[map][gui][views][camera-shift][navigation-ownership][focus]" )
{
    session_t session; auto &ws = session.workspace;
    REQUIRE( MapWorkspace_RegisterCommands( &ws, &session.gui.commands ) == command_registry_status_t::OK );
    MapWorkspace_SetTool( &ws, map_tool_t::NONE );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
    const QPointF point( 300, 220 );
    CameraShiftTestKey( camera.get(), QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier );
    DragButton( camera.get(), QEvent::MouseButtonPress, point, Qt::RightButton, Qt::ShiftModifier );
    CameraShiftTestKey( camera.get(), QEvent::KeyPress, Qt::Key_S, Qt::ShiftModifier ); CHECK( ws.tool == map_tool_t::NONE );
    REQUIRE( TestDistance( MapCameraView_NavigationVelocity( camera.get(), Qt::ShiftModifier ), {} ) > 3999.0 );
    CameraShiftTestKey( camera.get(), QEvent::KeyRelease, Qt::Key_S );
    DragButton( camera.get(), QEvent::MouseMove, point + QPointF( 8, 0 ), Qt::RightButton, Qt::ShiftModifier );
    DragButton( camera.get(), QEvent::MouseButtonRelease, point + QPointF( 8, 0 ), Qt::RightButton, Qt::ShiftModifier );
    // A speed modifier alone does not retain flight ownership.
    CameraShiftTestKey( camera.get(), QEvent::KeyPress, Qt::Key_S, Qt::ShiftModifier ); CHECK( ws.tool == map_tool_t::SELECT );
    CameraShiftTestKey( camera.get(), QEvent::KeyRelease, Qt::Key_S ); CameraShiftTestKey( camera.get(), QEvent::KeyRelease, Qt::Key_Shift );
    MapWorkspace_SetTool( &ws, map_tool_t::NONE );
    CameraShiftTestKey( camera.get(), QEvent::KeyPress, Qt::Key_W );
    CameraShiftTestKey( camera.get(), QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier );
    REQUIRE( TestDistance( MapCameraView_NavigationVelocity( camera.get(), Qt::ShiftModifier ), {} ) > 3999.0 );
    QFocusEvent lost( QEvent::FocusOut, Qt::OtherFocusReason ); QCoreApplication::sendEvent( camera.get(), &lost );
    CameraShiftTestKey( camera.get(), QEvent::KeyPress, Qt::Key_W, Qt::ShiftModifier, true );
    CheckPointClose( MapCameraView_NavigationVelocity( camera.get(), Qt::ShiftModifier ), {} );
    CameraShiftTestKey( camera.get(), QEvent::KeyPress, Qt::Key_S, Qt::ShiftModifier ); CHECK( ws.tool == map_tool_t::SELECT );
}

TEST_CASE( "Camera ownership follows live held remaps and keeps speed framing cancel and other commands", "[map][gui][views][camera-shift][navigation-ownership][keymap]" )
{
    session_t session; auto &ws = session.workspace;
    REQUIRE( MapWorkspace_RegisterCommands( &ws, &session.gui.commands ) == command_registry_status_t::OK );
    view_settings_t settings( &session.gui.settings ); settings.Real( "editor.camera.move_speed", 1000.0 );
    settings_document_t keys{}, unbound{};
    REQUIRE( SettingsDocument_Init( &keys, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Init( &unbound, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &keys, StringView_FromCString( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "camera_shift_remap"
  bindings = { "map.viewport" = { "map.tool.select" = [ "Shift+X" ] } }
  held = { "map.viewport.3d" = { "map.camera.forward" = [ "X", "Ctrl+Y" ] } }
})cykv" ) ).status == settings_document_status_t::OK );
    const auto *base = session.gui.keymapChain[0];
    session.gui.keymapChain[0] = SettingsDocument_Root( &keys ); session.gui.keymapChain[1] = base; session.gui.nKeymapChain = 2u;
    MapWorkspace_SetTool( &ws, map_tool_t::NONE );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
    const QPointF point( 300, 220 ); DragButton( camera.get(), QEvent::MouseButtonPress, point, Qt::RightButton );
    CameraShiftTestKey( camera.get(), QEvent::KeyPress, Qt::Key_X, Qt::ShiftModifier ); CHECK( ws.tool == map_tool_t::NONE );
    CHECK( TestDistance( MapCameraView_NavigationVelocity( camera.get(), Qt::ShiftModifier ), {} ) == Catch::Approx( 4000.0 ) );
    CameraSpeedTestPress( camera.get(), Qt::Key_Plus, Qt::ShiftModifier ); CHECK( ws.tool == map_tool_t::NONE );
    CHECK( EditorSettings_Real( &session.gui.settings, "editor.camera.move_speed", 0.0 ) == 2000.0 );
    CameraShiftTestKey( camera.get(), QEvent::KeyRelease, Qt::Key_X );
    CameraShiftTestKey( camera.get(), QEvent::KeyPress, Qt::Key_Y, Qt::ControlModifier );
    CHECK( TestDistance( MapCameraView_NavigationVelocity( camera.get(), Qt::ControlModifier ), {} ) == Catch::Approx( 2000.0 ) );
    CameraShiftTestKey( camera.get(), QEvent::KeyRelease, Qt::Key_Y );
    int framed = 0;
    EditorCommands_SetObserver( &session.gui.commands,
        []( void *context, const command_desc_t &command, const command_args_t &, command_result_t result ) {
            if ( StringView_Equals( StringView_FromCString( command.pId ), StringView_FromCString( "map.view.frame_all" ) ) && result == command_result_t::OK ) { ++*static_cast<int *>( context ); }
        }, &framed );
    CameraSpeedTestPress( camera.get(), Qt::Key_Home ); CHECK( framed == 1 ); CHECK( ws.tool == map_tool_t::NONE );
    EditorCommands_SetObserver( &session.gui.commands, nullptr, nullptr );
    DragButton( camera.get(), QEvent::MouseButtonPress, point, Qt::RightButton );
    CameraShiftTestKey( camera.get(), QEvent::KeyPress, Qt::Key_X );
    CameraSpeedTestPress( camera.get(), Qt::Key_Escape ); CHECK( ws.tool == map_tool_t::NONE );
    CheckPointClose( MapCameraView_NavigationVelocity( camera.get() ), {} );
    // Explicit held unbinding restores the tool command even during capture.
    REQUIRE( SettingsDocument_Load( &unbound, StringView_FromCString( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "camera_shift_unbound" held = { "map.viewport.3d" = { "map.camera.forward" = [] } } })cykv" ) ).status == settings_document_status_t::OK );
    session.gui.keymapChain[0] = SettingsDocument_Root( &unbound ); session.gui.keymapChain[1] = SettingsDocument_Root( &keys );
    session.gui.keymapChain[2] = base; session.gui.nKeymapChain = 3u;
    DragButton( camera.get(), QEvent::MouseButtonPress, point, Qt::RightButton );
    CameraShiftTestKey( camera.get(), QEvent::KeyPress, Qt::Key_X, Qt::ShiftModifier ); CHECK( ws.tool == map_tool_t::SELECT );
    CheckPointClose( MapCameraView_NavigationVelocity( camera.get(), Qt::ShiftModifier ), {} );
    // The ownership policy belongs only to camera navigation, never a 2D pane.
    MapWorkspace_SetTool( &ws, map_tool_t::NONE );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) ); ShowAt( top.get(), 800, 600 );
    CameraSpeedTestPress( top.get(), Qt::Key_X, Qt::ShiftModifier ); CHECK( ws.tool == map_tool_t::SELECT );
}

TEST_CASE( "Releasing an unbound command modifier resumes observed camera movement without another press", "[map][gui][views][camera-shift][navigation-ownership][timer]" )
{
    session_t session; auto &ws = session.workspace; MapWorkspace_SetTool( &ws, map_tool_t::NONE );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
    CameraShiftTestKey( camera.get(), QEvent::KeyPress, Qt::Key_W );
    REQUIRE( TestDistance( MapCameraView_NavigationVelocity( camera.get() ), {} ) > 999.0 );
    CameraShiftTestKey( camera.get(), QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier, false, false );
    // Model timer eligibility with Ctrl held before testing its release.
    // Offscreen Qt's synthetic global modifier cache is normalized below.
    CheckPointClose( MapCameraView_NavigationVelocity( camera.get(), Qt::ControlModifier ), {} );
    const auto stopped = MapCameraView_Position( camera.get() );
    QEventLoop pause; QTimer::singleShot( 50, &pause, &QEventLoop::quit ); pause.exec();
    CheckPointClose( MapCameraView_Position( camera.get() ), stopped );
    CameraShiftTestKey( camera.get(), QEvent::KeyRelease, Qt::Key_Control );
    // Offscreen Qt retains Ctrl in its global cache after this synthetic
    // modifier release. An unrelated press corrects that platform state but
    // cannot refresh held navigation or restart a stopped camera timer.
    QKeyEvent sync( QEvent::KeyPress, Qt::Key_F12, Qt::NoModifier );
    REQUIRE( MapInput_NavigationKeyMask( &ws, &sync ) == 0u );
    REQUIRE_FALSE( MapInput_DispatchKey( &ws, &sync, true, true, false ) );
    QCoreApplication::sendEvent( camera.get(), &sync );
    REQUIRE( QGuiApplication::queryKeyboardModifiers() == Qt::NoModifier );
    REQUIRE( QGuiApplication::keyboardModifiers() == Qt::NoModifier );
    QEventLoop resumed; QTimer::singleShot( 50, &resumed, &QEventLoop::quit ); resumed.exec();
    CHECK( TestDistance( MapCameraView_Position( camera.get() ), stopped ) > 1.0 );
    CameraShiftTestKey( camera.get(), QEvent::KeyRelease, Qt::Key_W );
    QFocusEvent lost( QEvent::FocusOut, Qt::OtherFocusReason ); QCoreApplication::sendEvent( camera.get(), &lost );
    CameraShiftTestKey( camera.get(), QEvent::KeyRelease, Qt::Key_Control );
    CameraShiftTestKey( camera.get(), QEvent::KeyPress, Qt::Key_W, Qt::NoModifier, true );
    CheckPointClose( MapCameraView_NavigationVelocity( camera.get() ), {} );
}

TEST_CASE( "Face extension shows opaque adjoining sides and moves the selected highlight in every pane", "[map][gui][views][face-volume-preview][render][cross-pane]" )
{
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    view_settings_t settings( &session.gui.settings );
    settings.Set( "editor.grid.show_surface_3d", false );
    settings.Set( "editor.viewport.show_selection_dimensions", false ); settings.Set( "editor.viewport.perspective.show_selection_dimensions", false );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -128, -128, -128 } ); MapBounds_AddPoint( box, { 128, 128, 128 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 );
    std::unique_ptr<QWidget> front( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::FRONT ) );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( front.get(), 800, 600 ); ShowAt( camera.get(), 800, 600 );
    MapCameraView_SetRenderMode( camera.get(), map_render_mode_t::FULLBRIGHT ); MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents();
    // Keep the prospective cap inside the ortho pane without reframing the
    // gesture; the live camera and all panes stay fixed during preview.
    const QPointF frontCenter = front->rect().center();
    QWheelEvent zoomOut( frontCenter, front->mapToGlobal( frontCenter ), QPoint(), QPoint( 0, -240 ), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false );
    QCoreApplication::sendEvent( front.get(), &zoomOut );
    MapWorkspace_SelectBrushFace( &ws, id, BrushSideInDirection( ws, id, 2, 1 ) );
    const auto position = MapCameraView_Position( camera.get() );
    const math::vec3d_t wallInterior{ position.x > 0 ? 128.0 : -128.0, 31.0, 160.0 };
    QPointF sideProbe; REQUIRE( MapCameraView_WorldToView( camera.get(), wallInterior, &sideProbe ) );
    REQUIRE( camera->rect().adjusted( 12, 12, -12, -12 ).contains( sideProbe.toPoint() ) );
    const QPointF oldFace = MapOrthoView_WorldToView( front.get(), { 31, 128 } ), newFace = MapOrthoView_WorldToView( front.get(), { 31, 192 } );
    const QColor selected = gui::EditorStyle_Color( session.gui.style, gui::STYLE_COLOR_SELECTION );
    const QImage cameraBefore = camera->grab().toImage(), frontBefore = front->grab().toImage();
    REQUIRE( HandleColorPixelsNear( frontBefore, oldFace, selected, 2 ) > 0 );
    REQUIRE( HandleColorPixelsNear( frontBefore, newFace, selected, 2 ) == 0 );
    const auto *document = ws.pDocument; const auto revision = document->geometry.revision; const auto original = ObjectLineVertices( ws.wire, id );
    MapWorkspace_SetFacePreview( &ws, 64 ); REQUIRE( ws.editPreview.status == map_status_t::OK );
    const QImage cameraPreview = camera->grab().toImage(), frontPreview = front->grab().toImage();
    CHECK( ChangedPixelsNear( cameraBefore, cameraPreview, sideProbe, 1 ) > 0 ); // Interior fill, away from the moved cap and wire edges.
    CHECK( HandleColorPixelsNear( frontPreview, newFace, selected, 2 ) > 0 );
    CHECK( HandleColorPixelsNear( frontPreview, oldFace, selected, 2 ) == 0 );
    CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision ); CheckObjectVertices( ws.wire, id, original );
    const auto candidate = ObjectLineVertices( ws.editPreviewWire, id ); REQUIRE( MapWorkspace_CommitFacePreview( &ws ) );
    CheckObjectVertices( ws.wire, id, candidate );
    CHECK( ChangedPixelsNear( cameraPreview, camera->grab().toImage(), sideProbe, 1 ) == 0 );
    CHECK( HandleColorPixelsNear( front->grab().toImage(), newFace, selected, 2 ) > 0 );
}

TEST_CASE( "Failed or cancelled face-volume gestures cannot publish when a later release reaches a valid pointer", "[map][gui][views][face-volume-preview][allocation][cancel][late-release]" )
{
    for ( const bool allocationFailure : { false, true } ) {
        CAPTURE( allocationFailure ); session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t box{}; MapBounds_AddPoint( box, { -128, -128, -128 } ); MapBounds_AddPoint( box, { 128, 128, 128 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 id = EditorSelection_At( &ws.selection, 0 ); const auto original = ObjectLineVertices( ws.wire, id );
        std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
        MapWorkspace_Frame( &ws, CY_TRUE ); QCoreApplication::processEvents(); MapWorkspace_SelectBrushFace( &ws, id, BrushSideInDirection( ws, id, 2, 1 ) );
        QPointF start, valid, failed;
        REQUIRE( MapCameraView_WorldToView( camera.get(), { 0, 0, 128 }, &start ) ); REQUIRE( MapCameraView_WorldToView( camera.get(), { 0, 0, 160 }, &valid ) );
        REQUIRE( MapCameraView_WorldToView( camera.get(), { 0, 0, 192 }, &failed ) );
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision; const auto token = UndoRedo_StateToken( ws.history.pUndo );
        DragMouse( camera.get(), QEvent::MouseButtonPress, start ); DragMouse( camera.get(), QEvent::MouseMove, valid );
        REQUIRE( ws.editPreview.status == map_status_t::OK ); REQUIRE( ws.editPreviewWire.faces.nCount == 6 );
        if ( allocationFailure ) {
            const allocator_t failAllocator{
                []( void *, usize, usize ) noexcept -> void * { return nullptr; }, nullptr,
                []( void *, void *memory, usize bytes, usize alignment ) noexcept { Allocator_Free( Allocator_GetSystem(), memory, bytes, alignment ); }, nullptr };
            const auto *originalAllocator = ws.pDocument->pAllocator; ws.pDocument->pAllocator = &failAllocator;
            DragMouse( camera.get(), QEvent::MouseMove, failed ); ws.pDocument->pAllocator = originalAllocator;
            REQUIRE( ws.editPreview.status == map_status_t::OUT_OF_MEMORY ); REQUIRE( ws.editPreviewWire.faces.nCount == 0 );
        } else {
            QKeyEvent cancel( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( camera.get(), &cancel );
            REQUIRE_FALSE( ws.editPreview.bActive ); REQUIRE( ws.editPreviewWire.faces.nCount == 0 );
        }
        // Allocator recovery alone must not rebuild an unseen candidate at
        // release. A cancelled gesture similarly ignores a late release.
        DragMouse( camera.get(), QEvent::MouseButtonRelease, failed );
        CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.editPreviewWire.points.nCount == 0 );
        CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision ); CheckObjectVertices( ws.wire, id, original );
        CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
        CHECK( EditorHistory_AppliedStepCount( &ws.history ) == 1 );
    }
}

namespace
{
bool LookWheel( QWidget *view, int angle, Qt::KeyboardModifiers modifiers = Qt::NoModifier, int pixels = 0 )
{
    const QPointF point( 300, 220 );
    QWheelEvent event( point, view->mapToGlobal( point ), QPoint( 0, pixels ), QPoint( 0, angle ), Qt::RightButton,
                       modifiers, Qt::NoScrollPhase, false );
    event.ignore(); QCoreApplication::sendEvent( view, &event ); return event.isAccepted();
}

struct look_wheel_context_requests_t : QObject {
    int count{};
    bool eventFilter( QObject *, QEvent *event ) override {
        if ( event->type() != QEvent::ContextMenu ) { return false; }
        if ( static_cast<QContextMenuEvent *>( event )->reason() == QContextMenuEvent::Other ) { ++count; }
        return true;
    }
};
}

TEST_CASE( "Captured look wheel changes flight speed without interrupting movement or aiming", "[map][gui][views][camera-look-wheel][navigation][camera-speed]" )
{
    for ( const bool customLook : { false, true } ) {
        CAPTURE( customLook ); session_t session; auto &ws = session.workspace;
        view_settings_t settings( &session.gui.settings ); settings.Real( "editor.camera.move_speed", 1000.0 );
        settings.Real( "editor.camera.fast_multiplier", 3.0 ); settings.Set( "editor.camera.invert_wheel", true );
        settings_document_t keys{};
        if ( customLook ) {
            REQUIRE( SettingsDocument_Init( &keys, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
            REQUIRE( SettingsDocument_Load( &keys, StringView_FromCString( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "look_wheel_custom" held = { "map.viewport.3d" = { "map.camera.forward" = [ "W" ] "map.camera.fast" = [ "Shift" ] } }
  mouse = { "map.viewport.3d" = { "map.camera.look" = [ "MiddleDrag" ] "map.camera.dolly" = [ "Wheel" ] }
            "map.camera.look" = { "map.camera.speed_increase" = [ "WheelUp" ] "map.camera.speed_decrease" = [ "WheelDown" ] } } })cykv" ) ).status == settings_document_status_t::OK );
            session.gui.keymapChain[0] = SettingsDocument_Root( &keys ); session.gui.nKeymapChain = 1u;
        }
        std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
        const auto *document = ws.pDocument; const auto geometry = document->geometry.revision, selection = ws.selection.revision;
        const auto token = UndoRedo_StateToken( ws.history.pUndo ); const usize history = EditorHistory_StepCount( &ws.history );
        const QPointF point( 300, 220 ); const auto button = customLook ? Qt::MiddleButton : Qt::RightButton;
        DragButton( camera.get(), QEvent::MouseButtonPress, point, button );
        CameraShiftTestKey( camera.get(), QEvent::KeyPress, Qt::Key_W );
        const auto position = MapCameraView_Position( camera.get() ), direction = MapCameraView_Forward( camera.get() );
        REQUIRE( LookWheel( camera.get(), 240, Qt::ShiftModifier ) );
        CHECK( EditorSettings_Real( &session.gui.settings, "editor.camera.move_speed", 0.0 ) == 4000.0 );
        CheckPointClose( MapCameraView_NavigationVelocity( camera.get(), Qt::ShiftModifier ), math::Vec3d_Scale( direction, 12000.0 ) );
        CheckPointClose( MapCameraView_Position( camera.get() ), position ); CheckPointClose( MapCameraView_Forward( camera.get() ), direction );
        REQUIRE( LookWheel( camera.get(), -240 ) ); CHECK( EditorSettings_Real( &session.gui.settings, "editor.camera.move_speed", 0.0 ) == 1000.0 );
        REQUIRE( LookWheel( camera.get(), 0, Qt::NoModifier, 20 ) );
        CHECK( EditorSettings_Real( &session.gui.settings, "editor.camera.move_speed", 0.0 ) == Catch::Approx( 1000.0 * std::sqrt( 2.0 ) ) );
        REQUIRE( LookWheel( camera.get(), 0, Qt::NoModifier, -20 ) );
        CHECK( EditorSettings_Real( &session.gui.settings, "editor.camera.move_speed", 0.0 ) == Catch::Approx( 1000.0 ) );
        DragButton( camera.get(), QEvent::MouseMove, point + QPointF( 20, 0 ), button );
        CHECK( TestDistance( MapCameraView_Forward( camera.get() ), direction ) > 0.01 );
        CHECK( TestDistance( MapCameraView_NavigationVelocity( camera.get() ), {} ) == Catch::Approx( 1000.0 ) );
        CheckPointClose( MapCameraView_Position( camera.get() ), position );
        CameraShiftTestKey( camera.get(), QEvent::KeyRelease, Qt::Key_W );
        DragButton( camera.get(), QEvent::MouseButtonRelease, point + QPointF( 20, 0 ), button );
        CheckPointClose( MapCameraView_NavigationVelocity( camera.get() ), {} );
        CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == geometry ); CHECK( ws.selection.revision == selection );
        CHECK( EditorHistory_StepCount( &ws.history ) == history ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    }
}

TEST_CASE( "Captured look wheel remaps and reservations preserve gesture ownership", "[map][gui][views][camera-look-wheel][keymap]" )
{
    for ( const int variant : { 0, 1, 2, 3 } ) {
        CAPTURE( variant ); session_t session; auto &ws = session.workspace;
        view_settings_t settings( &session.gui.settings ); settings.Real( "editor.camera.move_speed", 1000.0 ); settings.Set( "editor.camera.zoom_to_cursor", false );
        settings_document_t base{}, profile{};
        REQUIRE( SettingsDocument_Init( &base, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
        REQUIRE( SettingsDocument_Load( &base, StringView_FromCString( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "look_wheel_base" held = { "map.viewport.3d" = { "map.camera.forward" = [ "W" ] } }
  mouse = { "map.viewport.3d" = { "map.camera.look" = [ "RightDrag" ] "map.camera.dolly" = [ "Wheel" ] }
            "map.camera.look" = { "map.camera.speed_increase" = [ "WheelUp" ] "map.camera.speed_decrease" = [ "WheelDown" ] } } })cykv" ) ).status == settings_document_status_t::OK );
        const char *overrides[]{
            R"cykv({ id = "reversed" mouse = { "map.camera.look" = { "map.camera.speed_increase" = [ "WheelDown" ] "map.camera.speed_decrease" = [ "WheelUp" ] } } })cykv",
            R"cykv({ id = "unbound" mouse = { "map.camera.look" = { "map.camera.speed_increase" = [] } } })cykv",
            R"cykv({ id = "reserved" mouse = { "map.camera.look" = { "future.custom" = [ "Shift+WheelUp" ] } } })cykv",
            R"cykv({ id = "dolly_only" mouse = { "map.viewport.3d" = { "map.camera.look" = [ "RightDrag" ] "map.camera.dolly" = [ "Wheel" ] } } held = { "map.viewport.3d" = { "map.camera.forward" = [ "W" ] } } })cykv"
        };
        REQUIRE( SettingsDocument_Init( &profile, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
        const std::string text = std::string( "@cykv 1\n@schema \"cypher.editor_keymap\" 2\n" ) + overrides[variant];
        REQUIRE( SettingsDocument_Load( &profile, { text.data(), text.size() } ).status == settings_document_status_t::OK );
        session.gui.keymapChain[0] = SettingsDocument_Root( &profile ); session.gui.keymapChain[1] = SettingsDocument_Root( &base ); session.gui.nKeymapChain = variant == 3 ? 1u : 2u;
        std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
        look_wheel_context_requests_t requests; camera->installEventFilter( &requests ); const QPointF point( 300, 220 );
        DragButton( camera.get(), QEvent::MouseButtonPress, point, Qt::RightButton ); CameraShiftTestKey( camera.get(), QEvent::KeyPress, Qt::Key_W );
        const auto position = MapCameraView_Position( camera.get() ), direction = MapCameraView_Forward( camera.get() );
        REQUIRE( LookWheel( camera.get(), 120, variant == 2 ? Qt::ShiftModifier : Qt::NoModifier ) );
        const f64 expected = variant == 0 ? 500.0 : 1000.0;
        CHECK( EditorSettings_Real( &session.gui.settings, "editor.camera.move_speed", 0.0 ) == expected );
        CheckPointClose( MapCameraView_NavigationVelocity( camera.get() ), math::Vec3d_Scale( direction, expected ) );
        CheckPointClose( MapCameraView_Position( camera.get() ), variant == 3 ? math::Vec3d_Add( position, math::Vec3d_Scale( direction, 64.0 ) ) : position );
        CameraShiftTestKey( camera.get(), QEvent::KeyRelease, Qt::Key_W );
        // No mouse motion: handled wheel alone must suppress a release menu.
        DragButton( camera.get(), QEvent::MouseButtonRelease, point, Qt::RightButton );
        QCoreApplication::sendPostedEvents( camera.get(), QEvent::ContextMenu ); CHECK( requests.count == 0 );
        DragButton( camera.get(), QEvent::MouseButtonPress, point, Qt::RightButton );
        const auto after = MapCameraView_Position( camera.get() );
        REQUIRE( LookWheel( camera.get(), 120, variant == 2 ? Qt::ShiftModifier : Qt::NoModifier ) );
        DragButton( camera.get(), QEvent::MouseMove, point + QPointF( 10, 0 ), Qt::RightButton );
        CHECK( TestDistance( MapCameraView_Forward( camera.get() ), direction ) > 0.01 );
        if ( variant != 3 ) { CheckPointClose( MapCameraView_Position( camera.get() ), after ); }
        DragButton( camera.get(), QEvent::MouseButtonRelease, point + QPointF( 10, 0 ), Qt::RightButton );
    }
}

TEST_CASE( "Look wheel limits and allocation failure keep capture and suppress release menus", "[map][gui][views][camera-look-wheel][allocation][atomic]" )
{
    for ( const bool allocationFailure : { false, true } ) {
        CAPTURE( allocationFailure ); session_t session; auto &ws = session.workspace;
        view_settings_t settings( &session.gui.settings ); settings.Real( "editor.camera.move_speed", allocationFailure ? 1000.0 : 100000.0 );
        mesh_face_allocation_failure_t audit{}; const allocator_t allocator{ &MeshFaceTestAllocate, nullptr, &MeshFaceTestFree, &audit };
        settings_document_t scope{}; REQUIRE( SettingsDocument_Init( &scope, &allocator, EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
        EditorSettings_SetScope( &session.gui.settings, settings_scope_t::WORKSPACE, &scope );
        std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
        look_wheel_context_requests_t requests; camera->installEventFilter( &requests ); const QPointF point( 300, 220 );
        DragButton( camera.get(), QEvent::MouseButtonPress, point, Qt::RightButton ); CameraShiftTestKey( camera.get(), QEvent::KeyPress, Qt::Key_W );
        const auto position = MapCameraView_Position( camera.get() ), direction = MapCameraView_Forward( camera.get() );
        const auto *tree = scope.pDocument; const usize live = audit.live; audit.calls = 0; audit.failOn = allocationFailure ? 1u : 0u;
        REQUIRE( LookWheel( camera.get(), 480 ) ); audit.failOn = 0u;
        const f64 speed = allocationFailure ? 1000.0 : 100000.0;
        CHECK( EditorSettings_Real( &session.gui.settings, "editor.camera.move_speed", 0.0 ) == speed );
        CHECK( scope.pDocument == tree ); CHECK( audit.live == live );
        CheckPointClose( MapCameraView_Position( camera.get() ), position ); CheckPointClose( MapCameraView_Forward( camera.get() ), direction );
        CheckPointClose( MapCameraView_NavigationVelocity( camera.get() ), math::Vec3d_Scale( direction, speed ) );
        CameraShiftTestKey( camera.get(), QEvent::KeyRelease, Qt::Key_W ); DragButton( camera.get(), QEvent::MouseButtonRelease, point, Qt::RightButton );
        QCoreApplication::sendPostedEvents( camera.get(), QEvent::ContextMenu ); CHECK( requests.count == 0 );
        DragButton( camera.get(), QEvent::MouseButtonPress, point, Qt::RightButton ); REQUIRE( LookWheel( camera.get(), -120 ) );
        CHECK( EditorSettings_Real( &session.gui.settings, "editor.camera.move_speed", 0.0 ) == speed * 0.5 );
        DragButton( camera.get(), QEvent::MouseMove, point + QPointF( 10, 0 ), Qt::RightButton ); CHECK( TestDistance( MapCameraView_Forward( camera.get() ), direction ) > 0.01 );
        DragButton( camera.get(), QEvent::MouseButtonRelease, point + QPointF( 10, 0 ), Qt::RightButton );
        camera.reset(); EditorSettings_SetScope( &session.gui.settings, settings_scope_t::WORKSPACE, nullptr ); SettingsDocument_Shutdown( &scope ); CHECK( audit.live == 0u );
    }
}

TEST_CASE( "Look owns Shift wheel over staged construction and focus or Escape resets flight", "[map][gui][views][camera-look-wheel][block][focus]" )
{
    for ( const bool cancel : { false, true } ) {
        CAPTURE( cancel ); session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        view_settings_t settings( &session.gui.settings ); settings.Real( "editor.camera.move_speed", 1000.0 );
        MapWorkspace_SetTool( &ws, map_tool_t::BLOCK ); map_bounds_t bounds{}; MapBounds_AddPoint( bounds, { 0, 0, 0 } ); MapBounds_AddPoint( bounds, { 64, 64, 64 } );
        MapWorkspace_SetEditPreview( &ws, bounds ); REQUIRE( MapWorkspace_StageBlockPreview( &ws ) );
        const auto geometry = ws.pDocument->geometry.revision; const auto history = EditorHistory_StepCount( &ws.history );
        std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, &ws ) ); ShowAt( camera.get(), 800, 600 );
        const QPointF point( 300, 220 ); DragButton( camera.get(), QEvent::MouseButtonPress, point, Qt::RightButton );
        CameraShiftTestKey( camera.get(), QEvent::KeyPress, Qt::Key_W ); REQUIRE( LookWheel( camera.get(), 120, Qt::ShiftModifier ) );
        REQUIRE( MapWorkspace_HasBlockPreview( &ws ) ); CheckPointClose( ws.editPreview.bounds.box.minimum, bounds.box.minimum ); CheckPointClose( ws.editPreview.bounds.box.maximum, bounds.box.maximum );
        CHECK( EditorSettings_Real( &session.gui.settings, "editor.camera.move_speed", 0.0 ) == 2000.0 );
        REQUIRE( TestDistance( MapCameraView_NavigationVelocity( camera.get() ), {} ) > 1999.0 );
        const auto position = MapCameraView_Position( camera.get() ), direction = MapCameraView_Forward( camera.get() );
        if ( cancel ) { CameraShiftTestKey( camera.get(), QEvent::KeyPress, Qt::Key_Escape ); }
        else { QFocusEvent out( QEvent::FocusOut, Qt::OtherFocusReason ); QCoreApplication::sendEvent( camera.get(), &out ); }
        CameraShiftTestKey( camera.get(), QEvent::KeyPress, Qt::Key_W, Qt::NoModifier, true, false );
        CheckPointClose( MapCameraView_NavigationVelocity( camera.get() ), {} );
        DragButton( camera.get(), QEvent::MouseMove, point + QPointF( 10, 0 ), Qt::RightButton ); DragButton( camera.get(), QEvent::MouseButtonRelease, point + QPointF( 10, 0 ), Qt::RightButton );
        CheckPointClose( MapCameraView_Position( camera.get() ), position ); CheckPointClose( MapCameraView_Forward( camera.get() ), direction );
        CHECK( ws.tool == map_tool_t::BLOCK ); CHECK( ws.pDocument->geometry.revision == geometry ); CHECK( EditorHistory_StepCount( &ws.history ) == history );
    }
}


namespace
{
struct alignment_scene_t {
    session_t session;
    view_settings_t settings{ &session.gui.settings };
    u64 source{}, target{}, companion{};
    std::unique_ptr<QWidget> view;
    u32 axisU{}, axisV{};
    explicit alignment_scene_t( map_ortho_axes_t axes, bool camera = false, bool multiple = false ) {
        auto &ws = session.workspace;
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        axisU = axes == map_ortho_axes_t::FRONT ? 1u : 0u;
        axisV = axes == map_ortho_axes_t::TOP ? 1u : 2u;
        const u32 depth = 3u - axisU - axisV;
        const auto create = [&]( f64 lo, f64 hi, f64 offset ) {
            math::vec3d_t a{}, b{};
            SetTestCoordinate( a, axisU, lo ); SetTestCoordinate( b, axisU, hi );
            SetTestCoordinate( a, axisV, offset - 32 ); SetTestCoordinate( b, axisV, offset + 32 );
            SetTestCoordinate( b, depth, 64 );
            map_bounds_t box{}; MapBounds_AddPoint( box, a ); MapBounds_AddPoint( box, b );
            REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
            return EditorSelection_At( &ws.selection, 0u );
        };
        source = create( 0, 64, 0 );
        if ( multiple ) { companion = create( 0, 64, 96 ); }
        // This placement was authored on grid 16, rather than today's 64.
        target = create( 144, 176, 0 );
        const u64 ids[]{ source, companion };
        MapWorkspace_SetSelection( &ws, ids, multiple ? 2u : 1u );
        MapWorkspace_SetGridSize( &ws, 64 );
        settings.Set( "editor.grid.geometry_snap", true );
        view.reset( camera ? MapCameraView_Create( nullptr, &ws ) : MapOrthoView_Create( nullptr, &ws, axes ) );
        ShowAt( view.get(), 800, 600 );
        MapWorkspace_Frame( &ws, CY_FALSE ); QCoreApplication::processEvents();
    }
    QPointF Project( math::vec3d_t point, bool camera = false ) {
        QPointF result;
        if ( camera ) { REQUIRE( MapCameraView_WorldToView( view.get(), point, &result ) ); }
        else { result = MapOrthoView_WorldToView( view.get(), { TestCoordinate( point, axisU ), TestCoordinate( point, axisV ) } ); }
        return result;
    }
    std::pair<QPointF, QPointF> AxisDrag( f64 travel, bool camera = false ) {
        const auto *object = MapWireframe_FindObject( session.workspace.wire, source ); REQUIRE( object != nullptr );
        auto pivot = MapBounds_Center( object->bounds );
        if ( companion != 0u ) { pivot = MapBounds_Center( MapViews_SelectionGeometryBounds( &session.workspace ) ); }
        const f64 length = camera ? CameraGizmoLength( view.get(), pivot, session.gui.settings ) : 64.0 / MapOrthoView_Zoom( view.get() );
        auto pickup = pivot; SetTestCoordinate( pickup, axisU, TestCoordinate( pickup, axisU ) + length * 0.70 );
        auto end = pickup; SetTestCoordinate( end, axisU, TestCoordinate( end, axisU ) + travel );
        return { Project( pickup, camera ), Project( end, camera ) };
    }
};
}

TEST_CASE( "Geometry movement snapping aligns different grids in every orthographic pane without changing selection spacing", "[map][gui][views][geometry-snap]" )
{
    for ( const auto axes : { map_ortho_axes_t::TOP, map_ortho_axes_t::FRONT, map_ortho_axes_t::SIDE } ) {
        for ( const auto mode : { map_element_mode_t::OBJECTS, map_element_mode_t::GROUPS, map_element_mode_t::MESHES } ) {
            for ( const bool multiple : { false, true } ) {
                CAPTURE( static_cast<int>( axes ), static_cast<int>( mode ), multiple );
                alignment_scene_t scene( axes, false, multiple ); auto &ws = scene.session.workspace;
                MapWorkspace_SetElementMode( &ws, mode );
                const auto sourceBefore = MapWireframe_FindObject( ws.wire, scene.source )->bounds;
                const auto targetBefore = MapWireframe_FindObject( ws.wire, scene.target )->bounds;
                const usize steps = EditorHistory_StepCount( &ws.history ); const auto *document = ws.pDocument;
                const auto [start, end] = scene.AxisDrag( 78 );
                DragMouse( scene.view.get(), QEvent::MouseButtonPress, start ); DragMouse( scene.view.get(), QEvent::MouseMove, end );
                REQUIRE( ws.editPreview.bActive );
                CHECK( TestCoordinate( ws.editPreview.transform.delta, scene.axisU ) == Catch::Approx( 80 ) );
                CHECK( TestCoordinate( ws.editPreview.transform.delta, scene.axisV ) == 0 );
                CHECK( TestCoordinate( ws.editPreview.transform.delta, 3u - scene.axisU - scene.axisV ) == 0 );
                CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
                CHECK( ws.gridSize == 64 ); CHECK( ws.bSnapToGrid );
                DragMouse( scene.view.get(), QEvent::MouseButtonRelease, end );
                CHECK_FALSE( ws.editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1 );
                const auto moved = MapWireframe_FindObject( ws.wire, scene.source )->bounds;
                CHECK( TestCoordinate( moved.box.maximum, scene.axisU ) == 144 );
                CheckPointClose( MapWireframe_FindObject( ws.wire, scene.target )->bounds.box.minimum, targetBefore.box.minimum );
                if ( multiple ) {
                    const auto second = MapWireframe_FindObject( ws.wire, scene.companion )->bounds;
                    CHECK( TestCoordinate( second.box.maximum, scene.axisU ) == 144 );
                    CHECK( TestCoordinate( second.box.minimum, scene.axisV ) - TestCoordinate( moved.box.minimum, scene.axisV ) == 96 );
                }
                REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
                CheckPointClose( MapWireframe_FindObject( ws.wire, scene.source )->bounds.box.minimum, sourceBefore.box.minimum );
                REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
                CheckPointClose( MapWireframe_FindObject( ws.wire, scene.source )->bounds.box.maximum, moved.box.maximum );
                CHECK( EditorSelection_Count( &ws.selection ) == ( multiple ? 2u : 1u ) );
                CHECK( MapWorkspace_IsSelected( &ws, scene.source ) ); CHECK_FALSE( MapWorkspace_IsSelected( &ws, scene.target ) );
                if ( axes == map_ortho_axes_t::TOP && mode == map_element_mode_t::OBJECTS && multiple ) {
                    QTemporaryDir dir; REQUIRE( dir.isValid() ); const QString file = dir.filePath( "aligned.cymap" );
                    REQUIRE( MapWorkspace_SaveAs( &ws, file ).status == map_files_status_t::OK );
                    session_t reopened; REQUIRE( MapWorkspace_Open( &reopened.workspace, file ).status == map_files_status_t::OK );
                    CheckPointClose( MapWireframe_FindObject( reopened.workspace.wire, scene.source )->bounds.box.maximum, moved.box.maximum );
                    CheckPointClose( MapWireframe_FindObject( reopened.workspace.wire, scene.target )->bounds.box.minimum, targetBefore.box.minimum );
                }
            }
        }
    }
}

TEST_CASE( "Geometry snapping is opt in and bypassable with a screen-space tolerance independent of world grid", "[map][gui][views][geometry-snap]" )
{
    for ( int variant = 0; variant < 7; ++variant ) {
        CAPTURE( variant ); alignment_scene_t scene( map_ortho_axes_t::TOP ); auto &ws = scene.session.workspace;
        Qt::KeyboardModifiers modifiers = Qt::NoModifier; f64 travel = 78, expected = 80;
        if ( variant == 0 ) { scene.settings.Set( "editor.grid.geometry_snap", false ); expected = 64; }
        if ( variant == 1 ) { modifiers = Qt::ControlModifier; expected = travel; }
        if ( variant == 2 ) { modifiers = Qt::MetaModifier; expected = travel; }
        if ( variant == 3 ) { travel = 80 - 12.0 / MapOrthoView_Zoom( scene.view.get() ); expected = 64; }
        if ( variant == 4 ) { MapWorkspace_SetSnapToGrid( &ws, CY_FALSE ); }
        if ( variant == 5 ) { scene.settings.Real( "editor.grid.geometry_snap_pixels", 2 ); travel = 80 - 3.0 / MapOrthoView_Zoom( scene.view.get() ); expected = 64; }
        if ( variant == 6 ) { MapWorkspace_SetTool( &ws, map_tool_t::TRANSLATE ); }
        const auto [start, end] = scene.AxisDrag( travel ); const usize steps = EditorHistory_StepCount( &ws.history );
        // Bypass is a live drag modifier; its press is ordinary selection.
        DragMouse( scene.view.get(), QEvent::MouseButtonPress, start ); DragMouse( scene.view.get(), QEvent::MouseMove, end, modifiers );
        REQUIRE( ws.editPreview.bActive ); CHECK( ws.editPreview.transform.delta.x == Catch::Approx( expected ) );
        CHECK( ws.editPreview.transform.delta.y == 0 ); CHECK( ws.editPreview.transform.delta.z == 0 );
        DragMouse( scene.view.get(), QEvent::MouseButtonRelease, end, modifiers );
        CHECK( MapWireframe_FindObject( ws.wire, scene.source )->bounds.box.minimum.x == Catch::Approx( expected ) );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1 ); CHECK( ws.gridSize == 64 );
        CHECK( EditorSettings_Bool( &scene.session.gui.settings, "editor.grid.geometry_snap", CY_FALSE ) == ( variant != 0 ) );
    }
}

TEST_CASE( "Geometry snapping ignores hidden offscreen and selected-owner targets and cancels stale guides", "[map][gui][views][geometry-snap]" )
{
    for ( int variant = 0; variant < 6; ++variant ) {
        CAPTURE( variant ); alignment_scene_t scene( map_ortho_axes_t::TOP ); auto &ws = scene.session.workspace;
        f64 expected = 64; u64 owner = 0;
        if ( variant == 0 ) { REQUIRE( EditorSelection_Apply( &ws.hidden, scene.target, EDITOR_SELECT_ADD ) ); MapWorkspace_Notify( &ws, MAP_CHANGE_VIEW ); }
        if ( variant == 1 ) { MapWorkspace_Select( &ws, scene.target, MAP_SELECT_REPLACE ); REQUIRE( MapWorkspace_TranslateSelection( &ws, { 0, 100000, 0 } ) );
            MapWorkspace_Select( &ws, scene.source, MAP_SELECT_REPLACE ); }
        if ( variant == 2 ) {
            REQUIRE( MapDocument_AddEntity( ws.pDocument, StringView_FromCString( "default" ), StringView_FromCString( "func_door" ), { -800, -800, 0 }, &owner ) == map_status_t::OK );
            REQUIRE( MapDocument_SetGeometryOwner( ws.pDocument, scene.source, owner ) == map_status_t::OK );
            MapWorkspace_DocumentChanged( &ws ); MapWorkspace_Select( &ws, owner, MAP_SELECT_REPLACE ); expected = 80;
        }
        const auto [start, end] = scene.AxisDrag( 78 ); const auto *document = ws.pDocument; const usize steps = EditorHistory_StepCount( &ws.history );
        if ( variant == 2 ) {
            // Pick a geometry body, since the owner helper changes its gizmo pivot.
            const QPointF body = scene.Project( { 16, 0, 32 } );
            DragMouse( scene.view.get(), QEvent::MouseButtonPress, body );
            DragMouse( scene.view.get(), QEvent::MouseMove, body + end - start );
        } else { DragMouse( scene.view.get(), QEvent::MouseButtonPress, start ); DragMouse( scene.view.get(), QEvent::MouseMove, end ); }
        REQUIRE( ws.editPreview.bActive ); CHECK( ws.editPreview.transform.delta.x == Catch::Approx( variant >= 3 ? 80 : expected ) );
        if ( variant == 3 ) { scene.settings.Set( "editor.grid.geometry_snap", false ); }
        if ( variant == 4 ) { scene.settings.Real( "editor.grid.geometry_snap_pixels", 12 ); }
        if ( variant == 5 ) { QFocusEvent lost( QEvent::FocusOut ); QCoreApplication::sendEvent( scene.view.get(), &lost ); }
        if ( variant >= 3 ) {
            CHECK_FALSE( ws.editPreview.bActive ); DragMouse( scene.view.get(), QEvent::MouseButtonRelease, end );
            CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
        } else {
            QKeyEvent cancel( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( scene.view.get(), &cancel );
            CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
        }
    }
}

TEST_CASE( "Camera geometry snapping respects axis and world-plane constraints and never changes free-move plane semantics", "[map][gui][views][geometry-snap]" )
{
    for ( int handle = 0; handle < 3; ++handle ) {
        CAPTURE( handle ); alignment_scene_t scene( map_ortho_axes_t::TOP, true ); auto &ws = scene.session.workspace;
        const auto pivot = MapBounds_Center( MapWireframe_FindObject( ws.wire, scene.source )->bounds );
        const f64 length = CameraGizmoLength( scene.view.get(), pivot, scene.session.gui.settings );
        auto pickup = pivot;
        if ( handle == 0 ) { pickup.x += length * .70; }
        if ( handle == 1 ) { pickup.x += length * .31; pickup.y += length * .31; }
        const auto start = scene.Project( pickup, true ); auto target = pickup; target.x += 78;
        const auto end = scene.Project( target, true ); const auto *document = ws.pDocument;
        DragMouse( scene.view.get(), QEvent::MouseButtonPress, start ); DragMouse( scene.view.get(), QEvent::MouseMove, end );
        REQUIRE( ws.editPreview.bActive ); CHECK( ws.pDocument == document );
        if ( handle < 2 ) { CHECK( ws.editPreview.transform.delta.x == Catch::Approx( 80 ) ); CHECK( ws.editPreview.transform.delta.y == 0 ); CHECK( ws.editPreview.transform.delta.z == 0 ); }
        else { for ( const auto v : { ws.editPreview.transform.delta.x, ws.editPreview.transform.delta.y, ws.editPreview.transform.delta.z } ) { CHECK( v / 64.0 == Catch::Approx( std::round( v / 64.0 ) ) ) ; } }
        DragMouse( scene.view.get(), QEvent::MouseButtonRelease, end );
        CHECK_FALSE( ws.editPreview.bActive );
    }
}

TEST_CASE( "An allocation failure at aligned move publication preserves geometry selection and history", "[map][gui][views][geometry-snap][allocation]" )
{
    alignment_scene_t scene( map_ortho_axes_t::TOP ); auto &ws = scene.session.workspace;
    const auto before = MapWireframe_FindObject( ws.wire, scene.source )->bounds;
    const auto *document = ws.pDocument; const usize steps = EditorHistory_StepCount( &ws.history ); const u64 revision = ws.selection.revision;
    const auto [start, end] = scene.AxisDrag( 78 );
    DragMouse( scene.view.get(), QEvent::MouseButtonPress, start ); DragMouse( scene.view.get(), QEvent::MouseMove, end );
    REQUIRE( ws.editPreview.bActive ); CHECK( ws.editPreview.transform.delta.x == Catch::Approx( 80 ) );
    mesh_face_allocation_failure_t audit{ 0, 1, 0 };
    const allocator_t allocator{ &MeshFaceTestAllocate, nullptr, &MeshFaceTestFree, &audit };
    const auto *previous = ws.pDocument->pAllocator; ws.pDocument->pAllocator = &allocator;
    DragMouse( scene.view.get(), QEvent::MouseButtonRelease, end ); ws.pDocument->pAllocator = previous;
    CHECK( audit.calls > 0 ); CHECK( audit.live == 0 ); CHECK( ws.pDocument == document );
    CHECK_FALSE( ws.editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    CHECK( ws.selection.revision == revision ); CHECK( MapWorkspace_IsSelected( &ws, scene.source ) );
    CheckPointClose( MapWireframe_FindObject( ws.wire, scene.source )->bounds.box.minimum, before.box.minimum );
    // Recovery starts a new visible gesture, not a late invisible release.
    DragMouse( scene.view.get(), QEvent::MouseButtonRelease, end ); CHECK( ws.pDocument == document );
    DragMouse( scene.view.get(), QEvent::MouseButtonPress, start ); DragMouse( scene.view.get(), QEvent::MouseMove, end );
    DragMouse( scene.view.get(), QEvent::MouseButtonRelease, end );
    CHECK( MapWireframe_FindObject( ws.wire, scene.source )->bounds.box.maximum.x == 144 );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1 );
}


TEST_CASE( "Alignment candidates respect locked-axis diagonal pointer motion and zero travel", "[map][gui][views][geometry-snap]" )
{
    alignment_scene_t scene( map_ortho_axes_t::TOP ); auto &ws = scene.session.workspace;
    const auto [start, alignedEnd] = scene.AxisDrag( 78 );
    const auto *document = ws.pDocument; const usize steps = EditorHistory_StepCount( &ws.history );
    DragMouse( scene.view.get(), QEvent::MouseButtonPress, start );
    DragMouse( scene.view.get(), QEvent::MouseMove, start + QPointF( 0, 200 ) );
    // Existing transform presentation can retain an identity preview after a
    // real pointer gesture. It must not acquire any alignment movement.
    CheckPointClose( ws.editPreview.transform.delta, {} );
    CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    // Irrelevant Y travel can even leave the viewport. The X shaft constrains
    // the real candidate and its guide, before screen-space distance is tested.
    const QPointF diagonal = alignedEnd + QPointF( 0, 800 );
    DragMouse( scene.view.get(), QEvent::MouseMove, diagonal );
    REQUIRE( ws.editPreview.bActive ); CheckPointClose( ws.editPreview.transform.delta, { 80, 0, 0 } );
    DragMouse( scene.view.get(), QEvent::MouseMove, start );
    CHECK_FALSE( ws.editPreview.bActive );
    DragMouse( scene.view.get(), QEvent::MouseButtonRelease, start );
    CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
}

TEST_CASE( "Geometry snapping handles negative travel mesh targets center alignment and cloning", "[map][gui][views][geometry-snap]" )
{
    for ( int variant = 0; variant < 4; ++variant ) {
        CAPTURE( variant ); alignment_scene_t scene( map_ortho_axes_t::TOP ); auto &ws = scene.session.workspace;
        f64 travel = 78, expected = 80;
        if ( variant == 0 ) {
            MapWorkspace_Select( &ws, scene.target, MAP_SELECT_REPLACE ); REQUIRE( MapWorkspace_TranslateSelection( &ws, { -256, 0, 0 } ) );
            travel = -78; expected = -80; MapWorkspace_Frame( &ws, CY_FALSE ); QCoreApplication::processEvents();
        }
        if ( variant == 1 ) { MapWorkspace_Select( &ws, scene.target, MAP_SELECT_REPLACE ); REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) ); }
        if ( variant == 2 ) {
            MapWorkspace_Select( &ws, scene.target, MAP_SELECT_REPLACE ); REQUIRE( MapWorkspace_ScaleSelection( &ws, { 1.5, 1, 1 }, { 144, 0, 0 } ) );
            // Target 144..192 has center168; source0..64 has center32.
            // Only center alignment produces136; edge pairs give80/128/144/192.
            travel = 134; expected = 136;
        }
        MapWorkspace_Select( &ws, scene.source, MAP_SELECT_REPLACE );
        const usize steps = EditorHistory_StepCount( &ws.history ); const auto original = MapWireframe_FindObject( ws.wire, scene.source )->bounds;
        const auto [start, end] = scene.AxisDrag( travel ); const auto modifiers = variant == 3 ? Qt::ShiftModifier : Qt::NoModifier;
        DragMouse( scene.view.get(), QEvent::MouseButtonPress, start, modifiers ); DragMouse( scene.view.get(), QEvent::MouseMove, end, modifiers );
        REQUIRE( ws.editPreview.bActive ); CHECK( ws.editPreview.transform.delta.x == Catch::Approx( expected ) );
        DragMouse( scene.view.get(), QEvent::MouseButtonRelease, end, modifiers );
        const u64 moved = EditorSelection_At( &ws.selection, 0 );
        CHECK( MapWireframe_FindObject( ws.wire, moved )->bounds.box.minimum.x == Catch::Approx( expected ) );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1 );
        if ( variant == 3 ) { CHECK( moved != scene.source ); CheckPointClose( MapWireframe_FindObject( ws.wire, scene.source )->bounds.box.minimum, original.box.minimum ); }
        else { CHECK( moved == scene.source ); }
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
        CheckPointClose( MapWireframe_FindObject( ws.wire, scene.source )->bounds.box.minimum, original.box.minimum );
        REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
        CHECK( MapWireframe_FindObject( ws.wire, moved )->bounds.box.minimum.x == Catch::Approx( expected ) );
    }
}


TEST_CASE( "Selected owner geometry and point-entity helper bounds cannot attract their own aligned move", "[map][gui][views][geometry-snap]" )
{
    for ( const bool owned : { false, true } ) {
        CAPTURE( owned ); alignment_scene_t scene( map_ortho_axes_t::TOP ); auto &ws = scene.session.workspace;
        u64 owner = 0;
        REQUIRE( MapDocument_AddEntity( ws.pDocument, StringView_FromCString( "default" ), StringView_FromCString( owned ? "func_door" : "info_player_start" ),
            owned ? math::vec3d_t{ -800, -800, 0 } : math::vec3d_t{ 142, 0, 32 }, &owner ) == map_status_t::OK );
        if ( owned ) {
            REQUIRE( MapDocument_SetGeometryOwner( ws.pDocument, scene.source, owner ) == map_status_t::OK );
            REQUIRE( MapDocument_SetGeometryOwner( ws.pDocument, scene.target, owner ) == map_status_t::OK );
        } else { REQUIRE( EditorSelection_Apply( &ws.hidden, scene.target, EDITOR_SELECT_ADD ) ); }
        MapWorkspace_DocumentChanged( &ws ); MapWorkspace_Select( &ws, owned ? owner : scene.source, MAP_SELECT_REPLACE );
        const QPointF start = scene.Project( { 16, 0, 32 } ), end = start + QPointF( 86 * MapOrthoView_Zoom( scene.view.get() ), 0 );
        DragMouse( scene.view.get(), QEvent::MouseButtonPress, start ); DragMouse( scene.view.get(), QEvent::MouseMove, end );
        REQUIRE( ws.editPreview.bActive ); CheckPointClose( ws.editPreview.transform.delta, { 64, 0, 0 } );
        DragMouse( scene.view.get(), QEvent::MouseButtonRelease, end );
        CHECK( MapWireframe_FindObject( ws.wire, scene.source )->bounds.box.minimum.x == 64 );
        if ( owned ) { CHECK( MapWireframe_FindObject( ws.wire, scene.target )->bounds.box.minimum.x == 208 ); }
        else { CHECK( MapWireframe_FindObject( ws.wire, scene.target )->bounds.box.minimum.x == 144 ); }
    }
}
