//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/Picasso/Gui/PicassoCanvas.cpp
//  Purpose: Implements Picasso's interactive 2D texture viewport.
//  Details: The widget translates input into image-space interactions while the
//           Qt-independent document owns pixel mutation and undo history. Zoom
//           remains stable around the cursor and middle-button dragging pans.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "PicassoCanvas.h"

#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cypher::tools::picasso
{

namespace
{

constexpr qreal PICASSO_MIN_ZOOM = 0.05;
constexpr qreal PICASSO_MAX_ZOOM = 16.0;

} // namespace

PicassoCanvas::PicassoCanvas( QWidget *pParent )
    : QWidget( pParent )
{
    setFocusPolicy( Qt::StrongFocus );
    setMouseTracking( true );
    setMinimumSize( 320, 240 );
    setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
    setTool( picasso_canvas_tool_t::SELECT );
}

void PicassoCanvas::setImage( const QImage &image, bool bResetView )
{
    m_sourceImage = image.convertToFormat( QImage::Format_RGBA8888 );
    rebuildDisplayImage();
    if ( bResetView ) {
        m_panOffset = {};
        fitToView();
    } else {
        update();
    }
}

void PicassoCanvas::updateImageRegion(
    const QImage &image,
    const QRect &imageRegion )
{
    if ( image.isNull() ) {
        return;
    }
    const QImage source = image.format() == QImage::Format_RGBA8888
        ? image
        : image.convertToFormat( QImage::Format_RGBA8888 );
    if ( m_sourceImage.size() != source.size() ||
         m_sourceImage.format() != QImage::Format_RGBA8888 ) {
        setImage( source, false );
        return;
    }

    const QRect region = imageRegion.intersected( source.rect() );
    if ( region.isEmpty() ) {
        return;
    }
    const qsizetype cbRow = static_cast<qsizetype>( region.width() ) * 4;
    for ( int y = region.top(); y <= region.bottom(); ++y ) {
        std::memcpy(
            m_sourceImage.scanLine( y ) + region.left() * 4,
            source.constScanLine( y ) + region.left() * 4,
            static_cast<std::size_t>( cbRow ) );
    }
    rebuildDisplayImageRegion( region );

    const QRectF target = imageRect();
    const QRectF updateRegion(
        target.left() + region.left() * m_zoom,
        target.top() + region.top() * m_zoom,
        region.width() * m_zoom,
        region.height() * m_zoom );
    update( updateRegion.adjusted( -3.0, -3.0, 3.0, 3.0 ).toAlignedRect() );
}

void PicassoCanvas::setChannelMode( picasso_channel_mode_t mode )
{
    if ( m_channelMode == mode ) {
        return;
    }
    m_channelMode = mode;
    rebuildDisplayImage();
    update();
}

void PicassoCanvas::setTool( picasso_canvas_tool_t toolValue )
{
    if ( m_bToolInteracting ) {
        notifyToolInteraction(
            picasso_canvas_interaction_t::CANCEL,
            m_cursorPosition );
        m_bToolInteracting = false;
    }
    m_tool = toolValue;
    if ( m_bTemporaryPan && !m_bPanning ) {
        setCursor( Qt::OpenHandCursor );
        return;
    }
    switch ( m_tool ) {
        case picasso_canvas_tool_t::PAN:
            setCursor( Qt::OpenHandCursor );
            break;
        case picasso_canvas_tool_t::ZOOM:
            setCursor( Qt::SizeAllCursor );
            break;
        case picasso_canvas_tool_t::BRUSH:
        case picasso_canvas_tool_t::ERASER:
        case picasso_canvas_tool_t::FILL:
        case picasso_canvas_tool_t::GRADIENT:
        case picasso_canvas_tool_t::EYEDROPPER:
        case picasso_canvas_tool_t::CLONE:
        case picasso_canvas_tool_t::CROP:
        case picasso_canvas_tool_t::MARQUEE:
        case picasso_canvas_tool_t::LASSO:
        case picasso_canvas_tool_t::INSPECT:
            setCursor( Qt::CrossCursor );
            break;
        case picasso_canvas_tool_t::SELECT:
            setCursor( Qt::ArrowCursor );
            break;
        case picasso_canvas_tool_t::MOVE:
        case picasso_canvas_tool_t::SEAM:
        case picasso_canvas_tool_t::MASK:
            setCursor( Qt::SizeAllCursor );
            break;
    }
}

void PicassoCanvas::setLayerVisible( bool bVisible )
{
    m_bLayerVisible = bVisible;
    update();
}

void PicassoCanvas::setImageOpacity( qreal opacity )
{
    m_imageOpacity = std::clamp( opacity, 0.0, 1.0 );
    update();
}

void PicassoCanvas::setBrushDiameter( qreal nDiameter )
{
    m_brushDiameter = std::max( 1.0, nDiameter );
    update();
}

void PicassoCanvas::setZoom( qreal zoomValue )
{
    const qreal clamped = std::clamp(
        zoomValue,
        PICASSO_MIN_ZOOM,
        PICASSO_MAX_ZOOM );
    if ( std::abs( clamped - m_zoom ) < 0.0001 ) {
        return;
    }
    m_zoom = clamped;
    m_bFitToView = false;
    notifyZoomChanged();
    update();
}

void PicassoCanvas::zoomIn()
{
    setZoom( m_zoom * 1.25 );
}

void PicassoCanvas::zoomOut()
{
    setZoom( m_zoom / 1.25 );
}

void PicassoCanvas::actualSize()
{
    m_panOffset = {};
    setZoom( 1.0 );
}

void PicassoCanvas::fitToView()
{
    if ( m_displayImage.isNull() || width() <= 0 || height() <= 0 ) {
        return;
    }
    constexpr qreal margin = 56.0;
    const qreal availableWidth = std::max( 1.0, width() - margin );
    const qreal availableHeight = std::max( 1.0, height() - margin );
    m_zoom = std::clamp(
        std::min(
            availableWidth / m_displayImage.width(),
            availableHeight / m_displayImage.height() ),
        PICASSO_MIN_ZOOM,
        PICASSO_MAX_ZOOM );
    m_panOffset = {};
    m_bFitToView = true;
    notifyZoomChanged();
    update();
}

qreal PicassoCanvas::zoom() const noexcept
{
    return m_zoom;
}

bool PicassoCanvas::hasImage() const noexcept
{
    return !m_displayImage.isNull();
}

picasso_canvas_tool_t PicassoCanvas::tool() const noexcept
{
    return m_tool;
}

void PicassoCanvas::setZoomChangedCallback(
    std::function<void( qreal )> callback )
{
    m_zoomChanged = std::move( callback );
}

void PicassoCanvas::setPixelHoveredCallback(
    std::function<void( int, int, QRgb )> callback )
{
    m_pixelHovered = std::move( callback );
}

void PicassoCanvas::setToolInteractionCallback(
    std::function<void(
        picasso_canvas_tool_t,
        picasso_canvas_interaction_t,
        qreal,
        qreal )> callback )
{
    m_toolInteraction = std::move( callback );
}

void PicassoCanvas::paintEvent( QPaintEvent * )
{
    QPainter painter( this );
    painter.fillRect( rect(), QColor( 14, 18, 21 ) );

    if ( m_displayImage.isNull() ) {
        painter.setPen( QColor( 104, 111, 114 ) );
        painter.drawText( rect(), Qt::AlignCenter, tr( "No texture open" ) );
        return;
    }

    const QRectF target = imageRect();
    painter.save();
    painter.setClipRect( target );
    constexpr int checkerSize = 16;
    const int xBegin = static_cast<int>( std::floor( target.left() ) );
    const int yBegin = static_cast<int>( std::floor( target.top() ) );
    const int xEnd = static_cast<int>( std::ceil( target.right() ) );
    const int yEnd = static_cast<int>( std::ceil( target.bottom() ) );
    for ( int y = yBegin; y < yEnd; y += checkerSize ) {
        for ( int x = xBegin; x < xEnd; x += checkerSize ) {
            const bool bDark =
                ( ( ( x - xBegin ) / checkerSize ) +
                  ( ( y - yBegin ) / checkerSize ) ) % 2 != 0;
            painter.fillRect(
                QRect( x, y, checkerSize, checkerSize ),
                bDark ? QColor( 38, 44, 49 ) : QColor( 52, 59, 65 ) );
        }
    }
    painter.restore();

    if ( m_bLayerVisible ) {
        painter.setOpacity( m_imageOpacity );
        painter.setRenderHint(
            QPainter::SmoothPixmapTransform,
            m_zoom < 1.0 );
        painter.drawImage( target, m_displayImage );
        painter.setOpacity( 1.0 );
    }

    painter.setPen( QPen( QColor( 190, 105, 31 ), 1.0 ) );
    painter.drawRect( target );

    if ( m_bCursorOverImage &&
         ( m_tool == picasso_canvas_tool_t::BRUSH ||
           m_tool == picasso_canvas_tool_t::ERASER ) ) {
        const qreal radius = m_brushDiameter * m_zoom * 0.5;
        painter.setRenderHint( QPainter::Antialiasing, true );
        painter.setPen( QPen( QColor( 12, 15, 17, 220 ), 3.0 ) );
        painter.setBrush( Qt::NoBrush );
        painter.drawEllipse( m_cursorPosition, radius, radius );
        painter.setPen( QPen( QColor( 225, 230, 232, 230 ), 1.0 ) );
        painter.drawEllipse( m_cursorPosition, radius, radius );
    }
}

void PicassoCanvas::resizeEvent( QResizeEvent *pEvent )
{
    QWidget::resizeEvent( pEvent );
    if ( m_bFitToView ) {
        fitToView();
    }
}

void PicassoCanvas::wheelEvent( QWheelEvent *pEvent )
{
    if ( m_displayImage.isNull() ) {
        pEvent->ignore();
        return;
    }

    const qreal factor = pEvent->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    zoomAt( pEvent->position(), factor );
    pEvent->accept();
}

void PicassoCanvas::keyPressEvent( QKeyEvent *pEvent )
{
    if ( pEvent->key() == Qt::Key_Escape && m_bToolInteracting ) {
        notifyToolInteraction(
            picasso_canvas_interaction_t::CANCEL,
            m_cursorPosition );
        m_bToolInteracting = false;
        pEvent->accept();
        return;
    }
    if ( pEvent->key() == Qt::Key_Space ) {
        if ( !pEvent->isAutoRepeat() ) {
            m_bTemporaryPan = true;
            if ( !m_bPanning ) {
                setCursor( Qt::OpenHandCursor );
            }
        }
        pEvent->accept();
        return;
    }
    QWidget::keyPressEvent( pEvent );
}

void PicassoCanvas::keyReleaseEvent( QKeyEvent *pEvent )
{
    if ( pEvent->key() == Qt::Key_Space ) {
        if ( !pEvent->isAutoRepeat() ) {
            m_bTemporaryPan = false;
            if ( !m_bPanning ) {
                setTool( m_tool );
            }
        }
        pEvent->accept();
        return;
    }
    QWidget::keyReleaseEvent( pEvent );
}

void PicassoCanvas::focusOutEvent( QFocusEvent *pEvent )
{
    // A lost key-release event must not leave the canvas in a temporary mode.
    if ( m_bToolInteracting ) {
        notifyToolInteraction(
            picasso_canvas_interaction_t::CANCEL,
            m_cursorPosition );
        m_bToolInteracting = false;
    }
    m_bTemporaryPan = false;
    m_bPanning = false;
    setTool( m_tool );
    QWidget::focusOutEvent( pEvent );
}

void PicassoCanvas::mousePressEvent( QMouseEvent *pEvent )
{
    if ( pEvent->button() == Qt::MiddleButton ||
         ( pEvent->button() == Qt::LeftButton &&
           ( m_tool == picasso_canvas_tool_t::PAN ||
             m_bTemporaryPan ) ) ) {
        m_bPanning = true;
        m_bFitToView = false;
        m_lastMousePosition = pEvent->position().toPoint();
        setCursor( Qt::ClosedHandCursor );
        pEvent->accept();
        return;
    }
    if ( m_tool == picasso_canvas_tool_t::ZOOM &&
         ( pEvent->button() == Qt::LeftButton ||
           pEvent->button() == Qt::RightButton ) ) {
        zoomAt(
            pEvent->position(),
            pEvent->button() == Qt::LeftButton ? 1.25 : 1.0 / 1.25 );
        pEvent->accept();
        return;
    }
    if ( pEvent->button() == Qt::LeftButton &&
         ( m_tool == picasso_canvas_tool_t::BRUSH ||
           m_tool == picasso_canvas_tool_t::ERASER ) ) {
        QPointF imagePoint{};
        if ( imagePosition( pEvent->position(), &imagePoint ) ) {
            m_bToolInteracting = true;
            notifyToolInteraction(
                picasso_canvas_interaction_t::BEGIN,
                pEvent->position() );
            pEvent->accept();
            return;
        }
    }
    if ( pEvent->button() == Qt::LeftButton &&
         ( m_tool == picasso_canvas_tool_t::FILL ||
           m_tool == picasso_canvas_tool_t::EYEDROPPER ) ) {
        notifyToolInteraction(
            picasso_canvas_interaction_t::BEGIN,
            pEvent->position() );
        pEvent->accept();
        return;
    }
    notifyPixelHovered( pEvent->position() );
    QWidget::mousePressEvent( pEvent );
}

void PicassoCanvas::mouseMoveEvent( QMouseEvent *pEvent )
{
    m_cursorPosition = pEvent->position();
    QPointF imagePoint{};
    m_bCursorOverImage = imagePosition(
        pEvent->position(),
        &imagePoint );
    if ( m_bPanning ) {
        const QPoint current = pEvent->position().toPoint();
        m_panOffset += current - m_lastMousePosition;
        m_lastMousePosition = current;
        update();
        pEvent->accept();
        return;
    }
    if ( m_bToolInteracting ) {
        notifyToolInteraction(
            picasso_canvas_interaction_t::UPDATE,
            pEvent->position() );
        update();
        pEvent->accept();
        return;
    }
    notifyPixelHovered( pEvent->position() );
    update();
    QWidget::mouseMoveEvent( pEvent );
}

void PicassoCanvas::mouseReleaseEvent( QMouseEvent *pEvent )
{
    if ( ( pEvent->button() == Qt::MiddleButton ||
           pEvent->button() == Qt::LeftButton ) && m_bPanning ) {
        m_bPanning = false;
        if ( m_bTemporaryPan ) {
            setCursor( Qt::OpenHandCursor );
        } else {
            setTool( m_tool );
        }
        pEvent->accept();
        return;
    }
    if ( pEvent->button() == Qt::LeftButton && m_bToolInteracting ) {
        notifyToolInteraction(
            picasso_canvas_interaction_t::END,
            pEvent->position() );
        m_bToolInteracting = false;
        pEvent->accept();
        return;
    }
    QWidget::mouseReleaseEvent( pEvent );
}

void PicassoCanvas::leaveEvent( QEvent *pEvent )
{
    m_bCursorOverImage = false;
    if ( m_pixelHovered ) {
        m_pixelHovered( -1, -1, 0u );
    }
    update();
    QWidget::leaveEvent( pEvent );
}

void PicassoCanvas::notifyPixelHovered( const QPointF &position )
{
    if ( !m_pixelHovered || m_sourceImage.isNull() ) {
        return;
    }
    QPointF imagePoint{};
    if ( !imagePosition( position, &imagePoint ) ) {
        m_pixelHovered( -1, -1, 0u );
        return;
    }
    const int x = std::clamp(
        static_cast<int>( imagePoint.x() ),
        0,
        m_sourceImage.width() - 1 );
    const int y = std::clamp(
        static_cast<int>( imagePoint.y() ),
        0,
        m_sourceImage.height() - 1 );
    m_pixelHovered( x, y, m_sourceImage.pixel( x, y ) );
}

void PicassoCanvas::notifyToolInteraction(
    picasso_canvas_interaction_t interaction,
    const QPointF &position )
{
    if ( !m_toolInteraction ) {
        return;
    }
    QPointF imagePoint{};
    if ( imagePosition( position, &imagePoint ) ) {
        m_toolInteraction(
            m_tool,
            interaction,
            imagePoint.x(),
            imagePoint.y() );
    } else {
        m_toolInteraction( m_tool, interaction, -1.0, -1.0 );
    }
}

bool PicassoCanvas::imagePosition(
    const QPointF &position,
    QPointF *pImagePositionOut ) const
{
    if ( pImagePositionOut == nullptr || m_sourceImage.isNull() ) {
        return false;
    }
    const QRectF target = imageRect();
    if ( position.x() < target.left() || position.x() >= target.right() ||
         position.y() < target.top() || position.y() >= target.bottom() ) {
        return false;
    }
    *pImagePositionOut = {
        ( position.x() - target.left() ) / m_zoom,
        ( position.y() - target.top() ) / m_zoom
    };
    return true;
}

void PicassoCanvas::zoomAt( const QPointF &position, qreal factor )
{
    if ( m_displayImage.isNull() || factor <= 0.0 ) {
        return;
    }
    const QRectF oldRect = imageRect();
    const QPointF imagePoint(
        ( position.x() - oldRect.left() ) / m_zoom,
        ( position.y() - oldRect.top() ) / m_zoom );
    const qreal newZoom = std::clamp(
        m_zoom * factor,
        PICASSO_MIN_ZOOM,
        PICASSO_MAX_ZOOM );
    if ( std::abs( newZoom - m_zoom ) < 0.0001 ) {
        return;
    }

    m_zoom = newZoom;
    m_bFitToView = false;
    const QSizeF newSize(
        m_displayImage.width() * m_zoom,
        m_displayImage.height() * m_zoom );
    const QPointF centeredTopLeft(
        ( width() - newSize.width() ) * 0.5,
        ( height() - newSize.height() ) * 0.5 );
    m_panOffset = position - imagePoint * m_zoom - centeredTopLeft;
    notifyZoomChanged();
    update();
}

void PicassoCanvas::rebuildDisplayImage()
{
    m_displayImage = m_sourceImage.copy();
    if ( m_displayImage.isNull() ||
         m_channelMode == picasso_channel_mode_t::RGBA ) {
        return;
    }

    for ( int y = 0; y < m_displayImage.height(); ++y ) {
        uchar *pRow = m_displayImage.scanLine( y );
        for ( int x = 0; x < m_displayImage.width(); ++x ) {
            uchar *pPixel = pRow + x * 4;
            uchar value = 0u;
            switch ( m_channelMode ) {
                case picasso_channel_mode_t::RED:   value = pPixel[0]; break;
                case picasso_channel_mode_t::GREEN: value = pPixel[1]; break;
                case picasso_channel_mode_t::BLUE:  value = pPixel[2]; break;
                case picasso_channel_mode_t::ALPHA: value = pPixel[3]; break;
                default:                            break;
            }
            pPixel[0] = value;
            pPixel[1] = value;
            pPixel[2] = value;
            pPixel[3] = 255u;
        }
    }
}

void PicassoCanvas::rebuildDisplayImageRegion( const QRect &imageRegion )
{
    if ( m_displayImage.size() != m_sourceImage.size() ||
         m_displayImage.format() != QImage::Format_RGBA8888 ) {
        rebuildDisplayImage();
        return;
    }
    const QRect region = imageRegion.intersected( m_sourceImage.rect() );
    if ( region.isEmpty() ) {
        return;
    }

    for ( int y = region.top(); y <= region.bottom(); ++y ) {
        const uchar *pSourceRow = m_sourceImage.constScanLine( y );
        uchar *pDisplayRow = m_displayImage.scanLine( y );
        for ( int x = region.left(); x <= region.right(); ++x ) {
            const uchar *pSource = pSourceRow + x * 4;
            uchar *pDisplay = pDisplayRow + x * 4;
            if ( m_channelMode == picasso_channel_mode_t::RGBA ) {
                std::memcpy( pDisplay, pSource, 4u );
                continue;
            }
            uchar value = 0u;
            switch ( m_channelMode ) {
                case picasso_channel_mode_t::RED:   value = pSource[0]; break;
                case picasso_channel_mode_t::GREEN: value = pSource[1]; break;
                case picasso_channel_mode_t::BLUE:  value = pSource[2]; break;
                case picasso_channel_mode_t::ALPHA: value = pSource[3]; break;
                default:                            break;
            }
            pDisplay[0] = value;
            pDisplay[1] = value;
            pDisplay[2] = value;
            pDisplay[3] = 255u;
        }
    }
}

void PicassoCanvas::notifyZoomChanged()
{
    if ( m_zoomChanged ) {
        m_zoomChanged( m_zoom );
    }
}

QRectF PicassoCanvas::imageRect() const
{
    if ( m_displayImage.isNull() ) {
        return {};
    }
    const QSizeF size(
        m_displayImage.width() * m_zoom,
        m_displayImage.height() * m_zoom );
    const QPointF topLeft(
        ( width() - size.width() ) * 0.5 + m_panOffset.x(),
        ( height() - size.height() ) * 0.5 + m_panOffset.y() );
    return { topLeft, size };
}

} // namespace cypher::tools::picasso
