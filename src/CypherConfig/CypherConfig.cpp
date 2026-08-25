//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherConfig/CypherConfig.cpp
//  Purpose: Implements the CypherConfig Config module.
//  Details: This file participates in configuration loading and runtime settings.
//           Keep file format handling strict and predictable so startup failures are
//           easy to diagnose.
//
//  History:
//  - Created by Karlo Siric on 2026-04-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherConfig.h"
#include "CypherCommand.h"
#include "CypherCVar.h"
#include "CypherFileSystem.h"
#include "CypherLog.h"

#include <cctype>      // std::isspace while parsing cfg lines.
#include <cstring>     // strlen / strcmp / strncpy for line parsing.

namespace cypher::engine::cfg {

/*
================
Config Runtime State
================
*/
struct runtime_state_t {
	bool initialized{ false };
};

static runtime_state_t s_CfgRuntimeState;

constexpr const char *CYPHER_CONFIG_DEFAULT_PATH  = "config/default.cfg";
constexpr const char *CYPHER_CONFIG_AUTOEXEC_PATH = "config/autoexec.cfg";

/*
================
CypherConfig_Init
================
*/
cfg_error_t CypherConfig_Init() {
	if ( s_CfgRuntimeState.initialized ) {
		LOG_WARNING( log::channel_t::CFG, "config system init requested while already initialized." );
		return cfg_error_t::ERR_IS_INIT;
	}
	s_CfgRuntimeState = {};
	s_CfgRuntimeState.initialized = true;
	LOG_INFO( log::channel_t::CFG, "config system initialized." );
	return cfg_error_t::OK;
}

/*
================
CypherConfig_Shutdown
================
*/
cfg_error_t CypherConfig_Shutdown() {
	if ( !s_CfgRuntimeState.initialized ) {
		LOG_WARNING( log::channel_t::CFG, "config system shutdown requested while not initialized." );
		return cfg_error_t::ERR_NOT_INIT;
	}
	LOG_INFO( log::channel_t::CFG, "config system shutdown." );
	s_CfgRuntimeState = {};
	return cfg_error_t::OK;
}

/*
================
CypherConfig_LoadFile

Loads a cfg file through FS and executes it one line at a time.
================
*/
cfg_error_t CypherConfig_LoadFile( const char *path, const bool required ) {
	if ( path == nullptr || path[0] == '\0' ) {
		LOG_ERROR( log::channel_t::CFG, "config load failed: invalid path." );
		return cfg_error_t::ERR_INVALID_PATH;
	}
	if ( !s_CfgRuntimeState.initialized ) {
		LOG_ERROR( log::channel_t::CFG, "config load failed for '%s': config system is not initialized.", path );
		return cfg_error_t::ERR_NOT_INIT;
	}

    fs::file_t file{};

    const fs::fs_error_t openResult = fs::FS_Open( path, fs::open_mode_t::READ_TEXT, file );
    if ( openResult != fs::fs_error_t::OK ) {
        if ( required ) {
            LOG_ERROR( log::channel_t::CFG, "required config '%s' failed to open: %s.", path, fs::FS_ErrorDesc( openResult ) );
        } else {
            LOG_DEBUG( log::channel_t::CFG, "optional config '%s' not loaded: %s.", path, fs::FS_ErrorDesc( openResult ) );
        }
        return required ? cfg_error_t::ERR_FILE_OPEN_FAILED : cfg_error_t::OK;
    }

    if ( file.size == 0u ) {
        fs::FS_Close( file );
        LOG_INFO( log::channel_t::CFG, "config '%s' loaded: empty file.", path );
        return cfg_error_t::OK;
    }

    if ( file.size >= CYPHER_CONFIG_MAX_FILE_SIZE ) {
        fs::FS_Close( file );
        LOG_ERROR( log::channel_t::CFG, "config '%s' failed: file too large (%llu bytes).",
                          path,
                          static_cast<unsigned long long>( file.size ) );
        return cfg_error_t::ERR_IO_ERROR;
    }

    char buffer[CYPHER_CONFIG_MAX_FILE_SIZE]{};
    common::u64 nBytesRead{};

    const fs::fs_error_t readResult = fs::FS_Read( file, buffer, file.size, nBytesRead );
    const fs::fs_error_t closeResult = fs::FS_Close( file );

    if ( readResult != fs::fs_error_t::OK ) {
        LOG_ERROR( log::channel_t::CFG, "config '%s' failed: read failed: %s.", path, fs::FS_ErrorDesc( readResult ) );
        return cfg_error_t::ERR_IO_ERROR;
    }

    if ( closeResult != fs::fs_error_t::OK ) {
        LOG_ERROR( log::channel_t::CFG, "config '%s' failed: close failed: %s.", path, fs::FS_ErrorDesc( closeResult ) );
        return cfg_error_t::ERR_IO_ERROR;
    }

    buffer[nBytesRead] = '\0';
    cfg_error_t result = cfg_error_t::OK;

    char *szLineStart = buffer;

    while ( *szLineStart != '\0' ) {
        char *szLineEnd = szLineStart;

        while ( *szLineEnd != '\0' && *szLineEnd != '\n' && *szLineEnd != '\r' ) {
            ++szLineEnd;
        }

        char szSaveLineEnd = *szLineEnd;
        *szLineEnd = '\0';

        const cfg_error_t lineResult = CypherConfig_ExecuteLine( szLineStart );

        if ( lineResult != cfg_error_t::OK && result == cfg_error_t::OK ) {
            LOG_WARNING( log::channel_t::CFG, "config '%s' line failed: %s.", path, CypherConfig_ErrorDesc( lineResult ) );
            result = lineResult;
        }

        if ( szSaveLineEnd == '\0' ) {
            break;
        }

        szLineStart = szLineEnd + 1;

        // Skip the second byte of Windows CRLF line endings.
        if ( szSaveLineEnd == '\r' && *szLineStart == '\n' ) {
            ++szLineStart;
        }
    }

    if ( result == cfg_error_t::OK ) {
        LOG_INFO( log::channel_t::CFG, "config '%s' loaded successfully (%llu bytes).",
                         path,
                         static_cast<unsigned long long>( nBytesRead ) );
    }

    return result;
}

/*
================
CypherConfig_LoadDefault
================
*/
cfg_error_t CypherConfig_LoadDefault() {
	if ( !s_CfgRuntimeState.initialized ) {
		return cfg_error_t::ERR_NOT_INIT;
	}
	return CypherConfig_LoadFile( CYPHER_CONFIG_DEFAULT_PATH, true );
}

/*
================
CypherConfig_LoadAutoexec
================
*/
cfg_error_t CypherConfig_LoadAutoexec() {
	if ( !s_CfgRuntimeState.initialized ) {
		return cfg_error_t::ERR_NOT_INIT;
	}
	return CypherConfig_LoadFile( CYPHER_CONFIG_AUTOEXEC_PATH, false );
}

/*
================
CypherConfig_ExecuteLine

Executes one trimmed cfg line: exec, set/seta, or regular command.
================
*/
cfg_error_t CypherConfig_ExecuteLine( const char *nCommandLine ) {
	if ( !s_CfgRuntimeState.initialized ) {
		return cfg_error_t::ERR_INVALID_LINE;
	}

    if ( nCommandLine == nullptr ) {
        return cfg_error_t::ERR_INVALID_LINE;
    }
	char line[CYPHER_CONFIG_MAX_LINE_LENGTH] {};
	std::strncpy( line, nCommandLine, sizeof( line ) - 1 );

	char *cursor = line;
	while ( std::isspace( static_cast<unsigned char>( *cursor ) ) ) {
		++cursor;
	}
	if ( *cursor == '\0' || *cursor == '\n' || *cursor == '\r' ) {
		return cfg_error_t::OK;
	}
	bool inQuotes = false;
	for ( char *it = cursor; *it != '\0'; ++it ) {
		// Comments are ignored unless they are inside quoted values.
		if ( *it == '"' ) {
			inQuotes = !inQuotes;
		}
		if ( !inQuotes && it[0] == '/' && it[1] == '/' ) {
			*it = '\0';
			break;
		}
		if ( !inQuotes && *it == '#' ) {
			*it = '\0';
			break;
		}
	}
	char *end = cursor + std::strlen( cursor );
	while ( end > cursor && std::isspace( static_cast<unsigned char>( end[-1] ) ) ) {
		--end;
	}
	*end = '\0';
	if ( *cursor == '\0' ) {
		return cfg_error_t::OK;
	}
	char command[32]{};
	common::u32 i = 0;
	while ( *cursor != '\0' && !std::isspace( static_cast<unsigned char>( *cursor ) ) && ( i + 1u ) < sizeof( command ) ) {
		command[i++] = *cursor++;
	}
	command[i] = '\0';
	if ( std::strcmp( command, "exec" ) == 0 ) {
		while ( std::isspace( static_cast<unsigned char>( *cursor ) ) ) {
			++cursor;
		}
		if ( *cursor == '\0' ) {
			return cfg_error_t::ERR_PARSE_FAILED;
		}
		char szExecPath[CYPHER_CONFIG_MAX_PATH_LENGTH]{};
		i = 0;
		if ( *cursor == '"' ) {
			++cursor;
			while ( cursor[i] != '\0' && cursor[i] != '"' && ( i + 1u ) < sizeof( szExecPath ) ) {
				szExecPath[i] = cursor[i];
				++i;
			}
			if ( cursor[i] != '"' ) {
				return cfg_error_t::ERR_PARSE_FAILED;
			}
		} else {
			while ( cursor[i] != '\0' && !std::isspace( static_cast<unsigned char>( cursor[i] ) ) && ( i + 1u ) < sizeof( szExecPath ) ) {
				szExecPath[i] = cursor[i];
				++i;
			}
		}
		szExecPath[i] = '\0';
		return CypherConfig_LoadFile( szExecPath, false );
	}
	if ( std::strcmp( command, "set" ) == 0 || std::strcmp( command, "seta" ) == 0 ) {
		while ( std::isspace( static_cast<unsigned char>( *cursor ) ) ) {
			++cursor;
		}
		if ( *cursor == '\0' ) {
			return cfg_error_t::ERR_PARSE_FAILED;
		}
		char szCvarName[256]{};
		i = 0;
		while ( *cursor != '\0' && !std::isspace( static_cast<unsigned char>( *cursor ) ) && ( i + 1u ) < sizeof( szCvarName ) ) {
			szCvarName[i++] = *cursor++;
		}
		szCvarName[i] = '\0';

		if ( szCvarName[0] == '\0' ) {
			return cfg_error_t::ERR_PARSE_FAILED;
		}
		while ( std::isspace( static_cast<unsigned char>( *cursor ) ) ) {
			++cursor;
		}
		if ( *cursor == '\0' ) {
			return cfg_error_t::ERR_PARSE_FAILED;
		}
		char cvarValue[CYPHER_CONFIG_MAX_LINE_LENGTH]{};
		i = 0;
		if ( *cursor == '"' ) {
			++cursor;
			while ( cursor[i] != '\0' && cursor[i] != '"' && ( i + 1u ) < sizeof( cvarValue ) ) {
				cvarValue[i] = cursor[i];
				++i;
			}
			if ( cursor[i] != '"' ) {
				return cfg_error_t::ERR_PARSE_FAILED;
			}
		} else {
			while ( cursor[i] != '\0' && !std::isspace( static_cast<unsigned char>( cursor[i] ) ) && ( i + 1u ) < sizeof( cvarValue ) ) {
				cvarValue[i] = cursor[i];
				++i;
			}
		}
		cvarValue[i] = '\0';
		if ( cvarValue[0] == '\0' ) {
			return cfg_error_t::ERR_PARSE_FAILED;
		}
		if ( cvar::Cvar_Set( szCvarName, cvarValue ) != cvar::cvar_error_t::OK ) {
			return cfg_error_t::ERR_PARSE_FAILED;
		}
		return cfg_error_t::OK;
	}
	if ( cmd::Cmd_Execute( line ) != cmd::cmd_error_t::OK ) {
		return cfg_error_t::ERR_COMMAND_FAILED;
	}
	return cfg_error_t::OK;
}

} // namespace cypher::engine::cfg
