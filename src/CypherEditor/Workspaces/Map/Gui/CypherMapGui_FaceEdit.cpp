//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_FaceEdit.cpp
//  Purpose: Implements the texture editor (Hammer's Modify Texture).
//
//  History:
//  - Created by Karlo Siric on 2026-10-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_FaceEdit.h"

#include "CypherEditorGui_Style.h"
#include "CypherGeometry_DocumentBrushAttributes.h"

#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace cypher::editor::map
{
using namespace common;

namespace
{
class face_edit_t final : public QWidget
{
public:
    face_edit_t( QWidget *pParent, map_workspace_t *ws ) : QWidget( pParent ), m_ws( ws )
    {
        setObjectName( QStringLiteral( "MapFaceEdit" ) );
        auto *layout = new QVBoxLayout( this );
        layout->setContentsMargins( 6, 5, 6, 6 ); layout->setSpacing( 6 );
        m_target = new QLabel( this );
        m_target->setObjectName( QStringLiteral( "MapFaceEditTarget" ) );
        m_target->setWordWrap( true );
        layout->addWidget( m_target );

        // Material: the active one, applied with Shift+T.
        auto *material = new QHBoxLayout();
        m_material = new QLineEdit( this );
        m_material->setObjectName( QStringLiteral( "MapFaceEditMaterial" ) );
        m_material->setReadOnly( true );
        m_material->setToolTip( QStringLiteral( "The active material (Active Material panel, or the Eyedropper)" ) );
        material->addWidget( m_material, 1 );
        material->addWidget( Command( "MapFaceEditApply", "Apply", "map.tool.apply_material", "Apply the active material to the picked faces (Shift+T)" ) );
        layout->addLayout( material );

        auto *grid = new QGridLayout();
        grid->setHorizontalSpacing( 4 ); grid->setVerticalSpacing( 4 );
        grid->addWidget( new QLabel( QStringLiteral( "U" ), this ), 0, 1, Qt::AlignCenter );
        grid->addWidget( new QLabel( QStringLiteral( "V" ), this ), 0, 2, Qt::AlignCenter );
        // Scale, Hammer's convention: 0.25 is 128 units per repeat.
        grid->addWidget( new QLabel( QStringLiteral( "Scale" ), this ), 1, 0 );
        for ( int i = 0; i < 2; ++i ) {
            m_scale[i] = Spin( i == 0 ? "MapFaceEditScaleU" : "MapFaceEditScaleV", -64.0, 64.0, 0.05, 4, 0.25 );
            grid->addWidget( m_scale[i], 1, i + 1 );
        }
        auto *setScale = new QPushButton( QStringLiteral( "Set" ), this );
        setScale->setObjectName( QStringLiteral( "MapFaceEditSetScale" ) );
        setScale->setToolTip( QStringLiteral( "Give the picked faces this scale (0.25 is 128 world units per repeat)" ) );
        QObject::connect( setScale, &QPushButton::clicked, this, [this]() { Run( QStringLiteral( "map.texture.set_scale %1 %2" ).arg( m_scale[0]->value() ).arg( m_scale[1]->value() ) ); } );
        grid->addWidget( setScale, 1, 3 );
        // Shift: arrows move by the step, in world units.
        grid->addWidget( new QLabel( QStringLiteral( "Shift" ), this ), 2, 0 );
        m_shiftStep = Spin( "MapFaceEditShiftStep", 0.125, 4096.0, 1.0, 3, ws->gridSize > 0.0 ? ws->gridSize : 16.0 );
        m_shiftStep->setToolTip( QStringLiteral( "How far each arrow moves the material, in world units" ) );
        grid->addWidget( m_shiftStep, 2, 1 );
        auto *arrows = new QHBoxLayout(); arrows->setSpacing( 2 );
        const auto shift = [this]( f64 u, f64 v ) { Run( QStringLiteral( "map.texture.shift %1 %2" ).arg( u * m_shiftStep->value() ).arg( v * m_shiftStep->value() ) ); };
        arrows->addWidget( Button( "MapFaceEditShiftLeft", "←", "Shift left", [shift]() { shift( -1, 0 ); } ) );
        arrows->addWidget( Button( "MapFaceEditShiftRight", "→", "Shift right", [shift]() { shift( 1, 0 ); } ) );
        arrows->addWidget( Button( "MapFaceEditShiftUp", "↑", "Shift up", [shift]() { shift( 0, 1 ); } ) );
        arrows->addWidget( Button( "MapFaceEditShiftDown", "↓", "Shift down", [shift]() { shift( 0, -1 ); } ) );
        grid->addLayout( arrows, 2, 2, 1, 2 );
        // Rotate by the step.
        grid->addWidget( new QLabel( QStringLiteral( "Rotate" ), this ), 3, 0 );
        m_rotateStep = Spin( "MapFaceEditRotateStep", 0.1, 360.0, 5.0, 2, 15.0 );
        m_rotateStep->setSuffix( QStringLiteral( "°" ) );
        grid->addWidget( m_rotateStep, 3, 1 );
        auto *turns = new QHBoxLayout(); turns->setSpacing( 2 );
        turns->addWidget( Button( "MapFaceEditRotateLeft", "↺", "Rotate counter-clockwise by the step", [this]() { Run( QStringLiteral( "map.texture.rotate %1" ).arg( m_rotateStep->value() ) ); } ) );
        turns->addWidget( Button( "MapFaceEditRotateRight", "↻", "Rotate clockwise by the step", [this]() { Run( QStringLiteral( "map.texture.rotate %1" ).arg( -m_rotateStep->value() ) ); } ) );
        turns->addStretch( 1 );
        grid->addLayout( turns, 3, 2, 1, 2 );
        grid->setColumnStretch( 1, 1 ); grid->setColumnStretch( 2, 1 );
        layout->addLayout( grid );

        m_current = new QLabel( this );
        m_current->setObjectName( QStringLiteral( "MapFaceEditCurrent" ) );
        m_current->setProperty( "muted", true );
        m_current->setWordWrap( true );
        layout->addWidget( m_current );

        // Justify and fit, align and flip: Hammer's buttons, one row each.
        layout->addLayout( Row( "Justify", { { "map.texture.justify_left", "L" }, { "map.texture.justify_right", "R" }, { "map.texture.justify_top", "T" },
                                             { "map.texture.justify_bottom", "B" }, { "map.texture.justify_center", "C" }, { "map.texture.fit", "Fit" } } ) );
        layout->addLayout( Row( "Align", { { "map.texture.align_world", "World" }, { "map.texture.align_face", "Face" }, { "map.texture.unwrap", "Islands" } } ) );
        auto *flip = new QHBoxLayout(); flip->setSpacing( 2 );
        flip->addWidget( new QLabel( QStringLiteral( "Flip" ), this ) );
        flip->addWidget( Button( "MapFaceEditFlipU", "U", "Mirror the material along U", [this]() { Run( QStringLiteral( "map.texture.scale -1 1" ) ); } ) );
        flip->addWidget( Button( "MapFaceEditFlipV", "V", "Mirror the material along V", [this]() { Run( QStringLiteral( "map.texture.scale 1 -1" ) ); } ) );
        flip->addStretch( 1 );
        layout->addLayout( flip );

        Refresh();
    }

    QDoubleSpinBox *Spin( const char *name, f64 low, f64 high, f64 step, int decimals, f64 value )
    {
        auto *spin = new QDoubleSpinBox( this );
        spin->setObjectName( QString::fromLatin1( name ) );
        spin->setRange( low, high ); spin->setSingleStep( step ); spin->setDecimals( decimals ); spin->setValue( value );
        spin->setKeyboardTracking( false );
        spin->setMinimumWidth( 40 );
        return spin;
    }
    template <typename Action>
    QToolButton *Button( const char *name, const char *text, const char *tip, Action action )
    {
        auto *button = new QToolButton( this );
        button->setObjectName( QString::fromLatin1( name ) );
        button->setText( QString::fromUtf8( text ) );
        button->setToolTip( QString::fromUtf8( tip ) );
        button->setMinimumWidth( 26 );
        QObject::connect( button, &QToolButton::clicked, this, action );
        m_buttons.push_back( button );
        return button;
    }
    QPushButton *Command( const char *name, const char *text, const char *command, const char *tip )
    {
        auto *button = new QPushButton( QString::fromUtf8( text ), this );
        button->setObjectName( QString::fromLatin1( name ) );
        button->setToolTip( QString::fromUtf8( tip ) );
        const QString line = QString::fromLatin1( command );
        QObject::connect( button, &QPushButton::clicked, this, [this, line]() { Run( line ); } );
        return button;
    }
    QHBoxLayout *Row( const char *label, std::initializer_list<std::pair<const char *, const char *>> commands )
    {
        auto *row = new QHBoxLayout(); row->setSpacing( 2 );
        auto *title = new QLabel( QString::fromUtf8( label ), this ); title->setMinimumWidth( 44 );
        row->addWidget( title );
        for ( const auto &[command, text] : commands ) {
            auto *button = new QToolButton( this );
            button->setObjectName( QStringLiteral( "MapFaceEdit_%1" ).arg( QLatin1String( command ) ) );
            button->setText( QString::fromUtf8( text ) );
            const auto *desc = EditorCommands_Find( &m_ws->pGui->commands, StringView_FromCString( command ) );
            button->setToolTip( desc != nullptr ? QString::fromUtf8( desc->pDescription ) : QString::fromLatin1( command ) );
            const QString line = QString::fromLatin1( command );
            QObject::connect( button, &QToolButton::clicked, this, [this, line]() { Run( line ); } );
            m_buttons.push_back( button );
            row->addWidget( button, 1 );
        }
        return row;
    }

public:
    void Refresh()
    {
        RefreshNow();
    }

private:
    void Run( const QString &line )
    {
        const QByteArray utf8 = line.toUtf8();
        ( void )EditorCommands_ExecuteLine( &m_ws->pGui->commands, StringView_FromCString( utf8.constData() ) );
    }

    void RefreshNow()
    {
        const auto material = EditorSettings_Text( &m_ws->pGui->settings, "editor.map.default_material", StringView_FromCString( "materials/dev/dev_grid.cymat" ) );
        m_material->setText( QString::fromUtf8( material.pData, static_cast<qsizetype>( material.cchLength ) ) );
        const bool enabled = ( EditorCommands_State( &m_ws->pGui->commands, StringView_FromCString( "map.texture.shift" ) ) & COMMAND_STATE_ENABLED ) != 0u;
        for ( auto *button : m_buttons ) { button->setEnabled( enabled ); }
        for ( auto *spin : m_scale ) { spin->setEnabled( enabled ); }
        // What the controls act on.
        if ( MapWorkspace_HasBrushFace( m_ws ) ) { m_target->setText( QStringLiteral( "Brush #%1 · face #%2" ).arg( m_ws->selectedBrushFaceObject ).arg( m_ws->selectedBrushFaceSide ) ); }
        else if ( MapWorkspace_HasMeshFace( m_ws ) ) { m_target->setText( QStringLiteral( "Mesh #%1 · %2 picked faces" ).arg( m_ws->selectedMeshFaceObject ).arg( MapWorkspace_MeshFaces( m_ws ).nCount ) ); }
        else if ( enabled ) { m_target->setText( QStringLiteral( "Every face of the %1 selected objects" ).arg( m_ws->selection.ids.nCount ) ); }
        else { m_target->setText( QStringLiteral( "Pick faces in Faces mode, or select objects, to texture them." ) ); }
        // One brush face: its values (Hammer shows them in the fields).
        m_current->clear();
        if ( !MapWorkspace_HasBrushFace( m_ws ) || m_ws->pDocument == nullptr ) { return; }
        const auto *solid = geometry::GeometryDocument_FindBrush( &m_ws->pDocument->geometry, { m_ws->selectedBrushFaceObject } );
        const auto *store = geometry::GeometryDocument_FindBrushAttributes( &m_ws->pDocument->geometry, { m_ws->selectedBrushFaceObject } );
        for ( usize i = 0; solid != nullptr && store != nullptr && i < solid->sides.nCount; ++i ) {
            const auto &side = solid->sides.pData[i];
            if ( side.sourceId.value != m_ws->selectedBrushFaceSide || side.iAttributeIndex >= store->records.nCount ) { continue; }
            const auto &uv = store->records.pData[side.iAttributeIndex].uvProjection;
            const QSignalBlocker blockU( m_scale[0] ), blockV( m_scale[1] );
            m_scale[0]->setValue( uv.worldUnitsPerUv.x / 512.0 ); m_scale[1]->setValue( uv.worldUnitsPerUv.y / 512.0 );
            m_current->setText( QStringLiteral( "This face: scale %1 × %2, shift %3 × %4, rotation %5°" )
                .arg( uv.worldUnitsPerUv.x / 512.0, 0, 'g', 4 ).arg( uv.worldUnitsPerUv.y / 512.0, 0, 'g', 4 )
                .arg( uv.offset.x, 0, 'g', 4 ).arg( uv.offset.y, 0, 'g', 4 ).arg( uv.rotationRadians * 180.0 / M_PI, 0, 'g', 4 ) );
            break;
        }
    }

    map_workspace_t *m_ws{};
    QLabel *m_target{}, *m_current{};
    QLineEdit *m_material{};
    QDoubleSpinBox *m_scale[2]{}, *m_shiftStep{}, *m_rotateStep{};
    std::vector<QAbstractButton *> m_buttons;
};
} // namespace

QWidget *MapFaceEdit_Create( QWidget *pParent, map_workspace_t *pWorkspace ) noexcept
{
    if ( pWorkspace == nullptr || pWorkspace->pGui == nullptr ) { return nullptr; }
    return new face_edit_t( pParent, pWorkspace );
}

void MapFaceEdit_Refresh( QWidget *pEditor ) noexcept
{
    if ( auto *editor = dynamic_cast<face_edit_t *>( pEditor ) ) { editor->Refresh(); }
}

} // namespace cypher::editor::map
