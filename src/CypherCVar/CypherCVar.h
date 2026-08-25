//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCVar/CypherCVar.h
//  Purpose: Declares the CypherCVar CVar module.
//  Details: This file participates in console variable storage and runtime tuning.
//           Keep value ownership, defaults, and validation explicit so tools can
//           inspect and edit them safely.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_CVAR_H
#define CYPHER_ENGINE_CVAR_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCVar_Error.h"

#define CYPHER_CVAR_MAX_CVARS 256u // Fixed console-variable registry capacity.

namespace cypher::engine::cvar {

/*
================
Cvar Types

Cvars are named runtime variables used by configs, console and engine systems.
================
*/
enum flags_t : common::u32 {
		CYPHER_CVAR_NONE = 0,             // No special storage or mutation policy.
		CYPHER_CVAR_ARCHIVE = 1 << 0,     // Persist value to user configuration.
		CYPHER_CVAR_READONLY = 1 << 1,    // Reject ordinary runtime mutation.
		CYPHER_CVAR_CHEAT = 1 << 2,       // Mutation requires cheat policy to be enabled.
		CYPHER_CVAR_DEV = 1 << 3,         // Developer-only variable, hidden in shipping policy.
		CYPHER_CVAR_MODIFIED = 1 << 4     // Runtime value differs from its registered default.
};

struct cvar_t {
		const char *name;                  // Borrowed unique variable name.
		char valueString[256];             // Canonical current text value.
		char defaultString[256];           // Registered default text value.
		common::u32 valueInt;              // Cached unsigned integer interpretation.
		common::f32 valueFloat;            // Cached floating-point interpretation.
		bool valueBool;                    // Cached boolean interpretation.
		flags_t flags;                     // Registration policy plus modified state.
};

struct registry_t {
		cvar_t cvars[CYPHER_CVAR_MAX_CVARS]; // Compact registered-variable array.
		common::u32 nCvarCount;              // Initialized prefix of cvars.
		bool initialized;                    // Registry lifetime guard.
};

constexpr common::u32 CYPHER_CVAR_REGISTER_ALLOWED_FLAGS =
		CYPHER_CVAR_ARCHIVE | CYPHER_CVAR_READONLY | CYPHER_CVAR_CHEAT | CYPHER_CVAR_DEV; // MODIFIED is runtime-owned.

/*
================
Cvar API
================
*/
cvar_error_t Cvar_Init();

cvar_error_t Cvar_Register( const char *name, const char *defaultValue, flags_t flags );

cvar_error_t Cvar_Set( const char *name, const char *value );

cvar_error_t Cvar_Shutdown();

const cvar_t *Cvar_Find( const char *name );

const char *Cvar_GetString( const char *name );

common::u32 Cvar_GetInt( const char *name );

common::f32 Cvar_GetFloat( const char *name );

bool Cvar_GetBool( const char *name );

}

#endif // CYPHER_ENGINE_CVAR_H
