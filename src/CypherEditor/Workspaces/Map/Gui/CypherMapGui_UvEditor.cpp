//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_UvEditor.cpp
//  Purpose: Implements the UV Editor panel.
//
//  History:
//  - Created by Karlo Siric on 2026-10-14
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_UvEditor.h"

#include "CypherMapGui_Notice.h"

#include "CypherEditorGui_Style.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>

namespace cypher::editor::map
{
using namespace common;

namespace
{
using vec2 = math::vec2d_t;

constexpr f64 kPi = 3.14159265358979323846;
constexpr f64 kRing = 48.0;   // Rotation ring radius, screen pixels.
constexpr f64 kReach = 26.0;  // Scale handles beyond the ring.
constexpr f64 kGrab = 7.0;    // How close a press must be to a handle.

// uv' = M uv + t, for drawing a drag before it is applied.
struct affine2_t {
    f64 a{ 1 }, b{ 0 }, c{ 0 }, d{ 1 }, tx{ 0 }, ty{ 0 };
    vec2 Apply( vec2 p ) const noexcept { return { a * p.x + b * p.y + tx, c * p.x + d * p.y + ty }; }
};
affine2_t About( vec2 pivot, f64 a, f64 b, f64 c, f64 d ) noexcept
{
    return { a, b, c, d, pivot.x - ( a * pivot.x + b * pivot.y ), pivot.y - ( c * pivot.x + d * pivot.y ) };
}

f64 Snap( f64 value, f64 step ) noexcept { return step > 0 ? std::round( value / step ) * step : value; }

class uv_canvas_t final : public QWidget {
public:
    uv_canvas_t( QWidget *pParent, map_workspace_t *pWorkspace ) : QWidget( pParent ), m_pWorkspace( pWorkspace )
    {
        setObjectName( QStringLiteral( "MapUvCanvas" ) );
        setMouseTracking( true );
        setFocusPolicy( Qt::ClickFocus );
        setMinimumSize( 160, 160 );
    }

    std::function<void()> changed{}; // The status line should be read again.

    void Refresh( bool fit )
    {
        if ( m_drag != drag_t::NONE ) { return; } // View-only notices leave an active preview alone.
        m_faces.clear();
        for ( const map_surface_ref_t &surface : MapUvEditor_Surfaces( m_pWorkspace ) ) {
            map_uv_face_t face{};
            if ( MapTextureEdit_Describe( m_pWorkspace->pDocument, surface, &face ) == map_status_t::OK && face.uvs.size() >= 3 ) { m_faces.push_back( std::move( face ) ); }
        }
        const QString material = m_faces.empty() ? QString() : QString::fromStdString( m_faces.front().material );
        if ( material != m_material ) {
            m_material = material;
            m_image = material.isEmpty() ? QImage() : MapWorkspace_MaterialImage( m_pWorkspace, material );
            m_pixmap = m_image.isNull() ? QPixmap() : QPixmap::fromImage( m_image );
        }
        m_tw = m_image.isNull() ? 128.0 : std::max( 1, m_image.width() );
        m_th = m_image.isNull() ? 128.0 : std::max( 1, m_image.height() );
        if ( fit || !m_bPivot ) { ResetPivot(); }
        if ( fit ) { Fit(); }
        update();
        if ( changed ) { changed(); }
    }

    void Fit()
    {
        Cancel();
        if ( m_faces.empty() ) { m_centre = { m_tw * 0.5, m_th * 0.5 }; m_zoom = std::min( width(), height() ) * 0.8 / std::max( m_tw, m_th ); return; }
        f64 lx = 1e300, ly = 1e300, hx = -1e300, hy = -1e300;
        for ( const auto &face : m_faces ) {
            for ( const vec2 &uv : face.uvs ) { lx = std::min( lx, uv.x * m_tw ); hx = std::max( hx, uv.x * m_tw ); ly = std::min( ly, uv.y * m_th ); hy = std::max( hy, uv.y * m_th ); }
        }
        const f64 w = std::max( hx - lx, 1.0 ), h = std::max( hy - ly, 1.0 );
        m_centre = { ( lx + hx ) * 0.5, ( ly + hy ) * 0.5 };
        m_zoom = std::clamp( std::min( std::max( 1, width() ) / w, std::max( 1, height() ) / h ) * 0.7, 1.0 / 64.0, 64.0 );
        update();
    }

    QPointF ToScreen( vec2 uv ) const
    {
        return { ( uv.x * m_tw - m_centre.x() ) * m_zoom + width() * 0.5, ( uv.y * m_th - m_centre.y() ) * m_zoom + height() * 0.5 };
    }
    vec2 ToUv( QPointF screen ) const
    {
        return { ( ( screen.x() - width() * 0.5 ) / m_zoom + m_centre.x() ) / m_tw, ( ( screen.y() - height() * 0.5 ) / m_zoom + m_centre.y() ) / m_th };
    }

    // A preview belongs to one held button and one document/selection snapshot.
    // Cancellation never publishes an edit, including after a delayed release.
    void Cancel()
    {
        if ( m_drag == drag_t::PIVOT ) { m_pivot = m_pivotStart; m_bPivot = m_bPivotStart; }
        if ( m_drag == drag_t::PAN ) { m_centre = m_centreStart; }
        m_drag = drag_t::NONE;
        m_button = Qt::NoButton;
        m_edit = {};
        m_preview = {};
        update();
    }

    vec2 Pivot() const noexcept { return m_pivot; }
    QString Status() const
    {
        if ( m_faces.empty() ) { return QStringLiteral( "Select faces (Faces mode) to edit their texture layout" ); }
        QString text = QStringLiteral( "%1 %2 · %3 · %4×%5" ).arg( m_faces.size() ).arg( m_faces.size() == 1 ? QStringLiteral( "face" ) : QStringLiteral( "faces" ),
            m_material.isEmpty() ? QStringLiteral( "no material" ) : m_material ).arg( m_tw ).arg( m_th );
        if ( m_image.isNull() && !m_material.isEmpty() ) { text += QStringLiteral( " (image not loaded)" ); }
        return text;
    }

protected:
    bool event( QEvent *pEvent ) override
    {
        switch ( pEvent->type() ) {
            case QEvent::ShortcutOverride:
                if ( m_drag != drag_t::NONE && static_cast<QKeyEvent *>( pEvent )->key() == Qt::Key_Escape ) {
                    pEvent->accept();
                    return true;
                }
                break;
            case QEvent::FocusOut:
            case QEvent::Hide:
            case QEvent::WindowDeactivate:
            case QEvent::UngrabMouse: Cancel(); break;
            default: break;
        }
        return QWidget::event( pEvent );
    }

    void keyPressEvent( QKeyEvent *pEvent ) override
    {
        if ( pEvent->key() == Qt::Key_Escape && m_drag != drag_t::NONE ) {
            Cancel();
            pEvent->accept();
            return;
        }
        QWidget::keyPressEvent( pEvent );
    }

    void resizeEvent( QResizeEvent * ) override
    {
        Cancel(); // The press and current cursor must use the same screen-to-UV mapping.
        if ( !m_bViewed ) { Fit(); m_bViewed = !m_faces.empty(); }
    }

    void paintEvent( QPaintEvent * ) override
    {
        QPainter painter( this );
        const auto &style = m_pWorkspace->pGui->style;
        painter.fillRect( rect(), gui::EditorStyle_TokenColor( style, "viewport.background.2d" ) );
        if ( m_faces.empty() ) {
            painter.setPen( gui::EditorStyle_TokenColor( style, "ui.text.disabled" ) );
            painter.drawText( rect(), Qt::AlignCenter | Qt::TextWordWrap, Status() );
            return;
        }
        // The texture repeated across the view, in texel space.
        painter.save();
        painter.translate( width() * 0.5, height() * 0.5 );
        painter.scale( m_zoom, m_zoom );
        painter.translate( -m_centre.x(), -m_centre.y() );
        const QRectF visible( m_centre.x() - width() * 0.5 / m_zoom, m_centre.y() - height() * 0.5 / m_zoom, width() / m_zoom, height() / m_zoom );
        if ( !m_pixmap.isNull() ) {
            painter.fillRect( visible, QBrush( m_pixmap ) );
        } else {
            QPixmap checker( 16, 16 );
            checker.fill( QColor( 70, 70, 74 ) );
            QPainter cells( &checker );
            cells.fillRect( 0, 0, 8, 8, QColor( 92, 92, 98 ) );
            cells.fillRect( 8, 8, 8, 8, QColor( 92, 92, 98 ) );
            cells.end();
            QBrush brush( checker );
            brush.setTransform( QTransform::fromScale( m_tw / 16.0, m_th / 16.0 ) );
            painter.fillRect( visible, brush );
        }
        painter.restore();
        // Texel grid when zoomed in, and the edges of each repeat.
        const auto lines = [&]( f64 step, QColor colour ) {
            painter.setPen( QPen( colour, 1.0 ) );
            const vec2 low = ToUv( QPointF( 0, 0 ) ), high = ToUv( QPointF( width(), height() ) );
            for ( f64 u = std::floor( low.x * m_tw / step ) * step; u <= high.x * m_tw; u += step ) {
                const f64 x = ToScreen( { u / m_tw, 0 } ).x();
                painter.drawLine( QPointF( x, 0 ), QPointF( x, height() ) );
            }
            for ( f64 v = std::floor( low.y * m_th / step ) * step; v <= high.y * m_th; v += step ) {
                const f64 y = ToScreen( { 0, v / m_th } ).y();
                painter.drawLine( QPointF( 0, y ), QPointF( width(), y ) );
            }
        };
        if ( m_zoom >= 8.0 ) { lines( 1.0, QColor( 255, 255, 255, 24 ) ); }
        painter.setRenderHint( QPainter::Antialiasing, true );
        {
            const vec2 low = ToUv( QPointF( 0, 0 ) ), high = ToUv( QPointF( width(), height() ) );
            painter.setPen( QPen( QColor( 255, 255, 255, 110 ), 1.0, Qt::DashLine ) );
            for ( f64 u = std::floor( low.x ); u <= high.x; u += 1.0 ) { const f64 x = ToScreen( { u, 0 } ).x(); painter.drawLine( QPointF( x, 0 ), QPointF( x, height() ) ); }
            for ( f64 v = std::floor( low.y ); v <= high.y; v += 1.0 ) { const f64 y = ToScreen( { 0, v } ).y(); painter.drawLine( QPointF( 0, y ), QPointF( width(), y ) ); }
        }
        // The faces, the first in the selection colour; during a drag, where
        // the drag takes them.
        const QColor selected = gui::EditorStyle_TokenColor( style, "viewport.selection" );
        for ( usize f = m_faces.size(); f-- > 0; ) {
            QPolygonF polygon;
            for ( const vec2 &uv : m_faces[f].uvs ) { polygon << ToScreen( m_preview.Apply( uv ) ); }
            QColor fill = selected; fill.setAlpha( f == 0 ? 46 : 18 );
            QColor edge = f == 0 ? selected : QColor( 255, 255, 255, 150 );
            painter.setBrush( fill );
            painter.setPen( QPen( edge, f == 0 ? 2.0 : 1.0 ) );
            painter.drawPolygon( polygon );
            if ( f == 0 ) { for ( const QPointF &corner : polygon ) { painter.drawEllipse( corner, 2.5, 2.5 ); } }
        }
        // The handles about the pivot.
        const QPointF pivot = ToScreen( m_pivot );
        const QColor accent = gui::EditorStyle_TokenColor( style, "ui.accent" );
        painter.setBrush( Qt::NoBrush );
        painter.setPen( QPen( accent, m_drag == drag_t::ROTATE ? 2.4 : 1.4, Qt::DashLine ) );
        painter.drawEllipse( pivot, kRing, kRing );
        painter.setPen( QPen( accent, 1.6 ) );
        painter.drawLine( pivot + QPointF( -6, 0 ), pivot + QPointF( 6, 0 ) );
        painter.drawLine( pivot + QPointF( 0, -6 ), pivot + QPointF( 0, 6 ) );
        painter.drawEllipse( pivot, 4.0, 4.0 );
        const QPointF u = pivot + QPointF( kRing + kReach, 0 ), v = pivot + QPointF( 0, kRing + kReach );
        painter.drawLine( pivot + QPointF( kRing, 0 ), u );
        painter.drawLine( pivot + QPointF( 0, kRing ), v );
        painter.setBrush( accent );
        painter.drawRect( QRectF( u - QPointF( 4, 4 ), QSizeF( 8, 8 ) ) );
        painter.drawRect( QRectF( v - QPointF( 4, 4 ), QSizeF( 8, 8 ) ) );
        painter.setPen( gui::EditorStyle_TokenColor( style, "viewport.overlay.text" ) );
        painter.drawText( u + QPointF( 8, 4 ), QStringLiteral( "U" ) );
        painter.drawText( v + QPointF( 8, 4 ), QStringLiteral( "V" ) );
    }

    void wheelEvent( QWheelEvent *pEvent ) override
    {
        Cancel();
        const QPointF at = pEvent->position();
        const vec2 before = ToUv( at );
        m_zoom = std::clamp( m_zoom * std::pow( 1.2, pEvent->angleDelta().y() / 120.0 ), 1.0 / 64.0, 128.0 );
        const vec2 after = ToUv( at );
        m_centre += QPointF( ( before.x - after.x ) * m_tw, ( before.y - after.y ) * m_th );
        m_bViewed = true;
        update();
    }

    void mousePressEvent( QMouseEvent *pEvent ) override
    {
        if ( pEvent->button() != Qt::LeftButton && pEvent->button() != Qt::MiddleButton && pEvent->button() != Qt::RightButton ) { return; }
        Cancel();
        m_press = pEvent->position();
        m_button = pEvent->button();
        m_pivotStart = m_pivot;
        m_bPivotStart = m_bPivot;
        if ( m_button == Qt::MiddleButton || m_button == Qt::RightButton ) { m_drag = drag_t::PAN; m_centreStart = m_centre; return; }
        if ( m_faces.empty() ) { m_button = Qt::NoButton; return; }
        const QPointF pivot = ToScreen( m_pivot );
        const auto near = [&]( QPointF p, f64 reach ) { return QLineF( m_press, p ).length() <= reach; };
        const bool alt = ( pEvent->modifiers() & Qt::AltModifier ) != 0;
        if ( near( pivot, kGrab ) ) { m_drag = drag_t::PIVOT; }
        else if ( near( pivot + QPointF( kRing + kReach, 0 ), kGrab + 2 ) ) { m_drag = alt ? drag_t::SHEAR_U : drag_t::SCALE_U; }
        else if ( near( pivot + QPointF( 0, kRing + kReach ), kGrab + 2 ) ) { m_drag = alt ? drag_t::SHEAR_V : drag_t::SCALE_V; }
        else if ( std::fabs( QLineF( m_press, pivot ).length() - kRing ) <= kGrab ) { m_drag = drag_t::ROTATE; }
        else { m_drag = drag_t::MOVE; }
    }

    void mouseMoveEvent( QMouseEvent *pEvent ) override
    {
        const QPointF at = pEvent->position();
        if ( m_drag == drag_t::NONE ) { UpdateCursor( at ); return; }
        if ( ( pEvent->buttons() & m_button ) == 0 ) { Cancel(); return; }
        const bool free = ( pEvent->modifiers() & ( Qt::ControlModifier | Qt::MetaModifier ) ) != 0;
        const bool both = ( pEvent->modifiers() & Qt::ShiftModifier ) != 0;
        const QPointF pivotScreen = ToScreen( m_pivot );
        m_edit = map_uv_edit_t{};
        m_edit.pivot = m_pivot;
        switch ( m_drag ) {
            case drag_t::PAN:
                m_centre = m_centreStart - ( at - m_press ) / m_zoom;
                m_bViewed = true;
                break;
            case drag_t::PIVOT: {
                vec2 uv = ToUv( at );
                // Onto a corner when one is close, else onto whole texels.
                bool onCorner = false;
                for ( const vec2 &corner : m_faces.front().uvs ) {
                    if ( QLineF( ToScreen( corner ), at ).length() <= 10.0 ) { uv = corner; onCorner = true; break; }
                }
                if ( !onCorner && !free ) { uv = { Snap( uv.x * m_tw, 1.0 ) / m_tw, Snap( uv.y * m_th, 1.0 ) / m_th }; }
                m_pivot = uv;
                m_bPivot = true;
                break;
            }
            case drag_t::MOVE: {
                QPointF texels = ( at - m_press ) / m_zoom;
                if ( !free ) { texels = QPointF( std::round( texels.x() ), std::round( texels.y() ) ); }
                m_edit.kind = map_uv_edit_kind_t::SHIFT;
                m_edit.delta = { texels.x() / m_tw, texels.y() / m_th };
                m_preview = { 1, 0, 0, 1, m_edit.delta.x, m_edit.delta.y };
                break;
            }
            case drag_t::ROTATE: {
                const QPointF a = m_press - pivotScreen, b = at - pivotScreen;
                f64 angle = std::atan2( b.y(), b.x() ) - std::atan2( a.y(), a.x() );
                angle = std::remainder( angle, 2.0 * kPi );
                if ( !free ) { angle = Snap( angle, 15.0 * kPi / 180.0 ); }
                m_edit.kind = map_uv_edit_kind_t::ROTATE;
                m_edit.radians = angle;
                m_preview = About( m_pivot, std::cos( angle ), -std::sin( angle ), std::sin( angle ), std::cos( angle ) );
                break;
            }
            case drag_t::SCALE_U:
            case drag_t::SCALE_V: {
                const bool alongU = m_drag == drag_t::SCALE_U;
                const f64 from = alongU ? m_press.x() - pivotScreen.x() : m_press.y() - pivotScreen.y();
                const f64 to = alongU ? at.x() - pivotScreen.x() : at.y() - pivotScreen.y();
                f64 factor = std::fabs( from ) > 1.0 ? to / from : 1.0;
                if ( !free ) { factor = Snap( factor, 1.0 / 16.0 ); }
                if ( std::fabs( factor ) < 1.0 / 64.0 ) { factor = factor < 0 ? -1.0 / 64.0 : 1.0 / 64.0; }
                m_edit.kind = map_uv_edit_kind_t::SCALE;
                m_edit.factor = both ? vec2{ factor, factor } : alongU ? vec2{ factor, 1.0 } : vec2{ 1.0, factor };
                m_preview = About( m_pivot, m_edit.factor.x, 0, 0, m_edit.factor.y );
                break;
            }
            case drag_t::SHEAR_U:
            case drag_t::SHEAR_V: {
                // The handle sits (kRing + kReach) pixels from the pivot; moving it
                // across that axis slants the other.
                const f64 reach = ( kRing + kReach ) / m_zoom;
                vec2 shear{};
                if ( m_drag == drag_t::SHEAR_U ) { shear.y = ( ( at.y() - m_press.y() ) / m_zoom / m_th ) / ( reach / m_tw ); }
                else { shear.x = ( ( at.x() - m_press.x() ) / m_zoom / m_tw ) / ( reach / m_th ); }
                if ( !free ) { shear = { Snap( shear.x, 1.0 / 16.0 ), Snap( shear.y, 1.0 / 16.0 ) }; }
                m_edit.kind = map_uv_edit_kind_t::SHEAR;
                m_edit.shear = shear;
                m_preview = About( m_pivot, 1, shear.x, shear.y, 1 );
                break;
            }
            case drag_t::NONE: break;
        }
        update();
    }

    void mouseReleaseEvent( QMouseEvent *pEvent ) override
    {
        if ( m_drag == drag_t::NONE || pEvent->button() != m_button ) { return; }
        const drag_t drag = m_drag;
        const map_uv_edit_t edit = m_edit;
        // Clear the transaction before publication emits document/selection notices.
        // A click or a drag returning to its start leaves an identity pending edit.
        m_drag = drag_t::NONE;
        m_button = Qt::NoButton;
        m_edit = {};
        m_preview = {};
        if ( drag == drag_t::MOVE || drag == drag_t::ROTATE || drag == drag_t::SCALE_U || drag == drag_t::SCALE_V || drag == drag_t::SHEAR_U || drag == drag_t::SHEAR_V ) {
            Commit( edit );
        }
        update();
    }

private:
    enum class drag_t { NONE, MOVE, ROTATE, SCALE_U, SCALE_V, SHEAR_U, SHEAR_V, PIVOT, PAN };

    void ResetPivot()
    {
        m_bPivot = false;
        if ( m_faces.empty() ) { m_pivot = { 0.5, 0.5 }; return; }
        vec2 sum{};
        for ( const vec2 &uv : m_faces.front().uvs ) { sum.x += uv.x; sum.y += uv.y; }
        m_pivot = { sum.x / m_faces.front().uvs.size(), sum.y / m_faces.front().uvs.size() };
    }

    void UpdateCursor( QPointF at )
    {
        if ( m_faces.empty() ) { unsetCursor(); return; }
        const QPointF pivot = ToScreen( m_pivot );
        if ( QLineF( at, pivot ).length() <= kGrab ) { setCursor( Qt::SizeAllCursor ); }
        else if ( QLineF( at, pivot + QPointF( kRing + kReach, 0 ) ).length() <= kGrab + 2 ) { setCursor( Qt::SizeHorCursor ); }
        else if ( QLineF( at, pivot + QPointF( 0, kRing + kReach ) ).length() <= kGrab + 2 ) { setCursor( Qt::SizeVerCursor ); }
        else if ( std::fabs( QLineF( at, pivot ).length() - kRing ) <= kGrab ) { setCursor( Qt::CrossCursor ); }
        else { setCursor( Qt::OpenHandCursor ); }
    }

    bool Identity( const map_uv_edit_t &e ) const noexcept
    {
        switch ( e.kind ) {
            case map_uv_edit_kind_t::SHIFT: return e.delta.x == 0 && e.delta.y == 0;
            case map_uv_edit_kind_t::ROTATE: return e.radians == 0;
            case map_uv_edit_kind_t::SCALE: return e.factor.x == 1 && e.factor.y == 1;
            case map_uv_edit_kind_t::SHEAR: return e.shear.x == 0 && e.shear.y == 0;
        }
        return true;
    }

    void Commit( const map_uv_edit_t &edit )
    {
        if ( m_faces.empty() || Identity( edit ) ) { return; }
        struct job_t { std::vector<map_surface_ref_t> surfaces; map_uv_edit_t edit; std::vector<u64> keep; } job;
        for ( const auto &face : m_faces ) { job.surfaces.push_back( face.surface ); }
        job.edit = edit;
        job.keep.assign( m_pWorkspace->selection.ids.pData, m_pWorkspace->selection.ids.pData + m_pWorkspace->selection.ids.nCount );
        // The face selection comes back after the edit is published.
        const u64 brushObject = m_pWorkspace->selectedBrushFaceObject, brushSide = m_pWorkspace->selectedBrushFaceSide;
        const u64 meshObject = m_pWorkspace->selectedMeshFaceObject, activeFace = m_pWorkspace->selectedMeshFaceId;
        const auto meshFaces = MapWorkspace_MeshFaces( m_pWorkspace );
        const std::vector<u64> faces( meshFaces.pData, meshFaces.pData + meshFaces.nCount );
        const char *labels[]{ "Move Texture", "Rotate Texture", "Scale Texture", "Shear Texture" };
        const bool done = MapWorkspace_ApplyEdit( m_pWorkspace, labels[static_cast<u32>( edit.kind )], []( void *context, map_document_t *copy, vector_t<u64> *out ) noexcept {
            auto &j = *static_cast<job_t *>( context );
            for ( u64 id : j.keep ) { if ( !Vector_PushBack( out, id ) ) { return map_status_t::OUT_OF_MEMORY; } }
            return MapTextureEdit_EditUv( copy, { j.surfaces.data(), j.surfaces.size() }, j.edit );
        }, &job );
        if ( !done ) {
            MapNotice_Post( m_pWorkspace, map_notice_t::WARNING, QStringLiteral( "UV Editor: the texture could not be changed that way" ) );
            return;
        }
        if ( brushObject != 0 && !MapWorkspace_HasBrushFace( m_pWorkspace ) ) { MapWorkspace_SelectBrushFace( m_pWorkspace, brushObject, brushSide ); }
        if ( meshObject != 0 && !faces.empty() && MapWorkspace_MeshFaces( m_pWorkspace ).nCount == 0 ) {
            ( void )MapWorkspace_SetMeshFaces( m_pWorkspace, meshObject, { faces.data(), faces.size() }, activeFace );
        }
        // The pivot moves with a move, so the next turn is about the same texel of the face.
        if ( edit.kind == map_uv_edit_kind_t::SHIFT ) {
            // Publication may already have refreshed an implicit centre pivot.
            // Follow the captured pivot once, rather than adding to that new centre.
            m_pivot = { edit.pivot.x + edit.delta.x, edit.pivot.y + edit.delta.y };
            m_bPivot = true;
        }
    }

    map_workspace_t *m_pWorkspace{ nullptr };
    std::vector<map_uv_face_t> m_faces;
    QString m_material;
    QImage m_image;
    QPixmap m_pixmap;
    f64 m_tw{ 128.0 }, m_th{ 128.0 };
    f64 m_zoom{ 1.0 };
    QPointF m_centre{ 64.0, 64.0 };
    bool m_bViewed{ false };
    vec2 m_pivot{ 0.5, 0.5 };
    bool m_bPivot{ false };
    drag_t m_drag{ drag_t::NONE };
    Qt::MouseButton m_button{ Qt::NoButton };
    vec2 m_pivotStart{};
    bool m_bPivotStart{ false };
    QPointF m_press{}, m_centreStart{};
    affine2_t m_preview{};
    map_uv_edit_t m_edit{};
};

class uv_editor_t final : public QWidget {
public:
    uv_editor_t( QWidget *pParent, map_workspace_t *pWorkspace ) : QWidget( pParent ), m_pWorkspace( pWorkspace )
    {
        setObjectName( QStringLiteral( "MapUvEditor" ) );
        auto *pLayout = new QVBoxLayout( this );
        pLayout->setContentsMargins( 0, 0, 0, 0 );
        pLayout->setSpacing( 2 );
        auto *pBar = new QHBoxLayout();
        pBar->setContentsMargins( 2, 2, 2, 0 );
        pBar->setSpacing( 1 );
        const auto button = [&]( const char *pCommand, const char *pIcon, const QString &tip ) {
            auto *pButton = new QToolButton( this );
            pButton->setAutoRaise( true );
            pButton->setIcon( gui::EditorStyle_Icon( pWorkspace->pGui->style, pIcon ) );
            pButton->setToolTip( tip );
            const QByteArray command( pCommand );
            QObject::connect( pButton, &QToolButton::clicked, this, [this, command]() {
                ( void )EditorCommands_Execute( &m_pWorkspace->pGui->commands, StringView_FromCString( command.constData() ), {} );
            } );
            pBar->addWidget( pButton );
            return pButton;
        };
        button( "map.texture.rotate_ccw", "uv-rotate", QStringLiteral( "Rotate 90° counter-clockwise (Alt+Q)" ) );
        button( "map.texture.rotate_cw", "uv-rotate", QStringLiteral( "Rotate 90° clockwise (Alt+A)" ) );
        button( "map.texture.flip_horizontal", "uv-scale", QStringLiteral( "Flip horizontally (Alt+D)" ) );
        button( "map.texture.flip_vertical", "uv-scale", QStringLiteral( "Flip vertically (Alt+E)" ) );
        button( "map.texture.fit", "uv-fit", QStringLiteral( "Fit one repeat to each face" ) );
        button( "map.texture.align_face", "uv-align-face", QStringLiteral( "Reset: project in the face's plane" ) );
        auto *pFit = new QToolButton( this );
        pFit->setAutoRaise( true );
        pFit->setIcon( gui::EditorStyle_Icon( pWorkspace->pGui->style, "view-frame" ) );
        pFit->setToolTip( QStringLiteral( "Frame the faces" ) );
        pBar->addWidget( pFit );
        pBar->addStretch( 1 );
        m_pCanvas = new uv_canvas_t( this, pWorkspace );
        m_pStatus = new QLabel( this );
        m_pStatus->setObjectName( QStringLiteral( "MapUvStatus" ) );
        m_pStatus->setProperty( "muted", true );
        m_pStatus->setWordWrap( true );
        m_pStatus->setToolTip( QStringLiteral( "Drag the faces to move the texture · the ring turns it · a square scales it (Shift: both axes; Alt: shear) · "
                                               "the cross is the pivot · Ctrl drags freely · wheel zooms · middle or right drag pans" ) );
        pLayout->addLayout( pBar );
        pLayout->addWidget( m_pCanvas, 1 );
        pLayout->addWidget( m_pStatus );
        QObject::connect( pFit, &QToolButton::clicked, this, [this]() { m_pCanvas->Fit(); } );
        m_pCanvas->changed = [this]() { m_pStatus->setText( m_pCanvas->Status() ); };
        ( void )MapWorkspace_AddListener( pWorkspace, &uv_editor_t::OnChanged, this );
        m_selection = Key();
        m_pCanvas->Refresh( true );
    }
    ~uv_editor_t() override { MapWorkspace_RemoveListener( m_pWorkspace, &uv_editor_t::OnChanged, this ); }

    uv_canvas_t *Canvas() const noexcept { return m_pCanvas; }

private:
    QString Key() const
    {
        QStringList parts;
        for ( const auto &surface : MapUvEditor_Surfaces( m_pWorkspace ) ) { parts << QStringLiteral( "%1:%2" ).arg( surface.object ).arg( surface.face ); }
        return parts.join( QLatin1Char( ',' ) );
    }

    static void OnChanged( void *pContext, u32 changes ) noexcept
    {
        auto *self = static_cast<uv_editor_t *>( pContext );
        if ( ( changes & ( MAP_CHANGE_SHUTDOWN | MAP_CHANGE_DOCUMENT | MAP_CHANGE_SELECTION ) ) != 0u ) { self->m_pCanvas->Cancel(); }
        if ( ( changes & MAP_CHANGE_SHUTDOWN ) != 0u || ( changes & ( MAP_CHANGE_DOCUMENT | MAP_CHANGE_SELECTION | MAP_CHANGE_VIEW ) ) == 0u ) { return; }
        // Other faces: frame them afresh; the same ones edited: keep the view.
        const QString key = self->Key();
        const bool other = key != self->m_selection;
        if ( other ) { self->m_pCanvas->Cancel(); }
        self->m_selection = key;
        self->m_pCanvas->Refresh( other );
    }

    map_workspace_t *m_pWorkspace{ nullptr };
    uv_canvas_t *m_pCanvas{ nullptr };
    QLabel *m_pStatus{ nullptr };
    QString m_selection;
};

uv_editor_t *AsEditor( QWidget *pPanel ) { return dynamic_cast<uv_editor_t *>( pPanel ); }
} // namespace

std::vector<map_surface_ref_t> MapUvEditor_Surfaces( const map_workspace_t *ws )
{
    std::vector<map_surface_ref_t> out;
    if ( ws == nullptr || ws->pDocument == nullptr || ws->elementMode != map_element_mode_t::FACES ) { return out; }
    if ( MapWorkspace_HasBrushFace( ws ) ) { out.push_back( { ws->selectedBrushFaceObject, ws->selectedBrushFaceSide } ); return out; }
    if ( MapWorkspace_HasMeshFace( ws ) ) {
        const auto faces = MapWorkspace_MeshFaces( ws );
        for ( usize i = 0; i < faces.nCount; ++i ) { out.push_back( { ws->selectedMeshFaceObject, faces.pData[i] } ); }
    }
    return out;
}

QWidget *MapUvEditor_Create( QWidget *pParent, map_workspace_t *pWorkspace ) { return new uv_editor_t( pParent, pWorkspace ); }

QWidget *MapUvEditor_Canvas( QWidget *pPanel ) { auto *editor = AsEditor( pPanel ); return editor != nullptr ? editor->Canvas() : nullptr; }

QPointF MapUvEditor_ToScreen( QWidget *pPanel, math::vec2d_t uv )
{
    auto *editor = AsEditor( pPanel );
    return editor != nullptr ? editor->Canvas()->ToScreen( uv ) : QPointF();
}

math::vec2d_t MapUvEditor_Pivot( QWidget *pPanel )
{
    auto *editor = AsEditor( pPanel );
    return editor != nullptr ? editor->Canvas()->Pivot() : math::vec2d_t{};
}

QString MapUvEditor_Status( QWidget *pPanel )
{
    auto *editor = AsEditor( pPanel );
    return editor != nullptr ? editor->Canvas()->Status() : QString();
}

} // namespace cypher::editor::map
