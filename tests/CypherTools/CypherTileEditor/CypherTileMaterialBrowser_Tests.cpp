//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Verifies material browser selection, root boundaries, and teardown.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileMaterialBrowser.h"
#include "Core/CypherTileMaterialPreview.h"
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <filesystem>
#include <memory>

using namespace cypher::tools::tile_editor;

namespace
{
void EnsureMaterialBrowserApplication()
{
    if ( QApplication::instance() != nullptr ) return;
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    static int argc = 1;
    static char name[] = "CypherTileMaterialBrowserTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
}

struct browser_fixture_t {
    QTemporaryDir directory;
    QString organization{ QCoreApplication::organizationName() };
    QString application{ QCoreApplication::applicationName() };
    QSettings::Format format{ QSettings::defaultFormat() };
    QString previousIniRoot;
    QString cache;
    QString root;

    browser_fixture_t()
    {
        REQUIRE( directory.isValid() );
        const QSettings probe( QSettings::IniFormat, QSettings::UserScope, "CypherBrowserProbe", "Path" );
        previousIniRoot = QFileInfo( QFileInfo( probe.fileName() ).absolutePath() ).absolutePath();
        QSettings::setDefaultFormat( QSettings::IniFormat );
        QSettings::setPath( QSettings::IniFormat, QSettings::UserScope, directory.path() );
        QCoreApplication::setOrganizationName( "CypherTests" );
        QCoreApplication::setApplicationName( "TileMaterialBrowser-" + QFileInfo( directory.path() ).fileName() );
        cache = QStandardPaths::writableLocation( QStandardPaths::CacheLocation );
        root = directory.filePath( "assets" );
        REQUIRE( QDir().mkpath( root ) );
        QSettings().setValue( "TileEditor/materialSourceRoot", root );
    }
    ~browser_fixture_t()
    {
        if ( !cache.isEmpty() ) QDir( cache ).removeRecursively();
        QCoreApplication::setOrganizationName( organization );
        QCoreApplication::setApplicationName( application );
        QSettings::setPath( QSettings::IniFormat, QSettings::UserScope, previousIniRoot );
        QSettings::setDefaultFormat( format );
    }
    void material( const QString &name )
    {
        QFile file( QDir( root ).filePath( name ) );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        const QByteArray recipe = "@cykv 1\n@schema \"cypher.material\" 1\n{\n"
            "shader = \"shaders/tile_surface.cyshader\"\n"
            "textures = { base_color = \"textures/test.cytex\" }\n}\n";
        REQUIRE( file.write( recipe ) == recipe.size() );
    }
    void file( const QString &name, const QByteArray &contents )
    {
        const QString path = QDir( root ).filePath( name );
        REQUIRE( QDir().mkpath( QFileInfo( path ).absolutePath() ) );
        QFile output( path );
        REQUIRE( output.open( QIODevice::WriteOnly ) );
        REQUIRE( output.write( contents ) == contents.size() );
    }
    void image()
    {
        QImage pixels( 2, 2, QImage::Format_RGBA8888 );
        pixels.setPixelColor( 0, 0, Qt::red );
        pixels.setPixelColor( 1, 0, Qt::green );
        pixels.setPixelColor( 0, 1, Qt::blue );
        pixels.setPixelColor( 1, 1, Qt::white );
        REQUIRE( pixels.save( QDir( root ).filePath( "textures/test.png" ), "PNG" ) );
    }
    void cookableInputs( bool includeImage = true )
    {
        file( "shaders/tile_surface.cyshader", "@cykv 1\n@schema \"cypher.shader\" 1\n"
            "{ language = \"glsl\" vertex = \"shaders/tile_surface.vert\" fragment = \"shaders/tile_surface.frag\" }\n" );
        file( "shaders/tile_surface.vert", "#version 410 core\n"
            "layout(location = 0) out vec2 uv;\n"
            "void main() { uv = vec2(0.5); gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n" );
        file( "shaders/tile_surface.frag", "#version 410 core\n"
            "layout(location = 0) in vec2 uv;\n"
            "uniform sampler2D base_color;\n"
            "layout(location = 0) out vec4 color;\n"
            "void main() { color = texture(base_color, uv); }\n" );
        file( "textures/test.cytex", "@cykv 1\n@schema \"cypher.texture\" 1\n"
            "{ source = \"textures/test.png\" usage = \"color\" color_space = \"srgb\" generate_mips = false }\n" );
        if ( includeImage ) image();
    }
};

template <typename Widget>
Widget *BrowserWidget( CypherTileMaterialBrowser &browser, const char *name )
{
    auto *widget = browser.findChild<Widget *>( QString::fromLatin1( name ) );
    REQUIRE( widget != nullptr );
    return widget;
}

bool ResourceCompilerAvailable()
{
    const QDir directory( QCoreApplication::applicationDirPath() );
#ifdef Q_OS_WIN
    const QString name = QStringLiteral( "CypherResourceCompiler.exe" );
#else
    const QString name = QStringLiteral( "CypherResourceCompiler" );
#endif
    return QFileInfo( directory.filePath( name ) ).isExecutable() ||
        QFileInfo( directory.filePath( "../../../" + name ) ).isExecutable();
}

QProcess *RunningCompiler( CypherTileMaterialBrowser &browser )
{
    for ( auto *process : browser.findChildren<QProcess *>() )
        if ( process->state() != QProcess::NotRunning ) return process;
    return nullptr;
}

void WaitForCompiler( QProcess *process, bool success )
{
    REQUIRE( process != nullptr );
    REQUIRE( process->waitForFinished( 5000 ) );
    INFO( process->errorString().toStdString() );
    REQUIRE( process->exitStatus() == QProcess::NormalExit );
    CHECK( ( process->exitCode() == 0 ) == success );
    // Browser's finished handler runs synchronously; deferred process destruction
    // is left to Qt after these assertions, avoiding access to a deleted sender.
}
} // namespace

TEST_CASE( "Material filtering cannot leave a hidden cook or binding target",
           "[CypherTools][Qt][MaterialBrowser]" )
{
    EnsureMaterialBrowserApplication();
    browser_fixture_t fixture;
    fixture.material( "stone.cymat" );
    fixture.material( "metal.cymat" );
    CypherTileMaterialBrowser browser;
    auto *list = BrowserWidget<QListWidget>( browser, "TileProjectMaterialList" );
    auto *filter = BrowserWidget<QLineEdit>( browser, "TileProjectMaterialFilter" );
    auto *cook = BrowserWidget<QPushButton>( browser, "TileMaterialCook" );
    auto *bind = BrowserWidget<QPushButton>( browser, "TileMaterialBind" );
    REQUIRE( list->count() == 2 );
    list->setCurrentRow( 1 );
    REQUIRE( list->currentItem()->data( Qt::UserRole ).toString() == "stone.cymat" );
    filter->setText( "metal" );
    REQUIRE( list->currentItem() != nullptr );
    CHECK_FALSE( list->currentItem()->isHidden() );
    CHECK( list->currentItem()->data( Qt::UserRole ).toString() == "metal.cymat" );
    CHECK( cook->isEnabled() );
    browser.refresh();
    REQUIRE( list->currentItem() != nullptr );
    CHECK( list->currentItem()->data( Qt::UserRole ).toString() == "metal.cymat" );
    filter->setText( "missing" );
    CHECK( list->currentItem() == nullptr );
    CHECK_FALSE( cook->isEnabled() );
    CHECK_FALSE( bind->isEnabled() );
    int callbacks = 0;
    browser.setAssignCallback( [&]( unsigned short, const QString & ) { ++callbacks; } );
    bind->click();
    browser.cookSelected();
    CHECK( callbacks == 0 );
    CHECK( browser.findChild<QProcess *>() == nullptr );
    filter->clear();
    REQUIRE( list->currentItem() != nullptr );
    CHECK_FALSE( list->currentItem()->isHidden() );
}

TEST_CASE( "Material roots reject invalid directories and escaping links without losing current assets",
           "[CypherTools][Qt][MaterialBrowser]" )
{
    EnsureMaterialBrowserApplication();
    browser_fixture_t fixture;
    fixture.material( "stone.cymat" );
    CypherTileMaterialBrowser browser;
    const QString originalRoot = browser.sourceRoot();
    const QString originalCache = browser.cookedRoot();
    auto *list = BrowserWidget<QListWidget>( browser, "TileProjectMaterialList" );
    int reloads = 0;
    int errors = 0;
    browser.setReloadCallback( [&]( const QString & ) { ++reloads; } );
    browser.setStatusCallback( [&]( const QString &, bool error ) { errors += error; } );
    browser.setSourceRoot( fixture.directory.filePath( "missing" ) );
    browser.setSourceRoot( QDir( fixture.root ).filePath( "stone.cymat" ) );
    browser.setSourceRoot( QString() );
    CHECK( errors == 3 );
    CHECK( reloads == 0 );
    CHECK( browser.sourceRoot() == originalRoot );
    CHECK( browser.cookedRoot() == originalCache );
    CHECK( list->count() == 1 );
    CHECK( QSettings().value( "TileEditor/materialSourceRoot" ).toString() == originalRoot );

    const QString sibling = fixture.directory.filePath( "assets-backup" );
    REQUIRE( QDir().mkpath( sibling ) );
    QFile external( QDir( sibling ).filePath( "external.cymat" ) );
    REQUIRE( external.open( QIODevice::WriteOnly ) );
    external.write( "outside" );
    external.close();
    std::error_code linkError;
    std::filesystem::create_symlink( external.fileName().toStdString(),
        QDir( fixture.root ).filePath( "escape.cymat" ).toStdString(), linkError );
    if ( !linkError ) {
        browser.refresh();
        CHECK( list->count() == 1 );
        CHECK( list->item( 0 )->data( Qt::UserRole ).toString() == "stone.cymat" );
    }
}

TEST_CASE( "Clearing a map slot emits an empty binding without a selected material",
           "[CypherTools][Qt][MaterialBrowser]" )
{
    EnsureMaterialBrowserApplication();
    browser_fixture_t fixture;
    CypherTileMaterialBrowser browser;
    auto *slot = BrowserWidget<QSpinBox>( browser, "TileProjectMaterialSlot" );
    auto *clear = BrowserWidget<QPushButton>( browser, "TileMaterialClear" );
    slot->setValue( 65535 );
    int calls = 0;
    browser.setAssignCallback( [&]( unsigned short value, const QString &path ) {
        ++calls;
        CHECK( value == 65535u );
        CHECK( path.isEmpty() );
    } );
    REQUIRE( clear->isEnabled() );
    clear->click();
    CHECK( calls == 1 );
}

TEST_CASE( "Closing a material browser during a cook does not call its owner",
           "[CypherTools][Qt][MaterialBrowser]" )
{
    EnsureMaterialBrowserApplication();
    browser_fixture_t fixture;
    fixture.material( "stone.cymat" );
    auto browser = std::make_unique<CypherTileMaterialBrowser>();
    int callbacks = 0;
    browser->setStatusCallback( [&]( const QString &, bool ) { ++callbacks; } );
    browser->setReloadCallback( [&]( const QString & ) { ++callbacks; } );
    browser->setAssignCallback( [&]( unsigned short, const QString & ) { ++callbacks; } );
    BrowserWidget<QPushButton>( *browser, "TileMaterialBind" )->click();
    auto *process = browser->findChild<QProcess *>();
    if ( process == nullptr ) SKIP( "CypherResourceCompiler executable is unavailable beside this test target." );
    REQUIRE( process->state() != QProcess::NotRunning );
    const int beforeClose = callbacks;
    browser.reset();
    QApplication::processEvents();
    CHECK( callbacks == beforeClose );
}

TEST_CASE( "Use Material prepares real dependencies and retains the requested slot and path while selection changes",
           "[CypherTools][Qt][MaterialBrowser][MaterialPreparation]" )
{
    EnsureMaterialBrowserApplication();
    if ( !ResourceCompilerAvailable() ) SKIP( "CypherResourceCompiler is unavailable beside this test target." );
    browser_fixture_t fixture;
    fixture.cookableInputs();
    fixture.material( "stone.cymat" );
    fixture.material( "metal.cymat" );
    CypherTileMaterialBrowser browser;
    auto *list = BrowserWidget<QListWidget>( browser, "TileProjectMaterialList" );
    auto *slot = BrowserWidget<QSpinBox>( browser, "TileProjectMaterialSlot" );
    auto *use = BrowserWidget<QPushButton>( browser, "TileMaterialBind" );
    auto *apply = BrowserWidget<QPushButton>( browser, "TileMaterialApply" );
    auto *reset = BrowserWidget<QPushButton>( browser, "TileMaterialClear" );
    REQUIRE( list->count() == 2 );
    list->setCurrentRow( 1 );
    REQUIRE( list->currentItem()->data( Qt::UserRole ).toString() == "stone.cymat" );
    slot->setValue( 42 );
    int assignments = 0, errors = 0;
    unsigned short assignedSlot = 0u;
    QString assignedPath;
    QStringList callbacks;
    browser.setStatusCallback( [&]( const QString &, bool error ) { errors += error; } );
    browser.setReloadCallback( [&]( const QString &root ) {
        CHECK( root == browser.cookedRoot() );
        callbacks.append( "reload" );
    } );
    browser.setAssignCallback( [&]( unsigned short value, const QString &path ) {
        ++assignments;
        assignedSlot = value;
        assignedPath = path;
        callbacks.append( "assign" );
    } );
    REQUIRE_FALSE( QFileInfo::exists( QDir( browser.cookedRoot() ).filePath( "stone.cymat_c" ) ) );
    use->click();
    auto *process = RunningCompiler( browser );
    REQUIRE( process != nullptr );
    CHECK( assignments == 0 );
    CHECK_FALSE( use->isEnabled() );
    CHECK_FALSE( apply->isEnabled() );
    CHECK_FALSE( reset->isEnabled() );
    list->setCurrentRow( 0 );
    slot->setValue( 99 );
    WaitForCompiler( process, true );
    CHECK( errors == 0 );
    CHECK( assignments == 1 );
    CHECK( assignedSlot == 42u );
    CHECK( assignedPath == "stone.cymat" );
    CHECK( callbacks == QStringList{ "reload", "assign" } );
    CHECK( use->isEnabled() );
    CHECK( apply->isEnabled() );
    CHECK( reset->isEnabled() );
    tile_material_preview_source_t prepared;
    std::string decodeError;
    REQUIRE( CypherTileMaterialPreview_Read( std::filesystem::path( browser.cookedRoot().toStdString() ),
        "stone.cymat", prepared, decodeError ) );
    CHECK( prepared.width == 2u );
    CHECK( prepared.height == 2u );
    REQUIRE( prepared.pixels.size() == 16u );
    CHECK( prepared.pixels[0] == 255u );

    // A stale "ready" thumbnail must not allow assignment after a dependency
    // disappears: Use Material rechecks decoded assets and repairs the cache.
    list->setCurrentRow( 1 );
    REQUIRE( list->currentItem()->data( Qt::UserRole ).toString() == "stone.cymat" );
    REQUIRE( QFile::remove( QDir( browser.cookedRoot() ).filePath( "textures/test.cytex_c" ) ) );
    assignments = 0;
    callbacks.clear();
    slot->setValue( 51 );
    use->click();
    process = RunningCompiler( browser );
    REQUIRE( process != nullptr );
    CHECK( assignments == 0 );
    WaitForCompiler( process, true );
    CHECK( assignments == 1 );
    CHECK( assignedSlot == 51u );
    CHECK( assignedPath == "stone.cymat" );
}

TEST_CASE( "Failed material preparation clears pending assignment before a later successful manual update",
           "[CypherTools][Qt][MaterialBrowser][MaterialPreparation]" )
{
    EnsureMaterialBrowserApplication();
    if ( !ResourceCompilerAvailable() ) SKIP( "CypherResourceCompiler is unavailable beside this test target." );
    browser_fixture_t fixture;
    fixture.cookableInputs( false ); // The referenced PNG is genuinely missing.
    fixture.material( "broken.cymat" );
    CypherTileMaterialBrowser browser;
    auto *use = BrowserWidget<QPushButton>( browser, "TileMaterialBind" );
    auto *update = BrowserWidget<QPushButton>( browser, "TileMaterialCook" );
    auto *slot = BrowserWidget<QSpinBox>( browser, "TileProjectMaterialSlot" );
    int assignments = 0, reloads = 0, errors = 0;
    QString log;
    browser.setAssignCallback( [&]( unsigned short, const QString & ) { ++assignments; } );
    browser.setReloadCallback( [&]( const QString & ) { ++reloads; } );
    browser.setStatusCallback( [&]( const QString &message, bool error ) {
        errors += error;
        log += message;
    } );
    slot->setValue( 12 );
    use->click();
    WaitForCompiler( RunningCompiler( browser ), false );
    CHECK( assignments == 0 );
    CHECK( reloads == 0 );
    CHECK( errors > 0 );
    CHECK( log.contains( "test.png" ) );
    CHECK( use->isEnabled() );
    CHECK( update->isEnabled() );
    fixture.image();
    slot->setValue( 77 );
    update->click();
    WaitForCompiler( RunningCompiler( browser ), true );
    CHECK( reloads == 1 );
    CHECK( assignments == 0 ); // Update Material must not execute the old request.
    browser.setAssignCallback( [&]( unsigned short value, const QString &path ) {
        ++assignments;
        CHECK( value == 77u );
        CHECK( path == "broken.cymat" );
    } );
    use->click();
    CHECK( RunningCompiler( browser ) == nullptr );
    CHECK( assignments == 1 );
}

TEST_CASE( "Invalid material recipe fails automatic preparation before starting a compiler or assigning a slot",
           "[CypherTools][Qt][MaterialBrowser][MaterialPreparation]" )
{
    EnsureMaterialBrowserApplication();
    browser_fixture_t fixture;
    fixture.file( "invalid.cymat", "@cykv 1\n@schema \"cypher.material\" 1\n{ shader = false }\n" );
    CypherTileMaterialBrowser browser;
    int assignments = 0, errors = 0;
    browser.setAssignCallback( [&]( unsigned short, const QString & ) { ++assignments; } );
    browser.setStatusCallback( [&]( const QString &, bool error ) { errors += error; } );
    BrowserWidget<QPushButton>( browser, "TileMaterialBind" )->click();
    CHECK( assignments == 0 );
    CHECK( errors == 1 );
    CHECK( RunningCompiler( browser ) == nullptr );
    CHECK( BrowserWidget<QLabel>( browser, "TileMaterialDetails" )->text().contains( "invalid" ) );
}

TEST_CASE( "Cancelling a pending material assignment preserves prepared assets without binding into a replacement map",
           "[CypherTools][Qt][MaterialBrowser][MaterialPreparation]" )
{
    EnsureMaterialBrowserApplication();
    if ( !ResourceCompilerAvailable() ) SKIP( "CypherResourceCompiler is unavailable beside this test target." );
    browser_fixture_t fixture;
    fixture.cookableInputs();
    fixture.material( "stone.cymat" );
    CypherTileMaterialBrowser browser;
    auto *slot = BrowserWidget<QSpinBox>( browser, "TileProjectMaterialSlot" );
    auto *use = BrowserWidget<QPushButton>( browser, "TileMaterialBind" );
    int assignments = 0, reloads = 0;
    browser.setAssignCallback( [&]( unsigned short, const QString & ) { ++assignments; } );
    browser.setReloadCallback( [&]( const QString & ) { ++reloads; } );
    slot->setValue( 23 );
    use->click();
    auto *process = RunningCompiler( browser );
    REQUIRE( process != nullptr );
    // The owner invokes this when open/new replaces the document while the
    // asynchronous resource preparation still belongs to the previous map.
    browser.cancelPendingAssignment();
    WaitForCompiler( process, true );
    CHECK( assignments == 0 );
    CHECK( reloads == 1 );
    CHECK( QFileInfo::exists( QDir( browser.cookedRoot() ).filePath( "stone.cymat_c" ) ) );
    slot->setValue( 67 );
    browser.setAssignCallback( [&]( unsigned short value, const QString &path ) {
        ++assignments;
        CHECK( value == 67u );
        CHECK( path == "stone.cymat" );
    } );
    use->click();
    CHECK( RunningCompiler( browser ) == nullptr );
    CHECK( assignments == 1 );
}
