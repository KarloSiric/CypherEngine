//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/Picasso/Gui/PicassoCanvas.h
//  Purpose: Declares Picasso's interactive 2D texture viewport.
//  Details: The canvas displays authored pixels over transparency checks and
//           owns only a Qt display copy; document pixels remain in PicassoCore.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_PICASSO_CANVAS_H
#define CYPHER_TOOLS_PICASSO_CANVAS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRect>
#include <QWidget>

#include <functional>

class QMouseEvent;
class QEvent;
class QFocusEvent;
class QKeyEvent;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;

namespace cypher::tools::picasso
{

enum class picasso_channel_mode_t {
    RGBA,
    RED,
    GREEN,
    BLUE,
    ALPHA
};

enum class picasso_canvas_tool_t {
    SELECT,
    MARQUEE,
    LASSO,
    MOVE,
    BRUSH,
    ERASER,
    FILL,
    GRADIENT,
    EYEDROPPER,
    CLONE,
    CROP,
    SEAM,
    MASK,
    PAN,
    ZOOM,
    INSPECT
};

enum class picasso_canvas_interaction_t {
    BEGIN,
    UPDATE,
    END,
    CANCEL
};

class PicassoCanvas final : public QWidget
{
public:
    explicit PicassoCanvas( QWidget *pParent = nullptr );

    void setImage( const QImage &image, bool bResetView = true );
    void updateImageRegion( const QImage &image, const QRect &imageRegion );
    void setChannelMode( picasso_channel_mode_t mode );
    void setTool( picasso_canvas_tool_t tool );
    void setLayerVisible( bool bVisible );
    void setImageOpacity( qreal opacity );
    void setBrushDiameter( qreal nDiameter );

    void setZoom( qreal zoom );
    void zoomIn();
    void zoomOut();
    void actualSize();
    void fitToView();

    [[nodiscard]] qreal zoom() const noexcept;
    [[nodiscard]] bool hasImage() const noexcept;
    [[nodiscard]] picasso_canvas_tool_t tool() const noexcept;

    void setZoomChangedCallback( std::function<void( qreal )> callback );
    void setPixelHoveredCallback(
        std::function<void( int, int, QRgb )> callback );
    void setToolInteractionCallback(
        std::function<void(
            picasso_canvas_tool_t,
            picasso_canvas_interaction_t,
            qreal,
            qreal )> callback );

protected:
    void paintEvent( QPaintEvent *pEvent ) override;
    void resizeEvent( QResizeEvent *pEvent ) override;
    void wheelEvent( QWheelEvent *pEvent ) override;
    void keyPressEvent( QKeyEvent *pEvent ) override;
    void keyReleaseEvent( QKeyEvent *pEvent ) override;
    void focusOutEvent( QFocusEvent *pEvent ) override;
    void mousePressEvent( QMouseEvent *pEvent ) override;
    void mouseMoveEvent( QMouseEvent *pEvent ) override;
    void mouseReleaseEvent( QMouseEvent *pEvent ) override;
    void leaveEvent( QEvent *pEvent ) override;

private:
    void rebuildDisplayImage();
    void rebuildDisplayImageRegion( const QRect &imageRegion );
    void notifyZoomChanged();
    void notifyPixelHovered( const QPointF &position );
    void notifyToolInteraction(
        picasso_canvas_interaction_t interaction,
        const QPointF &position );
    [[nodiscard]] bool imagePosition(
        const QPointF &position,
        QPointF *pImagePositionOut ) const;
    void zoomAt( const QPointF &position, qreal factor );
    [[nodiscard]] QRectF imageRect() const;

    QImage m_sourceImage{};
    QImage m_displayImage{};
    picasso_channel_mode_t m_channelMode{ picasso_channel_mode_t::RGBA };
    picasso_canvas_tool_t m_tool{ picasso_canvas_tool_t::SELECT };
    QPointF m_panOffset{};
    QPoint m_lastMousePosition{};
    qreal m_zoom{ 1.0 };
    qreal m_imageOpacity{ 1.0 };
    qreal m_brushDiameter{ 24.0 };
    QPointF m_cursorPosition{};
    bool m_bFitToView{ true };
    bool m_bPanning{ false };
    bool m_bTemporaryPan{ false };
    bool m_bLayerVisible{ true };
    bool m_bToolInteracting{ false };
    bool m_bCursorOverImage{ false };
    std::function<void( qreal )> m_zoomChanged{};
    std::function<void( int, int, QRgb )> m_pixelHovered{};
    std::function<void(
        picasso_canvas_tool_t,
        picasso_canvas_interaction_t,
        qreal,
        qreal )> m_toolInteraction{};
};

} // namespace cypher::tools::picasso

#endif // CYPHER_TOOLS_PICASSO_CANVAS_H
