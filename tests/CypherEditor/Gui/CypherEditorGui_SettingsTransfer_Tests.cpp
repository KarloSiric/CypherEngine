//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_SettingsTransfer_Tests.cpp
//  Purpose: Contract tests for portable exports and atomic reviewed imports.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_SettingsTransfer.h"
#include "CypherEditorGui_SettingsDialog.h"
#include "CypherEditorGui_Application.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <QDialog>
#include <QDir>
#include <QCoreApplication>
#include <QApplication>
#include <QFile>
#include <QPushButton>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTreeWidget>

#include <memory>

using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::gui;

namespace
{

QByteArray Serialized( const settings_document_t &document )
{
    text_buffer_t buffer{};
    REQUIRE( TextBuffer_Init( &buffer, document.pAllocator ) );
    REQUIRE( SettingsDocument_Write( &document, &buffer ) == settings_document_status_t::OK );
    return { TextBuffer_CStr( &buffer ), static_cast<qsizetype>( TextBuffer_Length( &buffer ) ) };
}

QByteArray ReadFile( const QString &path )
{
    QFile file( path ); REQUIRE( file.open( QIODevice::ReadOnly ) ); return file.readAll();
}

void WriteFile( const QString &path, const QByteArray &bytes )
{
    QFile file( path ); REQUIRE( file.open( QIODevice::WriteOnly ) ); REQUIRE( file.write( bytes ) == bytes.size() );
}

struct audit_t {
    usize calls{}, bytes{}, failAt{ CY_USIZE_MAX };
    allocator_t allocator{};
    audit_t()
    {
        allocator.pUserData = this;
        allocator.pfnAllocate = []( void *context, usize size, usize alignment ) noexcept -> void * {
            auto &audit = *static_cast<audit_t *>( context );
            if ( audit.calls++ == audit.failAt ) { return nullptr; }
            void *pMemory = Allocator_Allocate( Allocator_GetSystem(), size, alignment ); if ( pMemory != nullptr ) { audit.bytes += size; } return pMemory;
        };
        allocator.pfnFree = []( void *context, void *pMemory, usize size, usize alignment ) noexcept {
            if ( pMemory != nullptr ) { static_cast<audit_t *>( context )->bytes -= size; }
            Allocator_Free( Allocator_GetSystem(), pMemory, size, alignment );
        };
    }
};

struct fixture_t {
    settings_registry_t registry{};
    settings_document_t user{}, project{};
    explicit fixture_t( const allocator_t *pAllocator = Allocator_GetSystem() )
    {
        REQUIRE( EditorSettings_Init( &registry, pAllocator ) == settings_registry_status_t::OK );
        usize count{}; const auto *pCatalogue = EditorSettings_FrameworkCatalogue( &count );
        REQUIRE( EditorSettings_Register( &registry, pCatalogue, count ) == settings_registry_status_t::OK );
        REQUIRE( SettingsDocument_Init( &user, pAllocator, EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
        REQUIRE( SettingsDocument_Init( &project, pAllocator, EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
        REQUIRE( SettingsDocument_Load( &user, StringView_FromCString( "@cykv 1\n@schema \"cypher.settings\" 2\n"
            "{ editor = { grid = { size = 32 } camera = { invert_y = true } } future = { user = \"retained\" } }" ) ).status == settings_document_status_t::OK );
        REQUIRE( SettingsDocument_Load( &project, StringView_FromCString( "@cykv 1\n@schema \"cypher.settings\" 2\n"
            "{ editor = { grid = { size = 64 } } future = { project = [ 1, \"two\" ] } }" ) ).status == settings_document_status_t::OK );
        EditorSettings_SetScope( &registry, settings_scope_t::USER, &user ); EditorSettings_SetScope( &registry, settings_scope_t::PROJECT, &project );
    }
};

constexpr const char *kImport = "@cykv 1\n@schema \"cypher.settings\" 2\n"
    "{ editor = { grid = { size = 16 snap = false } camera = { move_speed = 1400.0 } ui = { theme = \"my_theme\" } } "
    "future = { imported = { values = [ true, \"kept\" ] } } }";

struct persistence_t {
    settings_registry_t *pRegistry{};
    QString path;
    bool allowed{ true }, fail{ false }, unchangedBeforeSave{ false }, fileReadyOnNotify{ false };
    usize saves{}, notifications{};
    QByteArray previous, saved;
    settings_transfer_hooks_t Hooks()
    {
        return { this,
            []( void *context, settings_scope_t, QString *pError ) {
                auto &p = *static_cast<persistence_t *>( context );
                if ( !p.allowed && pError != nullptr ) { *pError = QStringLiteral( "Scope is read-only." ); }
                return p.allowed;
            },
            []( void *context, settings_scope_t scope, const settings_document_t &candidate, QString *pError ) {
                auto &p = *static_cast<persistence_t *>( context ); ++p.saves;
                p.unchangedBeforeSave = Serialized( *p.pRegistry->scopes[static_cast<usize>( scope )] ) == p.previous;
                if ( p.fail ) { if ( pError != nullptr ) { *pError = QStringLiteral( "Atomic save failed." ); } return false; }
                p.saved = Serialized( candidate ); QSaveFile file( p.path ); file.setDirectWriteFallback( false );
                return file.open( QIODevice::WriteOnly ) && file.write( p.saved ) == p.saved.size() && file.commit();
            } };
    }
    static void Changed( void *context, string_view_t path ) noexcept
    {
        auto &p = *static_cast<persistence_t *>( context ); ++p.notifications;
        QFile file( p.path ); p.fileReadyOnNotify = path.cchLength == 0u && file.open( QIODevice::ReadOnly ) && file.readAll() == p.saved;
    }
};

} // namespace

TEST_CASE( "Settings export contains every resolved registered value and unknown scope members", "[editor][gui][settingstransfer][export]" )
{
    fixture_t f; QTemporaryDir directory; REQUIRE( directory.isValid() );
    const QString path = directory.filePath( QStringLiteral( "portable.cysettings" ) );
    const QByteArray original = Serialized( f.user );
    REQUIRE( EditorSettingsTransfer_Export( &f.registry, path ) == settings_transfer_status_t::OK );
    settings_document_t exported{}; REQUIRE( SettingsDocument_Init( &exported, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
    const QByteArray bytes = ReadFile( path ); REQUIRE( SettingsDocument_Load( &exported, { bytes.constData(), static_cast<usize>( bytes.size() ) } ).status == settings_document_status_t::OK );
    for ( usize i = 0u; i < EditorSettings_Count( &f.registry ); ++i ) {
        const auto &descriptor = *EditorSettings_At( &f.registry, i ); CAPTURE( descriptor.pPath ); setting_value_t value{};
        REQUIRE( Setting_Read( SettingsDocument_Root( &exported ), descriptor, &value, nullptr ) == setting_read_status_t::VALUE );
        CHECK( Setting_ValuesEqual( value, EditorSettings_Resolve( &f.registry, descriptor ).value ) );
    }
    CHECK( bytes.contains( "retained" ) ); CHECK( bytes.contains( "two" ) );
    CHECK( Serialized( f.user ) == original );
    // A bad authored ancestor falls back without preventing a complete export.
    REQUIRE( SettingsDocument_Load( &f.user, StringView_FromCString( "@cykv 1\n@schema \"cypher.settings\" 2\n{ editor = 5 }" ) ).status == settings_document_status_t::OK );
    REQUIRE( EditorSettingsTransfer_Export( &f.registry, path ) == settings_transfer_status_t::OK );
    const QByteArray brokenBefore = Serialized( f.user );
    CHECK( brokenBefore.contains( "editor = 5" ) ); CHECK( ReadFile( path ).contains( "invert_y = false" ) );
    const QByteArray existing = ReadFile( path );
    CHECK( EditorSettingsTransfer_Export( &f.registry, path + QStringLiteral( "/blocked.cysettings" ) ) == settings_transfer_status_t::IO_ERROR );
    CHECK( ReadFile( path ) == existing ); CHECK( Serialized( f.user ) == brokenBefore );
}

TEST_CASE( "Settings import previews masking, merges unknown members and publishes only after saving", "[editor][gui][settingstransfer][import]" )
{
    fixture_t f; QTemporaryDir directory; REQUIRE( directory.isValid() );
    settings_import_t import; persistence_t persistence{ &f.registry, directory.filePath( QStringLiteral( "user.cysettings" ) ) };
    persistence.previous = Serialized( f.user ); WriteFile( persistence.path, persistence.previous );
    REQUIRE( EditorSettings_AddListener( &f.registry, &persistence_t::Changed, &persistence ) );
    REQUIRE( EditorSettingsTransfer_Prepare( &f.registry, kImport, settings_scope_t::USER, &import ) == settings_transfer_status_t::OK );
    CHECK( Serialized( f.user ) == persistence.previous ); CHECK( persistence.notifications == 0u );
    REQUIRE( import.rows.size() == 4 ); CHECK( import.nUnknown == 1u );
    bool masked = false;
    for ( const auto &row : import.rows ) { if ( row.path == QStringLiteral( "editor.grid.size" ) ) { masked = row.note.contains( QStringLiteral( "Project scope" ) ); } }
    CHECK( masked );
    REQUIRE( EditorSettingsTransfer_Apply( &import, persistence.Hooks() ) == settings_transfer_status_t::OK );
    CHECK( persistence.unchangedBeforeSave ); CHECK( persistence.saves == 1u ); CHECK( persistence.notifications == 1u ); CHECK( persistence.fileReadyOnNotify );
    CHECK_FALSE( import.bPrepared ); CHECK( ReadFile( persistence.path ) == Serialized( f.user ) );
    CHECK( EditorSettings_Integer( &f.registry, "editor.grid.size", 0 ) == 64 ); // Higher scope remains authoritative.
    settings_path_t grid{}; REQUIRE( SettingsPath_Parse( StringView_FromCString( "editor.grid.size" ), &grid ) );
    CHECK( SettingsNode_Find( SettingsDocument_Root( &f.user ), grid ) == nullptr ); // Incoming 16 equals the User inherited default.
    CHECK_FALSE( EditorSettings_Bool( &f.registry, "editor.grid.snap", CY_TRUE ) );
    CHECK( EditorSettings_Bool( &f.registry, "editor.camera.invert_y", CY_FALSE ) ); // Unmentioned value stays.
    CHECK( Serialized( f.user ).contains( "retained" ) ); CHECK( Serialized( f.user ).contains( "kept" ) );
    EditorSettings_RemoveListener( &f.registry, &persistence_t::Changed, &persistence );
}

TEST_CASE( "Settings import rejects invalid data, stale reviews, read-only scopes and failed saves", "[editor][gui][settingstransfer][failure]" )
{
    fixture_t f; QTemporaryDir directory; REQUIRE( directory.isValid() );
    const QByteArray before = Serialized( f.user ); settings_import_t import;
    QString error;
    for ( const QByteArray &invalid : { QByteArray( "broken" ), QByteArray( "@cykv 1\n@schema \"cypher.theme\" 2\n{}" ),
            QByteArray( "@cykv 1\n@schema \"cypher.settings\" 2\n{ editor = { grid = { size = 99999 } } }" ),
            QByteArray( "@cykv 1\n@schema \"cypher.settings\" 2\n{ editor = 5 }" ) } ) {
        CHECK( EditorSettingsTransfer_Prepare( &f.registry, invalid, settings_scope_t::USER, &import, &error ) == settings_transfer_status_t::INVALID_DOCUMENT );
        CHECK_FALSE( error.isEmpty() ); CHECK( Serialized( f.user ) == before );
    }
    CHECK( EditorSettingsTransfer_Prepare( &f.registry, kImport, settings_scope_t::WORKSPACE, &import, &error ) == settings_transfer_status_t::NO_SCOPE );
    REQUIRE( EditorSettingsTransfer_Prepare( &f.registry, kImport, settings_scope_t::USER, &import ) == settings_transfer_status_t::OK );
    persistence_t persistence{ &f.registry, directory.filePath( QStringLiteral( "user.cysettings" ) ) }; persistence.previous = before;
    WriteFile( persistence.path, before );
    CHECK( EditorSettingsTransfer_Apply( &import, {}, &error ) == settings_transfer_status_t::READ_ONLY );
    persistence.allowed = false; CHECK( EditorSettingsTransfer_Apply( &import, persistence.Hooks(), &error ) == settings_transfer_status_t::READ_ONLY );
    persistence.allowed = true; persistence.fail = true;
    CHECK( EditorSettingsTransfer_Apply( &import, persistence.Hooks(), &error ) == settings_transfer_status_t::IO_ERROR );
    CHECK( Serialized( f.user ) == before ); CHECK( ReadFile( persistence.path ) == before ); CHECK( persistence.notifications == 0u );
    persistence.fail = false;
    const auto *pSize = EditorSettings_Find( &f.registry, StringView_FromCString( "editor.grid.size" ) ); REQUIRE( pSize != nullptr );
    setting_value_t value{}; value.type = setting_type_t::INTEGER; value.nValue = 128;
    REQUIRE( EditorSettings_Write( &f.registry, settings_scope_t::PROJECT, *pSize, value ) == settings_registry_status_t::OK );
    CHECK( EditorSettingsTransfer_Apply( &import, persistence.Hooks(), &error ) == settings_transfer_status_t::CHANGED );
    CHECK( Serialized( f.user ) == before ); CHECK( ReadFile( persistence.path ) == before ); CHECK( persistence.saves == 1u );
}

TEST_CASE( "Import preparation preserves live stores and the previous candidate at every allocation failure", "[editor][gui][settingstransfer][allocation]" )
{
    audit_t audit;
    {
        fixture_t f( &audit.allocator ); settings_import_t import;
        audit.calls = 0u;
        REQUIRE( EditorSettingsTransfer_Prepare( &f.registry, kImport, settings_scope_t::USER, &import ) == settings_transfer_status_t::OK );
        const usize allocations = audit.calls; REQUIRE( allocations > 0u );
        const QByteArray before = Serialized( f.user ), candidate = Serialized( import.candidate ); const auto *pLive = f.user.pDocument;
        const auto *pCandidate = import.candidate.pDocument; const usize baselineBytes = audit.bytes;
        for ( usize failure = 0u; failure < allocations; ++failure ) {
            CAPTURE( failure, allocations ); audit.calls = 0u; audit.failAt = failure;
            const auto status = EditorSettingsTransfer_Prepare( &f.registry, kImport, settings_scope_t::USER, &import ); audit.failAt = CY_USIZE_MAX;
            CHECK( status == settings_transfer_status_t::OUT_OF_MEMORY ); CHECK( f.user.pDocument == pLive ); CHECK( import.candidate.pDocument == pCandidate );
            CHECK( Serialized( f.user ) == before ); CHECK( Serialized( import.candidate ) == candidate ); CHECK( audit.bytes == baselineBytes );
        }
        QTemporaryDir directory; REQUIRE( directory.isValid() );
        persistence_t persistence{ &f.registry, directory.filePath( QStringLiteral( "user.cysettings" ) ) }; persistence.previous = before;
        audit.calls = 0u; audit.failAt = 0u;
        const auto status = EditorSettingsTransfer_Apply( &import, persistence.Hooks() ); audit.failAt = CY_USIZE_MAX;
        CHECK( status == settings_transfer_status_t::OUT_OF_MEMORY ); CHECK( persistence.saves == 0u ); CHECK( f.user.pDocument == pLive ); CHECK( audit.bytes == baselineBytes );
    }
    CHECK( audit.bytes == 0u );
}

TEST_CASE( "Import review cancellation and missing persistence leave settings untouched", "[editor][gui][settingstransfer][dialog]" )
{
    fixture_t f; QTemporaryDir directory; REQUIRE( directory.isValid() );
    const QString path = directory.filePath( QStringLiteral( "incoming.cysettings" ) ); WriteFile( path, kImport );
    const QByteArray before = Serialized( f.user );
    std::unique_ptr<QDialog> review( EditorSettingsTransfer_CreateImport( nullptr, &f.registry, path ) );
    auto *pApply = review->findChild<QPushButton *>( QStringLiteral( "SettingsImportApply" ) ); REQUIRE( pApply != nullptr ); CHECK_FALSE( pApply->isEnabled() );
    auto *pRows = review->findChild<QTreeWidget *>( QStringLiteral( "SettingsImportRows" ) ); REQUIRE( pRows != nullptr ); CHECK( pRows->topLevelItemCount() == 4 );
    review->reject(); CHECK( review->result() == QDialog::Rejected ); CHECK( Serialized( f.user ) == before );
    std::unique_ptr<QDialog> settings( EditorSettingsDialog_Create( nullptr, &f.registry ) );
    CHECK( settings->findChild<QPushButton *>( QStringLiteral( "SettingsImport" ) ) != nullptr );
    CHECK( settings->findChild<QPushButton *>( QStringLiteral( "SettingsExport" ) ) != nullptr );
}

TEST_CASE( "Capture the reviewed settings import dialog", "[.][editor][gui][settingstransfer][capture]" )
{
    editor_gui_t gui{};
    auto *pApplication = qobject_cast<QApplication *>( QCoreApplication::instance() );
    REQUIRE( pApplication != nullptr );
    REQUIRE( EditorGui_Init( &gui, pApplication, Allocator_GetSystem() ) == editor_gui_status_t::OK );
    fixture_t f; QTemporaryDir directory; REQUIRE( directory.isValid() );
    const QString path = directory.filePath( QStringLiteral( "incoming.cysettings" ) );
    REQUIRE( EditorSettingsTransfer_Export( &f.registry, path ) == settings_transfer_status_t::OK );
    persistence_t persistence{ &f.registry, directory.filePath( QStringLiteral( "user.cysettings" ) ) };
    const QByteArray before = Serialized( f.user );
    std::unique_ptr<QDialog> review( EditorSettingsTransfer_CreateImport( nullptr, &f.registry, path, settings_scope_t::USER, persistence.Hooks() ) );
    review->show(); QCoreApplication::processEvents();
    auto *pApply = review->findChild<QPushButton *>( QStringLiteral( "SettingsImportApply" ) );
    REQUIRE( pApply != nullptr ); CHECK( pApply->isEnabled() );
    REQUIRE( QDir().mkpath( QStringLiteral( "artifacts" ) ) );
    CHECK( review->grab().save( QStringLiteral( "artifacts/mason_settings_import_review.png" ) ) );
    review->reject(); CHECK( Serialized( f.user ) == before ); CHECK( persistence.saves == 0u );
    review.reset(); EditorGui_Shutdown( &gui );
}
