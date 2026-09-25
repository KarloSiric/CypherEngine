//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CompilerInterchange.cpp
//  Purpose: Implements the compiler interchange writer and reader.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CompilerInterchange.h"
#include "CypherGeometry_CookBytes.h"

#include "CypherCommon/Tier1/CypherCommon_KeyValue.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValueParser.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValueWriter.h"

#include <cmath>
#include <cstring>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

constexpr usize kTriangleBytes = 3u * 4u + 4u + 8u + 8u;

string_view_t SV( const char *p ) noexcept { return StringView_FromCString( p ); }

const char *KindName( cook_source_kind_t k ) noexcept
{
    switch ( k ) {
    case cook_source_kind_t::BRUSH: return "brush";
    case cook_source_kind_t::MESH: return "mesh";
    case cook_source_kind_t::PATCH: return "patch";
    case cook_source_kind_t::HEIGHTFIELD: return "heightfield";
    default: return "invalid";
    }
}

cook_source_kind_t KindOf( string_view_t s ) noexcept
{
    for ( const cook_source_kind_t k : { cook_source_kind_t::BRUSH, cook_source_kind_t::MESH, cook_source_kind_t::PATCH, cook_source_kind_t::HEIGHTFIELD } ) {
        if ( StringView_Equals( s, SV( KindName( k ) ) ) ) { return k; }
    }
    return cook_source_kind_t::INVALID;
}

void PutU32( byte *p, u32 v ) noexcept
{
    for ( u32 i = 0u; i < 4u; ++i ) { p[i] = static_cast<byte>( v >> ( 8u * i ) ); }
}
void PutU64( byte *p, u64 v ) noexcept
{
    for ( u32 i = 0u; i < 8u; ++i ) { p[i] = static_cast<byte>( v >> ( 8u * i ) ); }
}
u32 GetU32( const byte *p ) noexcept
{
    u32 v = 0u;
    for ( u32 i = 0u; i < 4u; ++i ) { v |= static_cast<u32>( p[i] ) << ( 8u * i ); }
    return v;
}
u64 GetU64( const byte *p ) noexcept
{
    u64 v = 0u;
    for ( u32 i = 0u; i < 8u; ++i ) { v |= static_cast<u64>( p[i] ) << ( 8u * i ); }
    return v;
}
u64 F64Bits( f64 v ) noexcept
{
    u64 b = 0u;
    std::memcpy( &b, &v, sizeof b );
    return b;
}
f64 BitsF64( u64 b ) noexcept
{
    f64 v = 0.0;
    std::memcpy( &v, &b, sizeof v );
    return v;
}

// The content hash covers revision, policy, sources, objects and both
// blobs, in the order they are written.
content_hash_t Hash( geometry_revision_t revision, content_hash_t policy, const vector_t<cook_source_key_t> &sources, const cook_surface_soup_t &soup,
                     const allocator_t *pA, bool *pOk ) noexcept
{
    cook_byte_writer_t w{};
    if ( !CookBytes_Init( &w, pA ) ) {
        *pOk = false;
        return CY_CONTENT_HASH_INVALID;
    }
    CookBytes_U32( &w, GEOMETRY_INTERCHANGE_VERSION );
    CookBytes_U64( &w, revision );
    CookBytes_U64( &w, policy.low );
    CookBytes_U64( &w, policy.high );
    for ( usize i = 0u; i < sources.nCount; ++i ) {
        CookBytes_U64( &w, sources.pData[i].sourceId.value );
        CookBytes_U32( &w, static_cast<u32>( sources.pData[i].kind ) );
        CookBytes_U64( &w, sources.pData[i].sourceHash.low );
        CookBytes_U64( &w, sources.pData[i].sourceHash.high );
    }
    for ( usize i = 0u; i < soup.objects.nCount; ++i ) {
        const cook_soup_object_t &o = soup.objects.pData[i];
        CookBytes_U64( &w, o.objectId.value );
        CookBytes_U32( &w, static_cast<u32>( o.kind ) );
        CookBytes_U32( &w, o.iFirstVertex );
        CookBytes_U32( &w, o.cVertices );
        CookBytes_U32( &w, o.iFirstTriangle );
        CookBytes_U32( &w, o.cTriangles );
        CookBytes_U32( &w, ( o.bClosed ? 1u : 0u ) | ( o.bConvex ? 2u : 0u ) );
    }
    for ( usize i = 0u; i < soup.positions.nCount; ++i ) {
        CookBytes_F64( &w, soup.positions.pData[i].x );
        CookBytes_F64( &w, soup.positions.pData[i].y );
        CookBytes_F64( &w, soup.positions.pData[i].z );
    }
    for ( usize i = 0u; i < soup.triangles.nCount; ++i ) {
        const cook_soup_triangle_t &t = soup.triangles.pData[i];
        CookBytes_U32( &w, t.v[0] );
        CookBytes_U32( &w, t.v[1] );
        CookBytes_U32( &w, t.v[2] );
        CookBytes_U32( &w, t.iObject );
        CookBytes_U64( &w, t.elementId.value );
        CookBytes_U64( &w, t.material.value );
    }
    const content_hash_t h = CookBytes_Hash( &w );
    CookBytes_Shutdown( &w );
    return h;
}

struct doc_owner_t {
    key_value_document_t *p{ nullptr };
    ~doc_owner_t()
    {
        if ( p != nullptr ) { KeyValue_DestroyDocument( p ); }
    }
};

} // namespace

geometry_status_t GeometryInterchange_Init( geometry_interchange_t *p, const allocator_t *pA ) noexcept
{
    if ( p == nullptr || !Allocator_IsValid( pA ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !Vector_Init( &p->sources, pA ) || CookSurfaces_Init( &p->soup, pA ) != geometry_status_t::OK ) {
        GeometryInterchange_Shutdown( p );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

void GeometryInterchange_Shutdown( geometry_interchange_t *p ) noexcept
{
    if ( p == nullptr ) { return; }
    Vector_Shutdown( &p->sources );
    CookSurfaces_Shutdown( &p->soup );
}

geometry_status_t GeometryInterchange_TryWrite( const cook_key_set_t *pKeys, const cook_surface_soup_t *pSoup, text_buffer_t *pTextOut ) noexcept
{
    if ( pKeys == nullptr || pSoup == nullptr || pTextOut == nullptr || pTextOut->pAllocator == nullptr || pKeys->revision != pSoup->revision ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const allocator_t *pA = pTextOut->pAllocator;
    bool bOk = true;
    const content_hash_t hash = Hash( pSoup->revision, pKeys->policyHash, pKeys->keys, *pSoup, pA, &bOk );
    if ( !bOk ) { return geometry_status_t::ALLOCATION_FAILED; }
    key_value_document_desc_t desc{};
    desc.pAllocator = pA;
    doc_owner_t owner{ KeyValue_CreateDocument( desc ) };
    key_value_document_t *d = owner.p;
    if ( d == nullptr || !KeyValue_SetDocumentHeader( d, { CYKV_LANGUAGE_VERSION, SV( GEOMETRY_INTERCHANGE_SCHEMA_ID ), GEOMETRY_INTERCHANGE_VERSION } ) ||
         !KeyValue_SetRootType( d, key_value_type_t::OBJECT ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    key_value_t *root = KeyValue_Root( d );
    auto setU64 = [&]( key_value_t *obj, const char *name, u64 v ) noexcept {
        key_value_t *n = bOk ? KeyValue_ObjectInsert( d, obj, SV( name ), key_value_type_t::U64 ) : nullptr;
        bOk = n != nullptr && KeyValue_SetU64( d, n, v );
    };
    auto setString = [&]( key_value_t *obj, const char *name, string_view_t v ) noexcept {
        key_value_t *n = bOk ? KeyValue_ObjectInsert( d, obj, SV( name ), key_value_type_t::STRING ) : nullptr;
        bOk = n != nullptr && KeyValue_SetString( d, n, v );
    };
    auto setHash = [&]( key_value_t *obj, const char *name, content_hash_t h ) noexcept {
        char hex[CY_CONTENT_HASH_HEX_LENGTH + 1u]{};
        (void)ContentHash_ToHex( h, hex, sizeof hex );
        setString( obj, name, string_view_t{ hex, CY_CONTENT_HASH_HEX_LENGTH } );
    };
    auto setBool = [&]( key_value_t *obj, const char *name, bool v ) noexcept {
        key_value_t *n = bOk ? KeyValue_ObjectInsert( d, obj, SV( name ), key_value_type_t::BOOL ) : nullptr;
        bOk = n != nullptr && KeyValue_SetBool( d, n, v );
    };
    setU64( root, "revision", pSoup->revision );
    setHash( root, "policy_hash", pKeys->policyHash );
    key_value_t *sources = bOk ? KeyValue_ObjectInsert( d, root, SV( "sources" ), key_value_type_t::ARRAY ) : nullptr;
    bOk = bOk && sources != nullptr;
    for ( usize i = 0u; bOk && i < pKeys->keys.nCount; ++i ) {
        key_value_t *s = KeyValue_ArrayAppend( d, sources, key_value_type_t::OBJECT );
        bOk = s != nullptr;
        setU64( s, "id", pKeys->keys.pData[i].sourceId.value );
        setString( s, "kind", SV( KindName( pKeys->keys.pData[i].kind ) ) );
        setHash( s, "source_hash", pKeys->keys.pData[i].sourceHash );
    }
    key_value_t *objects = bOk ? KeyValue_ObjectInsert( d, root, SV( "objects" ), key_value_type_t::ARRAY ) : nullptr;
    bOk = bOk && objects != nullptr;
    for ( usize i = 0u; bOk && i < pSoup->objects.nCount; ++i ) {
        const cook_soup_object_t &o = pSoup->objects.pData[i];
        key_value_t *s = KeyValue_ArrayAppend( d, objects, key_value_type_t::OBJECT );
        bOk = s != nullptr;
        setU64( s, "id", o.objectId.value );
        setString( s, "kind", SV( KindName( o.kind ) ) );
        setU64( s, "first_vertex", o.iFirstVertex );
        setU64( s, "vertex_count", o.cVertices );
        setU64( s, "first_triangle", o.iFirstTriangle );
        setU64( s, "triangle_count", o.cTriangles );
        setBool( s, "closed", o.bClosed );
        setBool( s, "convex", o.bConvex );
    }
    // Bulk arrays as little-endian blobs.
    vector_t<byte> blob{};
    bOk = bOk && Vector_Init( &blob, pA ) && Vector_Resize( &blob, 24u * pSoup->positions.nCount );
    for ( usize i = 0u; bOk && i < pSoup->positions.nCount; ++i ) {
        PutU64( blob.pData + 24u * i, F64Bits( pSoup->positions.pData[i].x ) );
        PutU64( blob.pData + 24u * i + 8u, F64Bits( pSoup->positions.pData[i].y ) );
        PutU64( blob.pData + 24u * i + 16u, F64Bits( pSoup->positions.pData[i].z ) );
    }
    key_value_t *nPos = bOk ? KeyValue_ObjectInsert( d, root, SV( "positions" ), key_value_type_t::BINARY ) : nullptr;
    bOk = nPos != nullptr && KeyValue_SetBinary( d, nPos, binary_block_t{ blob.pData, blob.nCount } );
    bOk = bOk && Vector_Resize( &blob, kTriangleBytes * pSoup->triangles.nCount );
    for ( usize i = 0u; bOk && i < pSoup->triangles.nCount; ++i ) {
        const cook_soup_triangle_t &t = pSoup->triangles.pData[i];
        byte *p = blob.pData + kTriangleBytes * i;
        PutU32( p, t.v[0] );
        PutU32( p + 4u, t.v[1] );
        PutU32( p + 8u, t.v[2] );
        PutU32( p + 12u, t.iObject );
        PutU64( p + 16u, t.elementId.value );
        PutU64( p + 24u, t.material.value );
    }
    key_value_t *nTri = bOk ? KeyValue_ObjectInsert( d, root, SV( "triangles" ), key_value_type_t::BINARY ) : nullptr;
    bOk = nTri != nullptr && KeyValue_SetBinary( d, nTri, binary_block_t{ blob.pData, blob.nCount } );
    setHash( root, "content_hash", hash );
    if ( !bOk ) { return geometry_status_t::ALLOCATION_FAILED; }

    key_value_write_options_t wo{};
    wo.flags = KEY_VALUE_WRITE_FLAG_CANONICAL | KEY_VALUE_WRITE_FLAG_PRETTY | KEY_VALUE_WRITE_FLAG_FINAL_NEWLINE;
    wo.nIndentSpaces = 2u;
    wo.nMaxDepth = 8u;
    const key_value_write_result_t measured = KeyValue_WriteText( root, wo, nullptr, 0u );
    if ( measured.status != key_value_write_status_t::OUTPUT_TRUNCATED ) {
        return measured.status == key_value_write_status_t::OUT_OF_MEMORY ? geometry_status_t::ALLOCATION_FAILED : geometry_status_t::CORRUPT_STATE;
    }
    if ( measured.cchRequired > kGeometryInterchangeTextMax ) { return geometry_status_t::LIMIT_EXCEEDED; }
    text_buffer_t pending{};
    if ( !TextBuffer_Init( &pending, pA, measured.cchRequired ) || !TextBuffer_Resize( &pending, measured.cchRequired ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    const key_value_write_result_t written = KeyValue_WriteText( root, wo, TextBuffer_Data( &pending ), TextBuffer_Capacity( &pending ) + 1u );
    geometry_status_t st = geometry_status_t::OK;
    if ( written.status != key_value_write_status_t::OK || written.cchWritten != measured.cchRequired ) {
        st = written.status == key_value_write_status_t::OUT_OF_MEMORY ? geometry_status_t::ALLOCATION_FAILED : geometry_status_t::CORRUPT_STATE;
    } else if ( !TextBuffer_Assign( pTextOut, TextBuffer_View( &pending ) ) ) {
        st = geometry_status_t::ALLOCATION_FAILED;
    }
    TextBuffer_Shutdown( &pending );
    return st;
}

geometry_status_t GeometryInterchange_TryRead( string_view_t text, geometry_interchange_t *pOut ) noexcept
{
    if ( pOut == nullptr || pOut->sources.pAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    const allocator_t *pA = pOut->sources.pAllocator;
    Vector_Clear( &pOut->sources );
    Vector_Clear( &pOut->soup.positions );
    Vector_Clear( &pOut->soup.triangles );
    Vector_Clear( &pOut->soup.objects );
    Vector_Clear( &pOut->soup.problems );
    key_value_document_desc_t desc{};
    desc.pAllocator = pA;
    doc_owner_t owner{ KeyValue_CreateDocument( desc ) };
    if ( owner.p == nullptr ) { return geometry_status_t::ALLOCATION_FAILED; }
    key_value_parse_options_t po{};
    po.cbMaxInput = kGeometryInterchangeTextMax;
    po.nMaxDepth = 8u;
    const key_value_parse_result_t parsed = KeyValue_ParseText( text, po, owner.p );
    if ( parsed.status != key_value_parse_status_t::OK ) { return geometry_status_t::INVALID_ARGUMENT; }
    const key_value_document_header_t header = KeyValue_DocumentHeader( owner.p );
    if ( !StringView_Equals( header.schemaId, SV( GEOMETRY_INTERCHANGE_SCHEMA_ID ) ) || header.nSchemaVersion != GEOMETRY_INTERCHANGE_VERSION ) {
        return geometry_status_t::UNSUPPORTED;
    }
    const key_value_t *root = KeyValue_Root( owner.p );
    geometry_status_t st = geometry_status_t::OK;
    auto fail = [&]() noexcept {
        Vector_Clear( &pOut->sources );
        Vector_Clear( &pOut->soup.positions );
        Vector_Clear( &pOut->soup.triangles );
        Vector_Clear( &pOut->soup.objects );
        return st == geometry_status_t::OK ? geometry_status_t::CORRUPT_STATE : st;
    };
    auto getU64 = [&]( const key_value_t *obj, const char *name, u64 *pV ) noexcept {
        const key_value_t *n = KeyValue_Find( obj, SV( name ) );
        return n != nullptr && KeyValue_GetU64( n, pV );
    };
    auto getHash = [&]( const key_value_t *obj, const char *name, content_hash_t *pH ) noexcept {
        string_view_t s{};
        const key_value_t *n = KeyValue_Find( obj, SV( name ) );
        return n != nullptr && KeyValue_GetString( n, &s ) && ContentHash_FromHex( s, pH );
    };
    auto getBool = [&]( const key_value_t *obj, const char *name, bool *pB ) noexcept {
        bool_t b = CY_FALSE;
        const key_value_t *n = KeyValue_Find( obj, SV( name ) );
        const bool ok = n != nullptr && KeyValue_GetBool( n, &b );
        *pB = b;
        return ok;
    };
    auto getKind = [&]( const key_value_t *obj, cook_source_kind_t *pK ) noexcept {
        string_view_t s{};
        const key_value_t *n = KeyValue_Find( obj, SV( "kind" ) );
        if ( n == nullptr || !KeyValue_GetString( n, &s ) ) { return false; }
        *pK = KindOf( s );
        return *pK != cook_source_kind_t::INVALID;
    };
    u64 revision = 0u;
    content_hash_t stored{};
    if ( !getU64( root, "revision", &revision ) || !getHash( root, "policy_hash", &pOut->policyHash ) || !getHash( root, "content_hash", &stored ) ) {
        return fail();
    }
    pOut->revision = revision;
    const key_value_t *sources = KeyValue_Find( root, SV( "sources" ) );
    const key_value_t *objects = KeyValue_Find( root, SV( "objects" ) );
    const key_value_t *nPos = KeyValue_Find( root, SV( "positions" ) );
    const key_value_t *nTri = KeyValue_Find( root, SV( "triangles" ) );
    binary_block_t pos{}, tri{};
    if ( sources == nullptr || objects == nullptr || nPos == nullptr || nTri == nullptr || !KeyValue_GetBinary( nPos, &pos ) ||
         !KeyValue_GetBinary( nTri, &tri ) || pos.cbSize % 24u != 0u || tri.cbSize % kTriangleBytes != 0u ) {
        return fail();
    }
    for ( usize i = 0u; i < KeyValue_ChildCount( sources ); ++i ) {
        const key_value_t *s = KeyValue_ChildAt( sources, i );
        cook_source_key_t k{};
        u64 id = 0u;
        if ( !getU64( s, "id", &id ) || !getKind( s, &k.kind ) || !getHash( s, "source_hash", &k.sourceHash ) ) { return fail(); }
        k.sourceId.value = id;
        if ( !Vector_PushBack( &pOut->sources, k ) ) {
            st = geometry_status_t::ALLOCATION_FAILED;
            return fail();
        }
    }
    const usize cV = pos.cbSize / 24u, cT = tri.cbSize / kTriangleBytes;
    if ( !Vector_Resize( &pOut->soup.positions, cV ) || !Vector_Resize( &pOut->soup.triangles, cT ) ) {
        st = geometry_status_t::ALLOCATION_FAILED;
        return fail();
    }
    for ( usize i = 0u; i < cV; ++i ) {
        pOut->soup.positions.pData[i] = math::Vec3d_Make( BitsF64( GetU64( pos.pData + 24u * i ) ), BitsF64( GetU64( pos.pData + 24u * i + 8u ) ),
                                                          BitsF64( GetU64( pos.pData + 24u * i + 16u ) ) );
    }
    for ( usize i = 0u; i < KeyValue_ChildCount( objects ); ++i ) {
        const key_value_t *s = KeyValue_ChildAt( objects, i );
        cook_soup_object_t o{};
        u64 id = 0u, fv = 0u, cv = 0u, ft = 0u, ct = 0u;
        if ( !getU64( s, "id", &id ) || !getKind( s, &o.kind ) || !getU64( s, "first_vertex", &fv ) || !getU64( s, "vertex_count", &cv ) ||
             !getU64( s, "first_triangle", &ft ) || !getU64( s, "triangle_count", &ct ) || !getBool( s, "closed", &o.bClosed ) ||
             !getBool( s, "convex", &o.bConvex ) || fv + cv > cV || ft + ct > cT ) {
            return fail();
        }
        o.objectId.value = id;
        o.iFirstVertex = static_cast<u32>( fv );
        o.cVertices = static_cast<u32>( cv );
        o.iFirstTriangle = static_cast<u32>( ft );
        o.cTriangles = static_cast<u32>( ct );
        if ( !Vector_PushBack( &pOut->soup.objects, o ) ) {
            st = geometry_status_t::ALLOCATION_FAILED;
            return fail();
        }
    }
    for ( usize i = 0u; i < cT; ++i ) {
        const byte *p = tri.pData + kTriangleBytes * i;
        cook_soup_triangle_t &t = pOut->soup.triangles.pData[i];
        t = cook_soup_triangle_t{};
        t.v[0] = GetU32( p );
        t.v[1] = GetU32( p + 4u );
        t.v[2] = GetU32( p + 8u );
        t.iObject = GetU32( p + 12u );
        t.elementId.value = GetU64( p + 16u );
        t.material.value = GetU64( p + 24u );
        if ( t.v[0] >= cV || t.v[1] >= cV || t.v[2] >= cV || t.iObject >= pOut->soup.objects.nCount ) { return fail(); }
        // Normals and areas are derived, not stored.
        const math::vec3d_t p0 = pOut->soup.positions.pData[t.v[0]], p1 = pOut->soup.positions.pData[t.v[1]], p2 = pOut->soup.positions.pData[t.v[2]];
        const math::vec3d_t n = math::Vec3d_Cross( math::Vec3d_Subtract( p1, p0 ), math::Vec3d_Subtract( p2, p0 ) );
        const f64 len = std::sqrt( math::Vec3d_LengthSquared( n ) );
        t.area = 0.5 * len;
        t.normal = len > 0.0 ? math::Vec3d_Scale( n, 1.0 / len ) : math::vec3d_t{};
    }
    bool bOk = true;
    const content_hash_t check = Hash( pOut->revision, pOut->policyHash, pOut->sources, pOut->soup, pA, &bOk );
    if ( !bOk ) {
        st = geometry_status_t::ALLOCATION_FAILED;
        return fail();
    }
    if ( !ContentHash_Equals( check, stored ) ) { return fail(); }
    pOut->contentHash = stored;
    pOut->soup.revision = pOut->revision;
    for ( usize v = 0u; v < cV; ++v ) {
        if ( v == 0u ) { pOut->soup.lo = pOut->soup.hi = pOut->soup.positions.pData[0]; }
        pOut->soup.lo = math::Vec3d_Min( pOut->soup.lo, pOut->soup.positions.pData[v] );
        pOut->soup.hi = math::Vec3d_Max( pOut->soup.hi, pOut->soup.positions.pData[v] );
    }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
