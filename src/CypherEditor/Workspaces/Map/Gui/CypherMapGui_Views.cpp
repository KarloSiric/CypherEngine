//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Views.cpp
//  Purpose: Implements the 2D and 3D map views and their arrangement.
//  Details: Lines are gathered per colour and drawn with one drawLines call
//           each; per-segment drawLine calls are what makes QPainter slow on
//           a large map. Objects whose bounds miss the visible area are
//           skipped before any of their lines are touched.
//
//           The view classes are private to this file; the header exposes
//           free functions, as the framework contract asks.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Views.h"

#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_MeshSelection.h"
#include "CypherMapGui_Input.h"

#include "CypherEditorGui_Actions.h"
#include "CypherEditorGui_Style.h"
#include "CypherEditor_Keymap.h"
#include "CypherGeometry_RaycastQueries.h"
#include "CypherGeometry_HeightField.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Mathlib/CypherMath_Polygon.h"

#include <QApplication>
#include <QAbstractSpinBox>
#include <QActionGroup>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHash>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QRadialGradient>
#include <QPointer>
#include <QPolygonF>
#include <QPlainTextEdit>
#include <QSet>
#include <QSplitter>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QTextEdit>
#include <QToolButton>
#include <QFileDialog>
#include <limits>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QVector>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace cypher::editor::map
{

using namespace cypher::common;
using cypher::editor::gui::EditorStyle_Color;

namespace
{

constexpr f64 kZoomStep = 1.2;            // Per wheel notch.
constexpr f64 kCameraNear = 1.0;          // World units; lines closer are clipped.
constexpr usize kSurfaceGridMaxCandidates = 32768u;
constexpr usize kSurfaceGridMaxFaces = 4096u;
constexpr int kSurfaceGridLinesPerFamily = 64;
constexpr f64 kCameraWheelStep = 64.0;    // Per wheel notch.
constexpr int kCameraTickMs = 16;

f64 Axis( const math::vec3d_t &point, u32 axis ) noexcept
{
    return axis == 0u ? point.x : ( axis == 1u ? point.y : point.z );
}

math::vec3d_t Add( math::vec3d_t a, math::vec3d_t b ) noexcept { return math::Vec3d_Make( a.x + b.x, a.y + b.y, a.z + b.z ); }
math::vec3d_t Sub( math::vec3d_t a, math::vec3d_t b ) noexcept { return math::Vec3d_Make( a.x - b.x, a.y - b.y, a.z - b.z ); }
math::vec3d_t Scale( math::vec3d_t a, f64 s ) noexcept { return math::Vec3d_Make( a.x * s, a.y * s, a.z * s ); }
f64 Dot( math::vec3d_t a, math::vec3d_t b ) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }

bool MeshComponentMode( const map_workspace_t &workspace ) noexcept
{
    return workspace.elementMode == map_element_mode_t::VERTICES || workspace.elementMode == map_element_mode_t::EDGES;
}

// Colour slots a view batches lines into.
enum line_color_t : u32 {
    LINE_WORLD = 0u,
    LINE_TIED,
    LINE_MESH,
    LINE_PATCH,
    LINE_TERRAIN,
    LINE_ENTITY,
    LINE_ENTITY_LIGHT,
    LINE_ENTITY_INFO,
    LINE_ENTITY_TRIGGER,
    LINE_ENTITY_PROP,
    LINE_HOVER,    // Drawn after the plain slots, so what is about to be clicked stands out.
    LINE_SELECTED, // Drawn last of all.
    LINE_COUNT
};

bool EditPreviewTarget( const map_workspace_t &ws, u64 id ) noexcept {
    if ( !ws.editPreview.bActive || ws.editPreview.status != map_status_t::OK ) { return false; }
    if ( ws.editPreview.bFacePushPull ) { return MapWorkspace_HasFacePreview( &ws ) && id == ws.editPreview.faceObject; }
    if ( ws.editPreview.bClip ) { return MapWorkspace_IsSelected( &ws, id ); }
    const auto *object = MapWireframe_FindObject( ws.wire, id );
    return ws.editPreview.transform.kind != map_transform_preview_kind_t::NONE && object != nullptr &&
        MapWorkspace_IsTransformPreviewObject( &ws, *object );
}
bool MoveSourceActive( const map_workspace_t &ws ) noexcept {
    const auto &transform = ws.editPreview.transform;
    return ws.editPreview.bActive && ws.editPreview.status == map_status_t::OK &&
        transform.kind == map_transform_preview_kind_t::TRANSLATE &&
        ( transform.delta.x != 0.0 || transform.delta.y != 0.0 || transform.delta.z != 0.0 );
}
bool ObjectSelected( const map_workspace_t &ws, const map_wire_object_t &object ) noexcept {
    // Selecting an entity selects its owned geometry for whole-object edits.
    // Keep the same highlight before, during and after the edit.
    // Component selection keeps a root for inspection. It must not turn
    // that entire mesh (or its entity owner) into a selected solid.
    if ( MeshComponentMode( ws ) ) { return false; }
    return MapWorkspace_IsSelected( &ws, object.id ) ||
        ( ws.elementMode != map_element_mode_t::MESHES && object.owner != 0u && MapWorkspace_IsSelected( &ws, object.owner ) );
}
bool HitSelected( const map_workspace_t &ws, u64 id ) noexcept {
    const auto *object = MapWireframe_FindObject( ws.wire, id );
    return object != nullptr && ObjectSelected( ws, *object );
}
// Selection wins over hover: a selected object stays yellow under the
// pointer, as in Hammer, so the colour always says what a click would change.
line_color_t ColorSlot( const map_workspace_t &workspace, const map_wire_object_t &object, u64 hoverId = 0u ) noexcept
{
    if ( EditPreviewTarget( workspace, object.id ) ) { return LINE_WORLD; }
    if ( ObjectSelected( workspace, object ) ) { return LINE_SELECTED; }
    if ( !MeshComponentMode( workspace ) && hoverId != 0u && object.id == hoverId ) { return LINE_HOVER; }
    switch ( object.kind ) {
        case map_wire_kind_t::BRUSH: return object.owner != 0u ? LINE_TIED : LINE_WORLD;
        case map_wire_kind_t::MESH: return object.owner != 0u ? LINE_TIED : LINE_MESH;
        case map_wire_kind_t::PATCH: return object.owner != 0u ? LINE_TIED : LINE_PATCH;
        case map_wire_kind_t::TERRAIN: return LINE_TERRAIN;
        case map_wire_kind_t::ENTITY:
            switch ( MapWorkspace_VisgroupOf( &workspace, object ) ) {
                case map_visgroup_t::LIGHTS: return LINE_ENTITY_LIGHT;
                case map_visgroup_t::INFO: return LINE_ENTITY_INFO;
                case map_visgroup_t::TRIGGERS: return LINE_ENTITY_TRIGGER;
                case map_visgroup_t::PROPS: return LINE_ENTITY_PROP;
                default: return LINE_ENTITY;
            }
    }
    return LINE_WORLD;
}

QColor SlotColor( const map_workspace_t &workspace, line_color_t slot ) noexcept
{
    const gui::editor_style_t &style = workspace.pGui->style;
    switch ( slot ) {
        case LINE_WORLD: return EditorStyle_Color( style, gui::STYLE_COLOR_WIRE );
        case LINE_TIED: return gui::EditorStyle_TokenColor( style, "viewport.wire.entity" );
        case LINE_MESH: return gui::EditorStyle_TokenColor( style, "viewport.wire.mesh" );
        case LINE_PATCH: return gui::EditorStyle_TokenColor( style, "viewport.wire.patch" );
        case LINE_TERRAIN: return gui::EditorStyle_TokenColor( style, "viewport.wire.terrain" );
        case LINE_ENTITY: return gui::EditorStyle_TokenColor( style, "viewport.entity.default" );
        case LINE_ENTITY_LIGHT: return gui::EditorStyle_TokenColor( style, "viewport.entity.light" );
        case LINE_ENTITY_INFO: return gui::EditorStyle_TokenColor( style, "viewport.entity.info" );
        case LINE_ENTITY_TRIGGER: return gui::EditorStyle_TokenColor( style, "viewport.entity.trigger" );
        case LINE_ENTITY_PROP: return gui::EditorStyle_TokenColor( style, "viewport.entity.prop" );
        case LINE_HOVER: return gui::EditorStyle_TokenColor( style, "viewport.hover" );
        case LINE_SELECTED: return EditorStyle_Color( style, gui::STYLE_COLOR_SELECTION );
        case LINE_COUNT: break;
    }
    return Qt::white;
}

QColor Token( const map_workspace_t &workspace, const char *pId )
{
    return gui::EditorStyle_TokenColor( workspace.pGui->style, pId );
}

// The view under the pointer is outlined, and only while the pointer is
// there: the outline says where a click or key will land, and leaving the
// view takes it away (it never lingers on a view worked in earlier).
void DrawFocusBorder( QPainter &painter, const QWidget &view, const map_workspace_t &workspace )
{
    if ( !view.underMouse() || !EditorSettings_Bool( &workspace.pGui->settings, "editor.viewport.active_border", CY_FALSE ) ) { return; }
    painter.setPen( QPen( EditorStyle_Color( workspace.pGui->style, gui::STYLE_COLOR_ACTIVE_VIEW ), 2.0 ) );
    painter.setBrush( Qt::NoBrush );
    painter.drawRect( QRectF( 1.0, 1.0, view.width() - 2.0, view.height() - 2.0 ) );
}

// Whole numbers without a decimal point: rulers read "64", not "64.0".
QString NumberText( f64 value )
{
    const f64 rounded = std::round( value );
    return std::fabs( value - rounded ) < 1e-6 ? QString::number( static_cast<qlonglong>( rounded ) ) : QString::number( value, 'g', 6 );
}

const char kAxisLetters[3]{ 'X', 'Y', 'Z' };

bool ShowMetrics( const map_workspace_t &workspace, bool perspective = false )
{
    return EditorSettings_Bool( &workspace.pGui->settings, perspective ? "editor.viewport.perspective.show_metrics" : "editor.viewport.show_metrics", CY_FALSE );
}

void OnViewDisplaySettingChanged( void *pContext, string_view_t path ) noexcept
{
    ( void )path;
    static_cast<QWidget *>( pContext )->update();
}

bool DisplayFlag( const map_workspace_t &workspace, const char *pPath, bool_t fallback = CY_TRUE )
{
    return EditorSettings_Bool( &workspace.pGui->settings, pPath, fallback );
}

// Logical-pixel scale is shared by rendering and picking. World-unit grid
// snapping remains independent of the size of the on-screen controls.
f64 GizmoScale( const map_workspace_t &workspace ) noexcept
{
    return EditorSettings_Real( &workspace.pGui->settings, "editor.viewport.gizmo_scale", 1.0 );
}

f64 FrameMargin( const map_workspace_t &workspace ) noexcept
{
    return EditorSettings_Real( &workspace.pGui->settings, "editor.camera.frame_margin", 1.15 );
}

// Classify in world grid coordinates, not relative to the panned viewport.
// Rounding the cell index also handles negative coordinates and intervals
// such as 10 or 15, without relying on integer conversion of a huge world.
bool GridLineMultiple( f64 coordinate, f64 step, i64 interval ) noexcept
{
    return std::fabs( std::fmod( std::round( coordinate / step ), static_cast<f64>( interval ) ) ) < 0.25;
}

i64 DrawnMajorInterval( const map_workspace_t &workspace, f64 drawnStep ) noexcept
{
    const i64 authoredInterval = std::clamp<i64>( EditorSettings_Integer( &workspace.pGui->settings, "editor.grid.major_every", 8 ), 2, 64 );
    // Coarsening hides unreadably close minor lines; it does not redefine
    // the authored major interval. At least one minor line stays between
    // majors even after the view has zoomed past that interval.
    return std::max<i64>( 2, static_cast<i64>( std::ceil( workspace.gridSize * authoredInterval / drawnStep ) ) );
}

QRectF ViewHeaderRect( const QWidget &view )
{
    if ( view.parentWidget() != nullptr ) {
        const auto *header = view.parentWidget()->findChild<QWidget *>( QStringLiteral( "EditorViewHeader" ), Qt::FindDirectChildrenOnly );
        if ( header != nullptr && header->isVisible() ) {
            return QRectF( view.mapFromGlobal( header->mapToGlobal( QPoint() ) ), header->size() );
        }
    }
    return {};
}

QRectF AxisTriadRect( const QWidget &view )
{
    const QRectF header = ViewHeaderRect( view );
    return QRectF( view.width() - 88.0, header.isEmpty() ? 0.0 : header.bottom() + 4.0, 88.0, 88.0 );
}

// Measurements describe authored geometry, not the arbitrary helper boxes
// used to make point entities pickable. Hidden objects do not contribute.
map_bounds_t SelectionGeometryBounds( const map_workspace_t &workspace )
{
    map_bounds_t bounds{};
    if ( workspace.elementMode == map_element_mode_t::FACES &&
         ( MapWorkspace_HasMeshFace( &workspace ) || MapWorkspace_HasBrushFace( &workspace ) ) ) {
        for ( usize i = 0; i < workspace.wire.faces.nCount; ++i ) {
            const auto &face = workspace.wire.faces.pData[i];
            const bool selected = ( face.faceId != 0 && face.id == workspace.selectedMeshFaceObject && face.faceId == workspace.selectedMeshFaceId ) ||
                ( face.sideId != 0 && face.id == workspace.selectedBrushFaceObject && face.sideId == workspace.selectedBrushFaceSide );
            const auto *object = MapWireframe_FindObject( workspace.wire, face.id );
            if ( !selected || object == nullptr || !MapWorkspace_IsVisible( &workspace, *object ) ) { continue; }
            for ( u32 k = 0; k < face.nIndices; ++k ) { MapBounds_AddPoint( bounds, workspace.wire.points.pData[workspace.wire.faceIndices.pData[face.iFirstIndex + k]] ); }
            return bounds;
        }
    }
    for ( usize i = 0u; i < EditorSelection_Count( &workspace.selection ); ++i ) {
        const map_wire_object_t *pObject = MapWireframe_FindObject( workspace.wire, EditorSelection_At( &workspace.selection, i ) );
        if ( pObject != nullptr && pObject->kind != map_wire_kind_t::ENTITY && MapWorkspace_IsVisible( &workspace, *pObject ) ) {
            MapBounds_AddBounds( bounds, pObject->bounds );
        }
    }
    return bounds;
}

void BoundsCorners( const map_bounds_t &bounds, math::vec3d_t ( &corners )[8] )
{
    for ( u32 i = 0u; i < 8u; ++i ) {
        corners[i] = math::Vec3d_Make( ( i & 1u ) ? bounds.box.maximum.x : bounds.box.minimum.x,
                                      ( i & 2u ) ? bounds.box.maximum.y : bounds.box.minimum.y,
                                      ( i & 4u ) ? bounds.box.maximum.z : bounds.box.minimum.z );
    }
}

constexpr u32 kBoundsEdges[12][2]{ { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 },
                                  { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };

// A single clean stroke identifies selection without a dark rim around
// every edge. Picking tolerance remains independent of rendered line width.
void DrawWireBatch( QPainter &painter, const map_workspace_t &workspace, line_color_t slot, const QVector<QLineF> &lines )
{
    if ( lines.isEmpty() ) { return; }
    painter.save();
    painter.setRenderHint( QPainter::Antialiasing, true );
    const bool selected = slot == LINE_SELECTED;
    const bool emphasised = selected || slot == LINE_HOVER;
    const qreal width = gui::EditorStyle_Metric( workspace.pGui->style,
        emphasised ? "viewport.selection.line_width" : "viewport.wire.line_width", emphasised ? 2.0 : 1.2 );
    QColor color = SlotColor( workspace, slot );
    if ( !emphasised ) { color.setAlpha( EditorSelection_Count( &workspace.selection ) != 0u ? 125 : 175 ); }
    painter.setPen( QPen( color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
    painter.drawLines( lines );
    painter.restore();
}

void DrawGhostBatch( QPainter &painter, const map_workspace_t &workspace, const QVector<QLineF> &lines )
{
    if ( lines.isEmpty() ) { return; }
    painter.save();
    QColor color = SlotColor( workspace, LINE_WORLD );
    color.setAlpha( 48 );
    painter.setPen( QPen( color, 1.0, Qt::DotLine ) );
    painter.drawLines( lines );
    painter.restore();
}

// Plain axis-coloured numbers sit beside their corresponding edges. Keep
// them horizontal and separated; dimension ruler/extension lines add clutter.
void DrawDimension( QPainter &painter, const map_workspace_t &workspace, QLineF edge, QPointF center,
                    u32 axis, f64 extent, QRectF viewport, QVector<QRectF> &occupied )
{
    if ( edge.length() < 18.0 || extent <= 1e-9 ) { return; }
    const QPointF direction = ( edge.p2() - edge.p1() ) / edge.length();
    QPointF normal( -direction.y(), direction.x() );
    const QPointF outward = edge.center() - center;
    if ( QPointF::dotProduct( normal, outward ) < 0.0 ) { normal = -normal; }
    const QString text = NumberText( extent );
    const QFont font = gui::EditorStyle_Font( workspace.pGui->style, "viewport.dimensions" );
    const QFontMetrics metrics( font );
    const QSizeF textSize( metrics.horizontalAdvance( text ) + 6.0, metrics.height() + 4.0 );
    QRectF label( edge.center() + normal * 14.0 - QPointF( textSize.width() * 0.5, textSize.height() * 0.5 ), textSize );
    const QRectF safe = viewport.adjusted( 6.0, 22.0, -6.0, -6.0 );
    if ( label.width() > safe.width() || label.height() > safe.height() ) { return; }
    label.moveLeft( std::clamp( label.left(), safe.left(), safe.right() - label.width() ) );
    label.moveTop( std::clamp( label.top(), safe.top(), safe.bottom() - label.height() ) );
    // Clamping at a pane edge can put the number on its midpoint handle.
    // Slide beside that point along the measured side, retaining the axis.
    if ( label.intersects( QRectF( edge.center() - QPointF( 6, 6 ), QSizeF( 12, 12 ) ) ) ) {
        label.translate( direction * ( std::abs( direction.x() ) * label.width() * 0.5 + std::abs( direction.y() ) * label.height() * 0.5 + 10.0 ) );
        label.moveLeft( std::clamp( label.left(), safe.left(), safe.right() - label.width() ) );
        label.moveTop( std::clamp( label.top(), safe.top(), safe.bottom() - label.height() ) );
    }
    // Narrow or foreshortened solids can put two axes' numbers together.
    // Try nearby positions on the same side before omitting a measurement.
    const QRectF preferred = label;
    const qreal shift = textSize.width() * 0.5 + textSize.height() * 0.5 + 10.0;
    bool placed = false;
    for ( const QPointF offset : { QPointF(), direction * shift, -direction * shift,
                                  normal * ( textSize.height() + 6.0 ) } ) {
        label = preferred.translated( offset );
        label.moveLeft( std::clamp( label.left(), safe.left(), safe.right() - label.width() ) );
        label.moveTop( std::clamp( label.top(), safe.top(), safe.bottom() - label.height() ) );
        bool overlap = false;
        for ( const QRectF &previous : occupied ) { if ( previous.intersects( label ) ) { overlap = true; break; } }
        if ( !overlap ) { placed = true; break; }
    }
    if ( !placed ) { return; }
    occupied.append( label.adjusted( -3.0, -3.0, 3.0, 3.0 ) );
    const char *axisToken = axis == 0u ? "viewport.axis.x" : axis == 1u ? "viewport.axis.y" : "viewport.axis.z";
    QColor color = Token( workspace, axisToken );
    painter.save();
    painter.setRenderHint( QPainter::TextAntialiasing, true );
    painter.setFont( font );
    painter.setPen( color );
    painter.drawText( label, Qt::AlignCenter, text );
    painter.restore();
}

void DrawVertexCues( QPainter &painter, const map_workspace_t &workspace, const QVector<QPointF> &vertices, QRectF viewport )
{
    if ( workspace.tool == map_tool_t::NONE || MeshComponentMode( workspace ) ) { return; }
    if ( vertices.isEmpty() ) { return; }
    const qreal size = gui::EditorStyle_Metric( workspace.pGui->style, "viewport.vertex.size", 6.0 );
    QSet<QPoint> cells;
    painter.setPen( QPen( Token( workspace, "viewport.vertex" ), 1.0 ) );
    painter.setBrush( Token( workspace, "viewport.overlay.background" ) );
    for ( const QPointF &point : vertices ) {
        if ( !viewport.contains( point ) ) { continue; }
        const QPoint cell( static_cast<int>( point.x() / ( size + 2.0 ) ), static_cast<int>( point.y() / ( size + 2.0 ) ) );
        if ( cells.contains( cell ) ) { continue; }
        cells.insert( cell );
        painter.drawRect( QRectF( point.x() - size * 0.5, point.y() - size * 0.5, size, size ) );
    }
    painter.setBrush( Qt::NoBrush );
}

bool ShowConnection( const map_workspace_t &workspace, const map_wire_connection_t &connection, bool perspective = false )
{
    if ( connection.status != map_wire_connection_status_t::RESOLVED || !connection.bHasSourceOrigin || !connection.bHasTargetOrigin ) {
        return false;
    }
    const string_view_t mode = EditorSettings_Text( &workspace.pGui->settings, perspective ? "editor.viewport.perspective.io_lines" : "editor.viewport.io_lines", StringView_FromCString( "selected" ) );
    if ( StringView_Equals( mode, StringView_FromCString( "never" ) ) ) { return false; }
    if ( !StringView_Equals( mode, StringView_FromCString( "always" ) ) &&
         !MapWorkspace_IsSelected( &workspace, connection.sourceId ) && !MapWorkspace_IsSelected( &workspace, connection.targetId ) ) { return false; }
    const map_wire_object_t *source = MapWireframe_FindObject( workspace.wire, connection.sourceId );
    const map_wire_object_t *target = MapWireframe_FindObject( workspace.wire, connection.targetId );
    return source != nullptr && target != nullptr && MapWorkspace_IsVisible( &workspace, *source ) && MapWorkspace_IsVisible( &workspace, *target );
}

bool ShowEntityName( const map_workspace_t &workspace, u64 id, bool perspective = false )
{
    const string_view_t mode = EditorSettings_Text( &workspace.pGui->settings, perspective ? "editor.viewport.perspective.entity_names" : "editor.viewport.entity_names", StringView_FromCString( perspective ? "selected" : "always" ) );
    return StringView_Equals( mode, StringView_FromCString( "always" ) ) ||
           ( StringView_Equals( mode, StringView_FromCString( "selected" ) ) && MapWorkspace_IsSelected( &workspace, id ) );
}

void DrawConnection( QPainter &painter, const map_workspace_t &workspace, const map_wire_connection_t &connection, const QLineF &line )
{
    if ( line.length() < 10.0 ) { return; }
    QColor color = Token( workspace, "viewport.measure" );
    const bool selected = MapWorkspace_IsSelected( &workspace, connection.sourceId ) || MapWorkspace_IsSelected( &workspace, connection.targetId );
    color.setAlpha( selected ? 230 : 110 );
    painter.setPen( QPen( color, selected ? 1.5 : 1.0, Qt::DashLine ) );
    painter.drawLine( line );
    const QPointF direction = ( line.p2() - line.p1() ) / line.length();
    const QPointF normal( -direction.y(), direction.x() );
    const QPointF tip = line.pointAt( 0.68 );
    painter.setPen( QPen( color, selected ? 1.5 : 1.0 ) );
    painter.drawLine( QLineF( tip, tip - direction * 8.0 + normal * 4.0 ) );
    painter.drawLine( QLineF( tip, tip - direction * 8.0 - normal * 4.0 ) );
}

// Hover navigation must not steal an unfinished property/console edit, or
// move focus behind a popup. Inspect ancestors for compound editors such
// as a spin box whose line edit owns the actual focus.
bool CanFocusViewOnHover( const QWidget &view )
{
    if ( QApplication::activeModalWidget() != nullptr || QApplication::activePopupWidget() != nullptr ||
         QApplication::mouseButtons() != Qt::NoButton || !view.window()->isActiveWindow() ) {
        return false;
    }
    for ( const QWidget *pFocus = QApplication::focusWidget(); pFocus != nullptr; pFocus = pFocus->parentWidget() ) {
        if ( pFocus->testAttribute( Qt::WA_InputMethodEnabled ) || qobject_cast<const QLineEdit *>( pFocus ) != nullptr ||
             qobject_cast<const QTextEdit *>( pFocus ) != nullptr || qobject_cast<const QPlainTextEdit *>( pFocus ) != nullptr ||
             qobject_cast<const QAbstractSpinBox *>( pFocus ) != nullptr ) {
            return false;
        }
        const auto *pCombo = qobject_cast<const QComboBox *>( pFocus );
        if ( pCombo != nullptr && pCombo->isEditable() ) { return false; }
    }
    return true;
}

void ActivateHoveredView( QWidget &view, const map_workspace_t &workspace )
{
    if ( !view.hasFocus() && DisplayFlag( workspace, "editor.viewport.activate_on_hover" ) && CanFocusViewOnHover( view ) ) {
        view.setFocus( Qt::MouseFocusReason );
    }
}

// The object under the pointer, drawn in viewport.hover (Hammer's green) so
// it is clear what a click would select. Mouse moves only record the point;
// a short single-shot timer picks once per burst, because the 3D pick
// rebuilds and ray-tests brushes and must not run on every mouse event.
// Each view keeps its own: only the pane under the pointer highlights.
inline constexpr int kHoverPickMs = 30;

struct view_hover_t {
    QTimer timer{};
    QPointF point{};
    u64 id{ 0u };
};

template <typename PickFn>
void ViewHover_Init( view_hover_t &hover, QWidget &view, PickFn pick )
{
    hover.timer.setSingleShot( true );
    hover.timer.setInterval( kHoverPickMs );
    QObject::connect( &hover.timer, &QTimer::timeout, &view, [&hover, &view, pick]() {
        const u64 id = pick( hover.point );
        hover.id = id;
        // A different face of the same brush is still a different pick.
        view.update();
    } );
}

void ViewHover_Clear( view_hover_t &hover, QWidget &view )
{
    hover.timer.stop();
    if ( hover.id != 0u ) {
        hover.id = 0u;
        view.update();
    }
}

// Called from mouseMoveEvent. Dragging and navigation clear the highlight:
// it describes a click, and neither is one.
void ViewHover_Move( view_hover_t &hover, QWidget &view, const map_workspace_t &workspace, const QMouseEvent &event, bool navigating )
{
    // Handle feedback needs the current pointer even when object hover is
    // disabled. Only the expensive object pick belongs to that preference.
    hover.point = event.position();
    if ( event.buttons() != Qt::NoButton || navigating || !DisplayFlag( workspace, "editor.viewport.hover_highlight" ) ) {
        ViewHover_Clear( hover, view );
        return;
    }
    if ( !hover.timer.isActive() ) { hover.timer.start(); }
}

f64 ScreenSegmentDistance( QPointF point, const QLineF &line )
{
    const QPointF delta = line.p2() - line.p1();
    const f64 length2 = QPointF::dotProduct( delta, delta );
    const f64 t = length2 > 0.0 ? std::clamp( QPointF::dotProduct( point - line.p1(), delta ) / length2, 0.0, 1.0 ) : 0.0;
    const QPointF offset = point - ( line.p1() + delta * t );
    return std::hypot( offset.x(), offset.y() );
}

bool MeshEdgePickingActive( const map_workspace_t &workspace ) noexcept
{
    return workspace.tool == map_tool_t::SELECT && workspace.elementMode == map_element_mode_t::EDGES;
}

bool MeshVertexPickingActive( const map_workspace_t &workspace ) noexcept
{
    return workspace.tool == map_tool_t::SELECT && workspace.elementMode == map_element_mode_t::VERTICES;
}

bool SameMeshEdge( geometry::mesh_edge_ref_t a, geometry::mesh_edge_ref_t b ) noexcept
{
    return a.a.value == b.a.value && a.b.value == b.b.value;
}

bool MeshEdgeHitBefore( const map_mesh_edge_hit_t &a, const map_mesh_edge_hit_t &b ) noexcept
{
    return a.object < b.object || ( a.object == b.object &&
        ( a.edge.a.value < b.edge.a.value || ( a.edge.a.value == b.edge.a.value && a.edge.b.value < b.edge.b.value ) ) );
}

// Resolve source endpoints while their pool handles are local to this visit.
// Source-edge enumeration avoids a description allocation on every hover and
// does not infer component identity from the wireframe's duplicated points.
template <typename Visit>
void VisitMeshEdges( const geometry::mesh_source_t &source, Visit visit )
{
    ( void )GenerationPool_ForEach( &source.mesh.edges,
        [&]( geometry::geometry_mesh_edge_handle_t, const geometry::mesh_edge_record_t &edge ) noexcept -> bool_t {
            const auto *half = GenerationPool_Get( &source.mesh.halfEdges, edge.hHalfEdge );
            const auto *next = half != nullptr ? GenerationPool_Get( &source.mesh.halfEdges, half->hNext ) : nullptr;
            const auto *a = half != nullptr ? GenerationPool_Get( &source.mesh.vertices, half->hOrigin ) : nullptr;
            const auto *b = next != nullptr ? GenerationPool_Get( &source.mesh.vertices, next->hOrigin ) : nullptr;
            if ( a == nullptr || b == nullptr ) { return CY_TRUE; }
            const auto identity = geometry::MeshEdgeRef_Make( geometry::MeshSource_VertexId( &source, half->hOrigin ),
                geometry::MeshSource_VertexId( &source, next->hOrigin ) );
            if ( identity.a.value == 0u || identity.a.value == identity.b.value ||
                 !std::isfinite( a->position.x ) || !std::isfinite( a->position.y ) || !std::isfinite( a->position.z ) ||
                 !std::isfinite( b->position.x ) || !std::isfinite( b->position.y ) || !std::isfinite( b->position.z ) ) { return CY_TRUE; }
            visit( identity, a->position, b->position );
            return CY_TRUE;
        } );
}

template <typename Visit>
void VisitMeshVertices( const geometry::mesh_source_t &source, Visit visit )
{
    ( void )GenerationPool_ForEach( &source.mesh.vertices,
        [&]( geometry::geometry_mesh_vertex_handle_t handle, const geometry::mesh_vertex_record_t &vertex ) noexcept -> bool_t {
            const auto id = geometry::MeshSource_VertexId( &source, handle );
            if ( id.value != 0u && std::isfinite( vertex.position.x ) && std::isfinite( vertex.position.y ) && std::isfinite( vertex.position.z ) ) {
                visit( id, vertex.position );
            }
            return CY_TRUE;
        } );
}

void SelectViewMeshEdge( map_workspace_t &workspace, const map_mesh_edge_hit_t &hit, Qt::KeyboardModifiers modifiers )
{
    const bool toggle = ( modifiers & ( Qt::ControlModifier | Qt::MetaModifier ) ) != 0;
    const bool add = ( modifiers & Qt::ShiftModifier ) != 0;
    if ( hit.object != 0u ) {
        ( void )MapWorkspace_SelectMeshEdge( &workspace, hit.object, hit.edge,
            toggle ? MAP_SELECT_TOGGLE : add ? MAP_SELECT_ADD : MAP_SELECT_REPLACE );
    } else if ( !toggle && !add ) {
        MapWorkspace_ClearMeshEdges( &workspace );
    }
}

void SelectViewMeshVertex( map_workspace_t &workspace, const map_mesh_vertex_hit_t &hit, Qt::KeyboardModifiers modifiers )
{
    const bool toggle = ( modifiers & ( Qt::ControlModifier | Qt::MetaModifier ) ) != 0;
    const bool add = ( modifiers & Qt::ShiftModifier ) != 0;
    if ( hit.object != 0u ) {
        ( void )MapWorkspace_SelectMeshVertex( &workspace, hit.object, hit.vertex,
            toggle ? MAP_SELECT_TOGGLE : add ? MAP_SELECT_ADD : MAP_SELECT_REPLACE );
    } else if ( !toggle && !add ) {
        MapWorkspace_ClearMeshVertices( &workspace );
    }
}

template <typename Project>
void DrawMeshEdgeSelection( QPainter &painter, const map_workspace_t &workspace, const map_mesh_edge_hit_t &hover, Project project )
{
    // Neutral navigation keeps the authored component selection available for
    // inspection, but removes all editing overlays until a tool is resumed.
    if ( !MeshEdgePickingActive( workspace ) || workspace.pDocument == nullptr ) { return; }
    QVector<QLineF> selected, hovered;
    const u64 target = workspace.meshSelection.meshId.value;
    const u64 sources[2]{ target, hover.object };
    for ( int i = 0; i < 2; ++i ) {
        const u64 id = sources[i];
        if ( id == 0u || ( i == 1 && id == target ) ) { continue; }
        const auto *object = MapWireframe_FindObject( workspace.wire, id );
        const auto *source = geometry::GeometryDocument_FindMesh( &workspace.pDocument->geometry, { id } );
        if ( object == nullptr || !MapWorkspace_IsVisible( &workspace, *object ) || !geometry::MeshSource_IsInitialized( source ) ) { continue; }
        VisitMeshEdges( *source, [&]( geometry::mesh_edge_ref_t edge, math::vec3d_t a, math::vec3d_t b ) {
            const bool isSelected = id == target && geometry::MeshSelection_HasEdge( &workspace.meshSelection, edge );
            const bool isHovered = id == hover.object && SameMeshEdge( edge, hover.edge );
            if ( !isSelected && !isHovered ) { return; }
            QLineF line;
            if ( project( a, b, line ) ) { ( isSelected ? selected : hovered ).append( line ); }
        } );
    }
    DrawWireBatch( painter, workspace, LINE_HOVER, hovered );
    DrawWireBatch( painter, workspace, LINE_SELECTED, selected );
}

template <typename Project>
void DrawOrthoMeshVertexCandidates( QPainter &painter, const map_workspace_t &workspace, Project project, QRectF viewport )
{
    if ( !MeshVertexPickingActive( workspace ) || workspace.pDocument == nullptr ) { return; }
    QPolygonF points;
    for ( usize i = 0u; i < workspace.wire.objects.nCount; ++i ) {
        const auto &object = workspace.wire.objects.pData[i];
        if ( object.kind != map_wire_kind_t::MESH || !MapWorkspace_IsVisible( &workspace, object ) ) { continue; }
        const auto *source = geometry::GeometryDocument_FindMesh( &workspace.pDocument->geometry, { object.id } );
        if ( !geometry::MeshSource_IsInitialized( source ) ) { continue; }
        VisitMeshVertices( *source, [&]( geometry::geometry_source_id_t, math::vec3d_t point ) {
            QPointF screen; if ( project( point, screen ) && viewport.contains( screen ) ) { points.append( screen ); }
        } );
    }
    painter.save(); painter.setRenderHint( QPainter::Antialiasing, false );
    QColor color = SlotColor( workspace, LINE_MESH ); color.setAlpha( 190 );
    painter.setPen( QPen( color, 3.0, Qt::SolidLine, Qt::SquareCap ) );
    painter.drawPoints( points ); painter.restore();
}

template <typename Project>
void DrawMeshVertexSelection( QPainter &painter, const map_workspace_t &workspace, const map_mesh_vertex_hit_t &hover,
                              Project project, QRectF viewport )
{
    if ( !MeshVertexPickingActive( workspace ) || workspace.pDocument == nullptr ) { return; }
    QVector<QPointF> selected, hovered;
    const u64 target = workspace.meshSelection.meshId.value;
    const u64 sources[2]{ target, hover.object };
    for ( int i = 0; i < 2; ++i ) {
        const u64 id = sources[i];
        if ( id == 0u || ( i == 1 && id == target ) ) { continue; }
        const auto *object = MapWireframe_FindObject( workspace.wire, id );
        const auto *source = geometry::GeometryDocument_FindMesh( &workspace.pDocument->geometry, { id } );
        if ( object == nullptr || !MapWorkspace_IsVisible( &workspace, *object ) || !geometry::MeshSource_IsInitialized( source ) ) { continue; }
        VisitMeshVertices( *source, [&]( geometry::geometry_source_id_t vertex, math::vec3d_t point ) {
            const bool isSelected = id == target && geometry::MeshSelection_HasVertex( &workspace.meshSelection, vertex );
            const bool isHovered = id == hover.object && vertex.value == hover.vertex.value;
            if ( !isSelected && !isHovered ) { return; }
            QPointF screen;
            if ( project( point, screen ) && viewport.contains( screen ) ) { ( isSelected ? selected : hovered ).append( screen ); }
        } );
    }
    painter.save();
    // Compact filled squares distinguish authored vertices without the old
    // dark-rimmed whole-root corner cues. Selection wins when also hovered.
    painter.setRenderHint( QPainter::Antialiasing, false );
    const qreal size = std::clamp( gui::EditorStyle_Metric( workspace.pGui->style, "viewport.vertex.size", 6.0 ), 4.0, 10.0 );
    for ( const bool isSelected : { false, true } ) {
        const QColor color = SlotColor( workspace, isSelected ? LINE_SELECTED : LINE_HOVER );
        painter.setPen( QPen( color, 1.0 ) ); painter.setBrush( color );
        for ( const QPointF &point : isSelected ? selected : hovered ) {
            painter.drawRect( QRectF( point.x() - size * 0.5, point.y() - size * 0.5, size, size ) );
        }
    }
    painter.restore();
}

// Dedicated surface tools can pick a face before switching the component
// mode. Ordinary selection follows the current mode's root eligibility.
bool ViewPickableRoot( const map_workspace_t &workspace, const map_wire_object_t &object ) noexcept
{
    if ( workspace.tool == map_tool_t::EXTRUDE || workspace.tool == map_tool_t::TEXTURE || workspace.tool == map_tool_t::EYEDROPPER ) {
        return MapWorkspace_IsVisible( &workspace, object ) &&
            ( object.kind == map_wire_kind_t::BRUSH || object.kind == map_wire_kind_t::MESH );
    }
    return MapWorkspace_IsSelectableObject( &workspace, object );
}

void SelectViewObject( map_workspace_t &workspace, u64 id, Qt::KeyboardModifiers modifiers )
{
    if ( id != 0u ) {
        const auto *object = MapWireframe_FindObject( workspace.wire, id );
        if ( object == nullptr || !ViewPickableRoot( workspace, *object ) ) { return; }
    }
    const bool toggle = ( modifiers & ( Qt::ControlModifier | Qt::MetaModifier ) ) != 0;
    const bool add = ( modifiers & Qt::ShiftModifier ) != 0;
    MapWorkspace_Select( &workspace, id, toggle ? MAP_SELECT_TOGGLE : add ? MAP_SELECT_ADD : MAP_SELECT_REPLACE );
}

void OpenViewObjectInspector( map_workspace_t &workspace )
{
    // The shell owns the panel. This command shows it without toggling an
    // already-open inspector shut, and works with any host of this workspace.
    const command_registry_t &commands = workspace.pGui->commands;
    const string_view_t command = StringView_FromCString( "view.properties.open" );
    if ( EditorCommands_Find( &commands, command ) != nullptr ) { ( void )EditorCommands_Execute( &commands, command, {} ); }
}

void InspectViewObject( map_workspace_t &workspace, u64 id )
{
    MapWorkspace_Select( &workspace, id, MAP_SELECT_REPLACE );
    OpenViewObjectInspector( workspace );
}

void DrawEntityLabel( QPainter &painter, const map_workspace_t &workspace, const map_wire_entity_t &entity,
                      QRectF objectBounds, QRectF viewport, QVector<QRectF> &occupied, bool perspective = false )
{
    const bool selected = MapWorkspace_IsSelected( &workspace, entity.id );
    QString text = QString::fromUtf8( entity.name[0] != '\0' ? entity.name : entity.className );
    const QRectF safe = viewport.adjusted( 3.0, 3.0, -3.0, -3.0 );
    if ( safe.width() <= 8.0 || safe.height() < painter.fontMetrics().height() + 4.0 ) { return; }
    text = painter.fontMetrics().elidedText( text, Qt::ElideRight, std::min( 300, static_cast<int>( safe.width() - 8.0 ) ) );
    if ( text.isEmpty() ) { return; }
    const QSizeF size( painter.fontMetrics().horizontalAdvance( text ) + 8.0, painter.fontMetrics().height() + 4.0 );
    const QPointF anchors[]{
        { objectBounds.right() + 5.0, objectBounds.top() },
        { objectBounds.left() - size.width() - 5.0, objectBounds.top() },
        { objectBounds.center().x() - size.width() * 0.5, objectBounds.top() - size.height() - 5.0 },
        { objectBounds.center().x() - size.width() * 0.5, objectBounds.bottom() + 5.0 }
    };
    QRectF label;
    for ( QPointF anchor : anchors ) {
        QRectF candidate( anchor, size );
        candidate.moveLeft( std::clamp( candidate.left(), safe.left(), safe.right() - size.width() ) );
        candidate.moveTop( std::clamp( candidate.top(), safe.top(), safe.bottom() - size.height() ) );
        if ( std::any_of( occupied.cbegin(), occupied.cend(), [&]( const QRectF &used ) { return used.intersects( candidate ); } ) ) { continue; }
        label = candidate;
        break;
    }
    // Selected labels reserve space first. If all four nearby positions are
    // crowded, omit this label rather than stack text over another name.
    if ( label.isEmpty() ) { return; }
    occupied.append( label.adjusted( -3.0, -3.0, 3.0, 3.0 ) );
    const map_wire_object_t *object = MapWireframe_FindObject( workspace.wire, entity.id );
    const QColor color = selected ? SlotColor( workspace, LINE_SELECTED ) :
        object != nullptr ? SlotColor( workspace, ColorSlot( workspace, *object ) ) : Token( workspace, "viewport.overlay.text" );
    painter.save();
    if ( perspective ) {
        QColor background = Token( workspace, "viewport.overlay.background" );
        background.setAlpha( 220 );
        painter.fillRect( label, background );
        painter.setPen( color );
        painter.drawText( label.adjusted( 4.0, 2.0, -4.0, -2.0 ), Qt::AlignLeft | Qt::AlignVCenter, text );
    }
    else {
        QPainterPath glyphs;
        glyphs.addText( label.topLeft() + QPointF( 4.0, 2.0 + painter.fontMetrics().ascent() ), painter.font(), text );
        painter.setRenderHint( QPainter::Antialiasing, true );
        painter.strokePath( glyphs, QPen( Token( workspace, "viewport.overlay.background" ), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
        painter.fillPath( glyphs, color );
    }
    painter.restore();
}

map_bounds_t PaneFrameBounds( const map_workspace_t &workspace )
{
    map_bounds_t bounds{};
    for ( usize i = 0u; i < EditorSelection_Count( &workspace.selection ); ++i ) {
        const map_wire_object_t *pObject = MapWireframe_FindObject( workspace.wire, EditorSelection_At( &workspace.selection, i ) );
        if ( pObject != nullptr ) { MapBounds_AddBounds( bounds, pObject->bounds ); }
    }
    // An entity's helper bounds need not contain its owned geometry. Include
    // visible children so framing covers the actual editable selection.
    for ( usize i = 0u; i < workspace.wire.objects.nCount; ++i ) {
        const auto &object = workspace.wire.objects.pData[i];
        if ( object.owner != 0u && MapWorkspace_IsSelected( &workspace, object.owner ) && MapWorkspace_IsVisible( &workspace, object ) ) {
            MapBounds_AddBounds( bounds, object.bounds );
        }
    }
    if ( !bounds.bHas ) { bounds = workspace.wire.bounds; }
    if ( !bounds.bHas ) {
        MapBounds_AddPoint( bounds, math::Vec3d_Make( -512.0, -512.0, -512.0 ) );
        MapBounds_AddPoint( bounds, math::Vec3d_Make( 512.0, 512.0, 512.0 ) );
    }
    return bounds;
}

// Objects and selection, the second line of every view's info text.
QString CountText( const map_workspace_t &workspace )
{
    const usize nSelected = EditorSelection_Count( &workspace.selection );
    QString text = QStringLiteral( "%1 objects" ).arg( workspace.wire.objects.nCount );
    if ( nSelected != 0u ) { text += QStringLiteral( " \u00B7 %1 selected" ).arg( nSelected ); }
    return text;
}

void DrawAxisArrow( QPainter &painter, QPointF origin, QPointF tip, const QColor &color, char letter )
{
    painter.setPen( QPen( color, 1.8, Qt::SolidLine, Qt::RoundCap ) );
    painter.setBrush( Qt::NoBrush );
    const QPointF direction = tip - origin;
    const f64 length = std::max( std::hypot( direction.x(), direction.y() ), 1.0 );
    QPointF label;
    if ( length < 4.0 ) {
        // Looking along an axis: a dot in a ring denotes the out-of-plane
        // direction instead of overlapping its label with the other axes.
        painter.drawEllipse( origin, 3.0, 3.0 );
        painter.drawPoint( origin );
        label = origin + QPointF( -12.0, 12.0 );
    } else {
        const QPointF unit = direction / length;
        const QPointF normal( -unit.y(), unit.x() );
        painter.drawLine( QLineF( origin, tip ) );
        painter.drawLine( QLineF( tip, tip - unit * 5.0 + normal * 2.8 ) );
        painter.drawLine( QLineF( tip, tip - unit * 5.0 - normal * 2.8 ) );
        label = tip + unit * 9.0;
    }
    painter.drawText( QRectF( label.x() - 6.0, label.y() - 7.0, 12.0, 14.0 ), Qt::AlignCenter, QString( QLatin1Char( letter ) ) );
}

// ---------------------------------------------------------------------------
// 2D orthographic view
// ---------------------------------------------------------------------------

// Select supports direct movement of whole objects without switching tools.
// Component modes keep their own picking semantics; a face click never moves
// its entire brush implicitly.
bool SelectMovesObjects( const map_workspace_t &ws ) {
    return ws.tool == map_tool_t::SELECT &&
           ( ws.elementMode == map_element_mode_t::OBJECTS || ws.elementMode == map_element_mode_t::GROUPS || ws.elementMode == map_element_mode_t::MESHES );
}
map_tool_t TransformTool( const map_workspace_t &ws ) { return SelectMovesObjects( ws ) ? map_tool_t::TRANSLATE : ws.tool; }
map_bounds_t VisibleSelectionBounds( const map_workspace_t &ws ) {
    map_bounds_t result{};
    for ( usize i = 0; i < ws.selection.ids.nCount; ++i ) {
        const auto *object = MapWireframe_FindObject( ws.wire, ws.selection.ids.pData[i] );
        if ( object != nullptr && MapWorkspace_IsVisible( &ws, *object ) ) { MapBounds_AddBounds( result, object->bounds ); }
    } return result;
}
map_bounds_t EditSelectionBounds( const map_workspace_t &ws ) {
    return TransformTool( ws ) == map_tool_t::TRANSLATE ? VisibleSelectionBounds( ws ) : SelectionGeometryBounds( ws );
}
math::vec3d_t GizmoPivot( const map_workspace_t &ws, const map_bounds_t &bounds ) {
    // The captured drag belongs to one pane, but its world-space movement
    // preview is shared. Every pane's move handles follow that same ghost.
    if ( ws.editPreview.bActive && ws.editPreview.transform.kind != map_transform_preview_kind_t::NONE ) {
        return MapWorkspace_TransformPreviewPoint( ws.editPreview.transform, ws.editPreview.transform.pivot );
    }
    return MapBounds_Center( bounds );
}
bool CanTransform( const map_workspace_t &ws ) {
    if ( ws.tool == map_tool_t::NONE || ws.tool == map_tool_t::CAMERA ) { return false; }
    // Whole-root adapters cannot edit a selected component. Until a real
    // component transform is wired, do not show handles that would silently
    // move/scale/rotate its entire parent instead.
    if ( ws.elementMode == map_element_mode_t::VERTICES || ws.elementMode == map_element_mode_t::EDGES || ws.elementMode == map_element_mode_t::FACES ) { return false; }
    return TransformTool( ws ) == map_tool_t::TRANSLATE ? MapWorkspace_CanMoveSelection( &ws ) : MapWorkspace_CanEditSelection( &ws );
}

// Gestures remain presentation data until commit. Block/Clip release stages
// shared construction; transforms commit on release. Active drags cancel on focus loss.
enum class edit_drag_t { NONE, MARQUEE, BOX, CLIP, TRANSLATE, SCALE, ROTATE, PUSH_PULL, RESIZE };
struct view_edit_drag_t {
    edit_drag_t kind{ edit_drag_t::NONE };
    QPointF start{}, current{}, pivotScreen{};
    QPointF resizeScreenOffset{}; // Captured separation/pickup offset of a bounds control from its world boundary.
    map_bounds_t original{};
    math::vec3d_t pivot{}, planeStart{}, delta{}, factors{ 1, 1, 1 }, degrees{};
    int axis{ -1 };
    int planeAxis{ -1 }; // Normal of the XY/YZ/ZX handle's construction plane.
    bool uniformScale{ false };
    bool bodyMove{ false }, bodyActivated{ false };
    bool blockResize{ false }; // Refine the private Block descriptor, never scale the live selection.
    bool resizeFromCenter{ false }; // Anchor policy captured at press, including the temporary Shift override.
    bool resizeIndividually{ false }; // Capture the multi-root resize policy at press.
    math::vec3d_t resizeSides{}; // -1 minimum, +1 maximum, 0 unchanged for each axis.
    int depthAxis{ 2 };
    f64 depth{ 64.0 };
    f64 unitsPerPixel{ 1.0 }, distance{ 0.0 };
    math::vec3d_t faceNormal{};
    math::vec3d_t constraintNormal{};
    Qt::KeyboardModifiers modifiers{};
    const map_document_t *document{ nullptr };
    u64 geometryRevision{ 0u }, selectionRevision{ 0u }, faceObject{ 0u }, faceSide{ 0u };
    u64 meshFaceObject{ 0u }, meshFaceId{ 0u };
    map_tool_t tool{ map_tool_t::SELECT };
    map_element_mode_t mode{ map_element_mode_t::OBJECTS };
    map_preview_visibility_t visibility{};
    map_clip_guide_t clipGuide{};
    int clipEndpoint{ -1 };
    u32 clipAxisSetting{ 2u };
};
void CaptureDragContext( view_edit_drag_t &drag, const map_workspace_t &ws ) {
    drag.document = ws.pDocument;
    drag.geometryRevision = ws.pDocument != nullptr ? ws.pDocument->geometry.revision : 0u;
    drag.selectionRevision = ws.selection.revision;
    drag.tool = ws.tool; drag.mode = ws.elementMode;
    drag.visibility = MapWorkspace_PreviewVisibility( &ws );
    drag.faceObject = ws.selectedBrushFaceObject; drag.faceSide = ws.selectedBrushFaceSide;
    drag.meshFaceObject = ws.selectedMeshFaceObject; drag.meshFaceId = ws.selectedMeshFaceId;
    drag.clipAxisSetting = MapWorkspace_ClipAxis( &ws );
}
bool DragContextMatches( const view_edit_drag_t &drag, const map_workspace_t &ws ) {
    return ( drag.kind != edit_drag_t::CLIP || ( MapWorkspace_CanEditBrushSelection( &ws ) && drag.clipAxisSetting == MapWorkspace_ClipAxis( &ws ) ) ) &&
           drag.document == ws.pDocument && ws.pDocument != nullptr && drag.geometryRevision == ws.pDocument->geometry.revision &&
           drag.selectionRevision == ws.selection.revision && drag.tool == ws.tool && drag.mode == ws.elementMode &&
           MapWorkspace_PreviewVisibilityMatches( &ws, drag.visibility ) &&
           drag.faceObject == ws.selectedBrushFaceObject && drag.faceSide == ws.selectedBrushFaceSide &&
           drag.meshFaceObject == ws.selectedMeshFaceObject && drag.meshFaceId == ws.selectedMeshFaceId;
}
void SetAxis( math::vec3d_t &v, int axis, f64 value ) { if ( axis == 0 ) { v.x = value; } else if ( axis == 1 ) { v.y = value; } else { v.z = value; } }
math::vec3d_t AxisVector( int axis ) { math::vec3d_t v{}; SetAxis( v, axis, 1 ); return v; }

bool SurfaceTrianglePlane( math::vec3d_t a, math::vec3d_t b, math::vec3d_t c,
    math::vec3d_t &anchor, math::vec3d_t &normal ) noexcept
{
    const math::vec3d_t points[3]{ a, b, c };
    int origin = 0;
    f64 shortest = std::numeric_limits<f64>::max();
    for ( int edge = 0; edge < 3; ++edge ) {
        const auto delta = Sub( points[( edge + 1 ) % 3], points[edge] );
        const f64 length = std::hypot( delta.x, delta.y, delta.z );
        if ( length < shortest ) { shortest = length; origin = edge; }
    }
    // Anchor at an endpoint of the shortest edge. With one remote corner,
    // crossing two enormous, almost parallel vectors loses precision.
    anchor = points[origin];
    normal = math::Vec3d_Cross( Sub( points[( origin + 1 ) % 3], anchor ), Sub( points[( origin + 2 ) % 3], anchor ) );
    const f64 length = std::hypot( normal.x, normal.y, normal.z );
    if ( !std::isfinite( length ) || length <= 1e-18 ) { return false; }
    normal = Scale( normal, 1.0 / length );
    return true;
}
f64 Snap( f64 value, f64 step ) { return step > 0 ? std::round( value / step ) * step : value; }
bool BypassSnap( Qt::KeyboardModifiers modifiers ) { return ( modifiers & ( Qt::ControlModifier | Qt::MetaModifier ) ) != 0; }
math::vec3d_t SnapMove( math::vec3d_t v, const map_workspace_t &ws, bool bypass ) {
    const f64 step = ws.bSnapToGrid && !bypass ? ws.gridSize : 0;
    return { Snap( v.x, step ), Snap( v.y, step ), Snap( v.z, step ) };
}
math::vec3d_t ClipAnchor( math::vec3d_t point, const view_edit_drag_t &drag, const map_workspace_t &ws, bool bypass ) {
    point = SnapMove( point, ws, bypass );
    // The construction plane's depth is captured at press. Snapping changes
    // its two editable coordinates, never its extrusion-axis coordinate.
    SetAxis( point, drag.depthAxis, Axis( drag.pivot, drag.depthAxis ) );
    return point;
}
void UpdateClipGuide( view_edit_drag_t &drag, map_workspace_t &ws, math::vec3d_t point, bool bypass ) {
    auto guide = drag.clipGuide;
    if ( drag.clipEndpoint >= 0 ) {
        if ( QLineF( drag.start, drag.current ).length() < 3 ) {
            // A click preserves anchors, and returning after a real move
            // restores the captured guide rather than retaining a stale cut.
            MapWorkspace_SetClipGuide( &ws, guide ); return;
        }
        point = Add( guide.points[drag.clipEndpoint], Sub( point, drag.planeStart ) );
        guide.points[drag.clipEndpoint] = ClipAnchor( point, drag, ws, bypass );
    } else {
        guide.bHas = CY_TRUE; guide.extrusionAxis = static_cast<u32>( drag.depthAxis );
        guide.points[0] = ClipAnchor( drag.planeStart, drag, ws, bypass );
        guide.points[1] = ClipAnchor( point, drag, ws, bypass );
    }
    MapWorkspace_SetClipGuide( &ws, guide );
}
edit_drag_t TransformDrag( map_tool_t tool ) {
    return tool == map_tool_t::TRANSLATE ? edit_drag_t::TRANSLATE : tool == map_tool_t::SCALE ? edit_drag_t::SCALE :
           tool == map_tool_t::ROTATE ? edit_drag_t::ROTATE : edit_drag_t::NONE;
}
map_transform_preview_t TransformDescriptor( const view_edit_drag_t &drag ) {
    map_transform_preview_t result{};
    result.kind = drag.kind == edit_drag_t::TRANSLATE ? map_transform_preview_kind_t::TRANSLATE :
                  ( drag.kind == edit_drag_t::SCALE || drag.kind == edit_drag_t::RESIZE ) ? map_transform_preview_kind_t::SCALE :
                  drag.kind == edit_drag_t::ROTATE ? map_transform_preview_kind_t::ROTATE : map_transform_preview_kind_t::NONE;
    result.pivot = drag.pivot; result.delta = drag.delta; result.factors = drag.factors; result.degrees = drag.degrees;
    result.bClone = drag.kind == edit_drag_t::TRANSLATE && ( drag.modifiers & Qt::ShiftModifier ) != 0 ? CY_TRUE : CY_FALSE;
    result.bResize = drag.kind == edit_drag_t::RESIZE ? CY_TRUE : CY_FALSE;
    result.bResizeFromCenter = drag.kind == edit_drag_t::RESIZE && drag.resizeFromCenter ? CY_TRUE : CY_FALSE;
    result.bResizeIndividually = drag.kind == edit_drag_t::RESIZE && drag.resizeIndividually ? CY_TRUE : CY_FALSE;
    result.resizeSides = drag.resizeSides;
    return result;
}
map_bounds_t BoxFootprint( math::vec3d_t a, math::vec3d_t b, int u, int v, const map_workspace_t &ws, bool bypass, f64 depth ) {
    map_bounds_t bounds{};
    const f64 step = ws.bSnapToGrid && !bypass ? ws.gridSize : 0;
    SetAxis( a, u, Snap( Axis( a, u ), step ) ); SetAxis( a, v, Snap( Axis( a, v ), step ) );
    SetAxis( b, u, Snap( Axis( b, u ), step ) ); SetAxis( b, v, Snap( Axis( b, v ), step ) );
    const int depthAxis = 3 - u - v;
    SetAxis( a, depthAxis, 0 );
    SetAxis( b, depthAxis, depth );
    MapBounds_AddPoint( bounds, a ); MapBounds_AddPoint( bounds, b ); return bounds;
}
void CancelDrag( view_edit_drag_t &drag, map_workspace_t &ws ) {
    const bool active = drag.kind != edit_drag_t::NONE;
    drag = {}; if ( active ) { MapWorkspace_ClearEditPreview( &ws ); }
}
bool AwaitTransformMotion( view_edit_drag_t &drag, map_workspace_t &ws ) {
    if ( drag.kind != edit_drag_t::TRANSLATE && drag.kind != edit_drag_t::SCALE &&
         drag.kind != edit_drag_t::ROTATE && drag.kind != edit_drag_t::RESIZE ) { return false; }
    const f64 distance = QLineF( drag.start, drag.current ).length();
    if ( drag.bodyMove && distance >= QApplication::startDragDistance() ) { drag.bodyActivated = true; }
    if ( ( drag.bodyMove && !drag.bodyActivated ) || distance < 3 ) {
        drag.delta = {}; drag.factors = { 1, 1, 1 }; drag.degrees = {};
        if ( drag.blockResize ) { ( void )MapWorkspace_SetBlockPreviewBounds( &ws, drag.original ); return true; }
        MapWorkspace_ClearEditPreview( &ws ); return true;
    }
    return false;
}
math::vec3d_t ResizeSides( int handle, int u = -1, int v = -1 ) {
    math::vec3d_t sides{};
    if ( u < 0 ) { SetAxis( sides, handle / 2, handle & 1 ? 1 : -1 ); }
    else if ( handle < 4 ) { SetAxis( sides, handle < 2 ? u : v, handle & 1 ? 1 : -1 ); }
    else { SetAxis( sides, u, ( handle - 4 ) & 1 ? 1 : -1 ); SetAxis( sides, v, ( handle - 4 ) & 2 ? 1 : -1 ); }
    return sides;
}
math::vec3d_t ResizePoint( const map_bounds_t &bounds, math::vec3d_t sides ) {
    auto point = MapBounds_Center( bounds );
    for ( int axis = 0; axis < 3; ++axis ) {
        if ( Axis( sides, axis ) != 0 ) { SetAxis( point, axis, Axis( sides, axis ) > 0 ? Axis( bounds.box.maximum, axis ) : Axis( bounds.box.minimum, axis ) ); }
    }
    return point;
}
bool CanResizeSides( const map_bounds_t &bounds, math::vec3d_t sides ) {
    if ( !bounds.bHas ) { return false; }
    for ( int axis = 0; axis < 3; ++axis ) {
        if ( Axis( sides, axis ) == 0 ) { continue; }
        const f64 extent = Axis( bounds.box.maximum, axis ) - Axis( bounds.box.minimum, axis );
        if ( !std::isfinite( extent ) || extent <= 1e-12 ) { return false; }
    }
    return true;
}
bool IndividualResize( const map_workspace_t &ws ) {
    if ( ws.editPreview.bActive && ws.editPreview.transform.bResize ) { return ws.editPreview.transform.bResizeIndividually; }
    return ws.selection.ids.nCount > 1 && StringView_Equals( EditorSettings_Text( &ws.pGui->settings, "editor.map.resize_mode", StringView_FromCString( "Each object" ) ),
        StringView_FromCString( "Each object" ) );
}
bool CanResizeSelectionSides( const map_workspace_t &ws, math::vec3d_t sides ) {
    if ( !IndividualResize( ws ) || MapWorkspace_HasBlockPreview( &ws ) ) { return true; }
    for ( usize i = 0; i < ws.selection.ids.nCount; ++i ) {
        const auto *object = MapWireframe_FindObject( ws.wire, ws.selection.ids.pData[i] );
        if ( object == nullptr || !CanResizeSides( object->bounds, sides ) ) { return false; }
    }
    return true;
}
bool BeginResize( view_edit_drag_t &drag, const map_workspace_t &ws, math::vec3d_t sides ) {
    drag.blockResize = MapWorkspace_HasBlockPreview( &ws );
    if ( !drag.blockResize && ( !SelectMovesObjects( ws ) || !MapWorkspace_CanEditSelection( &ws ) ) ) { return false; }
    drag.original = drag.blockResize ? ws.editPreview.bounds : SelectionGeometryBounds( ws );
    if ( !CanResizeSides( drag.original, sides ) || ( !drag.blockResize && !CanResizeSelectionSides( ws, sides ) ) ) { return false; }
    drag.resizeIndividually = !drag.blockResize && IndividualResize( ws );
    drag.pivot = MapBounds_Center( drag.original );
    drag.resizeFromCenter = DisplayFlag( ws, "editor.map.resize_from_center", CY_FALSE ) || ( drag.modifiers & Qt::ShiftModifier ) != 0;
    for ( int axis = 0; axis < 3; ++axis ) {
        if ( drag.resizeFromCenter || Axis( sides, axis ) == 0 ) { continue; }
        SetAxis( drag.pivot, axis, Axis( sides, axis ) > 0 ? Axis( drag.original.box.minimum, axis ) : Axis( drag.original.box.maximum, axis ) );
    }
    drag.kind = edit_drag_t::RESIZE; drag.resizeSides = sides; CaptureDragContext( drag, ws ); return true;
}
void UpdateResize( view_edit_drag_t &drag, map_workspace_t &ws, math::vec3d_t delta, bool bypass ) {
    drag.factors = { 1, 1, 1 };
    if ( drag.resizeIndividually ) {
        // Snap one travel distance, not every object's boundary independently.
        // Unequal/off-grid roots therefore widen by the exact same increment.
        drag.delta = SnapMove( delta, ws, bypass );
        for ( int axis = 0; axis < 3; ++axis ) {
            const f64 side = Axis( drag.resizeSides, axis );
            if ( side == 0 ) { SetAxis( drag.delta, axis, 0 ); continue; }
            f64 minimumTravel = -std::numeric_limits<f64>::max();
            for ( usize i = 0; i < ws.selection.ids.nCount; ++i ) {
                const auto *object = MapWireframe_FindObject( ws.wire, ws.selection.ids.pData[i] );
                if ( object == nullptr ) { MapWorkspace_ClearEditPreview( &ws ); return; }
                const f64 extent = Axis( object->bounds.box.maximum, axis ) - Axis( object->bounds.box.minimum, axis );
                minimumTravel = std::max( minimumTravel, ( std::min( 1.0, extent ) - extent ) / ( drag.resizeFromCenter ? 2.0 : 1.0 ) );
            }
            // Keep all roots valid with one shared limit. Clamping each root
            // separately would secretly give the selection unequal travel.
            if ( ws.bSnapToGrid && !bypass ) {
                // Stay on whole grid increments even at the shared inward
                // limit. A thin root must stop the batch before collapse,
                // never turn a 64-unit gesture into a hidden 31-unit edit.
                minimumTravel = std::ceil( minimumTravel / ws.gridSize ) * ws.gridSize;
            }
            SetAxis( drag.delta, axis, side * std::max( side * Axis( drag.delta, axis ), minimumTravel ) );
        }
        MapWorkspace_SetTransformPreview( &ws, TransformDescriptor( drag ) ); return;
    }
    auto bounds = drag.original;
    for ( int axis = 0; axis < 3; ++axis ) {
        const f64 side = Axis( drag.resizeSides, axis );
        if ( side == 0 || Axis( delta, axis ) == 0 ) { continue; }
        const f64 extent = Axis( drag.original.box.maximum, axis ) - Axis( drag.original.box.minimum, axis );
        const f64 pivot = Axis( drag.pivot, axis );
        const f64 original = side > 0 ? Axis( drag.original.box.maximum, axis ) : Axis( drag.original.box.minimum, axis );
        const f64 boundary = Snap( original + Axis( delta, axis ), ws.bSnapToGrid && !bypass ? ws.gridSize : 0 );
        // Never cross the captured anchor or flip a solid inside out. With
        // a centered anchor, snap only the dragged boundary: mirroring an
        // off-grid center must not silently translate the geometry.
        const f64 divisor = drag.resizeFromCenter ? 2.0 : 1.0;
        const f64 length = std::max( std::min( 1.0, extent ) / divisor, ( boundary - pivot ) * side );
        SetAxis( drag.factors, axis, divisor * length / extent );
        if ( drag.resizeFromCenter || side > 0 ) { SetAxis( bounds.box.maximum, axis, pivot + length ); }
        if ( drag.resizeFromCenter || side < 0 ) { SetAxis( bounds.box.minimum, axis, pivot - length ); }
    }
    if ( drag.blockResize ) { ( void )MapWorkspace_SetBlockPreviewBounds( &ws, bounds ); return; }
    MapWorkspace_SetTransformPreview( &ws, TransformDescriptor( drag ) );
}
bool CancelIdleTool( map_workspace_t &ws, const QKeyEvent *event, bool perspective ) {
    if ( MapInput_ToolGestureKey( &ws, event, perspective ) != map_tool_gesture_key_t::CANCEL ) { return false; }
    // A live edit or construction preview is canceled by the owning view
    // before reaching here. Leaving an idle tool never discards inspection
    // context; a subsequent cancel in Navigation clears the whole selection.
    if ( ws.tool != map_tool_t::NONE ) { MapWorkspace_SetTool( &ws, map_tool_t::NONE ); }
    else { MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE ); }
    return true;
}
Qt::CursorShape ResizeCursor( int handle ) {
    if ( handle < 2 ) { return Qt::SizeHorCursor; }
    if ( handle < 4 ) { return Qt::SizeVerCursor; }
    return handle == 4 || handle == 7 ? Qt::SizeBDiagCursor : Qt::SizeFDiagCursor;
}
template <typename Project>
bool ResizeAxisProjection( Project project, math::vec3d_t point, int axis ) {
    QPointF start, end;
    if ( !project( point, start ) ) { return false; }
    // A visible handle can sit less than one unit from the near plane. Test
    // both signs instead of requiring an arbitrary positive probe to be visible.
    for ( f64 sign : { 1.0, -1.0 } ) {
        if ( project( Add( point, Scale( AxisVector( axis ), sign ) ), end ) &&
             std::isfinite( end.x() ) && std::isfinite( end.y() ) &&
             QPointF::dotProduct( end - start, end - start ) >= 1e-8 ) { return true; }
    }
    return false;
}
// Depth is a gesture value, so changing it does not alter creation defaults
// or append history before the completed primitive is published.
bool AdjustBlockDepth( view_edit_drag_t &drag, map_workspace_t &ws, const QWheelEvent &event ) {
    if ( drag.kind != edit_drag_t::BOX || !( event.modifiers() & Qt::ShiftModifier ) || event.angleDelta().y() == 0 ) { return false; }
    if ( !DragContextMatches( drag, ws ) ) { CancelDrag( drag, ws ); return true; }
    if ( MapWorkspace_PrimitiveDefaults( &ws, {} ).kind == map_primitive_kind_t::QUAD ) { return true; }
    const f64 step = BypassSnap( event.modifiers() ) ? 1.0 : ws.gridSize;
    drag.depth = std::clamp( drag.depth + event.angleDelta().y() / 120.0 * step, 1.0, 65536.0 );
    if ( ws.editPreview.bActive ) {
        auto bounds = ws.editPreview.bounds;
        SetAxis( bounds.box.maximum, drag.depthAxis, Axis( bounds.box.minimum, drag.depthAxis ) + drag.depth );
        MapWorkspace_SetEditPreview( &ws, bounds, static_cast<u32>( drag.depthAxis ) );
    }
    return true;
}
bool AdjustStagedBlockDepth( map_workspace_t &ws, const QWheelEvent &event ) {
    if ( !MapWorkspace_HasBlockPreview( &ws ) || !( event.modifiers() & Qt::ShiftModifier ) || event.angleDelta().y() == 0 ) { return false; }
    if ( ws.editPreview.primitive.kind == map_primitive_kind_t::QUAD ) { return true; }
    const int axis = static_cast<int>( ws.editPreview.blockDepthAxis );
    auto bounds = ws.editPreview.bounds;
    const f64 step = BypassSnap( event.modifiers() ) ? 1.0 : ws.gridSize;
    const f64 depth = std::clamp( Axis( bounds.box.maximum, axis ) - Axis( bounds.box.minimum, axis ) + event.angleDelta().y() / 120.0 * step, 1.0, 65536.0 );
    SetAxis( bounds.box.maximum, axis, Axis( bounds.box.minimum, axis ) + depth );
    ( void )MapWorkspace_SetBlockPreviewBounds( &ws, bounds );
    return true;
}
bool StageBlockDrag( view_edit_drag_t &drag, map_workspace_t &ws ) {
    if ( !DragContextMatches( drag, ws ) ) { CancelDrag( drag, ws ); return false; }
    const auto completed = drag;
    // No listener should still see a pane-owned drag when the shared stage is
    // announced. Releasing a resize leaves the same stage available elsewhere.
    drag = {};
    if ( completed.blockResize ) { return MapWorkspace_HasBlockPreview( &ws ); }
    if ( completed.kind != edit_drag_t::BOX || QLineF( completed.start, completed.current ).length() < 3 ) {
        MapWorkspace_ClearEditPreview( &ws ); return false;
    }
    return MapWorkspace_StageBlockPreview( &ws, static_cast<u32>( completed.depthAxis ) );
}
void CommitDrag( view_edit_drag_t &drag, map_workspace_t &ws ) {
    if ( drag.kind == edit_drag_t::NONE || !DragContextMatches( drag, ws ) ) { CancelDrag( drag, ws ); return; }
    if ( drag.kind == edit_drag_t::BOX || drag.blockResize ) {
        if ( StageBlockDrag( drag, ws ) ) { ( void )MapWorkspace_CommitBlockPreview( &ws ); }
        return;
    }
    if ( drag.kind == edit_drag_t::CLIP ) {
        if ( !ws.editPreview.bActive || ws.editPreview.status != map_status_t::OK ) { return; }
        // Clear the local gesture before publication notifies listeners.
        drag = {}; ( void )MapWorkspace_CommitClipPreview( &ws ); return;
    }
    if ( drag.kind == edit_drag_t::PUSH_PULL ) {
        // A failed private solid is not a publishable gesture. Never retry an
        // unseen distance on release, including after a failed allocation.
        if ( QLineF( drag.start, drag.current ).length() < 3 || !MapWorkspace_HasFacePreview( &ws ) ||
             ws.editPreview.status != map_status_t::OK ) { CancelDrag( drag, ws ); return; }
        drag = {}; ( void )MapWorkspace_CommitFacePreview( &ws ); return;
    }
    const auto completed = drag;
    CancelDrag( drag, ws );
    if ( ( completed.bodyMove && !completed.bodyActivated ) || QLineF( completed.start, completed.current ).length() < 3 ) { return; }
    switch ( completed.kind ) {
        case edit_drag_t::TRANSLATE: ( void )MapWorkspace_TranslateSelection( &ws, completed.delta, ( completed.modifiers & Qt::ShiftModifier ) != 0 ); break;
        case edit_drag_t::SCALE: ( void )MapWorkspace_ScaleSelection( &ws, completed.factors, completed.pivot ); break;
        case edit_drag_t::RESIZE:
            if ( completed.resizeIndividually ) { ( void )MapWorkspace_ResizeSelection( &ws, completed.resizeSides, completed.delta, completed.resizeFromCenter ); }
            else { ( void )MapWorkspace_ScaleSelection( &ws, completed.factors, completed.pivot ); }
            break;
        case edit_drag_t::ROTATE: ( void )MapWorkspace_RotateSelection( &ws, completed.degrees, completed.pivot ); break;
        default: break;
    }
}
template <typename Project>
QRectF ProjectedBounds( const map_bounds_t &bounds, Project project ) {
    QRectF result; bool first = true;
    for ( int c = 0; c < 8; ++c ) {
        const auto &b = bounds.box;
        QPointF point;
        if ( !project( { c & 1 ? b.maximum.x : b.minimum.x, c & 2 ? b.maximum.y : b.minimum.y, c & 4 ? b.maximum.z : b.minimum.z }, point ) ) { continue; }
        if ( first ) { result = QRectF( point, QSizeF() ); first = false; }
        else { result = result.united( QRectF( point, QSizeF( 0.001, 0.001 ) ) ); }
    }
    return result;
}
template <typename Project>
void SelectMarquee( map_workspace_t &ws, const view_edit_drag_t &drag, Project project ) {
    const QRectF area = QRectF( drag.start, drag.current ).normalized();
    const bool crossing = drag.current.x() < drag.start.x();
    const bool preserve = ( drag.modifiers & ( Qt::ControlModifier | Qt::MetaModifier | Qt::ShiftModifier ) ) != 0;
    std::vector<u64> ids;
    if ( preserve ) {
        for ( usize i = 0; i < ws.selection.ids.nCount; ++i ) {
            const auto *object = MapWireframe_FindObject( ws.wire, ws.selection.ids.pData[i] );
            if ( object != nullptr && MapWorkspace_IsSelectableObject( &ws, *object ) ) { ids.push_back( object->id ); }
        }
    }
    for ( usize i = 0; i < ws.wire.objects.nCount; ++i ) {
        const auto &object = ws.wire.objects.pData[i];
        if ( !object.bounds.bHas || !MapWorkspace_IsSelectableObject( &ws, object ) ) { continue; }
        const QRectF projected = ProjectedBounds( object.bounds, project );
        if ( projected.isEmpty() || !( crossing ? area.intersects( projected ) : area.contains( projected ) ) ) { continue; }
        if ( std::find( ids.begin(), ids.end(), object.id ) == ids.end() ) { ids.push_back( object.id ); }
    }
    MapWorkspace_SetSelection( &ws, ids.data(), ids.size() );
}
// Recolour only a real geometry edge. A sphere, wedge, rotated solid, or
// multi-object envelope must not acquire invented box edges as measurement rulers.
bool MeasuredEdgeExists( const map_workspace_t &ws, const map_wireframe_t &wire,
                         math::vec3d_t a, math::vec3d_t b, bool previewWire ) {
    const auto same = []( math::vec3d_t p, math::vec3d_t q ) { const auto delta = Sub( p, q ); return Dot( delta, delta ) <= 1e-12; };
    const auto matches = [&]( math::vec3d_t p, math::vec3d_t q ) { return ( same( a, p ) && same( b, q ) ) || ( same( a, q ) && same( b, p ) ); };
    const bool faceSelection = !previewWire && ws.elementMode == map_element_mode_t::FACES &&
        ( MapWorkspace_HasBrushFace( &ws ) || MapWorkspace_HasMeshFace( &ws ) );
    if ( faceSelection ) {
        for ( usize i = 0; i < wire.faces.nCount; ++i ) {
            const auto &face = wire.faces.pData[i];
            if ( !( ( face.sideId != 0 && face.id == ws.selectedBrushFaceObject && face.sideId == ws.selectedBrushFaceSide ) ||
                    ( face.faceId != 0 && face.id == ws.selectedMeshFaceObject && face.faceId == ws.selectedMeshFaceId ) ) ) { continue; }
            for ( u32 k = 0; k < face.nIndices; ++k ) {
                const auto p = wire.points.pData[wire.faceIndices.pData[face.iFirstIndex + k]];
                const auto q = wire.points.pData[wire.faceIndices.pData[face.iFirstIndex + ( k + 1 ) % face.nIndices]];
                if ( matches( p, q ) ) { return true; }
            }
        }
        return false;
    }
    for ( usize i = 0; i < wire.objects.nCount; ++i ) {
        const auto &object = wire.objects.pData[i];
        if ( object.kind == map_wire_kind_t::ENTITY || ( !previewWire &&
            ( !ObjectSelected( ws, object ) || !MapWorkspace_IsVisible( &ws, object ) ) ) ) { continue; }
        const bool transformed = !previewWire && ws.editPreview.bActive &&
            ws.editPreview.transform.kind != map_transform_preview_kind_t::NONE && MapWorkspace_IsTransformPreviewObject( &ws, object );
        const auto affine = MapWorkspace_TransformPreviewObjectAffine( ws.editPreview.transform, object );
        for ( u32 k = 0; k < object.nLines; ++k ) {
            const auto &line = wire.lines.pData[object.iFirstLine + k];
            auto p = wire.points.pData[line.iA], q = wire.points.pData[line.iB];
            if ( transformed ) { p = math::Affine3d_TransformPoint( affine, p ); q = math::Affine3d_TransformPoint( affine, q ); }
            if ( matches( p, q ) ) { return true; }
        }
    }
    return false;
}

template <typename Project>
void DrawBoundsDimensions( QPainter &painter, const map_workspace_t &ws, const map_bounds_t &bounds,
                           Project project, QRectF viewport, const map_wireframe_t *previewWire = nullptr ) {
    if ( !bounds.bHas ) { return; }
    const auto &wire = previewWire != nullptr ? *previewWire : ws.wire;
    math::vec3d_t corners[8]; BoundsCorners( bounds, corners );
    QPointF screen[8]; bool visible[8]{};
    QPointF center{}; int count = 0;
    for ( int c = 0; c < 8; ++c ) {
        visible[c] = project( corners[c], screen[c] );
        if ( visible[c] ) { center += screen[c]; ++count; }
    }
    if ( count == 0 ) { return; }
    center /= count;
    ( void )project( MapBounds_Center( bounds ), center );
    QVector<QRectF> labels;
    for ( u32 axis = 0; axis < 3; ++axis ) {
        const f64 extent = Axis( bounds.box.maximum, axis ) - Axis( bounds.box.minimum, axis );
        if ( extent <= 1e-9 ) { continue; }
        QLineF edge; f64 best = -1.0; bool realEdge = false;
        for ( u32 c = 0; c < 8; ++c ) {
            const u32 other = c | ( 1u << axis );
            // Only complete sides can describe a dimension. Other complete
            // sides can still be used when one corner crosses the near plane.
            if ( other == c || !visible[c] || !visible[other] ) { continue; }
            const QLineF candidate( screen[c], screen[other] );
            if ( candidate.length() < 18.0 ) { continue; }
            const bool real = MeasuredEdgeExists( ws, wire, corners[c], corners[other], previewWire != nullptr );
            const QPointF offset = candidate.center() - center;
            const f64 score = QPointF::dotProduct( offset, offset );
            if ( ( real && !realEdge ) || ( real == realEdge && score > best ) ) { edge = candidate; best = score; realEdge = real; }
        }
        if ( best < 0.0 ) { continue; }
        if ( realEdge ) {
            painter.save(); painter.setRenderHint( QPainter::Antialiasing );
            painter.setPen( QPen( Token( ws, axis == 0 ? "viewport.axis.x" : axis == 1 ? "viewport.axis.y" : "viewport.axis.z" ),
                gui::EditorStyle_Metric( ws.pGui->style, "viewport.selection.line_width", 1.5 ), Qt::SolidLine, Qt::RoundCap ) );
            painter.drawLine( edge ); painter.restore();
        }
        DrawDimension( painter, ws, edge, center, axis, extent, viewport, labels );
    }
}
template <typename Project, typename ProjectSegment, typename ProjectFace>
void DrawEditPreview( QPainter &painter, const map_workspace_t &ws, Project project, ProjectSegment projectSegment,
    ProjectFace projectFace, QRectF viewport, bool perspective, bool wireframe = false ) {
    if ( !ws.editPreview.bActive ) { return; }
    const auto &box = ws.editPreview.bounds.box;
    math::vec3d_t points[8];
    for ( int c = 0; c < 8; ++c ) { points[c] = { c & 1 ? box.maximum.x : box.minimum.x, c & 2 ? box.maximum.y : box.minimum.y, c & 4 ? box.maximum.z : box.minimum.z }; }
    painter.save();
    painter.setRenderHint( QPainter::Antialiasing );
    const bool clipping = ws.editPreview.bClip;
    const bool creating = ws.tool == map_tool_t::BLOCK || clipping;
    const bool facePushPull = ws.editPreview.bFacePushPull;
    const bool transforming = ws.editPreview.transform.kind != map_transform_preview_kind_t::NONE;
    const bool valid = ( !creating && !facePushPull ) || ws.editPreview.status == map_status_t::OK;
    const QColor color = valid ? SlotColor( ws, ( creating || transforming ) ? LINE_SELECTED : LINE_HOVER ) : Token( ws, "ui.status.error.text" );
    const auto &wire = ws.editPreviewWire;
    const bool exact = ( creating || facePushPull ) && wire.points.nCount != 0 && ( valid || MapWorkspace_HasBlockPreview( &ws ) );
    if ( exact && perspective && !facePushPull ) {
        QColor fill = color; fill.setAlpha( 14 );
        painter.setPen( Qt::NoPen ); painter.setBrush( fill );
        for ( usize i = 0; i < wire.faces.nCount; ++i ) {
            const auto &face = wire.faces.pData[i];
            QPolygonF polygon;
            if ( projectFace( wire, face, polygon ) ) { painter.drawPolygon( polygon ); }
        }
        painter.setBrush( Qt::NoBrush );
    }
    // The envelope is a construction guide; the generated solid is the
    // authoritative shape shown inside it, including curved primitives.
    QColor envelope = color;
    if ( transforming ) { envelope.setAlpha( 120 ); }
    else if ( exact && ws.editPreview.primitive.kind != map_primitive_kind_t::BOX ) { envelope.setAlpha( 70 ); }
    painter.setPen( QPen( envelope, 1.0, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin ) );
    if ( !clipping && !facePushPull && ( !transforming || DisplayFlag( ws, perspective ? "editor.viewport.perspective.show_selection_bounds" : "editor.viewport.show_selection_bounds" ) ) ) { for ( int c = 0; c < 8; ++c ) { for ( int a = 0; a < 3; ++a ) { if ( c & ( 1 << a ) ) { continue; } QLineF line;
        if ( projectSegment( points[c], points[c | ( 1 << a )], line ) ) { painter.drawLine( line ); }
    } } }
    if ( exact && ( !facePushPull || !perspective || wireframe ) ) {
        const map_wire_face_t *selectedSide = nullptr;
        if ( facePushPull && valid ) {
            for ( usize i = 0; i < wire.faces.nCount; ++i ) {
                if ( wire.faces.pData[i].id == ws.editPreview.faceObject && wire.faces.pData[i].sideId == ws.editPreview.faceSide ) {
                    selectedSide = &wire.faces.pData[i]; break;
                }
            }
        }
        painter.setPen( QPen( facePushPull ? SlotColor( ws, LINE_WORLD ) : color, facePushPull ? 1.0 : 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
        for ( usize i = 0; i < wire.lines.nCount; ++i ) {
            const auto &line = wire.lines.pData[i]; QLineF projected;
            // The selected cap was already outlined in selection colour.
            // A neutral antialiased stroke here would erase that cue at 1x.
            bool selectedBoundary = false;
            if ( selectedSide != nullptr ) {
                for ( u32 corner = 0; corner < selectedSide->nIndices; ++corner ) {
                    const u32 a = wire.faceIndices.pData[selectedSide->iFirstIndex + corner];
                    const u32 b = wire.faceIndices.pData[selectedSide->iFirstIndex + ( corner + 1u ) % selectedSide->nIndices];
                    if ( ( line.iA == a && line.iB == b ) || ( line.iA == b && line.iB == a ) ) { selectedBoundary = true; break; }
                }
            }
            if ( selectedBoundary ) { continue; }
            if ( projectSegment( wire.points.pData[line.iA], wire.points.pData[line.iB], projected ) ) { painter.drawLine( projected ); }
        }
    }
    if ( ws.editPreview.bounds.bHas && DisplayFlag( ws, perspective ? "editor.viewport.perspective.show_selection_dimensions" : "editor.viewport.show_selection_dimensions" ) ) {
        DrawBoundsDimensions( painter, ws, ws.editPreview.bounds, project, viewport, exact ? &wire : nullptr );
    }
    painter.restore();
}
template <typename Project, typename ProjectSegment>
int ClipGuideHandles( QPainter *painter, const map_workspace_t &ws, Project project, ProjectSegment projectSegment,
    QRectF viewport, QPointF hover, int orthoDepth = -1 ) {
    const auto &guide = ws.editPreview.clipGuide;
    if ( ws.tool != map_tool_t::CLIP || !ws.editPreview.bActive || !ws.editPreview.bClip || !guide.bHas ||
         guide.extrusionAxis > 2 || ( orthoDepth >= 0 && static_cast<u32>( orthoDepth ) != guide.extrusionAxis ) ) { return -1; }
    QPointF points[2]; bool visible[2]{};
    int hit = -1; f64 closest = 9.0;
    for ( int i = 0; i < 2; ++i ) {
        visible[i] = project( guide.points[i], points[i] ) && std::isfinite( points[i].x() ) && std::isfinite( points[i].y() ) && viewport.contains( points[i] );
        if ( !visible[i] ) { continue; }
        const f64 distance = QLineF( points[i], hover ).length();
        if ( distance <= closest ) { closest = distance; hit = i; }
    }
    if ( painter != nullptr ) {
        painter->save(); painter->setRenderHint( QPainter::Antialiasing );
        const QColor color = ws.editPreview.status == map_status_t::OK ? SlotColor( ws, LINE_HOVER ) : Token( ws, "ui.status.error.text" );
        painter->setPen( QPen( color, 1.5 ) );
        QLineF line; if ( projectSegment( guide.points[0], guide.points[1], line ) ) { painter->drawLine( line ); }
        for ( int i = 0; i < 2; ++i ) {
            if ( !visible[i] ) { continue; }
            painter->setPen( QPen( Token( ws, "viewport.overlay.text" ), 1.5 ) );
            painter->setBrush( i == hit ? SlotColor( ws, LINE_SELECTED ) : color );
            painter->drawRect( QRectF( points[i] - QPointF( 4, 4 ), QSizeF( 8, 8 ) ) );
            const QRectF label( points[i] + QPointF( 8, -15 ), QSizeF( 16, 18 ) );
            if ( viewport.contains( label ) ) { painter->fillRect( label, Token( ws, "viewport.overlay.background" ) ); painter->drawText( label, Qt::AlignCenter, QString::number( i + 1 ) ); }
        }
        painter->restore();
    }
    return hit;
}
template <typename Project, typename ProjectSegment>
void DrawClipGuide( QPainter &painter, const map_workspace_t &ws, Project project, ProjectSegment projectSegment, QRectF viewport ) {
    if ( !ws.editPreview.bActive || !ws.editPreview.bClip || !math::Planed_IsNormalized( ws.editPreview.clipPlane, 1e-8 ) ) { return; }
    const auto bounds = SelectionGeometryBounds( ws );
    if ( !bounds.bHas ) { return; }
    const auto plane = ws.editPreview.clipPlane;
    const auto center = math::Planed_ProjectPointUnit( plane, MapBounds_Center( bounds ) );
    const auto size = Sub( bounds.box.maximum, bounds.box.minimum );
    const f64 radius = std::max( { size.x, size.y, size.z, ws.gridSize * 2 } ) * 0.75;
    const auto basis = std::abs( plane.normal.z ) < 0.8 ? math::vec3d_t{ 0, 0, 1 } : math::vec3d_t{ 0, 1, 0 };
    auto u = math::Vec3d_Cross( plane.normal, basis );
    u = Scale( u, 1.0 / std::sqrt( Dot( u, u ) ) );
    const auto v = math::Vec3d_Cross( plane.normal, u );
    math::vec3d_t corners[4];
    for ( int i = 0; i < 4; ++i ) {
        corners[i] = Add( center, Add( Scale( u, i == 0 || i == 3 ? -radius : radius ), Scale( v, i < 2 ? -radius : radius ) ) );
    }
    painter.save(); painter.setRenderHint( QPainter::Antialiasing );
    const QColor color = ws.editPreview.status == map_status_t::OK ? SlotColor( ws, LINE_HOVER ) : Token( ws, "ui.status.error.text" );
    painter.setPen( QPen( color, 1.5, Qt::DashLine ) );
    for ( int i = 0; i < 4; ++i ) { QLineF line; if ( projectSegment( corners[i], corners[( i + 1 ) % 4], line ) ) { painter.drawLine( line ); } }
    // +normal is Front in every projection, irrespective of drag direction.
    QPointF origin, front;
    if ( project( center, origin ) && project( Add( center, Scale( plane.normal, radius * 0.2 ) ), front ) &&
         viewport.adjusted( -48, -48, 48, 48 ).contains( origin ) && std::isfinite( front.x() ) && std::isfinite( front.y() ) && QLineF( origin, front ).length() > 8 ) {
        const auto direction = ( front - origin ) / QLineF( origin, front ).length();
        front = origin + direction * 34;
        painter.setPen( QPen( color, 2 ) ); painter.drawLine( QLineF( origin, front ) );
        const QPointF tangent( -direction.y(), direction.x() );
        painter.drawLine( QLineF( front, front - direction * 7 + tangent * 4 ) );
        painter.drawLine( QLineF( front, front - direction * 7 - tangent * 4 ) );
        const QString label = QStringLiteral( "Front +" );
        const QRectF area( front + QPointF( 6, -painter.fontMetrics().height() ), QSizeF( painter.fontMetrics().horizontalAdvance( label ) + 4, painter.fontMetrics().height() + 4 ) );
        if ( viewport.contains( area ) ) { painter.fillRect( area, Token( ws, "viewport.overlay.background" ) ); painter.drawText( area, Qt::AlignCenter, label ); }
    }
    painter.restore();
}
void DrawEditReadout( QPainter &painter, const map_workspace_t &ws, const view_edit_drag_t &drag, QRectF viewport, bool perspective ) {
    const bool clipping = ws.editPreview.bActive && ws.editPreview.bClip;
    const bool block = MapWorkspace_HasBlockPreview( &ws );
    const bool facePushPull = MapWorkspace_HasFacePreview( &ws );
    const auto &transform = ws.editPreview.transform;
    const bool transforming = transform.kind != map_transform_preview_kind_t::NONE;
    const bool dimensions = DisplayFlag( ws, perspective ? "editor.viewport.perspective.show_selection_dimensions" : "editor.viewport.show_selection_dimensions" );
    if ( !ws.editPreview.bActive || ( !clipping && !block && !facePushPull && ( ( !transforming && drag.kind == edit_drag_t::NONE ) || drag.kind == edit_drag_t::MARQUEE ||
         !dimensions ) ) ) { return; }
    const auto &box = ws.editPreview.bounds.box;
    const math::vec3d_t size = Sub( box.maximum, box.minimum );
    QString operation, detail;
    const auto kind = clipping ? edit_drag_t::CLIP : block ? edit_drag_t::BOX : facePushPull ? edit_drag_t::PUSH_PULL : transforming ?
        transform.kind == map_transform_preview_kind_t::TRANSLATE ? edit_drag_t::TRANSLATE :
        transform.kind == map_transform_preview_kind_t::SCALE ? edit_drag_t::SCALE : edit_drag_t::ROTATE : drag.kind;
    switch ( kind ) {
        case edit_drag_t::CLIP: {
            const auto mode = ws.editPreview.clipMode;
            operation = mode == map_brush_clip_mode_t::BOTH ? QStringLiteral( "Slice · keep both" ) : mode == map_brush_clip_mode_t::FRONT ? QStringLiteral( "Clip · keep Front +" ) : QStringLiteral( "Clip · keep Back −" );
            const auto keys = MapInput_ToolGestureBindings( &ws, map_tool_gesture_key_t::CONFIRM, perspective );
            detail = keys.isEmpty() ? QStringLiteral( "Awaiting confirmation binding" ) : keys.join( QStringLiteral( " / " ) ) + QStringLiteral( ": confirm" );
            break;
        }
        case edit_drag_t::BOX: {
            operation = block ? ( drag.kind == edit_drag_t::RESIZE && drag.resizeFromCenter ? QStringLiteral( "Size primitive · center" ) : QStringLiteral( "Size primitive" ) ) : QStringLiteral( "Draw footprint" );
            const auto keys = MapInput_ToolGestureBindings( &ws, map_tool_gesture_key_t::CONFIRM, perspective );
            detail = block ? ( keys.isEmpty() ? QStringLiteral( "Awaiting confirmation binding" ) : keys.join( QStringLiteral( " / " ) ) + QStringLiteral( ": create" ) ) :
                QStringLiteral( "Release: adjust size · Shift + wheel: depth" );
            if ( block && ws.editPreview.bBlockCommitFailed ) { operation = QStringLiteral( "Create failed · retry" ); }
            break;
        }
        case edit_drag_t::TRANSLATE:
            operation = transform.bClone ? QStringLiteral( "Clone + move" ) : QStringLiteral( "Move" );
            detail = QStringLiteral( "Δ  %1 / %2 / %3 u" ).arg( NumberText( transform.delta.x ), NumberText( transform.delta.y ), NumberText( transform.delta.z ) ); break;
        case edit_drag_t::SCALE:
            operation = transform.bResize ? ( transform.bResizeFromCenter ? QStringLiteral( "Resize · center" ) : QStringLiteral( "Resize · opposite side" ) ) : QStringLiteral( "Scale" );
            if ( transform.bResizeIndividually ) {
                operation = QStringLiteral( "Resize %1 objects · %2" ).arg( ws.selection.ids.nCount ).arg(
                    transform.bResizeFromCenter ? QStringLiteral( "each center" ) : QStringLiteral( "each opposite side" ) );
                detail = QStringLiteral( "Δ  %1 / %2 / %3 u each" ).arg( NumberText( transform.delta.x ), NumberText( transform.delta.y ), NumberText( transform.delta.z ) );
            } else { detail = QStringLiteral( "×  %1 / %2 / %3" ).arg( NumberText( transform.factors.x ), NumberText( transform.factors.y ), NumberText( transform.factors.z ) ); }
            break;
        case edit_drag_t::ROTATE:
            operation = QStringLiteral( "Rotate" );
            detail = QStringLiteral( "%1° / %2° / %3°" ).arg( NumberText( transform.degrees.x ), NumberText( transform.degrees.y ), NumberText( transform.degrees.z ) ); break;
        case edit_drag_t::PUSH_PULL: operation = QStringLiteral( "Push / pull" ); detail = NumberText( ws.editPreview.faceDistance ) + QStringLiteral( " u" ); break;
        default: return;
    }
    const QString extents = clipping && !ws.editPreview.bounds.bHas ? QStringLiteral( "Retained result: empty" ) :
        QStringLiteral( "X %1   Y %2   Z %3 u" ).arg( NumberText( size.x ), NumberText( size.y ), NumberText( size.z ) );
    if ( ( drag.kind == edit_drag_t::BOX || block || clipping || facePushPull ) && ws.editPreview.status != map_status_t::OK ) {
        operation = clipping ? QStringLiteral( "Cannot clip" ) : facePushPull ? QStringLiteral( "Cannot push / pull" ) : QStringLiteral( "Cannot create" );
        detail = QString::fromLatin1( MapDocument_StatusName( ws.editPreview.status ) );
    }
    const QString heading = operation + QStringLiteral( "  ·  " ) + detail;
    const QFontMetrics metrics = painter.fontMetrics();
    const qreal available = viewport.width() - 36.0;
    if ( available <= 20.0 ) { return; }
    QStringList lines;
    if ( metrics.horizontalAdvance( heading ) > available ) { lines << operation << detail; }
    else { lines << heading; }
    qsizetype firstDimension = lines.size();
    if ( dimensions && ( !facePushPull || ws.editPreview.bounds.bHas ) ) {
        if ( ( clipping && !ws.editPreview.bounds.bHas ) || metrics.horizontalAdvance( extents ) <= available ) { lines << extents; }
        else {
            lines << QStringLiteral( "X %1 u" ).arg( NumberText( size.x ) )
                  << QStringLiteral( "Y %1 u" ).arg( NumberText( size.y ) )
                  << QStringLiteral( "Z %1 u" ).arg( NumberText( size.z ) );
        }
    }
    const qreal lineHeight = metrics.height();
    if ( lineHeight * lines.size() + 14 > viewport.height() - 40 && lines.size() > 2 ) {
        // Keep feedback visible in short split panes. A compact XYZ row is
        // preferable to dropping the entire construction/status readout.
        lines.clear(); lines << heading; firstDimension = 1;
        if ( dimensions ) {
            lines << ( clipping && !ws.editPreview.bounds.bHas ? extents :
                QStringLiteral( "XYZ %1 × %2 × %3 u" ).arg( NumberText( size.x ), NumberText( size.y ), NumberText( size.z ) ) );
        }
    }
    qreal textWidth = 0;
    for ( const auto &line : lines ) { textWidth = std::max<qreal>( textWidth, metrics.horizontalAdvance( line ) ); }
    const qreal width = std::min( textWidth, available ) + 20.0;
    const qreal height = lineHeight * lines.size() + 14.0;
    if ( height > viewport.height() - 16.0 ) { return; }
    const QRectF label( viewport.right() - width - 8.0, viewport.bottom() - height - 8.0, width, height );
    painter.save();
    painter.setPen( QPen( Token( ws, "ui.border" ), 1.0 ) );
    painter.setBrush( Token( ws, "viewport.overlay.background" ) );
    painter.drawRoundedRect( label, 3, 3 );
    for ( qsizetype i = 0; i < lines.size(); ++i ) {
        painter.setPen( i >= firstDimension ? SlotColor( ws, LINE_SELECTED ) : Token( ws, "viewport.overlay.text" ) );
        painter.drawText( label.adjusted( 10, 6 + metrics.height() * i, -10, -6 ), Qt::AlignLeft | Qt::AlignTop,
                          metrics.elidedText( lines[i], Qt::ElideRight, static_cast<int>( width - 20 ) ) );
    }
    painter.restore();
}
void DrawMarquee( QPainter &painter, const view_edit_drag_t &drag, const map_workspace_t &ws ) {
    if ( drag.kind != edit_drag_t::MARQUEE || QLineF( drag.start, drag.current ).length() < 3 ) { return; }
    QColor colour = SlotColor( ws, LINE_HOVER ), fill = colour; fill.setAlpha( 24 );
    painter.save(); painter.setPen( QPen( colour, 1, drag.current.x() < drag.start.x() ? Qt::DashLine : Qt::SolidLine ) );
    painter.setBrush( fill ); painter.drawRect( QRectF( drag.start, drag.current ).normalized() ); painter.restore();
}
// The original authored edges are a location reference, not another selected
// solid. Keep this separate from hidden-object and clip ghosts. Every pane
// reads the shared descriptor, including panes that did not capture the drag.
template <typename Project, typename ProjectSegment>
void DrawMoveSource( QPainter &painter, const map_workspace_t &ws, Project project, ProjectSegment projectSegment, QRectF viewport, bool perspective ) {
    if ( !MoveSourceActive( ws ) ) { return; }
    QVector<QLineF> source;
    for ( usize i = 0; i < ws.wire.objects.nCount; ++i ) {
        const auto &object = ws.wire.objects.pData[i];
        if ( !MapWorkspace_IsTransformPreviewObject( &ws, object ) ) { continue; }
        for ( u32 edge = 0; edge < object.nLines; ++edge ) {
            const auto &line = ws.wire.lines.pData[object.iFirstLine + edge]; QLineF projected;
            if ( projectSegment( ws.wire.points.pData[line.iA], ws.wire.points.pData[line.iB], projected ) ) { source.append( projected ); }
        }
        if ( object.kind == map_wire_kind_t::ENTITY && object.bounds.bHas ) {
            math::vec3d_t corners[8]; BoundsCorners( object.bounds, corners );
            for ( int corner = 0; corner < 8; ++corner ) { for ( int axis = 0; axis < 3; ++axis ) {
                if ( corner & ( 1 << axis ) ) { continue; }
                QLineF projected;
                if ( projectSegment( corners[corner], corners[corner | ( 1 << axis )], projected ) ) { source.append( projected ); }
            } }
        }
    }
    painter.save(); painter.setRenderHint( QPainter::Antialiasing ); painter.setBrush( Qt::NoBrush );
    const QColor color = Token( ws, "viewport.transform.source" );
    const QColor background = Token( ws, "viewport.overlay.background" );
    // A dark separator keeps the warm-red dashes readable on both textured
    // surfaces and dense grids. Qt pen widths/dashes use logical pixels.
    painter.setPen( QPen( background, 4.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) ); painter.drawLines( source );
    QPen ghost( color, 2.0, Qt::CustomDashLine, Qt::RoundCap, Qt::RoundJoin ); ghost.setDashPattern( { 4, 3 } );
    painter.setPen( ghost ); painter.drawLines( source );
    const auto &transform = ws.editPreview.transform;
    const auto destination = Add( transform.pivot, transform.delta );
    QLineF travel;
    const bool travelVisible = projectSegment( transform.pivot, destination, travel );
    if ( travelVisible && travel.length() > 1.0 ) {
        painter.setPen( QPen( background, 4.0 ) ); painter.drawLine( travel );
        painter.setPen( ghost ); painter.drawLine( travel );
        QPointF endpoint;
        if ( project( destination, endpoint ) && viewport.contains( endpoint ) && travel.length() >= 12 ) {
            const auto direction = ( travel.p2() - travel.p1() ) / travel.length();
            const QPointF normal( -direction.y(), direction.x() );
            painter.setPen( QPen( color, 1.5 ) );
            painter.drawLine( QLineF( endpoint, endpoint - direction * 7 + normal * 4 ) );
            painter.drawLine( QLineF( endpoint, endpoint - direction * 7 - normal * 4 ) );
        }
    }
    const auto label = [&]( QPointF point, const QString &text ) {
        const auto metrics = painter.fontMetrics();
        const QSizeF size( metrics.horizontalAdvance( text ) + 10, metrics.height() + 4 );
        const QRectF area( point, size );
        if ( !viewport.adjusted( 4, 4, -4, -4 ).contains( area ) ) { return; }
        painter.fillRect( area, background ); painter.setPen( color ); painter.drawText( area, Qt::AlignCenter, text );
    };
    QPointF start;
    if ( project( transform.pivot, start ) && viewport.contains( start ) ) {
        painter.setPen( QPen( background, 4 ) ); painter.drawEllipse( start, 3, 3 );
        painter.setPen( QPen( color, 1.5 ) ); painter.drawEllipse( start, 3, 3 );
        label( start + QPointF( 8, 8 ), transform.bClone ? QStringLiteral( "Source" ) : QStringLiteral( "Start" ) );
    }
    if ( travelVisible && travel.length() >= 90 && DisplayFlag( ws, perspective ? "editor.viewport.perspective.show_selection_dimensions" : "editor.viewport.show_selection_dimensions" ) ) {
        const QString distance = NumberText( std::hypot( transform.delta.x, transform.delta.y, transform.delta.z ) ) + QStringLiteral( " u" );
        const QPointF middle = ( travel.p1() + travel.p2() ) * 0.5;
        label( middle + QPointF( 8, 8 ), distance );
    }
    painter.restore();
}
template <typename Project, typename ProjectSegment, typename ProjectFace>
void DrawTransformPreview( QPainter &painter, const map_workspace_t &ws, Project project, ProjectSegment projectSegment,
    ProjectFace projectFace, QRectF viewport, bool perspective, bool translucentFaces = false ) {
    if ( !ws.editPreview.bActive || ws.editPreview.transform.kind == map_transform_preview_kind_t::NONE ) { return; }
    painter.save(); painter.setRenderHint( QPainter::Antialiasing );
    painter.setPen( QPen( SlotColor( ws, LINE_SELECTED ), 2.0 ) );
    if ( perspective && translucentFaces ) {
        QColor fill = SlotColor( ws, LINE_SELECTED ); fill.setAlpha( 30 );
        painter.setPen( Qt::NoPen ); painter.setBrush( fill );
        for ( usize i = 0; i < ws.wire.faces.nCount; ++i ) {
            const auto &face = ws.wire.faces.pData[i];
            const auto *object = MapWireframe_FindObject( ws.wire, face.id );
            if ( object == nullptr || !MapWorkspace_IsTransformPreviewObject( &ws, *object ) ) { continue; }
            QPolygonF polygon;
            if ( projectFace( ws.wire, face, polygon ) ) { painter.drawPolygon( polygon ); }
        }
        painter.setBrush( Qt::NoBrush ); painter.setPen( QPen( SlotColor( ws, LINE_SELECTED ), 2.0 ) );
    }
    QVector<QRectF> labels;
    QVector<QPointF> vertices;
    const bool showVertices = DisplayFlag( ws, perspective ? "editor.viewport.perspective.show_selection_vertices" : "editor.viewport.show_selection_vertices", CY_FALSE );
    for ( usize i = 0; i < ws.wire.objects.nCount; ++i ) {
        const auto &object = ws.wire.objects.pData[i];
        if ( !MapWorkspace_IsTransformPreviewObject( &ws, object ) ) { continue; }
        const auto affine = MapWorkspace_TransformPreviewObjectAffine( ws.editPreview.transform, object );
        if ( showVertices ) { for ( u32 vertex = 0; vertex < object.nPoints; ++vertex ) {
            QPointF point;
            if ( project( math::Affine3d_TransformPoint( affine, ws.wire.points.pData[object.iFirstPoint + vertex] ), point ) ) { vertices.append( point ); }
        } }
        for ( u32 edge = 0; edge < object.nLines; ++edge ) {
            const auto &line = ws.wire.lines.pData[object.iFirstLine + edge]; QLineF projected;
            const auto a = math::Affine3d_TransformPoint( affine, ws.wire.points.pData[line.iA] );
            const auto b = math::Affine3d_TransformPoint( affine, ws.wire.points.pData[line.iB] );
            if ( projectSegment( a, b, projected ) ) { painter.drawLine( projected ); }
        }
        if ( object.kind == map_wire_kind_t::ENTITY && object.bounds.bHas ) {
            math::vec3d_t corners[8]; BoundsCorners( object.bounds, corners );
            for ( int c = 0; c < 8; ++c ) { for ( int axis = 0; axis < 3; ++axis ) {
                if ( c & ( 1 << axis ) ) { continue; }
                QLineF line;
                if ( projectSegment( math::Affine3d_TransformPoint( affine, corners[c] ), math::Affine3d_TransformPoint( affine, corners[c | ( 1 << axis )] ), line ) ) { painter.drawLine( line ); }
            } }
            const auto *entity = MapWireframe_FindEntity( ws.wire, object.id );
            QPointF origin;
            if ( entity != nullptr && entity->bHasOrigin && project( math::Affine3d_TransformPoint( affine, entity->origin ), origin ) && viewport.contains( origin ) ) {
                painter.drawLine( QLineF( origin + QPointF( -4, 0 ), origin + QPointF( 4, 0 ) ) );
                painter.drawLine( QLineF( origin + QPointF( 0, -4 ), origin + QPointF( 0, 4 ) ) );
                if ( ShowEntityName( ws, entity->id, perspective ) ) {
                    DrawEntityLabel( painter, ws, *entity, QRectF( origin, QSizeF() ), viewport, labels, perspective );
                }
            }
        }
    }
    DrawVertexCues( painter, ws, vertices, viewport );
    painter.restore();
}
// A ball and a short pointer share an axis colour, with no dark contour.
// The ball's centre remains the authoritative pickup for bounds resizing.
void DrawResizeCap( QPainter &painter, QPointF at, QPointF direction, QColor color, bool active ) {
    constexpr qreal radius = 4.0;
    const QPointF perpendicular( -direction.y(), direction.x() );
    const QPolygonF pointer{ at + direction * 9.0, at + direction * 2.0 + perpendicular * 3.0,
                             at + direction * 2.0 - perpendicular * 3.0 };
    painter.setPen( Qt::NoPen ); painter.setBrush( color ); painter.drawPolygon( pointer );
    QRadialGradient sphere( at - QPointF( 1.0, 1.0 ), radius * 1.5 );
    sphere.setColorAt( 0.0, color.lighter( 112 ) ); sphere.setColorAt( 0.55, color );
    sphere.setColorAt( 1.0, color.darker( 115 ) );
    painter.setPen( QPen( color, active ? 1.25 : 0.75 ) ); painter.setBrush( sphere );
    painter.drawEllipse( at, radius, radius );
}

// Bounds controls resize around their captured anchor without leaving Select.
// Drawing and picking share the same projected points and hit priority.
template <typename Project>
int ResizeHandles( QPainter *painter, const map_workspace_t &ws, Project project, QRectF viewport,
                   QPointF pick, int u = -1, int v = -1, const view_edit_drag_t *drag = nullptr,
                   f64 hitRadius = 10.0, int hoverTarget = -2, u32 allowedHandles = 0xffu, f64 moveLength = 0.0 ) {
    const bool block = MapWorkspace_HasBlockPreview( &ws );
    if ( !block && ( !SelectMovesObjects( ws ) || !MapWorkspace_CanEditSelection( &ws ) ) ) { return -1; }
    if ( ws.editPreview.bActive && ws.editPreview.transform.kind != map_transform_preview_kind_t::NONE && !ws.editPreview.transform.bResize ) { return -1; }
    const auto bounds = block || ( ws.editPreview.bActive && ws.editPreview.transform.bResize ) ? ws.editPreview.bounds : SelectionGeometryBounds( ws );
    QPointF center;
    if ( !bounds.bHas || !project( MapBounds_Center( bounds ), center ) ) { return -1; }
    f64 minimumRadius = 24.0;
    if ( !block && moveLength > 0.0 && CanTransform( ws ) ) {
        // Keep bounds controls outside the foreground move controls in both
        // projections. Orthographic squares need enough room for two separate
        // ten-pixel pickups, rather than relying on priority at an overlap.
        // Measure the actual projection, including the configured gizmo scale.
        const f64 clearance = u >= 0 ? 24.0 : 18.0;
        const auto pivot = ws.editPreview.bActive && ws.editPreview.transform.bResize ? MapBounds_Center( bounds ) :
            GizmoPivot( ws, EditSelectionBounds( ws ) );
        for ( int axis = 0; axis < 3; ++axis ) {
            QPointF end;
            if ( project( Add( pivot, Scale( AxisVector( axis ), moveLength ) ), end ) &&
                 std::isfinite( end.x() ) && std::isfinite( end.y() ) ) {
                minimumRadius = std::max( minimumRadius, QLineF( center, end ).length() + clearance );
            }
        }
    }
    const int count = u < 0 ? 6 : 8;
    int hit = -1; f64 nearest = hitRadius;
    QPointF points[8], anchors[8]; bool visible[8]{};
    for ( int handle = 0; handle < count; ++handle ) {
        if ( ( allowedHandles & ( 1u << handle ) ) == 0u ) { continue; }
        const auto sides = ResizeSides( handle, u, v );
        const auto point = ResizePoint( bounds, sides );
        visible[handle] = CanResizeSides( bounds, sides ) && ( block || CanResizeSelectionSides( ws, sides ) ) && ( u >= 0 || ResizeAxisProjection( project, point, handle / 2 ) ) && project( point, anchors[handle] ) &&
            std::isfinite( anchors[handle].x() ) && std::isfinite( anchors[handle].y() ) && viewport.contains( anchors[handle] );
        if ( !visible[handle] ) { continue; }
        points[handle] = anchors[handle];
        // Small geometry must not lose its controls inside the pivot. Pull
        // controls out in logical pixels, with leaders to the real boundary.
        // Dragging still captures the real world anchor and press-plane offset.
        QPointF offset = points[handle] - center;
        if ( u >= 0 ) {
            if ( Axis( sides, u ) != 0 ) { offset.setX( std::copysign( std::max( std::abs( offset.x() ), minimumRadius ), Axis( sides, u ) ) ); }
            if ( Axis( sides, v ) != 0 ) { offset.setY( -std::copysign( std::max( std::abs( offset.y() ), minimumRadius ), Axis( sides, v ) ) ); }
        } else {
            const f64 distance = std::hypot( offset.x(), offset.y() );
            if ( distance < 1e-6 ) { visible[handle] = false; continue; }
            if ( distance < minimumRadius ) { offset *= minimumRadius / distance; }
        }
        points[handle] = center + offset;
        if ( drag != nullptr && drag->kind == edit_drag_t::RESIZE &&
             sides.x == drag->resizeSides.x && sides.y == drag->resizeSides.y && sides.z == drag->resizeSides.z ) {
            // Keep the captured control attached to the pickup while its boundary
            // grows past the minimum spacing; recomputing the idle offset
            // would leave the active control behind the cursor.
            points[handle] = anchors[handle] + drag->resizeScreenOffset;
        }
        visible[handle] = viewport.adjusted( 8, 8, -8, -8 ).contains( points[handle] );
        if ( !visible[handle] ) { continue; }
        const f64 distance = QLineF( points[handle], pick ).length();
        if ( distance <= nearest ) { nearest = distance; hit = handle; }
    }
    if ( painter != nullptr ) {
        if ( drag != nullptr && drag->kind == edit_drag_t::RESIZE ) { hit = -1; }
        else if ( hoverTarget >= -1 ) { hit = hoverTarget; }
        painter->save(); painter->setRenderHint( QPainter::Antialiasing );
        const QColor background = Token( ws, "viewport.overlay.background" );
        for ( int handle = 0; handle < count; ++handle ) {
            if ( visible[handle] && QLineF( anchors[handle], points[handle] ).length() > 1.0 ) {
                painter->setPen( QPen( Token( ws, "viewport.overlay.text" ), 1.0, Qt::DotLine ) );
                painter->drawLine( QLineF( anchors[handle], points[handle] ) );
            }
        }
        if ( u < 0 ) {
            // Each signed pair describes one world axis. The same projected
            // endpoints are used for hit testing; guides never invent a
            // handle across the near plane or beyond the viewport.
            for ( int axis = 0; axis < 3; ++axis ) {
                if ( !visible[axis * 2] || !visible[axis * 2 + 1] ) { continue; }
                QColor guide = Token( ws, axis == 0 ? "viewport.axis.x" : axis == 1 ? "viewport.axis.y" : "viewport.axis.z" );
                const bool active = ( hit >= 0 && hit / 2 == axis ) ||
                    ( drag != nullptr && drag->kind == edit_drag_t::RESIZE && Axis( drag->resizeSides, axis ) != 0 );
                guide.setAlpha( active ? 170 : 85 );
                QPen pen( guide, active ? 1.25 : 1.0, Qt::DashLine ); pen.setDashPattern( { 4, 4 } );
                painter->setPen( pen );
                painter->drawLine( QLineF( points[axis * 2], points[axis * 2 + 1] ) );
            }
        }
        for ( int handle = 0; handle < count; ++handle ) {
            if ( !visible[handle] ) { continue; }
            const auto sides = ResizeSides( handle, u, v );
            const bool captured = drag != nullptr && drag->kind == edit_drag_t::RESIZE &&
                sides.x == drag->resizeSides.x && sides.y == drag->resizeSides.y && sides.z == drag->resizeSides.z;
            const int axis = u < 0 ? handle / 2 : handle < 2 ? u : v;
            QColor color = handle == hit || captured ? SlotColor( ws, LINE_HOVER ) :
                u >= 0 && handle >= 4 ? SlotColor( ws, LINE_SELECTED ) : Token( ws, axis == 0 ? "viewport.axis.x" : axis == 1 ? "viewport.axis.y" : "viewport.axis.z" );
            painter->setPen( QPen( color, u >= 0 || captured ? 2.0 : 1.5 ) );
            painter->setBrush( background );
            const QPointF p = points[handle];
            if ( u < 0 ) {
                // The ball marks the boundary pickup; its short pointer
                // communicates the signed resize direction. Logical-pixel
                // caps do not change the shared ten-pixel hit tolerance.
                const QPointF direction = ( p - center ) / QLineF( center, p ).length();
                DrawResizeCap( *painter, p, direction, color, captured || handle == hit );
            }
            else {
                // The larger bounds control surrounds a vertex cue instead
                // of painting over it. The independent vertex display toggle
                // must remain visible even at box corners.
                painter->setBrush( Qt::NoBrush );
                painter->drawRect( QRectF( p - QPointF( 4, 4 ), QSizeF( 8, 8 ) ) );
            }
            if ( handle == hit || captured ) {
                QStringList constraints;
                for ( int dimension = 0; dimension < 3; ++dimension ) {
                    const f64 side = Axis( sides, dimension );
                    if ( side != 0 ) { constraints << QString( QChar( kAxisLetters[dimension] ) ) +
                        ( side > 0 ? QStringLiteral( "+" ) : QStringLiteral( "−" ) ); }
                }
                const bool centered = captured ? drag->resizeFromCenter : DisplayFlag( ws, "editor.map.resize_from_center", CY_FALSE ) || ( QApplication::keyboardModifiers() & Qt::ShiftModifier ) != 0;
                const QString label = QStringLiteral( "Resize " ) + constraints.join( QLatin1Char( ' ' ) ) +
                    ( !block && ( captured ? drag->resizeIndividually : IndividualResize( ws ) ) ? QStringLiteral( " · each object" ) : QString() ) +
                    ( centered ? QStringLiteral( " · center" ) : QStringLiteral( " · Shift: center" ) );
                const QFontMetrics metrics = painter->fontMetrics();
                const qreal width = std::min<qreal>( metrics.horizontalAdvance( label ) + 12, viewport.width() - 16 );
                const qreal height = metrics.height() + 6;
                if ( width > 20 && height < viewport.height() - 16 ) {
                    const QRectF area( std::clamp( p.x() + 12, viewport.left() + 8, viewport.right() - width - 8 ),
                                       std::clamp( p.y() + 14, viewport.top() + 8, viewport.bottom() - height - 8 ), width, height );
                    painter->fillRect( area, background ); painter->setPen( color );
                    painter->drawText( area, Qt::AlignCenter, metrics.elidedText( label, Qt::ElideRight, static_cast<int>( width - 12 ) ) );
                }
            }
        }
        painter->restore();
    }
    return hit;
}
// RGB shafts/rings, planar pads, and a free/uniform center use the same
// projected geometry for drawing and picking. Handle IDs 0..2 are axes,
// 3..5 are planes (normal = id-3), and 6 is the center.
template <typename Project>
int EditGizmo( QPainter *painter, const map_workspace_t &ws, Project project, f64 length, QPointF pick = { -10000, -10000 }, int rotationAxis = -1,
               const view_edit_drag_t *drag = nullptr, f64 hitRadius = 10.0, int hoverTarget = -2 ) {
    if ( ws.editPreview.bActive && ws.editPreview.transform.bResize ) { return -1; }
    const auto tool = TransformTool( ws );
    if ( TransformDrag( tool ) == edit_drag_t::NONE || !CanTransform( ws ) ) { return -1; }
    const map_bounds_t bounds = EditSelectionBounds( ws );
    const bool dragging = drag != nullptr && ( drag->kind == edit_drag_t::TRANSLATE || drag->kind == edit_drag_t::SCALE || drag->kind == edit_drag_t::ROTATE );
    const math::vec3d_t pivot = dragging ? MapWorkspace_TransformPreviewPoint( TransformDescriptor( *drag ), drag->pivot ) : GizmoPivot( ws, bounds );
    QPointF center;
    if ( !bounds.bHas || !project( pivot, center ) ) { return -1; }
    const bool rotate = tool == map_tool_t::ROTATE, scale = tool == map_tool_t::SCALE;
    int best = -1; f64 nearest = hitRadius;
    const int hovered = dragging ? ( drag->axis >= 0 ? drag->axis : drag->planeAxis >= 0 ? 3 + drag->planeAxis : 6 ) :
                        painter != nullptr ? hoverTarget >= -1 ? hoverTarget : EditGizmo( nullptr, ws, project, length, pick, rotationAxis, nullptr, hitRadius ) : -1;
    const QColor colours[]{ Token( ws, "viewport.axis.x" ), Token( ws, "viewport.axis.y" ), Token( ws, "viewport.axis.z" ) };
    const QColor backdrop = Token( ws, "viewport.overlay.background" );
    const auto polygonHit = [&]( const QPolygonF &polygon ) {
        if ( polygon.containsPoint( pick, Qt::OddEvenFill ) ) { return 0.0; }
        f64 distance = hitRadius + 1.0;
        for ( qsizetype i = 0; i < polygon.size(); ++i ) {
            distance = std::min( distance, ScreenSegmentDistance( pick, QLineF( polygon[i], polygon[( i + 1 ) % polygon.size()] ) ) );
        }
        return distance;
    };
    // Center has priority over converging shafts; pads precede nearby shafts.
    const QRectF centerRect( center.x() - 5, center.y() - 5, 10, 10 );
    if ( !rotate && centerRect.adjusted( -2, -2, 2, 2 ).contains( pick ) ) { best = 6; nearest = 0.0; }
    if ( !rotate ) {
        for ( int normal = 0; normal < 3; ++normal ) {
            const int u = ( normal + 1 ) % 3, v = ( normal + 2 ) % 3;
            QPolygonF pad;
            for ( const QPointF corner : { QPointF( .22, .22 ), QPointF( .40, .22 ), QPointF( .40, .40 ), QPointF( .22, .40 ) } ) {
                auto point = pivot;
                SetAxis( point, u, Axis( point, u ) + corner.x() * length );
                SetAxis( point, v, Axis( point, v ) + corner.y() * length );
                QPointF projected;
                if ( !project( point, projected ) ) { pad.clear(); break; }
                pad << projected;
            }
            if ( pad.size() != 4 ) { continue; }
            const auto a = pad[1] - pad[0], b = pad[3] - pad[0];
            if ( std::abs( a.x() * b.y() - a.y() * b.x() ) < 20.0 ) { continue; } // Edge-on plane.
            const f64 distance = polygonHit( pad );
            if ( distance < nearest ) { nearest = distance; best = 3 + normal; }
            if ( painter != nullptr ) {
                const QColor color = hovered == 3 + normal ? SlotColor( ws, LINE_HOVER ) : colours[normal];
                QColor fill = color; fill.setAlpha( hovered == 3 + normal ? 65 : 28 );
                painter->save(); painter->setPen( QPen( color, 1.0 ) ); painter->setBrush( fill ); painter->drawPolygon( pad ); painter->restore();
            }
        }
    }
    for ( int axis = 0; axis < 3; ++axis ) {
        if ( rotate && rotationAxis >= 0 && axis != rotationAxis ) { continue; }
        QPolygonF shape, cap;
        if ( rotate ) {
            // Do not bridge a near-clipped ring through invisible samples.
            for ( int i = 0; i < 48; ++i ) {
                QPointF ends[2]; bool visible = true;
                for ( int j = 0; j < 2; ++j ) {
                    const f64 theta = ( i + j ) * 6.283185307179586 / 48;
                    auto point = pivot;
                    SetAxis( point, ( axis + 1 ) % 3, Axis( point, ( axis + 1 ) % 3 ) + std::cos( theta ) * length );
                    SetAxis( point, ( axis + 2 ) % 3, Axis( point, ( axis + 2 ) % 3 ) + std::sin( theta ) * length );
                    visible = project( point, ends[j] ) && visible;
                }
                if ( !visible ) { continue; }
                const QLineF line( ends[0], ends[1] );
                const f64 distance = ScreenSegmentDistance( pick, line );
                if ( distance < nearest ) { nearest = distance; best = axis; }
                if ( painter != nullptr ) {
                    painter->save(); painter->setPen( QPen( hovered == axis ? SlotColor( ws, LINE_HOVER ) : colours[axis], hovered == axis ? 1.75 : 1.25 ) );
                    painter->drawLine( line ); painter->restore();
                }
            }
            continue;
        }
        QPointF end;
        if ( !project( Add( pivot, Scale( AxisVector( axis ), length ) ), end ) || QLineF( center, end ).length() < 14 ) { continue; }
        const QPointF direction = end - center;
        const f64 magnitude = std::hypot( direction.x(), direction.y() );
        const QPointF unit = direction / magnitude, perpendicular( -unit.y(), unit.x() );
        shape << center << end;
        if ( scale ) { cap << end + QPointF( -3, -3 ) << end + QPointF( 3, -3 ) << end + QPointF( 3, 3 ) << end + QPointF( -3, 3 ); }
        else { cap << end << end - unit * 8 + perpendicular * 3 << end - unit * 8 - perpendicular * 3; }
        const f64 distance = std::min( ScreenSegmentDistance( pick, QLineF( center, end ) ), polygonHit( cap ) );
        if ( distance < nearest ) { nearest = distance; best = axis; }
        if ( painter == nullptr ) { continue; }
        const QColor color = axis == hovered ? SlotColor( ws, LINE_HOVER ) : colours[axis];
        painter->save(); painter->setPen( QPen( color, axis == hovered ? 1.75 : 1.25 ) ); painter->setBrush( color );
        painter->drawPolyline( shape ); painter->drawPolygon( cap );
        painter->drawText( end + perpendicular * 8, QString( QChar( kAxisLetters[axis] ) ) ); painter->restore();
    }
    if ( painter != nullptr && !rotate ) {
        const QColor color = hovered == 6 ? SlotColor( ws, LINE_HOVER ) : QColor( 190, 139, 225 );
        const QRectF pivotRect( center - QPointF( 3, 3 ), QSizeF( 6, 6 ) );
        painter->save(); painter->setPen( QPen( color, 1.0 ) ); painter->setBrush( color );
        painter->drawRect( pivotRect ); painter->restore();
    }
    if ( painter != nullptr && hovered >= 0 ) {
        const QString operation = rotate ? QStringLiteral( "Rotate" ) : scale ? QStringLiteral( "Scale" ) : QStringLiteral( "Move" );
        const QString constraint = hovered < 3 ? QString( QChar( kAxisLetters[hovered] ) ) : hovered < 6 ?
            QString( QChar( kAxisLetters[( hovered - 3 + 1 ) % 3] ) ) + QChar( kAxisLetters[( hovered - 3 + 2 ) % 3] ) :
            scale ? QStringLiteral( "uniform" ) : QStringLiteral( "view plane" );
        const QString label = operation + QLatin1Char( ' ' ) + constraint;
        const QFontMetrics metrics = painter->fontMetrics();
        const QRectF viewport = painter->viewport();
        const qreal width = std::min<qreal>( metrics.horizontalAdvance( label ) + 12, viewport.width() - 16 );
        const qreal height = metrics.height() + 6;
        if ( width > 20 && height < viewport.height() - 16 ) {
            const QPointF at = dragging ? center : pick;
            const QRectF area( std::clamp( at.x() + 14, viewport.left() + 8, viewport.right() - width - 8 ),
                               std::clamp( at.y() + 14, viewport.top() + 8, viewport.bottom() - height - 8 ), width, height );
            painter->save(); painter->fillRect( area, backdrop ); painter->setPen( SlotColor( ws, LINE_HOVER ) );
            painter->drawText( area, Qt::AlignCenter, metrics.elidedText( label, Qt::ElideRight, static_cast<int>( width - 12 ) ) ); painter->restore();
        }
    }
    return best;
}

struct edit_handle_hit_t { int resize{ -1 }, gizmo{ -1 }; };
template <typename Project>
edit_handle_hit_t EditHandleAt( const map_workspace_t &ws, Project project, QRectF viewport, QPointF pick,
                              f64 length, int u = -1, int v = -1, int rotationAxis = -1, u32 allowedResizeHandles = 0xffu ) {
    // A precise visible target wins before any generous proximity fallback.
    // In 3D the foreground move controls win ties over the resize layer;
    // orthographic bounds squares retain their existing direct-hit priority.
    for ( bool precise : { true, false } ) {
        if ( u < 0 ) {
            const int gizmo = EditGizmo( nullptr, ws, project, length, pick, rotationAxis, nullptr, precise ? 3.0 : 10.0 );
            if ( gizmo >= 0 ) { return { -1, gizmo }; }
        }
        const int resize = ResizeHandles( nullptr, ws, project, viewport, pick, u, v, nullptr, precise ? 6.0 : 10.0, -2, allowedResizeHandles, length );
        if ( resize >= 0 ) { return { resize, -1 }; }
        if ( u >= 0 ) {
            const int gizmo = EditGizmo( nullptr, ws, project, length, pick, rotationAxis, nullptr, precise ? 3.0 : 10.0 );
            if ( gizmo >= 0 ) { return { -1, gizmo }; }
        }
    }
    return {};
}

Qt::CursorShape GizmoCursor( const map_workspace_t &ws ) {
    return TransformTool( ws ) == map_tool_t::ROTATE ? Qt::CrossCursor :
           TransformTool( ws ) == map_tool_t::SCALE ? Qt::SizeFDiagCursor : Qt::SizeAllCursor;
}

bool MeshFacePlanesPickable( const map_workspace_t &workspace, u64 meshId ) noexcept
{
    if ( workspace.pDocument == nullptr ) { return false; }
    const auto *source = geometry::GeometryDocument_FindMesh( &workspace.pDocument->geometry, { meshId } );
    if ( !geometry::MeshSource_IsInitialized( source ) || geometry::EditableMesh_FaceCount( &source->mesh ) == 0 ) { return false; }
    const auto &policy = workspace.pDocument->geometryPolicy.numerical;
    math::vec3d_t positions[MAP_MESH_FACE_VERTICES_MAX]{};
    bool valid = true;
    (void)GenerationPool_ForEach( &source->mesh.faces,
        [&]( geometry::geometry_mesh_face_handle_t, const geometry::mesh_face_record_t &face ) noexcept -> bool_t {
            const auto *loop = GenerationPool_Get( &source->mesh.loops, face.hOuterLoop );
            if ( loop == nullptr || loop->cHalfEdges < 3 || loop->cHalfEdges > MAP_MESH_FACE_VERTICES_MAX ) { valid = false; return false; }
            auto current = loop->hFirstHalfEdge;
            for ( u32 i = 0; i < loop->cHalfEdges; ++i ) {
                const auto *edge = GenerationPool_Get( &source->mesh.halfEdges, current );
                const auto *vertex = edge != nullptr ? GenerationPool_Get( &source->mesh.vertices, edge->hOrigin ) : nullptr;
                if ( vertex == nullptr || edge->hLoop.nSlot != face.hOuterLoop.nSlot || edge->hLoop.nGeneration != face.hOuterLoop.nGeneration ) {
                    valid = false; return false;
                }
                positions[i] = vertex->position;
                current = edge->hNext;
            }
            if ( current.nSlot != loop->hFirstHalfEdge.nSlot || current.nGeneration != loop->hFirstHalfEdge.nGeneration ) { valid = false; return false; }
            // Camera raycasting refuses the whole mesh if any face is not
            // planar. Use its exact basis and tolerance checks before 2D
            // relies on one plane to order a face at the cursor.
            math::polygon3d_basis_t basis{};
            valid = math::Vec3d_IsUnitLength( face.normal, policy.fUnitNormalTolerance ) &&
                math::Polygon3d_TryBasis( positions, loop->cHalfEdges, 2.0 * policy.fMinimumFaceArea, &basis ) &&
                math::Polygon3d_IsPlanar( positions, loop->cHalfEdges, basis, policy.fPlanarityTolerance ) &&
                Dot( basis.normal, face.normal ) > 0.0;
            return valid;
        } );
    return valid;
}

class map_ortho_view_t final : public QWidget {
public:
    map_ortho_view_t( QWidget *pParent, map_workspace_t *pWorkspace, map_ortho_axes_t axes )
        : QWidget( pParent ), m_pWorkspace( pWorkspace ), m_axes( axes )
    {
        CY_ASSERT( pWorkspace != nullptr );
        m_gizmoScale = GizmoScale( *pWorkspace );
        m_inputTool = pWorkspace->tool;
        m_inputMode = pWorkspace->elementMode;
        m_axisU = axes == map_ortho_axes_t::FRONT ? 1u : 0u;
        m_axisV = axes == map_ortho_axes_t::TOP ? 1u : 2u;
        setFocusPolicy( Qt::StrongFocus );
        setMouseTracking( true );
        setAttribute( Qt::WA_OpaquePaintEvent );
        setMinimumSize( 64, 64 );
        ( void )MapWorkspace_AddListener( pWorkspace, &map_ortho_view_t::OnChanged, this );
        ( void )EditorSettings_AddListener( &pWorkspace->pGui->settings, &map_ortho_view_t::OnSettingsChanged, this );
        ViewHover_Init( m_hover, *this, [this]( QPointF point ) {
            m_hoverEdge = {}; m_hoverVertex = {};
            if ( MeshVertexPickingActive( *m_pWorkspace ) ) {
                ( void )PickMeshVertex( point, &m_hoverVertex );
                m_hoverMeshFace = m_hoverBrushSide = 0u;
                return m_hoverVertex.object;
            }
            if ( MeshEdgePickingActive( *m_pWorkspace ) ) {
                ( void )PickMeshEdge( point, &m_hoverEdge );
                m_hoverMeshFace = m_hoverBrushSide = 0u;
                return m_hoverEdge.object;
            }
            const auto *face = FacePickingActive() ? PickFace( point ) : nullptr;
            m_hoverMeshFace = face != nullptr ? face->faceId : 0;
            m_hoverBrushSide = face != nullptr ? face->sideId : 0;
            return FacePickingActive() ? ( face != nullptr ? face->id : 0 ) : Pick( point );
        } );
        FrameBounds( pWorkspace->frameBounds );
    }

    ~map_ortho_view_t() override
    {
        EditorSettings_RemoveListener( &m_pWorkspace->pGui->settings, &map_ortho_view_t::OnSettingsChanged, this );
        MapWorkspace_RemoveListener( m_pWorkspace, &map_ortho_view_t::OnChanged, this );
        if ( m_drag.kind != edit_drag_t::NONE ) { CancelDrag( m_drag, *m_pWorkspace ); }
    }

    f64 Zoom() const noexcept { return m_zoom; }
    f64 GridStep() const noexcept { return DrawnGridStep(); }
    bool NavigationActive() const noexcept {
        return m_bPanning || m_bSpaceHeld || m_pWorkspace->tool == map_tool_t::NONE || m_pWorkspace->tool == map_tool_t::CAMERA;
    }
    bool CanNudgeSelection() const noexcept
    {
        return m_drag.kind == edit_drag_t::NONE && !NavigationActive() && !m_pWorkspace->editPreview.bActive &&
            !EditorHistory_IsTransactionOpen( &m_pWorkspace->history ) &&
            ( m_pWorkspace->elementMode == map_element_mode_t::OBJECTS || m_pWorkspace->elementMode == map_element_mode_t::GROUPS || m_pWorkspace->elementMode == map_element_mode_t::MESHES ) &&
            MapWorkspace_CanMoveSelection( m_pWorkspace );
    }

    bool NudgeSelection( int horizontal, int vertical, bool fine )
    {
        if ( !CanNudgeSelection() || horizontal < -1 || horizontal > 1 || vertical < -1 || vertical > 1 ||
             ( horizontal == 0 ) == ( vertical == 0 ) ) { return false; }
        const f64 step = fine ? 1.0 : static_cast<f64>( m_pWorkspace->gridSize );
        if ( !std::isfinite( step ) || step <= 0.0 ) { return false; }
        math::vec3d_t delta{};
        SetAxis( delta, m_axisU, horizontal * step );
        SetAxis( delta, m_axisV, vertical * step );
        return MapWorkspace_TranslateSelection( m_pWorkspace, delta );
    }
    u64 HoveredObject() const noexcept { return m_hover.id; }

    void ZoomBy( f64 notches, QPointF anchor )
    {
        if ( m_drag.kind != edit_drag_t::NONE ) { CancelDrag( m_drag, *m_pWorkspace ); }
        const QPointF before = ViewToWorld( anchor );
        m_zoom = std::clamp( m_zoom * std::pow( kZoomStep, notches ), MAP_VIEW_ZOOM_MIN, MAP_VIEW_ZOOM_MAX );
        m_center += before - ViewToWorld( anchor );
        update();
    }

    void CenterOrigin() { if ( m_drag.kind != edit_drag_t::NONE ) { CancelDrag( m_drag, *m_pWorkspace ); } m_center = QPointF(); update(); }

    // NetRadiant's background image: a plan or elevation to trace, drawn
    // under the grid across a world rectangle (u right, v up).
    void SetBackground( const QImage &image, const QRectF &world, f64 opacity )
    {
        m_background = image;
        m_backgroundWorld = world.normalized();
        m_backgroundOpacity = std::clamp( opacity, 0.05, 1.0 );
        update();
    }

    void ClearBackground()
    {
        m_background = QImage();
        update();
    }

    bool HasBackground() const noexcept { return !m_background.isNull(); }

    void FrameBounds( const map_bounds_t &bounds ) noexcept
    {
        if ( m_drag.kind != edit_drag_t::NONE ) { CancelDrag( m_drag, *m_pWorkspace ); }
        m_frameBounds = bounds;
        m_bFramePending = CY_TRUE;
        update();
    }

    QPointF WorldToView( QPointF world ) const noexcept
    {
        return QPointF( width() * 0.5 + ( world.x() - m_center.x() ) * m_zoom, height() * 0.5 - ( world.y() - m_center.y() ) * m_zoom );
    }

    QPointF ViewToWorld( QPointF view ) const noexcept
    {
        return QPointF( m_center.x() + ( view.x() - width() * 0.5 ) / m_zoom, m_center.y() - ( view.y() - height() * 0.5 ) / m_zoom );
    }

    bool FacePickingActive() const noexcept
    {
        return m_pWorkspace->elementMode == map_element_mode_t::FACES ||
            m_pWorkspace->tool == map_tool_t::EXTRUDE || m_pWorkspace->tool == map_tool_t::TEXTURE;
    }

    bool PickMeshEdge( QPointF point, map_mesh_edge_hit_t *out ) const
    {
        if ( out == nullptr ) { return false; }
        *out = {};
        if ( !MeshEdgePickingActive( *m_pWorkspace ) || m_pWorkspace->pDocument == nullptr || !rect().contains( point.toPoint() ) ) { return false; }
        f64 nearest = MAP_VIEW_PICK_PIXELS;
        f64 frontDepth = -std::numeric_limits<f64>::infinity();
        const int depthAxis = 3 - m_axisU - m_axisV;
        for ( usize i = 0u; i < m_pWorkspace->wire.objects.nCount; ++i ) {
            const auto &object = m_pWorkspace->wire.objects.pData[i];
            if ( object.kind != map_wire_kind_t::MESH || !object.bounds.bHas || !MapWorkspace_IsVisible( m_pWorkspace, object ) ) { continue; }
            const QPointF lo = WorldToView( { Axis( object.bounds.box.minimum, m_axisU ), Axis( object.bounds.box.minimum, m_axisV ) } );
            const QPointF hi = WorldToView( { Axis( object.bounds.box.maximum, m_axisU ), Axis( object.bounds.box.maximum, m_axisV ) } );
            if ( !QRectF( lo, hi ).normalized().adjusted( -MAP_VIEW_PICK_PIXELS, -MAP_VIEW_PICK_PIXELS,
                 MAP_VIEW_PICK_PIXELS, MAP_VIEW_PICK_PIXELS ).contains( point ) ) { continue; }
            const auto *source = geometry::GeometryDocument_FindMesh( &m_pWorkspace->pDocument->geometry, { object.id } );
            if ( !geometry::MeshSource_IsInitialized( source ) ) { continue; }
            VisitMeshEdges( *source, [&]( geometry::mesh_edge_ref_t edge, math::vec3d_t a, math::vec3d_t b ) {
                const QLineF line( WorldToView( { Axis( a, m_axisU ), Axis( a, m_axisV ) } ),
                    WorldToView( { Axis( b, m_axisU ), Axis( b, m_axisV ) } ) );
                const f64 distance = ScreenSegmentDistance( point, line );
                if ( distance > MAP_VIEW_PICK_PIXELS ) { return; }
                const QPointF delta = line.p2() - line.p1();
                const f64 length2 = QPointF::dotProduct( delta, delta );
                const f64 t = length2 > 0.0 ? std::clamp( QPointF::dotProduct( point - line.p1(), delta ) / length2, 0.0, 1.0 ) : 0.0;
                const f64 depth = length2 > 0.0 ? Axis( a, depthAxis ) + ( Axis( b, depthAxis ) - Axis( a, depthAxis ) ) * t :
                    std::max( Axis( a, depthAxis ), Axis( b, depthAxis ) );
                const map_mesh_edge_hit_t candidate{ object.id, edge };
                // Coincident orthographic wires are ordered from positive
                // omitted-axis depth, followed by stable authored identities.
                constexpr f64 screenTie = 1e-7;
                if ( out->object == 0u || distance < nearest - screenTie ||
                     ( std::abs( distance - nearest ) <= screenTie &&
                       ( depth > frontDepth || ( depth == frontDepth && MeshEdgeHitBefore( candidate, *out ) ) ) ) ) {
                    *out = candidate; nearest = distance; frontDepth = depth;
                }
            } );
        }
        return out->object != 0u;
    }

    bool PickMeshVertex( QPointF point, map_mesh_vertex_hit_t *out ) const
    {
        if ( out == nullptr ) { return false; }
        *out = {};
        if ( !MeshVertexPickingActive( *m_pWorkspace ) || m_pWorkspace->pDocument == nullptr ||
             !std::isfinite( point.x() ) || !std::isfinite( point.y() ) || !QRectF( rect() ).contains( point ) ) { return false; }
        f64 nearest = MAP_VIEW_PICK_PIXELS;
        f64 frontDepth = -std::numeric_limits<f64>::infinity();
        const int depthAxis = 3 - m_axisU - m_axisV;
        for ( usize i = 0u; i < m_pWorkspace->wire.objects.nCount; ++i ) {
            const auto &object = m_pWorkspace->wire.objects.pData[i];
            if ( object.kind != map_wire_kind_t::MESH || !object.bounds.bHas || !MapWorkspace_IsVisible( m_pWorkspace, object ) ) { continue; }
            const QPointF lo = WorldToView( { Axis( object.bounds.box.minimum, m_axisU ), Axis( object.bounds.box.minimum, m_axisV ) } );
            const QPointF hi = WorldToView( { Axis( object.bounds.box.maximum, m_axisU ), Axis( object.bounds.box.maximum, m_axisV ) } );
            if ( !QRectF( lo, hi ).normalized().adjusted( -MAP_VIEW_PICK_PIXELS, -MAP_VIEW_PICK_PIXELS,
                 MAP_VIEW_PICK_PIXELS, MAP_VIEW_PICK_PIXELS ).contains( point ) ) { continue; }
            const auto *source = geometry::GeometryDocument_FindMesh( &m_pWorkspace->pDocument->geometry, { object.id } );
            if ( !geometry::MeshSource_IsInitialized( source ) ) { continue; }
            VisitMeshVertices( *source, [&]( geometry::geometry_source_id_t vertex, math::vec3d_t position ) {
                const QPointF screen = WorldToView( { Axis( position, m_axisU ), Axis( position, m_axisV ) } );
                const f64 distance = QLineF( point, screen ).length();
                if ( distance > MAP_VIEW_PICK_PIXELS ) { return; }
                const f64 depth = Axis( position, depthAxis );
                const bool before = object.id < out->object || ( object.id == out->object && vertex.value < out->vertex.value );
                constexpr f64 screenTie = 1e-7;
                if ( out->object == 0u || distance < nearest - screenTie ||
                     ( std::abs( distance - nearest ) <= screenTie && ( depth > frontDepth || ( depth == frontDepth && before ) ) ) ) {
                    *out = { object.id, vertex }; nearest = distance; frontDepth = depth;
                }
            } );
        }
        return out->object != 0u;
    }

    u64 Pick( QPointF point ) const
    {
        if ( FacePickingActive() ) {
            const auto *face = PickFace( point );
            return face != nullptr ? face->id : 0;
        }
        u64 best = 0u;
        f64 bestArea = 0.0;
        for ( usize i = 0; i < m_pWorkspace->wire.objects.nCount; ++i ) {
            const map_wire_object_t &object = m_pWorkspace->wire.objects.pData[i];
            if ( !object.bounds.bHas || !ViewPickableRoot( *m_pWorkspace, object ) ) { continue; }
            const QPointF lo = WorldToView( { Axis( object.bounds.box.minimum, m_axisU ), Axis( object.bounds.box.minimum, m_axisV ) } );
            const QPointF hi = WorldToView( { Axis( object.bounds.box.maximum, m_axisU ), Axis( object.bounds.box.maximum, m_axisV ) } );
            const QRectF bounds = QRectF( lo, hi ).normalized();
            if ( !bounds.adjusted( -MAP_VIEW_PICK_PIXELS, -MAP_VIEW_PICK_PIXELS, MAP_VIEW_PICK_PIXELS, MAP_VIEW_PICK_PIXELS ).contains( point ) ) { continue; }
            bool hit = object.kind == map_wire_kind_t::ENTITY;
            for ( u32 edge = 0; edge < object.nLines && !hit; ++edge ) {
                const map_wire_line_t &line = m_pWorkspace->wire.lines.pData[object.iFirstLine + edge];
                const math::vec3d_t &a = m_pWorkspace->wire.points.pData[line.iA];
                const math::vec3d_t &b = m_pWorkspace->wire.points.pData[line.iB];
                hit = ScreenSegmentDistance( point, QLineF( WorldToView( { Axis( a, m_axisU ), Axis( a, m_axisV ) } ),
                    WorldToView( { Axis( b, m_axisU ), Axis( b, m_axisV ) } ) ) ) <= MAP_VIEW_PICK_PIXELS;
            }
            const f64 area = bounds.width() * bounds.height();
            if ( hit && ( best == 0u || area < bestArea || ( area == bestArea && object.id < best ) ) ) { best = object.id; bestArea = area; }
        }
        // Keep ordinary outline picks reachable inside selected rooms. Only
        // a selected whole object's actual projected faces supply a body
        // fallback; its bounds alone would hit empty space around a wedge.
        if ( best != 0u || !SelectMovesObjects( *m_pWorkspace ) ) { return best; }
        const auto &wire = m_pWorkspace->wire;
        for ( usize i = 0; i < wire.faces.nCount; ++i ) {
            const auto &face = wire.faces.pData[i];
            const auto *object = MapWireframe_FindObject( wire, face.id );
            if ( object == nullptr || !object->bounds.bHas || !ViewPickableRoot( *m_pWorkspace, *object ) || !ObjectSelected( *m_pWorkspace, *object ) ) { continue; }
            const QPointF lo = WorldToView( { Axis( object->bounds.box.minimum, m_axisU ), Axis( object->bounds.box.minimum, m_axisV ) } );
            const QPointF hi = WorldToView( { Axis( object->bounds.box.maximum, m_axisU ), Axis( object->bounds.box.maximum, m_axisV ) } );
            const QRectF bounds = QRectF( lo, hi ).normalized();
            if ( !bounds.contains( point ) || face.nIndices < 3u ) { continue; }
            QPolygonF polygon;
            polygon.reserve( face.nIndices );
            for ( u32 index = 0u; index < face.nIndices; ++index ) {
                const auto &vertex = wire.points.pData[wire.faceIndices.pData[face.iFirstIndex + index]];
                polygon << WorldToView( { Axis( vertex, m_axisU ), Axis( vertex, m_axisV ) } );
            }
            if ( !polygon.containsPoint( point, Qt::OddEvenFill ) ) { continue; }
            const f64 area = bounds.width() * bounds.height();
            if ( best == 0u || area < bestArea || ( area == bestArea && object->id < best ) ) { best = object->id; bestArea = area; }
        }
        return best;
    }

    const map_wire_face_t *PickFace( QPointF point ) const
    {
        if ( !rect().contains( point.toPoint() ) ) { return nullptr; }
        const auto &wire = m_pWorkspace->wire;
        const auto world = ViewToWorld( point );
        const int depthAxis = 3 - m_axisU - m_axisV;
        const map_wire_face_t *best = nullptr;
        f64 bestDepth = -std::numeric_limits<f64>::infinity();
        u64 checkedMesh = 0;
        bool meshPlanesPickable = false;
        for ( usize i = 0; i < wire.faces.nCount; ++i ) {
            const auto &face = wire.faces.pData[i];
            if ( ( face.sideId == 0 && face.faceId == 0 ) || face.nIndices < 3 || std::abs( Axis( face.normal, depthAxis ) ) < 1e-8 ) { continue; }
            const auto *object = MapWireframe_FindObject( wire, face.id );
            if ( object == nullptr || !MapWorkspace_IsVisible( m_pWorkspace, *object ) ) { continue; }
            if ( face.faceId != 0 ) {
                // Wire faces are packed in object build order. Check all
                // authored planes once per contiguous mesh, including faces
                // that do not project to a polygon in this particular pane.
                if ( checkedMesh != face.id ) { checkedMesh = face.id; meshPlanesPickable = MeshFacePlanesPickable( *m_pWorkspace, face.id ); }
                if ( !meshPlanesPickable ) { continue; }
            }
            QPolygonF polygon; polygon.reserve( face.nIndices );
            for ( u32 k = 0; k < face.nIndices; ++k ) {
                const auto vertex = wire.points.pData[wire.faceIndices.pData[face.iFirstIndex + k]];
                polygon << WorldToView( { Axis( vertex, m_axisU ), Axis( vertex, m_axisV ) } );
            }
            if ( !polygon.containsPoint( point, Qt::OddEvenFill ) ) { continue; }
            // Orthographic panes look from positive omitted-axis depth.
            // Solve the actual face plane at the cursor: a sloped polygon's
            // centroid or bounds would order overlaps incorrectly.
            const auto first = wire.points.pData[wire.faceIndices.pData[face.iFirstIndex]];
            const f64 depth = ( Dot( face.normal, first ) - Axis( face.normal, m_axisU ) * world.x() -
                Axis( face.normal, m_axisV ) * world.y() ) / Axis( face.normal, depthAxis );
            const u64 id = face.faceId != 0 ? face.faceId : face.sideId;
            const u64 bestId = best != nullptr ? ( best->faceId != 0 ? best->faceId : best->sideId ) : 0;
            if ( std::isfinite( depth ) && ( best == nullptr || depth > bestDepth ||
                 ( depth == bestDepth && ( face.id < best->id || ( face.id == best->id && id < bestId ) ) ) ) ) { best = &face; bestDepth = depth; }
        }
        return best;
    }

protected:
    bool event( QEvent *event ) override
    {
        if ( event->type() == QEvent::KeyPress ) {
            auto *key = static_cast<QKeyEvent *>( event );
            if ( key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab ) {
                // QWidget handles Tab traversal before keyPressEvent. Give
                // contextual bindings and tool gestures their normal priority;
                // an unmapped Tab can still use native focus traversal.
                keyPressEvent( key );
                if ( key->isAccepted() ) { return true; }
            }
        }
        if ( event->type() == QEvent::ShortcutOverride ) {
            const auto *key = static_cast<QKeyEvent *>( event );
            const auto gesture = MapInput_ToolGestureKey( m_pWorkspace, key, false );
            if ( gesture == map_tool_gesture_key_t::CANCEL ||
                 ( ( m_drag.kind != edit_drag_t::NONE || m_pWorkspace->editPreview.bClip || m_pWorkspace->editPreview.bStagedBlock ) && gesture != map_tool_gesture_key_t::NONE ) ) { event->accept(); return true; }
            if ( MapInput_DispatchKey( m_pWorkspace, const_cast<QKeyEvent *>( key ), false, NavigationActive(), false ) ) { event->accept(); return true; }
            if ( key->key() == Qt::Key_Space && key->modifiers() == Qt::NoModifier ) { event->accept(); return true; }
        }
        return QWidget::event( event );
    }

    void enterEvent( QEnterEvent *pEvent ) override
    {
        ActivateHoveredView( *this, *m_pWorkspace );
        QWidget::enterEvent( pEvent );
        update(); // The hover outline appears.
    }

    void leaveEvent( QEvent *pEvent ) override
    {
        QWidget::leaveEvent( pEvent );
        ViewHover_Clear( m_hover, *this );
        if ( m_drag.kind == edit_drag_t::NONE && !NavigationActive() ) { unsetCursor(); }
        update(); // ...and goes with the pointer.
    }

    void paintEvent( QPaintEvent * ) override
    {
        if ( m_bFramePending ) { ApplyFrame(); }
        QPainter painter( this );
        const map_workspace_t &workspace = *m_pWorkspace;
        painter.fillRect( rect(), EditorStyle_Color( workspace.pGui->style, gui::STYLE_COLOR_VIEWPORT_2D ) );
        painter.setFont( gui::EditorStyle_Font( workspace.pGui->style, "viewport.labels" ) );
        if ( !m_background.isNull() ) {
            // World v grows upwards, view y downwards: the top edge is the high v.
            const QRectF target( WorldToView( { m_backgroundWorld.left(), m_backgroundWorld.bottom() } ),
                                 WorldToView( { m_backgroundWorld.right(), m_backgroundWorld.top() } ) );
            painter.save();
            painter.setOpacity( m_backgroundOpacity );
            painter.setRenderHint( QPainter::SmoothPixmapTransform );
            painter.drawImage( target.normalized(), m_background );
            painter.restore();
        }
        if ( workspace.bGridVisible ) { DrawGrid( painter ); }
        DrawConnections( painter );
        DrawObjects( painter );
        DrawSelection( painter );
        DrawCordon( painter );
        if ( DisplayFlag( workspace, "editor.viewport.show_rulers" ) ) { DrawRulers( painter ); }
        if ( DisplayFlag( workspace, "editor.viewport.show_axes" ) ) { DrawAxisTriad( painter ); }
        if ( ShowMetrics( workspace ) ) { DrawInfo( painter ); }
        const auto project = [this]( math::vec3d_t p, QPointF &screen ) { screen = WorldToView( { Axis( p, m_axisU ), Axis( p, m_axisV ) } ); return true; };
        const auto segment = [&project]( math::vec3d_t a, math::vec3d_t b, QLineF &line ) {
            QPointF p, q; if ( !project( a, p ) || !project( b, q ) ) { return false; } line = QLineF( p, q ); return true;
        };
        const auto face = []( const map_wireframe_t &, const map_wire_face_t &, QPolygonF & ) { return false; };
        DrawClipGuide( painter, workspace, project, segment, rect() );
        ClipGuideHandles( &painter, workspace, project, segment, rect(),
            underMouse() && !NavigationActive() ? m_hover.point : QPointF( -10000, -10000 ), 3 - m_axisU - m_axisV );
        DrawMoveSource( painter, workspace, project, segment, rect(), false );
        DrawTransformPreview( painter, workspace, project, segment, face, rect(), false );
        DrawEditPreview( painter, workspace, project, segment, face, rect(), false );
        const QPointF pointer = underMouse() && !NavigationActive() ? m_hover.point : QPointF( -10000, -10000 );
        const f64 moveLength = 64.0 * GizmoScale( workspace ) / m_zoom;
        const auto hit = EditHandleAt( workspace, project, rect(), pointer, moveLength, m_axisU, m_axisV, 3 - m_axisU - m_axisV );
        EditGizmo( &painter, workspace, project, moveLength, pointer, 3 - m_axisU - m_axisV, &m_drag, 10.0, hit.gizmo );
        ResizeHandles( &painter, workspace, project, rect(), pointer, m_axisU, m_axisV, &m_drag, 10.0, hit.resize, 0xffu, moveLength );
        DrawMarquee( painter, m_drag, workspace );
        DrawEditReadout( painter, workspace, m_drag, rect(), false );
        DrawFocusBorder( painter, *this, workspace );
    }

    void wheelEvent( QWheelEvent *pEvent ) override
    {
        if ( AdjustBlockDepth( m_drag, *m_pWorkspace, *pEvent ) ||
             ( m_drag.kind == edit_drag_t::NONE && AdjustStagedBlockDepth( *m_pWorkspace, *pEvent ) ) ) { update(); pEvent->accept(); return; }
        const settings_registry_t &settings = m_pWorkspace->pGui->settings;
        f64 notches = pEvent->angleDelta().y() / 120.0;
        if ( notches == 0.0 ) { return; }
        notches *= EditorSettings_Real( &settings, "editor.camera.zoom_sensitivity", 1.0 );
        if ( EditorSettings_Bool( &settings, "editor.camera.invert_wheel", CY_FALSE ) ) { notches = -notches; }
        const QPointF anchor = EditorSettings_Bool( &settings, "editor.camera.zoom_to_cursor", CY_TRUE )
            ? pEvent->position() : QPointF( width() * 0.5, height() * 0.5 );
        ZoomBy( notches, anchor );
        pEvent->accept();
    }

    void mousePressEvent( QMouseEvent *pEvent ) override
    {
        setFocus( Qt::MouseFocusReason );
        const bool bPan = pEvent->button() == Qt::MiddleButton || ( pEvent->button() == Qt::LeftButton &&
            ( m_bSpaceHeld || m_pWorkspace->tool == map_tool_t::NONE || m_pWorkspace->tool == map_tool_t::CAMERA ) );
        if ( bPan ) {
            if ( m_drag.kind != edit_drag_t::NONE ) { CancelDrag( m_drag, *m_pWorkspace ); }
            m_bPanning = CY_TRUE;
            m_panStart = pEvent->position();
            m_panCenterStart = m_center;
            m_panSensitivity = EditorSettings_Real( &m_pWorkspace->pGui->settings, "editor.camera.pan_sensitivity", 1.0 );
            setCursor( Qt::ClosedHandCursor );
            return;
        }
        if ( pEvent->button() == Qt::LeftButton ) { BeginEdit( pEvent->position(), pEvent->modifiers() ); }
    }

    void mouseMoveEvent( QMouseEvent *pEvent ) override
    {
        if ( pEvent->buttons() == Qt::NoButton ) { ActivateHoveredView( *this, *m_pWorkspace ); }
        ViewHover_Move( m_hover, *this, *m_pWorkspace, *pEvent, NavigationActive() );
        if ( m_drag.kind == edit_drag_t::NONE && !NavigationActive() ) {
            const auto project = [this]( math::vec3d_t p, QPointF &screen ) { screen = WorldToView( { Axis( p, m_axisU ), Axis( p, m_axisV ) } ); return true; };
            const auto hit = EditHandleAt( *m_pWorkspace, project, rect(), pEvent->position(), 64.0 * GizmoScale( *m_pWorkspace ) / m_zoom, m_axisU, m_axisV, 3 - m_axisU - m_axisV );
            if ( hit.resize >= 0 ) { setCursor( ResizeCursor( hit.resize ) ); }
            else if ( hit.gizmo >= 0 ) { setCursor( GizmoCursor( *m_pWorkspace ) ); } else { unsetCursor(); }
        }
        if ( TransformDrag( TransformTool( *m_pWorkspace ) ) != edit_drag_t::NONE || m_pWorkspace->tool == map_tool_t::CLIP || MapWorkspace_HasBlockPreview( m_pWorkspace ) ) { update(); }
        if ( m_bPanning ) {
            const QPointF delta = ( pEvent->position() - m_panStart ) * m_panSensitivity;
            m_center = QPointF( m_panCenterStart.x() - delta.x() / m_zoom, m_panCenterStart.y() + delta.y() / m_zoom );
            update();
        }
        if ( m_drag.kind != edit_drag_t::NONE ) { UpdateEdit( pEvent->position(), pEvent->modifiers() ); }
        const QPointF world = ViewToWorld( pEvent->position() );
        math::vec3d_t cursor{};
        f64 *pComponents[3]{ &cursor.x, &cursor.y, &cursor.z };
        *pComponents[m_axisU] = world.x();
        *pComponents[m_axisV] = world.y();
        MapWorkspace_SetCursor( m_pWorkspace, cursor, ( 1u << m_axisU ) | ( 1u << m_axisV ) );
    }

    void mouseReleaseEvent( QMouseEvent *pEvent ) override
    {
        if ( pEvent->button() == Qt::LeftButton && m_drag.kind != edit_drag_t::NONE ) {
            UpdateEdit( pEvent->position(), pEvent->modifiers() );
            if ( m_drag.kind == edit_drag_t::MARQUEE && QLineF( m_drag.start, m_drag.current ).length() >= 3 ) {
                SelectMarquee( *m_pWorkspace, m_drag, [this]( math::vec3d_t p, QPointF &screen ) { screen = WorldToView( { Axis( p, m_axisU ), Axis( p, m_axisV ) } ); return true; } );
            }
            if ( m_drag.kind == edit_drag_t::CLIP ) { m_drag = {}; }
            else if ( m_drag.kind == edit_drag_t::BOX || m_drag.blockResize ) { ( void )StageBlockDrag( m_drag, *m_pWorkspace ); }
            else { CommitDrag( m_drag, *m_pWorkspace ); }
            if ( !NavigationActive() ) { unsetCursor(); }
            update();
        }
        if ( m_bPanning && ( pEvent->button() == Qt::MiddleButton || pEvent->button() == Qt::LeftButton ) ) {
            m_bPanning = CY_FALSE;
            unsetCursor();
        }
    }

    void keyPressEvent( QKeyEvent *pEvent ) override
    {
        const bool cancel = MapInput_ToolGestureKey( m_pWorkspace, pEvent, false ) == map_tool_gesture_key_t::CANCEL;
        // A held key must not cancel a drag, leave its tool and clear the
        // selection in one press. Each cancellation layer needs a fresh press.
        if ( cancel && pEvent->isAutoRepeat() ) { pEvent->accept(); return; }
        if ( cancel && ( m_bPanning || m_bSpaceHeld ) ) {
            m_bPanning = m_bSpaceHeld = CY_FALSE;
            unsetCursor(); update(); pEvent->accept(); return;
        }
        if ( m_drag.kind == edit_drag_t::NONE && m_pWorkspace->editPreview.bActive &&
             ( m_pWorkspace->editPreview.bClip || m_pWorkspace->editPreview.bStagedBlock ) ) {
            const auto gesture = MapInput_ToolGestureKey( m_pWorkspace, pEvent, false );
            if ( gesture != map_tool_gesture_key_t::NONE ) {
                if ( gesture == map_tool_gesture_key_t::CANCEL ) { MapWorkspace_ClearEditPreview( m_pWorkspace ); }
                else if ( m_pWorkspace->editPreview.bStagedBlock ) { ( void )MapWorkspace_CommitBlockPreview( m_pWorkspace ); }
                else { ( void )MapWorkspace_CommitClipPreview( m_pWorkspace ); }
                update(); pEvent->accept(); return;
            }
        }
        if ( m_drag.kind != edit_drag_t::NONE ) {
            const auto gesture = MapInput_ToolGestureKey( m_pWorkspace, pEvent, false );
            if ( gesture != map_tool_gesture_key_t::NONE ) {
                if ( gesture == map_tool_gesture_key_t::CANCEL ) { CancelDrag( m_drag, *m_pWorkspace ); if ( !NavigationActive() ) { unsetCursor(); } }
                else if ( m_drag.kind == edit_drag_t::MARQUEE ) {
                    if ( DragContextMatches( m_drag, *m_pWorkspace ) ) {
                        SelectMarquee( *m_pWorkspace, m_drag, [this]( math::vec3d_t p, QPointF &screen ) { screen = WorldToView( { Axis( p, m_axisU ), Axis( p, m_axisV ) } ); return true; } );
                    }
                    CancelDrag( m_drag, *m_pWorkspace );
                } else { CommitDrag( m_drag, *m_pWorkspace ); }
                update(); pEvent->accept(); return;
            }
        }
        if ( CancelIdleTool( *m_pWorkspace, pEvent, false ) ) { if ( !NavigationActive() ) { unsetCursor(); } update(); pEvent->accept(); return; }
        if ( MapInput_DispatchKey( m_pWorkspace, pEvent, false, NavigationActive(), true ) ) { pEvent->accept(); return; }
        if ( pEvent->key() == Qt::Key_Space && !pEvent->isAutoRepeat() ) {
            if ( m_drag.kind != edit_drag_t::NONE ) { CancelDrag( m_drag, *m_pWorkspace ); }
            m_bSpaceHeld = CY_TRUE;
            setCursor( Qt::OpenHandCursor );
            return;
        }
        QWidget::keyPressEvent( pEvent );
    }

    void keyReleaseEvent( QKeyEvent *pEvent ) override
    {
        if ( pEvent->key() == Qt::Key_Space && !pEvent->isAutoRepeat() ) {
            m_bSpaceHeld = CY_FALSE;
            if ( !m_bPanning ) { unsetCursor(); }
            return;
        }
        QWidget::keyReleaseEvent( pEvent );
    }

    void focusInEvent( QFocusEvent * ) override { update(); }
    void focusOutEvent( QFocusEvent * ) override
    {
        CancelDrag( m_drag, *m_pWorkspace );
        m_bSpaceHeld = CY_FALSE;
        m_bPanning = CY_FALSE;
        unsetCursor();
        update();
    }

    void resizeEvent( QResizeEvent * ) override
    {
        if ( m_drag.kind != edit_drag_t::NONE ) { CancelDrag( m_drag, *m_pWorkspace ); }
        if ( m_bFramePending ) { ApplyFrame(); }
    }

private:
    static void OnSettingsChanged( void *pContext, string_view_t path ) noexcept
    {
        auto *pView = static_cast<map_ortho_view_t *>( pContext );
        if ( path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( "editor.viewport.gizmo_scale" ) ) ) {
            const f64 scale = GizmoScale( *pView->m_pWorkspace );
            const bool changed = scale != pView->m_gizmoScale;
            pView->m_gizmoScale = scale;
            // A scope replacement also notifies this listener. Only an
            // effective scale change invalidates the captured pickup.
            if ( changed ) {
                if ( pView->m_drag.kind != edit_drag_t::NONE ) { CancelDrag( pView->m_drag, *pView->m_pWorkspace ); }
                if ( !pView->NavigationActive() ) { pView->unsetCursor(); }
            }
        }
        OnViewDisplaySettingChanged( pContext, path );
    }

    static void OnChanged( void *pContext, u32 changes ) noexcept
    {
        auto *pView = static_cast<map_ortho_view_t *>( pContext );
        if ( pView->m_inputTool != pView->m_pWorkspace->tool ) {
            pView->m_inputTool = pView->m_pWorkspace->tool;
            pView->m_bPanning = pView->m_bSpaceHeld = CY_FALSE;
            ViewHover_Clear( pView->m_hover, *pView ); pView->unsetCursor();
        }
        if ( pView->m_inputMode != pView->m_pWorkspace->elementMode ) {
            pView->m_inputMode = pView->m_pWorkspace->elementMode;
            ViewHover_Clear( pView->m_hover, *pView ); pView->unsetCursor();
        }
        if ( ( changes & ( MAP_CHANGE_DOCUMENT | MAP_CHANGE_VIEW ) ) != 0u ) {
            pView->m_hoverEdge = {}; pView->m_hoverVertex = {};
            ViewHover_Clear( pView->m_hover, *pView );
        }
        if ( pView->m_drag.kind != edit_drag_t::NONE && ( ( changes & MAP_CHANGE_DOCUMENT ) != 0 || !DragContextMatches( pView->m_drag, *pView->m_pWorkspace ) ) ) {
            CancelDrag( pView->m_drag, *pView->m_pWorkspace );
            if ( !pView->NavigationActive() ) { pView->unsetCursor(); }
        }
        if ( ( changes & MAP_CHANGE_FRAME ) != 0u && pView->m_pWorkspace->frameTarget != map_frame_target_t::PERSPECTIVE ) {
            pView->FrameBounds( pView->m_pWorkspace->frameBounds );
            pView->ApplyFrame();
        }
        if ( ( changes & ( MAP_CHANGE_DOCUMENT | MAP_CHANGE_SELECTION | MAP_CHANGE_VIEW | MAP_CHANGE_FRAME ) ) != 0u ) { pView->update(); }
    }

    math::vec3d_t EditPoint( QPointF screen ) const {
        const QPointF p = ViewToWorld( screen ); math::vec3d_t world{};
        SetAxis( world, m_axisU, p.x() ); SetAxis( world, m_axisV, p.y() ); return world;
    }
    void BeginEdit( QPointF screen, Qt::KeyboardModifiers modifiers ) {
        if ( m_pWorkspace->tool == map_tool_t::NONE || m_pWorkspace->tool == map_tool_t::CAMERA ) { return; }
        if ( MeshEdgePickingActive( *m_pWorkspace ) ) {
            map_mesh_edge_hit_t hit{};
            ( void )PickMeshEdge( screen, &hit );
            SelectViewMeshEdge( *m_pWorkspace, hit, modifiers );
            return;
        }
        if ( MeshVertexPickingActive( *m_pWorkspace ) ) {
            map_mesh_vertex_hit_t hit{};
            ( void )PickMeshVertex( screen, &hit );
            SelectViewMeshVertex( *m_pWorkspace, hit, modifiers );
            return;
        }
        const auto project = [this]( math::vec3d_t p, QPointF &out ) { out = WorldToView( { Axis( p, m_axisU ), Axis( p, m_axisV ) } ); return true; };
        const auto segment = [&project]( math::vec3d_t a, math::vec3d_t b, QLineF &line ) {
            QPointF p, q; project( a, p ); project( b, q ); line = QLineF( p, q ); return true;
        };
        const int endpoint = MapWorkspace_CanEditBrushSelection( m_pWorkspace ) ?
            ClipGuideHandles( nullptr, *m_pWorkspace, project, segment, rect(), screen, 3 - m_axisU - m_axisV ) : -1;
        const auto guide = m_pWorkspace->editPreview.clipGuide;
        CancelDrag( m_drag, *m_pWorkspace );
        if ( endpoint >= 0 ) {
            m_drag.kind = edit_drag_t::CLIP; m_drag.start = m_drag.current = screen; m_drag.modifiers = modifiers;
            m_drag.clipEndpoint = endpoint; m_drag.clipGuide = guide; m_drag.depthAxis = static_cast<int>( guide.extrusionAxis );
            m_drag.pivot = guide.points[0]; m_drag.planeStart = EditPoint( screen );
            // Match the captured construction depth before calculating deltas.
            SetAxis( m_drag.planeStart, m_drag.depthAxis, Axis( m_drag.pivot, m_drag.depthAxis ) );
            CaptureDragContext( m_drag, *m_pWorkspace );
            MapWorkspace_SetClipGuide( m_pWorkspace, guide ); return;
        }
        const bool stagedBlock = MapWorkspace_HasBlockPreview( m_pWorkspace );
        if ( !stagedBlock ) { MapWorkspace_ClearEditPreview( m_pWorkspace ); }
        const u64 id = Pick( screen );
        m_drag.start = m_drag.current = screen; m_drag.modifiers = modifiers;
        if ( !stagedBlock && ( ( m_pWorkspace->tool == map_tool_t::SELECT && m_pWorkspace->elementMode == map_element_mode_t::FACES ) ||
             m_pWorkspace->tool == map_tool_t::EXTRUDE || m_pWorkspace->tool == map_tool_t::TEXTURE ) ) {
            const auto *face = PickFace( screen );
            if ( face != nullptr && face->faceId != 0 ) {
                if ( BypassSnap( modifiers ) && face->id == m_pWorkspace->selectedMeshFaceObject && face->faceId == m_pWorkspace->selectedMeshFaceId ) { MapWorkspace_ClearMeshFace( m_pWorkspace ); }
                else { MapWorkspace_SelectMeshFace( m_pWorkspace, face->id, face->faceId ); }
            } else if ( face != nullptr && face->sideId != 0 ) { MapWorkspace_SelectBrushFace( m_pWorkspace, face->id, face->sideId ); }
            else if ( !BypassSnap( modifiers ) ) { MapWorkspace_Select( m_pWorkspace, 0, MAP_SELECT_REPLACE ); }
            return;
        }
        const auto hit = EditHandleAt( *m_pWorkspace, project, rect(), screen, 64.0 * GizmoScale( *m_pWorkspace ) / m_zoom, m_axisU, m_axisV, 3 - m_axisU - m_axisV );
        const int resize = hit.resize;
        if ( resize >= 0 && BeginResize( m_drag, *m_pWorkspace, ResizeSides( resize, m_axisU, m_axisV ) ) ) {
            QPointF boundary;
            project( ResizePoint( m_drag.original, m_drag.resizeSides ), boundary );
            m_drag.resizeScreenOffset = screen - boundary;
            m_drag.planeStart = EditPoint( screen ); setCursor( ResizeCursor( resize ) ); return;
        }
        if ( stagedBlock ) { return; }
        const int handle = hit.gizmo;
        if ( m_pWorkspace->tool == map_tool_t::SELECT ) {
            if ( handle < 0 ) {
                const bool bodyMove = SelectMovesObjects( *m_pWorkspace ) && id != 0 && modifiers == Qt::NoModifier;
                if ( !bodyMove || !HitSelected( *m_pWorkspace, id ) ) { SelectViewObject( *m_pWorkspace, id, modifiers ); }
                if ( id == 0 ) { m_drag.kind = edit_drag_t::MARQUEE; CaptureDragContext( m_drag, *m_pWorkspace ); }
                if ( !bodyMove ) { return; }
                m_drag.bodyMove = true;
            }
        }
        if ( m_pWorkspace->tool == map_tool_t::BLOCK ) {
            m_drag.kind = edit_drag_t::BOX; m_drag.depthAxis = 3 - m_axisU - m_axisV;
            m_drag.depth = EditorSettings_Real( &m_pWorkspace->pGui->settings, "editor.map.block_depth", 64 );
            CaptureDragContext( m_drag, *m_pWorkspace ); return;
        }
        if ( m_pWorkspace->tool == map_tool_t::CLIP ) {
            if ( MapWorkspace_CanEditBrushSelection( m_pWorkspace ) ) {
                m_drag.kind = edit_drag_t::CLIP; m_drag.depthAxis = 3 - m_axisU - m_axisV;
                m_drag.pivot = MapBounds_Center( SelectionGeometryBounds( *m_pWorkspace ) );
                m_drag.planeStart = EditPoint( screen );
                CaptureDragContext( m_drag, *m_pWorkspace );
            } return;
        }
        if ( handle < 0 && id == 0 ) { return; }
        if ( handle < 0 && id != 0 && !HitSelected( *m_pWorkspace, id ) ) { SelectViewObject( *m_pWorkspace, id, Qt::NoModifier ); }
        if ( !CanTransform( *m_pWorkspace ) ) { return; }
        m_drag.kind = TransformDrag( TransformTool( *m_pWorkspace ) );
        m_drag.axis = handle >= 0 && handle < 3 ? handle : -1;
        m_drag.planeAxis = handle >= 3 && handle < 6 ? handle - 3 : -1;
        m_drag.uniformScale = handle == 6 && m_drag.kind == edit_drag_t::SCALE;
        m_drag.original = EditSelectionBounds( *m_pWorkspace ); m_drag.pivot = MapBounds_Center( m_drag.original );
        m_drag.pivotScreen = WorldToView( { Axis( m_drag.pivot, m_axisU ), Axis( m_drag.pivot, m_axisV ) } );
        m_drag.planeStart = EditPoint( screen );
        CaptureDragContext( m_drag, *m_pWorkspace );
    }
    void UpdateEdit( QPointF screen, Qt::KeyboardModifiers modifiers ) {
        if ( m_drag.kind == edit_drag_t::NONE ) { return; }
        if ( !DragContextMatches( m_drag, *m_pWorkspace ) ) { CancelDrag( m_drag, *m_pWorkspace ); update(); return; }
        const bool bypass = BypassSnap( modifiers );
        m_drag.current = screen;
        if ( m_drag.kind == edit_drag_t::MARQUEE ) { update(); return; }
        if ( AwaitTransformMotion( m_drag, *m_pWorkspace ) ) { update(); return; }
        if ( m_drag.kind == edit_drag_t::RESIZE ) {
            UpdateResize( m_drag, *m_pWorkspace, Sub( EditPoint( screen ), m_drag.planeStart ), bypass ); return;
        }
        if ( m_drag.kind == edit_drag_t::BOX ) {
            const f64 depth = MapWorkspace_PrimitiveDefaults( m_pWorkspace, {} ).kind == map_primitive_kind_t::QUAD ? 0.0 : m_drag.depth;
            MapWorkspace_SetEditPreview( m_pWorkspace, BoxFootprint( EditPoint( m_drag.start ), EditPoint( screen ), m_axisU, m_axisV, *m_pWorkspace, bypass, depth ), static_cast<u32>( m_drag.depthAxis ) ); return;
        }
        if ( m_drag.kind == edit_drag_t::CLIP ) {
            UpdateClipGuide( m_drag, *m_pWorkspace, EditPoint( screen ), bypass ); return;
        }
        math::vec3d_t delta = SnapMove( Sub( EditPoint( screen ), m_drag.planeStart ), *m_pWorkspace, bypass );
        if ( m_drag.axis >= 0 ) { delta = Scale( AxisVector( m_drag.axis ), Axis( delta, m_drag.axis ) ); }
        if ( m_drag.planeAxis >= 0 ) { SetAxis( delta, m_drag.planeAxis, 0 ); }
        if ( m_drag.kind == edit_drag_t::TRANSLATE ) { m_drag.delta = delta; }
        if ( m_drag.kind == edit_drag_t::SCALE ) {
            if ( m_drag.uniformScale ) {
                const QPointF pixels = screen - m_drag.start;
                const f64 f = std::max( 0.01, Snap( 1 + ( pixels.x() - pixels.y() ) / 100.0, bypass ? 0 : m_pWorkspace->scaleSnap ) );
                m_drag.factors = { f, f, f };
            } else for ( int axis : { static_cast<int>( m_axisU ), static_cast<int>( m_axisV ) } ) {
                if ( m_drag.axis >= 0 && axis != m_drag.axis ) { continue; }
                if ( axis == m_drag.planeAxis ) { continue; }
                f64 f = 1 + Axis( delta, axis ) / std::max( 1.0, ( Axis( m_drag.original.box.maximum, axis ) - Axis( m_drag.original.box.minimum, axis ) ) * 0.5 );
                f = std::max( 0.01, Snap( f, bypass ? 0 : m_pWorkspace->scaleSnap ) ); SetAxis( m_drag.factors, axis, f );
            }
        }
        if ( m_drag.kind == edit_drag_t::ROTATE ) {
            const QPointF before = m_drag.start - m_drag.pivotScreen, after = screen - m_drag.pivotScreen;
            const f64 angle = std::atan2( QPointF::dotProduct( QPointF( before.y(), -before.x() ), after ), QPointF::dotProduct( before, after ) ) * 180 / 3.14159265358979323846;
            const int axis = 3 - m_axisU - m_axisV;
            SetAxis( m_drag.degrees, axis, Snap( angle, bypass ? 0 : m_pWorkspace->angleSnap ) * ( m_axisU == 0 && m_axisV == 2 ? -1 : 1 ) );
        }
        MapWorkspace_SetTransformPreview( m_pWorkspace, TransformDescriptor( m_drag ) );
    }

    // The grid step actually drawn: the set size, doubled until lines are
    // far enough apart to read.
    f64 DrawnGridStep() const noexcept
    {
        f64 step = m_pWorkspace->gridSize;
        const settings_registry_t &settings = m_pWorkspace->pGui->settings;
        if ( EditorSettings_Bool( &settings, "editor.grid.adaptive", CY_TRUE ) ) {
            const f64 spacing = static_cast<f64>( EditorSettings_Integer( &settings, "editor.grid.min_spacing_px", MAP_VIEW_GRID_MIN_PIXELS ) );
            while ( step * m_zoom < spacing ) { step *= 2.0; }
        }
        // Literal non-adaptive grids remain literal until their line count
        // would exceed 4096 per axis. This safety bound prevents millions of
        // subpixel lines at the smallest zoom without changing snap spacing.
        constexpr f64 kMaxLinesPerAxis = 4096.0;
        while ( std::max( width(), height() ) / ( step * m_zoom ) + 2.0 > kMaxLinesPerAxis ) { step *= 2.0; }
        return step;
    }

    // Coordinates along the top and left edges, TileEditor style: plain
    // numbers at a spacing that never crowds, with a short tick.
    void DrawRulers( QPainter &painter ) const
    {
        const map_workspace_t &workspace = *m_pWorkspace;
        const QFontMetrics metrics = painter.fontMetrics();
        const QPointF topLeft = ViewToWorld( QPointF( 0.0, 0.0 ) );
        const QPointF bottomRight = ViewToWorld( QPointF( width(), height() ) );
        const f64 drawnStep = DrawnGridStep();
        f64 step = drawnStep * static_cast<f64>( DrawnMajorInterval( workspace, drawnStep ) );
        const int longestLabel = std::max( metrics.horizontalAdvance( NumberText( topLeft.x() ) ),
                                           metrics.horizontalAdvance( NumberText( bottomRight.x() ) ) );
        while ( step * m_zoom < std::max( 48, longestLabel + 12 ) ) { step *= 2.0; }
        const QRectF header = ViewHeaderRect( *this );
        const QRectF triad = DisplayFlag( workspace, "editor.viewport.show_axes" )
            ? AxisTriadRect( *this ) : QRectF();
        painter.setPen( Token( workspace, "ui.text.muted" ) );
        for ( f64 u = std::ceil( topLeft.x() / step ) * step; u <= bottomRight.x(); u += step ) {
            const f64 x = WorldToView( QPointF( u, 0.0 ) ).x();
            const QString text = NumberText( u );
            const QRectF label( x + 3.0, 3.0, metrics.horizontalAdvance( text ), metrics.height() );
            if ( label.right() > width() || label.intersects( header ) || label.intersects( triad ) ) { continue; }
            painter.drawLine( QLineF( x, 0.0, x, 4.0 ) );
            painter.drawText( QPointF( x + 3.0, metrics.ascent() + 3.0 ), text );
        }
        // The left ruler keeps clear of the pane header (top) and the info
        // text (bottom), which sit on the same edge.
        const f64 top = metrics.height() * 2.0;
        const f64 bottom = height() - metrics.height() * ( ShowMetrics( workspace ) ? 3.0 : 1.0 );
        for ( f64 v = std::ceil( bottomRight.y() / step ) * step; v <= topLeft.y(); v += step ) {
            const f64 y = WorldToView( QPointF( 0.0, v ) ).y();
            if ( y < top || y > bottom ) { continue; }
            const QString text = NumberText( v );
            const QRectF label( 6.0, y - 3.0 - metrics.ascent(), metrics.horizontalAdvance( text ), metrics.height() );
            if ( label.intersects( header ) || label.intersects( triad ) ) { continue; }
            painter.drawLine( QLineF( 0.0, y, 4.0, y ) );
            painter.drawText( QPointF( 6.0, y - 3.0 ), text );
        }
    }

    // The two axes this view shows, in the top-right corner.
    void DrawAxisTriad( QPainter &painter ) const
    {
        const gui::editor_style_t &style = m_pWorkspace->pGui->style;
        const gui::editor_style_color_t axisColors[3]{ gui::STYLE_COLOR_AXIS_X, gui::STYLE_COLOR_AXIS_Y, gui::STYLE_COLOR_AXIS_Z };
        const QPointF origin = AxisTriadRect( *this ).center();
        painter.setRenderHint( QPainter::Antialiasing, true );
        DrawAxisArrow( painter, origin, origin + QPointF( 25.0, 0.0 ), EditorStyle_Color( style, axisColors[m_axisU] ), kAxisLetters[m_axisU] );
        DrawAxisArrow( painter, origin, origin + QPointF( 0.0, -25.0 ), EditorStyle_Color( style, axisColors[m_axisV] ), kAxisLetters[m_axisV] );
        const u32 normalAxis = 3u - m_axisU - m_axisV;
        DrawAxisArrow( painter, origin, origin, EditorStyle_Color( style, axisColors[normalAxis] ), kAxisLetters[normalAxis] );
        painter.setBrush( Qt::NoBrush );
        painter.setRenderHint( QPainter::Antialiasing, false );
    }

    // Bottom-left: plane, scale, grid, and what is in the map.
    void DrawInfo( QPainter &painter ) const
    {
        const map_workspace_t &workspace = *m_pWorkspace;
        const f64 step = DrawnGridStep();
        QString first = QStringLiteral( "%1%2 \u00B7 %3 px/u \u00B7 Grid %4 u" )
                            .arg( QLatin1Char( kAxisLetters[m_axisU] ) )
                            .arg( QLatin1Char( kAxisLetters[m_axisV] ) )
                            .arg( m_zoom, 0, 'g', 3 )
                            .arg( NumberText( workspace.gridSize ) );
        if ( step != workspace.gridSize ) { first += QStringLiteral( " (drawn %1)" ).arg( NumberText( step ) ); }
        const QFontMetrics metrics = painter.fontMetrics();
        const f64 line = metrics.height();
        painter.setPen( Token( workspace, "viewport.overlay.text" ) );
        painter.drawText( QPointF( 6.0, height() - 6.0 - line ), first );
        painter.drawText( QPointF( 6.0, height() - 6.0 ), CountText( workspace ) );
    }

    // Framing waits until the view has a size: a view created inside a
    // hidden dock is 0 x 0 when the first frame request arrives.
    void ApplyFrame() noexcept
    {
        if ( width() <= 1 || height() <= 1 ) { return; }
        const map_bounds_t &bounds = m_frameBounds;
        m_bFramePending = CY_FALSE;
        if ( !bounds.bHas ) { return; }
        const f64 minU = Axis( bounds.box.minimum, m_axisU );
        const f64 maxU = Axis( bounds.box.maximum, m_axisU );
        const f64 minV = Axis( bounds.box.minimum, m_axisV );
        const f64 maxV = Axis( bounds.box.maximum, m_axisV );
        const f64 margin = FrameMargin( *m_pWorkspace );
        const f64 extentU = std::max( maxU - minU, 16.0 ) * margin;
        const f64 extentV = std::max( maxV - minV, 16.0 ) * margin;
        m_zoom = std::clamp( std::min( width() / extentU, height() / extentV ), MAP_VIEW_ZOOM_MIN, MAP_VIEW_ZOOM_MAX );
        m_center = QPointF( ( minU + maxU ) * 0.5, ( minV + maxV ) * 0.5 );
        update();
    }

    void DrawGrid( QPainter &painter ) const
    {
        const map_workspace_t &workspace = *m_pWorkspace;
        const gui::editor_style_t &style = workspace.pGui->style;
        const f64 step = DrawnGridStep();
        const i64 majorInterval = DrawnMajorInterval( workspace, step );
        const QPointF topLeft = ViewToWorld( QPointF( 0.0, 0.0 ) );
        const QPointF bottomRight = ViewToWorld( QPointF( width(), height() ) );
        QVector<QLineF> minor;
        QVector<QLineF> major;
        const auto batch = [&]( f64 coordinate ) -> QVector<QLineF> & {
            return GridLineMultiple( coordinate, step, majorInterval ) ? major : minor;
        };
        for ( f64 u = std::floor( topLeft.x() / step ) * step; u <= bottomRight.x(); u += step ) {
            const f64 x = WorldToView( QPointF( u, 0.0 ) ).x();
            batch( u ).append( QLineF( x, 0.0, x, height() ) );
        }
        for ( f64 v = std::floor( bottomRight.y() / step ) * step; v <= topLeft.y(); v += step ) {
            const f64 y = WorldToView( QPointF( 0.0, v ) ).y();
            batch( v ).append( QLineF( 0.0, y, width(), y ) );
        }
        painter.setPen( QPen( EditorStyle_Color( style, gui::STYLE_COLOR_GRID_MINOR ), 0.0 ) );
        painter.drawLines( minor );
        painter.setPen( QPen( EditorStyle_Color( style, gui::STYLE_COLOR_GRID_MAJOR ), 0.0 ) );
        painter.drawLines( major );
        if ( !DisplayFlag( workspace, "editor.viewport.center_axes" ) ) { return; }
        // The origin lines are the world axes the view shows: the line where
        // v = 0 runs along axis U, so it takes U's colour.
        const QPointF origin = WorldToView( QPointF( 0.0, 0.0 ) );
        const gui::editor_style_color_t axisColors[3]{ gui::STYLE_COLOR_AXIS_X, gui::STYLE_COLOR_AXIS_Y, gui::STYLE_COLOR_AXIS_Z };
        painter.setPen( QPen( EditorStyle_Color( style, axisColors[m_axisU] ), 0.0 ) );
        painter.drawLine( QLineF( 0.0, origin.y(), width(), origin.y() ) );
        painter.setPen( QPen( EditorStyle_Color( style, axisColors[m_axisV] ), 0.0 ) );
        painter.drawLine( QLineF( origin.x(), 0.0, origin.x(), height() ) );
    }

    void DrawObjects( QPainter &painter ) const
    {
        const map_workspace_t &workspace = *m_pWorkspace;
        const map_wireframe_t &wire = workspace.wire;
        const QPointF topLeft = ViewToWorld( QPointF( 0.0, 0.0 ) );
        const QPointF bottomRight = ViewToWorld( QPointF( width(), height() ) );
        QVector<QLineF> batches[LINE_COUNT];
        QVector<QLineF> ghosts;
        QVector<QPointF> vertices;
        QVector<const map_wire_object_t *> entities;
        for ( usize i = 0u; i < wire.objects.nCount; ++i ) {
            const map_wire_object_t &object = wire.objects.pData[i];
            const bool visible = MapWorkspace_IsVisible( &workspace, object );
            if ( !object.bounds.bHas || ( !visible && ( object.kind == map_wire_kind_t::ENTITY || !DisplayFlag( workspace, "editor.viewport.ghost_hidden", CY_FALSE ) ) ) ) { continue; }
            if ( Axis( object.bounds.box.maximum, m_axisU ) < topLeft.x() || Axis( object.bounds.box.minimum, m_axisU ) > bottomRight.x() ||
                 Axis( object.bounds.box.maximum, m_axisV ) < bottomRight.y() || Axis( object.bounds.box.minimum, m_axisV ) > topLeft.y() ) {
                continue;
            }
            if ( object.kind == map_wire_kind_t::ENTITY ) {
                entities.append( &object );
                continue;
            }
            if ( visible && MoveSourceActive( workspace ) && EditPreviewTarget( workspace, object.id ) ) { continue; }
            const line_color_t slot = visible ? ColorSlot( workspace, object, m_hover.id ) : LINE_WORLD;
            const bool faceContext = FacePickingActive() && ( object.kind == map_wire_kind_t::BRUSH || object.kind == map_wire_kind_t::MESH );
            const line_color_t edgeSlot = faceContext && ( slot == LINE_SELECTED || slot == LINE_HOVER ) ? LINE_WORLD : slot;
            QVector<QLineF> &batch = visible && !EditPreviewTarget( workspace, object.id ) ? batches[edgeSlot] : ghosts;
            for ( u32 l = 0u; l < object.nLines; ++l ) {
                const map_wire_line_t &line = wire.lines.pData[object.iFirstLine + l];
                const math::vec3d_t &a = wire.points.pData[line.iA];
                const math::vec3d_t &b = wire.points.pData[line.iB];
                batch.append( QLineF( WorldToView( QPointF( Axis( a, m_axisU ), Axis( a, m_axisV ) ) ),
                                      WorldToView( QPointF( Axis( b, m_axisU ), Axis( b, m_axisV ) ) ) ) );
                if ( slot == LINE_SELECTED && DisplayFlag( workspace, "editor.viewport.show_selection_vertices", CY_FALSE ) ) {
                    vertices.append( batch.back().p1() );
                    vertices.append( batch.back().p2() );
                }
            }
        }
        DrawGhostBatch( painter, workspace, ghosts );
        // Selection last, so it is never hidden under unselected lines.
        for ( u32 slot = 0u; slot < LINE_SELECTED; ++slot ) {
            DrawWireBatch( painter, workspace, static_cast<line_color_t>( slot ), batches[slot] );
        }
        DrawEntities( painter, entities );
        DrawWireBatch( painter, workspace, LINE_SELECTED, batches[LINE_SELECTED] );
        DrawVertexCues( painter, workspace, vertices, rect() );
        DrawOrthoMeshVertexCandidates( painter, workspace, [this]( math::vec3d_t point, QPointF &screen ) {
            screen = WorldToView( { Axis( point, m_axisU ), Axis( point, m_axisV ) } ); return true;
        }, rect() );
        DrawMeshEdgeSelection( painter, workspace, m_hover.id == m_hoverEdge.object ? m_hoverEdge : map_mesh_edge_hit_t{},
            [this]( math::vec3d_t a, math::vec3d_t b, QLineF &line ) {
                line = QLineF( WorldToView( { Axis( a, m_axisU ), Axis( a, m_axisV ) } ),
                    WorldToView( { Axis( b, m_axisU ), Axis( b, m_axisV ) } ) );
                return true;
            } );
        DrawMeshVertexSelection( painter, workspace, m_hover.id == m_hoverVertex.object ? m_hoverVertex : map_mesh_vertex_hit_t{},
            [this]( math::vec3d_t point, QPointF &screen ) {
                screen = WorldToView( { Axis( point, m_axisU ), Axis( point, m_axisV ) } ); return true;
            }, rect() );
        if ( FacePickingActive() ) {
            const bool facePreview = MapWorkspace_HasFacePreview( &workspace ) && workspace.editPreview.status == map_status_t::OK;
            for ( usize i = 0; i < wire.faces.nCount + ( facePreview ? workspace.editPreviewWire.faces.nCount : 0u ); ++i ) {
                const bool candidate = i >= wire.faces.nCount;
                const auto &displayedWire = candidate ? workspace.editPreviewWire : wire;
                const auto &face = displayedWire.faces.pData[candidate ? i - wire.faces.nCount : i];
                const bool selected = ( face.faceId != 0 && MapWorkspace_HasMeshFace( &workspace ) && face.id == workspace.selectedMeshFaceObject && face.faceId == workspace.selectedMeshFaceId ) ||
                    ( face.sideId != 0 && MapWorkspace_HasBrushFace( &workspace ) && face.id == workspace.selectedBrushFaceObject && face.sideId == workspace.selectedBrushFaceSide );
                const bool hovered = !candidate && face.id == m_hover.id && ( ( face.faceId != 0 && face.faceId == m_hoverMeshFace ) || ( face.sideId != 0 && face.sideId == m_hoverBrushSide ) );
                if ( ( !selected && !hovered ) || ( !candidate && EditPreviewTarget( workspace, face.id ) ) ) { continue; }
                const auto *object = MapWireframe_FindObject( wire, face.id );
                if ( object == nullptr || !MapWorkspace_IsVisible( &workspace, *object ) ) { continue; }
                QPolygonF polygon; polygon.reserve( face.nIndices );
                for ( u32 k = 0; k < face.nIndices; ++k ) {
                    const auto point = displayedWire.points.pData[displayedWire.faceIndices.pData[face.iFirstIndex + k]];
                    polygon << WorldToView( { Axis( point, m_axisU ), Axis( point, m_axisV ) } );
                }
                const QColor color = SlotColor( workspace, selected ? LINE_SELECTED : LINE_HOVER );
                QColor fill = color; fill.setAlpha( selected ? 32 : 16 );
                painter.setPen( QPen( color, 2 ) ); painter.setBrush( fill ); painter.drawPolygon( polygon );
            }
        }
    }

    void DrawConnections( QPainter &painter ) const
    {
        const map_workspace_t &workspace = *m_pWorkspace;
        for ( usize i = 0u; i < workspace.wire.connections.nCount; ++i ) {
            const map_wire_connection_t &connection = workspace.wire.connections.pData[i];
            if ( !ShowConnection( workspace, connection ) ) { continue; }
            const auto project = [&]( const math::vec3d_t &point ) {
                return WorldToView( QPointF( Axis( point, m_axisU ), Axis( point, m_axisV ) ) );
            };
            DrawConnection( painter, workspace, connection, QLineF( project( connection.sourceOrigin ), project( connection.targetOrigin ) ) );
        }
    }

    // Outside the cordon is dimmed (Hammer's tinted region) and the box is
    // outlined, so it is clear what is hidden and why.
    void DrawCordon( QPainter &painter ) const
    {
        if ( !MapWorkspace_IsCordonActive( m_pWorkspace ) ) { return; }
        const map_bounds_t &cordon = m_pWorkspace->cordon;
        const QRectF box = QRectF( WorldToView( QPointF( Axis( cordon.box.minimum, m_axisU ), Axis( cordon.box.minimum, m_axisV ) ) ),
                                   WorldToView( QPointF( Axis( cordon.box.maximum, m_axisU ), Axis( cordon.box.maximum, m_axisV ) ) ) ).normalized();
        QPainterPath outside;
        outside.addRect( QRectF( rect() ) );
        QPainterPath inside;
        inside.addRect( box );
        painter.fillPath( outside.subtracted( inside ), Token( *m_pWorkspace, "viewport.cordon.outside" ) );
        painter.setPen( QPen( Token( *m_pWorkspace, "viewport.cordon" ), 1.5, Qt::DashLine ) );
        painter.setBrush( Qt::NoBrush );
        painter.drawRect( box );
    }

    void DrawSelection( QPainter &painter ) const
    {
        const map_workspace_t &workspace = *m_pWorkspace;
        // The retained mesh root supplies inspector/frame context only.
        // Component selection is conveyed by its overlay, not by a
        // whole-root yellow bounds box or its three dimension measurements.
        if ( workspace.tool == map_tool_t::NONE || MeshComponentMode( workspace ) ) { return; }
        const map_bounds_t bounds = SelectionGeometryBounds( workspace );
        if ( !bounds.bHas || ( workspace.editPreview.bActive && ( workspace.editPreview.bClip || workspace.editPreview.bFacePushPull ||
            workspace.editPreview.transform.kind != map_transform_preview_kind_t::NONE ) ) ) { return; }
        const QRectF box = QRectF( WorldToView( QPointF( Axis( bounds.box.minimum, m_axisU ), Axis( bounds.box.minimum, m_axisV ) ) ),
                                   WorldToView( QPointF( Axis( bounds.box.maximum, m_axisU ), Axis( bounds.box.maximum, m_axisV ) ) ) ).normalized();
        if ( !box.intersects( QRectF( rect() ) ) ) { return; }
        if ( DisplayFlag( workspace, "editor.viewport.show_selection_bounds" ) ) {
            QColor color = SlotColor( workspace, LINE_SELECTED );
            color.setAlpha( 155 );
            painter.setPen( QPen( color, 1.0, Qt::DashLine ) );
            painter.setBrush( Qt::NoBrush );
            painter.drawRect( box );
        }
        if ( !workspace.editPreview.bActive && DisplayFlag( workspace, "editor.viewport.show_selection_dimensions" ) ) {
            const auto project = [this]( math::vec3d_t point, QPointF &screen ) {
                screen = WorldToView( QPointF( Axis( point, m_axisU ), Axis( point, m_axisV ) ) ); return true;
            };
            DrawBoundsDimensions( painter, workspace, bounds, project, rect() );
        }
    }

    void DrawEntities( QPainter &painter, const QVector<const map_wire_object_t *> &entities ) const
    {
        const map_workspace_t &workspace = *m_pWorkspace;
        const auto screenBounds = [this]( const map_wire_object_t &object ) {
            const QPointF a = WorldToView( QPointF( Axis( object.bounds.box.minimum, m_axisU ), Axis( object.bounds.box.minimum, m_axisV ) ) );
            const QPointF b = WorldToView( QPointF( Axis( object.bounds.box.maximum, m_axisU ), Axis( object.bounds.box.maximum, m_axisV ) ) );
            return QRectF( a, b ).normalized();
        };
        for ( const map_wire_object_t *pObject : entities ) {
            if ( MoveSourceActive( workspace ) && EditPreviewTarget( workspace, pObject->id ) ) { continue; }
            const QRectF box = screenBounds( *pObject );
            const line_color_t slot = ColorSlot( workspace, *pObject, m_hover.id );
            QColor color = SlotColor( workspace, slot );
            QColor fill = color;
            fill.setAlpha( EditPreviewTarget( workspace, pObject->id ) ? 16 : 48 );
            painter.setPen( QPen( color, slot == LINE_SELECTED || slot == LINE_HOVER ? 1.5 : 0.0 ) );
            painter.setBrush( fill );
            painter.drawRect( box );
        }
        painter.setBrush( Qt::NoBrush );
        // Label priority is independent of authored object order and marker
        // drawing. Ordinary names can use only the space selection leaves.
        QVector<QRectF> labels;
        for ( const bool selected : { true, false } ) {
            for ( const map_wire_object_t *pObject : entities ) {
                if ( MapWorkspace_IsSelected( &workspace, pObject->id ) != selected ||
                     EditPreviewTarget( workspace, pObject->id ) || !ShowEntityName( workspace, pObject->id ) ) { continue; }
                const map_wire_entity_t *pEntity = MapWireframe_FindEntity( workspace.wire, pObject->id );
                if ( pEntity == nullptr ) { continue; }
                DrawEntityLabel( painter, workspace, *pEntity, screenBounds( *pObject ), rect(), labels );
            }
        }
    }

    map_workspace_t *m_pWorkspace{ nullptr };
    map_tool_t m_inputTool{ map_tool_t::SELECT }; // Tool transitions reset pane-local navigation input.
    map_element_mode_t m_inputMode{ map_element_mode_t::OBJECTS };
    map_ortho_axes_t m_axes{ map_ortho_axes_t::TOP };
    u32 m_axisU{ 0u };
    u32 m_axisV{ 1u };
    f64 m_gizmoScale{ 1.0 }; // Last effective scale, including inherited scope values.
    f64 m_zoom{ 0.25 };
    QPointF m_center{};
    QImage m_background{};
    QRectF m_backgroundWorld{};
    f64 m_backgroundOpacity{ 0.5 };
    QPointF m_panStart{};
    QPointF m_panCenterStart{};
    f64 m_panSensitivity{ 1.0 }; // Captured at press, so a settings edit cannot jump a live pan.
    map_bounds_t m_frameBounds{};
    view_hover_t m_hover{};
    map_mesh_edge_hit_t m_hoverEdge{};
    map_mesh_vertex_hit_t m_hoverVertex{};
    u64 m_hoverMeshFace{ 0 }, m_hoverBrushSide{ 0 };
    view_edit_drag_t m_drag{};
    bool_t m_bPanning{ CY_FALSE };
    bool_t m_bSpaceHeld{ CY_FALSE };
    bool_t m_bFramePending{ CY_FALSE };
};

// ---------------------------------------------------------------------------
// 3D camera view
// ---------------------------------------------------------------------------


bool RayBounds( geometry::geometry_raycast_ray_t ray, const map_bounds_t &bounds, f64 minimum, f64 maximum, f64 &distance )
{
    if ( !bounds.bHas ) { return false; }
    f64 enter = minimum, leave = maximum;
    for ( u32 axis = 0; axis < 3; ++axis ) {
        const f64 origin = Axis( ray.origin, axis ), direction = Axis( ray.direction, axis );
        const f64 lo = Axis( bounds.box.minimum, axis ), hi = Axis( bounds.box.maximum, axis );
        if ( std::abs( direction ) < 1e-12 ) { if ( origin < lo || origin > hi ) { return false; } }
        else {
            const f64 a = ( lo - origin ) / direction, b = ( hi - origin ) / direction;
            enter = std::max( enter, std::min( a, b ) );
            leave = std::min( leave, std::max( a, b ) );
            if ( enter > leave ) { return false; }
        }
    }
    distance = enter;
    return leave >= minimum;
}

class map_camera_view_t final : public QWidget {
public:
    map_camera_view_t( QWidget *pParent, map_workspace_t *pWorkspace ) : QWidget( pParent ), m_pWorkspace( pWorkspace )
    {
        CY_ASSERT( pWorkspace != nullptr );
        m_gizmoScale = GizmoScale( *pWorkspace );
        m_moveSpeed = Setting( "editor.camera.move_speed", 1000.0 );
        m_inputTool = pWorkspace->tool;
        m_inputMode = pWorkspace->elementMode;
        setFocusPolicy( Qt::StrongFocus );
        setMouseTracking( true );
        setAttribute( Qt::WA_OpaquePaintEvent );
        setMinimumSize( 64, 64 );
        m_timer.setInterval( kCameraTickMs );
        QObject::connect( &m_timer, &QTimer::timeout, this, [this]() { Tick(); } );
        m_speedFeedbackTimer.setSingleShot( true );
        m_speedFeedbackTimer.setInterval( 2000 );
        QObject::connect( &m_speedFeedbackTimer, &QTimer::timeout, this, [this]() { update(); } );
        ( void )MapWorkspace_AddListener( pWorkspace, &map_camera_view_t::OnChanged, this );
        ( void )EditorSettings_AddListener( &pWorkspace->pGui->settings, &map_camera_view_t::OnSettingsChanged, this );
        ViewHover_Init( m_hover, *this, [this]( QPointF point ) {
            m_hoverEdge = {}; m_hoverVertex = {};
            if ( MeshVertexPickingActive( *m_pWorkspace ) ) {
                ( void )PickMeshVertex( point, &m_hoverVertex );
                m_hoverFace = {}; m_hoverMeshFace = 0u;
                return m_hoverVertex.object;
            }
            if ( MeshEdgePickingActive( *m_pWorkspace ) ) {
                ( void )PickMeshEdge( point, &m_hoverEdge );
                m_hoverFace = {}; m_hoverMeshFace = 0u;
                return m_hoverEdge.object;
            }
            return Pick( point, &m_hoverFace, &m_hoverMeshFace );
        } );
        FrameBounds( pWorkspace->frameBounds );
    }

    ~map_camera_view_t() override
    {
        EditorSettings_RemoveListener( &m_pWorkspace->pGui->settings, &map_camera_view_t::OnSettingsChanged, this );
        MapWorkspace_RemoveListener( m_pWorkspace, &map_camera_view_t::OnChanged, this );
        if ( m_drag.kind != edit_drag_t::NONE ) { CancelDrag( m_drag, *m_pWorkspace ); }
    }

    math::vec3d_t Position() const noexcept { return m_position; }
    map_camera_grid_info_t GridInfo() const { return m_gridInfo; }
    bool NavigationActive() const noexcept {
        return m_cameraDrag.kind != map_camera_gesture_t::NONE || m_pWorkspace->tool == map_tool_t::NONE || m_pWorkspace->tool == map_tool_t::CAMERA;
    }
    bool NavigationOwnsInput( Qt::KeyboardModifiers modifiers ) const {
        if ( !NavigationActive() ) { return false; }
        if ( m_cameraDrag.kind != map_camera_gesture_t::NONE || m_pWorkspace->tool == map_tool_t::CAMERA ) { return true; }
        // Neutral navigation can still leave through an explicit idle tool
        // chord. Once a direction is held, Shift movement belongs to flight.
        // Resolve live bindings and modifiers; an unbound or command chord
        // must not retain ownership from an earlier navigation context.
        constexpr u32 directions = MAP_NAVIGATION_FORWARD | MAP_NAVIGATION_BACK | MAP_NAVIGATION_LEFT |
            MAP_NAVIGATION_RIGHT | MAP_NAVIGATION_UP | MAP_NAVIGATION_DOWN;
        for ( const int code : m_pressedNavigation ) {
            QKeyEvent event( QEvent::KeyPress, code, modifiers );
            if ( ( MapInput_NavigationKeyMask( m_pWorkspace, &event ) & directions ) != 0u ) { return true; }
        }
        return false;
    }
    u64 HoveredObject() const noexcept { return m_hover.id; }

    map_render_mode_t RenderMode() const noexcept { return m_renderMode; }
    void SetRenderMode( map_render_mode_t mode ) {
        if ( mode != m_renderMode && m_drag.kind != edit_drag_t::NONE ) { CancelDrag( m_drag, *m_pWorkspace ); }
        m_renderMode = mode; update();
    }
    bool MeshEdges() const noexcept { return m_bMeshEdges; }
    void SetMeshEdges( bool bOn ) { m_bMeshEdges = bOn; update(); }
    bool WireOverlay() const noexcept { return m_bWireOverlay; }
    void SetWireOverlay( bool bOn ) { m_bWireOverlay = bOn; update(); }

    void Dolly( f64 units ) { StopCameraDrag(); if ( m_drag.kind != edit_drag_t::NONE ) { CancelDrag( m_drag, *m_pWorkspace ); } m_position = Add( m_position, Scale( Forward(), units ) ); update(); }
    void Level() { StopCameraDrag(); if ( m_drag.kind != edit_drag_t::NONE ) { CancelDrag( m_drag, *m_pWorkspace ); } m_pitch = 0.0; update(); }

    void Frame( const map_bounds_t &bounds ) noexcept { FrameBounds( bounds ); }

    bool_t WorldToView( math::vec3d_t point, QPointF *pOut ) noexcept
    {
        if ( pOut == nullptr ) { return CY_FALSE; }
        if ( m_bFramePending ) { ApplyFrame(); }
        Basis();
        QLineF line;
        if ( !Project( point, point, &line ) ) { return CY_FALSE; }
        *pOut = line.p1();
        return CY_TRUE;
    }

    math::vec3d_t Forward() const noexcept
    {
        const f64 yaw = m_yaw * kDegToRad;
        const f64 pitch = m_pitch * kDegToRad;
        return math::Vec3d_Make( std::cos( pitch ) * std::cos( yaw ), std::cos( pitch ) * std::sin( yaw ), std::sin( pitch ) );
    }

    u64 Pick( QPointF screen, geometry::brush_raycast_hit_t *faceOut = nullptr, u64 *meshFaceOut = nullptr,
        bool allowEdgeFallback = true, f64 *pickedDistance = nullptr, bool physicalOnly = false, bool *physicalComplete = nullptr )
    {
        namespace geo = geometry;
        if ( physicalComplete != nullptr ) { *physicalComplete = true; }
        if ( faceOut != nullptr ) { *faceOut = {}; }
        if ( meshFaceOut != nullptr ) { *meshFaceOut = 0; }
        if ( pickedDistance != nullptr ) { *pickedDistance = 0.0; }
        geo::brush_raycast_hit_t selectedHit{};
        u64 meshRoot = 0, meshFace = 0;
        f64 meshDistance = 0;
        if ( m_bFramePending ) { ApplyFrame(); }
        Basis();
        const f64 focal = FocalLength();
        if ( focal <= 0.0 || !rect().contains( screen.toPoint() ) ) {
            if ( physicalComplete != nullptr ) { *physicalComplete = false; }
            return 0u;
        }
        const geo::geometry_raycast_ray_t ray{ m_position, ScreenDirection( screen ) };
        // Project clips forward depth, whereas geometry queries measure
        // distance along a unit ray. Their near limits differ off center.
        const f64 minimum = kCameraNear / Dot( ray.direction, m_forward );
        const map_document_t &map = *m_pWorkspace->pDocument;
        u64 best = 0u;
        f64 nearest = 1.0e6;
        const auto eligible = [&]( u64 id ) {
            const map_wire_object_t *object = MapWireframe_FindObject( m_pWorkspace->wire, id );
            f64 entry = 0.0;
            return object != nullptr && ( physicalOnly ? MapWorkspace_IsVisible( m_pWorkspace, *object ) : ViewPickableRoot( *m_pWorkspace, *object ) ) &&
                RayBounds( ray, object->bounds, minimum, nearest, entry );
        };
        const auto incomplete = [&]() { if ( physicalComplete != nullptr ) { *physicalComplete = false; } };
        const auto accept = [&]( u64 id, f64 distance ) {
            if ( distance >= minimum && std::isfinite( distance ) &&
                 ( distance < nearest || ( distance == nearest && ( best == 0u || id < best ) ) ) ) {
                nearest = distance;
                best = id;
            }
        };
        geo::brush_boundary_t boundary{};
        const bool boundaryReady = geo::BrushBoundary_Init( &boundary, map.pAllocator ) == geo::geometry_status_t::OK;
        {
            for ( usize i = 0; i < map.geometry.brushes.nCount; ++i ) {
                const auto *brush = map.geometry.brushes.pData[i];
                if ( !eligible( brush->sourceId.value ) ) { continue; }
                if ( !boundaryReady || geo::BrushBoundary_TryReconstruct( &boundary, brush, map.geometryPolicy ) != geo::geometry_status_t::OK ) { incomplete(); continue; }
                geo::brush_raycast_hit_t hit{};
                geo::geometry_raycast_options_t options{};
                options.fMinimumDistance = minimum;
                options.fMaximumDistance = nearest;
                const auto status = geo::BrushQueries_TryRaycast( brush, &boundary, ray, options, map.geometryPolicy, &hit );
                if ( status != geo::geometry_status_t::OK ) { incomplete(); }
                if ( status == geo::geometry_status_t::OK && hit.bHit ) {
                    accept( brush->sourceId.value, hit.fDistance );
                    if ( best == brush->sourceId.value && nearest == hit.fDistance ) { selectedHit = hit; }
                }
            }
        }
        geo::BrushBoundary_Shutdown( &boundary );
        for ( usize i = 0; i < map.geometry.meshes.nCount; ++i ) {
            const auto *mesh = map.geometry.meshes.pData[i];
            if ( !eligible( mesh->sourceId.value ) ) { continue; }
            usize bytes = 0u;
            if ( geo::MeshQueries_TryGetRaycastScratchSize( &mesh->mesh, map.geometryPolicy, &bytes ) != geo::geometry_status_t::OK ) { incomplete(); continue; }
            geo::geometry_scratch_t scratch{};
            geo::geometry_scratch_desc_t desc{};
            desc.pFallbackAllocator = map.pAllocator;
            desc.cbCapacity = bytes;
            desc.cbBudget = 64u * CY_MIB;
            if ( geo::GeometryScratch_Acquire( &scratch, desc ) != geo::geometry_status_t::OK ) { incomplete(); continue; }
            geo::mesh_raycast_hit_t hit{};
            geo::geometry_raycast_options_t options{};
            options.fMinimumDistance = minimum;
            options.fMaximumDistance = nearest;
            const auto status = geo::MeshQueries_TryRaycast( &mesh->mesh, mesh->sourceId, ray, options, map.geometryPolicy, &scratch, &hit );
            if ( status != geo::geometry_status_t::OK ) { incomplete(); }
            if ( status == geo::geometry_status_t::OK && hit.bHit ) {
                accept( mesh->sourceId.value, hit.fDistance );
                if ( best == mesh->sourceId.value && nearest == hit.fDistance ) {
                    // Resolve the transient raycast pool handle immediately.
                    // Workspace/hover state retains only authored identities.
                    meshRoot = best; meshFace = geo::MeshSource_FaceId( mesh, hit.hFace ).value; meshDistance = nearest;
                }
            }
            if ( geo::GeometryScratch_Release( &scratch ) != geo::geometry_status_t::OK ) { incomplete(); }
        }
        for ( usize i = 0; i < map.geometry.heightFields.nCount; ++i ) {
            const auto *field = map.geometry.heightFields.pData[i];
            if ( !eligible( field->sourceId.value ) ) { continue; }
            geo::heightfield_ray_hit_t hit{};
            if ( nearest >= minimum && geo::HeightField_TryRaycast( field, Add( ray.origin, Scale( ray.direction, minimum ) ),
                     ray.direction, nearest - minimum, &hit ) ) { accept( field->sourceId.value, hit.t + minimum ); }
        }
        // Point-entity helper markers have no physical surface. Patches are
        // currently shown as control nets, so pick those visible lines rather
        // than claiming the control hull is an evaluated curved surface.
        for ( usize i = 0; !physicalOnly && i < m_pWorkspace->wire.objects.nCount; ++i ) {
            const map_wire_object_t &object = m_pWorkspace->wire.objects.pData[i];
            if ( !object.bounds.bHas || !ViewPickableRoot( *m_pWorkspace, object ) ) { continue; }
            if ( object.kind == map_wire_kind_t::ENTITY ) {
                const auto *entity = MapWireframe_FindEntity( m_pWorkspace->wire, object.id );
                if ( entity == nullptr || !entity->bHasOrigin ) { continue; }
                f64 entry = 0.0;
                if ( RayBounds( ray, object.bounds, minimum, nearest, entry ) ) { accept( object.id, entry ); }
                else {
                    QLineF point;
                    if ( Project( entity->origin, entity->origin, &point ) && QLineF( screen, point.p1() ).length() <= MAP_VIEW_PICK_PIXELS ) {
                        accept( object.id, Dot( Sub( entity->origin, m_position ), ray.direction ) );
                    }
                }
            } else if ( object.kind == map_wire_kind_t::PATCH ) {
                for ( u32 edge = 0; edge < object.nLines; ++edge ) {
                    const auto &indices = m_pWorkspace->wire.lines.pData[object.iFirstLine + edge];
                    const auto &a = m_pWorkspace->wire.points.pData[indices.iA];
                    const auto &b = m_pWorkspace->wire.points.pData[indices.iB];
                    f64 distance = 0.0;
                    if ( PickSegment( screen, a, b, distance ) ) { accept( object.id, distance ); }
                }
            }
        }
        // A thin or grazing surface can occupy less than a pixel while its
        // antialiased outline is still clearly visible. Exact surfaces keep
        // priority; whole-root selection additionally accepts nearby authored
        // edges, using the same logical-pixel tolerance as the 2D views.
        const auto mode = m_pWorkspace->elementMode;
        const bool wholeObject = mode == map_element_mode_t::OBJECTS || mode == map_element_mode_t::GROUPS || mode == map_element_mode_t::MESHES;
        if ( !physicalOnly && best == 0u && allowEdgeFallback && wholeObject &&
             m_pWorkspace->tool != map_tool_t::EXTRUDE && m_pWorkspace->tool != map_tool_t::TEXTURE &&
             m_pWorkspace->tool != map_tool_t::EYEDROPPER ) {
            for ( usize i = 0; i < m_pWorkspace->wire.objects.nCount; ++i ) {
                const auto &object = m_pWorkspace->wire.objects.pData[i];
                if ( ( object.kind != map_wire_kind_t::BRUSH && object.kind != map_wire_kind_t::MESH ) ||
                     !object.bounds.bHas || !ViewPickableRoot( *m_pWorkspace, object ) ) { continue; }
                f64 objectDistance = 1.0e6;
                QPointF objectScreen;
                bool edgeHit = false;
                for ( u32 edge = 0; edge < object.nLines; ++edge ) {
                    const auto &line = m_pWorkspace->wire.lines.pData[object.iFirstLine + edge];
                    f64 distance = 0.0; QPointF closest;
                    if ( PickSegment( screen, m_pWorkspace->wire.points.pData[line.iA], m_pWorkspace->wire.points.pData[line.iB], distance, &closest ) &&
                         distance < objectDistance ) { objectDistance = distance; objectScreen = closest; edgeHit = true; }
                }
                if ( !edgeHit || objectDistance > nearest || !EdgeSourceQueryable( object, objectScreen ) ) { continue; }
                // The tolerance must not tunnel through a surface hiding the
                // edge. Check the edge's own projected position, not the nearby
                // cursor ray which has already missed all exact surfaces.
                f64 occluderDistance = 0.0;
                const u64 occluder = Pick( objectScreen, nullptr, nullptr, false, &occluderDistance );
                // PickSegment expresses its distance on the original cursor
                // ray. Compare both depths along the edge's recast ray: a
                // farther surface behind a grazing edge does not hide it.
                const f64 edgeDistance = objectDistance * Dot( ray.direction, m_forward ) / Dot( ScreenDirection( objectScreen ), m_forward );
                const auto &numerical = map.geometryPolicy.numerical;
                const f64 tolerance = numerical.fAbsoluteDistanceTolerance +
                    numerical.fRelativeDistanceTolerance * std::max( std::abs( edgeDistance ), std::abs( occluderDistance ) );
                if ( occluder != 0u && occluder != object.id && occluderDistance < edgeDistance - tolerance ) { continue; }
                accept( object.id, objectDistance );
            }
        }
        if ( faceOut != nullptr && selectedHit.brushSourceId.value == best ) { *faceOut = selectedHit; }
        if ( meshFaceOut != nullptr && meshRoot == best && meshDistance == nearest ) { *meshFaceOut = meshFace; }
        if ( pickedDistance != nullptr && best != 0u ) { *pickedDistance = nearest; }
        return best;
    }

    bool PickMeshEdge( QPointF screen, map_mesh_edge_hit_t *out, bool *queryComplete = nullptr )
    {
        if ( queryComplete != nullptr ) { *queryComplete = true; }
        if ( out == nullptr ) { return false; }
        *out = {};
        if ( !MeshEdgePickingActive( *m_pWorkspace ) || m_pWorkspace->pDocument == nullptr || !rect().contains( screen.toPoint() ) ) { return false; }
        if ( m_bFramePending ) { ApplyFrame(); }
        Basis();
        f64 nearest = 1.0e6;
        f64 screenDistance = MAP_VIEW_PICK_PIXELS;
        bool complete = true;
        const auto cursorRay = ScreenDirection( screen );
        const auto &numerical = m_pWorkspace->pDocument->geometryPolicy.numerical;
        for ( usize i = 0u; i < m_pWorkspace->wire.objects.nCount; ++i ) {
            const auto &object = m_pWorkspace->wire.objects.pData[i];
            if ( object.kind != map_wire_kind_t::MESH || !MapWorkspace_IsVisible( m_pWorkspace, object ) ) { continue; }
            const auto *source = geometry::GeometryDocument_FindMesh( &m_pWorkspace->pDocument->geometry, { object.id } );
            if ( !geometry::MeshSource_IsInitialized( source ) ) { continue; }
            VisitMeshEdges( *source, [&]( geometry::mesh_edge_ref_t edge, math::vec3d_t a, math::vec3d_t b ) {
                f64 distance = 0.0; QPointF closest;
                if ( !PickSegment( screen, a, b, distance, &closest ) || distance > nearest || !rect().contains( closest.toPoint() ) ) { return; }
                f64 occluderDistance = 0.0;
                bool probeComplete = false;
                const u64 occluder = Pick( closest, nullptr, nullptr, false, &occluderDistance, true, &probeComplete );
                // An unsuccessful surface query is not proof of empty space.
                // Fail closed, preserving the current selection on press.
                if ( !probeComplete ) { complete = false; return; }
                const f64 edgeDistance = distance * Dot( cursorRay, m_forward ) / Dot( ScreenDirection( closest ), m_forward );
                const f64 tolerance = numerical.fAbsoluteDistanceTolerance + numerical.fRelativeDistanceTolerance *
                    std::max( std::abs( edgeDistance ), std::abs( occluderDistance ) );
                // Include this mesh's own front faces: rear edges are hidden
                // even when the occluding surface has the same root identity.
                if ( occluder != 0u && occluderDistance < edgeDistance - tolerance ) { return; }
                const f64 proximity = QLineF( screen, closest ).length();
                const map_mesh_edge_hit_t candidate{ object.id, edge };
                if ( out->object == 0u || distance < nearest ||
                     ( distance == nearest && ( proximity < screenDistance ||
                       ( proximity == screenDistance && MeshEdgeHitBefore( candidate, *out ) ) ) ) ) {
                    *out = candidate; nearest = distance; screenDistance = proximity;
                }
            } );
        }
        if ( queryComplete != nullptr ) { *queryComplete = complete; }
        if ( !complete ) { *out = {}; }
        return complete && out->object != 0u;
    }

    bool PickMeshVertex( QPointF screen, map_mesh_vertex_hit_t *out, bool *queryComplete = nullptr )
    {
        if ( queryComplete != nullptr ) { *queryComplete = true; }
        if ( out == nullptr ) { return false; }
        *out = {};
        if ( !MeshVertexPickingActive( *m_pWorkspace ) || m_pWorkspace->pDocument == nullptr ||
             !std::isfinite( screen.x() ) || !std::isfinite( screen.y() ) || !QRectF( rect() ).contains( screen ) ) { return false; }
        if ( m_bFramePending ) { ApplyFrame(); }
        Basis();
        f64 nearest = 1.0e6;
        f64 screenDistance = MAP_VIEW_PICK_PIXELS;
        bool complete = true;
        const auto cursorRay = ScreenDirection( screen );
        const auto &numerical = m_pWorkspace->pDocument->geometryPolicy.numerical;
        for ( usize i = 0u; i < m_pWorkspace->wire.objects.nCount; ++i ) {
            const auto &object = m_pWorkspace->wire.objects.pData[i];
            if ( object.kind != map_wire_kind_t::MESH || !MapWorkspace_IsVisible( m_pWorkspace, object ) ) { continue; }
            const auto *source = geometry::GeometryDocument_FindMesh( &m_pWorkspace->pDocument->geometry, { object.id } );
            if ( !geometry::MeshSource_IsInitialized( source ) ) { continue; }
            VisitMeshVertices( *source, [&]( geometry::geometry_source_id_t vertex, math::vec3d_t point ) {
                QLineF projected;
                if ( !Project( point, point, &projected ) || !QRectF( rect() ).contains( projected.p1() ) ) { return; }
                const f64 proximity = QLineF( screen, projected.p1() ).length();
                if ( proximity > MAP_VIEW_PICK_PIXELS ) { return; }
                const f64 depth = Dot( Sub( point, m_position ), m_forward );
                const f64 distance = depth / Dot( cursorRay, m_forward );
                if ( !std::isfinite( distance ) || distance > nearest ) { return; }
                f64 occluderDistance = 0.0; bool probeComplete = false;
                const u64 occluder = Pick( projected.p1(), nullptr, nullptr, false, &occluderDistance, true, &probeComplete );
                if ( !probeComplete ) { complete = false; return; }
                const f64 vertexDistance = depth / Dot( ScreenDirection( projected.p1() ), m_forward );
                const f64 tolerance = numerical.fAbsoluteDistanceTolerance + numerical.fRelativeDistanceTolerance *
                    std::max( std::abs( vertexDistance ), std::abs( occluderDistance ) );
                if ( occluder != 0u && occluderDistance < vertexDistance - tolerance ) { return; }
                const bool before = object.id < out->object || ( object.id == out->object && vertex.value < out->vertex.value );
                if ( out->object == 0u || distance < nearest || ( distance == nearest &&
                     ( proximity < screenDistance || ( proximity == screenDistance && before ) ) ) ) {
                    *out = { object.id, vertex }; nearest = distance; screenDistance = proximity;
                }
            } );
        }
        if ( queryComplete != nullptr ) { *queryComplete = complete; }
        if ( !complete ) { *out = {}; }
        return complete && out->object != 0u;
    }

protected:
    bool event( QEvent *event ) override
    {
        if ( event->type() == QEvent::KeyPress ) {
            auto *key = static_cast<QKeyEvent *>( event );
            if ( key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab ) {
                keyPressEvent( key );
                if ( key->isAccepted() ) { return true; }
            }
        }
        if ( event->type() == QEvent::ShortcutOverride ) {
            const auto *key = static_cast<QKeyEvent *>( event );
            const auto gesture = MapInput_ToolGestureKey( m_pWorkspace, key, true );
            if ( gesture == map_tool_gesture_key_t::CANCEL ||
                 ( ( m_drag.kind != edit_drag_t::NONE || m_pWorkspace->editPreview.bClip || m_pWorkspace->editPreview.bStagedBlock ) && gesture != map_tool_gesture_key_t::NONE ) ) { event->accept(); return true; }
            const bool navigationOwned = NavigationOwnsInput( key->modifiers() );
            if ( NavigationActive() && MapInput_IsNavigationKey( m_pWorkspace, key, navigationOwned ) ) { event->accept(); return true; }
            if ( MapInput_DispatchKey( m_pWorkspace, const_cast<QKeyEvent *>( key ), true, NavigationActive(), false, navigationOwned ) ) { event->accept(); return true; }
        }
        return QWidget::event( event );
    }

    void enterEvent( QEnterEvent *pEvent ) override
    {
        ActivateHoveredView( *this, *m_pWorkspace );
        QWidget::enterEvent( pEvent );
        update(); // The hover outline appears.
    }

    void leaveEvent( QEvent *pEvent ) override
    {
        QWidget::leaveEvent( pEvent );
        ViewHover_Clear( m_hover, *this );
        if ( m_drag.kind == edit_drag_t::NONE && !NavigationActive() ) { unsetCursor(); }
        update(); // ...and goes with the pointer.
    }

    void paintEvent( QPaintEvent * ) override
    {
        if ( m_bFramePending ) { ApplyFrame(); }
        QPainter painter( this );
        const map_workspace_t &workspace = *m_pWorkspace;
        painter.fillRect( rect(), EditorStyle_Color( workspace.pGui->style, gui::STYLE_COLOR_VIEWPORT_3D ) );
        painter.setFont( gui::EditorStyle_Font( workspace.pGui->style, "viewport.labels" ) );
        Basis();
        DrawGround( painter );
        // Keep a small part of the frame's candidate budget for staged shapes.
        const bool constructing = workspace.editPreview.bActive &&
            ( workspace.tool == map_tool_t::BLOCK || workspace.editPreview.bClip );
        const usize previewReserve = constructing ? 4096u : 0u;
        usize committedBudget = kSurfaceGridMaxCandidates - previewReserve;
        if ( m_renderMode != map_render_mode_t::WIREFRAME ) { DrawFaces( painter, committedBudget ); }
        usize surfaceBudget = committedBudget + previewReserve;
        DrawConnections( painter );
        DrawObjects( painter );
        DrawCordon( painter );
        DrawBrushFace( painter );
        DrawSelection( painter );
        if ( DisplayFlag( workspace, "editor.viewport.perspective.show_axes" ) ) { DrawAxisTriad( painter ); }
        if ( ShowMetrics( workspace, true ) ) { DrawStatusStrip( painter ); }
        const auto project = [this]( math::vec3d_t p, QPointF &screen ) { QLineF line; if ( !Project( p, p, &line ) ) { return false; } screen = line.p1(); return true; };
        const auto segment = [this]( math::vec3d_t a, math::vec3d_t b, QLineF &line ) { return ProjectVisibleSegment( a, b, line ); };
        std::vector<math::vec3d_t> camera, clipped;
        const int previewFamilyLimit = static_cast<int>( std::min( static_cast<usize>( kSurfaceGridLinesPerFamily ),
            std::max( usize{ 1u }, surfaceBudget / ( 4u * std::max( usize{ 1u }, workspace.editPreviewWire.faces.nCount ) ) ) ) );
        const auto face = [this, &painter, &camera, &clipped, &surfaceBudget, previewFamilyLimit]( const map_wireframe_t &wire, const map_wire_face_t &face, QPolygonF &polygon ) {
            if ( !ProjectWireFace( wire, face, polygon, camera, clipped ) ) { return false; }
            const auto anchor = wire.points.pData[wire.faceIndices.pData[face.iFirstIndex]];
            if ( m_renderMode != map_render_mode_t::WIREFRAME && Dot( face.normal, Sub( m_position, anchor ) ) > 0.0 ) {
                // Construction remains a translucent guide. Its visible-side
                // lattice is drawn under the existing tint and exact edges.
                DrawWireSurfaceGrid( painter, wire, face, polygon,
                    EditorStyle_Color( m_pWorkspace->pGui->style, gui::STYLE_COLOR_VIEWPORT_3D ),
                    previewFamilyLimit, surfaceBudget, camera, clipped, nullptr, 0.45 );
            }
            return true;
        };
        DrawClipGuide( painter, workspace, project, segment, rect() );
        const auto handleProject = [this, &project, &workspace]( math::vec3d_t point, QPointF &screen ) {
            math::vec3d_t onPlane{};
            return project( point, screen ) && PlanePoint( screen, point,
                AxisVector( static_cast<int>( workspace.editPreview.clipGuide.extrusionAxis ) ), onPlane );
        };
        ClipGuideHandles( &painter, workspace, handleProject, segment, rect(),
            underMouse() && !NavigationActive() ? m_hover.point : QPointF( -10000, -10000 ) );
        const auto transformedFace = [this, &camera, &clipped, &workspace]( const map_wireframe_t &wire, const map_wire_face_t &face, QPolygonF &polygon ) {
            const auto *object = MapWireframe_FindObject( wire, face.id );
            if ( object == nullptr ) { return false; }
            const auto affine = MapWorkspace_TransformPreviewObjectAffine( workspace.editPreview.transform, *object );
            return ProjectWireFace( wire, face, polygon, camera, clipped, nullptr, &affine );
        };
        DrawMoveSource( painter, workspace, project, segment, rect(), true );
        DrawTransformPreview( painter, workspace, project, segment, transformedFace, rect(), true, m_renderMode == map_render_mode_t::WIREFRAME );
        DrawEditPreview( painter, workspace, project, segment, face, rect(), true, m_renderMode == map_render_mode_t::WIREFRAME );
        const QPointF pointer = underMouse() && !NavigationActive() ? m_hover.point : QPointF( -10000, -10000 );
        const u32 resizeMask = ResizeHandleMask();
        const auto hit = EditHandleAt( workspace, project, rect(), pointer, GizmoLength(), -1, -1, -1, resizeMask );
        ResizeHandles( &painter, workspace, project, rect(), pointer, -1, -1, &m_drag, 10.0, hit.resize, resizeMask, GizmoLength() );
        EditGizmo( &painter, workspace, project, GizmoLength(), pointer, -1, &m_drag, 10.0, hit.gizmo );
        DrawMarquee( painter, m_drag, workspace );
        DrawEditReadout( painter, workspace, m_drag, rect(), true );
        DrawSpeedFeedback( painter );
        DrawFocusBorder( painter, *this, workspace );
    }

    void wheelEvent( QWheelEvent *pEvent ) override
    {
        if ( m_cameraDrag.kind == map_camera_gesture_t::NONE &&
             ( AdjustBlockDepth( m_drag, *m_pWorkspace, *pEvent ) ||
               ( m_drag.kind == edit_drag_t::NONE && AdjustStagedBlockDepth( *m_pWorkspace, *pEvent ) ) ) ) { update(); pEvent->accept(); return; }
        const bool lookCaptured = m_cameraDrag.kind == map_camera_gesture_t::LOOK;
        const auto action = MapInput_CameraWheelAction( m_pWorkspace, pEvent, lookCaptured );
        if ( action == map_camera_wheel_action_t::NONE ) { pEvent->ignore(); return; }
        const f64 steps = pEvent->angleDelta().y() != 0 ? pEvent->angleDelta().y() / 120.0 : pEvent->pixelDelta().y() / 40.0;
        if ( lookCaptured ) {
            // Wheel belongs to the captured look context. It must not clear
            // held flight keys or leave a click candidate on button release.
            m_cameraDrag.contextClick = false; m_cameraDrag.moved = true;
        }
        if ( action == map_camera_wheel_action_t::RESERVED ) { pEvent->accept(); return; }
        if ( action == map_camera_wheel_action_t::SPEED_INCREASE || action == map_camera_wheel_action_t::SPEED_DECREASE ) {
            const auto speedAction = action == map_camera_wheel_action_t::SPEED_INCREASE
                ? map_camera_speed_action_t::INCREASE : map_camera_speed_action_t::DECREASE;
            ( void )MapWorkspace_ChangeCameraSpeed( m_pWorkspace, speedAction, std::abs( steps ) );
            pEvent->accept(); return;
        }
        CancelDrag( m_drag, *m_pWorkspace );
        if ( !lookCaptured ) { StopCameraDrag(); }
        if ( m_bFramePending ) { ApplyFrame(); }
        Basis();
        f64 units = steps * kCameraWheelStep * Setting( "editor.camera.zoom_sensitivity", 1.0 ) * SpeedFactor( pEvent->modifiers() );
        if ( DisplayFlag( *m_pWorkspace, "editor.camera.invert_wheel", CY_FALSE ) ) { units = -units; }
        const math::vec3d_t direction = DisplayFlag( *m_pWorkspace, "editor.camera.zoom_to_cursor" )
            ? ScreenDirection( pEvent->position() ) : m_forward;
        m_position = Add( m_position, Scale( direction, units ) );
        update(); pEvent->accept();
    }

    void mousePressEvent( QMouseEvent *pEvent ) override
    {
        setFocus( Qt::MouseFocusReason );
        // Resolve once at press. Modifier changes during a drag cannot switch
        // orbit into editing, or dolly into a context-menu right click.
        if ( m_cameraDrag.kind != map_camera_gesture_t::NONE ) { pEvent->accept(); return; }
        const auto gesture = MapInput_CameraDragGesture( m_pWorkspace, pEvent, m_bSpaceHeld );
        if ( gesture != map_camera_gesture_t::NONE ) { BeginCameraDrag( gesture, *pEvent ); pEvent->accept(); return; }
        m_bContextCandidate = pEvent->button() == Qt::RightButton && pEvent->modifiers() == Qt::NoModifier && !m_bSpaceHeld;
        m_contextPress = pEvent->position();
        if ( m_bContextCandidate ) { CancelDrag( m_drag, *m_pWorkspace ); }
        if ( pEvent->button() == Qt::LeftButton ) { BeginEdit( pEvent->position(), pEvent->modifiers() ); }
    }

    void mouseMoveEvent( QMouseEvent *pEvent ) override
    {
        if ( pEvent->buttons() == Qt::NoButton ) { ActivateHoveredView( *this, *m_pWorkspace ); }
        ViewHover_Move( m_hover, *this, *m_pWorkspace, *pEvent, NavigationActive() );
        if ( m_drag.kind == edit_drag_t::NONE && !NavigationActive() ) {
            const auto project = [this]( math::vec3d_t p, QPointF &screen ) { QLineF line; if ( !Project( p, p, &line ) ) { return false; } screen = line.p1(); return true; };
            const auto hit = EditHandleAt( *m_pWorkspace, project, rect(), pEvent->position(), GizmoLength(), -1, -1, -1, ResizeHandleMask() );
            if ( hit.resize >= 0 ) { setCursor( Qt::SizeAllCursor ); }
            else if ( hit.gizmo >= 0 ) { setCursor( GizmoCursor( *m_pWorkspace ) ); } else { unsetCursor(); }
        }
        if ( m_bContextCandidate && ( pEvent->position() - m_contextPress ).manhattanLength() > 3 ) { m_bContextCandidate = false; }
        if ( m_cameraDrag.kind != map_camera_gesture_t::NONE ) {
            if ( !pEvent->buttons().testFlag( m_cameraDrag.button ) ) { StopCameraDrag(); }
            else { UpdateCameraDrag( pEvent->position() ); }
            pEvent->accept(); return;
        }
        if ( TransformDrag( TransformTool( *m_pWorkspace ) ) != edit_drag_t::NONE || m_pWorkspace->tool == map_tool_t::CLIP || MapWorkspace_HasBlockPreview( m_pWorkspace ) ) { update(); }
        if ( m_drag.kind != edit_drag_t::NONE ) { UpdateEdit( pEvent->position(), pEvent->modifiers() ); }
    }

    void mouseReleaseEvent( QMouseEvent *pEvent ) override
    {
        if ( m_cameraDrag.kind != map_camera_gesture_t::NONE && pEvent->button() == m_cameraDrag.button ) {
            UpdateCameraDrag( pEvent->position() );
            const bool menu = m_cameraDrag.contextClick && !m_cameraDrag.moved;
            const bool keepHeld = m_cameraDrag.kind == map_camera_gesture_t::LOOK && m_cameraDrag.button == Qt::RightButton;
            StopCameraDrag( keepHeld );
            if ( menu ) {
                QCoreApplication::postEvent( this, new QContextMenuEvent( QContextMenuEvent::Other, pEvent->position().toPoint(),
                                                                          pEvent->globalPosition().toPoint() ) );
            }
            pEvent->accept(); return;
        }
        if ( pEvent->button() == Qt::RightButton && m_bContextCandidate ) {
            m_bContextCandidate = false;
            if ( ( pEvent->position() - m_contextPress ).manhattanLength() <= 3 ) {
                QCoreApplication::postEvent( this, new QContextMenuEvent( QContextMenuEvent::Other, pEvent->position().toPoint(), pEvent->globalPosition().toPoint() ) );
            }
            pEvent->accept(); return;
        }
        if ( pEvent->button() == Qt::LeftButton && m_drag.kind != edit_drag_t::NONE ) {
            if ( m_drag.kind == edit_drag_t::PUSH_PULL && m_pWorkspace->editPreview.bFacePushPull &&
                 m_pWorkspace->editPreview.status != map_status_t::OK ) {
                // Release cannot silently rebuild a failed move preview. A
                // later successful move is required before publication.
                CancelDrag( m_drag, *m_pWorkspace ); unsetCursor(); update(); return;
            }
            UpdateEdit( pEvent->position(), pEvent->modifiers() );
            if ( m_drag.kind == edit_drag_t::MARQUEE && QLineF( m_drag.start, m_drag.current ).length() >= 3 ) {
                SelectMarquee( *m_pWorkspace, m_drag, [this]( math::vec3d_t p, QPointF &screen ) { QLineF line; if ( !Project( p, p, &line ) ) { return false; } screen = line.p1(); return true; } );
            }
            if ( m_drag.kind == edit_drag_t::CLIP ) { m_drag = {}; }
            else if ( m_drag.kind == edit_drag_t::BOX || m_drag.blockResize ) { ( void )StageBlockDrag( m_drag, *m_pWorkspace ); }
            else { CommitDrag( m_drag, *m_pWorkspace ); }
            if ( !NavigationActive() ) { unsetCursor(); }
            update();
        }
    }

    void keyPressEvent( QKeyEvent *pEvent ) override
    {
        if ( pEvent->isAutoRepeat() && MapInput_ToolGestureKey( m_pWorkspace, pEvent, true ) == map_tool_gesture_key_t::CANCEL ) {
            pEvent->accept(); return;
        }
        // Observe pane-local eligible held keys before command dispatch. A
        // command still wins outside navigation, but its key can remain held
        // when the next look gesture starts without requiring another press.
        const bool navigationOwned = NavigationOwnsInput( pEvent->modifiers() );
        const bool navigationKey = SetMoveKey( pEvent, CY_TRUE );
        if ( pEvent->key() == Qt::Key_Space && !pEvent->isAutoRepeat() ) { m_bSpaceHeld = true; }
        if ( m_cameraDrag.kind != map_camera_gesture_t::NONE &&
             MapInput_ToolGestureKey( m_pWorkspace, pEvent, true ) == map_tool_gesture_key_t::CANCEL ) {
            StopCameraDrag(); update(); pEvent->accept(); return;
        }
        if ( m_drag.kind == edit_drag_t::NONE && m_pWorkspace->editPreview.bActive &&
             ( m_pWorkspace->editPreview.bClip || m_pWorkspace->editPreview.bStagedBlock ) ) {
            const auto gesture = MapInput_ToolGestureKey( m_pWorkspace, pEvent, true );
            if ( gesture != map_tool_gesture_key_t::NONE ) {
                if ( gesture == map_tool_gesture_key_t::CANCEL ) { MapWorkspace_ClearEditPreview( m_pWorkspace ); }
                else if ( m_pWorkspace->editPreview.bStagedBlock ) { ( void )MapWorkspace_CommitBlockPreview( m_pWorkspace ); }
                else { ( void )MapWorkspace_CommitClipPreview( m_pWorkspace ); }
                update(); pEvent->accept(); return;
            }
        }
        if ( m_drag.kind != edit_drag_t::NONE ) {
            const auto gesture = MapInput_ToolGestureKey( m_pWorkspace, pEvent, true );
            if ( gesture != map_tool_gesture_key_t::NONE ) {
                if ( gesture == map_tool_gesture_key_t::CANCEL ) { CancelDrag( m_drag, *m_pWorkspace ); if ( !NavigationActive() ) { unsetCursor(); } }
                else if ( m_drag.kind == edit_drag_t::MARQUEE ) {
                    if ( DragContextMatches( m_drag, *m_pWorkspace ) ) {
                        SelectMarquee( *m_pWorkspace, m_drag, [this]( math::vec3d_t p, QPointF &screen ) { QLineF line; if ( !Project( p, p, &line ) ) { return false; } screen = line.p1(); return true; } );
                    }
                    CancelDrag( m_drag, *m_pWorkspace );
                } else { CommitDrag( m_drag, *m_pWorkspace ); }
                update(); pEvent->accept(); return;
            }
        }
        if ( CancelIdleTool( *m_pWorkspace, pEvent, true ) ) { StopCameraDrag(); update(); pEvent->accept(); return; }
        if ( MapInput_DispatchKey( m_pWorkspace, pEvent, true, NavigationActive(), true, navigationOwned ) ) { pEvent->accept(); return; }
        if ( NavigationActive() && navigationKey ) { pEvent->accept(); return; }
        QWidget::keyPressEvent( pEvent );
    }

    void keyReleaseEvent( QKeyEvent *pEvent ) override
    {
        if ( pEvent->key() == Qt::Key_Space && !pEvent->isAutoRepeat() ) { m_bSpaceHeld = false; }
        if ( !SetMoveKey( pEvent, CY_FALSE ) ) { QWidget::keyReleaseEvent( pEvent ); }
    }

    void focusInEvent( QFocusEvent * ) override { update(); }
    void focusOutEvent( QFocusEvent * ) override
    {
        CancelDrag( m_drag, *m_pWorkspace );
        StopCameraDrag(); m_bSpaceHeld = false;
        update();
    }

    void resizeEvent( QResizeEvent * ) override
    {
        if ( m_drag.kind != edit_drag_t::NONE ) { CancelDrag( m_drag, *m_pWorkspace ); }
        StopCameraDrag();
        if ( m_bFramePending ) { ApplyFrame(); }
    }

private:
    void StopCameraDrag( bool keepHeld = false ) {
        m_cameraDrag = {};
        m_bContextCandidate = false;
        if ( !keepHeld ) { m_pressedNavigation.clear(); }
        m_moveKeys = 0u; m_bCommandNavigation = false; m_timer.stop();
        if ( keepHeld ) { RefreshMoveKeys( QGuiApplication::queryKeyboardModifiers() ); }
        unsetCursor();
    }

    void BeginCameraDrag( map_camera_gesture_t gesture, const QMouseEvent &event ) {
        CancelDrag( m_drag, *m_pWorkspace );
        // A released Clip guide belongs to the workspace, not this view's
        // active drag. Keep it available while navigating to inspect the cut.
        if ( m_bFramePending ) { ApplyFrame(); } Basis();
        m_cameraDrag = {};
        m_bContextCandidate = false;
        m_cameraDrag.kind = gesture; m_cameraDrag.button = event.button(); m_cameraDrag.start = event.position();
        m_cameraDrag.position = m_position; m_cameraDrag.yaw = m_yaw; m_cameraDrag.pitch = m_pitch;
        m_cameraDrag.right = m_right; m_cameraDrag.up = m_up; m_cameraDrag.forward = m_forward;
        // Selection bounds include point objects. Without a useful selection,
        // orbit the surface under the cursor, then the scene or view center.
        auto bounds = VisibleSelectionBounds( *m_pWorkspace );
        m_cameraDrag.pivot = MapBounds_Center( bounds );
        if ( !bounds.bHas ) {
            geometry::brush_raycast_hit_t hit{};
            ( void )Pick( event.position(), &hit );
            m_cameraDrag.pivot = hit.bHit ? hit.position : MapBounds_Center( m_pWorkspace->wire.bounds );
            bounds.bHas = hit.bHit || m_pWorkspace->wire.bounds.bHas;
        }
        f64 depth = Dot( Sub( m_cameraDrag.pivot, m_position ), m_forward );
        if ( !bounds.bHas || !std::isfinite( depth ) || depth <= kCameraNear * 2 ) {
            m_cameraDrag.pivot = Add( m_position, Scale( m_forward, 512 ) ); depth = 512;
        }
        m_cameraDrag.depth = depth;
        RefreshMoveKeys( event.modifiers() );
        m_cameraDrag.unitsPerPixel = depth / std::max( 1.0, FocalLength() );
        if ( gesture == map_camera_gesture_t::PAN ) { m_cameraDrag.unitsPerPixel *= Setting( "editor.camera.pan_sensitivity", 1.0 ); }
        m_cameraDrag.contextClick = gesture == map_camera_gesture_t::LOOK && event.button() == Qt::RightButton &&
                                    event.modifiers() == Qt::NoModifier && !m_bSpaceHeld;
        ViewHover_Clear( m_hover, *this );
        setCursor( gesture == map_camera_gesture_t::PAN ? Qt::ClosedHandCursor :
                   gesture == map_camera_gesture_t::DOLLY ? Qt::SizeVerCursor : Qt::BlankCursor );
        update();
    }

    void UpdateCameraDrag( QPointF screen ) {
        const auto &drag = m_cameraDrag;
        const QPointF pixels = screen - drag.start;
        if ( pixels.manhattanLength() > 3 ) { m_cameraDrag.moved = true; }
        const f64 sensitivity = Setting( "editor.camera.look_sensitivity", .2 );
        const f64 invert = DisplayFlag( *m_pWorkspace, "editor.camera.invert_y", CY_FALSE ) ? -1.0 : 1.0;
        if ( drag.kind == map_camera_gesture_t::LOOK || drag.kind == map_camera_gesture_t::ORBIT ) {
            m_yaw = drag.yaw - pixels.x() * sensitivity;
            m_pitch = std::clamp( drag.pitch - pixels.y() * sensitivity * invert, -89.0, 89.0 );
            Basis();
            if ( drag.kind == map_camera_gesture_t::ORBIT ) {
                // Rotate the entire camera frame around the captured pivot.
                // This preserves radius and the pivot's screen position even
                // when the selection was away from the center of the pane.
                const auto offset = Sub( drag.position, drag.pivot );
                m_position = Add( drag.pivot, Add( Scale( m_right, Dot( offset, drag.right ) ),
                    Add( Scale( m_up, Dot( offset, drag.up ) ), Scale( m_forward, Dot( offset, drag.forward ) ) ) ) );
            }
        } else if ( drag.kind == map_camera_gesture_t::PAN ) {
            m_position = Add( drag.position, Add( Scale( drag.right, -pixels.x() * drag.unitsPerPixel ),
                                                   Scale( drag.up, pixels.y() * drag.unitsPerPixel ) ) );
        } else if ( drag.kind == map_camera_gesture_t::DOLLY ) {
            const f64 units = std::clamp( ( pixels.x() - pixels.y() ) * drag.unitsPerPixel * Setting( "editor.camera.zoom_sensitivity", 1 ),
                                         -1.0e6, drag.depth - kCameraNear * 2 );
            m_position = Add( drag.position, Scale( drag.forward, units ) );
        }
        update();
    }

    static void OnSettingsChanged( void *pContext, string_view_t path ) noexcept
    {
        auto *pView = static_cast<map_camera_view_t *>( pContext );
        if ( path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( "editor.camera.move_speed" ) ) ) {
            const f64 speed = pView->Setting( "editor.camera.move_speed", 1000.0 );
            // Only successful changes to the effective setting announce a new
            // speed. Keep held flight keys and the captured look gesture intact.
            if ( speed != pView->m_moveSpeed ) {
                pView->m_moveSpeed = speed;
                pView->m_speedFeedbackTimer.start();
                pView->update();
            }
        }
        bool scaleChanged = false;
        if ( path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( "editor.viewport.gizmo_scale" ) ) ) {
            const f64 scale = GizmoScale( *pView->m_pWorkspace );
            scaleChanged = scale != pView->m_gizmoScale;
            pView->m_gizmoScale = scale;
        }
        if ( ( path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( "editor.camera.fov" ) ) || scaleChanged ) &&
             pView->m_drag.kind != edit_drag_t::NONE ) {
            CancelDrag( pView->m_drag, *pView->m_pWorkspace );
        }
        if ( path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( "editor.camera.fov" ) ) ) { pView->StopCameraDrag(); }
        OnViewDisplaySettingChanged( pContext, path );
    }

    struct brush_face_handle_t {
        math::vec3d_t origin{}, normal{};
        QLineF shaft{};
        f64 worldLength{ 0.0 };
    };

    bool FaceHandle( const map_wire_face_t &face, brush_face_handle_t &handle ) const {
        const map_wire_face_t *displayed = &face;
        const map_wireframe_t *wire = &m_pWorkspace->wire;
        if ( m_pWorkspace->editPreview.bFacePushPull ) {
            if ( !MapWorkspace_HasFacePreview( m_pWorkspace ) || m_pWorkspace->editPreview.status != map_status_t::OK ) { return false; }
            wire = &m_pWorkspace->editPreviewWire;
            displayed = nullptr;
            for ( usize i = 0; i < wire->faces.nCount; ++i ) {
                if ( wire->faces.pData[i].id == face.id && wire->faces.pData[i].sideId == face.sideId ) { displayed = &wire->faces.pData[i]; break; }
            }
        }
        if ( displayed == nullptr || displayed->nIndices < 3u ) { return false; }
        math::vec3d_t center{};
        u32 visibleVertices = 0u;
        for ( u32 i = 0; i < displayed->nIndices; ++i ) {
            const auto point = wire->points.pData[wire->faceIndices.pData[displayed->iFirstIndex + i]];
            center = Add( center, point ); QLineF projected;
            if ( Project( point, point, &projected ) ) { ++visibleVertices; }
        }
        if ( visibleVertices < 3u ) { return false; }
        handle.origin = Scale( center, 1.0 / displayed->nIndices );
        handle.normal = displayed->normal; handle.worldLength = GizmoLength();
        if ( !Project( handle.origin, Add( handle.origin, Scale( handle.normal, handle.worldLength ) ), &handle.shaft ) ) { return false; }
        return std::isfinite( handle.shaft.x1() ) && std::isfinite( handle.shaft.y1() ) &&
               std::isfinite( handle.shaft.x2() ) && std::isfinite( handle.shaft.y2() ) && handle.shaft.length() > 0.001;
    }

    bool ShowPushPullHandle() const {
        const auto &ws = *m_pWorkspace;
        return ( ws.tool == map_tool_t::EXTRUDE || ( ws.tool == map_tool_t::SELECT && ws.elementMode == map_element_mode_t::FACES ) ) &&
            ws.pDocument != nullptr && !ws.pDocument->bReadOnly && MapWorkspace_HasBrushFace( &ws );
    }

    bool PickPushPullHandle( QPointF screen, brush_face_handle_t &handle ) const {
        const auto &ws = *m_pWorkspace;
        if ( !ShowPushPullHandle() || !rect().contains( screen.toPoint() ) ) { return false; }
        for ( usize i = 0; i < ws.wire.faces.nCount; ++i ) {
            const auto &face = ws.wire.faces.pData[i];
            if ( face.id == ws.selectedBrushFaceObject && face.sideId == ws.selectedBrushFaceSide ) {
                // The shaft and compact arrowhead use the same eight-pixel
                // screen tolerance as the other transform gizmos.
                return FaceHandle( face, handle ) && ScreenSegmentDistance( screen, handle.shaft ) <= 8.0;
            }
        }
        return false;
    }

    void BeginPushPull( const brush_face_handle_t &handle ) {
        const auto ray = ScreenDirection( m_drag.start );
        // A plane containing the face normal preserves world distance even
        // when that axis changes perspective depth. Capture at the grab ray,
        // so picking anywhere along the shaft does not jump the face.
        m_drag.constraintNormal = Sub( ray, Scale( handle.normal, Dot( ray, handle.normal ) ) );
        const f64 length = std::sqrt( Dot( m_drag.constraintNormal, m_drag.constraintNormal ) );
        if ( !std::isfinite( length ) || length < 1e-8 ) { m_drag = {}; return; }
        m_drag.constraintNormal = Scale( m_drag.constraintNormal, 1.0 / length );
        if ( !PlanePoint( m_drag.start, handle.origin, m_drag.constraintNormal, m_drag.planeStart ) ) { m_drag = {}; return; }
        m_drag.kind = edit_drag_t::PUSH_PULL;
        // Idle measurements describe the face. The displacement preview still
        // describes the entire solid whose plane will be changed on commit.
        const auto *object = MapWireframe_FindObject( m_pWorkspace->wire, m_pWorkspace->selectedBrushFaceObject );
        m_drag.original = object != nullptr ? object->bounds : SelectionGeometryBounds( *m_pWorkspace );
        m_drag.pivot = handle.origin; m_drag.faceNormal = handle.normal;
        CaptureDragContext( m_drag, *m_pWorkspace );
    }

    void DrawBrushFace( QPainter &painter ) const {
        const auto &ws = *m_pWorkspace;
        const bool selected = MapWorkspace_HasBrushFace( &ws ) || MapWorkspace_HasMeshFace( &ws );
        const bool hovered = ws.elementMode == map_element_mode_t::FACES && m_hover.id != 0 && ( m_hoverFace.bHit || m_hoverMeshFace != 0 );
        if ( !selected && !hovered ) { return; }
        const bool facePreview = MapWorkspace_HasFacePreview( &ws ) && ws.editPreview.status == map_status_t::OK;
        std::vector<math::vec3d_t> camera, clipped;
        for ( usize i = 0; i < ws.wire.faces.nCount + ( facePreview ? ws.editPreviewWire.faces.nCount : 0u ); ++i ) {
            const bool candidate = i >= ws.wire.faces.nCount;
            const auto &wire = candidate ? ws.editPreviewWire : ws.wire;
            const auto &face = wire.faces.pData[candidate ? i - ws.wire.faces.nCount : i];
            const bool active = selected && ( ( face.sideId != 0 && face.id == ws.selectedBrushFaceObject && face.sideId == ws.selectedBrushFaceSide ) ||
                ( face.faceId != 0 && face.id == ws.selectedMeshFaceObject && face.faceId == ws.selectedMeshFaceId ) );
            const bool under = !candidate && hovered && face.id == m_hover.id && ( ( face.sideId != 0 && face.sideId == m_hoverFace.sideSourceId.value ) ||
                ( face.faceId != 0 && face.faceId == m_hoverMeshFace ) );
            if ( ( !active && !under ) || ( !candidate && EditPreviewTarget( ws, face.id ) ) ) { continue; }
            QPolygonF polygon;
            if ( !ProjectWireFace( wire, face, polygon, camera, clipped ) ) { continue; }
            QColor color = SlotColor( ws, active ? LINE_SELECTED : LINE_HOVER ), fill = color; fill.setAlpha( active ? 32 : 20 );
            painter.save(); painter.setPen( QPen( color, 2 ) ); painter.setBrush( fill ); painter.drawPolygon( polygon );
            if ( active && face.sideId != 0 && ShowPushPullHandle() && face.nIndices != 0 ) {
                brush_face_handle_t handle{};
                if ( FaceHandle( face, handle ) ) {
                    const auto &arrow = handle.shaft;
                    const QPointF direction = ( arrow.p2() - arrow.p1() ) / arrow.length();
                    int axis = 0;
                    for ( int dimension = 1; dimension < 3; ++dimension ) {
                        if ( std::abs( Axis( handle.normal, dimension ) ) > std::abs( Axis( handle.normal, axis ) ) ) { axis = dimension; }
                    }
                    const bool engaged = m_drag.kind == edit_drag_t::PUSH_PULL ||
                        ( underMouse() && !NavigationActive() && ScreenSegmentDistance( m_hover.point, arrow ) <= 8 );
                    const QColor color = engaged ? SlotColor( ws, LINE_HOVER ) : Token( ws, axis == 0 ? "viewport.axis.x" : axis == 1 ? "viewport.axis.y" : "viewport.axis.z" );
                    painter.setPen( QPen( color, engaged ? 1.75 : 1.25 ) );
                    painter.drawLine( arrow );
                    DrawResizeCap( painter, arrow.p2() - direction * 5.0, direction, color, engaged );
                    if ( engaged ) {
                        painter.setPen( color );
                        painter.drawText( arrow.p2() + QPointF( 8, -8 ), QStringLiteral( "Push/Pull · grid snap" ) );
                    }
                }
            }
            painter.restore();
        }
    }

    f64 GizmoLength() const {
        const auto bounds = EditSelectionBounds( *m_pWorkspace );
        const auto center = m_pWorkspace->editPreview.bActive && m_pWorkspace->editPreview.transform.bResize ? MapBounds_Center( m_pWorkspace->editPreview.bounds ) :
            m_drag.kind == edit_drag_t::TRANSLATE ? Add( m_drag.pivot, m_drag.delta ) : GizmoPivot( *m_pWorkspace, bounds );
        return std::max( 1.0, Dot( Sub( center, m_position ), m_forward ) ) * 72.0 * GizmoScale( *m_pWorkspace ) / std::max( 1.0, FocalLength() );
    }
    bool PlanePoint( QPointF screen, math::vec3d_t pivot, math::vec3d_t normal, math::vec3d_t &out ) const {
        const auto direction = ScreenDirection( screen ); const f64 denom = Dot( direction, normal );
        if ( std::abs( denom ) < 1e-5 ) { return false; }
        const f64 distance = Dot( Sub( pivot, m_position ), normal ) / denom;
        // Projection clips by forward depth, not radial ray distance. The
        // same near plane must bound off-center construction rays and picking.
        const f64 forward = Dot( direction, m_forward );
        if ( !std::isfinite( distance ) || !std::isfinite( forward ) || forward <= 0 ||
             distance < kCameraNear / forward || distance > 1.0e6 ) { return false; }
        out = Add( m_position, Scale( direction, distance ) ); return true;
    }
    u32 ResizeHandleMask() const {
        const auto &ws = *m_pWorkspace;
        const auto bounds = MapWorkspace_HasBlockPreview( &ws ) || ( ws.editPreview.bActive && ws.editPreview.transform.bResize ) ?
            ws.editPreview.bounds : SelectionGeometryBounds( ws );
        if ( !bounds.bHas ) { return 0u; }
        u32 mask = 0u;
        for ( int handle = 0; handle < 6; ++handle ) {
            const auto point = ResizePoint( bounds, ResizeSides( handle ) ); QLineF projected;
            if ( !Project( point, point, &projected ) ) { continue; }
            const auto axis = AxisVector( handle / 2 ), ray = ScreenDirection( projected.p1() );
            auto normal = Sub( ray, Scale( axis, Dot( ray, axis ) ) );
            const f64 length = std::sqrt( Dot( normal, normal ) );
            if ( !std::isfinite( length ) || length < 1e-8 ) { continue; }
            normal = Scale( normal, 1.0 / length ); math::vec3d_t grab{};
            // A tiny finite projection can still be too parallel for the
            // actual drag solver. Use its capture rule for drawing, hover and
            // pressing instead of advertising a dot that cannot be grabbed.
            if ( PlanePoint( projected.p1(), point, normal, grab ) ) { mask |= 1u << handle; }
        }
        return mask;
    }
    void BeginEdit( QPointF screen, Qt::KeyboardModifiers modifiers ) {
        if ( m_pWorkspace->tool == map_tool_t::NONE || m_pWorkspace->tool == map_tool_t::CAMERA ) { return; }
        if ( MeshEdgePickingActive( *m_pWorkspace ) ) {
            map_mesh_edge_hit_t hit{}; bool complete = true;
            ( void )PickMeshEdge( screen, &hit, &complete );
            if ( complete ) { SelectViewMeshEdge( *m_pWorkspace, hit, modifiers ); }
            return;
        }
        if ( MeshVertexPickingActive( *m_pWorkspace ) ) {
            map_mesh_vertex_hit_t hit{}; bool complete = true;
            ( void )PickMeshVertex( screen, &hit, &complete );
            if ( complete ) { SelectViewMeshVertex( *m_pWorkspace, hit, modifiers ); }
            return;
        }
        if ( m_bFramePending ) { ApplyFrame(); } Basis();
        const auto project = [this]( math::vec3d_t p, QPointF &out ) { QLineF line; if ( !Project( p, p, &line ) ) { return false; } out = line.p1(); return true; };
        const auto segment = [this]( math::vec3d_t a, math::vec3d_t b, QLineF &line ) { return ProjectVisibleSegment( a, b, line ); };
        const auto guide = m_pWorkspace->editPreview.clipGuide;
        const auto handleProject = [this, &project, &guide]( math::vec3d_t point, QPointF &out ) {
            math::vec3d_t hit{};
            return project( point, out ) && PlanePoint( out, point, AxisVector( static_cast<int>( guide.extrusionAxis ) ), hit );
        };
        const int endpoint = MapWorkspace_CanEditBrushSelection( m_pWorkspace ) ?
            ClipGuideHandles( nullptr, *m_pWorkspace, handleProject, segment, rect(), screen ) : -1;
        CancelDrag( m_drag, *m_pWorkspace );
        m_drag.start = m_drag.current = screen; m_drag.modifiers = modifiers;
        if ( endpoint >= 0 && PlanePoint( screen, guide.points[0], AxisVector( static_cast<int>( guide.extrusionAxis ) ), m_drag.planeStart ) ) {
            m_drag.kind = edit_drag_t::CLIP; m_drag.clipEndpoint = endpoint; m_drag.clipGuide = guide;
            m_drag.depthAxis = static_cast<int>( guide.extrusionAxis ); m_drag.pivot = guide.points[0];
            CaptureDragContext( m_drag, *m_pWorkspace );
            MapWorkspace_SetClipGuide( m_pWorkspace, guide ); return;
        }
        const bool stagedBlock = MapWorkspace_HasBlockPreview( m_pWorkspace );
        if ( !stagedBlock ) { MapWorkspace_ClearEditPreview( m_pWorkspace ); }
        if ( m_pWorkspace->tool == map_tool_t::CLIP ) {
            m_drag.depthAxis = static_cast<int>( MapWorkspace_ClipAxis( m_pWorkspace ) );
            m_drag.pivot = MapBounds_Center( SelectionGeometryBounds( *m_pWorkspace ) );
            if ( MapWorkspace_CanEditBrushSelection( m_pWorkspace ) &&
                 PlanePoint( screen, m_drag.pivot, AxisVector( m_drag.depthAxis ), m_drag.planeStart ) ) {
                m_drag.kind = edit_drag_t::CLIP;
                CaptureDragContext( m_drag, *m_pWorkspace );
            } return;
        }
        brush_face_handle_t faceHandle{};
        if ( PickPushPullHandle( screen, faceHandle ) ) { BeginPushPull( faceHandle ); return; }
        geometry::brush_raycast_hit_t faceHit{};
        u64 meshFace = 0;
        const u64 id = Pick( screen, &faceHit, &meshFace );
        const bool faceTool = m_pWorkspace->tool == map_tool_t::EXTRUDE || m_pWorkspace->tool == map_tool_t::TEXTURE;
        const bool selectFace = m_pWorkspace->tool == map_tool_t::SELECT && m_pWorkspace->elementMode == map_element_mode_t::FACES;
        if ( ( faceTool || selectFace ) && meshFace != 0 ) {
            if ( BypassSnap( modifiers ) && id == m_pWorkspace->selectedMeshFaceObject && meshFace == m_pWorkspace->selectedMeshFaceId ) { MapWorkspace_ClearMeshFace( m_pWorkspace ); }
            else { MapWorkspace_SelectMeshFace( m_pWorkspace, id, meshFace ); }
            return;
        }
        if ( ( faceTool || selectFace ) && faceHit.bHit ) {
            MapWorkspace_SelectBrushFace( m_pWorkspace, id, faceHit.sideSourceId.value );
            if ( m_pWorkspace->tool == map_tool_t::EXTRUDE ) {
                faceHandle.origin = faceHit.position; faceHandle.normal = faceHit.normal; faceHandle.worldLength = GizmoLength();
                if ( Project( faceHandle.origin, Add( faceHandle.origin, Scale( faceHandle.normal, faceHandle.worldLength ) ), &faceHandle.shaft ) && faceHandle.shaft.length() > 0.001 ) {
                    BeginPushPull( faceHandle );
                }
            }
            return;
        }
        if ( faceTool || selectFace ) {
            if ( !BypassSnap( modifiers ) ) { MapWorkspace_Select( m_pWorkspace, 0, MAP_SELECT_REPLACE ); }
            return;
        }
        const f64 length = GizmoLength();
        const auto hit = EditHandleAt( *m_pWorkspace, project, rect(), screen, length, -1, -1, -1, ResizeHandleMask() );
        const int resize = hit.resize;
        if ( resize >= 0 && BeginResize( m_drag, *m_pWorkspace, ResizeSides( resize ) ) ) {
            m_drag.axis = resize / 2;
            const auto point = ResizePoint( m_drag.original, m_drag.resizeSides );
            QPointF handlePoint;
            if ( !project( point, handlePoint ) ) { m_drag = {}; return; }
            m_drag.resizeScreenOffset = screen - handlePoint;
            const auto axis = AxisVector( m_drag.axis ), ray = ScreenDirection( handlePoint );
            m_drag.faceNormal = Sub( ray, Scale( axis, Dot( ray, axis ) ) );
            const f64 normalLength = std::sqrt( Dot( m_drag.faceNormal, m_drag.faceNormal ) );
            if ( normalLength < 1e-8 ) { m_drag = {}; return; }
            m_drag.faceNormal = Scale( m_drag.faceNormal, 1.0 / normalLength );
            // The separated visual dot maps back to its actual world boundary
            // ray. Keep that offset fixed throughout the drag: the cursor can
            // grab a tiny control without moving the boundary or pushing the
            // press intersection behind the camera's near plane. Capture the
            // whole pickup offset, including the hit tolerance around the dot.
            if ( !PlanePoint( screen - m_drag.resizeScreenOffset, point, m_drag.faceNormal, m_drag.planeStart ) ) { m_drag = {}; return; }
            setCursor( Qt::SizeAllCursor );
            return;
        }
        if ( stagedBlock ) { return; }
        const int handle = hit.gizmo;
        if ( m_pWorkspace->tool == map_tool_t::SELECT ) {
            if ( handle < 0 ) {
                const bool bodyMove = SelectMovesObjects( *m_pWorkspace ) && id != 0 && modifiers == Qt::NoModifier;
                if ( !bodyMove || !HitSelected( *m_pWorkspace, id ) ) { SelectViewObject( *m_pWorkspace, id, modifiers ); }
                if ( id == 0 ) { m_drag.kind = edit_drag_t::MARQUEE; CaptureDragContext( m_drag, *m_pWorkspace ); }
                if ( !bodyMove ) { return; }
                m_drag.bodyMove = true;
            }
        }
        if ( m_pWorkspace->tool == map_tool_t::BLOCK ) {
            if ( PlanePoint( screen, {}, { 0, 0, 1 }, m_drag.planeStart ) ) {
                m_drag.kind = edit_drag_t::BOX; m_drag.depthAxis = 2;
                m_drag.depth = EditorSettings_Real( &m_pWorkspace->pGui->settings, "editor.map.block_depth", 64 );
                CaptureDragContext( m_drag, *m_pWorkspace );
            } return;
        }
        if ( handle < 0 && id == 0 ) { return; }
        if ( handle < 0 && id != 0 && !HitSelected( *m_pWorkspace, id ) ) { SelectViewObject( *m_pWorkspace, id, Qt::NoModifier ); }
        if ( !CanTransform( *m_pWorkspace ) ) { return; }
        m_drag.kind = TransformDrag( TransformTool( *m_pWorkspace ) );
        m_drag.axis = handle >= 0 && handle < 3 ? handle : -1;
        m_drag.planeAxis = handle >= 3 && handle < 6 ? handle - 3 : -1;
        m_drag.uniformScale = handle == 6 && m_drag.kind == edit_drag_t::SCALE;
        m_drag.original = EditSelectionBounds( *m_pWorkspace ); m_drag.pivot = MapBounds_Center( m_drag.original );
        project( m_drag.pivot, m_drag.pivotScreen );
        m_drag.unitsPerPixel = std::max( 1.0, Dot( Sub( m_drag.pivot, m_position ), m_forward ) ) / std::max( 1.0, FocalLength() );
        if ( m_drag.axis >= 0 && ( m_drag.kind == edit_drag_t::TRANSLATE || m_drag.kind == edit_drag_t::SCALE ) ) {
            // Capture an axis-containing plane at the actual grab ray. A
            // projected shaft ratio is only a local approximation when the
            // axis changes camera depth; it cannot preserve a long drag.
            const auto axis = AxisVector( m_drag.axis ), ray = ScreenDirection( screen );
            m_drag.faceNormal = Sub( ray, Scale( axis, Dot( ray, axis ) ) );
            const f64 normalLength = std::sqrt( Dot( m_drag.faceNormal, m_drag.faceNormal ) );
            if ( !std::isfinite( normalLength ) || normalLength < 1e-8 ) { m_drag = {}; return; }
            m_drag.faceNormal = Scale( m_drag.faceNormal, 1.0 / normalLength );
            if ( !PlanePoint( screen, m_drag.pivot, m_drag.faceNormal, m_drag.planeStart ) ) { m_drag = {}; return; }
        } else if ( m_drag.kind == edit_drag_t::ROTATE && m_drag.axis >= 0 ) {
            if ( !PlanePoint( screen, m_drag.pivot, AxisVector( m_drag.axis ), m_drag.planeStart ) ) { m_drag.kind = edit_drag_t::NONE; }
        } else if ( !PlanePoint( screen, m_drag.pivot, m_drag.planeAxis >= 0 ? AxisVector( m_drag.planeAxis ) : m_forward, m_drag.planeStart ) ) {
            m_drag.kind = edit_drag_t::NONE;
        }
        CaptureDragContext( m_drag, *m_pWorkspace );
    }
    void UpdateEdit( QPointF screen, Qt::KeyboardModifiers modifiers ) {
        if ( m_drag.kind == edit_drag_t::NONE ) { return; }
        if ( !DragContextMatches( m_drag, *m_pWorkspace ) ) { CancelDrag( m_drag, *m_pWorkspace ); update(); return; }
        const bool bypass = BypassSnap( modifiers );
        m_drag.current = screen;
        if ( m_drag.kind == edit_drag_t::MARQUEE ) { update(); return; }
        if ( AwaitTransformMotion( m_drag, *m_pWorkspace ) ) { update(); return; }
        if ( m_drag.kind == edit_drag_t::RESIZE ) {
            math::vec3d_t point{};
            if ( PlanePoint( screen - m_drag.resizeScreenOffset, m_drag.planeStart, m_drag.faceNormal, point ) ) {
                const auto axis = AxisVector( m_drag.axis );
                UpdateResize( m_drag, *m_pWorkspace, Scale( axis, Dot( Sub( point, m_drag.planeStart ), axis ) ), bypass );
            } else {
                // Both collective factors and individual travel must lose
                // their last valid value when the visible preview is cleared.
                // Returning to a valid ray can repair this captured gesture.
                m_drag.factors = { 1, 1, 1 }; m_drag.delta = {};
                if ( m_drag.blockResize ) { ( void )MapWorkspace_SetBlockPreviewBounds( m_pWorkspace, {} ); }
                else { MapWorkspace_ClearEditPreview( m_pWorkspace ); }
            } return;
        }
        if ( m_drag.kind == edit_drag_t::BOX ) {
            math::vec3d_t point{}; if ( PlanePoint( screen, {}, { 0, 0, 1 }, point ) ) {
                const f64 depth = MapWorkspace_PrimitiveDefaults( m_pWorkspace, {} ).kind == map_primitive_kind_t::QUAD ? 0.0 : m_drag.depth;
                MapWorkspace_SetEditPreview( m_pWorkspace, BoxFootprint( m_drag.planeStart, point, 0, 1, *m_pWorkspace, bypass, depth ), 2u );
            } else { MapWorkspace_ClearEditPreview( m_pWorkspace ); } return;
        }
        if ( m_drag.kind == edit_drag_t::CLIP ) {
            math::vec3d_t point{};
            if ( PlanePoint( screen, m_drag.pivot, AxisVector( m_drag.depthAxis ), point ) ) {
                UpdateClipGuide( m_drag, *m_pWorkspace, point, bypass );
            } else {
                // Never confirm the last valid ghost after crossing a parallel
                // or behind-camera construction ray. Returning can repair it.
                MapWorkspace_SetClipGuide( m_pWorkspace, {} );
            } return;
        }
        if ( m_drag.kind == edit_drag_t::PUSH_PULL ) {
            math::vec3d_t point{};
            if ( !PlanePoint( screen, m_drag.pivot, m_drag.constraintNormal, point ) ) {
                CancelDrag( m_drag, *m_pWorkspace ); update(); return;
            }
            m_drag.distance = Snap( Dot( Sub( point, m_drag.planeStart ), m_drag.faceNormal ), m_pWorkspace->bSnapToGrid && !bypass ? m_pWorkspace->gridSize : 0 );
            if ( std::abs( m_drag.distance ) < 1e-8 || QLineF( screen, m_drag.start ).length() < 3 ) {
                m_drag.distance = 0; MapWorkspace_ClearEditPreview( m_pWorkspace );
            } else { MapWorkspace_SetFacePreview( m_pWorkspace, m_drag.distance ); }
            return;
        }
        math::vec3d_t delta{};
        if ( m_drag.axis >= 0 && ( m_drag.kind == edit_drag_t::TRANSLATE || m_drag.kind == edit_drag_t::SCALE ) ) {
            math::vec3d_t point{};
            if ( !PlanePoint( screen, m_drag.pivot, m_drag.faceNormal, point ) ) {
                // An invalid ray must not leave the last valid ghost ready
                // for a later release or Confirm to publish.
                CancelDrag( m_drag, *m_pWorkspace ); update(); return;
            }
            const auto axis = AxisVector( m_drag.axis );
            delta = Scale( axis, Dot( Sub( point, m_drag.planeStart ), axis ) );
        } else if ( m_drag.axis < 0 ) {
            math::vec3d_t point{};
            if ( !PlanePoint( screen, m_drag.pivot, m_drag.planeAxis >= 0 ? AxisVector( m_drag.planeAxis ) : m_forward, point ) ) {
                CancelDrag( m_drag, *m_pWorkspace ); update(); return;
            }
            delta = Sub( point, m_drag.planeStart );
        }
        delta = SnapMove( delta, *m_pWorkspace, bypass );
        if ( m_drag.planeAxis >= 0 ) { SetAxis( delta, m_drag.planeAxis, 0 ); }
        if ( m_drag.kind == edit_drag_t::TRANSLATE ) { m_drag.delta = delta; }
        if ( m_drag.kind == edit_drag_t::SCALE ) {
            if ( m_drag.axis >= 0 ) {
                const int axis = m_drag.axis;
                const f64 half = std::max( 1.0, ( Axis( m_drag.original.box.maximum, axis ) - Axis( m_drag.original.box.minimum, axis ) ) * 0.5 );
                SetAxis( m_drag.factors, axis, std::max( 0.01, Snap( 1 + Axis( delta, axis ) / half, bypass ? 0 : m_pWorkspace->scaleSnap ) ) );
            } else if ( m_drag.planeAxis >= 0 ) {
                for ( int axis = 0; axis < 3; ++axis ) {
                    if ( axis == m_drag.planeAxis ) { continue; }
                    const f64 half = std::max( 1.0, ( Axis( m_drag.original.box.maximum, axis ) - Axis( m_drag.original.box.minimum, axis ) ) * 0.5 );
                    SetAxis( m_drag.factors, axis, std::max( 0.01, Snap( 1 + Axis( delta, axis ) / half, bypass ? 0 : m_pWorkspace->scaleSnap ) ) );
                }
            } else {
                const QPointF pixels = screen - m_drag.start;
                const f64 f = std::max( 0.01, Snap( 1 + ( pixels.x() - pixels.y() ) / 100.0, bypass ? 0 : m_pWorkspace->scaleSnap ) ); m_drag.factors = { f, f, f };
            }
        }
        if ( m_drag.kind == edit_drag_t::ROTATE ) {
            const int axis = m_drag.axis >= 0 ? m_drag.axis : 2;
            f64 angle = 0;
            math::vec3d_t point{};
            if ( m_drag.axis >= 0 && PlanePoint( screen, m_drag.pivot, AxisVector( axis ), point ) ) {
                const auto a = Sub( m_drag.planeStart, m_drag.pivot ), b = Sub( point, m_drag.pivot );
                const math::vec3d_t cross{ a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x };
                angle = std::atan2( Axis( cross, axis ), Dot( a, b ) ) / kDegToRad;
            } else if ( m_drag.axis < 0 ) { angle = ( screen.x() - m_drag.start.x() ) * 0.5; }
            SetAxis( m_drag.degrees, axis, Snap( angle, bypass ? 0 : m_pWorkspace->angleSnap ) );
        }
        MapWorkspace_SetTransformPreview( m_pWorkspace, TransformDescriptor( m_drag ) );
    }

    static constexpr f64 kDegToRad = 3.14159265358979323846 / 180.0;

    enum move_key_t : u32 { KEY_FORWARD = 1u, KEY_BACK = 2u, KEY_LEFT = 4u, KEY_RIGHT = 8u, KEY_UP = 16u, KEY_DOWN = 32u };

    static void OnChanged( void *pContext, u32 changes ) noexcept
    {
        auto *pView = static_cast<map_camera_view_t *>( pContext );
        if ( pView->m_inputTool != pView->m_pWorkspace->tool ) {
            const bool neutralTransition = pView->m_inputTool == map_tool_t::NONE || pView->m_pWorkspace->tool == map_tool_t::NONE;
            pView->m_inputTool = pView->m_pWorkspace->tool;
            if ( neutralTransition ) { pView->StopCameraDrag(); }
            else if ( pView->m_cameraDrag.kind != map_camera_gesture_t::NONE ) { pView->StopCameraDrag( true ); }
            // Authoring commands may use a held flight key before a look
            // gesture begins. Keep that existing eligibility between editing
            // tools; only crossing the neutral boundary clears held presses.
            ViewHover_Clear( pView->m_hover, *pView );
        }
        if ( pView->m_inputMode != pView->m_pWorkspace->elementMode ) {
            pView->m_inputMode = pView->m_pWorkspace->elementMode;
            ViewHover_Clear( pView->m_hover, *pView ); pView->unsetCursor();
        }
        if ( ( changes & ( MAP_CHANGE_DOCUMENT | MAP_CHANGE_VIEW ) ) != 0u ) {
            pView->m_hoverEdge = {}; pView->m_hoverVertex = {};
            ViewHover_Clear( pView->m_hover, *pView );
        }
        if ( pView->m_drag.kind != edit_drag_t::NONE && ( ( changes & MAP_CHANGE_DOCUMENT ) != 0 || !DragContextMatches( pView->m_drag, *pView->m_pWorkspace ) ) ) {
            CancelDrag( pView->m_drag, *pView->m_pWorkspace );
            if ( !pView->NavigationActive() ) { pView->unsetCursor(); }
        }
        if ( ( changes & MAP_CHANGE_DOCUMENT ) != 0u ) { pView->StopCameraDrag(); pView->m_materialColors.clear(); }
        if ( ( changes & MAP_CHANGE_FRAME ) != 0u && pView->m_pWorkspace->frameTarget != map_frame_target_t::ORTHOGRAPHIC ) { pView->FrameBounds( pView->m_pWorkspace->frameBounds ); }
        if ( ( changes & ( MAP_CHANGE_DOCUMENT | MAP_CHANGE_SELECTION | MAP_CHANGE_VIEW | MAP_CHANGE_FRAME ) ) != 0u ) { pView->update(); }
    }

    // Defer until the view has its real dimensions. A fixed sphere distance
    // fails for short/wide panes because horizontal FOV is not vertical FOV.
    void FrameBounds( const map_bounds_t &bounds ) noexcept
    {
        if ( !bounds.bHas ) { return; }
        StopCameraDrag();
        if ( m_drag.kind != edit_drag_t::NONE ) { CancelDrag( m_drag, *m_pWorkspace ); }
        m_frameBounds = bounds;
        m_bFramePending = CY_TRUE;
        if ( isVisible() ) { ApplyFrame(); }
        update();
    }

    void ApplyFrame() noexcept
    {
        if ( width() <= 1 || height() <= 1 || !m_frameBounds.bHas ) { return; }
        const math::vec3d_t center = MapBounds_Center( m_frameBounds );
        // Equal-axis overview: horizontal floors and vertical walls remain
        // readable, rather than starting at floor level looking edge-on.
        m_yaw = 45.0;
        m_pitch = -35.264389682754654;
        Basis();
        math::vec3d_t corners[8];
        BoundsCorners( m_frameBounds, corners );
        const f64 horizontalScale = FocalLength() / ( width() * 0.5 );
        const f64 verticalScale = FocalLength() / ( height() * 0.5 );
        f64 distance = 64.0;
        const f64 margin = FrameMargin( *m_pWorkspace );
        for ( const math::vec3d_t &corner : corners ) {
            const math::vec3d_t relative = Sub( corner, center );
            const f64 depth = Dot( relative, m_forward );
            distance = std::max( distance, std::abs( Dot( relative, m_right ) ) * horizontalScale * margin - depth );
            distance = std::max( distance, std::abs( Dot( relative, m_up ) ) * verticalScale * margin - depth );
            distance = std::max( distance, kCameraNear * 2.0 - depth );
        }
        m_position = Sub( center, Scale( m_forward, distance ) );
        m_bFramePending = CY_FALSE;
    }

    bool_t SetMoveKey( QKeyEvent *pEvent, bool_t bDown ) noexcept
    {
        const int code = pEvent->key();
        if ( !bDown ) {
            if ( pEvent->isAutoRepeat() ) { return m_pressedNavigation.contains( code ) ? CY_TRUE : CY_FALSE; }
            if ( !m_pressedNavigation.remove( code ) ) {
                // Releasing an unbound command modifier can make an observed
                // direction eligible again. Refresh without synthesizing a press.
                RefreshMoveKeys( pEvent->modifiers() ); return CY_FALSE;
            }
        } else {
            const u32 mask = MapInput_NavigationKeyMask( m_pWorkspace, pEvent );
            if ( mask == 0 ) { return CY_FALSE; }
            // Auto-repeat cannot resurrect a press cleared by focus loss,
            // Escape, document replacement, or another navigation reset.
            if ( pEvent->isAutoRepeat() && !m_pressedNavigation.contains( code ) ) { return CY_TRUE; }
            m_pressedNavigation.insert( code );
        }
        RefreshMoveKeys( pEvent->modifiers() );
        return CY_TRUE;
    }

    void RefreshMoveKeys( Qt::KeyboardModifiers modifiers ) noexcept
    {
        // Resolve again rather than retaining masks from an old tool/keymap.
        // Only keys whose presses this pane observed are eligible; moving the
        // mouse out of a text field cannot synthesize a physical key press.
        m_moveKeys = 0; m_bCommandNavigation = false;
        for ( const int code : m_pressedNavigation ) {
            QKeyEvent event( QEvent::KeyPress, code, modifiers );
            const u32 mask = MapInput_NavigationKeyMask( m_pWorkspace, &event );
            if ( mask == 0u ) { continue; }
            m_moveKeys |= mask;
            if ( ( mask & 63u ) != 0u && ( modifiers & ( Qt::ControlModifier | Qt::MetaModifier ) ) != 0 ) { m_bCommandNavigation = true; }
        }
        const bool flight = NavigationActive() && ( m_cameraDrag.kind == map_camera_gesture_t::NONE || m_cameraDrag.kind == map_camera_gesture_t::LOOK );
        if ( flight && ( m_moveKeys & 63u ) != 0u ) {
            if ( !m_timer.isActive() ) { m_clock.start(); m_timer.start(); }
        } else { m_timer.stop(); }
    }

    void Tick() noexcept
    {
        // Real elapsed time, so speed does not depend on timer accuracy.
        const f64 seconds = std::min( m_clock.restart() / 1000.0, 0.1 );
        const Qt::KeyboardModifiers modifiers = QGuiApplication::queryKeyboardModifiers();
        m_position = Add( m_position, Scale( NavigationVelocity( modifiers ), seconds ) );
        update();
    }

public:
    math::vec3d_t NavigationVelocity( Qt::KeyboardModifiers modifiers ) noexcept
    {
        RefreshMoveKeys( modifiers );
        if ( ( m_cameraDrag.kind != map_camera_gesture_t::NONE && m_cameraDrag.kind != map_camera_gesture_t::LOOK ) ||
             !NavigationActive() || ( ( modifiers & ( Qt::ControlModifier | Qt::MetaModifier ) ) != 0 && !m_bCommandNavigation ) ) { return {}; }
        Basis();
        math::vec3d_t move{};
        if ( ( m_moveKeys & KEY_FORWARD ) != 0u ) { move = Add( move, m_forward ); }
        if ( ( m_moveKeys & KEY_BACK ) != 0u ) { move = Sub( move, m_forward ); }
        if ( ( m_moveKeys & KEY_RIGHT ) != 0u ) { move = Add( move, m_right ); }
        if ( ( m_moveKeys & KEY_LEFT ) != 0u ) { move = Sub( move, m_right ); }
        if ( ( m_moveKeys & KEY_UP ) != 0u ) { move.z += 1.0; }
        if ( ( m_moveKeys & KEY_DOWN ) != 0u ) { move.z -= 1.0; }
        const f64 length = std::sqrt( Dot( move, move ) );
        const f64 speed = Setting( "editor.camera.move_speed", 1000.0 ) * SpeedFactor( modifiers );
        return length > 1e-12 ? Scale( move, speed / length ) : math::vec3d_t{};
    }

private:

    f64 Setting( const char *key, f64 fallback ) const noexcept
    {
        return EditorSettings_Real( &m_pWorkspace->pGui->settings, key, fallback );
    }

    f64 SpeedFactor( Qt::KeyboardModifiers modifiers ) const noexcept
    {
        u32 mask = m_moveKeys;
        for ( const auto key : { Qt::Key_Shift, Qt::Key_Alt } ) {
            const auto flag = key == Qt::Key_Shift ? Qt::ShiftModifier : Qt::AltModifier;
            if ( ( modifiers & flag ) != 0 ) { QKeyEvent event( QEvent::KeyPress, key, modifiers ); mask |= MapInput_NavigationKeyMask( m_pWorkspace, &event ); }
        }
        if ( mask & MAP_NAVIGATION_SLOW ) { return Setting( "editor.camera.slow_multiplier", 0.25 ); }
        return mask & MAP_NAVIGATION_FAST ? Setting( "editor.camera.fast_multiplier", 4.0 ) : 1.0;
    }

    f64 FocalLength() const noexcept
    {
        const f64 fov = std::clamp( Setting( "editor.camera.fov", 75.0 ), 20.0, 130.0 );
        return width() * 0.5 / std::tan( fov * kDegToRad * 0.5 );
    }

    math::vec3d_t ScreenDirection( QPointF screen ) const noexcept
    {
        const f64 focal = std::max( FocalLength(), 1.0 );
        const math::vec3d_t direction = Add( m_forward, Add( Scale( m_right, ( screen.x() - width() * 0.5 ) / focal ),
            Scale( m_up, ( height() * 0.5 - screen.y() ) / focal ) ) );
        return Scale( direction, 1.0 / std::sqrt( Dot( direction, direction ) ) );
    }

    // Depth belongs to the clicked control-net segment, never the whole
    // patch's bounds. Clip first, then use perspective-correct interpolation.
    bool PickSegment( QPointF screen, math::vec3d_t a, math::vec3d_t b, f64 &distance, QPointF *closest = nullptr ) const
    {
        f64 za = Dot( Sub( a, m_position ), m_forward );
        f64 zb = Dot( Sub( b, m_position ), m_forward );
        if ( za < kCameraNear && zb < kCameraNear ) { return false; }
        if ( za < kCameraNear || zb < kCameraNear ) {
            const math::vec3d_t clipped = Add( a, Scale( Sub( b, a ), ( kCameraNear - za ) / ( zb - za ) ) );
            if ( za < kCameraNear ) { a = clipped; za = kCameraNear; }
            else { b = clipped; zb = kCameraNear; }
        }
        QLineF line;
        if ( !Project( a, b, &line ) || ScreenSegmentDistance( screen, line ) > MAP_VIEW_PICK_PIXELS ) { return false; }
        const QPointF delta = line.p2() - line.p1();
        const f64 length2 = QPointF::dotProduct( delta, delta );
        const f64 t = length2 > 0.0 ? std::clamp( QPointF::dotProduct( screen - line.p1(), delta ) / length2, 0.0, 1.0 ) : 0.0;
        const f64 depth = 1.0 / ( ( 1.0 - t ) / za + t / zb );
        distance = depth / Dot( ScreenDirection( screen ), m_forward );
        if ( closest != nullptr ) { *closest = line.pointAt( t ); }
        return std::isfinite( distance );
    }

    bool EdgeSourceQueryable( const map_wire_object_t &object, QPointF screen ) const
    {
        namespace geo = geometry;
        const auto &map = *m_pWorkspace->pDocument;
        const geo::geometry_raycast_ray_t ray{ m_position, ScreenDirection( screen ) };
        geo::geometry_raycast_options_t options{};
        options.fMinimumDistance = kCameraNear / Dot( ray.direction, m_forward );
        if ( object.kind == map_wire_kind_t::BRUSH ) {
            const auto *brush = geo::GeometryDocument_FindBrush( &map.geometry, { object.id } );
            if ( brush == nullptr ) { return false; }
            geo::brush_boundary_t boundary{}; geo::brush_raycast_hit_t hit{};
            const bool valid = geo::BrushBoundary_Init( &boundary, map.pAllocator ) == geo::geometry_status_t::OK &&
                geo::BrushBoundary_TryReconstruct( &boundary, brush, map.geometryPolicy ) == geo::geometry_status_t::OK &&
                geo::BrushQueries_TryRaycast( brush, &boundary, ray, options, map.geometryPolicy, &hit ) == geo::geometry_status_t::OK;
            geo::BrushBoundary_Shutdown( &boundary );
            return valid;
        }
        const auto *mesh = geo::GeometryDocument_FindMesh( &map.geometry, { object.id } );
        usize bytes = 0;
        if ( mesh == nullptr || geo::MeshQueries_TryGetRaycastScratchSize( &mesh->mesh, map.geometryPolicy, &bytes ) != geo::geometry_status_t::OK ) { return false; }
        geo::geometry_scratch_t scratch{}; geo::geometry_scratch_desc_t desc{};
        desc.pFallbackAllocator = map.pAllocator; desc.cbCapacity = bytes; desc.cbBudget = 64u * CY_MIB;
        if ( geo::GeometryScratch_Acquire( &scratch, desc ) != geo::geometry_status_t::OK ) { return false; }
        geo::mesh_raycast_hit_t hit{};
        const bool valid = geo::MeshQueries_TryRaycast( &mesh->mesh, mesh->sourceId, ray, options, map.geometryPolicy, &scratch, &hit ) == geo::geometry_status_t::OK;
        const auto released = geo::GeometryScratch_Release( &scratch );
        return valid && released == geo::geometry_status_t::OK;
    }

    void Basis() noexcept
    {
        m_forward = Forward();
        const f64 yaw = m_yaw * kDegToRad;
        m_right = math::Vec3d_Make( std::sin( yaw ), -std::cos( yaw ), 0.0 );
        // up = right x forward
        m_up = math::Vec3d_Make( m_right.y * m_forward.z - m_right.z * m_forward.y, m_right.z * m_forward.x - m_right.x * m_forward.z,
                                 m_right.x * m_forward.y - m_right.y * m_forward.x );
    }

    // Clips a world segment against the near plane and projects it; false
    // when it is entirely behind the camera.
    bool_t Project( math::vec3d_t a, math::vec3d_t b, QLineF *pLineOut ) const noexcept
    {
        const math::vec3d_t ra = Sub( a, m_position );
        const math::vec3d_t rb = Sub( b, m_position );
        f64 za = Dot( ra, m_forward );
        f64 zb = Dot( rb, m_forward );
        if ( za < kCameraNear && zb < kCameraNear ) { return CY_FALSE; }
        f64 xa = Dot( ra, m_right );
        f64 ya = Dot( ra, m_up );
        f64 xb = Dot( rb, m_right );
        f64 yb = Dot( rb, m_up );
        if ( za < kCameraNear || zb < kCameraNear ) {
            const f64 t = ( kCameraNear - za ) / ( zb - za );
            const f64 xc = xa + ( xb - xa ) * t;
            const f64 yc = ya + ( yb - ya ) * t;
            if ( za < kCameraNear ) {
                xa = xc; ya = yc; za = kCameraNear;
            } else {
                xb = xc; yb = yc; zb = kCameraNear;
            }
        }
        // The same configured horizontal FOV is used by framing and picking.
        const f64 focal = FocalLength();
        const f64 cx = width() * 0.5;
        const f64 cy = height() * 0.5;
        *pLineOut = QLineF( cx + xa * focal / za, cy - ya * focal / za, cx + xb * focal / zb, cy - yb * focal / zb );
        return CY_TRUE;
    }

    // Near clipping alone can create enormous screen coordinates at the
    // horizon. Clip floor/axis segments to the viewport before painting;
    // reject non-finite intermediates rather than passing them to QPainter.
    bool ProjectVisibleSegment( math::vec3d_t a, math::vec3d_t b, QLineF &line ) const
    {
        if ( !Project( a, b, &line ) ) { return false; }
        const f64 x = line.x1(), y = line.y1();
        const f64 dx = line.x2() - x, dy = line.y2() - y;
        if ( !std::isfinite( x ) || !std::isfinite( y ) || !std::isfinite( dx ) || !std::isfinite( dy ) ) { return false; }
        f64 begin = 0.0, end = 1.0;
        const auto clip = [&]( f64 p, f64 q ) {
            if ( p == 0.0 ) { return q >= 0.0; }
            const f64 t = q / p;
            if ( p < 0.0 ) { begin = std::max( begin, t ); }
            else { end = std::min( end, t ); }
            return begin <= end;
        };
        if ( !clip( -dx, x ) || !clip( dx, width() - x ) || !clip( -dy, y ) || !clip( dy, height() - y ) ) { return false; }
        line = QLineF( x + begin * dx, y + begin * dy, x + end * dx, y + end * dy );
        return std::isfinite( line.x1() ) && std::isfinite( line.y1() ) && std::isfinite( line.x2() ) && std::isfinite( line.y2() );
    }

    f64 GroundGridOpacity( QPointF screen, QPointF normal, int axis, f64 spacing ) const noexcept
    {
        const f64 focal = FocalLength();
        if ( focal <= 0.0 ) { return 0.0; }
        const f64 x = ( screen.x() - width() * 0.5 ) / focal;
        const f64 y = ( height() * 0.5 - screen.y() ) / focal;
        const f64 rayZ = m_forward.z + m_right.z * x + m_up.z * y;
        if ( std::abs( rayZ ) < 1e-12 ) { return 0.0; }
        const f64 depth = -m_position.z / rayZ;
        if ( !std::isfinite( depth ) || depth < kCameraNear ) { return 0.0; }
        // Differentiate perspective projection across adjacent floor lines.
        // Perpendicular screen spacing, not world distance alone, determines
        // when lines converge into distracting subpixel moire at the horizon.
        const f64 dx = ( Axis( m_right, axis ) - x * Axis( m_forward, axis ) ) * focal / depth;
        const f64 dy = -( Axis( m_up, axis ) - y * Axis( m_forward, axis ) ) * focal / depth;
        const f64 pixels = std::abs( dx * normal.x() + dy * normal.y() ) * spacing;
        const f64 t = std::clamp( ( pixels - 1.25 ) / 2.75, 0.0, 1.0 );
        return t * t * ( 3.0 - 2.0 * t );
    }

    void DrawGround( QPainter &painter )
    {
        m_gridInfo = {};
        if ( !std::isfinite( m_position.x ) || !std::isfinite( m_position.y ) || !std::isfinite( m_position.z ) ) { return; }
        const map_workspace_t &workspace = *m_pWorkspace;
        const settings_registry_t &settings = workspace.pGui->settings;
        const gui::editor_style_t &style = workspace.pGui->style;
        constexpr int kRadius = 256;
        static_assert( MAP_CAMERA_GRID_MAX_SEGMENTS == ( kRadius * 2 + 1 ) * 2 );
        const f64 heightAboveFloor = std::max( std::abs( m_position.z ), workspace.gridSize * 4.0 );
        // One world-aligned lattice follows the camera. A broad finite
        // footprint and soft ends replace stacked grid levels, which made
        // coincident lines and a dense crosshatch near the horizon.
        f64 desired = heightAboveFloor * 4.0 / kRadius;
        if ( EditorSettings_Bool( &settings, "editor.grid.adaptive_3d", CY_TRUE ) ) {
            const f64 spacing = static_cast<f64>( EditorSettings_Integer( &settings, "editor.grid.min_spacing_px", MAP_VIEW_GRID_MIN_PIXELS ) );
            desired = std::max( desired, heightAboveFloor * ( spacing / std::max( FocalLength(), 1.0 ) ) * 2.0 );
        }
        f64 step = workspace.gridSize;
        if ( !std::isfinite( desired ) || !std::isfinite( step ) || step <= 0.0 ) { return; }
        // Sample whole authored cells with the smallest sufficient stride.
        // Power-of-two rounding hid changes such as Grid 16 -> 4 at normal
        // camera heights. Sampling never changes the modeling snap itself.
        step *= std::max( 1.0, std::ceil( desired / step ) );
        const f64 extent = step * kRadius;
        if ( !std::isfinite( step ) || !std::isfinite( extent ) || step <= 0.0 ) { return; }
        m_gridInfo.center = math::Vec3d_Make( m_position.x, m_position.y, 0.0 );
        m_gridInfo.step = step;
        m_gridInfo.halfExtent = extent;
        if ( DisplayFlag( workspace, "editor.grid.show_3d" ) ) {
            painter.save();
            painter.setRenderHint( QPainter::Antialiasing, true );
            const i64 majorInterval = DrawnMajorInterval( workspace, step );
            const f64 centerX = std::floor( m_position.x / step ) * step;
            const f64 centerY = std::floor( m_position.y / step ) * step;
            for ( int i = -kRadius; i <= kRadius; ++i ) {
                for ( int axis = 0; axis < 2; ++axis ) {
                    const f64 coordinate = ( axis == 0 ? centerX : centerY ) + i * step;
                    const bool major = GridLineMultiple( coordinate, step, majorInterval );
                    QColor color = EditorStyle_Color( style, major ? gui::STYLE_COLOR_GRID_MAJOR : gui::STYLE_COLOR_GRID_MINOR );
                    const math::vec3d_t a = axis == 0 ? math::Vec3d_Make( coordinate, centerY - extent, 0.0 )
                                                    : math::Vec3d_Make( centerX - extent, coordinate, 0.0 );
                    const math::vec3d_t b = axis == 0 ? math::Vec3d_Make( coordinate, centerY + extent, 0.0 )
                                                    : math::Vec3d_Make( centerX + extent, coordinate, 0.0 );
                    ++m_gridInfo.nCandidateSegments;
                    QLineF line;
                    if ( !ProjectVisibleSegment( a, b, line ) || line.length() < 0.5 ) { continue; }
                    const QPointF normal( -line.dy() / line.length(), line.dx() / line.length() );
                    const f64 spacing = step * ( major ? majorInterval : 1 );
                    QLinearGradient fade( line.p1(), line.p2() );
                    f64 maximumAlpha = 0.0;
                    constexpr int kFadeSamples = 16;
                    for ( int sample = 0; sample <= kFadeSamples; ++sample ) {
                        const f64 t = static_cast<f64>( sample ) / kFadeSamples;
                        const f64 endFade = std::min( { t / 0.12, ( 1.0 - t ) / 0.12, 1.0 } );
                        const f64 alpha = color.alphaF() * ( major ? 0.65 : 0.45 ) * endFade *
                            GroundGridOpacity( line.pointAt( t ), normal, axis, spacing );
                        QColor sampleColor = color;
                        sampleColor.setAlphaF( alpha );
                        fade.setColorAt( t, sampleColor );
                        maximumAlpha = std::max( maximumAlpha, alpha );
                    }
                    if ( maximumAlpha < 0.01 ) { continue; }
                    ++m_gridInfo.nVisibleSegments;
                    const QRectF box = QRectF( line.p1(), line.p2() ).normalized();
                    m_gridInfo.screenBounds = m_gridInfo.nVisibleSegments == 1u ? box : m_gridInfo.screenBounds.united( box );
                    painter.setPen( QPen( QBrush( fade ), 0.0 ) );
                    painter.drawLine( line );
                }
            }
            painter.restore();
        }
        if ( !DisplayFlag( workspace, "editor.viewport.perspective.center_axes" ) ) { return; }
        // World axes remain at x/y/z=0. They never move to the camera's
        // local grid center. The vertical blue axis is visible above/below
        // the floor wherever its world location enters the frustum.
        const gui::editor_style_color_t colors[]{ gui::STYLE_COLOR_AXIS_X, gui::STYLE_COLOR_AXIS_Y, gui::STYLE_COLOR_AXIS_Z };
        for ( u32 axis = 0u; axis < 3u; ++axis ) {
            math::vec3d_t a{}, b{};
            f64 *lo[]{ &a.x, &a.y, &a.z };
            f64 *hi[]{ &b.x, &b.y, &b.z };
            *lo[axis] = Axis( m_position, axis ) - extent;
            *hi[axis] = Axis( m_position, axis ) + extent;
            QLineF line;
            if ( ProjectVisibleSegment( a, b, line ) ) {
                painter.setPen( QPen( EditorStyle_Color( style, colors[axis] ), 1.4 ) );
                painter.drawLine( line );
            }
        }
    }

    // The material's look as one colour: its image averaged, cached until
    // the document changes. Unassigned or imageless faces are neutral.
    QColor MaterialColor( u64 material, const QColor &neutral ) const
    {
        if ( material == 0u || m_pWorkspace->pDocument == nullptr ) { return neutral; }
        if ( const auto it = m_materialColors.constFind( material ); it != m_materialColors.constEnd() ) { return it.value(); }
        const string_view_t path = MapMaterials_Path( &m_pWorkspace->pDocument->materials, material );
        QColor colour = neutral;
        if ( path.cchLength != 0u ) {
            const QImage image = MapWorkspace_MaterialImage( m_pWorkspace, QString::fromUtf8( path.pData, static_cast<qsizetype>( path.cchLength ) ) );
            if ( !image.isNull() ) { colour = image.scaled( 1, 1, Qt::IgnoreAspectRatio, Qt::SmoothTransformation ).pixelColor( 0, 0 ); }
        }
        m_materialColors.insert( material, colour );
        return colour;
    }

    // Construction and committed faces use the same near-plane clipping.
    // Caller-owned scratch vectors reuse their capacity across face rings.
    bool ProjectWireFace( const map_wireframe_t &wire, const map_wire_face_t &face, QPolygonF &polygon,
        std::vector<math::vec3d_t> &camera, std::vector<math::vec3d_t> &clipped, f64 *depthOut = nullptr, const math::affine3d_t *transform = nullptr ) const
    {
        polygon.clear(); camera.clear(); clipped.clear();
        for ( u32 k = 0; k < face.nIndices; ++k ) {
            const auto point = wire.points.pData[wire.faceIndices.pData[face.iFirstIndex + k]];
            const auto relative = Sub( transform != nullptr ? math::Affine3d_TransformPoint( *transform, point ) : point, m_position );
            camera.push_back( { Dot( relative, m_right ), Dot( relative, m_up ), Dot( relative, m_forward ) } );
        }
        return ProjectCameraFace( polygon, camera, clipped, depthOut );
    }

    bool ProjectCameraFace( QPolygonF &polygon, const std::vector<math::vec3d_t> &camera,
        std::vector<math::vec3d_t> &clipped, f64 *depthOut = nullptr ) const
    {
        polygon.clear(); clipped.clear();
        for ( usize k = 0; k < camera.size(); ++k ) {
            const auto &a = camera[k]; const auto &b = camera[( k + 1 ) % camera.size()];
            const bool inA = a.z >= kCameraNear, inB = b.z >= kCameraNear;
            if ( inA ) { clipped.push_back( a ); }
            if ( inA != inB ) {
                const f64 t = ( kCameraNear - a.z ) / ( b.z - a.z );
                clipped.push_back( { a.x + ( b.x - a.x ) * t, a.y + ( b.y - a.y ) * t, kCameraNear } );
            }
        }
        if ( clipped.size() < 3 ) { return false; }
        polygon.reserve( static_cast<qsizetype>( clipped.size() ) );
        const f64 focal = FocalLength(), cx = width() * 0.5, cy = height() * 0.5;
        f64 depth = 0;
        for ( const auto &point : clipped ) {
            polygon << QPointF( cx + point.x * focal / point.z, cy - point.y * focal / point.z );
            depth += point.z;
        }
        if ( depthOut != nullptr ) { *depthOut = depth / static_cast<f64>( clipped.size() ); }
        const QRectF viewport = QRectF( rect() ).adjusted( -2, -2, 2, 2 );
        if ( !polygon.boundingRect().intersects( viewport ) ) { return false; }
        if ( viewport.contains( polygon.boundingRect() ) ) { return true; }
        // Close-up faces can project beyond the raster engine's coordinate
        // range. Bound the polygon before painting, for committed faces and
        // previews alike; authoritative wire edges are projected separately.
        for ( int boundary = 0; boundary < 4; ++boundary ) {
            QPolygonF bounded; bounded.reserve( polygon.size() + 2 );
            const bool horizontal = boundary < 2;
            const bool minimum = ( boundary & 1 ) == 0;
            const qreal limit = boundary == 0 ? viewport.left() : boundary == 1 ? viewport.right() :
                                boundary == 2 ? viewport.top() : viewport.bottom();
            const auto coordinate = [horizontal]( QPointF point ) { return horizontal ? point.x() : point.y(); };
            for ( qsizetype i = 0; i < polygon.size(); ++i ) {
                const auto a = polygon[i], b = polygon[( i + 1 ) % polygon.size()];
                const qreal ca = coordinate( a ), cb = coordinate( b );
                const bool inA = minimum ? ca >= limit : ca <= limit;
                const bool inB = minimum ? cb >= limit : cb <= limit;
                if ( inA ) { bounded << a; }
                if ( inA != inB ) { bounded << a + ( b - a ) * ( ( limit - ca ) / ( cb - ca ) ); }
            }
            polygon = std::move( bounded );
            if ( polygon.size() < 3 ) { return false; }
        }
        return true;
    }

    // A display lattice in world coordinates, independent of material UVs.
    // Recover the visible plane footprint from the clipped screen polygon:
    // a close-up wall must not enumerate its enormous offscreen bounds.
    void DrawSurfaceGrid( QPainter &painter, const QPolygonF &polygon, math::vec3d_t anchor,
        math::vec3d_t normal, const QColor &fill, int familyLimit, usize &budget, f64 opacity = 1.0 ) const
    {
        const auto &workspace = *m_pWorkspace;
        if ( budget == 0u || polygon.size() < 3 || !DisplayFlag( workspace, "editor.grid.show_surface_3d" ) ) { return; }
        int drop = 0;
        for ( int axis = 1; axis < 3; ++axis ) {
            if ( std::abs( Axis( normal, axis ) ) > std::abs( Axis( normal, drop ) ) ) { drop = axis; }
        }
        const f64 denominator = Axis( normal, drop );
        if ( !std::isfinite( denominator ) || std::abs( denominator ) < 1e-12 ) { return; }
        const int axes[2]{ drop == 0 ? 1 : 0, drop == 2 ? 1 : 2 };
        f64 minimum[2]{ std::numeric_limits<f64>::max(), std::numeric_limits<f64>::max() };
        f64 maximum[2]{ -std::numeric_limits<f64>::max(), -std::numeric_limits<f64>::max() };
        const f64 focal = FocalLength();
        if ( !std::isfinite( focal ) || focal <= 0.0 ) { return; }
        const auto planePoint = [&]( QPointF screen, math::vec3d_t &out ) {
            // Unnormalized rays have forward component one, so this is the
            // same forward-depth near plane used by ProjectCameraFace.
            const auto ray = Add( m_forward, Add( Scale( m_right, ( screen.x() - width() * 0.5 ) / focal ),
                Scale( m_up, ( height() * 0.5 - screen.y() ) / focal ) ) );
            const f64 divisor = Dot( normal, ray );
            if ( !std::isfinite( divisor ) || std::abs( divisor ) < 1e-12 ) { return false; }
            const f64 depth = Dot( normal, Sub( anchor, m_position ) ) / divisor;
            if ( !std::isfinite( depth ) || depth < kCameraNear * ( 1.0 - 1e-8 ) ) { return false; }
            out = Add( m_position, Scale( ray, depth ) );
            return std::isfinite( out.x ) && std::isfinite( out.y ) && std::isfinite( out.z );
        };
        QPointF center;
        for ( const QPointF screen : polygon ) {
            math::vec3d_t point;
            if ( !planePoint( screen, point ) ) { return; }
            center += screen / static_cast<f64>( polygon.size() );
            for ( int axis = 0; axis < 2; ++axis ) {
                minimum[axis] = std::min( minimum[axis], Axis( point, axes[axis] ) );
                maximum[axis] = std::max( maximum[axis], Axis( point, axes[axis] ) );
            }
        }
        math::vec3d_t tangents[2]{ AxisVector( axes[0] ), AxisVector( axes[1] ) };
        for ( int axis = 0; axis < 2; ++axis ) { SetAxis( tangents[axis], drop, -Axis( normal, axes[axis] ) / denominator ); }
        const auto pixelSpacing = [&]( QPointF screen, int family ) {
            math::vec3d_t point;
            if ( !planePoint( screen, point ) ) { return 0.0; }
            const auto relative = Sub( point, m_position );
            const f64 z = Dot( relative, m_forward );
            if ( !std::isfinite( z ) || z < kCameraNear ) { return 0.0; }
            const f64 x = Dot( relative, m_right ) / z, y = Dot( relative, m_up ) / z;
            QPointF derivatives[2];
            for ( int axis = 0; axis < 2; ++axis ) {
                derivatives[axis] = { ( Dot( tangents[axis], m_right ) - x * Dot( tangents[axis], m_forward ) ) * focal / z,
                    -( Dot( tangents[axis], m_up ) - y * Dot( tangents[axis], m_forward ) ) * focal / z };
            }
            const auto along = derivatives[1 - family];
            const f64 length = std::hypot( along.x(), along.y() );
            const f64 area = std::abs( derivatives[0].x() * derivatives[1].y() - derivatives[0].y() * derivatives[1].x() );
            const f64 spacing = length > 1e-12 ? area / length : 0.0;
            return std::isfinite( spacing ) ? spacing : 0.0;
        };
        const auto pointAt = [&]( f64 u, f64 v ) {
            // Solve relative to a face point to avoid cancelling large world
            // coordinates when calculating the plane's omitted component.
            auto point = anchor;
            SetAxis( point, axes[0], u ); SetAxis( point, axes[1], v );
            SetAxis( point, drop, Axis( anchor, drop ) -
                ( Axis( normal, axes[0] ) * ( u - Axis( anchor, axes[0] ) ) +
                  Axis( normal, axes[1] ) * ( v - Axis( anchor, axes[1] ) ) ) / denominator );
            return point;
        };
        const bool adaptive = DisplayFlag( workspace, "editor.grid.adaptive_3d" );
        const f64 minimumPixels = EditorSettings_Integer( &workspace.pGui->settings, "editor.grid.min_spacing_px", MAP_VIEW_GRID_MIN_PIXELS );
        const f64 authoredStep = workspace.gridSize;
        if ( !std::isfinite( authoredStep ) || authoredStep <= 0.0 ) { return; }
        familyLimit = std::clamp( familyLimit, 1, kSurfaceGridLinesPerFamily );
        f64 stride = 1.0;
        for ( int family = 0; family < 2; ++family ) {
            const f64 extent = maximum[family] - minimum[family];
            if ( !std::isfinite( extent ) || extent < 0.0 ) { return; }
            stride = std::max( stride, std::ceil( extent / authoredStep / familyLimit ) );
            if ( adaptive ) {
                const f64 pixels = pixelSpacing( center, family ) * authoredStep;
                if ( pixels <= 1e-12 ) { return; }
                stride = std::max( stride, std::ceil( minimumPixels / pixels ) );
            }
        }
        f64 step = authoredStep * stride;
        if ( !std::isfinite( step ) || step <= 0.0 ) { return; }
        // Both families use one world-cell size. Different screen slopes
        // must not turn a square construction lattice into unequal cells.
        for ( int family = 0; family < 2; ++family ) {
            const f64 first = std::ceil( minimum[family] / step ) * step;
            const f64 count = std::floor( ( maximum[family] - first ) / step ) + 1.0;
            if ( count > familyLimit ) { step = authoredStep * ( stride + 1.0 ); break; }
        }
        if ( !std::isfinite( step ) || step <= 0.0 ) { return; }
        QPainterPath clip; clip.addPolygon( polygon ); clip.closeSubpath();
        painter.save();
        painter.setClipPath( clip, Qt::IntersectClip );
        painter.setRenderHint( QPainter::Antialiasing, true );
        for ( int family = 0; family < 2 && budget != 0u; ++family ) {
            // A bounded integer count, never a floating point increment loop.
            // Budget coarsening applies even when adaptive spacing is disabled.
            const f64 first = std::ceil( minimum[family] / step ) * step;
            const f64 count = std::floor( ( maximum[family] - first ) / step ) + 1.0;
            if ( !std::isfinite( first ) || !std::isfinite( count ) || count <= 0.0 ) { continue; }
            const int nLines = static_cast<int>( std::min( count, static_cast<f64>( familyLimit ) ) );
            const i64 majorInterval = DrawnMajorInterval( workspace, step );
            for ( int lineIndex = 0; lineIndex < nLines && budget != 0u; ++lineIndex ) {
                --budget;
                const f64 coordinate = first + lineIndex * step;
                const auto a = family == 0 ? pointAt( coordinate, minimum[1] ) : pointAt( minimum[0], coordinate );
                const auto b = family == 0 ? pointAt( coordinate, maximum[1] ) : pointAt( maximum[0], coordinate );
                QLineF line;
                if ( !ProjectVisibleSegment( a, b, line ) || line.length() < 0.5 ) { continue; }
                const bool major = GridLineMultiple( coordinate, step, majorInterval );
                const QColor themed = EditorStyle_Color( workspace.pGui->style, major ? gui::STYLE_COLOR_GRID_MAJOR : gui::STYLE_COLOR_GRID_MINOR );
                const f64 luminance = fill.redF() * 0.2126 + fill.greenF() * 0.7152 + fill.blueF() * 0.0722;
                const f64 ink = luminance > 0.45 ? 0.0 : 1.0;
                QColor color = QColor::fromRgbF( ink, ink, ink );
                QLinearGradient fade( line.p1(), line.p2() );
                f64 maximumAlpha = 0.0;
                constexpr int kSamples = 8;
                for ( int sample = 0; sample <= kSamples; ++sample ) {
                    const f64 t = static_cast<f64>( sample ) / kSamples;
                    const f64 pixels = pixelSpacing( line.pointAt( t ), family ) * step * ( major ? majorInterval : 1 );
                    const f64 level = std::clamp( ( pixels - 1.25 ) / 2.75, 0.0, 1.0 );
                    const f64 alpha = themed.alphaF() * ( major ? 0.30 : 0.17 ) * opacity * level * level * ( 3.0 - 2.0 * level );
                    color.setAlphaF( alpha ); fade.setColorAt( t, color );
                    maximumAlpha = std::max( maximumAlpha, alpha );
                }
                if ( maximumAlpha < 0.01 ) { continue; }
                painter.setPen( QPen( QBrush( fade ), 0.0 ) );
                painter.drawLine( line );
            }
        }
        painter.restore();
    }

    void DrawWireSurfaceGrid( QPainter &painter, const map_wireframe_t &wire, const map_wire_face_t &face,
        const QPolygonF &polygon, const QColor &fill, int familyLimit, usize &budget,
        std::vector<math::vec3d_t> &camera, std::vector<math::vec3d_t> &clipped,
        const math::affine3d_t *transform = nullptr, f64 opacity = 1.0 ) const
    {
        if ( budget == 0u || !DisplayFlag( *m_pWorkspace, "editor.grid.show_surface_3d" ) ) { return; }
        const auto pointAt = [&]( u32 corner ) {
            const auto point = wire.points.pData[wire.faceIndices.pData[face.iFirstIndex + corner]];
            return transform != nullptr ? math::Affine3d_TransformPoint( *transform, point ) : point;
        };
        auto anchor = pointAt( 0 );
        auto normal = face.normal;
        if ( face.nIndices == 3u ) {
            // Three points define their plane directly. An averaged cached
            // normal can lose precision through cancellation on very large
            // cells; use the same positions as the warped-quad triangles.
            if ( SurfaceTrianglePlane( anchor, pointAt( 1 ), pointAt( 2 ), anchor, normal ) ) {
                DrawSurfaceGrid( painter, polygon, anchor, normal, fill, familyLimit, budget, opacity );
            }
            return;
        }
        if ( transform != nullptr && !math::Affine3d_TryTransformNormal( *transform, normal, 1e-18, &normal ) ) { return; }
        const f64 length = std::sqrt( Dot( normal, normal ) );
        if ( !std::isfinite( length ) || length <= 1e-18 ) { return; }
        normal = Scale( normal, 1.0 / length );
        const f64 planarityTolerance = m_pWorkspace->pDocument != nullptr ?
            m_pWorkspace->pDocument->geometryPolicy.numerical.fPlanarityTolerance : 1e-6;
        bool planar = true;
        for ( u32 corner = 1; corner < face.nIndices; ++corner ) {
            const auto relative = Sub( pointAt( corner ), anchor );
            const f64 distance = Dot( normal, relative );
            if ( !std::isfinite( distance ) ) { return; }
            if ( std::abs( distance ) > planarityTolerance ) { planar = false; break; }
        }
        if ( planar ) { DrawSurfaceGrid( painter, polygon, anchor, normal, fill, familyLimit, budget, opacity ); return; }
        // Patch control cells and thinned terrain cells may be warped quads.
        // Their two presentation triangles each need an actual surface plane,
        // rather than a lattice hovering on the quad's averaged Newell plane.
        if ( !face.bTwoSided || face.nIndices != 4u ) { return; }
        camera.reserve( 3 ); clipped.reserve( 4 );
        painter.save();
        QPainterPath faceClip; faceClip.addPolygon( polygon ); faceClip.closeSubpath();
        painter.setClipPath( faceClip, Qt::IntersectClip );
        for ( u32 triangle = 0; triangle < 2u && budget != 0u; ++triangle ) {
            const auto b = pointAt( triangle + 1u ), c = pointAt( triangle + 2u );
            math::vec3d_t triangleAnchor{}, triangleNormal{};
            if ( !SurfaceTrianglePlane( anchor, b, c, triangleAnchor, triangleNormal ) ) { continue; }
            camera.clear();
            for ( const auto point : { anchor, b, c } ) {
                const auto relative = Sub( point, m_position );
                camera.push_back( { Dot( relative, m_right ), Dot( relative, m_up ), Dot( relative, m_forward ) } );
            }
            QPolygonF projected;
            if ( ProjectCameraFace( projected, camera, clipped ) ) {
                DrawSurfaceGrid( painter, projected, triangleAnchor, triangleNormal, fill, familyLimit, budget, opacity );
            }
        }
        painter.restore();
    }

    // Shaded modes without a GPU: faces clipped to the near plane, sorted
    // far to near, and filled (the painter's algorithm). Each face's outline
    // is drawn with its fill, so nearer faces hide farther edges.
    void DrawFaces( QPainter &painter, usize &surfaceBudget ) const
    {
        const map_workspace_t &workspace = *m_pWorkspace;
        const map_wireframe_t &committedWire = workspace.wire;
        const bool facePreview = MapWorkspace_HasFacePreview( &workspace ) && workspace.editPreview.status == map_status_t::OK;
        struct drawn_t {
            f64 depth;
            QPolygonF polygon;
            QColor fill;
            const map_wire_face_t *face;
            bool transformed;
            const map_wireframe_t *wire;
        };
        std::vector<drawn_t> drawn;
        drawn.reserve( committedWire.faces.nCount + ( facePreview ? workspace.editPreviewWire.faces.nCount : 0u ) );
        const QColor neutral = Token( workspace, "viewport.face.world" );
        const QColor hover = SlotColor( workspace, LINE_HOVER );
        const math::vec3d_t sun = Scale( math::Vec3d_Make( 0.35, 0.55, 0.76 ), 1.0 / std::sqrt( 0.35 * 0.35 + 0.55 * 0.55 + 0.76 * 0.76 ) );
        const auto blend = []( const QColor &a, const QColor &b, f64 t ) {
            return QColor::fromRgbF( static_cast<float>( a.redF() + ( b.redF() - a.redF() ) * t ), static_cast<float>( a.greenF() + ( b.greenF() - a.greenF() ) * t ),
                                     static_cast<float>( a.blueF() + ( b.blueF() - a.blueF() ) * t ) );
        };
        u64 lastId = 0u;
        bool bVisible = false, bObjectSelected = false, bSelected = false, bHovered = false, bTransforming = false;
        auto affine = MapWorkspace_TransformPreviewAffine( workspace.editPreview.transform );
        std::vector<math::vec3d_t> camera, clipped;
        for ( usize i = 0u; i < committedWire.faces.nCount + ( facePreview ? workspace.editPreviewWire.faces.nCount : 0u ); ++i ) {
            const bool candidate = i >= committedWire.faces.nCount;
            const map_wireframe_t &wire = candidate ? workspace.editPreviewWire : committedWire;
            const map_wire_face_t &face = wire.faces.pData[candidate ? i - committedWire.faces.nCount : i];
            if ( face.id != lastId || i == 0u || i == committedWire.faces.nCount ) {
                lastId = face.id;
                const map_wire_object_t *pObject = MapWireframe_FindObject( committedWire, face.id );
                bVisible = pObject != nullptr && MapWorkspace_IsVisible( &workspace, *pObject );
                bObjectSelected = pObject != nullptr && ObjectSelected( workspace, *pObject );
                bTransforming = !candidate && workspace.editPreview.bActive && workspace.editPreview.transform.kind != map_transform_preview_kind_t::NONE &&
                    pObject != nullptr && MapWorkspace_IsTransformPreviewObject( &workspace, *pObject );
                if ( bTransforming ) { affine = MapWorkspace_TransformPreviewObjectAffine( workspace.editPreview.transform, *pObject ); }
            }
            if ( !bVisible || ( !candidate && ( workspace.editPreview.bClip || facePreview ) && EditPreviewTarget( workspace, face.id ) ) ) { continue; }
            const bool cloning = bTransforming && workspace.editPreview.transform.bClone;
            // A clone retains its source solid. Both instances participate in
            // the same depth sort, so overlap and occlusion match the commit.
            for ( int instance = 0; instance < ( cloning ? 2 : 1 ); ++instance ) {
                const bool sourceClone = cloning && instance == 0;
                const bool transformed = bTransforming && !sourceClone;
                bSelected = !sourceClone && bObjectSelected &&
                    ( !MapWorkspace_HasBrushFace( &workspace ) || face.sideId == workspace.selectedBrushFaceSide ) &&
                    ( !MapWorkspace_HasMeshFace( &workspace ) || face.faceId == workspace.selectedMeshFaceId );
                bHovered = !candidate && !MeshComponentMode( workspace ) && !bTransforming && !bSelected && face.id == m_hover.id &&
                    ( workspace.elementMode != map_element_mode_t::FACES ||
                      ( face.sideId != 0 && face.sideId == m_hoverFace.sideSourceId.value ) ||
                      ( face.faceId != 0 && face.faceId == m_hoverMeshFace ) );
                auto first = wire.points.pData[wire.faceIndices.pData[face.iFirstIndex]];
                auto faceNormal = face.normal;
                if ( transformed ) {
                    first = math::Affine3d_TransformPoint( affine, first );
                    if ( !math::Affine3d_TryTransformNormal( affine, face.normal, 1e-18, &faceNormal ) ) { continue; }
                    const f64 length = std::sqrt( Dot( faceNormal, faceNormal ) );
                    if ( !std::isfinite( length ) || length <= 1e-18 ) { continue; }
                    faceNormal = Scale( faceNormal, 1.0 / length );
                }
                const f64 facing = Dot( faceNormal, Sub( m_position, first ) );
                if ( facing <= 0.0 && !face.bTwoSided ) { continue; } // Back faces of solids are never seen.
                const math::vec3d_t normal = facing >= 0.0 ? faceNormal : Scale( faceNormal, -1.0 );
                drawn_t entry{ 0.0, QPolygonF(), QColor(), &face, transformed, &wire };
                if ( !ProjectWireFace( wire, face, entry.polygon, camera, clipped, &entry.depth, transformed ? &affine : nullptr ) ) { continue; }
                const QColor base = MaterialColor( face.material, neutral );
                QColor colour = base;
                switch ( m_renderMode ) {
                    case map_render_mode_t::SHADED: {
                        // A fixed sun plus a little headlight, so no face goes black.
                        const f64 light = 0.32 + 0.56 * std::max( 0.0, Dot( normal, sun ) ) + 0.16 * std::abs( Dot( normal, m_forward ) );
                        colour = QColor::fromRgbF( static_cast<float>( std::min( 1.0, base.redF() * light ) ), static_cast<float>( std::min( 1.0, base.greenF() * light ) ),
                                                   static_cast<float>( std::min( 1.0, base.blueF() * light ) ) );
                        break;
                    }
                    case map_render_mode_t::NORMALS:
                        colour = QColor::fromRgbF( static_cast<float>( normal.x * 0.5 + 0.5 ), static_cast<float>( normal.y * 0.5 + 0.5 ), static_cast<float>( normal.z * 0.5 + 0.5 ) );
                        break;
                    default: break; // Fullbright: the material as it is.
                }
                // Whole-object selection is communicated by edges and handles.
                // Face mode has its own restrained overlay; do not tint twice.
                if ( bHovered && workspace.elementMode != map_element_mode_t::FACES ) { colour = blend( colour, hover, 0.10 ); }
                entry.fill = colour;
                drawn.push_back( std::move( entry ) );
            }
        }
        std::sort( drawn.begin(), drawn.end(), []( const drawn_t &a, const drawn_t &b ) { return a.depth > b.depth; } );
        painter.save();
        const bool componentPicking = MeshEdgePickingActive( workspace ) || MeshVertexPickingActive( workspace );
        const bool showEdges = m_bMeshEdges || componentPicking;
        painter.setRenderHint( QPainter::Antialiasing, showEdges ); // Outlines cover antialiasing seams.
        const usize gridFaces = std::min( drawn.size(), kSurfaceGridMaxFaces );
        const int familyLimit = static_cast<int>( std::min( static_cast<usize>( kSurfaceGridLinesPerFamily ),
            std::max( usize{ 1u }, surfaceBudget / ( 4u * std::max( usize{ 1u }, gridFaces ) ) ) ) );
        usize iFace = 0u;
        for ( const drawn_t &entry : drawn ) {
            const map_wireframe_t &wire = *entry.wire;
            painter.setBrush( entry.fill );
            painter.setPen( Qt::NoPen );
            painter.drawPolygon( entry.polygon );
            // Fill and grid share the painter's depth order. A separate grid
            // pass would leak lines from rear surfaces through nearer solids.
            const bool nearFace = iFace++ >= drawn.size() - gridFaces;
            if ( nearFace ) {
                if ( entry.transformed ) {
                    const auto *object = MapWireframe_FindObject( wire, entry.face->id );
                    if ( object != nullptr ) { affine = MapWorkspace_TransformPreviewObjectAffine( workspace.editPreview.transform, *object ); }
                }
                DrawWireSurfaceGrid( painter, wire, *entry.face, entry.polygon, entry.fill,
                    familyLimit, surfaceBudget, camera, clipped, entry.transformed ? &affine : nullptr );
            }
            const bool candidate = entry.wire == &workspace.editPreviewWire;
            if ( showEdges || candidate ) {
                const auto *object = MapWireframe_FindObject( wire, entry.face->id );
                const QColor edgeColor = candidate ? SlotColor( workspace, LINE_WORLD ) : componentPicking && object != nullptr && object->kind == map_wire_kind_t::MESH ?
                    SlotColor( workspace, LINE_MESH ) : entry.fill.darker( 160 );
                painter.setBrush( Qt::NoBrush ); painter.setPen( QPen( edgeColor, 1.0 ) );
                painter.drawPolygon( entry.polygon );
            }
            if ( nearFace && MeshVertexPickingActive( workspace ) && !entry.transformed ) {
                const auto *object = MapWireframe_FindObject( wire, entry.face->id );
                if ( object != nullptr && object->kind == map_wire_kind_t::MESH ) {
                    QPolygonF vertices;
                    vertices.reserve( entry.face->nIndices );
                    for ( u32 i = 0u; i < entry.face->nIndices; ++i ) {
                        const auto point = wire.points.pData[wire.faceIndices.pData[entry.face->iFirstIndex + i]];
                        QLineF projected;
                        if ( Project( point, point, &projected ) && QRectF( rect() ).contains( projected.p1() ) ) { vertices.append( projected.p1() ); }
                    }
                    // Use original authored corners, not new corners created
                    // by near-plane clipping. Keep cues in the face's depth
                    // slot so nearer opaque fills cover hidden candidates.
                    QPainterPath clip; clip.addPolygon( entry.polygon );
                    painter.save(); painter.setClipPath( clip, Qt::IntersectClip );
                    painter.setRenderHint( QPainter::Antialiasing, false );
                    QColor color = SlotColor( workspace, LINE_MESH ); color.setAlpha( 190 );
                    painter.setPen( QPen( color, 3.0, Qt::SolidLine, Qt::SquareCap ) );
                    painter.drawPoints( vertices ); painter.restore();
                }
            }
        }
        painter.restore();
    }

    void DrawObjects( QPainter &painter ) const
    {
        const map_workspace_t &workspace = *m_pWorkspace;
        const map_wireframe_t &wire = workspace.wire;
        QVector<QLineF> batches[LINE_COUNT];
        QVector<QLineF> ghosts;
        QVector<QPointF> vertices;
        QLineF projected;
        for ( usize i = 0u; i < wire.objects.nCount; ++i ) {
            const map_wire_object_t &object = wire.objects.pData[i];
            const bool visible = MapWorkspace_IsVisible( &workspace, object );
            if ( !object.bounds.bHas || ( !visible && ( object.kind == map_wire_kind_t::ENTITY || !DisplayFlag( workspace, "editor.viewport.perspective.ghost_hidden", CY_FALSE ) ) ) ) { continue; }
            if ( visible && MoveSourceActive( workspace ) && EditPreviewTarget( workspace, object.id ) ) { continue; }
            const bool faceContext = workspace.elementMode == map_element_mode_t::FACES && ( object.kind == map_wire_kind_t::BRUSH || object.kind == map_wire_kind_t::MESH );
            const line_color_t slot = visible ? ColorSlot( workspace, object, m_hover.id ) : LINE_WORLD;
            const line_color_t edgeSlot = faceContext && ( slot == LINE_SELECTED || slot == LINE_HOVER ) ? LINE_WORLD : slot;
            // Shaded modes: the fills show geometry; only what is selected
            // or under the pointer keeps its edges on top (unless overlaid).
            if ( !EditPreviewTarget( workspace, object.id ) && m_renderMode != map_render_mode_t::WIREFRAME && !m_bWireOverlay && object.kind != map_wire_kind_t::ENTITY &&
                 slot != LINE_SELECTED && slot != LINE_HOVER ) { continue; }
            QVector<QLineF> &batch = visible && !EditPreviewTarget( workspace, object.id ) ? batches[edgeSlot] : ghosts;
            if ( object.kind == map_wire_kind_t::ENTITY ) {
                // The twelve edges of the entity's box.
                const math::vec3d_t lo = object.bounds.box.minimum;
                const math::vec3d_t hi = object.bounds.box.maximum;
                math::vec3d_t corners[8];
                for ( u32 c = 0u; c < 8u; ++c ) {
                    corners[c] = math::Vec3d_Make( ( c & 1u ) != 0u ? hi.x : lo.x, ( c & 2u ) != 0u ? hi.y : lo.y, ( c & 4u ) != 0u ? hi.z : lo.z );
                }
                constexpr u32 kEdges[12][2]{ { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 },
                                             { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
                for ( const auto &edge : kEdges ) {
                    if ( Project( corners[edge[0]], corners[edge[1]], &projected ) ) { batch.append( projected ); }
                }
                continue;
            }
            for ( u32 l = 0u; l < object.nLines; ++l ) {
                const map_wire_line_t &line = wire.lines.pData[object.iFirstLine + l];
                if ( Project( wire.points.pData[line.iA], wire.points.pData[line.iB], &projected ) ) { batch.append( projected ); }
                if ( slot == LINE_SELECTED && DisplayFlag( workspace, "editor.viewport.perspective.show_selection_vertices", CY_FALSE ) ) {
                    if ( Project( wire.points.pData[line.iA], wire.points.pData[line.iA], &projected ) ) { vertices.append( projected.p1() ); }
                    if ( Project( wire.points.pData[line.iB], wire.points.pData[line.iB], &projected ) ) { vertices.append( projected.p1() ); }
                }
            }
        }
        DrawGhostBatch( painter, workspace, ghosts );
        for ( u32 slot = 0u; slot < LINE_COUNT; ++slot ) {
            DrawWireBatch( painter, workspace, static_cast<line_color_t>( slot ), batches[slot] );
        }
        DrawVertexCues( painter, workspace, vertices, rect() );
        DrawMeshEdgeSelection( painter, workspace, m_hover.id == m_hoverEdge.object ? m_hoverEdge : map_mesh_edge_hit_t{},
            [this]( math::vec3d_t a, math::vec3d_t b, QLineF &line ) { return ProjectVisibleSegment( a, b, line ); } );
        DrawMeshVertexSelection( painter, workspace, m_hover.id == m_hoverVertex.object ? m_hoverVertex : map_mesh_vertex_hit_t{},
            [this]( math::vec3d_t point, QPointF &screen ) {
                QLineF line; if ( !Project( point, point, &line ) ) { return false; } screen = line.p1(); return true;
            }, rect() );
        QVector<QRectF> labels;
        for ( const bool selected : { true, false } ) {
            for ( usize i = 0u; i < wire.entities.nCount; ++i ) {
                const map_wire_entity_t &entity = wire.entities.pData[i];
                const map_wire_object_t *object = MapWireframe_FindObject( wire, entity.id );
                if ( MapWorkspace_IsSelected( &workspace, entity.id ) != selected ||
                     !entity.bHasOrigin || object == nullptr || !MapWorkspace_IsVisible( &workspace, *object ) ||
                     EditPreviewTarget( workspace, entity.id ) || !ShowEntityName( workspace, entity.id, true ) ) { continue; }
                if ( !Project( entity.origin, entity.origin, &projected ) || !rect().contains( projected.p1().toPoint() ) ) { continue; }
                DrawEntityLabel( painter, workspace, entity, QRectF( projected.p1(), QSizeF() ), rect(), labels, true );
            }
        }
    }

    void DrawConnections( QPainter &painter ) const
    {
        const map_workspace_t &workspace = *m_pWorkspace;
        for ( usize i = 0u; i < workspace.wire.connections.nCount; ++i ) {
            const map_wire_connection_t &connection = workspace.wire.connections.pData[i];
            QLineF line;
            if ( ShowConnection( workspace, connection, true ) && Project( connection.sourceOrigin, connection.targetOrigin, &line ) ) {
                DrawConnection( painter, workspace, connection, line );
            }
        }
    }

    // The cordon's box, its edges dashed in the cordon colour.
    void DrawCordon( QPainter &painter ) const
    {
        if ( !MapWorkspace_IsCordonActive( m_pWorkspace ) ) { return; }
        math::vec3d_t corners[8];
        BoundsCorners( m_pWorkspace->cordon, corners );
        painter.setPen( QPen( Token( *m_pWorkspace, "viewport.cordon" ), 1.5, Qt::DashLine ) );
        QLineF line;
        for ( const auto &edge : kBoundsEdges ) {
            if ( Project( corners[edge[0]], corners[edge[1]], &line ) ) { painter.drawLine( line ); }
        }
    }

    void DrawSelection( QPainter &painter ) const
    {
        const map_workspace_t &workspace = *m_pWorkspace;
        if ( workspace.tool == map_tool_t::NONE || MeshComponentMode( workspace ) ) { return; }
        const map_bounds_t bounds = SelectionGeometryBounds( workspace );
        if ( !bounds.bHas || ( workspace.editPreview.bActive && ( workspace.editPreview.bClip || workspace.editPreview.bFacePushPull ||
            workspace.editPreview.transform.kind != map_transform_preview_kind_t::NONE ) ) ) { return; }
        math::vec3d_t corners[8];
        BoundsCorners( bounds, corners );
        QLineF line;
        if ( DisplayFlag( workspace, "editor.viewport.perspective.show_selection_bounds" ) ) {
            QColor color = SlotColor( workspace, LINE_SELECTED );
            color.setAlpha( 155 );
            painter.setPen( QPen( color, 1.0, Qt::DashLine ) );
            for ( const auto &edge : kBoundsEdges ) {
                if ( Project( corners[edge[0]], corners[edge[1]], &line ) ) { painter.drawLine( line ); }
            }
        }
        if ( workspace.editPreview.bActive || !DisplayFlag( workspace, "editor.viewport.perspective.show_selection_dimensions" ) ) { return; }
        const auto project = [this]( math::vec3d_t point, QPointF &screen ) {
            QLineF line;
            if ( !Project( point, point, &line ) ) { return false; }
            screen = line.p1(); return true;
        };
        DrawBoundsDimensions( painter, workspace, bounds, project, rect() );
    }

    // World axes as the camera sees them, in the top-right corner; the
    // farthest axis is drawn first so nearer ones overlap it.
    void DrawAxisTriad( QPainter &painter ) const
    {
        const gui::editor_style_t &style = m_pWorkspace->pGui->style;
        const gui::editor_style_color_t axisColors[3]{ gui::STYLE_COLOR_AXIS_X, gui::STYLE_COLOR_AXIS_Y, gui::STYLE_COLOR_AXIS_Z };
        const math::vec3d_t axes[3]{ math::Vec3d_Make( 1.0, 0.0, 0.0 ), math::Vec3d_Make( 0.0, 1.0, 0.0 ), math::Vec3d_Make( 0.0, 0.0, 1.0 ) };
        u32 order[3]{ 0u, 1u, 2u };
        std::sort( order, order + 3, [&]( u32 a, u32 b ) { return Dot( axes[a], m_forward ) > Dot( axes[b], m_forward ); } );
        const QPointF origin = AxisTriadRect( *this ).center();
        painter.setRenderHint( QPainter::Antialiasing, true );
        for ( const u32 axis : order ) {
            const QPointF tip = origin + QPointF( Dot( axes[axis], m_right ), -Dot( axes[axis], m_up ) ) * 25.0;
            DrawAxisArrow( painter, origin, tip, EditorStyle_Color( style, axisColors[axis] ), kAxisLetters[axis] );
        }
        painter.setRenderHint( QPainter::Antialiasing, false );
    }

    void DrawSpeedFeedback( QPainter &painter ) const
    {
        if ( !m_speedFeedbackTimer.isActive() ) { return; }
        const auto &workspace = *m_pWorkspace;
        const auto metrics = painter.fontMetrics();
        const QString text = QStringLiteral( "Camera speed %1 u/s" ).arg( NumberText( m_moveSpeed ) );
        const qreal width = std::min<qreal>( metrics.horizontalAdvance( text ) + 16.0, this->width() - 16.0 );
        const qreal height = metrics.height() + 8.0;
        const qreal stripHeight = ShowMetrics( workspace, true ) ? metrics.height() + 6.0 : 0.0;
        const QRectF area( 8.0, this->height() - stripHeight - height - 8.0, width, height );
        painter.save();
        painter.fillRect( area, Token( workspace, "viewport.overlay.background" ) );
        painter.setPen( QPen( Token( workspace, "viewport.overlay.border" ), 1.0 ) );
        painter.drawRect( area );
        painter.setPen( Token( workspace, "viewport.overlay.text" ) );
        painter.drawText( area.adjusted( 8.0, 0.0, -8.0, 0.0 ), Qt::AlignLeft | Qt::AlignVCenter,
            metrics.elidedText( text, Qt::ElideRight, static_cast<int>( width - 16.0 ) ) );
        painter.restore();
    }

    // The TileEditor's 3D status strip: where the camera is and how it moves.
    void DrawStatusStrip( QPainter &painter ) const
    {
        const map_workspace_t &workspace = *m_pWorkspace;
        const f64 stripHeight = painter.fontMetrics().height() + 6.0;
        const QRectF strip( 0.0, height() - stripHeight, width(), stripHeight );
        painter.fillRect( strip, Token( workspace, "viewport.overlay.background" ) );
        painter.setPen( Token( workspace, "viewport.overlay.border" ) );
        painter.drawLine( strip.topLeft(), strip.topRight() );
        const QString operation = m_cameraDrag.kind == map_camera_gesture_t::LOOK ? QStringLiteral( "Looking" ) :
                                  m_cameraDrag.kind == map_camera_gesture_t::ORBIT ? QStringLiteral( "Orbiting" ) :
                                  m_cameraDrag.kind == map_camera_gesture_t::PAN ? QStringLiteral( "Panning" ) :
                                  m_cameraDrag.kind == map_camera_gesture_t::DOLLY ? QStringLiteral( "Dolly" ) : QStringLiteral( "Fly" );
        const QString text = QStringLiteral( "XYZ %1, %2, %3 \u00B7 %4 \u00B7 FOV %5\u00B0 \u00B7 %6 \u00B7 %7 u/s" )
                                 .arg( m_position.x, 0, 'f', 0 )
                                 .arg( m_position.y, 0, 'f', 0 )
                                 .arg( m_position.z, 0, 'f', 0 )
                                 .arg( CountText( workspace ) )
                                 .arg( Setting( "editor.camera.fov", 75.0 ), 0, 'f', 0 )
                                 .arg( operation )
                                 .arg( Setting( "editor.camera.move_speed", 1000.0 ), 0, 'f', 0 );
        painter.setPen( Token( workspace, "viewport.overlay.text" ) );
        painter.drawText( strip, Qt::AlignCenter, text );
    }

    map_workspace_t *m_pWorkspace{ nullptr };
    map_camera_grid_info_t m_gridInfo{};
    map_tool_t m_inputTool{ map_tool_t::SELECT }; // Held flight keys never cross the neutral boundary.
    map_element_mode_t m_inputMode{ map_element_mode_t::OBJECTS };
    math::vec3d_t m_position{};
    math::vec3d_t m_forward{};
    math::vec3d_t m_right{};
    math::vec3d_t m_up{};
    f64 m_gizmoScale{ 1.0 }; // Last effective scale, including inherited scope values.
    f64 m_moveSpeed{ 1000.0 }; // Last effective setting; scope replacement may inherit another value.
    f64 m_yaw{ 45.0 };   // Degrees; 0 looks along +x.
    f64 m_pitch{ -30.0 };
    QTimer m_timer{};
    QTimer m_speedFeedbackTimer{}; // Brief feedback remains visible with viewport metrics hidden.
    QElapsedTimer m_clock{};
    u32 m_moveKeys{ 0u };
    QSet<int> m_pressedNavigation; // Eligible physical presses observed by this pane.
    bool m_bCommandNavigation{ false }; // An effective explicit Ctrl/Meta movement chord is held.
    view_hover_t m_hover{};
    map_mesh_edge_hit_t m_hoverEdge{};
    map_mesh_vertex_hit_t m_hoverVertex{};
    geometry::brush_raycast_hit_t m_hoverFace{};
    u64 m_hoverMeshFace{ 0 };
    view_edit_drag_t m_drag{};
    struct camera_drag_t {
        map_camera_gesture_t kind{ map_camera_gesture_t::NONE };
        Qt::MouseButton button{ Qt::NoButton };
        QPointF start{};
        math::vec3d_t position{}, pivot{}, right{}, up{}, forward{};
        f64 yaw{ 0 }, pitch{ 0 }, unitsPerPixel{ 1 }, depth{ 512 };
        bool moved{ false }, contextClick{ false };
    } m_cameraDrag{};
    bool m_bSpaceHeld{ false };
    QPointF m_contextPress{};
    bool m_bContextCandidate{ false };
    map_bounds_t m_frameBounds{};
    bool_t m_bFramePending{ CY_FALSE };
    map_render_mode_t m_renderMode{ map_render_mode_t::WIREFRAME };
    bool m_bMeshEdges{ true };   // Face outlines over the fills (Hammer's F11).
    bool m_bWireOverlay{ false }; // Every edge on top, hidden ones too (Hammer's F8).
    mutable QHash<u64, QColor> m_materialColors{}; // Average colour per material reference.
};

// ---------------------------------------------------------------------------
// View workspace: four panes, TileEditor style
// ---------------------------------------------------------------------------

const char *TypeObjectName( map_view_type_t type ) noexcept
{
    switch ( type ) {
        case map_view_type_t::CAMERA: return "mapCameraView";
        case map_view_type_t::TOP: return "mapTopView";
        case map_view_type_t::FRONT: return "mapFrontView";
        case map_view_type_t::SIDE: return "mapSideView";
        case map_view_type_t::ASSETS: return "mapAssetView";
        case map_view_type_t::DATABASE: return "mapDatabaseView";
        case map_view_type_t::SHADERS: return "mapShaderView";
        case map_view_type_t::COUNT: break;
    }
    return "mapView";
}

QWidget *CreateView( QWidget *pParent, map_workspace_t *pWorkspace, map_view_type_t type )
{
    QWidget *pView = nullptr;
    switch ( type ) {
        case map_view_type_t::CAMERA: pView = new map_camera_view_t( pParent, pWorkspace ); break;
        case map_view_type_t::TOP: pView = new map_ortho_view_t( pParent, pWorkspace, map_ortho_axes_t::TOP ); break;
        case map_view_type_t::FRONT: pView = new map_ortho_view_t( pParent, pWorkspace, map_ortho_axes_t::FRONT ); break;
        case map_view_type_t::SIDE: pView = new map_ortho_view_t( pParent, pWorkspace, map_ortho_axes_t::SIDE ); break;
        case map_view_type_t::ASSETS:
        case map_view_type_t::DATABASE:
        case map_view_type_t::SHADERS:
        case map_view_type_t::COUNT: return nullptr;
    }
    pView->setObjectName( QString::fromLatin1( TypeObjectName( type ) ) );
    return pView;
}

class map_view_grid_t final : public QWidget {
public:
    map_view_grid_t( QWidget *pParent, map_workspace_t *pWorkspace ) : QWidget( pParent ), m_pWorkspace( pWorkspace )
    {
        setObjectName( QStringLiteral( "EditorViewGrid" ) );
        setAttribute( Qt::WA_StyledBackground );
        auto *pLayout = new QVBoxLayout( this );
        pLayout->setContentsMargins( 0, 0, 0, 0 );
        pLayout->setSpacing( 0 );
        auto *pSession = new QWidget( this );
        pSession->setObjectName( QStringLiteral( "masonSessionToolbar" ) );
        pSession->setAttribute( Qt::WA_StyledBackground );
        pSession->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
        auto *pSessionLayout = new QHBoxLayout( pSession );
        pSessionLayout->setContentsMargins( 4, 2, 4, 2 );
        pSessionLayout->setSpacing( 4 );
        pSessionLayout->addWidget( new QLabel( QStringLiteral( "Layout:" ), pSession ) );
        // Hammer's layout button: the current arrangement as an icon, every
        // arrangement as icons in its menu. Names live in the tooltips.
        m_pArrangementButton = new QToolButton( pSession );
        m_pArrangementButton->setObjectName( QStringLiteral( "masonLayoutPreset" ) );
        m_pArrangementButton->setPopupMode( QToolButton::InstantPopup );
        m_pArrangementButton->setToolButtonStyle( Qt::ToolButtonIconOnly );
        m_pArrangementButton->setIconSize( QSize( 20, 20 ) );
        m_pArrangementButton->setMenu( BuildArrangementMenu( m_pArrangementButton ) );
        pSessionLayout->addWidget( m_pArrangementButton );
        auto *pSessionOptions = new QToolButton( pSession );
        pSessionOptions->setObjectName( QStringLiteral( "masonSessionOptions" ) );
        pSessionOptions->setText( QStringLiteral( "Session Options" ) );
        pSessionOptions->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
        pSessionOptions->setPopupMode( QToolButton::InstantPopup );
        pSessionOptions->setMenu( new QMenu( pSessionOptions ) );
        pSessionLayout->addWidget( pSessionOptions );
        pSessionLayout->addStretch( 1 );
        pLayout->addWidget( pSession );
        m_pRows = new QSplitter( Qt::Vertical, this );
        m_pRows->setObjectName( QStringLiteral( "EditorViewRows" ) );
        m_pRows->setChildrenCollapsible( false );
        pLayout->addWidget( m_pRows, 1 );
        constexpr map_view_type_t kDefaults[MAP_VIEW_PANE_COUNT]{ map_view_type_t::CAMERA, map_view_type_t::TOP, map_view_type_t::FRONT,
                                                                  map_view_type_t::SIDE };
        for ( int row = 0; row < 2; ++row ) {
            m_pRow[row] = new QSplitter( Qt::Horizontal, m_pRows );
            m_pRow[row]->setObjectName( QStringLiteral( "EditorViewRow" ) );
            m_pRow[row]->setChildrenCollapsible( false );
            for ( int column = 0; column < 2; ++column ) { BuildPane( row * 2 + column, m_pRow[row], kDefaults[row * 2 + column] ); }
        }
        ApplyGutterWidth();
        // The quad's shared divider does not couple the cameras. Other
        // arrangements have different row populations and independent dividers.
        for ( int row = 0; row < 2; ++row ) {
            QObject::connect( m_pRow[row], &QSplitter::splitterMoved, this, [this, row]() {
                if ( m_arrangement == map_view_arrangement_t::FOUR && OpenPaneCount() == MAP_VIEW_PANE_COUNT && m_iMaximized < 0 ) {
                    m_pRow[1 - row]->setSizes( m_pRow[row]->sizes() );
                }
                RememberSplitterSizes();
            } );
        }
        QObject::connect( m_pRows, &QSplitter::splitterMoved, this, [this]() { RememberSplitterSizes(); } );
        QObject::connect( qApp, &QApplication::focusChanged, this, [this]( QWidget *, QWidget * ) { UpdateActive(); } );
        ( void )EditorSettings_AddListener( &m_pWorkspace->pGui->settings, &OnMenuSettingsChanged, this );
        ( void )MapWorkspace_AddListener( m_pWorkspace, &OnMenuWorkspaceChanged, this );
        ( void )gui::EditorGui_AddStyleListener( m_pWorkspace->pGui, &OnStyleChanged, this );
        SetArrangement( map_view_arrangement_t::FOUR );
        RefreshMenus();
    }

    ~map_view_grid_t() override
    {
        gui::EditorGui_RemoveStyleListener( m_pWorkspace->pGui, &OnStyleChanged, this );
        EditorSettings_RemoveListener( &m_pWorkspace->pGui->settings, &OnMenuSettingsChanged, this );
        MapWorkspace_RemoveListener( m_pWorkspace, &OnMenuWorkspaceChanged, this );
    }

    map_view_arrangement_t Arrangement() const { return m_arrangement; }

    static bool GeometryType( map_view_type_t type ) { return type <= map_view_type_t::SIDE; }

    void SetContentFactory( map_view_content_create_fn create, void *context )
    {
        m_pfnContent = create;
        m_pContentContext = context;
        RefreshMenus();
    }

    QWidget *EnsureView( int iPane, map_view_type_t type )
    {
        auto &pane = m_panes[iPane];
        auto *&view = pane.views[static_cast<int>( type )];
        if ( view != nullptr ) { return view; }
        view = GeometryType( type ) ? CreateView( pane.pPane, m_pWorkspace, type ) :
            m_pfnContent != nullptr ? m_pfnContent( pane.pPane, type, m_pContentContext ) : nullptr;
        if ( view != nullptr ) {
            view->setObjectName( QString::fromLatin1( TypeObjectName( type ) ) );
            view->installEventFilter( this );
            view->hide();
        }
        return view;
    }

    // A pane that is closed or not in the arrangement is never active; the
    // first visible pane stands in.
    int ActivePane() const
    {
        if ( PaneShown( m_iActive ) ) { return m_iActive; }
        for ( int i = 0; i < MAP_VIEW_PANE_COUNT; ++i ) {
            if ( PaneShown( i ) ) { return i; }
        }
        return 0;
    }

    bool PaneShown( int iPane ) const
    {
        return iPane >= 0 && iPane < MAP_VIEW_PANE_COUNT &&
            ( m_iMaximized >= 0 ? iPane == m_iMaximized : !m_panes[iPane].bClosed && PaneRow( iPane ) >= 0 );
    }

    void FocusPane( int iPane, Qt::FocusReason reason )
    {
        if ( !PaneShown( iPane ) ) { return; }
        QWidget *view = m_panes[iPane].pView;
        if ( view == nullptr ) { return; }
        m_iActive = iPane;
        QWidget *target = view->focusProxy() != nullptr ? view->focusProxy() : view;
        // Tool content roots need not accept Tab focus themselves. Follow
        // their existing focus chain to a visible, enabled editor control.
        if ( ( !GeometryType( m_panes[iPane].type ) && target == view ) ||
             !target->isVisibleTo( view ) || !target->isEnabled() || ( target->focusPolicy() & Qt::TabFocus ) == 0 ) {
            target = view;
            for ( QWidget *child = view->nextInFocusChain(); child != view; child = child->nextInFocusChain() ) {
                if ( view->isAncestorOf( child ) && child->isVisibleTo( view ) && child->isEnabled() &&
                     ( child->focusPolicy() & Qt::TabFocus ) != 0 ) { target = child; break; }
            }
        }
        target->setFocus( reason );
    }

    bool CycleActivePane()
    {
        const int current = ActivePane();
        for ( int offset = 1; offset < MAP_VIEW_PANE_COUNT; ++offset ) {
            const int next = ( current + offset ) % MAP_VIEW_PANE_COUNT;
            if ( !PaneShown( next ) ) { continue; }
            FocusPane( next, Qt::TabFocusReason );
            return true;
        }
        return false;
    }

    bool HandleToolCancel( QEvent *event )
    {
        if ( event == nullptr || ( event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress ) ||
             QApplication::activePopupWidget() != nullptr || QApplication::activeModalWidget() != nullptr ) { return false; }
        const int pane = ActivePane();
        if ( !GeometryType( m_panes[pane].type ) ) { return false; }
        auto *key = static_cast<QKeyEvent *>( event );
        if ( MapInput_ToolGestureKey( m_pWorkspace, key, m_panes[pane].type == map_view_type_t::CAMERA ) != map_tool_gesture_key_t::CANCEL ) { return false; }
        if ( event->type() == QEvent::KeyPress ) {
            QKeyEvent forwarded( QEvent::KeyPress, key->key(), key->modifiers(), key->text(), key->isAutoRepeat(), key->count() );
            QCoreApplication::sendEvent( m_panes[pane].pView, &forwarded );
        }
        event->accept(); return true;
    }

    bool CanNudgeSelection() const
    {
        auto *view = dynamic_cast<map_ortho_view_t *>( m_panes[ActivePane()].pView );
        return view != nullptr && view->CanNudgeSelection();
    }

    bool NudgeSelection( int horizontal, int vertical, bool fine )
    {
        auto *view = dynamic_cast<map_ortho_view_t *>( m_panes[ActivePane()].pView );
        return view != nullptr && view->NudgeSelection( horizontal, vertical, fine );
    }

    void SetActivePane( int iPane )
    {
        FocusPane( iPane, Qt::OtherFocusReason );
    }

    void SetArrangement( map_view_arrangement_t arrangement )
    {
        m_arrangement = arrangement;
        m_iMaximized = -1;
        for ( int i = 0; i < MAP_VIEW_PANE_COUNT; ++i ) { m_panes[i].bClosed = PaneRow( i ) < 0; }
        PlacePanes();
        ResetSplitterSizes();
        ApplyVisibility();
        RestoreSplitterSizes();
    }

    QByteArray SavePresentation()
    {
        RememberSplitterSizes();
        QJsonArray panes;
        for ( const pane_t &pane : m_panes ) {
            panes.append( QJsonObject{ { QStringLiteral( "type" ), static_cast<int>( pane.type ) },
                                       { QStringLiteral( "visible" ), !pane.bClosed },
                                       { QStringLiteral( "render" ), static_cast<int>( pane.render ) },
                                       { QStringLiteral( "mesh_edges" ), pane.bMeshEdges },
                                       { QStringLiteral( "wire_overlay" ), pane.bWireOverlay } } );
        }
        QJsonArray splitters;
        for ( const QList<int> &sizes : m_splitSizes ) {
            QJsonArray values;
            for ( int size : sizes ) { values.append( size ); }
            splitters.append( values );
        }
        return QJsonDocument( QJsonObject{ { QStringLiteral( "version" ), 2 },
                                           { QStringLiteral( "arrangement" ), static_cast<int>( m_arrangement ) },
                                           { QStringLiteral( "maximized" ), m_iMaximized },
                                           { QStringLiteral( "panes" ), panes },
                                           { QStringLiteral( "splitters" ), splitters } } ).toJson( QJsonDocument::Compact );
    }

    struct presentation_t {
        map_view_arrangement_t arrangement{};
        int maximized{ -1 };
        int types[MAP_VIEW_PANE_COUNT]{};
        bool visible[MAP_VIEW_PANE_COUNT]{};
        int renders[MAP_VIEW_PANE_COUNT]{};
        bool meshEdges[MAP_VIEW_PANE_COUNT]{ true, true, true, true };
        bool wireOverlay[MAP_VIEW_PANE_COUNT]{};
        QList<int> sizes[3];
    };

    static bool ReadPresentation( const QByteArray &state, presentation_t &out )
    {
        // Validate the entire envelope before touching a widget. Restrict
        // sizes so a corrupt preferences file cannot request absurd geometry.
        if ( state.isEmpty() || state.size() > 16384 ) { return false; }
        QJsonParseError error{};
        const QJsonDocument document = QJsonDocument::fromJson( state, &error );
        if ( error.error != QJsonParseError::NoError || !document.isObject() ) { return false; }
        const QJsonObject object = document.object();
        const auto integer = []( const QJsonValue &value, int minimum, int maximum, int &out ) {
            if ( !value.isDouble() ) { return false; }
            const double number = value.toDouble();
            if ( !std::isfinite( number ) || std::floor( number ) != number || number < minimum || number > maximum ) { return false; }
            out = static_cast<int>( number );
            return true;
        };
        int version = 0, arrangement = 0, maximized = -1;
        if ( !integer( object.value( QStringLiteral( "version" ) ), 1, 2, version ) ||
             !integer( object.value( QStringLiteral( "arrangement" ) ), 0, static_cast<int>( map_view_arrangement_t::COUNT ) - 1, arrangement ) ||
             !integer( object.value( QStringLiteral( "maximized" ) ), -1, MAP_VIEW_PANE_COUNT - 1, maximized ) ) {
            return false;
        }
        const QJsonArray panes = object.value( QStringLiteral( "panes" ) ).toArray();
        if ( panes.size() != MAP_VIEW_PANE_COUNT ) { return false; }
        auto &types = out.types;
        auto &visible = out.visible;
        int open = 0;
        const auto preset = static_cast<map_view_arrangement_t>( arrangement );
        for ( int i = 0; i < MAP_VIEW_PANE_COUNT; ++i ) {
            if ( !panes[i].isObject() ) { return false; }
            const QJsonObject pane = panes[i].toObject();
            const QJsonValue show = pane.value( QStringLiteral( "visible" ) );
            const int maxType = version == 1 ? static_cast<int>( map_view_type_t::SIDE ) : static_cast<int>( map_view_type_t::COUNT ) - 1;
            if ( !show.isBool() || !integer( pane.value( QStringLiteral( "type" ) ), 0, maxType, types[i] ) ) {
                return false;
            }
            visible[i] = show.toBool();
            // 3D modes arrived later: absent means the defaults; present
            // must be valid, like everything else here.
            if ( pane.contains( QStringLiteral( "render" ) ) &&
                 !integer( pane.value( QStringLiteral( "render" ) ), 0, static_cast<int>( map_render_mode_t::COUNT ) - 1, out.renders[i] ) ) {
                return false;
            }
            for ( const auto &[pKey, pFlag] : { std::pair{ "mesh_edges", &out.meshEdges[i] }, std::pair{ "wire_overlay", &out.wireOverlay[i] } } ) {
                const QJsonValue flag = pane.value( QString::fromLatin1( pKey ) );
                if ( flag.isUndefined() ) { continue; }
                if ( !flag.isBool() ) { return false; }
                *pFlag = flag.toBool();
            }
            if ( visible[i] && PaneRow( preset, i ) < 0 ) { return false; }
            open += visible[i] ? 1 : 0;
        }
        if ( open == 0 || ( maximized >= 0 && !visible[maximized] ) ) { return false; }
        const QJsonArray splitters = object.value( QStringLiteral( "splitters" ) ).toArray();
        if ( splitters.size() != 3 ) { return false; }
        auto &sizes = out.sizes;
        for ( int splitter = 0; splitter < 3; ++splitter ) {
            if ( !splitters[splitter].isArray() ) { return false; }
            const QJsonArray values = splitters[splitter].toArray();
            int expected = splitter == 0 ? 2 : 0;
            if ( splitter != 0 ) {
                for ( int i = 0; i < MAP_VIEW_PANE_COUNT; ++i ) { expected += PaneRow( preset, i ) == splitter - 1 ? 1 : 0; }
            }
            if ( values.size() != expected ) { return false; }
            for ( const QJsonValue &value : values ) {
                int size = 0;
                if ( !integer( value, 1, 1000000, size ) ) { return false; }
                sizes[splitter].append( size );
            }
        }
        out.arrangement = preset;
        out.maximized = maximized;
        return true;
    }

    bool RestorePresentation( const QByteArray &state )
    {
        presentation_t presentation;
        if ( !ReadPresentation( state, presentation ) ) { return false; }
        // Prepare content before changing any visible layout. Unavailable
        // providers leave the presentation intact; successful hidden widgets
        // remain cached and owned for a later retry.
        for ( int i = 0; i < MAP_VIEW_PANE_COUNT; ++i ) {
            if ( EnsureView( i, static_cast<map_view_type_t>( presentation.types[i] ) ) == nullptr ) { return false; }
        }
        m_arrangement = presentation.arrangement;
        m_iMaximized = presentation.maximized;
        for ( int i = 0; i < MAP_VIEW_PANE_COUNT; ++i ) {
            m_panes[i].render = static_cast<map_render_mode_t>( presentation.renders[i] );
            m_panes[i].bMeshEdges = presentation.meshEdges[i];
            m_panes[i].bWireOverlay = presentation.wireOverlay[i];
            SetType( i, static_cast<map_view_type_t>( presentation.types[i] ) );
            ApplyCameraState( i );
            UpdateViewChecks( i );
            m_panes[i].bClosed = !presentation.visible[i];
        }
        PlacePanes();
        for ( int i = 0; i < 3; ++i ) { m_splitSizes[i] = presentation.sizes[i]; }
        ApplyVisibility();
        RestoreSplitterSizes();
        return true;
    }

    void SetType( int iPane, map_view_type_t type )
    {
        pane_t &pane = m_panes[iPane];
        if ( pane.pView != nullptr && pane.type == type ) { return; }
        QWidget *next = EnsureView( iPane, type );
        if ( next == nullptr ) { UpdateViewChecks( iPane ); return; }
        const QWidget *focused = QApplication::focusWidget();
        const bool transferFocus = pane.pView != nullptr && focused != nullptr &&
            ( focused == pane.pView || pane.pView->isAncestorOf( focused ) );
        if ( pane.pView != nullptr ) {
            // A cached viewport must stop its gesture/navigation just as a
            // replaced viewport did, even when it did not hold focus.
            if ( GeometryType( pane.type ) ) {
                QFocusEvent leave( QEvent::FocusOut, Qt::OtherFocusReason );
                QCoreApplication::sendEvent( pane.pView, &leave );
            }
            pane.pView->clearFocus();
            pane.pView->hide();
            pane.pLayout->removeWidget( pane.pView );
        }
        pane.type = type;
        pane.pView = next;
        ApplyBackground( pane );
        const bool geometry = GeometryType( type );
        pane.pLayout->setRowStretch( 0, geometry ? 1 : 0 );
        pane.pLayout->setRowStretch( 1, geometry ? 0 : 1 );
        pane.pLayout->addWidget( pane.pView, geometry ? 0 : 1, 0 );
        pane.pView->show();
        pane.pHeader->raise(); // The header floats over the view.
        pane.pTitle->setText( MapViews_TypeTitle( type ) );
        ApplyCameraState( iPane );
        UpdateViewChecks( iPane );
        RefreshMenus();
        // A view-mode shortcut can replace the widget that received it. Keep
        // keyboard editing in that pane, without taking an inspector's focus.
        if ( transferFocus ) { FocusPane( iPane, Qt::OtherFocusReason ); }
    }

    // The tab's choice: a 2D projection, or the 3D view in one of its modes.
    void SetView( int iPane, map_view_type_t type, map_render_mode_t render )
    {
        if ( iPane < 0 || iPane >= MAP_VIEW_PANE_COUNT ) { return; }
        pane_t &pane = m_panes[iPane];
        if ( type == map_view_type_t::CAMERA ) { pane.render = render; }
        SetType( iPane, type );
        ApplyCameraState( iPane );
        UpdateViewChecks( iPane );
    }

    map_render_mode_t PaneRender( int iPane ) const { return iPane >= 0 && iPane < MAP_VIEW_PANE_COUNT ? m_panes[iPane].render : map_render_mode_t::WIREFRAME; }
    bool PaneMeshEdges( int iPane ) const { return iPane >= 0 && iPane < MAP_VIEW_PANE_COUNT && m_panes[iPane].bMeshEdges; }
    bool PaneWireOverlay( int iPane ) const { return iPane >= 0 && iPane < MAP_VIEW_PANE_COUNT && m_panes[iPane].bWireOverlay; }

    void SetPaneMeshEdges( int iPane, bool bOn )
    {
        if ( iPane < 0 || iPane >= MAP_VIEW_PANE_COUNT ) { return; }
        m_panes[iPane].bMeshEdges = bOn;
        ApplyCameraState( iPane );
    }

    void SetPaneWireOverlay( int iPane, bool bOn )
    {
        if ( iPane < 0 || iPane >= MAP_VIEW_PANE_COUNT ) { return; }
        m_panes[iPane].bWireOverlay = bOn;
        ApplyCameraState( iPane );
    }

    void ApplyCameraState( int iPane )
    {
        auto &pane = m_panes[iPane];
        if ( auto *pCamera = dynamic_cast<map_camera_view_t *>( pane.pView ) ) {
            pCamera->SetRenderMode( pane.render );
            pCamera->SetMeshEdges( pane.bMeshEdges );
            pCamera->SetWireOverlay( pane.bWireOverlay );
        }
        if ( pane.pMeshEdges != nullptr ) {
            const QSignalBlocker a( pane.pMeshEdges ), b( pane.pWireOverlay );
            pane.pMeshEdges->setChecked( pane.bMeshEdges );
            pane.pWireOverlay->setChecked( pane.bWireOverlay );
        }
    }

    void UpdateViewChecks( int iPane )
    {
        auto &pane = m_panes[iPane];
        for ( QAction *pAction : pane.pTypes->actions() ) {
            const int data = pAction->data().toInt();
            const auto type = static_cast<map_view_type_t>( data / 16 );
            const auto render = static_cast<map_render_mode_t>( data % 16 );
            pAction->setChecked( type == pane.type && ( type != map_view_type_t::CAMERA || render == pane.render ) );
        }
    }

    // One background per 2D projection of a pane: a floor plan under Top,
    // an elevation under Front, kept while the pane shows something else.
    void SetBackground( int iPane, const QImage &image, const QRectF &world, f64 opacity )
    {
        pane_t &pane = m_panes[iPane];
        const int slot = BackgroundSlot( pane.type );
        if ( slot < 0 ) { return; }
        pane.backgrounds[slot] = background_t{ image, world, opacity };
        ApplyBackground( pane );
    }

    void ClearBackground( int iPane )
    {
        pane_t &pane = m_panes[iPane];
        const int slot = BackgroundSlot( pane.type );
        if ( slot < 0 ) { return; }
        pane.backgrounds[slot] = background_t{};
        ApplyBackground( pane );
    }

    bool HasBackground( int iPane ) const
    {
        const int slot = BackgroundSlot( m_panes[iPane].type );
        return slot >= 0 && !m_panes[iPane].backgrounds[slot].image.isNull();
    }

    map_view_type_t Type( int iPane ) const { return m_panes[iPane].type; }
    QWidget *View( int iPane ) const { return m_panes[iPane].pView; }
    QString Title( int iPane ) const { return m_panes[iPane].pTitle->text(); }
    int Maximized() const { return m_iMaximized; }
    bool IsPaneVisible( int iPane ) const { return !m_panes[iPane].bClosed; }

    void SetMaximized( int iPane )
    {
        RememberSplitterSizes();
        if ( iPane >= 0 && PaneRow( iPane ) < 0 ) { ExpandToFour(); }
        if ( iPane >= 0 ) { m_panes[iPane].bClosed = false; }
        m_iMaximized = iPane;
        ApplyVisibility();
        RestoreSplitterSizes();
        if ( iPane >= 0 ) { m_panes[iPane].pView->setFocus( Qt::OtherFocusReason ); }
    }

    void SetPaneVisible( int iPane, bool bVisible )
    {
        if ( !bVisible && !m_panes[iPane].bClosed && OpenPaneCount() == 1 ) { return; }
        RememberSplitterSizes();
        if ( bVisible && PaneRow( iPane ) < 0 ) { ExpandToFour(); }
        m_panes[iPane].bClosed = !bVisible;
        if ( !bVisible && m_iMaximized == iPane ) { m_iMaximized = -1; }
        ApplyVisibility();
        RestoreSplitterSizes();
    }

    void ShowAll()
    {
        SetArrangement( map_view_arrangement_t::FOUR );
    }

    void FramePane( int iPane )
    {
        const map_bounds_t bounds = PaneFrameBounds( *m_pWorkspace );
        QWidget *pView = m_panes[iPane].pView;
        if ( auto *pOrtho = dynamic_cast<map_ortho_view_t *>( pView ) ) { pOrtho->FrameBounds( bounds ); }
        if ( auto *pCamera = dynamic_cast<map_camera_view_t *>( pView ) ) { pCamera->Frame( bounds ); }
    }

protected:
    bool eventFilter( QObject *pWatched, QEvent *pEvent ) override
    {
        if ( pEvent->type() == QEvent::Enter ) {
            for ( pane_t &pane : m_panes ) {
                if ( pWatched == pane.pHeader && GeometryType( pane.type ) ) { ActivateHoveredView( *pane.pView, *m_pWorkspace ); }
            }
        }
        if ( pEvent->type() == QEvent::ContextMenu ) {
            auto *pContext = static_cast<QContextMenuEvent *>( pEvent );
            for ( int i = 0; i < MAP_VIEW_PANE_COUNT; ++i ) {
                const pane_t &pane = m_panes[i];
                const bool header = pWatched == pane.pHeader;
                const bool view = pWatched == pane.pView;
                // Mouse RMB is camera look; the camera posts its own event
                // (reason Other) for a right click that did not drag.
                if ( header || ( view && GeometryType( pane.type ) && ( pane.type != map_view_type_t::CAMERA || pContext->reason() != QContextMenuEvent::Mouse ) ) ) {
                    const QPoint position = pContext->reason() == QContextMenuEvent::Keyboard
                        ? pane.pHeader->mapToGlobal( QPoint( 0, pane.pHeader->height() ) ) : pContext->globalPos();
                    pane.pOptionsMenu->popup( position );
                    pEvent->accept();
                    return true;
                }
            }
        }
        if ( pEvent->type() == QEvent::MouseButtonDblClick ) {
            const auto *mouse = static_cast<QMouseEvent *>( pEvent );
            if ( mouse->button() != Qt::LeftButton ) { return QWidget::eventFilter( pWatched, pEvent ); }
            for ( int i = 0; i < MAP_VIEW_PANE_COUNT; ++i ) {
                if ( pWatched == m_panes[i].pHeader ) {
                    SetMaximized( m_iMaximized == i ? -1 : i );
                    return true;
                }
                if ( pWatched == m_panes[i].pView && GeometryType( m_panes[i].type ) ) {
                    if ( MapWorkspace_HasBlockPreview( m_pWorkspace ) ) { pEvent->accept(); return true; }
                    u64 id = 0u;
                    map_mesh_edge_hit_t edge{};
                    map_mesh_vertex_hit_t vertex{};
                    const bool edgeMode = MeshEdgePickingActive( *m_pWorkspace );
                    const bool vertexMode = MeshVertexPickingActive( *m_pWorkspace );
                    bool complete = true;
                    if ( auto *ortho = dynamic_cast<map_ortho_view_t *>( m_panes[i].pView ) ) {
                        if ( ortho->NavigationActive() ) { return true; }
                        if ( edgeMode ) { ( void )ortho->PickMeshEdge( mouse->position(), &edge ); }
                        else if ( vertexMode ) { ( void )ortho->PickMeshVertex( mouse->position(), &vertex ); }
                        else { id = ortho->Pick( mouse->position() ); }
                    }
                    if ( auto *camera = dynamic_cast<map_camera_view_t *>( m_panes[i].pView ) ) {
                        if ( camera->NavigationActive() ) { return true; }
                        if ( edgeMode ) { ( void )camera->PickMeshEdge( mouse->position(), &edge, &complete ); }
                        else if ( vertexMode ) { ( void )camera->PickMeshVertex( mouse->position(), &vertex, &complete ); }
                        else { id = camera->Pick( mouse->position() ); }
                    }
                    if ( edgeMode && edge.object != 0u ) {
                        // Retain the selected component while opening its
                        // mesh inspector; selecting the root would clear it.
                        if ( MapWorkspace_SelectMeshEdge( m_pWorkspace, edge.object, edge.edge ) ) { OpenViewObjectInspector( *m_pWorkspace ); }
                    } else if ( vertexMode && vertex.object != 0u ) {
                        if ( MapWorkspace_SelectMeshVertex( m_pWorkspace, vertex.object, vertex.vertex ) ) { OpenViewObjectInspector( *m_pWorkspace ); }
                    } else if ( complete ) {
                        if ( id != 0u ) { InspectViewObject( *m_pWorkspace, id ); }
                        else { m_panes[i].pOptionsMenu->popup( mouse->globalPosition().toPoint() ); }
                    }
                    pEvent->accept();
                    return true;
                }
            }
        }
        return QWidget::eventFilter( pWatched, pEvent );
    }

private:
    struct display_action_t {
        QAction *pAction{ nullptr };
        const char *pPath{ nullptr };
        const char *pChoice{ nullptr }; // Null denotes a boolean setting.
    };

    static void OnMenuSettingsChanged( void *pContext, string_view_t ) noexcept
    {
        static_cast<map_view_grid_t *>( pContext )->RefreshMenus();
    }

    static void OnMenuWorkspaceChanged( void *pContext, u32 ) noexcept
    {
        static_cast<map_view_grid_t *>( pContext )->RefreshMenus();
    }

    // Hammer's panes sit in a dark frame with a clear gutter between them;
    // a hairline splitter made four views read as one.
    void ApplyGutterWidth()
    {
        const int width = std::max( 1, qRound( gui::EditorStyle_Metric( m_pWorkspace->pGui->style, "viewport.splitter_width", 6.0 ) ) );
        m_pRows->setHandleWidth( width );
        for ( QSplitter *pRow : m_pRow ) { pRow->setHandleWidth( width ); }
    }

    static void OnStyleChanged( void *pContext ) noexcept
    {
        auto *grid = static_cast<map_view_grid_t *>( pContext );
        grid->ApplyGutterWidth();
        const gui::editor_style_t &style = grid->m_pWorkspace->pGui->style;
        grid->m_pGridAction->setIcon( gui::EditorStyle_Icon( style, "grid-show" ) );
        grid->m_pShowAllAction->setIcon( gui::EditorStyle_Icon( style, "view-quad" ) );
        grid->RefreshArrangementIcons();
        // Icon engines hold their palette at construction. Replace only the
        // icons: a theme change must not frame, move, or recreate any view.
        for ( int i = 0; i < MAP_VIEW_PANE_COUNT; ++i ) {
            auto &pane = grid->m_panes[i];
            for ( QAction *action : pane.pTypes->actions() ) {
                action->setIcon( gui::EditorStyle_Icon( style, action->property( "viewIcon" ).toByteArray().constData() ) );
            }
            pane.pFrameAction->setIcon( gui::EditorStyle_Icon( style, "view-frame" ) );
            pane.pMaximizeAction->setIcon( gui::EditorStyle_Icon( style, grid->m_iMaximized == i ? "view-quad" : "view-maximize", true ) );
            pane.pView->update();
        }
    }

    settings_scope_t DisplayWriteScope() const
    {
        const settings_registry_t &settings = m_pWorkspace->pGui->settings;
        // The narrowest attached scope wins resolution, so a quick toggle
        // must write there rather than silently lose to a project override.
        for ( usize i = 0u; i < static_cast<usize>( settings_scope_t::COUNT ); ++i ) {
            if ( settings.scopes[i] != nullptr ) { return static_cast<settings_scope_t>( i ); }
        }
        return settings_scope_t::DEFAULT;
    }

    void WriteDisplaySetting( const char *pPath, const char *pChoice, bool checked )
    {
        settings_registry_t &settings = m_pWorkspace->pGui->settings;
        const setting_descriptor_t *descriptor = EditorSettings_Find( &settings, StringView_FromCString( pPath ) );
        const settings_scope_t scope = DisplayWriteScope();
        if ( descriptor != nullptr && scope != settings_scope_t::DEFAULT ) {
            setting_value_t value{};
            value.type = descriptor->type;
            if ( pChoice != nullptr ) { value.text = StringView_FromCString( pChoice ); }
            else { value.bValue = checked; }
            ( void )EditorSettings_Write( &settings, scope, *descriptor, value );
        }
        RefreshMenus(); // A rejected write never leaves a misleading checkmark.
    }

    QAction *AddDisplayAction( QMenu *pMenu, const QString &label, const char *pPath, const char *pChoice = nullptr )
    {
        QAction *action = pMenu->addAction( label );
        action->setObjectName( QStringLiteral( "EditorViewSetting_%1%2" ).arg( QString::fromLatin1( pPath ),
            pChoice != nullptr ? QStringLiteral( ".%1" ).arg( QString::fromLatin1( pChoice ) ) : QString() ) );
        action->setCheckable( true );
        m_displayActions.append( { action, pPath, pChoice } );
        QObject::connect( action, &QAction::triggered, this, [this, pPath, pChoice]( bool checked ) {
            WriteDisplaySetting( pPath, pChoice, checked );
        } );
        return action;
    }

    void ApplyCleanView( bool perspective )
    {
        // This is an explicit menu operation on existing settings, not a
        // second display mode. Keep the user's dimensions, axes and grid.
        const char *paths2D[]{ "editor.viewport.show_rulers", "editor.viewport.show_selection_bounds",
            "editor.viewport.show_selection_vertices", "editor.viewport.show_metrics" };
        const char *paths3D[]{ "editor.viewport.perspective.show_selection_bounds",
            "editor.viewport.perspective.show_selection_vertices", "editor.viewport.perspective.show_metrics" };
        if ( perspective ) { for ( const char *path : paths3D ) { WriteDisplaySetting( path, nullptr, false ); } }
        else { for ( const char *path : paths2D ) { WriteDisplaySetting( path, nullptr, false ); } }
        WriteDisplaySetting( perspective ? "editor.viewport.perspective.entity_names" : "editor.viewport.entity_names",
                             perspective ? "selected" : "always", false );
        WriteDisplaySetting( perspective ? "editor.viewport.perspective.io_lines" : "editor.viewport.io_lines", "selected", false );
    }

    QMenu *DrawingAidsMenu( bool perspective )
    {
        QMenu *&menu = m_pDrawingAids[perspective ? 1 : 0];
        if ( menu != nullptr ) { return menu; }
        menu = new QMenu( perspective ? QStringLiteral( "3D Drawing Aids" ) : QStringLiteral( "2D Drawing Aids — All Orthographic Views" ), this );
        menu->setObjectName( perspective ? QStringLiteral( "EditorViewDrawingAids3D" ) : QStringLiteral( "EditorViewDrawingAids" ) );
        menu->setToolTipsVisible( true );
        QAction *clean = menu->addAction( QStringLiteral( "Clean View" ) );
        clean->setObjectName( perspective ? QStringLiteral( "EditorViewClean3D" ) : QStringLiteral( "EditorViewClean2D" ) );
        clean->setToolTip( QStringLiteral( "Hide bounds, vertex cues and metrics%1; keep dimensions, axes and grid. Show %2 entity names and selected connections." )
            .arg( perspective ? QString() : QStringLiteral( " and coordinate rulers" ), perspective ? QStringLiteral( "selected" ) : QStringLiteral( "all" ) ) );
        m_pCleanViewActions[perspective ? 1 : 0] = clean;
        QObject::connect( clean, &QAction::triggered, this, [this, perspective]() { ApplyCleanView( perspective ); } );
        menu->addSeparator();
        if ( perspective ) {
            AddDisplayAction( menu, QStringLiteral( "Floor Grid" ), "editor.grid.show_3d" );
            AddDisplayAction( menu, QStringLiteral( "Surface Grid" ), "editor.grid.show_surface_3d" );
        }
        else {
            m_pGridAction = menu->addAction( QStringLiteral( "Show Grid" ) );
            m_pGridAction->setObjectName( QStringLiteral( "EditorViewShowGrid" ) );
            m_pGridAction->setIcon( gui::EditorStyle_Icon( m_pWorkspace->pGui->style, "grid-show" ) );
            m_pGridAction->setCheckable( true );
            QObject::connect( m_pGridAction, &QAction::triggered, this, [this]( bool checked ) {
                const auto id = StringView_FromCString( "map.grid.show" );
                const command_registry_t &commands = m_pWorkspace->pGui->commands;
                if ( EditorCommands_Find( &commands, id ) != nullptr ) { ( void )EditorCommands_Execute( &commands, id, {} ); }
                else { MapWorkspace_SetGridVisible( m_pWorkspace, checked ); }
                RefreshMenus();
            } );
        }
        AddDisplayAction( menu, QStringLiteral( "Adaptive Grid Spacing" ), perspective ? "editor.grid.adaptive_3d" : "editor.grid.adaptive" );
        AddDisplayAction( menu, QStringLiteral( "Orientation Axes" ), perspective ? "editor.viewport.perspective.show_axes" : "editor.viewport.show_axes" );
        AddDisplayAction( menu, QStringLiteral( "World Origin Axes" ), perspective ? "editor.viewport.perspective.center_axes" : "editor.viewport.center_axes" );
        if ( !perspective ) { AddDisplayAction( menu, QStringLiteral( "Coordinate Rulers" ), "editor.viewport.show_rulers" ); }
        menu->addSeparator();
        AddDisplayAction( menu, QStringLiteral( "Selection Bounds" ), perspective ? "editor.viewport.perspective.show_selection_bounds" : "editor.viewport.show_selection_bounds" );
        AddDisplayAction( menu, QStringLiteral( "Selection Dimensions" ), perspective ? "editor.viewport.perspective.show_selection_dimensions" : "editor.viewport.show_selection_dimensions" );
        AddDisplayAction( menu, QStringLiteral( "Selection Vertex Cues" ), perspective ? "editor.viewport.perspective.show_selection_vertices" : "editor.viewport.show_selection_vertices" );
        AddDisplayAction( menu, QStringLiteral( "Ghost Hidden Geometry" ), perspective ? "editor.viewport.perspective.ghost_hidden" : "editor.viewport.ghost_hidden" );
        menu->addSeparator();
        const std::pair<const char *, const char *> policies[]{
            { "Entity Names", perspective ? "editor.viewport.perspective.entity_names" : "editor.viewport.entity_names" },
            { "Entity Connections", perspective ? "editor.viewport.perspective.io_lines" : "editor.viewport.io_lines" }
        };
        for ( const auto &policy : policies ) {
            QMenu *policyMenu = menu->addMenu( QString::fromLatin1( policy.first ) );
            policyMenu->setToolTipsVisible( true );
            policyMenu->setObjectName( QStringLiteral( "EditorViewPolicy_%1" ).arg( QString::fromLatin1( policy.second ) ) );
            auto *group = new QActionGroup( policyMenu );
            group->setExclusive( true );
            group->addAction( AddDisplayAction( policyMenu, QStringLiteral( "Never" ), policy.second, "never" ) );
            group->addAction( AddDisplayAction( policyMenu, QStringLiteral( "Selected Objects" ), policy.second, "selected" ) );
            group->addAction( AddDisplayAction( policyMenu, QStringLiteral( "Always" ), policy.second, "always" ) );
        }
        menu->addSeparator();
        AddDisplayAction( menu, QStringLiteral( "Viewport Metrics" ), perspective ? "editor.viewport.perspective.show_metrics" : "editor.viewport.show_metrics" );
        QObject::connect( menu, &QMenu::aboutToShow, this, [this]() { RefreshMenus(); } );
        return menu;
    }

    void RefreshMenus()
    {
        if ( m_pGridAction != nullptr ) {
            const QSignalBlocker blocker( m_pGridAction );
            m_pGridAction->setChecked( m_pWorkspace->bGridVisible );
            const auto id = StringView_FromCString( "map.grid.show" );
            const command_registry_t &commands = m_pWorkspace->pGui->commands;
            m_pGridAction->setEnabled( EditorCommands_Find( &commands, id ) == nullptr || ( EditorCommands_State( &commands, id ) & COMMAND_STATE_ENABLED ) != 0u );
        }
        const settings_scope_t scope = DisplayWriteScope();
        const settings_registry_t &settings = m_pWorkspace->pGui->settings;
        for ( QAction *action : m_pCleanViewActions ) {
            if ( action != nullptr ) { action->setEnabled( scope != settings_scope_t::DEFAULT ); }
        }
        for ( const display_action_t &binding : m_displayActions ) {
            const QSignalBlocker blocker( binding.pAction );
            const setting_descriptor_t *descriptor = EditorSettings_Find( &settings, StringView_FromCString( binding.pPath ) );
            binding.pAction->setEnabled( descriptor != nullptr && scope != settings_scope_t::DEFAULT );
            bool checked = false;
            if ( descriptor != nullptr ) {
                const setting_value_t value = EditorSettings_Resolve( &settings, *descriptor ).value;
                checked = binding.pChoice != nullptr ? StringView_Equals( value.text, StringView_FromCString( binding.pChoice ) ) : value.bValue;
            }
            binding.pAction->setChecked( checked );
            binding.pAction->setToolTip( scope == settings_scope_t::DEFAULT ? QStringLiteral( "No settings store is attached" )
                : QStringLiteral( "%1 · %2 settings" ).arg( QString::fromLatin1( binding.pPath ).contains( QStringLiteral( "perspective" ) ) || QString::fromLatin1( binding.pPath ).endsWith( QStringLiteral( "_3d" ) ) ? QStringLiteral( "3D views" ) : QStringLiteral( "All orthographic views" ), QString::fromLatin1( EditorSettings_ScopeName( scope ) ) ) );
        }
        for ( int i = 0; i < MAP_VIEW_PANE_COUNT; ++i ) {
            pane_t &pane = m_panes[i];
            if ( pane.pFrameAction == nullptr ) { continue; }
            const bool geometry = GeometryType( pane.type );
            if ( pane.pOrthoAids != nullptr ) { pane.pOrthoAids->setVisible( geometry && pane.type != map_view_type_t::CAMERA ); }
            if ( pane.pBackgroundMenu != nullptr ) { pane.pBackgroundMenu->menuAction()->setVisible( geometry && pane.type != map_view_type_t::CAMERA ); }
            if ( pane.pCameraAids != nullptr ) { pane.pCameraAids->setVisible( pane.type == map_view_type_t::CAMERA ); }
            pane.pFrameAction->setVisible( geometry );
            for ( QAction *action : pane.pTypes->actions() ) {
                const auto type = static_cast<map_view_type_t>( action->data().toInt() / 16 );
                if ( !GeometryType( type ) ) { action->setEnabled( m_pfnContent != nullptr || pane.views[static_cast<int>( type )] != nullptr ); }
            }
            pane.pFrameAction->setText( EditorSelection_Count( &m_pWorkspace->selection ) == 0u
                ? QStringLiteral( "Frame Map in This View" ) : QStringLiteral( "Frame Selection in This View" ) );
            pane.pMaximizeAction->setText( m_iMaximized == i ? QStringLiteral( "Restore Views" ) : QStringLiteral( "Maximize This View" ) );
            {
                const QSignalBlocker blocker( pane.pMaximizeAction );
                pane.pMaximizeAction->setChecked( m_iMaximized == i );
            }
            pane.pMaximizeAction->setToolTip( m_iMaximized == i ? QStringLiteral( "Restore the previous view arrangement" )
                                                               : QStringLiteral( "Maximize this view; click again to restore the arrangement" ) );
            pane.pCloseAction->setEnabled( OpenPaneCount() > 1 );
            if ( pane.pCameraControls != nullptr ) { pane.pCameraControls->menuAction()->setVisible( pane.type == map_view_type_t::CAMERA ); }
            if ( pane.pOrthoControls != nullptr ) { pane.pOrthoControls->menuAction()->setVisible( geometry && pane.type != map_view_type_t::CAMERA ); }
            if ( pane.pMeshEdges != nullptr ) {
                pane.pMeshEdges->setVisible( pane.type == map_view_type_t::CAMERA );
                pane.pWireOverlay->setVisible( pane.type == map_view_type_t::CAMERA );
                pane.pMeshEdges->setEnabled( pane.render != map_render_mode_t::WIREFRAME );
                pane.pWireOverlay->setEnabled( pane.render != map_render_mode_t::WIREFRAME );
            }
            // View and toggle items show their keys after a tab, QMenu's
            // shortcut column, without registering a second shortcut.
            QList<QAction *> keyed = pane.pTypes->actions();
            if ( pane.pMeshEdges != nullptr ) { keyed << pane.pMeshEdges << pane.pWireOverlay; }
            for ( QAction *pAction : keyed ) {
                const QString label = pAction->text().section( QLatin1Char( '\t' ), 0, 0 );
                const QString keys = CommandKeys( pAction->property( "viewCommand" ).toByteArray().constData() );
                pAction->setText( keys.isEmpty() ? label : label + QLatin1Char( '\t' ) + keys );
            }
            bool bAnyWindow = false;
            for ( QAction *pAction : pane.windowActions ) {
                const auto id = StringView_FromCString( pAction->property( "viewCommand" ).toByteArray().constData() );
                const command_registry_t &registry = m_pWorkspace->pGui->commands;
                const bool bRegistered = EditorCommands_Find( &registry, id ) != nullptr;
                pAction->setVisible( bRegistered );
                pAction->setEnabled( bRegistered && ( EditorCommands_State( &registry, id ) & COMMAND_STATE_ENABLED ) != 0u );
                bAnyWindow = bAnyWindow || bRegistered;
            }
            if ( pane.pWindowsSeparator != nullptr ) { pane.pWindowsSeparator->setVisible( bAnyWindow ); }
        }
    }

    // The command's first chord in the map context (then global), in the
    // platform's notation; empty when unbound.
    QString CommandKeys( const char *pCommand ) const
    {
        const gui::editor_gui_t &gui = *m_pWorkspace->pGui;
        if ( pCommand == nullptr || *pCommand == '\0' || gui.nKeymapChain == 0u ) { return {}; }
        for ( const char *pContext : { "map", "global" } ) {
            keymap_binding_t binding{};
            const keymap_lookup_t lookup = EditorKeymap_FindBindingOn( gui.keymapChain, gui.nKeymapChain, EditorKeymap_HostPlatform(),
                                                                       StringView_FromCString( pContext ), StringView_FromCString( pCommand ), &binding );
            if ( lookup == keymap_lookup_t::BOUND && binding.nChords > 0u ) {
                return gui::EditorKeyChord_ToKeySequence( binding.chords[0] ).toString( QKeySequence::NativeText );
            }
            if ( lookup == keymap_lookup_t::UNBOUND ) { return {}; }
        }
        return {};
    }

    // The group (row, or column when the shape is columns) a pane sits in;
    // -1 when the arrangement does not use it.
    static int PaneRow( map_view_arrangement_t arrangement, int iPane )
    {
        const map_view_arrangement_shape_t shape = MapViews_ArrangementShape( arrangement );
        if ( iPane < shape.counts[0] ) { return 0; }
        if ( iPane < shape.counts[0] + shape.counts[1] ) { return 1; }
        return -1;
    }

    int PaneRow( int iPane ) const { return PaneRow( m_arrangement, iPane ); }

    void PlacePanes()
    {
        // Move only panes whose slot actually changes. Emptying every row
        // transiently makes QSplitter advertise a zero maximum height to
        // the surrounding layout; retaining pane 0 avoids that stale size
        // constraint while preserving all live view widgets.
        // Rows of panes or columns of panes: the same two splitters, turned.
        const bool bColumns = MapViews_ArrangementShape( m_arrangement ).bColumns;
        m_pRows->setOrientation( bColumns ? Qt::Horizontal : Qt::Vertical );
        for ( QSplitter *pGroup : m_pRow ) { pGroup->setOrientation( bColumns ? Qt::Vertical : Qt::Horizontal ); }
        int columns[2]{};
        for ( int i = 0; i < MAP_VIEW_PANE_COUNT; ++i ) {
            const int row = PaneRow( i );
            QWidget *pPane = m_panes[i].pPane;
            if ( row < 0 ) {
                pPane->hide();
                if ( pPane->parentWidget() != this ) { pPane->setParent( this ); }
            } else {
                const int column = columns[row]++;
                if ( m_pRow[row]->indexOf( pPane ) != column ) { m_pRow[row]->insertWidget( column, pPane ); }
            }
        }
        setProperty( "viewArrangement", static_cast<int>( m_arrangement ) );
        UpdateArrangementPicker();
    }

    QMenu *BuildArrangementMenu( QWidget *pParent )
    {
        auto *pMenu = new QMenu( pParent );
        pMenu->setObjectName( QStringLiteral( "masonLayoutMenu" ) );
        auto *pGrid = new QWidget( pMenu );
        pGrid->setObjectName( QStringLiteral( "masonLayoutGrid" ) );
        auto *pLayout = new QGridLayout( pGrid );
        pLayout->setContentsMargins( 6, 6, 6, 6 );
        pLayout->setHorizontalSpacing( 2 );
        pLayout->setVerticalSpacing( 4 );
        usize nArrangements = 0u;
        const map_view_arrangement_t *pArrangements = MapViews_PickerArrangements( &nArrangements );
        int columnOf[MAP_VIEW_PANE_COUNT + 1]{};
        for ( usize i = 0u; i < nArrangements; ++i ) {
            const map_view_arrangement_t arrangement = pArrangements[i];
            const int nPanes = MapViews_ArrangementPaneCount( arrangement );
            auto *pButton = new QToolButton( pGrid );
            pButton->setObjectName( QStringLiteral( "masonLayout_%1" ).arg( QString::fromLatin1( MapViews_ArrangementIcon( arrangement ) ) ) );
            pButton->setCheckable( true );
            pButton->setAutoRaise( true );
            pButton->setIconSize( QSize( 32, 32 ) );
            pButton->setToolTip( MapViews_ArrangementTitle( arrangement ) );
            pButton->setProperty( "arrangement", static_cast<int>( arrangement ) );
            // One row per pane count, so the menu reads 1, 2, 3, 4 downwards.
            pLayout->addWidget( pButton, nPanes - 1, columnOf[nPanes]++ );
            QObject::connect( pButton, &QToolButton::clicked, this, [this, pMenu, arrangement]() {
                pMenu->close();
                SetArrangement( arrangement );
            } );
            m_arrangementButtons.append( pButton );
        }
        auto *pAction = new QWidgetAction( pMenu );
        pAction->setDefaultWidget( pGrid );
        pMenu->addAction( pAction );
        RefreshArrangementIcons();
        return pMenu;
    }

    void RefreshArrangementIcons()
    {
        const gui::editor_style_t &style = m_pWorkspace->pGui->style;
        for ( QToolButton *pButton : m_arrangementButtons ) {
            const auto arrangement = static_cast<map_view_arrangement_t>( pButton->property( "arrangement" ).toInt() );
            pButton->setIcon( gui::EditorStyle_Icon( style, MapViews_ArrangementIcon( arrangement ) ) );
        }
        if ( m_pArrangementButton != nullptr ) { m_pArrangementButton->setIcon( gui::EditorStyle_Icon( style, MapViews_ArrangementIcon( m_arrangement ) ) ); }
    }

    void UpdateArrangementPicker()
    {
        if ( m_pArrangementButton == nullptr ) { return; }
        m_pArrangementButton->setToolTip( QStringLiteral( "Layout: %1" ).arg( MapViews_ArrangementTitle( m_arrangement ) ) );
        for ( QToolButton *pButton : m_arrangementButtons ) {
            pButton->setChecked( pButton->property( "arrangement" ).toInt() == static_cast<int>( m_arrangement ) );
        }
        RefreshArrangementIcons();
    }

    void ResetSplitterSizes()
    {
        // The single pane beside a group is the main view and gets the larger share.
        const map_view_arrangement_shape_t shape = MapViews_ArrangementShape( m_arrangement );
        if ( shape.counts[0] == 1 && shape.counts[1] > 1 ) {
            m_splitSizes[0] = QList<int>{ 700, 300 };
        } else if ( shape.counts[1] == 1 && shape.counts[0] > 1 ) {
            m_splitSizes[0] = QList<int>{ 300, 700 };
        } else {
            m_splitSizes[0] = QList<int>{ 500, 500 };
        }
        for ( int row = 0; row < 2; ++row ) {
            m_splitSizes[row + 1].clear();
            for ( int i = 0; i < m_pRow[row]->count(); ++i ) { m_splitSizes[row + 1].append( 500 ); }
        }
    }

    void RememberSplitterSizes()
    {
        if ( m_iMaximized >= 0 ) { return; }
        QSplitter *splitters[3]{ m_pRows, m_pRow[0], m_pRow[1] };
        for ( int i = 0; i < 3; ++i ) {
            const QList<int> sizes = splitters[i]->sizes();
            // A hidden child reports zero; retain the proportions from
            // before it closed so restoring cannot collapse its neighbors.
            if ( !sizes.empty() && std::all_of( sizes.begin(), sizes.end(), []( int size ) { return size > 0; } ) ) {
                m_splitSizes[i] = sizes;
            }
        }
    }

    void RestoreSplitterSizes()
    {
        m_pRows->setSizes( m_splitSizes[0] );
        m_pRow[0]->setSizes( m_splitSizes[1] );
        m_pRow[1]->setSizes( m_splitSizes[2] );
    }

    void ExpandToFour()
    {
        m_arrangement = map_view_arrangement_t::FOUR;
        PlacePanes();
        ResetSplitterSizes();
    }

    struct pane_t;

    struct background_t {
        QImage image{};
        QRectF world{};
        f64 opacity{ 0.5 };
    };

    static int BackgroundSlot( map_view_type_t type )
    {
        return type == map_view_type_t::TOP ? 0 : type == map_view_type_t::FRONT ? 1 : type == map_view_type_t::SIDE ? 2 : -1;
    }

    void ApplyBackground( pane_t &pane )
    {
        auto *pOrtho = dynamic_cast<map_ortho_view_t *>( pane.pView );
        const int slot = BackgroundSlot( pane.type );
        if ( pOrtho == nullptr || slot < 0 ) { return; }
        const background_t &background = pane.backgrounds[slot];
        if ( background.image.isNull() ) {
            pOrtho->ClearBackground();
        } else {
            pOrtho->SetBackground( background.image, background.world, background.opacity );
        }
        if ( pane.pBackgroundMenu != nullptr ) {
            for ( QAction *pAction : pane.pBackgroundMenu->actions() ) {
                if ( pAction->property( "backgroundOpacity" ).isValid() ) {
                    pAction->setChecked( std::fabs( pAction->property( "backgroundOpacity" ).toDouble() - background.opacity ) < 1e-6 );
                    pAction->setEnabled( !background.image.isNull() );
                }
                if ( pAction->objectName().startsWith( QStringLiteral( "EditorViewBackgroundClear" ) ) ) { pAction->setEnabled( !background.image.isNull() ); }
            }
        }
    }

    // NetRadiant fits the image to the selection; with nothing selected,
    // one image pixel is one world unit, centred on the origin.
    QRectF BackgroundPlacement( const pane_t &pane, const QImage &image ) const
    {
        const map_bounds_t bounds = MapViews_SelectionGeometryBounds( m_pWorkspace );
        const int u = pane.type == map_view_type_t::FRONT ? 1 : 0;
        const int v = pane.type == map_view_type_t::TOP ? 1 : 2;
        if ( bounds.bHas ) {
            const f64 lo[3]{ bounds.box.minimum.x, bounds.box.minimum.y, bounds.box.minimum.z };
            const f64 hi[3]{ bounds.box.maximum.x, bounds.box.maximum.y, bounds.box.maximum.z };
            if ( hi[u] > lo[u] && hi[v] > lo[v] ) { return QRectF( QPointF( lo[u], lo[v] ), QPointF( hi[u], hi[v] ) ); }
        }
        return QRectF( -image.width() * 0.5, -image.height() * 0.5, image.width(), image.height() );
    }

    void LoadBackground( int iPane )
    {
        const QString file = QFileDialog::getOpenFileName( this, QStringLiteral( "Background Image" ), QString(),
                                                           QStringLiteral( "Images (*.png *.jpg *.jpeg *.bmp *.tga *.webp)" ) );
        if ( file.isEmpty() ) { return; }
        QImage image( file );
        if ( image.isNull() ) {
            CY_LOG_WRITE( Warning, Editor, "The background image could not be read" );
            return;
        }
        const int slot = BackgroundSlot( m_panes[iPane].type );
        const f64 opacity = slot >= 0 && !m_panes[iPane].backgrounds[slot].image.isNull() ? m_panes[iPane].backgrounds[slot].opacity : 0.5;
        SetBackground( iPane, image, BackgroundPlacement( m_panes[iPane], image ), opacity );
    }

    struct pane_t {
        background_t backgrounds[3]{}; // Top, Front, Side.
        QMenu *pBackgroundMenu{ nullptr };
        QAction *pOrthoAids{ nullptr };
        QAction *pCameraAids{ nullptr };
        QWidget *pPane{ nullptr };
        QGridLayout *pLayout{ nullptr };
        QWidget *pHeader{ nullptr };
        QLabel *pNumber{ nullptr };
        QToolButton *pTitle{ nullptr };      // The view tab: its name; left click chooses the view.
        QMenu *pViewMenu{ nullptr };         // Views only: 2D projections and 3D modes.
        QMenu *pOptionsMenu{ nullptr };      // Right click: navigation, drawing, the pane.
        QActionGroup *pTypes{ nullptr };     // The view menu's items.
        QAction *pMeshEdges{ nullptr };
        QAction *pWireOverlay{ nullptr };
        map_render_mode_t render{ map_render_mode_t::WIREFRAME }; // Kept while the pane shows a 2D view.
        bool bMeshEdges{ true };
        bool bWireOverlay{ false };
        QAction *pWindowsSeparator{ nullptr };
        QVector<QAction *> windowActions{};  // Asset Browser, Object Properties: application commands.
        QMenu *pCameraControls{ nullptr };
        QMenu *pOrthoControls{ nullptr };
        QAction *pFrameAction{ nullptr };
        QAction *pMaximizeAction{ nullptr };
        QAction *pCloseAction{ nullptr };
        QWidget *pView{ nullptr };
        QWidget *views[static_cast<int>( map_view_type_t::COUNT )]{}; // Pane-owned; hidden views retain drafts/cameras.
        map_view_type_t type{ map_view_type_t::CAMERA };
        bool bClosed{ false };
    };

    QToolButton *HeaderButton( QWidget *pHeader, const QString &glyph, const QString &tip, const QString &name )
    {
        auto *pButton = new QToolButton( pHeader );
        pButton->setObjectName( name );
        pButton->setText( glyph );
        pButton->setToolTip( tip );
        pButton->setAutoRaise( true );
        pButton->setFocusPolicy( Qt::NoFocus );
        return pButton;
    }

    void BuildPane( int iPane, QSplitter *pRow, map_view_type_t type )
    {
        pane_t &pane = m_panes[iPane];
        pane.pPane = new QWidget( pRow );
        pane.pPane->setObjectName( QStringLiteral( "EditorViewPane%1" ).arg( iPane ) );
        pane.pPane->setProperty( "editorViewPane", true );
        pane.pLayout = new QGridLayout( pane.pPane );
        pane.pLayout->setContentsMargins( 0, 0, 0, 0 );
        pane.pLayout->setSpacing( 0 );

        pane.pHeader = new QWidget( pane.pPane );
        pane.pHeader->setObjectName( QStringLiteral( "EditorViewHeader" ) );
        pane.pHeader->setAttribute( Qt::WA_StyledBackground );
        pane.pHeader->setSizePolicy( QSizePolicy::Maximum, QSizePolicy::Fixed );
        pane.pHeader->setToolTip( QStringLiteral( "Double-click to maximize or restore this pane" ) );
        pane.pHeader->installEventFilter( this );
        // Hammer's view chrome: one dropdown inset in the corner of the view,
        // the pane controls appearing only while the pointer is over it.
        auto *pHeaderLayout = new QHBoxLayout( pane.pHeader );
        pHeaderLayout->setContentsMargins( 4, 4, 4, 0 );
        pHeaderLayout->setSpacing( 2 );
        pane.pNumber = new QLabel( QString::number( iPane + 1 ), pane.pHeader );
        pane.pNumber->setProperty( "viewNumber", true );
        pane.pNumber->setAlignment( Qt::AlignCenter );
        pane.pNumber->hide(); // Kept for automation; Hammer's views carry no numbers.
        pHeaderLayout->addWidget( pane.pNumber );
        pane.pPane->installEventFilter( this );

        // Hammer's view tab: the view's name. A left click lists only the
        // views to choose from - the 2D projections and the 3D modes; every
        // other option is the right-click menu on the tab (or the view), so
        // a left click never toggles drawing by accident.
        pane.pTitle = HeaderButton( pane.pHeader, QString(), QStringLiteral( "Choose this pane's view. Right-click for view options." ),
                                    QStringLiteral( "EditorViewChoose%1" ).arg( iPane ) );
        pane.pTitle->setAccessibleName( QStringLiteral( "View %1" ).arg( iPane + 1 ) );
        pane.pTitle->setArrowType( Qt::DownArrow );
        pane.pTitle->setProperty( "viewOptions", true ); // Explicit arrow; suppress the stylesheet's second menu marker.
        pane.pTitle->setProperty( "viewTitle", true );
        pane.pTitle->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
        pane.pTitle->setPopupMode( QToolButton::InstantPopup );
        auto *pViewMenu = new QMenu( pane.pTitle );
        pViewMenu->setObjectName( QStringLiteral( "EditorViewTypeMenu%1" ).arg( iPane ) );
        pViewMenu->setToolTipsVisible( true );
        pane.pViewMenu = pViewMenu;
        // "2d Top  F2": the key column comes from the keymap, so rebinding
        // updates the menu. Data packs the type and the 3D mode.
        pane.pTypes = new QActionGroup( pViewMenu );
        pane.pTypes->setExclusive( true );
        struct view_choice_t {
            const char *pLabel;
            const char *pIcon;
            map_view_type_t type;
            map_render_mode_t render;
            const char *pCommand;
            const char *pTip;
        };
        static constexpr view_choice_t kChoices[]{
            { "2d Top", "view-top", map_view_type_t::TOP, map_render_mode_t::WIREFRAME, "map.view.top", "Looking down the Z axis" },
            { "2d Front", "view-front", map_view_type_t::FRONT, map_render_mode_t::WIREFRAME, "map.view.front", "Looking along the X axis" },
            { "2d Side", "view-side", map_view_type_t::SIDE, map_render_mode_t::WIREFRAME, "map.view.side", "Looking along the Y axis" },
            { "3d Wireframe", "render-wireframe", map_view_type_t::CAMERA, map_render_mode_t::WIREFRAME, "map.render.wireframe", "Edges only" },
            { "3d Shaded", "render-lit", map_view_type_t::CAMERA, map_render_mode_t::SHADED, "map.render.shaded", "Material colours lit by a fixed sun" },
            { "3d Fullbright", "render-flat", map_view_type_t::CAMERA, map_render_mode_t::FULLBRIGHT, "map.render.fullbright", "Material colours without lighting" },
            { "3d Normals", "render-textured", map_view_type_t::CAMERA, map_render_mode_t::NORMALS, "map.render.normals", "Face directions as colour: a tools view" },
        };
        for ( const view_choice_t &choice : kChoices ) {
            if ( choice.type == map_view_type_t::CAMERA && choice.render == map_render_mode_t::WIREFRAME ) { pViewMenu->addSeparator(); }
            QAction *pAction = pViewMenu->addAction( QString::fromLatin1( choice.pLabel ) );
            pAction->setObjectName( choice.render == map_render_mode_t::WIREFRAME
                                        ? QStringLiteral( "EditorViewProjection%1_%2" ).arg( iPane ).arg( static_cast<int>( choice.type ) )
                                        : QStringLiteral( "EditorViewRender%1_%2" ).arg( iPane ).arg( static_cast<int>( choice.render ) ) );
            pAction->setIcon( gui::EditorStyle_Icon( m_pWorkspace->pGui->style, choice.pIcon ) );
            pAction->setToolTip( QString::fromLatin1( choice.pTip ) );
            pAction->setCheckable( true );
            pAction->setData( static_cast<int>( choice.type ) * 16 + static_cast<int>( choice.render ) );
            pAction->setProperty( "viewCommand", QByteArray( choice.pCommand ) );
            pAction->setProperty( "viewIcon", QByteArray( choice.pIcon ) );
            pane.pTypes->addAction( pAction );
            QObject::connect( pAction, &QAction::triggered, this, [this, iPane, type = choice.type, render = choice.render]() { SetView( iPane, type, render ); } );
        }
        pViewMenu->addSeparator();
        const view_choice_t contentChoices[]{
            { "Asset Manager", "asset-browser", map_view_type_t::ASSETS, map_render_mode_t::WIREFRAME, "", "Browse and preview assets in this pane" },
            { "Content Library", "asset-material", map_view_type_t::DATABASE, map_render_mode_t::WIREFRAME, "", "Browse content and inspect map records" },
            { "Shader View", "asset-shader", map_view_type_t::SHADERS, map_render_mode_t::WIREFRAME, "", "Edit shader recipes and stage source in this pane" }
        };
        for ( const auto &choice : contentChoices ) {
            auto *action = pViewMenu->addAction( gui::EditorStyle_Icon( m_pWorkspace->pGui->style, choice.pIcon ), QString::fromLatin1( choice.pLabel ) );
            action->setObjectName( QStringLiteral( "EditorViewContent%1_%2" ).arg( iPane ).arg( static_cast<int>( choice.type ) ) );
            action->setToolTip( QString::fromLatin1( choice.pTip ) );
            action->setCheckable( true );
            action->setData( static_cast<int>( choice.type ) * 16 );
            action->setProperty( "viewIcon", QByteArray( choice.pIcon ) );
            action->setEnabled( m_pfnContent != nullptr );
            pane.pTypes->addAction( action );
            QObject::connect( action, &QAction::triggered, this, [this, iPane, type = choice.type]() { SetType( iPane, type ); } );
        }
        QObject::connect( pViewMenu, &QMenu::aboutToShow, this, [this]() { RefreshMenus(); } );
        pane.pTitle->setMenu( pViewMenu );

        // The right-click menu: this view's navigation, drawing, and pane.
        auto *pOptionsMenu = new QMenu( pane.pTitle );
        pOptionsMenu->setObjectName( QStringLiteral( "EditorViewOptionsMenu%1" ).arg( iPane ) );
        pOptionsMenu->setToolTipsVisible( true );
        pane.pOptionsMenu = pOptionsMenu;

        // What this projection can do: navigation, then drawing.
        pane.pCameraControls = pOptionsMenu->addMenu( QStringLiteral( "Camera" ) );
        pane.pCameraControls->setObjectName( QStringLiteral( "EditorViewCameraControls%1" ).arg( iPane ) );
        pane.pCameraControls->setToolTipsVisible( true );
        const auto cameraAction = [&]( const QString &label, const QString &suffix, const QString &tip, int operation ) {
            QAction *action = pane.pCameraControls->addAction( label );
            action->setObjectName( QStringLiteral( "EditorViewCamera%1_%2" ).arg( suffix ).arg( iPane ) );
            action->setToolTip( tip );
            QObject::connect( action, &QAction::triggered, this, [this, iPane, operation]() {
                if ( auto *camera = dynamic_cast<map_camera_view_t *>( m_panes[iPane].pView ) ) {
                    if ( operation == 0 ) { camera->Dolly( kCameraWheelStep ); }
                    else if ( operation == 1 ) { camera->Dolly( -kCameraWheelStep ); }
                    else { camera->Level(); }
                }
            } );
        };
        cameraAction( QStringLiteral( "Move Forward" ), QStringLiteral( "Forward" ), QStringLiteral( "Move along the camera direction. Wheel up or W also moves forward." ), 0 );
        cameraAction( QStringLiteral( "Move Backward" ), QStringLiteral( "Backward" ), QStringLiteral( "Move away along the camera direction. Wheel down or S also moves backward." ), 1 );
        cameraAction( QStringLiteral( "Level Camera" ), QStringLiteral( "Level" ), QStringLiteral( "Look horizontally while preserving position and heading. Hold the right mouse button to look freely." ), 2 );
        pane.pOrthoControls = pOptionsMenu->addMenu( QStringLiteral( "Navigation" ) );
        pane.pOrthoControls->setObjectName( QStringLiteral( "EditorViewOrthoControls%1" ).arg( iPane ) );
        pane.pOrthoControls->setToolTipsVisible( true );
        const auto orthoAction = [&]( const QString &label, const QString &suffix, const QString &tip, int operation ) {
            QAction *action = pane.pOrthoControls->addAction( label );
            action->setObjectName( QStringLiteral( "EditorViewOrtho%1_%2" ).arg( suffix ).arg( iPane ) );
            action->setToolTip( tip );
            QObject::connect( action, &QAction::triggered, this, [this, iPane, operation]() {
                if ( auto *ortho = dynamic_cast<map_ortho_view_t *>( m_panes[iPane].pView ) ) {
                    if ( operation == 2 ) { ortho->CenterOrigin(); }
                    else { ortho->ZoomBy( operation == 0 ? 1.0 : -1.0, ortho->rect().center() ); }
                }
            } );
        };
        orthoAction( QStringLiteral( "Zoom In" ), QStringLiteral( "ZoomIn" ), QStringLiteral( "Zoom this view in about its center. Mouse wheel zooms about the cursor." ), 0 );
        orthoAction( QStringLiteral( "Zoom Out" ), QStringLiteral( "ZoomOut" ), QStringLiteral( "Zoom this view out about its center. Middle mouse or Space + left drag pans." ), 1 );
        orthoAction( QStringLiteral( "Center on World Origin" ), QStringLiteral( "Origin" ), QStringLiteral( "Center this view on coordinate zero without changing its zoom." ), 2 );
        pane.pOrthoAids = pOptionsMenu->addAction( QStringLiteral( "2D Drawing Aids" ) );
        pane.pOrthoAids->setMenu( DrawingAidsMenu( false ) );
        pane.pCameraAids = pOptionsMenu->addAction( QStringLiteral( "3D Drawing Aids" ) );
        pane.pCameraAids->setMenu( DrawingAidsMenu( true ) );
        pane.pMeshEdges = pOptionsMenu->addAction( QStringLiteral( "Mesh Edges" ) );
        pane.pMeshEdges->setObjectName( QStringLiteral( "EditorViewMeshEdges%1" ).arg( iPane ) );
        pane.pMeshEdges->setCheckable( true );
        pane.pMeshEdges->setChecked( true );
        pane.pMeshEdges->setToolTip( QStringLiteral( "Outline every face in the shaded modes" ) );
        pane.pMeshEdges->setProperty( "viewCommand", QByteArray( "map.render.mesh_edges" ) );
        QObject::connect( pane.pMeshEdges, &QAction::toggled, this, [this, iPane]( bool bOn ) { SetPaneMeshEdges( iPane, bOn ); } );
        pane.pWireOverlay = pOptionsMenu->addAction( QStringLiteral( "Wireframe Overlay" ) );
        pane.pWireOverlay->setObjectName( QStringLiteral( "EditorViewWireOverlay%1" ).arg( iPane ) );
        pane.pWireOverlay->setCheckable( true );
        pane.pWireOverlay->setToolTip( QStringLiteral( "Every edge over the shaded view, hidden ones included" ) );
        pane.pWireOverlay->setProperty( "viewCommand", QByteArray( "map.render.wire_overlay" ) );
        QObject::connect( pane.pWireOverlay, &QAction::toggled, this, [this, iPane]( bool bOn ) { SetPaneWireOverlay( iPane, bOn ); } );
        pane.pBackgroundMenu = new QMenu( QStringLiteral( "Background Image" ), pOptionsMenu );
        pane.pBackgroundMenu->setObjectName( QStringLiteral( "EditorViewBackgroundMenu%1" ).arg( iPane ) );
        {
            QAction *pLoad = pane.pBackgroundMenu->addAction( QStringLiteral( "Load..." ) );
            pLoad->setObjectName( QStringLiteral( "EditorViewBackgroundLoad%1" ).arg( iPane ) );
            pLoad->setToolTip( QStringLiteral( "Trace over a plan or elevation: fitted to the selection, or one pixel per unit at the origin" ) );
            QObject::connect( pLoad, &QAction::triggered, this, [this, iPane]() { LoadBackground( iPane ); } );
            pane.pBackgroundMenu->addSeparator();
            auto *pOpacity = new QActionGroup( pane.pBackgroundMenu );
            for ( const f64 opacity : { 0.25, 0.5, 0.75, 1.0 } ) {
                QAction *pAction = pane.pBackgroundMenu->addAction( QStringLiteral( "%1% Opacity" ).arg( static_cast<int>( opacity * 100.0 ) ) );
                pAction->setCheckable( true );
                pAction->setProperty( "backgroundOpacity", opacity );
                pOpacity->addAction( pAction );
                QObject::connect( pAction, &QAction::triggered, this, [this, iPane, opacity]() {
                    pane_t &target = m_panes[iPane];
                    const int slot = BackgroundSlot( target.type );
                    if ( slot >= 0 && !target.backgrounds[slot].image.isNull() ) {
                        SetBackground( iPane, target.backgrounds[slot].image, target.backgrounds[slot].world, opacity );
                    }
                } );
            }
            pane.pBackgroundMenu->addSeparator();
            QAction *pClear = pane.pBackgroundMenu->addAction( QStringLiteral( "Clear" ) );
            pClear->setObjectName( QStringLiteral( "EditorViewBackgroundClear%1" ).arg( iPane ) );
            QObject::connect( pClear, &QAction::triggered, this, [this, iPane]() { ClearBackground( iPane ); } );
        }
        pOptionsMenu->addMenu( pane.pBackgroundMenu );
        pOptionsMenu->addSeparator();

        // The pane itself.
        pane.pFrameAction = new QAction( gui::EditorStyle_Icon( m_pWorkspace->pGui->style, "view-frame" ), QStringLiteral( "Frame Map in This View" ), this );
        pane.pFrameAction->setObjectName( QStringLiteral( "EditorViewFrameAction%1" ).arg( iPane ) );
        pane.pFrameAction->setToolTip( QStringLiteral( "Frame selection, or the map when nothing is selected, in this view only" ) );
        QObject::connect( pane.pFrameAction, &QAction::triggered, this, [this, iPane]() { FramePane( iPane ); } );
        pOptionsMenu->addAction( pane.pFrameAction );
        pane.pMaximizeAction = new QAction( gui::EditorStyle_Icon( m_pWorkspace->pGui->style, "view-maximize" ), QStringLiteral( "Maximize This View" ), this );
        pane.pMaximizeAction->setObjectName( QStringLiteral( "EditorViewMaximizeAction%1" ).arg( iPane ) );
        pane.pMaximizeAction->setCheckable( true );
        QObject::connect( pane.pMaximizeAction, &QAction::triggered, this, [this, iPane]() { SetMaximized( m_iMaximized == iPane ? -1 : iPane ); } );
        pOptionsMenu->addAction( pane.pMaximizeAction );
        pane.pCloseAction = new QAction( QStringLiteral( "Hide This View" ), this );
        pane.pCloseAction->setObjectName( QStringLiteral( "EditorViewCloseAction%1" ).arg( iPane ) );
        QObject::connect( pane.pCloseAction, &QAction::triggered, this, [this, iPane]() { SetPaneVisible( iPane, false ); } );
        pOptionsMenu->addAction( pane.pCloseAction );
        if ( m_pShowAllAction == nullptr ) {
            m_pShowAllAction = new QAction( gui::EditorStyle_Icon( m_pWorkspace->pGui->style, "view-quad" ), QStringLiteral( "Show All Four Views" ), this );
            m_pShowAllAction->setObjectName( QStringLiteral( "EditorViewShowAll" ) );
            QObject::connect( m_pShowAllAction, &QAction::triggered, this, [this]() { ShowAll(); } );
        }
        pOptionsMenu->addAction( m_pShowAllAction );

        // Editor windows Hammer reaches from a view: shown only when the
        // application registered the command, so a bare view stays honest.
        pane.pWindowsSeparator = pOptionsMenu->addSeparator();
        constexpr std::pair<const char *, const char *> kWindows[]{
            { "Asset Browser", "assets.browser" },
            { "Object Properties", "view.properties" },
        };
        for ( const auto &[pLabel, pCommand] : kWindows ) {
            QAction *pAction = pOptionsMenu->addAction( QString::fromLatin1( pLabel ) );
            pAction->setObjectName( QStringLiteral( "EditorViewCommand_%1_%2" ).arg( QString::fromLatin1( pCommand ) ).arg( iPane ) );
            pAction->setProperty( "viewCommand", QByteArray( pCommand ) );
            QObject::connect( pAction, &QAction::triggered, this, [this, pCommand]() {
                ( void )EditorCommands_Execute( &m_pWorkspace->pGui->commands, StringView_FromCString( pCommand ), {} );
            } );
            pane.windowActions.append( pAction );
        }
        QObject::connect( pOptionsMenu, &QMenu::aboutToShow, this, [this]() { RefreshMenus(); } );
        pHeaderLayout->addWidget( pane.pTitle );

        pane.pLayout->addWidget( pane.pHeader, 0, 0, Qt::AlignTop | Qt::AlignLeft ); // Hammer's tab sits in the top-left corner.
        pRow->addWidget( pane.pPane );
        SetType( iPane, type );
    }

    void ApplyVisibility()
    {
        const bool bCanClose = OpenPaneCount() > 1;
        for ( int i = 0; i < MAP_VIEW_PANE_COUNT; ++i ) {
            const bool bShow = m_iMaximized >= 0 ? i == m_iMaximized : !m_panes[i].bClosed;
            m_panes[i].pPane->setVisible( bShow );
            m_panes[i].pMaximizeAction->setIcon( gui::EditorStyle_Icon( m_pWorkspace->pGui->style, m_iMaximized == i ? "view-quad" : "view-maximize", true ) );
            m_panes[i].pCloseAction->setEnabled( bCanClose );
            m_panes[i].pCloseAction->setToolTip( bCanClose ? QStringLiteral( "Hide this pane (Show All Four Views brings it back)" )
                                                        : QStringLiteral( "Keep at least one view open" ) );
        }
        for ( int row = 0; row < 2; ++row ) {
            bool visible = false;
            for ( int i = 0; i < MAP_VIEW_PANE_COUNT; ++i ) {
                if ( PaneRow( i ) == row && ( m_iMaximized >= 0 ? i == m_iMaximized : !m_panes[i].bClosed ) ) { visible = true; }
            }
            m_pRow[row]->setVisible( visible );
        }
        RefreshMenus();
    }

    int OpenPaneCount() const
    {
        int count = 0;
        for ( const pane_t &pane : m_panes ) { count += pane.bClosed ? 0 : 1; }
        return count;
    }

    // The header of the pane holding focus is highlighted (TileEditor's
    // active pane), through a dynamic property the stylesheet reads.
    void UpdateActive()
    {
        const QWidget *pFocus = QApplication::focusWidget();
        for ( int i = 0; i < MAP_VIEW_PANE_COUNT; ++i ) {
            if ( pFocus != nullptr && m_panes[i].pPane->isAncestorOf( pFocus ) ) { m_iActive = i; }
        }
        for ( pane_t &pane : m_panes ) {
            const bool bActive = pFocus != nullptr && pane.pPane->isAncestorOf( pFocus );
            if ( pane.pHeader->property( "active" ).toBool() == bActive ) { continue; }
            pane.pHeader->setProperty( "active", bActive );
            pane.pHeader->style()->unpolish( pane.pHeader );
            pane.pHeader->style()->polish( pane.pHeader );
        }
    }

    map_workspace_t *m_pWorkspace{ nullptr };
    map_view_content_create_fn m_pfnContent{};
    void *m_pContentContext{};
    QToolButton *m_pArrangementButton{ nullptr };
    QVector<QToolButton *> m_arrangementButtons;
    QMenu *m_pDrawingAids[2]{};
    QAction *m_pCleanViewActions[2]{};
    QAction *m_pGridAction{ nullptr };
    QAction *m_pShowAllAction{ nullptr };
    QVector<display_action_t> m_displayActions;
    QSplitter *m_pRows{ nullptr };
    QSplitter *m_pRow[2]{};
    pane_t m_panes[MAP_VIEW_PANE_COUNT]{};
    int m_iMaximized{ -1 };
    int m_iActive{ 0 }; // The pane last holding focus: view commands act on it (Hammer's active viewport).
    map_view_arrangement_t m_arrangement{ map_view_arrangement_t::FOUR };
    QList<int> m_splitSizes[3];
};

map_view_grid_t *AsGrid( QWidget *pViews ) noexcept
{
    auto *pGrid = dynamic_cast<map_view_grid_t *>( pViews );
    CY_ASSERT( pGrid != nullptr );
    return pGrid;
}

bool ValidPane( int iPane ) noexcept
{
    CY_ASSERT( iPane >= 0 && iPane < MAP_VIEW_PANE_COUNT );
    return iPane >= 0 && iPane < MAP_VIEW_PANE_COUNT;
}

const map_ortho_view_t *AsOrtho( const QWidget *pView ) noexcept
{
    const auto *pOrtho = dynamic_cast<const map_ortho_view_t *>( pView );
    CY_ASSERT( pOrtho != nullptr );
    return pOrtho;
}

const map_camera_view_t *AsCamera( const QWidget *pView ) noexcept
{
    const auto *pCamera = dynamic_cast<const map_camera_view_t *>( pView );
    CY_ASSERT( pCamera != nullptr );
    return pCamera;
}

} // namespace

QWidget *MapOrthoView_Create( QWidget *pParent, map_workspace_t *pWorkspace, map_ortho_axes_t axes )
{
    return new map_ortho_view_t( pParent, pWorkspace, axes );
}

f64 MapOrthoView_Zoom( const QWidget *pView )
{
    return AsOrtho( pView )->Zoom();
}

f64 MapOrthoView_GridStep( const QWidget *pView )
{
    return AsOrtho( pView )->GridStep();
}

QPointF MapOrthoView_WorldToView( const QWidget *pView, QPointF world )
{
    return AsOrtho( pView )->WorldToView( world );
}

QPointF MapOrthoView_ViewToWorld( const QWidget *pView, QPointF view )
{
    return AsOrtho( pView )->ViewToWorld( view );
}

u64 MapOrthoView_Pick( const QWidget *pView, QPointF position )
{
    return AsOrtho( pView )->Pick( position );
}

bool MapOrthoView_PickMeshEdge( const QWidget *pView, QPointF position, map_mesh_edge_hit_t *pOut )
{
    if ( pOut != nullptr ) { *pOut = {}; }
    const auto *pOrtho = dynamic_cast<const map_ortho_view_t *>( pView );
    return pOrtho != nullptr && pOrtho->PickMeshEdge( position, pOut );
}

bool MapOrthoView_PickMeshVertex( const QWidget *pView, QPointF position, map_mesh_vertex_hit_t *pOut )
{
    if ( pOut != nullptr ) { *pOut = {}; }
    const auto *pOrtho = dynamic_cast<const map_ortho_view_t *>( pView );
    return pOrtho != nullptr && pOrtho->PickMeshVertex( position, pOut );
}

u64 MapCameraView_Pick( QWidget *pView, QPointF position )
{
    return const_cast<map_camera_view_t *>( AsCamera( pView ) )->Pick( position );
}

bool MapCameraView_PickMeshEdge( QWidget *pView, QPointF position, map_mesh_edge_hit_t *pOut )
{
    if ( pOut != nullptr ) { *pOut = {}; }
    auto *pCamera = dynamic_cast<map_camera_view_t *>( pView );
    return pCamera != nullptr && pCamera->PickMeshEdge( position, pOut );
}

bool MapCameraView_PickMeshVertex( QWidget *pView, QPointF position, map_mesh_vertex_hit_t *pOut )
{
    if ( pOut != nullptr ) { *pOut = {}; }
    auto *pCamera = dynamic_cast<map_camera_view_t *>( pView );
    return pCamera != nullptr && pCamera->PickMeshVertex( position, pOut );
}

map_render_mode_t MapCameraView_RenderMode( const QWidget *pView )
{
    const auto *pCamera = dynamic_cast<const map_camera_view_t *>( pView );
    return pCamera != nullptr ? pCamera->RenderMode() : map_render_mode_t::WIREFRAME;
}

void MapCameraView_SetRenderMode( QWidget *pView, map_render_mode_t mode )
{
    if ( auto *pCamera = dynamic_cast<map_camera_view_t *>( pView ) ) { pCamera->SetRenderMode( mode ); }
}

u64 MapView_HoveredObject( const QWidget *pView )
{
    if ( const auto *pOrtho = dynamic_cast<const map_ortho_view_t *>( pView ) ) { return pOrtho->HoveredObject(); }
    if ( const auto *pCamera = dynamic_cast<const map_camera_view_t *>( pView ) ) { return pCamera->HoveredObject(); }
    return 0u;
}

QWidget *MapCameraView_Create( QWidget *pParent, map_workspace_t *pWorkspace )
{
    return new map_camera_view_t( pParent, pWorkspace );
}

math::vec3d_t MapCameraView_Position( const QWidget *pView )
{
    return AsCamera( pView )->Position();
}

math::vec3d_t MapCameraView_Forward( const QWidget *pView )
{
    return AsCamera( pView )->Forward();
}

math::vec3d_t MapCameraView_NavigationVelocity( QWidget *pView, Qt::KeyboardModifiers modifiers )
{
    return static_cast<map_camera_view_t *>( pView )->NavigationVelocity( modifiers );
}

map_camera_grid_info_t MapCameraView_GridInfo( const QWidget *pView )
{
    return AsCamera( pView )->GridInfo();
}

bool_t MapCameraView_WorldToView( QWidget *pView, math::vec3d_t point, QPointF *pOut )
{
    return static_cast<map_camera_view_t *>( pView )->WorldToView( point, pOut );
}

map_bounds_t MapViews_SelectionGeometryBounds( const map_workspace_t *pWorkspace )
{
    return pWorkspace != nullptr ? SelectionGeometryBounds( *pWorkspace ) : map_bounds_t{};
}

QWidget *MapViews_Create( QWidget *pParent, map_workspace_t *pWorkspace )
{
    CY_ASSERT( pWorkspace != nullptr );
    return new map_view_grid_t( pParent, pWorkspace );
}

void MapViews_SetPaneBackground( QWidget *pViews, int iPane, const QImage &image, const QRectF &worldRect, f64 opacity )
{
    if ( iPane >= 0 && iPane < MAP_VIEW_PANE_COUNT ) { AsGrid( pViews )->SetBackground( iPane, image, worldRect, opacity ); }
}

void MapViews_ClearPaneBackground( QWidget *pViews, int iPane )
{
    if ( iPane >= 0 && iPane < MAP_VIEW_PANE_COUNT ) { AsGrid( pViews )->ClearBackground( iPane ); }
}

bool MapViews_PaneHasBackground( QWidget *pViews, int iPane )
{
    return iPane >= 0 && iPane < MAP_VIEW_PANE_COUNT && AsGrid( pViews )->HasBackground( iPane );
}

int MapViews_ActivePane( QWidget *pViews )
{
    return AsGrid( pViews )->ActivePane();
}

bool MapViews_CycleActivePane( QWidget *pViews )
{
    return pViews != nullptr && AsGrid( pViews )->CycleActivePane();
}

bool MapViews_HandleToolCancel( QWidget *pViews, QEvent *pEvent )
{
    return pViews != nullptr && AsGrid( pViews )->HandleToolCancel( pEvent );
}

bool MapViews_CanNudgeSelection( QWidget *pViews )
{
    return pViews != nullptr && AsGrid( pViews )->CanNudgeSelection();
}

bool MapViews_NudgeSelection( QWidget *pViews, int horizontal, int vertical, bool fine )
{
    return pViews != nullptr && AsGrid( pViews )->NudgeSelection( horizontal, vertical, fine );
}

void MapViews_SetActivePane( QWidget *pViews, int iPane )
{
    if ( pViews != nullptr && iPane >= 0 && iPane < MAP_VIEW_PANE_COUNT ) { AsGrid( pViews )->SetActivePane( iPane ); }
}

void MapViews_SetArrangement( QWidget *pViews, map_view_arrangement_t arrangement )
{
    if ( arrangement < map_view_arrangement_t::COUNT ) { AsGrid( pViews )->SetArrangement( arrangement ); }
}

map_view_arrangement_t MapViews_Arrangement( QWidget *pViews )
{
    return AsGrid( pViews )->Arrangement();
}

map_view_arrangement_shape_t MapViews_ArrangementShape( map_view_arrangement_t arrangement ) noexcept
{
    const auto rows = []( int a, int b ) { return map_view_arrangement_shape_t{ CY_FALSE, { a, b } }; };
    const auto columns = []( int a, int b ) { return map_view_arrangement_shape_t{ CY_TRUE, { a, b } }; };
    switch ( arrangement ) {
        case map_view_arrangement_t::FOUR: return rows( 2, 2 );
        case map_view_arrangement_t::HAMMER: return rows( 1, 2 );
        case map_view_arrangement_t::PERSPECTIVE: return rows( 1, 0 );
        case map_view_arrangement_t::TWO: return rows( 2, 0 );
        case map_view_arrangement_t::TWO_ROWS: return columns( 2, 0 );
        case map_view_arrangement_t::THREE_COLUMNS: return rows( 3, 0 );
        case map_view_arrangement_t::THREE_ROWS: return columns( 3, 0 );
        case map_view_arrangement_t::TWO_TOP_ONE_BOTTOM: return rows( 2, 1 );
        case map_view_arrangement_t::ONE_LEFT_TWO_RIGHT: return columns( 1, 2 );
        case map_view_arrangement_t::TWO_LEFT_ONE_RIGHT: return columns( 2, 1 );
        case map_view_arrangement_t::ONE_TOP_THREE_BOTTOM: return rows( 1, 3 );
        case map_view_arrangement_t::THREE_TOP_ONE_BOTTOM: return rows( 3, 1 );
        case map_view_arrangement_t::ONE_LEFT_THREE_RIGHT: return columns( 1, 3 );
        case map_view_arrangement_t::THREE_LEFT_ONE_RIGHT: return columns( 3, 1 );
        case map_view_arrangement_t::FOUR_COLUMNS: return rows( 4, 0 );
        case map_view_arrangement_t::FOUR_ROWS: return columns( 4, 0 );
        case map_view_arrangement_t::COUNT: break;
    }
    return rows( 2, 2 );
}

int MapViews_ArrangementPaneCount( map_view_arrangement_t arrangement ) noexcept
{
    const map_view_arrangement_shape_t shape = MapViews_ArrangementShape( arrangement );
    return shape.counts[0] + shape.counts[1];
}

const char *MapViews_ArrangementIcon( map_view_arrangement_t arrangement ) noexcept
{
    switch ( arrangement ) {
        case map_view_arrangement_t::FOUR: return "layout-four";
        case map_view_arrangement_t::HAMMER: return "layout-one-top-two-bottom";
        case map_view_arrangement_t::PERSPECTIVE: return "layout-single";
        case map_view_arrangement_t::TWO: return "layout-two-columns";
        case map_view_arrangement_t::TWO_ROWS: return "layout-two-rows";
        case map_view_arrangement_t::THREE_COLUMNS: return "layout-three-columns";
        case map_view_arrangement_t::THREE_ROWS: return "layout-three-rows";
        case map_view_arrangement_t::TWO_TOP_ONE_BOTTOM: return "layout-two-top-one-bottom";
        case map_view_arrangement_t::ONE_LEFT_TWO_RIGHT: return "layout-one-left-two-right";
        case map_view_arrangement_t::TWO_LEFT_ONE_RIGHT: return "layout-two-left-one-right";
        case map_view_arrangement_t::ONE_TOP_THREE_BOTTOM: return "layout-one-top-three-bottom";
        case map_view_arrangement_t::THREE_TOP_ONE_BOTTOM: return "layout-three-top-one-bottom";
        case map_view_arrangement_t::ONE_LEFT_THREE_RIGHT: return "layout-one-left-three-right";
        case map_view_arrangement_t::THREE_LEFT_ONE_RIGHT: return "layout-three-left-one-right";
        case map_view_arrangement_t::FOUR_COLUMNS: return "layout-four-columns";
        case map_view_arrangement_t::FOUR_ROWS: return "layout-four-rows";
        case map_view_arrangement_t::COUNT: break;
    }
    return "layout-four";
}

const map_view_arrangement_t *MapViews_PickerArrangements( usize *pnOut ) noexcept
{
    using a = map_view_arrangement_t;
    static constexpr map_view_arrangement_t kOrder[]{
        a::PERSPECTIVE,
        a::TWO, a::TWO_ROWS,
        a::HAMMER, a::TWO_TOP_ONE_BOTTOM, a::ONE_LEFT_TWO_RIGHT, a::TWO_LEFT_ONE_RIGHT, a::THREE_COLUMNS, a::THREE_ROWS,
        a::FOUR, a::ONE_TOP_THREE_BOTTOM, a::THREE_TOP_ONE_BOTTOM, a::ONE_LEFT_THREE_RIGHT, a::THREE_LEFT_ONE_RIGHT, a::FOUR_COLUMNS, a::FOUR_ROWS,
    };
    static_assert( std::size( kOrder ) == static_cast<usize>( map_view_arrangement_t::COUNT ) );
    if ( pnOut != nullptr ) { *pnOut = std::size( kOrder ); }
    return kOrder;
}

QString MapViews_ArrangementTitle( map_view_arrangement_t arrangement )
{
    switch ( arrangement ) {
        case map_view_arrangement_t::FOUR: return QStringLiteral( "Four Views" );
        case map_view_arrangement_t::HAMMER: return QStringLiteral( "Perspective + Two Views" );
        case map_view_arrangement_t::PERSPECTIVE: return QStringLiteral( "Perspective" );
        case map_view_arrangement_t::TWO: return QStringLiteral( "Two Views" );
        case map_view_arrangement_t::TWO_ROWS: return QStringLiteral( "Two Views, Stacked" );
        case map_view_arrangement_t::THREE_COLUMNS: return QStringLiteral( "Three Columns" );
        case map_view_arrangement_t::THREE_ROWS: return QStringLiteral( "Three Rows" );
        case map_view_arrangement_t::TWO_TOP_ONE_BOTTOM: return QStringLiteral( "Two Above One" );
        case map_view_arrangement_t::ONE_LEFT_TWO_RIGHT: return QStringLiteral( "One Left, Two Right" );
        case map_view_arrangement_t::TWO_LEFT_ONE_RIGHT: return QStringLiteral( "Two Left, One Right" );
        case map_view_arrangement_t::ONE_TOP_THREE_BOTTOM: return QStringLiteral( "One Above Three" );
        case map_view_arrangement_t::THREE_TOP_ONE_BOTTOM: return QStringLiteral( "Three Above One" );
        case map_view_arrangement_t::ONE_LEFT_THREE_RIGHT: return QStringLiteral( "One Left, Three Right" );
        case map_view_arrangement_t::THREE_LEFT_ONE_RIGHT: return QStringLiteral( "Three Left, One Right" );
        case map_view_arrangement_t::FOUR_COLUMNS: return QStringLiteral( "Four Columns" );
        case map_view_arrangement_t::FOUR_ROWS: return QStringLiteral( "Four Rows" );
        case map_view_arrangement_t::COUNT: break;
    }
    return QString();
}

QByteArray MapViews_SavePresentation( QWidget *pViews )
{
    return AsGrid( pViews )->SavePresentation();
}

bool MapViews_ValidatePresentation( const QByteArray &state )
{
    map_view_grid_t::presentation_t presentation;
    return map_view_grid_t::ReadPresentation( state, presentation );
}

bool MapViews_RestorePresentation( QWidget *pViews, const QByteArray &state )
{
    return AsGrid( pViews )->RestorePresentation( state );
}

void MapViews_SetContentFactory( QWidget *pViews, map_view_content_create_fn create, void *context )
{
    AsGrid( pViews )->SetContentFactory( create, context );
}

void MapViews_SetPaneType( QWidget *pViews, int iPane, map_view_type_t type )
{
    if ( ValidPane( iPane ) && type < map_view_type_t::COUNT ) { AsGrid( pViews )->SetType( iPane, type ); }
}

void MapViews_SetPaneView( QWidget *pViews, int iPane, map_view_type_t type, map_render_mode_t render )
{
    if ( ValidPane( iPane ) && type < map_view_type_t::COUNT && render < map_render_mode_t::COUNT ) { AsGrid( pViews )->SetView( iPane, type, render ); }
}

map_render_mode_t MapViews_PaneRenderMode( QWidget *pViews, int iPane )
{
    return ValidPane( iPane ) ? AsGrid( pViews )->PaneRender( iPane ) : map_render_mode_t::WIREFRAME;
}

void MapViews_SetPaneMeshEdges( QWidget *pViews, int iPane, bool bOn )
{
    if ( ValidPane( iPane ) ) { AsGrid( pViews )->SetPaneMeshEdges( iPane, bOn ); }
}

bool MapViews_PaneMeshEdges( QWidget *pViews, int iPane )
{
    return ValidPane( iPane ) && AsGrid( pViews )->PaneMeshEdges( iPane );
}

void MapViews_SetPaneWireOverlay( QWidget *pViews, int iPane, bool bOn )
{
    if ( ValidPane( iPane ) ) { AsGrid( pViews )->SetPaneWireOverlay( iPane, bOn ); }
}

bool MapViews_PaneWireOverlay( QWidget *pViews, int iPane )
{
    return ValidPane( iPane ) && AsGrid( pViews )->PaneWireOverlay( iPane );
}

map_view_type_t MapViews_PaneType( QWidget *pViews, int iPane )
{
    return ValidPane( iPane ) ? AsGrid( pViews )->Type( iPane ) : map_view_type_t::COUNT;
}

QWidget *MapViews_PaneView( QWidget *pViews, int iPane )
{
    return ValidPane( iPane ) ? AsGrid( pViews )->View( iPane ) : nullptr;
}

QString MapViews_PaneTitle( QWidget *pViews, int iPane )
{
    return ValidPane( iPane ) ? AsGrid( pViews )->Title( iPane ) : QString();
}

void MapViews_SetMaximized( QWidget *pViews, int iPane )
{
    if ( iPane < 0 || ValidPane( iPane ) ) { AsGrid( pViews )->SetMaximized( iPane < 0 ? -1 : iPane ); }
}

int MapViews_MaximizedPane( QWidget *pViews )
{
    return AsGrid( pViews )->Maximized();
}

void MapViews_SetPaneVisible( QWidget *pViews, int iPane, bool bVisible )
{
    if ( ValidPane( iPane ) ) { AsGrid( pViews )->SetPaneVisible( iPane, bVisible ); }
}

bool MapViews_IsPaneVisible( QWidget *pViews, int iPane )
{
    return ValidPane( iPane ) && AsGrid( pViews )->IsPaneVisible( iPane );
}

void MapViews_ShowAllPanes( QWidget *pViews )
{
    AsGrid( pViews )->ShowAll();
}

void MapViews_FramePane( QWidget *pViews, int iPane )
{
    if ( ValidPane( iPane ) ) { AsGrid( pViews )->FramePane( iPane ); }
}

QString MapViews_TypeTitle( map_view_type_t type )
{
    // Hammer's names.
    switch ( type ) {
        case map_view_type_t::CAMERA: return QStringLiteral( "Perspective" );
        case map_view_type_t::TOP: return QStringLiteral( "Top" );
        case map_view_type_t::FRONT: return QStringLiteral( "Front" );
        case map_view_type_t::SIDE: return QStringLiteral( "Side" );
        case map_view_type_t::ASSETS: return QStringLiteral( "Asset Manager" );
        case map_view_type_t::DATABASE: return QStringLiteral( "Content Library" );
        case map_view_type_t::SHADERS: return QStringLiteral( "Shader View" );
        case map_view_type_t::COUNT: break;
    }
    return QString();
}

} // namespace cypher::editor::map
