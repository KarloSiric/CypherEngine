//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_FastTexture.cpp
//  Purpose: Implements the Fast Texture tool window.
//
//  History:
//  - Created by Karlo Siric on 2026-10-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_FastTexture.h"
#include "CypherMapGui_GeometryCommands.h"
#include "CypherMapGui_Hotspots.h"
#include "CypherMapGui_Notice.h"
#include "CypherMap_TextureEdit.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <functional>

namespace cypher::editor::map
{
using namespace common;

namespace
{
// The texture with its grid; a drag picks a rectangle, a click one cell.
class texture_canvas_t final : public QWidget {
public:
    std::function<void( QRectF )> onChosen;

    explicit texture_canvas_t( QWidget *pParent ) : QWidget( pParent )
    {
        setObjectName( QStringLiteral( "MapFastTextureCanvas" ) );
        setMinimumSize( 360, 360 );
        setMouseTracking( true );
        setCursor( Qt::CrossCursor );
    }
    void SetImage( const QImage &image ) { m_image = image; update(); }
    void SetDivisions( int divisions ) { m_divisions = std::max( 1, divisions ); update(); }
    void SetRect( QRectF uv ) { m_rect = uv; update(); }
    QRectF Rect() const { return m_rect; }
    void SetHotspots( QVector<QRectF> hotspots ) { m_hotspots = std::move( hotspots ); update(); }

protected:
    void paintEvent( QPaintEvent * ) override
    {
        QPainter painter( this );
        painter.fillRect( rect(), QColor( 24, 24, 28 ) );
        const QRectF area = Area();
        if ( m_image.isNull() ) {
            // No image: a checker so the grid still reads as a texture.
            for ( int y = 0; y < 8; ++y ) { for ( int x = 0; x < 8; ++x ) {
                painter.fillRect( QRectF( area.left() + area.width() * x / 8, area.top() + area.height() * y / 8, area.width() / 8, area.height() / 8 ),
                                  ( x + y ) % 2 ? QColor( 70, 70, 76 ) : QColor( 96, 96, 104 ) );
            } }
        } else {
            painter.drawImage( area, m_image );
        }
        painter.setPen( QPen( QColor( 255, 255, 255, 70 ), 1.0 ) );
        for ( int k = 1; k < m_divisions; ++k ) {
            const f64 t = static_cast<f64>( k ) / m_divisions;
            painter.drawLine( QPointF( area.left() + area.width() * t, area.top() ), QPointF( area.left() + area.width() * t, area.bottom() ) );
            painter.drawLine( QPointF( area.left(), area.top() + area.height() * t ), QPointF( area.right(), area.top() + area.height() * t ) );
        }
        // The material's hotspot regions (Hammer's rect editor): a click in
        // one picks it exactly.
        painter.setPen( QPen( QColor( 70, 200, 230, 200 ), 1.5 ) );
        for ( const QRectF &h : m_hotspots ) {
            painter.drawRect( QRectF( area.left() + h.left() * area.width(), area.top() + h.top() * area.height(), h.width() * area.width(), h.height() * area.height() ) );
        }
        if ( m_rect.isValid() ) {
            const QRectF chosen( area.left() + m_rect.left() * area.width(), area.top() + m_rect.top() * area.height(), m_rect.width() * area.width(), m_rect.height() * area.height() );
            // Outside the rectangle dims, as in Hammer's rect editor.
            QPainterPath outside; outside.addRect( area ); QPainterPath inside; inside.addRect( chosen );
            painter.fillPath( outside.subtracted( inside ), QColor( 0, 0, 0, 120 ) );
            painter.setPen( QPen( QColor( 255, 205, 40 ), 2.0 ) );
            painter.drawRect( chosen );
        }
        painter.setPen( QPen( QColor( 140, 140, 150 ), 1.0 ) );
        painter.setBrush( Qt::NoBrush );
        painter.drawRect( area );
    }
    void mousePressEvent( QMouseEvent *pEvent ) override
    {
        if ( pEvent->button() != Qt::LeftButton ) { return; }
        m_anchor = Snap( UvAt( pEvent->position() ) ); m_press = pEvent->position(); m_bDragging = true;
    }
    void mouseMoveEvent( QMouseEvent *pEvent ) override
    {
        if ( !m_bDragging ) { return; }
        m_rect = QRectF( m_anchor, Snap( UvAt( pEvent->position() ) ) ).normalized();
        update();
    }
    void mouseReleaseEvent( QMouseEvent *pEvent ) override
    {
        if ( !m_bDragging || pEvent->button() != Qt::LeftButton ) { return; }
        m_bDragging = false;
        QRectF chosen = QRectF( m_anchor, Snap( UvAt( pEvent->position() ) ) ).normalized();
        if ( QLineF( m_press, pEvent->position() ).length() < 4.0 || chosen.width() <= 0.0 || chosen.height() <= 0.0 ) {
            // A click: the hotspot region under it (the smallest), else the
            // grid cell.
            const QPointF uv = UvAt( pEvent->position() );
            const QRectF *hit = nullptr;
            for ( const QRectF &h : m_hotspots ) { if ( h.contains( uv ) && ( hit == nullptr || h.width() * h.height() < hit->width() * hit->height() ) ) { hit = &h; } }
            if ( hit != nullptr ) { m_rect = *hit; update(); if ( onChosen ) { onChosen( *hit ); } return; }
            const f64 cell = 1.0 / m_divisions;
            const f64 u = std::min( std::floor( uv.x() / cell ), m_divisions - 1.0 ) * cell, v = std::min( std::floor( uv.y() / cell ), m_divisions - 1.0 ) * cell;
            chosen = QRectF( u, v, cell, cell );
        }
        m_rect = chosen; update();
        if ( onChosen ) { onChosen( chosen ); }
    }

private:
    // The image's place: as large as fits, keeping its aspect.
    QRectF Area() const
    {
        const QRectF bounds = QRectF( rect() ).adjusted( 8, 8, -8, -8 );
        const f64 aspect = m_image.isNull() || m_image.height() == 0 ? 1.0 : static_cast<f64>( m_image.width() ) / m_image.height();
        f64 w = bounds.width(), h = w / aspect;
        if ( h > bounds.height() ) { h = bounds.height(); w = h * aspect; }
        return QRectF( bounds.center().x() - w / 2, bounds.center().y() - h / 2, w, h );
    }
    QPointF UvAt( QPointF p ) const
    {
        const QRectF area = Area();
        return { std::clamp( ( p.x() - area.left() ) / area.width(), 0.0, 1.0 ), std::clamp( ( p.y() - area.top() ) / area.height(), 0.0, 1.0 ) };
    }
    QPointF Snap( QPointF uv ) const { return { std::round( uv.x() * m_divisions ) / m_divisions, std::round( uv.y() * m_divisions ) / m_divisions }; }

    QImage m_image{};
    int m_divisions{ 4 };
    QRectF m_rect{};
    QVector<QRectF> m_hotspots{};
    QPointF m_anchor{}, m_press{};
    bool m_bDragging{ false };
};

class fast_texture_t;
QHash<const map_workspace_t *, QPointer<fast_texture_t>> &Windows();

class fast_texture_t final : public QDialog {
public:
    fast_texture_t( QWidget *pParent, map_workspace_t *pWorkspace ) : QDialog( pParent ), m_pWorkspace( pWorkspace )
    {
        setObjectName( QStringLiteral( "MapFastTexture" ) );
        setWindowTitle( QStringLiteral( "Fast Texture" ) );
        setAttribute( Qt::WA_DeleteOnClose );
        auto *pLayout = new QVBoxLayout( this );
        m_pSummary = new QLabel( this );
        m_pSummary->setObjectName( QStringLiteral( "MapFastTextureSummary" ) );
        m_pSummary->setWordWrap( true );
        pLayout->addWidget( m_pSummary );
        m_pCanvas = new texture_canvas_t( this );
        m_pCanvas->onChosen = [this]( QRectF uv ) { ( void )Fit( uv ); };
        pLayout->addWidget( m_pCanvas, 1 );
        auto *pRow = new QHBoxLayout();
        m_pMode = new QComboBox( this );
        m_pMode->setObjectName( QStringLiteral( "MapFastTextureMode" ) );
        m_pMode->addItems( { QStringLiteral( "Use Existing" ), QStringLiteral( "Unwrap Planar" ), QStringLiteral( "Unwrap Square" ) } );
        m_pMode->setToolTip( QStringLiteral( "Use Existing keeps the faces' layout; Unwrap Planar projects them flat first; Unwrap Square fits each face on its own." ) );
        m_pGrid = new QComboBox( this );
        for ( int d : { 1, 2, 4, 8, 16, 32 } ) { m_pGrid->addItem( QStringLiteral( "Grid %1 × %1" ).arg( d ), d ); }
        m_pGrid->setCurrentIndex( 2 );
        m_pFlipU = new QCheckBox( QStringLiteral( "Flip U" ), this );
        m_pFlipV = new QCheckBox( QStringLiteral( "Flip V" ), this );
        m_pInset = new QSpinBox( this );
        m_pInset->setRange( 0, 256 ); m_pInset->setSuffix( QStringLiteral( " px inset" ) );
        m_pInset->setToolTip( QStringLiteral( "Keep the fit this many texture pixels inside the rectangle (trims bleed less)." ) );
        pRow->addWidget( m_pMode ); pRow->addWidget( m_pGrid ); pRow->addWidget( m_pFlipU ); pRow->addWidget( m_pFlipV ); pRow->addWidget( m_pInset );
        pLayout->addLayout( pRow );
        auto *pButtons = new QHBoxLayout();
        auto *pWhole = new QPushButton( QStringLiteral( "Whole Texture" ), this );
        pWhole->setToolTip( QStringLiteral( "Fit the faces to the whole texture once" ) );
        auto *pClose = new QPushButton( QStringLiteral( "Close" ), this );
        pButtons->addWidget( new QLabel( QStringLiteral( "Drag a rectangle on the texture, or click a grid cell." ), this ), 1 );
        pButtons->addWidget( pWhole ); pButtons->addWidget( pClose );
        pLayout->addLayout( pButtons );
        // Hotspot regions of this material (materials/x.cyrect).
        auto *pHotspots = new QHBoxLayout();
        m_pHotspotInfo = new QLabel( this );
        m_pHotspotInfo->setObjectName( QStringLiteral( "MapFastTextureHotspots" ) );
        m_pHotspotInfo->setText( QStringLiteral( "No hotspot regions" ) );
        m_pTileU = new QCheckBox( QStringLiteral( "Tile U" ), this );
        m_pTileV = new QCheckBox( QStringLiteral( "Tile V" ), this );
        m_pTexel = new QDoubleSpinBox( this );
        m_pTexel->setRange( 0.01, 64.0 ); m_pTexel->setDecimals( 3 ); m_pTexel->setValue( 0.25 ); m_pTexel->setSuffix( QStringLiteral( " u/texel" ) );
        m_pTexel->setToolTip( QStringLiteral( "World units one texel covers: a new region's world size is its size in texels times this." ) );
        auto *pAdd = new QPushButton( QStringLiteral( "Add Hotspot" ), this );
        pAdd->setObjectName( QStringLiteral( "MapFastTextureAddHotspot" ) );
        pAdd->setToolTip( QStringLiteral( "Save the chosen rectangle as a hotspot region of this material (Tile U/V: a trim that may repeat that way)." ) );
        auto *pRemove = new QPushButton( QStringLiteral( "Remove" ), this );
        pRemove->setObjectName( QStringLiteral( "MapFastTextureRemoveHotspot" ) );
        pRemove->setToolTip( QStringLiteral( "Remove the hotspot region under the chosen rectangle's centre." ) );
        auto *pApply = new QPushButton( QStringLiteral( "Apply Hotspots" ), this );
        pApply->setObjectName( QStringLiteral( "MapFastTextureApplyHotspots" ) );
        pApply->setToolTip( QStringLiteral( "Each picked face takes the region that fits its size best." ) );
        pHotspots->addWidget( m_pHotspotInfo, 1 ); pHotspots->addWidget( m_pTileU ); pHotspots->addWidget( m_pTileV ); pHotspots->addWidget( m_pTexel );
        pHotspots->addWidget( pAdd ); pHotspots->addWidget( pRemove ); pHotspots->addWidget( pApply );
        pLayout->addLayout( pHotspots );
        QObject::connect( pAdd, &QPushButton::clicked, this, [this]() { ( void )AddHotspot(); } );
        QObject::connect( pRemove, &QPushButton::clicked, this, [this]() { ( void )RemoveHotspot(); } );
        QObject::connect( pApply, &QPushButton::clicked, this, [this]() { ( void )MapGeometryCommands_ApplyHotspots( m_pWorkspace ); } );
        QObject::connect( m_pGrid, &QComboBox::currentIndexChanged, this, [this]( int ) { m_pCanvas->SetDivisions( m_pGrid->currentData().toInt() ); } );
        QObject::connect( pWhole, &QPushButton::clicked, this, [this]() { ( void )Fit( QRectF( 0, 0, 1, 1 ) ); } );
        QObject::connect( pClose, &QPushButton::clicked, this, &QDialog::close );
        ( void )MapWorkspace_AddListener( pWorkspace, &fast_texture_t::OnChanged, this );
        resize( 560, 640 );
        Refresh();
    }
    ~fast_texture_t() override { DetachWorkspace(); }

    void done( int result ) override
    {
        // Close, Escape and reject can queue deletion. Retire the cache and
        // listener now so an immediate Show cannot revive a doomed dialog.
        DetachWorkspace();
        QDialog::done( result );
    }

    bool Fit( QRectF uv )
    {
        if ( m_pWorkspace == nullptr ) { return false; }
        m_pCanvas->SetRect( uv );
        // The inset, from texture pixels to texture coordinates.
        const f64 width = m_image.isNull() ? 256.0 : m_image.width(), height = m_image.isNull() ? 256.0 : m_image.height();
        const f64 du = std::min( m_pInset->value() / width, uv.width() * 0.49 ), dv = std::min( m_pInset->value() / height, uv.height() * 0.49 );
        map_uv_rect_t rect{};
        rect.minimum = { uv.left() + du, uv.top() + dv }; rect.maximum = { uv.right() - du, uv.bottom() - dv };
        rect.bFlipU = m_pFlipU->isChecked(); rect.bFlipV = m_pFlipV->isChecked();
        return MapGeometryCommands_FitTexture( m_pWorkspace, rect, static_cast<map_uv_fit_mode_t>( m_pMode->currentIndex() ) );
    }
    QString Material() const { return m_material; }
    usize HotspotCount() const { return m_hotspots.size(); }

    // The chosen rectangle as a new region of the material's file.
    bool AddHotspot()
    {
        if ( m_pWorkspace == nullptr ) { return false; }
        const QRectF uv = m_pCanvas->Rect();
        if ( m_material.isEmpty() || !uv.isValid() ) { MapNotice_Post( m_pWorkspace, map_notice_t::ERROR, QStringLiteral( "Add Hotspot: pick faces and choose a rectangle first" ) ); return false; }
        const f64 width = m_image.isNull() ? 512.0 : m_image.width(), height = m_image.isNull() ? 512.0 : m_image.height();
        geometry::mesh_hotspot_rect_t rect{};
        rect.uvMin = { uv.left(), uv.top() }; rect.uvMax = { uv.right(), uv.bottom() };
        rect.worldSize = { uv.width() * width * m_pTexel->value(), uv.height() * height * m_pTexel->value() };
        rect.bTileU = m_pTileU->isChecked(); rect.bTileV = m_pTileV->isChecked();
        auto rects = m_hotspots;
        rects.push_back( rect );
        return SaveHotspots( rects, QStringLiteral( "Hotspot added" ) );
    }
    bool RemoveHotspot()
    {
        if ( m_pWorkspace == nullptr ) { return false; }
        const QPointF centre = m_pCanvas->Rect().center();
        auto rects = m_hotspots;
        const auto it = std::find_if( rects.begin(), rects.end(), [&]( const geometry::mesh_hotspot_rect_t &r ) {
            return centre.x() >= r.uvMin.x && centre.x() <= r.uvMax.x && centre.y() >= r.uvMin.y && centre.y() <= r.uvMax.y;
        } );
        if ( it == rects.end() ) { MapNotice_Post( m_pWorkspace, map_notice_t::INFO, QStringLiteral( "Remove: no hotspot region under the chosen rectangle" ) ); return false; }
        rects.erase( it );
        return SaveHotspots( rects, QStringLiteral( "Hotspot removed" ) );
    }

protected:
    void closeEvent( QCloseEvent *event ) override
    {
        // Closing an already hidden QDialog can bypass done().
        DetachWorkspace();
        QDialog::closeEvent( event );
    }

private:
    void DetachWorkspace()
    {
        if ( m_pWorkspace == nullptr ) { return; }
        auto *workspace = m_pWorkspace;
        m_pWorkspace = nullptr;
        MapWorkspace_RemoveListener( workspace, &fast_texture_t::OnChanged, this );
        auto &windows = Windows();
        const auto it = windows.find( workspace );
        if ( it != windows.end() && it.value().data() == this ) { windows.erase( it ); }
        setEnabled( false );
    }

    static void OnChanged( void *pContext, u32 changes )
    {
        auto *pWindow = static_cast<fast_texture_t *>( pContext );
        if ( ( changes & MAP_CHANGE_SHUTDOWN ) != 0u ) {
            pWindow->DetachWorkspace();
            pWindow->close(); // Deletes itself (WA_DeleteOnClose).
            return;
        }
        if ( ( changes & ( MAP_CHANGE_SELECTION | MAP_CHANGE_DOCUMENT ) ) != 0u ) { pWindow->Refresh(); }
    }
    // The first target's material, from the wireframe's faces.
    QString TargetMaterial( const std::vector<map_surface_ref_t> &targets ) const
    {
        if ( targets.empty() || m_pWorkspace->pDocument == nullptr ) { return QString(); }
        const auto &wire = m_pWorkspace->wire;
        for ( usize f = 0; f < wire.faces.nCount; ++f ) {
            const auto &face = wire.faces.pData[f];
            if ( face.id != targets[0].object || ( targets[0].face != 0 && face.sideId != targets[0].face && face.faceId != targets[0].face ) ) { continue; }
            const string_view_t path = MapMaterials_Path( &m_pWorkspace->pDocument->materials, face.material );
            return QString::fromUtf8( path.pData, static_cast<qsizetype>( path.cchLength ) );
        }
        return QString();
    }
    bool SaveHotspots( const std::vector<geometry::mesh_hotspot_rect_t> &rects, const QString &what )
    {
        QString file;
        if ( !MapHotspots_Save( m_pWorkspace, m_material, rects, &file ) ) {
            MapNotice_Post( m_pWorkspace, map_notice_t::ERROR, file.isEmpty() ? QStringLiteral( "Hotspots: %1 is not in a content folder" ).arg( m_material )
                                                                             : QStringLiteral( "Hotspots: could not write %1" ).arg( file ) );
            return false;
        }
        LoadHotspots();
        MapNotice_Post( m_pWorkspace, map_notice_t::SUCCESS, QStringLiteral( "%1: %2 regions in %3" ).arg( what ).arg( rects.size() ).arg( file ) );
        return true;
    }
    void LoadHotspots()
    {
        m_hotspots = m_material.isEmpty() ? std::vector<geometry::mesh_hotspot_rect_t>{} : MapHotspots_Load( m_pWorkspace, m_material );
        QVector<QRectF> shown;
        for ( const auto &r : m_hotspots ) { shown.append( QRectF( QPointF( r.uvMin.x, r.uvMin.y ), QPointF( r.uvMax.x, r.uvMax.y ) ) ); }
        m_pCanvas->SetHotspots( shown );
        m_pHotspotInfo->setText( m_hotspots.empty() ? QStringLiteral( "No hotspot regions" ) : QStringLiteral( "%1 hotspot regions" ).arg( m_hotspots.size() ) );
    }
    void Refresh()
    {
        if ( m_pWorkspace == nullptr ) { return; }
        const auto targets = MapGeometryCommands_TextureTargets( m_pWorkspace );
        const QString material = TargetMaterial( targets );
        if ( material != m_material ) {
            m_material = material;
            m_image = material.isEmpty() ? QImage() : MapWorkspace_MaterialImage( m_pWorkspace, material );
            m_pCanvas->SetImage( m_image );
            LoadHotspots();
        }
        m_pSummary->setText( targets.empty() ? QStringLiteral( "Pick faces (Faces mode) or select brushes or meshes." )
                                             : QStringLiteral( "%1 · %2" ).arg( targets.size() == 1 && targets[0].face != 0 ? QStringLiteral( "1 face" ) :
                                                                                QStringLiteral( "%1 targets" ).arg( targets.size() ),
                                                                                material.isEmpty() ? QStringLiteral( "no material" ) : material ) );
    }

    map_workspace_t *m_pWorkspace{ nullptr };
    texture_canvas_t *m_pCanvas{ nullptr };
    QLabel *m_pSummary{ nullptr };
    QComboBox *m_pMode{ nullptr };
    QComboBox *m_pGrid{ nullptr };
    QCheckBox *m_pFlipU{ nullptr };
    QCheckBox *m_pFlipV{ nullptr };
    QSpinBox *m_pInset{ nullptr };
    QLabel *m_pHotspotInfo{ nullptr };
    QCheckBox *m_pTileU{ nullptr };
    QCheckBox *m_pTileV{ nullptr };
    QDoubleSpinBox *m_pTexel{ nullptr };
    QImage m_image{};
    QString m_material{};
    std::vector<geometry::mesh_hotspot_rect_t> m_hotspots{};
};

QHash<const map_workspace_t *, QPointer<fast_texture_t>> &Windows()
{
    static QHash<const map_workspace_t *, QPointer<fast_texture_t>> windows;
    return windows;
}
} // namespace

QDialog *MapFastTexture_Show( map_workspace_t *pWorkspace, QWidget *pParent ) noexcept
{
    if ( pWorkspace == nullptr ) { return nullptr; }
    QPointer<fast_texture_t> &window = Windows()[pWorkspace];
    if ( window.isNull() ) { window = new fast_texture_t( pParent, pWorkspace ); }
    window->show(); window->raise(); window->activateWindow();
    return window.data();
}

bool MapFastTexture_Choose( QDialog *pDialog, QRectF uv )
{
    auto *pWindow = dynamic_cast<fast_texture_t *>( pDialog );
    return pWindow != nullptr && pWindow->Fit( uv );
}

usize MapFastTexture_HotspotCount( QDialog *pDialog )
{
    auto *pWindow = dynamic_cast<fast_texture_t *>( pDialog );
    return pWindow != nullptr ? pWindow->HotspotCount() : 0u;
}

QString MapFastTexture_Material( QDialog *pDialog )
{
    auto *pWindow = dynamic_cast<fast_texture_t *>( pDialog );
    return pWindow != nullptr ? pWindow->Material() : QString();
}

} // namespace cypher::editor::map
