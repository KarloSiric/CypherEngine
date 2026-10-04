//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_AssetWindow.cpp
//  Purpose: Implements the Asset Browser window (Hammer 5's pop-up asset
//           browser): tabs, filter, saved searches, list / grid / tree,
//           status badges, and Accept.
//
//  History:
//  - Created by Karlo Siric on 2026-10-02
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_AssetWindow.h"

#include "CypherEditorGui_AssetBrowser.h"
#include "CypherEditorGui_Style.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"

#include <QAbstractTableModel>
#include <QAction>
#include <QActionGroup>
#include <QButtonGroup>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QSet>
#include <QShowEvent>
#include <QSlider>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

namespace cypher::editor::gui
{

using namespace cypher::common;

namespace
{

constexpr const char *kSavedSearchesSetting = "editor.assets.saved_searches";
constexpr const char *kTileSizeSetting = "editor.assets.thumbnail_size";
constexpr int kListIcon = 18;
constexpr int kTileMin = 48;
constexpr int kTileMax = 256;
constexpr int kLoadsPerTick = 6;  // Thumbnails decoded per timer tick: the window stays responsive.
constexpr int kNameLines = 18;    // Pixels under a grid tile for its name.
constexpr int kBadge = 14;        // Status badge size on grid tiles.

enum column_t : int { COLUMN_NAME = 0, COLUMN_TYPE, COLUMN_FOLDER, COLUMN_ROOT, COLUMN_SIZE, COLUMN_MODIFIED, COLUMN_COUNT };

QString FromView( string_view_t view )
{
    return QString::fromUtf8( view.pData != nullptr ? view.pData : "", static_cast<qsizetype>( view.cchLength ) );
}

const char *KindIcon( editor_asset_kind_t kind )
{
    switch ( kind ) {
        case editor_asset_kind_t::MATERIAL: return "asset-material";
        case editor_asset_kind_t::TEXTURE: return "asset-texture";
        case editor_asset_kind_t::MODEL: return "asset-model";
        case editor_asset_kind_t::PARTICLE: return "asset-particle";
        case editor_asset_kind_t::PREFAB: return "asset-prefab";
        case editor_asset_kind_t::SOUND: return "asset-sound";
        case editor_asset_kind_t::SHADER: return "asset-shader";
        case editor_asset_kind_t::MAP: return "asset-map";
        case editor_asset_kind_t::FONT: return "asset-font";
        case editor_asset_kind_t::ANIMATION: return "asset-animation";
        case editor_asset_kind_t::COUNT: break;
    }
    return "asset-folder";
}

QString SizeText( u64 cbSize )
{
    if ( cbSize < 1024u ) { return QStringLiteral( "%1 B" ).arg( cbSize ); }
    if ( cbSize < 1024u * 1024u ) { return QStringLiteral( "%1 KB" ).arg( static_cast<double>( cbSize ) / 1024.0, 0, 'f', 1 ); }
    return QStringLiteral( "%1 MB" ).arg( static_cast<double>( cbSize ) / ( 1024.0 * 1024.0 ), 0, 'f', 1 );
}

// Only materials and textures resolve to an image; asking the browser for
// anything else would read the file for nothing.
bool HasImageKind( editor_asset_kind_t kind )
{
    return kind == editor_asset_kind_t::MATERIAL || kind == editor_asset_kind_t::TEXTURE;
}

struct row_t {
    QString path;
    int iAsset{ -1 }; // Catalogue index; -1 for a path no root has.
    editor_asset_kind_t kind{ editor_asset_kind_t::COUNT };
};

struct saved_search_t {
    QString name;
    QString filter;
    int tab{ ASSET_WINDOW_TAB_ALL };
    u32 kindMask{ EDITOR_ASSET_KIND_ALL };
    bool bSources{ false };
};

class asset_window_t;

// One model behind List and Grid: the grid shows column 0 as tiles.
class asset_model_t final : public QAbstractTableModel {
public:
    explicit asset_model_t( asset_window_t *pWindow ) : QAbstractTableModel( nullptr ), m_pWindow( pWindow ) {}
    int rowCount( const QModelIndex &parent = {} ) const override;
    int columnCount( const QModelIndex &parent = {} ) const override { return parent.isValid() ? 0 : COLUMN_COUNT; }
    QVariant data( const QModelIndex &index, int role ) const override;
    QVariant headerData( int section, Qt::Orientation orientation, int role ) const override;
    void Reset() { beginResetModel(); endResetModel(); }
    void Changed( int iRow ) { emit dataChanged( index( iRow, 0 ), index( iRow, COLUMN_COUNT - 1 ) ); }

private:
    asset_window_t *m_pWindow;
};

// Grid tiles: the thumbnail, the name under it, and Hammer's small status
// icons in the corners (in the map, a source file, missing).
class tile_delegate_t final : public QStyledItemDelegate {
public:
    explicit tile_delegate_t( asset_window_t *pWindow ) : QStyledItemDelegate( nullptr ), m_pWindow( pWindow ) {}
    void paint( QPainter *pPainter, const QStyleOptionViewItem &option, const QModelIndex &index ) const override;
    QSize sizeHint( const QStyleOptionViewItem &option, const QModelIndex &index ) const override;

private:
    asset_window_t *m_pWindow;
};

class asset_window_t final : public QDialog {
public:
    asset_window_t( QWidget *pParent, editor_gui_t *pGui, QWidget *pBrowser, bool bEmbedded = false )
        : QDialog( pParent ), m_pGui( pGui ), m_pBrowser( pBrowser ), m_bEmbedded( bEmbedded ), m_model( this ), m_delegate( this )
    {
        CY_ASSERT( pGui != nullptr );
        setObjectName( QStringLiteral( "EditorAssetWindow" ) );
        setWindowTitle( QStringLiteral( "Asset Browser" ) );
        if ( m_bEmbedded ) { setWindowFlags( Qt::Widget ); }
        else { setWindowFlag( Qt::Tool, true ); }
        setSizeGripEnabled( !m_bEmbedded );
        resize( 1180, 760 );
        m_tileSize = static_cast<int>( std::clamp<i64>( EditorSettings_Integer( &pGui->settings, kTileSizeSetting, 128 ), kTileMin, kTileMax ) );
        LoadSavedSearches();
        Build();
        m_loader.setInterval( 0 );
        QObject::connect( &m_loader, &QTimer::timeout, this, [this]() { LoadSome( kLoadsPerTick ); } );
    }

    // ---- State the model and delegate read -------------------------------

    const editor_asset_catalog_t *Catalog() const { return m_pBrowser != nullptr ? EditorAssetBrowser_Catalog( m_pBrowser ) : nullptr; }
    const std::vector<row_t> &Rows() const { return m_rows; }
    const editor_asset_t *AssetOf( const row_t &row ) const
    {
        const editor_asset_catalog_t *pCatalog = Catalog();
        return pCatalog != nullptr && row.iAsset >= 0 ? EditorAssets_At( pCatalog, static_cast<usize>( row.iAsset ) ) : nullptr;
    }
    bool IsUsed( const QString &path ) const { return m_used.contains( path ); }
    int TileSize() const { return m_tileSize; }
    const editor_style_t &Style() const { return m_pGui->style; }

    QPixmap Thumbnail( const row_t &row, int size )
    {
        const QString key = QStringLiteral( "%1@%2" ).arg( row.path ).arg( size );
        if ( const auto it = m_tiles.constFind( key ); it != m_tiles.constEnd() ) { return it.value(); }
        const qreal dpr = devicePixelRatioF();
        QPixmap tile;
        bool bFinal = true;
        if ( HasImageKind( row.kind ) && row.iAsset >= 0 ) {
            if ( const auto image = m_images.constFind( row.path ); image != m_images.constEnd() ) {
                if ( !image->isNull() ) {
                    tile = QPixmap::fromImage( image->scaled( QSize( size, size ) * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation ) );
                    tile.setDevicePixelRatio( dpr );
                }
            } else {
                Enqueue( row.path );
                bFinal = false; // The kind icon stands in until the image arrives.
            }
        }
        if ( tile.isNull() ) {
            const int iconSize = size <= kListIcon ? size : std::max( 24, size / 2 );
            tile = EditorStyle_Icon( m_pGui->style, KindIcon( row.kind ) ).pixmap( QSize( iconSize, iconSize ), dpr );
        }
        if ( bFinal ) { m_tiles.insert( key, tile ); }
        return tile;
    }

    // ---- Public operations ---------------------------------------------

    void SetAccept( editor_asset_accept_fn pfnAccept, void *pContext ) { m_pfnAccept = pfnAccept; m_pAcceptContext = pContext; }
    void SetSelectionSource( editor_asset_selection_fn pfn, void *pContext ) { m_pfnSelection = pfn; m_pSelectionContext = pContext; }

    void SetUsed( const QStringList &paths )
    {
        m_usedList = paths;
        m_usedList.removeDuplicates();
        std::sort( m_usedList.begin(), m_usedList.end() );
        m_used = QSet<QString>( m_usedList.cbegin(), m_usedList.cend() );
        if ( isVisible() ) { Rebuild(); }
    }

    void Pick( editor_asset_kind_t kind, const QString &current, editor_asset_accept_fn pfnPick, void *pContext )
    {
        // A central pane is persistent browsing content. Picker ownership
        // belongs to a separate dialog, never a pane that may be hidden.
        if ( m_bEmbedded ) { return; }
        m_pfnPick = pfnPick;
        m_pPickContext = pContext;
        m_pickKind = kind;
        m_bPicking = true;
        setWindowTitle( QStringLiteral( "Asset Browser - Choose %1" ).arg( QString::fromLatin1( EditorAssets_KindLabel( kind ) ) ) );
        { const QSignalBlocker blocker( m_pFilter ); m_pFilter->clear(); }
        SetTab( EditorAssetWindow_KindTab( kind ) );
        Refresh();
        if ( !current.isEmpty() ) { ( void )Select( current ); }
        show();
        raise();
        activateWindow();
        m_pFilter->setFocus();
    }

    bool IsPicking() const { return m_bPicking; }

    void Refresh( bool invalidateImages = false )
    {
        if ( invalidateImages ) {
            m_images.clear();
            m_tiles.clear();
            m_queue.clear();
            m_queued.clear();
        }
        RefreshRoots();
        RefreshTabs();
        Rebuild();
    }

    void SetTab( int tab )
    {
        if ( tab < 0 || tab >= ASSET_WINDOW_TAB_COUNT ) { return; }
        if ( m_pTabs->currentIndex() != tab ) { m_pTabs->setCurrentIndex( tab ); } // Rebuilds through the signal.
        else { Rebuild(); }
    }
    int Tab() const { return m_pTabs->currentIndex(); }

    void SetFilter( const QString &text )
    {
        if ( m_pFilter->text() != text ) { m_pFilter->setText( text ); } // Rebuilds through the signal.
    }

    void SetView( editor_asset_window_view_t view )
    {
        m_view = view;
        const QSignalBlocker a( m_pList ), b( m_pGrid ), c( m_pTree );
        m_pListRadio->setChecked( view == editor_asset_window_view_t::LIST );
        m_pGridRadio->setChecked( view == editor_asset_window_view_t::GRID );
        m_pTreeRadio->setChecked( view == editor_asset_window_view_t::TREE );
        m_pStack->setCurrentIndex( static_cast<int>( view ) );
        m_pSize->setEnabled( view == editor_asset_window_view_t::GRID );
        if ( view == editor_asset_window_view_t::TREE ) { RebuildTree(); }
        RestoreSelection();
    }
    editor_asset_window_view_t View() const { return m_view; }

    void SetKindMask( u32 mask )
    {
        m_kindMask = mask & EDITOR_ASSET_KIND_ALL;
        for ( QAction *pAction : m_kindActions ) {
            const QSignalBlocker blocker( pAction );
            pAction->setChecked( ( m_kindMask & EditorAssets_KindBit( static_cast<editor_asset_kind_t>( pAction->data().toInt() ) ) ) != 0u );
        }
        UpdateFilterButtons();
        Rebuild();
    }

    void SetSources( bool bShow )
    {
        m_bSources = bShow;
        { const QSignalBlocker blocker( m_pSources ); m_pSources->setChecked( bShow ); }
        Rebuild();
    }

    void SetRootVisible( int iRoot, bool bVisible )
    {
        if ( iRoot < 0 ) { return; }
        if ( bVisible ) { m_hiddenRoots.remove( iRoot ); } else { m_hiddenRoots.insert( iRoot ); }
        RefreshRoots();
        UpdateFilterButtons();
        Rebuild();
    }

    QStringList Visible() const
    {
        QStringList paths;
        paths.reserve( static_cast<qsizetype>( m_rows.size() ) );
        for ( const row_t &row : m_rows ) { paths.append( row.path ); }
        return paths;
    }

    bool Select( const QString &path )
    {
        for ( usize i = 0u; i < m_rows.size(); ++i ) {
            if ( m_rows[i].path != path ) { continue; }
            m_selected = path;
            RestoreSelection();
            return true;
        }
        return false;
    }

    QString Selected() const { return m_selected; }

    bool Accept()
    {
        const row_t *pRow = SelectedRow();
        if ( pRow == nullptr || !CanAccept( *pRow ) ) { return false; }
        const QString path = pRow->path;
        const editor_asset_kind_t kind = pRow->kind;
        if ( m_bPicking ) {
            const editor_asset_accept_fn pfn = m_pfnPick;
            void *pContext = m_pPickContext;
            EndPick();
            hide();
            if ( pfn != nullptr ) { pfn( pContext, path, kind ); }
            return true;
        }
        if ( m_pfnAccept != nullptr ) { m_pfnAccept( m_pAcceptContext, path, kind ); }
        return true;
    }

    void FocusResults()
    {
        if ( SelectedRow() == nullptr && !m_rows.empty() ) { ( void )Select( m_rows.front().path ); }
        CurrentView()->setFocus();
    }

    QString Status() const { return m_pCount->text(); }
    bool HasThumbnail( const QString &path ) const { return !m_images.value( path ).isNull(); }

    void LoadAll()
    {
        for ( const row_t &row : m_rows ) { ( void )Thumbnail( row, m_tileSize ); }
        while ( !m_queue.isEmpty() ) { LoadSome( kLoadsPerTick ); }
    }

    bool SaveSearch( const QString &name )
    {
        const QString trimmed = name.trimmed();
        if ( trimmed.isEmpty() ) { return false; }
        saved_search_t search{ trimmed, m_pFilter->text(), Tab(), m_kindMask, m_bSources };
        bool bReplaced = false;
        for ( saved_search_t &existing : m_saved ) {
            if ( existing.name.compare( trimmed, Qt::CaseInsensitive ) == 0 ) { existing = search; bReplaced = true; }
        }
        if ( !bReplaced ) { m_saved.append( search ); }
        WriteSavedSearches();
        RefreshSavedCombo( trimmed );
        return true;
    }

    bool LoadSearch( const QString &name )
    {
        for ( const saved_search_t &search : m_saved ) {
            if ( search.name.compare( name, Qt::CaseInsensitive ) != 0 ) { continue; }
            { const QSignalBlocker a( m_pFilter ), b( m_pTabs ); m_pFilter->setText( search.filter ); m_pTabs->setCurrentIndex( search.tab ); }
            m_bSources = search.bSources;
            { const QSignalBlocker blocker( m_pSources ); m_pSources->setChecked( search.bSources ); }
            SetKindMask( search.kindMask ); // Rebuilds.
            RefreshSavedCombo( search.name );
            return true;
        }
        return false;
    }

    bool DeleteSearch( const QString &name )
    {
        for ( qsizetype i = 0; i < m_saved.size(); ++i ) {
            if ( m_saved[i].name.compare( name, Qt::CaseInsensitive ) != 0 ) { continue; }
            m_saved.removeAt( i );
            WriteSavedSearches();
            RefreshSavedCombo( QString() );
            return true;
        }
        return false;
    }

    QStringList SavedSearches() const
    {
        QStringList names;
        for ( const saved_search_t &search : m_saved ) { names.append( search.name ); }
        return names;
    }

protected:
    void showEvent( QShowEvent *pEvent ) override
    {
        if ( m_bEmbedded ) { QWidget::showEvent( pEvent ); }
        else { QDialog::showEvent( pEvent ); }
        Refresh(); // The catalogue may have been rescanned while hidden.
    }

    void reject() override
    {
        if ( m_bEmbedded ) { return; }
        EndPick();
        QDialog::reject();
    }

    void keyPressEvent( QKeyEvent *pEvent ) override
    {
        if ( pEvent->key() == Qt::Key_Return || pEvent->key() == Qt::Key_Enter ) {
            ( void )Accept();
            return;
        }
        if ( m_bEmbedded ) { QWidget::keyPressEvent( pEvent ); }
        else { QDialog::keyPressEvent( pEvent ); }
    }

    bool eventFilter( QObject *pWatched, QEvent *pEvent ) override
    {
        // Down from the filter moves into the results, as in Hammer.
        if ( pWatched == m_pFilter && pEvent->type() == QEvent::KeyPress && static_cast<QKeyEvent *>( pEvent )->key() == Qt::Key_Down ) {
            FocusResults();
            return true;
        }
        return m_bEmbedded ? QWidget::eventFilter( pWatched, pEvent ) : QDialog::eventFilter( pWatched, pEvent );
    }

private:
    // ---- Construction ----------------------------------------------------

    QToolButton *ToolButton( const QString &text, const QString &tip, const QString &name, const char *pIcon = nullptr )
    {
        auto *pButton = new QToolButton( this );
        pButton->setObjectName( name );
        pButton->setText( text );
        pButton->setToolTip( tip );
        pButton->setFocusPolicy( Qt::NoFocus );
        if ( pIcon != nullptr ) {
            pButton->setIcon( EditorStyle_Icon( m_pGui->style, pIcon ) );
            pButton->setToolButtonStyle( text.isEmpty() ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon );
        }
        return pButton;
    }

    void Build()
    {
        auto *pLayout = new QVBoxLayout( this );
        pLayout->setContentsMargins( 6, 6, 6, 6 );
        pLayout->setSpacing( 4 );

        m_pTabs = new QTabBar( this );
        m_pTabs->setObjectName( QStringLiteral( "AssetWindowTabs" ) );
        m_pTabs->setDocumentMode( true );
        m_pTabs->setExpanding( false );
        m_pTabs->setDrawBase( false );
        m_pTabs->setUsesScrollButtons( true );
        m_pTabs->addTab( EditorStyle_Icon( m_pGui->style, "asset-browser" ), QStringLiteral( "All" ) );
        for ( u32 k = 0u; k < static_cast<u32>( editor_asset_kind_t::COUNT ); ++k ) {
            const auto kind = static_cast<editor_asset_kind_t>( k );
            m_pTabs->addTab( EditorStyle_Icon( m_pGui->style, KindIcon( kind ) ), QString::fromLatin1( EditorAssets_KindName( kind ) ) );
        }
        m_pTabs->addTab( EditorStyle_Icon( m_pGui->style, "map-info" ), QStringLiteral( "Used in Map" ) );
        m_pTabs->addTab( EditorStyle_Icon( m_pGui->style, "select-objects" ), QStringLiteral( "Selection" ) );
        m_pTabs->setTabToolTip( ASSET_WINDOW_TAB_USED, QStringLiteral( "Every asset the open map names; missing ones are marked" ) );
        m_pTabs->setTabToolTip( ASSET_WINDOW_TAB_SELECTION, QStringLiteral( "Materials and assets the selected objects use" ) );
        QObject::connect( m_pTabs, &QTabBar::currentChanged, this, [this]( int ) { Rebuild(); } );
        pLayout->addWidget( m_pTabs );

        // Filter row: what to look for and how to show it.
        auto *pTop = new QHBoxLayout();
        pTop->setSpacing( 4 );
        auto *pFilterLabel = new QLabel( QStringLiteral( "Filter:" ), this );
        pTop->addWidget( pFilterLabel );
        m_pFilter = new QLineEdit( this );
        m_pFilter->setObjectName( QStringLiteral( "AssetWindowFilter" ) );
        m_pFilter->setPlaceholderText( QStringLiteral( "Name or folder: fuzzy, so \"cncwrn\" finds concrete_worn" ) );
        m_pFilter->setClearButtonEnabled( true );
        m_pFilter->addAction( EditorStyle_Icon( m_pGui->style, "edit-find" ), QLineEdit::LeadingPosition );
        m_pFilter->installEventFilter( this );
        QObject::connect( m_pFilter, &QLineEdit::textChanged, this, [this]( const QString & ) { Rebuild(); } );
        pTop->addWidget( m_pFilter, 3 );

        m_pSaved = new QComboBox( this );
        m_pSaved->setObjectName( QStringLiteral( "AssetWindowSavedSearch" ) );
        m_pSaved->setToolTip( QStringLiteral( "Saved searches: a tab, filter, and asset types under a name" ) );
        m_pSaved->setMinimumContentsLength( 14 );
        QObject::connect( m_pSaved, &QComboBox::activated, this, [this]( int index ) {
            if ( index > 0 ) { ( void )LoadSearch( m_pSaved->itemText( index ) ); }
        } );
        pTop->addWidget( m_pSaved, 1 );
        auto *pSave = ToolButton( QString(), QStringLiteral( "Save this search..." ), QStringLiteral( "AssetWindowSaveSearch" ), "file-save" );
        auto *pSavedMenu = new QMenu( pSave );
        pSavedMenu->addAction( QStringLiteral( "Save Search..." ), this, [this]() {
            bool bOk = false;
            const QString name = QInputDialog::getText( this, QStringLiteral( "Save Search" ), QStringLiteral( "Name:" ), QLineEdit::Normal,
                                                        m_pFilter->text(), &bOk );
            if ( bOk ) { ( void )SaveSearch( name ); }
        } );
        pSavedMenu->addAction( QStringLiteral( "Delete Selected Search" ), this, [this]() {
            if ( m_pSaved->currentIndex() > 0 ) { ( void )DeleteSearch( m_pSaved->currentText() ); }
        } );
        pSave->setMenu( pSavedMenu );
        pSave->setPopupMode( QToolButton::InstantPopup );
        pTop->addWidget( pSave );
        RefreshSavedCombo( QString() );

        pTop->addSpacing( 10 );
        m_pListRadio = new QRadioButton( QStringLiteral( "List" ), this );
        m_pGridRadio = new QRadioButton( QStringLiteral( "Grid" ), this );
        m_pTreeRadio = new QRadioButton( QStringLiteral( "Tree" ), this );
        m_pListRadio->setObjectName( QStringLiteral( "AssetWindowList" ) );
        m_pGridRadio->setObjectName( QStringLiteral( "AssetWindowGrid" ) );
        m_pTreeRadio->setObjectName( QStringLiteral( "AssetWindowTree" ) );
        auto *pViews = new QButtonGroup( this );
        pViews->addButton( m_pListRadio, static_cast<int>( editor_asset_window_view_t::LIST ) );
        pViews->addButton( m_pGridRadio, static_cast<int>( editor_asset_window_view_t::GRID ) );
        pViews->addButton( m_pTreeRadio, static_cast<int>( editor_asset_window_view_t::TREE ) );
        QObject::connect( pViews, &QButtonGroup::idClicked, this, [this]( int id ) { SetView( static_cast<editor_asset_window_view_t>( id ) ); } );
        for ( QRadioButton *pRadio : { m_pListRadio, m_pGridRadio, m_pTreeRadio } ) { pTop->addWidget( pRadio ); }
        m_pSize = new QSlider( Qt::Horizontal, this );
        m_pSize->setObjectName( QStringLiteral( "AssetWindowSize" ) );
        m_pSize->setRange( kTileMin, kTileMax );
        m_pSize->setValue( m_tileSize );
        m_pSize->setFixedWidth( 120 );
        m_pSize->setToolTip( QStringLiteral( "Thumbnail size" ) );
        QObject::connect( m_pSize, &QSlider::valueChanged, this, [this]( int value ) { SetTileSize( value ); } );
        pTop->addWidget( m_pSize );
        pLayout->addLayout( pTop );

        // Filter buttons: Hammer's Asset Types and Mods, plus sources.
        auto *pFilters = new QHBoxLayout();
        pFilters->setSpacing( 4 );
        m_pTypes = ToolButton( QStringLiteral( "Asset Types" ), QStringLiteral( "Which kinds the All tab lists" ), QStringLiteral( "AssetWindowTypes" ), "asset-browser" );
        auto *pTypesMenu = new QMenu( m_pTypes );
        pTypesMenu->setObjectName( QStringLiteral( "AssetWindowTypesMenu" ) );
        pTypesMenu->addAction( QStringLiteral( "Show All Types" ), this, [this]() { SetKindMask( EDITOR_ASSET_KIND_ALL ); } );
        pTypesMenu->addSeparator();
        for ( u32 k = 0u; k < static_cast<u32>( editor_asset_kind_t::COUNT ); ++k ) {
            const auto kind = static_cast<editor_asset_kind_t>( k );
            QAction *pAction = pTypesMenu->addAction( EditorStyle_Icon( m_pGui->style, KindIcon( kind ) ), QString::fromLatin1( EditorAssets_KindName( kind ) ) );
            pAction->setCheckable( true );
            pAction->setChecked( true );
            pAction->setData( static_cast<int>( k ) );
            QObject::connect( pAction, &QAction::toggled, this, [this, kind]( bool bOn ) {
                SetKindMask( bOn ? m_kindMask | EditorAssets_KindBit( kind ) : m_kindMask & ~EditorAssets_KindBit( kind ) );
            } );
            m_kindActions.append( pAction );
        }
        m_pTypes->setMenu( pTypesMenu );
        m_pTypes->setPopupMode( QToolButton::InstantPopup );
        pFilters->addWidget( m_pTypes );
        m_pRoots = ToolButton( QStringLiteral( "Content Roots" ), QStringLiteral( "Which content folders to list (Hammer's Mods)" ), QStringLiteral( "AssetWindowRoots" ), "asset-folder" );
        m_pRootsMenu = new QMenu( m_pRoots );
        m_pRootsMenu->setObjectName( QStringLiteral( "AssetWindowRootsMenu" ) );
        m_pRoots->setMenu( m_pRootsMenu );
        m_pRoots->setPopupMode( QToolButton::InstantPopup );
        pFilters->addWidget( m_pRoots );
        m_pSources = ToolButton( QStringLiteral( "Sources" ), QStringLiteral( "Also list source files (.png, .fbx, .wav) the recipes are built from" ),
                                 QStringLiteral( "AssetWindowSources" ), "file-import" );
        m_pSources->setCheckable( true );
        QObject::connect( m_pSources, &QToolButton::toggled, this, [this]( bool bOn ) { SetSources( bOn ); } );
        pFilters->addWidget( m_pSources );
        pFilters->addStretch( 1 );
        auto *pRefresh = ToolButton( QStringLiteral( "Rescan" ), QStringLiteral( "Read the content folders again" ), QStringLiteral( "AssetWindowRescan" ), "layout-reset" );
        QObject::connect( pRefresh, &QToolButton::clicked, this, [this]() {
            if ( m_pBrowser != nullptr ) { EditorAssetBrowser_Rescan( m_pBrowser ); }
            m_images.clear();
            m_tiles.clear();
            Refresh();
        } );
        pFilters->addWidget( pRefresh );
        pLayout->addLayout( pFilters );

        // The three views over the same rows.
        m_pStack = new QStackedWidget( this );
        m_pList = new QTreeView( m_pStack );
        m_pList->setObjectName( QStringLiteral( "AssetWindowListView" ) );
        m_pList->setRootIsDecorated( false );
        m_pList->setUniformRowHeights( true );
        m_pList->setAlternatingRowColors( true );
        m_pList->setSortingEnabled( false );
        m_pList->setIconSize( QSize( kListIcon, kListIcon ) );
        m_pList->setModel( &m_model );
        m_pList->header()->setStretchLastSection( false );
        m_pList->header()->setSectionResizeMode( COLUMN_NAME, QHeaderView::Stretch );
        for ( int c = COLUMN_TYPE; c < COLUMN_COUNT; ++c ) { m_pList->header()->setSectionResizeMode( c, QHeaderView::ResizeToContents ); }
        m_pGrid = new QListView( m_pStack );
        m_pGrid->setObjectName( QStringLiteral( "AssetWindowGridView" ) );
        m_pGrid->setViewMode( QListView::IconMode );
        m_pGrid->setResizeMode( QListView::Adjust );
        m_pGrid->setMovement( QListView::Static );
        m_pGrid->setUniformItemSizes( true );
        m_pGrid->setSpacing( 4 );
        m_pGrid->setWordWrap( false );
        m_pGrid->setModel( &m_model );
        m_pGrid->setItemDelegate( &m_delegate );
        {
            // One selection, whichever view shows it; the grid's own goes.
            QItemSelectionModel *pOwn = m_pGrid->selectionModel();
            m_pGrid->setSelectionModel( m_pList->selectionModel() );
            delete pOwn;
        }
        m_pTree = new QTreeWidget( m_pStack );
        m_pTree->setObjectName( QStringLiteral( "AssetWindowTreeView" ) );
        m_pTree->setHeaderLabels( { QStringLiteral( "Name" ), QStringLiteral( "Type" ), QStringLiteral( "Size" ) } );
        m_pTree->setIconSize( QSize( kListIcon, kListIcon ) );
        m_pTree->header()->setSectionResizeMode( 0, QHeaderView::Stretch );
        m_pStack->addWidget( m_pList );
        m_pStack->addWidget( m_pGrid );
        m_pStack->addWidget( m_pTree );
        pLayout->addWidget( m_pStack, 1 );
        for ( QAbstractItemView *pView : std::initializer_list<QAbstractItemView *>{ m_pList, m_pGrid, m_pTree } ) {
            pView->setSelectionMode( QAbstractItemView::SingleSelection );
            pView->setEditTriggers( QAbstractItemView::NoEditTriggers );
            QObject::connect( pView, &QAbstractItemView::doubleClicked, this, [this]( const QModelIndex & ) { ( void )Accept(); } );
        }
        QObject::connect( m_pList->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this]( const QModelIndex &current, const QModelIndex & ) {
            if ( m_bRestoring ) { return; }
            m_selected = current.isValid() && current.row() < static_cast<int>( m_rows.size() ) ? m_rows[static_cast<usize>( current.row() )].path : QString();
            UpdateFooter();
        } );
        QObject::connect( m_pTree, &QTreeWidget::currentItemChanged, this, [this]( QTreeWidgetItem *pItem, QTreeWidgetItem * ) {
            if ( m_bRestoring ) { return; }
            m_selected = pItem != nullptr ? pItem->data( 0, Qt::UserRole ).toString() : QString(); // A folder selects nothing.
            UpdateFooter();
        } );

        // Footer: what is visible, what is selected, and Accept.
        auto *pFooter = new QHBoxLayout();
        m_pCount = new QLabel( this );
        m_pCount->setObjectName( QStringLiteral( "AssetWindowCount" ) );
        pFooter->addWidget( m_pCount );
        pFooter->addSpacing( 12 );
        m_pPath = new QLabel( this );
        m_pPath->setObjectName( QStringLiteral( "AssetWindowPath" ) );
        m_pPath->setTextInteractionFlags( Qt::TextSelectableByMouse );
        pFooter->addWidget( m_pPath, 1 );
        m_pAccept = new QPushButton( QStringLiteral( "Accept" ), this );
        m_pAccept->setObjectName( QStringLiteral( "AssetWindowAccept" ) );
        m_pAccept->setDefault( !m_bEmbedded );
        m_pAccept->setAutoDefault( !m_bEmbedded );
        QObject::connect( m_pAccept, &QPushButton::clicked, this, [this]() { ( void )Accept(); } );
        auto *pClose = new QPushButton( QStringLiteral( "Close" ), this );
        pClose->setObjectName( QStringLiteral( "AssetWindowClose" ) );
        pClose->setAutoDefault( false );
        pClose->setVisible( !m_bEmbedded );
        QObject::connect( pClose, &QPushButton::clicked, this, [this]() { reject(); } );
        pFooter->addWidget( m_pAccept );
        pFooter->addWidget( pClose );
        pLayout->addLayout( pFooter );

        SetView( editor_asset_window_view_t::GRID );
        UpdateFilterButtons();
    }

    // ---- Rows ------------------------------------------------------------

    void Rebuild()
    {
        m_rows.clear();
        const editor_asset_catalog_t *pCatalog = Catalog();
        const int tab = Tab();
        const QString filter = m_pFilter->text().trimmed();
        const auto addPath = [&]( const QString &path ) {
            if ( !filter.isEmpty() && !path.contains( filter, Qt::CaseInsensitive ) ) { return; }
            const QByteArray utf8 = path.toUtf8();
            const editor_asset_t *pAsset = pCatalog != nullptr ? EditorAssets_Find( pCatalog, string_view_t{ utf8.constData(), static_cast<usize>( utf8.size() ) } ) : nullptr;
            row_t row{ path, -1, EditorAssets_KindOf( string_view_t{ utf8.constData(), static_cast<usize>( utf8.size() ) }, nullptr ) };
            if ( pAsset != nullptr ) { row.iAsset = static_cast<int>( pAsset - pCatalog->assets.pData ); row.kind = pAsset->kind; }
            m_rows.push_back( row );
        };
        if ( tab == ASSET_WINDOW_TAB_USED ) {
            for ( const QString &path : m_usedList ) { addPath( path ); }
        } else if ( tab == ASSET_WINDOW_TAB_SELECTION ) {
            QStringList paths = m_pfnSelection != nullptr ? m_pfnSelection( m_pSelectionContext ) : QStringList();
            paths.removeDuplicates();
            std::sort( paths.begin(), paths.end() );
            for ( const QString &path : paths ) { addPath( path ); }
        } else if ( pCatalog != nullptr ) {
            const u32 mask = tab == ASSET_WINDOW_TAB_ALL ? m_kindMask
                : EditorAssets_KindBit( static_cast<editor_asset_kind_t>( tab - ASSET_WINDOW_TAB_FIRST_KIND ) );
            const QByteArray query = filter.toUtf8();
            vector_t<u32> indices{};
            if ( Vector_Init( &indices, m_pGui->pAllocator ) &&
                 EditorAssets_Filter( pCatalog, mask, string_view_t{ query.constData(), static_cast<usize>( query.size() ) },
                                      m_bSources ? CY_TRUE : CY_FALSE, &indices ) == editor_asset_status_t::OK ) {
                m_rows.reserve( indices.nCount );
                for ( usize i = 0u; i < indices.nCount; ++i ) {
                    const editor_asset_t *pAsset = EditorAssets_At( pCatalog, indices.pData[i] );
                    if ( pAsset == nullptr || m_hiddenRoots.contains( pAsset->iRoot ) ) { continue; }
                    m_rows.push_back( row_t{ FromView( EditorAssets_Path( pCatalog, *pAsset ) ), static_cast<int>( indices.pData[i] ), pAsset->kind } );
                }
            }
            Vector_Shutdown( &indices );
        }
        m_model.Reset();
        if ( m_view == editor_asset_window_view_t::TREE ) { RebuildTree(); }
        RestoreSelection();
        UpdateFooter();
    }

    void RebuildTree()
    {
        const QSignalBlocker blocker( m_pTree );
        m_pTree->clear();
        QHash<QString, QTreeWidgetItem *> folders;
        const QIcon folderIcon = EditorStyle_Icon( m_pGui->style, "asset-folder" );
        const auto folderItem = [&]( const QString &folder ) {
            QTreeWidgetItem *pParent = nullptr;
            QString partial;
            for ( const QString &part : folder.split( QLatin1Char( '/' ), Qt::SkipEmptyParts ) ) {
                partial = partial.isEmpty() ? part : partial + QLatin1Char( '/' ) + part;
                QTreeWidgetItem *&pItem = folders[partial];
                if ( pItem == nullptr ) {
                    pItem = pParent != nullptr ? new QTreeWidgetItem( pParent ) : new QTreeWidgetItem( m_pTree );
                    pItem->setText( 0, part );
                    pItem->setIcon( 0, folderIcon );
                    pItem->setFlags( Qt::ItemIsEnabled );
                }
                pParent = pItem;
            }
            return pParent;
        };
        for ( const row_t &row : m_rows ) {
            const QFileInfo info( row.path );
            const QString folder = info.path() == QStringLiteral( "." ) ? QString() : info.path();
            QTreeWidgetItem *pParent = folderItem( folder );
            auto *pItem = pParent != nullptr ? new QTreeWidgetItem( pParent ) : new QTreeWidgetItem( m_pTree );
            pItem->setText( 0, info.completeBaseName() );
            pItem->setIcon( 0, QIcon( Thumbnail( row, kListIcon ) ) );
            pItem->setText( 1, row.kind != editor_asset_kind_t::COUNT ? QString::fromLatin1( EditorAssets_KindLabel( row.kind ) ) : QString() );
            if ( const editor_asset_t *pAsset = AssetOf( row ) ) { pItem->setText( 2, SizeText( pAsset->cbSize ) ); }
            else { pItem->setText( 2, QStringLiteral( "missing" ) ); }
            pItem->setData( 0, Qt::UserRole, row.path );
            pItem->setToolTip( 0, row.path );
        }
        m_pTree->expandToDepth( m_rows.size() < 400u ? 2 : 0 );
    }

    void RestoreSelection()
    {
        m_bRestoring = true;
        int iRow = -1;
        for ( usize i = 0u; i < m_rows.size(); ++i ) {
            if ( m_rows[i].path == m_selected ) { iRow = static_cast<int>( i ); break; }
        }
        if ( iRow < 0 ) {
            m_pList->selectionModel()->clearSelection();
        } else {
            const QModelIndex index = m_model.index( iRow, 0 );
            m_pList->selectionModel()->setCurrentIndex( index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows );
            m_pList->scrollTo( index );
            m_pGrid->scrollTo( index );
        }
        if ( m_view == editor_asset_window_view_t::TREE ) {
            QTreeWidgetItemIterator it( m_pTree );
            for ( ; *it != nullptr; ++it ) {
                if ( ( *it )->data( 0, Qt::UserRole ).toString() == m_selected && !m_selected.isEmpty() ) {
                    m_pTree->setCurrentItem( *it );
                    m_pTree->scrollToItem( *it );
                    break;
                }
            }
        }
        m_bRestoring = false;
        UpdateFooter();
    }

    const row_t *SelectedRow() const
    {
        for ( const row_t &row : m_rows ) {
            if ( row.path == m_selected ) { return &row; }
        }
        return nullptr;
    }

    bool CanAccept( const row_t &row ) const
    {
        if ( row.iAsset < 0 ) { return false; } // Missing: nothing to hand over.
        return !m_bPicking || row.kind == m_pickKind;
    }

    void UpdateFooter()
    {
        m_pCount->setText( QStringLiteral( "%1 Asset%2 Visible" ).arg( m_rows.size() ).arg( m_rows.size() == 1u ? QString() : QStringLiteral( "s" ) ) );
        const row_t *pRow = SelectedRow();
        if ( pRow == nullptr ) {
            m_pPath->setText( m_bPicking ? QStringLiteral( "Choose a %1, then Accept" ).arg( QString::fromLatin1( EditorAssets_KindLabel( m_pickKind ) ).toLower() ) : QString() );
            m_pAccept->setEnabled( false );
            return;
        }
        const editor_asset_t *pAsset = AssetOf( *pRow );
        QString detail = pRow->path;
        if ( pAsset != nullptr ) {
            detail += QStringLiteral( "    %1 · %2" ).arg( QString::fromLatin1( EditorAssets_KindLabel( pAsset->kind ) ), SizeText( pAsset->cbSize ) );
            if ( IsUsed( pRow->path ) ) { detail += QStringLiteral( " · in the map" ); }
        } else {
            detail += QStringLiteral( "    missing from every content folder" );
        }
        m_pPath->setText( detail );
        m_pAccept->setEnabled( CanAccept( *pRow ) );
    }

    void EndPick()
    {
        if ( !m_bPicking ) { return; }
        m_bPicking = false;
        m_pfnPick = nullptr;
        m_pPickContext = nullptr;
        setWindowTitle( QStringLiteral( "Asset Browser" ) );
        UpdateFooter();
    }

    QAbstractItemView *CurrentView() const
    {
        switch ( m_view ) {
            case editor_asset_window_view_t::LIST: return m_pList;
            case editor_asset_window_view_t::GRID: return m_pGrid;
            case editor_asset_window_view_t::TREE: return m_pTree;
        }
        return m_pGrid;
    }

    void RefreshTabs()
    {
        const editor_asset_catalog_t *pCatalog = Catalog();
        usize nAll = 0u;
        for ( u32 k = 0u; k < static_cast<u32>( editor_asset_kind_t::COUNT ); ++k ) {
            const auto kind = static_cast<editor_asset_kind_t>( k );
            const u32 n = pCatalog != nullptr ? EditorAssets_KindCount( pCatalog, kind ) : 0u;
            nAll += n;
            m_pTabs->setTabText( EditorAssetWindow_KindTab( kind ), QStringLiteral( "%1  %2" ).arg( QString::fromLatin1( EditorAssets_KindName( kind ) ) ).arg( n ) );
        }
        m_pTabs->setTabText( ASSET_WINDOW_TAB_ALL, QStringLiteral( "All  %1" ).arg( nAll ) );
        int nMissing = 0;
        for ( const QString &path : m_usedList ) {
            const QByteArray utf8 = path.toUtf8();
            nMissing += pCatalog != nullptr && EditorAssets_Find( pCatalog, string_view_t{ utf8.constData(), static_cast<usize>( utf8.size() ) } ) == nullptr ? 1 : 0;
        }
        m_pTabs->setTabText( ASSET_WINDOW_TAB_USED, nMissing != 0 ? QStringLiteral( "Used in Map  %1 (%2 missing)" ).arg( m_usedList.size() ).arg( nMissing )
                                                                 : QStringLiteral( "Used in Map  %1" ).arg( m_usedList.size() ) );
    }

    void RefreshRoots()
    {
        m_pRootsMenu->clear();
        const QStringList roots = m_pBrowser != nullptr ? EditorAssetBrowser_Roots( m_pBrowser ) : QStringList();
        if ( roots.isEmpty() ) {
            m_pRootsMenu->addAction( QStringLiteral( "No content folders" ) )->setEnabled( false );
            return;
        }
        for ( int i = 0; i < roots.size(); ++i ) {
            QAction *pAction = m_pRootsMenu->addAction( QStringLiteral( "%1. %2" ).arg( i + 1 ).arg( roots[i] ) );
            pAction->setCheckable( true );
            pAction->setChecked( !m_hiddenRoots.contains( i ) );
            pAction->setToolTip( i == 0 ? QStringLiteral( "Highest priority: its files hide the same paths below" ) : QString() );
            QObject::connect( pAction, &QAction::toggled, this, [this, i]( bool bOn ) { SetRootVisible( i, bOn ); } );
        }
    }

    // The filter buttons read as checked while they narrow the list, so a
    // short list is never a mystery.
    void UpdateFilterButtons()
    {
        m_pTypes->setCheckable( true );
        m_pTypes->setChecked( m_kindMask != EDITOR_ASSET_KIND_ALL );
        m_pRoots->setCheckable( true );
        m_pRoots->setChecked( !m_hiddenRoots.isEmpty() );
    }

    void SetTileSize( int size )
    {
        const int clamped = std::clamp( size, kTileMin, kTileMax );
        if ( clamped == m_tileSize ) { return; }
        m_tileSize = clamped;
        m_tiles.clear();
        m_model.Reset();
        RestoreSelection();
        const setting_descriptor_t *pDescriptor = EditorSettings_Find( &m_pGui->settings, StringView_FromCString( kTileSizeSetting ) );
        if ( pDescriptor != nullptr ) {
            setting_value_t value{};
            value.type = setting_type_t::INTEGER;
            value.nValue = clamped;
            ( void )EditorSettings_Write( &m_pGui->settings, settings_scope_t::USER, *pDescriptor, value );
        }
    }

    // ---- Thumbnails ------------------------------------------------------

    void Enqueue( const QString &path )
    {
        if ( m_queued.contains( path ) ) { return; }
        m_queued.insert( path );
        m_queue.append( path );
        if ( !m_loader.isActive() ) { m_loader.start(); }
    }

    void LoadSome( int nLoads )
    {
        for ( int i = 0; i < nLoads && !m_queue.isEmpty(); ++i ) {
            const QString path = m_queue.takeFirst();
            m_queued.remove( path );
            m_images.insert( path, m_pBrowser != nullptr ? EditorAssetBrowser_Image( m_pBrowser, path ) : QImage() ); // Null: tried, none.
            for ( usize r = 0u; r < m_rows.size(); ++r ) {
                if ( m_rows[r].path == path ) { m_model.Changed( static_cast<int>( r ) ); break; }
            }
        }
        if ( m_queue.isEmpty() ) { m_loader.stop(); }
    }

    // ---- Saved searches --------------------------------------------------

    void LoadSavedSearches()
    {
        const string_view_t text = EditorSettings_Text( &m_pGui->settings, kSavedSearchesSetting, string_view_t{} );
        const QJsonArray entries = QJsonDocument::fromJson( QByteArray( text.pData != nullptr ? text.pData : "", static_cast<qsizetype>( text.cchLength ) ) ).array();
        for ( const QJsonValue &value : entries ) {
            const QJsonObject entry = value.toObject();
            saved_search_t search{};
            search.name = entry.value( QStringLiteral( "name" ) ).toString();
            if ( search.name.isEmpty() ) { continue; }
            search.filter = entry.value( QStringLiteral( "filter" ) ).toString();
            search.tab = std::clamp( entry.value( QStringLiteral( "tab" ) ).toInt(), 0, static_cast<int>( ASSET_WINDOW_TAB_COUNT ) - 1 );
            search.kindMask = static_cast<u32>( entry.value( QStringLiteral( "types" ) ).toInteger( EDITOR_ASSET_KIND_ALL ) ) & EDITOR_ASSET_KIND_ALL;
            search.bSources = entry.value( QStringLiteral( "sources" ) ).toBool();
            m_saved.append( search );
        }
    }

    void WriteSavedSearches()
    {
        QJsonArray entries;
        for ( const saved_search_t &search : m_saved ) {
            entries.append( QJsonObject{ { QStringLiteral( "name" ), search.name }, { QStringLiteral( "filter" ), search.filter },
                                         { QStringLiteral( "tab" ), search.tab }, { QStringLiteral( "types" ), static_cast<qint64>( search.kindMask ) },
                                         { QStringLiteral( "sources" ), search.bSources } } );
        }
        const QByteArray json = QJsonDocument( entries ).toJson( QJsonDocument::Compact );
        const setting_descriptor_t *pDescriptor = EditorSettings_Find( &m_pGui->settings, StringView_FromCString( kSavedSearchesSetting ) );
        setting_value_t value{};
        value.type = setting_type_t::STRING;
        value.text = string_view_t{ json.constData(), static_cast<usize>( json.size() ) };
        if ( pDescriptor == nullptr || EditorSettings_Write( &m_pGui->settings, settings_scope_t::USER, *pDescriptor, value ) != settings_registry_status_t::OK ) {
            // Kept for this session; the next start will not have it.
            CY_LOG_WRITE( Warning, Gui, "Saved asset searches could not be written to the user settings" );
        }
    }

    void RefreshSavedCombo( const QString &current )
    {
        const QSignalBlocker blocker( m_pSaved );
        m_pSaved->clear();
        m_pSaved->addItem( m_saved.isEmpty() ? QStringLiteral( "No saved searches" ) : QStringLiteral( "Saved Search" ) );
        for ( const saved_search_t &search : m_saved ) { m_pSaved->addItem( search.name ); }
        const int index = current.isEmpty() ? 0 : m_pSaved->findText( current, Qt::MatchFixedString );
        m_pSaved->setCurrentIndex( std::max( 0, index ) );
    }

    editor_gui_t *m_pGui;
    QPointer<QWidget> m_pBrowser;
    const bool m_bEmbedded;
    asset_model_t m_model;
    tile_delegate_t m_delegate;
    std::vector<row_t> m_rows{};
    QStringList m_usedList{};
    QSet<QString> m_used{};
    QSet<int> m_hiddenRoots{};
    QVector<saved_search_t> m_saved{};
    QVector<QAction *> m_kindActions{};
    QHash<QString, QImage> m_images{};
    QHash<QString, QPixmap> m_tiles{};
    QStringList m_queue{};
    QSet<QString> m_queued{};
    QTimer m_loader{};
    QString m_selected{};
    u32 m_kindMask{ EDITOR_ASSET_KIND_ALL };
    int m_tileSize{ 128 };
    editor_asset_window_view_t m_view{ editor_asset_window_view_t::GRID };
    bool m_bSources{ false };
    bool m_bPicking{ false };
    bool m_bRestoring{ false };
    editor_asset_kind_t m_pickKind{ editor_asset_kind_t::MATERIAL };
    editor_asset_accept_fn m_pfnAccept{ nullptr };
    void *m_pAcceptContext{ nullptr };
    editor_asset_accept_fn m_pfnPick{ nullptr };
    void *m_pPickContext{ nullptr };
    editor_asset_selection_fn m_pfnSelection{ nullptr };
    void *m_pSelectionContext{ nullptr };

    QTabBar *m_pTabs{ nullptr };
    QLineEdit *m_pFilter{ nullptr };
    QComboBox *m_pSaved{ nullptr };
    QRadioButton *m_pListRadio{ nullptr };
    QRadioButton *m_pGridRadio{ nullptr };
    QRadioButton *m_pTreeRadio{ nullptr };
    QSlider *m_pSize{ nullptr };
    QToolButton *m_pTypes{ nullptr };
    QToolButton *m_pRoots{ nullptr };
    QMenu *m_pRootsMenu{ nullptr };
    QToolButton *m_pSources{ nullptr };
    QStackedWidget *m_pStack{ nullptr };
    QTreeView *m_pList{ nullptr };
    QListView *m_pGrid{ nullptr };
    QTreeWidget *m_pTree{ nullptr };
    QLabel *m_pCount{ nullptr };
    QLabel *m_pPath{ nullptr };
    QPushButton *m_pAccept{ nullptr };
};

// ---- Model ---------------------------------------------------------------

int asset_model_t::rowCount( const QModelIndex &parent ) const
{
    return parent.isValid() ? 0 : static_cast<int>( m_pWindow->Rows().size() );
}

QVariant asset_model_t::data( const QModelIndex &index, int role ) const
{
    if ( !index.isValid() || index.row() >= rowCount() ) { return {}; }
    const row_t &row = m_pWindow->Rows()[static_cast<usize>( index.row() )];
    const editor_asset_t *pAsset = m_pWindow->AssetOf( row );
    const editor_asset_catalog_t *pCatalog = m_pWindow->Catalog();
    if ( role == Qt::DisplayRole ) {
        switch ( index.column() ) {
            case COLUMN_NAME: return QFileInfo( row.path ).completeBaseName();
            case COLUMN_TYPE:
                if ( row.kind == editor_asset_kind_t::COUNT ) { return QString(); }
                return pAsset != nullptr && pAsset->bSource ? QStringLiteral( "%1 source" ).arg( QString::fromLatin1( EditorAssets_KindLabel( row.kind ) ) )
                                                            : QString::fromLatin1( EditorAssets_KindLabel( row.kind ) );
            case COLUMN_FOLDER: return QFileInfo( row.path ).path() == QStringLiteral( "." ) ? QString() : QFileInfo( row.path ).path();
            case COLUMN_ROOT: return pAsset != nullptr ? QString::number( pAsset->iRoot + 1 ) : QStringLiteral( "missing" );
            case COLUMN_SIZE: return pAsset != nullptr ? SizeText( pAsset->cbSize ) : QString();
            case COLUMN_MODIFIED:
                return pAsset != nullptr && pAsset->modifiedMs != 0 ? QDateTime::fromMSecsSinceEpoch( pAsset->modifiedMs ).toString( QStringLiteral( "yyyy-MM-dd HH:mm" ) )
                                                                    : QString();
            default: return {};
        }
    }
    if ( role == Qt::DecorationRole && index.column() == COLUMN_NAME ) {
        return const_cast<asset_window_t *>( m_pWindow )->Thumbnail( row, kListIcon );
    }
    if ( role == Qt::ToolTipRole ) {
        QString tip = row.path;
        if ( pAsset == nullptr ) { tip += QStringLiteral( "\nMissing from every content folder" ); }
        else if ( m_pWindow->IsUsed( row.path ) ) { tip += QStringLiteral( "\nUsed in the open map" ); }
        if ( pAsset != nullptr && pCatalog != nullptr && pAsset->bSource ) { tip += QStringLiteral( "\nSource file: a recipe is built from it" ); }
        return tip;
    }
    if ( role == Qt::ForegroundRole ) {
        if ( pAsset == nullptr ) { return EditorStyle_TokenColor( m_pWindow->Style(), "ui.status.error.text" ); }
        if ( pAsset->bSource ) { return EditorStyle_TokenColor( m_pWindow->Style(), "ui.text.muted" ); }
    }
    return {};
}

QVariant asset_model_t::headerData( int section, Qt::Orientation orientation, int role ) const
{
    if ( orientation != Qt::Horizontal || role != Qt::DisplayRole ) { return {}; }
    switch ( section ) {
        case COLUMN_NAME: return QStringLiteral( "Name" );
        case COLUMN_TYPE: return QStringLiteral( "Type" );
        case COLUMN_FOLDER: return QStringLiteral( "Folder" );
        case COLUMN_ROOT: return QStringLiteral( "Root" );
        case COLUMN_SIZE: return QStringLiteral( "Size" );
        case COLUMN_MODIFIED: return QStringLiteral( "Modified" );
        default: return {};
    }
}

// ---- Grid tiles ----------------------------------------------------------

QSize tile_delegate_t::sizeHint( const QStyleOptionViewItem &, const QModelIndex & ) const
{
    const int size = m_pWindow->TileSize();
    return QSize( size + 8, size + 8 + kNameLines );
}

void tile_delegate_t::paint( QPainter *pPainter, const QStyleOptionViewItem &option, const QModelIndex &index ) const
{
    if ( !index.isValid() || index.row() >= static_cast<int>( m_pWindow->Rows().size() ) ) { return; }
    const row_t &row = m_pWindow->Rows()[static_cast<usize>( index.row() )];
    const editor_asset_t *pAsset = m_pWindow->AssetOf( row );
    const editor_style_t &style = m_pWindow->Style();
    const int size = m_pWindow->TileSize();
    const bool bSelected = ( option.state & QStyle::State_Selected ) != 0;
    const bool bHover = ( option.state & QStyle::State_MouseOver ) != 0;
    pPainter->save();
    pPainter->setRenderHint( QPainter::SmoothPixmapTransform, true );
    const QRect cell = option.rect.adjusted( 2, 2, -2, -2 );
    const QRect frame( cell.left() + ( cell.width() - size ) / 2, cell.top() + 2, size, size );
    if ( bSelected || bHover ) {
        QColor fill = EditorStyle_TokenColor( style, bSelected ? "ui.selection" : "ui.hover" );
        pPainter->fillRect( cell, fill );
    }
    pPainter->fillRect( frame, EditorStyle_TokenColor( style, "ui.deepest" ) );
    const QPixmap tile = m_pWindow->Thumbnail( row, size );
    const QSizeF tileSize = tile.deviceIndependentSize();
    const QPointF topLeft( frame.left() + ( frame.width() - tileSize.width() ) / 2.0, frame.top() + ( frame.height() - tileSize.height() ) / 2.0 );
    pPainter->drawPixmap( topLeft, tile );
    pPainter->setPen( QPen( EditorStyle_TokenColor( style, bSelected ? "ui.accent" : "ui.edge" ), bSelected ? 2.0 : 1.0 ) );
    pPainter->setBrush( Qt::NoBrush );
    pPainter->drawRect( QRectF( frame ).adjusted( 0.5, 0.5, -0.5, -0.5 ) );

    // Hammer's small status icons: kind bottom-left, state top-right.
    const qreal dpr = pPainter->device() != nullptr ? pPainter->device()->devicePixelRatioF() : 1.0;
    const QRect kindRect( frame.left() + 3, frame.bottom() - kBadge - 2, kBadge, kBadge );
    pPainter->fillRect( kindRect.adjusted( -1, -1, 1, 1 ), EditorStyle_TokenColor( style, "ui.deepest" ) );
    pPainter->drawPixmap( kindRect, EditorStyle_Icon( style, KindIcon( row.kind ) ).pixmap( QSize( kBadge, kBadge ), dpr ) );
    QRect badge( frame.right() - kBadge - 2, frame.top() + 3, kBadge, kBadge );
    const auto drawBadge = [&]( const char *pToken, const QString &glyph ) {
        pPainter->setPen( Qt::NoPen );
        pPainter->setBrush( EditorStyle_TokenColor( style, pToken ) );
        pPainter->drawEllipse( badge );
        pPainter->setPen( EditorStyle_TokenColor( style, "ui.deepest" ) );
        QFont font = pPainter->font();
        font.setBold( true );
        font.setPixelSize( kBadge - 4 );
        pPainter->setFont( font );
        pPainter->drawText( badge, Qt::AlignCenter, glyph );
        badge.translate( -( kBadge + 2 ), 0 );
    };
    if ( pAsset == nullptr ) { drawBadge( "ui.status.error.text", QStringLiteral( "!" ) ); }
    else {
        if ( m_pWindow->IsUsed( row.path ) ) { drawBadge( "viewport.hover", QStringLiteral( "✓" ) ); } // In the map.
        if ( pAsset->bSource ) { drawBadge( "ui.text.muted", QStringLiteral( "S" ) ); }
    }

    QFont nameFont = option.font;
    pPainter->setFont( nameFont );
    pPainter->setPen( pAsset == nullptr ? EditorStyle_TokenColor( style, "ui.status.error.text" ) : EditorStyle_TokenColor( style, bSelected ? "ui.text.selected" : "ui.text" ) );
    const QRect nameRect( cell.left(), frame.bottom() + 2, cell.width(), kNameLines );
    const QString name = QFileInfo( row.path ).completeBaseName();
    pPainter->drawText( nameRect, Qt::AlignHCenter | Qt::AlignVCenter, option.fontMetrics.elidedText( name, Qt::ElideMiddle, nameRect.width() - 4 ) );
    pPainter->restore();
}

asset_window_t *AsWindow( QWidget *pWindow )
{
    return dynamic_cast<asset_window_t *>( pWindow );
}

} // namespace

QDialog *EditorAssetWindow_Create( QWidget *pParent, editor_gui_t *pGui, QWidget *pBrowser )
{
    if ( pGui == nullptr ) { return nullptr; }
    return new asset_window_t( pParent, pGui, pBrowser );
}

QWidget *EditorAssetWindow_CreateEmbedded( QWidget *pParent, editor_gui_t *pGui, QWidget *pBrowser )
{
    if ( pParent == nullptr || pGui == nullptr ) { return nullptr; }
    auto *pWindow = new asset_window_t( pParent, pGui, pBrowser, true );
    pWindow->Refresh();
    return pWindow;
}

void EditorAssetWindow_SetAccept( QWidget *pWindow, editor_asset_accept_fn pfnAccept, void *pContext )
{
    if ( auto *p = AsWindow( pWindow ) ) { p->SetAccept( pfnAccept, pContext ); }
}

void EditorAssetWindow_SetSelectionSource( QWidget *pWindow, editor_asset_selection_fn pfnSelection, void *pContext )
{
    if ( auto *p = AsWindow( pWindow ) ) { p->SetSelectionSource( pfnSelection, pContext ); }
}

void EditorAssetWindow_SetUsed( QWidget *pWindow, const QStringList &paths )
{
    if ( auto *p = AsWindow( pWindow ) ) { p->SetUsed( paths ); }
}

void EditorAssetWindow_Pick( QWidget *pWindow, editor_asset_kind_t kind, const QString &current, editor_asset_accept_fn pfnPick, void *pContext )
{
    if ( auto *p = AsWindow( pWindow ) ) { p->Pick( kind, current, pfnPick, pContext ); }
}

bool EditorAssetWindow_IsPicking( QWidget *pWindow )
{
    auto *p = AsWindow( pWindow );
    return p != nullptr && p->IsPicking();
}

void EditorAssetWindow_Refresh( QWidget *pWindow )
{
    if ( auto *p = AsWindow( pWindow ) ) { p->Refresh( true ); }
}

void EditorAssetWindow_SetTab( QWidget *pWindow, int tab )
{
    if ( auto *p = AsWindow( pWindow ) ) { p->SetTab( tab ); }
}

int EditorAssetWindow_Tab( QWidget *pWindow )
{
    auto *p = AsWindow( pWindow );
    return p != nullptr ? p->Tab() : -1;
}

void EditorAssetWindow_SetFilter( QWidget *pWindow, const QString &text )
{
    if ( auto *p = AsWindow( pWindow ) ) { p->SetFilter( text ); }
}

void EditorAssetWindow_SetView( QWidget *pWindow, editor_asset_window_view_t view )
{
    if ( auto *p = AsWindow( pWindow ) ) { p->SetView( view ); }
}

editor_asset_window_view_t EditorAssetWindow_View( QWidget *pWindow )
{
    auto *p = AsWindow( pWindow );
    return p != nullptr ? p->View() : editor_asset_window_view_t::GRID;
}

void EditorAssetWindow_SetKindMask( QWidget *pWindow, u32 kindMask )
{
    if ( auto *p = AsWindow( pWindow ) ) { p->SetKindMask( kindMask ); }
}

void EditorAssetWindow_SetSources( QWidget *pWindow, bool bShow )
{
    if ( auto *p = AsWindow( pWindow ) ) { p->SetSources( bShow ); }
}

void EditorAssetWindow_SetRootVisible( QWidget *pWindow, int iRoot, bool bVisible )
{
    if ( auto *p = AsWindow( pWindow ) ) { p->SetRootVisible( iRoot, bVisible ); }
}

QStringList EditorAssetWindow_Visible( QWidget *pWindow )
{
    auto *p = AsWindow( pWindow );
    return p != nullptr ? p->Visible() : QStringList();
}

bool EditorAssetWindow_Select( QWidget *pWindow, const QString &path )
{
    auto *p = AsWindow( pWindow );
    return p != nullptr && p->Select( path );
}

QString EditorAssetWindow_Selected( QWidget *pWindow )
{
    auto *p = AsWindow( pWindow );
    return p != nullptr ? p->Selected() : QString();
}

bool EditorAssetWindow_Accept( QWidget *pWindow )
{
    auto *p = AsWindow( pWindow );
    return p != nullptr && p->Accept();
}

void EditorAssetWindow_FocusResults( QWidget *pWindow )
{
    if ( auto *p = AsWindow( pWindow ) ) { p->FocusResults(); }
}

QString EditorAssetWindow_Status( QWidget *pWindow )
{
    auto *p = AsWindow( pWindow );
    return p != nullptr ? p->Status() : QString();
}

bool EditorAssetWindow_HasThumbnail( QWidget *pWindow, const QString &path )
{
    auto *p = AsWindow( pWindow );
    return p != nullptr && p->HasThumbnail( path );
}

void EditorAssetWindow_LoadThumbnails( QWidget *pWindow )
{
    if ( auto *p = AsWindow( pWindow ) ) { p->LoadAll(); }
}

bool EditorAssetWindow_SaveSearch( QWidget *pWindow, const QString &name )
{
    auto *p = AsWindow( pWindow );
    return p != nullptr && p->SaveSearch( name );
}

bool EditorAssetWindow_LoadSearch( QWidget *pWindow, const QString &name )
{
    auto *p = AsWindow( pWindow );
    return p != nullptr && p->LoadSearch( name );
}

bool EditorAssetWindow_DeleteSearch( QWidget *pWindow, const QString &name )
{
    auto *p = AsWindow( pWindow );
    return p != nullptr && p->DeleteSearch( name );
}

QStringList EditorAssetWindow_SavedSearches( QWidget *pWindow )
{
    auto *p = AsWindow( pWindow );
    return p != nullptr ? p->SavedSearches() : QStringList();
}

} // namespace cypher::editor::gui
