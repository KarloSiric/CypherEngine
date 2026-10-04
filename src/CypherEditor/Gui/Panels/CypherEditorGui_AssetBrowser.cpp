//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_AssetBrowser.cpp
//  Purpose: Implements the asset browser panel.
//  Details: Rows are indices into the catalogue (or a missing path on the
//           Used tab), and the list model reads them directly, so filtering
//           50k assets rebuilds one vector, not 50k items. Source images are
//           decoded once, at most kImageMax pixels, and kept; tiles - the
//           square the view paints, with background, image or icon, and
//           a neutral border - are cheap to remake and are dropped when the
//           tile size or the theme changes.
//
//  History:
//  - Created by Karlo Siric on 2026-10-01
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_AssetBrowser.h"

#include "CypherEditor_FuzzyMatch.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValueParser.h"

#include <QAbstractListModel>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QSlider>
#include <QSplitter>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QTabBar>
#include <QTabWidget>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

namespace cypher::editor::gui
{

using namespace cypher::common;

namespace
{

constexpr int kImageMax = 512;       // Supports a 256-pixel tile on a 2x display.
constexpr int kLoadsPerTick = 12;    // Images decoded per idle tick while scrolling.
constexpr int kListIcon = 22;
constexpr int kTileMin = 48;
constexpr int kTileMax = 256;
constexpr int kTileDefault = 144;
constexpr const char *kTileSizeSetting = "editor.assets.thumbnail_size";
constexpr const char *kListViewSetting = "editor.assets.list_view";
constexpr int kFolderRole = Qt::UserRole + 1;
constexpr usize kMaxFiles = 250000u; // A root that big is probably the wrong folder.
constexpr const char *kMimeType = "application/x-cypher-asset-path";

string_view_t ViewOf( const QByteArray &bytes ) noexcept
{
    return string_view_t{ bytes.constData(), static_cast<usize>( bytes.size() ) };
}

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

// Source's missing-texture checker: unmistakable in a grid of real images.
QImage MissingImage()
{
    QImage image( 64, 64, QImage::Format_RGB32 );
    QPainter painter( &image );
    for ( int y = 0; y < 64; y += 16 ) {
        for ( int x = 0; x < 64; x += 16 ) { painter.fillRect( x, y, 16, 16, ( ( x + y ) / 16 ) % 2 == 0 ? QColor( 0xFF, 0x00, 0xFF ) : QColor( Qt::black ) ); }
    }
    return image;
}

struct row_t {
    i32 iAsset{ -1 };  // Catalogue index; -1 for a missing used path.
    QString path{};
};

// Preview references remain virtual content paths. A malformed recipe must
// not turn its source viewer into an arbitrary absolute-file browser.
QString PreviewFile( const QStringList &roots, const QString &path )
{
    if ( path.isEmpty() || QDir::isAbsolutePath( path ) || path.contains( QLatin1Char( '\\' ) ) ||
         path.contains( QLatin1Char( ':' ) ) || path.startsWith( QStringLiteral( "../" ) ) || QDir::cleanPath( path ) != path ) { return {}; }
    for ( const QString &root : roots ) {
        const QString file = QDir( root ).filePath( path );
        if ( QFileInfo( file ).isFile() ) { return file; }
    }
    return {};
}

QByteArray ReadRecipe( const QStringList &roots, const QString &path )
{
    QFile file( PreviewFile( roots, path ) );
    constexpr qint64 limit = 4 * 1024 * 1024;
    if ( !file.open( QIODevice::ReadOnly ) || file.size() > limit ) { return {}; }
    const QByteArray bytes = file.read( limit + 1 );
    return bytes.size() <= limit ? bytes : QByteArray();
}

QString PreviewImageSource( const QStringList &roots, const QString &path, const allocator_t *allocator, int depth = 0 )
{
    if ( depth > 3 ) { return {}; }
    bool_t isSource{};
    const auto kind = EditorAssets_KindOf( ViewOf( path.toUtf8() ), &isSource );
    if ( kind == editor_asset_kind_t::TEXTURE && isSource ) { return path; }
    text_buffer_t reference{};
    if ( !TextBuffer_Init( &reference, allocator ) ) { return {}; }
    const QByteArray recipe = ReadRecipe( roots, path );
    QString source;
    if ( kind == editor_asset_kind_t::MATERIAL && EditorAssets_MaterialTexture( ViewOf( recipe ), allocator, &reference ) == editor_asset_status_t::OK ) {
        source = PreviewImageSource( roots, FromView( TextBuffer_View( &reference ) ), allocator, depth + 1 );
    } else if ( kind == editor_asset_kind_t::TEXTURE && EditorAssets_TextureSource( ViewOf( recipe ), allocator, &reference ) == editor_asset_status_t::OK ) {
        source = FromView( TextBuffer_View( &reference ) );
    }
    TextBuffer_Shutdown( &reference );
    return source;
}

class asset_image_preview_t final : public QWidget {
public:
    explicit asset_image_preview_t( QWidget *pParent ) : QWidget( pParent )
    {
        setObjectName( QStringLiteral( "AssetPreviewImage" ) );
        setMinimumSize( 240, 200 );
        setAccessibleName( QStringLiteral( "Decoded source image preview" ) );
    }
    void SetImage( QImage image )
    {
        m_image = std::move( image );
        setProperty( "decodedSize", m_image.size() );
        update();
    }
protected:
    void paintEvent( QPaintEvent * ) override
    {
        QPainter painter( this );
        painter.fillRect( rect(), palette().brush( QPalette::Base ) );
        if ( m_image.isNull() ) { return; }
        const QSize size = m_image.size().scaled( this->size() - QSize( 16, 16 ), Qt::KeepAspectRatio );
        const QRect target( QPoint( ( width() - size.width() ) / 2, ( height() - size.height() ) / 2 ), size );
        painter.setClipRect( target );
        for ( int y = target.top(); y <= target.bottom(); y += 12 ) {
            for ( int x = target.left(); x <= target.right(); x += 12 ) {
                painter.fillRect( x, y, 12, 12, ( ( x - target.left() + y - target.top() ) / 12 ) % 2
                    ? palette().brush( QPalette::AlternateBase ) : palette().brush( QPalette::Mid ) );
            }
        }
        painter.setRenderHint( QPainter::SmoothPixmapTransform );
        painter.drawImage( target, m_image );
    }
private:
    QImage m_image{};
};

QString PreviewValue( const key_value_t *value )
{
    bool_t boolean{};
    i64 signedValue{};
    u64 unsignedValue{};
    f64 real{};
    string_view_t text{};
    switch ( KeyValue_Type( value ) ) {
        case key_value_type_t::NULL_VALUE: return QStringLiteral( "null" );
        case key_value_type_t::BOOL: ( void )KeyValue_GetBool( value, &boolean ); return boolean ? QStringLiteral( "true" ) : QStringLiteral( "false" );
        case key_value_type_t::I64: ( void )KeyValue_GetI64( value, &signedValue ); return QString::number( signedValue );
        case key_value_type_t::U64: ( void )KeyValue_GetU64( value, &unsignedValue ); return QString::number( unsignedValue );
        case key_value_type_t::F64: ( void )KeyValue_GetF64( value, &real ); return QString::number( real, 'g', 15 );
        case key_value_type_t::STRING: ( void )KeyValue_GetString( value, &text ); return FromView( text );
        case key_value_type_t::BINARY: return QStringLiteral( "Binary data" );
        case key_value_type_t::OBJECT: return QStringLiteral( "%1 properties" ).arg( KeyValue_ChildCount( value ) );
        case key_value_type_t::ARRAY: return QStringLiteral( "%1 items" ).arg( KeyValue_ChildCount( value ) );
    }
    return {};
}

// A bounded read-only inspector, deliberately independent of map loading and
// renderer ownership. The displayed data are snapshots of files on disk.
class asset_preview_t final : public QDialog {
public:
    asset_preview_t( QWidget *parent, editor_gui_t *gui, QStringList roots, QString path, editor_asset_kind_t kind )
        : QDialog( parent ), m_pGui( gui ), m_roots( std::move( roots ) ), m_path( std::move( path ) ),
          m_kind( kind )
    {
        setObjectName( QStringLiteral( "AssetPreviewDialog" ) );
        setAttribute( Qt::WA_DeleteOnClose );
        setWindowTitle( QStringLiteral( "%1 — Asset Inspector" ).arg( QFileInfo( m_path ).fileName() ) );
        resize( 920, 650 );
        auto *layout = new QVBoxLayout( this );
        auto *title = new QLabel( m_path, this );
        title->setObjectName( QStringLiteral( "AssetPreviewPath" ) );
        title->setTextFormat( Qt::PlainText );
        title->setWordWrap( true );
        title->setTextInteractionFlags( Qt::TextSelectableByMouse );
        layout->addWidget( title );
        auto *note = new QLabel( kind == editor_asset_kind_t::MAP
            ? QStringLiteral( "Map document inspection · on-disk definition and chunks. This does not open the map or render its geometry." )
            : kind == editor_asset_kind_t::SHADER
                ? QStringLiteral( "Shader source inspection · recipe and referenced stages. This does not compile or render the shader." )
                : QStringLiteral( "Read-only asset inspection · source images and authored properties. Material images are not a shaded material rendering." ), this );
        note->setObjectName( QStringLiteral( "AssetPreviewExplanation" ) );
        note->setWordWrap( true );
        note->setProperty( "muted", true );
        layout->addWidget( note );
        m_pTabs = new QTabWidget( this );
        m_pTabs->setObjectName( QStringLiteral( "AssetPreviewTabs" ) );
        layout->addWidget( m_pTabs, 1 );

        if ( kind == editor_asset_kind_t::MATERIAL || kind == editor_asset_kind_t::TEXTURE ) {
            auto *imagePage = new QWidget( m_pTabs );
            auto *imageLayout = new QVBoxLayout( imagePage );
            m_pImage = new asset_image_preview_t( imagePage );
            imageLayout->addWidget( m_pImage, 1 );
            m_pImageInfo = new QLabel( imagePage );
            m_pImageInfo->setObjectName( QStringLiteral( "AssetPreviewImageInfo" ) );
            m_pImageInfo->setTextFormat( Qt::PlainText );
            m_pImageInfo->setWordWrap( true );
            m_pImageInfo->setTextInteractionFlags( Qt::TextSelectableByMouse );
            imageLayout->addWidget( m_pImageInfo );
            m_pTabs->addTab( imagePage, QStringLiteral( "Image" ) );
        }
        m_pMetadata = MakeTree( "AssetPreviewMetadata", { QStringLiteral( "Property" ), QStringLiteral( "Value" ) } );
        m_pTabs->addTab( m_pMetadata, QStringLiteral( "Metadata" ) );
        m_pStructure = MakeTree( "AssetPreviewStructure", { QStringLiteral( "Field" ), QStringLiteral( "Value" ) } );
        m_pTabs->addTab( m_pStructure, QStringLiteral( "Structure" ) );
        m_pDependencies = MakeTree( "AssetPreviewDependencies", { QStringLiteral( "Referenced asset" ), QStringLiteral( "Resolution" ) } );
        m_pTabs->addTab( m_pDependencies, QStringLiteral( "Dependencies" ) );

        auto *sourcePage = new QWidget( m_pTabs );
        auto *sourceLayout = new QVBoxLayout( sourcePage );
        auto *searchRow = new QHBoxLayout();
        m_pFind = new QLineEdit( sourcePage );
        m_pFind->setObjectName( QStringLiteral( "AssetPreviewFind" ) );
        m_pFind->setPlaceholderText( QStringLiteral( "Find in source…" ) );
        m_pFind->setAccessibleName( QStringLiteral( "Find in source" ) );
        m_pFind->setClearButtonEnabled( true );
        auto *next = new QPushButton( QStringLiteral( "Find Next" ), sourcePage );
        next->setObjectName( QStringLiteral( "AssetPreviewFindNext" ) );
        searchRow->addWidget( m_pFind, 1 );
        searchRow->addWidget( next );
        sourceLayout->addLayout( searchRow );
        m_pSource = new QPlainTextEdit( sourcePage );
        m_pSource->setObjectName( QStringLiteral( "AssetPreviewSource" ) );
        m_pSource->setAccessibleName( QStringLiteral( "Read-only asset source" ) );
        m_pSource->setReadOnly( true );
        m_pSource->setLineWrapMode( QPlainTextEdit::NoWrap );
        m_pSource->setFont( QFontDatabase::systemFont( QFontDatabase::FixedFont ) );
        sourceLayout->addWidget( m_pSource, 1 );
        m_pTabs->addTab( sourcePage, QStringLiteral( "Source" ) );
        QObject::connect( m_pFind, &QLineEdit::returnPressed, this, [this]() { FindNext(); } );
        QObject::connect( next, &QPushButton::clicked, this, [this]() { FindNext(); } );
        QObject::connect( m_pFind, &QLineEdit::textChanged, this, [this]() {
            m_pSource->moveCursor( QTextCursor::Start );
            if ( !m_pFind->text().isEmpty() ) { FindNext(); }
        } );

        // One selector applies to source, metadata and structure together.
        auto *fileRow = new QHBoxLayout();
        auto *fileLabel = new QLabel( QStringLiteral( "Inspect file" ), this );
        m_pFiles = new QComboBox( this );
        m_pFiles->setObjectName( QStringLiteral( "AssetPreviewFiles" ) );
        m_pFiles->setAccessibleName( QStringLiteral( "File to inspect" ) );
        m_pFiles->setSizeAdjustPolicy( QComboBox::AdjustToMinimumContentsLengthWithIcon );
        m_pFiles->setMinimumContentsLength( 24 );
        fileLabel->setBuddy( m_pFiles );
        fileRow->addWidget( fileLabel );
        fileRow->addWidget( m_pFiles, 1 );
        layout->insertLayout( 2, fileRow );
        m_pStatus = new QLabel( this );
        m_pStatus->setObjectName( QStringLiteral( "AssetPreviewStatus" ) );
        m_pStatus->setWordWrap( true );
        layout->addWidget( m_pStatus );
        auto *buttons = new QDialogButtonBox( QDialogButtonBox::Close, this );
        auto *reload = buttons->addButton( QStringLiteral( "Reload from Disk" ), QDialogButtonBox::ActionRole );
        reload->setObjectName( QStringLiteral( "AssetPreviewReload" ) );
        layout->addWidget( buttons );
        QObject::connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::close );
        QObject::connect( reload, &QPushButton::clicked, this, [this]() { Load(); LoadImage(); } );
        m_pFiles->addItem( m_path, m_path );
        AddMapChunks();
        QObject::connect( m_pFiles, &QComboBox::currentIndexChanged, this, [this]( int ) { Load(); } );
        Load();
        LoadImage();
        if ( kind == editor_asset_kind_t::SHADER ) { m_pTabs->setCurrentWidget( sourcePage ); }
        else if ( kind == editor_asset_kind_t::MAP ) { m_pTabs->setCurrentWidget( m_pStructure ); }
    }
    QString Path() const { return m_path; }

private:
    static constexpr qint64 kSourceLimit = 1024 * 1024;
    static constexpr int kTreeLimit = 2000;
    QTreeWidget *MakeTree( const char *name, const QStringList &columns )
    {
        auto *tree = new QTreeWidget( m_pTabs );
        tree->setObjectName( QString::fromLatin1( name ) );
        tree->setColumnCount( 2 );
        tree->setHeaderLabels( columns );
        tree->setAlternatingRowColors( true );
        tree->setUniformRowHeights( true );
        tree->header()->setSectionResizeMode( 0, QHeaderView::Interactive );
        tree->setColumnWidth( 0, 250 );
        return tree;
    }
    void Meta( const QString &key, const QString &value )
    {
        auto *row = new QTreeWidgetItem( m_pMetadata, { key, value } );
        row->setToolTip( 1, value );
    }
    void AddMapChunks()
    {
        if ( m_kind != editor_asset_kind_t::MAP || !m_path.endsWith( QStringLiteral( ".cymap" ), Qt::CaseInsensitive ) ) { return; }
        const QString resolvedRoot = PreviewFile( m_roots, m_path );
        if ( resolvedRoot.isEmpty() ) { return; }
        const QFileInfo root( resolvedRoot );
        const QDir chunks( root.dir().filePath( root.completeBaseName() ) );
        QStringList paths;
        QDirIterator it( chunks.path(), { QStringLiteral( "*.cymapchunk" ) }, QDir::Files, QDirIterator::Subdirectories );
        int scanned = 0;
        while ( it.hasNext() && paths.size() < 128 && scanned++ < 4096 ) {
            const QString file = it.next();
            const QString relative = chunks.relativeFilePath( file );
            if ( relative.count( QLatin1Char( '/' ) ) != 1 ) { continue; } // layer/cell, as the map format defines.
            const QString path = QDir::cleanPath( QFileInfo( m_path ).path() + QLatin1Char( '/' ) + root.completeBaseName() + QLatin1Char( '/' ) + relative );
            paths.append( path );
            // CYMAP chunks belong to this root file's sibling directory;
            // unrelated higher-priority asset roots must not shadow them.
            m_chunkFiles.insert( path, file );
        }
        paths.sort();
        for ( const QString &path : paths ) { m_pFiles->addItem( path, QDir::cleanPath( path ) ); }
        m_chunkCount = paths.size();
    }
    void AddStructure( const key_value_t *value, QTreeWidgetItem *parent, int &count )
    {
        for ( usize i = 0; i < KeyValue_ChildCount( value ) && count < kTreeLimit; ++i ) {
            const auto *child = KeyValue_ChildAt( value, i );
            const QString name = KeyValue_Type( value ) == key_value_type_t::ARRAY ? QStringLiteral( "[%1]" ).arg( i ) : FromView( KeyValue_Name( child ) );
            auto *row = new QTreeWidgetItem( parent, { name, PreviewValue( child ) } );
            row->setToolTip( 1, row->text( 1 ) );
            ++count;
            AddStructure( child, row, count );
        }
    }
    void AddReference( string_view_t reference, editor_asset_kind_t kind )
    {
        const QString path = FromView( reference );
        if ( m_references.contains( path ) ) { return; }
        m_references.insert( path );
        const QString file = m_chunkFiles.contains( path ) ? m_chunkFiles.value( path ) : PreviewFile( m_roots, path );
        auto *row = new QTreeWidgetItem( m_pDependencies, { path, file.isEmpty() ? QStringLiteral( "Missing" ) : QStringLiteral( "Found" ) } );
        row->setToolTip( 0, path );
        row->setToolTip( 1, file.isEmpty() ? QStringLiteral( "Not resolved in the current content roots" ) : QDir::toNativeSeparators( file ) );
        if ( file.isEmpty() ) { row->setForeground( 1, EditorStyle_TokenColor( m_pGui->style, "ui.status.error.text" ) ); }
        bool_t source{};
        ( void )EditorAssets_KindOf( reference, &source );
        if ( ( !source || kind == editor_asset_kind_t::SHADER ) && m_pFiles->findData( path ) < 0 && m_pFiles->count() < 256 ) {
            m_pFiles->addItem( path, path );
        }
    }
    void Load()
    {
        m_pMetadata->clear();
        m_pStructure->clear();
        m_pDependencies->clear();
        m_references.clear();
        m_pSource->clear();
        const QString path = m_pFiles->currentData().toString();
        const QString file = m_chunkFiles.contains( path ) ? m_chunkFiles.value( path ) : PreviewFile( m_roots, path );
        Meta( QStringLiteral( "Virtual path" ), path );
        Meta( QStringLiteral( "Resolved file" ), file.isEmpty() ? QStringLiteral( "Missing" ) : QDir::toNativeSeparators( file ) );
        if ( file.isEmpty() ) { m_pStatus->setText( QStringLiteral( "File is missing from the current content roots." ) ); return; }
        const QFileInfo info( file );
        Meta( QStringLiteral( "File size" ), SizeText( static_cast<u64>( info.size() ) ) );
        Meta( QStringLiteral( "Modified" ), info.lastModified().toString( Qt::ISODate ) );
        if ( m_kind == editor_asset_kind_t::MAP ) {
            Meta( QStringLiteral( "Available chunk files" ), QStringLiteral( "%1%2" ).arg( m_chunkCount ).arg( m_chunkCount == 128 ? QStringLiteral( " (listing capped at 128)" ) : QString() ) );
        }
        bool_t isSource{};
        const auto kind = EditorAssets_KindOf( ViewOf( path.toUtf8() ), &isSource );
        if ( kind != editor_asset_kind_t::COUNT ) { Meta( QStringLiteral( "Asset type" ), QString::fromLatin1( EditorAssets_KindLabel( kind ) ) ); }
        if ( isSource && kind != editor_asset_kind_t::SHADER ) {
            m_pStatus->setText( QStringLiteral( "Binary source asset · inspect its image and file metadata; no text source is displayed." ) );
            return;
        }
        QFile input( file );
        if ( !input.open( QIODevice::ReadOnly ) ) { m_pStatus->setText( QStringLiteral( "Could not read the file: %1" ).arg( input.errorString() ) ); return; }
        const QByteArray bytes = input.read( kSourceLimit + 1 );
        const bool truncated = bytes.size() > kSourceLimit;
        m_pSource->setPlainText( QString::fromUtf8( bytes.first( std::min<qsizetype>( bytes.size(), kSourceLimit ) ) ) );
        if ( truncated ) { m_pStatus->setText( QStringLiteral( "Source display limited to the first 1 MiB. Structure and dependency parsing skipped." ) ); return; }
        if ( isSource ) { m_pStatus->setText( QStringLiteral( "Shader source · %1 lines · read only" ).arg( m_pSource->blockCount() ) ); return; }
        key_value_document_desc_t desc{};
        desc.pAllocator = m_pGui->pAllocator;
        std::unique_ptr<key_value_document_t, decltype( &KeyValue_DestroyDocument )> document( KeyValue_CreateDocument( desc ), &KeyValue_DestroyDocument );
        if ( !document ) { m_pStatus->setText( QStringLiteral( "Could not allocate the document inspector." ) ); return; }
        key_value_parse_options_t options{};
        options.cbMaxInput = kSourceLimit;
        options.nMaxDepth = 64;
        options.nMaxNodes = 20000;
        options.nMaxContainerValues = 20000;
        options.cbMaxStringData = kSourceLimit;
        const auto result = KeyValue_ParseText( ViewOf( bytes ), options, document.get() );
        if ( result.status != key_value_parse_status_t::OK ) {
            m_pStatus->setText( QStringLiteral( "Source is available; CYKV structure could not be parsed: %1. No schema validation was performed." )
                .arg( QString::fromLatin1( KeyValue_ParseStatusName( result.status ) ) ) );
            return;
        }
        const auto header = KeyValue_DocumentHeader( document.get() );
        Meta( QStringLiteral( "Schema" ), QStringLiteral( "%1 · version %2" ).arg( FromView( header.schemaId ) ).arg( header.nSchemaVersion ) );
        const auto *root = KeyValue_Root( document.get() );
        for ( const char *key : { "name", "description", "language", "shader", "source", "layers", "entities", "brushes", "meshes", "patches", "terrain" } ) {
            if ( const auto *value = KeyValue_Find( root, StringView_FromCString( key ) ) ) { Meta( QString::fromLatin1( key ), PreviewValue( value ) ); }
        }
        int count = 0;
        AddStructure( root, m_pStructure->invisibleRootItem(), count );
        m_pStructure->expandToDepth( 0 );
        EditorAssets_VisitReferences( root, []( void *context, string_view_t reference, editor_asset_kind_t kind ) {
            static_cast<asset_preview_t *>( context )->AddReference( reference, kind );
        }, this );
        m_pStatus->setText( QStringLiteral( "CYKV structure · %1 nodes shown%2 · read only; not a map or asset validation result." )
            .arg( count ).arg( count == kTreeLimit ? QStringLiteral( " (display capped at 2,000)" ) : QString() ) );
    }
    void LoadImage()
    {
        if ( m_pImage == nullptr ) { return; }
        m_imagePath = PreviewImageSource( m_roots, m_path, m_pGui->pAllocator );
        const QString file = PreviewFile( m_roots, m_imagePath );
        QImageReader reader( file );
        reader.setAutoTransform( true );
        const QSize original = reader.size();
        QSize decoded = original;
        if ( decoded.width() > 2048 || decoded.height() > 2048 ) { decoded.scale( 2048, 2048, Qt::KeepAspectRatio ); reader.setScaledSize( decoded ); }
        // Respect Qt's decoder allocation limit as well as the display bound.
        QImage image = file.isEmpty() || !original.isValid() ? QImage() : reader.read();
        if ( image.width() > 2048 || image.height() > 2048 ) { image = image.scaled( 2048, 2048, Qt::KeepAspectRatio, Qt::SmoothTransformation ); }
        m_pImage->SetImage( image );
        m_pImageInfo->setText( image.isNull()
            ? QStringLiteral( "No decoded image preview. The material may have no source image, or the source is missing or unsupported. Inspect Metadata and Dependencies." )
            : QStringLiteral( "%1\n%2 × %3 px · %4%5" ).arg( m_imagePath ).arg( original.width() ).arg( original.height() )
                .arg( QString::fromLatin1( reader.format() ).toUpper() ).arg( original != image.size() ? QStringLiteral( " · preview scaled to at most 2048 px" ) : QString() ) );
    }
    void FindNext()
    {
        if ( m_pFind->text().isEmpty() ) { return; }
        bool found = m_pSource->find( m_pFind->text() );
        if ( !found ) { m_pSource->moveCursor( QTextCursor::Start ); found = m_pSource->find( m_pFind->text() ); }
        m_pFind->setToolTip( found ? QStringLiteral( "Match found; Enter finds the next occurrence" ) : QStringLiteral( "No matching text in this file" ) );
    }
    editor_gui_t *m_pGui;
    QStringList m_roots;
    QString m_path;
    editor_asset_kind_t m_kind;
    QString m_imagePath;
    QSet<QString> m_references{};
    QHash<QString, QString> m_chunkFiles{};
    qsizetype m_chunkCount{};
    QTabWidget *m_pTabs{};
    QTreeWidget *m_pMetadata{}, *m_pStructure{}, *m_pDependencies{};
    QPlainTextEdit *m_pSource{};
    QLineEdit *m_pFind{};
    QComboBox *m_pFiles{};
    QLabel *m_pStatus{}, *m_pImageInfo{};
    asset_image_preview_t *m_pImage{};
};

class asset_browser_t;

class asset_model_t final : public QAbstractListModel {
public:
    explicit asset_model_t( asset_browser_t *pBrowser ) : QAbstractListModel( nullptr ), m_pBrowser( pBrowser ) {}
    int rowCount( const QModelIndex &parent ) const override;
    QVariant data( const QModelIndex &index, int role ) const override;
    Qt::ItemFlags flags( const QModelIndex &index ) const override;
    QStringList mimeTypes() const override { return { QString::fromLatin1( kMimeType ), QStringLiteral( "text/plain" ) }; }
    QMimeData *mimeData( const QModelIndexList &indexes ) const override;

    void Reset( const std::function<void()> &change )
    {
        beginResetModel();
        change();
        endResetModel();
    }
    void Changed( int row ) { emit dataChanged( index( row ), index( row ), { Qt::DecorationRole } ); }
    void ChangedAll()
    {
        if ( rowCount( {} ) != 0 ) { emit dataChanged( index( 0 ), index( rowCount( {} ) - 1 ) ); }
    }

private:
    asset_browser_t *m_pBrowser;
};

// Folder connections stay visible under every theme without relying on
// platform-specific branch images. The shared tree preference also applies here.
class asset_folder_tree_t final : public QTreeWidget {
public:
    asset_folder_tree_t( QWidget *pParent, editor_gui_t *pGui ) : QTreeWidget( pParent ), m_pGui( pGui ) {}
protected:
    void drawBranches( QPainter *pPainter, const QRect &rect, const QModelIndex &index ) const override
    {
        if ( rect.isEmpty() || !index.isValid() ) { return; }
        const bool rtl = layoutDirection() == Qt::RightToLeft;
        const int step = rtl ? indentation() : -indentation();
        const int x = rtl ? rect.left() + indentation() / 2 : rect.right() - indentation() / 2;
        const int middle = rect.center().y();
        const bool children = model()->hasChildren( index );
        pPainter->save();
        pPainter->setClipRect( rect, Qt::IntersectClip );
        if ( EditorSettings_Bool( &m_pGui->settings, "editor.ui.tree_lines", CY_TRUE ) ) {
            QPen pen( EditorStyle_TokenColor( m_pGui->style, "ui.text.disabled" ) );
            pen.setCosmetic( true );
            pPainter->setPen( pen );
            if ( index.parent().isValid() ) {
                const int gap = children ? 4 : 0;
                pPainter->drawLine( x, rect.top(), x, middle - gap );
                if ( index.row() + 1 < model()->rowCount( index.parent() ) ) { pPainter->drawLine( x, middle + gap, x, rect.bottom() ); }
                pPainter->drawLine( x + ( rtl ? -gap : gap ), middle, rtl ? rect.left() : rect.right(), middle );
            }
            int ancestorX = x + step;
            for ( QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent(), ancestorX += step ) {
                if ( parent.parent().isValid() && parent.row() + 1 < model()->rowCount( parent.parent() ) ) {
                    pPainter->drawLine( ancestorX, rect.top(), ancestorX, rect.bottom() );
                }
            }
        }
        if ( children ) {
            QStyleOptionViewItem option;
            option.initFrom( this );
            option.rect = QRect( x - indentation() / 2, rect.top(), indentation(), rect.height() );
            option.state |= QStyle::State_Children;
            if ( isExpanded( index ) ) { option.state |= QStyle::State_Open; }
            style()->drawPrimitive( QStyle::PE_IndicatorBranch, &option, pPainter, this );
        }
        pPainter->restore();
    }
private:
    editor_gui_t *m_pGui;
};

// Selection outlines the image and its name as one item. Unselected previews
// carry no category-colored strips or badges that compete with the artwork.
class asset_item_delegate_t final : public QStyledItemDelegate {
public:
    asset_item_delegate_t( QObject *pParent, editor_gui_t *pGui ) : QStyledItemDelegate( pParent ), m_pGui( pGui ) {}
    void paint( QPainter *pPainter, const QStyleOptionViewItem &option, const QModelIndex &index ) const override
    {
        QStyleOptionViewItem clean = option;
        clean.state &= ~QStyle::State_HasFocus;
        QStyledItemDelegate::paint( pPainter, clean, index );
        if ( option.state.testFlag( QStyle::State_Selected ) ) {
            pPainter->save();
            QPen pen( EditorStyle_TokenColor( m_pGui->style, "ui.accent" ) );
            pen.setCosmetic( true );
            pPainter->setPen( pen );
            pPainter->setBrush( Qt::NoBrush );
            pPainter->drawRect( option.rect.adjusted( 1, 1, -2, -2 ) );
            pPainter->restore();
        }
    }
private:
    editor_gui_t *m_pGui;
};

class asset_browser_t final : public QWidget {
public:
    asset_browser_t( QWidget *pParent, editor_gui_t *pGui ) : QWidget( pParent ), m_pGui( pGui )
    {
        CY_ASSERT( pGui != nullptr && pGui->bInitialized );
        setObjectName( QStringLiteral( "EditorAssetBrowser" ) );
        m_tileSize = static_cast<int>( std::clamp<i64>( EditorSettings_Integer( &pGui->settings, kTileSizeSetting, kTileDefault ), kTileMin, kTileMax ) );
        m_bList = EditorSettings_Bool( &pGui->settings, kListViewSetting, CY_FALSE );
        const editor_asset_status_t status = EditorAssets_Init( &m_catalog, pGui->pAllocator );
        CY_ASSERT( status == editor_asset_status_t::OK );
        ( void )status;
        const bool bIndices = Vector_Init( &m_indices, pGui->pAllocator );
        CY_ASSERT( bIndices );
        ( void )bIndices;

        auto *pRoot = new QVBoxLayout( this );
        pRoot->setContentsMargins( 0, 0, 0, 0 );
        pRoot->setSpacing( 0 );
        pRoot->addWidget( BuildTabs() );
        pRoot->addWidget( BuildToolbar() );

        auto *pSplitter = new QSplitter( Qt::Horizontal, this );
        pSplitter->setObjectName( QStringLiteral( "AssetBrowserSplitter" ) );
        m_pTree = new asset_folder_tree_t( pSplitter, pGui );
        m_pTree->setObjectName( QStringLiteral( "AssetBrowserFolders" ) );
        m_pTree->setHeaderHidden( true );
        m_pTree->setUniformRowHeights( true );
        m_pTree->setColumnCount( 2 );
        m_pTree->setIndentation( 14 );
        m_pTree->setIconSize( QSize( 16, 16 ) );
        m_pTree->setRootIsDecorated( true );
        m_pTree->header()->setStretchLastSection( false );
        m_pTree->header()->setMinimumSectionSize( 20 );
        m_pTree->header()->setSectionResizeMode( 0, QHeaderView::Stretch );
        m_pTree->header()->setSectionResizeMode( 1, QHeaderView::ResizeToContents );
        m_pTree->setTextElideMode( Qt::ElideMiddle );
        m_pModel = new asset_model_t( this );
        m_pModel->setParent( this );
        m_pView = new QListView( pSplitter );
        m_pView->setObjectName( QStringLiteral( "AssetBrowserView" ) );
        m_pView->setModel( m_pModel );
        m_pView->setItemDelegate( new asset_item_delegate_t( m_pView, pGui ) );
        m_pView->setSelectionMode( QAbstractItemView::ExtendedSelection );
        m_pView->setDragEnabled( true );
        m_pView->setDragDropMode( QAbstractItemView::DragOnly );
        m_pView->setContextMenuPolicy( Qt::CustomContextMenu );
        m_pView->setUniformItemSizes( true );
        pSplitter->addWidget( m_pTree );
        pSplitter->addWidget( m_pView );
        pSplitter->setStretchFactor( 1, 1 );
        pSplitter->setSizes( { 210, 720 } );
        pRoot->addWidget( pSplitter, 1 );

        m_pStatus = new QLabel( this );
        m_pStatus->setObjectName( QStringLiteral( "AssetBrowserStatus" ) );
        m_pStatus->setTextInteractionFlags( Qt::TextSelectableByMouse );
        m_pStatus->setProperty( "muted", true );
        pRoot->addWidget( m_pStatus );

        m_pLoader = new QTimer( this );
        m_pLoader->setInterval( 0 );
        QObject::connect( m_pLoader, &QTimer::timeout, this, [this]() { LoadSome( kLoadsPerTick ); } );
        QObject::connect( m_pTree, &QTreeWidget::currentItemChanged, this, [this]( QTreeWidgetItem *pItem, QTreeWidgetItem * ) {
            if ( m_bFilling ) { return; }
            m_folder = pItem != nullptr ? pItem->data( 0, kFolderRole ).toString() : QString();
            Refilter();
        } );
        QObject::connect( m_pView, &QListView::activated, this, [this]( const QModelIndex &index ) { ActivateRow( index.row() ); } );
        QObject::connect( m_pView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() { UpdateStatus(); } );
        QObject::connect( m_pView, &QListView::customContextMenuRequested, this, [this]( const QPoint &at ) { ContextMenu( at ); } );
        ( void )EditorGui_AddStyleListener( pGui, &asset_browser_t::OnStyleChanged, this );
        ( void )EditorSettings_AddListener( &pGui->settings, &asset_browser_t::OnSettingsChanged, this );
        ApplyViewMode();
        FillTabs();
        Refilter();
    }

    ~asset_browser_t() override
    {
        EditorGui_RemoveStyleListener( m_pGui, &asset_browser_t::OnStyleChanged, this );
        EditorSettings_RemoveListener( &m_pGui->settings, &asset_browser_t::OnSettingsChanged, this );
        Vector_Shutdown( &m_indices );
        EditorAssets_Shutdown( &m_catalog );
    }

    // ---- API -------------------------------------------------------------

    void SetRoots( const QStringList &roots )
    {
        // The inspector owns a snapshot of mount resolution. A different
        // project (or mount priority) must not reuse the previous project's
        // image/source under an identical virtual path.
        if ( m_roots != roots ) { delete m_pPreview.data(); }
        m_roots = roots;
        Rescan();
    }

    QStringList Roots() const { return m_roots; }
    void SetRescanCallback( editor_asset_rescan_fn callback, void *context ) { m_pfnRescan = callback; m_pRescanContext = context; }

    void Rescan()
    {
        QElapsedTimer timer;
        timer.start();
        EditorAssets_Clear( &m_catalog );
        m_images.clear();
        m_tiles.clear();
        m_queue.clear();
        m_queued.clear();
        usize nFiles = 0u;
        for ( qsizetype iRoot = 0; iRoot < m_roots.size() && iRoot < 0xFFFF; ++iRoot ) {
            const QDir root( m_roots[iRoot] );
            if ( !root.exists() ) {
                Log( log_level_t::Warning, QStringLiteral( "Asset folder not found: %1" ).arg( QDir::toNativeSeparators( m_roots[iRoot] ) ) );
                continue;
            }
            QDirIterator it( root.absolutePath(), QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories );
            while ( it.hasNext() && nFiles < kMaxFiles ) {
                it.next();
                ++nFiles;
                const QFileInfo info = it.fileInfo();
                const QByteArray path = root.relativeFilePath( info.absoluteFilePath() ).toUtf8();
                ( void )EditorAssets_Add( &m_catalog, static_cast<u16>( iRoot ), ViewOf( path ), static_cast<u64>( info.size() ),
                                          info.lastModified().toMSecsSinceEpoch() );
            }
        }
        if ( nFiles >= kMaxFiles ) { Log( log_level_t::Warning, QStringLiteral( "Asset scan stopped at %1 files; check the content folders" ).arg( kMaxFiles ) ); }
        EditorAssets_Finish( &m_catalog );
        Log( log_level_t::Info, QStringLiteral( "Asset browser: %1 assets in %2 folder(s), %3 ms" )
                                    .arg( EditorAssets_Count( &m_catalog ) )
                                    .arg( m_roots.size() )
                                    .arg( timer.elapsed() ) );
        FillTabs();
        FillTree();
        Refilter();
        if ( m_pfnRescan != nullptr ) { m_pfnRescan( m_pRescanContext ); }
    }

    void SetUsed( const QString &title, const QStringList &paths )
    {
        m_usedTitle = title.isEmpty() ? QStringLiteral( "Used" ) : title;
        m_used = paths;
        m_used.removeDuplicates();
        std::sort( m_used.begin(), m_used.end() );
        FillTabs();
        if ( m_tab == ASSET_TAB_USED ) { Refilter(); }
    }

    void SetActivate( editor_asset_activate_fn pfnActivate, void *pContext )
    {
        m_pfnActivate = pfnActivate;
        m_pActivateContext = pContext;
    }

    void SetTab( editor_asset_tab_t tab )
    {
        m_pTabs->setCurrentIndex( std::clamp( static_cast<int>( tab ), 0, ASSET_TAB_COUNT - 1 ) ); // Signals Refilter.
    }

    void SetSearch( const QString &text ) { m_pSearch->setText( text ); }

    void Search( const QString &text, bool focusFirstResult )
    {
        SetTab( ASSET_TAB_ALL );
        SetFolder( QString() );
        SetSearch( text );
        if ( focusFirstResult ) {
            const QModelIndex first = m_pModel->index( 0, 0 );
            m_pView->setCurrentIndex( first );
            if ( first.isValid() ) { m_pView->scrollTo( first ); }
            m_pView->setFocus( Qt::ShortcutFocusReason );
        }
    }

    void SetFolder( const QString &folder )
    {
        for ( QTreeWidgetItemIterator it( m_pTree ); *it != nullptr; ++it ) {
            if ( ( *it )->data( 0, kFolderRole ).toString() == folder ) {
                m_pTree->setCurrentItem( *it ); // Signals Refilter.
                return;
            }
        }
    }

    void SetSources( bool bShow ) { m_pSources->setChecked( bShow ); }

    QStringList Visible() const
    {
        QStringList paths;
        for ( const row_t &row : m_rows ) { paths.append( row.path ); }
        return paths;
    }

    QStringList Missing() const
    {
        QStringList paths;
        for ( const QString &path : m_used ) {
            const QByteArray utf8 = path.toUtf8();
            if ( EditorAssets_Find( &m_catalog, ViewOf( utf8 ) ) == nullptr ) { paths.append( path ); }
        }
        return paths;
    }

    const editor_asset_catalog_t *Catalog() const { return &m_catalog; }

    void Activate( const QString &path )
    {
        for ( usize i = 0u; i < m_rows.size(); ++i ) {
            if ( m_rows[i].path == path ) {
                ActivateRow( static_cast<int>( i ) );
                return;
            }
        }
        const QByteArray utf8 = path.toUtf8();
        if ( const editor_asset_t *pAsset = EditorAssets_Find( &m_catalog, ViewOf( utf8 ) ) ) { ActivateAsset( path, pAsset->kind ); }
    }

    QWidget *Preview( const QString &path )
    {
        const QByteArray utf8 = path.toUtf8();
        const editor_asset_t *asset = EditorAssets_Find( &m_catalog, ViewOf( utf8 ) );
        if ( asset == nullptr ) { return nullptr; }
        if ( m_pPreview != nullptr && m_pPreview->Path() != path ) { delete m_pPreview.data(); }
        if ( m_pPreview == nullptr ) { m_pPreview = new asset_preview_t( this, m_pGui, m_roots, path, asset->kind ); }
        m_pPreview->show();
        m_pPreview->raise();
        m_pPreview->activateWindow();
        return m_pPreview;
    }

    void LoadAll()
    {
        // Ask for every visible row's tile so the queue holds them all.
        for ( const row_t &row : m_rows ) { ( void )Tile( row ); }
        while ( !m_queue.isEmpty() ) { LoadSome( kLoadsPerTick ); }
    }

    bool HasImage( const QString &path ) const { return !m_images.value( path ).isNull(); }

    QImage Image( const QString &path )
    {
        if ( !m_images.contains( path ) ) { m_images.insert( path, LoadImageFor( path ) ); }
        return m_images.value( path );
    }

    QString Status() const { return m_pStatus->text(); }

    // ---- Model callbacks -------------------------------------------------

    int RowCount() const { return static_cast<int>( m_rows.size() ); }

    QVariant Data( int iRow, int role )
    {
        if ( iRow < 0 || iRow >= RowCount() ) { return {}; }
        const row_t &row = m_rows[static_cast<usize>( iRow )];
        const editor_asset_t *pAsset = row.iAsset >= 0 ? EditorAssets_At( &m_catalog, static_cast<usize>( row.iAsset ) ) : nullptr;
        switch ( role ) {
            case Qt::DisplayRole: {
                const QString name = pAsset != nullptr ? FromView( EditorAssets_Name( &m_catalog, *pAsset ) ) : QFileInfo( row.path ).completeBaseName();
                if ( !m_bList ) { return name; }
                const QString folder = pAsset != nullptr ? FromView( EditorAssets_Folder( &m_catalog, *pAsset ) ) : QFileInfo( row.path ).path();
                return pAsset != nullptr ? QStringLiteral( "%1    %2" ).arg( name, folder ) : QStringLiteral( "%1    %2  (missing)" ).arg( name, folder );
            }
            case Qt::DecorationRole: return Tile( row );
            case Qt::ToolTipRole: return Tooltip( row, pAsset );
            case Qt::ForegroundRole:
                if ( pAsset == nullptr ) { return EditorStyle_TokenColor( m_pGui->style, "ui.status.error.text" ); }
                if ( pAsset->bSource ) { return EditorStyle_TokenColor( m_pGui->style, "ui.text.muted" ); }
                return {};
            default: return {};
        }
    }

    QString PathAt( int iRow ) const { return iRow >= 0 && iRow < RowCount() ? m_rows[static_cast<usize>( iRow )].path : QString(); }

private:
    // ---- Building --------------------------------------------------------

    QWidget *BuildTabs()
    {
        m_pTabs = new QTabBar( this );
        m_pTabs->setObjectName( QStringLiteral( "AssetBrowserTabs" ) );
        m_pTabs->setDrawBase( false );
        m_pTabs->setExpanding( false );
        m_pTabs->setUsesScrollButtons( true );
        m_pTabs->setElideMode( Qt::ElideNone );
        for ( int i = 0; i < ASSET_TAB_COUNT; ++i ) { m_pTabs->addTab( QString() ); }
        QObject::connect( m_pTabs, &QTabBar::currentChanged, this, [this]( int index ) {
            m_tab = index;
            FillTree();
            Refilter();
        } );
        return m_pTabs;
    }

    QToolButton *ToolButton( QWidget *pParent, const char *pName, const char *pIcon, const QString &text, const QString &tip, bool bCheckable )
    {
        auto *pButton = new QToolButton( pParent );
        pButton->setObjectName( QString::fromLatin1( pName ) );
        pButton->setText( text );
        pButton->setToolTip( tip );
        pButton->setCheckable( bCheckable );
        pButton->setAutoRaise( true );
        if ( pIcon != nullptr ) {
            pButton->setIcon( EditorStyle_Icon( m_pGui->style, pIcon ) );
            pButton->setIconSize( QSize( 18, 18 ) );
            pButton->setToolButtonStyle( Qt::ToolButtonIconOnly );
        }
        return pButton;
    }

    QWidget *BuildToolbar()
    {
        auto *pBar = new QWidget( this );
        pBar->setObjectName( QStringLiteral( "AssetBrowserToolbar" ) );
        auto *pLayout = new QHBoxLayout( pBar );
        pLayout->setContentsMargins( 4, 3, 4, 3 );
        pLayout->setSpacing( 3 );
        m_pFolders = ToolButton( pBar, "AssetBrowserShowFolders", "asset-folder", QStringLiteral( "Folders" ), QStringLiteral( "Show the folder tree" ), true );
        m_pFolders->setChecked( true );
        m_pSearch = new QLineEdit( pBar );
        m_pSearch->setObjectName( QStringLiteral( "AssetBrowserSearch" ) );
        m_pSearch->setPlaceholderText( QStringLiteral( "Search assets" ) );
        m_pSearch->setToolTip( QStringLiteral( "Fuzzy search over names and paths: \"flrtl\" finds floor_tile" ) );
        m_pSearch->setMinimumWidth( 80 );
        m_pSearch->setClearButtonEnabled( true );
        m_pSources = new QToolButton( pBar );
        m_pSources->setObjectName( QStringLiteral( "AssetBrowserShowSources" ) );
        m_pSources->setText( QStringLiteral( "Sources" ) );
        m_pSources->setCheckable( true );
        m_pSources->setToolTip( QStringLiteral( "Also list source files (.png, .fbx, .wav) next to the recipes built from them" ) );
        m_pGrid = ToolButton( pBar, "AssetBrowserGrid", "view-quad", QStringLiteral( "Grid" ), QStringLiteral( "Thumbnails" ), true );
        m_pGrid->setChecked( !m_bList );
        m_pList = ToolButton( pBar, "AssetBrowserList", "view-outliner", QStringLiteral( "List" ), QStringLiteral( "Names and folders" ), true );
        m_pList->setChecked( m_bList );
        m_pSize = new QSlider( Qt::Horizontal, pBar );
        m_pSize->setObjectName( QStringLiteral( "AssetBrowserTileSize" ) );
        m_pSize->setRange( kTileMin, kTileMax );
        m_pSize->setValue( m_tileSize );
        m_pSize->setSingleStep( 8 );
        m_pSize->setPageStep( 32 );
        m_pSize->setFixedWidth( 88 );
        m_pSize->setToolTip( QStringLiteral( "Thumbnail size" ) );
        m_pSizePreset = new QToolButton( pBar );
        m_pSizePreset->setObjectName( QStringLiteral( "AssetBrowserSizePreset" ) );
        m_pSizePreset->setPopupMode( QToolButton::InstantPopup );
        auto *pSizes = new QMenu( m_pSizePreset );
        pSizes->addSection( QStringLiteral( "Thumbnail size" ) );
        auto *pSizeGroup = new QActionGroup( pSizes );
        for ( const int size : { 64, 96, 128, 144, 192, 256 } ) {
            auto *pAction = pSizes->addAction( QStringLiteral( "%1 px%2" ).arg( size ).arg( size == kTileDefault ? QStringLiteral( " (default)" ) : QString() ) );
            pAction->setObjectName( QStringLiteral( "AssetBrowserSize%1" ).arg( size ) );
            pAction->setCheckable( true );
            pAction->setData( size );
            pSizeGroup->addAction( pAction );
            QObject::connect( pAction, &QAction::triggered, this, [this, size]() { SetTileSize( size ); } );
        }
        QObject::connect( pSizes, &QMenu::aboutToShow, this, [this, pSizeGroup]() {
            for ( QAction *pAction : pSizeGroup->actions() ) { pAction->setChecked( pAction->data().toInt() == m_tileSize ); }
        } );
        m_pSizePreset->setMenu( pSizes );
        QToolButton *pRefresh =
            ToolButton( pBar, "AssetBrowserRefresh", "layout-reset", QStringLiteral( "Refresh" ), QStringLiteral( "Scan the content folders again" ), false );
        pLayout->addWidget( m_pFolders );
        pLayout->addWidget( m_pSearch, 1 );
        pLayout->addWidget( m_pSources );
        pLayout->addSpacing( 6 );
        pLayout->addWidget( m_pGrid );
        pLayout->addWidget( m_pList );
        pLayout->addWidget( m_pSize );
        pLayout->addWidget( m_pSizePreset );
        pLayout->addSpacing( 6 );
        pLayout->addWidget( pRefresh );

        QObject::connect( m_pFolders, &QToolButton::toggled, this, [this]( bool bShow ) { m_pTree->setVisible( bShow ); } );
        QObject::connect( m_pSearch, &QLineEdit::textChanged, this, [this]( const QString & ) { Refilter(); } );
        QObject::connect( m_pSources, &QToolButton::toggled, this, [this]( bool ) {
            FillTabs();
            FillTree();
            Refilter();
        } );
        QObject::connect( m_pGrid, &QToolButton::clicked, this, [this]() { SetList( false ); } );
        QObject::connect( m_pList, &QToolButton::clicked, this, [this]() { SetList( true ); } );
        QObject::connect( m_pSize, &QSlider::valueChanged, this, [this]( int nSize ) { SetTileSize( nSize ); } );
        QObject::connect( pRefresh, &QToolButton::clicked, this, [this]() { Rescan(); } );
        return pBar;
    }

    void SetList( bool bList )
    {
        if ( m_bList == bList ) {
            m_pGrid->setChecked( !bList );
            m_pList->setChecked( bList );
            return;
        }
        m_bList = bList;
        m_tiles.clear();
        ApplyViewMode();
        setting_value_t value{};
        value.type = setting_type_t::BOOL;
        value.bValue = bList;
        WriteSetting( kListViewSetting, value );
    }

    void SetTileSize( int size )
    {
        const int clamped = std::clamp( size, kTileMin, kTileMax );
        if ( m_tileSize == clamped ) { return; }
        m_tileSize = clamped;
        m_tiles.clear();
        ApplyViewMode();
        setting_value_t value{};
        value.type = setting_type_t::INTEGER;
        value.nValue = clamped;
        WriteSetting( kTileSizeSetting, value );
    }

    void WriteSetting( const char *pPath, const setting_value_t &value )
    {
        const setting_descriptor_t *pDescriptor = EditorSettings_Find( &m_pGui->settings, StringView_FromCString( pPath ) );
        if ( pDescriptor != nullptr ) { ( void )EditorSettings_Write( &m_pGui->settings, settings_scope_t::USER, *pDescriptor, value ); }
    }

    static void OnSettingsChanged( void *pContext, string_view_t path ) noexcept
    {
        auto *pBrowser = static_cast<asset_browser_t *>( pContext );
        if ( path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( "editor.ui.tree_lines" ) ) ) {
            pBrowser->m_pTree->viewport()->update();
        }
        if ( path.cchLength != 0u && !StringView_Equals( path, StringView_FromCString( kTileSizeSetting ) ) &&
             !StringView_Equals( path, StringView_FromCString( kListViewSetting ) ) ) { return; }
        const int size = static_cast<int>( std::clamp<i64>( EditorSettings_Integer( &pBrowser->m_pGui->settings, kTileSizeSetting, kTileDefault ), kTileMin, kTileMax ) );
        const bool list = EditorSettings_Bool( &pBrowser->m_pGui->settings, kListViewSetting, CY_FALSE );
        if ( size == pBrowser->m_tileSize && list == pBrowser->m_bList ) { return; }
        pBrowser->m_tileSize = size;
        pBrowser->m_bList = list;
        pBrowser->m_tiles.clear();
        pBrowser->ApplyViewMode();
    }

    void ApplyViewMode()
    {
        m_pGrid->setChecked( !m_bList );
        m_pList->setChecked( m_bList );
        m_pSize->setEnabled( !m_bList );
        m_pSizePreset->setEnabled( !m_bList );
        { const QSignalBlocker blocker( m_pSize ); m_pSize->setValue( m_tileSize ); }
        m_pSizePreset->setText( QStringLiteral( "%1 px" ).arg( m_tileSize ) );
        m_pSizePreset->setToolTip( QStringLiteral( "Thumbnail size presets — %1 pixels" ).arg( m_tileSize ) );
        if ( m_bList ) {
            m_pView->setViewMode( QListView::ListMode );
            m_pView->setFlow( QListView::TopToBottom );
            m_pView->setWrapping( false );
            m_pView->setIconSize( QSize( kListIcon, kListIcon ) );
            m_pView->setGridSize( QSize() );
            m_pView->setSpacing( 0 );
            m_pView->setWordWrap( false );
        } else {
            m_pView->setViewMode( QListView::IconMode );
            m_pView->setFlow( QListView::LeftToRight );
            m_pView->setWrapping( true );
            m_pView->setResizeMode( QListView::Adjust );
            m_pView->setMovement( QListView::Static );
            m_pView->setIconSize( QSize( m_tileSize, m_tileSize ) );
            m_pView->setGridSize( QSize( m_tileSize + 18, m_tileSize + 34 ) );
            m_pView->setSpacing( 2 );
            m_pView->setWordWrap( true );
            m_pView->setTextElideMode( Qt::ElideMiddle );
        }
        m_pModel->ChangedAll();
    }

    // ---- Filling ---------------------------------------------------------

    u32 TabMask() const
    {
        if ( m_tab >= ASSET_TAB_FIRST_KIND && m_tab < ASSET_TAB_USED ) {
            return EditorAssets_KindBit( static_cast<editor_asset_kind_t>( m_tab - ASSET_TAB_FIRST_KIND ) );
        }
        return EDITOR_ASSET_KIND_ALL;
    }

    bool Sources() const { return m_pSources->isChecked(); }

    void FillTabs()
    {
        const QSignalBlocker blocker( m_pTabs );
        u32 nAll = 0u;
        for ( usize k = 0u; k < static_cast<usize>( editor_asset_kind_t::COUNT ); ++k ) {
            const auto kind = static_cast<editor_asset_kind_t>( k );
            u32 nKind = 0u;
            for ( usize i = 0u; i < EditorAssets_Count( &m_catalog ); ++i ) {
                const editor_asset_t *pAsset = EditorAssets_At( &m_catalog, i );
                nKind += pAsset->kind == kind && ( Sources() || !pAsset->bSource ) ? 1u : 0u;
            }
            nAll += nKind;
            const int tab = EditorAssetBrowser_KindTab( kind );
            m_pTabs->setTabText( tab, QStringLiteral( "%1  %2" ).arg( QString::fromLatin1( EditorAssets_KindName( kind ) ) ).arg( nKind ) );
            m_pTabs->setTabIcon( tab, EditorStyle_Icon( m_pGui->style, KindIcon( kind ) ) );
            // Materials and Models stay as Hammer's anchors; empty kinds hide.
            const bool bAnchor = kind == editor_asset_kind_t::MATERIAL || kind == editor_asset_kind_t::MODEL;
            m_pTabs->setTabVisible( tab, nKind != 0u || bAnchor || tab == m_tab );
        }
        m_pTabs->setTabText( ASSET_TAB_ALL, QStringLiteral( "All  %1" ).arg( nAll ) );
        m_pTabs->setTabIcon( ASSET_TAB_ALL, EditorStyle_Icon( m_pGui->style, "asset-browser" ) );
        const qsizetype nMissing = Missing().size();
        m_pTabs->setTabText( ASSET_TAB_USED, nMissing != 0 ? QStringLiteral( "%1  %2 (%3 missing)" ).arg( m_usedTitle ).arg( m_used.size() ).arg( nMissing )
                                                           : QStringLiteral( "%1  %2" ).arg( m_usedTitle ).arg( m_used.size() ) );
        m_pTabs->setTabIcon( ASSET_TAB_USED, EditorStyle_Icon( m_pGui->style, "material-pick" ) );
        m_pTabs->setTabToolTip( ASSET_TAB_USED, QStringLiteral( "Assets the open document names; missing ones show the magenta checker." ) );
    }

    void FillTree()
    {
        m_bFilling = true;
        QSet<QString> knownFolders;
        QSet<QString> expandedFolders;
        for ( QTreeWidgetItemIterator it( m_pTree ); *it != nullptr; ++it ) {
            const QString folder = ( *it )->data( 0, kFolderRole ).toString();
            knownFolders.insert( folder );
            if ( ( *it )->isExpanded() ) { expandedFolders.insert( folder ); }
        }
        m_pTree->clear();
        QHash<QString, QTreeWidgetItem *> items;
        QHash<QString, int> counts;
        const u32 mask = TabMask();
        int nTotal = 0;
        const auto count = [&]( const QString &folder ) {
            ++nTotal;
            QString path = folder;
            while ( !path.isEmpty() ) {
                ++counts[path];
                const qsizetype iSlash = path.lastIndexOf( QLatin1Char( '/' ) );
                path = iSlash < 0 ? QString() : path.left( iSlash );
            }
        };
        if ( m_tab == ASSET_TAB_USED ) {
            for ( const QString &path : m_used ) { count( QFileInfo( path ).path() == QStringLiteral( "." ) ? QString() : QFileInfo( path ).path() ); }
        } else {
            for ( usize i = 0u; i < EditorAssets_Count( &m_catalog ); ++i ) {
                const editor_asset_t *pAsset = EditorAssets_At( &m_catalog, i );
                if ( ( mask & EditorAssets_KindBit( pAsset->kind ) ) == 0u || ( pAsset->bSource && !Sources() ) ) { continue; }
                count( FromView( EditorAssets_Folder( &m_catalog, *pAsset ) ) );
            }
        }
        auto *pAll = new QTreeWidgetItem( m_pTree );
        pAll->setText( 0, QStringLiteral( "All folders" ) );
        pAll->setText( 1, QString::number( nTotal ) );
        pAll->setTextAlignment( 1, Qt::AlignRight | Qt::AlignVCenter );
        pAll->setForeground( 1, EditorStyle_TokenColor( m_pGui->style, "ui.text.muted" ) );
        pAll->setToolTip( 0, QStringLiteral( "All folders · %1 assets" ).arg( nTotal ) );
        pAll->setData( 0, kFolderRole, QString() );
        pAll->setIcon( 0, EditorStyle_Icon( m_pGui->style, "asset-folder" ) );
        QStringList folders = counts.keys();
        std::sort( folders.begin(), folders.end() );
        QTreeWidgetItem *pCurrent = pAll;
        for ( const QString &folder : folders ) {
            const qsizetype iSlash = folder.lastIndexOf( QLatin1Char( '/' ) );
            QTreeWidgetItem *pParent = iSlash < 0 ? pAll : items.value( folder.left( iSlash ), pAll );
            auto *pItem = new QTreeWidgetItem( pParent );
            pItem->setText( 0, folder.mid( iSlash + 1 ) );
            pItem->setText( 1, QString::number( counts.value( folder ) ) );
            pItem->setTextAlignment( 1, Qt::AlignRight | Qt::AlignVCenter );
            pItem->setForeground( 1, EditorStyle_TokenColor( m_pGui->style, "ui.text.muted" ) );
            pItem->setToolTip( 0, QStringLiteral( "%1 (%2)" ).arg( folder ).arg( counts.value( folder ) ) );
            pItem->setData( 0, kFolderRole, folder );
            pItem->setIcon( 0, EditorStyle_Icon( m_pGui->style, "asset-folder" ) );
            items.insert( folder, pItem );
            if ( folder == m_folder ) { pCurrent = pItem; }
        }
        pAll->setExpanded( true );
        for ( auto it = items.cbegin(); it != items.cend(); ++it ) {
            it.value()->setExpanded( knownFolders.contains( it.key() ) ? expandedFolders.contains( it.key() ) : it.value()->parent() == pAll );
        }
        m_pTree->setCurrentItem( pCurrent );
        m_folder = pCurrent->data( 0, kFolderRole ).toString(); // A folder this tab lacks falls back to all.
        m_bFilling = false;
    }

    bool InFolder( const QString &folder ) const
    {
        return m_folder.isEmpty() || folder == m_folder || folder.startsWith( m_folder + QLatin1Char( '/' ) );
    }

    void Refilter()
    {
        const QString query = m_pSearch->text().trimmed();
        const QByteArray queryUtf8 = query.toUtf8();
        m_pModel->Reset( [&]() {
            m_rows.clear();
            m_rowOf.clear();
            if ( m_tab == ASSET_TAB_USED ) {
                for ( const QString &path : m_used ) {
                    const QByteArray utf8 = path.toUtf8();
                    const QString folder = QFileInfo( path ).path() == QStringLiteral( "." ) ? QString() : QFileInfo( path ).path();
                    if ( !InFolder( folder ) ) { continue; }
                    if ( !query.isEmpty() && EditorFuzzy_Score( ViewOf( utf8 ), ViewOf( queryUtf8 ) ) == EDITOR_FUZZY_NO_MATCH ) { continue; }
                    const editor_asset_t *pAsset = EditorAssets_Find( &m_catalog, ViewOf( utf8 ) );
                    const i32 iAsset = pAsset != nullptr ? static_cast<i32>( pAsset - EditorAssets_At( &m_catalog, 0u ) ) : -1;
                    m_rows.push_back( row_t{ iAsset, path } );
                }
            } else if ( EditorAssets_Filter( &m_catalog, TabMask(), ViewOf( queryUtf8 ), Sources() ? CY_TRUE : CY_FALSE, &m_indices ) ==
                        editor_asset_status_t::OK ) {
                for ( usize i = 0u; i < Vector_Count( &m_indices ); ++i ) {
                    const u32 iAsset = Vector_Data( &m_indices )[i];
                    const editor_asset_t *pAsset = EditorAssets_At( &m_catalog, iAsset );
                    if ( !InFolder( FromView( EditorAssets_Folder( &m_catalog, *pAsset ) ) ) ) { continue; }
                    m_rows.push_back( row_t{ static_cast<i32>( iAsset ), FromView( EditorAssets_Path( &m_catalog, *pAsset ) ) } );
                }
            }
            for ( usize i = 0u; i < m_rows.size(); ++i ) { m_rowOf.insert( m_rows[i].path, static_cast<int>( i ) ); }
        } );
        UpdateStatus();
    }

    // ---- Thumbnails ------------------------------------------------------

    QString Resolve( const QString &path ) const
    {
        return PreviewFile( m_roots, path );
    }

    QByteArray ReadAsset( const QString &path ) const
    {
        return ReadRecipe( m_roots, path );
    }

    QImage LoadImageFile( const QString &path ) const
    {
        QImageReader reader( Resolve( path ) );
        reader.setAutoTransform( true );
        QSize size = reader.size();
        if ( size.isValid() && ( size.width() > kImageMax || size.height() > kImageMax ) ) {
            size.scale( kImageMax, kImageMax, Qt::KeepAspectRatio );
            reader.setScaledSize( size );
        }
        return reader.read(); // Null for formats Qt cannot read (EXR, HDR without plugins).
    }

    // Material -> its texture recipe -> that texture's source image. The
    // depth stops a recipe that names itself (or a cycle) from recursing.
    QString ImageSource( const QString &path ) const
    {
        return PreviewImageSource( m_roots, path, m_pGui->pAllocator );
    }

    QImage LoadImageFor( const QString &path ) const
    {
        const QString source = ImageSource( path );
        return source.isEmpty() ? QImage() : LoadImageFile( source );
    }

    void Enqueue( const QString &path )
    {
        if ( m_queued.contains( path ) ) { return; }
        m_queued.insert( path );
        m_queue.append( path );
        if ( !m_pLoader->isActive() ) { m_pLoader->start(); }
    }

    void LoadSome( int nLoads )
    {
        for ( int i = 0; i < nLoads && !m_queue.isEmpty(); ++i ) {
            const QString path = m_queue.takeFirst();
            m_images.insert( path, LoadImageFor( path ) ); // A null image marks "tried, none".
            m_tiles.remove( path );
            if ( const auto it = m_rowOf.constFind( path ); it != m_rowOf.constEnd() ) { m_pModel->Changed( it.value() ); }
        }
        if ( m_queue.isEmpty() ) { m_pLoader->stop(); }
    }

    QPixmap Tile( const row_t &row )
    {
        const int size = m_bList ? kListIcon : m_tileSize;
        if ( const auto it = m_tiles.constFind( row.path ); it != m_tiles.constEnd() ) { return it.value(); }
        const editor_asset_t *pAsset = row.iAsset >= 0 ? EditorAssets_At( &m_catalog, static_cast<usize>( row.iAsset ) ) : nullptr;
        const editor_asset_kind_t kind = pAsset != nullptr ? pAsset->kind : EditorAssets_KindOf( ViewOf( row.path.toUtf8() ), nullptr );
        QImage image;
        bool bFinal = true;
        if ( pAsset == nullptr ) {
            image = MissingImage();
        } else if ( kind == editor_asset_kind_t::MATERIAL || kind == editor_asset_kind_t::TEXTURE ) {
            if ( m_images.contains( row.path ) ) {
                image = m_images.value( row.path );
            } else {
                Enqueue( row.path );
                bFinal = false; // Painted with the icon until the image arrives.
            }
        }
        const qreal dpr = devicePixelRatioF();
        QPixmap tile( QSize( size, size ) * dpr );
        tile.setDevicePixelRatio( dpr );
        tile.fill( Qt::transparent );
        QPainter painter( &tile );
        painter.setRenderHint( QPainter::SmoothPixmapTransform );
        painter.setRenderHint( QPainter::Antialiasing );
        const QRectF frame( 0.5, 0.5, size - 1.0, size - 1.0 );
        painter.fillRect( frame, EditorStyle_TokenColor( m_pGui->style, "ui.deepest" ) );
        if ( !image.isNull() ) {
            const QSizeF fitted = QSizeF( image.size() ).scaled( QSizeF( size - 2.0, size - 2.0 ), Qt::KeepAspectRatio );
            painter.drawImage( QRectF( QPointF( ( size - fitted.width() ) / 2.0, ( size - fitted.height() ) / 2.0 ), fitted ), image );
        } else {
            const int iconSize = m_bList ? size - 4 : std::max( 16, size * 11 / 20 );
            const QPixmap icon = EditorStyle_Icon( m_pGui->style, KindIcon( kind ) ).pixmap( QSize( iconSize, iconSize ), dpr );
            painter.drawPixmap( QPointF( ( size - iconSize ) / 2.0, ( size - iconSize ) / 2.0 - ( m_bList ? 0.0 : 2.0 ) ), icon );
        }
        if ( !m_bList ) {
            if ( pAsset != nullptr && pAsset->bSource ) {
                QFont font = painter.font();
                font.setPixelSize( std::max( 8, size / 9 ) );
                font.setBold( true );
                painter.setFont( font );
                const QRectF badge( 3.0, 3.0, font.pixelSize() * 2.6, font.pixelSize() + 3.0 );
                painter.fillRect( badge, QColor( 0, 0, 0, 160 ) );
                painter.setPen( QColor( 0xE6, 0xE6, 0xE6 ) );
                painter.drawText( badge, Qt::AlignCenter, QStringLiteral( "SRC" ) );
            }
        }
        painter.setPen( EditorStyle_TokenColor( m_pGui->style, "ui.edge" ) );
        painter.drawRect( frame );
        painter.end();
        if ( bFinal ) { m_tiles.insert( row.path, tile ); }
        return tile;
    }

    QString Tooltip( const row_t &row, const editor_asset_t *pAsset ) const
    {
        if ( pAsset == nullptr ) { return QStringLiteral( "%1\nMissing: no content folder has this file." ).arg( row.path ); }
        const QString kind = QString::fromLatin1( EditorAssets_KindLabel( pAsset->kind ) ) +
                             ( pAsset->bSource ? QStringLiteral( " source (%1)" ).arg( FromView( EditorAssets_Extension( &m_catalog, *pAsset ) ) )
                                               : QString() );
        const QString file = pAsset->iRoot < m_roots.size() ? QDir( m_roots[pAsset->iRoot] ).filePath( row.path ) : row.path;
        QString tip = QStringLiteral( "%1\n%2 · %3" ).arg( row.path, kind, SizeText( pAsset->cbSize ) );
        if ( pAsset->modifiedMs != 0 ) {
            tip += QStringLiteral( " · %1" ).arg( QDateTime::fromMSecsSinceEpoch( pAsset->modifiedMs ).toString( QStringLiteral( "yyyy-MM-dd HH:mm" ) ) );
        }
        return tip + QStringLiteral( "\n%1" ).arg( QDir::toNativeSeparators( file ) );
    }

    // ---- Interaction -----------------------------------------------------

    void Fire( const QString &path, editor_asset_kind_t kind )
    {
        if ( m_pfnActivate != nullptr ) { m_pfnActivate( m_pActivateContext, path, kind ); }
    }

    void ActivateAsset( const QString &path, editor_asset_kind_t kind )
    {
        if ( kind == editor_asset_kind_t::TEXTURE || kind == editor_asset_kind_t::MAP || kind == editor_asset_kind_t::SHADER ) {
            ( void )Preview( path );
        } else { Fire( path, kind ); }
    }

    void ActivateRow( int iRow )
    {
        if ( iRow < 0 || iRow >= RowCount() ) { return; }
        const row_t &row = m_rows[static_cast<usize>( iRow )];
        if ( row.iAsset < 0 ) {
            Log( log_level_t::Warning, QStringLiteral( "%1 is missing; nothing to use" ).arg( row.path ) );
            return;
        }
        ActivateAsset( row.path, EditorAssets_At( &m_catalog, static_cast<usize>( row.iAsset ) )->kind );
    }

    void ContextMenu( const QPoint &at )
    {
        const QModelIndex index = m_pView->indexAt( at );
        if ( !index.isValid() ) { return; }
        const QString path = PathAt( index.row() );
        const QString file = Resolve( path );
        QMenu menu( this );
        menu.setObjectName( QStringLiteral( "AssetBrowserContextMenu" ) );
        const auto kind = EditorAssets_KindOf( ViewOf( path.toUtf8() ), nullptr );
        QAction *pPreview = menu.addAction( QStringLiteral( "Preview / Inspect…" ) );
        pPreview->setObjectName( QStringLiteral( "AssetBrowserPreview" ) );
        pPreview->setIcon( EditorStyle_Icon( m_pGui->style, "search" ) );
        pPreview->setEnabled( m_rows[static_cast<usize>( index.row() )].iAsset >= 0 );
        QAction *pUse = nullptr;
        if ( kind != editor_asset_kind_t::TEXTURE && kind != editor_asset_kind_t::MAP && kind != editor_asset_kind_t::SHADER ) {
            pUse = menu.addAction( kind == editor_asset_kind_t::MATERIAL ? QStringLiteral( "Use Material" ) : QStringLiteral( "Use" ) );
            pUse->setEnabled( m_pfnActivate != nullptr && !file.isEmpty() );
        }
        QAction *pDefault = kind == editor_asset_kind_t::MATERIAL ? pUse : pPreview;
        QFont bold = pDefault->font();
        bold.setBold( true );
        pDefault->setFont( bold );
        menu.setDefaultAction( pDefault );
        menu.addSeparator();
        QAction *pCopy = menu.addAction( QStringLiteral( "Copy Path" ) );
        QAction *pReveal = menu.addAction( QStringLiteral( "Show in Folder" ) );
        pReveal->setEnabled( !file.isEmpty() );
        QAction *pChosen = menu.exec( m_pView->viewport()->mapToGlobal( at ) );
        if ( pChosen == pPreview ) {
            ( void )Preview( path );
        } else if ( pUse != nullptr && pChosen == pUse ) {
            Activate( path );
        } else if ( pChosen == pCopy ) {
            QApplication::clipboard()->setText( path );
        } else if ( pChosen == pReveal ) {
            QDesktopServices::openUrl( QUrl::fromLocalFile( QFileInfo( file ).absolutePath() ) );
        }
    }

    void UpdateStatus()
    {
        const QModelIndexList selected = m_pView->selectionModel() != nullptr ? m_pView->selectionModel()->selectedIndexes() : QModelIndexList{};
        const QString what = m_tab == ASSET_TAB_USED ? m_usedTitle.toLower()
                             : m_tab == ASSET_TAB_ALL
                                 ? QStringLiteral( "assets" )
                                 : QString::fromLatin1( EditorAssets_KindName( static_cast<editor_asset_kind_t>( m_tab - ASSET_TAB_FIRST_KIND ) ) ).toLower();
        QString text = QStringLiteral( "%1 %2" ).arg( m_rows.size() ).arg( what );
        if ( m_roots.isEmpty() ) { text = QStringLiteral( "No content folders. Open a map inside a project, or set Files > Content folders." ); }
        if ( selected.size() == 1 ) {
            const QString path = PathAt( selected.front().row() );
            const QByteArray utf8 = path.toUtf8();
            const editor_asset_t *pAsset = EditorAssets_Find( &m_catalog, ViewOf( utf8 ) );
            text += QStringLiteral( "  ·  %1" ).arg( path );
            if ( pAsset == nullptr ) {
                text += QStringLiteral( "  ·  missing" );
            } else {
                text += QStringLiteral( "  ·  %1" ).arg( SizeText( pAsset->cbSize ) );
                // What it is built from: the dependency list Hammer shows.
                QStringList uses;
                if ( !pAsset->bSource ) {
                    const QByteArray recipe = ReadAsset( path );
                    ( void )EditorAssets_References(
                        ViewOf( recipe ), m_pGui->pAllocator,
                        []( void *pContext, string_view_t reference, editor_asset_kind_t ) { static_cast<QStringList *>( pContext )->append( FromView( reference ) ); },
                        &uses );
                }
                if ( !uses.isEmpty() ) { text += QStringLiteral( "  ·  uses %1" ).arg( uses.join( QStringLiteral( ", " ) ) ); }
            }
        } else if ( selected.size() > 1 ) {
            text += QStringLiteral( "  ·  %1 selected" ).arg( selected.size() );
        }
        m_pStatus->setText( text );
    }

    static void OnStyleChanged( void *pContext ) noexcept
    {
        auto *pBrowser = static_cast<asset_browser_t *>( pContext );
        pBrowser->m_tiles.clear(); // Backgrounds and icons follow the theme.
        pBrowser->FillTabs();
        for ( QTreeWidgetItemIterator it( pBrowser->m_pTree ); *it != nullptr; ++it ) {
            ( *it )->setIcon( 0, EditorStyle_Icon( pBrowser->m_pGui->style, "asset-folder" ) );
            ( *it )->setForeground( 1, EditorStyle_TokenColor( pBrowser->m_pGui->style, "ui.text.muted" ) );
        }
        pBrowser->m_pTree->viewport()->update();
        pBrowser->m_pModel->ChangedAll();
    }

    static void Log( log_level_t level, const QString &text )
    {
        const QByteArray utf8 = text.toUtf8();
        Cy_LogWriteAt( level, log_channel_t::Editor, utf8.constData(), CY_SOURCE_LOCATION );
    }

    editor_gui_t *m_pGui{ nullptr };
    editor_asset_catalog_t m_catalog{};
    vector_t<u32> m_indices{};
    QStringList m_roots{};
    QStringList m_used{};
    QString m_usedTitle{ QStringLiteral( "Used" ) };
    std::vector<row_t> m_rows{};
    QHash<QString, int> m_rowOf{};
    QHash<QString, QImage> m_images{};
    QHash<QString, QPixmap> m_tiles{};
    QStringList m_queue{};
    QSet<QString> m_queued{};
    QTabBar *m_pTabs{ nullptr };
    QLineEdit *m_pSearch{ nullptr };
    QToolButton *m_pFolders{ nullptr };
    QToolButton *m_pSources{ nullptr };
    QToolButton *m_pGrid{ nullptr };
    QToolButton *m_pList{ nullptr };
    QSlider *m_pSize{ nullptr };
    QToolButton *m_pSizePreset{ nullptr };
    QTreeWidget *m_pTree{ nullptr };
    QListView *m_pView{ nullptr };
    asset_model_t *m_pModel{ nullptr };
    QLabel *m_pStatus{ nullptr };
    QTimer *m_pLoader{ nullptr };
    QPointer<asset_preview_t> m_pPreview{};
    editor_asset_activate_fn m_pfnActivate{ nullptr };
    void *m_pActivateContext{ nullptr };
    editor_asset_rescan_fn m_pfnRescan{ nullptr };
    void *m_pRescanContext{ nullptr };
    QString m_folder{};
    int m_tab{ ASSET_TAB_ALL };
    int m_tileSize{ kTileDefault };
    bool m_bList{ false };
    bool m_bFilling{ false };
};

int asset_model_t::rowCount( const QModelIndex &parent ) const
{
    return parent.isValid() ? 0 : m_pBrowser->RowCount();
}

QVariant asset_model_t::data( const QModelIndex &index, int role ) const
{
    return index.isValid() ? m_pBrowser->Data( index.row(), role ) : QVariant();
}

Qt::ItemFlags asset_model_t::flags( const QModelIndex &index ) const
{
    return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled : Qt::NoItemFlags;
}

// Dragged assets carry their virtual paths: what a viewport, a property, or
// a text field wants to receive.
QMimeData *asset_model_t::mimeData( const QModelIndexList &indexes ) const
{
    QStringList paths;
    for ( const QModelIndex &index : indexes ) { paths.append( m_pBrowser->PathAt( index.row() ) ); }
    auto *pMime = new QMimeData();
    pMime->setData( QString::fromLatin1( kMimeType ), paths.join( QLatin1Char( '\n' ) ).toUtf8() );
    pMime->setText( paths.join( QLatin1Char( '\n' ) ) );
    return pMime;
}

asset_browser_t *AsBrowser( QWidget *pBrowser )
{
    auto *pImpl = dynamic_cast<asset_browser_t *>( pBrowser );
    CY_ASSERT( pImpl != nullptr );
    return pImpl;
}

} // namespace

QWidget *EditorAssetBrowser_Create( QWidget *pParent, editor_gui_t *pGui )
{
    CY_ASSERT( pGui != nullptr && pGui->bInitialized );
    if ( pGui == nullptr || !pGui->bInitialized ) { return nullptr; }
    return new asset_browser_t( pParent, pGui );
}

void EditorAssetBrowser_SetRoots( QWidget *pBrowser, const QStringList &roots )
{
    if ( asset_browser_t *pImpl = AsBrowser( pBrowser ) ) { pImpl->SetRoots( roots ); }
}

QStringList EditorAssetBrowser_Roots( QWidget *pBrowser )
{
    asset_browser_t *pImpl = AsBrowser( pBrowser );
    return pImpl != nullptr ? pImpl->Roots() : QStringList{};
}

void EditorAssetBrowser_Rescan( QWidget *pBrowser )
{
    if ( asset_browser_t *pImpl = AsBrowser( pBrowser ) ) { pImpl->Rescan(); }
}

void EditorAssetBrowser_SetRescanCallback( QWidget *pBrowser, editor_asset_rescan_fn callback, void *context )
{
    if ( asset_browser_t *pImpl = AsBrowser( pBrowser ) ) { pImpl->SetRescanCallback( callback, context ); }
}

void EditorAssetBrowser_SetUsed( QWidget *pBrowser, const QString &title, const QStringList &paths )
{
    if ( asset_browser_t *pImpl = AsBrowser( pBrowser ) ) { pImpl->SetUsed( title, paths ); }
}

void EditorAssetBrowser_SetActivate( QWidget *pBrowser, editor_asset_activate_fn pfnActivate, void *pContext )
{
    if ( asset_browser_t *pImpl = AsBrowser( pBrowser ) ) { pImpl->SetActivate( pfnActivate, pContext ); }
}

void EditorAssetBrowser_SetTab( QWidget *pBrowser, editor_asset_tab_t tab )
{
    if ( asset_browser_t *pImpl = AsBrowser( pBrowser ) ) { pImpl->SetTab( tab ); }
}

void EditorAssetBrowser_SetSearch( QWidget *pBrowser, const QString &text )
{
    if ( asset_browser_t *pImpl = AsBrowser( pBrowser ) ) { pImpl->SetSearch( text ); }
}

void EditorAssetBrowser_Search( QWidget *pBrowser, const QString &text, bool focusFirstResult )
{
    if ( asset_browser_t *pImpl = AsBrowser( pBrowser ) ) { pImpl->Search( text, focusFirstResult ); }
}

void EditorAssetBrowser_SetFolder( QWidget *pBrowser, const QString &folder )
{
    if ( asset_browser_t *pImpl = AsBrowser( pBrowser ) ) { pImpl->SetFolder( folder ); }
}

void EditorAssetBrowser_SetSources( QWidget *pBrowser, bool bShow )
{
    if ( asset_browser_t *pImpl = AsBrowser( pBrowser ) ) { pImpl->SetSources( bShow ); }
}

QStringList EditorAssetBrowser_Visible( QWidget *pBrowser )
{
    asset_browser_t *pImpl = AsBrowser( pBrowser );
    return pImpl != nullptr ? pImpl->Visible() : QStringList{};
}

QStringList EditorAssetBrowser_Missing( QWidget *pBrowser )
{
    asset_browser_t *pImpl = AsBrowser( pBrowser );
    return pImpl != nullptr ? pImpl->Missing() : QStringList{};
}

const editor_asset_catalog_t *EditorAssetBrowser_Catalog( QWidget *pBrowser )
{
    asset_browser_t *pImpl = AsBrowser( pBrowser );
    return pImpl != nullptr ? pImpl->Catalog() : nullptr;
}

void EditorAssetBrowser_Activate( QWidget *pBrowser, const QString &path )
{
    if ( asset_browser_t *pImpl = AsBrowser( pBrowser ) ) { pImpl->Activate( path ); }
}

QWidget *EditorAssetBrowser_Preview( QWidget *pBrowser, const QString &path )
{
    asset_browser_t *browser = AsBrowser( pBrowser );
    return browser != nullptr ? browser->Preview( path ) : nullptr;
}

void EditorAssetBrowser_LoadThumbnails( QWidget *pBrowser )
{
    if ( asset_browser_t *pImpl = AsBrowser( pBrowser ) ) { pImpl->LoadAll(); }
}

bool EditorAssetBrowser_HasImage( QWidget *pBrowser, const QString &path )
{
    asset_browser_t *pImpl = AsBrowser( pBrowser );
    return pImpl != nullptr && pImpl->HasImage( path );
}

QImage EditorAssetBrowser_Image( QWidget *pBrowser, const QString &path )
{
    asset_browser_t *pImpl = AsBrowser( pBrowser );
    return pImpl != nullptr ? pImpl->Image( path ) : QImage();
}

QString EditorAssetBrowser_Status( QWidget *pBrowser )
{
    asset_browser_t *pImpl = AsBrowser( pBrowser );
    return pImpl != nullptr ? pImpl->Status() : QString();
}

} // namespace cypher::editor::gui
