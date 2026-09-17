//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCVar/CypherCVar.cpp
//  Purpose: Implements the CypherCVar CVar module.
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

#include "CypherCVar.h"
#include "CypherCVar_Error.h"
#include "CypherLog.h"

#include <cctype>      // std::tolower for bool parsing.
#include <cstdlib>     // Defined-range strto* numeric conversion.
#include <cstring>     // strcmp / strncpy for cvar storage.

namespace cypher::engine::cvar {

static registry_t s_CvarRegistry;

/*
================
Cvar_Init
================
*/
cvar_error_t Cvar_Init() {
	if ( s_CvarRegistry.initialized ) {
		LOG_WARNING( log::channel_t::CVAR, "cvar system init requested while already initialized." );
		return cvar_error_t::ERR_IS_INIT;
	}

	s_CvarRegistry = {};
	s_CvarRegistry.initialized = true;

	LOG_DEBUG( log::channel_t::CVAR, "cvar registry ready." );

	return cvar_error_t::OK;
}

/*
================
Cvar_IsInitialized
================
*/
bool Cvar_IsInitialized()
{
	return s_CvarRegistry.initialized;
}

/*
================
Cvar_Count
================
*/
common::u32 Cvar_Count()
{
	return s_CvarRegistry.initialized ? s_CvarRegistry.nCvarCount : 0u;
}

/*
================
Cvar_GetByIndex
================
*/
const cvar_t *Cvar_GetByIndex( const common::u32 index )
{
	if ( !s_CvarRegistry.initialized || index >= s_CvarRegistry.nCvarCount ) {
		return nullptr;
	}

	return &s_CvarRegistry.cvars[index];
}

/*
================
Cvar_ParseBool

Accepts common console-friendly true/false strings.
================
*/
bool Cvar_ParseBool( const char *value ) {
	if ( value == nullptr || value[0] == '\0' ) {
		return false;
	}

	char lower[32]{};
	common::u32 i;

	for ( i = 0; i < 31 && value[i] != '\0'; i++ ) {
    lower[i] = static_cast<char>( std::tolower( static_cast<unsigned char>( value[i] ) ) );
	}
	lower[i] = '\0';

	if ( std::strcmp( lower, "0" ) == 0 || std::strcmp( lower, "false" ) == 0 || std::strcmp( lower, "off" ) == 0 || std::strcmp( lower, "no" ) == 0 ) {
		return false;
	}

	if ( std::strcmp( lower, "1" ) == 0 || std::strcmp( lower, "true" ) == 0 || std::strcmp( lower, "on" ) == 0 || std::strcmp( lower, "yes" ) == 0 ) {
		return true;
	}

	// Numeric text can exceed the temporary word buffer or the range of int.
	return ( std::strtoll( value, nullptr, 10 ) != 0 );
}

/*
================
Cvar_ParseFloat

Accepts a floating-point value only when strtof consumes the complete string.
This prevents ordinary text that begins with a special token, such as "info",
from being cached as infinity through the accepted "inf" prefix.
================
*/
common::f32 Cvar_ParseFloat( const char *value ) {
	if ( value == nullptr || value[0] == '\0' ) {
		return 0.0f;
	}

	char *end = nullptr;
	const common::f32 parsed = std::strtof( value, &end );
	if ( end == value || end == nullptr || end[0] != '\0' ) {
		return 0.0f;
	}

	return parsed;
}

/*
================
Cvar_Register

Adds a cvar with default string and cached numeric views.
================
*/
cvar_error_t Cvar_Register( const char *name, const char *defaultValue, flags_t flags ) {
	if ( !s_CvarRegistry.initialized ) {
		LOG_ERROR( log::channel_t::CVAR, "cvar register failed for '%s': cvar system is not initialized.", name ? name : "<null>" );
		return cvar_error_t::ERR_NOT_INIT;
	}

	if ( name == nullptr || name[0] == '\0' ) {
		LOG_ERROR( log::channel_t::CVAR, "cvar register failed: invalid cvar name." );
		return cvar_error_t::ERR_INVALID_CVAR;
	}

	if ( defaultValue == nullptr || defaultValue[0] == '\0' ) {
		LOG_ERROR( log::channel_t::CVAR, "cvar register failed for '%s': invalid default value.", name );
		return cvar_error_t::ERR_INVALID_DEFAULT_VALUE;
	}
	if ( std::strlen( defaultValue ) >= sizeof( s_CvarRegistry.cvars[0].valueString ) ) {
		return cvar_error_t::ERR_INVALID_DEFAULT_VALUE;
	}

	common::u32 nFlagsBits = static_cast<common::u32>( flags );

	if ( ( nFlagsBits & CYPHER_CVAR_MODIFIED ) != 0u ) {
		LOG_ERROR( log::channel_t::CVAR, "cvar register failed for '%s': registration passed runtime modified flag.", name );
		return cvar_error_t::ERR_INVALID_FLAG;
	}

	// CYPHER_CVAR_MODIFIED is runtime-owned; registration only accepts authoring flags.
	if ( ( nFlagsBits & ~CYPHER_CVAR_REGISTER_ALLOWED_FLAGS ) != 0 ) {
		LOG_ERROR( log::channel_t::CVAR, "cvar register failed for '%s': invalid flags 0x%x.", name, nFlagsBits );
		return cvar_error_t::ERR_INVALID_FLAG;
	}

	const cvar_t *cvar = Cvar_Find( name );

	if ( cvar != nullptr ) {
		LOG_WARNING( log::channel_t::CVAR, "cvar register skipped: '%s' already exists.", name );
		return cvar_error_t::ERR_CVAR_ALREADY_EXISTS;
	}

	if ( s_CvarRegistry.nCvarCount >= CYPHER_CVAR_MAX_CVARS ) {
		LOG_ERROR( log::channel_t::CVAR, "cvar register failed for '%s': registry full (%u).", name, CYPHER_CVAR_MAX_CVARS );
		return cvar_error_t::ERR_REGISTRY_FULL;
	}

	cvar_t &entry = s_CvarRegistry.cvars[s_CvarRegistry.nCvarCount];

	entry.name = name;
	std::strncpy( entry.valueString, defaultValue, sizeof( entry.valueString ) - 1 );
	std::strncpy( entry.defaultString, defaultValue, sizeof( entry.defaultString ) - 1 );
	entry.valueString[sizeof( entry.valueString ) - 1] = '\0';
	entry.defaultString[sizeof( entry.defaultString ) - 1] = '\0';
	entry.valueInt = static_cast<common::u32>( std::strtoull( entry.valueString, nullptr, 10 ) );
	entry.valueFloat = Cvar_ParseFloat( entry.valueString );
	entry.flags = flags;
	entry.valueBool = Cvar_ParseBool( entry.valueString );
	s_CvarRegistry.nCvarCount++;

	LOG_DEBUG( log::channel_t::CVAR, "registered cvar '%s' default='%s' flags=0x%x.", name, defaultValue, nFlagsBits );

	return cvar_error_t::OK;
}

/*
================
Cvar_Set

Changes a cvar string and updates its cached int/float/bool values.
================
*/
cvar_error_t Cvar_Set( const char *name, const char *value ) {
	if ( !s_CvarRegistry.initialized ) {
		LOG_ERROR( log::channel_t::CVAR, "cvar set failed for '%s': cvar system is not initialized.", name ? name : "<null>" );
		return cvar_error_t::ERR_NOT_INIT;
	}

	if ( name == nullptr || name[0] == '\0' ) {
        LOG_ERROR( log::channel_t::CVAR, "cvar set failed: invalid cvar name." );
        return cvar_error_t::ERR_INVALID_CVAR;
	}

    if ( value == nullptr ) {
        LOG_ERROR( log::channel_t::CVAR, "cvar set failed for '%s': value is null.", name );
        return cvar_error_t::ERR_INVALID_CVAR;

    }

    cvar_t *target = nullptr;

    // Linear for now; later this can become a hash table without changing API.
    for ( common::u32 i = 0; i < s_CvarRegistry.nCvarCount; ++i ) {
        if ( std::strcmp( s_CvarRegistry.cvars[i].name, name ) == 0 ) {
            target = &s_CvarRegistry.cvars[i];
            break;
        }
    }
    if ( target == nullptr ) {
        LOG_WARNING( log::channel_t::CVAR, "cvar set failed: '%s' not found.", name );
        return cvar_error_t::ERR_CVAR_NOT_FOUND;
    }
    if ( ( static_cast<common::u32>( target->flags ) & CYPHER_CVAR_READONLY ) != 0 ) {
        LOG_WARNING( log::channel_t::CVAR, "cvar set blocked: '%s' is read-only.", name );
        return cvar_error_t::ERR_READONLY;
    }

    // Cheat protection will later be controlled by server/game authority.
    const bool bCheatsEnabled = false;
    if ( ( static_cast<common::u32>( target->flags ) & CYPHER_CVAR_CHEAT ) != 0 && !bCheatsEnabled ) {
        LOG_WARNING( log::channel_t::CVAR, "cvar set blocked: '%s' is cheat-protected.", name );
        return cvar_error_t::ERR_CHEAT_PROTECTED;
    }

    const common::usize length = std::strlen( value );
    if ( length >= sizeof( target->valueString ) ) {
        return cvar_error_t::ERR_INVALID_CVAR;
    }
    // Callers may reuse a view returned by Cvar_GetString, including a suffix.
    // Stage it before mutation so overlapping input never reaches strncpy.
    char valueCopy[sizeof( target->valueString )]{};
    std::memcpy( valueCopy, value, length + 1u );
    std::memcpy( target->valueString, valueCopy, sizeof( valueCopy ) );

    target->valueInt = static_cast<common::u32>( std::strtoull( target->valueString, nullptr, 10 ) );
    target->valueFloat = Cvar_ParseFloat( target->valueString );
    target->valueBool = Cvar_ParseBool( target->valueString );

    common::u32 flags = static_cast<common::u32>( target->flags ) & ~CYPHER_CVAR_MODIFIED;
    if ( std::strcmp( target->valueString, target->defaultString ) != 0 ) {
        flags |= CYPHER_CVAR_MODIFIED;
    }
    target->flags = static_cast<flags_t>( flags );

    LOG_DEBUG( log::channel_t::CVAR, "cvar '%s' set to '%s'.", name, target->valueString );

    return cvar_error_t::OK;
}

/*
================
Cvar_Shutdown
================
*/
cvar_error_t Cvar_Shutdown() {
    if ( !s_CvarRegistry.initialized ) {
        LOG_WARNING( log::channel_t::CVAR, "cvar system shutdown requested while not initialized." );
        return cvar_error_t::ERR_NOT_INIT;
    }
    LOG_INFO( log::channel_t::CVAR, "cvar system shutdown: cvars=%u.", s_CvarRegistry.nCvarCount );
    s_CvarRegistry = {};

    return cvar_error_t::OK;
}

/*
================
Cvar_Find
================
*/
const cvar_t *Cvar_Find( const char *name ) {
    if ( !s_CvarRegistry.initialized ) {
        return nullptr;
    }

    if ( name == nullptr || name[0] == '\0' ) {
        return nullptr;
    }

    const cvar_t *cvar = nullptr;

    for ( common::u32 i = 0; i < s_CvarRegistry.nCvarCount; ++i ) {
        if ( s_CvarRegistry.cvars[i].name == nullptr ) {
            continue;
        }
        if ( std::strcmp( s_CvarRegistry.cvars[i].name, name ) == 0 ) {
            cvar = &s_CvarRegistry.cvars[i];
            return cvar;
        }
    }

    return cvar;
}

/*
================
Cvar_GetString
================
*/
const char *Cvar_GetString( const char *name ) {
    if ( !s_CvarRegistry.initialized ) {
        return nullptr;
    }

    if ( name == nullptr || name[0] == '\0' ) {
        return nullptr;
    }

    const cvar_t *cvar = Cvar_Find( name );

    if ( cvar == nullptr ) {
        return "";
    }

    return cvar->valueString;
}

/*
================
Cvar_GetInt
================
*/
common::u32 Cvar_GetInt( const char *name ) {
    if ( !s_CvarRegistry.initialized ) {
        return (common::u32)0u;
    }

    if ( name == nullptr || name[0] == '\0' ) {
        return (common::u32)0u;
    }

    const cvar_t *cvar = Cvar_Find( name );

    if ( cvar == nullptr ) {
        return (common::u32)0u;
    }

    return cvar->valueInt;
}

/*
================
Cvar_GetFloat
================
*/
common::f32 Cvar_GetFloat( const char *name ) {
    if ( !s_CvarRegistry.initialized ) {
        return (common::f32)0.0f;
    }

    if ( name == nullptr || name[0] == '\0' ) {
        return (common::f32)0.0f;
    }

    const cvar_t *cvar = Cvar_Find( name );

    if ( cvar == nullptr ) {
        return (common::f32)0.0f;
    }

    return cvar->valueFloat;
}

/*
================
Cvar_GetBool
================
*/
bool Cvar_GetBool( const char *name ) {
    if ( !s_CvarRegistry.initialized ) {
        return false;
    }

    if ( name == nullptr || name[0] == '\0' ) {
        return false;
    }

    const cvar_t *cvar = Cvar_Find( name );

    if ( cvar == nullptr ) {
        return false;
    }

    return cvar->valueBool;
}

} // namespace cypher::engine::cvar
