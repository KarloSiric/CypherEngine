//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileDocumentBridge.cpp
//  Purpose: Implements Qt/Core translation and transactional map file I/O.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherTileDocumentBridge.h"

#include "Core/CypherTileMapSerialization.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherCommon/Tier1/CypherCommon_StringView.h"
#include "CypherCommon/Tier1/CypherCommon_TextBuffer.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <new>
#include <utility>

namespace cypher::tools::tile_editor
{

namespace
{

QString tr( const char *pText )
{
    return QCoreApplication::translate( "CypherTileDocumentBridge", pText );
}

void SetError( QString *pErrorOut, const QString &message )
{
    if ( pErrorOut != nullptr ) *pErrorOut = message;
}

std::unique_ptr<tile_map_document_t> AllocateDocument()
{
    return std::unique_ptr<tile_map_document_t>(
        new ( std::nothrow ) tile_map_document_t{} );
}

} // namespace

CypherTileDocumentBridge::CypherTileDocumentBridge()
    : m_pDocument( AllocateDocument() )
{
}

CypherTileDocumentBridge::~CypherTileDocumentBridge()
{
    if ( isInitialized() ) {
        CypherTileMapDocument_Shutdown( m_pDocument.get() );
    }
}

const tile_map_document_t *CypherTileDocumentBridge::document() const
{
    return m_pDocument.get();
}

tile_map_document_t *CypherTileDocumentBridge::document()
{
    return m_pDocument.get();
}

bool CypherTileDocumentBridge::isInitialized() const
{
    return m_pDocument != nullptr &&
           CypherTileMapDocument_IsInitialized( m_pDocument.get() );
}

bool CypherTileDocumentBridge::isDirty() const
{
    return isInitialized() && CypherTileMapDocument_IsDirty( m_pDocument.get() );
}

const QString &CypherTileDocumentBridge::filePath() const
{
    return m_filePath;
}

void CypherTileDocumentBridge::markSaved()
{
    if ( isInitialized() ) {
        CypherTileMapDocument_MarkSaved( m_pDocument.get() );
    }
}

bool CypherTileDocumentBridge::newDocument(
    const tile_map_document_desc_t &description,
    QString *pErrorOut )
{
    std::unique_ptr<tile_map_document_t> pCandidate = AllocateDocument();
    if ( pCandidate == nullptr ) {
        SetError( pErrorOut, tr( "Out of memory while creating a map document." ) );
        return false;
    }
    const tile_map_document_status_t status = CypherTileMapDocument_Init(
        pCandidate.get(),
        Allocator_GetSystem(),
        description );
    if ( status != tile_map_document_status_t::OK ) {
        SetError( pErrorOut, documentError( tr( "Create map" ), status ) );
        return false;
    }
    replaceDocument( std::move( pCandidate ), {} );
    return true;
}

bool CypherTileDocumentBridge::loadFromFile(
    const QString &path,
    QString *pErrorOut )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        SetError( pErrorOut, tr( "Could not open %1: %2" )
            .arg( QDir::toNativeSeparators( path ), file.errorString() ) );
        return false;
    }
    if ( file.size() < 0 ||
         file.size() > static_cast<qint64>( TILE_MAP_SERIALIZATION_MAX_TEXT_BYTES ) ) {
        SetError( pErrorOut, tr( "Map source exceeds the %1 MiB input limit." )
            .arg( TILE_MAP_SERIALIZATION_MAX_TEXT_BYTES / CY_MIB ) );
        return false;
    }
    const QByteArray source = file.readAll();
    if ( source.size() != file.size() ) {
        SetError( pErrorOut, tr( "Could not read the complete map source: %1" )
            .arg( file.errorString() ) );
        return false;
    }

    std::unique_ptr<tile_map_document_t> pCandidate = AllocateDocument();
    if ( pCandidate == nullptr ) {
        SetError( pErrorOut, tr( "Out of memory while loading the map." ) );
        return false;
    }
    const tile_map_serialization_result_t result =
        CypherTileMapSerialization_LoadFromText(
            { source.constData(), static_cast<usize>( source.size() ) },
            Allocator_GetSystem(),
            pCandidate.get() );
    if ( result.status != tile_map_serialization_status_t::OK ) {
        QString detail = QString::fromLatin1(
            CypherTileMapSerialization_StatusName( result.status ) );
        if ( result.field[0] != '\0' ) {
            detail += tr( " at %1" ).arg( QString::fromUtf8( result.field ) );
        }
        if ( result.location.nLine != 0u ) {
            detail += tr( " (line %1, column %2)" )
                .arg( result.location.nLine )
                .arg( result.location.nColumn );
        }
        SetError( pErrorOut, tr( "Map decode failed: %1" ).arg( detail ) );
        return false;
    }
    replaceDocument(
        std::move( pCandidate ),
        QFileInfo( path ).absoluteFilePath() );
    return true;
}

bool CypherTileDocumentBridge::saveToFile(
    const QString &path,
    QString *pErrorOut )
{
    if ( !writeSnapshotToFile( path, pErrorOut ) ) return false;

    m_filePath = QFileInfo( path ).absoluteFilePath();
    CypherTileMapDocument_MarkSaved( m_pDocument.get() );
    return true;
}

bool CypherTileDocumentBridge::writeSnapshotToFile(
    const QString &path,
    QString *pErrorOut ) const
{
    if ( !isInitialized() || path.isEmpty() ) {
        SetError( pErrorOut, tr( "Save requires an initialized map and a path." ) );
        return false;
    }
    if ( CypherTileMapDocument_IsEditGroupOpen( m_pDocument.get() ) ) {
        SetError( pErrorOut, tr(
            "Finish or cancel the active paint operation before saving." ) );
        return false;
    }

    // text_buffer_t is an RAII owner: its destructor calls TextBuffer_Shutdown
    // on every return path below.
    text_buffer_t encoded{};
    if ( !TextBuffer_Init( &encoded, Allocator_GetSystem() ) ) {
        SetError( pErrorOut, tr( "Out of memory while preparing map output." ) );
        return false;
    }
    const tile_map_serialization_result_t result =
        CypherTileMapSerialization_SaveToText( m_pDocument.get(), &encoded );
    if ( result.status != tile_map_serialization_status_t::OK ) {
        SetError( pErrorOut, tr( "Map encode failed: %1" ).arg(
            QString::fromLatin1(
                CypherTileMapSerialization_StatusName( result.status ) ) ) );
        return false;
    }

    QSaveFile file( path );
    if ( !file.open( QIODevice::WriteOnly ) ) {
        SetError( pErrorOut, tr( "Could not create %1: %2" )
            .arg( QDir::toNativeSeparators( path ), file.errorString() ) );
        return false;
    }
    const qint64 expected = static_cast<qint64>( TextBuffer_Length( &encoded ) );
    const qint64 written = file.write( TextBuffer_Data( &encoded ), expected );
    if ( written != expected || !file.commit() ) {
        SetError( pErrorOut, tr( "Could not finish writing %1: %2" )
            .arg( QDir::toNativeSeparators( path ), file.errorString() ) );
        return false;
    }

    return true;
}

bool CypherTileDocumentBridge::setDescription(
    const tile_map_document_desc_t &description, QString *pErrorOut )
{
    const auto status = CypherTileMapDocument_SetDescription( m_pDocument.get(), description );
    if ( status == tile_map_document_status_t::OK ) return true;
    if ( status == tile_map_document_status_t::OUT_OF_BOUNDS ) {
        SetError( pErrorOut, tr( "The requested dimensions would remove authored cells or markers. "
            "Move or delete that content before shrinking the map." ) );
    } else if ( status == tile_map_document_status_t::INVALID_STATE ) {
        SetError( pErrorOut, tr( "Finish or cancel the active edit before changing map properties." ) );
    } else {
        SetError( pErrorOut, documentError( tr( "Change map properties" ), status ) );
    }
    return false;
}

bool CypherTileDocumentBridge::beginEdit(
    const QString &label,
    QString *pErrorOut )
{
    const QByteArray utf8 = label.toUtf8();
    const tile_map_document_status_t status =
        CypherTileMapDocument_BeginEditGroup(
            m_pDocument.get(),
            { utf8.constData(), static_cast<usize>( utf8.size() ) } );
    if ( status == tile_map_document_status_t::OK ) return true;
    SetError( pErrorOut, documentError( tr( "Begin edit" ), status ) );
    return false;
}

bool CypherTileDocumentBridge::commitEdit( QString *pErrorOut )
{
    const tile_map_document_status_t status =
        CypherTileMapDocument_CommitEditGroup( m_pDocument.get() );
    if ( status == tile_map_document_status_t::OK ) return true;
    SetError( pErrorOut, documentError( tr( "Commit edit" ), status ) );
    return false;
}

void CypherTileDocumentBridge::cancelEdit()
{
    CypherTileMapDocument_CancelEditGroup( m_pDocument.get() );
}

bool CypherTileDocumentBridge::paintCell(
    tile_map_grid_coord_t coordinate,
    const tile_map_paint_t &paint,
    QString *pErrorOut )
{
    const tile_map_document_status_t status = CypherTileMapDocument_PaintCell(
        m_pDocument.get(), coordinate, paint );
    if ( status == tile_map_document_status_t::OK ) return true;
    SetError( pErrorOut, documentError( tr( "Paint cell" ), status ) );
    return false;
}

bool CypherTileDocumentBridge::eraseCell(
    tile_map_grid_coord_t coordinate,
    QString *pErrorOut )
{
    const tile_map_document_status_t status = CypherTileMapDocument_EraseCell(
        m_pDocument.get(), coordinate );
    if ( status == tile_map_document_status_t::OK ) return true;
    SetError( pErrorOut, documentError( tr( "Erase cell" ), status ) );
    return false;
}

bool CypherTileDocumentBridge::paintRect(
    const tile_map_grid_rect_t &rectangle,
    const tile_map_paint_t &paint,
    QString *pErrorOut )
{
    const tile_map_document_status_t status = CypherTileMapDocument_PaintRect(
        m_pDocument.get(), rectangle, paint );
    if ( status == tile_map_document_status_t::OK ) return true;
    SetError( pErrorOut, documentError( tr( "Paint rectangle" ), status ) );
    return false;
}

bool CypherTileDocumentBridge::eraseRect(
    const tile_map_grid_rect_t &rectangle,
    QString *pErrorOut )
{
    const tile_map_document_status_t status = CypherTileMapDocument_EraseRect(
        m_pDocument.get(), rectangle );
    if ( status == tile_map_document_status_t::OK ) return true;
    SetError( pErrorOut, documentError( tr( "Erase rectangle" ), status ) );
    return false;
}

bool CypherTileDocumentBridge::placePlayerSpawn(
    tile_map_grid_coord_t coordinate,
    float yawDegrees,
    QString *pErrorOut )
{
    const tile_map_document_status_t status =
        CypherTileMapDocument_PlacePlayerSpawn(
            m_pDocument.get(), coordinate, yawDegrees );
    if ( status == tile_map_document_status_t::OK ) return true;
    SetError( pErrorOut, documentError( tr( "Place player spawn" ), status ) );
    return false;
}

bool CypherTileDocumentBridge::removePlayerSpawn( QString *pErrorOut )
{
    const tile_map_document_status_t status =
        CypherTileMapDocument_RemovePlayerSpawn( m_pDocument.get() );
    if ( status == tile_map_document_status_t::OK ) return true;
    SetError( pErrorOut, documentError( tr( "Remove player spawn" ), status ) );
    return false;
}

bool CypherTileDocumentBridge::placeDoor(
    tile_map_grid_coord_t coordinate,
    tile_map_marker_side_t side,
    unique_id_t *pDoorIdOut,
    QString *pErrorOut )
{
    const tile_map_document_status_t status =
        CypherTileMapDocument_PlaceDoor(
            m_pDocument.get(), coordinate, side, pDoorIdOut );
    if ( status == tile_map_document_status_t::OK ) return true;
    SetError( pErrorOut, documentError( tr( "Place door" ), status ) );
    return false;
}

bool CypherTileDocumentBridge::removeDoorAt(
    tile_map_grid_coord_t coordinate,
    tile_map_marker_side_t side,
    QString *pErrorOut )
{
    const tile_map_document_status_t status =
        CypherTileMapDocument_RemoveDoorAt(
            m_pDocument.get(), coordinate, side );
    if ( status == tile_map_document_status_t::OK ) return true;
    SetError( pErrorOut, documentError( tr( "Remove door" ), status ) );
    return false;
}

bool CypherTileDocumentBridge::moveRegion( tile_map_grid_rect_t rect, int dx, int dy, bool copy, QString *error )
{
    const auto status = copy ? CypherTileMapDocument_CopyRegion( m_pDocument.get(), rect, dx, dy )
                             : CypherTileMapDocument_MoveRegion( m_pDocument.get(), rect, dx, dy );
    if ( status == tile_map_document_status_t::OK ) return true;
    SetError( error, status == tile_map_document_status_t::INVALID_STATE
        ? tr( "Destination is occupied or another edit is active. Choose an empty destination." )
        : documentError( copy ? tr( "Duplicate selection" ) : tr( "Move selection" ), status ) );
    return false;
}

bool CypherTileDocumentBridge::deleteRegion( tile_map_grid_rect_t rect, QString *error )
{
    const auto status = CypherTileMapDocument_DeleteRegion( m_pDocument.get(), rect );
    if ( status == tile_map_document_status_t::OK ) return true;
    SetError( error, documentError( tr( "Delete selection" ), status ) );
    return false;
}

bool CypherTileDocumentBridge::raiseRegion( tile_map_grid_rect_t rect, int delta, QString *error )
{
    const auto status = CypherTileMapDocument_AdjustRegionFloorLevel( m_pDocument.get(), rect, delta );
    if ( status == tile_map_document_status_t::OK ) return true;
    SetError( error, documentError( tr( "Change selection elevation" ), status ) );
    return false;
}

bool CypherTileDocumentBridge::rotateRegion( tile_map_grid_rect_t rect, QString *error )
{
    const auto status = CypherTileMapDocument_RotateRegionClockwise( m_pDocument.get(), rect );
    if ( status == tile_map_document_status_t::OK ) return true;
    SetError( error, documentError( tr( "Rotate selection" ), status ) );
    return false;
}

bool CypherTileDocumentBridge::moveSelection( std::span<const tile_map_grid_coord_t> selection,
    int dx, int dy, bool copy, QString *error )
{
    const span_t<const tile_map_grid_coord_t> cells{ selection.data(), selection.size() };
    const auto status = copy ? CypherTileMapDocument_CopySelection( m_pDocument.get(), cells, dx, dy )
        : CypherTileMapDocument_MoveSelection( m_pDocument.get(), cells, dx, dy );
    if ( status == tile_map_document_status_t::OK ) return true;
    SetError( error, status == tile_map_document_status_t::INVALID_STATE
        ? tr( "Destination is occupied or another edit is active. Choose an empty destination." )
        : documentError( copy ? tr( "Duplicate selection" ) : tr( "Move selection" ), status ) );
    return false;
}

bool CypherTileDocumentBridge::deleteSelection( std::span<const tile_map_grid_coord_t> selection, QString *error )
{
    const auto status = CypherTileMapDocument_DeleteSelection( m_pDocument.get(), { selection.data(), selection.size() } );
    if ( status == tile_map_document_status_t::OK ) return true;
    SetError( error, documentError( tr( "Delete selection" ), status ) );
    return false;
}

bool CypherTileDocumentBridge::raiseSelection( std::span<const tile_map_grid_coord_t> selection,
    int delta, QString *error )
{
    const auto status = CypherTileMapDocument_AdjustSelectionFloorLevel(
        m_pDocument.get(), { selection.data(), selection.size() }, delta );
    if ( status == tile_map_document_status_t::OK ) return true;
    SetError( error, documentError( tr( "Change selection elevation" ), status ) );
    return false;
}

bool CypherTileDocumentBridge::adjustSelectionWallHeight( std::span<const tile_map_grid_coord_t> selection,
    int delta, QString *error )
{
    const auto status = CypherTileMapDocument_AdjustSelectionWallHeight(
        m_pDocument.get(), { selection.data(), selection.size() }, delta );
    if ( status == tile_map_document_status_t::OK ) return true;
    SetError( error, documentError( tr( "Change selection wall height" ), status ) );
    return false;
}

bool CypherTileDocumentBridge::rotateSelection( std::span<const tile_map_grid_coord_t> selection, QString *error )
{
    const auto status = CypherTileMapDocument_RotateSelectionClockwise(
        m_pDocument.get(), { selection.data(), selection.size() } );
    if ( status == tile_map_document_status_t::OK ) return true;
    SetError( error, documentError( tr( "Rotate selection" ), status ) );
    return false;
}

bool CypherTileDocumentBridge::applySelectionProperties( std::span<const tile_map_grid_coord_t> selection,
    const tile_map_selection_patch_t &patch, QString *error )
{
    const auto status = CypherTileMapDocument_ApplySelectionProperties(
        m_pDocument.get(), { selection.data(), selection.size() }, patch );
    if ( status == tile_map_document_status_t::OK ) return true;
    SetError( error, documentError( tr( "Change selection properties" ), status ) );
    return false;
}

bool CypherTileDocumentBridge::canUndo() const
{
    return isInitialized() && CypherTileMapDocument_CanUndo( m_pDocument.get() );
}

bool CypherTileDocumentBridge::canRedo() const
{
    return isInitialized() && CypherTileMapDocument_CanRedo( m_pDocument.get() );
}

bool CypherTileDocumentBridge::undo( QString *pErrorOut )
{
    const tile_map_document_status_t status =
        CypherTileMapDocument_Undo( m_pDocument.get() );
    if ( status == tile_map_document_status_t::OK ) return true;
    SetError( pErrorOut, documentError( tr( "Undo" ), status ) );
    return false;
}

bool CypherTileDocumentBridge::redo( QString *pErrorOut )
{
    const tile_map_document_status_t status =
        CypherTileMapDocument_Redo( m_pDocument.get() );
    if ( status == tile_map_document_status_t::OK ) return true;
    SetError( pErrorOut, documentError( tr( "Redo" ), status ) );
    return false;
}

QString CypherTileDocumentBridge::documentError(
    const QString &operation,
    tile_map_document_status_t status )
{
    return tr( "%1 failed: %2" ).arg(
        operation,
        QString::fromLatin1( CypherTileMapDocument_StatusName( status ) ) );
}

void CypherTileDocumentBridge::replaceDocument(
    std::unique_ptr<tile_map_document_t> pReplacement,
    const QString &filePath )
{
    if ( isInitialized() ) {
        CypherTileMapDocument_Shutdown( m_pDocument.get() );
    }
    m_pDocument = std::move( pReplacement );
    m_filePath = filePath;
}

} // namespace cypher::tools::tile_editor
