//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CookBytes.h
//  Purpose: Declares the little-endian byte writer used to build hashable
//           canonical encodings for cook keys and cook product hashes.
//  Details: Hashing structs directly would bake in padding bytes, host
//           endianness, and compiler layout, so two machines could compute
//           different keys for the same geometry. Every hashed value goes
//           through these explicit encoders instead: integers byte by byte
//           in little-endian order, floats by their IEEE bit pattern.
//           Floats keep their exact bits, including the sign of zero,
//           because a -0.0 vs +0.0 difference is a real difference in cooked
//           output.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_COOK_BYTES_H
#define CYPHER_EDITOR_GEOMETRY_COOK_BYTES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Vector.h"
#include "CypherCommon/Tier1/CypherCommon_ContentHash.h"

#include <cstring>

namespace cypher::editor::geometry
{

struct cook_byte_writer_t {
    common::vector_t<common::byte> bytes{};
    bool bOk{ true };
};

inline bool CookBytes_Init( cook_byte_writer_t *pWriter, const common::allocator_t *pAllocator ) noexcept
{
    pWriter->bOk = common::Vector_Init( &pWriter->bytes, pAllocator );
    return pWriter->bOk;
}

inline void CookBytes_Shutdown( cook_byte_writer_t *pWriter ) noexcept
{
    common::Vector_Shutdown( &pWriter->bytes );
}

inline void CookBytes_Clear( cook_byte_writer_t *pWriter ) noexcept
{
    common::Vector_Clear( &pWriter->bytes );
    pWriter->bOk = true;
}

inline void CookBytes_U64( cook_byte_writer_t *pWriter, common::u64 value ) noexcept
{
    for ( int i = 0; i < 8 && pWriter->bOk; ++i ) {
        pWriter->bOk = common::Vector_PushBack( &pWriter->bytes, static_cast<common::byte>( ( value >> ( 8 * i ) ) & 0xFFu ) );
    }
}

inline void CookBytes_U32( cook_byte_writer_t *pWriter, common::u32 value ) noexcept
{
    for ( int i = 0; i < 4 && pWriter->bOk; ++i ) {
        pWriter->bOk = common::Vector_PushBack( &pWriter->bytes, static_cast<common::byte>( ( value >> ( 8 * i ) ) & 0xFFu ) );
    }
}

inline void CookBytes_F64( cook_byte_writer_t *pWriter, common::f64 value ) noexcept
{
    common::u64 bits = 0u;
    std::memcpy( &bits, &value, sizeof( bits ) );
    CookBytes_U64( pWriter, bits );
}

inline void CookBytes_F32( cook_byte_writer_t *pWriter, common::f32 value ) noexcept
{
    common::u32 bits = 0u;
    std::memcpy( &bits, &value, sizeof( bits ) );
    CookBytes_U32( pWriter, bits );
}

// Hash of the bytes written so far (invalid sentinel if a write failed).
inline common::content_hash_t CookBytes_Hash( const cook_byte_writer_t *pWriter ) noexcept
{
    if ( !pWriter->bOk ) { return common::CY_CONTENT_HASH_INVALID; }
    return common::ContentHash_Data( common::binary_block_t{ pWriter->bytes.pData, pWriter->bytes.nCount } );
}

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_COOK_BYTES_H
