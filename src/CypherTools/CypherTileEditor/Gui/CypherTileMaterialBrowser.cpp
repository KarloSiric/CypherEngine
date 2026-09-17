//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Uses existing material schemas and cooked readers in a Qt palette.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileMaterialBrowser.h"
#include "CypherTileEditorIcons.h"
#include "Core/CypherTileMaterialPreview.h"
#include "CypherCommon/Formats/CypherCommon_RenderAsset.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValueParser.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <filesystem>
#include <utility>

namespace cypher::tools::tile_editor
{
namespace
{
using namespace cypher::common;

constexpr int MATERIAL_BROWSER_MAX_ITEMS = 512;
constexpr qsizetype MATERIAL_COMPILER_LOG_LIMIT = 128 * 1024;

QString FromView( string_view_t text )
{
    return QString::fromUtf8( text.pData, static_cast<qsizetype>( text.cchLength ) );
}

std::filesystem::path NativePath( const QString &path )
{
    const QByteArray utf8 = path.toUtf8();
    return std::filesystem::path( std::u8string(
        reinterpret_cast<const char8_t *>( utf8.constData() ),
        static_cast<size_t>( utf8.size() ) ) );
}

bool IsInsideCanonicalRoot( const QString &canonicalRoot, const QString &canonicalFile )
{
    if ( canonicalRoot.isEmpty() || canonicalFile.isEmpty() ) return false;
    // QFileInfo returns Qt paths with forward slashes on every platform. Keep
    // the separator boundary so a sibling such as assets-backup is not inside assets.
    const QString prefix = canonicalRoot.endsWith( '/' ) ? canonicalRoot : canonicalRoot + '/';
#ifdef Q_OS_WIN
    return canonicalFile.startsWith( prefix, Qt::CaseInsensitive );
#else
    return canonicalFile.startsWith( prefix, Qt::CaseSensitive );
#endif
}

QString CompilerPath()
{
    const QDir app( QCoreApplication::applicationDirPath() );
#ifdef Q_OS_WIN
    const QString name = QStringLiteral( "CypherResourceCompiler.exe" );
#else
    const QString name = QStringLiteral( "CypherResourceCompiler" );
#endif
    for ( const auto &path : { app.filePath( name ), app.filePath( "../../../" + name ) } ) {
        if ( QFileInfo( path ).isExecutable() ) return QFileInfo( path ).absoluteFilePath();
    }
    return {};
}

QString StagedAssets()
{
    const QDir app( QCoreApplication::applicationDirPath() );
    for ( const auto &path : { app.filePath( "resources" ), app.filePath( "../../../resources" ) } ) {
        if ( QFileInfo::exists( path + "/shaders/tile_surface.cyshader_c" ) )
            return QDir( path ).absolutePath();
    }
    return {};
}

bool MaterialInputs( const QString &root, const QString &path, QStringList &inputs, QString &error )
{
    const QString canonicalRoot = QFileInfo( root ).canonicalFilePath();
    const QString filePath = QFileInfo( QDir( root ).filePath( path ) ).canonicalFilePath();
    if ( !IsInsideCanonicalRoot( canonicalRoot, filePath ) ) {
        error = QObject::tr( "Material must be inside the asset source root." );
        return false;
    }
    QFile file( filePath );
    if ( !file.open( QIODevice::ReadOnly ) || file.size() > 1024 * 1024 ) {
        error = QObject::tr( "Could not read material recipe (maximum 1 MiB)." );
        return false;
    }
    const QByteArray bytes = file.readAll();
    key_value_document_desc_t description{};
    description.pAllocator = Allocator_GetSystem();
    auto *document = KeyValue_CreateDocument( description );
    if ( document == nullptr ) {
        error = QObject::tr( "Could not allocate material parser." );
        return false;
    }
    const auto parsed = KeyValue_ParseText(
        { bytes.constData(), static_cast<usize>( bytes.size() ) }, {}, document );
    render_material_source_view_t material{};
    schema_diagnostic_t diagnostic{};
    bool ok = parsed.status == key_value_parse_status_t::OK;
    if ( ok ) {
        ok = RenderAsset_DecodeSucceeded(
            RenderMaterialSource_Decode( document, {}, &diagnostic, 1, &material ) );
    }
    if ( ok ) {
        inputs << FromView( material.shader );
        for ( usize i = 0u; i < material.nTextures; ++i )
            inputs << FromView( material.textures[i].texture );
        inputs << path;
        inputs.removeDuplicates();
    } else {
        error = QObject::tr( "The material recipe is invalid. Check its format and required shader and texture references." );
    }
    KeyValue_DestroyDocument( document );
    return ok;
}
} // namespace

CypherTileMaterialBrowser::CypherTileMaterialBrowser( QWidget *parent ) : QWidget( parent )
{
    setObjectName( "TileProjectMaterials" );
    auto *layout = new QVBoxLayout( this );
    layout->setContentsMargins( 4, 4, 4, 4 );
    layout->setSpacing( 3 );
    auto *rootRow = new QHBoxLayout;
    m_root = new QLineEdit( this );
    m_root->setObjectName( "TileMaterialSourceRoot" );
    m_root->setReadOnly( true );
    m_root->setPlaceholderText( tr( "Asset source root" ) );
    auto *browse = new QPushButton( tr( "Folder…" ), this );
    auto *refreshButton = new QPushButton( tr( "Refresh" ), this );
    rootRow->addWidget( m_root, 1 );
    rootRow->addWidget( browse );
    rootRow->addWidget( refreshButton );
    layout->addLayout( rootRow );
    connect( browse, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getExistingDirectory( this, tr( "Choose asset source root" ), sourceRoot() );
        if ( !path.isEmpty() ) setSourceRoot( path );
    } );
    connect( refreshButton, &QPushButton::clicked, this, [this] {
        if ( isBusy() ) return;
        refresh();
        if ( !m_cookedRoot.isEmpty() && m_reloadCallback ) m_reloadCallback( m_cookedRoot );
    } );

    m_filter = new QLineEdit( this );
    m_filter->setObjectName( "TileProjectMaterialFilter" );
    m_filter->setPlaceholderText( tr( "Filter materials…" ) );
    m_filter->setClearButtonEnabled( true );
    layout->addWidget( m_filter );
    m_list = new QListWidget( this );
    m_list->setObjectName( "TileProjectMaterialList" );
    m_list->setViewMode( QListView::IconMode );
    m_list->setResizeMode( QListView::Adjust );
    m_list->setMovement( QListView::Static );
    m_list->setIconSize( { 64, 48 } );
    m_list->setGridSize( { 112, 72 } );
    m_list->setMinimumHeight( 72 );
    layout->addWidget( m_list, 1 );
    connect( m_filter, &QLineEdit::textChanged, this, [this] { applyFilter(); } );
    connect( m_list, &QListWidget::currentItemChanged, this, [this] { updateSelection(); } );

    auto *commands = new QGridLayout;
    commands->addWidget( new QLabel( tr( "Map slot" ), this ), 0, 0 );
    m_slot = new QSpinBox( this );
    m_slot->setObjectName( "TileProjectMaterialSlot" );
    m_slot->setRange( 0, 65535 );
    m_slot->setValue( 8 );
    commands->addWidget( m_slot, 0, 1 );
    m_cook = new QPushButton( tr( "Update Material" ), this );
    m_assign = new QPushButton( tr( "Use Material" ), this );
    m_apply = new QPushButton( tr( "Apply to Selection" ), this );
    m_clear = new QPushButton( tr( "Reset Slot" ), this );
    m_cook->setObjectName( "TileMaterialCook" );
    m_assign->setObjectName( "TileMaterialBind" );
    m_apply->setObjectName( "TileMaterialApply" );
    m_clear->setObjectName( "TileMaterialClear" );
    m_cook->setToolTip( tr( "Prepare this material and its textures for previewing and painting." ) );
    m_assign->setToolTip( tr( "Bind this material to the map slot and select it for painting. Every tile using this slot will update." ) );
    m_apply->setToolTip( tr( "Apply this slot to selected floors. Shape and height stay the same; Undo restores the previous materials." ) );
    m_clear->setToolTip( tr( "Remove this slot's project material and use its blockout color. Tile assignments stay the same; Undo restores the binding." ) );
    commands->addWidget( m_assign, 1, 0 );
    commands->addWidget( m_apply, 1, 1 );
    commands->addWidget( m_cook, 2, 0 );
    commands->addWidget( m_clear, 2, 1 );
    layout->addLayout( commands );
    m_details = new QLabel( this );
    m_details->setObjectName( "TileMaterialDetails" );
    m_details->setWordWrap( true );
    m_details->setProperty( "muted", true );
    layout->addWidget( m_details );
    connect( m_cook, &QPushButton::clicked, this, [this] { m_pendingAssignPath.clear(); cookSelected(); } );
    connect( m_list, &QListWidget::itemDoubleClicked, this, [this] { assignSelected(); } );
    connect( m_assign, &QPushButton::clicked, this, [this] { assignSelected(); } );
    connect( m_apply, &QPushButton::clicked, this, [this] {
        if ( !isBusy() && m_applyCallback ) m_applyCallback( static_cast<unsigned short>( m_slot->value() ) );
    } );
    connect( m_clear, &QPushButton::clicked, this, [this] {
        if ( !isBusy() && m_assignCallback ) m_assignCallback( static_cast<unsigned short>( m_slot->value() ), QString() );
    } );

    QSettings settings;
#ifdef CYPHER_TILE_EDITOR_ASSET_SOURCE_DIR
    const QString defaultRoot = QString::fromUtf8( CYPHER_TILE_EDITOR_ASSET_SOURCE_DIR );
#else
    const QString defaultRoot = QDir::current().filePath( "assets" );
#endif
    const QString savedRoot = settings.value( "TileEditor/materialSourceRoot", defaultRoot ).toString();
    setSourceRoot( QFileInfo( savedRoot ).isDir() ? savedRoot : defaultRoot );
}

CypherTileMaterialBrowser::~CypherTileMaterialBrowser()
{
    // waitForFinished can deliver signals synchronously. Neither browser widgets
    // nor callbacks into the owning main window may be used during teardown.
    m_assignCallback = {};
    m_applyCallback = {};
    m_reloadCallback = {};
    m_statusCallback = {};
    if ( m_process != nullptr ) {
        m_process->disconnect( this );
        if ( m_process->state() != QProcess::NotRunning ) {
            m_process->kill();
            m_process->waitForFinished( 1000 );
        }
    }
}

QString CypherTileMaterialBrowser::sourceRoot() const { return m_root->text(); }
QString CypherTileMaterialBrowser::cookedRoot() const { return m_cookedRoot; }
void CypherTileMaterialBrowser::cancelPendingAssignment() { m_pendingAssignPath.clear(); }
bool CypherTileMaterialBrowser::isBusy() const
{
    return m_process != nullptr && m_process->state() != QProcess::NotRunning;
}

void CypherTileMaterialBrowser::setAssignCallback( std::function<void( unsigned short, const QString & )> callback )
{
    m_assignCallback = std::move( callback );
}
void CypherTileMaterialBrowser::setApplyCallback( std::function<void( unsigned short )> callback )
{
    m_applyCallback = std::move( callback );
}
void CypherTileMaterialBrowser::setReloadCallback( std::function<void( const QString & )> callback )
{
    m_reloadCallback = std::move( callback );
}
void CypherTileMaterialBrowser::setStatusCallback( std::function<void( const QString &, bool )> callback )
{
    m_statusCallback = std::move( callback );
}

void CypherTileMaterialBrowser::reportError( const QString &message )
{
    m_details->setText( message );
    if ( m_statusCallback ) m_statusCallback( message, true );
}

void CypherTileMaterialBrowser::setSourceRoot( const QString &root )
{
    if ( isBusy() ) return;
    m_pendingAssignPath.clear();
    const QFileInfo rootInfo( root );
    const QString canonical = rootInfo.canonicalFilePath();
    if ( root.isEmpty() || canonical.isEmpty() || !rootInfo.isDir() ) {
        updateSelection();
        reportError( tr( "Choose an existing asset folder." ) );
        return;
    }
    const QString cacheRoot = QStandardPaths::writableLocation( QStandardPaths::CacheLocation );
    const auto hash = QCryptographicHash::hash( canonical.toUtf8(), QCryptographicHash::Sha256 ).toHex().left( 16 );
    const QString cooked = cacheRoot + "/materials/" + QString::fromLatin1( hash );
    if ( cacheRoot.isEmpty() || !QDir().mkpath( cooked ) ) {
        reportError( tr( "Could not create the material preview cache." ) );
        return;
    }
    m_root->setText( canonical );
    m_cookedRoot = cooked;
#ifdef CYPHER_TILE_EDITOR_ASSET_SOURCE_DIR
    // Seed the default project's cache without overwriting materials explicitly
    // cooked by the user. Project switches receive separate cache namespaces.
    if ( canonical == QFileInfo( QString::fromUtf8( CYPHER_TILE_EDITOR_ASSET_SOURCE_DIR ) ).canonicalFilePath() ) {
        const auto staged = StagedAssets();
        if ( !staged.isEmpty() ) {
            QDirIterator iterator( staged, { "*.cymat_c", "*.cytex_c", "*.cyshader_c" },
                QDir::Files, QDirIterator::Subdirectories );
            while ( iterator.hasNext() ) {
                const auto file = iterator.next();
                const auto relative = QDir( staged ).relativeFilePath( file );
                const auto target = QDir( m_cookedRoot ).filePath( relative );
                if ( !QFileInfo::exists( target ) ) {
                    QDir().mkpath( QFileInfo( target ).absolutePath() );
                    QFile::copy( file, target );
                }
            }
        }
    }
#endif
    QSettings().setValue( "TileEditor/materialSourceRoot", sourceRoot() );
    refresh();
    if ( m_reloadCallback ) m_reloadCallback( m_cookedRoot );
}

QString CypherTileMaterialBrowser::selectedPath() const
{
    const auto *item = m_list->currentItem();
    return item != nullptr && !item->isHidden() ? item->data( Qt::UserRole ).toString() : QString();
}

void CypherTileMaterialBrowser::refresh()
{
    const QString selected = selectedPath();
    const QSignalBlocker blockListSignals( m_list );
    m_list->clear();
    const QFileInfo rootInfo( sourceRoot() );
    const QString canonicalRoot = rootInfo.canonicalFilePath();
    if ( sourceRoot().isEmpty() || !rootInfo.isDir() || canonicalRoot.isEmpty() ) {
        updateSelection();
        return;
    }
    QDirIterator iterator( canonicalRoot, { "*.cymat" }, QDir::Files, QDirIterator::Subdirectories );
    int count = 0;
    while ( iterator.hasNext() && count < MATERIAL_BROWSER_MAX_ITEMS ) {
        const auto file = iterator.next();
        const auto relative = QDir( canonicalRoot ).relativeFilePath( file );
        if ( !IsInsideCanonicalRoot( canonicalRoot, QFileInfo( file ).canonicalFilePath() ) ) continue;
        auto *item = new QListWidgetItem( CypherTileEditorIcon_Create( tile_editor_icon_t::MATERIAL ),
            QFileInfo( file ).completeBaseName(), m_list );
        item->setData( Qt::UserRole, relative );
        item->setToolTip( relative );
        tile_material_preview_source_t material;
        std::string error;
        if ( CypherTileMaterialPreview_Read( NativePath( m_cookedRoot ), relative.toUtf8().toStdString(), material, error ) ) {
            const QImage image( reinterpret_cast<const uchar *>( material.pixels.data() ),
                static_cast<int>( material.width ), static_cast<int>( material.height ), QImage::Format_RGBA8888 );
            item->setIcon( QPixmap::fromImage( image.copy().scaled( 128, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation ) ) );
            item->setData( Qt::UserRole + 1, true );
            item->setToolTip( tr( "%1\n%2 × %3 · %4\nReady to use. Double-click to choose this material." )
                .arg( relative ).arg( material.width ).arg( material.height )
                .arg( material.sRGB ? tr( "sRGB" ) : tr( "Linear" ) ) );
        } else {
            item->setToolTip( relative + tr( "\nChoose Use Material to prepare it automatically." ) );
        }
        if ( relative == selected ) m_list->setCurrentItem( item );
        ++count;
    }
    m_list->sortItems();
    applyFilter();
}

void CypherTileMaterialBrowser::applyFilter()
{
    QListWidgetItem *firstVisible = nullptr;
    for ( int i = 0; i < m_list->count(); ++i ) {
        auto *item = m_list->item( i );
        const bool visible = item->data( Qt::UserRole ).toString().contains( m_filter->text(), Qt::CaseInsensitive );
        item->setHidden( !visible );
        if ( visible && firstVisible == nullptr ) firstVisible = item;
    }
    const auto *current = m_list->currentItem();
    if ( current == nullptr || current->isHidden() ) m_list->setCurrentItem( firstVisible );
    updateSelection();
}

void CypherTileMaterialBrowser::updateSelection()
{
    const bool busy = isBusy();
    const auto *item = m_list->currentItem();
    const bool selected = item != nullptr && !item->isHidden();
    m_cook->setEnabled( selected && !busy );
    m_assign->setEnabled( selected && !busy );
    m_apply->setEnabled( !busy );
    m_clear->setEnabled( !busy );
    if ( selected ) m_details->setText( item->toolTip() );
    else if ( !m_filter->text().isEmpty() ) m_details->setText( tr( "No materials match this filter." ) );
    else m_details->setText( tr( "Choose an asset folder containing materials, then select one to prepare and paint." ) );
}

void CypherTileMaterialBrowser::assignSelected()
{
    const QString path = selectedPath();
    if ( path.isEmpty() || isBusy() ) return;
    tile_material_preview_source_t source;
    std::string error;
    if ( !CypherTileMaterialPreview_Read( NativePath( m_cookedRoot ), path.toUtf8().toStdString(), source, error ) ) {
        m_pendingAssignPath = path;
        m_pendingAssignSlot = static_cast<unsigned short>( m_slot->value() );
        cookSelected();
        return;
    }
    if ( m_assignCallback ) m_assignCallback( static_cast<unsigned short>( m_slot->value() ), path );
}

void CypherTileMaterialBrowser::appendCompilerOutput( QProcess *process )
{
    m_compilerLog += QString::fromUtf8( process->readAllStandardOutput() );
    if ( m_compilerLog.size() > MATERIAL_COMPILER_LOG_LIMIT )
        m_compilerLog = m_compilerLog.right( MATERIAL_COMPILER_LOG_LIMIT );
}

void CypherTileMaterialBrowser::cookSelected()
{
    const QString path = selectedPath();
    if ( path.isEmpty() || isBusy() ) return;
    QStringList inputs;
    QString error;
    if ( !MaterialInputs( sourceRoot(), path, inputs, error ) ) {
        m_pendingAssignPath.clear();
        reportError( error );
        return;
    }
    const QString compiler = CompilerPath();
    if ( compiler.isEmpty() ) {
        m_pendingAssignPath.clear();
        reportError( tr( "CypherResourceCompiler is missing beside the editor." ) );
        return;
    }
    auto *process = new QProcess( this );
    m_process = process;
    m_compilerLog.clear();
    QStringList arguments{ "compile", "-s", sourceRoot(), "-o", m_cookedRoot,
                           "--color", "never", "--progress", "none" };
    arguments.append( inputs );
    process->setProgram( compiler );
    process->setArguments( arguments );
    process->setProcessChannelMode( QProcess::MergedChannels );
    connect( process, &QProcess::readyReadStandardOutput, this, [this, process] { appendCompilerOutput( process ); } );
    connect( process, &QProcess::errorOccurred, this, [this, process]( QProcess::ProcessError error ) {
        if ( error != QProcess::FailedToStart ) return;
        m_process = nullptr;
        m_pendingAssignPath.clear();
        process->deleteLater();
        updateSelection();
        reportError( process->errorString() );
    } );
    connect( process, qOverload<int, QProcess::ExitStatus>( &QProcess::finished ), this,
        [this, process]( int code, QProcess::ExitStatus status ) {
            appendCompilerOutput( process );
            m_process = nullptr;
            process->deleteLater();
            refresh();
            const bool ok = code == 0 && status == QProcess::NormalExit;
            if ( m_statusCallback ) m_statusCallback( m_compilerLog, !ok );
            if ( ok && m_reloadCallback ) m_reloadCallback( m_cookedRoot );
            const QString pending = std::exchange( m_pendingAssignPath, {} );
            if ( ok && !pending.isEmpty() ) {
                tile_material_preview_source_t source;
                std::string error;
                if ( CypherTileMaterialPreview_Read( NativePath( m_cookedRoot ), pending.toUtf8().toStdString(), source, error ) ) {
                    if ( m_assignCallback ) m_assignCallback( m_pendingAssignSlot, pending );
                } else reportError( QString::fromStdString( error ) );
            }
        } );
    process->start();
    updateSelection();
    m_details->setText( tr( "Preparing material and textures…" ) );
}

} // namespace cypher::tools::tile_editor
