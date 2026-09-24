//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Serialization.cpp
//  Purpose: Implements the deterministic authored-geometry binary format.
//  Details: The writer sizes the output exactly before writing, so a fixed
//           ByteWriter over the final buffer can never run short. The
//           reader validates in three passes of increasing cost: header
//           and sizes, checksum, then per-brush structure and geometry.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Serialization.h"

#include "CypherCommon_ByteReader.h"
#include "CypherCommon_ByteWriter.h"
#include "CypherCommon_StableHash.h"

namespace cypher::editor::geometry
{

namespace
{

using common::byte;
using common::u16;
using common::u32;
using common::u64;
using common::usize;
using math::f64;

// Domain separator for the payload checksum ("CYGDSER1").
constexpr common::stable_hash_domain_t cChecksumDomain = 0x3152455344475943ull;

bool Checksum( const byte *pData, usize cbData, u64 *pHashOut ) noexcept
{
    common::stable_hash_builder_t builder{};
    common::hash64_t hash{};
    if ( !common::StableHash_Begin( &builder, cChecksumDomain, GEOMETRY_SERIALIZATION_VERSION ) ||
         !common::StableHash_WriteBytes( &builder, common::binary_block_t{ pData, cbData } ) ||
         !common::StableHash_End( &builder, &hash ) ) {
        return false;
    }
    *pHashOut = static_cast<u64>( hash );
    return true;
}

void WriteVec3( common::byte_writer_t *pWriter, math::vec3d_t v ) noexcept
{
    ( void )common::ByteWriter_WriteF64( pWriter, v.x );
    ( void )common::ByteWriter_WriteF64( pWriter, v.y );
    ( void )common::ByteWriter_WriteF64( pWriter, v.z );
}

bool ReadF64( common::byte_reader_t *pReader, f64 *pOut ) noexcept
{
    return common::ByteReader_ReadF64( pReader, pOut ) && math::Scalar_IsFinite( *pOut );
}

bool ReadVec3( common::byte_reader_t *pReader, math::vec3d_t *pOut ) noexcept
{
    return ReadF64( pReader, &pOut->x ) && ReadF64( pReader, &pOut->y ) &&
           ReadF64( pReader, &pOut->z );
}

geometry_status_t Fail(
    geometry_serialize_report_t *pReport, geometry_status_t status, u64 offset,
    geometry_source_id_t brushId = {} ) noexcept
{
    if ( pReport != nullptr ) {
        pReport->status = status;
        pReport->byteOffset = offset;
        pReport->brushId = brushId;
    }
    return status;
}

struct value_list_t {
    common::vector_t<const geometry_brush_value_t *> values{};
    ~value_list_t() noexcept
    {
        for ( usize i = 0u; i < common::Vector_Count( &values ); ++i ) {
            BrushValue_Release( values.pData[i] );
        }
    }
};

} // namespace

geometry_status_t GeometrySerialize_TryMeasure(
    const geometry_document_snapshot_t *pSnapshot, usize *pBytesOut ) noexcept
{
    if ( pBytesOut == nullptr || pSnapshot == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pBytesOut = 0u;
    usize cBytes = GEOMETRY_SERIALIZATION_HEADER_BYTES + GEOMETRY_SERIALIZATION_FOOTER_BYTES;
    for ( usize i = 0u; i < pSnapshot->cBrushes; ++i ) {
        cBytes += GEOMETRY_SERIALIZATION_BRUSH_BYTES +
                  GEOMETRY_SERIALIZATION_SIDE_BYTES *
                      BrushSolid_SideCount( &pSnapshot->pBrushes[i].pValue->brush );
    }
    *pBytesOut = cBytes;
    return geometry_status_t::OK;
}

geometry_status_t GeometrySerialize_TryWriteSnapshot(
    const geometry_document_snapshot_t *pSnapshot,
    geometry_source_id_t nextSourceId,
    common::vector_t<byte> *pBytesOut ) noexcept
{
    if ( pBytesOut == nullptr || pBytesOut->pAllocator == nullptr || pSnapshot == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    common::Vector_Clear( pBytesOut );
    if ( pSnapshot->cBrushes > static_cast<usize>( common::CY_U32_MAX ) ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    // Every written ID must be below the high-water mark (zero means the
    // identity space is exhausted, which is above every ID).
    for ( usize i = 0u; i < pSnapshot->cBrushes; ++i ) {
        const brush_solid_t &brush = pSnapshot->pBrushes[i].pValue->brush;
        if ( GeometrySourceId_IsValid( nextSourceId ) && brush.sourceId.value >= nextSourceId.value ) {
            return geometry_status_t::INVALID_ARGUMENT;
        }
        for ( usize s = 0u; s < BrushSolid_SideCount( &brush ); ++s ) {
            if ( GeometrySourceId_IsValid( nextSourceId ) &&
                 brush.sides.pData[s].sourceId.value >= nextSourceId.value ) {
                return geometry_status_t::INVALID_ARGUMENT;
            }
        }
    }

    usize cBytes = 0u;
    ( void )GeometrySerialize_TryMeasure( pSnapshot, &cBytes );
    if ( !common::Vector_Resize( pBytesOut, cBytes ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    common::byte_writer_t writer{};
    if ( !common::ByteWriter_Init( &writer, common::byte_span_t{ pBytesOut->pData, cBytes } ) ) {
        common::Vector_Clear( pBytesOut );
        return geometry_status_t::CORRUPT_STATE;
    }

    const u64 payloadBytes = cBytes - GEOMETRY_SERIALIZATION_HEADER_BYTES -
                             GEOMETRY_SERIALIZATION_FOOTER_BYTES;
    ( void )common::ByteWriter_WriteU32( &writer, GEOMETRY_SERIALIZATION_MAGIC );
    ( void )common::ByteWriter_WriteU16( &writer, GEOMETRY_SERIALIZATION_VERSION );
    ( void )common::ByteWriter_WriteU16( &writer, 0u );
    ( void )common::ByteWriter_WriteU64( &writer, nextSourceId.value );
    ( void )common::ByteWriter_WriteU32( &writer, static_cast<u32>( pSnapshot->cBrushes ) );
    ( void )common::ByteWriter_WriteU32( &writer, 0u );
    ( void )common::ByteWriter_WriteU64( &writer, payloadBytes );

    for ( usize i = 0u; i < pSnapshot->cBrushes; ++i ) {
        const geometry_brush_value_t *pValue = pSnapshot->pBrushes[i].pValue;
        const usize cSides = BrushSolid_SideCount( &pValue->brush );
        ( void )common::ByteWriter_WriteU64( &writer, pValue->brush.sourceId.value );
        ( void )common::ByteWriter_WriteU32( &writer, static_cast<u32>( cSides ) );
        ( void )common::ByteWriter_WriteU32( &writer, 0u );
        for ( usize s = 0u; s < cSides; ++s ) {
            const brush_solid_side_t &side = pValue->brush.sides.pData[s];
            const geometry_brush_side_attributes_t &record =
                pValue->attributes.records.pData[side.iAttributeIndex];
            const math::planar_uv_mappingd_t &uv = record.uvProjection;
            ( void )common::ByteWriter_WriteU64( &writer, side.sourceId.value );
            WriteVec3( &writer, side.plane.normal );
            ( void )common::ByteWriter_WriteF64( &writer, side.plane.d );
            ( void )common::ByteWriter_WriteU64( &writer, record.material.value );
            WriteVec3( &writer, uv.origin );
            WriteVec3( &writer, uv.uAxis );
            WriteVec3( &writer, uv.vAxis );
            WriteVec3( &writer, uv.normal );
            ( void )common::ByteWriter_WriteF64( &writer, uv.worldUnitsPerUv.x );
            ( void )common::ByteWriter_WriteF64( &writer, uv.worldUnitsPerUv.y );
            ( void )common::ByteWriter_WriteF64( &writer, uv.rotationRadians );
            ( void )common::ByteWriter_WriteF64( &writer, uv.offset.x );
            ( void )common::ByteWriter_WriteF64( &writer, uv.offset.y );
        }
    }

    u64 checksum = 0u;
    if ( !Checksum( pBytesOut->pData, cBytes - GEOMETRY_SERIALIZATION_FOOTER_BYTES, &checksum ) ) {
        common::Vector_Clear( pBytesOut );
        return geometry_status_t::CORRUPT_STATE;
    }
    ( void )common::ByteWriter_WriteU64( &writer, checksum );
    if ( common::ByteWriter_Status( &writer ) != common::byte_cursor_status_t::OK ||
         common::ByteWriter_Offset( &writer ) != cBytes ) {
        common::Vector_Clear( pBytesOut );
        return geometry_status_t::CORRUPT_STATE;
    }
    return geometry_status_t::OK;
}

geometry_status_t GeometrySerialize_TryWriteDocument(
    geometry_document_t *pDocument, common::vector_t<byte> *pBytesOut ) noexcept
{
    const geometry_document_snapshot_t *pSnapshot = nullptr;
    geometry_status_t status = GeometryDocument_TryAcquireSnapshot( pDocument, &pSnapshot );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = GeometrySerialize_TryWriteSnapshot(
        pSnapshot, GeometryDocument_NextSourceId( pDocument ), pBytesOut );
    GeometrySnapshot_Release( pSnapshot );
    return status;
}

geometry_status_t GeometrySerialize_TryReadDocument(
    common::span_t<const byte> bytes,
    const geometry_document_desc_t &desc,
    geometry_document_t *pDocument,
    geometry_serialize_report_t *pReportOut ) noexcept
{
    if ( pReportOut != nullptr ) {
        *pReportOut = {};
    }
    if ( pDocument == nullptr || pDocument->bInitialized || !common::Span_IsValid( bytes ) ) {
        return Fail( pReportOut, geometry_status_t::INVALID_ARGUMENT, 0u );
    }
    const usize cbTotal = bytes.nCount;
    const geometry_limit_policy_t &limits = desc.policy.limits;

    // ---- Pass 1: header and sizes ----------------------------------------
    if ( cbTotal < GEOMETRY_SERIALIZATION_HEADER_BYTES + GEOMETRY_SERIALIZATION_FOOTER_BYTES ) {
        return Fail( pReportOut, geometry_status_t::CORRUPT_STATE, 0u );
    }
    common::byte_reader_t reader{};
    if ( !common::ByteReader_Init( &reader, common::BinaryBlock_FromSpan( bytes ) ) ) {
        return Fail( pReportOut, geometry_status_t::INVALID_ARGUMENT, 0u );
    }
    u32 magic = 0u;
    u16 version = 0u;
    u16 flags = 0u;
    u64 nextSourceId = 0u;
    u32 cBrushes = 0u;
    u32 reserved = 0u;
    u64 payloadBytes = 0u;
    ( void )common::ByteReader_ReadU32( &reader, &magic );
    ( void )common::ByteReader_ReadU16( &reader, &version );
    ( void )common::ByteReader_ReadU16( &reader, &flags );
    ( void )common::ByteReader_ReadU64( &reader, &nextSourceId );
    ( void )common::ByteReader_ReadU32( &reader, &cBrushes );
    ( void )common::ByteReader_ReadU32( &reader, &reserved );
    ( void )common::ByteReader_ReadU64( &reader, &payloadBytes );
    if ( magic != GEOMETRY_SERIALIZATION_MAGIC ) {
        return Fail( pReportOut, geometry_status_t::CORRUPT_STATE, 0u );
    }
    if ( version != GEOMETRY_SERIALIZATION_VERSION || flags != 0u ) {
        return Fail( pReportOut, geometry_status_t::UNSUPPORTED, 4u );
    }
    if ( reserved != 0u ||
         payloadBytes != cbTotal - GEOMETRY_SERIALIZATION_HEADER_BYTES -
                             GEOMETRY_SERIALIZATION_FOOTER_BYTES ) {
        return Fail( pReportOut, geometry_status_t::CORRUPT_STATE, 20u );
    }
    if ( static_cast<u64>( cBrushes ) > limits.cBrushesMax ||
         static_cast<u64>( cBrushes ) > payloadBytes / GEOMETRY_SERIALIZATION_BRUSH_BYTES ) {
        return Fail( pReportOut, geometry_status_t::LIMIT_EXCEEDED, 16u );
    }

    // ---- Pass 2: checksum ------------------------------------------------
    u64 expected = 0u;
    u64 stored = 0u;
    const usize cbHashed = cbTotal - GEOMETRY_SERIALIZATION_FOOTER_BYTES;
    if ( !Checksum( bytes.pData, cbHashed, &expected ) ) {
        return Fail( pReportOut, geometry_status_t::CORRUPT_STATE, 0u );
    }
    for ( usize i = 0u; i < 8u; ++i ) {
        stored |= static_cast<u64>( bytes.pData[cbHashed + i] ) << ( 8u * i );
    }
    if ( stored != expected ) {
        return Fail( pReportOut, geometry_status_t::CORRUPT_STATE, cbHashed );
    }

    // ---- Pass 3: brushes ---------------------------------------------------
    // The persisted high-water mark seeds the identity domain, so IDs
    // retired before saving are never handed out again after loading.
    geometry_document_desc_t loadDesc = desc;
    loadDesc.firstSourceId = geometry_source_id_t{ nextSourceId };
    geometry_status_t status = GeometryDocument_Init( pDocument, loadDesc );
    if ( status != geometry_status_t::OK ) {
        return Fail( pReportOut, status, 0u );
    }

    const common::allocator_t *pAllocator = pDocument->pAllocator;
    value_list_t values{};
    common::vector_t<geometry_source_id_t> ids{};
    if ( !common::Vector_Init( &values.values, pAllocator, cBrushes ) ||
         !common::Vector_Init( &ids, pAllocator, 0u ) ) {
        GeometryDocument_Shutdown( pDocument );
        return Fail( pReportOut, geometry_status_t::ALLOCATION_FAILED, 0u );
    }

    u64 cTotalSides = 0u;
    u64 previousBrushId = 0u;
    brush_solid_t brush{};
    geometry_brush_side_attribute_store_t attributes{};
    for ( u32 b = 0u; status == geometry_status_t::OK && b < cBrushes; ++b ) {
        const u64 brushOffset = common::ByteReader_Offset( &reader );
        u64 brushId = 0u;
        u32 cSides = 0u;
        u32 brushReserved = 0u;
        if ( !common::ByteReader_ReadU64( &reader, &brushId ) ||
             !common::ByteReader_ReadU32( &reader, &cSides ) ||
             !common::ByteReader_ReadU32( &reader, &brushReserved ) || brushReserved != 0u ||
             brushId <= previousBrushId ) {
            status = Fail( pReportOut, geometry_status_t::CORRUPT_STATE, brushOffset,
                           geometry_source_id_t{ brushId } );
            break;
        }
        previousBrushId = brushId;
        cTotalSides += cSides;
        if ( cSides < 4u || static_cast<u64>( cSides ) > limits.cBrushSidesPerBrushMax ||
             cTotalSides > limits.cBrushSidesMax ||
             static_cast<u64>( cSides ) * GEOMETRY_SERIALIZATION_SIDE_BYTES >
                 common::ByteReader_Remaining( &reader ) ) {
            status = Fail( pReportOut, geometry_status_t::LIMIT_EXCEEDED, brushOffset,
                           geometry_source_id_t{ brushId } );
            break;
        }

        status = BrushSolid_Init( &brush, pAllocator, geometry_source_id_t{ brushId } );
        if ( status == geometry_status_t::OK ) {
            status = BrushSideAttributeStore_Init( &attributes, pAllocator );
        }
        if ( status == geometry_status_t::OK &&
             !common::Vector_PushBack( &ids, geometry_source_id_t{ brushId } ) ) {
            status = geometry_status_t::ALLOCATION_FAILED;
        }
        for ( u32 s = 0u; status == geometry_status_t::OK && s < cSides; ++s ) {
            const u64 sideOffset = common::ByteReader_Offset( &reader );
            u64 sideId = 0u;
            brush_solid_side_t side{};
            geometry_brush_side_attributes_t record{};
            math::planar_uv_mappingd_t &uv = record.uvProjection;
            const bool bRead =
                common::ByteReader_ReadU64( &reader, &sideId ) &&
                ReadVec3( &reader, &side.plane.normal ) && ReadF64( &reader, &side.plane.d ) &&
                common::ByteReader_ReadU64( &reader, &record.material.value ) &&
                ReadVec3( &reader, &uv.origin ) && ReadVec3( &reader, &uv.uAxis ) &&
                ReadVec3( &reader, &uv.vAxis ) && ReadVec3( &reader, &uv.normal ) &&
                ReadF64( &reader, &uv.worldUnitsPerUv.x ) &&
                ReadF64( &reader, &uv.worldUnitsPerUv.y ) &&
                ReadF64( &reader, &uv.rotationRadians ) && ReadF64( &reader, &uv.offset.x ) &&
                ReadF64( &reader, &uv.offset.y );
            if ( !bRead || sideId == 0u ) {
                status = Fail( pReportOut, bRead ? geometry_status_t::CORRUPT_STATE
                                                 : geometry_status_t::NUMERIC_FAILURE,
                               sideOffset, geometry_source_id_t{ brushId } );
                break;
            }
            usize iRecord = 0u;
            side.sourceId = geometry_source_id_t{ sideId };
            status = BrushSideAttributeStore_TryAppend( &attributes, desc.policy, record, &iRecord );
            if ( status == geometry_status_t::OK ) {
                side.iAttributeIndex = static_cast<u32>( iRecord );
                status = BrushSolid_TryAddSide( &brush, limits, side, nullptr );
            }
            if ( status == geometry_status_t::OK && !common::Vector_PushBack( &ids, side.sourceId ) ) {
                status = geometry_status_t::ALLOCATION_FAILED;
            }
            if ( status != geometry_status_t::OK ) {
                status = Fail( pReportOut, status, sideOffset, geometry_source_id_t{ brushId } );
            }
        }
        const geometry_brush_value_t *pValue = nullptr;
        if ( status == geometry_status_t::OK ) {
            status = GeometryDocument_TryCreateBrushValue( pDocument, &brush, &attributes, &pValue,
                                                           nullptr );
            if ( status != geometry_status_t::OK ) {
                status = Fail( pReportOut, status, brushOffset, geometry_source_id_t{ brushId } );
            }
        }
        if ( status == geometry_status_t::OK ) {
            ( void )common::Vector_PushBack( &values.values, pValue );
        }
        BrushSideAttributeStore_Shutdown( &attributes );
        BrushSolid_Shutdown( &brush );
    }

    if ( status == geometry_status_t::OK && common::ByteReader_Offset( &reader ) != cbHashed ) {
        status = Fail( pReportOut, geometry_status_t::CORRUPT_STATE,
                       common::ByteReader_Offset( &reader ) );
    }
    // Every identity must lie below the persisted high-water mark.
    for ( usize i = 0u; status == geometry_status_t::OK && i < common::Vector_Count( &ids ); ++i ) {
        if ( nextSourceId != 0u && ids.pData[i].value >= nextSourceId ) {
            status = Fail( pReportOut, geometry_status_t::CORRUPT_STATE, 8u, ids.pData[i] );
        }
    }
    if ( status == geometry_status_t::OK ) {
        status = GeometryDocument_TryRegisterLoadedIds(
            pDocument, { ids.pData, common::Vector_Count( &ids ) } );
        if ( status != geometry_status_t::OK ) {
            status = Fail( pReportOut, status, 0u );
        }
    }
    if ( status == geometry_status_t::OK && cBrushes > 0u ) {
        common::vector_t<geometry_document_change_t> changes{};
        if ( !common::Vector_Init( &changes, pAllocator, cBrushes ) ) {
            status = geometry_status_t::ALLOCATION_FAILED;
        }
        for ( u32 b = 0u; status == geometry_status_t::OK && b < cBrushes; ++b ) {
            const geometry_brush_value_t *pValue = values.values.pData[b];
            ( void )common::Vector_PushBack(
                &changes, geometry_document_change_t{ geometry_document_change_kind_t::INSERT_BRUSH,
                                                      pValue->brush.sourceId, pValue } );
        }
        if ( status == geometry_status_t::OK ) {
            status = GeometryDocument_TryApply( pDocument, GeometryDocument_Revision( pDocument ),
                                                { changes.pData, cBrushes }, nullptr );
        }
        if ( status != geometry_status_t::OK ) {
            status = Fail( pReportOut, status, 0u );
        }
    }
    if ( status == geometry_status_t::OK ) {
        ( void )GeometryDocument_SealLoadedIds( pDocument );
        return geometry_status_t::OK;
    }
    GeometryDocument_Shutdown( pDocument );
    return status;
}

} // namespace cypher::editor::geometry
