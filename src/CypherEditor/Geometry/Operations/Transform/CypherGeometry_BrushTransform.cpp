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
    geometry_brush_side_attribute_store_t *pStore,
    common::usize cSides,
    math::vec3d_t offset,
    const geometry_policy_t &policy ) noexcept
{
    if ( pStore == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !math::Vec3d_IsFinite( offset ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    // Translation lock: shift each side's UV origin by the same offset
    // so the texture stays pinned to the same world position.
    for ( common::usize i = 0u; i < cSides; ++i ) {
        geometry_brush_side_attributes_t attrs{};
        geometry_status_t s =
            BrushSideAttributeStore_TryGet( pStore, i, &attrs );
        if ( s != geometry_status_t::OK ) { continue; }

        attrs.uvProjection.origin = math::Vec3d_Add(
            attrs.uvProjection.origin, offset );

        (void)BrushSideAttributeStore_TrySet(
            pStore, policy.numerical, i, attrs );
    }

    return geometry_status_t::OK;
}

geometry_status_t BrushTransform_TextureLockRotate(
    geometry_brush_side_attribute_store_t *pStore,
    common::usize cSides,
    math::vec3d_t pivot,
    math::affine3d_t rotation,
    const geometry_policy_t &policy ) noexcept
{
    if ( pStore == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !math::Affine3d_IsFinite( rotation ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    // Build pivot-relative transform for the UV origin.
    const math::affine3d_t toOrigin =
        math::Affine3d_FromTranslation( math::Vec3d_Negate( pivot ) );
    const math::affine3d_t fromOrigin =
        math::Affine3d_FromTranslation( pivot );
    const math::affine3d_t combined = math::Affine3d_Multiply(
        fromOrigin,
        math::Affine3d_Multiply( rotation, toOrigin ) );

    for ( common::usize i = 0u; i < cSides; ++i ) {
        geometry_brush_side_attributes_t attrs{};
        geometry_status_t s =
            BrushSideAttributeStore_TryGet( pStore, i, &attrs );
        if ( s != geometry_status_t::OK ) { continue; }

        // Rotate the UV origin through the full pivot-relative transform.
        attrs.uvProjection.origin = math::Affine3d_TransformPoint(
            combined, attrs.uvProjection.origin );

        // Rotate the UV axes as directions (no translation component).
        attrs.uvProjection.uAxis = math::Affine3d_TransformDirection(
            rotation, attrs.uvProjection.uAxis );
        attrs.uvProjection.vAxis = math::Affine3d_TransformDirection(
            rotation, attrs.uvProjection.vAxis );
        attrs.uvProjection.normal = math::Affine3d_TransformDirection(
            rotation, attrs.uvProjection.normal );

        (void)BrushSideAttributeStore_TrySet(
            pStore, policy.numerical, i, attrs );
    }

    return geometry_status_t::OK;
}

geometry_status_t BrushTransform_TextureLockScale(
    geometry_brush_side_attribute_store_t *pStore,
    common::usize cSides,
    math::vec3d_t pivot,
    math::vec3d_t scale,
    const geometry_policy_t &policy ) noexcept
{
    if ( pStore == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !math::Vec3d_IsFinite( scale ) ||
         !math::Vec3d_IsFinite( pivot ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( scale.x <= 0.0 || scale.y <= 0.0 || scale.z <= 0.0 ) {
        return geometry_status_t::INVALID_ARGUMENT;
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

    for ( common::usize i = 0u; i < cSides; ++i ) {
        geometry_brush_side_attributes_t attrs{};
        geometry_status_t s =
            BrushSideAttributeStore_TryGet( pStore, i, &attrs );
        if ( s != geometry_status_t::OK ) { continue; }

        // Scale the UV origin through the pivot-relative transform.
        attrs.uvProjection.origin = math::Affine3d_TransformPoint(
            combined, attrs.uvProjection.origin );

        // Adjust worldUnitsPerUv by the scale factor projected onto each
        // UV axis. This keeps the texel density constant after scaling.
        const common::f64 uScale =
            math::Scalar_Abs( scale.x * attrs.uvProjection.uAxis.x ) +
            math::Scalar_Abs( scale.y * attrs.uvProjection.uAxis.y ) +
            math::Scalar_Abs( scale.z * attrs.uvProjection.uAxis.z );
        const common::f64 vScale =
            math::Scalar_Abs( scale.x * attrs.uvProjection.vAxis.x ) +
            math::Scalar_Abs( scale.y * attrs.uvProjection.vAxis.y ) +
            math::Scalar_Abs( scale.z * attrs.uvProjection.vAxis.z );

        if ( uScale > 1.0e-12 ) {
            attrs.uvProjection.worldUnitsPerUv.x *= uScale;
        }
        if ( vScale > 1.0e-12 ) {
            attrs.uvProjection.worldUnitsPerUv.y *= vScale;
        }

        // Scale the UV axes — they must follow the geometry.
        attrs.uvProjection.uAxis = math::Affine3d_TransformDirection(
            scaleXform, attrs.uvProjection.uAxis );
        attrs.uvProjection.vAxis = math::Affine3d_TransformDirection(
            scaleXform, attrs.uvProjection.vAxis );

        // Renormalize the UV axes to unit length.
        const common::f64 uLen =
            math::Scalar_Sqrt( math::Vec3d_LengthSquared(
                attrs.uvProjection.uAxis ) );
        const common::f64 vLen =
            math::Scalar_Sqrt( math::Vec3d_LengthSquared(
                attrs.uvProjection.vAxis ) );
        if ( uLen > 1.0e-12 ) {
            attrs.uvProjection.uAxis = math::Vec3d_Scale(
                attrs.uvProjection.uAxis, 1.0 / uLen );
        }
        if ( vLen > 1.0e-12 ) {
            attrs.uvProjection.vAxis = math::Vec3d_Scale(
                attrs.uvProjection.vAxis, 1.0 / vLen );
        }

        // Recompute normal from the scaled axes.
        attrs.uvProjection.normal = math::Vec3d_Cross(
            attrs.uvProjection.uAxis, attrs.uvProjection.vAxis );
        const common::f64 nLen =
            math::Scalar_Sqrt( math::Vec3d_LengthSquared(
                attrs.uvProjection.normal ) );
        if ( nLen > 1.0e-12 ) {
            attrs.uvProjection.normal = math::Vec3d_Scale(
                attrs.uvProjection.normal, 1.0 / nLen );
        }

        (void)BrushSideAttributeStore_TrySet(
            pStore, policy.numerical, i, attrs );
    }

    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
