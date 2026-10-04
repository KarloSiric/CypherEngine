//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_DatabaseView_Tests.cpp
//  Purpose: Contract tests for the code editor (GLSL and CYKV highlighting,
//           find, indentation) and the Database View on a scratch project:
//           material rows typed by the shader's interface, overrides and
//           defaults, saving at the recipe's schema version, folder buttons
//           between tabs, the shader editor and New Shader, the texture
//           preview, the entity inspector, and generic recipes.
//
//  History:
//  - Created by Karlo Siric on 2026-10-02
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_DatabaseView.h"
#include "CypherEditorGui_AssetBrowser.h"
#include "CypherEditorGui_AssetWindow.h"
#include "CypherEditorGui_CodeEditor.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherCommon/Tier2/CypherCommon_SettingsDocument.h"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QAction>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <memory>

using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::gui;

namespace
{

void WriteFile( const QString &path, const QByteArray &contents )
{
    REQUIRE( QDir().mkpath( QFileInfo( path ).absolutePath() ) );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    REQUIRE( file.write( contents ) == contents.size() );
}

QByteArray ReadFile( const QString &path )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    return file.readAll();
}

bool HasRow( const QStringList &rows, const QString &row ) { return rows.contains( row ); }

QString RowValue( const QStringList &rows, const QString &id )
{
    for ( const QString &row : rows ) {
        if ( row.startsWith( id + QLatin1Char( '=' ) ) ) { return row.mid( id.size() + 1 ); }
    }
    return QStringLiteral( "<absent>" );
}

struct project_t {
    QApplication *pApplication{ qobject_cast<QApplication *>( QCoreApplication::instance() ) };
    editor_gui_t gui{};
    QTemporaryDir folder{};
    QString root;
    std::unique_ptr<QWidget> pBrowser{};
    std::unique_ptr<QDialog> pView{};

    project_t()
    {
        REQUIRE( pApplication != nullptr );
        REQUIRE( folder.isValid() );
        REQUIRE( EditorGui_Init( &gui, pApplication, Allocator_GetSystem() ) == editor_gui_status_t::OK );
        root = folder.filePath( QStringLiteral( "content" ) );
        // A schema 2 shader that declares what materials may set.
        WriteFile( root + QStringLiteral( "/shaders/lit.cyshader" ),
                   "@cykv 1\n@schema \"cypher.shader\" 2\n{\n"
                   "    language = \"glsl\"\n"
                   "    stages = { vertex = { source = \"shaders/lit.vert\" } fragment = { source = \"shaders/lit.frag\" } }\n"
                   "    interface = {\n"
                   "        textures = {\n"
                   "            base_color = { type = \"texture2d\" usage = \"color\" color_space = \"srgb\" required = true }\n"
                   "            normal_map = { type = \"texture2d\" usage = \"normal\" color_space = \"linear\" required = false }\n"
                   "        }\n"
                   "        parameters = {\n"
                   "            roughness = { type = \"f32\" default = 0.6 minimum = 0 maximum = 1 }\n"
                   "            tint = { type = \"color4\" default = [1, 1, 1, 1] }\n"
                   "            emissive = { type = \"bool\" default = false }\n"
                   "            model = { type = \"mat4\" required = true }\n"
                   "        }\n"
                   "    }\n"
                   "}\n" );
        WriteFile( root + QStringLiteral( "/shaders/lit.vert" ), "#version 410 core\nvoid main() { gl_Position = vec4(0.0); }\n" );
        WriteFile( root + QStringLiteral( "/shaders/lit.frag" ), "#version 410 core\nout vec4 color;\nvoid main() { color = vec4(1.0); }\n" );
        WriteFile( root + QStringLiteral( "/materials/wall.cymat" ),
                   "@cykv 1\n@schema \"cypher.material\" 2\n{\n    shader = \"shaders/lit.cyshader\"\n"
                   "    textures = { base_color = { resource = \"textures/brick.cytex\" } }\n    parameters = { tint = [1, 0.5, 0.25, 1] }\n}\n" );
        WriteFile( root + QStringLiteral( "/materials/old.cymat" ),
                   "@cykv 1\n@schema \"cypher.material\" 1\n{\n    shader = \"shaders/lit.cyshader\"\n"
                   "    textures = { base_color = \"textures/brick.cytex\" }\n    parameters = { tint = [1, 1, 1, 1] uv_scale = [2, 2] glow = true }\n}\n" );
        WriteFile( root + QStringLiteral( "/textures/brick.cytex" ),
                   "@cykv 1\n@schema \"cypher.texture\" 1\n{\n    source = \"textures/brick.png\"\n    usage = \"color\"\n    color_space = \"srgb\"\n    generate_mips = true\n}\n" );
        QImage brick( 16, 8, QImage::Format_ARGB32 );
        brick.fill( QColor( 0xB0, 0x40, 0x30, 0x80 ) );
        REQUIRE( brick.save( root + QStringLiteral( "/textures/brick.png" ) ) );
        WriteFile( root + QStringLiteral( "/sounds/door.cysnd" ), "@cykv 1\n@schema \"cypher.sound\" 1\n{\n    source = \"sounds/door.wav\"\n    volume = 0.8\n    loop = false\n}\n" );
        WriteFile( root + QStringLiteral( "/sounds/door.wav" ), "RIFF" );
        pBrowser.reset( EditorAssetBrowser_Create( nullptr, &gui ) );
        EditorAssetBrowser_SetRoots( pBrowser.get(), { root } );
        pView.reset( EditorDatabaseView_Create( nullptr, &gui, pBrowser.get() ) );
        REQUIRE( pView != nullptr );
    }
    ~project_t()
    {
        pView.reset();
        pBrowser.reset();
        EditorGui_Shutdown( &gui );
    }
};

// The source deliberately enumerates live records on every callback, as
// Mason does after a map is replaced or an entity changes.
struct library_entities_t {
    settings_document_t store{};
    u64 selected{ 0u };

    library_entities_t()
    {
        const settings_document_identity_t identity{ StringView_FromCString( "test.library_entities" ), 1u, 1u };
        REQUIRE( SettingsDocument_Init( &store, Allocator_GetSystem(), identity ) == settings_document_status_t::OK );
        Load( "@cykv 1\n@schema \"test.library_entities\" 1\n{ entities = [\n"
              "{ id = 7u class = \"light_spot\" name = \"lamp\" origin = [64, 32, 128] },\n"
              "{ id = 8u class = \"Trigger_Once\" name = \"start_trigger\" origin = [1, 2, 3] properties = { delay = 1 } },\n"
              "{ id = 9u class = \"triggerish\" name = \"not_a_trigger\" },\n"
              "{ id = 10u class = \"logic_script\" name = \"waves\" },\n"
              "{ id = 11u class = \"logic_relay\" name = \"script_path\" properties = { script_path = \"scripts/relay.cfg\" } },\n"
              "{ id = 12u class = \"scripted_sequence\" name = \"intro\" },\n"
              "{ id = 13u class = \"logic_relay\" name = \"sequence_path\" properties = { sequence_path = \"sequences/intro.cycine\" } },\n"
              "{ id = 14u class = \"logic_relay\" name = \"empty_properties\" properties = { script = \"\" script_path = \"\" vscripts = \"\" sequence = \"\" sequence_path = \"\" } },\n"
              "{ id = 15u class = \"logic_relay\" name = \"vscripts\" properties = { vscripts = \"waves\" } },\n"
              "{ id = 16u class = \"logic_relay\" name = \"sequence\" properties = { sequence = \"intro\" } },\n"
              "{ id = 18u class = \"logic_relay\" name = \"script\" properties = { script = \"scripts/waves.cfg\" } }\n"
              "] }\n" );
    }
    ~library_entities_t() { SettingsDocument_Shutdown( &store ); }

    void Load( const QByteArray &text )
    {
        REQUIRE( SettingsDocument_Load( &store, string_view_t{ text.constData(), static_cast<usize>( text.size() ) } ).status == settings_document_status_t::OK );
    }

    const key_value_t *Records() const { return KeyValue_Find( SettingsDocument_Root( &store ), StringView_FromCString( "entities" ) ); }

    static QString Text( const key_value_t *pRecord, const char *pKey )
    {
        string_view_t text{};
        if ( !KeyValue_GetString( KeyValue_Find( pRecord, StringView_FromCString( pKey ) ), &text ) ) { return {}; }
        return QString::fromUtf8( text.pData, static_cast<qsizetype>( text.cchLength ) );
    }

    static u64 Id( const key_value_t *pRecord )
    {
        u64 id = 0u;
        ( void )KeyValue_GetU64( KeyValue_Find( pRecord, StringView_FromCString( "id" ) ), &id );
        return id;
    }

    static QVector<editor_database_entity_t> Entities( void *pContext )
    {
        const key_value_t *pRecords = static_cast<library_entities_t *>( pContext )->Records();
        QVector<editor_database_entity_t> entities;
        for ( usize i = 0u; i < KeyValue_ChildCount( pRecords ); ++i ) {
            const key_value_t *pRecord = KeyValue_ChildAt( pRecords, i );
            entities.append( editor_database_entity_t{ Id( pRecord ), Text( pRecord, "class" ), Text( pRecord, "name" ) } );
        }
        return entities;
    }

    static const key_value_t *Record( void *pContext, u64 id )
    {
        const key_value_t *pRecords = static_cast<library_entities_t *>( pContext )->Records();
        for ( usize i = 0u; i < KeyValue_ChildCount( pRecords ); ++i ) {
            const key_value_t *pRecord = KeyValue_ChildAt( pRecords, i );
            if ( Id( pRecord ) == id ) { return pRecord; }
        }
        return nullptr;
    }

    static void GoTo( void *pContext, u64 id ) { static_cast<library_entities_t *>( pContext )->selected = id; }
    void Connect( QWidget *pView ) { EditorDatabaseView_SetEntitySource( pView, &Entities, &Record, &GoTo, this ); }
};

void AddLibraryFiles( project_t &p )
{
    // These pages inspect catalogued files without requiring a mesh, font,
    // animation, or map loader to accept their contents.
    WriteFile( p.root + QStringLiteral( "/models/crate.cymesh" ), "mesh recipe fixture\n" );
    WriteFile( p.root + QStringLiteral( "/models/crate.obj" ), "o crate\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n" );
    WriteFile( p.root + QStringLiteral( "/maps/arena.cymap" ), "map source fixture\n" );
    WriteFile( p.root + QStringLiteral( "/fonts/interface.cyfont" ), "font recipe fixture\n" );
    WriteFile( p.root + QStringLiteral( "/fonts/interface.ttf" ), QByteArray::fromHex( "000100000000000000000000" ) );
    WriteFile( p.root + QStringLiteral( "/animations/idle.cyanim" ), "animation recipe fixture\n" );
    EditorAssetBrowser_Rescan( p.pBrowser.get() );
    EditorDatabaseView_Refresh( p.pView.get() );
}

QTreeWidgetItem *LibraryEntry( QTreeWidget *pLibrary, const QString &path )
{
    for ( QTreeWidgetItemIterator it( pLibrary ); *it != nullptr; ++it ) {
        if ( ( *it )->data( 0, Qt::UserRole ).toString() == path ) { return *it; }
    }
    return nullptr;
}

void ActivateLibraryEntry( QWidget *pView, int tab, const QString &path )
{
    EditorDatabaseView_SetTab( pView, tab );
    auto *pLibrary = pView->findChild<QTreeWidget *>( QStringLiteral( "DatabaseLibrary%1" ).arg( tab ) );
    REQUIRE( pLibrary != nullptr );
    QTreeWidgetItem *pItem = LibraryEntry( pLibrary, path );
    REQUIRE( pItem != nullptr );
    pLibrary->setCurrentItem( pItem );
    pLibrary->itemActivated( pItem, 0 );
}

} // namespace

TEST_CASE( "The code editor highlights GLSL and CYKV and keeps indentation", "[editor][gui][code-editor]" )
{
    project_t p;
    std::unique_ptr<QWidget> pEditor( EditorCodeEditor_Create( nullptr, &p.gui.style, editor_code_language_t::GLSL ) );
    REQUIRE( pEditor != nullptr );
    EditorCodeEditor_SetText( pEditor.get(), QStringLiteral( "#version 410 core\n"
                                                             "uniform sampler2D base_color; // the albedo\n"
                                                             "/* a block\n"
                                                             "   comment */ float x = 1.5;\n"
                                                             "void main() { gl_Position = texture(base_color, vec2(0.5)); }\n" ) );
    CHECK_FALSE( EditorCodeEditor_IsModified( pEditor.get() ) );
    CHECK( EditorCodeEditor_CategoryAt( pEditor.get(), 1, 1 ) == QStringLiteral( "preprocessor" ) );
    CHECK( EditorCodeEditor_CategoryAt( pEditor.get(), 2, 0 ) == QStringLiteral( "keyword" ) );   // uniform
    CHECK( EditorCodeEditor_CategoryAt( pEditor.get(), 2, 9 ) == QStringLiteral( "type" ) );      // sampler2D
    CHECK( EditorCodeEditor_CategoryAt( pEditor.get(), 2, 33 ) == QStringLiteral( "comment" ) );
    CHECK( EditorCodeEditor_CategoryAt( pEditor.get(), 3, 4 ) == QStringLiteral( "comment" ) );
    CHECK( EditorCodeEditor_CategoryAt( pEditor.get(), 4, 3 ) == QStringLiteral( "comment" ) );   // Still inside /* */.
    CHECK( EditorCodeEditor_CategoryAt( pEditor.get(), 4, 14 ) == QStringLiteral( "type" ) );     // float, after the comment closes.
    CHECK( EditorCodeEditor_CategoryAt( pEditor.get(), 4, 24 ) == QStringLiteral( "number" ) );   // 1.5
    CHECK( EditorCodeEditor_CategoryAt( pEditor.get(), 5, 14 ) == QStringLiteral( "builtin" ) );  // gl_Position
    // Find wraps; Go to Line moves the cursor.
    CHECK( EditorCodeEditor_Find( pEditor.get(), QStringLiteral( "base_color" ) ) );
    CHECK( EditorCodeEditor_Find( pEditor.get(), QStringLiteral( "base_color" ) ) );
    CHECK( EditorCodeEditor_Find( pEditor.get(), QStringLiteral( "base_color" ) ) ); // Wrapped back to the first.
    CHECK_FALSE( EditorCodeEditor_Find( pEditor.get(), QStringLiteral( "nowhere_to_be_found" ) ) );
    REQUIRE( EditorCodeEditor_GoToLine( pEditor.get(), 5 ) );
    QPlainTextEdit *pText = EditorCodeEditor_Text( pEditor.get() );
    CHECK( pText->textCursor().blockNumber() == 4 );
    // Enter after an opening brace indents one level more.
    EditorCodeEditor_SetText( pEditor.get(), QStringLiteral( "    void f() {" ) );
    QTextCursor cursor = pText->textCursor();
    cursor.movePosition( QTextCursor::End );
    pText->setTextCursor( cursor );
    QKeyEvent enter( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier );
    QCoreApplication::sendEvent( pText, &enter );
    CHECK( pText->document()->findBlockByNumber( 1 ).text() == QStringLiteral( "        " ) );
    CHECK( EditorCodeEditor_IsModified( pEditor.get() ) );

    // CYKV: directives, keys, strings, numbers.
    EditorCodeEditor_SetLanguage( pEditor.get(), editor_code_language_t::CYKV );
    EditorCodeEditor_SetText( pEditor.get(), QStringLiteral( "@schema \"cypher.material\" 1\n{ shader = \"shaders/lit.cyshader\" roughness = 0.5 }\n" ) );
    CHECK( EditorCodeEditor_CategoryAt( pEditor.get(), 1, 0 ) == QStringLiteral( "preprocessor" ) );
    CHECK( EditorCodeEditor_CategoryAt( pEditor.get(), 2, 3 ) == QStringLiteral( "key" ) );
    CHECK( EditorCodeEditor_CategoryAt( pEditor.get(), 2, 13 ) == QStringLiteral( "string" ) );
    CHECK( EditorCodeEditor_CategoryAt( pEditor.get(), 2, 47 ) == QStringLiteral( "number" ) ); // 0.5
}

TEST_CASE( "Material rows follow the shader's declared interface and edit the recipe", "[editor][gui][database]" )
{
    project_t p;
    QDialog *pView = p.pView.get();
    EditorDatabaseView_SetTab( pView, DATABASE_TAB_MATERIALS );
    CHECK( EditorDatabaseView_Library( pView ) == ( QStringList{ QStringLiteral( "materials/old.cymat" ), QStringLiteral( "materials/wall.cymat" ) } ) );
    REQUIRE( EditorDatabaseView_Open( pView, QStringLiteral( "materials/wall.cymat" ) ) );
    CHECK( EditorDatabaseView_Current( pView ) == QStringLiteral( "materials/wall.cymat" ) );
    QStringList rows = EditorDatabaseView_Properties( pView );
    CHECK( RowValue( rows, QStringLiteral( "Material Settings/Shader" ) ) == QStringLiteral( "shaders/lit.cyshader" ) );
    CHECK( RowValue( rows, QStringLiteral( "Opacity and State/Alpha mode" ) ) == QStringLiteral( "opaque" ) ); // The default.
    // Every slot the shader declares, set or not.
    CHECK( RowValue( rows, QStringLiteral( "Texture Maps/base_color" ) ) == QStringLiteral( "textures/brick.cytex" ) );
    CHECK( RowValue( rows, QStringLiteral( "Texture Maps/normal_map" ) ).isEmpty() );
    // Declared parameters show their defaults; matrices are the engine's, not the material's.
    CHECK( RowValue( rows, QStringLiteral( "Shader Params/roughness" ) ) == QStringLiteral( "0.6" ) );
    CHECK( RowValue( rows, QStringLiteral( "Shader Params/tint" ) ) == QStringLiteral( "1 0.5 0.25 1" ) );
    CHECK( RowValue( rows, QStringLiteral( "Shader Params/emissive" ) ) == QStringLiteral( "false" ) );
    CHECK( RowValue( rows, QStringLiteral( "Shader Params/model" ) ) == QStringLiteral( "<absent>" ) );

    // Bounded values clamp; typed values parse; a bad value is refused.
    CHECK_FALSE( EditorDatabaseView_IsModified( pView ) );
    CHECK( EditorDatabaseView_SetProperty( pView, QStringLiteral( "Shader Params/roughness" ), QStringLiteral( "1.7" ) ) );
    CHECK( RowValue( EditorDatabaseView_Properties( pView ), QStringLiteral( "Shader Params/roughness" ) ) == QStringLiteral( "1" ) );
    CHECK( EditorDatabaseView_SetProperty( pView, QStringLiteral( "Shader Params/roughness" ), QStringLiteral( "0.25" ) ) );
    CHECK( EditorDatabaseView_SetProperty( pView, QStringLiteral( "Shader Params/emissive" ), QStringLiteral( "true" ) ) );
    CHECK( EditorDatabaseView_SetProperty( pView, QStringLiteral( "Shader Params/tint" ), QStringLiteral( "1 0 0 1" ) ) );
    CHECK_FALSE( EditorDatabaseView_SetProperty( pView, QStringLiteral( "Shader Params/tint" ), QStringLiteral( "red" ) ) );
    CHECK( EditorDatabaseView_SetProperty( pView, QStringLiteral( "Opacity and State/Alpha mode" ), QStringLiteral( "mask" ) ) );
    CHECK_FALSE( EditorDatabaseView_SetProperty( pView, QStringLiteral( "Opacity and State/Alpha mode" ), QStringLiteral( "glass" ) ) );
    CHECK( EditorDatabaseView_IsModified( pView ) );
    // Revert reads the file again.
    REQUIRE( EditorDatabaseView_Revert( pView ) );
    CHECK_FALSE( EditorDatabaseView_IsModified( pView ) );
    CHECK( RowValue( EditorDatabaseView_Properties( pView ), QStringLiteral( "Shader Params/roughness" ) ) == QStringLiteral( "0.6" ) );

    // Save writes the recipe at its own schema version.
    REQUIRE( EditorDatabaseView_SetProperty( pView, QStringLiteral( "Shader Params/roughness" ), QStringLiteral( "0.25" ) ) );
    REQUIRE( EditorDatabaseView_SetProperty( pView, QStringLiteral( "Texture Maps/normal_map" ), QStringLiteral( "textures/brick.cytex" ) ) );
    REQUIRE( EditorDatabaseView_Save( pView ) );
    CHECK_FALSE( EditorDatabaseView_IsModified( pView ) );
    const QByteArray saved = ReadFile( p.root + QStringLiteral( "/materials/wall.cymat" ) );
    CHECK( saved.contains( "@schema \"cypher.material\" 2" ) );
    CHECK( saved.contains( "roughness" ) );
    CHECK( saved.contains( "normal_map" ) );
    REQUIRE( EditorDatabaseView_Open( pView, QStringLiteral( "materials/old.cymat" ) ) );
    REQUIRE( EditorDatabaseView_Open( pView, QStringLiteral( "materials/wall.cymat" ) ) );
    rows = EditorDatabaseView_Properties( pView );
    CHECK( RowValue( rows, QStringLiteral( "Shader Params/roughness" ) ) == QStringLiteral( "0.25" ) );
    CHECK( RowValue( rows, QStringLiteral( "Texture Maps/normal_map" ) ) == QStringLiteral( "textures/brick.cytex" ) );
    // Returning an override to the shader's default removes it.
    REQUIRE( EditorDatabaseView_ResetProperty( pView, QStringLiteral( "Shader Params/roughness" ) ) );
    CHECK( RowValue( EditorDatabaseView_Properties( pView ), QStringLiteral( "Shader Params/roughness" ) ) == QStringLiteral( "0.6" ) );
    REQUIRE( EditorDatabaseView_ResetProperty( pView, QStringLiteral( "Texture Maps/normal_map" ) ) );
    REQUIRE( EditorDatabaseView_Save( pView ) );
    CHECK_FALSE( ReadFile( p.root + QStringLiteral( "/materials/wall.cymat" ) ).contains( "roughness" ) );
    CHECK_FALSE( ReadFile( p.root + QStringLiteral( "/materials/wall.cymat" ) ).contains( "normal_map" ) );
}

TEST_CASE( "A schema 1 material is typed by its own values and stays schema 1", "[editor][gui][database]" )
{
    project_t p;
    QDialog *pView = p.pView.get();
    REQUIRE( EditorDatabaseView_Open( pView, QStringLiteral( "materials/old.cymat" ) ) );
    const QStringList rows = EditorDatabaseView_Properties( pView );
    CHECK( RowValue( rows, QStringLiteral( "Texture Maps/base_color" ) ) == QStringLiteral( "textures/brick.cytex" ) );
    CHECK( RowValue( rows, QStringLiteral( "Shader Params/uv_scale" ) ) == QStringLiteral( "2 2" ) );
    CHECK( RowValue( rows, QStringLiteral( "Shader Params/glow" ) ) == QStringLiteral( "true" ) );
    CHECK( RowValue( rows, QStringLiteral( "Opacity and State/Alpha mode" ) ) == QStringLiteral( "<absent>" ) ); // Schema 2 only.
    CHECK_FALSE( EditorDatabaseView_SetProperty( pView, QStringLiteral( "Shader Params/uv_scale" ), QStringLiteral( "1 2 3" ) ) ); // Two components.
    REQUIRE( EditorDatabaseView_SetProperty( pView, QStringLiteral( "Shader Params/uv_scale" ), QStringLiteral( "4 4" ) ) );
    REQUIRE( EditorDatabaseView_Save( pView ) );
    const QByteArray saved = ReadFile( p.root + QStringLiteral( "/materials/old.cymat" ) );
    CHECK( saved.contains( "@schema \"cypher.material\" 1" ) );
    CHECK( saved.contains( "base_color = \"textures/brick.cytex\"" ) ); // Schema 1 texture form kept.
}

TEST_CASE( "Folder buttons open the shader and texture a material names, and they list their users", "[editor][gui][database]" )
{
    project_t p;
    QDialog *pView = p.pView.get();
    REQUIRE( EditorDatabaseView_Open( pView, QStringLiteral( "materials/wall.cymat" ) ) );
    REQUIRE( EditorDatabaseView_OpenRow( pView, QStringLiteral( "Texture Maps/base_color" ) ) );
    CHECK( EditorDatabaseView_Tab( pView ) == DATABASE_TAB_TEXTURES );
    CHECK( EditorDatabaseView_Current( pView ) == QStringLiteral( "textures/brick.cytex" ) );
    const QStringList textureRows = EditorDatabaseView_Properties( pView );
    CHECK( RowValue( textureRows, QStringLiteral( "Texture Settings/Usage" ) ) == QStringLiteral( "color" ) );
    CHECK( RowValue( textureRows, QStringLiteral( "Texture Settings/Generate mips" ) ) == QStringLiteral( "true" ) );
    CHECK( EditorDatabaseView_UsedBy( pView ) == ( QStringList{ QStringLiteral( "materials/old.cymat" ), QStringLiteral( "materials/wall.cymat" ) } ) );
    // The preview is the source image; one channel shows as grey.
    CHECK( EditorDatabaseView_PreviewImage( pView ).size() == QSize( 16, 8 ) );
    EditorDatabaseView_SetChannel( pView, QStringLiteral( "a" ) );
    const QColor alpha = EditorDatabaseView_PreviewImage( pView ).pixelColor( 0, 0 );
    CHECK( alpha.red() == 0x80 );
    CHECK( alpha.green() == 0x80 );
    EditorDatabaseView_SetChannel( pView, QStringLiteral( "rgb" ) );

    REQUIRE( EditorDatabaseView_Open( pView, QStringLiteral( "materials/wall.cymat" ) ) );
    REQUIRE( EditorDatabaseView_OpenRow( pView, QStringLiteral( "Material Settings/Shader" ) ) );
    CHECK( EditorDatabaseView_Tab( pView ) == DATABASE_TAB_SHADERS );
    CHECK( EditorDatabaseView_Current( pView ) == QStringLiteral( "shaders/lit.cyshader" ) );
    REQUIRE( EditorDatabaseView_CodeEditorCount( pView ) == 3 ); // The recipe and both stages.
    CHECK( EditorCodeEditor_TextOf( EditorDatabaseView_CodeEditor( pView, 2 ) ).contains( QStringLiteral( "out vec4 color" ) ) );
    CHECK( EditorDatabaseView_UsedBy( pView ).size() == 2 );
    const QStringList shaderRows = EditorDatabaseView_Properties( pView );
    CHECK( RowValue( shaderRows, QStringLiteral( "Stages/fragment source" ) ) == QStringLiteral( "shaders/lit.frag" ) );
    CHECK( RowValue( shaderRows, QStringLiteral( "Interface/param roughness" ) ) == QStringLiteral( "f32 = 0.6 in [0, 1]" ) );
    // The stage folder shows that stage's editor.
    CHECK( EditorDatabaseView_OpenRow( pView, QStringLiteral( "Stages/vertex source" ) ) );
}

TEST_CASE( "The shader editor saves stages and recipe edits, and New Shader starts from a template", "[editor][gui][database][shader]" )
{
    project_t p;
    QDialog *pView = p.pView.get();
    REQUIRE( EditorDatabaseView_Open( pView, QStringLiteral( "shaders/lit.cyshader" ) ) );
    QWidget *pFragment = EditorDatabaseView_CodeEditor( pView, 2 );
    REQUIRE( pFragment != nullptr );
    EditorCodeEditor_Text( pFragment )->appendPlainText( QStringLiteral( "// tuned" ) );
    CHECK( EditorDatabaseView_IsModified( pView ) );
    // A grid edit rewrites the recipe tab, so the two agree.
    REQUIRE( EditorDatabaseView_SetProperty( pView, QStringLiteral( "Shader Settings/Defines" ), QStringLiteral( "CY_FOG, CY_WORLD" ) ) );
    CHECK( EditorCodeEditor_TextOf( EditorDatabaseView_CodeEditor( pView, 0 ) ).contains( QStringLiteral( "CY_FOG" ) ) );
    REQUIRE( EditorDatabaseView_Save( pView ) );
    CHECK_FALSE( EditorDatabaseView_IsModified( pView ) );
    CHECK( ReadFile( p.root + QStringLiteral( "/shaders/lit.frag" ) ).contains( "// tuned" ) );
    const QByteArray recipe = ReadFile( p.root + QStringLiteral( "/shaders/lit.cyshader" ) );
    CHECK( recipe.contains( "CY_FOG" ) );
    CHECK( recipe.contains( "@schema \"cypher.shader\" 2" ) );
    CHECK( recipe.contains( "roughness" ) ); // The interface survives a grid edit.

    QString path;
    CHECK_FALSE( EditorDatabaseView_NewShader( pView, QStringLiteral( "Bad Name" ) ) );
    REQUIRE( EditorDatabaseView_NewShader( pView, QStringLiteral( "glass" ), &path ) );
    CHECK( path == QStringLiteral( "shaders/glass.cyshader" ) );
    CHECK( EditorDatabaseView_Current( pView ) == path );
    CHECK( QFileInfo::exists( p.root + QStringLiteral( "/shaders/glass.vert" ) ) );
    CHECK( QFileInfo::exists( p.root + QStringLiteral( "/shaders/glass.frag" ) ) );
    REQUIRE( EditorDatabaseView_CodeEditorCount( pView ) == 3 );
    CHECK( EditorCodeEditor_TextOf( EditorDatabaseView_CodeEditor( pView, 1 ) ).startsWith( QStringLiteral( "#version 410 core" ) ) );
    CHECK( EditorDatabaseView_Library( pView ).contains( path ) );
    CHECK_FALSE( EditorDatabaseView_NewShader( pView, QStringLiteral( "glass" ) ) ); // Exists.
}

TEST_CASE( "New Shader preserves existing standalone stage files", "[editor][gui][database][shader]" )
{
    project_t p;
    REQUIRE( EditorDatabaseView_Open( p.pView.get(), QStringLiteral( "shaders/lit.cyshader" ) ) );
    QString extension;
    SECTION( "An existing vertex stage blocks creation" ) { extension = QStringLiteral( "vert" ); }
    SECTION( "An existing fragment stage blocks creation" ) { extension = QStringLiteral( "frag" ); }
    const QString stage = p.root + QStringLiteral( "/shaders/orphan.%1" ).arg( extension );
    const QByteArray original( "#version 410 core\n// Authored source: preserve this file.\n" );
    WriteFile( stage, original );
    QString output = QStringLiteral( "unchanged" );
    CHECK_FALSE( EditorDatabaseView_NewShader( p.pView.get(), QStringLiteral( "orphan" ), &output ) );
    CHECK( ReadFile( stage ) == original );
    CHECK_FALSE( QFileInfo::exists( p.root + QStringLiteral( "/shaders/orphan.cyshader" ) ) );
    const QString other = extension == QStringLiteral( "vert" ) ? QStringLiteral( "frag" ) : QStringLiteral( "vert" );
    CHECK_FALSE( QFileInfo::exists( p.root + QStringLiteral( "/shaders/orphan.%1" ).arg( other ) ) );
    CHECK( output == QStringLiteral( "unchanged" ) );
    CHECK( EditorDatabaseView_Current( p.pView.get() ) == QStringLiteral( "shaders/lit.cyshader" ) );
}

TEST_CASE( "Raw shader recipe edits cannot be overwritten by property edits", "[editor][gui][database][shader]" )
{
    project_t p;
    QDialog *pView = p.pView.get();
    REQUIRE( EditorDatabaseView_Open( pView, QStringLiteral( "shaders/lit.cyshader" ) ) );
    // Property editing already marks the recipe tab modified. A subsequent
    // raw edit must still disable properties even without a new dirty-state signal.
    REQUIRE( EditorDatabaseView_SetProperty( pView, QStringLiteral( "Shader Settings/Defines" ), QStringLiteral( "CY_FOG" ) ) );
    QWidget *pRecipe = EditorDatabaseView_CodeEditor( pView, 0 );
    REQUIRE( pRecipe != nullptr );
    REQUIRE( EditorCodeEditor_IsModified( pRecipe ) );
    QString source = EditorCodeEditor_TextOf( pRecipe );
    const QString language = QStringLiteral( "language = \"glsl\"" );
    REQUIRE( source.contains( language ) );
    source.replace( language, language + QStringLiteral( "\n    author_note = \"preserve this raw edit\"" ) );
    QTextCursor cursor = EditorCodeEditor_Text( pRecipe )->textCursor();
    cursor.select( QTextCursor::Document );
    cursor.insertText( source );
    REQUIRE( EditorCodeEditor_IsModified( pRecipe ) );
    auto *pGrid = pView->findChild<QTreeWidget *>( QStringLiteral( "DatabaseGrid1" ) );
    REQUIRE( pGrid != nullptr );
    CHECK_FALSE( pGrid->isEnabled() );
    CHECK( pGrid->toolTip().contains( QStringLiteral( "Save or Revert" ) ) );
    CHECK_FALSE( EditorDatabaseView_SetProperty( pView, QStringLiteral( "Shader Settings/Defines" ), QStringLiteral( "CY_WORLD" ) ) );
    CHECK( EditorCodeEditor_TextOf( pRecipe ) == source );
    CHECK( EditorDatabaseView_IsModified( pView ) );
    SECTION( "Saving preserves raw fields and resumes property editing" ) {
        REQUIRE( EditorDatabaseView_Save( pView ) );
        CHECK( ReadFile( p.root + QStringLiteral( "/shaders/lit.cyshader" ) ).contains( "preserve this raw edit" ) );
        CHECK( pGrid->isEnabled() );
        REQUIRE( EditorDatabaseView_SetProperty( pView, QStringLiteral( "Shader Settings/Defines" ), QStringLiteral( "CY_WORLD" ) ) );
        REQUIRE( EditorDatabaseView_Save( pView ) );
        const QByteArray saved = ReadFile( p.root + QStringLiteral( "/shaders/lit.cyshader" ) );
        CHECK( saved.contains( "preserve this raw edit" ) );
        CHECK( saved.contains( "CY_WORLD" ) );
    }
    SECTION( "Reverting restores the disk recipe and resumes property editing" ) {
        REQUIRE( EditorDatabaseView_Revert( pView ) );
        CHECK( pGrid->isEnabled() );
        CHECK_FALSE( EditorCodeEditor_TextOf( EditorDatabaseView_CodeEditor( pView, 0 ) ).contains( QStringLiteral( "preserve this raw edit" ) ) );
        REQUIRE( EditorDatabaseView_SetProperty( pView, QStringLiteral( "Shader Settings/Defines" ), QStringLiteral( "CY_WORLD" ) ) );
    }
}

TEST_CASE( "Invalid shader recipe saves preserve files and recoverable edits", "[editor][gui][database][shader]" )
{
    project_t p;
    QDialog *pView = p.pView.get();
    REQUIRE( EditorDatabaseView_Open( pView, QStringLiteral( "shaders/lit.cyshader" ) ) );
    const QString recipeFile = p.root + QStringLiteral( "/shaders/lit.cyshader" );
    const QString fragmentFile = p.root + QStringLiteral( "/shaders/lit.frag" );
    const QByteArray originalRecipe = ReadFile( recipeFile );
    const QByteArray originalFragment = ReadFile( fragmentFile );
    QString invalid = QString::fromUtf8( originalRecipe );
    SECTION( "A syntax error blocks all writes" ) { invalid.chop( 2 ); }
    SECTION( "A different recipe schema blocks all writes" ) { invalid.replace( QStringLiteral( "cypher.shader" ), QStringLiteral( "cypher.material" ) ); }
    SECTION( "An unsupported schema version blocks all writes" ) { invalid.replace( QStringLiteral( "@schema \"cypher.shader\" 2" ), QStringLiteral( "@schema \"cypher.shader\" 99" ) ); }
    QWidget *pRecipe = EditorDatabaseView_CodeEditor( pView, 0 );
    QWidget *pFragment = EditorDatabaseView_CodeEditor( pView, 2 );
    REQUIRE( pRecipe != nullptr );
    REQUIRE( pFragment != nullptr );
    QTextCursor cursor = EditorCodeEditor_Text( pRecipe )->textCursor();
    cursor.select( QTextCursor::Document );
    cursor.insertText( invalid );
    EditorCodeEditor_Text( pFragment )->appendPlainText( QStringLiteral( "// unsaved stage edit" ) );
    const QString editedFragment = EditorCodeEditor_TextOf( pFragment );
    auto *pSave = pView->findChild<QToolButton *>( QStringLiteral( "DatabaseSave" ) );
    auto *pRevert = pView->findChild<QToolButton *>( QStringLiteral( "DatabaseRevert" ) );
    REQUIRE( pSave != nullptr );
    REQUIRE( pRevert != nullptr );
    REQUIRE_FALSE( EditorDatabaseView_Save( pView ) );
    CHECK( ReadFile( recipeFile ) == originalRecipe );
    CHECK( ReadFile( fragmentFile ) == originalFragment );
    CHECK( EditorCodeEditor_TextOf( pRecipe ) == invalid );
    CHECK( EditorCodeEditor_TextOf( pFragment ) == editedFragment );
    CHECK( EditorCodeEditor_IsModified( pRecipe ) );
    CHECK( EditorCodeEditor_IsModified( pFragment ) );
    CHECK( pSave->isEnabled() );
    CHECK( pRevert->isEnabled() );
    // Correcting the source resumes the same Save workflow without discarding
    // the independently edited stage or reopening the asset.
    cursor = EditorCodeEditor_Text( pRecipe )->textCursor();
    cursor.select( QTextCursor::Document );
    cursor.insertText( QString::fromUtf8( originalRecipe ) );
    REQUIRE( EditorDatabaseView_Save( pView ) );
    CHECK( ReadFile( recipeFile ) == originalRecipe );
    CHECK( ReadFile( fragmentFile ).contains( "// unsaved stage edit" ) );
    CHECK_FALSE( EditorDatabaseView_IsModified( pView ) );
}

TEST_CASE( "The entity inspector shows a record's transform and keys, and generic recipes edit as a tree", "[editor][gui][database]" )
{
    project_t p;
    QDialog *pView = p.pView.get();
    // A stand-in map: one entity record.
    settings_document_t store;
    const settings_document_identity_t identity{ StringView_FromCString( "test.entity" ), 1u, 1u };
    REQUIRE( SettingsDocument_Init( &store, Allocator_GetSystem(), identity ) == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &store, StringView_FromCString( "@cykv 1\n@schema \"test.entity\" 1\n{ id = 7u class = \"light_spot\" name = \"lamp\" "
                                                                   "origin = [64, 32, 128] angles = [0, 90, -45] brightness = 300 }\n" ) ).status == settings_document_status_t::OK );
    struct source_t {
        settings_document_t *pStore;
        u64 selected{ 0u };
    } source{ &store };
    EditorDatabaseView_SetEntitySource(
        pView,
        []( void * ) { return QVector<editor_database_entity_t>{ { 7u, QStringLiteral( "light_spot" ), QStringLiteral( "lamp" ) } }; },
        []( void *pContext, u64 id ) -> const key_value_t * { return id == 7u ? SettingsDocument_Root( static_cast<source_t *>( pContext )->pStore ) : nullptr; },
        []( void *pContext, u64 id ) { static_cast<source_t *>( pContext )->selected = id; },
        &source );
    REQUIRE( EditorDatabaseView_OpenEntity( pView, 7u ) );
    CHECK( EditorDatabaseView_Tab( pView ) == DATABASE_TAB_ENTITIES );
    CHECK( EditorDatabaseView_Library( pView ) == QStringList{ QStringLiteral( "#7" ) } );
    const QStringList rows = EditorDatabaseView_Properties( pView );
    CHECK( HasRow( rows, QStringLiteral( "Entity/origin=64 32 128" ) ) );
    CHECK( HasRow( rows, QStringLiteral( "Entity/angles=0 90 -45" ) ) );
    CHECK( HasRow( rows, QStringLiteral( "Entity/brightness=300" ) ) );
    CHECK_FALSE( EditorDatabaseView_SetProperty( pView, QStringLiteral( "Entity/origin" ), QStringLiteral( "0 0 0" ) ) ); // Read only.
    CHECK_FALSE( EditorDatabaseView_OpenEntity( pView, 99u ) );

    // A sound recipe: every scalar is a row.
    REQUIRE( EditorDatabaseView_Open( pView, QStringLiteral( "sounds/door.cysnd" ) ) );
    CHECK( EditorDatabaseView_Tab( pView ) == DATABASE_TAB_SOUNDS );
    const QStringList sound = EditorDatabaseView_Properties( pView );
    CHECK( RowValue( sound, QStringLiteral( "Recipe/volume" ) ) == QStringLiteral( "0.8" ) );
    CHECK( RowValue( sound, QStringLiteral( "Recipe/loop" ) ) == QStringLiteral( "false" ) );
    REQUIRE( EditorDatabaseView_SetProperty( pView, QStringLiteral( "Recipe/volume" ), QStringLiteral( "0.5" ) ) );
    REQUIRE( EditorDatabaseView_Save( pView ) );
    CHECK( ReadFile( p.root + QStringLiteral( "/sounds/door.cysnd" ) ).contains( "0.5" ) );
    CHECK_FALSE( EditorDatabaseView_Open( pView, QStringLiteral( "maps/nothing.cymap" ) ) );
    CHECK_FALSE( EditorDatabaseView_Open( pView, QStringLiteral( "materials/missing.cymat" ) ) );
    SettingsDocument_Shutdown( &store );
}

TEST_CASE( "An embedded database belongs to its pane and Shader View stays on shaders", "[editor][gui][database][embedded]" )
{
    project_t p;
    QWidget host;
    CHECK( EditorDatabaseView_CreateEmbedded( nullptr, &p.gui, p.pBrowser.get() ) == nullptr );
    CHECK( EditorDatabaseView_CreateEmbedded( &host, nullptr, p.pBrowser.get() ) == nullptr );
    CHECK( EditorDatabaseView_CreateEmbedded( &host, &p.gui, p.pBrowser.get(), DATABASE_TAB_COUNT ) == nullptr );
    QWidget *pDatabase = EditorDatabaseView_CreateEmbedded( &host, &p.gui, p.pBrowser.get(), DATABASE_TAB_TEXTURES );
    REQUIRE( pDatabase != nullptr );
    CHECK( pDatabase->parentWidget() == &host );
    CHECK_FALSE( pDatabase->isWindow() );
    CHECK( ( pDatabase->windowFlags() & Qt::WindowType_Mask ) == Qt::Widget );
    CHECK( EditorDatabaseView_Tab( pDatabase ) == DATABASE_TAB_TEXTURES );
    EditorDatabaseView_SetTab( pDatabase, DATABASE_TAB_SOUNDS );
    CHECK( EditorDatabaseView_Tab( pDatabase ) == DATABASE_TAB_SOUNDS );

    QWidget *pShader = EditorDatabaseView_CreateEmbedded( &host, &p.gui, p.pBrowser.get(), DATABASE_TAB_MATERIALS, true );
    REQUIRE( pShader != nullptr );
    CHECK( pShader->parentWidget() == &host );
    CHECK_FALSE( pShader->isWindow() );
    CHECK( EditorDatabaseView_Tab( pShader ) == DATABASE_TAB_SHADERS );
    auto *pTabs = pShader->findChild<QTabWidget *>( QStringLiteral( "DatabaseTabs" ) );
    REQUIRE( pTabs != nullptr );
    CHECK( pTabs->tabBar()->isHidden() );
    for ( int tab = 0; tab < DATABASE_TAB_COUNT; ++tab ) { CHECK( pTabs->isTabVisible( tab ) == ( tab == DATABASE_TAB_SHADERS ) ); }
    REQUIRE( EditorDatabaseView_Open( pShader, QStringLiteral( "shaders/lit.cyshader" ) ) );
    for ( int tab = 0; tab < DATABASE_TAB_COUNT; ++tab ) {
        EditorDatabaseView_SetTab( pShader, tab );
        CHECK( EditorDatabaseView_Tab( pShader ) == DATABASE_TAB_SHADERS );
    }
    CHECK_FALSE( EditorDatabaseView_Open( pShader, QStringLiteral( "materials/wall.cymat" ) ) );
    CHECK_FALSE( EditorDatabaseView_OpenEntity( pShader, 7u ) );
    CHECK( EditorDatabaseView_Current( pShader ) == QStringLiteral( "shaders/lit.cyshader" ) );
    host.show();
    pDatabase->show();
    pShader->show();
    QCoreApplication::processEvents();
    QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
    QCoreApplication::sendEvent( pShader, &escape );
    CHECK( pShader->isVisible() );

    // Standalone Database View retains all tabs and normal dialog Escape.
    CHECK( p.pView->isWindow() );
    EditorDatabaseView_SetTab( p.pView.get(), DATABASE_TAB_SOUNDS );
    CHECK( EditorDatabaseView_Tab( p.pView.get() ) == DATABASE_TAB_SOUNDS );
    auto *pStandaloneTabs = p.pView->findChild<QTabWidget *>( QStringLiteral( "DatabaseTabs" ) );
    REQUIRE( pStandaloneTabs != nullptr );
    for ( int tab = 0; tab < DATABASE_TAB_COUNT; ++tab ) { CHECK( pStandaloneTabs->isTabVisible( tab ) ); }
    p.pView->show();
    QKeyEvent standaloneEscape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
    QCoreApplication::sendEvent( p.pView.get(), &standaloneEscape );
    CHECK_FALSE( p.pView->isVisible() );
}

TEST_CASE( "Embedded shader recipe and stage drafts survive pane switches and refresh", "[editor][gui][database][shader][embedded]" )
{
    project_t p;
    QWidget host;
    QWidget *pShader = EditorDatabaseView_CreateEmbedded( &host, &p.gui, p.pBrowser.get(), DATABASE_TAB_SHADERS, true );
    REQUIRE( pShader != nullptr );
    REQUIRE( EditorDatabaseView_Open( pShader, QStringLiteral( "shaders/lit.cyshader" ) ) );
    REQUIRE( EditorDatabaseView_CodeEditorCount( pShader ) == 3 );
    QWidget *pRecipe = EditorDatabaseView_CodeEditor( pShader, 0 );
    QWidget *pFragment = EditorDatabaseView_CodeEditor( pShader, 2 );
    REQUIRE( pRecipe != nullptr );
    REQUIRE( pFragment != nullptr );
    REQUIRE( EditorCodeEditor_TextOf( pFragment ).contains( QStringLiteral( "out vec4 color" ) ) );
    const QString recipeFile = p.root + QStringLiteral( "/shaders/lit.cyshader" );
    const QString fragmentFile = p.root + QStringLiteral( "/shaders/lit.frag" );
    const QByteArray diskRecipe = ReadFile( recipeFile );
    const QByteArray diskFragment = ReadFile( fragmentFile );
    QString recipeDraft = EditorCodeEditor_TextOf( pRecipe );
    const QString language = QStringLiteral( "language = \"glsl\"" );
    REQUIRE( recipeDraft.contains( language ) );
    recipeDraft.replace( language, language + QStringLiteral( "\n    author_note = \"embedded recipe draft\"" ) );
    QTextCursor cursor = EditorCodeEditor_Text( pRecipe )->textCursor();
    cursor.select( QTextCursor::Document );
    cursor.insertText( recipeDraft );
    EditorCodeEditor_Text( pFragment )->appendPlainText( QStringLiteral( "// embedded fragment draft" ) );
    const QString fragmentDraft = EditorCodeEditor_TextOf( pFragment );
    REQUIRE( EditorCodeEditor_IsModified( pRecipe ) );
    REQUIRE( EditorCodeEditor_IsModified( pFragment ) );
    REQUIRE( EditorDatabaseView_IsModified( pShader ) );

    host.show();
    pShader->show();
    QCoreApplication::processEvents();
    for ( int i = 0; i < 2; ++i ) {
        pShader->hide();
        EditorDatabaseView_Refresh( pShader );
        pShader->show();
        QCoreApplication::processEvents();
        CHECK( EditorDatabaseView_Current( pShader ) == QStringLiteral( "shaders/lit.cyshader" ) );
        CHECK( EditorDatabaseView_CodeEditor( pShader, 0 ) == pRecipe );
        CHECK( EditorDatabaseView_CodeEditor( pShader, 2 ) == pFragment );
        CHECK( EditorCodeEditor_TextOf( pRecipe ) == recipeDraft );
        CHECK( EditorCodeEditor_TextOf( pFragment ) == fragmentDraft );
        CHECK( EditorCodeEditor_IsModified( pRecipe ) );
        CHECK( EditorCodeEditor_IsModified( pFragment ) );
        CHECK( EditorDatabaseView_IsModified( pShader ) );
    }
    CHECK( ReadFile( recipeFile ) == diskRecipe );
    CHECK( ReadFile( fragmentFile ) == diskFragment );
    REQUIRE( EditorDatabaseView_Save( pShader ) );
    CHECK( ReadFile( recipeFile ).contains( "embedded recipe draft" ) );
    CHECK( ReadFile( fragmentFile ).contains( "embedded fragment draft" ) );
    CHECK_FALSE( EditorDatabaseView_IsModified( pShader ) );
}

TEST_CASE( "An embedded database retains drafts when its shared source is destroyed first", "[editor][gui][database][shader][embedded]" )
{
    project_t p;
    QWidget host;
    std::unique_ptr<QWidget> pShader( EditorDatabaseView_CreateEmbedded( &host, &p.gui, p.pBrowser.get(), DATABASE_TAB_SHADERS, true ) );
    REQUIRE( pShader != nullptr );
    REQUIRE( EditorDatabaseView_Open( pShader.get(), QStringLiteral( "shaders/lit.cyshader" ) ) );
    QWidget *pFragment = EditorDatabaseView_CodeEditor( pShader.get(), 2 );
    REQUIRE( pFragment != nullptr );
    EditorCodeEditor_Text( pFragment )->appendPlainText( QStringLiteral( "// source gone, draft still owned" ) );
    const QString draft = EditorCodeEditor_TextOf( pFragment );
    p.pBrowser.reset();
    EditorDatabaseView_Refresh( pShader.get() );
    CHECK( EditorDatabaseView_Library( pShader.get() ).isEmpty() );
    CHECK( EditorCodeEditor_TextOf( pFragment ) == draft );
    CHECK( EditorDatabaseView_IsModified( pShader.get() ) );
    CHECK_FALSE( EditorDatabaseView_Open( pShader.get(), QStringLiteral( "shaders/lit.cyshader" ) ) );
    pShader.reset();
    QCoreApplication::processEvents();
}

TEST_CASE( "Catalogue rescan refreshes a database picker without replacing authored drafts", "[editor][gui][database][shader][embedded][rescan]" )
{
    project_t p;
    QWidget host;
    QWidget *pDatabase = EditorDatabaseView_CreateEmbedded( &host, &p.gui, p.pBrowser.get() );
    REQUIRE( pDatabase != nullptr );
    REQUIRE( EditorDatabaseView_Open( pDatabase, QStringLiteral( "shaders/lit.cyshader" ) ) );
    QWidget *pFragment = EditorDatabaseView_CodeEditor( pDatabase, 2 );
    REQUIRE( pFragment != nullptr );
    EditorCodeEditor_Text( pFragment )->appendPlainText( QStringLiteral( "// retained while a picker rescans" ) );
    const QString stageDraft = EditorCodeEditor_TextOf( pFragment );
    REQUIRE( EditorDatabaseView_Open( pDatabase, QStringLiteral( "materials/wall.cymat" ) ) );
    REQUIRE( EditorDatabaseView_SetProperty( pDatabase, QStringLiteral( "Shader Params/roughness" ), QStringLiteral( "0.42" ) ) );
    host.show();
    pDatabase->show();
    QCoreApplication::processEvents();
    auto *pChoose = pDatabase->findChild<QToolButton *>( QStringLiteral( "DatabaseChoose_base_color" ) );
    REQUIRE( pChoose != nullptr );
    pChoose->click();
    auto *pPicker = pDatabase->findChild<QDialog *>( QStringLiteral( "EditorAssetWindow" ) );
    REQUIRE( pPicker != nullptr );
    REQUIRE( EditorAssetWindow_IsPicking( pPicker ) );
    REQUIRE( EditorAssetWindow_Selected( pPicker ) == QStringLiteral( "textures/brick.cytex" ) );
    REQUIRE( pPicker->isVisible() );

    // Mirror Mason's completed-rescan callback. It must refresh the cached
    // picker as well as page libraries, without reopening either recipe.
    struct refresh_context_t {
        QWidget *pView;
        QWidget *pBrowser;
        int scans{ 0 };
        ~refresh_context_t() { EditorAssetBrowser_SetRescanCallback( pBrowser, nullptr, nullptr ); }
        static void OnRescan( void *pContext )
        {
            auto &context = *static_cast<refresh_context_t *>( pContext );
            ++context.scans;
            EditorDatabaseView_Refresh( context.pView );
        }
    } refresh{ pDatabase, p.pBrowser.get() };
    EditorAssetBrowser_SetRescanCallback( p.pBrowser.get(), &refresh_context_t::OnRescan, &refresh );
    REQUIRE( QFile::remove( p.root + QStringLiteral( "/textures/brick.cytex" ) ) );
    WriteFile( p.root + QStringLiteral( "/textures/added.cytex" ),
               "@cykv 1\n@schema \"cypher.texture\" 1\n{ source = \"textures/brick.png\" usage = \"color\" }\n" );
    EditorAssetBrowser_Rescan( p.pBrowser.get() );
    CHECK( refresh.scans == 1 );
    CHECK( pPicker->isVisible() );
    CHECK( EditorAssetWindow_IsPicking( pPicker ) );
    CHECK_FALSE( EditorAssetWindow_Visible( pPicker ).contains( QStringLiteral( "textures/brick.cytex" ) ) );
    CHECK( EditorAssetWindow_Visible( pPicker ).contains( QStringLiteral( "textures/added.cytex" ) ) );
    CHECK_FALSE( EditorAssetWindow_Accept( pPicker ) ); // The removed selection is no longer an answer.
    CHECK( RowValue( EditorDatabaseView_Properties( pDatabase ), QStringLiteral( "Shader Params/roughness" ) ) == QStringLiteral( "0.42" ) );
    CHECK( EditorDatabaseView_IsModified( pDatabase ) );
    CHECK( EditorCodeEditor_TextOf( pFragment ) == stageDraft );
    CHECK( EditorCodeEditor_IsModified( pFragment ) );

    // The still-open picker now accepts the replacement asset into the
    // material, while the independent shader stage draft remains intact.
    REQUIRE( EditorAssetWindow_Select( pPicker, QStringLiteral( "textures/added.cytex" ) ) );
    REQUIRE( EditorAssetWindow_Accept( pPicker ) );
    CHECK_FALSE( pPicker->isVisible() );
    CHECK( RowValue( EditorDatabaseView_Properties( pDatabase ), QStringLiteral( "Texture Maps/base_color" ) ) == QStringLiteral( "textures/added.cytex" ) );
    CHECK( EditorCodeEditor_TextOf( pFragment ) == stageDraft );
    EditorDatabaseView_SetTab( pDatabase, DATABASE_TAB_SHADERS );
    CHECK( EditorDatabaseView_IsModified( pDatabase ) );
}

TEST_CASE( "Embedded shader Save belongs to its content and leaves native text undo intact", "[editor][gui][database][shader][embedded][input]" )
{
    project_t p;
    QMainWindow window;
    auto *pHost = new QWidget( &window );
    window.setCentralWidget( pHost );
    QWidget *pShader = EditorDatabaseView_CreateEmbedded( pHost, &p.gui, p.pBrowser.get(), DATABASE_TAB_SHADERS, true );
    REQUIRE( pShader != nullptr );
    int mapSaves = 0;
    QAction mapSave( QStringLiteral( "Save Map" ), &window );
    mapSave.setShortcut( QKeySequence::Save );
    mapSave.setShortcutContext( Qt::WindowShortcut );
    QObject::connect( &mapSave, &QAction::triggered, &window, [&]() { ++mapSaves; } );
    window.addAction( &mapSave );
    mapSave.trigger();
    REQUIRE( mapSaves == 1 ); // The competing main-window action is connected.
    mapSaves = 0;
    REQUIRE( EditorDatabaseView_Open( pShader, QStringLiteral( "shaders/lit.cyshader" ) ) );
    const QString recipeFile = p.root + QStringLiteral( "/shaders/lit.cyshader" );
    const QString fragmentFile = p.root + QStringLiteral( "/shaders/lit.frag" );
    const QByteArray recipeOnDisk = ReadFile( recipeFile );
    const QByteArray fragmentOnDisk = ReadFile( fragmentFile );
    auto *pSave = pShader->findChild<QToolButton *>( QStringLiteral( "DatabaseSave" ) );
    REQUIRE( pSave != nullptr );
    window.show();
    pShader->show();
    window.activateWindow();
    QCoreApplication::processEvents();

    const auto press = []( QPlainTextEdit *pText, QKeySequence::StandardKey standardKey ) {
        pText->setFocus();
        const QKeyCombination key = QKeySequence( standardKey )[0];
        QKeyEvent preflight( QEvent::ShortcutOverride, static_cast<int>( key.key() ), key.keyboardModifiers() );
        preflight.ignore();
        QCoreApplication::sendEvent( pText, &preflight );
        CHECK( preflight.isAccepted() );
        QKeyEvent keyPress( QEvent::KeyPress, static_cast<int>( key.key() ), key.keyboardModifiers() );
        keyPress.ignore();
        QCoreApplication::sendEvent( pText, &keyPress );
        CHECK( keyPress.isAccepted() );
    };

    SECTION( "Save from a nested stage editor writes the stage instead of the map" ) {
        QWidget *pFragment = EditorDatabaseView_CodeEditor( pShader, 2 );
        REQUIRE( pFragment != nullptr );
        QPlainTextEdit *pText = EditorCodeEditor_Text( pFragment );
        REQUIRE( pText != nullptr );
        pText->appendPlainText( QStringLiteral( "// saved through embedded shortcut" ) );
        REQUIRE( pSave->isEnabled() );
        press( pText, QKeySequence::Save );
        CHECK( ReadFile( fragmentFile ).contains( "saved through embedded shortcut" ) );
        CHECK( ReadFile( recipeFile ) == recipeOnDisk );
        CHECK_FALSE( EditorDatabaseView_IsModified( pShader ) );
        CHECK( mapSaves == 0 );
    }

    SECTION( "An invalid recipe reports the normal Save error and retains all drafts" ) {
        QWidget *pRecipe = EditorDatabaseView_CodeEditor( pShader, 0 );
        QWidget *pFragment = EditorDatabaseView_CodeEditor( pShader, 2 );
        REQUIRE( pRecipe != nullptr );
        REQUIRE( pFragment != nullptr );
        QPlainTextEdit *pText = EditorCodeEditor_Text( pRecipe );
        QString invalid = EditorCodeEditor_TextOf( pRecipe );
        invalid.replace( QStringLiteral( "cypher.shader" ), QStringLiteral( "cypher.material" ) );
        QTextCursor cursor = pText->textCursor();
        cursor.select( QTextCursor::Document );
        cursor.insertText( invalid );
        EditorCodeEditor_Text( pFragment )->appendPlainText( QStringLiteral( "// recoverable stage draft" ) );
        const QString stageDraft = EditorCodeEditor_TextOf( pFragment );
        REQUIRE( pSave->isEnabled() );
        press( pText, QKeySequence::Save );
        CHECK( ReadFile( recipeFile ) == recipeOnDisk );
        CHECK( ReadFile( fragmentFile ) == fragmentOnDisk );
        CHECK( EditorCodeEditor_TextOf( pRecipe ) == invalid );
        CHECK( EditorCodeEditor_TextOf( pFragment ) == stageDraft );
        CHECK( EditorDatabaseView_IsModified( pShader ) );
        auto *pStatus = pShader->findChild<QLabel *>( QStringLiteral( "DatabaseStatus" ) );
        REQUIRE( pStatus != nullptr );
        CHECK( pStatus->text().contains( QStringLiteral( "is not a cypher.shader recipe" ) ) );
        CHECK( mapSaves == 0 );
    }

    SECTION( "Disabled content Save still does not fall through to map Save" ) {
        REQUIRE_FALSE( pSave->isEnabled() );
        QWidget *pFragment = EditorDatabaseView_CodeEditor( pShader, 2 );
        REQUIRE( pFragment != nullptr );
        press( EditorCodeEditor_Text( pFragment ), QKeySequence::Save );
        CHECK( ReadFile( recipeFile ) == recipeOnDisk );
        CHECK( ReadFile( fragmentFile ) == fragmentOnDisk );
        CHECK_FALSE( EditorDatabaseView_IsModified( pShader ) );
        CHECK( mapSaves == 0 );
    }

    SECTION( "Undo remains the code text widget's own operation" ) {
        QWidget *pFragment = EditorDatabaseView_CodeEditor( pShader, 2 );
        REQUIRE( pFragment != nullptr );
        QPlainTextEdit *pText = EditorCodeEditor_Text( pFragment );
        const QString original = pText->toPlainText();
        pText->appendPlainText( QStringLiteral( "// undo this stage draft" ) );
        REQUIRE( pText->document()->isUndoAvailable() );
        press( pText, QKeySequence::Undo );
        CHECK( pText->toPlainText() == original );
        CHECK( ReadFile( fragmentFile ) == fragmentOnDisk );
        CHECK( mapSaves == 0 );
    }
}

TEST_CASE( "Content library metadata pages expose real catalog files without editing their formats", "[editor][gui][database][content-library]" )
{
    project_t p;
    AddLibraryFiles( p );
    QWidget host;
    QWidget *pEmbedded = EditorDatabaseView_CreateEmbedded( &host, &p.gui, p.pBrowser.get(), DATABASE_TAB_MODELS );
    REQUIRE( pEmbedded != nullptr );
    const struct { int tab; editor_asset_kind_t kind; const char *path; bool source; } files[]{
        { DATABASE_TAB_MODELS, editor_asset_kind_t::MODEL, "models/crate.cymesh", false },
        { DATABASE_TAB_MODELS, editor_asset_kind_t::MODEL, "models/crate.obj", true },
        { DATABASE_TAB_MAPS, editor_asset_kind_t::MAP, "maps/arena.cymap", false },
        { DATABASE_TAB_FONTS, editor_asset_kind_t::FONT, "fonts/interface.cyfont", false },
        { DATABASE_TAB_FONTS, editor_asset_kind_t::FONT, "fonts/interface.ttf", true },
        { DATABASE_TAB_ANIMATIONS, editor_asset_kind_t::ANIMATION, "animations/idle.cyanim", false },
    };
    for ( QWidget *pView : { static_cast<QWidget *>( p.pView.get() ), pEmbedded } ) {
        auto *pCategories = pView->findChild<QToolButton *>( QStringLiteral( "DatabaseCategories" ) );
        auto *pCategoryMenu = pView->findChild<QMenu *>( QStringLiteral( "DatabaseCategoriesMenu" ) );
        REQUIRE( pCategories != nullptr );
        REQUIRE( pCategoryMenu != nullptr );
        CHECK_FALSE( pCategories->isHidden() );
        CHECK( pCategories->menu() == pCategoryMenu );
        for ( int tab = 0; tab < DATABASE_TAB_COUNT; ++tab ) {
            auto *pAction = pCategoryMenu->findChild<QAction *>( QStringLiteral( "DatabaseCategory%1" ).arg( tab ) );
            REQUIRE( pAction != nullptr );
            CHECK( pAction->isCheckable() );
            CHECK_FALSE( pAction->text().isEmpty() );
        }
        for ( const auto &file : files ) {
            const QString path = QString::fromLatin1( file.path );
            const QByteArray onDisk = ReadFile( p.root + QLatin1Char( '/' ) + path );
            auto *pCategory = pCategoryMenu->findChild<QAction *>( QStringLiteral( "DatabaseCategory%1" ).arg( file.tab ) );
            REQUIRE( pCategory != nullptr );
            pCategory->trigger();
            CHECK( EditorDatabaseView_Tab( pView ) == file.tab );
            CHECK( pCategory->isChecked() );
            for ( int tab = 0; tab < DATABASE_TAB_COUNT; ++tab ) {
                auto *pAction = pCategoryMenu->findChild<QAction *>( QStringLiteral( "DatabaseCategory%1" ).arg( tab ) );
                REQUIRE( pAction != nullptr );
                CHECK( pAction->isChecked() == ( tab == file.tab ) );
            }
            ActivateLibraryEntry( pView, file.tab, path );
            CHECK( EditorDatabaseView_Tab( pView ) == file.tab );
            CHECK( EditorDatabaseView_Current( pView ) == path );
            const QStringList rows = EditorDatabaseView_Properties( pView );
            CHECK( RowValue( rows, QStringLiteral( "File/Path" ) ) == path );
            CHECK( RowValue( rows, QStringLiteral( "File/Type" ) ) == QString::fromLatin1( EditorAssets_KindLabel( file.kind ) ) );
            CHECK( RowValue( rows, QStringLiteral( "File/Size" ) ) == QStringLiteral( "%1 bytes" ).arg( onDisk.size() ) );
            CHECK( RowValue( rows, QStringLiteral( "File/Content root" ) ) == p.root );
            CHECK( RowValue( rows, QStringLiteral( "File/Source" ) ) == ( file.source ? QStringLiteral( "true" ) : QStringLiteral( "false" ) ) );
            CHECK_FALSE( EditorDatabaseView_SetProperty( pView, QStringLiteral( "File/Path" ), QStringLiteral( "elsewhere" ) ) );
            CHECK_FALSE( EditorDatabaseView_IsModified( pView ) );
            CHECK_FALSE( EditorDatabaseView_Save( pView ) );
            CHECK_FALSE( EditorDatabaseView_Revert( pView ) );
            CHECK( ReadFile( p.root + QLatin1Char( '/' ) + path ) == onDisk );
        }
        EditorDatabaseView_SetTab( pView, DATABASE_TAB_MODELS );
        CHECK( EditorDatabaseView_Library( pView ) == ( QStringList{ QStringLiteral( "models/crate.cymesh" ), QStringLiteral( "models/crate.obj" ) } ) );
        CHECK_FALSE( EditorDatabaseView_Open( pView, QStringLiteral( "models/missing.obj" ) ) );
        CHECK_FALSE( EditorDatabaseView_Open( pView, QStringLiteral( "scripts/unregistered.cyscript" ) ) );
        CHECK_FALSE( EditorDatabaseView_Open( pView, QStringLiteral( "sequences/unregistered.cysequence" ) ) );
    }
}

TEST_CASE( "Content library trigger script and sequence tabs are filtered views of map instances", "[editor][gui][database][content-library][entities]" )
{
    library_entities_t source;
    project_t p;
    QWidget host;
    QWidget *pEmbedded = EditorDatabaseView_CreateEmbedded( &host, &p.gui, p.pBrowser.get(), DATABASE_TAB_TRIGGERS );
    REQUIRE( pEmbedded != nullptr );
    const struct { int tab; QStringList expected; u64 selected; } categories[]{
        { DATABASE_TAB_TRIGGERS, { QStringLiteral( "#8" ) }, 8u },
        { DATABASE_TAB_SCRIPTS, { QStringLiteral( "#10" ), QStringLiteral( "#11" ), QStringLiteral( "#15" ), QStringLiteral( "#18" ) }, 11u },
        { DATABASE_TAB_SEQUENCES, { QStringLiteral( "#12" ), QStringLiteral( "#13" ), QStringLiteral( "#16" ) }, 13u },
    };
    for ( QWidget *pView : { static_cast<QWidget *>( p.pView.get() ), pEmbedded } ) {
        source.Connect( pView );
        for ( const auto &category : categories ) {
            EditorDatabaseView_SetTab( pView, category.tab );
            CHECK( EditorDatabaseView_Library( pView ) == category.expected );
            // A normal tree activation must inspect the record in this tab.
            const QString id = QStringLiteral( "#%1" ).arg( category.selected );
            ActivateLibraryEntry( pView, category.tab, id );
            CHECK( EditorDatabaseView_Tab( pView ) == category.tab );
            CHECK( EditorDatabaseView_Current( pView ) == id );
            CHECK_FALSE( EditorDatabaseView_Properties( pView ).isEmpty() );
            CHECK_FALSE( EditorDatabaseView_SetProperty( pView, QStringLiteral( "Entity/name" ), QStringLiteral( "renamed" ) ) );
            CHECK_FALSE( EditorDatabaseView_Save( pView ) );
            CHECK_FALSE( EditorDatabaseView_Revert( pView ) );
            auto *pPage = pView->findChild<QWidget *>( QStringLiteral( "DatabasePage%1" ).arg( category.tab ) );
            REQUIRE( pPage != nullptr );
            auto *pSelect = pPage->findChild<QPushButton *>( QStringLiteral( "DatabaseSelectEntity" ) );
            REQUIRE( pSelect != nullptr );
            source.selected = 0u;
            pSelect->click();
            CHECK( source.selected == category.selected );

            // The public open operation preserves an eligible filtered tab.
            REQUIRE( EditorDatabaseView_OpenEntity( pView, category.selected ) );
            CHECK( EditorDatabaseView_Tab( pView ) == category.tab );
            // A light still opens through the general entity inspector.
            REQUIRE( EditorDatabaseView_OpenEntity( pView, 7u ) );
            CHECK( EditorDatabaseView_Tab( pView ) == DATABASE_TAB_ENTITIES );
            CHECK( RowValue( EditorDatabaseView_Properties( pView ), QStringLiteral( "Entity/origin" ) ) == QStringLiteral( "64 32 128" ) );
        }

        EditorDatabaseView_SetTab( pView, DATABASE_TAB_SCRIPTS );
        auto *pFilter = pView->findChild<QLineEdit *>( QStringLiteral( "DatabaseFilter%1" ).arg( DATABASE_TAB_SCRIPTS ) );
        REQUIRE( pFilter != nullptr );
        pFilter->setText( QStringLiteral( "script_path" ) );
        CHECK( EditorDatabaseView_Library( pView ) == QStringList{ QStringLiteral( "#11" ) } );
        pFilter->clear();
        CHECK( EditorDatabaseView_Library( pView ) == categories[1].expected );
        REQUIRE( EditorDatabaseView_OpenEntity( pView, 11u ) );
        CHECK( EditorDatabaseView_Tab( pView ) == DATABASE_TAB_SCRIPTS );
        CHECK_FALSE( EditorDatabaseView_OpenEntity( pView, 99u ) );
    }
}

TEST_CASE( "Content library refresh replaces stale entity details while preserving recipe drafts", "[editor][gui][database][content-library][entities][rescan]" )
{
    library_entities_t source;
    project_t p;
    AddLibraryFiles( p );
    QWidget *pView = p.pView.get();
    source.Connect( pView );
    const QString materialFile = p.root + QStringLiteral( "/materials/wall.cymat" );
    const QByteArray materialOnDisk = ReadFile( materialFile );
    REQUIRE( EditorDatabaseView_Open( pView, QStringLiteral( "materials/wall.cymat" ) ) );
    REQUIRE( EditorDatabaseView_SetProperty( pView, QStringLiteral( "Shader Params/roughness" ), QStringLiteral( "0.42" ) ) );
    auto *pModelCategory = pView->findChild<QAction *>( QStringLiteral( "DatabaseCategory%1" ).arg( DATABASE_TAB_MODELS ) );
    auto *pMaterialCategory = pView->findChild<QAction *>( QStringLiteral( "DatabaseCategory%1" ).arg( DATABASE_TAB_MATERIALS ) );
    REQUIRE( pModelCategory != nullptr );
    REQUIRE( pMaterialCategory != nullptr );
    pModelCategory->trigger();
    CHECK( EditorDatabaseView_Tab( pView ) == DATABASE_TAB_MODELS );
    pMaterialCategory->trigger();
    CHECK( EditorDatabaseView_Tab( pView ) == DATABASE_TAB_MATERIALS );
    CHECK( RowValue( EditorDatabaseView_Properties( pView ), QStringLiteral( "Shader Params/roughness" ) ) == QStringLiteral( "0.42" ) );
    CHECK( EditorDatabaseView_IsModified( pView ) );
    ActivateLibraryEntry( pView, DATABASE_TAB_TRIGGERS, QStringLiteral( "#8" ) );
    ActivateLibraryEntry( pView, DATABASE_TAB_SCRIPTS, QStringLiteral( "#11" ) );
    ActivateLibraryEntry( pView, DATABASE_TAB_SEQUENCES, QStringLiteral( "#13" ) );

    // Replace the source document: records are borrowed only while building
    // the view, and hidden tabs must also refresh their selected details.
    source.Load( "@cykv 1\n@schema \"test.library_entities\" 1\n{ entities = [\n"
                 "{ id = 8u class = \"Trigger_Once\" name = \"moved_trigger\" origin = [9, 8, 7] properties = { delay = 2 } },\n"
                 "{ id = 11u class = \"logic_relay\" name = \"updated_script\" properties = { script_path = \"scripts/new.cfg\" } }\n"
                 "] }\n" );
    EditorDatabaseView_Refresh( pView );
    CHECK( EditorDatabaseView_Current( pView ).isEmpty() );
    CHECK( EditorDatabaseView_Properties( pView ).isEmpty() );
    CHECK( EditorDatabaseView_Library( pView ).isEmpty() );
    EditorDatabaseView_SetTab( pView, DATABASE_TAB_TRIGGERS );
    CHECK( EditorDatabaseView_Current( pView ) == QStringLiteral( "#8" ) );
    CHECK( RowValue( EditorDatabaseView_Properties( pView ), QStringLiteral( "Entity/name" ) ) == QStringLiteral( "moved_trigger" ) );
    CHECK( RowValue( EditorDatabaseView_Properties( pView ), QStringLiteral( "Entity/origin" ) ) == QStringLiteral( "9 8 7" ) );
    EditorDatabaseView_SetTab( pView, DATABASE_TAB_SCRIPTS );
    CHECK( EditorDatabaseView_Current( pView ) == QStringLiteral( "#11" ) );
    CHECK( RowValue( EditorDatabaseView_Properties( pView ), QStringLiteral( "properties/script_path" ) ) == QStringLiteral( "scripts/new.cfg" ) );

    // Rescan a content file while the script page is active. Metadata is
    // rebuilt from the new catalog, and the material buffer stays authored.
    WriteFile( p.root + QStringLiteral( "/models/crate.obj" ), "o changed_crate\nv 0 0 0\n" );
    EditorAssetBrowser_Rescan( p.pBrowser.get() );
    EditorDatabaseView_Refresh( pView );
    REQUIRE( EditorDatabaseView_Open( pView, QStringLiteral( "models/crate.obj" ) ) );
    CHECK( RowValue( EditorDatabaseView_Properties( pView ), QStringLiteral( "File/Size" ) ) == QStringLiteral( "%1 bytes" ).arg( ReadFile( p.root + QStringLiteral( "/models/crate.obj" ) ).size() ) );
    EditorDatabaseView_SetTab( pView, DATABASE_TAB_MATERIALS );
    CHECK( EditorDatabaseView_Current( pView ) == QStringLiteral( "materials/wall.cymat" ) );
    CHECK( RowValue( EditorDatabaseView_Properties( pView ), QStringLiteral( "Shader Params/roughness" ) ) == QStringLiteral( "0.42" ) );
    CHECK( EditorDatabaseView_IsModified( pView ) );
    CHECK( ReadFile( materialFile ) == materialOnDisk );
    REQUIRE( EditorDatabaseView_Save( pView ) );
    CHECK( ReadFile( materialFile ).contains( "0.42" ) );
}

TEST_CASE( "Shader-only panes reject every new content category and retain their shader", "[editor][gui][database][content-library][shader][embedded]" )
{
    library_entities_t source;
    project_t p;
    AddLibraryFiles( p );
    QWidget host;
    QWidget *pShader = EditorDatabaseView_CreateEmbedded( &host, &p.gui, p.pBrowser.get(), DATABASE_TAB_MODELS, true );
    REQUIRE( pShader != nullptr );
    source.Connect( pShader );
    REQUIRE( EditorDatabaseView_Open( pShader, QStringLiteral( "shaders/lit.cyshader" ) ) );
    auto *pTabs = pShader->findChild<QTabWidget *>( QStringLiteral( "DatabaseTabs" ) );
    REQUIRE( pTabs != nullptr );
    auto *pCategories = pShader->findChild<QToolButton *>( QStringLiteral( "DatabaseCategories" ) );
    REQUIRE( pCategories != nullptr );
    CHECK( pCategories->isHidden() );
    for ( const int tab : { DATABASE_TAB_MODELS, DATABASE_TAB_TRIGGERS, DATABASE_TAB_SCRIPTS, DATABASE_TAB_SEQUENCES,
                           DATABASE_TAB_MAPS, DATABASE_TAB_FONTS, DATABASE_TAB_ANIMATIONS } ) {
        CHECK_FALSE( pTabs->isTabVisible( tab ) );
        EditorDatabaseView_SetTab( pShader, tab );
        CHECK( EditorDatabaseView_Tab( pShader ) == DATABASE_TAB_SHADERS );
        CHECK( EditorDatabaseView_Current( pShader ) == QStringLiteral( "shaders/lit.cyshader" ) );
    }
    for ( const char *path : { "models/crate.cymesh", "models/crate.obj", "maps/arena.cymap", "fonts/interface.cyfont",
                              "fonts/interface.ttf", "animations/idle.cyanim" } ) {
        CHECK_FALSE( EditorDatabaseView_Open( pShader, QString::fromLatin1( path ) ) );
    }
    CHECK_FALSE( EditorDatabaseView_OpenEntity( pShader, 8u ) );
    CHECK_FALSE( EditorDatabaseView_OpenEntity( pShader, 11u ) );
    CHECK_FALSE( EditorDatabaseView_OpenEntity( pShader, 13u ) );
    CHECK( EditorDatabaseView_Current( pShader ) == QStringLiteral( "shaders/lit.cyshader" ) );
}
