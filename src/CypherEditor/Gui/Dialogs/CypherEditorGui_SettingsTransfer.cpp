//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_SettingsTransfer.cpp
//  Purpose: Implements complete settings export and reviewed scope imports.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_SettingsTransfer.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <utility>

namespace cypher::editor::gui
{

using namespace cypher::common;

namespace
{

QString Text( string_view_t value ) { return QString::fromUtf8( value.pData != nullptr ? value.pData : "", static_cast<qsizetype>( value.cchLength ) ); }

settings_transfer_status_t Fail( settings_transfer_status_t status, const QString &message, QString *pError )
{
    if ( pError != nullptr ) { *pError = message; }
    return status;
}

settings_transfer_status_t DocumentFailure( settings_document_status_t status, QString *pError )
{
    return Fail( status == settings_document_status_t::OUT_OF_MEMORY ? settings_transfer_status_t::OUT_OF_MEMORY : settings_transfer_status_t::INVALID_DOCUMENT,
                 QString::fromLatin1( SettingsDocument_StatusName( status ) ), pError );
}

settings_document_status_t Serialize( const settings_document_t &document, QByteArray &bytes )
{
    text_buffer_t buffer{};
    if ( !TextBuffer_Init( &buffer, document.pAllocator ) ) { return settings_document_status_t::OUT_OF_MEMORY; }
    const auto status = SettingsDocument_Write( &document, &buffer );
    if ( status == settings_document_status_t::OK ) { bytes = QByteArray( TextBuffer_CStr( &buffer ), static_cast<qsizetype>( TextBuffer_Length( &buffer ) ) ); }
    return status;
}

// All merging is into a private tree. Even a partially cloned member is safe
// to discard when allocation fails; no live store has been touched.
bool Merge( settings_document_t &target, key_value_t *pTarget, const key_value_t *pSource )
{
    for ( const auto *pMember = KeyValue_FirstChild( pSource ); pMember != nullptr; pMember = KeyValue_NextSibling( pMember ) ) {
        auto *pExisting = KeyValue_Find( pTarget, KeyValue_Name( pMember ) );
        if ( pExisting != nullptr && KeyValue_Type( pExisting ) == key_value_type_t::OBJECT && KeyValue_Type( pMember ) == key_value_type_t::OBJECT ) {
            if ( !Merge( target, pExisting, pMember ) ) { return false; }
        } else {
            if ( pExisting != nullptr && !KeyValue_Remove( target.pDocument, pTarget, pExisting ) ) { return false; }
            if ( KeyValue_CloneInto( target.pDocument, pTarget, pMember ) == nullptr ) { return false; }
        }
    }
    return true;
}

usize UnknownLeaves( const key_value_t *pNode, const QSet<QString> &known, const QString &prefix = {} )
{
    usize count = 0u;
    for ( const auto *pMember = KeyValue_FirstChild( pNode ); pMember != nullptr; pMember = KeyValue_NextSibling( pMember ) ) {
        const QString path = prefix.isEmpty() ? Text( KeyValue_Name( pMember ) ) : prefix + QLatin1Char( '.' ) + Text( KeyValue_Name( pMember ) );
        if ( known.contains( path ) ) { continue; }
        if ( KeyValue_Type( pMember ) == key_value_type_t::OBJECT && KeyValue_ChildCount( pMember ) != 0u ) { count += UnknownLeaves( pMember, known, path ); }
        else { ++count; }
    }
    return count;
}

QString ValueText( const setting_value_t &value )
{
    switch ( value.type ) {
        case setting_type_t::BOOL: return value.bValue ? QStringLiteral( "true" ) : QStringLiteral( "false" );
        case setting_type_t::INTEGER: return QString::number( value.nValue );
        case setting_type_t::REAL: return QString::number( value.flValue, 'g', 12 );
        case setting_type_t::COLOR: { char color[10]{}; SettingColor_Format( value.rgba, color ); return QString::fromLatin1( color ); }
        default: return Text( value.text );
    }
}

void SwapDocumentTree( settings_document_t &a, settings_document_t &b ) noexcept
{
    // Both stores use the same allocator and accepted identity. The attached
    // settings_document_t remains at its original address for its owner.
    std::swap( a.pDocument, b.pDocument );
    std::swap( a.nLoadedVersion, b.nLoadedVersion );
}

void ClearBlockedAncestor( settings_document_t &document, const setting_descriptor_t &descriptor )
{
    settings_path_t path{};
    if ( !SettingsPath_Parse( StringView_FromCString( descriptor.pPath ), &path ) ) { return; }
    auto *pParent = KeyValue_Root( document.pDocument );
    for ( usize i = 0u; i + 1u < path.nSegments; ++i ) {
        auto *pChild = KeyValue_Find( pParent, path.segments[i] );
        if ( pChild == nullptr ) { return; }
        if ( KeyValue_Type( pChild ) != key_value_type_t::OBJECT ) {
            // An invalid authored ancestor has no unknown child members to
            // preserve. The export writes the registered resolved fallback.
            ( void )KeyValue_Remove( document.pDocument, pParent, pChild ); return;
        }
        pParent = pChild;
    }
}

class settings_import_dialog_t final : public QDialog {
public:
    settings_import_dialog_t( QWidget *pParent, settings_registry_t *pRegistry, const QString &path,
                             settings_scope_t scope, settings_transfer_hooks_t hooks ) : QDialog( pParent ), m_hooks( hooks )
    {
        setObjectName( QStringLiteral( "EditorSettingsImport" ) );
        setWindowTitle( QStringLiteral( "Review Settings Import" ) ); resize( 900, 560 );
        auto *pLayout = new QVBoxLayout( this );
        auto *pSummary = new QLabel( QStringLiteral( "Import into %1 settings. Only values present in the file are merged; existing unmentioned values stay. "
                                                   "Higher scopes continue to control their settings." ).arg( QString::fromLatin1( EditorSettings_ScopeName( scope ) ) ), this );
        pSummary->setWordWrap( true ); pLayout->addWidget( pSummary );
        auto *pRows = new QTreeWidget( this ); pRows->setObjectName( QStringLiteral( "SettingsImportRows" ) );
        pRows->setHeaderLabels( { QStringLiteral( "Setting" ), QStringLiteral( "Current" ), QStringLiteral( "Imported" ), QStringLiteral( "Effect" ) } );
        pRows->setRootIsDecorated( false ); pRows->setAlternatingRowColors( true );
        pRows->header()->setSectionResizeMode( QHeaderView::ResizeToContents ); pRows->header()->setStretchLastSection( true ); pLayout->addWidget( pRows, 1 );
        m_pStatus = new QLabel( this ); m_pStatus->setObjectName( QStringLiteral( "SettingsImportStatus" ) ); m_pStatus->setWordWrap( true ); pLayout->addWidget( m_pStatus );
        auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Cancel, this );
        auto *pApply = pButtons->addButton( QStringLiteral( "Apply Import" ), QDialogButtonBox::AcceptRole ); pApply->setObjectName( QStringLiteral( "SettingsImportApply" ) );
        pLayout->addWidget( pButtons ); QObject::connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );
        QFile file( path ); QString error;
        bool ready = file.open( QIODevice::ReadOnly );
        if ( !ready ) { error = file.errorString(); }
        else if ( file.size() > static_cast<qint64>( CY_SETTINGS_TEXT_MAX_BYTES ) ) { ready = false; error = QStringLiteral( "The settings file exceeds the 16 MiB limit." ); }
        else {
            const QByteArray bytes = file.read( static_cast<qint64>( CY_SETTINGS_TEXT_MAX_BYTES ) + 1 );
            if ( file.error() != QFileDevice::NoError || bytes.size() > static_cast<qsizetype>( CY_SETTINGS_TEXT_MAX_BYTES ) ) {
                ready = false; error = QStringLiteral( "The settings file could not be read completely." );
            } else { ready = EditorSettingsTransfer_Prepare( pRegistry, bytes, scope, &m_import, &error ) == settings_transfer_status_t::OK; }
        }
        for ( const auto &row : m_import.rows ) {
            const QByteArray settingPath = row.path.toUtf8();
            const auto *descriptor = EditorSettings_Find( pRegistry, { settingPath.constData(), static_cast<usize>( settingPath.size() ) } );
            const QString label = descriptor != nullptr && descriptor->pLabel != nullptr ? QString::fromUtf8( descriptor->pLabel ) : row.path;
            const QString page = descriptor != nullptr && descriptor->pPage != nullptr ? QString::fromUtf8( descriptor->pPage ) : QString();
            auto *pItem = new QTreeWidgetItem( pRows, { page.isEmpty() ? label : page + QStringLiteral( " / " ) + label, row.current, row.incoming, row.note } );
            for ( int column = 0; column < 4; ++column ) { pItem->setToolTip( column, pItem->text( column ) ); }
            pItem->setToolTip( 0, row.path );
        }
        if ( ready && m_hooks.pfnCanWrite != nullptr ) {
            ready = m_hooks.pfnCanWrite( m_hooks.pContext, scope, &error );
            if ( !ready && error.isEmpty() ) { error = QStringLiteral( "This scope is read-only; importing is unavailable." ); }
        }
        if ( ready && m_hooks.pfnSave == nullptr ) { ready = false; error = QStringLiteral( "This scope has no settings persistence handler; review is available, but Apply is disabled." ); }
        pApply->setEnabled( ready );
        m_pStatus->setText( ready ? QStringLiteral( "%1 registered values reviewed; %2 unknown members are preserved. "
                                                   "Theme, keymap and layout IDs refer to separately installed files." ).arg( m_import.rows.size() ).arg( m_import.nUnknown ) : error );
        QObject::connect( pApply, &QPushButton::clicked, this, [this]() {
            QString error;
            if ( EditorSettingsTransfer_Apply( &m_import, m_hooks, &error ) != settings_transfer_status_t::OK ) { m_pStatus->setText( error ); return; }
            QDialog::accept();
        } );
    }
private:
    settings_import_t m_import;
    settings_transfer_hooks_t m_hooks;
    QLabel *m_pStatus{ nullptr };
};

} // namespace

settings_transfer_status_t EditorSettingsTransfer_Export( const settings_registry_t *pRegistry, const QString &path, QString *pErrorOut )
{
    if ( pErrorOut != nullptr ) { pErrorOut->clear(); }
    if ( pRegistry == nullptr || path.isEmpty() || pRegistry->descriptors.pAllocator == nullptr ) {
        return Fail( settings_transfer_status_t::INVALID_DOCUMENT, QStringLiteral( "Settings registry or destination is unavailable." ), pErrorOut );
    }
    settings_document_t complete{};
    auto status = SettingsDocument_Init( &complete, pRegistry->descriptors.pAllocator, EditorSettings_FileIdentity() );
    if ( status != settings_document_status_t::OK ) { return DocumentFailure( status, pErrorOut ); }
    for ( usize i = static_cast<usize>( settings_scope_t::COUNT ); i-- > 0u; ) {
        if ( pRegistry->scopes[i] != nullptr && !Merge( complete, KeyValue_Root( complete.pDocument ), SettingsDocument_Root( pRegistry->scopes[i] ) ) ) {
            return DocumentFailure( settings_document_status_t::OUT_OF_MEMORY, pErrorOut );
        }
    }
    for ( usize i = 0u; i < EditorSettings_Count( pRegistry ); ++i ) {
        const auto &descriptor = *EditorSettings_At( pRegistry, i );
        const auto value = EditorSettings_Resolve( pRegistry, descriptor );
        ClearBlockedAncestor( complete, descriptor );
        status = Setting_Write( &complete, descriptor, value.value );
        if ( status != settings_document_status_t::OK ) { return DocumentFailure( status, pErrorOut ); }
    }
    QByteArray bytes; status = Serialize( complete, bytes );
    if ( status != settings_document_status_t::OK ) { return DocumentFailure( status, pErrorOut ); }
    QSaveFile file( path ); file.setDirectWriteFallback( false );
    if ( !file.open( QIODevice::WriteOnly ) || file.write( bytes ) != bytes.size() || !file.commit() ) {
        return Fail( settings_transfer_status_t::IO_ERROR, file.errorString(), pErrorOut );
    }
    return settings_transfer_status_t::OK;
}

settings_transfer_status_t EditorSettingsTransfer_Prepare( settings_registry_t *pRegistry, const QByteArray &text, settings_scope_t scope,
                                                          settings_import_t *pImport, QString *pErrorOut )
{
    if ( pErrorOut != nullptr ) { pErrorOut->clear(); }
    if ( pRegistry == nullptr || pImport == nullptr || scope >= settings_scope_t::COUNT || pRegistry->scopes[static_cast<usize>( scope )] == nullptr ) {
        return Fail( settings_transfer_status_t::NO_SCOPE, QStringLiteral( "The selected scope has no settings store." ), pErrorOut );
    }
    auto *pTarget = pRegistry->scopes[static_cast<usize>( scope )];
    settings_document_t imported{};
    auto status = SettingsDocument_Init( &imported, pTarget->pAllocator, EditorSettings_FileIdentity() );
    if ( status != settings_document_status_t::OK ) { return DocumentFailure( status, pErrorOut ); }
    status = SettingsDocument_Load( &imported, { text.constData(), static_cast<usize>( text.size() ) } ).status;
    if ( status != settings_document_status_t::OK ) { return DocumentFailure( status, pErrorOut ); }
    settings_import_t prepared;
    QSet<QString> known;
    for ( usize i = 0u; i < EditorSettings_Count( pRegistry ); ++i ) {
        const auto &descriptor = *EditorSettings_At( pRegistry, i ); known.insert( QString::fromUtf8( descriptor.pPath ) );
        setting_value_t incoming{}; setting_problem_code_t problem{};
        const auto read = Setting_Read( SettingsDocument_Root( &imported ), descriptor, &incoming, &problem );
        if ( read == setting_read_status_t::INVALID ) {
            return Fail( settings_transfer_status_t::INVALID_DOCUMENT, QStringLiteral( "%1: %2. No settings were changed." )
                         .arg( QString::fromUtf8( descriptor.pPath ), QString::fromLatin1( Setting_ProblemName( problem ) ) ), pErrorOut );
        }
        if ( read == setting_read_status_t::ABSENT ) { continue; }
        const auto current = EditorSettings_Resolve( pRegistry, descriptor );
        QString note = Setting_ValuesEqual( incoming, current.value ) ? QStringLiteral( "Same effective value" ) : QStringLiteral( "Updates this scope" );
        if ( current.source < scope ) { note = QStringLiteral( "%1 scope still controls the effective value" ).arg( QString::fromLatin1( EditorSettings_ScopeName( current.source ) ) ); }
        prepared.rows.append( { QString::fromUtf8( descriptor.pPath ), ValueText( current.value ), ValueText( incoming ), note } );
    }
    for ( usize i = 0u; i < static_cast<usize>( settings_scope_t::COUNT ); ++i ) {
        prepared.scopeStores[i] = pRegistry->scopes[i];
        if ( prepared.scopeStores[i] == nullptr ) { continue; }
        status = Serialize( *prepared.scopeStores[i], prepared.scopeBaselines[i] );
        if ( status != settings_document_status_t::OK ) { return DocumentFailure( status, pErrorOut ); }
    }
    prepared.nDescriptors = EditorSettings_Count( pRegistry );
    prepared.baseline = prepared.scopeBaselines[static_cast<usize>( scope )];
    status = SettingsDocument_Init( &prepared.candidate, pTarget->pAllocator, pTarget->identity );
    if ( status == settings_document_status_t::OK ) { status = SettingsDocument_Load( &prepared.candidate, { prepared.baseline.constData(), static_cast<usize>( prepared.baseline.size() ) } ).status; }
    if ( status != settings_document_status_t::OK ) { return DocumentFailure( status, pErrorOut ); }
    if ( !Merge( prepared.candidate, KeyValue_Root( prepared.candidate.pDocument ), SettingsDocument_Root( &imported ) ) ) {
        return DocumentFailure( settings_document_status_t::OUT_OF_MEMORY, pErrorOut );
    }
    for ( usize i = 0u; i < EditorSettings_Count( pRegistry ); ++i ) {
        const auto &descriptor = *EditorSettings_At( pRegistry, i ); setting_value_t incoming{};
        if ( Setting_Read( SettingsDocument_Root( &imported ), descriptor, &incoming, nullptr ) != setting_read_status_t::VALUE ) { continue; }
        status = Setting_WriteOverride( &prepared.candidate, descriptor, incoming, EditorSettings_ResolveInherited( pRegistry, descriptor, scope ).value );
        if ( status != settings_document_status_t::OK ) { return DocumentFailure( status, pErrorOut ); }
    }
    prepared.nUnknown = UnknownLeaves( SettingsDocument_Root( &imported ), known );
    prepared.pRegistry = pRegistry; prepared.pTarget = pTarget; prepared.scope = scope; prepared.bPrepared = true;
    SettingsDocument_Shutdown( &pImport->candidate );
    pImport->candidate.pAllocator = prepared.candidate.pAllocator; pImport->candidate.identity = prepared.candidate.identity;
    SwapDocumentTree( pImport->candidate, prepared.candidate );
    pImport->pRegistry = prepared.pRegistry; pImport->pTarget = pTarget; pImport->scope = scope;
    pImport->baseline = std::move( prepared.baseline ); pImport->rows = std::move( prepared.rows );
    pImport->nDescriptors = prepared.nDescriptors;
    for ( usize i = 0u; i < static_cast<usize>( settings_scope_t::COUNT ); ++i ) {
        pImport->scopeStores[i] = prepared.scopeStores[i]; pImport->scopeBaselines[i] = std::move( prepared.scopeBaselines[i] );
    }
    pImport->nUnknown = prepared.nUnknown; pImport->bPrepared = true;
    return settings_transfer_status_t::OK;
}

settings_transfer_status_t EditorSettingsTransfer_Apply( settings_import_t *pImport, const settings_transfer_hooks_t &hooks, QString *pErrorOut )
{
    if ( pErrorOut != nullptr ) { pErrorOut->clear(); }
    if ( pImport == nullptr || !pImport->bPrepared || pImport->pRegistry == nullptr || pImport->pTarget == nullptr ) {
        return Fail( settings_transfer_status_t::INVALID_DOCUMENT, QStringLiteral( "No validated import is ready." ), pErrorOut );
    }
    if ( hooks.pfnSave == nullptr || ( hooks.pfnCanWrite != nullptr && !hooks.pfnCanWrite( hooks.pContext, pImport->scope, pErrorOut ) ) ) {
        return Fail( settings_transfer_status_t::READ_ONLY, pErrorOut != nullptr && !pErrorOut->isEmpty() ? *pErrorOut : QStringLiteral( "This scope cannot be saved." ), pErrorOut );
    }
    if ( EditorSettings_Count( pImport->pRegistry ) != pImport->nDescriptors ) {
        return Fail( settings_transfer_status_t::CHANGED, QStringLiteral( "The registered settings changed after review. Open the import again." ), pErrorOut );
    }
    for ( usize i = 0u; i < static_cast<usize>( settings_scope_t::COUNT ); ++i ) {
        if ( pImport->pRegistry->scopes[i] != pImport->scopeStores[i] ) {
            return Fail( settings_transfer_status_t::CHANGED, QStringLiteral( "The settings scopes changed after review. Open the import again." ), pErrorOut );
        }
        if ( pImport->scopeStores[i] == nullptr ) { continue; }
        QByteArray current; const auto status = Serialize( *pImport->scopeStores[i], current );
        if ( status != settings_document_status_t::OK ) { return DocumentFailure( status, pErrorOut ); }
        if ( current != pImport->scopeBaselines[i] ) {
            return Fail( settings_transfer_status_t::CHANGED, QStringLiteral( "Settings changed after review. Open the import again to review the current values." ), pErrorOut );
        }
    }
    if ( pImport->pRegistry->scopes[static_cast<usize>( pImport->scope )] != pImport->pTarget ) {
        return Fail( settings_transfer_status_t::CHANGED, QStringLiteral( "The settings scope changed after review. Open the import again." ), pErrorOut );
    }
    if ( !hooks.pfnSave( hooks.pContext, pImport->scope, pImport->candidate, pErrorOut ) ) {
        return Fail( settings_transfer_status_t::IO_ERROR, pErrorOut != nullptr && !pErrorOut->isEmpty() ? *pErrorOut : QStringLiteral( "Saving the imported settings failed; live settings were preserved." ), pErrorOut );
    }
    SwapDocumentTree( *pImport->pTarget, pImport->candidate );
    pImport->bPrepared = false;
    EditorSettings_SetScope( pImport->pRegistry, pImport->scope, pImport->pTarget );
    return settings_transfer_status_t::OK;
}

QDialog *EditorSettingsTransfer_CreateImport( QWidget *pParent, settings_registry_t *pRegistry, const QString &path,
                                               settings_scope_t scope, const settings_transfer_hooks_t &hooks )
{
    return new settings_import_dialog_t( pParent, pRegistry, path, scope, hooks );
}

} // namespace cypher::editor::gui
