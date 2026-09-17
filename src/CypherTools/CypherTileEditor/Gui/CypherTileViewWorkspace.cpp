//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Resizable Top / 3D Map / Front / Side workspace and pane controls.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileViewWorkspace.h"
#include "CypherTileEditorIcons.h"

#include <QEvent>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QAction>
#include <QActionGroup>
#include <QFontMetrics>
#include <QLabel>
#include <QMenu>
#include <QSettings>
#include <QSplitter>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace cypher::tools::tile_editor
{
namespace
{
constexpr std::array kDefaultViewOrder{
    tile_editor_view_t::PERSPECTIVE, tile_editor_view_t::TOP,
    tile_editor_view_t::FRONT, tile_editor_view_t::SIDE };

bool IsValidView( tile_editor_view_t view )
{
    return static_cast<unsigned>( view ) < 4;
}

QString ViewTitle( tile_editor_view_t view )
{
    switch ( view ) {
        case tile_editor_view_t::TOP: return QObject::tr( "XY TOP" );
        case tile_editor_view_t::PERSPECTIVE: return QObject::tr( "3D CAMERA" );
        case tile_editor_view_t::FRONT: return QObject::tr( "XZ FRONT" );
        case tile_editor_view_t::SIDE: return QObject::tr( "YZ SIDE" );
    }
    return {};
}
}

CypherTileViewWorkspace::CypherTileViewWorkspace(
    const std::array<QWidget *, 4> &views, QWidget *pParent )
    : QWidget( pParent ), m_views( views ), m_pPerspectiveView( views[1] )
{
    setObjectName( QStringLiteral( "TileEditorFourViews" ) );
    auto *pLayout = new QVBoxLayout( this );
    pLayout->setContentsMargins( 0, 0, 0, 0 );
    pLayout->setSpacing( 0 );
    m_pColumns = new QSplitter( Qt::Vertical, this );
    m_pColumns->setObjectName( QStringLiteral( "TileViewRows" ) );
    m_pColumns->setChildrenCollapsible( false );
    // Q3-style view editors use one deliberate gutter between render surfaces.
    // The panes themselves do not add another frame around that gutter.
    m_pColumns->setHandleWidth( 6 );
    pLayout->addWidget( m_pColumns );

    const QStringList titles{ tr( "XY TOP" ), tr( "3D CAMERA" ),
                              tr( "XZ FRONT" ), tr( "YZ SIDE" ) };
    const QStringList hints{
        tr( "Edit tiles · MMB or Space+LMB pans · wheel zooms at pointer" ),
        tr( "RMB + WASD/QE to fly · Alt+RMB orbit · MMB pan · F frames selection" ),
        tr( "Author X/Z at the fixed Y construction slice · MMB/RMB or Space+LMB pans" ),
        tr( "Author Y/Z at the fixed X construction slice · MMB/RMB or Space+LMB pans" ) };
    // Keep IDs and shortcuts semantic; physical pane placement is independent.
    for ( int row = 0; row < 2; ++row ) {
        m_rows[row] = new QSplitter( Qt::Horizontal, m_pColumns );
        m_rows[row]->setObjectName( QStringLiteral( "TileViewRow%1" ).arg( row ) );
        m_rows[row]->setChildrenCollapsible( false );
        m_rows[row]->setHandleWidth( 6 );
        for ( int column = 0; column < 2; ++column ) {
            const auto view = m_viewOrder[row * 2 + column];
            const int i = static_cast<int>( view );
            auto *pPane = new QWidget( m_rows[row] );
            pPane->setObjectName( QStringLiteral( "TileViewPane%1" ).arg( i ) );
            pPane->setProperty( "tileViewPane", true );
            pPane->setAttribute( Qt::WA_StyledBackground );
            pPane->setMouseTracking( true );
            // The pane itself participates in pointer routing. This covers its
            // compact header overlay and the actual view surface.
            pPane->installEventFilter( this );
            auto *pPaneLayout = new QGridLayout( pPane );
            pPaneLayout->setContentsMargins( 0, 0, 0, 0 );
            pPaneLayout->setSpacing( 0 );
            auto *pHeader = new QWidget( pPane );
            pHeader->setObjectName( QStringLiteral( "TileViewHeader" ) );
            pHeader->setAttribute( Qt::WA_StyledBackground );
            pHeader->setMouseTracking( true );
            pHeader->setProperty( "viewIndex", i );
            pHeader->setProperty( "tileViewOverlayHeader", true );
            pHeader->setSizePolicy( QSizePolicy::Maximum, QSizePolicy::Fixed );
            pHeader->installEventFilter( this );
            pHeader->setToolTip( hints[i] + tr( " · Double-click header to maximize" ) );
            auto *pHeaderLayout = new QHBoxLayout( pHeader );
            pHeaderLayout->setContentsMargins( 5, 0, 1, 0 );
            pHeaderLayout->setSpacing( 1 );
            auto *pNumber = new QLabel( pHeader );
            m_numberLabels[i] = pNumber;
            pNumber->setObjectName( QStringLiteral( "TileViewNumber%1" ).arg( i ) );
            pNumber->setProperty( "tileViewNumber", true );
            pNumber->setAlignment( Qt::AlignCenter );
            pNumber->setAccessibleName( tr( "Viewport number" ) );
            pHeaderLayout->addWidget( pNumber );
            auto *pTitle = new QToolButton( pHeader );
            m_titleButtons[i] = pTitle;
            pTitle->setObjectName( QStringLiteral( "TileViewChoose%1" ).arg( i ) );
            pTitle->setProperty( "tileViewTitle", true );
            pTitle->setText( titles[i] );
            pTitle->setToolTip( tr( "Choose a view for this pane" ) );
            pTitle->setAccessibleName( tr( "Change %1 view" ).arg( titles[i] ) );
            pTitle->setAutoRaise( true );
            pTitle->setPopupMode( QToolButton::InstantPopup );
            auto *pMenu = new QMenu( pTitle );
            for ( int target = 0; target < 4; ++target ) {
                auto *pAction = pMenu->addAction( titles[target] );
                pAction->setObjectName( QStringLiteral( "TileViewChoose%1To%2" ).arg( i ).arg( target ) );
                pAction->setData( target );
                pAction->setCheckable( true );
                pAction->setChecked( target == i );
                if ( target == static_cast<int>( tile_editor_view_t::PERSPECTIVE ) )
                    pAction->setToolTip( tr( "One shared engine renderer: choosing 3D moves its existing view here." ) );
                connect( pAction, &QAction::triggered, this, [this, i, target] {
                    if ( m_viewFactory ) setPaneView( positionOfPane( i ), static_cast<tile_editor_view_t>( target ) );
                    else swapViews( m_viewTypes[i], static_cast<tile_editor_view_t>( target ) );
                } );
            }
            pTitle->setMenu( pMenu );
            connect( pMenu, &QMenu::aboutToShow, this, [this, i, pMenu] {
                setActivePane( i );
                for ( auto *pAction : pMenu->actions() )
                    pAction->setChecked( pAction->data().toInt() == static_cast<int>( m_viewTypes[i] ) );
            } );
            pHeaderLayout->addWidget( pTitle );
            pHeaderLayout->addStretch( 1 );
            if ( view == tile_editor_view_t::PERSPECTIVE ) {
                m_pCameraMenuButton = new QToolButton( pHeader );
                m_pCameraMenuButton->setObjectName( QStringLiteral( "TileViewCameraOptions" ) );
                m_pCameraMenuButton->setText( tr( "Camera" ) );
                m_pCameraMenuButton->setToolTip( tr( "Camera mode, speed, framing and navigation settings" ) );
                m_pCameraMenuButton->setAccessibleName( tr( "3D camera options" ) );
                m_pCameraMenuButton->setAutoRaise( true );
                m_pCameraMenuButton->setPopupMode( QToolButton::InstantPopup );
                m_pCameraMenuButton->hide();
                pHeaderLayout->addWidget( m_pCameraMenuButton );
            }
            auto *pFrame = new QToolButton( pHeader );
            pFrame->setObjectName( QStringLiteral( "TileViewFrame%1" ).arg( i ) );
            pFrame->setIcon( CypherTileEditorIcon_Create( tile_editor_icon_t::FIT_VIEW ) );
            pFrame->setToolTip( tr( "Frame this view" ) );
            pFrame->setAccessibleName( tr( "Frame %1" ).arg( titles[i] ) );
            pFrame->setAutoRaise( true );
            const int iconSize = qMax( 14, pFrame->fontMetrics().height() );
            pFrame->setIconSize( QSize( iconSize, iconSize ) );
            connect( pFrame, &QToolButton::clicked, this, [this, i] {
                focusPane( i );
                if ( m_frameCallback ) m_frameCallback( m_viewTypes[i] );
            } );
            pHeaderLayout->addWidget( pFrame );
            auto *pMaximize = new QToolButton( pHeader );
            pMaximize->setObjectName( QStringLiteral( "TileViewMaximize%1" ).arg( i ) );
            pMaximize->setText( QStringLiteral( "□" ) );
            pMaximize->setToolTip( tr( "Maximize view / restore four views" ) );
            pMaximize->setAccessibleName( tr( "Maximize %1" ).arg( titles[i] ) );
            pMaximize->setAutoRaise( true );
            connect( pMaximize, &QToolButton::clicked, this, [this, i] {
                focusPane( i );
                toggleMaximize();
            } );
            pHeaderLayout->addWidget( pMaximize );
            auto *pOptions = new QToolButton( pHeader );
            pOptions->setObjectName( QStringLiteral( "TileViewOptions%1" ).arg( i ) );
            pOptions->setText( QStringLiteral( "⋮" ) );
            pOptions->setToolTip( tr( "Viewport options" ) );
            pOptions->setAccessibleName( tr( "Open viewport options" ) );
            pOptions->setAutoRaise( true );
            connect( pOptions, &QToolButton::clicked, this, [this, i, pOptions] {
                showPaneContextMenu( i, pOptions->mapToGlobal(
                    QPoint( pOptions->width(), pOptions->height() ) ) );
            } );
            pHeaderLayout->addWidget( pOptions );
            auto *pClose = new QToolButton( pHeader );
            m_closeButtons[i] = pClose;
            pClose->setObjectName( QStringLiteral( "TileViewClose%1" ).arg( i ) );
            pClose->setText( QStringLiteral( "×" ) );
            pClose->setToolTip( tr( "Close this viewport" ) );
            pClose->setAccessibleName( tr( "Close viewport" ) );
            pClose->setAutoRaise( true );
            connect( pClose, &QToolButton::clicked, this, [this, i] {
                (void)setPaneVisible( positionOfPane( i ), false );
            } );
            pHeaderLayout->addWidget( pClose );
            // The title and pane controls float over the view, matching the
            // compact treatment used by Q3-family editors. They no longer
            // consume a full-width row or visually box each viewport.
            pPaneLayout->addWidget( m_views[i], 0, 0 );
            pPaneLayout->addWidget(
                pHeader, 0, 0, Qt::AlignTop | Qt::AlignLeft );
            pHeader->raise();
            m_viewCache[i][i] = m_views[i];
            m_views[i]->setMouseTracking( true );
            m_views[i]->installEventFilter( this );
            m_panes[i] = pPane;
            m_headers[i] = pHeader;
            m_maximizeButtons[i] = pMaximize;
        }
        m_rows[row]->setSizes( { 500, 500 } );
    }
    m_pColumns->setSizes( { 400, 400 } );
    connect( m_pColumns, &QSplitter::splitterMoved, this,
             [this] { captureAllVisibleSplitterState(); } );
    for ( auto *row : m_rows ) connect( row, &QSplitter::splitterMoved, this,
        [this] { captureAllVisibleSplitterState(); } );
    for ( int pane = 0; pane < 4; ++pane ) updatePaneHeader( pane );
    updatePaneControls();
    captureAllVisibleSplitterState();
    focusView( tile_editor_view_t::PERSPECTIVE );
}

tile_editor_view_t CypherTileViewWorkspace::activeView() const { return m_viewTypes[m_activePane]; }
QWidget *CypherTileViewWorkspace::activeWidget() const { return m_views[m_activePane]; }
bool CypherTileViewWorkspace::isMaximized() const { return m_bMaximized; }

void CypherTileViewWorkspace::setActiveView( tile_editor_view_t view )
{
    if ( !IsValidView( view ) ) return;
    const int position = positionOf( view );
    if ( position >= 0 ) setActivePane( static_cast<int>( m_viewOrder[position] ) );
}

void CypherTileViewWorkspace::setActivePane( int pane )
{
    if ( pane < 0 || pane >= 4 ) return;
    m_activePane = pane;
    for ( int i = 0; i < 4; ++i ) {
        for ( QWidget *pWidget : { m_headers[i], m_panes[i] } ) {
            pWidget->setProperty( "active", i == pane );
            pWidget->style()->unpolish( pWidget );
            pWidget->style()->polish( pWidget );
            pWidget->update();
        }
    }
}

void CypherTileViewWorkspace::focusView( tile_editor_view_t view )
{
    if ( !IsValidView( view ) ) return;
    const int position = positionOf( view );
    if ( position >= 0 ) focusPane( static_cast<int>( m_viewOrder[position] ) );
    else setPaneView( positionOfPane( m_activePane ), view );
}

void CypherTileViewWorkspace::focusPane( int pane )
{
    if ( pane < 0 || pane >= 4 ) return;
    if ( m_bMaximized && pane != m_activePane ) leaveMaximized();
    if ( !m_paneVisible[pane] ) {
        m_paneVisible[pane] = true;
        applyPaneVisibility();
        updatePaneControls();
    }
    setActivePane( pane );
    m_views[pane]->setFocus( Qt::ShortcutFocusReason );
}

tile_editor_view_t CypherTileViewWorkspace::viewAtPosition( int position ) const
{
    return position >= 0 && position < 4
        ? m_viewTypes[static_cast<int>( m_viewOrder[position] )] : tile_editor_view_t::PERSPECTIVE;
}

int CypherTileViewWorkspace::positionOf( tile_editor_view_t view ) const
{
    if ( m_paneVisible[m_activePane] && activeView() == view )
        return positionOfPane( m_activePane );
    // Duplicate orthographic views are legal. Prefer one the user has kept
    // open instead of reopening the first hidden duplicate in physical order.
    for ( int position = 0; position < 4; ++position ) {
        const int pane = static_cast<int>( m_viewOrder[position] );
        if ( m_paneVisible[pane] && m_viewTypes[pane] == view ) return position;
    }
    for ( int position = 0; position < 4; ++position )
        if ( viewAtPosition( position ) == view ) return position;
    return -1;
}

int CypherTileViewWorkspace::positionOfPane( int pane ) const
{
    const auto found = std::find( m_viewOrder.begin(), m_viewOrder.end(), static_cast<tile_editor_view_t>( pane ) );
    return found == m_viewOrder.end() ? -1 : static_cast<int>( found - m_viewOrder.begin() );
}

void CypherTileViewWorkspace::setViewFactory( std::function<QWidget *( tile_editor_view_t )> factory )
{
    m_viewFactory = std::move( factory );
    for ( int pane = 0; pane < 4; ++pane ) updatePaneHeader( pane );
}

QWidget *CypherTileViewWorkspace::widgetForPane( int pane, tile_editor_view_t view )
{
    const int type = static_cast<int>( view );
    if ( auto *cached = m_viewCache[pane][type] ) return cached;
    if ( view == tile_editor_view_t::PERSPECTIVE ) {
        // The renderer has one owner/display location, even when hidden. Never
        // request a second native renderer from the orthographic widget factory.
        for ( auto &cache : m_viewCache ) cache[type] = nullptr;
        m_viewCache[pane][type] = m_pPerspectiveView;
        return m_pPerspectiveView;
    }
    if ( !m_viewFactory ) return nullptr;
    QWidget *widget = m_viewFactory( view );
    if ( widget == nullptr || widget == this || widget->isAncestorOf( this ) ) return nullptr;
    for ( const auto &cache : m_viewCache )
        for ( const auto *cached : cache )
            if ( widget == cached ) return nullptr; // 2D views must be independent instances.
    widget->hide();
    widget->setMouseTracking( true );
    widget->installEventFilter( this );
    m_viewCache[pane][type] = widget;
    return widget;
}

void CypherTileViewWorkspace::updatePaneHeader( int pane )
{
    const auto view = m_viewTypes[pane];
    const QString title = ViewTitle( view );
    auto *button = m_titleButtons[pane];
    button->setText( title );
    button->setAccessibleName( tr( "Change %1 view" ).arg( title ) );
    button->setToolTip( m_viewFactory
        ? tr( "Choose an independent 2D view for this pane. The 3D view uses one shared engine renderer." )
        : tr( "Choose a view for this pane" ) );
    for ( auto *action : button->menu()->actions() )
        action->setChecked( action->data().toInt() == static_cast<int>( view ) );
    const QString hint = view == tile_editor_view_t::PERSPECTIVE
        ? tr( "RMB + WASD/QE to fly · Alt+RMB orbit · MMB pan · One shared engine renderer" )
        : view == tile_editor_view_t::TOP
            ? tr( "Edit tiles · MMB or Space+LMB pans · wheel zooms at pointer" )
            : view == tile_editor_view_t::FRONT
                ? tr( "Author X/Z at the fixed Y construction slice · MMB/RMB or Space+LMB pans" )
                : tr( "Author Y/Z at the fixed X construction slice · MMB/RMB or Space+LMB pans" );
    m_headers[pane]->setToolTip( hint + tr( " · Double-click header to maximize" ) );
    if ( auto *frame = m_headers[pane]->findChild<QToolButton *>( QStringLiteral( "TileViewFrame%1" ).arg( pane ) ) )
        frame->setAccessibleName( tr( "Frame %1" ).arg( title ) );
    m_maximizeButtons[pane]->setAccessibleName( tr( "Maximize %1" ).arg( title ) );
    const int position = positionOfPane( pane );
    m_numberLabels[pane]->setText( QString::number( position + 1 ) );
    m_numberLabels[pane]->setToolTip( tr( "Viewport %1" ).arg( position + 1 ) );
    m_numberLabels[pane]->setAccessibleName( tr( "Viewport %1" ).arg( position + 1 ) );
}

void CypherTileViewWorkspace::replacePaneWidget( int pane, tile_editor_view_t view, QWidget *widget )
{
    auto *layout = static_cast<QGridLayout *>( m_panes[pane]->layout() );
    QWidget *previous = m_views[pane];
    previous->hide();
    layout->removeWidget( previous );
    if ( previous == m_pPerspectiveView ) m_pCameraMenuButton->hide();
    m_views[pane] = widget;
    m_viewTypes[pane] = view;
    layout->addWidget( widget, 0, 0 );
    widget->show();
    // A newly inserted viewport is later in the child stacking order. Keep
    // the compact header accessible above it without reserving a title row.
    m_headers[pane]->raise();
    if ( view == tile_editor_view_t::PERSPECTIVE ) {
        auto *headerLayout = static_cast<QHBoxLayout *>( m_headers[pane]->layout() );
        if ( m_pCameraMenuButton->parentWidget() != m_headers[pane] ) {
            if ( auto *previousLayout = m_pCameraMenuButton->parentWidget()->layout() )
                previousLayout->removeWidget( m_pCameraMenuButton );
            m_pCameraMenuButton->setParent( m_headers[pane] );
            auto *frame = m_headers[pane]->findChild<QToolButton *>(
                QStringLiteral( "TileViewFrame%1" ).arg( pane ) );
            headerLayout->insertWidget( std::max( 0, headerLayout->indexOf( frame ) ),
                                        m_pCameraMenuButton );
        }
        m_pCameraMenuButton->setVisible( m_pCameraMenuButton->menu() != nullptr );
    }
    updatePaneHeader( pane );
}

bool CypherTileViewWorkspace::setPaneView( int physicalPosition, tile_editor_view_t view )
{
    if ( physicalPosition < 0 || physicalPosition >= 4 || !IsValidView( view ) ) return false;
    const int pane = static_cast<int>( m_viewOrder[physicalPosition] );
    const bool wasMaximized = m_bMaximized;
    if ( m_viewTypes[pane] == view ) {
        focusPane( pane );
        if ( wasMaximized && !m_bMaximized ) toggleMaximize();
        return true;
    }
    if ( !m_viewFactory ) return swapViews( m_viewTypes[pane], view );
    const int existingPosition = positionOf( view );
    if ( view == tile_editor_view_t::PERSPECTIVE && existingPosition >= 0 ) {
        // Relocate the existing entire 3D pane. Its native context and camera
        // never move between widget parents; the requested pane takes its place.
        if ( wasMaximized ) leaveMaximized();
        auto order = m_viewOrder;
        std::swap( order[physicalPosition], order[existingPosition] );
        applyViewOrder( order );
        focusPane( static_cast<int>( order[physicalPosition] ) );
        if ( wasMaximized ) toggleMaximize();
        return true;
    }
    QWidget *widget = widgetForPane( pane, view );
    if ( widget == nullptr ) return false;
    if ( wasMaximized && pane != m_activePane ) leaveMaximized();
    replacePaneWidget( pane, view, widget );
    focusPane( pane );
    if ( wasMaximized && !m_bMaximized ) toggleMaximize();
    return true;
}

void CypherTileViewWorkspace::applyViewOrder( const std::array<tile_editor_view_t, 4> &order )
{
    // Moving a whole pane keeps its camera, navigation state and native renderer.
    // All panes stay under this same top-level window, including the GL viewport.
    const bool wasMaximized = m_bMaximized;
    const bool wereUpdatesEnabled = updatesEnabled();
    const int previousActive = m_activePane;
    setUpdatesEnabled( false );
    if ( wasMaximized ) leaveMaximized();
    const std::array sizes{ m_pColumns->saveState(), m_rows[0]->saveState(), m_rows[1]->saveState() };
    m_viewOrder = order;
    for ( int position = 0; position < 4; ++position ) {
        auto *pPane = m_panes[static_cast<int>( order[position] )];
        m_rows[position / 2]->insertWidget( position % 2, pPane );
    }
    m_pColumns->restoreState( sizes[0] );
    for ( int row = 0; row < 2; ++row ) m_rows[row]->restoreState( sizes[row + 1] );
    setActivePane( previousActive );
    if ( wasMaximized ) toggleMaximize();
    applyPaneVisibility();
    for ( int pane = 0; pane < 4; ++pane ) updatePaneHeader( pane );
    updatePaneControls();
    setUpdatesEnabled( wereUpdatesEnabled );
}

bool CypherTileViewWorkspace::swapViews( tile_editor_view_t first, tile_editor_view_t requested )
{
    if ( !IsValidView( first ) || !IsValidView( requested ) ) return false;
    if ( positionOf( first ) < 0 || positionOf( requested ) < 0 ) return false;
    const bool wasMaximized = m_bMaximized;
    // The requested view takes the calling pane's place, even when it fills the workspace.
    if ( wasMaximized ) leaveMaximized();
    if ( first != requested ) {
        auto order = m_viewOrder;
        std::swap( order[positionOf( first )], order[positionOf( requested )] );
        applyViewOrder( order );
    }
    focusView( requested );
    if ( wasMaximized ) toggleMaximize();
    return true;
}

void CypherTileViewWorkspace::resetViewOrder()
{
    if ( m_viewFactory ) {
        const auto previousView = activeView();
        const bool wasMaximized = m_bMaximized;
        if ( wasMaximized ) leaveMaximized();
        applyViewOrder( kDefaultViewOrder );
        // Restore 2D panes first so the single Perspective host is free to move.
        for ( int position = 1; position < 4; ++position ) setPaneView( position, kDefaultViewOrder[position] );
        setPaneView( 0, tile_editor_view_t::PERSPECTIVE );
        focusView( previousView );
        if ( wasMaximized ) toggleMaximize();
        return;
    }
    applyViewOrder( kDefaultViewOrder );
}

void CypherTileViewWorkspace::showFourViews()
{
    leaveMaximized();
    m_paneVisible.fill( true );
    applyPaneVisibility();
    if ( !m_allVisibleState[0].isEmpty() ) {
        m_pColumns->restoreState( m_allVisibleState[0] );
        for ( int i = 0; i < 2; ++i ) m_rows[i]->restoreState( m_allVisibleState[i + 1] );
    }
    updatePaneControls();
}

void CypherTileViewWorkspace::leaveMaximized()
{
    if ( !m_bMaximized ) return;
    m_bMaximized = false;
    applyPaneVisibility();
    m_pColumns->restoreState( m_quadState[0] );
    for ( int i = 0; i < 2; ++i ) m_rows[i]->restoreState( m_quadState[i + 1] );
    for ( QToolButton *pButton : m_maximizeButtons ) pButton->setText( QStringLiteral( "□" ) );
    updatePaneControls();
}

void CypherTileViewWorkspace::toggleMaximize()
{
    if ( m_bMaximized ) { leaveMaximized(); return; }
    m_quadState = { m_pColumns->saveState(), m_rows[0]->saveState(), m_rows[1]->saveState() };
    m_bMaximized = true;
    const int active = m_activePane;
    applyPaneVisibility();
    m_maximizeButtons[active]->setText( QStringLiteral( "▦" ) );
    updatePaneControls();
}

int CypherTileViewWorkspace::visiblePaneCount() const
{
    return static_cast<int>( std::count( m_paneVisible.begin(), m_paneVisible.end(), true ) );
}

bool CypherTileViewWorkspace::isPaneVisible( int physicalPosition ) const
{
    if ( physicalPosition < 0 || physicalPosition >= 4 ) return false;
    return m_paneVisible[static_cast<int>( m_viewOrder[physicalPosition] )];
}

bool CypherTileViewWorkspace::setPaneVisible( int physicalPosition, bool visible )
{
    if ( physicalPosition < 0 || physicalPosition >= 4 ) return false;
    const int pane = static_cast<int>( m_viewOrder[physicalPosition] );
    if ( m_paneVisible[pane] == visible ) return true;
    if ( !visible && visiblePaneCount() <= 1 ) return false;
    leaveMaximized();
    if ( !visible && visiblePaneCount() == 4 ) captureAllVisibleSplitterState();
    m_paneVisible[pane] = visible;
    if ( !visible && pane == m_activePane ) {
        for ( int position = 0; position < 4; ++position ) {
            const int candidate = static_cast<int>( m_viewOrder[position] );
            if ( m_paneVisible[candidate] ) { m_activePane = candidate; break; }
        }
    }
    applyPaneVisibility();
    if ( visiblePaneCount() == 4 && !m_allVisibleState[0].isEmpty() ) {
        m_pColumns->restoreState( m_allVisibleState[0] );
        for ( int i = 0; i < 2; ++i ) m_rows[i]->restoreState( m_allVisibleState[i + 1] );
    }
    setActivePane( m_activePane );
    m_views[m_activePane]->setFocus( Qt::ShortcutFocusReason );
    updatePaneControls();
    return true;
}

void CypherTileViewWorkspace::applyPaneVisibility()
{
    if ( m_bMaximized ) {
        const int activeRow = positionOfPane( m_activePane ) / 2;
        for ( int pane = 0; pane < 4; ++pane ) m_panes[pane]->setVisible( pane == m_activePane );
        for ( int row = 0; row < 2; ++row ) m_rows[row]->setVisible( row == activeRow );
        return;
    }
    for ( int pane = 0; pane < 4; ++pane ) m_panes[pane]->setVisible( m_paneVisible[pane] );
    for ( int row = 0; row < 2; ++row ) {
        const int first = static_cast<int>( m_viewOrder[row * 2] );
        const int second = static_cast<int>( m_viewOrder[row * 2 + 1] );
        m_rows[row]->setVisible( m_paneVisible[first] || m_paneVisible[second] );
    }
}

void CypherTileViewWorkspace::captureAllVisibleSplitterState()
{
    if ( m_bMaximized || visiblePaneCount() != 4 ) return;
    m_allVisibleState = { m_pColumns->saveState(), m_rows[0]->saveState(), m_rows[1]->saveState() };
}

void CypherTileViewWorkspace::setSplitterWidth( int width )
{
    width = std::clamp( width, 3, 16 );
    m_pColumns->setHandleWidth( width );
    for ( auto *row : m_rows ) row->setHandleWidth( width );
}

int CypherTileViewWorkspace::splitterWidth() const
{
    return m_pColumns->handleWidth();
}

void CypherTileViewWorkspace::updatePaneControls()
{
    const bool canClose = visiblePaneCount() > 1;
    for ( int pane = 0; pane < 4; ++pane ) {
        if ( m_closeButtons[pane] != nullptr ) {
            m_closeButtons[pane]->setEnabled( canClose && m_paneVisible[pane] );
            m_closeButtons[pane]->setToolTip( canClose
                ? tr( "Close this viewport; its camera and navigation state are retained" )
                : tr( "At least one viewport must remain open" ) );
        }
        if ( m_maximizeButtons[pane] != nullptr ) {
            m_maximizeButtons[pane]->setEnabled( m_paneVisible[pane] );
            if ( !m_bMaximized ) m_maximizeButtons[pane]->setText( QStringLiteral( "□" ) );
        }
    }
}

void CypherTileViewWorkspace::showPaneContextMenu(
    QWidget *view, const QPoint &globalPosition )
{
    if ( view == nullptr ) return;
    for ( int pane = 0; pane < 4; ++pane ) {
        if ( m_views[pane] == view ) {
            showPaneContextMenu( pane, globalPosition );
            return;
        }
    }
}

void CypherTileViewWorkspace::showPaneContextMenu(
    int pane, const QPoint &globalPosition )
{
    if ( pane < 0 || pane >= 4 || !m_paneVisible[pane] ) return;
    setActivePane( pane );
    const int physicalPosition = positionOfPane( pane );
    QMenu menu( this );
    menu.setObjectName( QStringLiteral( "TileViewContextMenu" ) );
    QAction *heading = menu.addAction( tr( "Viewport %1 · %2" )
        .arg( physicalPosition + 1 ).arg( ViewTitle( m_viewTypes[pane] ) ) );
    heading->setEnabled( false );

    QMenu *viewType = menu.addMenu( tr( "View Type" ) );
    viewType->setObjectName( QStringLiteral( "TileViewContextTypeMenu" ) );
    auto *types = new QActionGroup( viewType );
    types->setExclusive( true );
    for ( int target = 0; target < 4; ++target ) {
        const auto requested = static_cast<tile_editor_view_t>( target );
        QAction *action = viewType->addAction( ViewTitle( requested ) );
        action->setObjectName( QStringLiteral( "TileViewContextType%1" ).arg( target ) );
        action->setCheckable( true );
        action->setChecked( m_viewTypes[pane] == requested );
        types->addAction( action );
        connect( action, &QAction::triggered, this,
                 [this, physicalPosition, requested] {
            (void)setPaneView( physicalPosition, requested );
        } );
    }

    QAction *frame = menu.addAction( tr( "Frame This View" ) );
    frame->setObjectName( QStringLiteral( "TileViewContextFrame" ) );
    connect( frame, &QAction::triggered, this, [this, pane] {
        focusPane( pane );
        if ( m_frameCallback ) m_frameCallback( m_viewTypes[pane] );
    } );
    QAction *maximize = menu.addAction(
        m_bMaximized ? tr( "Restore Viewports" ) : tr( "Maximize This View" ) );
    maximize->setObjectName( QStringLiteral( "TileViewContextMaximize" ) );
    connect( maximize, &QAction::triggered, this, [this, pane] {
        if ( !m_bMaximized ) focusPane( pane );
        toggleMaximize();
    } );

    if ( m_contextMenuContributor ) {
        menu.addSeparator();
        m_contextMenuContributor( menu, m_viewTypes[pane] );
    }

    menu.addSeparator();
    QAction *close = menu.addAction( tr( "Close This View" ) );
    close->setObjectName( QStringLiteral( "TileViewContextClose" ) );
    close->setEnabled( visiblePaneCount() > 1 );
    connect( close, &QAction::triggered, this, [this, physicalPosition] {
        (void)setPaneVisible( physicalPosition, false );
    } );

    QMenu *visible = menu.addMenu( tr( "Visible Viewports" ) );
    visible->setObjectName( QStringLiteral( "TileViewContextVisibleMenu" ) );
    for ( int position = 0; position < 4; ++position ) {
        const int slotPane = static_cast<int>( m_viewOrder[position] );
        QAction *action = visible->addAction( tr( "%1 · %2" )
            .arg( position + 1 ).arg( ViewTitle( m_viewTypes[slotPane] ) ) );
        action->setObjectName( QStringLiteral( "TileViewContextVisible%1" ).arg( position ) );
        action->setCheckable( true );
        action->setChecked( m_paneVisible[slotPane] );
        action->setEnabled( !m_paneVisible[slotPane] || visiblePaneCount() > 1 );
        connect( action, &QAction::toggled, this, [this, position]( bool checked ) {
            (void)setPaneVisible( position, checked );
        } );
    }
    QAction *restore = menu.addAction( tr( "Restore Four Views" ) );
    restore->setObjectName( QStringLiteral( "TileViewContextRestoreAll" ) );
    restore->setEnabled( visiblePaneCount() < 4 || m_bMaximized );
    connect( restore, &QAction::triggered, this, &CypherTileViewWorkspace::showFourViews );
    QAction *reset = menu.addAction( tr( "Reset Four-View Layout" ) );
    reset->setObjectName( QStringLiteral( "TileViewContextReset" ) );
    connect( reset, &QAction::triggered, this, [this] {
        showFourViews();
        resetViewOrder();
    } );
    menu.exec( globalPosition );
}

bool CypherTileViewWorkspace::eventFilter( QObject *pObject, QEvent *pEvent )
{
    for ( int i = 0; i < 4; ++i ) {
        if ( pObject != m_views[i] && pObject != m_headers[i] && pObject != m_panes[i] ) continue;
        const bool bPointerRoutingEvent = pEvent->type() == QEvent::Enter ||
            pEvent->type() == QEvent::MouseMove;
        const bool bEventHasButtons = pEvent->type() == QEvent::MouseMove &&
            static_cast<QMouseEvent *>( pEvent )->buttons() != Qt::NoButton;
        if ( bPointerRoutingEvent && m_bActivateOnHover &&
             window()->isActiveWindow() && !bEventHasButtons &&
             QApplication::mouseButtons() == Qt::NoButton &&
             QApplication::activePopupWidget() == nullptr && QApplication::activeModalWidget() == nullptr ) {
            // Viewport commands follow the pointer. The guarded popup/modal
            // and mouse-button cases keep menu interaction and cross-pane
            // drags intact; entering a viewport deliberately ends text entry
            // just as clicking it would, but without the extra click.
            if ( m_activePane != i ) setActivePane( i );
            if ( !m_views[i]->hasFocus() )
                m_views[i]->setFocus( Qt::MouseFocusReason );
        }
        if ( pEvent->type() == QEvent::FocusIn || pEvent->type() == QEvent::MouseButtonPress ) {
            setActivePane( i );
        }
        if ( pObject == m_headers[i] && pEvent->type() == QEvent::MouseButtonDblClick ) {
            focusPane( i );
            toggleMaximize();
            return true;
        }
        if ( pObject == m_headers[i] && pEvent->type() == QEvent::ContextMenu ) {
            auto *context = static_cast<QContextMenuEvent *>( pEvent );
            showPaneContextMenu( i, context->globalPos() );
            return true;
        }
        break;
    }
    return QWidget::eventFilter( pObject, pEvent );
}

void CypherTileViewWorkspace::setFrameCallback( std::function<void( tile_editor_view_t )> callback )
{
    m_frameCallback = std::move( callback );
}

void CypherTileViewWorkspace::setContextMenuContributor(
    std::function<void( QMenu &, tile_editor_view_t )> callback )
{
    m_contextMenuContributor = std::move( callback );
}

void CypherTileViewWorkspace::setActivateOnHover( bool enabled )
{
    m_bActivateOnHover = enabled;
}

void CypherTileViewWorkspace::setCameraMenu( QMenu *menu )
{
    // QToolButton does not take ownership; the editor owns the actions/menu.
    m_pCameraMenuButton->setMenu( menu );
    m_pCameraMenuButton->setVisible( menu != nullptr && positionOf( tile_editor_view_t::PERSPECTIVE ) >= 0 );
}

void CypherTileViewWorkspace::restoreLayout( QSettings &settings )
{
    const bool wasMaximized = m_bMaximized;
    const int previousActive = m_activePane;
    if ( wasMaximized ) leaveMaximized();
    m_paneVisible.fill( true );
    applyPaneVisibility();
    auto order = kDefaultViewOrder;
    const auto stored = settings.value( QStringLiteral( "TileEditor/QuadView/order" ) ).toList();
    if ( stored.size() == 4 ) {
        std::array<bool, 4> seen{};
        bool valid = true;
        for ( int position = 0; position < 4; ++position ) {
            bool isInteger = false;
            const int view = stored[position].toInt( &isInteger );
            if ( !isInteger || view < 0 || view >= 4 || seen[view] ||
                 stored[position].toString() != QString::number( view ) ) {
                valid = false;
                break;
            }
            seen[view] = true;
            order[position] = static_cast<tile_editor_view_t>( view );
        }
        if ( !valid ) order = kDefaultViewOrder;
    }
    applyViewOrder( order );
    if ( m_viewFactory ) {
        std::array<tile_editor_view_t, 4> types{ tile_editor_view_t::TOP, tile_editor_view_t::PERSPECTIVE,
            tile_editor_view_t::FRONT, tile_editor_view_t::SIDE };
        const auto storedTypes = settings.value( QStringLiteral( "TileEditor/QuadView/types" ) ).toList();
        if ( storedTypes.size() == 4 ) {
            auto pending = types;
            bool valid = true;
            int perspectiveCount = 0;
            for ( int pane = 0; pane < 4; ++pane ) {
                bool integer = false;
                const int type = storedTypes[pane].toInt( &integer );
                if ( !integer || type < 0 || type >= 4 || storedTypes[pane].toString() != QString::number( type ) ) {
                    valid = false;
                    break;
                }
                pending[pane] = static_cast<tile_editor_view_t>( type );
                perspectiveCount += pending[pane] == tile_editor_view_t::PERSPECTIVE;
            }
            if ( valid && perspectiveCount <= 1 ) types = pending;
        }
        // Replace 2D types first, freeing a currently displayed renderer if its
        // saved destination changed. Duplicates are legal for all three 2D types.
        for ( int pane = 0; pane < 4; ++pane )
            if ( types[pane] != tile_editor_view_t::PERSPECTIVE )
                setPaneView( positionOfPane( pane ), types[pane] );
        for ( int pane = 0; pane < 4; ++pane )
            if ( types[pane] == tile_editor_view_t::PERSPECTIVE )
                setPaneView( positionOfPane( pane ), types[pane] );
        focusPane( previousActive );
    }
    for ( int i = 0; i < 3; ++i ) {
        const QByteArray state = settings.value( QStringLiteral( "TileEditor/QuadView/splitter%1" ).arg( i ) ).toByteArray();
        if ( !state.isEmpty() ) ( i == 0 ? m_pColumns : m_rows[i - 1] )->restoreState( state );
    }
    captureAllVisibleSplitterState();
    const auto storedVisibility = settings.value(
        QStringLiteral( "TileEditor/QuadView/visiblePanes" ) ).toList();
    if ( storedVisibility.size() == 4 ) {
        std::array<bool, 4> restoredVisibility{};
        bool valid = true;
        int visibleCount = 0;
        for ( int pane = 0; pane < 4; ++pane ) {
            const QString encoded = storedVisibility[pane].toString().trimmed().toLower();
            if ( encoded != QStringLiteral( "true" ) && encoded != QStringLiteral( "false" ) &&
                 encoded != QStringLiteral( "1" ) && encoded != QStringLiteral( "0" ) ) {
                valid = false;
                break;
            }
            restoredVisibility[pane] = encoded == QStringLiteral( "true" ) || encoded == QStringLiteral( "1" );
            visibleCount += restoredVisibility[pane] ? 1 : 0;
        }
        if ( valid && visibleCount > 0 ) m_paneVisible = restoredVisibility;
    }
    if ( !m_paneVisible[m_activePane] ) {
        for ( int position = 0; position < 4; ++position ) {
            const int pane = static_cast<int>( m_viewOrder[position] );
            if ( m_paneVisible[pane] ) { m_activePane = pane; break; }
        }
    }
    applyPaneVisibility();
    setActivePane( m_activePane );
    updatePaneControls();
    if ( wasMaximized ) toggleMaximize();
}

void CypherTileViewWorkspace::saveLayout( QSettings &settings ) const
{
    QVariantList order;
    for ( const auto view : m_viewOrder ) order.append( static_cast<int>( view ) );
    settings.setValue( QStringLiteral( "TileEditor/QuadView/order" ), order );
    QVariantList types;
    for ( const auto view : m_viewTypes ) types.append( static_cast<int>( view ) );
    settings.setValue( QStringLiteral( "TileEditor/QuadView/types" ), types );
    QVariantList visibility;
    for ( const bool visible : m_paneVisible ) visibility.append( visible );
    settings.setValue( QStringLiteral( "TileEditor/QuadView/visiblePanes" ), visibility );
    for ( int i = 0; i < 3; ++i ) {
        // A maximized workspace with one or more intentionally hidden panes
        // has zero-sized children in m_quadState. Persist the last complete
        // four-pane state so Restore Four Views cannot resurrect collapsed
        // viewports after an application restart.
        const QByteArray normalState = visiblePaneCount() == 4
            ? ( m_bMaximized
                ? m_quadState[i]
                : ( i == 0 ? m_pColumns : m_rows[i - 1] )->saveState() )
            : m_allVisibleState[i];
        settings.setValue( QStringLiteral( "TileEditor/QuadView/splitter%1" ).arg( i ),
            normalState );
    }
}
}
