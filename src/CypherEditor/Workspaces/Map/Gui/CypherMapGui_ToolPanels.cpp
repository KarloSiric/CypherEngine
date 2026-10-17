//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_ToolPanels.cpp
//  Purpose: Implements the Map workspace's Hammer 5 panels.
//  Details: Each panel reads the session and listens; none knows another.
//           Tool Properties rebuilds only when the tool or the keymap
//           changes, and merely re-reads button states otherwise, so
//           selecting in a view never rebuilds widgets.
//
//  History:
//  - Created by Karlo Siric on 2026-09-30
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_EntityLook.h"
#include "CypherMapGui_ToolPanels.h"
#include "CypherMapGui_FaceEdit.h"
#include "CypherMapGui_Input.h"
#include "CypherMapGui_ToolGestures.h"
#include "CypherMapGui_GeometryCommands.h"
#include "CypherMapGui_Falloff.h"
#include "CypherMapGui_Game.h"
#include "CypherMap_SelectionSets.h"

#include "CypherEditorGui_Section.h"
#include "CypherEditorGui_SettingControl.h"
#include "CypherEditorGui_Style.h"

#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherCommon/Mathlib/CypherMath_Scalar.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"

#include <QFileInfo>
#include <QApplication>
#include <QAbstractButton>
#include <QEvent>
#include <QClipboard>
#include <QButtonGroup>
#include <QMenu>
#include <QMouseEvent>
#include <QLineEdit>
#include <QLocale>
#include <QInputDialog>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QGridLayout>
#include <QHash>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QLabel>
#include <QCheckBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QPointer>
#include <QScrollArea>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <cstring>

#include <initializer_list>
#include <array>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace cypher::editor::map
{

using namespace cypher::common;

namespace
{

QString FromView( string_view_t view )
{
    return QString::fromUtf8( view.pData != nullptr ? view.pData : "", static_cast<qsizetype>( view.cchLength ) );
}

// A creation draft is independent of the selected terrain. Existing sources
// are never resized or overwritten by these fields. Publication uses Edit.
class terrain_controls_t final : public QWidget {
public:
    terrain_controls_t( QWidget *parent, map_workspace_t *workspace ) : QWidget( parent ), m_workspace( workspace ) {
        setObjectName( QStringLiteral( "MapTerrainCreation" ) );
        auto *layout = new QVBoxLayout( this ); layout->setContentsMargins( 6, 6, 6, 6 ); layout->setSpacing( 5 );
        auto *form = new QFormLayout(); form->setFieldGrowthPolicy( QFormLayout::AllNonFixedFieldsGrow );
        const f64 coordinates[]{ workspace->cursor.x, workspace->cursor.y, workspace->cursor.z };
        const char *axes[]{ "X", "Y", "Z" };
        for ( int axis = 0; axis < 3; ++axis ) {
            m_origin[axis] = new QDoubleSpinBox( this );
            m_origin[axis]->setObjectName( QStringLiteral( "MapTerrainOrigin%1" ).arg( axis ) );
            m_origin[axis]->setRange( -1048576.0, 1048576.0 ); m_origin[axis]->setDecimals( 3 );
            m_origin[axis]->setSingleStep( workspace->gridSize ); m_origin[axis]->setValue( coordinates[axis] );
            form->addRow( QStringLiteral( "Origin %1" ).arg( QString::fromLatin1( axes[axis] ) ), m_origin[axis] );
        }
        for ( int axis = 0; axis < 2; ++axis ) {
            m_cells[axis] = new QSpinBox( this ); m_cells[axis]->setObjectName( QStringLiteral( "MapTerrainCells%1" ).arg( axis ) );
            m_cells[axis]->setRange( MAP_TERRAIN_TILE_CELLS, MAP_TERRAIN_MAX_CELLS );
            m_cells[axis]->setSingleStep( MAP_TERRAIN_TILE_CELLS ); m_cells[axis]->setValue( MAP_TERRAIN_TILE_CELLS );
            form->addRow( QStringLiteral( "Cells %1" ).arg( QString::fromLatin1( axes[axis] ) ), m_cells[axis] );
        }
        m_spacing = new QDoubleSpinBox( this ); m_spacing->setObjectName( QStringLiteral( "MapTerrainSpacing" ) );
        m_spacing->setDecimals( 3 ); m_spacing->setRange( 1.0, 8192.0 ); m_spacing->setSingleStep( 8.0 ); m_spacing->setValue( 64.0 );
        m_spacing->setToolTip( QStringLiteral( "World units per cell. This controls terrain resolution independently of the editor snap grid." ) );
        form->addRow( QStringLiteral( "Cell spacing" ), m_spacing ); layout->addLayout( form );
        auto *cursor = new QPushButton( QStringLiteral( "Use map cursor" ), this ); cursor->setObjectName( QStringLiteral( "MapTerrainUseCursor" ) );
        layout->addWidget( cursor );
        m_footprint = new QLabel( this ); m_footprint->setObjectName( QStringLiteral( "MapTerrainFootprint" ) ); m_footprint->setWordWrap( true );
        layout->addWidget( m_footprint );
        m_create = new QPushButton( QStringLiteral( "Create Terrain" ), this ); m_create->setObjectName( QStringLiteral( "MapTerrainCreate" ) );
        m_create->setIcon( gui::EditorStyle_Icon( workspace->pGui->style, "tool-terrain" ) ); layout->addWidget( m_create );
        m_feedback = new QLabel( this ); m_feedback->setObjectName( QStringLiteral( "MapTerrainFeedback" ) ); m_feedback->setWordWrap( true );
        layout->addWidget( m_feedback );
        auto *note = new QLabel( QStringLiteral( "Flat surface, 8–128 cells per axis in steps of 8. Then drag on it to sculpt (Sculpt, below); material painting is planned." ), this );
        note->setProperty( "muted", true ); note->setWordWrap( true ); layout->addWidget( note );
        for ( auto *field : m_origin ) { QObject::connect( field, &QDoubleSpinBox::valueChanged, this, [this]() { DraftChanged(); } ); }
        for ( auto *field : m_cells ) { QObject::connect( field, &QSpinBox::valueChanged, this, [this]() { DraftChanged(); } ); }
        QObject::connect( m_spacing, &QDoubleSpinBox::valueChanged, this, [this]() { DraftChanged(); } );
        QObject::connect( cursor, &QPushButton::clicked, this, [this]() {
            const f64 values[]{ m_workspace->cursor.x, m_workspace->cursor.y, m_workspace->cursor.z };
            for ( int axis = 0; axis < 3; ++axis ) { m_origin[axis]->setValue( values[axis] ); }
        } );
        QObject::connect( m_create, &QPushButton::clicked, this, [this]() {
            const bool changed = MapWorkspace_CreateTerrain( m_workspace, Descriptor() );
            m_feedback->setText( changed ? QStringLiteral( "Terrain created. Undo restores the previous map." ) :
                QStringLiteral( "Terrain could not be created. The map is unchanged." ) ); RefreshState();
        } );
        RefreshState();
    }
    void RefreshState() {
        const auto desc = Descriptor();
        const QLocale locale;
        m_footprint->setText( QStringLiteral( "%1 × %2 units · %3 × %4 cells" )
            .arg( locale.toString( desc.cellsX * desc.cellSize, 'g', 10 ), locale.toString( desc.cellsY * desc.cellSize, 'g', 10 ) )
            .arg( desc.cellsX ).arg( desc.cellsY ) );
        const bool allowed = MapWorkspace_CanCreateTerrain( m_workspace, desc ); m_create->setEnabled( allowed );
        QString reason;
        if ( !allowed ) {
            if ( desc.cellsX % MAP_TERRAIN_TILE_CELLS != 0u || desc.cellsY % MAP_TERRAIN_TILE_CELLS != 0u ) {
                reason = QStringLiteral( "Cell counts must be multiples of 8." );
            } else if ( m_workspace->pDocument != nullptr && m_workspace->pDocument->bReadOnly ) {
                reason = QStringLiteral( "This map is read-only." );
            } else if ( m_workspace->editPreview.bActive || EditorHistory_IsTransactionOpen( &m_workspace->history ) ) {
                reason = QStringLiteral( "Finish or cancel the current edit before creating terrain." );
            } else {
                reason = QStringLiteral( "The footprint must fit within ±1,048,576 units in a writable map with available object IDs." );
            }
            m_feedback->setText( reason );
        } else if ( m_blocked ) { m_feedback->clear(); }
        m_blocked = !allowed;
        m_create->setToolTip( allowed ? QStringLiteral( "Create a new flat surface in one undo step." ) : reason );
    }
private:
    map_terrain_create_desc_t Descriptor() const {
        return { { m_origin[0]->value(), m_origin[1]->value(), m_origin[2]->value() }, m_spacing->value(),
                 static_cast<u32>( m_cells[0]->value() ), static_cast<u32>( m_cells[1]->value() ) };
    }
    void DraftChanged() { m_feedback->clear(); RefreshState(); }
    map_workspace_t *m_workspace{};
    QDoubleSpinBox *m_origin[3]{}, *m_spacing{};
    QSpinBox *m_cells[2]{};
    QLabel *m_footprint{}, *m_feedback{};
    QPushButton *m_create{};
    bool m_blocked{};
};

// Cutter identity is an explicit edit parameter, never inferred from order
// in the selection. The original cutter remains available for further cuts.
class subtract_controls_t final : public QWidget {
public:
    subtract_controls_t( QWidget *parent, map_workspace_t *workspace ) : QWidget( parent ), m_workspace( workspace ) {
        setObjectName( QStringLiteral( "MapSubtractControls" ) );
        auto *layout = new QVBoxLayout( this ); layout->setContentsMargins( 6, 5, 6, 6 ); layout->setSpacing( 5 );
        auto *row = new QHBoxLayout(); row->addWidget( new QLabel( QStringLiteral( "Cutter" ), this ) );
        m_cutter = new QComboBox( this ); m_cutter->setObjectName( QStringLiteral( "MapSubtractCutter" ) );
        m_cutter->setToolTip( QStringLiteral( "Choose a visible brush outside the target selection. This brush is retained." ) );
        row->addWidget( m_cutter, 1 ); layout->addLayout( row );
        m_apply = new QPushButton( QStringLiteral( "Subtract from Selection" ), this );
        m_apply->setObjectName( QStringLiteral( "MapSubtractApply" ) );
        m_apply->setIcon( gui::EditorStyle_Icon( workspace->pGui->style, "boolean-subtract" ) );
        layout->addWidget( m_apply );
        m_feedback = new QLabel( QStringLiteral( "Select target brushes, then choose the cutting brush. The cutter is retained." ), this );
        m_feedback->setObjectName( QStringLiteral( "MapSubtractFeedback" ) ); m_feedback->setWordWrap( true ); layout->addWidget( m_feedback );
        QObject::connect( m_cutter, &QComboBox::currentIndexChanged, this, [this]() { RefreshEnabled(); } );
        QObject::connect( m_apply, &QPushButton::clicked, this, [this]() {
            const bool changed = MapWorkspace_SubtractBrush( m_workspace, m_cutter->currentData().toULongLong() );
            m_feedback->setText( changed ? QStringLiteral( "Cut applied. Undo restores the original targets." ) :
                QStringLiteral( "Nothing changed. Targets must overlap the cutter with a valid solid intersection." ) );
        } );
        RefreshState();
    }
    void RefreshState( bool force = false ) {
        const auto sameBounds = []( const map_bounds_t &a, const map_bounds_t &b ) {
            return a.bHas == b.bHas && a.box.minimum.x == b.box.minimum.x && a.box.minimum.y == b.box.minimum.y &&
                a.box.minimum.z == b.box.minimum.z && a.box.maximum.x == b.box.maximum.x &&
                a.box.maximum.y == b.box.maximum.y && a.box.maximum.z == b.box.maximum.z;
        };
        // Preview-only view notifications do not change eligible cutters.
        // Avoid scanning the map or replacing combo items on every mouse move.
        if ( !force && m_cached && m_document == m_workspace->pDocument && m_selectionRevision == m_workspace->selection.revision &&
             m_hiddenRevision == m_workspace->hidden.revision && m_hiddenVisgroups == m_workspace->hiddenVisgroups &&
             m_cordonActive == m_workspace->bCordonActive && sameBounds( m_cordon, m_workspace->cordon ) ) {
            RefreshEnabled(); return;
        }
        m_cached = true; m_document = m_workspace->pDocument;
        m_selectionRevision = m_workspace->selection.revision; m_hiddenRevision = m_workspace->hidden.revision;
        m_hiddenVisgroups = m_workspace->hiddenVisgroups; m_cordonActive = m_workspace->bCordonActive; m_cordon = m_workspace->cordon;
        const u64 chosen = m_cutter->currentData().toULongLong();
        const QSignalBlocker blocker( m_cutter ); m_cutter->clear();
        m_cutter->addItem( QStringLiteral( "Choose cutting brush..." ), QVariant::fromValue<qulonglong>( 0 ) );
        for ( usize i = 0; i < m_workspace->wire.objects.nCount; ++i ) {
            const auto &object = m_workspace->wire.objects.pData[i];
            if ( object.kind != map_wire_kind_t::BRUSH || !MapWorkspace_IsVisible( m_workspace, object ) || MapWorkspace_IsSelected( m_workspace, object.id ) ) { continue; }
            const auto *record = MapDocument_FindObject( m_workspace->pDocument, object.id, nullptr );
            string_view_t name{};
            ( void )KeyValue_GetString( KeyValue_Find( record, StringView_FromCString( "name" ) ), &name );
            const QString label = name.cchLength != 0 ? FromView( name ) + QStringLiteral( " (#%1)" ).arg( object.id ) : QStringLiteral( "Brush #%1" ).arg( object.id );
            m_cutter->addItem( label, QVariant::fromValue<qulonglong>( object.id ) );
        }
        const int index = m_cutter->findData( QVariant::fromValue<qulonglong>( chosen ) );
        m_cutter->setCurrentIndex( index >= 0 ? index : 0 ); RefreshEnabled();
    }
private:
    void RefreshEnabled() { m_apply->setEnabled( MapWorkspace_CanEditBrushSelection( m_workspace ) && m_cutter->currentData().toULongLong() != 0 ); }
    map_workspace_t *m_workspace{};
    QComboBox *m_cutter{};
    QPushButton *m_apply{};
    QLabel *m_feedback{};
    const map_document_t *m_document{};
    u64 m_selectionRevision{}, m_hiddenRevision{};
    u32 m_hiddenVisgroups{};
    bool_t m_cordonActive{};
    map_bounds_t m_cordon{};
    bool m_cached{ false };
};

// Object-space values are kept in the controls, rather than settings: a
// translation amount is an edit request, not a persistent creation default.
enum class numeric_edit_t { BOX, TRANSLATE, ROTATE, SCALE };

map_bounds_t SelectedBounds( const map_workspace_t &workspace ) noexcept
{
    map_bounds_t bounds{};
    for ( usize i = 0u; i < workspace.wire.objects.nCount; ++i ) {
        const map_wire_object_t &object = workspace.wire.objects.pData[i];
        if ( EditorSelection_Contains( &workspace.selection, object.id ) ) { MapBounds_AddBounds( bounds, object.bounds ); }
    }
    return bounds;
}

// The brush profile across its diameter, and the editor of the Custom
// falloff: drag a point (the curve is mirrored, so either half works),
// click to add one, double-click or right-click to remove one; the ends stay
// at the centre and the edge. Dragging a preset's curve starts a Custom
// curve from its shape. The curve is written when the drag ends.
class falloff_curve_t final : public QWidget {
public:
    falloff_curve_t( QWidget *parent, map_workspace_t *ws ) : QWidget( parent ), m_ws( ws ) {
        setObjectName( QStringLiteral( "MapDisplaceCurve" ) );
        setFixedHeight( 64 ); setMouseTracking( true );
        setToolTip( QStringLiteral( "Brush falloff: strength from the centre (middle) to the edge.\nDrag the curve to draw a Custom falloff; click to add a point, double-click to remove one." ) );
    }
    void SetFalloff( const QString &name, const QString &curve ) {
        if ( m_drag >= 0 ) { return; } // The drag owns the points until it ends.
        m_name = name;
        const QByteArray utf8 = curve.toUtf8();
        m_points = MapFalloff_ParseCurve( { utf8.constData(), static_cast<usize>( utf8.size() ) } );
        update();
    }
    bool IsCustom() const { return m_name == QStringLiteral( "custom" ); }
    const std::vector<map_curve_point_t> &Points() const { return m_points; }
    QRectF Area() const { return QRectF( rect() ).adjusted( 6, 6, -6, -5 ); }
    // A distance (0 centre .. 1 edge) and strength as a point on the right half.
    QPointF At( f64 x, f64 y ) const { const QRectF a = Area(); return { a.center().x() + a.width() * 0.5 * x, a.bottom() - a.height() * y }; }
protected:
    void paintEvent( QPaintEvent * ) override {
        QPainter p( this ); p.setRenderHint( QPainter::Antialiasing );
        const QRectF a = Area();
        p.setPen( QPen( QColor( 255, 255, 255, 40 ), 1.0 ) ); p.drawLine( a.bottomLeft(), a.bottomRight() );
        p.drawLine( QPointF( a.center().x(), a.top() ), QPointF( a.center().x(), a.bottom() ) );
        const QByteArray name = m_name.toUtf8();
        const QByteArray curve = QByteArray::fromStdString( MapFalloff_FormatCurve( m_points ) );
        const map_falloff_t falloff = MapFalloff_Make( { name.constData(), static_cast<usize>( name.size() ) }, { curve.constData(), static_cast<usize>( curve.size() ) } );
        QPainterPath path, fill;
        for ( int i = 0; i <= 96; ++i ) {
            const f64 x = -1.0 + 2.0 * i / 96.0;
            const QPointF at( a.left() + a.width() * ( x + 1.0 ) / 2.0, a.bottom() - a.height() * MapFalloff_At( falloff, std::fabs( x ) ) );
            if ( i == 0 ) { path.moveTo( at ); fill.moveTo( a.bottomLeft() ); fill.lineTo( at ); } else { path.lineTo( at ); fill.lineTo( at ); }
        }
        fill.lineTo( a.bottomRight() ); fill.closeSubpath();
        p.fillPath( fill, QColor( 90, 170, 230, 60 ) );
        p.setPen( QPen( QColor( 120, 190, 240 ), 2.0 ) ); p.drawPath( path );
        if ( !IsCustom() ) { return; }
        for ( usize i = 0; i < m_points.size(); ++i ) {
            const bool hot = static_cast<int>( i ) == ( m_drag >= 0 ? m_drag : m_hover );
            p.setPen( QPen( QColor( 20, 22, 24 ), 1.0 ) ); p.setBrush( hot ? QColor( 240, 200, 80 ) : QColor( 220, 230, 240 ) );
            p.drawEllipse( At( m_points[i].x, m_points[i].y ), hot ? 4.0 : 3.2, hot ? 4.0 : 3.2 );
        }
    }
    void mousePressEvent( QMouseEvent *event ) override {
        if ( !IsCustom() ) { StartCustom(); }
        const int hit = Hit( event->position() );
        if ( event->button() == Qt::RightButton ) { if ( Remove( hit ) ) { Commit(); } return; }
        if ( event->button() != Qt::LeftButton ) { return; }
        m_drag = hit >= 0 ? hit : Insert( event->position() );
        Move( event->position() );
    }
    void mouseMoveEvent( QMouseEvent *event ) override {
        if ( m_drag >= 0 ) { Move( event->position() ); return; }
        const int hover = IsCustom() ? Hit( event->position() ) : -1;
        if ( hover != m_hover ) { m_hover = hover; update(); }
    }
    void mouseReleaseEvent( QMouseEvent *event ) override {
        if ( event->button() != Qt::LeftButton || m_drag < 0 ) { return; }
        m_drag = -1;
        Commit();
    }
    void mouseDoubleClickEvent( QMouseEvent *event ) override {
        if ( IsCustom() && Remove( Hit( event->position() ) ) ) { m_drag = -1; Commit(); }
    }
private:
    // Distance and strength under the mouse (either half).
    map_curve_point_t Value( QPointF at ) const {
        const QRectF a = Area();
        return { std::clamp( std::fabs( at.x() - a.center().x() ) / ( a.width() * 0.5 ), 0.0, 1.0 ), std::clamp( ( a.bottom() - at.y() ) / a.height(), 0.0, 1.0 ) };
    }
    int Hit( QPointF at ) const {
        const map_curve_point_t v = Value( at );
        const QPointF mirrored = At( v.x, v.y );
        int best = -1; f64 bestDistance = 7.0;
        for ( usize i = 0; i < m_points.size(); ++i ) {
            const QPointF d = At( m_points[i].x, m_points[i].y ) - mirrored;
            const f64 distance = std::hypot( d.x(), d.y() );
            if ( distance < bestDistance ) { best = static_cast<int>( i ); bestDistance = distance; }
        }
        return best;
    }
    int Insert( QPointF at ) {
        const map_curve_point_t v = Value( at );
        if ( m_points.size() >= MAP_FALLOFF_CURVE_MAX_POINTS || v.x <= 0.01 || v.x >= 0.99 ) { return -1; }
        const auto it = std::upper_bound( m_points.begin(), m_points.end(), v.x, []( f64 x, const map_curve_point_t &p ) { return x < p.x; } );
        return static_cast<int>( m_points.insert( it, v ) - m_points.begin() );
    }
    bool Remove( int i ) {
        if ( i <= 0 || i + 1 >= static_cast<int>( m_points.size() ) ) { return false; } // The ends stay.
        m_points.erase( m_points.begin() + i ); m_hover = -1;
        return true;
    }
    // Inner points keep their order; the ends keep their distance.
    void Move( QPointF at ) {
        if ( m_drag < 0 ) { return; }
        const map_curve_point_t v = Value( at );
        auto &point = m_points[static_cast<usize>( m_drag )];
        point.y = v.y;
        if ( m_drag > 0 && m_drag + 1 < static_cast<int>( m_points.size() ) ) {
            point.x = std::clamp( v.x, m_points[static_cast<usize>( m_drag - 1 )].x + 0.01, m_points[static_cast<usize>( m_drag + 1 )].x - 0.01 );
        }
        update();
    }
    // The preset's shape, sampled, as the start of a Custom curve.
    void StartCustom() {
        const QByteArray name = m_name.toUtf8();
        const map_falloff_t preset = MapFalloff_Make( { name.constData(), static_cast<usize>( name.size() ) }, {} );
        m_points.clear();
        for ( f64 x : { 0.0, 0.25, 0.5, 0.75, 1.0 } ) { m_points.push_back( { x, MapFalloff_At( preset, x ) } ); }
        // The curve first: the switch to Custom reloads the curve from settings.
        const std::string text = MapFalloff_FormatCurve( m_points );
        Write( "editor.map.displace_curve", setting_type_t::STRING, text.c_str() );
        Write( "editor.map.displace_falloff", setting_type_t::ENUM, "custom" );
        m_name = QStringLiteral( "custom" );
    }
    void Commit() {
        const std::string text = MapFalloff_FormatCurve( m_points );
        Write( "editor.map.displace_curve", setting_type_t::STRING, text.c_str() );
        update();
    }
    void Write( const char *path, setting_type_t type, const char *text ) {
        const auto *descriptor = EditorSettings_Find( &m_ws->pGui->settings, StringView_FromCString( path ) );
        setting_value_t value{}; value.type = type; value.text = StringView_FromCString( text );
        if ( descriptor != nullptr ) { ( void )EditorSettings_Write( &m_ws->pGui->settings, settings_scope_t::USER, *descriptor, value ); }
    }
    map_workspace_t *m_ws{};
    QString m_name{ QStringLiteral( "smooth" ) };
    std::vector<map_curve_point_t> m_points{};
    int m_drag{ -1 }, m_hover{ -1 };
};

// Hammer 5's Paint Displacement brush: the operations as an icon row and the
// falloff as curves, both drawn here (no image files), writing the same
// settings the Sculpt section shows.
class displace_controls_t final : public QWidget {
public:
    displace_controls_t( QWidget *parent, map_workspace_t *ws ) : QWidget( parent ), m_ws( ws ) {
        setObjectName( QStringLiteral( "MapDisplaceBrush" ) );
        auto *layout = new QVBoxLayout( this ); layout->setContentsMargins( 6, 5, 6, 6 ); layout->setSpacing( 6 );
        const auto row = [&]( const auto &items, const char *path, std::vector<QToolButton *> &buttons, bool curve ) {
            auto *line = new QHBoxLayout(); line->setSpacing( 3 ); line->setAlignment( Qt::AlignLeft );
            auto *group = new QButtonGroup( this ); group->setExclusive( true );
            for ( const auto &item : items ) {
                auto *button = new QToolButton( this );
                button->setObjectName( QStringLiteral( "MapDisplace.%1" ).arg( QString::fromLatin1( item.name ) ) );
                button->setToolTip( QString::fromUtf8( item.help ) ); button->setAccessibleName( QString::fromLatin1( item.label ) );
                button->setIcon( curve ? CurveIcon( item.name ) : ModeIcon( item.name ) ); button->setIconSize( QSize( 28, 28 ) ); button->setFixedSize( 36, 36 );
                button->setCheckable( true ); button->setAutoRaise( true );
                group->addButton( button ); line->addWidget( button ); buttons.push_back( button );
                QObject::connect( button, &QToolButton::clicked, this, [this, path, name = item.name]() {
                    const auto *descriptor = EditorSettings_Find( &m_ws->pGui->settings, StringView_FromCString( path ) );
                    setting_value_t value{}; value.type = setting_type_t::ENUM; value.text = StringView_FromCString( name );
                    if ( descriptor != nullptr ) { ( void )EditorSettings_Write( &m_ws->pGui->settings, settings_scope_t::USER, *descriptor, value ); }
                    RefreshState();
                } );
            }
            layout->addLayout( line );
        };
        row( kModes, "editor.map.displace_mode", m_modes, false );
        m_curve = new falloff_curve_t( this, ws );
        layout->addWidget( m_curve );
        row( kFalloffs, "editor.map.displace_falloff", m_falloffs, true );
        ( void )EditorSettings_AddListener( &ws->pGui->settings, &OnSettingsChanged, this );
        RefreshState();
    }
    ~displace_controls_t() override { EditorSettings_RemoveListener( &m_ws->pGui->settings, &OnSettingsChanged, this ); }
    void RefreshState() {
        const auto mode = EditorSettings_Text( &m_ws->pGui->settings, "editor.map.displace_mode", StringView_FromCString( "push_pull" ) );
        const auto falloff = EditorSettings_Text( &m_ws->pGui->settings, "editor.map.displace_falloff", StringView_FromCString( "smooth" ) );
        const auto curve = EditorSettings_Text( &m_ws->pGui->settings, "editor.map.displace_curve", StringView_FromCString( MAP_FALLOFF_CURVE_DEFAULT ) );
        for ( usize i = 0; i < m_modes.size(); ++i ) { const QSignalBlocker b( m_modes[i] ); m_modes[i]->setChecked( StringView_Equals( mode, StringView_FromCString( kModes[i].name ) ) ); }
        for ( usize i = 0; i < m_falloffs.size(); ++i ) {
            const bool on = StringView_Equals( falloff, StringView_FromCString( kFalloffs[i].name ) );
            const QSignalBlocker b( m_falloffs[i] ); m_falloffs[i]->setChecked( on );
        }
        // The Custom button shows the curve as drawn.
        if ( !m_falloffs.empty() ) { m_falloffs.back()->setIcon( QIcon( CurvePixmap( MapFalloff_Make( StringView_FromCString( "custom" ), curve ), QSize( 28, 28 ), false ) ) ); }
        m_curve->SetFalloff( FromView( falloff ), FromView( curve ) );
    }
private:
    struct item_t { const char *name; const char *label; const char *help; };
    static constexpr item_t kModes[]{
        { "push_pull", "Push/Pull", "Push/Pull: raise the surface (Ctrl/Command lowers it)" },
        { "smooth", "Smooth", "Smooth: even out bumps under the brush" },
        { "flatten", "Flatten", "Flatten: press the surface onto the plane under the brush" },
        { "inflate", "Inflate", "Inflate: swell the surface along its normals" },
        { "pinch", "Pinch", "Pinch: draw the surface toward the brush centre" },
        { "noise", "Noise", "Noise: roughen the surface" } };
    static constexpr item_t kFalloffs[]{
        { "smooth", "Smooth", "Smooth falloff: full in the middle, easing out to the edge" },
        { "linear", "Linear", "Linear falloff: fading evenly from the middle to the edge" },
        { "sharp", "Sharp", "Sharp falloff: strong in the middle only" },
        { "constant", "Constant", "Constant: the same strength across the whole brush" },
        { "custom", "Custom", "Custom falloff: the curve drawn above (drag it to change it)" } };
    // The brush profile across its diameter: strength against distance.
    static QPixmap CurvePixmap( const map_falloff_t &falloff, QSize size, bool framed ) {
        QPixmap pixmap( size * 2 ); pixmap.setDevicePixelRatio( 2.0 ); pixmap.fill( Qt::transparent );
        QPainter p( &pixmap ); p.setRenderHint( QPainter::Antialiasing );
        const QRectF area = QRectF( QPointF( 0, 0 ), QSizeF( size ) ).adjusted( 3, 4, -3, -3 );
        if ( framed ) { p.setPen( QPen( QColor( 255, 255, 255, 40 ), 1.0 ) ); p.drawLine( area.bottomLeft(), area.bottomRight() ); p.drawLine( QPointF( area.center().x(), area.top() ), QPointF( area.center().x(), area.bottom() ) ); }
        QPainterPath path; QPainterPath fill;
        for ( int i = 0; i <= 64; ++i ) {
            const f64 x = -1.0 + 2.0 * i / 64.0, y = MapFalloff_At( falloff, std::fabs( x ) );
            const QPointF at( area.left() + area.width() * ( x + 1.0 ) / 2.0, area.bottom() - area.height() * y );
            if ( i == 0 ) { path.moveTo( at ); fill.moveTo( area.bottomLeft() ); fill.lineTo( at ); } else { path.lineTo( at ); fill.lineTo( at ); }
        }
        fill.lineTo( area.bottomRight() ); fill.closeSubpath();
        p.fillPath( fill, QColor( 90, 170, 230, 60 ) );
        p.setPen( QPen( QColor( 120, 190, 240 ), framed ? 2.0 : 1.6 ) ); p.drawPath( path );
        return pixmap;
    }
    static QIcon CurveIcon( const char *name ) { return QIcon( CurvePixmap( MapFalloff_Make( StringView_FromCString( name ), StringView_FromCString( MAP_FALLOFF_CURVE_DEFAULT ) ), QSize( 28, 28 ), false ) ); }
    // A grey ground line and the operation drawn over it.
    static QIcon ModeIcon( const char *name ) {
        QPixmap pixmap( 56, 56 ); pixmap.setDevicePixelRatio( 2.0 ); pixmap.fill( Qt::transparent );
        QPainter p( &pixmap ); p.setRenderHint( QPainter::Antialiasing );
        const QColor ground( 150, 158, 166 ), mark( 230, 200, 80 );
        p.setPen( QPen( ground, 1.8, Qt::SolidLine, Qt::RoundCap ) );
        QPainterPath surface;
        const auto wave = [&]( f64 amplitude, int ridges, f64 base ) {
            surface = QPainterPath();
            for ( int i = 0; i <= 24; ++i ) { const f64 x = 2 + 24.0 * i / 24; const f64 y = base - amplitude * std::sin( 3.14159265 * ridges * i / 24.0 ); if ( i == 0 ) { surface.moveTo( x, y ); } else { surface.lineTo( x, y ); } }
        };
        const auto arrow = [&]( QPointF from, QPointF to ) {
            p.setPen( QPen( mark, 1.8, Qt::SolidLine, Qt::RoundCap ) ); p.drawLine( from, to );
            const QPointF d = ( to - from ) / std::max( 1e-6, std::hypot( ( to - from ).x(), ( to - from ).y() ) );
            const QPointF n( -d.y(), d.x() );
            p.drawLine( to, to - d * 4 + n * 3 ); p.drawLine( to, to - d * 4 - n * 3 );
        };
        if ( std::strcmp( name, "push_pull" ) == 0 ) {
            surface.moveTo( 2, 22 ); surface.cubicTo( 9, 22, 10, 12, 14, 12 ); surface.cubicTo( 18, 12, 19, 22, 26, 22 );
            p.drawPath( surface ); arrow( QPointF( 14, 10 ), QPointF( 14, 3 ) );
        } else if ( std::strcmp( name, "smooth" ) == 0 ) {
            wave( 3.0, 4, 20 ); p.setPen( QPen( ground, 1.2, Qt::DashLine ) ); p.drawPath( surface );
            p.setPen( QPen( mark, 1.8 ) ); p.drawLine( QPointF( 2, 13 ), QPointF( 26, 13 ) );
        } else if ( std::strcmp( name, "flatten" ) == 0 ) {
            wave( 4.0, 2, 19 ); p.drawPath( surface );
            p.setPen( QPen( mark, 2.2 ) ); p.drawLine( QPointF( 4, 12 ), QPointF( 24, 12 ) ); arrow( QPointF( 14, 4 ), QPointF( 14, 10 ) );
        } else if ( std::strcmp( name, "inflate" ) == 0 ) {
            p.drawEllipse( QPointF( 14, 16 ), 5, 5 );
            p.setPen( QPen( mark, 1.6, Qt::DashLine ) ); p.drawEllipse( QPointF( 14, 16 ), 9.5, 9.5 );
        } else if ( std::strcmp( name, "pinch" ) == 0 ) {
            p.drawLine( QPointF( 2, 22 ), QPointF( 26, 22 ) );
            arrow( QPointF( 3, 12 ), QPointF( 11, 12 ) ); arrow( QPointF( 25, 12 ), QPointF( 17, 12 ) );
        } else {
            surface.moveTo( 2, 18 );
            const f64 jag[]{ 14, 21, 12, 19, 10, 20, 13, 22, 15, 18, 12, 20 };
            for ( int i = 0; i < 12; ++i ) { surface.lineTo( 4 + i * 2, jag[i] ); }
            p.setPen( QPen( mark, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) ); p.drawPath( surface );
        }
        return QIcon( pixmap );
    }
    static void OnSettingsChanged( void *context, string_view_t path ) noexcept {
        if ( path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( "editor.map.displace_mode" ) ) ||
             StringView_Equals( path, StringView_FromCString( "editor.map.displace_falloff" ) ) ||
             StringView_Equals( path, StringView_FromCString( "editor.map.displace_curve" ) ) ) { static_cast<displace_controls_t *>( context )->RefreshState(); }
    }
    map_workspace_t *m_ws{};
    std::vector<QToolButton *> m_modes{}, m_falloffs{};
    falloff_curve_t *m_curve{};
};

// The game's point classes for the Entity tool (Hammer's class list,
// TrenchBroom's entity browser): by category, filtered as you type; a click
// makes a class the one the tool places. Without a game profile it says why.
class entity_classes_t final : public QWidget {
public:
    entity_classes_t( QWidget *parent, map_workspace_t *ws ) : QWidget( parent ), m_ws( ws ) {
        setObjectName( QStringLiteral( "MapEntityClassBrowser" ) );
        auto *layout = new QVBoxLayout( this ); layout->setContentsMargins( 6, 5, 6, 6 ); layout->setSpacing( 4 );
        auto *row = new QHBoxLayout(); row->setSpacing( 3 );
        m_filter = new QLineEdit( this ); m_filter->setObjectName( QStringLiteral( "MapEntityClassFilter" ) );
        m_filter->setPlaceholderText( QStringLiteral( "Filter classes" ) ); m_filter->setClearButtonEnabled( true );
        auto *reload = new QToolButton( this ); reload->setObjectName( QStringLiteral( "MapEntityClassReload" ) );
        reload->setIcon( EditorStyle_Icon( ws->pGui->style, "file-open" ) ); reload->setAutoRaise( true );
        reload->setToolTip( QStringLiteral( "Reload the game profile (game/<game>.cygame)" ) );
        row->addWidget( m_filter, 1 ); row->addWidget( reload );
        m_tree = new QTreeWidget( this ); m_tree->setObjectName( QStringLiteral( "MapEntityClasses" ) );
        m_tree->setHeaderHidden( true ); m_tree->setIndentation( 12 ); m_tree->setMinimumHeight( 160 );
        m_status = new QLabel( this ); m_status->setObjectName( QStringLiteral( "MapEntityClassesStatus" ) );
        m_status->setWordWrap( true ); m_status->setProperty( "muted", true );
        layout->addLayout( row ); layout->addWidget( m_tree ); layout->addWidget( m_status );
        QObject::connect( m_filter, &QLineEdit::textChanged, this, [this]() { Filter(); } );
        QObject::connect( reload, &QToolButton::clicked, this, [this]() {
            ( void )EditorCommands_Execute( &m_ws->pGui->commands, StringView_FromCString( "map.game.reload" ), command_args_t{} );
            Rebuild();
        } );
        QObject::connect( m_tree, &QTreeWidget::itemClicked, this, [this]( QTreeWidgetItem *item, int ) { Choose( item ); } );
        ( void )MapWorkspace_AddListener( ws, &OnChanged, this );
        ( void )EditorSettings_AddListener( &ws->pGui->settings, &OnSettingsChanged, this );
        Rebuild();
    }
    ~entity_classes_t() override {
        EditorSettings_RemoveListener( &m_ws->pGui->settings, &OnSettingsChanged, this );
        MapWorkspace_RemoveListener( m_ws, &OnChanged, this );
    }
private:
    static constexpr int kClassRole = Qt::UserRole;
    // A different map can name a different game: rebuild when the profile changes.
    static void OnChanged( void *context, u32 ) noexcept {
        auto *self = static_cast<entity_classes_t *>( context );
        if ( MapGame_Profile( self->m_ws ) != self->m_profile ) { self->Rebuild(); }
    }
    static void OnSettingsChanged( void *context, string_view_t path ) noexcept {
        if ( path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( "editor.map.default_point_class" ) ) ) { static_cast<entity_classes_t *>( context )->MarkCurrent(); }
    }
    void Rebuild() {
        m_profile = MapGame_Profile( m_ws );
        const QSignalBlocker blocker( m_tree );
        m_tree->clear();
        QHash<QString, QTreeWidgetItem *> categories;
        for ( const map_game_class_t *cls : MapGame_Placeable( m_ws, map_game_kind_t::POINT ) ) {
            const QString category = cls->category.empty() ? QStringLiteral( "Other" ) : QString::fromStdString( cls->category );
            QTreeWidgetItem *&parent = categories[category];
            if ( parent == nullptr ) {
                parent = new QTreeWidgetItem( m_tree, { category } );
                parent->setFlags( Qt::ItemIsEnabled ); parent->setExpanded( true );
                QFont font = parent->font( 0 ); font.setBold( true ); parent->setFont( 0, font );
            }
            const QString name = QString::fromStdString( cls->name );
            auto *item = new QTreeWidgetItem( parent, { cls->label.empty() ? name : QStringLiteral( "%1  (%2)" ).arg( QString::fromStdString( cls->label ), name ) } );
            item->setData( 0, kClassRole, name );
            item->setToolTip( 0, cls->description.empty() ? name : QStringLiteral( "%1\n%2" ).arg( name, QString::fromStdString( cls->description ) ) );
        }
        const QString status = MapGame_Status( m_ws );
        m_status->setText( m_profile != nullptr ? status : QStringLiteral( "%1. The tool places the class set in Default point class." ).arg( status ) );
        m_tree->setVisible( m_profile != nullptr );
        Filter();
        MarkCurrent();
    }
    void Filter() {
        const QString text = m_filter->text().trimmed();
        for ( int c = 0; c < m_tree->topLevelItemCount(); ++c ) {
            QTreeWidgetItem *category = m_tree->topLevelItem( c );
            bool any = false;
            for ( int i = 0; i < category->childCount(); ++i ) {
                QTreeWidgetItem *item = category->child( i );
                const bool show = text.isEmpty() || item->text( 0 ).contains( text, Qt::CaseInsensitive ) || category->text( 0 ).contains( text, Qt::CaseInsensitive );
                item->setHidden( !show ); any = any || show;
            }
            category->setHidden( !any );
        }
    }
    void MarkCurrent() {
        const QString current = FromView( EditorSettings_Text( &m_ws->pGui->settings, "editor.map.default_point_class", StringView_FromCString( "info_player_start" ) ) );
        const QSignalBlocker blocker( m_tree );
        for ( QTreeWidgetItemIterator it( m_tree ); *it != nullptr; ++it ) {
            if ( ( *it )->data( 0, kClassRole ).toString() == current ) { m_tree->setCurrentItem( *it ); return; }
        }
        m_tree->setCurrentItem( nullptr );
    }
    void Choose( QTreeWidgetItem *item ) {
        const QByteArray name = item != nullptr ? item->data( 0, kClassRole ).toString().toUtf8() : QByteArray();
        if ( name.isEmpty() ) { return; }
        const auto *descriptor = EditorSettings_Find( &m_ws->pGui->settings, StringView_FromCString( "editor.map.default_point_class" ) );
        setting_value_t value{}; value.type = setting_type_t::STRING; value.text = { name.constData(), static_cast<usize>( name.size() ) };
        if ( descriptor != nullptr ) { ( void )EditorSettings_Write( &m_ws->pGui->settings, settings_scope_t::USER, *descriptor, value ); }
    }
    map_workspace_t *m_ws{};
    const map_game_profile_t *m_profile{};
    QLineEdit *m_filter{};
    QTreeWidget *m_tree{};
    QLabel *m_status{};
};

// Shape defaults share the registry with Settings, while the viewport stage
// keeps its captured descriptor. Switching icons never rewrites a staged mesh.
class primitive_controls_t final : public QWidget {
public:
    primitive_controls_t( QWidget *parent, map_workspace_t *ws ) : QWidget( parent ), m_ws( ws ) {
        setObjectName( QStringLiteral( "editor.map.new_brush_shape" ) );
        auto *layout = new QVBoxLayout( this ); layout->setContentsMargins( 6, 5, 6, 6 ); layout->setSpacing( 5 );
        auto *icons = new QHBoxLayout(); icons->setSpacing( 3 ); icons->setAlignment( Qt::AlignLeft );
        auto *group = new QButtonGroup( this ); group->setExclusive( true );
        for ( const auto &shape : kShapes ) {
            auto *button = new QToolButton( this );
            button->setObjectName( QStringLiteral( "MapPrimitive.%1" ).arg( QString::fromLatin1( shape.name ) ) );
            button->setText( QString::fromLatin1( shape.label ) ); button->setAccessibleName( button->text() );
            button->setToolTip( QString::fromLatin1( shape.help ) );
            button->setIcon( gui::EditorStyle_Icon( ws->pGui->style, shape.icon ) );
            button->setIconSize( QSize( 24, 24 ) ); button->setFixedSize( 32, 32 );
            button->setToolButtonStyle( Qt::ToolButtonIconOnly ); button->setProperty( "compactIcon", true );
            button->setCheckable( true ); button->setFocusPolicy( Qt::StrongFocus );
            group->addButton( button ); icons->addWidget( button ); m_buttons.push_back( button );
            QObject::connect( button, &QToolButton::clicked, this, [this, shape]() {
                const QPointer<primitive_controls_t> alive( this );
                const auto *descriptor = EditorSettings_Find( &m_ws->pGui->settings, StringView_FromCString( "editor.map.new_brush_shape" ) );
                setting_value_t value{}; value.type = setting_type_t::ENUM; value.text = StringView_FromCString( shape.name );
                if ( descriptor != nullptr ) { ( void )EditorSettings_Write( &m_ws->pGui->settings, settings_scope_t::USER, *descriptor, value ); }
                if ( alive ) { alive->RefreshState(); } // A listener can change tools and destroy this control.
            } );
        }
        layout->addLayout( icons );
        m_identity = new QLabel( this ); m_identity->setObjectName( QStringLiteral( "MapPrimitiveIdentity" ) ); layout->addWidget( m_identity );
        m_form = new QFormLayout(); m_form->setContentsMargins( 0, 0, 0, 0 ); m_form->setSpacing( 4 );
        for ( const char *path : kParameters ) {
            auto *control = gui::EditorSettingControl_Create( this, &ws->pGui->settings, path );
            m_form->addRow( gui::EditorSettingControl_Label( &ws->pGui->settings, path ), control );
        }
        layout->addLayout( m_form );
        ( void )EditorSettings_AddListener( &ws->pGui->settings, &OnSettingsChanged, this );
        RefreshState();
    }
    ~primitive_controls_t() override { EditorSettings_RemoveListener( &m_ws->pGui->settings, &OnSettingsChanged, this ); }
    void SetRefreshCallback( void ( *callback )( void * ), void *context ) { m_refresh = callback; m_refreshContext = context; }
    void RefreshState() {
        const auto shape = EditorSettings_Text( &m_ws->pGui->settings, "editor.map.new_brush_shape", StringView_FromCString( "box" ) );
        for ( usize i = 0; i < m_buttons.size(); ++i ) {
            const bool selected = StringView_Equals( shape, StringView_FromCString( kShapes[i].name ) );
            const QSignalBlocker blocker( m_buttons[i] ); m_buttons[i]->setChecked( selected );
            if ( selected ) { m_identity->setText( QString::fromLatin1( kShapes[i].label ) ); }
        }
        const bool quad = StringView_Equals( shape, StringView_FromCString( "quad" ) );
        const bool cylinder = StringView_Equals( shape, StringView_FromCString( "cylinder" ) );
        const bool spike = StringView_Equals( shape, StringView_FromCString( "spike" ) );
        const auto is = [&]( const char *name ) { return StringView_Equals( shape, StringView_FromCString( name ) ); };
        const bool arch = is( "arch" ), stairs = is( "stairs" ), spiral = is( "spiral_stairs" ), pipe = is( "pipe" ), torus = is( "torus" );
        const bool round = arch || spiral || pipe || torus;
        m_form->setRowVisible( 0, quad || cylinder || spike || is( "wedge" ) || round );
        m_form->setRowVisible( 1, cylinder || spike || arch || pipe || torus ); m_form->setRowVisible( 2, spike );
        m_form->setRowVisible( 3, is( "sphere" ) );
        m_form->setRowVisible( 4, arch || pipe ); m_form->setRowVisible( 5, arch ); m_form->setRowVisible( 6, arch ); // Wall, arc, start.
        m_form->setRowVisible( 7, stairs || spiral ); m_form->setRowVisible( 8, stairs );                          // Steps, rise.
        m_form->setRowVisible( 9, spiral ); m_form->setRowVisible( 10, spiral ); m_form->setRowVisible( 11, torus ); // Turn, inner, tube.
        if ( auto *label = qobject_cast<QLabel *>( m_form->itemAt( 0, QFormLayout::LabelRole )->widget() ) ) {
            label->setText( quad ? QStringLiteral( "Plane normal" ) : QStringLiteral( "Primitive axis" ) );
        }
        if ( MapWorkspace_HasBlockPreview( m_ws ) ) { m_identity->setText( m_identity->text() + QStringLiteral( " · next creation" ) ); }
    }
    void RefreshIcons() {
        for ( usize i = 0; i < m_buttons.size(); ++i ) { m_buttons[i]->setIcon( gui::EditorStyle_Icon( m_ws->pGui->style, kShapes[i].icon ) ); }
    }
private:
    static void OnSettingsChanged( void *context, string_view_t path ) noexcept {
        if ( path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( "editor.map.new_brush_shape" ) ) ||
             StringView_Equals( path, StringView_FromCString( "editor.map.primitive_axis" ) ) ) {
            auto *control = static_cast<primitive_controls_t *>( context );
            control->RefreshState();
            if ( control->m_refresh != nullptr ) { control->m_refresh( control->m_refreshContext ); }
        }
    }
    struct shape_t { const char *name; const char *label; const char *icon; const char *help; };
    static constexpr shape_t kShapes[]{
        { "box", "Box", "shape-box", "Box · solid rectangular brush" },
        { "quad", "Quad", "shape-quad", "Quad · flat rectangular mesh with four vertices and one face. Draw in a 2D pane or on the 3D ground plane; Plane normal controls numeric creation." },
        { "wedge", "Wedge", "shape-wedge", "Wedge · solid ramp brush" },
        { "cylinder", "Cylinder", "shape-cylinder", "Cylinder · solid brush with adjustable sides" },
        { "spike", "Spike", "shape-spike", "Spike · solid cone or tapered brush" },
        { "sphere", "Sphere", "shape-sphere", "Sphere · round latitude bands with adjustable detail" },
        { "arch", "Arch", "shape-arch", "Arch · a curved wall round the primitive axis (Hammer 5); set the axis to X or Y for a doorway arch" },
        { "stairs", "Stairs", "shape-stairs", "Stairs · a staircase mesh filling the box, rising toward the chosen direction" },
        { "spiral_stairs", "Spiral Stairs", "shape-spiral-stairs", "Spiral stairs · wedge treads turning round the primitive axis, each one step higher" },
        { "pipe", "Pipe", "shape-pipe", "Pipe · a hollow tube with the chosen wall width" },
        { "torus", "Torus", "shape-torus", "Torus · a ring mesh round the primitive axis" }
    };
    static constexpr const char *kParameters[]{ "editor.map.primitive_axis", "editor.map.cylinder_sides", "editor.map.cone_top_radius", "editor.map.sphere_subdivisions",
                                                "editor.map.arch_thickness", "editor.map.arch_angle", "editor.map.arch_start", "editor.map.stairs_steps",
                                                "editor.map.stairs_direction", "editor.map.spiral_turn", "editor.map.spiral_inner", "editor.map.torus_tube" };
    map_workspace_t *m_ws{};
    QFormLayout *m_form{};
    QLabel *m_identity{};
    std::vector<QToolButton *> m_buttons{};
    void ( *m_refresh )( void * ){};
    void *m_refreshContext{};
};

// Whole units stay readable in the narrow sidebar without hiding a useful
// fractional value or changing the precision accepted by the control.
class geometry_number_t final : public QDoubleSpinBox {
public:
    explicit geometry_number_t( QWidget *parent ) : QDoubleSpinBox( parent ) { setLocale( QLocale::c() ); }
protected:
    QString textFromValue( double value ) const override { return QLocale::c().toString( value, 'g', 10 ); }
};

class geometry_controls_t final : public QWidget {
public:
    geometry_controls_t( QWidget *pParent, map_workspace_t *pWorkspace, numeric_edit_t kind, bool subscribe = true )
        : QWidget( pParent ), m_pWorkspace( pWorkspace ), m_kind( kind ), m_subscribed( subscribe )
    {
        const char *pName = kind == numeric_edit_t::BOX ? "MapBoxControls" : kind == numeric_edit_t::TRANSLATE ? "MapMoveControls" :
                            kind == numeric_edit_t::ROTATE ? "MapRotateControls" : "MapScaleControls";
        setObjectName( QString::fromLatin1( pName ) );
        auto *pLayout = new QVBoxLayout( this );
        pLayout->setContentsMargins( 6, 5, 6, 6 );
        pLayout->setSpacing( 5 );
        auto *pGrid = new QGridLayout();
        pGrid->setContentsMargins( 0, 0, 0, 0 );
        pGrid->setHorizontalSpacing( 4 );
        pGrid->setVerticalSpacing( 3 );
        for ( int axis = 0; axis < 3; ++axis ) {
            auto *pAxis = new QLabel( QString::fromLatin1( axis == 0 ? "X" : axis == 1 ? "Y" : "Z" ), this );
            pAxis->setAlignment( Qt::AlignCenter );
            pGrid->addWidget( pAxis, 0, axis + 1 );
        }
        const auto row = [&]( int nRow, const QString &label, const char *pPrefix, QDoubleSpinBox **pFields, f64 initial, f64 minimum ) {
            pGrid->addWidget( new QLabel( label, this ), nRow, 0 );
            for ( int axis = 0; axis < 3; ++axis ) {
                auto *pField = new geometry_number_t( this );
                pField->setObjectName( QStringLiteral( "%1%2" ).arg( QString::fromLatin1( pPrefix ), QString::fromLatin1( axis == 0 ? "X" : axis == 1 ? "Y" : "Z" ) ) );
                pField->setDecimals( kind == numeric_edit_t::SCALE ? 4 : 3 );
                pField->setRange( minimum, kind == numeric_edit_t::SCALE ? 1000.0 : 10000000.0 );
                pField->setSingleStep( kind == numeric_edit_t::SCALE ? 0.1 : kind == numeric_edit_t::ROTATE ? std::max( pWorkspace->angleSnap, 1.0 ) : pWorkspace->gridSize );
                pField->setValue( initial );
                pField->setMinimumWidth( 45 );
                pField->setKeyboardTracking( false );
                pField->setAccessibleName( QStringLiteral( "%1 %2" ).arg( label, pGrid->itemAtPosition( 0, axis + 1 )->widget()->property( "text" ).toString() ) );
                pFields[axis] = pField;
                pGrid->addWidget( pField, nRow, axis + 1 );
            }
        };
        if ( kind == numeric_edit_t::BOX ) {
            row( 1, QStringLiteral( "Center" ), "MapBoxCenter", m_auxiliary, 0.0, -10000000.0 );
            row( 2, QStringLiteral( "Size" ), "MapBoxSize", m_values, std::max( 64.0, pWorkspace->gridSize ), 0.001 );
            const map_bounds_t bounds = SelectedBounds( *pWorkspace );
            const math::vec3d_t center = bounds.bHas ? MapBounds_Center( bounds ) : pWorkspace->cursor;
            m_auxiliary[0]->setValue( center.x ); m_auxiliary[1]->setValue( center.y ); m_auxiliary[2]->setValue( center.z );
        } else {
            row( 1, kind == numeric_edit_t::TRANSLATE ? QStringLiteral( "Offset" ) : kind == numeric_edit_t::ROTATE ? QStringLiteral( "Degrees" ) : QStringLiteral( "Factor" ),
                 kind == numeric_edit_t::TRANSLATE ? "MapMove" : kind == numeric_edit_t::ROTATE ? "MapRotate" : "MapScale", m_values,
                 kind == numeric_edit_t::SCALE ? 1.0 : 0.0, kind == numeric_edit_t::SCALE ? 0.0001 : -10000000.0 );
        }
        pLayout->addLayout( pGrid );
        if ( kind == numeric_edit_t::ROTATE || kind == numeric_edit_t::SCALE ) {
            m_pPivot = new QComboBox( this );
            m_pPivot->setObjectName( QStringLiteral( "MapTransformPivot" ) );
            m_pPivot->addItems( { QStringLiteral( "Selection center" ), QStringLiteral( "World origin" ), QStringLiteral( "Pivot point" ) } );
            m_pPivot->setItemData( 2, QStringLiteral( "The point placed with the Pivot tool, or with Set Pivot Here in a view's right-click menu." ), Qt::ToolTipRole );
            auto *pPivotRow = new QHBoxLayout();
            pPivotRow->addWidget( new QLabel( QStringLiteral( "Pivot" ), this ) );
            pPivotRow->addWidget( m_pPivot, 1 );
            pLayout->addLayout( pPivotRow );
        }
        auto *pButtonRow = new QHBoxLayout();
        m_pApply = new QPushButton( kind == numeric_edit_t::BOX ? QStringLiteral( "Create Brush" ) : kind == numeric_edit_t::TRANSLATE ? QStringLiteral( "Move Selection" ) :
                                   kind == numeric_edit_t::ROTATE ? QStringLiteral( "Rotate Selection" ) : QStringLiteral( "Scale Selection" ), this );
        m_pApply->setObjectName( QStringLiteral( "MapNumericApply" ) );
        m_pApply->setIcon( gui::EditorStyle_Icon( pWorkspace->pGui->style, kind == numeric_edit_t::BOX ? "tool-block" :
                         kind == numeric_edit_t::TRANSLATE ? "tool-translate" : kind == numeric_edit_t::ROTATE ? "tool-rotate" : "tool-scale" ) );
        pButtonRow->addWidget( m_pApply, 1 );
        QObject::connect( m_pApply, &QPushButton::clicked, this, [this]() { Apply( false ); } );
        if ( kind == numeric_edit_t::TRANSLATE ) {
            m_pClone = new QPushButton( QStringLiteral( "Clone + Move" ), this );
            m_pClone->setObjectName( QStringLiteral( "MapNumericClone" ) );
            m_pClone->setToolTip( QStringLiteral( "Duplicate the selection and move the copies in one undo step" ) );
            pButtonRow->addWidget( m_pClone );
            QObject::connect( m_pClone, &QPushButton::clicked, this, [this]() { Apply( true ); } );
        }
        pLayout->addLayout( pButtonRow );
        m_pFeedback = new QLabel( this );
        m_pFeedback->setObjectName( QStringLiteral( "MapNumericFeedback" ) );
        m_pFeedback->setWordWrap( true );
        m_pFeedback->setProperty( "muted", true );
        pLayout->addWidget( m_pFeedback );
        if ( kind == numeric_edit_t::BOX ) {
            for ( int axis = 0; axis < 3; ++axis ) {
                QObject::connect( m_values[axis], &QDoubleSpinBox::valueChanged, this, [this, axis]() { UpdateBlockBounds( axis, true ); } );
                QObject::connect( m_auxiliary[axis], &QDoubleSpinBox::valueChanged, this, [this, axis]() { UpdateBlockBounds( axis, false ); } );
            }
        }
        if ( m_subscribed ) { ( void )MapWorkspace_AddListener( pWorkspace, &geometry_controls_t::OnChanged, this ); }
        Refresh();
    }

    ~geometry_controls_t() override { if ( m_subscribed ) { MapWorkspace_RemoveListener( m_pWorkspace, &geometry_controls_t::OnChanged, this ); } }
    void RefreshState() { Refresh(); }

private:
    static void OnChanged( void *pContext, u32 changes ) noexcept
    {
        if ( ( changes & ( MAP_CHANGE_DOCUMENT | MAP_CHANGE_SELECTION | MAP_CHANGE_VIEW ) ) != 0u ) { static_cast<geometry_controls_t *>( pContext )->Refresh(); }
    }

    bool ComponentMode() const noexcept
    {
        return m_pWorkspace->elementMode == map_element_mode_t::VERTICES || m_pWorkspace->elementMode == map_element_mode_t::EDGES ||
               m_pWorkspace->elementMode == map_element_mode_t::FACES;
    }

    map_component_transform_t ComponentKind() const noexcept
    {
        return m_kind == numeric_edit_t::TRANSLATE ? map_component_transform_t::TRANSLATE :
               m_kind == numeric_edit_t::ROTATE ? map_component_transform_t::ROTATE : map_component_transform_t::SCALE;
    }

    void Refresh()
    {
        const bool staged = m_kind == numeric_edit_t::BOX && MapWorkspace_HasBlockPreview( m_pWorkspace );
        const auto primitive = staged ? m_pWorkspace->editPreview.primitive : MapWorkspace_PrimitiveDefaults( m_pWorkspace, {} );
        const bool quad = m_kind == numeric_edit_t::BOX && primitive.kind == map_primitive_kind_t::QUAD;
        const bool components = m_kind != numeric_edit_t::BOX && ComponentMode();
        const bool enabled = m_kind == numeric_edit_t::BOX ? m_pWorkspace->pDocument != nullptr && !m_pWorkspace->pDocument->bReadOnly :
                             components ? MapToolGestures_CanTransformComponents( m_pWorkspace, ComponentKind() ) :
                             m_kind == numeric_edit_t::TRANSLATE ? MapWorkspace_CanMoveSelection( m_pWorkspace ) : MapWorkspace_CanTurnSelection( m_pWorkspace );
        const f64 step = m_kind == numeric_edit_t::SCALE ? 0.1 : m_kind == numeric_edit_t::ROTATE ? std::max( m_pWorkspace->angleSnap, 1.0 ) : m_pWorkspace->gridSize;
        for ( int axis = 0; axis < 3; ++axis ) {
            if ( m_kind == numeric_edit_t::BOX ) {
                const QSignalBlocker blocker( m_values[axis] );
                const bool normal = quad && static_cast<u32>( axis ) == primitive.axis;
                m_values[axis]->setMinimum( normal ? 0.0 : 0.001 );
                m_values[axis]->setEnabled( !normal );
                if ( normal ) { m_values[axis]->setValue( 0.0 ); }
                else if ( !staged && m_values[axis]->value() <= 0.001 ) { m_values[axis]->setValue( std::max( 64.0, m_pWorkspace->gridSize ) ); }
                m_values[axis]->setToolTip( normal ? QStringLiteral( "A Quad has no thickness on its plane normal." ) : QString() );
            }
            m_values[axis]->setSingleStep( step );
            if ( m_auxiliary[axis] != nullptr ) { m_auxiliary[axis]->setSingleStep( step ); }
            if ( staged ) {
                const auto &box = m_pWorkspace->editPreview.bounds.box;
                const f64 minimum = axis == 0 ? box.minimum.x : axis == 1 ? box.minimum.y : box.minimum.z;
                const f64 maximum = axis == 0 ? box.maximum.x : axis == 1 ? box.maximum.y : box.maximum.z;
                const QSignalBlocker valueBlocker( m_values[axis] ), centerBlocker( m_auxiliary[axis] );
                m_values[axis]->setValue( maximum - minimum );
                m_auxiliary[axis]->setValue( minimum * 0.5 + maximum * 0.5 );
            }
        }
        if ( m_kind == numeric_edit_t::BOX ) { m_pApply->setText( staged ? QStringLiteral( "Create Staged Primitive" ) : quad ? QStringLiteral( "Create Quad" ) : QStringLiteral( "Create Brush" ) ); }
        if ( m_kind != numeric_edit_t::BOX ) {
            const QString what = components ? QStringLiteral( "Components" ) : QStringLiteral( "Selection" );
            m_pApply->setText( ( m_kind == numeric_edit_t::TRANSLATE ? QStringLiteral( "Move " ) : m_kind == numeric_edit_t::ROTATE ? QStringLiteral( "Rotate " ) : QStringLiteral( "Scale " ) ) + what );
        }
        m_pApply->setEnabled( enabled );
        if ( m_pClone != nullptr ) { m_pClone->setEnabled( enabled && !components ); } // Components are moved, never cloned.
        m_pFeedback->setText( enabled ? components ? QStringLiteral( "Moves the selected components; the rest of the object stays. One edit = one undo step." ) :
                                                     QStringLiteral( "One edit = one undo step." ) :
                             m_kind == numeric_edit_t::BOX ? QStringLiteral( "The map is read-only." ) :
                             components ? ( m_kind != numeric_edit_t::TRANSLATE && m_pWorkspace->elementMode == map_element_mode_t::FACES && MapWorkspace_HasBrushFace( m_pWorkspace ) ?
                                 QStringLiteral( "A brush face only moves. Convert the brush to a mesh to rotate or scale its faces." ) :
                                 QStringLiteral( "Select mesh vertices, edges or a face to transform them. Objects and Meshes modes transform whole objects." ) ) :
                             QStringLiteral( "Select supported objects to edit. Mixed or unsupported selections are left intact." ) );
        if ( staged ) {
            m_pFeedback->setText( m_pWorkspace->editPreview.status == map_status_t::OK ?
                m_pWorkspace->editPreview.bBlockCommitFailed ? QStringLiteral( "Creation failed. The preview is retained; Create retries the same geometry." ) :
                QStringLiteral( "Center and Size adjust the shared preview. Create commits one undo step." ) :
                QStringLiteral( "Preview update failed: %1. Adjust a value to retry; the last valid geometry is retained." ).arg( QString::fromLatin1( MapDocument_StatusName( m_pWorkspace->editPreview.status ) ) ) );
        }
    }

    map_bounds_t BlockBounds() const
    {
        const math::vec3d_t size{ m_values[0]->value(), m_values[1]->value(), m_values[2]->value() };
        const math::vec3d_t center{ m_auxiliary[0]->value(), m_auxiliary[1]->value(), m_auxiliary[2]->value() };
        map_bounds_t bounds{};
        MapBounds_AddPoint( bounds, { center.x - size.x * 0.5, center.y - size.y * 0.5, center.z - size.z * 0.5 } );
        MapBounds_AddPoint( bounds, { center.x + size.x * 0.5, center.y + size.y * 0.5, center.z + size.z * 0.5 } );
        return bounds;
    }
    void UpdateBlockBounds( int axis, bool size )
    {
        if ( !MapWorkspace_HasBlockPreview( m_pWorkspace ) ) { return; }
        auto bounds = m_pWorkspace->editPreview.bounds;
        auto &minimum = axis == 0 ? bounds.box.minimum.x : axis == 1 ? bounds.box.minimum.y : bounds.box.minimum.z;
        auto &maximum = axis == 0 ? bounds.box.maximum.x : axis == 1 ? bounds.box.maximum.y : bounds.box.maximum.z;
        // A displayed spinbox can round a free drag. Editing another axis
        // must never quantize that retained geometry as a side effect.
        const f64 center = size ? minimum * 0.5 + maximum * 0.5 : m_auxiliary[axis]->value();
        const f64 half = size ? m_values[axis]->value() * 0.5 : ( maximum - minimum ) * 0.5;
        minimum = center - half; maximum = center + half;
        ( void )MapWorkspace_SetBlockPreviewBounds( m_pWorkspace, bounds );
    }

    void Apply( bool clone )
    {
        const math::vec3d_t values{ m_values[0]->value(), m_values[1]->value(), m_values[2]->value() };
        // Component selections share their parent's object selection: they
        // go through the component transform, never a root edit.
        if ( m_kind != numeric_edit_t::BOX && ComponentMode() ) {
            const auto where = m_pPivot == nullptr ? map_component_pivot_t::CENTER : static_cast<map_component_pivot_t>( m_pPivot->currentIndex() );
            const bool moved = !clone && MapToolGestures_TransformComponents( m_pWorkspace, ComponentKind(), values, where );
            Refresh();
            if ( moved ) { m_pFeedback->setText( QStringLiteral( "Applied. Undo restores the previous geometry." ) ); }
            return;
        }
        bool success = false;
        if ( m_kind == numeric_edit_t::BOX ) {
            success = MapWorkspace_HasBlockPreview( m_pWorkspace ) ? MapWorkspace_CommitBlockPreview( m_pWorkspace ) :
                MapWorkspace_CreatePrimitive( m_pWorkspace, BlockBounds() );
        } else if ( m_kind == numeric_edit_t::TRANSLATE ) {
            success = MapWorkspace_TranslateSelection( m_pWorkspace, values, clone );
        } else {
            const map_bounds_t bounds = SelectedBounds( *m_pWorkspace );
            math::vec3d_t pivot = m_pPivot->currentIndex() == 0 ? MapBounds_Center( bounds ) : math::vec3d_t{};
            if ( m_pPivot->currentIndex() == 2 && !MapToolGestures_Pivot( m_pWorkspace, &pivot ) ) { pivot = MapBounds_Center( bounds ); }
            success = m_kind == numeric_edit_t::ROTATE ? MapWorkspace_RotateSelection( m_pWorkspace, values, pivot ) : MapWorkspace_ScaleSelection( m_pWorkspace, values, pivot );
        }
        if ( !success && m_kind == numeric_edit_t::BOX && MapWorkspace_HasBlockPreview( m_pWorkspace ) ) { Refresh(); }
        else { m_pFeedback->setText( success ? QStringLiteral( "Applied. Undo restores the previous geometry." ) : QStringLiteral( "Nothing changed. Check the selection and the values." ) ); }
    }

    map_workspace_t *m_pWorkspace{};
    numeric_edit_t m_kind{};
    bool m_subscribed{};
    QDoubleSpinBox *m_values[3]{};
    QDoubleSpinBox *m_auxiliary[3]{};
    QComboBox *m_pPivot{};
    QPushButton *m_pApply{};
    QPushButton *m_pClone{};
    QLabel *m_pFeedback{};
};

// Authored mesh face operations. The selector holds persistent IDs;
// settings controls and command buttons share the registry with menus/keys.
class mesh_face_controls_t final : public QWidget {
public:
    mesh_face_controls_t( QWidget *parent, map_workspace_t *ws ) : QWidget( parent ), m_ws( ws )
    {
        setObjectName( QStringLiteral( "MapMeshFaceControls" ) );
        auto *layout = new QVBoxLayout( this ); layout->setContentsMargins( 6, 5, 6, 6 ); layout->setSpacing( 5 );
        m_identity = new QLabel( this ); m_identity->setObjectName( QStringLiteral( "MapMeshFaceIdentity" ) ); m_identity->setWordWrap( true ); layout->addWidget( m_identity );
        m_faces = new QComboBox( this ); m_faces->setObjectName( QStringLiteral( "MapMeshFaceSelector" ) );
        m_faces->setToolTip( QStringLiteral( "Choose one authored face here, or Shift-click faces in a view to select a region." ) );
        QObject::connect( m_faces, &QComboBox::currentIndexChanged, this, [this]( int index ) {
            const u64 id = m_faces->itemData( index ).toULongLong();
            if ( m_root != 0 && id != 0 ) { MapWorkspace_SelectMeshFace( m_ws, m_root, id ); }
            else { MapWorkspace_ClearMeshFace( m_ws ); }
        } );
        layout->addWidget( m_faces );
        auto *form = new QFormLayout();
        for ( const auto &[path, label] : { std::pair{ "editor.map.mesh_extrude_distance", "Extrude distance" }, std::pair{ "editor.map.mesh_inset_distance", "Corner inset distance" },
                                            std::pair{ "editor.map.mesh_inset_width", "Inset width" }, std::pair{ "editor.map.mesh_slice_u", "Quad Slice U cells" }, std::pair{ "editor.map.mesh_slice_v", "Quad Slice V cells" } } ) {
            if ( auto *control = gui::EditorSettingControl_Create( this, &ws->pGui->settings, path ) ) { form->addRow( QString::fromLatin1( label ), control ); }
        }
        layout->addLayout( form );
        for ( const auto &[id, button] : { std::pair{ "map.mesh.extrude", &m_extrude }, std::pair{ "map.mesh.inset", &m_inset }, std::pair{ "map.mesh.inset_width", &m_insetWidth }, std::pair{ "map.mesh.quad_slice", &m_slice } } ) {
            const auto *command = EditorCommands_Find( &ws->pGui->commands, StringView_FromCString( id ) );
            *button = new QPushButton( command != nullptr ? QString::fromUtf8( command->pLabel ) : QString::fromLatin1( id ), this );
            ( *button )->setObjectName( QString::fromLatin1( id ) );
            if ( command != nullptr ) { ( *button )->setIcon( gui::EditorStyle_Icon( ws->pGui->style, command->pIcon ) ); ( *button )->setToolTip( QString::fromUtf8( command->pDescription ) ); }
            QObject::connect( *button, &QPushButton::clicked, this, [this, id]() {
                const auto result = EditorCommands_Execute( &m_ws->pGui->commands, StringView_FromCString( id ), {} );
                m_feedback->setText( result == command_result_t::OK ? QStringLiteral( "Applied. Retained source faces stay selected; Undo restores the previous geometry." ) :
                    QStringLiteral( "Face unchanged. Check the selection and distance; the result must remain valid geometry." ) );
            } );
            layout->addWidget( *button );
        }
        m_feedback = new QLabel( this ); m_feedback->setObjectName( QStringLiteral( "MapMeshFaceFeedback" ) ); m_feedback->setWordWrap( true ); m_feedback->setProperty( "muted", true ); layout->addWidget( m_feedback );
        RefreshState();
    }
    void RefreshState()
    {
        const bool active = MapWorkspace_HasMeshFace( m_ws );
        const QSignalBlocker blocker( m_faces );
        m_faces->clear(); m_faces->addItem( QStringLiteral( "Choose a mesh face..." ), QVariant::fromValue( qulonglong{ 0 } ) ); m_root = 0;
        if ( m_ws->selection.ids.nCount == 1 ) {
            const u64 root = m_ws->selection.ids.pData[0];
            const auto *object = MapWireframe_FindObject( m_ws->wire, root );
            if ( object != nullptr && object->kind == map_wire_kind_t::MESH && MapWorkspace_IsVisible( m_ws, *object ) ) {
                m_root = root;
                for ( usize i = 0; i < m_ws->wire.faces.nCount; ++i ) {
                    const auto &face = m_ws->wire.faces.pData[i];
                    if ( face.id != root || face.faceId == 0 ) { continue; }
                    m_faces->addItem( QStringLiteral( "Face #%1 · %2 corners" ).arg( face.faceId ).arg( face.nIndices ), QVariant::fromValue( static_cast<qulonglong>( face.faceId ) ) );
                    if ( active && face.faceId == m_ws->selectedMeshFaceId ) { m_faces->setCurrentIndex( m_faces->count() - 1 ); }
                }
            }
        }
        m_faces->setEnabled( m_root != 0 );
        m_identity->setText( active ? QStringLiteral( "Mesh #%1 · %2 selected · Active face #%3" ).arg( m_ws->selectedMeshFaceObject ).arg( MapWorkspace_MeshFaces( m_ws ).nCount ).arg( m_ws->selectedMeshFaceId ) : QStringLiteral( "Select mesh faces in Faces mode." ) );
        m_extrude->setEnabled( ( EditorCommands_State( &m_ws->pGui->commands, StringView_FromCString( "map.mesh.extrude" ) ) & COMMAND_STATE_ENABLED ) != 0 );
        m_inset->setEnabled( ( EditorCommands_State( &m_ws->pGui->commands, StringView_FromCString( "map.mesh.inset" ) ) & COMMAND_STATE_ENABLED ) != 0 );
        m_insetWidth->setEnabled( ( EditorCommands_State( &m_ws->pGui->commands, StringView_FromCString( "map.mesh.inset_width" ) ) & COMMAND_STATE_ENABLED ) != 0 );
        m_slice->setEnabled( ( EditorCommands_State( &m_ws->pGui->commands, StringView_FromCString( "map.mesh.quad_slice" ) ) & COMMAND_STATE_ENABLED ) != 0 );
        m_feedback->setText( QStringLiteral( "Extrude raises one face or an edge-connected coplanar region, adding walls only around its perimeter. Corner Inset moves corners radially. Inset Face (Width) makes a measured border on one planar convex face. Both require exactly one face. Quad Slice operates on the active four-corner face; U follows corners 0–1 and V follows 0–3. Shared boundary vertices connect neighboring faces." ) );
    }
private:
    map_workspace_t *m_ws{};
    u64 m_root{};
    QLabel *m_identity{}, *m_feedback{};
    QComboBox *m_faces{};
    QPushButton *m_extrude{}, *m_inset{}, *m_insetWidth{}, *m_slice{};
};

// Read the authored surface on each notification. No geometry pointer survives
// publication/Undo, and creation defaults never masquerade as selected-face UVs.
class face_texture_state_t final : public QWidget {
public:
    face_texture_state_t( QWidget *parent, map_workspace_t *workspace ) : QWidget( parent ), m_workspace( workspace )
    {
        setObjectName( QStringLiteral( "MapFaceTextureState" ) );
        auto *layout = new QVBoxLayout( this ); layout->setContentsMargins( 6, 5, 6, 6 ); layout->setSpacing( 5 );
        m_identity = new QLabel( this ); m_identity->setObjectName( QStringLiteral( "MapFaceTextureIdentity" ) );
        m_identity->setWordWrap( true ); layout->addWidget( m_identity );
        m_material = Field( "MapFaceTextureMaterial", "Selected face material" );
        m_material->setPlaceholderText( QStringLiteral( "No assigned material" ) ); layout->addWidget( m_material );
        auto *grid = new QGridLayout(); grid->setContentsMargins( 0, 0, 0, 0 ); grid->setHorizontalSpacing( 4 ); grid->setVerticalSpacing( 4 );
        grid->addWidget( new QLabel( QStringLiteral( "U" ), this ), 0, 1 ); grid->addWidget( new QLabel( QStringLiteral( "V" ), this ), 0, 2 );
        int rowIndex = 1;
        for ( const auto &row : { row_t{ "Repeat size", "MapFaceTextureSize", "World units represented by one full texture repeat", " u/repeat" },
                                 row_t{ "Shift", "MapFaceTextureShift", "Offset in UV repeats after projection", " repeats" },
                                 row_t{ "UV bounds", "MapFaceTextureRange", "Minimum and maximum authored corner UV", "" } } ) {
            auto *label = new QLabel( QString::fromLatin1( row.label ), this ); label->setToolTip( QString::fromLatin1( row.help ) );
            grid->addWidget( label, rowIndex, 0 );
            row_controls_t controls{}; controls.label = label; controls.suffix = QString::fromLatin1( row.suffix );
            for ( int i = 0; i < 2; ++i ) {
                const QByteArray name = QByteArray( row.name ) + ( i == 0 ? "U" : "V" );
                controls.fields[i] = Field( name.constData(), row.help );
                controls.fields[i]->setAccessibleName( QStringLiteral( "%1 %2" ).arg( QString::fromLatin1( row.label ), i == 0 ? QStringLiteral( "U" ) : QStringLiteral( "V" ) ) );
                grid->addWidget( controls.fields[i], rowIndex, i + 1 );
            }
            m_rows[static_cast<usize>( rowIndex - 1 )] = controls; ++rowIndex;
        }
        m_rotationLabel = new QLabel( QStringLiteral( "Rotate" ), this );
        m_rotation = Field( "MapFaceTextureRotation", "Selected face rotation in degrees" );
        grid->addWidget( m_rotationLabel, 4, 0 ); grid->addWidget( m_rotation, 4, 1, 1, 2 );
        grid->setColumnStretch( 1, 1 ); grid->setColumnStretch( 2, 1 ); layout->addLayout( grid );
        m_basis = new QLabel( this ); m_basis->setObjectName( QStringLiteral( "MapFaceTextureBasis" ) );
        m_basis->setWordWrap( true ); m_basis->setTextInteractionFlags( Qt::TextSelectableByMouse );
        m_basisSection = gui::EditorSection_Create( this, QStringLiteral( "Projection basis" ), m_basis, false ); layout->addWidget( m_basisSection );
        m_note = new QLabel( QStringLiteral( "This state is read only; change scale, shift, rotation and alignment in the Texture section below." ), this );
        m_note->setObjectName( QStringLiteral( "MapFaceTextureStateNote" ) ); m_note->setWordWrap( true ); m_note->setProperty( "muted", true );
        layout->addWidget( m_note ); RefreshState();
    }
    void RefreshState()
    {
        geometry::geometry_brush_side_attributes_t attributes{};
        bool brush = false;
        const auto *document = m_workspace->pDocument;
        if ( document != nullptr && MapWorkspace_HasBrushFace( m_workspace ) ) {
            const auto *solid = geometry::GeometryDocument_FindBrush( &document->geometry, { m_workspace->selectedBrushFaceObject } );
            const auto *store = geometry::GeometryDocument_FindBrushAttributes( &document->geometry, { m_workspace->selectedBrushFaceObject } );
            if ( solid != nullptr && store != nullptr ) {
                for ( usize i = 0; i < solid->sides.nCount; ++i ) {
                    const auto &side = solid->sides.pData[i];
                    if ( side.sourceId.value == m_workspace->selectedBrushFaceSide && side.iAttributeIndex < store->records.nCount ) {
                        attributes = store->records.pData[side.iAttributeIndex]; brush = true; break;
                    }
                }
            }
        }
        math::vec2d_t minimum{}, maximum{}; u32 corners = 0; u64 material = 0;
        bool mesh = false;
        if ( !brush && document != nullptr && MapWorkspace_HasMeshFace( m_workspace ) ) {
            const auto *source = geometry::GeometryDocument_FindMesh( &document->geometry, { m_workspace->selectedMeshFaceObject } );
            geometry::geometry_mesh_face_handle_t handle{};
            if ( source != nullptr && geometry::MeshSource_TryFindFace( source, { m_workspace->selectedMeshFaceId }, &handle ) ) {
                const auto *face = geometry::EditableMesh_GetFace( &source->mesh, handle );
                const auto *loop = face != nullptr ? geometry::EditableMesh_GetLoop( &source->mesh, face->hOuterLoop ) : nullptr;
                if ( loop != nullptr && loop->cHalfEdges != 0 && loop->cHalfEdges <= geometry::kMeshSourceCornersPerFaceMax ) {
                    auto edge = loop->hFirstHalfEdge;
                    for ( u32 i = 0; i < loop->cHalfEdges; ++i ) {
                        const auto *record = geometry::EditableMesh_GetHalfEdge( &source->mesh, edge ); if ( record == nullptr ) { break; }
                        const auto uv = geometry::MeshAttributeStore_GetCorner( &source->attributes, edge ).uv0;
                        if ( corners == 0 ) { minimum = uv; maximum = uv; }
                        else { minimum.x = std::min( minimum.x, uv.x ); minimum.y = std::min( minimum.y, uv.y ); maximum.x = std::max( maximum.x, uv.x ); maximum.y = std::max( maximum.y, uv.y ); }
                        ++corners; edge = record->hNext;
                    }
                    mesh = corners == loop->cHalfEdges;
                    if ( mesh ) { material = geometry::MeshAttributeStore_GetFace( &source->attributes, handle ).material.value; }
                }
            }
        }
        const bool active = brush || mesh;
        m_identity->setText( brush ? QStringLiteral( "Brush #%1 · Face #%2" ).arg( m_workspace->selectedBrushFaceObject ).arg( m_workspace->selectedBrushFaceSide ) :
            mesh ? QStringLiteral( "Mesh #%1 · Face #%2 · %3 UV corners" ).arg( m_workspace->selectedMeshFaceObject ).arg( m_workspace->selectedMeshFaceId ).arg( corners ) : QStringLiteral( "Select one face to inspect its texture state." ) );
        const QString path = active ? FromView( MapMaterials_Path( &document->materials, brush ? attributes.material.value : material ) ) : QString{};
        m_material->setText( path ); m_material->setToolTip( path ); m_material->setEnabled( active );
        ShowRow( 0, !mesh, active ); ShowRow( 1, !mesh, active ); ShowRow( 2, mesh, mesh );
        m_rotationLabel->setVisible( !mesh ); m_rotation->setVisible( !mesh ); m_rotation->setEnabled( brush );
        m_basisSection->setVisible( brush );
        if ( brush ) {
            SetPair( 0, attributes.uvProjection.worldUnitsPerUv ); SetPair( 1, attributes.uvProjection.offset );
            m_rotation->setText( Number( math::Scalar_RadiansToDegrees( attributes.uvProjection.rotationRadians ) ) + QStringLiteral( "°" ) );
            const auto vector = []( const math::vec3d_t &v ) { return QStringLiteral( "%1, %2, %3" ).arg( Number( v.x ), Number( v.y ), Number( v.z ) ); };
            m_basis->setText( QStringLiteral( "Origin: %1\nU axis: %2\nV axis: %3\nNormal: %4" ).arg( vector( attributes.uvProjection.origin ), vector( attributes.uvProjection.uAxis ), vector( attributes.uvProjection.vAxis ), vector( attributes.uvProjection.normal ) ) );
        } else {
            for ( int row = 0; row < 2; ++row ) { for ( auto *field : m_rows[row].fields ) { field->clear(); } }
            m_rotation->clear(); m_basis->clear();
        }
        for ( int i = 0; i < 2; ++i ) {
            m_rows[2].fields[i]->setText( mesh ? QStringLiteral( "%1 … %2" ).arg( Number( i == 0 ? minimum.x : minimum.y ), Number( i == 0 ? maximum.x : maximum.y ) ) : QString{} );
        }
    }
private:
    static QString Number( f64 value ) { return QString::number( value == 0.0 ? 0.0 : value, 'g', 12 ); }
    QLineEdit *Field( const char *name, const char *help ) {
        auto *field = new QLineEdit( this ); field->setObjectName( QString::fromLatin1( name ) ); field->setReadOnly( true );
        field->setAccessibleName( QString::fromLatin1( help ) ); field->setToolTip( QString::fromLatin1( help ) ); field->setMinimumWidth( 30 ); return field;
    }
    void ShowRow( int index, bool visible, bool enabled ) {
        m_rows[index].label->setVisible( visible ); for ( auto *field : m_rows[index].fields ) { field->setVisible( visible ); field->setEnabled( enabled ); }
    }
    void SetPair( int index, math::vec2d_t values ) {
        m_rows[index].fields[0]->setText( Number( values.x ) + m_rows[index].suffix ); m_rows[index].fields[1]->setText( Number( values.y ) + m_rows[index].suffix );
    }
    struct row_t { const char *label, *name, *help, *suffix; };
    struct row_controls_t { QLabel *label{}; QLineEdit *fields[2]{}; QString suffix{}; };
    map_workspace_t *m_workspace{};
    QLabel *m_identity{}, *m_rotationLabel{}, *m_basis{}, *m_note{};
    QLineEdit *m_material{}, *m_rotation{};
    QWidget *m_basisSection{};
    std::array<row_controls_t, 3> m_rows{};
};

class face_controls_t final : public QWidget {
public:
    face_controls_t( QWidget *pParent, map_workspace_t *pWorkspace ) : QWidget( pParent ), m_pWorkspace( pWorkspace )
    {
        setObjectName( QStringLiteral( "MapBrushFaceControls" ) );
        auto *pLayout = new QVBoxLayout( this );
        pLayout->setContentsMargins( 6, 5, 6, 6 );
        pLayout->setSpacing( 5 );
        m_pFace = new QLabel( this );
        m_pFace->setObjectName( QStringLiteral( "MapBrushFaceIdentity" ) );
        m_pFace->setWordWrap( true );
        pLayout->addWidget( m_pFace );
        m_pFaces = new QComboBox( this );
        m_pFaces->setObjectName( QStringLiteral( "MapBrushFaceSelector" ) );
        m_pFaces->setToolTip( QStringLiteral( "Choose a face of the single selected brush. This also works after selecting a brush in a 2D view." ) );
        QObject::connect( m_pFaces, &QComboBox::currentIndexChanged, this, [this]( int index ) {
            const u64 side = m_pFaces->itemData( index ).toULongLong();
            if ( m_comboObject != 0u && side != 0u ) { MapWorkspace_SelectBrushFace( m_pWorkspace, m_comboObject, side ); }
            else { MapWorkspace_ClearBrushFace( m_pWorkspace ); }
        } );
        pLayout->addWidget( m_pFaces );
        auto *pDistance = new QHBoxLayout();
        pDistance->addWidget( new QLabel( QStringLiteral( "Distance" ), this ) );
        m_pDistance = new QDoubleSpinBox( this );
        m_pDistance->setObjectName( QStringLiteral( "MapBrushFaceDistance" ) );
        m_pDistance->setDecimals( 3 );
        m_pDistance->setRange( -1000000.0, 1000000.0 );
        m_pDistance->setSingleStep( pWorkspace->gridSize );
        m_pDistance->setValue( pWorkspace->gridSize );
        m_pDistance->setSuffix( QStringLiteral( " u" ) );
        m_pDistance->setKeyboardTracking( false );
        m_pDistance->setToolTip( QStringLiteral( "Positive extends the brush along this face's outward normal. Negative contracts it. The result must remain a closed convex brush." ) );
        pDistance->addWidget( m_pDistance, 1 );
        pLayout->addLayout( pDistance );
        m_pPushPull = new QToolButton( this );
        m_pPushPull->setAccessibleName( QStringLiteral( "Push / Pull Face" ) );
        m_pPushPull->setToolTip( QStringLiteral( "Push / Pull Face\nApply the distance along this face's outward normal. In Faces mode, pick a brush face outline in 2D or a face in 3D, then drag its normal handle for the same grid-snapped operation. If the normal points into the 2D view, use another pane or this distance. The Translate tool also drags a picked face with a gizmo. One Undo restores the brush." ) );
        m_pPushPull->setIconSize( QSize( 20, 20 ) ); m_pPushPull->setFixedSize( 30, 30 );
        m_pPushPull->setToolButtonStyle( Qt::ToolButtonIconOnly ); m_pPushPull->setProperty( "compactIcon", true );
        m_pPushPull->setObjectName( QStringLiteral( "MapBrushFacePushPull" ) );
        m_pPushPull->setIcon( gui::EditorStyle_Icon( pWorkspace->pGui->style, "mesh-extrude" ) );
        QObject::connect( m_pPushPull, &QToolButton::clicked, this, [this]() {
            const bool success = MapWorkspace_PushPullFace( m_pWorkspace, m_pDistance->value() );
            m_pFeedback->setText( success ? QStringLiteral( "Face moved. Undo restores the brush." ) : QStringLiteral( "Face unchanged. The requested distance must leave a valid convex brush." ) );
        } );
        pDistance->addWidget( m_pPushPull );
        auto *pMaterial = new QHBoxLayout();
        m_pMaterial = new QLineEdit( this );
        m_pMaterial->setObjectName( QStringLiteral( "MapBrushFaceActiveMaterial" ) );
        m_pMaterial->setReadOnly( true );
        m_pMaterial->setPlaceholderText( QStringLiteral( "No active material" ) );
        pMaterial->addWidget( m_pMaterial, 1 );
        m_pBrowse = new QToolButton( this );
        auto *pBrowse = m_pBrowse;
        pBrowse->setObjectName( QStringLiteral( "MapBrushFaceBrowseMaterial" ) );
        pBrowse->setIcon( gui::EditorStyle_Icon( pWorkspace->pGui->style, "asset-folder" ) );
        pBrowse->setAccessibleName( QStringLiteral( "Choose Active Material" ) );
        pBrowse->setToolTip( QStringLiteral( "Choose the active material" ) );
        pBrowse->setIconSize( QSize( 20, 20 ) ); pBrowse->setFixedSize( 30, 30 ); pBrowse->setProperty( "compactIcon", true );
        pBrowse->setEnabled( EditorCommands_Find( &pWorkspace->pGui->commands, StringView_FromCString( "assets.browse_materials" ) ) != nullptr );
        QObject::connect( pBrowse, &QToolButton::clicked, this, [this]() {
            ( void )EditorCommands_Execute( &m_pWorkspace->pGui->commands, StringView_FromCString( "assets.browse_materials" ), {} );
        } );
        pMaterial->addWidget( pBrowse );
        pLayout->addLayout( pMaterial );
        m_pApplyMaterial = new QToolButton( this );
        m_pApplyMaterial->setAccessibleName( QStringLiteral( "Apply Active Material to Face" ) );
        m_pApplyMaterial->setToolTip( QStringLiteral( "Apply Active Material to Face\nChanges only this selected brush face. Other sides retain their material and UVs. One Undo restores its previous material." ) );
        m_pApplyMaterial->setIconSize( QSize( 20, 20 ) ); m_pApplyMaterial->setFixedSize( 30, 30 );
        m_pApplyMaterial->setToolButtonStyle( Qt::ToolButtonIconOnly ); m_pApplyMaterial->setProperty( "compactIcon", true );
        m_pApplyMaterial->setObjectName( QStringLiteral( "MapBrushFaceApplyMaterial" ) );
        m_pApplyMaterial->setIcon( gui::EditorStyle_Icon( pWorkspace->pGui->style, "tool-apply-material" ) );
        QObject::connect( m_pApplyMaterial, &QToolButton::clicked, this, [this]() {
            const bool success = MapWorkspace_ApplyFaceMaterial( m_pWorkspace );
            m_pFeedback->setText( success ? QStringLiteral( "Material applied to this face." ) : QStringLiteral( "Material unchanged. Select a brush face first." ) );
        } );
        pMaterial->addWidget( m_pApplyMaterial );
        m_pFeedback = new QLabel( this );
        m_pFeedback->setObjectName( QStringLiteral( "MapBrushFaceFeedback" ) );
        m_pFeedback->setWordWrap( true );
        m_pFeedback->setProperty( "muted", true );
        pLayout->addWidget( m_pFeedback );
        ( void )EditorSettings_AddListener( &pWorkspace->pGui->settings, &face_controls_t::OnSettingsChanged, this );
        RefreshIcons(); Refresh();
    }

    ~face_controls_t() override
    {
        EditorSettings_RemoveListener( &m_pWorkspace->pGui->settings, &face_controls_t::OnSettingsChanged, this );
    }
    void RefreshState() { Refresh(); }
    void RefreshIcons() {
        m_pPushPull->setIcon( gui::EditorStyle_Icon( m_pWorkspace->pGui->style, "mesh-extrude" ) );
        m_pApplyMaterial->setIcon( gui::EditorStyle_Icon( m_pWorkspace->pGui->style, "tool-apply-material" ) );
        m_pBrowse->setIcon( gui::EditorStyle_Icon( m_pWorkspace->pGui->style, "asset-folder" ) );
        const auto icon = gui::EditorStyle_Icon( m_pWorkspace->pGui->style, "select-faces" );
        for ( int i = 1; i < m_pFaces->count(); ++i ) { m_pFaces->setItemIcon( i, icon ); }
        // Parent/application stylesheet polishing can reset QWidget's fixed
        // minimum after construction. Keep the geometry in the button's own
        // stylesheet: 26px content + 1px padding/border per side = 30px. Only
        // geometry is local; theme colors, hover and checked states still
        // come from the shared editor style.
        for ( auto *button : { m_pPushPull, m_pApplyMaterial, m_pBrowse } ) {
            button->setStyleSheet( QStringLiteral( "QToolButton { min-width: 26px; max-width: 26px; min-height: 26px; max-height: 26px; padding: 1px; border-width: 1px; }" ) );
            button->setFixedSize( 30, 30 );
        }
    }

private:
    static void OnSettingsChanged( void *pContext, string_view_t path ) noexcept
    {
        if ( path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( "editor.map.default_material" ) ) ) {
            static_cast<face_controls_t *>( pContext )->Refresh();
        }
    }
    void Refresh()
    {
        const bool active = MapWorkspace_HasBrushFace( m_pWorkspace );
        const bool writable = m_pWorkspace->pDocument != nullptr && !m_pWorkspace->pDocument->bReadOnly;
        const QSignalBlocker blocker( m_pFaces );
        m_pFaces->clear();
        m_pFaces->addItem( QStringLiteral( "Choose a brush face..." ), QVariant::fromValue( qulonglong{ 0u } ) );
        m_comboObject = 0u;
        if ( m_pWorkspace->pDocument != nullptr && m_pWorkspace->selection.ids.nCount == 1u ) {
            const u64 object = m_pWorkspace->selection.ids.pData[0];
            const auto *pBrush = geometry::GeometryDocument_FindBrush( &m_pWorkspace->pDocument->geometry, { object } );
            const auto *pWire = MapWireframe_FindObject( m_pWorkspace->wire, object );
            if ( pBrush != nullptr && pWire != nullptr && MapWorkspace_IsVisible( m_pWorkspace, *pWire ) ) {
                m_comboObject = object;
                for ( usize i = 0u; i < pBrush->sides.nCount; ++i ) {
                    const auto &side = pBrush->sides.pData[i];
                    const auto n = side.plane.normal;
                    const f64 ax = std::abs( n.x ), ay = std::abs( n.y ), az = std::abs( n.z );
                    const char *pAxis = ax >= ay && ax >= az ? ( n.x >= 0.0 ? "+X" : "-X" ) : ay >= az ? ( n.y >= 0.0 ? "+Y" : "-Y" ) : ( n.z >= 0.0 ? "+Z" : "-Z" );
                    m_pFaces->addItem( gui::EditorStyle_Icon( m_pWorkspace->pGui->style, "select-faces" ), QStringLiteral( "Face %1 · %2 · #%3" ).arg( i + 1u ).arg( QString::fromLatin1( pAxis ) ).arg( side.sourceId.value ), QVariant::fromValue( static_cast<qulonglong>( side.sourceId.value ) ) );
                    if ( active && side.sourceId.value == m_pWorkspace->selectedBrushFaceSide ) { m_pFaces->setCurrentIndex( m_pFaces->count() - 1 ); }
                }
            }
        }
        m_pFaces->setEnabled( m_comboObject != 0u );
        m_pFace->setText( active ? QStringLiteral( "Brush #%1 · Face #%2" ).arg( m_pWorkspace->selectedBrushFaceObject ).arg( m_pWorkspace->selectedBrushFaceSide ) :
                         QStringLiteral( "Select a brush face in Faces mode." ) );
        m_pPushPull->setEnabled( active && writable );
        m_pApplyMaterial->setEnabled( active && writable );
        m_pDistance->setSingleStep( m_pWorkspace->gridSize );
        const auto path = FromView( EditorSettings_Text( &m_pWorkspace->pGui->settings, "editor.map.default_material", {} ) );
        m_pMaterial->setText( path );
        m_pMaterial->setToolTip( path );
        m_pFeedback->setText( active ? QStringLiteral( "Drag the normal handle in 2D or 3D, or apply a distance here. A head-on normal needs another pane." ) : QStringLiteral( "Push/pull edits the existing convex brush; mesh topology extrusion is separate." ) );
    }
    map_workspace_t *m_pWorkspace{};
    QLabel *m_pFace{};
    QComboBox *m_pFaces{};
    u64 m_comboObject{};
    QDoubleSpinBox *m_pDistance{};
    QToolButton *m_pPushPull{};
    QToolButton *m_pApplyMaterial{}, *m_pBrowse{};
    QLineEdit *m_pMaterial{};
    QLabel *m_pFeedback{};
};

class clip_controls_t final : public QWidget {
public:
    clip_controls_t( QWidget *pParent, map_workspace_t *pWorkspace ) : QWidget( pParent ), m_pWorkspace( pWorkspace )
    {
        setObjectName( QStringLiteral( "MapBrushClipControls" ) );
        auto *pLayout = new QVBoxLayout( this );
        pLayout->setContentsMargins( 6, 5, 6, 6 );
        pLayout->setSpacing( 5 );
        // Hammer 5's Keep Mode: which side stays, one click each (the plane's
        // back, its front, or both halves), and whether mesh cuts are closed.
        auto *pKeep = new QHBoxLayout();
        pKeep->setSpacing( 4 );
        pKeep->addWidget( new QLabel( QStringLiteral( "Keep" ), this ) );
        static constexpr const char *kModes[]{ "back", "front", "both" };
        // A staged cut switches side live; nothing is cut until Enter.
        static constexpr const char *kTips[]{ "Keep the back side (−): the part behind the plane's normal. Switch any time before Enter (Shift+X cycles).",
                                              "Keep the front side (+): the part the normal points to. Switch any time before Enter (Shift+X cycles).",
                                              "Keep both: split into two pieces. Switch any time before Enter (Shift+X cycles)." };
        for ( int i = 0; i < 3; ++i ) {
            auto *pButton = new QToolButton( this );
            pButton->setObjectName( QStringLiteral( "MapClipKeep_%1" ).arg( QLatin1String( kModes[i] ) ) );
            pButton->setCheckable( true );
            pButton->setAutoRaise( true );
            pButton->setIcon( KeepIcon( i ) );
            pButton->setIconSize( QSize( 30, 20 ) );
            pButton->setToolTip( QString::fromUtf8( kTips[i] ) );
            QObject::connect( pButton, &QToolButton::clicked, this, [this, i]() { WriteChoice( "editor.map.clip_mode", kModes[i] ); RefreshState(); } );
            pKeep->addWidget( pButton );
            m_keep[i] = pButton;
        }
        pKeep->addStretch( 1 );
        pLayout->addLayout( pKeep );
        m_pCap = new QCheckBox( QStringLiteral( "Cap new surfaces" ), this );
        m_pCap->setObjectName( QStringLiteral( "MapClipCap" ) );
        m_pCap->setToolTip( QStringLiteral( "Close the cut of a clipped mesh with new faces. Brushes are always closed." ) );
        QObject::connect( m_pCap, &QCheckBox::toggled, this, [this]( bool checked ) {
            const auto *pSetting = EditorSettings_Find( &m_pWorkspace->pGui->settings, StringView_FromCString( "editor.map.clip_cap" ) );
            if ( pSetting == nullptr ) { return; }
            setting_value_t value{}; value.type = pSetting->type; value.bValue = checked ? CY_TRUE : CY_FALSE;
            ( void )EditorSettings_Write( &m_pWorkspace->pGui->settings, settings_scope_t::USER, *pSetting, value );
        } );
        pLayout->addWidget( m_pCap );
        m_pRule = new QLabel( this );
        m_pRule->setObjectName( QStringLiteral( "MapClipRetainRule" ) );
        m_pRule->setToolTip( QStringLiteral( "The normal points toward Front. For normal X=1, Y=0, Z=0 and d=-64, Back keeps X ≤ 64 and Front keeps X ≥ 64. Both retains the two halves." ) );
        pLayout->addWidget( m_pRule );
        m_pGuide = new QLabel( this );
        m_pGuide->setObjectName( QStringLiteral( "MapClipGuide" ) );
        m_pGuide->setWordWrap( true );
        m_pGuide->setTextInteractionFlags( Qt::TextSelectableByMouse );
        m_pGuide->setToolTip( QStringLiteral( "Staged endpoints in world coordinates. Drag either endpoint in a map view to adjust it. These coordinates are read-only." ) );
        pLayout->addWidget( m_pGuide );
        auto *pNormal = new QGridLayout();
        pNormal->setContentsMargins( 0, 0, 0, 0 );
        pNormal->setSpacing( 4 );
        pNormal->addWidget( new QLabel( QStringLiteral( "Normal" ), this ), 1, 0 );
        for ( int axis = 0; axis < 3; ++axis ) {
            const auto name = QString::fromLatin1( axis == 0 ? "X" : axis == 1 ? "Y" : "Z" );
            auto *pLabel = new QLabel( name, this );
            pLabel->setAlignment( Qt::AlignCenter );
            pNormal->addWidget( pLabel, 0, axis + 1 );
            m_normals[axis] = new QDoubleSpinBox( this );
            m_normals[axis]->setObjectName( QStringLiteral( "MapClipNormal%1" ).arg( name ) );
            m_normals[axis]->setDecimals( 5 );
            m_normals[axis]->setRange( -1000000.0, 1000000.0 );
            m_normals[axis]->setSingleStep( 0.1 );
            m_normals[axis]->setValue( axis == 0 ? 1.0 : 0.0 );
            m_normals[axis]->setMinimumWidth( 45 );
            m_normals[axis]->setKeyboardTracking( false );
            pNormal->addWidget( m_normals[axis], 1, axis + 1 );
        }
        pLayout->addLayout( pNormal );
        auto *pDistance = new QHBoxLayout();
        pDistance->addWidget( new QLabel( QStringLiteral( "Distance d" ), this ) );
        m_pDistance = new QDoubleSpinBox( this );
        m_pDistance->setObjectName( QStringLiteral( "MapClipPlaneDistance" ) );
        m_pDistance->setDecimals( 3 );
        m_pDistance->setRange( -1000000.0, 1000000.0 );
        m_pDistance->setSingleStep( pWorkspace->gridSize );
        m_pDistance->setKeyboardTracking( false );
        const auto bounds = SelectedBounds( *pWorkspace );
        m_pDistance->setValue( bounds.bHas ? -MapBounds_Center( bounds ).x : 0.0 );
        pDistance->addWidget( m_pDistance, 1 );
        pLayout->addLayout( pDistance );
        m_pApply = new QPushButton( QStringLiteral( "Clip Selection" ), this );
        m_pApply->setObjectName( QStringLiteral( "MapClipApply" ) );
        m_pApply->setIcon( gui::EditorStyle_Icon( pWorkspace->pGui->style, "tool-clip" ) );
        m_pApply->setToolTip( QStringLiteral( "Cut the selected brushes and meshes with this plane. New cut faces use the active material (meshes: the material of the faces they close). One undo restores the originals." ) );
        QObject::connect( m_pApply, &QPushButton::clicked, this, [this]() {
            const math::planed_t input{ { m_normals[0]->value(), m_normals[1]->value(), m_normals[2]->value() }, m_pDistance->value() };
            const auto mode = MapWorkspace_ClipMode( m_pWorkspace );
            math::planed_t normalized{};
            if ( !math::Planed_TryNormalize( input, 1e-8, &normalized ) ) {
                m_pFeedback->setText( QStringLiteral( "Enter a nonzero plane normal. Nothing changed." ) );
                return;
            }
            const bool success = MapWorkspace_ClipSelection( m_pWorkspace, normalized, mode );
            m_pFeedback->setText( success ? QStringLiteral( "Clipped. Undo restores the originals." ) : QStringLiteral( "Nothing changed. Check that the plane crosses the selection." ) );
        } );
        pLayout->addWidget( m_pApply );
        m_pFeedback = new QLabel( this );
        m_pFeedback->setWordWrap( true );
        m_pFeedback->setProperty( "muted", true );
        pLayout->addWidget( m_pFeedback );
        RefreshState();
    }

    void RefreshState()
    {
        const bool active = MapWorkspace_CanClipSelection( m_pWorkspace );
        m_pApply->setEnabled( active );
        m_pDistance->setSingleStep( m_pWorkspace->gridSize );
        const auto &preview = m_pWorkspace->editPreview;
        const bool hasGuide = preview.bActive && preview.bClip && preview.clipGuide.bHas && preview.clipGuide.extrusionAxis < 3u;
        m_pGuide->setVisible( hasGuide );
        if ( hasGuide ) {
            const auto &guide = preview.clipGuide;
            const auto point = []( math::vec3d_t value ) {
                return QStringLiteral( "X %1 · Y %2 · Z %3" )
                    .arg( QString::number( value.x, 'g', 7 ), QString::number( value.y, 'g', 7 ), QString::number( value.z, 'g', 7 ) );
            };
            m_pGuide->setText( QStringLiteral( "Extrude %1\nP1  %2\nP2  %3" )
                .arg( QChar( "XYZ"[guide.extrusionAxis] ) ).arg( point( guide.points[0] ), point( guide.points[1] ) ) );
        } else { m_pGuide->clear(); }
        if ( preview.bActive && preview.bClip ) {
            const auto &plane = preview.clipPlane;
            if ( !m_bHasDisplayedPlane || plane.normal.x != m_displayedPlane.normal.x || plane.normal.y != m_displayedPlane.normal.y ||
                 plane.normal.z != m_displayedPlane.normal.z || plane.d != m_displayedPlane.d ) {
                // Inspect a newly staged plane without replacing numeric
                // drafts on later mode changes or unrelated view notices.
                const double normal[]{ plane.normal.x, plane.normal.y, plane.normal.z };
                for ( int axis = 0; axis < 3; ++axis ) {
                    const QSignalBlocker blocker( m_normals[axis] );
                    m_normals[axis]->setValue( normal[axis] );
                }
                const QSignalBlocker blocker( m_pDistance );
                m_pDistance->setValue( plane.d );
                m_displayedPlane = plane; m_bHasDisplayedPlane = true;
            }
        } else { m_bHasDisplayedPlane = false; }
        const auto mode = MapWorkspace_ClipMode( m_pWorkspace );
        for ( int i = 0; i < 3; ++i ) {
            const QSignalBlocker blocker( m_keep[i] );
            m_keep[i]->setChecked( ( i == 0 && mode == map_brush_clip_mode_t::BACK ) || ( i == 1 && mode == map_brush_clip_mode_t::FRONT ) ||
                                   ( i == 2 && mode == map_brush_clip_mode_t::BOTH ) );
        }
        {
            const QSignalBlocker blocker( m_pCap );
            m_pCap->setChecked( EditorSettings_Bool( &m_pWorkspace->pGui->settings, "editor.map.clip_cap", CY_TRUE ) );
        }
        m_pRule->setText( mode == map_brush_clip_mode_t::BACK ? QStringLiteral( "Back: keep n · p + d ≤ 0" ) :
                         mode == map_brush_clip_mode_t::FRONT ? QStringLiteral( "Front: keep n · p + d ≥ 0" ) : QStringLiteral( "Both: retain both sides of the plane" ) );
        m_pFeedback->setText( active ? QStringLiteral( "Numeric plane clipping affects every selected brush and mesh." ) : QStringLiteral( "Select brushes or meshes to clip." ) );
    }
private:
    // A box split down the middle, the kept half (or both) filled green.
    static QIcon KeepIcon( int which )
    {
        QPixmap pixmap( 60, 40 );
        pixmap.fill( Qt::transparent );
        QPainter painter( &pixmap );
        painter.setRenderHint( QPainter::Antialiasing, true );
        const QRectF box( 6, 6, 48, 28 );
        const QColor kept( 63, 185, 80 );
        if ( which == 0 || which == 2 ) { painter.fillRect( QRectF( box.left(), box.top(), box.width() * 0.5, box.height() ), kept ); }
        if ( which == 1 || which == 2 ) { painter.fillRect( QRectF( box.center().x(), box.top(), box.width() * 0.5, box.height() ), kept ); }
        painter.setPen( QPen( QColor( 220, 220, 220 ), 2.5 ) );
        painter.drawRect( box );
        painter.setPen( QPen( QColor( 255, 210, 60 ), 3.0 ) );
        painter.drawLine( QPointF( box.center().x(), 2 ), QPointF( box.center().x(), 38 ) );
        return QIcon( pixmap );
    }
    void WriteChoice( const char *pPath, const char *pValue )
    {
        const auto *pSetting = EditorSettings_Find( &m_pWorkspace->pGui->settings, StringView_FromCString( pPath ) );
        if ( pSetting == nullptr ) { return; }
        setting_value_t value{}; value.type = pSetting->type; value.text = StringView_FromCString( pValue );
        ( void )EditorSettings_Write( &m_pWorkspace->pGui->settings, settings_scope_t::USER, *pSetting, value );
    }
    QToolButton *m_keep[3]{};
    QCheckBox *m_pCap{};
    map_workspace_t *m_pWorkspace{};
    QDoubleSpinBox *m_normals[3]{};
    QDoubleSpinBox *m_pDistance{};
    QPushButton *m_pApply{};
    QLabel *m_pRule{};
    QLabel *m_pGuide{};
    QLabel *m_pFeedback{};
    math::planed_t m_displayedPlane{};
    bool m_bHasDisplayedPlane{};
};

// ---------------------------------------------------------------------------
// Tool Properties
// ---------------------------------------------------------------------------

struct key_source_t {
    keymap_section_t section;
    const char *pContext;
};

constexpr u32 ModeMask( map_element_mode_t mode ) noexcept { return 1u << static_cast<u32>( mode ); }
constexpr u32 kVertexMode = ModeMask( map_element_mode_t::VERTICES );
constexpr u32 kEdgeMode = ModeMask( map_element_mode_t::EDGES );
constexpr u32 kFaceMode = ModeMask( map_element_mode_t::FACES );
constexpr u32 kMeshMode = ModeMask( map_element_mode_t::MESHES );
constexpr u32 kObjectMode = ModeMask( map_element_mode_t::OBJECTS );
constexpr u32 kGroupMode = ModeMask( map_element_mode_t::GROUPS );
constexpr u32 kRootModes = kMeshMode | kObjectMode | kGroupMode;
constexpr u32 kAllModes = ( 1u << static_cast<u32>( map_element_mode_t::COUNT ) ) - 1u;

// Variable-length groups keep each tool's parameters and operations together.
// Entries resolve through the registries: absent descriptors are omitted;
// registered but unwired commands keep the registry's disabled state.
struct tool_group_t {
    const char *pTitle;
    std::initializer_list<const char *> settings;
    std::initializer_list<const char *> commands;
    u32 modes{ kAllModes };
    bool namedOperations{};
    const char *pObjectName{};
    const char *pNote{};
};

struct tool_panel_t {
    const char *pSummary;
    std::initializer_list<key_source_t> keys;
    std::initializer_list<tool_group_t> groups;
};

// Keep the edge workflow available while changing transform tools.
constexpr tool_group_t kEdgeDragGroup{
    "Edge dragging", { "editor.map.edge_drag_action" },
    { "map.tool.translate", "map.tool.rotate", "map.tool.scale", "map.tool.pivot", "map.pivot.clear" },
    kEdgeMode, false, "MapEdgeDragSection",
    "Move deforms the picked edges. Extend grows connected faces with move, rotate and scale gizmos. "
    "Place a Pivot away from the edge, then use Rotate to grow a curve in successive angle-snapped steps. "
    "Select also shows move arrows; grabbing the edge itself keeps direct dragging. "
    "Direct 3D edge drags stay horizontal; hold Alt after starting to raise or lower."
};

constexpr tool_panel_t kToolPanels[]{
    // SELECT
    { "Click in any view to select; Ctrl/Command+click toggles and Shift+click adds. In Objects or Groups mode, drag a selected object or an RGB move handle to move the selection. Drag a 2D resize control on the real boundary or a 3D RGB bounds handle to resize with the opposite side fixed. Zoom in or use another pane if handles are too close to distinguish. With multiple objects, Each applies the same grid-distance change to every object; Group scales their combined bounds. Hold Shift when grabbing a resize handle to widen both sides around the center. The anchor is captured at press. Ctrl/Command bypasses snapping. A dashed red source outline and travel guide mark the starting position during movement. Dimensions update in every view. Double-click an object to inspect it, or empty space for view options.",
      { { keymap_section_t::MOUSE, "map.viewport" }, { keymap_section_t::BINDINGS, "map.viewport" }, { keymap_section_t::MOUSE, "map.viewport.2d" } },
      { kEdgeDragGroup,
        { "Vertex Editing", {}, { "map.mesh.merge", "map.mesh.collapse", "map.mesh.bevel", "map.mesh.dissolve", "map.mesh.fill_hole", "map.mesh.connect_edges", "map.texture.weld_uvs",
                                  "map.mesh.radial_align", "map.mesh.offset", "map.mesh.flatten", "map.mesh.flatten_x", "map.mesh.flatten_y", "map.mesh.flatten_z",
                                  "map.select.grow", "map.select.shrink", "map.select.connected", "map.select.path", "map.select.invert_components", "map.select.to_edges", "map.pivot.clear", "map.mesh.project" },
          kVertexMode, true, "MapToolModeVertices", "Pick mesh vertices. Convert brushes to meshes first. Merge, Collapse, Connect, Bevel, Dissolve, Fill Hole, Weld UVs, Radial Align, Offset, Flatten and Snap act on the picked vertices; the Translate, Rotate and Scale tools drag them with a gizmo." },
        { "Vertex snapping", { "editor.grid.size", "editor.grid.snap" }, { "map.brush.snap_to_grid" }, kVertexMode },
        { "Edge Editing", {}, { "map.select.loop", "map.select.ring", "map.mesh.dissolve", "map.mesh.collapse", "map.mesh.bevel", "map.mesh.extrude_edges", "map.mesh.connect_edges", "map.mesh.extend_edges", "map.mesh.merge", "map.mesh.split_edges", "map.mesh.snap_edge_to_edge", "map.mesh.fill_hole", "map.mesh.bridge", "map.mesh.normals_hard", "map.mesh.normals_soft", "map.mesh.normals_default", "map.texture.weld_uvs", "map.select.ribs", "map.pivot.clear", "map.tool.edge_cut", "map.tool.edge_arc", "map.mesh.radial_align", "map.mesh.seam_mark", "map.mesh.seam_clear",
                                "map.mesh.slide_loop", "map.mesh.offset", "map.mesh.flatten", "map.select.connected", "map.select.invert_components", "map.select.boundary_edges", "map.select.sharp_edges", "map.select.to_vertices" },
          kEdgeMode, true, "MapToolModeEdges", "Pick mesh edges; use Select Loop or Select Ring. Convert brushes to meshes first. Open edges extrude, extend, bridge and fill. Snap Edge to Edge welds the other edge onto the one picked last; Slide Edge Loop moves a loop between its neighbours. Edge Cut and Edge Arc are tools; the Translate, Rotate and Scale tools drag edges with a gizmo." },
        // Hammer 5's Faces operations, in its order, then the rest.
        { "Face Editing", {}, { "map.tool.apply_material", "map.texture.fast", "map.texture.hotspot", "map.texture.replace", "map.mesh.merge", "map.mesh.extract_faces", "map.mesh.split", "map.mesh.bridge", "map.mesh.bevel",
                                "map.mesh.thicken_faces", "map.mesh.radial_align", "map.mesh.combine_faces", "map.mesh.collapse", "map.mesh.remove_bad_faces", "map.mesh.repair", "map.select.uv_island", "map.pivot.clear", "map.tool.edge_cut",
                                "map.mesh.extrude", "map.mesh.inset", "map.mesh.inset_width", "map.mesh.solidify", "map.mesh.fill_hole", "map.mesh.offset", "map.mesh.flatten", "map.mesh.delete_face", "map.brush.extrude_new",
                                "map.select.same_material", "map.select.grow", "map.select.shrink", "map.select.connected", "map.select.invert_components", "map.select.coplanar",
                                "map.select.faces_facing", "map.select.same_corners", "map.select.faces_material", "map.select.face_path", "map.mesh.project" },
          kFaceMode, true, "MapToolModeFaces", "Brush faces: normal Push / Pull, Extrude to New Brush and material assignment. In 2D, pick a face outline; a head-on normal needs another pane or Distance. Mesh faces: Shift+click picks several; numeric Extrude raises a connected coplanar region. Corner Inset requires one face; Quad Slice acts on the active face. Split, Offset, Flatten, Delete Face and Bridge (two faces) use the face set. Translate, Rotate and Scale transform picked faces with a gizmo." },
        { "Brush face tools", { "editor.map.sweep_path", "editor.map.sweep_distance", "editor.map.sweep_sideways", "editor.map.sweep_angle", "editor.map.sweep_segments",
                                "editor.map.sweep_iterations" }, { "map.brush.sweep", "map.patch.from_face", "map.brush.extrude_new", "map.texture.wrap_coplanar" }, kFaceMode },
        { "Mapping and smoothing", { "editor.map.projection_axis", "editor.map.smoothing_group", "editor.map.sharp_edge_angle" },
          { "map.texture.project_box", "map.texture.project_cylinder", "map.texture.project_planar", "map.mesh.smoothing_set", "map.mesh.smoothing_clear", "map.mesh.auto_hard_edges" },
          kFaceMode | kRootModes },
        { "Quad Slice", { "editor.map.mesh_slice_u", "editor.map.mesh_slice_v" }, { "map.mesh.quad_slice" }, kFaceMode },
        { "Project onto World", { "editor.map.project_direction", "editor.map.project_offset" }, { "map.mesh.project" }, kVertexMode | kRootModes },
        { "Displacement", { "editor.map.displacement_power", "editor.map.displacement_elevation", "editor.map.displacement_noise", "editor.map.displacement_noise_scale",
                            "editor.map.displacement_octaves", "editor.map.displacement_seed" }, { "map.mesh.create_displacement", "map.tool.displace" }, kFaceMode },
        { "Thicken", { "editor.map.thicken_distance" }, {}, kFaceMode },
        { "Subdivision", {}, { "map.mesh.subdivision_increase", "map.mesh.subdivision_decrease", "map.mesh.subdivision_bake", "map.mesh.subdivision_level_0", "map.mesh.subdivision_level_1",
                               "map.mesh.subdivision_level_2", "map.mesh.subdivision_level_3", "map.mesh.subdivision_level_4", "map.mesh.subdivision_level_5", "map.mesh.subdivide", "map.mesh.smooth" },
          kFaceMode | kRootModes },
        { "Modifiers", { "editor.map.array_axis", "editor.map.array_count", "editor.map.array_distance", "editor.map.radial_count", "editor.map.bend_angle", "editor.map.taper_factor" },
          { "map.mesh.modifier_mirror_x", "map.mesh.modifier_mirror_y", "map.mesh.modifier_mirror_z", "map.mesh.modifier_array", "map.mesh.modifier_radial", "map.mesh.modifier_bend",
            "map.mesh.modifier_taper", "map.mesh.modifiers_apply", "map.mesh.modifier_remove_last", "map.mesh.modifiers_clear" },
          kFaceMode | kRootModes },
        { "Modify Texture", {}, { "map.texture.align_world", "map.texture.align_face", "map.texture.fit", "map.texture.shift", "map.texture.scale", "map.texture.rotate", "map.texture.justify_left", "map.texture.justify_center", "map.texture.justify_right", "map.texture.justify_top", "map.texture.justify_bottom", "map.texture.unwrap" }, kFaceMode },
        { "Mesh Editing", {}, { "map.transform.dialog", "edit.duplicate", "edit.delete", "map.mesh.subdivide", "map.mesh.smooth", "map.mesh.solidify", "map.mesh.boolean_union", "map.mesh.boolean_subtract", "map.mesh.boolean_intersect", "map.mesh.boolean_exclusive", "map.mesh.boolean_split", "map.mesh.to_brush",
                                "map.mesh.weld", "map.mesh.merge_by_distance" },
          kMeshMode, true, "MapToolModeMeshes", "Select whole brushes and meshes. Root transforms, Smooth, Solidify, Subdivide, the booleans and Convert to Brush act on those objects; Weld and Merge by Distance close seams." },
        { "Object Editing", {}, { "map.transform.dialog", "edit.duplicate", "edit.delete", "map.entity.io" }, kObjectMode, true, "MapToolModeObjects" },
        { "Group Editing", {}, { "map.transform.dialog", "edit.duplicate", "edit.delete" }, kGroupMode, true, "MapToolModeGroups" },
        { "Selection", { "editor.map.select_created" }, { "edit.select_all", "edit.invert_selection", "map.select.same_class", "map.select.same_material", "map.select.touching", "map.select.inside", "map.select.tall" }, kRootModes },
        { "Patch Matrix", { "editor.map.patch_thickness", "editor.map.patch_thicken_seams" }, { "map.patch.insert_row", "map.patch.insert_column", "map.patch.remove_row", "map.patch.remove_column", "map.patch.transpose", "map.patch.invert", "map.patch.thicken", "map.patch.cap" }, kRootModes },
        { "Movement snapping", { "editor.grid.size", "editor.grid.snap", "editor.grid.geometry_snap", "editor.grid.geometry_snap_pixels" }, {}, kRootModes },
        { "Bounds resizing", { "editor.map.resize_mode", "editor.map.resize_from_center" }, {}, kRootModes },
        { "2D display", { "editor.viewport.show_selection_dimensions", "editor.viewport.entity_names" },
          { "map.view.center_selection_2d" } },
        { "3D display", { "editor.viewport.perspective.show_selection_bounds", "editor.viewport.perspective.show_selection_dimensions", "editor.viewport.perspective.entity_names" },
          { "map.view.center_selection_3d" } },
        { "Clipboard", { "editor.map.paste_offset" }, { "edit.copy", "edit.cut", "edit.paste", "edit.paste_special" }, kRootModes },
        { "Brush operations", { "editor.map.hollow_thickness" }, { "map.brush.hollow", "map.brush.merge", "map.brush.to_mesh", "map.brush.make_room" }, kRootModes },
        { "Mesh objects", {}, { "map.mesh.flip_normals", "map.mesh.triangulate" }, kRootModes },
        { "Inspect and isolate", {}, { "view.properties.open", "map.hide.selected", "map.hide.unselected", "map.hide.show_all" }, kRootModes } } },
    // CAMERA
    { "Camera navigation works independently of the editing tool. The key reference lists the currently bound look, orbit, pan, dolly, flight, speed, and framing controls. Fly while looking, or with the Camera tool idle; orbit, pan, and dolly pause flight movement. Speed controls change the persisted fly speed. Go To finds an object or frames a position.",
      { { keymap_section_t::HELD, "map.viewport.3d" }, { keymap_section_t::MOUSE, "map.viewport.3d" },
        { keymap_section_t::BINDINGS, "map.viewport.3d" }, { keymap_section_t::BINDINGS, "map.viewport" } },
      { { "Movement", { "editor.camera.move_speed", "editor.camera.fast_multiplier", "editor.camera.slow_multiplier", "editor.camera.look_sensitivity", "editor.camera.invert_y" },
          { "map.camera.speed_increase", "map.camera.speed_decrease", "map.camera.speed_reset" } },
        { "Activation", { "editor.viewport.activate_on_hover" }, {} },
        { "Navigation", { "editor.camera.fov", "editor.camera.zoom_to_cursor", "editor.camera.zoom_sensitivity", "editor.camera.invert_wheel" },
          { "map.go_to", "map.view.frame_all", "map.view.center_selection_3d", "map.view.center_selection_2d" } } } },
    // ENTITY
    { "Click in a 2D view (on the grid) or on a surface in 3D to place the default point class there; it is selected. Ctrl/Command bypasses snapping.", { { keymap_section_t::BINDINGS, "map.tool.entity" } },
      { { "Entity defaults", { "editor.map.default_point_class", "editor.map.default_solid_class", "editor.map.write_all_properties", "editor.map.select_created" }, {} },
        { "Entity display", { "editor.viewport.entity_names", "editor.viewport.io_lines" }, { "map.select.same_class" } } } },
    // BLOCK
    { "Draw a box, quad, wedge, cylinder, spike, or sphere in a map view (in 3D it stands on the surface under the cursor, or the ground), or create it from exact dimensions below. Shift + wheel adjusts depth during a drag. Refine the staged primitive using bounds handles; hold Shift at handle press to resize around its center, then press Enter to create it (or turn on Create blocks on release; picking another tool also creates it).", { { keymap_section_t::BINDINGS, "map.tool.block" } },
      { { "Creation defaults", { "editor.map.block_output", "editor.map.block_depth", "editor.map.block_create_on_release", "editor.map.select_created" }, {} },
        { "Bounds resizing", { "editor.map.resize_from_center" }, {} },
        { "Primitive", { "editor.map.new_brush_shape", "editor.map.primitive_axis", "editor.map.cylinder_sides", "editor.map.cone_top_radius", "editor.map.sphere_subdivisions",
                         "editor.map.arch_thickness", "editor.map.arch_angle", "editor.map.arch_start", "editor.map.stairs_steps", "editor.map.stairs_direction",
                         "editor.map.spiral_turn", "editor.map.spiral_inner", "editor.map.torus_tube" }, {} },
        { "New faces", { "editor.map.default_material", "editor.map.default_texture_scale" }, {} },
        { "Brush operations", { "editor.map.hollow_thickness", "editor.map.carve_mode" }, { "map.brush.hollow", "map.brush.carve", "map.brush.merge", "map.brush.to_mesh" } },
        { "Construction grid", { "editor.grid.size", "editor.grid.snap" }, {} } } },
    // TEXTURE
    { "Choose the active material and inspect available alignment and UV operations. Defaults apply to newly created faces.",
      { { keymap_section_t::BINDINGS, "map.tool.texture" } },
      { { "Material and creation defaults", { "editor.map.default_material", "editor.map.default_texture_scale" }, { "assets.browse_materials", "map.tool.apply_material", "map.texture.replace" } },
        { "Texture behavior", { "editor.map.texture_lock", "editor.map.texture_scale_lock" }, {} },
        { "Modify Texture", {}, { "map.texture.align_world", "map.texture.align_face", "map.texture.fit", "map.texture.shift", "map.texture.scale", "map.texture.rotate",
                                    "map.texture.justify_left", "map.texture.justify_center", "map.texture.justify_right", "map.texture.justify_top", "map.texture.justify_bottom", "map.texture.unwrap" } } } },
    // DECAL
    { "Click a surface to place an infodecal there with the active material.", { { keymap_section_t::BINDINGS, "map.tool.decal" } },
      { { "Material", { "editor.map.default_material" }, { "assets.browse_materials" } } } },
    // OVERLAY
    { "Click a surface to place an info_overlay there with the active material, facing out of the surface.", { { keymap_section_t::BINDINGS, "map.tool.overlay" } },
      { { "Material", { "editor.map.default_material" }, { "assets.browse_materials" } } } },
    // CLIP
    { "3D (Hammer 5): draw the cut across any face, or click point 1 and point 2; points lock onto vertices (Ctrl/Command places them freely), so you can cut from vertex to vertex. The cut stands straight through the face; a third click tilts it through all three points (Backspace takes point 3 back). With nothing selected, the first click picks what to cut. 2D: drag a cutting line; it extrudes along the view's depth. Drag staged points to adjust them. Confirm applies the cut; Cancel discards it. Keep Front, Back or Both below; repeat this tool to cycle them.", { { keymap_section_t::BINDINGS, "map.tool.clip" } },
      { { "Construction grid", { "editor.grid.size", "editor.grid.snap" }, {} },
        { "Plane placement", { "editor.map.clip_3d_placement", "editor.map.clip_axis" }, {} },
        { "Retained half-space", { "editor.map.clip_mode" }, {} },
        { "Mesh cutting", {}, { "map.mesh.slice" } } } },
    // VERTEX
    { "Drag the corners of the selected brushes and meshes. A brush rebuilds as a convex solid, as in TrenchBroom; a mesh vertex in the component selection moves the whole selection. Ctrl/Command bypasses snapping; click elsewhere to select.", { { keymap_section_t::BINDINGS, "map.tool.vertex" } },
      { { "Snapping", { "editor.grid.size", "editor.grid.snap" }, { "map.brush.snap_to_grid" } },
        { "Topology", {}, { "map.mesh.merge", "map.mesh.collapse", "map.mesh.split", "map.mesh.dissolve", "map.mesh.triangulate", "map.mesh.fill_hole" } } } },
    // PATH
    { "Click to place path_corner entities; each new corner becomes the previous corner's target. Confirm or Cancel starts a new path.", { { keymap_section_t::BINDINGS, "map.tool.path" } },
      { { "Construction grid", { "editor.grid.size", "editor.grid.snap" }, {} } } },
    // MEASURE
    { "Drag to measure: on the grid plane in 2D, between surface points in 3D. Length and per-axis distances stay until the next drag or Cancel.", { { keymap_section_t::BINDINGS, "map.tool.measure" } },
      { { "Measurement display", { "editor.viewport.show_rulers", "editor.viewport.show_selection_dimensions", "editor.grid.size" }, {} } } },
    // TERRAIN
    { "Drag on a terrain to sculpt it: Raise (Ctrl/Command lowers), Lower, Smooth, or Flatten to the height the stroke started at. One stroke is one undo step; Escape cancels it. Create Terrain below makes a flat one. Material painting is not connected yet.", { { keymap_section_t::BINDINGS, "map.tool.terrain" } },
      { { "Sculpt", { "editor.map.terrain_mode", "editor.map.terrain_radius", "editor.map.terrain_strength" }, { "map.terrain.raise", "map.terrain.lower", "map.terrain.smooth", "map.terrain.flatten" } },
        { "Painting (planned)", {}, { "map.terrain.paint" } } } },
    // PATCH
    { "Drag a box in a 2D view, or on a surface or the ground in 3D, to create a patch of the chosen shape. Cylinders, cones and discs run along the pane's depth by the block depth.", { { keymap_section_t::BINDINGS, "map.tool.patch" } },
      { { "Construction grid", { "editor.grid.size", "editor.grid.snap" }, {} },
        { "Shape", { "editor.map.patch_shape", "editor.map.cylinder_sides", "editor.map.block_depth" }, {} },
        { "Material", { "editor.map.default_material" }, { "assets.browse_materials" } } } },
    // TRANSLATE
    { "Move the selection along an axis or plane. Enable Snap to geometry to align nearby brush, mesh, or patch bounds even when they were made on another grid. Edges meet edges and centers match centers; RGB Snap labels identify the target coordinate. The selection keeps its spacing. This works in 2D and with 3D axis or world-plane handles; free camera-plane movement keeps grid steps. Ctrl/Command temporarily bypasses both snap types.", { { keymap_section_t::BINDINGS, "map.tool.translate" } },
      { kEdgeDragGroup,
        { "Move steps", { "editor.grid.size", "editor.grid.snap", "editor.grid.geometry_snap", "editor.grid.geometry_snap_pixels", "editor.map.paste_offset", "editor.map.texture_lock" }, { "map.brush.snap_to_grid" } },
        { "Alignment", {}, { "map.align.left", "map.align.right", "map.align.top", "map.align.bottom" } } } },
    // ROTATE
    { "Rotate the selection about its pivot.", { { keymap_section_t::BINDINGS, "map.tool.rotate" } },
      { kEdgeDragGroup,
        { "Rotation", { "editor.grid.angle_snap", "editor.map.texture_lock" }, { "map.grid.angle_snap", "map.transform.rotate_cw", "map.transform.rotate_ccw" } } } },
    // SCALE
    { "Scale the selection about its pivot.", { { keymap_section_t::BINDINGS, "map.tool.scale" } },
      { kEdgeDragGroup,
        { "Scale", { "editor.grid.scale_snap", "editor.map.texture_scale_lock" }, { "map.grid.scale_snap" } } } },
    // PIVOT
    { "Click or drag in any view to place the pivot (2D: on the grid; 3D: on the surface). The Rotate and Scale tools, their numeric panels, Rotate 90 and the flips then turn and scale about it. Without one, the dashed marker shows the selection centre they use. Cancel or Clear Pivot removes it; right-click > Set Pivot Here places it too.", { { keymap_section_t::BINDINGS, "map.tool.pivot" } },
      { { "Construction grid", { "editor.grid.size", "editor.grid.snap" }, { "map.pivot.clear", "map.tool.rotate", "map.tool.scale", "map.transform.rotate_cw", "map.transform.rotate_ccw", "map.transform.flip_horizontal", "map.transform.flip_vertical" } } } },
    // POLYGON
    { "Click points in a 2D view, or on a surface in 3D (on the workplane when one is set). Click the first point or Confirm to extrude the polygon into a mesh by the block depth; Backspace removes the last point.", { { keymap_section_t::BINDINGS, "map.tool.polygon" } },
      { { "Construction", { "editor.grid.size", "editor.grid.snap", "editor.map.block_depth", "editor.map.default_material", "editor.map.select_created" }, {} } } },
    // MIRROR
    { "Drag a line in a 2D view to mirror the selection across it; hold Shift on release to mirror a copy and keep the original.", { { keymap_section_t::BINDINGS, "map.tool.mirror" } },
      { { "Reflection", { "editor.grid.snap", "editor.map.texture_lock" }, { "map.transform.flip_horizontal", "map.transform.flip_vertical" } } } },
    // PAINT
    { "Drag over a mesh to paint its vertex colours with the paint colour (or one channel); the shaded 3D view shows the result. Ctrl/Command paints back toward white. Convert brushes to meshes first.", { { keymap_section_t::BINDINGS, "map.tool.paint" } },
      { { "Brush", { "editor.map.paint_channel", "editor.map.paint_color", "editor.map.paint_radius", "editor.map.paint_strength" }, {} },
        { "Material", { "editor.map.default_material" }, { "assets.browse_materials", "map.terrain.paint" } } } },
    // EXTRUDE
    { "Pick a brush face outline in 2D or a face in 3D, then drag its normal handle to push or pull its convex solid. If the normal points into the 2D view, use another pane or the Distance control. For an authored mesh, numeric Extrude supports one face or a connected coplanar region. Corner Inset requires one face. Translate, Rotate and Scale transform picked faces with a gizmo.", { { keymap_section_t::BINDINGS, "map.tool.extrude" } },
      { { "Construction grid", { "editor.grid.size", "editor.grid.snap" }, {} },
        { "Face operations", {}, { "map.mesh.extrude", "map.mesh.inset", "map.mesh.inset_width", "map.mesh.bevel", "map.mesh.solidify", "map.mesh.bridge", "map.mesh.fill_hole" } } } },
    // KNIFE
    { "Click points across a mesh: corners, edges and face interiors snap. Confirm cuts, Backspace removes the last point, Cancel discards the path.", { { keymap_section_t::BINDINGS, "map.tool.knife" } },
      { { "Cutting", { "editor.grid.snap" }, { "map.mesh.slice", "map.mesh.split", "map.mesh.dissolve" } } } },
    // LOOP_CUT
    { "Hover a mesh edge and click to cut a loop across its ring at that position; Ctrl/Command cuts at the middle.", { { keymap_section_t::BINDINGS, "map.tool.loop_cut" } },
      { { "Loops and surfaces", {}, { "map.select.loop", "map.select.ring", "map.mesh.subdivide", "map.mesh.smooth" } } } },
    // EYEDROPPER
    { "Click a face to make its material the active material.", { { keymap_section_t::BINDINGS, "map.tool.eyedropper" } },
      { { "Material", { "editor.map.default_material" }, { "assets.browse_materials", "map.select.same_material", "map.texture.replace" } } } },
    // WORKPLANE
    { "Click a face in the 3D view to make its plane the workplane: in 3D the Entity, Polygon, Patch, Path and Curve tools then place on it, and its grid stays visible until Cancel in this tool clears it.", { { keymap_section_t::BINDINGS, "map.tool.workplane" } },
      { { "Construction grid", { "editor.grid.size", "editor.grid.snap", "editor.grid.show_3d" }, {} } } },
    // CURVE
    { "Click path points, then Confirm. Straight and Smooth make solid strips or cables; Hollow Pipe makes inner and outer walls with open bores and annular end rims. Hollow Pipe supports open Mesh paths only. The viewport previews the path; geometry is built on Confirm. Backspace removes the last point, Cancel discards the path.", { { keymap_section_t::BINDINGS, "map.tool.curve" } },
      { { "Construction grid", { "editor.grid.size", "editor.grid.snap" }, {} },
        { "Shape", { "editor.map.curve_shape", "editor.map.curve_output", "editor.map.curve_profile", "editor.map.curve_closed", "editor.map.wall_join", "editor.map.lathe_angle", "editor.map.cylinder_sides" }, {} },
        { "Dimensions", { "editor.map.curve_width", "editor.map.curve_height", "editor.map.curve_outer_radius", "editor.map.curve_wall_thickness", "editor.map.default_material" }, {} } } },
    // EDGE_CUT
    { "Click points on mesh edges or corners; consecutive points that share a face are cut across it. Ctrl/Command snaps to the edge's middle. Confirm cuts, Backspace removes the last point, Cancel discards the path.",
      { { keymap_section_t::BINDINGS, "map.tool.knife" } },
      { { "Cutting", {}, { "map.mesh.connect_edges", "map.mesh.split_edges", "map.mesh.dissolve" } } } },
    // EDGE_ARC
    { "Select a chain of mesh edges in Edges mode, then drag in a view: the chain bends into a circular arc between its two ends, as high as the drag. Release applies; Escape cancels.",
      { { keymap_section_t::BINDINGS, "map.tool.knife" } },
      { { "Arc", { "editor.grid.snap" }, { "map.select.loop", "map.mesh.radial_align" } } } },
    // DISPLACE
    { "Paint over a mesh to sculpt it (Hammer 5's Paint Displacement): Push/Pull raises the surface and Ctrl/Command lowers it; the other brushes smooth, flatten, inflate, pinch or roughen. [ and ] resize the brush. The surface moves as you paint; each stroke is one undo step, Escape during a stroke puts it back. A mesh needs vertices to move: Quad Slice or Subdivide a flat face first.",
      { { keymap_section_t::BINDINGS, "map.tool.displace" } },
      // The brush and falloff are the icon row and curves above these.
      { { "Sculpt", { "editor.map.displace_radius", "editor.map.displace_strength", "editor.map.displace_smooth", "editor.map.displace_normal", "editor.map.displace_auto_tessellate" }, {} },
        { "Apply displacement to", { "editor.map.displace_target" }, {} },
        { "Resolution", { "editor.map.mesh_slice_u", "editor.map.mesh_slice_v" }, { "map.mesh.quad_slice", "map.mesh.subdivide", "map.mesh.smooth" } } } },
    // SHEAR
    { "Drag a side of the selection's bounds to slide it along itself; the opposite side stays put and everything between leans (TrenchBroom's Shear tool). In 3D, Alt slides a wall side up or down; the top and bottom slide across. Textures stay on brushes with Texture Lock.",
      { { keymap_section_t::BINDINGS, "map.tool.shear" } },
      { { "Shear", { "editor.grid.snap", "editor.map.texture_lock" }, { "map.tool.scale", "map.tool.rotate" } } } },
    // NONE — navigation only; preserved selection remains inspectable.
    { "No editing tool is active. Pan a 2D view or navigate the 3D camera while keeping the selection for inspection. Select an editing tool to resume; Cancel again clears the preserved selection.",
      { { keymap_section_t::BINDINGS, "map.viewport" }, { keymap_section_t::BINDINGS, "map.viewport.3d" },
        { keymap_section_t::BINDINGS, "map.viewport.2d" } }, {} },
};
static_assert( std::size( kToolPanels ) == static_cast<usize>( map_tool_t::COUNT ), "Tool panel table and enum diverged" );

struct selection_profile_t {
    const char *pTitle;
    const char *pIcon;
    const char *pSummary;
};
constexpr selection_profile_t kSelectionProfiles[]{
    { "Vertex Editing", "select-vertices", "Pick authored mesh vertices in a 2D or 3D view. Shift+click adds and Ctrl/Command+click toggles vertices on the same mesh. Convert brushes to meshes before selecting their vertices. Merge, Collapse, Connect, Weld UVs, Radial Align, Offset, Flatten and Snap edit the picked vertices. The Translate, Rotate and Scale tools drag them with a gizmo; dragging does not move the parent mesh." },
    { "Edge Editing", "select-edges", "Pick authored mesh edges in a 2D or 3D view. Shift+click adds and Ctrl/Command+click toggles edges on the same mesh. Select Loop and Select Ring extend the selection from the last picked edge. Convert brushes to meshes before selecting their edges. Bevel, Split, Connect, Dissolve, Collapse, normals and Weld UVs edit the picked edges; open edges also Extrude, Extend, Bridge and Fill Hole. The Translate, Rotate and Scale tools drag the picked edges with a gizmo." },
    { "Face Editing", "select-faces", "In 2D, pick a brush face outline and drag its projected normal handle for grid-snapped Push / Pull. If the normal points into the view, use another pane or the Distance control. In 3D, pick a face and drag its normal handle. Mesh faces: Shift+click adds faces of the same mesh and Ctrl/Command+click toggles one. Numeric Extrude raises a connected coplanar region; Corner Inset requires one face; Quad Slice acts on the active face. Offset, Flatten, Detach, Delete and face queries use the face set. Translate, Rotate and Scale transform picked faces with a gizmo." },
    { "Mesh Editing", "select-meshes", "Select whole brushes and meshes. Move or resize their bounds; use supported brush and mesh operations below." },
    { "Selection Tool", "select-objects", kToolPanels[static_cast<usize>( map_tool_t::SELECT )].pSummary },
    { "Selection Tool", "select-groups", kToolPanels[static_cast<usize>( map_tool_t::SELECT )].pSummary },
    { "Selection Tool", "select-navigation", "Select an editing mode or use the Camera tool to navigate the map." },
};
static_assert( std::size( kSelectionProfiles ) == static_cast<usize>( map_element_mode_t::COUNT ), "Selection profiles and enum diverged" );

struct tool_section_t { QWidget *pSection; u32 modes; };
struct tool_inventory_entry_t { QString id; u32 modes; };

// Collapsing a section changes its body visibility, not its advertised
// inventory. Use the same mode mask as section visibility, independently of
// whether a parent window has been shown or a body has been collapsed.
QStringList VisibleInventory( const std::vector<tool_inventory_entry_t> &entries, u32 mode )
{
    QStringList result;
    for ( const auto &entry : entries ) {
        if ( ( entry.modes & mode ) != 0u && !result.contains( entry.id ) ) { result.append( entry.id ); }
    }
    return result;
}

const char *SectionKey( keymap_section_t section ) noexcept
{
    switch ( section ) {
        case keymap_section_t::BINDINGS: return "bindings";
        case keymap_section_t::HELD: return "held";
        case keymap_section_t::MOUSE: return "mouse";
    }
    return "bindings";
}

const char *PlatformKey( keymap_platform_t platform ) noexcept
{
    switch ( platform ) {
        case keymap_platform_t::MACOS: return "macos";
        case keymap_platform_t::WINDOWS: return "windows";
        case keymap_platform_t::LINUX: return "linux";
        case keymap_platform_t::NONE: break;
    }
    return "";
}

// "map.camera.forward" -> "Camera forward": the readable name of an action
// that is not a registered command (held keys, mouse gestures).
QString ActionLabel( const gui::editor_gui_t &gui, const QString &id )
{
    const QByteArray utf8 = id.toUtf8();
    if ( const command_desc_t *pCommand = EditorCommands_Find( &gui.commands, string_view_t{ utf8.constData(), static_cast<usize>( utf8.size() ) } ) ) {
        return QString::fromUtf8( pCommand->pLabel );
    }
    QStringList parts = id.split( QLatin1Char( '.' ) );
    if ( parts.size() > 1 ) { parts.removeFirst(); }
    QString text = parts.join( QLatin1Char( ' ' ) ).replace( QLatin1Char( '_' ), QLatin1Char( ' ' ) );
    if ( !text.isEmpty() ) { text[0] = text[0].toUpper(); }
    return text;
}

// Keep the actual binding in the tooltip and query API. Mouse-button names
// are abbreviated only in the narrow, always-visible key reference.
QString KeyDisplay( QString text )
{
    if ( text.startsWith( QLatin1Char( '[' ) ) && text.endsWith( QLatin1Char( ']' ) ) ) { text = text.mid( 1, text.size() - 2 ); }
    for ( const auto &[name, label] : { std::pair{ "LeftClick", "LMB" }, { "RightClick", "RMB" }, { "MiddleClick", "MMB" },
                                      { "LeftDrag", "LMB drag" }, { "RightDrag", "RMB drag" }, { "MiddleDrag", "MMB drag" } } ) {
        text.replace( QString::fromLatin1( name ), QString::fromLatin1( label ), Qt::CaseInsensitive );
    }
    return text;
}

// Every ID the keymap chain mentions in one section and context, the main
// section and this platform's overlay, in first-seen order.
QStringList IdsIn( const gui::editor_gui_t &gui, keymap_section_t section, const char *pContext )
{
    QStringList ids;
    const string_view_t sectionKey = StringView_FromCString( SectionKey( section ) );
    const string_view_t context = StringView_FromCString( pContext );
    for ( usize i = 0u; i < gui.nKeymapChain; ++i ) {
        const key_value_t *pRoot = gui.keymapChain[i];
        const key_value_t *pPlatforms = KeyValue_Find( pRoot, StringView_FromCString( "platforms" ) );
        const key_value_t *pOverlay = KeyValue_Find( pPlatforms, StringView_FromCString( PlatformKey( EditorKeymap_HostPlatform() ) ) );
        for ( const key_value_t *pBase : { pRoot, pOverlay } ) {
            const key_value_t *pContextObject = KeyValue_Find( KeyValue_Find( pBase, sectionKey ), context );
            for ( usize e = 0u; e < KeyValue_ChildCount( pContextObject ); ++e ) {
                const QString id = FromView( KeyValue_Name( KeyValue_ChildAt( pContextObject, e ) ) );
                if ( !ids.contains( id ) ) { ids.append( id ); }
            }
        }
    }
    return ids;
}

QString ToolCommandId( map_tool_t tool )
{
    return QStringLiteral( "map.tool.%1" ).arg( QString::fromUtf8( MapWorkspace_ToolName( tool ) ).toLower().replace( QLatin1Char( ' ' ), QLatin1Char( '_' ) ) );
}

class tool_properties_t final : public QWidget {
public:
    tool_properties_t( QWidget *pParent, map_workspace_t *pWorkspace ) : QWidget( pParent ), m_pWorkspace( pWorkspace )
    {
        CY_ASSERT( pWorkspace != nullptr );
        setObjectName( QStringLiteral( "MapToolProperties" ) );
        auto *pLayout = new QVBoxLayout( this );
        pLayout->setContentsMargins( 0, 0, 0, 0 );
        m_pScroll = new QScrollArea( this );
        m_pScroll->setWidgetResizable( true );
        m_pScroll->setFrameShape( QFrame::NoFrame );
        pLayout->addWidget( m_pScroll );
        ( void )MapWorkspace_AddListener( pWorkspace, &tool_properties_t::OnMapChanged, this );
        ( void )gui::EditorGui_AddKeymapListener( pWorkspace->pGui, &tool_properties_t::OnKeymapChanged, this );
        ( void )gui::EditorGui_AddStyleListener( pWorkspace->pGui, &tool_properties_t::OnStyleChanged, this );
        if ( auto *clipboard = QApplication::clipboard() ) {
            QObject::connect( clipboard, &QClipboard::dataChanged, this, [this]() { RefreshButtons(); } );
        }
        ( void )EditorSettings_AddListener( &pWorkspace->pGui->settings, &tool_properties_t::OnSettingsChanged, this );
        Rebuild();
    }

    ~tool_properties_t() override
    {
        MapWorkspace_RemoveListener( m_pWorkspace, &tool_properties_t::OnMapChanged, this );
        gui::EditorGui_RemoveKeymapListener( m_pWorkspace->pGui, &tool_properties_t::OnKeymapChanged, this );
        gui::EditorGui_RemoveStyleListener( m_pWorkspace->pGui, &tool_properties_t::OnStyleChanged, this );
        EditorSettings_RemoveListener( &m_pWorkspace->pGui->settings, &tool_properties_t::OnSettingsChanged, this );
    }

    QString Title() const { return m_title; }
    QStringList KeyRows() const { return m_keyRows; }
    QStringList Options() const
    {
        QStringList result = VisibleInventory( m_optionInventory, ModeMask( m_mode ) );
        if ( m_tool == map_tool_t::CURVE ) {
            for ( const auto &control : m_curveControls ) {
                if ( !CurveOptionVisible( control.first->objectName() ) ) { result.removeAll( control.first->objectName() ); }
            }
        }
        return result;
    }
    QStringList Operations() const { return VisibleInventory( m_operationInventory, ModeMask( m_mode ) ); }
    void SetCancelHandler( map_tool_cancel_event_fn handler, void *context )
    {
        m_pfnCancelHandler = handler;
        m_pCancelContext = context;
    }

protected:
    bool eventFilter( QObject *watched, QEvent *event ) override
    {
        if ( m_pfnCancelHandler != nullptr &&
             ( event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress ) &&
             QApplication::focusWidget() == watched && QApplication::activePopupWidget() == nullptr &&
             QApplication::activeModalWidget() == nullptr ) {
            // The owning geometric pane resolves the effective Cancel key
            // and handles its current drag/stage/navigation state. A handled
            // event may rebuild this content and delete watched: touch no
            // control after the callback and return immediately.
            if ( m_pfnCancelHandler( m_pCancelContext, event ) ) { return true; }
        }
        return QWidget::eventFilter( watched, event );
    }

private:
    static void OnSettingsChanged( void *context, string_view_t path ) noexcept
    {
        if ( path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( "editor.camera.move_speed" ) ) ||
             StringView_Equals( path, StringView_FromCString( "editor.map.mesh_slice_u" ) ) ||
             StringView_Equals( path, StringView_FromCString( "editor.map.mesh_slice_v" ) ) ||
             StringView_Equals( path, StringView_FromCString( "editor.map.curve_shape" ) ) ||
             StringView_Equals( path, StringView_FromCString( "editor.map.curve_output" ) ) ||
             StringView_Equals( path, StringView_FromCString( "editor.map.curve_closed" ) ) ) {
            static_cast<tool_properties_t *>( context )->RefreshButtons();
        }
    }
    static void OnMapChanged( void *pContext, u32 changes ) noexcept
    {
        auto *pPanel = static_cast<tool_properties_t *>( pContext );
        if ( ( changes & MAP_CHANGE_VIEW ) != 0u && pPanel->m_pWorkspace->tool != pPanel->m_tool ) {
            pPanel->Rebuild();
        } else if ( ( changes & ~MAP_CHANGE_CURSOR ) != 0u ) {
            const bool modeChanged = pPanel->m_pWorkspace->elementMode != pPanel->m_mode;
            pPanel->m_mode = pPanel->m_pWorkspace->elementMode;
            if ( modeChanged || ( changes & ( MAP_CHANGE_SELECTION | MAP_CHANGE_DOCUMENT ) ) != 0u ) {
                // A face selector emits this notification while its own
                // callback is active. Refresh the reference table only;
                // never replace the selected component's control tree.
                pPanel->RefreshKeys( pPanel->findChild<QTreeWidget *>( QStringLiteral( "MapToolKeys" ) ) );
            }
            pPanel->RefreshButtons( ( changes & MAP_CHANGE_DOCUMENT ) != 0u );
        }
    }

    static void OnKeymapChanged( void *pContext ) noexcept
    {
        auto *pPanel = static_cast<tool_properties_t *>( pContext );
        pPanel->RefreshKeys( pPanel->findChild<QTreeWidget *>( QStringLiteral( "MapToolKeys" ) ) );
        pPanel->RefreshButtons( false );
    }

    static void OnStyleChanged( void *pContext ) noexcept
    {
        auto &panel = *static_cast<tool_properties_t *>( pContext );
        const gui::editor_gui_t &gui = *panel.m_pWorkspace->pGui;
        // A theme can change inside a settings notification. Refresh visuals
        // in place: rebuilding here would destroy controls still present in
        // the settings registry's listener snapshot.
        if ( auto *pKeys = panel.findChild<QTreeWidget *>( QStringLiteral( "MapToolKeys" ) ) ) {
            const QColor keyColor = gui::EditorStyle_TokenColor( gui.style, "ui.accent" );
            for ( int i = 0; i < pKeys->topLevelItemCount(); ++i ) { pKeys->topLevelItem( i )->setForeground( 0, keyColor ); }
        }
        if ( panel.m_tool == map_tool_t::SELECT ) { panel.RefreshSelectionProfile(); }
        else if ( panel.m_pToolIcon != nullptr ) {
            const QByteArray id = ToolCommandId( panel.m_tool ).toUtf8();
            const command_desc_t *pTool = EditorCommands_Find( &gui.commands, { id.constData(), static_cast<usize>( id.size() ) } );
            if ( pTool != nullptr && pTool->pIcon != nullptr ) {
                panel.m_pToolIcon->setPixmap( gui::EditorStyle_Icon( gui.style, pTool->pIcon ).pixmap( 24, 24 ) );
            }
        }
        if ( panel.m_pPrimitiveControls != nullptr ) { panel.m_pPrimitiveControls->RefreshIcons(); }
        if ( panel.m_pFaceControls != nullptr ) { panel.m_pFaceControls->RefreshIcons(); }
        for ( QToolButton *pButton : panel.m_buttons ) {
            const QByteArray id = pButton->objectName().toUtf8();
            const command_desc_t *pCommand = EditorCommands_Find( &gui.commands, { id.constData(), static_cast<usize>( id.size() ) } );
            if ( pCommand != nullptr && pCommand->pIcon != nullptr ) {
                pButton->setIcon( gui::EditorStyle_Icon( gui.style, pCommand->pIcon ) );
            }
        }
    }

    void RefreshKeys( QTreeWidget *pKeys )
    {
        if ( pKeys == nullptr ) { return; }
        // Selection can change from a face-selector signal. Refresh only
        // this table, preserving controls that are still emitting signals.
        pKeys->clear();
        m_keyRows.clear();
        const gui::editor_gui_t &gui = *m_pWorkspace->pGui;
        const tool_panel_t &info = kToolPanels[static_cast<usize>( m_tool )];
        const QByteArray toolId = ToolCommandId( m_tool ).toUtf8();
        const QColor keyColor = gui::EditorStyle_TokenColor( gui.style, "ui.accent" );
        const auto addRow = [&]( const QString &keys, const QString &label ) {
            auto *pItem = new QTreeWidgetItem( pKeys );
            pItem->setText( 0, KeyDisplay( keys ) );
            pItem->setToolTip( 0, keys );
            pItem->setForeground( 0, keyColor );
            pItem->setText( 1, label );
            pItem->setToolTip( 1, label );
            m_keyRows.append( QStringLiteral( "%1 %2" ).arg( keys, label ) );
        };
        const bool brushFace = MapWorkspace_HasBrushFace( m_pWorkspace );
        const bool rootMode = ( ModeMask( m_mode ) & kRootModes ) != 0u;
        QStringList listedCommands;
        const auto addCommand = [&]( const char *command, const QString &label ) {
            const QString id = QString::fromUtf8( command );
            if ( m_tool == map_tool_t::SELECT ) {
                if ( !rootMode && ( id == "map.brush.to_mesh" || id == "map.mesh.flip_normals" || id == "map.mesh.triangulate" ) ) { return; }
                if ( m_mode != map_element_mode_t::FACES && ( id == "map.mesh.extrude" || id == "map.mesh.inset" || id == "map.mesh.inset_width" || id == "map.mesh.quad_slice" ) ) { return; }
            }
            if ( brushFace && ( id == QStringLiteral( "map.mesh.extrude" ) || id == QStringLiteral( "map.mesh.inset" ) || id == QStringLiteral( "map.mesh.inset_width" ) ||
                                id == QStringLiteral( "map.mesh.quad_slice" ) ) ) { return; }
            if ( listedCommands.contains( id ) ||
                 EditorCommands_Find( &gui.commands, StringView_FromCString( command ) ) == nullptr ) { return; }
            listedCommands.append( id );
            // Use the same contextual one-stroke lookup as the view. A raw
            // declaration may be shadowed, explicitly unbound, or a sequence
            // the viewport cannot execute.
            const auto keys2d = MapInput_CommandBindings( m_pWorkspace, command, false );
            const auto keys3d = MapInput_CommandBindings( m_pWorkspace, command, true );
            const auto addKeys = [&]( const QStringList &keys, const QString &suffix ) {
                if ( !keys.isEmpty() ) { addRow( QStringLiteral( "[%1]" ).arg( keys.join( QStringLiteral( " / " ) ) ), label + suffix ); }
            };
            if ( keys2d == keys3d ) { addKeys( keys2d, {} ); }
            else { addKeys( keys2d, QStringLiteral( " (2D)" ) ); addKeys( keys3d, QStringLiteral( " (3D)" ) ); }
        };
        // Navigation needs a direct route back to editing rather than a
        // reference that merely activates the state already in use.
        if ( m_tool == map_tool_t::NONE ) {
            addCommand( "map.tool.select", QStringLiteral( "Resume editing with Select" ) );
        } else {
            addCommand( toolId.constData(), m_tool == map_tool_t::CLIP ? QStringLiteral( "Cycle retained half-space while active" ) :
                        QStringLiteral( "Select this tool" ) );
        }
        keymap_triggers_t triggers{};
        if ( m_tool == map_tool_t::SELECT || m_tool == map_tool_t::EXTRUDE ) {
            for ( const auto &[command, label] : {
                      std::pair{ "map.brush.to_mesh", "Brush objects: convert actual faces to authored meshes" },
                      std::pair{ "map.mesh.flip_normals", "Mesh objects: reverse all faces (Objects/Groups/Meshes)" },
                      std::pair{ "map.mesh.triangulate", "Mesh objects: triangulate all polygons (Objects/Groups/Meshes)" },
                      std::pair{ "map.mesh.extrude", "Mesh faces: extrude a connected coplanar region (Faces)" },
                      std::pair{ "map.mesh.inset", "One mesh face: inset corners toward its center (Faces)" },
                      std::pair{ "map.mesh.inset_width", "One planar convex face: measured border width (Faces)" },
                      std::pair{ "map.mesh.quad_slice", "Mesh face: slice a quad into U/V cells (Faces)" } } ) {
                addCommand( command, QString::fromLatin1( label ) );
            }
        }
        if ( m_tool == map_tool_t::SELECT ) {
            // A mode profile exists independently of the selected geometry.
            // Show its configured shortcuts with the same effective lookup
            // as the views. Placeholder operations stay identified as planned
            // rather than promising an enabled edit gesture.
            for ( const tool_group_t &group : info.groups ) {
                if ( ( group.modes & ModeMask( m_mode ) ) == 0u || !group.namedOperations ) { continue; }
                for ( const char *id : group.commands ) {
                    QString label = ActionLabel( gui, QString::fromUtf8( id ) );
                    if ( ( m_mode == map_element_mode_t::VERTICES || m_mode == map_element_mode_t::EDGES ) &&
                         MapWorkspace_IsPlannedCommand( &gui.commands, StringView_FromCString( id ) ) ) {
                        label += QStringLiteral( " (planned)" );
                    }
                    addCommand( id, label );
                }
            }
        }
        if ( ( m_tool == map_tool_t::SELECT || m_tool == map_tool_t::TRANSLATE || m_tool == map_tool_t::ROTATE || m_tool == map_tool_t::SCALE ) &&
             rootMode ) {
            for ( const auto &[command, label] : {
                      std::pair{ "map.nudge.left", "Nudge left by the authored grid step (2D)" },
                      std::pair{ "map.nudge.right", "Nudge right by the authored grid step (2D)" },
                      std::pair{ "map.nudge.up", "Nudge up by the authored grid step (2D)" },
                      std::pair{ "map.nudge.down", "Nudge down by the authored grid step (2D)" },
                      std::pair{ "map.nudge_fine.left", "Nudge left by one unit (2D)" },
                      std::pair{ "map.nudge_fine.right", "Nudge right by one unit (2D)" },
                      std::pair{ "map.nudge_fine.up", "Nudge up by one unit (2D)" },
                      std::pair{ "map.nudge_fine.down", "Nudge down by one unit (2D)" } } ) {
                if ( EditorCommands_Find( &gui.commands, StringView_FromCString( command ) ) == nullptr ) { continue; }
                const auto keys = MapInput_CommandBindings( m_pWorkspace, command, false );
                if ( !keys.isEmpty() ) {
                    addRow( QStringLiteral( "[%1]" ).arg( keys.join( QStringLiteral( " / " ) ) ), QString::fromLatin1( label ) );
                }
                listedCommands.append( QString::fromLatin1( command ) );
            }
        }
        if ( m_tool == map_tool_t::SELECT ) {
            if ( m_mode == map_element_mode_t::VERTICES ) {
                addRow( QStringLiteral( "[LeftClick]" ), QStringLiteral( "Pick an authored mesh vertex" ) );
                addRow( QStringLiteral( "[Shift+LeftClick]" ), QStringLiteral( "Add a vertex on the same mesh" ) );
                addRow( QStringLiteral( "[Ctrl/Command+LeftClick]" ), QStringLiteral( "Toggle a vertex on the same mesh" ) );
            }
            if ( m_mode == map_element_mode_t::EDGES ) {
                addRow( QStringLiteral( "[LeftClick]" ), QStringLiteral( "Pick an authored mesh edge" ) );
                addRow( QStringLiteral( "[LeftDrag]" ), QStringLiteral( "Edge drag action: Move deforms; Extend grows a connected face" ) );
                addRow( QStringLiteral( "[Alt during drag]" ), QStringLiteral( "3D direct edge drag: raise / lower instead of sliding across the horizontal plane" ) );
                addRow( QStringLiteral( "[Shift+LeftClick]" ), QStringLiteral( "Add an edge on the same mesh" ) );
                addRow( QStringLiteral( "[Ctrl/Command+LeftClick]" ), QStringLiteral( "Toggle an edge on the same mesh" ) );
            }
            if ( m_mode == map_element_mode_t::FACES ) {
                addRow( QStringLiteral( "[Shift+LeftClick]" ), QStringLiteral( "Add a face of the same mesh; it becomes the active face" ) );
                addRow( QStringLiteral( "[Ctrl/Command+LeftClick]" ), QStringLiteral( "Remove a selected mesh face" ) );
            }
            addRow( QStringLiteral( "[Double LeftClick]" ), QStringLiteral( "Object: inspect properties" ) );
            addRow( QStringLiteral( "[Double LeftClick]" ), QStringLiteral( "Empty space: view options" ) );
            if ( brushFace ) {
                addRow( QStringLiteral( "[LeftDrag]" ), QStringLiteral( "Brush face-normal handle: push / pull by the grid step (2D / 3D)" ) );
                addRow( QStringLiteral( "[Ctrl/Command+LeftDrag]" ), QStringLiteral( "Brush face-normal handle: temporarily bypass snapping" ) );
            }
            if ( rootMode ) {
                const QString roots = m_mode == map_element_mode_t::MESHES ? QStringLiteral( "Meshes" ) : QStringLiteral( "Objects/Groups" );
                addRow( QStringLiteral( "[LeftDrag]" ), roots + QStringLiteral( ": move a selected object" ) );
                addRow( QStringLiteral( "[LeftDrag]" ), roots + QStringLiteral( " RGB arrow: move along its axis" ) );
                addRow( QStringLiteral( "[LeftDrag]" ), roots + QStringLiteral( " plane square: move along two axes" ) );
                addRow( QStringLiteral( "[LeftDrag]" ), roots + QStringLiteral( " center handle: move in the view plane" ) );
                addRow( QStringLiteral( "[LeftDrag]" ), QStringLiteral( "2D bounds side/corner: resize using the captured anchor" ) );
                addRow( QStringLiteral( "[LeftDrag]" ), QStringLiteral( "3D RGB bounds handle: resize along its world axis" ) );
                addRow( QStringLiteral( "[Shift+LeftDrag]" ), QStringLiteral( "Bounds handle: resize both sides around the center" ) );
                addRow( QStringLiteral( "[Shift+LeftDrag]" ), QStringLiteral( "Move handle: clone and move" ) );
                addRow( QStringLiteral( "[Ctrl/Command+LeftDrag]" ), QStringLiteral( "Move/resize handle: temporarily bypass grid snapping" ) );
            }
        }
        if ( m_tool == map_tool_t::NONE ) {
            // Orthographic panning and wheel zoom are fixed view gestures,
            // including bare LMB in Navigation; they do not use the 3D
            // camera mouse-binding resolver below.
            addRow( QStringLiteral( "[LeftDrag / MiddleDrag / Space+LeftDrag]" ), QStringLiteral( "2D: pan the view" ) );
            addRow( QStringLiteral( "[Wheel]" ), QStringLiteral( "2D: zoom the view" ) );
        }
        const bool navigationReference = m_tool == map_tool_t::SELECT || m_tool == map_tool_t::CAMERA || m_tool == map_tool_t::NONE;
        if ( navigationReference ) {
            // Destination and framing actions live in the general map
            // context. Resolve this small navigation set explicitly rather
            // than filling the Camera reference with every map command.
            for ( const char *id : { "map.go_to", "map.view.frame_all", "map.view.center_selection_2d", "map.view.center_selection_3d",
                                    "map.camera.speed_increase", "map.camera.speed_decrease", "map.camera.speed_reset" } ) {
                addCommand( id, ActionLabel( gui, QString::fromUtf8( id ) ) );
            }
            // These hints use the same effective resolver as the viewport.
            // Raw mouse entries can be unbound, shadowed, or unsupported.
            for ( const auto &[gesture, label] : {
                      std::pair{ map_camera_gesture_t::LOOK, "3D: look around" },
                      std::pair{ map_camera_gesture_t::ORBIT, "3D: orbit the selection or view center" },
                      std::pair{ map_camera_gesture_t::PAN, "3D: pan in the view plane" },
                      std::pair{ map_camera_gesture_t::DOLLY, "3D: move forward / backward" } } ) {
                const auto texts = MapInput_CameraGestureBindings( m_pWorkspace, gesture );
                if ( !texts.isEmpty() ) {
                    addRow( QStringLiteral( "[%1]" ).arg( texts.join( QStringLiteral( " / " ) ) ), QString::fromLatin1( label ) );
                }
            }
            for ( const bool increase : { true, false } ) {
                const auto texts = MapInput_CameraLookWheelBindings( m_pWorkspace, increase );
                if ( !texts.isEmpty() ) {
                    addRow( QStringLiteral( "[%1]" ).arg( texts.join( QStringLiteral( " / " ) ) ),
                        increase ? QStringLiteral( "During 3D look: increase flight speed" ) : QStringLiteral( "During 3D look: decrease flight speed" ) );
                }
            }
            for ( const auto &[flag, label] : {
                      std::pair{ MAP_NAVIGATION_FORWARD, "Camera forward" }, std::pair{ MAP_NAVIGATION_BACK, "Camera backward" },
                      std::pair{ MAP_NAVIGATION_LEFT, "Camera left" }, std::pair{ MAP_NAVIGATION_RIGHT, "Camera right" },
                      std::pair{ MAP_NAVIGATION_UP, "Camera up" }, std::pair{ MAP_NAVIGATION_DOWN, "Camera down" },
                      std::pair{ MAP_NAVIGATION_FAST, "Camera faster" }, std::pair{ MAP_NAVIGATION_SLOW, "Camera slower" } } ) {
                const auto texts = MapInput_NavigationBindings( m_pWorkspace, flag );
                if ( !texts.isEmpty() ) {
                    addRow( QStringLiteral( "[%1]" ).arg( texts.join( QStringLiteral( " / " ) ) ), QString::fromLatin1( label ) );
                }
            }
        }
        if ( m_tool == map_tool_t::BLOCK ) {
            const bool quad = MapWorkspace_HasBlockPreview( m_pWorkspace ) ? m_pWorkspace->editPreview.primitive.kind == map_primitive_kind_t::QUAD :
                MapWorkspace_PrimitiveDefaults( m_pWorkspace, {} ).kind == map_primitive_kind_t::QUAD;
            addRow( QStringLiteral( "[LeftDrag]" ), quad ? QStringLiteral( "Draw a flat Quad in this pane's construction plane" ) : QStringLiteral( "Draw footprint; release to stage a private brush" ) );
            addRow( QStringLiteral( "[LeftDrag]" ), QStringLiteral( "2D bounds side/corner: resize the staged brush" ) );
            addRow( QStringLiteral( "[Shift+LeftDrag]" ), QStringLiteral( "Bounds handle: resize the staged brush around its center" ) );
            addRow( QStringLiteral( "[LeftDrag]" ), QStringLiteral( "3D RGB bounds handle: adjust its width, height, or depth" ) );
            addRow( QStringLiteral( "[Ctrl/Command+LeftDrag]" ), QStringLiteral( "Staged bounds handle: temporarily bypass grid snapping" ) );
            if ( !quad ) {
                addRow( QStringLiteral( "[Shift+Wheel]" ), QStringLiteral( "Adjust original footprint depth by the grid step, in any pane" ) );
                addRow( QStringLiteral( "[Ctrl+Shift+Wheel]" ), QStringLiteral( "Adjust depth by one unit" ) );
            }
        }
        if ( m_tool == map_tool_t::CLIP ) {
            addRow( QStringLiteral( "[LeftDrag]" ), QStringLiteral( "2D: stage clipping plane; release to preview" ) );
            addRow( QStringLiteral( "[LeftDrag]" ), QStringLiteral( "3D on any face (Surface placement): draw the cut across it" ) );
            addRow( QStringLiteral( "[LeftClick] ×2" ), QStringLiteral( "3D: point 1, then point 2; near a vertex they lock onto it" ) );
            addRow( QStringLiteral( "[LeftClick]" ), QStringLiteral( "3D, cut staged: point 3 tilts the cut through all three" ) );
            addRow( QStringLiteral( "[Backspace]" ), QStringLiteral( "3D: take point 3 back" ) );
            addRow( QStringLiteral( "[Ctrl+LeftClick]" ), QStringLiteral( "3D: place a point freely (no vertex or grid snap)" ) );
            addRow( QStringLiteral( "[LeftDrag]" ), QStringLiteral( "3D off every surface: draw on the plane perpendicular to the 3D plane axis" ) );
            addRow( QStringLiteral( "[LeftDrag]" ), QStringLiteral( "Staged endpoint: move it; release to preview" ) );
        }
        if ( m_tool == map_tool_t::TRANSLATE || m_tool == map_tool_t::ROTATE || m_tool == map_tool_t::SCALE ) {
            addRow( QStringLiteral( "[LeftDrag]" ), m_tool == map_tool_t::TRANSLATE ? QStringLiteral( "Move selection" ) :
                    m_tool == map_tool_t::ROTATE ? QStringLiteral( "Rotate selection" ) : QStringLiteral( "Scale selection" ) );
            if ( m_tool == map_tool_t::TRANSLATE ) {
                addRow( QStringLiteral( "[LeftDrag]" ), rootMode ? QStringLiteral( "RGB axis, plane square, or center: constrain movement" ) :
                    QStringLiteral( "RGB arrow: move along that axis; center: free movement" ) );
                if ( m_pWorkspace->elementMode == map_element_mode_t::FACES ) {
                    addRow( QStringLiteral( "[Shift+LeftDrag]" ), QStringLiteral( "Mesh face: extrude a new section" ) );
                } else if ( m_pWorkspace->elementMode == map_element_mode_t::OBJECTS || m_pWorkspace->elementMode == map_element_mode_t::MESHES ) {
                    addRow( QStringLiteral( "[Shift+LeftDrag]" ), QStringLiteral( "Clone and move" ) );
                }
            }
            if ( m_pWorkspace->elementMode == map_element_mode_t::EDGES ) {
                addRow( QStringLiteral( "[Shift+LeftDrag]" ), QStringLiteral( "Open edge: extend new faces; the new edge stays selected" ) );
                addRow( QStringLiteral( "[LeftDrag]" ), QStringLiteral( "Edge drag action: Move deforms; Extend grows connected faces" ) );
            }
            if ( m_tool == map_tool_t::ROTATE ) {
                addRow( QStringLiteral( "[LeftDrag]" ), QStringLiteral( "RGB ring: rotate about that axis; Pivot tool sets the centre" ) );
            }
            addRow( QStringLiteral( "[Ctrl+LeftDrag]" ), QStringLiteral( "Temporarily bypass snapping" ) );
        }
        const bool editingGestures = m_tool == map_tool_t::SELECT || m_tool == map_tool_t::BLOCK || m_tool == map_tool_t::TRANSLATE ||
            m_tool == map_tool_t::ROTATE || m_tool == map_tool_t::SCALE || m_tool == map_tool_t::EXTRUDE || m_tool == map_tool_t::CLIP;
        if ( MapWorkspace_IsToolAvailable( m_tool ) ) {
            for ( const auto &[gesture, label] : { std::pair{ map_tool_gesture_key_t::CONFIRM, "Confirm current gesture" },
                                                   std::pair{ map_tool_gesture_key_t::CANCEL, "Cancel current gesture" } } ) {
                if ( gesture == map_tool_gesture_key_t::CONFIRM && !editingGestures ) { continue; }
                const auto keys2d = MapInput_ToolGestureBindings( m_pWorkspace, gesture, false );
                const auto keys3d = MapInput_ToolGestureBindings( m_pWorkspace, gesture, true );
                const auto addGesture = [&]( const QStringList &texts, const QString &suffix ) {
                    if ( !texts.isEmpty() ) {
                        const QString operation = m_tool == map_tool_t::BLOCK ?
                            ( gesture == map_tool_gesture_key_t::CONFIRM ? QStringLiteral( "Create staged primitive (one undo step)" ) :
                                QStringLiteral( "Discard footprint/staged primitive; idle: enter Navigation (keep selection)" ) ) :
                            gesture != map_tool_gesture_key_t::CANCEL ? QString::fromLatin1( label ) :
                            m_tool == map_tool_t::NONE ? QStringLiteral( "Clear preserved selection" ) :
                            m_tool == map_tool_t::CLIP ? QStringLiteral( "Cancel drag/stage; idle: enter Navigation (keep selection)" ) :
                            QStringLiteral( "Cancel drag; idle: enter Navigation (keep selection)" );
                        addRow( QStringLiteral( "[%1]" ).arg( texts.join( QStringLiteral( " / " ) ) ), operation + suffix );
                    }
                };
                if ( keys2d == keys3d ) { addGesture( keys2d, {} ); }
                else { addGesture( keys2d, QStringLiteral( " (2D)" ) ); addGesture( keys3d, QStringLiteral( " (3D)" ) ); }
            }
            if ( editingGestures ) {
                addRow( QStringLiteral( "[Focus loss]" ), m_tool == map_tool_t::BLOCK ?
                        QStringLiteral( "Cancel active drag; idle stage retained" ) : m_tool == map_tool_t::CLIP ?
                        QStringLiteral( "Cancel unreleased drag; staged plane retained" ) : QStringLiteral( "Cancel current gesture" ) );
            }
        }
        for ( const key_source_t &source : info.keys ) {
            if ( source.pContext == nullptr ) { continue; }
            for ( const QString &id : IdsIn( gui, source.section, source.pContext ) ) {
                // Logical gesture keys are reported above after full input
                // resolution, never from a raw tool or viewport section.
                if ( id == "map.tool.cancel" || id == "map.tool.confirm" ) { continue; }
                // Nudge execution is restricted to object/group editing in
                // orthographic views, even when a user binds it elsewhere.
                // Its eligible 2D reference is owned by the list above.
                if ( id.startsWith( QStringLiteral( "map.nudge." ) ) ||
                     id.startsWith( QStringLiteral( "map.nudge_fine." ) ) ) { continue; }
                if ( navigationReference && id.startsWith( QStringLiteral( "map.camera." ) ) ) { continue; }
                // The keymap also reserves gestures for future tools. An
                // active tool's reference must only promise working input.
                if ( navigationReference ) {
                    if ( id == "map.select.cycle" || id == "map.transform.drag" ||
                         id == "map.transform.clone_drag" ||
                         id == "map.camera.orbit" || id == "map.camera.pan" ||
                         id == "map.camera.mouselook" ) { continue; }
                }
                if ( source.section == keymap_section_t::BINDINGS ) {
                    const QByteArray commandId = id.toUtf8();
                    addCommand( commandId.constData(), ActionLabel( gui, id ) );
                    continue;
                }
                const QByteArray idUtf8 = id.toUtf8();
                if ( EditorKeymap_FindTriggers( gui.keymapChain, gui.nKeymapChain, source.section, EditorKeymap_HostPlatform(),
                                                StringView_FromCString( source.pContext ),
                                                string_view_t{ idUtf8.constData(), static_cast<usize>( idUtf8.size() ) },
                                                &triggers ) != keymap_lookup_t::BOUND ) {
                    continue;
                }
                QStringList texts;
                for ( usize t = 0u; t < triggers.nTexts; ++t ) {
                    const QString trigger = FromView( triggers.texts[t] );
                    if ( id == "map.view.pan" && trigger == "RightDrag" ) { continue; }
                    texts.append( trigger );
                }
                if ( texts.empty() ) { continue; }
                addRow( QStringLiteral( "[%1]" ).arg( texts.join( QStringLiteral( " / " ) ) ), ActionLabel( gui, id ) );
            }
        }
        if ( m_pWorkspace->tool == map_tool_t::EXTRUDE ) {
            addRow( QStringLiteral( "[LMB]" ), QStringLiteral( "Pick a brush or authored mesh face" ) );
            addRow( QStringLiteral( "[LMB drag]" ), QStringLiteral( "3D brush face: push / pull" ) );
            addRow( QStringLiteral( "[Ctrl/Command+LMB drag]" ), QStringLiteral( "Brush push / pull: temporarily bypass snapping" ) );
        }
        if ( pKeys->topLevelItemCount() == 0 ) { new QTreeWidgetItem( pKeys, { QString(), QStringLiteral( "No keys bound" ) } ); }
        pKeys->resizeColumnToContents( 0 );
        pKeys->setColumnWidth( 0, std::min( pKeys->columnWidth( 0 ) + 8, 128 ) );
        // Show the reference by default without a long navigation table
        // displacing every parameter. Remaining rows scroll inside it.
        const int rowHeight = std::max( pKeys->sizeHintForRow( 0 ), 18 );
        pKeys->setFixedHeight( pKeys->header()->sizeHint().height() + rowHeight * std::min( pKeys->topLevelItemCount(), 7 ) + 4 );
        pKeys->setVerticalScrollBarPolicy( Qt::ScrollBarAsNeeded );
        pKeys->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
        pKeys->setSelectionMode( QAbstractItemView::NoSelection );
        pKeys->setEditTriggers( QAbstractItemView::NoEditTriggers );
    }

    void Rebuild()
    {
        const map_workspace_t &workspace = *m_pWorkspace;
        gui::editor_gui_t &gui = *workspace.pGui;
        m_tool = workspace.tool;
        m_mode = workspace.elementMode;
        const tool_panel_t &info = kToolPanels[static_cast<usize>( m_tool )];
        m_buttons.clear();
        m_sections.clear();
        m_optionInventory.clear(); m_operationInventory.clear();
        m_geometryControls.clear(); m_curveControls.clear();
        m_pTerrainControls = nullptr;
        m_pTextureState = nullptr; m_pTextureStateSection = nullptr; m_pFaceEditSection = nullptr; m_pFaceEdit = nullptr;
        m_pFaceControls = nullptr;
        m_pBrushFaceSection = nullptr; m_lastBrushFace = 0;
        m_pMeshFaceControls = nullptr; m_pMeshFaceSection = nullptr; m_lastMeshFace = 0;
        m_pClipControls = nullptr;
        m_pSubtractControls = nullptr;
        m_pToolIcon = nullptr;
        m_pSelection = nullptr; m_pStartHint = nullptr;
        m_keyRows.clear();
        m_pPrimitiveControls = nullptr;
        m_pBlockDepthControl = nullptr; m_pBlockDepthForm = nullptr;
        // Release the previous controls' subscriptions before registering the
        // replacements. Keeping both trees alive until setWidget() would
        // temporarily consume two tools' worth of settings listener slots.
        delete m_pScroll->takeWidget();
        const QByteArray toolId = ToolCommandId( m_tool ).toUtf8();
        const command_desc_t *pToolCommand = EditorCommands_Find( &gui.commands, string_view_t{ toolId.constData(), static_cast<usize>( toolId.size() ) } );
        m_title = pToolCommand != nullptr ? QString::fromUtf8( pToolCommand->pLabel ) : QString::fromUtf8( MapWorkspace_ToolName( m_tool ) );

        auto *pContent = new QWidget();
        auto *pLayout = new QVBoxLayout( pContent );
        pLayout->setContentsMargins( 0, 0, 0, 0 );
        pLayout->setSpacing( 0 );

        // The active tool stays readable above its parameter groups.
        auto *pSummaryBody = new QWidget( pContent );
        pSummaryBody->setObjectName( QStringLiteral( "MapToolSummary" ) );
        auto *pSummaryLayout = new QVBoxLayout( pSummaryBody );
        pSummaryLayout->setContentsMargins( 6, 6, 6, 6 );
        pSummaryLayout->setSpacing( 4 );
        auto *pTitleRow = new QHBoxLayout();
        m_pToolIcon = new QLabel( pSummaryBody );
        m_pToolIcon->setObjectName( QStringLiteral( "MapToolIcon" ) );
        m_pToolIcon->setFixedSize( 26, 26 );
        if ( pToolCommand != nullptr && pToolCommand->pIcon != nullptr ) {
            m_pToolIcon->setPixmap( gui::EditorStyle_Icon( gui.style, pToolCommand->pIcon ).pixmap( 24, 24 ) );
        }
        auto *pTitle = new QLabel( m_title, pSummaryBody );
        pTitle->setObjectName( QStringLiteral( "MapToolTitle" ) );
        pTitle->setWordWrap( true );
        QFont titleFont = pTitle->font();
        titleFont.setBold( true );
        pTitle->setFont( titleFont );
        pTitleRow->addWidget( m_pToolIcon );
        pTitleRow->addWidget( pTitle, 1 );
        pSummaryLayout->addLayout( pTitleRow );
        // Hammer's panels explain nothing in place; the summary is the
        // title's tooltip, and the key table below says how to use the tool.
        pTitle->setToolTip( QString::fromUtf8( info.pSummary ) );
        m_pToolIcon->setToolTip( QString::fromUtf8( info.pSummary ) );
        m_pSelection = new QLabel( pSummaryBody );
        m_pSelection->setObjectName( QStringLiteral( "MapToolSelection" ) );
        m_pSelection->setProperty( "muted", true );
        m_pSelection->setWordWrap( true );
        pSummaryLayout->addWidget( m_pSelection );
        // How to start when a component mode has nothing picked yet: the
        // buttons below wait for a selection, and this says how to make one.
        m_pStartHint = new QLabel( pSummaryBody );
        m_pStartHint->setObjectName( QStringLiteral( "MapToolStartHint" ) );
        m_pStartHint->setWordWrap( true );
        m_pStartHint->setStyleSheet( QStringLiteral( "color: #e3b341;" ) );
        pSummaryLayout->addWidget( m_pStartHint );
        if ( m_tool == map_tool_t::NONE ) {
            auto *pNote = new QLabel( QStringLiteral( "No editing tool active. Selection is retained for inspection." ), pSummaryBody );
            pNote->setObjectName( QStringLiteral( "MapToolNavigationStatus" ) );
            pNote->setProperty( "muted", true );
            pNote->setWordWrap( true );
            pSummaryLayout->addWidget( pNote );
        }
        if ( !MapWorkspace_IsToolAvailable( m_tool ) ) {
            auto *pNote = new QLabel( QStringLiteral( "Not connected to the views yet." ), pSummaryBody );
            pNote->setToolTip( QStringLiteral( "The settings below already apply as defaults; dimmed operations are unavailable." ) );
            pNote->setObjectName( QStringLiteral( "MapToolUnavailable" ) );
            pNote->setWordWrap( true );
            pSummaryLayout->addWidget( pNote );
        }
        pLayout->addWidget( pSummaryBody );

        if ( m_tool == map_tool_t::BLOCK ) {
            m_pPrimitiveControls = new primitive_controls_t( pContent, m_pWorkspace );
            auto *section = gui::EditorSection_Create( pContent, QStringLiteral( "Primitive" ), m_pPrimitiveControls );
            pLayout->addWidget( section );
            for ( const char *path : { "editor.map.new_brush_shape", "editor.map.primitive_axis", "editor.map.cylinder_sides", "editor.map.cone_top_radius", "editor.map.sphere_subdivisions",
                                       "editor.map.arch_thickness", "editor.map.arch_angle", "editor.map.arch_start", "editor.map.stairs_steps", "editor.map.stairs_direction",
                                       "editor.map.spiral_turn", "editor.map.spiral_inner", "editor.map.torus_tube" } ) {
                m_optionInventory.push_back( { QString::fromLatin1( path ), kAllModes } );
            }
        }

        if ( m_tool == map_tool_t::DISPLACE ) {
            pLayout->addWidget( gui::EditorSection_Create( pContent, QStringLiteral( "Brush and falloff" ), new displace_controls_t( pContent, m_pWorkspace ) ) );
        }

        if ( m_tool == map_tool_t::ENTITY ) {
            pLayout->addWidget( gui::EditorSection_Create( pContent, QStringLiteral( "Entity Classes" ), new entity_classes_t( pContent, m_pWorkspace ) ) );
            m_operationInventory.push_back( { QStringLiteral( "map.game.reload" ), kAllModes } );
        }

        if ( m_tool == map_tool_t::TERRAIN ) {
            m_pTerrainControls = new terrain_controls_t( pContent, m_pWorkspace );
            pLayout->addWidget( gui::EditorSection_Create( pContent, QStringLiteral( "Create Terrain" ), m_pTerrainControls ) );
            m_operationInventory.push_back( { QStringLiteral( "map.terrain.create" ), kAllModes } );
        }

        // Faces show the selected authored surface first; geometry defaults
        // remain separate and cannot be mistaken for its current UV values.
        if ( m_tool == map_tool_t::SELECT || m_tool == map_tool_t::EXTRUDE || m_tool == map_tool_t::TEXTURE ||
             m_tool == map_tool_t::TRANSLATE || m_tool == map_tool_t::ROTATE || m_tool == map_tool_t::SCALE ) {
            m_pTextureState = new face_texture_state_t( pContent, m_pWorkspace );
            m_pTextureStateSection = gui::EditorSection_Create( pContent, QStringLiteral( "Texture State" ), m_pTextureState );
            m_pTextureStateSection->setObjectName( QStringLiteral( "MapFaceTextureSection" ) ); pLayout->addWidget( m_pTextureStateSection );
            // Hammer's Modify Texture: scale, shift, rotate, justify, align.
            if ( QWidget *pFaceEdit = MapFaceEdit_Create( pContent, m_pWorkspace ) ) {
                m_pFaceEdit = pFaceEdit;
                m_pFaceEditSection = gui::EditorSection_Create( pContent, QStringLiteral( "Texture" ), pFaceEdit );
                m_pFaceEditSection->setObjectName( QStringLiteral( "MapFaceEditSection" ) ); pLayout->addWidget( m_pFaceEditSection );
            }
        }

        // Keys: Hammer lists every key the tool answers to, with the key first.
        auto *pKeys = new QTreeWidget();
        pKeys->setObjectName( QStringLiteral( "MapToolKeys" ) );
        pKeys->setColumnCount( 2 );
        pKeys->setHeaderLabels( { QStringLiteral( "Key" ), QStringLiteral( "Operation" ) } );
        pKeys->setRootIsDecorated( false );
        pKeys->setUniformRowHeights( true );
        pKeys->setAlternatingRowColors( true );
        // Long lists of alternate gestures elide; their complete bindings
        // remain available in the tooltip without shrinking the text.
        pKeys->header()->setStretchLastSection( true );
        pKeys->header()->setSectionResizeMode( 0, QHeaderView::Interactive );
        pKeys->setTextElideMode( Qt::ElideRight );
        RefreshKeys( pKeys );
        QWidget *pShortcuts = gui::EditorSection_Create( pContent, QStringLiteral( "Key reference" ), pKeys, true );
        pShortcuts->setObjectName( QStringLiteral( "MapToolShortcuts" ) );
        pLayout->addWidget( pShortcuts );

        // Exact values complement the viewport handles without changing
        // the tool strip or replacing the document's canonical geometry.
        if ( m_tool == map_tool_t::BLOCK || m_tool == map_tool_t::TRANSLATE || m_tool == map_tool_t::ROTATE || m_tool == map_tool_t::SCALE ) {
            const numeric_edit_t kind = m_tool == map_tool_t::BLOCK ? numeric_edit_t::BOX : m_tool == map_tool_t::TRANSLATE ? numeric_edit_t::TRANSLATE :
                                        m_tool == map_tool_t::ROTATE ? numeric_edit_t::ROTATE : numeric_edit_t::SCALE;
            const QString title = m_tool == map_tool_t::BLOCK ? QStringLiteral( "Dimensions" ) : QStringLiteral( "Numeric transform" );
            auto *pNumeric = new geometry_controls_t( pContent, m_pWorkspace, kind, false );
            m_geometryControls.push_back( pNumeric );
            if ( m_pPrimitiveControls != nullptr ) {
                m_pPrimitiveControls->SetRefreshCallback( []( void *context ) {
                    auto *panel = static_cast<tool_properties_t *>( context );
                    panel->RefreshButtons();
                    panel->RefreshKeys( panel->findChild<QTreeWidget *>( QStringLiteral( "MapToolKeys" ) ) );
                }, this );
            }
            auto *section = gui::EditorSection_Create( pContent, title, pNumeric );
            if ( m_tool == map_tool_t::BLOCK ) { pLayout->insertWidget( 2, section ); }
            else { pLayout->addWidget( section ); }
        }
        if ( m_tool == map_tool_t::SELECT || m_tool == map_tool_t::EXTRUDE || m_tool == map_tool_t::TEXTURE ) {
            m_pFaceControls = new face_controls_t( pContent, m_pWorkspace );
            m_pBrushFaceSection = gui::EditorSection_Create( pContent, QStringLiteral( "Brush face" ), m_pFaceControls,
                m_tool != map_tool_t::SELECT && !MapWorkspace_HasMeshFace( m_pWorkspace ) );
            pLayout->addWidget( m_pBrushFaceSection );
        }
        if ( m_tool == map_tool_t::SELECT || m_tool == map_tool_t::EXTRUDE ) {
            m_pMeshFaceControls = new mesh_face_controls_t( pContent, m_pWorkspace );
            m_pMeshFaceSection = gui::EditorSection_Create( pContent, QStringLiteral( "Mesh face" ), m_pMeshFaceControls, m_tool == map_tool_t::EXTRUDE || MapWorkspace_HasMeshFace( m_pWorkspace ) );
            pLayout->addWidget( m_pMeshFaceSection );
            for ( const char *path : { "editor.map.mesh_extrude_distance", "editor.map.mesh_inset_distance", "editor.map.mesh_inset_width", "editor.map.mesh_slice_u", "editor.map.mesh_slice_v" } ) {
                m_optionInventory.push_back( { QString::fromLatin1( path ), m_tool == map_tool_t::SELECT ? kFaceMode : kAllModes } );
            }
            for ( const char *id : { "map.mesh.extrude", "map.mesh.inset", "map.mesh.inset_width", "map.mesh.quad_slice" } ) {
                m_operationInventory.push_back( { QString::fromLatin1( id ), m_tool == map_tool_t::SELECT ? kFaceMode : kAllModes } );
            }
        }
        if ( m_tool == map_tool_t::CLIP ) {
            m_pClipControls = new clip_controls_t( pContent, m_pWorkspace );
            pLayout->addWidget( gui::EditorSection_Create( pContent, QStringLiteral( "Brush clipping plane" ), m_pClipControls ) );
        }
        if ( m_tool == map_tool_t::SELECT || m_tool == map_tool_t::BLOCK ) {
            m_pSubtractControls = new subtract_controls_t( pContent, m_pWorkspace );
            pLayout->addWidget( gui::EditorSection_Create( pContent, QStringLiteral( "Brush subtraction" ), m_pSubtractControls, false ) );
        }

        // Each group combines real registered settings and command buttons.
        // No numeric field pretends to be a selected-face transform.
        for ( const tool_group_t &group : info.groups ) {
            if ( m_tool == map_tool_t::BLOCK && QLatin1StringView( group.pTitle ) == QLatin1StringView( "Primitive" ) ) { continue; }
            auto *pBody = new QWidget();
            pBody->setObjectName( QStringLiteral( "MapToolParameterGroup" ) );
            pBody->setProperty( "toolGroup", QString::fromUtf8( group.pTitle ) );
            auto *pGroupLayout = new QVBoxLayout( pBody );
            pGroupLayout->setContentsMargins( 6, 5, 6, 6 );
            pGroupLayout->setSpacing( 5 );
            auto *pForm = new QFormLayout();
            pForm->setContentsMargins( 0, 0, 0, 0 );
            pForm->setHorizontalSpacing( 6 );
            pForm->setVerticalSpacing( 4 );
            pForm->setFieldGrowthPolicy( QFormLayout::AllNonFixedFieldsGrow );
            pForm->setRowWrapPolicy( QFormLayout::WrapLongRows );
            int controls = 0;
            QStringList groupOptions, groupOperations;
            for ( const char *pPath : group.settings ) {
                if ( QWidget *pControl = gui::EditorSettingControl_Create( pBody, &gui.settings, pPath ) ) {
                    pControl->setMinimumWidth( 52 );
                    pForm->addRow( gui::EditorSettingControl_Label( &gui.settings, pPath ), pControl );
                    if ( m_tool == map_tool_t::CURVE ) { m_curveControls.push_back( { pControl, pForm } ); }
                    if ( m_tool == map_tool_t::BLOCK && QLatin1StringView( pPath ) == QLatin1StringView( "editor.map.block_depth" ) ) {
                        m_pBlockDepthControl = pControl; m_pBlockDepthForm = pForm;
                    }
                    groupOptions.append( QString::fromUtf8( pPath ) );
                    ++controls;
                }
            }
            if ( controls != 0 ) { pGroupLayout->addLayout( pForm ); }
            else { delete pForm; }
            auto *pOperations = new QGridLayout();
            pOperations->setContentsMargins( 0, 0, 0, 0 );
            pOperations->setSpacing( 3 );
            pOperations->setAlignment( group.namedOperations ? Qt::AlignTop : Qt::AlignLeft );
            if ( group.namedOperations ) { pOperations->setColumnStretch( 0, 1 ); }
            int operations = 0;
            for ( const char *pId : group.commands ) {
                const command_desc_t *pCommand = EditorCommands_Find( &gui.commands, StringView_FromCString( pId ) );
                if ( pCommand == nullptr ) { continue; }
                auto *pButton = new QToolButton( pBody );
                pButton->setObjectName( QString::fromUtf8( pId ) );
                pButton->setProperty( "toolOperation", true );
                pButton->setAccessibleName( QString::fromUtf8( pCommand->pLabel ) );
                pButton->setText( QString::fromUtf8( pCommand->pLabel ) );
                if ( group.modes == kEdgeMode && QLatin1StringView( pId ) == QLatin1StringView( "map.mesh.merge" ) ) {
                    pButton->setText( QStringLiteral( "Merge" ) ); pButton->setAccessibleName( pButton->text() );
                }
                pButton->setCheckable( ( pCommand->flags & COMMAND_FLAG_CHECKABLE ) != 0u );
                pButton->setIconSize( QSize( 20, 20 ) );
                const bool hasIcon = pCommand->pIcon != nullptr;
                if ( group.namedOperations ) {
                    if ( hasIcon ) { pButton->setIcon( gui::EditorStyle_Icon( gui.style, pCommand->pIcon ) ); }
                    pButton->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
                    pButton->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
                    pButton->setMinimumHeight( 30 );
                    pButton->setProperty( "namedToolOperation", true );
                    pOperations->addWidget( pButton, operations, 0 );
                } else if ( hasIcon ) {
                    pButton->setIcon( gui::EditorStyle_Icon( gui.style, pCommand->pIcon ) );
                    pButton->setToolButtonStyle( Qt::ToolButtonIconOnly );
                    pButton->setProperty( "compactIcon", true ); // Padding must not grow into the icon.
                    pButton->setFixedSize( 20 + 10, 20 + 10 );
                    pOperations->addWidget( pButton, operations / 6, operations % 6 );
                } else {
                    // Some existing view commands have no icon. Their full
                    // name is preferable to a blank or unrelated symbol.
                    pButton->setToolButtonStyle( Qt::ToolButtonTextOnly );
                    pOperations->addWidget( pButton, ( operations + 5 ) / 6, 0, 1, 6 );
                    operations = ( operations + 5 ) / 6 * 6 + 5;
                }
                const QString description = QString::fromUtf8( pCommand->pDescription != nullptr ? pCommand->pDescription : "" );
                pButton->setProperty( "commandHelp", QStringLiteral( "%1\n%2\n%3" ).arg( pButton->text(), description, QString::fromUtf8( pId ) ) );
                const QString id = QString::fromUtf8( pId );
                QObject::connect( pButton, &QToolButton::clicked, this, [this, id]() {
                    const QByteArray utf8 = id.toUtf8();
                    ( void )EditorCommands_Execute( &m_pWorkspace->pGui->commands, { utf8.constData(), static_cast<usize>( utf8.size() ) }, {} );
                    RefreshButtons();
                } );
                m_buttons.push_back( pButton );
                groupOperations.append( id );
                ++operations;
            }
            if ( operations != 0 ) { pGroupLayout->addLayout( pOperations ); }
            else { delete pOperations; }
            if ( std::any_of( group.commands.begin(), group.commands.end(), []( const char *pId ) { return QLatin1StringView( pId ) == QLatin1StringView( "map.brush.merge" ); } ) ) {
                auto *pHelp = new QLabel( QStringLiteral( "Merge: select exactly two visible brushes in the same layer and entity. Their union must be convex. The lowest-ID brush retains its name and custom properties; surviving source faces retain their materials and UVs. Undo restores both brushes." ), pBody );
                pHelp->setObjectName( QStringLiteral( "MapBrushMergeHelp" ) );
                pHelp->setWordWrap( true );
                pHelp->setProperty( "muted", true );
                pGroupLayout->addWidget( pHelp );
            }
            if ( controls != 0 || operations != 0 ) {
                if ( group.pNote != nullptr ) {
                    auto *note = new QLabel( QString::fromUtf8( group.pNote ), pBody );
                    note->setObjectName( QStringLiteral( "MapToolModeAvailability" ) ); note->setProperty( "muted", true ); note->setWordWrap( true );
                    pGroupLayout->insertWidget( 0, note );
                }
                const bool textureGroup = ( m_tool == map_tool_t::TEXTURE || m_tool == map_tool_t::SELECT ) && QLatin1StringView( group.pTitle ) == QLatin1StringView( "Modify Texture" );
                const bool plannedUv = textureGroup && MapWorkspace_IsPlannedCommand( &m_pWorkspace->pGui->commands, StringView_FromCString( "map.texture.fit" ) );
                auto *section = gui::EditorSection_Create( pContent, plannedUv ? QStringLiteral( "Modify Texture · planned" ) : QString::fromUtf8( group.pTitle ), pBody, !plannedUv );
                if ( group.pObjectName != nullptr ) { section->setObjectName( QString::fromUtf8( group.pObjectName ) ); }
                if ( plannedUv ) { section->setObjectName( QStringLiteral( "MapTexturePlannedOperations" ) ); section->setToolTip( QStringLiteral( "These UV operations are not connected to authored face edits yet." ) ); }
                else if ( textureGroup ) { section->setObjectName( QStringLiteral( "MapTextureOperations" ) ); }
                m_sections.push_back( { section, group.modes } );
                for ( const auto &path : groupOptions ) { m_optionInventory.push_back( { path, group.modes } ); }
                for ( const auto &id : groupOperations ) { m_operationInventory.push_back( { id, group.modes } ); }
                if ( group.pObjectName != nullptr && QLatin1StringView( group.pObjectName ) == QLatin1StringView( "MapEdgeDragSection" ) ) {
                    pLayout->insertWidget( 1, section );
                } else { pLayout->addWidget( section ); }
            } else { delete pBody; }
        }
        pLayout->addStretch( 1 );
        m_pScroll->setWidget( pContent );
        // Deliberately local: text/field editors, combo boxes, spin boxes,
        // property edits and separate dialogs retain their own Escape paths.
        for ( auto *button : pContent->findChildren<QAbstractButton *>() ) { button->installEventFilter( this ); }
        pKeys->installEventFilter( this );
        RefreshButtons();
    }

    void RefreshSelectionProfile()
    {
        const u32 mode = ModeMask( m_mode );
        for ( const auto &section : m_sections ) { section.pSection->setVisible( ( section.modes & mode ) != 0u ); }
        if ( m_tool != map_tool_t::SELECT ) { return; }
        const selection_profile_t &profile = kSelectionProfiles[static_cast<usize>( m_mode )];
        m_title = QString::fromUtf8( profile.pTitle );
        if ( auto *title = findChild<QLabel *>( QStringLiteral( "MapToolTitle" ) ) ) {
            title->setText( m_title ); title->setToolTip( QString::fromUtf8( profile.pSummary ) );
        }
        if ( m_pToolIcon != nullptr ) {
            m_pToolIcon->setPixmap( gui::EditorStyle_Icon( m_pWorkspace->pGui->style, profile.pIcon ).pixmap( 24, 24 ) );
            m_pToolIcon->setToolTip( QString::fromUtf8( profile.pSummary ) );
        }
        // Keep component selectors, numeric drafts and subscriptions alive.
        // Only the containing section changes visibility; no signal sender
        // is replaced when its face selection changes the current mode.
        if ( m_pBrushFaceSection != nullptr ) { m_pBrushFaceSection->setVisible( m_mode == map_element_mode_t::FACES ); }
        if ( m_pMeshFaceSection != nullptr ) { m_pMeshFaceSection->setVisible( m_mode == map_element_mode_t::FACES ); }
        if ( m_pSubtractControls != nullptr ) { m_pSubtractControls->parentWidget()->setVisible( ( mode & kRootModes ) != 0u ); }
    }

    bool CurveOptionVisible( const QString &path ) const
    {
        const bool pipe = StringView_Equals( EditorSettings_Text( &m_pWorkspace->pGui->settings,
            "editor.map.curve_shape", {} ), StringView_FromCString( "hollow_pipe" ) );
        if ( path == QLatin1StringView( "editor.map.curve_outer_radius" ) || path == QLatin1StringView( "editor.map.curve_wall_thickness" ) ) { return pipe; }
        if ( !pipe ) { return true; }
        return path != QLatin1StringView( "editor.map.curve_profile" ) && path != QLatin1StringView( "editor.map.curve_width" ) &&
               path != QLatin1StringView( "editor.map.curve_height" ) && path != QLatin1StringView( "editor.map.wall_join" ) &&
               path != QLatin1StringView( "editor.map.lathe_angle" );
    }

    void RefreshButtons( bool documentChanged = false )
    {
        RefreshSelectionProfile();
        for ( const auto &control : m_curveControls ) { control.second->setRowVisible( control.first, CurveOptionVisible( control.first->objectName() ) ); }
        if ( m_pPrimitiveControls != nullptr ) { m_pPrimitiveControls->RefreshState(); }
        if ( m_pFaceEdit != nullptr ) { MapFaceEdit_Refresh( m_pFaceEdit ); }
        if ( m_pTextureState != nullptr ) {
            m_pTextureState->RefreshState();
            m_pTextureStateSection->setVisible( m_mode == map_element_mode_t::FACES || m_tool == map_tool_t::TEXTURE );
            if ( m_pFaceEditSection != nullptr ) { m_pFaceEditSection->setVisible( m_mode == map_element_mode_t::FACES || m_tool == map_tool_t::TEXTURE ); }
        }
        if ( m_pBlockDepthForm != nullptr ) {
            const bool solid = MapWorkspace_PrimitiveDefaults( m_pWorkspace, {} ).kind != map_primitive_kind_t::QUAD;
            m_pBlockDepthForm->setRowVisible( m_pBlockDepthControl, solid );
        }
        for ( auto *pControl : m_geometryControls ) { pControl->RefreshState(); }
        if ( m_pTerrainControls != nullptr ) { m_pTerrainControls->RefreshState(); }
        if ( m_pFaceControls != nullptr ) {
            m_pFaceControls->RefreshState();
            const u64 side = MapWorkspace_HasBrushFace( m_pWorkspace ) ? m_pWorkspace->selectedBrushFaceSide : 0;
            if ( side != 0 && side != m_lastBrushFace ) {
                gui::EditorSection_SetExpanded( m_pBrushFaceSection, true );
                if ( m_pMeshFaceSection != nullptr ) { gui::EditorSection_SetExpanded( m_pMeshFaceSection, false ); }
            }
            m_lastBrushFace = side;
        }
        if ( m_pMeshFaceControls != nullptr ) {
            m_pMeshFaceControls->RefreshState();
            const u64 face = MapWorkspace_HasMeshFace( m_pWorkspace ) ? m_pWorkspace->selectedMeshFaceId : 0;
            if ( face != 0 && face != m_lastMeshFace ) {
                gui::EditorSection_SetExpanded( m_pMeshFaceSection, true );
                if ( m_pBrushFaceSection != nullptr ) { gui::EditorSection_SetExpanded( m_pBrushFaceSection, false ); }
            }
            m_lastMeshFace = face;
        }
        if ( m_pClipControls != nullptr ) { m_pClipControls->RefreshState(); }
        if ( m_pSubtractControls != nullptr ) { m_pSubtractControls->RefreshState( documentChanged ); }
        if ( m_pSelection != nullptr ) {
            if ( m_pWorkspace->elementMode == map_element_mode_t::VERTICES ) {
                const usize vertices = MapWorkspace_HasMeshVertices( m_pWorkspace ) ? m_pWorkspace->meshSelection.vertices.nCount : 0;
                m_pSelection->setText( ( vertices == 1 ? QStringLiteral( "%1 vertex selected · Vertices mode" ) :
                    QStringLiteral( "%1 vertices selected · Vertices mode" ) ).arg( vertices ) );
            } else if ( m_pWorkspace->elementMode == map_element_mode_t::EDGES ) {
                const usize edges = MapWorkspace_HasMeshEdges( m_pWorkspace ) ? m_pWorkspace->meshSelection.edges.nCount : 0;
                m_pSelection->setText( ( edges == 1 ? QStringLiteral( "%1 edge selected · Edges mode" ) :
                    QStringLiteral( "%1 edges selected · Edges mode" ) ).arg( edges ) );
            } else {
                m_pSelection->setText( QStringLiteral( "%1 selected · %2 mode" ).arg( EditorSelection_Count( &m_pWorkspace->selection ) )
                                          .arg( QString::fromUtf8( MapWorkspace_ElementModeName( m_pWorkspace->elementMode ) ) ) );
            }
        }
        if ( m_pStartHint != nullptr ) {
            const auto *ws = m_pWorkspace;
            const bool vertices = ws->elementMode == map_element_mode_t::VERTICES && !MapWorkspace_HasMeshVertices( ws );
            const bool edges = ws->elementMode == map_element_mode_t::EDGES && !MapWorkspace_HasMeshEdges( ws );
            const bool faces = ws->elementMode == map_element_mode_t::FACES && !MapWorkspace_HasMeshFace( ws ) && !MapWorkspace_HasBrushFace( ws );
            const bool picking = ws->tool == map_tool_t::SELECT;
            const bool componentPicking = picking || ws->tool == map_tool_t::TRANSLATE || ws->tool == map_tool_t::ROTATE || ws->tool == map_tool_t::SCALE;
            QString hint;
            if ( vertices ) { hint = componentPicking ? QStringLiteral( "Click a vertex to select it. Shift-click adds; Ctrl/Command-click toggles. Transform gizmos move the selected vertices." )
                                                       : QStringLiteral( "Choose Vertices above to pick vertices, then use a transform tool." ); }
            if ( edges ) { hint = componentPicking ? QStringLiteral( "Click an edge to select it. Shift-click adds; Ctrl/Command-click toggles. Choose Extend under Edge dragging for ordinary drags, or Shift-drag a transform gizmo to extend an open boundary." )
                                                    : QStringLiteral( "Choose Edges above to pick edges, then use a transform tool." ); }
            if ( faces ) { hint = picking ? QStringLiteral( "Click a face in any view to start. On meshes Shift adds faces and Ctrl removes one; brush faces push and pull. The operations below light up for what you pick." )
                                          : QStringLiteral( "Pick faces with the Select tool (Q); this tool then moves what is picked." ); }
            if ( m_tool == map_tool_t::CURVE ) {
                const bool pipe = StringView_Equals( EditorSettings_Text( &ws->pGui->settings, "editor.map.curve_shape", {} ), StringView_FromCString( "hollow_pipe" ) );
                if ( pipe ) {
                    const bool compatible = EditorSettings_Bool( &ws->pGui->settings, "editor.map.curve_closed", CY_FALSE ) == CY_FALSE &&
                        StringView_Equals( EditorSettings_Text( &ws->pGui->settings, "editor.map.curve_output", {} ), StringView_FromCString( "mesh" ) );
                    hint = compatible ? QStringLiteral( "Hollow Pipe: click an open path, then Confirm. Outer radius includes the wall; thickness must be smaller. Preview shows the path only. Tight intersections and meshes above 2048 faces are refused. Backspace edits points; Cancel discards them." ) :
                        QStringLiteral( "Hollow Pipe needs Mesh output and Closed loop turned off. Change those controls before confirming; the path stays available." );
                } else { hint = QStringLiteral( "Click path points, Confirm to build, Backspace to remove the last point, or Cancel to discard. Round profile makes a solid cable; choose Hollow Pipe for an open bore." ); }
            }
            m_pStartHint->setText( hint );
            m_pStartHint->setVisible( !hint.isEmpty() );
        }
        for ( QToolButton *pButton : m_buttons ) {
            const QByteArray id = pButton->objectName().toUtf8();
            const u32 state = EditorCommands_State( &m_pWorkspace->pGui->commands, { id.constData(), static_cast<usize>( id.size() ) } );
            const bool enabled = ( state & COMMAND_STATE_ENABLED ) != 0u;
            const QSignalBlocker blocker( pButton );
            pButton->setEnabled( enabled );
            pButton->setChecked( ( state & COMMAND_STATE_CHECKED ) != 0u );
            QString help = pButton->property( "commandHelp" ).toString();
            if ( !enabled ) {
                const char *needs = MapGeometryCommands_Requirement( id.constData() );
                help += needs != nullptr ? QStringLiteral( "\nUnavailable: needs %1." ).arg( QString::fromUtf8( needs ) )
                                         : QStringLiteral( "\nUnavailable for the current tool or selection." );
            }
            pButton->setToolTip( help );
        }
    }

    map_workspace_t *m_pWorkspace{ nullptr };
    QScrollArea *m_pScroll{ nullptr };
    QLabel *m_pToolIcon{ nullptr };
    QLabel *m_pSelection{ nullptr };
    QLabel *m_pStartHint{ nullptr };
    map_tool_cancel_event_fn m_pfnCancelHandler{};
    void *m_pCancelContext{};
    map_tool_t m_tool{ map_tool_t::COUNT };
    map_element_mode_t m_mode{ map_element_mode_t::COUNT };
    QString m_title{};
    QStringList m_keyRows{};
    std::vector<tool_section_t> m_sections{};
    std::vector<tool_inventory_entry_t> m_optionInventory{};
    std::vector<tool_inventory_entry_t> m_operationInventory{};
    std::vector<QToolButton *> m_buttons{};
    std::vector<geometry_controls_t *> m_geometryControls{};
    std::vector<std::pair<QWidget *, QFormLayout *>> m_curveControls{};
    primitive_controls_t *m_pPrimitiveControls{};
    terrain_controls_t *m_pTerrainControls{};
    QWidget *m_pBlockDepthControl{};
    QFormLayout *m_pBlockDepthForm{};
    face_texture_state_t *m_pTextureState{};
    QWidget *m_pTextureStateSection{};
    QWidget *m_pFaceEditSection{}, *m_pFaceEdit{};
    face_controls_t *m_pFaceControls{};
    QWidget *m_pBrushFaceSection{};
    u64 m_lastBrushFace{};
    mesh_face_controls_t *m_pMeshFaceControls{};
    QWidget *m_pMeshFaceSection{};
    u64 m_lastMeshFace{};
    clip_controls_t *m_pClipControls{};
    subtract_controls_t *m_pSubtractControls{};
};

// ---------------------------------------------------------------------------
// Active Material
// ---------------------------------------------------------------------------

class active_material_t final : public QWidget {
public:
    active_material_t( QWidget *pParent, map_workspace_t *pWorkspace ) : QWidget( pParent ), m_pWorkspace( pWorkspace )
    {
        setObjectName( QStringLiteral( "MapActiveMaterial" ) );
        auto *pLayout = new QVBoxLayout( this );
        pLayout->setContentsMargins( 6, 6, 6, 6 );
        pLayout->setSpacing( 4 );
        m_pPreview = new QLabel( this );
        m_pPreview->setObjectName( QStringLiteral( "MapMaterialPreview" ) );
        m_pPreview->setMinimumSize( 96, 64 );
        m_pPreview->setAlignment( Qt::AlignCenter );
        // The pixmap is drawn at the label's size; Ignored stops it feeding
        // back into the layout and growing the panel.
        m_pPreview->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Ignored );
        m_pPath = new QLabel( this );
        m_pPath->setProperty( "muted", true );
        m_pPath->setTextInteractionFlags( Qt::TextSelectableByMouse );
        auto *pRow = new QHBoxLayout();
        // The application owns the asset browser; its command opens it on
        // the Materials tab, where double-clicking sets this material.
        auto *pBrowse = new QPushButton( QStringLiteral( "Browse..." ), this );
        pBrowse->setObjectName( QStringLiteral( "MapActiveMaterialBrowse" ) );
        pBrowse->setIcon( EditorStyle_Icon( pWorkspace->pGui->style, "asset-browser" ) );
        pBrowse->setToolTip( QStringLiteral( "Choose the material in the Asset Browser window; Accept makes it active." ) );
        QObject::connect( pBrowse, &QPushButton::clicked, this, [pWorkspace]() {
            if ( EditorCommands_Execute( &pWorkspace->pGui->commands, StringView_FromCString( "assets.browse_materials" ), command_args_t{} ) !=
                 command_result_t::OK ) {
                CY_LOG_WRITE( Warning, Editor, "No asset browser to open" );
            }
        } );
        pRow->addWidget( pBrowse, 1 );
        // Sandbox's small folder: the material's settings, shader, and
        // textures in the Database View, when the application has one.
        auto *pEdit = new QToolButton( this );
        pEdit->setObjectName( QStringLiteral( "MapActiveMaterialEdit" ) );
        pEdit->setIcon( EditorStyle_Icon( pWorkspace->pGui->style, "asset-folder" ) );
        pEdit->setToolTip( QStringLiteral( "Open the active material in the Database View" ) );
        pEdit->setVisible( EditorCommands_Find( &pWorkspace->pGui->commands, StringView_FromCString( "assets.database" ) ) != nullptr );
        QObject::connect( pEdit, &QToolButton::clicked, this, [pWorkspace]() {
            const string_view_t material = EditorSettings_Text( &pWorkspace->pGui->settings, "editor.map.default_material", string_view_t{} );
            const command_args_t args{ &material, material.cchLength != 0u ? 1u : 0u };
            ( void )EditorCommands_Execute( &pWorkspace->pGui->commands, StringView_FromCString( "assets.database" ), args );
        } );
        m_pEdit = pEdit;
        pRow->addWidget( pEdit );
        pLayout->addWidget( m_pPreview, 1 );
        pLayout->addWidget( m_pPath );
        pLayout->addLayout( pRow );
        ( void )EditorSettings_AddListener( &pWorkspace->pGui->settings, &active_material_t::OnSettingsChanged, this );
        ( void )MapWorkspace_AddListener( pWorkspace, &active_material_t::OnMapChanged, this );
        Refresh();
    }

    ~active_material_t() override
    {
        MapWorkspace_RemoveListener( m_pWorkspace, &active_material_t::OnMapChanged, this );
        EditorSettings_RemoveListener( &m_pWorkspace->pGui->settings, &active_material_t::OnSettingsChanged, this );
    }

protected:
    void resizeEvent( QResizeEvent * ) override { Refresh(); }

private:
    static void OnSettingsChanged( void *pContext, string_view_t path ) noexcept
    {
        if ( path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( "editor.map.default_material" ) ) ) {
            static_cast<active_material_t *>( pContext )->Refresh();
        }
    }

    static void OnMapChanged( void *pContext, u32 changes ) noexcept
    {
        // An image source arriving (or another project's) announces itself as a view change.
        if ( ( changes & MAP_CHANGE_VIEW ) != 0u ) { static_cast<active_material_t *>( pContext )->Refresh(); }
    }

public:
    // The material's own image (its base-colour texture, from the asset
    // browser) when there is one; otherwise a dev-texture stand-in, a
    // two-tone grid. Either way the name is on it, so the active material
    // is always recognisable.
    void Refresh()
    {
        if ( m_pEdit != nullptr ) { m_pEdit->setVisible( EditorCommands_Find( &m_pWorkspace->pGui->commands, StringView_FromCString( "assets.database" ) ) != nullptr ); }
        const QString path = FromView( EditorSettings_Text( &m_pWorkspace->pGui->settings, "editor.map.default_material", string_view_t{} ) );
        m_pPath->setText( path );
        // The whole panel, edge to edge: the material repeats across the
        // preview the way it repeats across a face, so no backdrop shows
        // whatever the panel's shape.
        const qreal dpr = m_pPreview->devicePixelRatioF();
        const QSize area( std::max( 64, m_pPreview->width() ), std::max( 64, m_pPreview->height() ) );
        const int edge = std::min( area.width(), area.height() );
        const QImage image = MapWorkspace_MaterialImage( m_pWorkspace, path );
        m_bHasImage = !image.isNull();
        QPixmap pixmap( area * dpr );
        pixmap.setDevicePixelRatio( dpr );
        pixmap.fill( QColor( 0x8a, 0x8d, 0x91 ) );
        QPainter painter( &pixmap );
        painter.setRenderHint( QPainter::SmoothPixmapTransform );
        const int tile = std::max( 16, edge / 2 );
        // Tiles centred on the panel, so the visible repeat is symmetric.
        const int x0 = ( area.width() / 2 ) % tile - tile;
        const int y0 = ( area.height() / 2 ) % tile - tile;
        if ( m_bHasImage ) {
            for ( int y = y0; y < area.height(); y += tile ) {
                for ( int x = x0; x < area.width(); x += tile ) { painter.drawImage( QRect( x, y, tile, tile ), image ); }
            }
        } else {
            const int cell = std::max( 4, tile / 4 );
            for ( int y = y0; y < area.height(); y += cell ) {
                for ( int x = x0; x < area.width(); x += cell ) {
                    if ( ( ( x - x0 ) / cell + ( y - y0 ) / cell ) % 2 == 0 ) { painter.fillRect( x, y, cell, cell, QColor( 0x9c, 0x9f, 0xa3 ) ); }
                }
            }
            painter.setPen( QColor( 0x3a, 0x3c, 0x40 ) );
            for ( int x = x0; x < area.width(); x += tile ) { painter.drawLine( x, 0, x, area.height() ); }
            for ( int y = y0; y < area.height(); y += tile ) { painter.drawLine( 0, y, area.width(), y ); }
        }
        QFont font = painter.font();
        font.setBold( true );
        painter.setFont( font );
        const QString name = QFileInfo( path ).completeBaseName().toUpper();
        const QRect label( 0, 0, std::min( area.width(), painter.fontMetrics().horizontalAdvance( name ) + 10 ), painter.fontMetrics().height() + 4 );
        painter.fillRect( label, QColor( 0, 0, 0, m_bHasImage ? 150 : 0 ) );
        painter.setPen( m_bHasImage ? QColor( 0xF0, 0xF0, 0xF0 ) : QColor( 0x20, 0x22, 0x25 ) );
        painter.drawText( label.adjusted( 5, 0, 0, 0 ), Qt::AlignLeft | Qt::AlignVCenter, name );
        painter.end();
        m_pPreview->setPixmap( pixmap );
    }

    bool HasImage() const { return m_bHasImage; }

private:

    map_workspace_t *m_pWorkspace{ nullptr };
    QLabel *m_pPreview{ nullptr };
    QLabel *m_pPath{ nullptr };
    QToolButton *m_pEdit{ nullptr }; // Database View; shown once the application registers it.
    bool m_bHasImage{ false };
};

// ---------------------------------------------------------------------------
// Auto Vis Groups
// ---------------------------------------------------------------------------

constexpr int kGroupRole = Qt::UserRole;

class visgroups_t final : public QWidget {
public:
    visgroups_t( QWidget *pParent, map_workspace_t *pWorkspace ) : QWidget( pParent ), m_pWorkspace( pWorkspace )
    {
        setObjectName( QStringLiteral( "MapVisgroups" ) );
        auto *pLayout = new QVBoxLayout( this );
        pLayout->setContentsMargins( 0, 0, 0, 0 );
        pLayout->setSpacing( 2 );
        // Hammer's row: "Presets: [preset] Load Save >>", Show All and Hide
        // All behind the >> menu.
        auto *pRow = new QHBoxLayout();
        pRow->setContentsMargins( 4, 3, 4, 0 );
        pRow->setSpacing( 3 );
        m_pPresets = new QComboBox( this );
        m_pPresets->setObjectName( QStringLiteral( "MapVisgroupsPresets" ) );
        m_pPresets->setToolTip( QStringLiteral( "Saved visibility of the groups below" ) );
        m_pPresets->setMinimumContentsLength( 6 );
        m_pPresets->setSizeAdjustPolicy( QComboBox::AdjustToMinimumContentsLengthWithIcon );
        auto *pLoad = new QPushButton( QStringLiteral( "Load" ), this );
        pLoad->setObjectName( QStringLiteral( "MapVisgroupsLoad" ) );
        pLoad->setToolTip( QStringLiteral( "Show and hide the groups as the preset says" ) );
        auto *pSave = new QPushButton( QStringLiteral( "Save" ), this );
        pSave->setObjectName( QStringLiteral( "MapVisgroupsSave" ) );
        pSave->setToolTip( QStringLiteral( "Save the current visibility as a preset" ) );
        auto *pMore = new QToolButton( this );
        pMore->setObjectName( QStringLiteral( "MapVisgroupsMore" ) );
        pMore->setText( QStringLiteral( "\u00BB" ) );
        pMore->setPopupMode( QToolButton::InstantPopup );
        auto *pMoreMenu = new QMenu( pMore );
        QAction *pShowAll = pMoreMenu->addAction( QStringLiteral( "Show All Groups" ) );
        QAction *pHideAll = pMoreMenu->addAction( QStringLiteral( "Hide All Groups" ) );
        pMoreMenu->addSeparator();
        m_pDeletePreset = pMoreMenu->addAction( QStringLiteral( "Delete Preset" ) );
        pMore->setMenu( pMoreMenu );
        pRow->addWidget( new QLabel( QStringLiteral( "Presets:" ), this ) );
        pRow->addWidget( m_pPresets, 1 );
        pRow->addWidget( pLoad );
        pRow->addWidget( pSave );
        pRow->addWidget( pMore );
        m_pTree = new QTreeWidget( this );
        m_pTree->setColumnCount( 2 );
        m_pTree->setHeaderLabels( { QStringLiteral( "Group" ), QStringLiteral( "Num" ) } );
        m_pTree->header()->setStretchLastSection( false );
        m_pTree->header()->setSectionResizeMode( 0, QHeaderView::Stretch );
        m_pTree->header()->setSectionResizeMode( 1, QHeaderView::ResizeToContents );
        m_pTree->setUniformRowHeights( true );
        pLayout->addLayout( pRow );
        pLayout->addWidget( m_pTree );
        BuildTree();
        QObject::connect( m_pTree, &QTreeWidget::itemChanged, this, [this]( QTreeWidgetItem *pItem, int column ) {
            if ( !m_bSyncing && column == 0 && pItem->data( 0, kClassRole ).isValid() ) {
                const QString className = pItem->data( 0, kClassRole ).toString();
                if ( !className.isEmpty() ) { MapEntityLook_SetClassHidden( m_pWorkspace, className, pItem->checkState( 0 ) == Qt::Unchecked ); }
                return;
            }
            if ( !m_bSyncing && column == 0 && pItem->data( 0, kUserVisgroupRole ).isValid() ) {
                SetUserVisgroupHidden( pItem->data( 0, kUserVisgroupRole ).toULongLong(), pItem->checkState( 0 ) == Qt::Unchecked );
                return;
            }
            if ( m_bSyncing || column != 0 || !pItem->data( 0, kGroupRole ).isValid() ) { return; }
            MapWorkspace_SetVisgroupHidden( m_pWorkspace, static_cast<map_visgroup_t>( pItem->data( 0, kGroupRole ).toInt() ),
                                            pItem->checkState( 0 ) == Qt::Unchecked );
        } );
        QObject::connect( pShowAll, &QAction::triggered, this, [this]() { SetAll( false ); } );
        QObject::connect( pHideAll, &QAction::triggered, this, [this]() { SetAll( true ); } );
        QObject::connect( pLoad, &QPushButton::clicked, this, [this]() { ( void )LoadPreset( m_pPresets->currentText() ); } );
        QObject::connect( m_pPresets, &QComboBox::activated, this, [this]( int ) { ( void )LoadPreset( m_pPresets->currentText() ); } );
        QObject::connect( pSave, &QPushButton::clicked, this, [this]() {
            bool bOk = false;
            const QString name = QInputDialog::getText( this, QStringLiteral( "Save Preset" ), QStringLiteral( "Preset name" ), QLineEdit::Normal,
                                                        m_pPresets->currentText(), &bOk ).trimmed();
            if ( bOk && !name.isEmpty() ) { ( void )SavePreset( name ); }
        } );
        QObject::connect( m_pDeletePreset, &QAction::triggered, this, [this]() { ( void )DeletePreset( m_pPresets->currentText() ); } );
        ( void )MapWorkspace_AddListener( pWorkspace, &visgroups_t::OnChanged, this );
        ( void )EditorSettings_AddListener( &pWorkspace->pGui->settings, &visgroups_t::OnSettingsChanged, this );
        FillPresets();
        Refresh();
    }

    ~visgroups_t() override
    {
        EditorSettings_RemoveListener( &m_pWorkspace->pGui->settings, &visgroups_t::OnSettingsChanged, this );
        MapWorkspace_RemoveListener( m_pWorkspace, &visgroups_t::OnChanged, this );
    }

    // name -> hidden-group mask, in saved order.
    QVector<QPair<QString, u32>> Presets() const
    {
        QVector<QPair<QString, u32>> presets;
        const string_view_t text = EditorSettings_Text( &m_pWorkspace->pGui->settings, kPresetsSetting, string_view_t{} );
        for ( const QString &entry : FromView( text ).split( QLatin1Char( ';' ), Qt::SkipEmptyParts ) ) {
            const qsizetype iEquals = entry.lastIndexOf( QLatin1Char( '=' ) );
            bool bOk = false;
            const u32 mask = iEquals > 0 ? entry.mid( iEquals + 1 ).toUInt( &bOk ) : 0u;
            if ( bOk ) { presets.append( { entry.left( iEquals ).trimmed(), mask } ); }
        }
        return presets;
    }

    bool SavePreset( const QString &name )
    {
        // Names carry no separators, so the stored list stays readable.
        QString clean = name.trimmed();
        clean.remove( QLatin1Char( ';' ) ).remove( QLatin1Char( '=' ) );
        if ( clean.isEmpty() ) { return false; }
        QVector<QPair<QString, u32>> presets = Presets();
        bool bReplaced = false;
        for ( auto &preset : presets ) {
            if ( preset.first == clean ) {
                preset.second = m_pWorkspace->hiddenVisgroups;
                bReplaced = true;
            }
        }
        if ( !bReplaced ) { presets.append( { clean, m_pWorkspace->hiddenVisgroups } ); }
        const bool bWritten = WritePresets( presets );
        if ( bWritten ) { m_pPresets->setCurrentText( clean ); }
        return bWritten;
    }

    bool LoadPreset( const QString &name )
    {
        for ( const auto &preset : Presets() ) {
            if ( preset.first != name ) { continue; }
            for ( u32 g = 0u; g < static_cast<u32>( map_visgroup_t::COUNT ); ++g ) {
                MapWorkspace_SetVisgroupHidden( m_pWorkspace, static_cast<map_visgroup_t>( g ), ( preset.second & ( 1u << g ) ) != 0u ? CY_TRUE : CY_FALSE );
            }
            return true;
        }
        return false;
    }

    bool DeletePreset( const QString &name )
    {
        QVector<QPair<QString, u32>> presets = Presets();
        const qsizetype before = presets.size();
        presets.erase( std::remove_if( presets.begin(), presets.end(), [&name]( const auto &preset ) { return preset.first == name; } ), presets.end() );
        return presets.size() != before && WritePresets( presets );
    }

    QStringList Rows() const
    {
        QStringList rows;
        for ( QTreeWidgetItemIterator it( m_pTree ); *it != nullptr; ++it ) {
            if ( ( *it )->data( 0, kGroupRole ).isValid() ) { rows.append( QStringLiteral( "%1 %2" ).arg( ( *it )->text( 0 ), ( *it )->text( 1 ) ) ); }
        }
        return rows;
    }

private:
    static constexpr const char *kPresetsSetting = "editor.map.visgroup_presets";

    static void OnChanged( void *pContext, u32 changes ) noexcept
    {
        if ( ( changes & ( MAP_CHANGE_DOCUMENT | MAP_CHANGE_VIEW ) ) != 0u ) { static_cast<visgroups_t *>( pContext )->Refresh(); }
    }

    static void OnSettingsChanged( void *pContext, string_view_t path ) noexcept
    {
        if ( path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( kPresetsSetting ) ) ) {
            static_cast<visgroups_t *>( pContext )->FillPresets();
        }
    }

    void FillPresets()
    {
        const QString current = m_pPresets->currentText();
        const QSignalBlocker blocker( m_pPresets );
        m_pPresets->clear();
        for ( const auto &preset : Presets() ) { m_pPresets->addItem( preset.first ); }
        m_pPresets->setCurrentIndex( std::max( 0, m_pPresets->findText( current ) ) );
        m_pDeletePreset->setEnabled( m_pPresets->count() != 0 );
    }

    bool WritePresets( const QVector<QPair<QString, u32>> &presets )
    {
        QStringList entries;
        for ( const auto &preset : presets ) { entries.append( QStringLiteral( "%1=%2" ).arg( preset.first ).arg( preset.second ) ); }
        const QByteArray utf8 = entries.join( QLatin1Char( ';' ) ).toUtf8();
        const setting_descriptor_t *pSetting = EditorSettings_Find( &m_pWorkspace->pGui->settings, StringView_FromCString( kPresetsSetting ) );
        if ( pSetting == nullptr ) { return false; }
        setting_value_t value{};
        value.type = setting_type_t::STRING;
        value.text = string_view_t{ utf8.constData(), static_cast<usize>( utf8.size() ) };
        return EditorSettings_Write( &m_pWorkspace->pGui->settings, settings_scope_t::USER, *pSetting, value ) == settings_registry_status_t::OK;
    }

    void BuildTree()
    {
        const struct {
            const char *pName;
            map_visgroup_t first;
            map_visgroup_t last;
        } parents[]{ { "World", map_visgroup_t::BRUSHES, map_visgroup_t::TIED }, { "Entities", map_visgroup_t::LIGHTS, map_visgroup_t::OTHER_ENTITIES },
                     { "Prefabs", map_visgroup_t::PREFABS, map_visgroup_t::PREFABS } };
        for ( const auto &parent : parents ) {
            auto *pParent = new QTreeWidgetItem( m_pTree, { QString::fromUtf8( parent.pName ) } );
            pParent->setFlags( Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate );
            pParent->setExpanded( true );
            for ( u32 g = static_cast<u32>( parent.first ); g <= static_cast<u32>( parent.last ); ++g ) {
                auto *pItem = new QTreeWidgetItem( pParent, { QString::fromUtf8( MapWorkspace_VisgroupName( static_cast<map_visgroup_t>( g ) ) ) } );
                pItem->setFlags( Qt::ItemIsEnabled | Qt::ItemIsUserCheckable );
                pItem->setData( 0, kGroupRole, static_cast<int>( g ) );
                pItem->setCheckState( 0, Qt::Checked );
            }
        }
    }

    void SetAll( bool bHidden )
    {
        for ( u32 g = 0u; g < static_cast<u32>( map_visgroup_t::COUNT ); ++g ) {
            MapWorkspace_SetVisgroupHidden( m_pWorkspace, static_cast<map_visgroup_t>( g ), bHidden );
        }
    }

    void Refresh()
    {
        u32 counts[static_cast<usize>( map_visgroup_t::COUNT )]{};
        for ( usize i = 0u; i < m_pWorkspace->wire.objects.nCount; ++i ) {
            ++counts[static_cast<usize>( MapWorkspace_VisgroupOf( m_pWorkspace, m_pWorkspace->wire.objects.pData[i] ) )];
        }
        m_bSyncing = true;
        for ( QTreeWidgetItemIterator it( m_pTree ); *it != nullptr; ++it ) {
            const QVariant group = ( *it )->data( 0, kGroupRole );
            if ( !group.isValid() ) { continue; }
            const auto visgroup = static_cast<map_visgroup_t>( group.toInt() );
            ( *it )->setText( 1, QString::number( counts[static_cast<usize>( visgroup )] ) );
            ( *it )->setCheckState( 0, MapWorkspace_IsVisgroupHidden( m_pWorkspace, visgroup ) ? Qt::Unchecked : Qt::Checked );
        }
        RefreshUserVisgroups();
        RefreshClasses();
        for ( int p = 0; p < m_pTree->topLevelItemCount(); ++p ) {
            QTreeWidgetItem *pParent = m_pTree->topLevelItem( p );
            u32 total = 0u;
            for ( int c = 0; c < pParent->childCount(); ++c ) { total += pParent->child( c )->text( 1 ).toUInt(); }
            pParent->setText( 1, QString::number( total ) );
        }
        m_bSyncing = false;
    }

    // Authored visgroups (map.visgroup.create): one row each under
    // "Visgroups", unchecked while all their members are hidden for the
    // session (the same hiding as Hide Selected).
    static constexpr int kUserVisgroupRole = Qt::UserRole + 1;
    void RefreshUserVisgroups()
    {
        QTreeWidgetItem *pParent = nullptr;
        for ( int p = 0; p < m_pTree->topLevelItemCount(); ++p ) {
            if ( m_pTree->topLevelItem( p )->data( 0, kUserVisgroupRole ).isValid() ) { pParent = m_pTree->topLevelItem( p ); }
        }
        const key_value_t *pList = m_pWorkspace->pDocument != nullptr
            ? KeyValue_Find( SettingsDocument_Root( &m_pWorkspace->pDocument->root ), StringView_FromCString( "visgroups" ) ) : nullptr;
        if ( KeyValue_ChildCount( pList ) == 0u ) { delete pParent; return; }
        if ( pParent == nullptr ) {
            pParent = new QTreeWidgetItem( m_pTree, { QStringLiteral( "Visgroups" ) } );
            pParent->setFlags( Qt::ItemIsEnabled );
            pParent->setData( 0, kUserVisgroupRole, QVariant::fromValue( qulonglong{ 0u } ) );
            pParent->setExpanded( true );
        }
        qDeleteAll( pParent->takeChildren() );
        vector_t<u64> members{};
        if ( !Vector_Init( &members, m_pWorkspace->pGui->pAllocator ) ) { return; }
        for ( usize i = 0u; i < KeyValue_ChildCount( pList ); ++i ) {
            const key_value_t *pVisgroup = KeyValue_ChildAt( pList, i );
            u64 id = 0u; string_view_t name{};
            if ( !KeyValue_GetU64( KeyValue_Find( pVisgroup, StringView_FromCString( "id" ) ), &id ) ) { continue; }
            ( void )KeyValue_GetString( KeyValue_Find( pVisgroup, StringView_FromCString( "name" ) ), &name );
            if ( MapDocument_VisgroupMembers( m_pWorkspace->pDocument, id, &members ) != map_status_t::OK ) { continue; }
            bool hidden = members.nCount != 0u;
            for ( usize m = 0u; m < members.nCount; ++m ) { hidden &= EditorSelection_Contains( &m_pWorkspace->hidden, members.pData[m] ) != CY_FALSE; }
            auto *pItem = new QTreeWidgetItem( pParent, { FromView( name ), QString::number( members.nCount ) } );
            pItem->setFlags( Qt::ItemIsEnabled | Qt::ItemIsUserCheckable );
            pItem->setData( 0, kUserVisgroupRole, QVariant::fromValue( static_cast<qulonglong>( id ) ) );
            pItem->setCheckState( 0, hidden ? Qt::Unchecked : Qt::Checked );
        }
        Vector_Shutdown( &members );
    }

    // TrenchBroom's entity definition visibility: one row per class the map
    // uses, unchecked while that class is hidden.
    static constexpr int kClassRole = Qt::UserRole + 2;
    void RefreshClasses()
    {
        QTreeWidgetItem *pParent = nullptr;
        for ( int p = 0; p < m_pTree->topLevelItemCount(); ++p ) {
            if ( m_pTree->topLevelItem( p )->data( 0, kClassRole ).isValid() ) { pParent = m_pTree->topLevelItem( p ); }
        }
        QMap<QString, u32> classes;
        for ( usize i = 0u; i < m_pWorkspace->wire.entities.nCount; ++i ) { ++classes[QString::fromUtf8( m_pWorkspace->wire.entities.pData[i].className )]; }
        if ( classes.isEmpty() ) { delete pParent; return; }
        if ( pParent == nullptr ) {
            pParent = new QTreeWidgetItem( m_pTree, { QStringLiteral( "Classes" ) } );
            pParent->setFlags( Qt::ItemIsEnabled );
            pParent->setData( 0, kClassRole, QString() );
            pParent->setExpanded( false );
            pParent->setToolTip( 0, QStringLiteral( "Show or hide entities by class; a hidden class takes its entities' brushes with it" ) );
        }
        qDeleteAll( pParent->takeChildren() );
        for ( auto it = classes.cbegin(); it != classes.cend(); ++it ) {
            auto *pItem = new QTreeWidgetItem( pParent, { it.key(), QString::number( it.value() ) } );
            pItem->setFlags( Qt::ItemIsEnabled | Qt::ItemIsUserCheckable );
            pItem->setData( 0, kClassRole, it.key() );
            pItem->setCheckState( 0, MapEntityLook_IsClassHidden( m_pWorkspace, it.key().toUtf8().constData() ) ? Qt::Unchecked : Qt::Checked );
        }
    }

    void SetUserVisgroupHidden( u64 visgroup, bool hide )
    {
        vector_t<u64> members{};
        if ( visgroup == 0u || !Vector_Init( &members, m_pWorkspace->pGui->pAllocator ) ) { return; }
        if ( MapDocument_VisgroupMembers( m_pWorkspace->pDocument, visgroup, &members ) == map_status_t::OK ) {
            for ( usize m = 0u; m < members.nCount; ++m ) {
                ( void )EditorSelection_Apply( &m_pWorkspace->hidden, members.pData[m], hide ? EDITOR_SELECT_ADD : EDITOR_SELECT_REMOVE );
                if ( hide ) { ( void )EditorSelection_Apply( &m_pWorkspace->selection, members.pData[m], EDITOR_SELECT_REMOVE ); }
            }
        }
        Vector_Shutdown( &members );
        MapWorkspace_Notify( m_pWorkspace, MAP_CHANGE_SELECTION | MAP_CHANGE_VIEW );
    }

    map_workspace_t *m_pWorkspace{ nullptr };
    QTreeWidget *m_pTree{ nullptr };
    QComboBox *m_pPresets{ nullptr };
    QAction *m_pDeletePreset{ nullptr };
    bool m_bSyncing{ false };
};

// ---------------------------------------------------------------------------
// Selection Sets
// ---------------------------------------------------------------------------

constexpr int kMembersRole = Qt::UserRole;
constexpr int kSetIdRole = Qt::UserRole + 1;

// Hammer 5's Selection Sets: named object lists kept with the map. The
// buttons run the map.selection_set.* commands with the current set, so
// every edit is one undo step; double-click selects, F2 renames.
class selection_sets_t final : public QWidget {
public:
    selection_sets_t( QWidget *pParent, map_workspace_t *pWorkspace ) : QWidget( pParent ), m_pWorkspace( pWorkspace )
    {
        setObjectName( QStringLiteral( "MapSelectionSets" ) );
        auto *pLayout = new QVBoxLayout( this );
        pLayout->setContentsMargins( 0, 0, 0, 0 );
        pLayout->setSpacing( 2 );
        auto *pRow = new QHBoxLayout();
        pRow->setContentsMargins( 4, 2, 4, 0 );
        pRow->setSpacing( 2 );
        const auto button = [&]( const char *pName, const char *pIcon, const QString &tip ) {
            auto *pButton = new QToolButton( this );
            pButton->setObjectName( QString::fromLatin1( pName ) );
            pButton->setIcon( EditorStyle_Icon( pWorkspace->pGui->style, pIcon ) );
            pButton->setToolTip( tip );
            pButton->setAutoRaise( true );
            pRow->addWidget( pButton );
            return pButton;
        };
        m_pNew = button( "MapSelectionSetNew", "selection-set-new", QStringLiteral( "New set from the selection" ) );
        m_pAdd = button( "MapSelectionSetAdd", "selection-set-add", QStringLiteral( "Add the selection to the set" ) );
        m_pRemove = button( "MapSelectionSetRemove", "selection-set-remove", QStringLiteral( "Remove the selection from the set" ) );
        m_pDelete = button( "MapSelectionSetDelete", "edit-delete", QStringLiteral( "Delete the set (its objects stay)" ) );
        pRow->addSpacing( 6 );
        m_pHide = button( "MapSelectionSetHide", "hide-selected", QStringLiteral( "Hide the set's objects" ) );
        m_pShow = button( "MapSelectionSetShow", "show-all", QStringLiteral( "Show the set's objects" ) );
        m_pSelect = button( "MapSelectionSetSelect", "select-objects", QStringLiteral( "Select the set's objects (double-click a set does the same)" ) );
        pRow->addStretch( 1 );
        m_pTree = new QTreeWidget( this );
        m_pTree->setColumnCount( 2 );
        m_pTree->setHeaderLabels( { QStringLiteral( "Selection Set" ), QStringLiteral( "Num" ) } );
        m_pTree->setRootIsDecorated( false );
        m_pTree->setEditTriggers( QAbstractItemView::EditKeyPressed );
        m_pTree->header()->setStretchLastSection( false );
        m_pTree->header()->setSectionResizeMode( 0, QHeaderView::Stretch );
        m_pTree->header()->setSectionResizeMode( 1, QHeaderView::ResizeToContents );
        pLayout->addLayout( pRow );
        pLayout->addWidget( m_pTree );
        QObject::connect( m_pNew, &QToolButton::clicked, this, [this]() { ( void )Run( "map.selection_set.create", false ); } );
        QObject::connect( m_pAdd, &QToolButton::clicked, this, [this]() { ( void )Run( "map.selection_set.add", true ); } );
        QObject::connect( m_pRemove, &QToolButton::clicked, this, [this]() { ( void )Run( "map.selection_set.remove", true ); } );
        QObject::connect( m_pDelete, &QToolButton::clicked, this, [this]() { ( void )Run( "map.selection_set.delete", true ); } );
        QObject::connect( m_pHide, &QToolButton::clicked, this, [this]() { ( void )Run( "map.selection_set.hide", true ); } );
        QObject::connect( m_pShow, &QToolButton::clicked, this, [this]() { ( void )Run( "map.selection_set.show", true ); } );
        QObject::connect( m_pSelect, &QToolButton::clicked, this, [this]() { ( void )Run( "map.selection_set.select", true ); } );
        QObject::connect( m_pTree, &QTreeWidget::itemDoubleClicked, this, [this]( QTreeWidgetItem *, int ) { ( void )Run( "map.selection_set.select", true ); } );
        QObject::connect( m_pTree, &QTreeWidget::currentItemChanged, this, [this]( QTreeWidgetItem *, QTreeWidgetItem * ) { UpdateButtons(); } );
        QObject::connect( m_pTree, &QTreeWidget::itemChanged, this, [this]( QTreeWidgetItem *pItem, int column ) { Rename( pItem, column ); } );
        ( void )MapWorkspace_AddListener( pWorkspace, &selection_sets_t::OnChanged, this );
        Rebuild();
    }

    ~selection_sets_t() override { MapWorkspace_RemoveListener( m_pWorkspace, &selection_sets_t::OnChanged, this ); }

private:
    static void OnChanged( void *pContext, u32 changes ) noexcept
    {
        auto *pSelf = static_cast<selection_sets_t *>( pContext );
        if ( ( changes & MAP_CHANGE_DOCUMENT ) != 0u ) { pSelf->Rebuild(); }
        else if ( ( changes & MAP_CHANGE_SELECTION ) != 0u ) { pSelf->UpdateButtons(); }
    }

    // The current set survives a rebuild by ID; a set made since the last
    // rebuild becomes current.
    void Rebuild()
    {
        const u64 current = CurrentSet();
        QSignalBlocker blocker( m_pTree );
        m_pTree->clear();
        QTreeWidgetItem *pCurrent = nullptr, *pNewest = nullptr;
        u64 newest = 0u;
        for ( const auto &set : MapSelectionSets_All( m_pWorkspace->pDocument ) ) {
            QList<QVariant> members;
            for ( u64 id : set.members ) { members.append( QVariant::fromValue<qulonglong>( id ) ); }
            auto *pItem = new QTreeWidgetItem( m_pTree, { QString::fromStdString( set.name ), QString::number( members.size() ) } );
            pItem->setData( 0, kMembersRole, members );
            pItem->setData( 0, kSetIdRole, QVariant::fromValue<qulonglong>( set.id ) );
            pItem->setFlags( pItem->flags() | Qt::ItemIsEditable );
            if ( set.id == current ) { pCurrent = pItem; }
            if ( set.id > newest ) { newest = set.id; pNewest = pItem; }
        }
        if ( m_bPrimed && newest > m_lastSeen ) { pCurrent = pNewest; }
        m_lastSeen = std::max( m_lastSeen, newest );
        m_bPrimed = true;
        if ( pCurrent != nullptr ) { m_pTree->setCurrentItem( pCurrent ); }
        UpdateButtons();
    }

    u64 CurrentSet() const
    {
        const QTreeWidgetItem *pItem = m_pTree->currentItem();
        return pItem != nullptr ? pItem->data( 0, kSetIdRole ).toULongLong() : 0u;
    }

    void UpdateButtons()
    {
        const bool bSet = CurrentSet() != 0u;
        const bool bSelection = m_pWorkspace->selection.ids.nCount != 0u;
        const bool bWritable = m_pWorkspace->pDocument != nullptr && !m_pWorkspace->pDocument->bReadOnly;
        m_pNew->setEnabled( bWritable && bSelection );
        m_pAdd->setEnabled( bWritable && bSet && bSelection );
        m_pRemove->setEnabled( bWritable && bSet && bSelection );
        m_pDelete->setEnabled( bWritable && bSet );
        m_pHide->setEnabled( bSet );
        m_pShow->setEnabled( bSet );
        m_pSelect->setEnabled( bSet );
    }

    command_result_t Run( const char *pCommand, bool bWithSet, u64 set = 0u, const QString &extra = {} )
    {
        if ( bWithSet && set == 0u ) { set = CurrentSet(); }
        if ( bWithSet && set == 0u ) { return command_result_t::INVALID_ARGUMENTS; }
        const QByteArray id = QByteArray::number( static_cast<qulonglong>( set ) ), text = extra.toUtf8();
        const string_view_t args[2]{ { id.constData(), static_cast<usize>( id.size() ) }, { text.constData(), static_cast<usize>( text.size() ) } };
        const usize nArgs = bWithSet ? ( extra.isEmpty() ? 1u : 2u ) : 0u;
        return EditorCommands_Execute( &m_pWorkspace->pGui->commands, StringView_FromCString( pCommand ), command_args_t{ args, nArgs } );
    }

    // An edited name renames the set; an empty one puts the old name back.
    // The command rebuilds the tree, so it runs after the item's signal.
    void Rename( QTreeWidgetItem *pItem, int column )
    {
        if ( column != 0 || pItem == nullptr ) { return; }
        const u64 set = pItem->data( 0, kSetIdRole ).toULongLong();
        const QString name = pItem->text( 0 ).trimmed();
        QMetaObject::invokeMethod( this, [this, set, name]() {
            if ( name.isEmpty() || Run( "map.selection_set.rename", true, set, name ) != command_result_t::OK ) { Rebuild(); }
        }, Qt::QueuedConnection );
    }

    map_workspace_t *m_pWorkspace{ nullptr };
    QTreeWidget *m_pTree{ nullptr };
    QToolButton *m_pNew{ nullptr }, *m_pAdd{ nullptr }, *m_pRemove{ nullptr }, *m_pDelete{ nullptr }, *m_pHide{ nullptr }, *m_pShow{ nullptr }, *m_pSelect{ nullptr };
    u64 m_lastSeen{ 0u };
    bool m_bPrimed{ false };
};

template <typename T>
T *As( QWidget *pWidget )
{
    auto *pImpl = dynamic_cast<T *>( pWidget );
    CY_ASSERT( pImpl != nullptr );
    return pImpl;
}

} // namespace

QWidget *MapToolProperties_Create( QWidget *pParent, map_workspace_t *pWorkspace )
{
    return new tool_properties_t( pParent, pWorkspace );
}

void MapToolProperties_SetCancelHandler( QWidget *pPanel, map_tool_cancel_event_fn pfnHandler, void *pContext )
{
    auto *pImpl = As<tool_properties_t>( pPanel );
    if ( pImpl != nullptr ) { pImpl->SetCancelHandler( pfnHandler, pContext ); }
}

QString MapToolProperties_Title( QWidget *pPanel )
{
    auto *pImpl = As<tool_properties_t>( pPanel );
    return pImpl != nullptr ? pImpl->Title() : QString();
}

QStringList MapToolProperties_KeyRows( QWidget *pPanel )
{
    auto *pImpl = As<tool_properties_t>( pPanel );
    return pImpl != nullptr ? pImpl->KeyRows() : QStringList{};
}

QStringList MapToolProperties_Options( QWidget *pPanel )
{
    auto *pImpl = As<tool_properties_t>( pPanel );
    return pImpl != nullptr ? pImpl->Options() : QStringList{};
}

QStringList MapToolProperties_Operations( QWidget *pPanel )
{
    auto *pImpl = As<tool_properties_t>( pPanel );
    return pImpl != nullptr ? pImpl->Operations() : QStringList();
}

QDialog *MapTransformDialog_Create( QWidget *pParent, map_workspace_t *pWorkspace )
{
    if ( pWorkspace == nullptr || pWorkspace->pGui == nullptr ) { return nullptr; }
    auto *pDialog = new QDialog( pParent );
    pDialog->setObjectName( QStringLiteral( "MapTransformDialog" ) );
    pDialog->setWindowTitle( QStringLiteral( "Transform Selection" ) );
    pDialog->setModal( false );
    pDialog->resize( 430, 230 );
    auto *pLayout = new QVBoxLayout( pDialog );
    auto *pTabs = new QTabWidget( pDialog );
    pTabs->setObjectName( QStringLiteral( "MapTransformTabs" ) );
    pTabs->addTab( new geometry_controls_t( pTabs, pWorkspace, numeric_edit_t::TRANSLATE ), QStringLiteral( "Move" ) );
    pTabs->addTab( new geometry_controls_t( pTabs, pWorkspace, numeric_edit_t::ROTATE ), QStringLiteral( "Rotate" ) );
    pTabs->addTab( new geometry_controls_t( pTabs, pWorkspace, numeric_edit_t::SCALE ), QStringLiteral( "Scale" ) );
    pLayout->addWidget( pTabs );
    auto *pClose = new QPushButton( QStringLiteral( "Close" ), pDialog );
    QObject::connect( pClose, &QPushButton::clicked, pDialog, &QDialog::hide );
    pLayout->addWidget( pClose, 0, Qt::AlignRight );
    return pDialog;
}

QWidget *MapActiveMaterial_Create( QWidget *pParent, map_workspace_t *pWorkspace )
{
    return new active_material_t( pParent, pWorkspace );
}

bool MapActiveMaterial_HasImage( QWidget *pPanel )
{
    auto *pImpl = dynamic_cast<active_material_t *>( pPanel );
    return pImpl != nullptr && pImpl->HasImage();
}

QWidget *MapVisgroups_Create( QWidget *pParent, map_workspace_t *pWorkspace )
{
    return new visgroups_t( pParent, pWorkspace );
}

QStringList MapVisgroups_Rows( QWidget *pPanel )
{
    auto *pImpl = As<visgroups_t>( pPanel );
    return pImpl != nullptr ? pImpl->Rows() : QStringList{};
}

bool MapVisgroups_SavePreset( QWidget *pPanel, const QString &name )
{
    auto *pImpl = As<visgroups_t>( pPanel );
    return pImpl != nullptr && pImpl->SavePreset( name );
}

bool MapVisgroups_LoadPreset( QWidget *pPanel, const QString &name )
{
    auto *pImpl = As<visgroups_t>( pPanel );
    return pImpl != nullptr && pImpl->LoadPreset( name );
}

bool MapVisgroups_DeletePreset( QWidget *pPanel, const QString &name )
{
    auto *pImpl = As<visgroups_t>( pPanel );
    return pImpl != nullptr && pImpl->DeletePreset( name );
}

QStringList MapVisgroups_Presets( QWidget *pPanel )
{
    QStringList names;
    if ( auto *pImpl = As<visgroups_t>( pPanel ) ) {
        for ( const auto &preset : pImpl->Presets() ) { names.append( preset.first ); }
    }
    return names;
}

QWidget *MapSelectionSets_Create( QWidget *pParent, map_workspace_t *pWorkspace )
{
    return new selection_sets_t( pParent, pWorkspace );
}

} // namespace cypher::editor::map
