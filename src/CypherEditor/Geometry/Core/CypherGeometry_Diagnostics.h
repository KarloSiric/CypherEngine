//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Diagnostics.h
//  Purpose: Declares bounded structured diagnostics for editable geometry.
//  Details: Records use stable module-qualified codes and persistent source
//           identities. The buffer borrows caller storage and never allocates.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_DIAGNOSTICS_H
#define CYPHER_EDITOR_GEOMETRY_DIAGNOSTICS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Types.h"

#include "CypherCommon_Span.h"

#include <type_traits>

namespace cypher::editor::geometry
{

using common::bool_t;
using common::span_t;
using common::u16;
using common::u32;
using common::u64;
using common::u8;
using common::usize;

// Module values are encoded into diagnostic codes. Existing values therefore
// remain fixed even if new modules are appended before COUNT in the future.
enum class geometry_diagnostic_module_t : u8 {
    INVALID = 0u,
    CORE = 1u,
    REPRESENTATIONS = 2u,
    TOPOLOGY = 3u,
    VALIDATION = 4u,
    TRANSACTIONS = 5u,
    PRIMITIVES = 6u,
    OPERATIONS = 7u,
    CSG = 8u,
    ATTRIBUTES = 9u,
    SPATIAL = 10u,
    COOK = 11u,
    SERIALIZATION = 12u,
    KERNEL = 13u,
    INTERMEDIATES = 14u,
    PLANAR = 15u,
    QUERIES = 16u,
    REPAIR = 17u,
    MODIFIERS = 18u,
    TESSELLATION = 19u,
    PROCEDURAL = 20u,
    EXCHANGE = 21u,
    SELECTION = 22u,
    COUNT = 23u
};

// Zero remains an invalid default so an uninitialized record cannot be emitted
// accidentally. Severity values are stable presentation-independent metadata.
enum class geometry_diagnostic_severity_t : u8 {
    INVALID = 0u,
    NOTE = 1u,
    WARNING = 2u,
    ERROR = 3u,
    FATAL = 4u,
    COUNT = 5u
};

using geometry_diagnostic_code_t = u32;
using geometry_diagnostic_component_code_t = u16;

inline constexpr geometry_diagnostic_code_t
    GEOMETRY_DIAGNOSTIC_CODE_NONE = 0u;
inline constexpr geometry_diagnostic_component_code_t
    GEOMETRY_DIAGNOSTIC_COMPONENT_ROOT = 0u;

inline constexpr u32 GEOMETRY_DIAGNOSTIC_LOCAL_CODE_BITS = 24u;
inline constexpr u32 GEOMETRY_DIAGNOSTIC_LOCAL_CODE_MASK = 0x00FFFFFFu;

// A component code is interpreted only within `representation`. Code zero names
// the representation root; a nonzero code names a representation-specific
// persistent component category. For example, component code 1 for a mesh and
// component code 1 for a planar region are deliberately different namespaces.
// This descriptor never stores a process-local topology handle.
struct geometry_diagnostic_target_t {
    geometry_source_id_t source{};       // Persistent ID of the representation root.
    geometry_source_id_t component{};    // Optional persistent component ID.
    geometry_source_representation_kind_t representation{
        geometry_source_representation_kind_t::INVALID };
    geometry_diagnostic_component_code_t componentCode{
        GEOMETRY_DIAGNOSTIC_COMPONENT_ROOT };
};

// Records contain machine-readable facts only. Frontends map stable codes to
// localized prose. `related` is optional but cannot be present without `target`.
// Geometric witnesses will be added after the shared double-precision geometry
// math types exist; this contract must not silently narrow them to float. These
// are process-local API values, not a wire ABI: never serialize, hash, compare,
// or transfer their raw padded bytes.
struct geometry_diagnostic_t {
    geometry_diagnostic_code_t code{ GEOMETRY_DIAGNOSTIC_CODE_NONE };
    geometry_diagnostic_module_t module{ geometry_diagnostic_module_t::INVALID };
    geometry_diagnostic_severity_t severity{
        geometry_diagnostic_severity_t::INVALID };
    geometry_diagnostic_target_t target{};
    geometry_diagnostic_target_t related{};
};

// A single-writer, bounded sink over borrowed storage. Initialization grants the
// buffer exclusive write access to storage until Shutdown. The caller must not
// mutate the published prefix; use ValidateDeep at debug, transaction, or
// serialization boundaries. Append order is observable. Readers may consume
// Records() after the writer stops mutating or after external synchronization.
struct geometry_diagnostic_buffer_t {
    span_t<geometry_diagnostic_t> storage{}; // Borrowed for the initialized lifetime.
    usize cStored{ 0u };                     // Prefix containing published records.
    u64 cTruncated{ 0u };                    // Valid records dropped after capacity.
    bool_t bInitialized{ false };
};

[[nodiscard]] constexpr bool_t GeometryDiagnosticModule_IsValid(
    geometry_diagnostic_module_t module ) noexcept
{
    return static_cast<u8>( module ) >
               static_cast<u8>( geometry_diagnostic_module_t::INVALID ) &&
           static_cast<u8>( module ) <
               static_cast<u8>( geometry_diagnostic_module_t::COUNT );
}

[[nodiscard]] constexpr bool_t GeometryDiagnosticSeverity_IsValid(
    geometry_diagnostic_severity_t severity ) noexcept
{
    return static_cast<u8>( severity ) >
               static_cast<u8>( geometry_diagnostic_severity_t::INVALID ) &&
           static_cast<u8>( severity ) <
               static_cast<u8>( geometry_diagnostic_severity_t::COUNT );
}

// Codes reserve the high byte for the stable module and the low 24 bits for a
// module-local nonzero code. Invalid inputs produce the reserved zero code.
[[nodiscard]] constexpr geometry_diagnostic_code_t
GeometryDiagnosticCode_Make(
    geometry_diagnostic_module_t module,
    u32 localCode ) noexcept
{
    return GeometryDiagnosticModule_IsValid( module ) && localCode != 0u &&
                   localCode <= GEOMETRY_DIAGNOSTIC_LOCAL_CODE_MASK
        ? ( static_cast<u32>( module ) <<
            GEOMETRY_DIAGNOSTIC_LOCAL_CODE_BITS ) |
              localCode
        : GEOMETRY_DIAGNOSTIC_CODE_NONE;
}

[[nodiscard]] constexpr geometry_diagnostic_module_t
GeometryDiagnosticCode_Module(
    geometry_diagnostic_code_t code ) noexcept
{
    const u32 encoded = code >> GEOMETRY_DIAGNOSTIC_LOCAL_CODE_BITS;
    return encoded < static_cast<u32>( geometry_diagnostic_module_t::COUNT )
        ? static_cast<geometry_diagnostic_module_t>( encoded )
        : geometry_diagnostic_module_t::INVALID;
}

[[nodiscard]] constexpr u32 GeometryDiagnosticCode_Local(
    geometry_diagnostic_code_t code ) noexcept
{
    return code & GEOMETRY_DIAGNOSTIC_LOCAL_CODE_MASK;
}

[[nodiscard]] constexpr bool_t GeometryDiagnosticCode_IsValid(
    geometry_diagnostic_code_t code ) noexcept
{
    return GeometryDiagnosticModule_IsValid(
               GeometryDiagnosticCode_Module( code ) ) &&
           GeometryDiagnosticCode_Local( code ) != 0u;
}

[[nodiscard]] constexpr geometry_diagnostic_target_t
GeometryDiagnosticTarget_Root(
    geometry_source_representation_kind_t representation,
    geometry_source_id_t source ) noexcept
{
    return { source, {}, representation, GEOMETRY_DIAGNOSTIC_COMPONENT_ROOT };
}

[[nodiscard]] constexpr geometry_diagnostic_target_t
GeometryDiagnosticTarget_Component(
    geometry_source_representation_kind_t representation,
    geometry_source_id_t source,
    geometry_source_id_t component,
    geometry_diagnostic_component_code_t componentCode ) noexcept
{
    return { source, component, representation, componentCode };
}

[[nodiscard]] bool_t GeometryDiagnosticTarget_IsEmpty(
    const geometry_diagnostic_target_t &target ) noexcept;

[[nodiscard]] bool_t GeometryDiagnosticTarget_IsValid(
    const geometry_diagnostic_target_t &target ) noexcept;

[[nodiscard]] bool_t GeometryDiagnostic_IsValid(
    const geometry_diagnostic_t &diagnostic ) noexcept;

// Initialization accepts a valid empty span as a deliberate zero-record budget.
// On every failure, *pBuffer is left unchanged. Reinitialization requires an
// explicit Shutdown so storage lifetime transitions remain visible to callers.
[[nodiscard]] geometry_status_t GeometryDiagnosticBuffer_Init(
    geometry_diagnostic_buffer_t *pBuffer,
    span_t<geometry_diagnostic_t> storage ) noexcept;

void GeometryDiagnosticBuffer_Shutdown(
    geometry_diagnostic_buffer_t *pBuffer ) noexcept;

[[nodiscard]] bool_t GeometryDiagnosticBuffer_IsValid(
    const geometry_diagnostic_buffer_t *pBuffer ) noexcept;

// Performs an O(n) audit of every record in the published prefix in addition to
// the constant-time structural checks performed by IsValid.
[[nodiscard]] bool_t GeometryDiagnosticBuffer_ValidateDeep(
    const geometry_diagnostic_buffer_t *pBuffer ) noexcept;

// Clear preserves borrowed storage and resets both the logical record count and
// truncation count, making the same budget reusable for the next operation.
[[nodiscard]] geometry_status_t GeometryDiagnosticBuffer_Clear(
    geometry_diagnostic_buffer_t *pBuffer ) noexcept;

// A valid diagnostic that does not fit increments cTruncated and returns
// INSUFFICIENT_CAPACITY. Existing records remain unchanged. Invalid records do
// not consume storage and do not count as truncation.
[[nodiscard]] geometry_status_t GeometryDiagnosticBuffer_Append(
    geometry_diagnostic_buffer_t *pBuffer,
    const geometry_diagnostic_t &diagnostic ) noexcept;

[[nodiscard]] span_t<const geometry_diagnostic_t>
GeometryDiagnosticBuffer_Records(
    const geometry_diagnostic_buffer_t *pBuffer ) noexcept;

[[nodiscard]] usize GeometryDiagnosticBuffer_Count(
    const geometry_diagnostic_buffer_t *pBuffer ) noexcept;

[[nodiscard]] usize GeometryDiagnosticBuffer_Capacity(
    const geometry_diagnostic_buffer_t *pBuffer ) noexcept;

[[nodiscard]] u64 GeometryDiagnosticBuffer_TruncatedCount(
    const geometry_diagnostic_buffer_t *pBuffer ) noexcept;

static_assert( std::is_standard_layout_v<geometry_diagnostic_target_t> );
static_assert( std::is_trivially_copyable_v<geometry_diagnostic_target_t> );
static_assert( std::is_standard_layout_v<geometry_diagnostic_t> );
static_assert( std::is_trivially_copyable_v<geometry_diagnostic_t> );

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_DIAGNOSTICS_H
