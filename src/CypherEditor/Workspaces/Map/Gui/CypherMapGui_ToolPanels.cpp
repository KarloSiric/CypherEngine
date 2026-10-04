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

#include "CypherMapGui_ToolPanels.h"
#include "CypherMapGui_Input.h"

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
#include <QSignalBlocker>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QPointer>
#include <QScrollArea>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

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
        for ( const char *path : { "editor.map.primitive_axis", "editor.map.cylinder_sides", "editor.map.cone_top_radius", "editor.map.sphere_subdivisions" } ) {
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
        m_form->setRowVisible( 0, quad || cylinder || spike || StringView_Equals( shape, StringView_FromCString( "wedge" ) ) );
        m_form->setRowVisible( 1, cylinder || spike ); m_form->setRowVisible( 2, spike );
        m_form->setRowVisible( 3, StringView_Equals( shape, StringView_FromCString( "sphere" ) ) );
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
        { "sphere", "Sphere", "shape-sphere", "Sphere · solid brush with adjustable subdivisions" }
    };
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
            m_pPivot->addItems( { QStringLiteral( "Selection center" ), QStringLiteral( "World origin" ) } );
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

    void Refresh()
    {
        const bool staged = m_kind == numeric_edit_t::BOX && MapWorkspace_HasBlockPreview( m_pWorkspace );
        const auto primitive = staged ? m_pWorkspace->editPreview.primitive : MapWorkspace_PrimitiveDefaults( m_pWorkspace, {} );
        const bool quad = m_kind == numeric_edit_t::BOX && primitive.kind == map_primitive_kind_t::QUAD;
        const bool enabled = m_kind == numeric_edit_t::BOX ? m_pWorkspace->pDocument != nullptr && !m_pWorkspace->pDocument->bReadOnly :
                             !ComponentMode() && ( m_kind == numeric_edit_t::TRANSLATE ? MapWorkspace_CanMoveSelection( m_pWorkspace ) : MapWorkspace_CanEditSelection( m_pWorkspace ) );
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
        m_pApply->setEnabled( enabled );
        if ( m_pClone != nullptr ) { m_pClone->setEnabled( enabled ); }
        m_pFeedback->setText( enabled ? QStringLiteral( "One edit = one undo step." ) :
                             m_kind == numeric_edit_t::BOX ? QStringLiteral( "The map is read-only." ) :
                             ComponentMode() ? QStringLiteral( "Switch to Objects or Meshes for whole-object transforms. Component transforms are not available here yet." ) :
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
        // Component selections share their parent's object selection. Until a
        // component adapter exists, never interpret those IDs as a root edit.
        if ( m_kind != numeric_edit_t::BOX && ComponentMode() ) { Refresh(); return; }
        const math::vec3d_t values{ m_values[0]->value(), m_values[1]->value(), m_values[2]->value() };
        bool success = false;
        if ( m_kind == numeric_edit_t::BOX ) {
            success = MapWorkspace_HasBlockPreview( m_pWorkspace ) ? MapWorkspace_CommitBlockPreview( m_pWorkspace ) :
                MapWorkspace_CreatePrimitive( m_pWorkspace, BlockBounds() );
        } else if ( m_kind == numeric_edit_t::TRANSLATE ) {
            success = MapWorkspace_TranslateSelection( m_pWorkspace, values, clone );
        } else {
            const map_bounds_t bounds = SelectedBounds( *m_pWorkspace );
            const math::vec3d_t pivot = m_pPivot->currentIndex() == 0 ? MapBounds_Center( bounds ) : math::vec3d_t{};
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

// Single authored mesh face operations. The selector holds persistent IDs;
// settings controls and command buttons share the registry with menus/keys.
class mesh_face_controls_t final : public QWidget {
public:
    mesh_face_controls_t( QWidget *parent, map_workspace_t *ws ) : QWidget( parent ), m_ws( ws )
    {
        setObjectName( QStringLiteral( "MapMeshFaceControls" ) );
        auto *layout = new QVBoxLayout( this ); layout->setContentsMargins( 6, 5, 6, 6 ); layout->setSpacing( 5 );
        m_identity = new QLabel( this ); m_identity->setObjectName( QStringLiteral( "MapMeshFaceIdentity" ) ); m_identity->setWordWrap( true ); layout->addWidget( m_identity );
        m_faces = new QComboBox( this ); m_faces->setObjectName( QStringLiteral( "MapMeshFaceSelector" ) );
        m_faces->setToolTip( QStringLiteral( "Pick one authored mesh face in a view, or choose its persistent ID here." ) );
        QObject::connect( m_faces, &QComboBox::currentIndexChanged, this, [this]( int index ) {
            const u64 id = m_faces->itemData( index ).toULongLong();
            if ( m_root != 0 && id != 0 ) { MapWorkspace_SelectMeshFace( m_ws, m_root, id ); }
            else { MapWorkspace_ClearMeshFace( m_ws ); }
        } );
        layout->addWidget( m_faces );
        auto *form = new QFormLayout();
        for ( const auto &[path, label] : { std::pair{ "editor.map.mesh_extrude_distance", "Extrude distance" }, std::pair{ "editor.map.mesh_inset_distance", "Corner inset distance" },
                                            std::pair{ "editor.map.mesh_slice_u", "Quad Slice U cells" }, std::pair{ "editor.map.mesh_slice_v", "Quad Slice V cells" } } ) {
            if ( auto *control = gui::EditorSettingControl_Create( this, &ws->pGui->settings, path ) ) { form->addRow( QString::fromLatin1( label ), control ); }
        }
        layout->addLayout( form );
        for ( const auto &[id, button] : { std::pair{ "map.mesh.extrude", &m_extrude }, std::pair{ "map.mesh.inset", &m_inset }, std::pair{ "map.mesh.quad_slice", &m_slice } } ) {
            const auto *command = EditorCommands_Find( &ws->pGui->commands, StringView_FromCString( id ) );
            *button = new QPushButton( command != nullptr ? QString::fromUtf8( command->pLabel ) : QString::fromLatin1( id ), this );
            ( *button )->setObjectName( QString::fromLatin1( id ) );
            if ( command != nullptr ) { ( *button )->setIcon( gui::EditorStyle_Icon( ws->pGui->style, command->pIcon ) ); ( *button )->setToolTip( QString::fromUtf8( command->pDescription ) ); }
            QObject::connect( *button, &QPushButton::clicked, this, [this, id]() {
                const auto result = EditorCommands_Execute( &m_ws->pGui->commands, StringView_FromCString( id ), {} );
                m_feedback->setText( result == command_result_t::OK ? QStringLiteral( "Applied. The retained source face stays selected; Undo restores the previous geometry." ) :
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
        m_identity->setText( active ? QStringLiteral( "Mesh #%1 · Face #%2" ).arg( m_ws->selectedMeshFaceObject ).arg( m_ws->selectedMeshFaceId ) : QStringLiteral( "Select one mesh face in Faces mode." ) );
        m_extrude->setEnabled( MapWorkspace_CanEditMeshFace( m_ws ) ); m_inset->setEnabled( MapWorkspace_CanEditMeshFace( m_ws ) );
        m_slice->setEnabled( ( EditorCommands_State( &m_ws->pGui->commands, StringView_FromCString( "map.mesh.quad_slice" ) ) & COMMAND_STATE_ENABLED ) != 0 );
        m_feedback->setText( QStringLiteral( "Extrude creates side walls. Inset moves corners toward the center. Quad Slice creates a U/V grid in a four-corner face; U follows corners 0–1 and V follows 0–3. Shared boundary vertices connect neighboring faces. Mesh face dragging and multi-face editing are not available yet." ) );
    }
private:
    map_workspace_t *m_ws{};
    u64 m_root{};
    QLabel *m_identity{}, *m_feedback{};
    QComboBox *m_faces{};
    QPushButton *m_extrude{}, *m_inset{}, *m_slice{};
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
        m_note = new QLabel( QStringLiteral( "UV state is read only; UV editing is not available yet." ), this );
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
        m_pPushPull->setToolTip( QStringLiteral( "Push / Pull Face\nApply the distance along this face's outward normal. In 3D Faces mode, drag the normal handle for the same grid-snapped operation. One Undo restores the brush." ) );
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
        m_pFeedback->setText( active ? QStringLiteral( "Drag the face-normal handle in 3D, or apply a distance here. Edits affect this face only." ) : QStringLiteral( "Push/pull edits the existing convex brush; mesh topology extrusion is separate." ) );
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
        m_pApply = new QPushButton( QStringLiteral( "Clip Brushes" ), this );
        m_pApply->setObjectName( QStringLiteral( "MapClipApply" ) );
        m_pApply->setIcon( gui::EditorStyle_Icon( pWorkspace->pGui->style, "tool-clip" ) );
        m_pApply->setToolTip( QStringLiteral( "Cut selected brushes with this plane. New cut faces use the active material. One undo restores the original brushes." ) );
        QObject::connect( m_pApply, &QPushButton::clicked, this, [this]() {
            const math::planed_t input{ { m_normals[0]->value(), m_normals[1]->value(), m_normals[2]->value() }, m_pDistance->value() };
            const auto mode = MapWorkspace_ClipMode( m_pWorkspace );
            math::planed_t normalized{};
            if ( !math::Planed_TryNormalize( input, 1e-8, &normalized ) ) {
                m_pFeedback->setText( QStringLiteral( "Enter a nonzero plane normal. Nothing changed." ) );
                return;
            }
            const bool success = MapWorkspace_ClipSelection( m_pWorkspace, normalized, mode );
            m_pFeedback->setText( success ? QStringLiteral( "Brushes clipped. Undo restores the originals." ) : QStringLiteral( "Nothing changed. Check that the plane intersects the selected brushes." ) );
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
        const bool active = MapWorkspace_CanEditBrushSelection( m_pWorkspace );
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
        m_pRule->setText( mode == map_brush_clip_mode_t::BACK ? QStringLiteral( "Back: keep n · p + d ≤ 0" ) :
                         mode == map_brush_clip_mode_t::FRONT ? QStringLiteral( "Front: keep n · p + d ≥ 0" ) : QStringLiteral( "Both: retain both sides of the plane" ) );
        m_pFeedback->setText( active ? QStringLiteral( "Numeric plane clipping affects every selected brush." ) : QStringLiteral( "Select editable brushes only." ) );
    }
private:
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

constexpr tool_panel_t kToolPanels[]{
    // SELECT
    { "Click in any view to select; Ctrl/Command+click toggles and Shift+click adds. In Objects or Groups mode, drag a selected object or an RGB move handle to move the selection. Drag a 2D bounds side/corner or a 3D RGB bounds handle to resize with the opposite side fixed. With multiple objects, Each applies the same grid-distance change to every object; Group scales their combined bounds. Hold Shift when grabbing a resize handle to widen both sides around the center. The anchor is captured at press. Ctrl/Command bypasses snapping. A dashed red source outline and travel guide mark the starting position during movement. Dimensions update in every view. Double-click an object to inspect it, or empty space for view options.",
      { { keymap_section_t::MOUSE, "map.viewport" }, { keymap_section_t::BINDINGS, "map.viewport" }, { keymap_section_t::MOUSE, "map.viewport.2d" } },
      { { "Vertex Editing", {}, { "map.mesh.merge", "map.mesh.collapse", "map.mesh.bevel", "map.mesh.dissolve", "map.mesh.fill_hole", "map.select.grow", "map.select.shrink", "map.pivot.clear" },
          kVertexMode, true, "MapToolModeVertices", "Pick mesh vertices. Convert brushes to meshes first. Vertex transforms and topology edits are planned." },
        { "Vertex snapping", { "editor.grid.size", "editor.grid.snap" }, { "map.brush.snap_to_grid" }, kVertexMode },
        { "Edge Editing", {}, { "map.select.loop", "map.select.ring", "map.mesh.dissolve", "map.mesh.collapse", "map.mesh.bevel", "map.mesh.extrude_edges", "map.mesh.connect_edges", "map.mesh.extend_edges", "map.mesh.merge", "map.mesh.split_edges", "map.mesh.snap_edge_to_edge", "map.mesh.fill_hole", "map.mesh.bridge", "map.mesh.normals_hard", "map.mesh.normals_soft", "map.mesh.normals_default", "map.texture.weld_uvs", "map.select.ribs", "map.pivot.clear", "map.tool.edge_cut", "map.tool.edge_arc", "map.mesh.radial_align" },
          kEdgeMode, true, "MapToolModeEdges", "Pick mesh edges; use Select Loop or Select Ring. Convert brushes to meshes first. Other topology edits are planned." },
        { "Face Editing", {}, { "map.mesh.bevel", "map.mesh.solidify", "map.mesh.bridge", "map.mesh.fill_hole", "map.mesh.split", "map.mesh.subdivide", "map.mesh.smooth", "map.select.same_material" },
          kFaceMode, true, "MapToolModeFaces", "Select a brush face for normal Push / Pull and material assignment, or an authored mesh face for Extrude, Inset and Quad Slice. Other topology operations below are planned." },
        { "Modify Texture", {}, { "map.texture.align_world", "map.texture.align_face", "map.texture.fit", "map.texture.shift", "map.texture.scale", "map.texture.rotate", "map.texture.justify_left", "map.texture.justify_center", "map.texture.justify_right", "map.texture.justify_top", "map.texture.justify_bottom", "map.texture.unwrap" }, kFaceMode },
        { "Mesh Editing", {}, { "map.transform.dialog", "edit.duplicate", "edit.delete", "map.mesh.subdivide", "map.mesh.smooth", "map.mesh.solidify", "map.mesh.boolean_union", "map.mesh.boolean_subtract", "map.mesh.boolean_intersect", "map.mesh.to_brush" },
          kMeshMode, true, "MapToolModeMeshes", "Select whole brushes and meshes. Root transforms and supported brush/mesh operations act on those objects; topology operations below remain planned." },
        { "Object Editing", {}, { "map.transform.dialog", "edit.duplicate", "edit.delete" }, kObjectMode, true, "MapToolModeObjects" },
        { "Group Editing", {}, { "map.transform.dialog", "edit.duplicate", "edit.delete" }, kGroupMode, true, "MapToolModeGroups" },
        { "Selection", { "editor.map.select_created" }, { "edit.select_all", "edit.invert_selection", "map.select.same_class", "map.select.same_material", "map.select.touching" }, kRootModes },
        { "Bounds resizing", { "editor.map.resize_mode", "editor.map.resize_from_center" }, {}, kRootModes },
        { "2D display", { "editor.viewport.show_selection_bounds", "editor.viewport.show_selection_dimensions", "editor.viewport.show_selection_vertices", "editor.viewport.entity_names" },
          { "map.view.center_selection_2d" } },
        { "3D display", { "editor.viewport.perspective.show_selection_bounds", "editor.viewport.perspective.show_selection_dimensions", "editor.viewport.perspective.show_selection_vertices", "editor.viewport.perspective.entity_names" },
          { "map.view.center_selection_3d" } },
        { "Clipboard", { "editor.map.paste_offset" }, { "edit.copy", "edit.cut", "edit.paste", "edit.paste_special" }, kRootModes },
        { "Brush operations", { "editor.map.hollow_thickness" }, { "map.brush.hollow", "map.brush.merge", "map.brush.to_mesh" }, kRootModes },
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
    { "Choose the default class for point and brush entities.", { { keymap_section_t::BINDINGS, "map.tool.entity" } },
      { { "Entity defaults", { "editor.map.default_point_class", "editor.map.default_solid_class", "editor.map.write_all_properties", "editor.map.select_created" }, {} },
        { "Entity display", { "editor.viewport.entity_names", "editor.viewport.io_lines" }, { "map.select.same_class" } } } },
    // BLOCK
    { "Draw a box, quad, wedge, cylinder, spike, or sphere in a map view, or create it from exact dimensions below. Shift + wheel adjusts depth during a drag. Refine the staged primitive using bounds handles; hold Shift at handle press to resize around its center, then confirm to create it.", { { keymap_section_t::BINDINGS, "map.tool.block" } },
      { { "Creation defaults", { "editor.map.block_depth", "editor.map.select_created" }, {} },
        { "Bounds resizing", { "editor.map.resize_from_center" }, {} },
        { "Primitive", { "editor.map.new_brush_shape", "editor.map.primitive_axis", "editor.map.cylinder_sides", "editor.map.cone_top_radius", "editor.map.sphere_subdivisions" }, {} },
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
    { "Surface decal placement uses the active material.", { { keymap_section_t::BINDINGS, "map.tool.decal" } },
      { { "Material", { "editor.map.default_material" }, { "assets.browse_materials" } } } },
    // OVERLAY
    { "Surface overlays use the active material.", { { keymap_section_t::BINDINGS, "map.tool.overlay" } },
      { { "Material", { "editor.map.default_material" }, { "assets.browse_materials" } } } },
    // CLIP
    { "Drag a cutting line in a 2D view, or in 3D on the plane through the selection center perpendicular to the 3D plane axis. The line extrudes along that axis to define the Clip plane. Drag staged endpoints to adjust them. Confirm applies it; Cancel discards it. Repeat this tool to cycle Back, Front, and Both. Exact plane controls are also available.", { { keymap_section_t::BINDINGS, "map.tool.clip" } },
      { { "Construction grid", { "editor.grid.size", "editor.grid.snap" }, {} },
        { "Plane placement", { "editor.map.clip_axis" }, {} },
        { "Retained half-space", { "editor.map.clip_mode" }, {} },
        { "Mesh cutting", {}, { "map.mesh.slice" } } } },
    // VERTEX
    { "Inspect vertex and edge topology operations.", { { keymap_section_t::BINDINGS, "map.tool.vertex" } },
      { { "Snapping", { "editor.grid.size", "editor.grid.snap", "editor.viewport.show_selection_vertices" }, { "map.brush.snap_to_grid" } },
        { "Topology", {}, { "map.mesh.merge", "map.mesh.collapse", "map.mesh.split", "map.mesh.dissolve", "map.mesh.triangulate", "map.mesh.fill_hole" } } } },
    // PATH
    { "Path authoring uses the construction grid.", { { keymap_section_t::BINDINGS, "map.tool.path" } },
      { { "Construction grid", { "editor.grid.size", "editor.grid.snap" }, {} } } },
    // MEASURE
    { "Read dimensions and coordinates in the map views.", { { keymap_section_t::BINDINGS, "map.tool.measure" } },
      { { "Measurement display", { "editor.viewport.show_rulers", "editor.viewport.show_selection_dimensions", "editor.grid.size" }, {} } } },
    // TERRAIN
    { "Terrain operations are grouped by the change they make to the surface.", { { keymap_section_t::BINDINGS, "map.tool.terrain" } },
      { { "Surface", {}, { "map.terrain.raise", "map.terrain.lower", "map.terrain.smooth", "map.terrain.flatten", "map.terrain.paint" } },
        { "Material", { "editor.map.default_material" }, { "assets.browse_materials" } } } },
    // PATCH
    { "Curved patch authoring uses the construction grid.", { { keymap_section_t::BINDINGS, "map.tool.patch" } },
      { { "Construction grid", { "editor.grid.size", "editor.grid.snap" }, {} },
        { "Material", { "editor.map.default_material" }, { "assets.browse_materials" } } } },
    // TRANSLATE
    { "Move the selection along an axis or plane.", { { keymap_section_t::BINDINGS, "map.tool.translate" } },
      { { "Move steps", { "editor.grid.size", "editor.grid.snap", "editor.map.paste_offset", "editor.map.texture_lock" }, { "map.brush.snap_to_grid" } },
        { "Alignment", {}, { "map.align.left", "map.align.right", "map.align.top", "map.align.bottom" } } } },
    // ROTATE
    { "Rotate the selection about its pivot.", { { keymap_section_t::BINDINGS, "map.tool.rotate" } },
      { { "Rotation", { "editor.grid.angle_snap", "editor.map.texture_lock" }, { "map.grid.angle_snap", "map.transform.rotate_cw", "map.transform.rotate_ccw" } } } },
    // SCALE
    { "Scale the selection about its pivot.", { { keymap_section_t::BINDINGS, "map.tool.scale" } },
      { { "Scale", { "editor.grid.scale_snap", "editor.map.texture_scale_lock" }, { "map.grid.scale_snap" } } } },
    // PIVOT
    { "A pivot defines the center used by rotation and scaling.", { { keymap_section_t::BINDINGS, "map.tool.pivot" } },
      { { "Construction grid", { "editor.grid.size", "editor.grid.snap" }, {} } } },
    // POLYGON
    { "Polygon authoring uses the construction grid and active material.", { { keymap_section_t::BINDINGS, "map.tool.polygon" } },
      { { "Construction", { "editor.grid.size", "editor.grid.snap", "editor.map.default_material", "editor.map.select_created" }, {} } } },
    // MIRROR
    { "Reflect the selection across the view axes.", { { keymap_section_t::BINDINGS, "map.tool.mirror" } },
      { { "Reflection", { "editor.grid.snap", "editor.map.texture_lock" }, { "map.transform.flip_horizontal", "map.transform.flip_vertical" } } } },
    // PAINT
    { "Material painting uses the active material.", { { keymap_section_t::BINDINGS, "map.tool.paint" } },
      { { "Material", { "editor.map.default_material" }, { "assets.browse_materials", "map.terrain.paint" } } } },
    // EXTRUDE
    { "Drag a brush face to push or pull its convex solid. For an authored mesh, select one face and use Extrude or Corner inset below.", { { keymap_section_t::BINDINGS, "map.tool.extrude" } },
      { { "Construction grid", { "editor.grid.size", "editor.grid.snap" }, {} },
        { "Face operations", {}, { "map.mesh.extrude", "map.mesh.inset", "map.mesh.bevel", "map.mesh.solidify", "map.mesh.bridge", "map.mesh.fill_hole" } } } },
    // KNIFE
    { "Cut and separate mesh topology.", { { keymap_section_t::BINDINGS, "map.tool.knife" } },
      { { "Cutting", { "editor.grid.snap" }, { "map.mesh.slice", "map.mesh.split", "map.mesh.dissolve" } } } },
    // LOOP_CUT
    { "Work with edge loops, rings, and surface subdivision.", { { keymap_section_t::BINDINGS, "map.tool.loop_cut" } },
      { { "Loops and surfaces", {}, { "map.select.loop", "map.select.ring", "map.mesh.subdivide", "map.mesh.smooth" } } } },
    // EYEDROPPER
    { "Inspect and choose the active surface material.", { { keymap_section_t::BINDINGS, "map.tool.eyedropper" } },
      { { "Material", { "editor.map.default_material" }, { "assets.browse_materials", "map.select.same_material", "map.texture.replace" } } } },
    // WORKPLANE
    { "A workplane defines the construction orientation for drawing tools.", { { keymap_section_t::BINDINGS, "map.tool.workplane" } },
      { { "Construction grid", { "editor.grid.size", "editor.grid.snap", "editor.grid.show_3d" }, {} } } },
    // CURVE
    { "Curve authoring uses the construction grid.", { { keymap_section_t::BINDINGS, "map.tool.curve" } },
      { { "Construction grid", { "editor.grid.size", "editor.grid.snap" }, {} } } },
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
    { "Vertex Editing", "select-vertices", "Pick authored mesh vertices in a 2D or 3D view. Shift+click adds and Ctrl/Command+click toggles vertices on the same mesh. Convert brushes to meshes before selecting their vertices. Vertex transforms and topology edits are planned; dragging does not move the parent mesh." },
    { "Edge Editing", "select-edges", "Pick authored mesh edges in a 2D or 3D view. Shift+click adds and Ctrl/Command+click toggles edges on the same mesh. Select Loop and Select Ring extend the selection from the last picked edge. Convert brushes to meshes before selecting their edges. Topology edits and component transforms are planned." },
    { "Face Editing", "select-faces", "Pick a face in a 2D or 3D view. Drag a selected brush face's normal handle for grid-snapped Push / Pull. Mesh faces use the exact Extrude, Inset and Quad Slice controls below." },
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
    QStringList Options() const { return VisibleInventory( m_optionInventory, ModeMask( m_mode ) ); }
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
             StringView_Equals( path, StringView_FromCString( "editor.map.mesh_slice_v" ) ) ) {
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
                if ( m_mode != map_element_mode_t::FACES && ( id == "map.mesh.extrude" || id == "map.mesh.inset" || id == "map.mesh.quad_slice" ) ) { return; }
            }
            if ( brushFace && ( id == QStringLiteral( "map.mesh.extrude" ) || id == QStringLiteral( "map.mesh.inset" ) ||
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
                      std::pair{ "map.mesh.extrude", "Mesh face: extrude along its normal (Faces)" },
                      std::pair{ "map.mesh.inset", "Mesh face: inset corners toward its center (Faces)" },
                      std::pair{ "map.mesh.quad_slice", "Mesh face: slice a quad into U/V cells (Faces)" } } ) {
                addCommand( command, QString::fromLatin1( label ) );
            }
        }
        if ( m_tool == map_tool_t::SELECT ) {
            // A mode profile exists independently of the selected geometry.
            // Show its configured shortcuts with the same effective lookup
            // as the views. Planned component operations stay identified as
            // planned rather than promising an enabled edit gesture.
            for ( const tool_group_t &group : info.groups ) {
                if ( ( group.modes & ModeMask( m_mode ) ) == 0u || !group.namedOperations ) { continue; }
                for ( const char *id : group.commands ) {
                    QString label = ActionLabel( gui, QString::fromUtf8( id ) );
                    const bool edgeSelection = m_mode == map_element_mode_t::EDGES &&
                        ( QLatin1StringView( id ) == QLatin1StringView( "map.select.loop" ) ||
                          QLatin1StringView( id ) == QLatin1StringView( "map.select.ring" ) );
                    if ( m_mode == map_element_mode_t::VERTICES || ( m_mode == map_element_mode_t::EDGES && !edgeSelection ) ) {
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
                addRow( QStringLiteral( "[Shift+LeftClick]" ), QStringLiteral( "Add an edge on the same mesh" ) );
                addRow( QStringLiteral( "[Ctrl/Command+LeftClick]" ), QStringLiteral( "Toggle an edge on the same mesh" ) );
            }
            addRow( QStringLiteral( "[Double LeftClick]" ), QStringLiteral( "Object: inspect properties" ) );
            addRow( QStringLiteral( "[Double LeftClick]" ), QStringLiteral( "Empty space: view options" ) );
            if ( brushFace ) {
                addRow( QStringLiteral( "[LeftDrag]" ), QStringLiteral( "Brush face-normal handle: push / pull by the grid step (3D)" ) );
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
            addRow( QStringLiteral( "[LeftDrag]" ), QStringLiteral( "3D: draw on the plane perpendicular to the 3D plane axis" ) );
            addRow( QStringLiteral( "[LeftDrag]" ), QStringLiteral( "Staged endpoint: move it; release to preview" ) );
        }
        if ( m_tool == map_tool_t::TRANSLATE || m_tool == map_tool_t::ROTATE || m_tool == map_tool_t::SCALE ) {
            addRow( QStringLiteral( "[LeftDrag]" ), m_tool == map_tool_t::TRANSLATE ? QStringLiteral( "Move selection" ) :
                    m_tool == map_tool_t::ROTATE ? QStringLiteral( "Rotate selection" ) : QStringLiteral( "Scale selection" ) );
            if ( m_tool == map_tool_t::TRANSLATE ) {
                addRow( QStringLiteral( "[LeftDrag]" ), QStringLiteral( "RGB axis, plane square, or center: constrain movement" ) );
                addRow( QStringLiteral( "[Shift+LeftDrag]" ), QStringLiteral( "Clone and move" ) );
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
        m_geometryControls.clear();
        m_pTextureState = nullptr; m_pTextureStateSection = nullptr;
        m_pFaceControls = nullptr;
        m_pBrushFaceSection = nullptr; m_lastBrushFace = 0;
        m_pMeshFaceControls = nullptr; m_pMeshFaceSection = nullptr; m_lastMeshFace = 0;
        m_pClipControls = nullptr;
        m_pSubtractControls = nullptr;
        m_pToolIcon = nullptr;
        m_pSelection = nullptr;
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
            for ( const char *path : { "editor.map.new_brush_shape", "editor.map.primitive_axis", "editor.map.cylinder_sides", "editor.map.cone_top_radius", "editor.map.sphere_subdivisions" } ) {
                m_optionInventory.push_back( { QString::fromLatin1( path ), kAllModes } );
            }
        }

        // Faces show the selected authored surface first; geometry defaults
        // remain separate and cannot be mistaken for its current UV values.
        if ( m_tool == map_tool_t::SELECT || m_tool == map_tool_t::EXTRUDE || m_tool == map_tool_t::TEXTURE ||
             m_tool == map_tool_t::TRANSLATE || m_tool == map_tool_t::ROTATE || m_tool == map_tool_t::SCALE ) {
            m_pTextureState = new face_texture_state_t( pContent, m_pWorkspace );
            m_pTextureStateSection = gui::EditorSection_Create( pContent, QStringLiteral( "Texture State" ), m_pTextureState );
            m_pTextureStateSection->setObjectName( QStringLiteral( "MapFaceTextureSection" ) ); pLayout->addWidget( m_pTextureStateSection );
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
            for ( const char *path : { "editor.map.mesh_extrude_distance", "editor.map.mesh_inset_distance", "editor.map.mesh_slice_u", "editor.map.mesh_slice_v" } ) {
                m_optionInventory.push_back( { QString::fromLatin1( path ), m_tool == map_tool_t::SELECT ? kFaceMode : kAllModes } );
            }
            for ( const char *id : { "map.mesh.extrude", "map.mesh.inset", "map.mesh.quad_slice" } ) {
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
                const bool plannedUv = ( m_tool == map_tool_t::TEXTURE || m_tool == map_tool_t::SELECT ) && QLatin1StringView( group.pTitle ) == QLatin1StringView( "Modify Texture" );
                auto *section = gui::EditorSection_Create( pContent, plannedUv ? QStringLiteral( "Modify Texture · planned" ) : QString::fromUtf8( group.pTitle ), pBody, !plannedUv );
                if ( group.pObjectName != nullptr ) { section->setObjectName( QString::fromUtf8( group.pObjectName ) ); }
                if ( plannedUv ) { section->setObjectName( QStringLiteral( "MapTexturePlannedOperations" ) ); section->setToolTip( QStringLiteral( "These UV operations are not connected to authored face edits yet." ) ); }
                m_sections.push_back( { section, group.modes } );
                for ( const auto &path : groupOptions ) { m_optionInventory.push_back( { path, group.modes } ); }
                for ( const auto &id : groupOperations ) { m_operationInventory.push_back( { id, group.modes } ); }
                pLayout->addWidget( section );
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
        const u32 mode = ModeMask( m_mode );
        for ( const auto &section : m_sections ) { section.pSection->setVisible( ( section.modes & mode ) != 0u ); }
        // Keep component selectors, numeric drafts and subscriptions alive.
        // Only the containing section changes visibility; no signal sender
        // is replaced when its face selection changes the current mode.
        if ( m_pBrushFaceSection != nullptr ) { m_pBrushFaceSection->setVisible( m_mode == map_element_mode_t::FACES ); }
        if ( m_pMeshFaceSection != nullptr ) { m_pMeshFaceSection->setVisible( m_mode == map_element_mode_t::FACES ); }
        if ( m_pSubtractControls != nullptr ) { m_pSubtractControls->parentWidget()->setVisible( ( mode & kRootModes ) != 0u ); }
    }

    void RefreshButtons( bool documentChanged = false )
    {
        RefreshSelectionProfile();
        if ( m_pPrimitiveControls != nullptr ) { m_pPrimitiveControls->RefreshState(); }
        if ( m_pTextureState != nullptr ) {
            m_pTextureState->RefreshState();
            m_pTextureStateSection->setVisible( m_mode == map_element_mode_t::FACES || m_tool == map_tool_t::TEXTURE );
        }
        if ( m_pBlockDepthForm != nullptr ) {
            const bool solid = MapWorkspace_PrimitiveDefaults( m_pWorkspace, {} ).kind != map_primitive_kind_t::QUAD;
            m_pBlockDepthForm->setRowVisible( m_pBlockDepthControl, solid );
        }
        for ( auto *pControl : m_geometryControls ) { pControl->RefreshState(); }
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
        for ( QToolButton *pButton : m_buttons ) {
            const QByteArray id = pButton->objectName().toUtf8();
            const u32 state = EditorCommands_State( &m_pWorkspace->pGui->commands, { id.constData(), static_cast<usize>( id.size() ) } );
            const bool enabled = ( state & COMMAND_STATE_ENABLED ) != 0u;
            const QSignalBlocker blocker( pButton );
            pButton->setEnabled( enabled );
            pButton->setChecked( ( state & COMMAND_STATE_CHECKED ) != 0u );
            QString help = pButton->property( "commandHelp" ).toString();
            if ( !enabled ) { help += QStringLiteral( "\nUnavailable for the current tool or selection." ); }
            pButton->setToolTip( help );
        }
    }

    map_workspace_t *m_pWorkspace{ nullptr };
    QScrollArea *m_pScroll{ nullptr };
    QLabel *m_pToolIcon{ nullptr };
    QLabel *m_pSelection{ nullptr };
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
    primitive_controls_t *m_pPrimitiveControls{};
    QWidget *m_pBlockDepthControl{};
    QFormLayout *m_pBlockDepthForm{};
    face_texture_state_t *m_pTextureState{};
    QWidget *m_pTextureStateSection{};
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
        } parents[]{ { "World", map_visgroup_t::BRUSHES, map_visgroup_t::TIED }, { "Entities", map_visgroup_t::LIGHTS, map_visgroup_t::OTHER_ENTITIES } };
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
        for ( int p = 0; p < m_pTree->topLevelItemCount(); ++p ) {
            QTreeWidgetItem *pParent = m_pTree->topLevelItem( p );
            u32 total = 0u;
            for ( int c = 0; c < pParent->childCount(); ++c ) { total += pParent->child( c )->text( 1 ).toUInt(); }
            pParent->setText( 1, QString::number( total ) );
        }
        m_bSyncing = false;
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
        m_pSelect = new QPushButton( QStringLiteral( "Select" ), this );
        m_pSelect->setToolTip( QStringLiteral( "Select the set's objects (double-click a set does the same)" ) );
        pRow->addWidget( m_pSelect );
        pRow->addStretch( 1 );
        m_pTree = new QTreeWidget( this );
        m_pTree->setColumnCount( 2 );
        m_pTree->setHeaderLabels( { QStringLiteral( "Selection Set" ), QStringLiteral( "Num" ) } );
        m_pTree->setRootIsDecorated( false );
        m_pTree->header()->setStretchLastSection( false );
        m_pTree->header()->setSectionResizeMode( 0, QHeaderView::Stretch );
        m_pTree->header()->setSectionResizeMode( 1, QHeaderView::ResizeToContents );
        pLayout->addLayout( pRow );
        pLayout->addWidget( m_pTree );
        QObject::connect( m_pSelect, &QPushButton::clicked, this, [this]() { SelectCurrent(); } );
        QObject::connect( m_pTree, &QTreeWidget::itemDoubleClicked, this, [this]( QTreeWidgetItem *, int ) { SelectCurrent(); } );
        QObject::connect( m_pTree, &QTreeWidget::currentItemChanged, this, [this]( QTreeWidgetItem *pItem, QTreeWidgetItem * ) {
            m_pSelect->setEnabled( pItem != nullptr );
        } );
        ( void )MapWorkspace_AddListener( pWorkspace, &selection_sets_t::OnChanged, this );
        Rebuild();
    }

    ~selection_sets_t() override { MapWorkspace_RemoveListener( m_pWorkspace, &selection_sets_t::OnChanged, this ); }

private:
    static void OnChanged( void *pContext, u32 changes ) noexcept
    {
        if ( ( changes & MAP_CHANGE_DOCUMENT ) != 0u ) { static_cast<selection_sets_t *>( pContext )->Rebuild(); }
    }

    void Rebuild()
    {
        m_pTree->clear();
        const key_value_t *pRoot = SettingsDocument_Root( &m_pWorkspace->pDocument->root );
        const key_value_t *pSets = KeyValue_Find( pRoot, StringView_FromCString( "selection_sets" ) );
        for ( usize i = 0u; i < KeyValue_ChildCount( pSets ); ++i ) {
            const key_value_t *pSet = KeyValue_ChildAt( pSets, i );
            string_view_t name{};
            ( void )KeyValue_GetString( KeyValue_Find( pSet, StringView_FromCString( "name" ) ), &name );
            const key_value_t *pMembers = KeyValue_Find( pSet, StringView_FromCString( "members" ) );
            QList<QVariant> members;
            for ( usize m = 0u; m < KeyValue_ChildCount( pMembers ); ++m ) {
                u64 id = 0u;
                if ( KeyValue_GetU64( KeyValue_ChildAt( pMembers, m ), &id ) ) { members.append( QVariant::fromValue<qulonglong>( id ) ); }
            }
            auto *pItem = new QTreeWidgetItem( m_pTree, { FromView( name ), QString::number( members.size() ) } );
            pItem->setData( 0, kMembersRole, members );
        }
        m_pSelect->setEnabled( false );
    }

    // Members that no longer exist are skipped; the set keeps them.
    void SelectCurrent()
    {
        const QTreeWidgetItem *pItem = m_pTree->currentItem();
        if ( pItem == nullptr ) { return; }
        std::vector<u64> ids;
        for ( const QVariant &member : pItem->data( 0, kMembersRole ).toList() ) {
            const u64 id = member.toULongLong();
            if ( MapWireframe_FindObject( m_pWorkspace->wire, id ) != nullptr || MapWireframe_FindEntity( m_pWorkspace->wire, id ) != nullptr ) {
                ids.push_back( id );
            }
        }
        MapWorkspace_SetSelection( m_pWorkspace, ids.data(), ids.size() );
    }

    map_workspace_t *m_pWorkspace{ nullptr };
    QTreeWidget *m_pTree{ nullptr };
    QPushButton *m_pSelect{ nullptr };
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
