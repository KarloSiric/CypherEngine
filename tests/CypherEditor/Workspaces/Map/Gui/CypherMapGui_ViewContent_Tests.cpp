// CypherEngine Source Code. Copyright (c) 2026 Karlo Siric. All rights reserved.
#include "CypherMapGui_Views.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include <catch2/catch_test_macros.hpp>
#include <QAction>
#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <QVBoxLayout>
#include <filesystem>
#include <memory>

using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::map;
namespace {
struct content_session_t {
    gui::editor_gui_t gui{};
    map_workspace_t workspace{};
    content_session_t() {
        REQUIRE( gui::EditorGui_Init( &gui, qobject_cast<QApplication *>( QCoreApplication::instance() ), Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
        REQUIRE( MapWorkspace_Init( &workspace, &gui ) );
        const auto path = std::filesystem::path( CYPHER_MAP_EXAMPLE_DIR ) / "facility.cymap";
        REQUIRE( MapWorkspace_Open( &workspace, QString::fromStdString( path.string() ) ).status == map_files_status_t::OK );
    }
    ~content_session_t() { MapWorkspace_Shutdown( &workspace ); gui::EditorGui_Shutdown( &gui ); }
};
struct content_source_t {
    int calls[static_cast<int>( map_view_type_t::COUNT )]{};
    map_view_type_t fail{ map_view_type_t::COUNT };
    static QWidget *Create( QWidget *parent, map_view_type_t type, void *context ) {
        auto &source = *static_cast<content_source_t *>( context );
        ++source.calls[static_cast<int>( type )];
        if ( source.fail == type ) { return nullptr; }
        auto *widget = new QWidget( parent );
        auto *layout = new QVBoxLayout( widget );
        auto *edit = new QLineEdit( QStringLiteral( "original draft" ), widget );
        layout->addWidget( edit );
        widget->setFocusProxy( edit );
        return widget;
    }
};
void ShowContent( QWidget *views ) { views->resize( 1000, 800 ); views->show(); QCoreApplication::processEvents(); }
}

TEST_CASE( "Pane content choices retain drafts and cameras without overlaying editor controls", "[map][gui][views][view-content]" )
{
    content_session_t session;
    content_source_t source;
    std::unique_ptr<QWidget> views( MapViews_Create( nullptr, &session.workspace ) );
    ShowContent( views.get() );
    QWidget *camera = MapViews_PaneView( views.get(), 0 );
    const auto position = MapCameraView_Position( camera );
    auto *choice = views->findChild<QAction *>( QStringLiteral( "EditorViewContent0_4" ) );
    REQUIRE( choice != nullptr );
    CHECK_FALSE( choice->isEnabled() );
    MapViews_SetPaneType( views.get(), 0, map_view_type_t::ASSETS );
    CHECK( MapViews_PaneView( views.get(), 0 ) == camera );
    MapViews_SetContentFactory( views.get(), &content_source_t::Create, &source );
    REQUIRE( choice->isEnabled() );
    choice->trigger(); QCoreApplication::processEvents();
    QWidget *assets = MapViews_PaneView( views.get(), 0 );
    REQUIRE( assets != camera );
    REQUIRE( assets->parentWidget() == camera->parentWidget() );
    CHECK_FALSE( assets->isWindow() );
    CHECK( MapViews_PaneTitle( views.get(), 0 ) == QStringLiteral( "Asset Manager" ) );
    auto *edit = assets->findChild<QLineEdit *>(); REQUIRE( edit != nullptr );
    edit->setText( QStringLiteral( "unsaved library filter" ) );
    MapViews_SetActivePane( views.get(), 0 );
    CHECK( edit->hasFocus() );
    CHECK( MapViews_ActivePane( views.get() ) == 0 );
    auto *header = assets->parentWidget()->findChild<QWidget *>( QStringLiteral( "EditorViewHeader" ) ); REQUIRE( header != nullptr );
    CHECK( assets->geometry().top() >= header->geometry().bottom() );
    auto *frame = views->findChild<QAction *>( QStringLiteral( "EditorViewFrameAction0" ) ); REQUIRE( frame != nullptr );
    CHECK_FALSE( frame->isVisible() );
    MapViews_SetPaneType( views.get(), 0, map_view_type_t::CAMERA );
    CHECK( MapViews_PaneView( views.get(), 0 ) == camera );
    CHECK( MapCameraView_Position( camera ).x == position.x );
    CHECK( MapCameraView_Position( camera ).y == position.y );
    CHECK( MapCameraView_Position( camera ).z == position.z );
    CHECK( frame->isVisible() );
    MapViews_SetPaneType( views.get(), 0, map_view_type_t::ASSETS );
    CHECK( MapViews_PaneView( views.get(), 0 ) == assets );
    CHECK( edit->text() == QStringLiteral( "unsaved library filter" ) );
    CHECK( source.calls[static_cast<int>( map_view_type_t::ASSETS )] == 1 );
    CHECK_FALSE( MapWorkspace_IsModified( &session.workspace ) );
}

TEST_CASE( "Content presentation restores version two and rejects unavailable sources atomically", "[map][gui][views][view-content][presentation]" )
{
    content_session_t session;
    content_source_t source;
    std::unique_ptr<QWidget> views( MapViews_Create( nullptr, &session.workspace ) );
    MapViews_SetContentFactory( views.get(), &content_source_t::Create, &source );
    ShowContent( views.get() );
    const auto legacyGeometry = QJsonDocument::fromJson( MapViews_SavePresentation( views.get() ) ).object();
    const map_view_type_t types[]{ map_view_type_t::ASSETS, map_view_type_t::DATABASE, map_view_type_t::SHADERS };
    QWidget *contents[3]{};
    for ( int i = 0; i < 3; ++i ) {
        MapViews_SetPaneType( views.get(), i, types[i] );
        contents[i] = MapViews_PaneView( views.get(), i );
        contents[i]->findChild<QLineEdit *>()->setText( QStringLiteral( "retained %1" ).arg( i ) );
    }
    const QByteArray saved = MapViews_SavePresentation( views.get() );
    REQUIRE( MapViews_ValidatePresentation( saved ) );
    CHECK( QJsonDocument::fromJson( saved ).object().value( QStringLiteral( "version" ) ).toInt() == 2 );
    for ( int i = 0; i < 3; ++i ) { MapViews_SetPaneType( views.get(), i, map_view_type_t::CAMERA ); }
    REQUIRE( MapViews_RestorePresentation( views.get(), saved ) );
    for ( int i = 0; i < 3; ++i ) {
        CHECK( MapViews_PaneType( views.get(), i ) == types[i] );
        CHECK( MapViews_PaneView( views.get(), i ) == contents[i] );
        CHECK( contents[i]->findChild<QLineEdit *>()->text() == QStringLiteral( "retained %1" ).arg( i ) );
    }
    std::unique_ptr<QWidget> other( MapViews_Create( nullptr, &session.workspace ) );
    ShowContent( other.get() );
    const QByteArray before = MapViews_SavePresentation( other.get() );
    CHECK_FALSE( MapViews_RestorePresentation( other.get(), saved ) );
    CHECK( MapViews_SavePresentation( other.get() ) == before );
    source.fail = map_view_type_t::DATABASE;
    MapViews_SetContentFactory( other.get(), &content_source_t::Create, &source );
    CHECK_FALSE( MapViews_RestorePresentation( other.get(), saved ) );
    CHECK( MapViews_SavePresentation( other.get() ) == before );
    source.fail = map_view_type_t::COUNT;
    REQUIRE( MapViews_RestorePresentation( other.get(), saved ) );
    auto legacy = legacyGeometry;
    legacy[QStringLiteral( "version" )] = 1;
    REQUIRE( MapViews_RestorePresentation( other.get(), QJsonDocument( legacy ).toJson() ) );
    CHECK( MapViews_PaneType( other.get(), 0 ) == map_view_type_t::CAMERA );
    auto invalidLegacy = QJsonDocument::fromJson( saved ).object(); invalidLegacy[QStringLiteral( "version" )] = 1;
    CHECK_FALSE( MapViews_ValidatePresentation( QJsonDocument( invalidLegacy ).toJson() ) );
}
