//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushTransform.cpp
//  Purpose: Implements affine transform operations on brush side planes.
//  Details: All transforms follow a two-pass pattern: first compute all
//           transformed planes into a scratch buffer, validating each;
//           then write them back to the brush atomically. This ensures
//           the brush is either fully transformed or unchanged on failure.
//
//           Translation is a special case: plane normals are invariant
//           under translation, so only the distance term changes.
//           d' = d - dot(normal, offset). No renormalization needed.
//
//           Rotation and general affine use Planed_TryTransform, which
//           applies the inverse-transpose to the normal, renormalizes,
//           and recomputes the distance.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushTransform.h"

namespace cypher::editor::geometry
{

namespace
{

// Determinant tolerance — transforms with a smaller absolute
// determinant are treated as degenerate (collapsed to a plane or
// line) and rejected.
inline constexpr common::f64 kMinAbsDeterminant = 1.0e-12;

// Minimum normal length after transform. If the inverse-transpose
// produces a normal shorter than this, the plane is degenerate.
inline constexpr common::f64 kMinNormalLength = 1.0e-12;

geometry_status_t ValidateTextureLockBindings(
    const brush_solid_t *pBrush,
    const geometry_brush_side_attribute_store_t *pStore,
    const geometry_policy_t &policy ) noexcept
{
    if ( pBrush == nullptr || pStore == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pBrush->sides.pAllocator == nullptr ||
         pStore->records.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !common::Vector_IsValid( &pBrush->sides ) ||
         !common::Vector_IsValid( &pStore->records ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    const common::usize cSides = BrushSolid_SideCount( pBrush );
    if ( static_cast<common::u64>( cSides ) >
         policy.limits.cBrushSidesPerBrushMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    const geometry_status_t storeStatus =
        BrushSideAttributeStore_Validate( pStore, policy );
    if ( storeStatus != geometry_status_t::OK ) {
        return storeStatus;
    }

    const common::usize cAttributes =
        BrushSideAttributeStore_Count( pStore );
    for ( common::usize iSide = 0u; iSide < cSides; ++iSide ) {
        if ( static_cast<common::usize>(
                 pBrush->sides.pData[iSide].iAttributeIndex ) >=
             cAttributes ) {
            return geometry_status_t::CORRUPT_STATE;
        }
    }
    return geometry_status_t::OK;
}

bool IsFirstAttributeReference(
    const brush_solid_t *pBrush,
    common::usize iSide ) noexcept
{
    const common::u32 iAttribute =
        pBrush->sides.pData[iSide].iAttributeIndex;
    for ( common::usize iPrevious = 0u;
          iPrevious < iSide;
          ++iPrevious ) {
        if ( pBrush->sides.pData[iPrevious].iAttributeIndex == iAttribute ) {
            return false;
        }
    }
    return true;
}

geometry_brush_side_attributes_t TextureLockTranslated(
    geometry_brush_side_attributes_t attributes,
    math::vec3d_t offset ) noexcept
{
    attributes.uvProjection.origin = math::Vec3d_Add(
        attributes.uvProjection.origin, offset );
    return attributes;
}

geometry_brush_side_attributes_t TextureLockRotated(
    geometry_brush_side_attributes_t attributes,
    const math::affine3d_t &combined,
    const math::affine3d_t &rotation ) noexcept
{
    attributes.uvProjection.origin = math::Affine3d_TransformPoint(
        combined, attributes.uvProjection.origin );
    attributes.uvProjection.uAxis = math::Affine3d_TransformDirection(
        rotation, attributes.uvProjection.uAxis );
    attributes.uvProjection.vAxis = math::Affine3d_TransformDirection(
        rotation, attributes.uvProjection.vAxis );
    attributes.uvProjection.normal = math::Affine3d_TransformDirection(
        rotation, attributes.uvProjection.normal );
    return attributes;
}

geometry_brush_side_attributes_t TextureLockScaled(
    geometry_brush_side_attributes_t attributes,
    const math::affine3d_t &combined,
    const math::affine3d_t &scaleTransform,
    math::vec3d_t scale ) noexcept
{
    attributes.uvProjection.origin = math::Affine3d_TransformPoint(
        combined, attributes.uvProjection.origin );

    const common::f64 uScale =
        math::Scalar_Abs( scale.x * attributes.uvProjection.uAxis.x ) +
        math::Scalar_Abs( scale.y * attributes.uvProjection.uAxis.y ) +
        math::Scalar_Abs( scale.z * attributes.uvProjection.uAxis.z );
    const common::f64 vScale =
        math::Scalar_Abs( scale.x * attributes.uvProjection.vAxis.x ) +
        math::Scalar_Abs( scale.y * attributes.uvProjection.vAxis.y ) +
        math::Scalar_Abs( scale.z * attributes.uvProjection.vAxis.z );

    if ( uScale > 1.0e-12 ) {
        attributes.uvProjection.worldUnitsPerUv.x *= uScale;
    }
    if ( vScale > 1.0e-12 ) {
        attributes.uvProjection.worldUnitsPerUv.y *= vScale;
    }

    attributes.uvProjection.uAxis = math::Affine3d_TransformDirection(
        scaleTransform, attributes.uvProjection.uAxis );
    attributes.uvProjection.vAxis = math::Affine3d_TransformDirection(
        scaleTransform, attributes.uvProjection.vAxis );

    const common::f64 uLen = math::Scalar_Sqrt(
        math::Vec3d_LengthSquared( attributes.uvProjection.uAxis ) );
    const common::f64 vLen = math::Scalar_Sqrt(
        math::Vec3d_LengthSquared( attributes.uvProjection.vAxis ) );
    if ( uLen > 1.0e-12 ) {
        attributes.uvProjection.uAxis = math::Vec3d_Scale(
            attributes.uvProjection.uAxis, 1.0 / uLen );
    }
    if ( vLen > 1.0e-12 ) {
        attributes.uvProjection.vAxis = math::Vec3d_Scale(
            attributes.uvProjection.vAxis, 1.0 / vLen );
    }

    attributes.uvProjection.normal = math::Vec3d_Cross(
        attributes.uvProjection.uAxis, attributes.uvProjection.vAxis );
    const common::f64 nLen = math::Scalar_Sqrt(
        math::Vec3d_LengthSquared( attributes.uvProjection.normal ) );
    if ( nLen > 1.0e-12 ) {
        attributes.uvProjection.normal = math::Vec3d_Scale(
            attributes.uvProjection.normal, 1.0 / nLen );
    }
    return attributes;
}

} // namespace

geometry_status_t BrushTransform_TryTranslate(
    brush_solid_t *pBrush,
    math::vec3d_t offset ) noexcept
{
    if ( pBrush == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pBrush->sides.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !math::Vec3d_IsFinite( offset ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const common::usize cSides = BrushSolid_SideCount( pBrush );
    if ( cSides == 0u ) {
        return geometry_status_t::OK;
    }

    // Preflight every result before publishing any plane. Finite operands can
    // still overflow at authoring-scale extremes, so translating in one pass
    // would otherwise leave an early subset changed.
    for ( common::usize i = 0u; i < cSides; ++i ) {
        const brush_solid_side_t &side = pBrush->sides.pData[i];
        const common::f64 shift =
            math::Vec3d_Dot( side.plane.normal, offset );
        const common::f64 translatedDistance = side.plane.d - shift;
        if ( !math::Scalar_IsFinite( shift ) ||
             !math::Scalar_IsFinite( translatedDistance ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
    }

    for ( common::usize i = 0u; i < cSides; ++i ) {
        brush_solid_side_t &side = pBrush->sides.pData[i];
        const common::f64 shift =
            math::Vec3d_Dot( side.plane.normal, offset );
        side.plane.d -= shift;
    }

    return geometry_status_t::OK;
}

geometry_status_t BrushTransform_TryRotate(
    brush_solid_t *pBrush,
    math::vec3d_t pivot,
    math::affine3d_t rotation ) noexcept
{
    if ( pBrush == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pBrush->sides.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !math::Vec3d_IsFinite( pivot ) ||
         !math::Affine3d_IsFinite( rotation ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    // Build: translate(-pivot) → rotate → translate(+pivot).
    const math::affine3d_t toOrigin =
        math::Affine3d_FromTranslation( math::Vec3d_Negate( pivot ) );
    const math::affine3d_t fromOrigin =
        math::Affine3d_FromTranslation( pivot );

    const math::affine3d_t combined = math::Affine3d_Multiply(
        fromOrigin,
        math::Affine3d_Multiply( rotation, toOrigin ) );

    return BrushTransform_TryApplyAffine( pBrush, combined );
}

geometry_status_t BrushTransform_TryScale(
    brush_solid_t *pBrush,
    math::vec3d_t pivot,
    math::vec3d_t scale ) noexcept
{
    if ( pBrush == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pBrush->sides.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !math::Vec3d_IsFinite( scale ) ||
         !math::Vec3d_IsFinite( pivot ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    // Reject zero or negative scale on any axis.
    if ( scale.x <= 0.0 || scale.y <= 0.0 || scale.z <= 0.0 ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    // Build: translate(-pivot) → scale → translate(+pivot).
    const math::affine3d_t toOrigin =
        math::Affine3d_FromTranslation( math::Vec3d_Negate( pivot ) );
    const math::affine3d_t scaleXform =
        math::Affine3d_FromScale( scale );
    const math::affine3d_t fromOrigin =
        math::Affine3d_FromTranslation( pivot );

    const math::affine3d_t combined = math::Affine3d_Multiply(
        fromOrigin,
        math::Affine3d_Multiply( scaleXform, toOrigin ) );

    return BrushTransform_TryApplyAffine( pBrush, combined );
}

geometry_status_t BrushTransform_TryApplyAffine(
    brush_solid_t *pBrush,
    math::affine3d_t transform ) noexcept
{
    if ( pBrush == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pBrush->sides.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !math::Affine3d_IsFinite( transform ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const common::usize cSides = BrushSolid_SideCount( pBrush );
    if ( cSides == 0u ) {
        return geometry_status_t::OK;
    }
    // First pass validates every result. The second pass recomputes the pure
    // transform and publishes it, avoiding both a hidden fixed side limit and
    // a fallible scratch allocation.
    for ( common::usize i = 0u; i < cSides; ++i ) {
        const brush_solid_side_t &side = pBrush->sides.pData[i];
        math::planed_t transformed{};
        if ( !math::Planed_TryTransform(
                 side.plane, transform,
                 kMinAbsDeterminant, kMinNormalLength,
                 &transformed ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
    }

    // Second pass cannot fail after the identical preflight above.
    for ( common::usize i = 0u; i < cSides; ++i ) {
        math::planed_t transformed{};
        const bool bTransformed = math::Planed_TryTransform(
            pBrush->sides.pData[i].plane,
            transform,
            kMinAbsDeterminant,
            kMinNormalLength,
            &transformed );
        if ( !bTransformed ) {
            return geometry_status_t::CORRUPT_STATE;
        }
        pBrush->sides.pData[i].plane = transformed;
    }

    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// Texture lock
// ---------------------------------------------------------------------------

geometry_status_t BrushTransform_TextureLockTranslate(
    const brush_solid_t *pBrush,
    geometry_brush_side_attribute_store_t *pStore,
    math::vec3d_t offset,
    const geometry_policy_t &policy ) noexcept
{
    if ( !math::Vec3d_IsFinite( offset ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const geometry_status_t bindings =
        ValidateTextureLockBindings( pBrush, pStore, policy );
    if ( bindings != geometry_status_t::OK ) {
        return bindings;
    }

    const common::usize cSides = BrushSolid_SideCount( pBrush );
    for ( common::usize iSide = 0u; iSide < cSides; ++iSide ) {
        if ( !IsFirstAttributeReference( pBrush, iSide ) ) { continue; }
        const common::usize iAttribute = static_cast<common::usize>(
            pBrush->sides.pData[iSide].iAttributeIndex );
        const geometry_brush_side_attributes_t transformed =
            TextureLockTranslated(
                pStore->records.pData[iAttribute], offset );
        const geometry_status_t status = BrushSideAttributes_Validate(
            policy.numerical, transformed );
        if ( status != geometry_status_t::OK ) { return status; }
    }

    for ( common::usize iSide = 0u; iSide < cSides; ++iSide ) {
        if ( !IsFirstAttributeReference( pBrush, iSide ) ) { continue; }
        const common::usize iAttribute = static_cast<common::usize>(
            pBrush->sides.pData[iSide].iAttributeIndex );
        pStore->records.pData[iAttribute] = TextureLockTranslated(
            pStore->records.pData[iAttribute], offset );
    }

    return geometry_status_t::OK;
}

geometry_status_t BrushTransform_TextureLockRotate(
    const brush_solid_t *pBrush,
    geometry_brush_side_attribute_store_t *pStore,
    math::vec3d_t pivot,
    math::affine3d_t rotation,
    const geometry_policy_t &policy ) noexcept
{
    if ( !math::Vec3d_IsFinite( pivot ) ||
         !math::Affine3d_IsFinite( rotation ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const geometry_status_t bindings =
        ValidateTextureLockBindings( pBrush, pStore, policy );
    if ( bindings != geometry_status_t::OK ) {
        return bindings;
    }

    // Build pivot-relative transform for the UV origin.
    const math::affine3d_t toOrigin =
        math::Affine3d_FromTranslation( math::Vec3d_Negate( pivot ) );
    const math::affine3d_t fromOrigin =
        math::Affine3d_FromTranslation( pivot );
    const math::affine3d_t combined = math::Affine3d_Multiply(
        fromOrigin,
        math::Affine3d_Multiply( rotation, toOrigin ) );

    const common::usize cSides = BrushSolid_SideCount( pBrush );
    for ( common::usize iSide = 0u; iSide < cSides; ++iSide ) {
        if ( !IsFirstAttributeReference( pBrush, iSide ) ) { continue; }
        const common::usize iAttribute = static_cast<common::usize>(
            pBrush->sides.pData[iSide].iAttributeIndex );
        const geometry_brush_side_attributes_t transformed =
            TextureLockRotated(
                pStore->records.pData[iAttribute], combined, rotation );
        const geometry_status_t status = BrushSideAttributes_Validate(
            policy.numerical, transformed );
        if ( status != geometry_status_t::OK ) { return status; }
    }

    for ( common::usize iSide = 0u; iSide < cSides; ++iSide ) {
        if ( !IsFirstAttributeReference( pBrush, iSide ) ) { continue; }
        const common::usize iAttribute = static_cast<common::usize>(
            pBrush->sides.pData[iSide].iAttributeIndex );
        pStore->records.pData[iAttribute] = TextureLockRotated(
            pStore->records.pData[iAttribute], combined, rotation );
    }

    return geometry_status_t::OK;
}

geometry_status_t BrushTransform_TextureLockScale(
    const brush_solid_t *pBrush,
    geometry_brush_side_attribute_store_t *pStore,
    math::vec3d_t pivot,
    math::vec3d_t scale,
    const geometry_policy_t &policy ) noexcept
{
    if ( !math::Vec3d_IsFinite( scale ) ||
         !math::Vec3d_IsFinite( pivot ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( scale.x <= 0.0 || scale.y <= 0.0 || scale.z <= 0.0 ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const geometry_status_t bindings =
        ValidateTextureLockBindings( pBrush, pStore, policy );
    if ( bindings != geometry_status_t::OK ) {
        return bindings;
    }

    // Build the pivot-relative scale transform for the UV origin.
    const math::affine3d_t toOrigin =
        math::Affine3d_FromTranslation( math::Vec3d_Negate( pivot ) );
    const math::affine3d_t scaleXform =
        math::Affine3d_FromScale( scale );
    const math::affine3d_t fromOrigin =
        math::Affine3d_FromTranslation( pivot );
    const math::affine3d_t combined = math::Affine3d_Multiply(
        fromOrigin,
        math::Affine3d_Multiply( scaleXform, toOrigin ) );

    const common::usize cSides = BrushSolid_SideCount( pBrush );
    for ( common::usize iSide = 0u; iSide < cSides; ++iSide ) {
        if ( !IsFirstAttributeReference( pBrush, iSide ) ) { continue; }
        const common::usize iAttribute = static_cast<common::usize>(
            pBrush->sides.pData[iSide].iAttributeIndex );
        const geometry_brush_side_attributes_t transformed =
            TextureLockScaled(
                pStore->records.pData[iAttribute],
                combined,
                scaleXform,
                scale );
        const geometry_status_t status = BrushSideAttributes_Validate(
            policy.numerical, transformed );
        if ( status != geometry_status_t::OK ) { return status; }
    }

    for ( common::usize iSide = 0u; iSide < cSides; ++iSide ) {
        if ( !IsFirstAttributeReference( pBrush, iSide ) ) { continue; }
        const common::usize iAttribute = static_cast<common::usize>(
            pBrush->sides.pData[iSide].iAttributeIndex );
        pStore->records.pData[iAttribute] = TextureLockScaled(
            pStore->records.pData[iAttribute],
            combined,
            scaleXform,
            scale );
    }

    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
