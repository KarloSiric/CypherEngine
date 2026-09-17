//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Hosts four map views without recreating their widgets or cameras.
//////////////////////////////////////////////////////////////////////////
#pragma once

#include <QByteArray>
#include <QWidget>
#include <array>
#include <functional>

class QSettings;
class QSplitter;
class QToolButton;
class QMenu;
class QLabel;

namespace cypher::tools::tile_editor
{
enum class tile_editor_view_t { TOP, PERSPECTIVE, FRONT, SIDE };

class CypherTileViewWorkspace final : public QWidget
{
public:
    explicit CypherTileViewWorkspace(
        const std::array<QWidget *, 4> &views, QWidget *pParent = nullptr );
    tile_editor_view_t activeView() const;
    QWidget *activeWidget() const;
    // The factory creates independent Top/Front/Side widgets; workspace owns
    // their lifetime. Each pane retains one widget per type for camera/pan state.
    // Perspective always uses the single renderer supplied to the constructor.
    void setViewFactory( std::function<QWidget *( tile_editor_view_t )> factory );
    bool setPaneView( int physicalPosition, tile_editor_view_t view );
    void focusView( tile_editor_view_t view );
    // Positions are row-major: top-left, top-right, bottom-left, bottom-right.
    tile_editor_view_t viewAtPosition( int position ) const;
    bool swapViews( tile_editor_view_t first, tile_editor_view_t requested );
    void resetViewOrder();
    void showFourViews();
    void toggleMaximize();
    bool isMaximized() const;
    int visiblePaneCount() const;
    bool isPaneVisible( int physicalPosition ) const;
    bool setPaneVisible( int physicalPosition, bool visible );
    void setSplitterWidth( int width );
    int splitterWidth() const;
    // View widgets call this after a stationary right-click. Drag navigation
    // remains owned by the individual 2D/3D viewport implementation.
    void showPaneContextMenu( QWidget *view, const QPoint &globalPosition );
    void setFrameCallback( std::function<void( tile_editor_view_t )> callback );
    void setContextMenuContributor(
        std::function<void( QMenu &, tile_editor_view_t )> callback );
    void setActivateOnHover( bool enabled );
    void setCameraMenu( QMenu *menu );
    void restoreLayout( QSettings &settings );
    void saveLayout( QSettings &settings ) const;

protected:
    bool eventFilter( QObject *pObject, QEvent *pEvent ) override;

private:
    void setActiveView( tile_editor_view_t view );
    void setActivePane( int pane );
    void focusPane( int pane );
    void updatePaneHeader( int pane );
    void updatePaneControls();
    void showPaneContextMenu( int pane, const QPoint &globalPosition );
    void replacePaneWidget( int pane, tile_editor_view_t view, QWidget *widget );
    QWidget *widgetForPane( int pane, tile_editor_view_t view );
    void applyViewOrder( const std::array<tile_editor_view_t, 4> &order );
    void applyPaneVisibility();
    void leaveMaximized();
    void captureAllVisibleSplitterState();
    int positionOf( tile_editor_view_t view ) const;
    int positionOfPane( int pane ) const;
    std::array<tile_editor_view_t, 4> m_viewOrder{
        tile_editor_view_t::PERSPECTIVE, tile_editor_view_t::TOP,
        tile_editor_view_t::FRONT, tile_editor_view_t::SIDE };
    std::array<QWidget *, 4> m_views{};
    std::array<tile_editor_view_t, 4> m_viewTypes{
        tile_editor_view_t::TOP, tile_editor_view_t::PERSPECTIVE,
        tile_editor_view_t::FRONT, tile_editor_view_t::SIDE };
    std::array<std::array<QWidget *, 4>, 4> m_viewCache{};
    std::array<QWidget *, 4> m_panes{};
    std::array<QWidget *, 4> m_headers{};
    std::array<QLabel *, 4> m_numberLabels{};
    std::array<QToolButton *, 4> m_titleButtons{};
    std::array<QToolButton *, 4> m_maximizeButtons{};
    std::array<QToolButton *, 4> m_closeButtons{};
    std::array<QSplitter *, 2> m_rows{};
    QSplitter *m_pColumns{ nullptr };
    QToolButton *m_pCameraMenuButton{ nullptr };
    QWidget *m_pPerspectiveView{ nullptr };
    std::array<QByteArray, 3> m_quadState{};
    std::array<QByteArray, 3> m_allVisibleState{};
    std::array<bool, 4> m_paneVisible{ true, true, true, true };
    int m_activePane{ 1 };
    bool m_bMaximized{ false };
    bool m_bActivateOnHover{ true };
    std::function<void( tile_editor_view_t )> m_frameCallback{};
    std::function<void( QMenu &, tile_editor_view_t )> m_contextMenuContributor{};
    std::function<QWidget *( tile_editor_view_t )> m_viewFactory{};
};
}
