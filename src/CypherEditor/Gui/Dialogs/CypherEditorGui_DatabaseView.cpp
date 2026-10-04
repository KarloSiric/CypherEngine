//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_DatabaseView.cpp
//  Purpose: Implements the Database View: libraries, previews, property
//           grids that edit recipes, the shader code editor, the texture
//           preview, and the entity inspector.
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
#include "CypherEditorGui_Style.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"
#include "CypherCommon/Tier1/CypherCommon_TextBuffer.h"
#include "CypherCommon/Tier2/CypherCommon_SettingsDocument.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QImage>
#include <QImageReader>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSet>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace cypher::editor::gui
{

using namespace cypher::common;

namespace
{

// ---------------------------------------------------------------------------
// Recipes: one CYKV document read and written through the settings store,
// at the schema version it was written in.
// ---------------------------------------------------------------------------

QString FromView( string_view_t view )
{
    return QString::fromUtf8( view.pData != nullptr ? view.pData : "", static_cast<qsizetype>( view.cchLength ) );
}

string_view_t ViewOf( const QByteArray &bytes )
{
    return string_view_t{ bytes.constData(), static_cast<usize>( bytes.size() ) };
}

// settings_path_t borrows its segments; this keeps the bytes alive.
struct key_path_t {
    explicit key_path_t( const QStringList &parts )
    {
        bytes.reserve( static_cast<usize>( parts.size() ) ); // No reallocation once views are taken.
        bOk = !parts.isEmpty();
        for ( const QString &part : parts ) {
            bytes.push_back( part.toUtf8() );
            bOk = bOk && SettingsPath_Append( &path, ViewOf( bytes.back() ) );
        }
    }
    std::vector<QByteArray> bytes{};
    settings_path_t path{};
    bool bOk{ false };
};

struct recipe_t {
    std::unique_ptr<settings_document_t> pStore{};
    QString path{};               // Virtual path.
    QString file{};               // On disk.
    const char *pSchema{ nullptr };
    u32 version{ 0u };
    bool bDirty{ false };

    bool IsOpen() const { return pStore != nullptr; }
    void Close() { pStore.reset(); path.clear(); file.clear(); version = 0u; bDirty = false; }
};

bool Recipe_Parse( settings_document_t *pStore, const allocator_t *pAllocator, const QByteArray &text, const QString &path, const char *pSchema, QString *pError )
{
    const settings_document_identity_t identity{ StringView_FromCString( pSchema ), 1u, 2u };
    if ( SettingsDocument_Init( pStore, pAllocator, identity ) != settings_document_status_t::OK ) {
        if ( pError != nullptr ) { *pError = QStringLiteral( "Out of memory" ); }
        return false;
    }
    const settings_document_load_result_t loaded = SettingsDocument_Load( pStore, ViewOf( text ) );
    if ( loaded.status != settings_document_status_t::OK ) {
        if ( pError != nullptr ) {
            *pError = loaded.status == settings_document_status_t::SCHEMA_MISMATCH ? QStringLiteral( "%1 is not a %2 recipe" ).arg( path, QString::fromLatin1( pSchema ) )
                    : loaded.status == settings_document_status_t::UNSUPPORTED_VERSION ? QStringLiteral( "%1: schema version %2 is not supported" ).arg( path ).arg( loaded.nDeclaredVersion )
                    : QStringLiteral( "%1 does not parse (line %2)" ).arg( path ).arg( loaded.location.nLine );
        }
        return false;
    }
    return true;
}

bool Recipe_Load( recipe_t &recipe, const allocator_t *pAllocator, const QString &file, const QString &path, const char *pSchema, QString *pError )
{
    recipe.Close();
    QFile in( file );
    if ( !in.open( QIODevice::ReadOnly ) ) {
        if ( pError != nullptr ) { *pError = QStringLiteral( "Cannot read %1" ).arg( file ); }
        return false;
    }
    auto pStore = std::make_unique<settings_document_t>();
    if ( !Recipe_Parse( pStore.get(), pAllocator, in.readAll(), path, pSchema, pError ) ) { return false; }
    // Write back at the version it was written in: a schema 1 material
    // stays schema 1 until the user upgrades it.
    pStore->identity.nCurrentVersion = pStore->nLoadedVersion;
    recipe.pStore = std::move( pStore );
    recipe.path = path;
    recipe.file = file;
    recipe.pSchema = pSchema;
    recipe.version = recipe.pStore->nLoadedVersion;
    recipe.bDirty = false;
    return true;
}

QByteArray Recipe_Text( const recipe_t &recipe, const allocator_t *pAllocator )
{
    if ( !recipe.IsOpen() ) { return {}; }
    text_buffer_t text{};
    QByteArray result;
    if ( TextBuffer_Init( &text, pAllocator ) && SettingsDocument_Write( recipe.pStore.get(), &text ) == settings_document_status_t::OK ) {
        result = QByteArray( TextBuffer_Data( &text ), static_cast<qsizetype>( TextBuffer_Length( &text ) ) );
    }
    TextBuffer_Shutdown( &text );
    return result;
}

bool WriteFile( const QString &file, const QByteArray &bytes, QString *pError )
{
    QSaveFile out( file ); // All or nothing: a failed save never truncates the recipe.
    if ( !out.open( QIODevice::WriteOnly ) || out.write( bytes ) != bytes.size() || !out.commit() ) {
        if ( pError != nullptr ) { *pError = QStringLiteral( "Cannot write %1" ).arg( file ); }
        return false;
    }
    return true;
}

bool Recipe_Save( recipe_t &recipe, const allocator_t *pAllocator, QString *pError )
{
    const QByteArray text = Recipe_Text( recipe, pAllocator );
    if ( text.isEmpty() ) {
        if ( pError != nullptr ) { *pError = QStringLiteral( "%1 could not be written as CYKV" ).arg( recipe.path ); }
        return false;
    }
    if ( !WriteFile( recipe.file, text, pError ) ) { return false; }
    recipe.bDirty = false;
    return true;
}

const key_value_t *Recipe_Find( const recipe_t &recipe, const QStringList &key )
{
    if ( !recipe.IsOpen() ) { return nullptr; }
    const key_path_t path( key );
    return path.bOk ? SettingsNode_Find( SettingsDocument_Root( recipe.pStore.get() ), path.path ) : nullptr;
}

QStringList ChildNames( const key_value_t *pObject )
{
    QStringList names;
    if ( pObject == nullptr || KeyValue_Type( pObject ) != key_value_type_t::OBJECT ) { return names; }
    for ( usize i = 0u; i < KeyValue_ChildCount( pObject ); ++i ) { names.append( FromView( KeyValue_Name( KeyValue_ChildAt( pObject, i ) ) ) ); }
    return names;
}

QVariant ValueOf( const key_value_t *pValue )
{
    if ( pValue == nullptr ) { return {}; }
    switch ( KeyValue_Type( pValue ) ) {
        case key_value_type_t::BOOL: { bool_t b = CY_FALSE; ( void )KeyValue_GetBool( pValue, &b ); return b != CY_FALSE; }
        case key_value_type_t::I64: { i64 n = 0; ( void )KeyValue_GetI64( pValue, &n ); return static_cast<qlonglong>( n ); }
        case key_value_type_t::U64: { u64 n = 0u; ( void )KeyValue_GetU64( pValue, &n ); return static_cast<qulonglong>( n ); }
        case key_value_type_t::F64: { f64 n = 0.0; ( void )KeyValue_GetF64( pValue, &n ); return n; }
        case key_value_type_t::STRING: { string_view_t s{}; ( void )KeyValue_GetString( pValue, &s ); return FromView( s ); }
        case key_value_type_t::ARRAY: {
            QVariantList list;
            for ( usize i = 0u; i < KeyValue_ChildCount( pValue ); ++i ) { list.append( ValueOf( KeyValue_ChildAt( pValue, i ) ) ); }
            return list;
        }
        default: return {};
    }
}

QString TextOf( const QVariant &value )
{
    if ( !value.isValid() ) { return {}; }
    if ( value.typeId() == QMetaType::Bool ) { return value.toBool() ? QStringLiteral( "true" ) : QStringLiteral( "false" ); }
    if ( value.typeId() == QMetaType::Double ) { return QString::number( value.toDouble(), 'g', 6 ); }
    if ( value.typeId() == QMetaType::QVariantList ) {
        QStringList parts;
        for ( const QVariant &item : value.toList() ) { parts.append( TextOf( item ) ); }
        return parts.join( QLatin1Char( ' ' ) );
    }
    return value.toString();
}

bool Recipe_Set( recipe_t &recipe, const QStringList &key, const QVariant &value )
{
    if ( !recipe.IsOpen() ) { return false; }
    const key_path_t path( key );
    key_value_t *pNode = nullptr;
    if ( !path.bOk || SettingsDocument_Ensure( recipe.pStore.get(), path.path, &pNode ) != settings_document_status_t::OK || pNode == nullptr ) { return false; }
    key_value_document_t *pDocument = recipe.pStore->pDocument;
    bool bOk = false;
    switch ( value.typeId() ) {
        case QMetaType::Bool: bOk = KeyValue_SetBool( pDocument, pNode, value.toBool() ? CY_TRUE : CY_FALSE ) != CY_FALSE; break;
        case QMetaType::LongLong:
        case QMetaType::Int: bOk = KeyValue_SetI64( pDocument, pNode, value.toLongLong() ) != CY_FALSE; break;
        case QMetaType::ULongLong:
        case QMetaType::UInt: bOk = KeyValue_SetU64( pDocument, pNode, value.toULongLong() ) != CY_FALSE; break;
        case QMetaType::Double: bOk = KeyValue_SetF64( pDocument, pNode, value.toDouble() ) != CY_FALSE; break;
        case QMetaType::QString: { const QByteArray utf8 = value.toString().toUtf8(); bOk = KeyValue_SetString( pDocument, pNode, ViewOf( utf8 ) ) != CY_FALSE; break; }
        case QMetaType::QVariantList:
        case QMetaType::QStringList: {
            if ( KeyValue_Type( pNode ) != key_value_type_t::ARRAY ) { bOk = KeyValue_SetContainerType( pDocument, pNode, key_value_type_t::ARRAY ) != CY_FALSE; }
            else { bOk = true; }
            while ( bOk && KeyValue_ChildCount( pNode ) != 0u ) { bOk = KeyValue_Remove( pDocument, pNode, KeyValue_ChildAt( pNode, KeyValue_ChildCount( pNode ) - 1u ) ) != CY_FALSE; }
            for ( const QVariant &item : value.toList() ) {
                if ( !bOk ) { break; }
                if ( item.typeId() == QMetaType::QString ) {
                    key_value_t *pChild = KeyValue_ArrayAppend( pDocument, pNode, key_value_type_t::STRING );
                    const QByteArray utf8 = item.toString().toUtf8();
                    bOk = pChild != nullptr && KeyValue_SetString( pDocument, pChild, ViewOf( utf8 ) ) != CY_FALSE;
                } else if ( item.typeId() == QMetaType::Bool ) {
                    key_value_t *pChild = KeyValue_ArrayAppend( pDocument, pNode, key_value_type_t::BOOL );
                    bOk = pChild != nullptr && KeyValue_SetBool( pDocument, pChild, item.toBool() ? CY_TRUE : CY_FALSE ) != CY_FALSE;
                } else {
                    key_value_t *pChild = KeyValue_ArrayAppend( pDocument, pNode, key_value_type_t::F64 );
                    bOk = pChild != nullptr && KeyValue_SetF64( pDocument, pChild, item.toDouble() ) != CY_FALSE;
                }
            }
            break;
        }
        default: break;
    }
    if ( bOk ) { recipe.bDirty = true; }
    return bOk;
}

bool Recipe_Remove( recipe_t &recipe, const QStringList &key )
{
    if ( !recipe.IsOpen() ) { return false; }
    const key_path_t path( key );
    if ( !path.bOk || SettingsDocument_Remove( recipe.pStore.get(), path.path ) != settings_document_status_t::OK ) { return false; }
    recipe.bDirty = true;
    return true;
}

// ---------------------------------------------------------------------------
// Shader interface (schema 2): what a material can set and how.
// ---------------------------------------------------------------------------

struct slot_t {
    QString name;
    QString type;       // "texture2d" ...
    QString usage;      // "color", "normal", "data"
    QString colorSpace;
    bool bRequired{ true };
};

struct parameter_t {
    QString name;
    QString type;       // "f32", "color4", "bool" ...
    QVariant fallback;  // Declared default.
    double minimum{ 0.0 };
    double maximum{ 0.0 };
    bool bBounded{ false };
    bool bRequired{ false };
};

struct feature_t {
    QString name;
    QString type;       // "bool", "enum"
    QStringList values;
    QVariant fallback;
};

struct shader_interface_t {
    bool bKnown{ false }; // Schema 2 with an interface block.
    QVector<slot_t> textureSlots{};
    QVector<parameter_t> parameters{};
    QVector<feature_t> features{};
};

int Components( const QString &type )
{
    if ( type == QStringLiteral( "f32x2" ) ) { return 2; }
    if ( type == QStringLiteral( "f32x3" ) || type == QStringLiteral( "color3" ) ) { return 3; }
    if ( type == QStringLiteral( "f32x4" ) || type == QStringLiteral( "color4" ) ) { return 4; }
    return 1;
}

shader_interface_t ReadInterface( const recipe_t &shader )
{
    shader_interface_t result;
    if ( !shader.IsOpen() || shader.version < 2u ) { return result; }
    const key_value_t *pInterface = Recipe_Find( shader, { QStringLiteral( "interface" ) } );
    const auto text = []( const key_value_t *pObject, const char *pName ) {
        return ValueOf( pObject != nullptr ? KeyValue_Find( pObject, StringView_FromCString( pName ) ) : nullptr );
    };
    if ( pInterface != nullptr ) {
        result.bKnown = true;
        const key_value_t *pTextures = KeyValue_Find( pInterface, StringView_FromCString( "textures" ) );
        for ( const QString &name : ChildNames( pTextures ) ) {
            const QByteArray utf8 = name.toUtf8();
            const key_value_t *pSlot = KeyValue_Find( pTextures, ViewOf( utf8 ) );
            slot_t slot{ name, text( pSlot, "type" ).toString(), text( pSlot, "usage" ).toString(), text( pSlot, "color_space" ).toString() };
            const QVariant required = text( pSlot, "required" );
            slot.bRequired = !required.isValid() || required.toBool();
            result.textureSlots.append( slot );
        }
        const key_value_t *pParameters = KeyValue_Find( pInterface, StringView_FromCString( "parameters" ) );
        for ( const QString &name : ChildNames( pParameters ) ) {
            const QByteArray utf8 = name.toUtf8();
            const key_value_t *pParameter = KeyValue_Find( pParameters, ViewOf( utf8 ) );
            parameter_t parameter{ name, text( pParameter, "type" ).toString(), text( pParameter, "default" ) };
            const QVariant minimum = text( pParameter, "minimum" );
            const QVariant maximum = text( pParameter, "maximum" );
            parameter.bBounded = minimum.isValid() && maximum.isValid();
            parameter.minimum = minimum.toDouble();
            parameter.maximum = maximum.toDouble();
            parameter.bRequired = text( pParameter, "required" ).toBool();
            result.parameters.append( parameter );
        }
    }
    const key_value_t *pFeatures = Recipe_Find( shader, { QStringLiteral( "features" ) } );
    for ( const QString &name : ChildNames( pFeatures ) ) {
        const QByteArray utf8 = name.toUtf8();
        const key_value_t *pFeature = KeyValue_Find( pFeatures, ViewOf( utf8 ) );
        feature_t feature{ name, text( pFeature, "type" ).toString(), {}, text( pFeature, "default" ) };
        for ( const QVariant &value : text( pFeature, "values" ).toList() ) { feature.values.append( value.toString() ); }
        result.features.append( feature );
    }
    return result;
}

// ---------------------------------------------------------------------------
// Property grid rows.
// ---------------------------------------------------------------------------

enum class row_kind_t : u8 { PATH, ENUM, BOOL, NUMBER, INTEGER, VECTOR, COLOR, TEXT, LIST, READONLY };

struct row_t {
    QString group;
    QString name;
    QStringList key;                 // Recipe path; empty for read-only rows.
    row_kind_t kind{ row_kind_t::TEXT };
    editor_asset_kind_t pathKind{ editor_asset_kind_t::COUNT }; // What a PATH row's folder opens.
    QStringList choices{};
    double minimum{ -1.0e6 };
    double maximum{ 1.0e6 };
    bool bBounded{ false };
    int components{ 1 };
    QVariant fallback{};             // Shown and used while the recipe leaves it unset.
    bool bResettable{ false };       // An override that can return to the default.
    bool bRequired{ false };
    QString tip{};
    QString readOnlyText{};
};

QString RowId( const row_t &row ) { return row.group + QLatin1Char( '/' ) + row.name; }

// Parses what a user would type into the row's control.
QVariant ParseValue( const row_t &row, const QString &text, bool *pbOk )
{
    *pbOk = true;
    const QString trimmed = text.trimmed();
    switch ( row.kind ) {
        case row_kind_t::BOOL:
            if ( trimmed == QStringLiteral( "true" ) || trimmed == QStringLiteral( "1" ) ) { return true; }
            if ( trimmed == QStringLiteral( "false" ) || trimmed == QStringLiteral( "0" ) ) { return false; }
            *pbOk = false;
            return {};
        case row_kind_t::NUMBER: {
            const double n = trimmed.toDouble( pbOk );
            return row.bBounded ? std::clamp( n, row.minimum, row.maximum ) : n;
        }
        case row_kind_t::INTEGER: return static_cast<qlonglong>( trimmed.toLongLong( pbOk ) );
        case row_kind_t::VECTOR:
        case row_kind_t::COLOR: {
            const QStringList parts = trimmed.split( QRegularExpression( QStringLiteral( "[\\s,]+" ) ), Qt::SkipEmptyParts );
            QVariantList list;
            for ( const QString &part : parts ) {
                bool bNumber = false;
                list.append( part.toDouble( &bNumber ) );
                *pbOk = *pbOk && bNumber;
            }
            *pbOk = *pbOk && list.size() == row.components;
            return list;
        }
        case row_kind_t::ENUM:
            *pbOk = row.choices.contains( trimmed );
            return trimmed;
        case row_kind_t::LIST: {
            QVariantList list;
            for ( const QString &part : trimmed.split( QRegularExpression( QStringLiteral( "[\\s,]+" ) ), Qt::SkipEmptyParts ) ) { list.append( part ); }
            return list;
        }
        case row_kind_t::READONLY: *pbOk = false; return {};
        case row_kind_t::PATH:
        case row_kind_t::TEXT: return trimmed;
    }
    return {};
}

editor_asset_kind_t KindOfPath( const QString &path )
{
    const QByteArray utf8 = path.toUtf8();
    return EditorAssets_KindOf( ViewOf( utf8 ), nullptr );
}

constexpr const char *kTabTitles[DATABASE_TAB_COUNT]{ "Materials", "Shaders", "Textures", "Entities", "Prefabs", "Particles", "Sounds",
    "Models", "Triggers", "Scripts", "Sequences", "Maps", "Fonts", "Animations" };
constexpr const char *kTabIcons[DATABASE_TAB_COUNT]{ "asset-material", "asset-shader", "asset-texture", "entity-point", "asset-prefab", "asset-particle", "asset-sound",
    "asset-model", "entity-trigger", "view-console", "asset-animation", "asset-map", "asset-font", "asset-animation" };

bool IsEntityTab( int tab )
{
    return tab == DATABASE_TAB_ENTITIES || tab == DATABASE_TAB_TRIGGERS || tab == DATABASE_TAB_SCRIPTS || tab == DATABASE_TAB_SEQUENCES;
}
bool IsFileTab( int tab )
{
    return tab == DATABASE_TAB_MODELS || tab == DATABASE_TAB_MAPS || tab == DATABASE_TAB_FONTS || tab == DATABASE_TAB_ANIMATIONS;
}
QString EntityReferences( const key_value_t *record )
{
    QStringList refs;
    const auto *properties = record != nullptr ? KeyValue_Find( record, StringView_FromCString( "properties" ) ) : nullptr;
    for ( const auto *parent : { record, properties } ) {
        if ( parent == nullptr ) { continue; }
        for ( const char *key : { "script", "script_path", "vscripts", "sequence", "sequence_path" } ) {
            string_view_t value{};
            if ( KeyValue_GetString( KeyValue_Find( parent, StringView_FromCString( key ) ), &value ) ) { refs.append( FromView( value ) ); }
        }
    }
    return refs.join( QLatin1Char( ' ' ) );
}
// Authored class/key conventions until game profiles supply explicit categories.
// Category pages borrow the same map records; they never own competing copies.
bool EntityMatches( int tab, const editor_database_entity_t &entity, const key_value_t *record )
{
    const auto hasText = [record]( const char *key ) {
        const auto *properties = record != nullptr ? KeyValue_Find( record, StringView_FromCString( "properties" ) ) : nullptr;
        for ( const auto *parent : { record, properties } ) {
            if ( parent == nullptr ) { continue; }
            string_view_t value{};
            if ( KeyValue_GetString( KeyValue_Find( parent, StringView_FromCString( key ) ), &value ) && !FromView( value ).trimmed().isEmpty() ) { return true; }
        }
        return false;
    };
    switch ( tab ) {
        case DATABASE_TAB_ENTITIES: return true;
        case DATABASE_TAB_TRIGGERS: return entity.className.startsWith( QStringLiteral( "trigger_" ), Qt::CaseInsensitive );
        case DATABASE_TAB_SCRIPTS: return entity.className.compare( QStringLiteral( "logic_script" ), Qt::CaseInsensitive ) == 0 ||
            hasText( "script" ) || hasText( "script_path" ) || hasText( "vscripts" );
        case DATABASE_TAB_SEQUENCES: return entity.className.compare( QStringLiteral( "scripted_sequence" ), Qt::CaseInsensitive ) == 0 ||
            hasText( "sequence" ) || hasText( "sequence_path" );
        default: return false;
    }
}

int TabOfKind( editor_asset_kind_t kind )
{
    switch ( kind ) {
        case editor_asset_kind_t::MATERIAL: return DATABASE_TAB_MATERIALS;
        case editor_asset_kind_t::SHADER: return DATABASE_TAB_SHADERS;
        case editor_asset_kind_t::TEXTURE: return DATABASE_TAB_TEXTURES;
        case editor_asset_kind_t::PREFAB: return DATABASE_TAB_PREFABS;
        case editor_asset_kind_t::PARTICLE: return DATABASE_TAB_PARTICLES;
        case editor_asset_kind_t::SOUND: return DATABASE_TAB_SOUNDS;
        case editor_asset_kind_t::MODEL: return DATABASE_TAB_MODELS;
        case editor_asset_kind_t::MAP: return DATABASE_TAB_MAPS;
        case editor_asset_kind_t::FONT: return DATABASE_TAB_FONTS;
        case editor_asset_kind_t::ANIMATION: return DATABASE_TAB_ANIMATIONS;
        default: return -1;
    }
}

editor_asset_kind_t KindOfTab( int tab )
{
    switch ( tab ) {
        case DATABASE_TAB_MATERIALS: return editor_asset_kind_t::MATERIAL;
        case DATABASE_TAB_SHADERS: return editor_asset_kind_t::SHADER;
        case DATABASE_TAB_TEXTURES: return editor_asset_kind_t::TEXTURE;
        case DATABASE_TAB_PREFABS: return editor_asset_kind_t::PREFAB;
        case DATABASE_TAB_PARTICLES: return editor_asset_kind_t::PARTICLE;
        case DATABASE_TAB_SOUNDS: return editor_asset_kind_t::SOUND;
        case DATABASE_TAB_MODELS: return editor_asset_kind_t::MODEL;
        case DATABASE_TAB_MAPS: return editor_asset_kind_t::MAP;
        case DATABASE_TAB_FONTS: return editor_asset_kind_t::FONT;
        case DATABASE_TAB_ANIMATIONS: return editor_asset_kind_t::ANIMATION;
        default: return editor_asset_kind_t::COUNT;
    }
}

const char *SchemaOf( editor_asset_kind_t kind )
{
    switch ( kind ) {
        case editor_asset_kind_t::MATERIAL: return "cypher.material";
        case editor_asset_kind_t::SHADER: return "cypher.shader";
        case editor_asset_kind_t::TEXTURE: return "cypher.texture";
        case editor_asset_kind_t::PREFAB: return "cypher.prefab";
        case editor_asset_kind_t::PARTICLE: return "cypher.particle";
        case editor_asset_kind_t::SOUND: return "cypher.sound";
        default: return "";
    }
}

// A page: one tab's library on the left, its editor on the right.
struct page_t {
    int tab{ 0 };
    QWidget *pRoot{ nullptr };
    QLineEdit *pFilter{ nullptr };
    QTreeWidget *pLibrary{ nullptr };
    QLabel *pTitle{ nullptr };
    QLabel *pSubtitle{ nullptr };
    QLabel *pPreview{ nullptr };
    QTreeWidget *pGrid{ nullptr };
    QListWidget *pUsedBy{ nullptr };
    QPushButton *pSelect{ nullptr };
    std::vector<row_t> rows{};
    QHash<QString, QWidget *> editors{}; // RowId -> its control.
    recipe_t recipe{};
    QString current{};
};

struct code_tab_t {
    QString path;    // Virtual.
    QString file;
    QWidget *pEditor{ nullptr };
    bool bRecipe{ false };
};

class database_view_t final : public QDialog {
public:
    database_view_t( QWidget *pParent, editor_gui_t *pGui, QWidget *pBrowser, bool bEmbedded = false, bool bShaderOnly = false )
        : QDialog( pParent ), m_pGui( pGui ), m_pBrowser( pBrowser ), m_bEmbedded( bEmbedded ), m_bShaderOnly( bShaderOnly )
    {
        CY_ASSERT( pGui != nullptr );
        setObjectName( QStringLiteral( "EditorDatabaseView" ) );
        setWindowTitle( QStringLiteral( "Content Library" ) );
        if ( m_bEmbedded ) { setWindowFlags( Qt::Widget ); }
        else { setWindowFlag( Qt::Window, true ); }
        setSizeGripEnabled( !m_bEmbedded );
        resize( 1280, 820 );
        Build();
        if ( m_bShaderOnly ) {
            SetTab( DATABASE_TAB_SHADERS );
            for ( int tab = 0; tab < DATABASE_TAB_COUNT; ++tab ) { m_pTabs->setTabVisible( tab, tab == DATABASE_TAB_SHADERS ); }
            m_pTabs->tabBar()->hide();
        }
    }

    // ---- Public operations ---------------------------------------------

    void SetEntitySource( editor_database_entities_fn pfnEntities, editor_database_record_fn pfnRecord, editor_database_go_to_fn pfnGoTo, void *pContext )
    {
        m_pfnEntities = pfnEntities;
        m_pfnRecord = pfnRecord;
        m_pfnGoTo = pfnGoTo;
        m_pEntityContext = pContext;
        for ( page_t &page : m_pages ) {
            if ( !IsEntityTab( page.tab ) ) { continue; }
            RefreshLibrary( page );
            if ( !page.current.isEmpty() ) { ( void )ShowEntity( page, page.current.mid( 1 ).toULongLong() ); }
        }
        UpdateActions();
    }

    void Refresh()
    {
        for ( page_t &page : m_pages ) {
            RefreshLibrary( page );
            if ( IsEntityTab( page.tab ) && !page.current.isEmpty() ) { ( void )ShowEntity( page, page.current.mid( 1 ).toULongLong() ); }
            else if ( IsFileTab( page.tab ) && !page.current.isEmpty() ) { ( void )OpenFile( page, page.current ); }
        }
        // A cached picker has its own rows and thumbnails over the same
        // catalogue; refreshing libraries must not leave its indices stale.
        if ( m_pPicker != nullptr ) { EditorAssetWindow_Refresh( m_pPicker ); }
    }

    bool Open( const QString &path )
    {
        const int tab = TabOfKind( KindOfPath( path ) );
        if ( tab < 0 || ( m_bShaderOnly && tab != DATABASE_TAB_SHADERS ) || FileOf( path ).isEmpty() ) { return false; }
        SetTab( tab );
        return OpenIn( m_pages[tab], path );
    }

    bool OpenEntity( u64 id )
    {
        if ( m_bShaderOnly ) { return false; }
        int target = DATABASE_TAB_ENTITIES;
        if ( IsEntityTab( Tab() ) ) {
            for ( const auto &entity : m_pfnEntities != nullptr ? m_pfnEntities( m_pEntityContext ) : QVector<editor_database_entity_t>() ) {
                if ( entity.id == id && EntityMatches( Tab(), entity, m_pfnRecord != nullptr ? m_pfnRecord( m_pEntityContext, id ) : nullptr ) ) { target = Tab(); break; }
            }
        }
        SetTab( target ); RefreshLibrary( m_pages[target] );
        return ShowEntity( m_pages[target], id );
    }

    void SetTab( int tab )
    {
        if ( m_bShaderOnly && tab != DATABASE_TAB_SHADERS ) { return; }
        if ( tab >= 0 && tab < DATABASE_TAB_COUNT && m_pTabs->currentIndex() != tab ) { m_pTabs->setCurrentIndex( tab ); }
    }
    int Tab() const { return m_pTabs->currentIndex(); }
    page_t &Current() { return m_pages[Tab()]; }
    QString CurrentPath() { return Current().current; }

    QStringList Library()
    {
        QStringList paths;
        for ( QTreeWidgetItemIterator it( Current().pLibrary ); *it != nullptr; ++it ) {
            const QString path = ( *it )->data( 0, Qt::UserRole ).toString();
            if ( !path.isEmpty() ) { paths.append( path ); }
        }
        return paths;
    }

    QStringList Properties()
    {
        QStringList lines;
        page_t &page = Current();
        for ( const row_t &row : page.rows ) {
            QString value;
            if ( row.kind == row_kind_t::READONLY ) { value = row.readOnlyText; }
            else {
                const QVariant set = ValueOf( Recipe_Find( page.recipe, row.key ) );
                value = TextOf( set.isValid() ? set : row.fallback );
            }
            lines.append( RowId( row ) + QLatin1Char( '=' ) + value );
        }
        return lines;
    }

    bool SetProperty( const QString &id, const QString &text )
    {
        page_t &page = Current();
        for ( const row_t &row : page.rows ) {
            if ( RowId( row ) != id ) { continue; }
            bool bOk = false;
            const QVariant value = ParseValue( row, text, &bOk );
            if ( !bOk ) { return false; }
            return Apply( page, row, value, true );
        }
        return false;
    }

    bool ResetProperty( const QString &id )
    {
        page_t &page = Current();
        for ( const row_t &row : page.rows ) {
            if ( RowId( row ) != id || !row.bResettable ) { continue; }
            return Reset( page, row );
        }
        return false;
    }

    bool OpenRow( const QString &id )
    {
        page_t &page = Current();
        for ( const row_t &row : page.rows ) {
            if ( RowId( row ) == id && row.kind == row_kind_t::PATH ) { return FollowPath( page, row ); }
        }
        return false;
    }

    bool IsModified()
    {
        page_t &page = Current();
        if ( page.tab == DATABASE_TAB_SHADERS ) {
            for ( const code_tab_t &tab : m_codeTabs ) {
                if ( EditorCodeEditor_IsModified( tab.pEditor ) ) { return true; }
            }
        }
        return page.recipe.bDirty;
    }

    bool Save()
    {
        page_t &page = Current();
        QString error;
        if ( page.tab == DATABASE_TAB_SHADERS ) {
            // Validate the edited recipe before writing any stage. A failed
            // parse must leave the authored buffers and the saved asset intact.
            for ( const code_tab_t &tab : m_codeTabs ) {
                if ( !tab.bRecipe || !EditorCodeEditor_IsModified( tab.pEditor ) ) { continue; }
                settings_document_t parsed;
                if ( !Recipe_Parse( &parsed, m_pGui->pAllocator, EditorCodeEditor_TextOf( tab.pEditor ).toUtf8(),
                                    tab.path, "cypher.shader", &error ) ) {
                    Report( error );
                    return false;
                }
            }
            bool bOk = true;
            for ( code_tab_t &tab : m_codeTabs ) {
                if ( !EditorCodeEditor_IsModified( tab.pEditor ) ) { continue; }
                if ( WriteFile( tab.file, EditorCodeEditor_TextOf( tab.pEditor ).toUtf8(), &error ) ) { EditorCodeEditor_SetModified( tab.pEditor, false ); }
                else { bOk = false; Report( error ); }
            }
            if ( bOk && page.recipe.bDirty ) { bOk = Recipe_Save( page.recipe, m_pGui->pAllocator, &error ); }
            if ( !bOk ) { Report( error ); return false; }
            const QString path = page.current;
            const bool bReloaded = OpenIn( page, path, true ); // The recipe text is the source of truth: read it back.
            UpdateActions();
            return bReloaded;
        }
        if ( !page.recipe.IsOpen() ) { return false; }
        if ( !Recipe_Save( page.recipe, m_pGui->pAllocator, &error ) ) {
            Report( error );
            return false;
        }
        UpdateActions();
        return true;
    }

    bool Revert()
    {
        page_t &page = Current();
        if ( page.current.isEmpty() || !page.recipe.IsOpen() ) { return false; }
        const QString path = page.current;
        return OpenIn( page, path, true );
    }

    QWidget *CodeEditor( int index ) const { return index >= 0 && index < m_codeTabs.size() ? m_codeTabs[index].pEditor : nullptr; }
    int CodeEditorCount() const { return static_cast<int>( m_codeTabs.size() ); }

    bool NewShader( const QString &name, QString *pPathOut )
    {
        static const QRegularExpression valid( QStringLiteral( "^[a-z][a-z0-9_]{0,63}$" ) );
        const QStringList roots = m_pBrowser != nullptr ? EditorAssetBrowser_Roots( m_pBrowser ) : QStringList();
        if ( !valid.match( name ).hasMatch() || roots.isEmpty() ) {
            Report( QStringLiteral( "A shader name is lower-case letters, digits, and underscores, and needs a content folder" ) );
            return false;
        }
        const QString path = QStringLiteral( "shaders/%1.cyshader" ).arg( name );
        const QDir root( roots.front() );
        const QString vertex = QStringLiteral( "shaders/%1.vert" ).arg( name );
        const QString fragment = QStringLiteral( "shaders/%1.frag" ).arg( name );
        // Stages may exist without a recipe (imported source, or a recipe
        // removed earlier). New Shader must never overwrite those assets.
        for ( const QString &candidate : { path, vertex, fragment } ) {
            if ( QFileInfo::exists( root.filePath( candidate ) ) ) {
                Report( QStringLiteral( "%1 already exists" ).arg( candidate ) );
                return false;
            }
        }
        QString error;
        if ( !QDir().mkpath( QFileInfo( root.filePath( path ) ).absolutePath() ) ||
             !WriteFile( root.filePath( path ), QStringLiteral( "@cykv 1\n@schema \"cypher.shader\" 1\n{\n    language = \"glsl\"\n    vertex = \"shaders/%1.vert\"\n"
                                                                "    fragment = \"shaders/%1.frag\"\n}\n" ).arg( name ).toUtf8(), &error ) ||
             !WriteFile( root.filePath( vertex ), VertexTemplate( name ), &error ) ||
             !WriteFile( root.filePath( fragment ), FragmentTemplate( name ), &error ) ) {
            Report( error );
            return false;
        }
        if ( m_pBrowser != nullptr ) { EditorAssetBrowser_Rescan( m_pBrowser ); }
        Refresh();
        if ( pPathOut != nullptr ) { *pPathOut = path; }
        return Open( path );
    }

    QStringList UsedBy() const
    {
        QStringList items;
        const page_t &page = m_pages[Tab()];
        if ( page.pUsedBy == nullptr ) { return items; }
        for ( int i = 0; i < page.pUsedBy->count(); ++i ) {
            const QString path = page.pUsedBy->item( i )->data( Qt::UserRole ).toString();
            if ( !path.isEmpty() ) { items.append( path ); }
        }
        return items;
    }

    void SetChannel( const QString &channel )
    {
        const int index = m_pChannel->findData( channel );
        if ( index >= 0 ) { m_pChannel->setCurrentIndex( index ); }
    }

    QImage PreviewImage() const { return m_previewImage; }

protected:
    bool event( QEvent *pEvent ) override
    {
        if ( m_bEmbedded && pEvent->type() == QEvent::ShortcutOverride &&
             static_cast<QKeyEvent *>( pEvent )->matches( QKeySequence::Save ) ) {
            // Claim Save from nested editors before the main window's map
            // action can handle it, even when this content has no changes.
            pEvent->accept();
            return true;
        }
        return QDialog::event( pEvent );
    }

    void reject() override
    {
        if ( !m_bEmbedded ) { QDialog::reject(); }
    }

    void keyPressEvent( QKeyEvent *pEvent ) override
    {
        if ( m_bEmbedded && pEvent->matches( QKeySequence::Save ) ) {
            if ( m_pSave != nullptr && m_pSave->isEnabled() ) { m_pSave->click(); }
            pEvent->accept();
            return;
        }
        // Embedded editors use ordinary widget key handling. QDialog's
        // Escape / default-button behavior must not hide a cached pane.
        if ( m_bEmbedded ) { QWidget::keyPressEvent( pEvent ); }
        else { QDialog::keyPressEvent( pEvent ); }
    }

private:
    // ---- Construction ----------------------------------------------------

    QToolButton *Button( QWidget *pParent, const QString &text, const char *pIcon, const QString &tip, const QString &name )
    {
        auto *pButton = new QToolButton( pParent );
        pButton->setObjectName( name );
        pButton->setText( text );
        pButton->setToolTip( tip );
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

        // Sandbox's toolbar: what acts on the open item.
        auto *pBar = new QHBoxLayout();
        m_pSave = Button( this, QStringLiteral( "Save" ), "file-save", QStringLiteral( "Write the recipe (and edited shader sources)" ), QStringLiteral( "DatabaseSave" ) );
        m_pRevert = Button( this, QStringLiteral( "Revert" ), "edit-undo", QStringLiteral( "Discard changes and read the file again" ), QStringLiteral( "DatabaseRevert" ) );
        m_pExternal = Button( this, QStringLiteral( "External Edit" ), "file-open", QStringLiteral( "Open the file in the system's editor" ), QStringLiteral( "DatabaseExternal" ) );
        m_pReveal = Button( this, QStringLiteral( "Show in Folder" ), "asset-folder", QStringLiteral( "Open the folder that holds the file" ), QStringLiteral( "DatabaseReveal" ) );
        m_pNewShader = Button( this, QStringLiteral( "New Shader..." ), "asset-shader", QStringLiteral( "A recipe with vertex and fragment stages from a template" ), QStringLiteral( "DatabaseNewShader" ) );
        QObject::connect( m_pSave, &QToolButton::clicked, this, [this]() { ( void )Save(); } );
        QObject::connect( m_pRevert, &QToolButton::clicked, this, [this]() { ( void )Revert(); } );
        QObject::connect( m_pExternal, &QToolButton::clicked, this, [this]() {
            const QString file = CurrentFile();
            if ( !file.isEmpty() ) { QDesktopServices::openUrl( QUrl::fromLocalFile( file ) ); }
        } );
        QObject::connect( m_pReveal, &QToolButton::clicked, this, [this]() {
            const QString file = CurrentFile();
            if ( !file.isEmpty() ) { QDesktopServices::openUrl( QUrl::fromLocalFile( QFileInfo( file ).absolutePath() ) ); }
        } );
        QObject::connect( m_pNewShader, &QToolButton::clicked, this, [this]() {
            bool bOk = false;
            const QString name = QInputDialog::getText( this, QStringLiteral( "New Shader" ), QStringLiteral( "Name (lower_case):" ), QLineEdit::Normal, QString(), &bOk );
            if ( bOk ) { ( void )NewShader( name, nullptr ); }
        } );
        for ( QToolButton *pButton : { m_pSave, m_pRevert, m_pExternal, m_pReveal, m_pNewShader } ) { pBar->addWidget( pButton ); }
        pBar->addStretch( 1 );
        m_pStatus = new QLabel( this );
        m_pStatus->setObjectName( QStringLiteral( "DatabaseStatus" ) );
        pBar->addWidget( m_pStatus );
        pLayout->addLayout( pBar );

        m_pTabs = new QTabWidget( this );
        m_pTabs->setObjectName( QStringLiteral( "DatabaseTabs" ) );
        m_pTabs->setDocumentMode( true );
        m_pTabs->setUsesScrollButtons( true );
        m_pTabs->tabBar()->setExpanding( false );
        m_pTabs->setElideMode( Qt::ElideNone );
        for ( int tab = 0; tab < DATABASE_TAB_COUNT; ++tab ) {
            m_pages[tab].tab = tab;
            m_pTabs->addTab( BuildPage( m_pages[tab] ), EditorStyle_Icon( m_pGui->style, kTabIcons[tab] ), QString::fromLatin1( kTabTitles[tab] ) );
        }
        // Full labels scroll naturally. A direct category menu remains usable
        // when the library is embedded in a narrow map pane.
        auto *categories = Button( m_pTabs, QStringLiteral( "Categories" ), "asset-browser",
            QStringLiteral( "Choose any content category" ), QStringLiteral( "DatabaseCategories" ) );
        categories->setAccessibleName( QStringLiteral( "Content categories" ) );
        categories->setPopupMode( QToolButton::InstantPopup );
        auto *categoryMenu = new QMenu( categories );
        categoryMenu->setObjectName( QStringLiteral( "DatabaseCategoriesMenu" ) );
        for ( int tab = 0; tab < DATABASE_TAB_COUNT; ++tab ) {
            auto *action = categoryMenu->addAction( EditorStyle_Icon( m_pGui->style, kTabIcons[tab] ), QString::fromLatin1( kTabTitles[tab] ) );
            action->setObjectName( QStringLiteral( "DatabaseCategory%1" ).arg( tab ) );
            action->setData( tab ); action->setCheckable( true ); action->setChecked( tab == Tab() );
            QObject::connect( action, &QAction::triggered, this, [this, tab, action]() {
                SetTab( tab );
                // Selecting the current category emits no currentChanged,
                // but its checkmark must remain selected after QAction toggles.
                action->setChecked( Tab() == tab );
            } );
        }
        categories->setMenu( categoryMenu );
        m_pTabs->setCornerWidget( categories, Qt::TopRightCorner );
        categories->setVisible( !m_bShaderOnly );
        QObject::connect( m_pTabs, &QTabWidget::currentChanged, this, [this, categoryMenu]( int tab ) {
            for ( QAction *action : categoryMenu->actions() ) { action->setChecked( action->data().toInt() == tab ); }
            if ( IsEntityTab( tab ) ) {
                RefreshLibrary( m_pages[tab] );
                if ( !m_pages[tab].current.isEmpty() ) { ( void )ShowEntity( m_pages[tab], m_pages[tab].current.mid( 1 ).toULongLong() ); }
            }
            UpdateActions();
        } );
        pLayout->addWidget( m_pTabs, 1 );
        Refresh();
        UpdateActions();
    }

    QWidget *BuildPage( page_t &page )
    {
        auto *pSplit = new QSplitter( Qt::Horizontal );
        pSplit->setObjectName( QStringLiteral( "DatabasePage%1" ).arg( page.tab ) );
        page.pRoot = pSplit;

        // Library: the tab's assets by folder (a VFS view), or entities by class.
        auto *pLeft = new QWidget( pSplit );
        auto *pLeftLayout = new QVBoxLayout( pLeft );
        pLeftLayout->setContentsMargins( 0, 0, 0, 0 );
        pLeftLayout->setSpacing( 2 );
        page.pFilter = new QLineEdit( pLeft );
        page.pFilter->setObjectName( QStringLiteral( "DatabaseFilter%1" ).arg( page.tab ) );
        page.pFilter->setPlaceholderText( IsEntityTab( page.tab ) ? QStringLiteral( "Filter by class, name, or reference" ) : QStringLiteral( "Filter the library" ) );
        page.pFilter->setClearButtonEnabled( true );
        QObject::connect( page.pFilter, &QLineEdit::textChanged, this, [this, &page]( const QString & ) { RefreshLibrary( page ); } );
        pLeftLayout->addWidget( page.pFilter );
        page.pLibrary = new QTreeWidget( pLeft );
        page.pLibrary->setObjectName( QStringLiteral( "DatabaseLibrary%1" ).arg( page.tab ) );
        page.pLibrary->setHeaderHidden( true );
        page.pLibrary->setIconSize( QSize( 16, 16 ) );
        QObject::connect( page.pLibrary, &QTreeWidget::itemActivated, this, [this, &page]( QTreeWidgetItem *pItem, int ) { Activate( page, pItem ); } );
        QObject::connect( page.pLibrary, &QTreeWidget::itemClicked, this, [this, &page]( QTreeWidgetItem *pItem, int ) { Activate( page, pItem ); } );
        pLeftLayout->addWidget( page.pLibrary, 1 );

        // Editor: header, preview, grid (and per tab: code, used by).
        auto *pRight = new QWidget( pSplit );
        auto *pRightLayout = new QVBoxLayout( pRight );
        pRightLayout->setContentsMargins( 4, 0, 0, 0 );
        auto *pHeader = new QHBoxLayout();
        page.pPreview = new QLabel( pRight );
        page.pPreview->setObjectName( QStringLiteral( "DatabasePreview%1" ).arg( page.tab ) );
        page.pPreview->setFixedSize( 132, 132 );
        page.pPreview->setAlignment( Qt::AlignCenter );
        page.pPreview->setStyleSheet( QStringLiteral( "background: %1; border: 1px solid %2;" )
                                          .arg( EditorStyle_TokenColor( m_pGui->style, "ui.deepest" ).name(), EditorStyle_TokenColor( m_pGui->style, "ui.edge" ).name() ) );
        auto *pTitles = new QVBoxLayout();
        page.pTitle = new QLabel( pRight );
        QFont titleFont = page.pTitle->font();
        titleFont.setBold( true );
        titleFont.setPointSizeF( titleFont.pointSizeF() + 2.0 );
        page.pTitle->setFont( titleFont );
        page.pSubtitle = new QLabel( pRight );
        page.pSubtitle->setWordWrap( true );
        page.pSubtitle->setTextInteractionFlags( Qt::TextSelectableByMouse );
        pTitles->addWidget( page.pTitle );
        pTitles->addWidget( page.pSubtitle );
        pTitles->addStretch( 1 );
        pHeader->addWidget( page.pPreview );
        pHeader->addLayout( pTitles, 1 );
        pRightLayout->addLayout( pHeader );

        page.pGrid = new QTreeWidget( pRight );
        page.pGrid->setObjectName( QStringLiteral( "DatabaseGrid%1" ).arg( page.tab ) );
        page.pGrid->setColumnCount( 2 );
        page.pGrid->setHeaderLabels( { QStringLiteral( "Property" ), QStringLiteral( "Value" ) } );
        page.pGrid->setRootIsDecorated( true );
        page.pGrid->setUniformRowHeights( false );
        page.pGrid->setSelectionMode( QAbstractItemView::NoSelection );
        page.pGrid->header()->setSectionResizeMode( 0, QHeaderView::Interactive );
        page.pGrid->header()->resizeSection( 0, 200 );
        page.pGrid->header()->setStretchLastSection( true );

        if ( page.tab == DATABASE_TAB_TEXTURES ) {
            // Texture panel: a zoomable preview with channel views over the grid.
            auto *pView = new QWidget( pRight );
            auto *pViewLayout = new QVBoxLayout( pView );
            pViewLayout->setContentsMargins( 0, 0, 0, 0 );
            auto *pControls = new QHBoxLayout();
            pControls->addWidget( new QLabel( QStringLiteral( "Zoom:" ), pView ) );
            m_pZoom = new QComboBox( pView );
            m_pZoom->setObjectName( QStringLiteral( "DatabaseTextureZoom" ) );
            for ( const auto &[label, factor] : { std::pair{ "Fit", 0.0 }, std::pair{ "25%", 0.25 }, std::pair{ "50%", 0.5 }, std::pair{ "100%", 1.0 },
                                                  std::pair{ "200%", 2.0 }, std::pair{ "400%", 4.0 } } ) {
                m_pZoom->addItem( QString::fromLatin1( label ), factor );
            }
            pControls->addWidget( m_pZoom );
            pControls->addWidget( new QLabel( QStringLiteral( "Channel:" ), pView ) );
            m_pChannel = new QComboBox( pView );
            m_pChannel->setObjectName( QStringLiteral( "DatabaseTextureChannel" ) );
            for ( const auto &[label, id] : { std::pair{ "RGB", "rgb" }, std::pair{ "Red", "r" }, std::pair{ "Green", "g" }, std::pair{ "Blue", "b" }, std::pair{ "Alpha", "a" } } ) {
                m_pChannel->addItem( QString::fromLatin1( label ), QString::fromLatin1( id ) );
            }
            pControls->addWidget( m_pChannel );
            m_pImageInfo = new QLabel( pView );
            pControls->addWidget( m_pImageInfo, 1 );
            pViewLayout->addLayout( pControls );
            m_pTextureScroll = new QScrollArea( pView );
            m_pTextureScroll->setObjectName( QStringLiteral( "DatabaseTextureView" ) );
            m_pTextureScroll->setAlignment( Qt::AlignCenter );
            m_pTextureImage = new QLabel();
            m_pTextureImage->setAlignment( Qt::AlignCenter );
            m_pTextureScroll->setWidget( m_pTextureImage );
            m_pTextureScroll->setWidgetResizable( false );
            pViewLayout->addWidget( m_pTextureScroll, 1 );
            QObject::connect( m_pZoom, &QComboBox::currentIndexChanged, this, [this]( int ) { ShowTextureImage(); } );
            QObject::connect( m_pChannel, &QComboBox::currentIndexChanged, this, [this]( int ) { ShowTextureImage(); } );
            auto *pVertical = new QSplitter( Qt::Vertical, pRight );
            pVertical->addWidget( pView );
            pVertical->addWidget( page.pGrid );
            pVertical->setSizes( { 420, 300 } );
            pRightLayout->addWidget( pVertical, 1 );
            page.pPreview->hide(); // The big view replaces the thumbnail.
        } else if ( page.tab == DATABASE_TAB_SHADERS ) {
            // Shader panel: the recipe's fields over the code editors.
            m_pCode = new QTabWidget( pRight );
            m_pCode->setObjectName( QStringLiteral( "DatabaseShaderCode" ) );
            m_pCode->setDocumentMode( true );
            auto *pVertical = new QSplitter( Qt::Vertical, pRight );
            pVertical->addWidget( page.pGrid );
            pVertical->addWidget( m_pCode );
            pVertical->setSizes( { 240, 520 } );
            pRightLayout->addWidget( pVertical, 1 );
        } else {
            pRightLayout->addWidget( page.pGrid, 1 );
        }

        if ( page.tab == DATABASE_TAB_SHADERS || page.tab == DATABASE_TAB_TEXTURES ) {
            auto *pUsedLabel = new QLabel( QStringLiteral( "Used by" ), pRight );
            pRightLayout->addWidget( pUsedLabel );
            page.pUsedBy = new QListWidget( pRight );
            page.pUsedBy->setObjectName( QStringLiteral( "DatabaseUsedBy%1" ).arg( page.tab ) );
            page.pUsedBy->setMaximumHeight( 96 );
            QObject::connect( page.pUsedBy, &QListWidget::itemDoubleClicked, this, [this]( QListWidgetItem *pItem ) {
                const QString path = pItem->data( Qt::UserRole ).toString();
                if ( !path.isEmpty() ) { ( void )Open( path ); }
            } );
            pRightLayout->addWidget( page.pUsedBy );
        }
        if ( IsEntityTab( page.tab ) ) {
            auto *pSelect = new QPushButton( QStringLiteral( "Select in Map" ), pRight );
            page.pSelect = pSelect;
            pSelect->setObjectName( QStringLiteral( "DatabaseSelectEntity" ) );
            pSelect->setIcon( EditorStyle_Icon( m_pGui->style, "view-properties" ) );
            pSelect->setEnabled( false );
            QObject::connect( pSelect, &QPushButton::clicked, this, [this, &page]() {
                bool bOk = false;
                const u64 id = page.current.mid( 1 ).toULongLong( &bOk );
                if ( bOk && m_pfnGoTo != nullptr && m_pfnRecord != nullptr && m_pfnRecord( m_pEntityContext, id ) != nullptr ) {
                    m_pfnGoTo( m_pEntityContext, id );
                }
            } );
            pRightLayout->addWidget( pSelect, 0, Qt::AlignLeft );
        }
        pSplit->addWidget( pLeft );
        pSplit->addWidget( pRight );
        pSplit->setSizes( { 280, 1000 } );
        ShowEmpty( page );
        return pSplit;
    }

    // ---- Library ---------------------------------------------------------

    void RefreshLibrary( page_t &page )
    {
        const QSignalBlocker blocker( page.pLibrary );
        page.pLibrary->clear();
        const QString filter = page.pFilter->text().trimmed();
        QHash<QString, QTreeWidgetItem *> groups;
        const QIcon folderIcon = EditorStyle_Icon( m_pGui->style, "asset-folder" );
        const auto groupItem = [&]( const QString &groupPath ) {
            QTreeWidgetItem *pParent = nullptr;
            QString partial;
            for ( const QString &part : groupPath.split( QLatin1Char( '/' ), Qt::SkipEmptyParts ) ) {
                partial = partial.isEmpty() ? part : partial + QLatin1Char( '/' ) + part;
                QTreeWidgetItem *&pItem = groups[partial];
                if ( pItem == nullptr ) {
                    pItem = pParent != nullptr ? new QTreeWidgetItem( pParent ) : new QTreeWidgetItem( page.pLibrary );
                    pItem->setText( 0, part );
                    pItem->setIcon( 0, folderIcon );
                }
                pParent = pItem;
            }
            return pParent;
        };
        if ( IsEntityTab( page.tab ) ) {
            const QVector<editor_database_entity_t> entities = m_pfnEntities != nullptr ? m_pfnEntities( m_pEntityContext ) : QVector<editor_database_entity_t>();
            const QIcon icon = EditorStyle_Icon( m_pGui->style, kTabIcons[page.tab] );
            for ( const editor_database_entity_t &entity : entities ) {
                const auto *record = m_pfnRecord != nullptr ? m_pfnRecord( m_pEntityContext, entity.id ) : nullptr;
                if ( !EntityMatches( page.tab, entity, record ) ) { continue; }
                const QString label = entity.name.isEmpty() ? QStringLiteral( "#%1" ).arg( entity.id ) : QStringLiteral( "%1  (#%2)" ).arg( entity.name ).arg( entity.id );
                if ( !filter.isEmpty() && !entity.className.contains( filter, Qt::CaseInsensitive ) && !label.contains( filter, Qt::CaseInsensitive ) && !EntityReferences( record ).contains( filter, Qt::CaseInsensitive ) ) { continue; }
                QTreeWidgetItem *pParent = groupItem( entity.className.isEmpty() ? QStringLiteral( "(no class)" ) : entity.className );
                auto *pItem = new QTreeWidgetItem( pParent );
                pItem->setText( 0, label );
                pItem->setIcon( 0, icon );
                pItem->setData( 0, Qt::UserRole, QStringLiteral( "#%1" ).arg( entity.id ) );
                pItem->setToolTip( 0, entity.className + QStringLiteral( " · " ) + label );
                if ( page.current == QStringLiteral( "#%1" ).arg( entity.id ) ) { page.pLibrary->setCurrentItem( pItem ); }
            }
            page.pLibrary->expandAll();
            return;
        }
        const editor_asset_catalog_t *pCatalog = Catalog();
        if ( pCatalog == nullptr ) { return; }
        const editor_asset_kind_t kind = KindOfTab( page.tab );
        const QIcon icon = EditorStyle_Icon( m_pGui->style, kTabIcons[page.tab] );
        for ( usize i = 0u; i < EditorAssets_Count( pCatalog ); ++i ) {
            const editor_asset_t *pAsset = EditorAssets_At( pCatalog, i );
            if ( pAsset == nullptr || pAsset->kind != kind || ( pAsset->bSource && !IsFileTab( page.tab ) ) ) { continue; }
            const QString path = FromView( EditorAssets_Path( pCatalog, *pAsset ) );
            if ( !filter.isEmpty() && !path.contains( filter, Qt::CaseInsensitive ) ) { continue; }
            QTreeWidgetItem *pParent = groupItem( FromView( EditorAssets_Folder( pCatalog, *pAsset ) ) );
            auto *pItem = pParent != nullptr ? new QTreeWidgetItem( pParent ) : new QTreeWidgetItem( page.pLibrary );
            pItem->setText( 0, FromView( EditorAssets_Name( pCatalog, *pAsset ) ) );
            pItem->setIcon( 0, icon );
            pItem->setData( 0, Qt::UserRole, path );
            pItem->setToolTip( 0, path );
            if ( path == page.current ) { page.pLibrary->setCurrentItem( pItem ); }
        }
        page.pLibrary->expandToDepth( filter.isEmpty() ? 1 : 8 );
    }

    void Activate( page_t &page, QTreeWidgetItem *pItem )
    {
        const QString path = pItem != nullptr ? pItem->data( 0, Qt::UserRole ).toString() : QString();
        if ( path.isEmpty() || path == page.current ) { return; }
        if ( !ConfirmLeave( page ) ) { return; }
        if ( IsEntityTab( page.tab ) ) { ( void )ShowEntity( page, path.mid( 1 ).toULongLong() ); }
        else { ( void )OpenIn( page, path ); }
    }

    // Unsaved edits are never lost by clicking another item.
    bool ConfirmLeave( page_t &page )
    {
        const bool bCodeModified = page.tab == DATABASE_TAB_SHADERS && std::any_of( m_codeTabs.begin(), m_codeTabs.end(),
            []( const code_tab_t &tab ) { return EditorCodeEditor_IsModified( tab.pEditor ); } );
        if ( !page.recipe.bDirty && !bCodeModified ) { return true; }
        const auto answer = QMessageBox::question( this, QStringLiteral( "Unsaved Changes" ),
            QStringLiteral( "%1 has unsaved changes. Save them?" ).arg( page.current ), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel );
        if ( answer == QMessageBox::Cancel ) { return false; }
        if ( answer == QMessageBox::Save ) { return Save(); }
        return true;
    }

    // ---- Opening ---------------------------------------------------------

    bool OpenIn( page_t &page, const QString &path, bool bForce = false )
    {
        if ( IsFileTab( page.tab ) ) { return OpenFile( page, path ); }
        if ( !bForce && path == page.current && page.recipe.IsOpen() ) { return true; }
        const editor_asset_kind_t kind = KindOfTab( page.tab );
        QString error;
        page.current = path;
        if ( !Recipe_Load( page.recipe, m_pGui->pAllocator, FileOf( path ), path, SchemaOf( kind ), &error ) ) {
            page.rows.clear();
            page.pGrid->clear();
            page.pTitle->setText( QFileInfo( path ).completeBaseName() );
            page.pSubtitle->setText( error );
            Report( error );
            if ( page.tab == DATABASE_TAB_SHADERS ) { BuildCodeTabs( page ); }
            UpdateActions();
            return false;
        }
        page.pTitle->setText( QFileInfo( path ).completeBaseName() );
        switch ( page.tab ) {
            case DATABASE_TAB_MATERIALS: BuildMaterialRows( page ); break;
            case DATABASE_TAB_SHADERS: BuildShaderRows( page ); break;
            case DATABASE_TAB_TEXTURES: BuildTextureRows( page ); break;
            default: BuildGenericRows( page ); break;
        }
        RebuildGrid( page );
        if ( page.tab == DATABASE_TAB_SHADERS ) { BuildCodeTabs( page ); }
        UpdateHeader( page );
        UpdateUsedBy( page );
        // Keep the library's highlight on what is open.
        for ( QTreeWidgetItemIterator it( page.pLibrary ); *it != nullptr; ++it ) {
            if ( ( *it )->data( 0, Qt::UserRole ).toString() == path ) {
                const QSignalBlocker blocker( page.pLibrary );
                page.pLibrary->setCurrentItem( *it );
                page.pLibrary->scrollToItem( *it );
                break;
            }
        }
        Report( QString() );
        UpdateActions();
        return true;
    }

    void ShowEmpty( page_t &page )
    {
        page.pTitle->setText( QString::fromLatin1( kTabTitles[page.tab] ) );
        QString text = QStringLiteral( "Choose an item in the library to inspect and edit it" );
        if ( IsFileTab( page.tab ) ) { text = QStringLiteral( "Browse project files and inspect their source information" ); }
        else if ( IsEntityTab( page.tab ) ) {
            text = QStringLiteral( "Browse instances in the open map; Select in Map opens the editable inspector" );
            if ( page.tab == DATABASE_TAB_SCRIPTS ) { text += QStringLiteral( ". Script references and logic_script records; source editing is separate." ); }
            else if ( page.tab == DATABASE_TAB_SEQUENCES ) { text += QStringLiteral( ". Authored sequence records; playback is not available." ); }
            else if ( page.tab == DATABASE_TAB_TRIGGERS ) { text += QStringLiteral( ". Classes beginning with trigger_." ); }
        }
        page.pSubtitle->setText( text ); page.pPreview->clear();
    }

    void ClearPage( page_t &page )
    {
        page.current.clear(); page.rows.clear(); page.recipe.Close(); page.editors.clear(); page.pGrid->clear();
        ShowEmpty( page ); UpdateActions();
    }

    bool OpenFile( page_t &page, const QString &path )
    {
        const auto bytes = path.toUtf8(); const auto *catalog = Catalog();
        const auto *asset = catalog != nullptr ? EditorAssets_Find( catalog, ViewOf( bytes ) ) : nullptr;
        const QString file = asset != nullptr ? FileOf( path ) : QString();
        if ( asset == nullptr || asset->kind != KindOfTab( page.tab ) || !QFileInfo::exists( file ) ) { ClearPage( page ); return false; }
        page.recipe.Close(); page.current = path; page.rows.clear();
        const QStringList roots = EditorAssetBrowser_Roots( m_pBrowser );
        const auto row = [&page]( const QString &name, const QString &value ) {
            row_t r{ QStringLiteral( "File" ), name, {}, row_kind_t::READONLY }; r.readOnlyText = value; page.rows.push_back( r );
        };
        row( QStringLiteral( "Path" ), path );
        row( QStringLiteral( "Type" ), QString::fromLatin1( EditorAssets_KindLabel( asset->kind ) ) );
        row( QStringLiteral( "Size" ), QStringLiteral( "%1 bytes" ).arg( asset->cbSize ) );
        row( QStringLiteral( "Content root" ), asset->iRoot < roots.size() ? roots[asset->iRoot] : QString() );
        row( QStringLiteral( "Source" ), asset->bSource ? QStringLiteral( "true" ) : QStringLiteral( "false" ) );
        RebuildGrid( page ); page.pTitle->setText( QFileInfo( path ).fileName() );
        page.pSubtitle->setText( QStringLiteral( "%1 · file information. Specialized preview and editing are not available here yet." ).arg( path ) );
        page.pPreview->setPixmap( EditorStyle_Icon( m_pGui->style, kTabIcons[page.tab] ).pixmap( QSize( 64, 64 ), devicePixelRatioF() ) );
        for ( QTreeWidgetItemIterator it( page.pLibrary ); *it != nullptr; ++it ) {
            if ( ( *it )->data( 0, Qt::UserRole ).toString() == path ) { const QSignalBlocker blocker( page.pLibrary ); page.pLibrary->setCurrentItem( *it ); break; }
        }
        Report( QString() ); UpdateActions(); return true;
    }

    // ---- Materials ---------------------------------------------------------

    void BuildMaterialRows( page_t &page )
    {
        page.rows.clear();
        recipe_t &material = page.recipe;
        const bool bV2 = material.version >= 2u;
        const QString shaderPath = ValueOf( Recipe_Find( material, { QStringLiteral( "shader" ) } ) ).toString();
        m_interface = {};
        recipe_t shader;
        QString ignored;
        if ( !shaderPath.isEmpty() && !FileOf( shaderPath ).isEmpty() &&
             Recipe_Load( shader, m_pGui->pAllocator, FileOf( shaderPath ), shaderPath, "cypher.shader", &ignored ) ) {
            m_interface = ReadInterface( shader );
        }
        const QString settings = QStringLiteral( "Material Settings" );
        row_t shaderRow{ settings, QStringLiteral( "Shader" ), { QStringLiteral( "shader" ) }, row_kind_t::PATH, editor_asset_kind_t::SHADER };
        shaderRow.tip = QStringLiteral( "The .cyshader that draws this material; the folder opens it in the Shaders tab" );
        shaderRow.bRequired = !bV2;
        page.rows.push_back( shaderRow );
        if ( bV2 ) {
            row_t base{ settings, QStringLiteral( "Base" ), { QStringLiteral( "base" ) }, row_kind_t::PATH, editor_asset_kind_t::MATERIAL };
            base.tip = QStringLiteral( "A material this one inherits from; unset fields come from it" );
            page.rows.push_back( base );
            row_t domain{ settings, QStringLiteral( "Domain" ), { QStringLiteral( "domain" ) }, row_kind_t::ENUM };
            domain.choices = { QStringLiteral( "surface" ), QStringLiteral( "decal" ), QStringLiteral( "ui" ), QStringLiteral( "postprocess" ), QStringLiteral( "particle" ) };
            domain.fallback = QStringLiteral( "surface" );
            page.rows.push_back( domain );
            row_t surface{ settings, QStringLiteral( "Surface" ), { QStringLiteral( "surface" ) }, row_kind_t::TEXT };
            surface.tip = QStringLiteral( "The .cysurface (footsteps, impacts, physics) for surface and decal domains" );
            page.rows.push_back( surface );

            const QString state = QStringLiteral( "Opacity and State" );
            row_t alpha{ state, QStringLiteral( "Alpha mode" ), { QStringLiteral( "state" ), QStringLiteral( "alpha_mode" ) }, row_kind_t::ENUM };
            alpha.choices = { QStringLiteral( "opaque" ), QStringLiteral( "mask" ), QStringLiteral( "blend" ), QStringLiteral( "additive" ) };
            alpha.fallback = QStringLiteral( "opaque" );
            page.rows.push_back( alpha );
            row_t cutoff{ state, QStringLiteral( "Alpha cutoff" ), { QStringLiteral( "state" ), QStringLiteral( "alpha_cutoff" ) }, row_kind_t::NUMBER };
            cutoff.minimum = 0.0;
            cutoff.maximum = 1.0;
            cutoff.bBounded = true;
            cutoff.fallback = 0.5;
            cutoff.tip = QStringLiteral( "Mask mode: alpha below this is cut" );
            page.rows.push_back( cutoff );
            for ( const auto &[label, key, fallback] : { std::tuple{ "Two sided", "two_sided", false }, std::tuple{ "Casts shadows", "casts_shadows", true },
                                                        std::tuple{ "Receives shadows", "receives_shadows", true } } ) {
                row_t flag{ state, QString::fromLatin1( label ), { QStringLiteral( "state" ), QString::fromLatin1( key ) }, row_kind_t::BOOL };
                flag.fallback = fallback;
                page.rows.push_back( flag );
            }
        }

        // Texture maps: what the material sets, and every slot its shader declares.
        const QString maps = QStringLiteral( "Texture Maps" );
        QStringList slotNames = ChildNames( Recipe_Find( material, { QStringLiteral( "textures" ) } ) );
        for ( const slot_t &slot : m_interface.textureSlots ) {
            if ( !slotNames.contains( slot.name ) ) { slotNames.append( slot.name ); }
        }
        for ( const QString &name : slotNames ) {
            row_t row{ maps, name, bV2 ? QStringList{ QStringLiteral( "textures" ), name, QStringLiteral( "resource" ) } : QStringList{ QStringLiteral( "textures" ), name },
                       row_kind_t::PATH, editor_asset_kind_t::TEXTURE };
            row.bResettable = true;
            for ( const slot_t &slot : m_interface.textureSlots ) {
                if ( slot.name != name ) { continue; }
                row.bRequired = slot.bRequired;
                row.tip = QStringLiteral( "%1 · %2 · %3%4" ).arg( slot.type, slot.usage, slot.colorSpace.isEmpty() ? QStringLiteral( "default colour space" ) : slot.colorSpace,
                                                                  slot.bRequired ? QStringLiteral( " · required" ) : QString() );
            }
            if ( row.tip.isEmpty() ) { row.tip = QStringLiteral( "Not declared by the shader's interface (schema 1 shaders declare none)" ); }
            page.rows.push_back( row );
        }

        // Shader params: the material's values, typed by the shader's declaration.
        const QString params = QStringLiteral( "Shader Params" );
        QStringList parameterNames = ChildNames( Recipe_Find( material, { QStringLiteral( "parameters" ) } ) );
        for ( const parameter_t &parameter : m_interface.parameters ) {
            if ( !parameterNames.contains( parameter.name ) && parameter.type != QStringLiteral( "mat3" ) && parameter.type != QStringLiteral( "mat4" ) ) {
                parameterNames.append( parameter.name );
            }
        }
        for ( const QString &name : parameterNames ) {
            row_t row{ params, name, { QStringLiteral( "parameters" ), name } };
            row.bResettable = true;
            const parameter_t *pDeclared = nullptr;
            for ( const parameter_t &parameter : m_interface.parameters ) {
                if ( parameter.name == name ) { pDeclared = &parameter; }
            }
            const QVariant value = ValueOf( Recipe_Find( material, row.key ) );
            if ( pDeclared != nullptr ) {
                const QString &type = pDeclared->type;
                row.fallback = pDeclared->fallback;
                row.components = Components( type );
                row.kind = type == QStringLiteral( "bool" ) ? row_kind_t::BOOL
                         : type == QStringLiteral( "i32" ) || type == QStringLiteral( "u32" ) ? row_kind_t::INTEGER
                         : type == QStringLiteral( "f32" ) ? row_kind_t::NUMBER
                         : type.startsWith( QStringLiteral( "color" ) ) ? row_kind_t::COLOR : row_kind_t::VECTOR;
                row.bBounded = pDeclared->bBounded;
                if ( row.bBounded ) { row.minimum = pDeclared->minimum; row.maximum = pDeclared->maximum; }
                if ( type == QStringLiteral( "u32" ) ) { row.minimum = std::max( row.minimum, 0.0 ); }
                row.tip = QStringLiteral( "%1%2%3" ).arg( type, row.bBounded ? QStringLiteral( " in [%1, %2]" ).arg( row.minimum ).arg( row.maximum ) : QString(),
                                                          row.fallback.isValid() ? QStringLiteral( " · default %1" ).arg( TextOf( row.fallback ) ) : QString() );
            } else {
                // Schema 1: the value's own shape says what it is.
                row.tip = QStringLiteral( "Not declared by the shader's interface" );
                if ( value.typeId() == QMetaType::Bool ) { row.kind = row_kind_t::BOOL; }
                else if ( value.typeId() == QMetaType::QVariantList ) {
                    row.components = static_cast<int>( value.toList().size() );
                    const bool bColour = name.contains( QStringLiteral( "color" ) ) || name.contains( QStringLiteral( "colour" ) ) || name.contains( QStringLiteral( "tint" ) );
                    row.kind = bColour && ( row.components == 3 || row.components == 4 ) ? row_kind_t::COLOR : row_kind_t::VECTOR;
                } else if ( value.typeId() == QMetaType::LongLong || value.typeId() == QMetaType::ULongLong ) { row.kind = row_kind_t::INTEGER; }
                else { row.kind = row_kind_t::NUMBER; }
            }
            page.rows.push_back( row );
        }
        for ( const feature_t &feature : m_interface.features ) {
            row_t row{ QStringLiteral( "Shader Features" ), feature.name, { QStringLiteral( "features" ), feature.name } };
            row.kind = feature.type == QStringLiteral( "enum" ) ? row_kind_t::ENUM : row_kind_t::BOOL;
            row.choices = feature.values;
            row.fallback = feature.fallback.isValid() ? feature.fallback : QVariant( false );
            row.bResettable = true;
            if ( bV2 ) { page.rows.push_back( row ); }
        }
    }

    // ---- Shaders -----------------------------------------------------------

    void BuildShaderRows( page_t &page )
    {
        page.rows.clear();
        recipe_t &shader = page.recipe;
        const bool bV2 = shader.version >= 2u;
        const QString recipe = QStringLiteral( "Shader Settings" );
        row_t language{ recipe, QStringLiteral( "Language" ), { QStringLiteral( "language" ) }, row_kind_t::ENUM };
        language.choices = { QStringLiteral( "glsl" ) };
        page.rows.push_back( language );
        const QString stages = QStringLiteral( "Stages" );
        if ( bV2 ) {
            for ( const char *pStage : { "vertex", "fragment" } ) {
                row_t source{ stages, QStringLiteral( "%1 source" ).arg( QString::fromLatin1( pStage ) ),
                              { QStringLiteral( "stages" ), QString::fromLatin1( pStage ), QStringLiteral( "source" ) }, row_kind_t::PATH, editor_asset_kind_t::SHADER };
                source.tip = QStringLiteral( "The folder shows this stage in the code editor" );
                page.rows.push_back( source );
                row_t entry{ stages, QStringLiteral( "%1 entry" ).arg( QString::fromLatin1( pStage ) ),
                             { QStringLiteral( "stages" ), QString::fromLatin1( pStage ), QStringLiteral( "entry" ) }, row_kind_t::TEXT };
                entry.fallback = QStringLiteral( "main" );
                page.rows.push_back( entry );
            }
        } else {
            for ( const char *pStage : { "vertex", "fragment" } ) {
                row_t source{ stages, QString::fromLatin1( pStage ), { QString::fromLatin1( pStage ) }, row_kind_t::PATH, editor_asset_kind_t::SHADER };
                source.tip = QStringLiteral( "The folder shows this stage in the code editor" );
                page.rows.push_back( source );
            }
        }
        row_t defines{ recipe, QStringLiteral( "Defines" ), { QStringLiteral( "defines" ) }, row_kind_t::LIST };
        defines.tip = QStringLiteral( "Preprocessor names, separated by spaces or commas" );
        page.rows.push_back( defines );
        const shader_interface_t declared = ReadInterface( shader );
        const QString contract = QStringLiteral( "Interface" );
        if ( !declared.bKnown ) {
            row_t note{ contract, QStringLiteral( "Declared" ), {}, row_kind_t::READONLY };
            note.readOnlyText = bV2 ? QStringLiteral( "No interface block" ) : QStringLiteral( "Schema 1 shaders declare no interface; materials set values by name" );
            page.rows.push_back( note );
        }
        for ( const slot_t &slot : declared.textureSlots ) {
            row_t row{ contract, QStringLiteral( "texture %1" ).arg( slot.name ), {}, row_kind_t::READONLY };
            row.readOnlyText = QStringLiteral( "%1, %2%3%4" ).arg( slot.type, slot.usage, slot.colorSpace.isEmpty() ? QString() : QStringLiteral( ", " ) + slot.colorSpace,
                                                                    slot.bRequired ? QStringLiteral( ", required" ) : QString() );
            page.rows.push_back( row );
        }
        for ( const parameter_t &parameter : declared.parameters ) {
            row_t row{ contract, QStringLiteral( "param %1" ).arg( parameter.name ), {}, row_kind_t::READONLY };
            row.readOnlyText = parameter.type + ( parameter.fallback.isValid() ? QStringLiteral( " = %1" ).arg( TextOf( parameter.fallback ) ) : QString() )
                             + ( parameter.bBounded ? QStringLiteral( " in [%1, %2]" ).arg( parameter.minimum ).arg( parameter.maximum ) : QString() );
            page.rows.push_back( row );
        }
    }

    QStringList StagePaths( const page_t &page ) const
    {
        QStringList paths;
        const bool bV2 = page.recipe.version >= 2u;
        for ( const char *pStage : { "vertex", "fragment" } ) {
            const QStringList key = bV2 ? QStringList{ QStringLiteral( "stages" ), QString::fromLatin1( pStage ), QStringLiteral( "source" ) } : QStringList{ QString::fromLatin1( pStage ) };
            const QString path = ValueOf( Recipe_Find( page.recipe, key ) ).toString();
            if ( !path.isEmpty() ) { paths.append( path ); }
        }
        return paths;
    }

    void BuildCodeTabs( page_t &page )
    {
        while ( m_pCode->count() > 0 ) {
            QWidget *pWidget = m_pCode->widget( 0 );
            m_pCode->removeTab( 0 );
            delete pWidget;
        }
        m_codeTabs.clear();
        if ( page.current.isEmpty() ) { return; }
        const auto addTab = [this]( const QString &path, const QString &file, editor_code_language_t language, bool bRecipe ) {
            QWidget *pEditor = EditorCodeEditor_Create( m_pCode, &m_pGui->style, language );
            pEditor->setObjectName( QStringLiteral( "DatabaseCode_%1" ).arg( QFileInfo( path ).fileName() ) );
            QFile in( file );
            const bool bRead = !file.isEmpty() && in.open( QIODevice::ReadOnly );
            EditorCodeEditor_SetText( pEditor, bRead ? QString::fromUtf8( in.readAll() ) : QStringLiteral( "// %1 was not found in the content folders\n" ).arg( path ) );
            EditorCodeEditor_SetReadOnly( pEditor, !bRead );
            const int index = m_pCode->addTab( pEditor, QFileInfo( path ).fileName() );
            m_pCode->setTabToolTip( index, path );
            QObject::connect( EditorCodeEditor_Text( pEditor ), &QPlainTextEdit::modificationChanged, this, [this, pEditor]( bool bModified ) {
                const int at = m_pCode->indexOf( pEditor );
                if ( at >= 0 ) {
                    QString title = m_pCode->tabText( at );
                    if ( title.endsWith( QLatin1Char( '*' ) ) ) { title.chop( 1 ); }
                    m_pCode->setTabText( at, bModified ? title + QLatin1Char( '*' ) : title );
                }
                UpdateActions();
            } );
            // A grid edit marks the recipe tab modified too. Further raw
            // edits do not emit modificationChanged again, so track text
            // changes to keep the property/source boundary accurate.
            QObject::connect( EditorCodeEditor_Text( pEditor ), &QPlainTextEdit::textChanged, this, [this]() { UpdateActions(); } );
            m_codeTabs.push_back( code_tab_t{ path, file, pEditor, bRecipe } );
        };
        addTab( page.current, FileOf( page.current ), editor_code_language_t::CYKV, true );
        for ( const QString &stage : StagePaths( page ) ) { addTab( stage, FileOf( stage ), editor_code_language_t::GLSL, false ); }
        if ( m_codeTabs.size() > 1u ) { m_pCode->setCurrentIndex( 1 ); } // The first stage: what an author came to edit.
    }

    // Grid edits rewrite the recipe tab, so the two never disagree.
    void SyncRecipeTab( page_t &page )
    {
        if ( page.tab != DATABASE_TAB_SHADERS || m_codeTabs.empty() ) { return; }
        QWidget *pEditor = m_codeTabs.front().pEditor;
        EditorCodeEditor_SetText( pEditor, QString::fromUtf8( Recipe_Text( page.recipe, m_pGui->pAllocator ) ) );
        EditorCodeEditor_SetModified( pEditor, true );
        page.recipe.bDirty = false; // The tab carries the change; Save writes the tab.
    }

    static QByteArray VertexTemplate( const QString &name )
    {
        return QStringLiteral(
                   "#version 410 core\n"
                   "// %1.vert: world transform, normal, and texture coordinates for %1.frag.\n"
                   "layout(location = 0) in vec3 position;\n"
                   "layout(location = 1) in vec3 normal;\n"
                   "layout(location = 2) in vec2 texcoord;\n"
                   "layout(std140) uniform Transforms {\n"
                   "    mat4 model;\n"
                   "    mat4 view;\n"
                   "    mat4 projection;\n"
                   "    vec4 tint;\n"
                   "    vec4 uvScale;\n"
                   "};\n"
                   "out vec3 worldNormal;\n"
                   "out vec4 surfaceTint;\n"
                   "out vec2 surfaceUV;\n"
                   "void main() {\n"
                   "    gl_Position = projection * view * model * vec4(position, 1.0);\n"
                   "    worldNormal = normalize(transpose(inverse(mat3(model))) * normal);\n"
                   "    surfaceTint = tint;\n"
                   "    surfaceUV = texcoord * uvScale.xy;\n"
                   "}\n" )
            .arg( name )
            .toUtf8();
    }

    static QByteArray FragmentTemplate( const QString &name )
    {
        return QStringLiteral(
                   "#version 410 core\n"
                   "// %1.frag: base colour, tinted, with simple directional light.\n"
                   "in vec3 worldNormal;\n"
                   "in vec4 surfaceTint;\n"
                   "in vec2 surfaceUV;\n"
                   "uniform sampler2D base_color;\n"
                   "layout(location = 0) out vec4 color;\n"
                   "void main() {\n"
                   "    vec3 lightDirection = normalize(vec3(0.4, -0.7, 1.0));\n"
                   "    float diffuse = max(dot(normalize(worldNormal), lightDirection), 0.0);\n"
                   "    vec3 surface = texture(base_color, surfaceUV).rgb * surfaceTint.rgb;\n"
                   "    color = vec4(surface * (0.30 + 0.70 * diffuse), 1.0);\n"
                   "}\n" )
            .arg( name )
            .toUtf8();
    }

    // ---- Textures ----------------------------------------------------------

    void BuildTextureRows( page_t &page )
    {
        page.rows.clear();
        const bool bV2 = page.recipe.version >= 2u;
        const QString settings = QStringLiteral( "Texture Settings" );
        row_t source{ settings, QStringLiteral( "Source" ), { QStringLiteral( "source" ) }, row_kind_t::PATH, editor_asset_kind_t::TEXTURE };
        source.tip = QStringLiteral( "The image the texture is built from; the folder shows it in its folder" );
        source.bRequired = true;
        page.rows.push_back( source );
        if ( bV2 ) {
            row_t type{ settings, QStringLiteral( "Type" ), { QStringLiteral( "type" ) }, row_kind_t::ENUM };
            type.choices = { QStringLiteral( "2d" ) };
            page.rows.push_back( type );
        }
        row_t usage{ settings, QStringLiteral( "Usage" ), { QStringLiteral( "usage" ) }, row_kind_t::ENUM };
        usage.choices = { QStringLiteral( "color" ), QStringLiteral( "normal" ), QStringLiteral( "data" ) };
        usage.fallback = QStringLiteral( "color" );
        page.rows.push_back( usage );
        row_t space{ settings, QStringLiteral( "Colour space" ), { QStringLiteral( "color_space" ) }, row_kind_t::ENUM };
        space.choices = { QStringLiteral( "srgb" ), QStringLiteral( "linear" ) };
        space.fallback = QStringLiteral( "srgb" );
        space.tip = QStringLiteral( "Normal and data textures are linear" );
        page.rows.push_back( space );
        if ( !bV2 ) {
            row_t mips{ settings, QStringLiteral( "Generate mips" ), { QStringLiteral( "generate_mips" ) }, row_kind_t::BOOL };
            mips.fallback = true;
            page.rows.push_back( mips );
            return;
        }
        const auto choice = [&page]( const QString &group, const char *pLabel, QStringList key, QStringList choices, const char *pFallback ) {
            row_t row{ group, QString::fromLatin1( pLabel ), std::move( key ), row_kind_t::ENUM };
            row.choices = std::move( choices );
            row.fallback = QString::fromLatin1( pFallback );
            page.rows.push_back( row );
        };
        const QString alpha = QStringLiteral( "Alpha" );
        choice( alpha, "Mode", { QStringLiteral( "alpha" ), QStringLiteral( "mode" ) },
                { QStringLiteral( "none" ), QStringLiteral( "straight" ), QStringLiteral( "premultiplied" ), QStringLiteral( "mask" ), QStringLiteral( "data" ) }, "none" );
        row_t cutoff{ alpha, QStringLiteral( "Cutoff" ), { QStringLiteral( "alpha" ), QStringLiteral( "cutoff" ) }, row_kind_t::NUMBER };
        cutoff.minimum = 0.0;
        cutoff.maximum = 1.0;
        cutoff.bBounded = true;
        cutoff.fallback = 0.5;
        cutoff.tip = QStringLiteral( "Mask mode only" );
        page.rows.push_back( cutoff );
        const QString mips = QStringLiteral( "Mips" );
        choice( mips, "Mode", { QStringLiteral( "mips" ), QStringLiteral( "mode" ) }, { QStringLiteral( "generate" ), QStringLiteral( "preserve" ), QStringLiteral( "none" ) }, "generate" );
        choice( mips, "Filter", { QStringLiteral( "mips" ), QStringLiteral( "filter" ) }, { QStringLiteral( "box" ) }, "box" );
        choice( mips, "Edge", { QStringLiteral( "mips" ), QStringLiteral( "edge" ) }, { QStringLiteral( "clamp" ), QStringLiteral( "wrap" ) }, "clamp" );
        const QString output = QStringLiteral( "Output" );
        choice( output, "Format", { QStringLiteral( "output" ), QStringLiteral( "format" ) }, { QStringLiteral( "auto" ), QStringLiteral( "uncompressed" ) }, "auto" );
        choice( output, "Quality", { QStringLiteral( "output" ), QStringLiteral( "quality" ) }, { QStringLiteral( "fast" ), QStringLiteral( "balanced" ), QStringLiteral( "production" ) }, "balanced" );
        const QString streaming = QStringLiteral( "Streaming" );
        choice( streaming, "Class", { QStringLiteral( "streaming" ), QStringLiteral( "class" ) },
                { QStringLiteral( "critical" ), QStringLiteral( "ui" ), QStringLiteral( "character" ), QStringLiteral( "world" ), QStringLiteral( "effects" ), QStringLiteral( "background" ) }, "world" );
        row_t resident{ streaming, QStringLiteral( "Resident mips" ), { QStringLiteral( "streaming" ), QStringLiteral( "resident_mips" ) }, row_kind_t::INTEGER };
        resident.minimum = 1.0;
        resident.maximum = 15.0;
        resident.bBounded = true;
        resident.fallback = 3;
        page.rows.push_back( resident );
    }

    void ShowTextureImage()
    {
        page_t &page = m_pages[DATABASE_TAB_TEXTURES];
        const QString source = ValueOf( Recipe_Find( page.recipe, { QStringLiteral( "source" ) } ) ).toString();
        QImage image;
        QString info;
        if ( !source.isEmpty() ) {
            const QString file = FileOf( source );
            QImageReader reader( file );
            image = reader.read();
            info = image.isNull() ? QStringLiteral( "%1: %2" ).arg( source, file.isEmpty() ? QStringLiteral( "not in the content folders" ) : reader.errorString() )
                                  : QStringLiteral( "%1 × %2 · %3-bit%4 · %5" ).arg( image.width() ).arg( image.height() ).arg( image.depth() )
                                        .arg( image.hasAlphaChannel() ? QStringLiteral( " with alpha" ) : QString() ).arg( QFileInfo( file ).suffix().toUpper() );
        }
        const QString channel = m_pChannel->currentData().toString();
        if ( !image.isNull() && channel != QStringLiteral( "rgb" ) ) {
            // One channel as grey, so a mask or roughness map reads plainly.
            QImage single( image.size(), QImage::Format_RGB32 );
            const QImage rgba = image.convertToFormat( QImage::Format_ARGB32 );
            for ( int y = 0; y < rgba.height(); ++y ) {
                const auto *pIn = reinterpret_cast<const QRgb *>( rgba.constScanLine( y ) );
                auto *pOut = reinterpret_cast<QRgb *>( single.scanLine( y ) );
                for ( int x = 0; x < rgba.width(); ++x ) {
                    const int v = channel == QStringLiteral( "r" ) ? qRed( pIn[x] ) : channel == QStringLiteral( "g" ) ? qGreen( pIn[x] )
                                : channel == QStringLiteral( "b" ) ? qBlue( pIn[x] ) : qAlpha( pIn[x] );
                    pOut[x] = qRgb( v, v, v );
                }
            }
            image = single;
        }
        m_previewImage = image;
        m_pImageInfo->setText( info );
        if ( image.isNull() ) {
            m_pTextureImage->setPixmap( QPixmap() );
            m_pTextureImage->setText( source.isEmpty() ? QStringLiteral( "No source image" ) : QStringLiteral( "The source image cannot be shown" ) );
            m_pTextureImage->adjustSize();
            return;
        }
        double factor = m_pZoom->currentData().toDouble();
        if ( factor <= 0.0 ) {
            const QSize area = m_pTextureScroll->viewport()->size() - QSize( 8, 8 );
            factor = std::min( static_cast<double>( area.width() ) / image.width(), static_cast<double>( area.height() ) / image.height() );
            factor = std::clamp( factor, 0.05, 8.0 );
        }
        const QSize size( std::max( 1, qRound( image.width() * factor ) ), std::max( 1, qRound( image.height() * factor ) ) );
        // Alpha over a checkerboard, as every texture tool shows it.
        QPixmap canvas( size );
        QPainter painter( &canvas );
        for ( int y = 0; y < size.height(); y += 8 ) {
            for ( int x = 0; x < size.width(); x += 8 ) { painter.fillRect( x, y, 8, 8, ( ( x + y ) / 8 ) % 2 == 0 ? QColor( 0x55, 0x55, 0x55 ) : QColor( 0x40, 0x40, 0x40 ) ); }
        }
        painter.drawImage( QRect( QPoint( 0, 0 ), size ), image );
        painter.end();
        m_pTextureImage->setText( QString() );
        m_pTextureImage->setPixmap( canvas );
        m_pTextureImage->resize( size );
    }

    // ---- Generic recipes (prefabs, particles, sounds) ----------------------

    void BuildGenericRows( page_t &page )
    {
        page.rows.clear();
        const std::function<void( const key_value_t *, QStringList, const QString & )> walk = [&]( const key_value_t *pObject, QStringList prefix, const QString &group ) {
            for ( const QString &name : ChildNames( pObject ) ) {
                const QByteArray utf8 = name.toUtf8();
                const key_value_t *pChild = KeyValue_Find( pObject, ViewOf( utf8 ) );
                QStringList key = prefix;
                key.append( name );
                if ( KeyValue_Type( pChild ) == key_value_type_t::OBJECT ) {
                    walk( pChild, key, key.join( QLatin1Char( '.' ) ) );
                    continue;
                }
                row_t row{ group, name, key };
                const QVariant value = ValueOf( pChild );
                switch ( KeyValue_Type( pChild ) ) {
                    case key_value_type_t::BOOL: row.kind = row_kind_t::BOOL; break;
                    case key_value_type_t::F64: row.kind = row_kind_t::NUMBER; break;
                    case key_value_type_t::I64:
                    case key_value_type_t::U64: row.kind = row_kind_t::INTEGER; break;
                    case key_value_type_t::STRING: row.kind = KindOfPath( value.toString() ) != editor_asset_kind_t::COUNT ? row_kind_t::PATH : row_kind_t::TEXT;
                                                   row.pathKind = KindOfPath( value.toString() ); break;
                    case key_value_type_t::ARRAY: row.kind = row_kind_t::READONLY; row.readOnlyText = TextOf( value ); break;
                    default: row.kind = row_kind_t::READONLY; row.readOnlyText = QStringLiteral( "null" ); break;
                }
                page.rows.push_back( row );
            }
        };
        walk( SettingsDocument_Root( page.recipe.pStore.get() ), {}, QStringLiteral( "Recipe" ) );
    }

    // ---- Entities ----------------------------------------------------------

    bool ShowEntity( page_t &page, u64 id )
    {
        const key_value_t *pRecord = m_pfnRecord != nullptr ? m_pfnRecord( m_pEntityContext, id ) : nullptr;
        page.current = QStringLiteral( "#%1" ).arg( id );
        page.rows.clear();
        page.pGrid->clear();
        page.editors.clear();
        QString className, name;
        bool matches = false;
        for ( const editor_database_entity_t &entity : m_pfnEntities != nullptr ? m_pfnEntities( m_pEntityContext ) : QVector<editor_database_entity_t>() ) {
            if ( entity.id == id ) { className = entity.className; name = entity.name; matches = EntityMatches( page.tab, entity, pRecord ); break; }
        }
        page.pTitle->setText( name.isEmpty() ? QStringLiteral( "Entity #%1" ).arg( id ) : name );
        if ( pRecord == nullptr || !matches ) { ClearPage( page ); return false; }
        page.pSubtitle->setText( QStringLiteral( "%1 · #%2 · Select in Map to edit its properties" ).arg( className.isEmpty() ? QStringLiteral( "no class" ) : className ).arg( id ) );
        page.pPreview->setPixmap( EditorStyle_Icon( m_pGui->style, kTabIcons[page.tab] ).pixmap( QSize( 64, 64 ), devicePixelRatioF() ) );
        // The transform first: what an inspector is opened for.
        const std::function<void( QTreeWidgetItem *, const key_value_t *, const QString & )> add = [&]( QTreeWidgetItem *pParent, const key_value_t *pValue, const QString &label ) {
            auto *pItem = pParent != nullptr ? new QTreeWidgetItem( pParent ) : new QTreeWidgetItem( page.pGrid );
            pItem->setText( 0, label );
            const key_value_type_t type = KeyValue_Type( pValue );
            pItem->setIcon( 0, EditorStyle_Icon( m_pGui->style, type == key_value_type_t::OBJECT || type == key_value_type_t::ARRAY ? "asset-folder" : "entity-point" ) );
            if ( type == key_value_type_t::OBJECT ) {
                for ( usize i = 0u; i < KeyValue_ChildCount( pValue ); ++i ) {
                    const key_value_t *pChild = KeyValue_ChildAt( pValue, i );
                    add( pItem, pChild, FromView( KeyValue_Name( pChild ) ) );
                }
                pItem->setExpanded( true );
            } else if ( type == key_value_type_t::ARRAY && KeyValue_ChildCount( pValue ) != 0u &&
                        KeyValue_Type( KeyValue_ChildAt( pValue, 0u ) ) == key_value_type_t::OBJECT ) {
                for ( usize i = 0u; i < KeyValue_ChildCount( pValue ); ++i ) { add( pItem, KeyValue_ChildAt( pValue, i ), QStringLiteral( "[%1]" ).arg( i ) ); }
                pItem->setExpanded( true );
            } else {
                pItem->setText( 1, TextOf( ValueOf( pValue ) ) );
            }
            row_t row{ pParent != nullptr ? pParent->text( 0 ) : QStringLiteral( "Entity" ), label, {}, row_kind_t::READONLY };
            row.readOnlyText = pItem->text( 1 );
            page.rows.push_back( row );
        };
        for ( const char *pKey : { "class", "name", "origin", "angles", "rotation", "scale" } ) {
            if ( const key_value_t *pValue = KeyValue_Find( pRecord, StringView_FromCString( pKey ) ) ) { add( nullptr, pValue, QString::fromLatin1( pKey ) ); }
        }
        const QSet<QString> shown{ QStringLiteral( "class" ), QStringLiteral( "name" ), QStringLiteral( "origin" ), QStringLiteral( "angles" ), QStringLiteral( "rotation" ), QStringLiteral( "scale" ) };
        for ( usize i = 0u; i < KeyValue_ChildCount( pRecord ); ++i ) {
            const key_value_t *pChild = KeyValue_ChildAt( pRecord, i );
            const QString label = FromView( KeyValue_Name( pChild ) );
            if ( !shown.contains( label ) ) { add( nullptr, pChild, label ); }
        }
        for ( QTreeWidgetItemIterator it( page.pLibrary ); *it != nullptr; ++it ) {
            if ( ( *it )->data( 0, Qt::UserRole ).toString() == page.current ) {
                const QSignalBlocker blocker( page.pLibrary );
                page.pLibrary->setCurrentItem( *it );
                page.pLibrary->scrollToItem( *it );
                break;
            }
        }
        UpdateActions();
        return true;
    }

    // ---- The grid ----------------------------------------------------------

    void RebuildGrid( page_t &page )
    {
        page.pGrid->clear();
        page.editors.clear();
        QHash<QString, QTreeWidgetItem *> groups;
        const QColor headerColor = EditorStyle_TokenColor( m_pGui->style, "ui.header" );
        for ( row_t &row : page.rows ) {
            QTreeWidgetItem *&pGroup = groups[row.group];
            if ( pGroup == nullptr ) {
                // Sandbox's recessed group bars.
                pGroup = new QTreeWidgetItem( page.pGrid );
                pGroup->setText( 0, row.group );
                pGroup->setFirstColumnSpanned( true );
                QFont font = pGroup->font( 0 );
                font.setBold( true );
                pGroup->setFont( 0, font );
                pGroup->setBackground( 0, headerColor );
                pGroup->setExpanded( true );
            }
            auto *pItem = new QTreeWidgetItem( pGroup );
            pItem->setText( 0, row.name );
            pItem->setToolTip( 0, row.tip );
            const QVariant value = ValueOf( Recipe_Find( page.recipe, row.key ) );
            if ( row.bRequired && !value.isValid() && row.kind != row_kind_t::READONLY ) {
                pItem->setForeground( 0, EditorStyle_TokenColor( m_pGui->style, "ui.status.error.text" ) );
                pItem->setToolTip( 0, row.tip + QStringLiteral( "\nRequired and not set" ) );
            }
            QWidget *pEditor = MakeEditor( page, row, value );
            page.pGrid->setItemWidget( pItem, 1, pEditor );
            page.editors.insert( RowId( row ), pEditor );
        }
    }

    QWidget *MakeEditor( page_t &page, const row_t &row, const QVariant &value )
    {
        auto *pBox = new QWidget();
        auto *pLayout = new QHBoxLayout( pBox );
        pLayout->setContentsMargins( 2, 1, 2, 1 );
        pLayout->setSpacing( 3 );
        const bool bSet = value.isValid();
        const QVariant shown = bSet ? value : row.fallback;
        const QString id = RowId( row );
        page_t *pPage = &page;
        // Unset rows show the default in muted text: editing makes it an override.
        const auto muted = [this, bSet]( QWidget *pWidget ) {
            if ( !bSet ) { pWidget->setStyleSheet( QStringLiteral( "color: %1;" ).arg( EditorStyle_TokenColor( m_pGui->style, "ui.text.muted" ).name() ) ); }
        };
        switch ( row.kind ) {
            case row_kind_t::READONLY: {
                auto *pLabel = new QLabel( row.readOnlyText, pBox );
                pLabel->setTextInteractionFlags( Qt::TextSelectableByMouse );
                pLayout->addWidget( pLabel, 1 );
                break;
            }
            case row_kind_t::PATH:
            case row_kind_t::TEXT:
            case row_kind_t::LIST: {
                auto *pEdit = new QLineEdit( TextOf( shown ), pBox );
                pEdit->setPlaceholderText( row.kind == row_kind_t::PATH ? QStringLiteral( "none" ) : QString() );
                muted( pEdit );
                QObject::connect( pEdit, &QLineEdit::editingFinished, this, [this, pPage, id, pEdit]() { ( void )SetFromText( *pPage, id, pEdit->text() ); } );
                pLayout->addWidget( pEdit, 1 );
                if ( row.kind == row_kind_t::PATH ) {
                    // Sandbox's small folder: open what this row names.
                    auto *pOpen = Button( pBox, QString(), "asset-folder", QStringLiteral( "Open in its tab" ), QStringLiteral( "DatabaseOpen_%1" ).arg( row.name ) );
                    pOpen->setAutoRaise( true );
                    pOpen->setEnabled( !TextOf( shown ).isEmpty() );
                    QObject::connect( pOpen, &QToolButton::clicked, this, [this, pPage, id]() { ( void )FollowRowId( *pPage, id ); } );
                    pLayout->addWidget( pOpen );
                    if ( row.pathKind != editor_asset_kind_t::COUNT && page.tab != DATABASE_TAB_SHADERS && !( page.tab == DATABASE_TAB_TEXTURES && row.name == QStringLiteral( "Source" ) ) ) {
                        auto *pChoose = Button( pBox, QStringLiteral( "..." ), nullptr, QStringLiteral( "Choose in the Asset Browser" ), QStringLiteral( "DatabaseChoose_%1" ).arg( row.name ) );
                        pChoose->setAutoRaise( true );
                        QObject::connect( pChoose, &QToolButton::clicked, this, [this, pPage, id, kind = row.pathKind, pEdit]() { Choose( *pPage, id, kind, pEdit->text() ); } );
                        pLayout->addWidget( pChoose );
                    }
                }
                break;
            }
            case row_kind_t::ENUM: {
                auto *pCombo = new QComboBox( pBox );
                pCombo->addItems( row.choices );
                pCombo->setCurrentText( TextOf( shown ) );
                muted( pCombo );
                QObject::connect( pCombo, &QComboBox::activated, this, [this, pPage, id, pCombo]( int ) { ( void )SetFromText( *pPage, id, pCombo->currentText() ); } );
                pLayout->addWidget( pCombo, 1 );
                break;
            }
            case row_kind_t::BOOL: {
                auto *pCheck = new QCheckBox( pBox );
                pCheck->setChecked( shown.toBool() );
                muted( pCheck );
                QObject::connect( pCheck, &QCheckBox::toggled, this, [this, pPage, id]( bool bOn ) { ( void )SetFromText( *pPage, id, bOn ? QStringLiteral( "true" ) : QStringLiteral( "false" ) ); } );
                pLayout->addWidget( pCheck );
                pLayout->addStretch( 1 );
                break;
            }
            case row_kind_t::INTEGER: {
                auto *pSpin = new QSpinBox( pBox );
                pSpin->setLocale( QLocale::c() );
                pSpin->setRange( static_cast<int>( std::max( row.minimum, -2.0e9 ) ), static_cast<int>( std::min( row.maximum, 2.0e9 ) ) );
                pSpin->setValue( shown.toInt() );
                pSpin->setKeyboardTracking( false );
                muted( pSpin );
                QObject::connect( pSpin, &QSpinBox::valueChanged, this, [this, pPage, id]( int n ) { ( void )SetFromText( *pPage, id, QString::number( n ) ); } );
                pLayout->addWidget( pSpin, 1 );
                break;
            }
            case row_kind_t::NUMBER: {
                auto *pSpin = new QDoubleSpinBox( pBox );
                pSpin->setLocale( QLocale::c() ); // Numbers read as the recipe writes them: 0.5, not 0,5.
                pSpin->setDecimals( 3 );
                pSpin->setRange( row.minimum, row.maximum );
                pSpin->setSingleStep( row.bBounded ? ( row.maximum - row.minimum ) / 100.0 : 0.1 );
                pSpin->setValue( shown.toDouble() );
                pSpin->setKeyboardTracking( false );
                muted( pSpin );
                pLayout->addWidget( pSpin, row.bBounded ? 0 : 1 );
                if ( row.bBounded ) {
                    // A slider across the declared range, Sandbox's way.
                    auto *pSlider = new QSlider( Qt::Horizontal, pBox );
                    pSlider->setRange( 0, 1000 );
                    const auto toSlider = [row]( double v ) { return qRound( ( v - row.minimum ) / std::max( 1e-9, row.maximum - row.minimum ) * 1000.0 ); };
                    pSlider->setValue( toSlider( shown.toDouble() ) );
                    QObject::connect( pSlider, &QSlider::sliderReleased, this, [this, pPage, id, pSlider, row]() {
                        ( void )SetFromText( *pPage, id, QString::number( row.minimum + ( row.maximum - row.minimum ) * pSlider->value() / 1000.0 ) );
                    } );
                    QObject::connect( pSlider, &QSlider::valueChanged, pSpin, [pSpin, pSlider, row]( int v ) {
                        if ( pSlider->isSliderDown() ) {
                            const QSignalBlocker blocker( pSpin );
                            pSpin->setValue( row.minimum + ( row.maximum - row.minimum ) * v / 1000.0 );
                        }
                    } );
                    QObject::connect( pSpin, &QDoubleSpinBox::valueChanged, pSlider, [pSlider, toSlider]( double v ) {
                        const QSignalBlocker blocker( pSlider );
                        pSlider->setValue( toSlider( v ) );
                    } );
                    pLayout->addWidget( pSlider, 1 );
                }
                QObject::connect( pSpin, &QDoubleSpinBox::valueChanged, this, [this, pPage, id]( double v ) { ( void )SetFromText( *pPage, id, QString::number( v, 'g', 9 ) ); } );
                break;
            }
            case row_kind_t::VECTOR:
            case row_kind_t::COLOR: {
                QVariantList components = shown.toList();
                while ( components.size() < row.components ) { components.append( components.size() == 3 ? 1.0 : 0.0 ); }
                std::vector<QDoubleSpinBox *> spins;
                if ( row.kind == row_kind_t::COLOR ) {
                    auto *pSwatch = new QToolButton( pBox );
                    pSwatch->setObjectName( QStringLiteral( "DatabaseSwatch_%1" ).arg( row.name ) );
                    pSwatch->setFixedSize( 34, 18 );
                    const QColor colour = QColor::fromRgbF( static_cast<float>( std::clamp( components.value( 0 ).toDouble(), 0.0, 1.0 ) ),
                                                            static_cast<float>( std::clamp( components.value( 1 ).toDouble(), 0.0, 1.0 ) ),
                                                            static_cast<float>( std::clamp( components.value( 2 ).toDouble(), 0.0, 1.0 ) ) );
                    pSwatch->setStyleSheet( QStringLiteral( "QToolButton { background: %1; border: 1px solid %2; }" ).arg( colour.name(), EditorStyle_TokenColor( m_pGui->style, "ui.edge" ).name() ) );
                    pSwatch->setToolTip( QStringLiteral( "Choose a colour" ) );
                    QObject::connect( pSwatch, &QToolButton::clicked, this, [this, pPage, id, components, n = row.components]() {
                        const QColor initial = QColor::fromRgbF( static_cast<float>( components.value( 0 ).toDouble() ), static_cast<float>( components.value( 1 ).toDouble() ),
                                                                 static_cast<float>( components.value( 2 ).toDouble() ), static_cast<float>( n == 4 ? components.value( 3 ).toDouble() : 1.0 ) );
                        const QColor chosen = QColorDialog::getColor( initial, this, QStringLiteral( "Colour" ), n == 4 ? QColorDialog::ShowAlphaChannel : QColorDialog::ColorDialogOptions() );
                        if ( !chosen.isValid() ) { return; }
                        QString text = QStringLiteral( "%1 %2 %3" ).arg( chosen.redF(), 0, 'g', 4 ).arg( chosen.greenF(), 0, 'g', 4 ).arg( chosen.blueF(), 0, 'g', 4 );
                        if ( n == 4 ) { text += QStringLiteral( " %1" ).arg( chosen.alphaF(), 0, 'g', 4 ); }
                        ( void )SetFromText( *pPage, id, text );
                        RebuildGrid( *pPage );
                    } );
                    pLayout->addWidget( pSwatch );
                }
                for ( int c = 0; c < row.components; ++c ) {
                    auto *pSpin = new QDoubleSpinBox( pBox );
                    pSpin->setLocale( QLocale::c() );
                    pSpin->setDecimals( 3 );
                    pSpin->setRange( row.kind == row_kind_t::COLOR ? 0.0 : -1.0e6, row.kind == row_kind_t::COLOR ? 16.0 : 1.0e6 );
                    pSpin->setSingleStep( 0.05 );
                    pSpin->setValue( components.value( c ).toDouble() );
                    pSpin->setKeyboardTracking( false );
                    pSpin->setFixedWidth( 64 );
                    muted( pSpin );
                    spins.push_back( pSpin );
                    pLayout->addWidget( pSpin );
                }
                for ( QDoubleSpinBox *pSpin : spins ) {
                    QObject::connect( pSpin, &QDoubleSpinBox::valueChanged, this, [this, pPage, id, spins]( double ) {
                        QStringList parts;
                        for ( QDoubleSpinBox *pPart : spins ) { parts.append( QString::number( pPart->value(), 'g', 9 ) ); }
                        ( void )SetFromText( *pPage, id, parts.join( QLatin1Char( ' ' ) ) );
                    } );
                }
                pLayout->addStretch( 1 );
                break;
            }
        }
        if ( row.bResettable && bSet ) {
            auto *pReset = Button( pBox, QStringLiteral( "×" ), nullptr, page.tab == DATABASE_TAB_MATERIALS ? QStringLiteral( "Return to the shader's default (remove from the material)" )
                                                                                                            : QStringLiteral( "Remove" ),
                                   QStringLiteral( "DatabaseReset_%1" ).arg( row.name ) );
            pReset->setAutoRaise( true );
            QObject::connect( pReset, &QToolButton::clicked, this, [this, pPage, id]() {
                for ( const row_t &candidate : pPage->rows ) {
                    if ( RowId( candidate ) == id ) { ( void )Reset( *pPage, candidate ); break; }
                }
            } );
            pLayout->addWidget( pReset );
        }
        return pBox;
    }

    bool SetFromText( page_t &page, const QString &id, const QString &text )
    {
        for ( const row_t &row : page.rows ) {
            if ( RowId( row ) != id ) { continue; }
            bool bOk = false;
            const QVariant value = ParseValue( row, text, &bOk );
            return bOk && Apply( page, row, value, false );
        }
        return false;
    }

    // Writes one value into the open recipe. Clearing a path removes it.
    bool Apply( page_t &page, const row_t &row, const QVariant &value, bool bRebuild )
    {
        if ( row.key.isEmpty() || !page.recipe.IsOpen() ) { return false; }
        if ( ShaderRecipeDiverged( page ) ) { return false; }
        const QVariant current = ValueOf( Recipe_Find( page.recipe, row.key ) );
        if ( current.isValid() && TextOf( current ) == TextOf( value ) ) { return true; } // Nothing changed.
        bool bOk = false;
        if ( ( row.kind == row_kind_t::PATH || row.kind == row_kind_t::TEXT ) && value.toString().isEmpty() ) { bOk = !current.isValid() || Recipe_Remove( page.recipe, row.key ); }
        else { bOk = Recipe_Set( page.recipe, row.key, value ); }
        if ( !bOk ) { return false; }
        const bool bStructural = page.tab == DATABASE_TAB_MATERIALS && row.key.value( 0 ) == QStringLiteral( "shader" ); // New shader, new slots.
        if ( bStructural ) { BuildMaterialRows( page ); }
        if ( bStructural || bRebuild || !current.isValid() ) { RebuildGrid( page ); } // Rows that were defaults gain a reset button.
        SyncRecipeTab( page );
        UpdateHeader( page );
        UpdateActions();
        return true;
    }

    bool Reset( page_t &page, const row_t &row )
    {
        if ( row.key.isEmpty() || !Recipe_Find( page.recipe, row.key ) ) { return false; }
        if ( ShaderRecipeDiverged( page ) ) { return false; }
        // A schema 2 texture override is the whole slot object.
        const QStringList key = page.tab == DATABASE_TAB_MATERIALS && row.group == QStringLiteral( "Texture Maps" ) && page.recipe.version >= 2u
                                    ? row.key.mid( 0, 2 ) : row.key;
        if ( !Recipe_Remove( page.recipe, key ) ) { return false; }
        if ( page.tab == DATABASE_TAB_MATERIALS ) { BuildMaterialRows( page ); }
        RebuildGrid( page );
        SyncRecipeTab( page );
        UpdateHeader( page );
        UpdateActions();
        return true;
    }

    bool FollowRowId( page_t &page, const QString &id )
    {
        for ( const row_t &row : page.rows ) {
            if ( RowId( row ) == id ) { return FollowPath( page, row ); }
        }
        return false;
    }

    // The folder button: shader stages open in the code editor, a texture's
    // source shows in its folder, everything else in its own tab.
    bool FollowPath( page_t &page, const row_t &row )
    {
        const QVariant set = ValueOf( Recipe_Find( page.recipe, row.key ) );
        const QString path = ( set.isValid() ? set : row.fallback ).toString();
        if ( path.isEmpty() ) { return false; }
        if ( page.tab == DATABASE_TAB_SHADERS ) {
            for ( usize i = 0u; i < m_codeTabs.size(); ++i ) {
                if ( m_codeTabs[i].path == path ) {
                    m_pCode->setCurrentIndex( static_cast<int>( i ) );
                    m_codeTabs[i].pEditor->setFocus();
                    return true;
                }
            }
            return false;
        }
        if ( page.tab == DATABASE_TAB_TEXTURES && row.name == QStringLiteral( "Source" ) ) {
            const QString file = FileOf( path );
            if ( file.isEmpty() ) { return false; }
            QDesktopServices::openUrl( QUrl::fromLocalFile( QFileInfo( file ).absolutePath() ) );
            return true;
        }
        if ( IsModified() && !ConfirmLeave( page ) ) { return false; }
        return Open( path );
    }

    void Choose( page_t &page, const QString &id, editor_asset_kind_t kind, const QString &current )
    {
        if ( m_pBrowser == nullptr ) { return; }
        if ( m_pPicker == nullptr ) { m_pPicker = EditorAssetWindow_Create( this, m_pGui, m_pBrowser ); }
        m_pendingPage = page.tab;
        m_pendingRow = id;
        EditorAssetWindow_Pick( m_pPicker, kind, current, []( void *pContext, const QString &path, editor_asset_kind_t ) {
            auto *pView = static_cast<database_view_t *>( pContext );
            page_t &target = pView->m_pages[pView->m_pendingPage];
            if ( pView->SetFromText( target, pView->m_pendingRow, path ) ) { pView->RebuildGrid( target ); }
        }, this );
    }

    // ---- Header, preview, used by -----------------------------------------

    void UpdateHeader( page_t &page )
    {
        if ( !page.recipe.IsOpen() ) { return; }
        QString subtitle = QStringLiteral( "%1 · schema %2" ).arg( page.current ).arg( page.recipe.version );
        if ( page.recipe.bDirty || IsModified() ) { subtitle += QStringLiteral( " · modified" ); }
        if ( page.tab == DATABASE_TAB_MATERIALS ) {
            subtitle += m_interface.bKnown ? QStringLiteral( "\nSlots and parameters follow the shader's declared interface" )
                                           : QStringLiteral( "\nThe shader declares no interface (schema 1): values are shown as written" );
            page.pPreview->setPixmap( MaterialPreview( page ) );
        } else if ( page.tab == DATABASE_TAB_TEXTURES ) {
            ShowTextureImage();
        } else if ( page.tab == DATABASE_TAB_SHADERS ) {
            page.pPreview->setPixmap( EditorStyle_Icon( m_pGui->style, "asset-shader" ).pixmap( QSize( 64, 64 ), devicePixelRatioF() ) );
        } else {
            page.pPreview->setPixmap( EditorStyle_Icon( m_pGui->style, kTabIcons[page.tab] ).pixmap( QSize( 64, 64 ), devicePixelRatioF() ) );
        }
        page.pSubtitle->setText( subtitle );
    }

    // The material's base colour as it is now (unsaved edits included).
    QPixmap MaterialPreview( const page_t &page )
    {
        const bool bV2 = page.recipe.version >= 2u;
        QString texture = ValueOf( Recipe_Find( page.recipe, bV2 ? QStringList{ QStringLiteral( "textures" ), QStringLiteral( "base_color" ), QStringLiteral( "resource" ) }
                                                                 : QStringList{ QStringLiteral( "textures" ), QStringLiteral( "base_color" ) } ) ).toString();
        if ( texture.isEmpty() ) {
            const QStringList named = ChildNames( Recipe_Find( page.recipe, { QStringLiteral( "textures" ) } ) );
            if ( !named.isEmpty() ) {
                texture = ValueOf( Recipe_Find( page.recipe, bV2 ? QStringList{ QStringLiteral( "textures" ), named.front(), QStringLiteral( "resource" ) }
                                                                 : QStringList{ QStringLiteral( "textures" ), named.front() } ) ).toString();
            }
        }
        QImage image;
        recipe_t recipe;
        QString ignored;
        if ( !texture.isEmpty() && !FileOf( texture ).isEmpty() && Recipe_Load( recipe, m_pGui->pAllocator, FileOf( texture ), texture, "cypher.texture", &ignored ) ) {
            const QString source = ValueOf( Recipe_Find( recipe, { QStringLiteral( "source" ) } ) ).toString();
            if ( !source.isEmpty() ) { image = QImageReader( FileOf( source ) ).read(); }
        }
        if ( image.isNull() ) { return EditorStyle_Icon( m_pGui->style, "asset-material" ).pixmap( QSize( 64, 64 ), devicePixelRatioF() ); }
        const qreal dpr = devicePixelRatioF();
        QPixmap pixmap = QPixmap::fromImage( image.scaled( QSize( 128, 128 ) * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation ) );
        pixmap.setDevicePixelRatio( dpr );
        return pixmap;
    }

    // Materials whose recipe names the open shader or texture.
    void UpdateUsedBy( page_t &page )
    {
        if ( page.pUsedBy == nullptr ) { return; }
        page.pUsedBy->clear();
        const editor_asset_catalog_t *pCatalog = Catalog();
        if ( pCatalog == nullptr || page.current.isEmpty() ) { return; }
        const QByteArray target = page.current.toUtf8();
        for ( usize i = 0u; i < EditorAssets_Count( pCatalog ); ++i ) {
            const editor_asset_t *pAsset = EditorAssets_At( pCatalog, i );
            if ( pAsset == nullptr || pAsset->kind != editor_asset_kind_t::MATERIAL || pAsset->bSource ) { continue; }
            const QString path = FromView( EditorAssets_Path( pCatalog, *pAsset ) );
            QFile file( FileOf( path ) );
            if ( !file.open( QIODevice::ReadOnly ) ) { continue; }
            const QByteArray text = file.readAll();
            if ( !text.contains( target ) ) { continue; } // Cheap reject before parsing.
            struct match_t { QByteArray target; bool bFound{ false }; } match{ target };
            ( void )EditorAssets_References( ViewOf( text ), m_pGui->pAllocator, []( void *pContext, string_view_t reference, editor_asset_kind_t ) {
                auto *pMatch = static_cast<match_t *>( pContext );
                pMatch->bFound = pMatch->bFound || QByteArray( reference.pData, static_cast<qsizetype>( reference.cchLength ) ) == pMatch->target;
            }, &match );
            if ( !match.bFound ) { continue; }
            auto *pItem = new QListWidgetItem( EditorStyle_Icon( m_pGui->style, "asset-material" ), path, page.pUsedBy );
            pItem->setData( Qt::UserRole, path );
        }
        if ( page.pUsedBy->count() == 0 ) { page.pUsedBy->addItem( QStringLiteral( "No material uses it" ) ); }
    }

    bool ShaderRecipeDiverged( const page_t &page ) const
    {
        if ( page.tab != DATABASE_TAB_SHADERS || m_codeTabs.empty() || !EditorCodeEditor_IsModified( m_codeTabs.front().pEditor ) ) { return false; }
        // Property edits and the source editor are two views of one recipe.
        // SyncRecipeTab may replace its text only while it still represents
        // the parsed store; raw edits need Save/Revert before another grid edit.
        return EditorCodeEditor_TextOf( m_codeTabs.front().pEditor ) != QString::fromUtf8( Recipe_Text( page.recipe, m_pGui->pAllocator ) );
    }

    void UpdateActions()
    {
        const page_t &page = m_pages[Tab()];
        for ( const page_t &entityPage : m_pages ) {
            if ( entityPage.pSelect != nullptr ) { entityPage.pSelect->setEnabled( !entityPage.current.isEmpty() && m_pfnGoTo != nullptr ); }
        }
        const bool bAsset = page.recipe.IsOpen();
        const bool bModified = bAsset && IsModified();
        m_pSave->setEnabled( bModified );
        m_pRevert->setEnabled( bModified );
        m_pExternal->setEnabled( bAsset || ( IsFileTab( page.tab ) && !page.current.isEmpty() ) );
        m_pReveal->setEnabled( bAsset || ( IsFileTab( page.tab ) && !page.current.isEmpty() ) );
        m_pNewShader->setVisible( page.tab == DATABASE_TAB_SHADERS );
        setWindowTitle( bModified ? QStringLiteral( "Content Library - %1*" ).arg( page.current ) : QStringLiteral( "Content Library" ) );
        const QString sourceNotice = QStringLiteral( "Save or Revert the shader recipe text before editing its properties." );
        const bool bSourceDiverged = ShaderRecipeDiverged( page );
        page.pGrid->setEnabled( !bSourceDiverged );
        page.pGrid->setToolTip( bSourceDiverged ? sourceNotice : QString() );
        if ( bSourceDiverged ) { m_pStatus->setText( sourceNotice ); }
        else if ( m_pStatus->text() == sourceNotice ) { m_pStatus->clear(); }
    }

    void Report( const QString &text )
    {
        m_pStatus->setText( text );
        if ( !text.isEmpty() ) {
            const QByteArray utf8 = text.toUtf8();
            Cy_LogWriteAt( log_level_t::Warning, log_channel_t::Gui, utf8.constData(), CY_SOURCE_LOCATION );
        }
    }

    // ---- Files -------------------------------------------------------------

    const editor_asset_catalog_t *Catalog() const { return m_pBrowser != nullptr ? EditorAssetBrowser_Catalog( m_pBrowser ) : nullptr; }

    // The file a virtual path resolves to: the catalogue's root for it, or
    // the first root that has it (a source image the catalogue lists too).
    QString FileOf( const QString &path ) const
    {
        if ( path.isEmpty() || m_pBrowser == nullptr ) { return {}; }
        const QStringList roots = EditorAssetBrowser_Roots( m_pBrowser );
        if ( const editor_asset_catalog_t *pCatalog = Catalog() ) {
            const QByteArray utf8 = path.toUtf8();
            if ( const editor_asset_t *pAsset = EditorAssets_Find( pCatalog, ViewOf( utf8 ) ) ) {
                if ( pAsset->iRoot < roots.size() ) { return QDir( roots[pAsset->iRoot] ).filePath( path ); }
            }
        }
        for ( const QString &root : roots ) {
            const QString file = QDir( root ).filePath( path );
            if ( QFileInfo::exists( file ) ) { return file; }
        }
        return {};
    }

    QString CurrentFile()
    {
        page_t &page = Current();
        if ( page.tab == DATABASE_TAB_SHADERS && m_pCode->currentIndex() >= 0 && m_pCode->currentIndex() < static_cast<int>( m_codeTabs.size() ) ) {
            return m_codeTabs[static_cast<usize>( m_pCode->currentIndex() )].file;
        }
        return IsFileTab( page.tab ) ? FileOf( page.current ) : page.recipe.file;
    }

    editor_gui_t *m_pGui;
    QPointer<QWidget> m_pBrowser;
    const bool m_bEmbedded;
    const bool m_bShaderOnly;
    page_t m_pages[DATABASE_TAB_COUNT]{};
    std::vector<code_tab_t> m_codeTabs{};
    shader_interface_t m_interface{};
    QImage m_previewImage{};
    QDialog *m_pPicker{ nullptr };
    int m_pendingPage{ 0 };
    QString m_pendingRow{};
    editor_database_entities_fn m_pfnEntities{ nullptr };
    editor_database_record_fn m_pfnRecord{ nullptr };
    editor_database_go_to_fn m_pfnGoTo{ nullptr };
    void *m_pEntityContext{ nullptr };

    QTabWidget *m_pTabs{ nullptr };
    QTabWidget *m_pCode{ nullptr };
    QToolButton *m_pSave{ nullptr };
    QToolButton *m_pRevert{ nullptr };
    QToolButton *m_pExternal{ nullptr };
    QToolButton *m_pReveal{ nullptr };
    QToolButton *m_pNewShader{ nullptr };
    QLabel *m_pStatus{ nullptr };
    QComboBox *m_pZoom{ nullptr };
    QComboBox *m_pChannel{ nullptr };
    QLabel *m_pImageInfo{ nullptr };
    QScrollArea *m_pTextureScroll{ nullptr };
    QLabel *m_pTextureImage{ nullptr };
};

database_view_t *AsView( QWidget *pView )
{
    return dynamic_cast<database_view_t *>( pView );
}

} // namespace

QDialog *EditorDatabaseView_Create( QWidget *pParent, editor_gui_t *pGui, QWidget *pBrowser )
{
    if ( pGui == nullptr ) { return nullptr; }
    return new database_view_t( pParent, pGui, pBrowser );
}

QWidget *EditorDatabaseView_CreateEmbedded( QWidget *pParent, editor_gui_t *pGui, QWidget *pBrowser, int initialTab, bool shaderOnly )
{
    if ( pParent == nullptr || pGui == nullptr || initialTab < 0 || initialTab >= DATABASE_TAB_COUNT ) { return nullptr; }
    auto *pView = new database_view_t( pParent, pGui, pBrowser, true, shaderOnly );
    pView->SetTab( shaderOnly ? DATABASE_TAB_SHADERS : initialTab );
    return pView;
}

void EditorDatabaseView_SetEntitySource( QWidget *pView, editor_database_entities_fn pfnEntities, editor_database_record_fn pfnRecord,
                                         editor_database_go_to_fn pfnGoTo, void *pContext )
{
    if ( auto *p = AsView( pView ) ) { p->SetEntitySource( pfnEntities, pfnRecord, pfnGoTo, pContext ); }
}

bool EditorDatabaseView_Open( QWidget *pView, const QString &path )
{
    auto *p = AsView( pView );
    return p != nullptr && p->Open( path );
}

bool EditorDatabaseView_OpenEntity( QWidget *pView, u64 id )
{
    auto *p = AsView( pView );
    return p != nullptr && p->OpenEntity( id );
}

void EditorDatabaseView_Refresh( QWidget *pView )
{
    if ( auto *p = AsView( pView ) ) { p->Refresh(); }
}

void EditorDatabaseView_SetTab( QWidget *pView, int tab )
{
    if ( auto *p = AsView( pView ) ) { p->SetTab( tab ); }
}

int EditorDatabaseView_Tab( QWidget *pView )
{
    auto *p = AsView( pView );
    return p != nullptr ? p->Tab() : -1;
}

QString EditorDatabaseView_Current( QWidget *pView )
{
    auto *p = AsView( pView );
    return p != nullptr ? p->CurrentPath() : QString();
}

QStringList EditorDatabaseView_Library( QWidget *pView )
{
    auto *p = AsView( pView );
    return p != nullptr ? p->Library() : QStringList();
}

QStringList EditorDatabaseView_Properties( QWidget *pView )
{
    auto *p = AsView( pView );
    return p != nullptr ? p->Properties() : QStringList();
}

bool EditorDatabaseView_SetProperty( QWidget *pView, const QString &groupAndName, const QString &value )
{
    auto *p = AsView( pView );
    return p != nullptr && p->SetProperty( groupAndName, value );
}

bool EditorDatabaseView_ResetProperty( QWidget *pView, const QString &groupAndName )
{
    auto *p = AsView( pView );
    return p != nullptr && p->ResetProperty( groupAndName );
}

bool EditorDatabaseView_OpenRow( QWidget *pView, const QString &groupAndName )
{
    auto *p = AsView( pView );
    return p != nullptr && p->OpenRow( groupAndName );
}

bool EditorDatabaseView_IsModified( QWidget *pView )
{
    auto *p = AsView( pView );
    return p != nullptr && p->IsModified();
}

bool EditorDatabaseView_Save( QWidget *pView )
{
    auto *p = AsView( pView );
    return p != nullptr && p->Save();
}

bool EditorDatabaseView_Revert( QWidget *pView )
{
    auto *p = AsView( pView );
    return p != nullptr && p->Revert();
}

QWidget *EditorDatabaseView_CodeEditor( QWidget *pView, int index )
{
    auto *p = AsView( pView );
    return p != nullptr ? p->CodeEditor( index ) : nullptr;
}

int EditorDatabaseView_CodeEditorCount( QWidget *pView )
{
    auto *p = AsView( pView );
    return p != nullptr ? p->CodeEditorCount() : 0;
}

bool EditorDatabaseView_NewShader( QWidget *pView, const QString &name, QString *pPathOut )
{
    auto *p = AsView( pView );
    return p != nullptr && p->NewShader( name, pPathOut );
}

QStringList EditorDatabaseView_UsedBy( QWidget *pView )
{
    auto *p = AsView( pView );
    return p != nullptr ? p->UsedBy() : QStringList();
}

void EditorDatabaseView_SetChannel( QWidget *pView, const QString &channel )
{
    if ( auto *p = AsView( pView ) ) { p->SetChannel( channel ); }
}

QImage EditorDatabaseView_PreviewImage( QWidget *pView )
{
    auto *p = AsView( pView );
    return p != nullptr ? p->PreviewImage() : QImage();
}

} // namespace cypher::editor::gui
