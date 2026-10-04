//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_Log.cpp
//  Purpose: Implements the editor's log store and its Tier0 sink.
//
//  History:
//  - Created by Karlo Siric on 2026-09-30
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_Log.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"

#include <QCoreApplication>
#include <QPointer>
#include <QThread>

#include <algorithm>
#include <cstdio>
#include <mutex>

namespace cypher::editor::gui
{

using namespace cypher::common;

namespace
{

// The installed store. Guarded because producers may be any thread; the
// store itself is touched on the GUI thread only.
struct sink_state_t {
    std::mutex mutex;
    editor_log_t *pLog{ nullptr };
};

sink_state_t &SinkState()
{
    static sink_state_t state;
    return state;
}

void SinkCallback( const log_record_t &record, void *pUserData ) noexcept
{
    editor_log_t *pLog = nullptr;
    log_callback_t pfnPrevious = nullptr;
    void *pPreviousUserData = nullptr;
    {
        sink_state_t &state = SinkState();
        std::lock_guard<std::mutex> lock( state.mutex );
        pLog = state.pLog == pUserData ? state.pLog : nullptr;
        if ( pLog != nullptr ) {
            pfnPrevious = pLog->pfnPrevious;
            pPreviousUserData = pLog->pPreviousUserData;
        }
    }
    // The terminal keeps everything it printed before the editor started.
    if ( pfnPrevious != nullptr ) {
        pfnPrevious( record, pPreviousUserData );
    } else {
        std::fprintf( stderr, "[%s][%s] %s\n", Cy_LogLevelName( record.level ), Cy_LogChannelName( record.channel ), record.pMessage );
    }
    if ( pLog == nullptr ) { return; }
    const log_level_t level = record.level;
    const log_channel_t channel = record.channel;
    const QString message = QString::fromUtf8( record.pMessage );
    const QString file = Cy_SourceLocation_IsValid( record.location ) ? QString::fromUtf8( record.location.pFile ) : QString();
    const int line = static_cast<int>( record.location.line );
    QCoreApplication *pApp = QCoreApplication::instance();
    if ( pApp == nullptr || QThread::currentThread() == pApp->thread() ) {
        EditorLog_Append( pLog, level, channel, message, file.isEmpty() ? nullptr : file.toUtf8().constData(), line );
        return;
    }
    // Another thread: append on the GUI thread, if the store is still the
    // installed one when the call runs.
    QMetaObject::invokeMethod( pApp, [pLog, level, channel, message, file, line]() {
        {
            sink_state_t &state = SinkState();
            std::lock_guard<std::mutex> lock( state.mutex );
            if ( state.pLog != pLog ) { return; }
        }
        const QByteArray fileUtf8 = file.toUtf8();
        EditorLog_Append( pLog, level, channel, message, file.isEmpty() ? nullptr : fileUtf8.constData(), line );
    }, Qt::QueuedConnection );
}

} // namespace

void EditorLog_Init( editor_log_t *pLog )
{
    CY_ASSERT( pLog != nullptr );
    pLog->entries.clear();
    pLog->entries.reserve( 1024 );
    pLog->nDropped = 0u;
    std::fill( std::begin( pLog->counts ), std::end( pLog->counts ), usize{ 0u } );
    pLog->nListeners = 0u;
    pLog->clock.start();
}

void EditorLog_Install( editor_log_t *pLog )
{
    CY_ASSERT( pLog != nullptr && !pLog->bInstalled );
    Cy_LogGetCallback( &pLog->pfnPrevious, &pLog->pPreviousUserData );
    {
        sink_state_t &state = SinkState();
        std::lock_guard<std::mutex> lock( state.mutex );
        state.pLog = pLog;
    }
    pLog->bInstalled = true;
    Cy_LogSetCallback( SinkCallback, pLog );
}

void EditorLog_Uninstall( editor_log_t *pLog )
{
    if ( pLog == nullptr || !pLog->bInstalled ) { return; }
    {
        sink_state_t &state = SinkState();
        std::lock_guard<std::mutex> lock( state.mutex );
        // The store installed before this one becomes the target again when
        // it was the previous sink.
        state.pLog = pLog->pfnPrevious == SinkCallback ? static_cast<editor_log_t *>( pLog->pPreviousUserData ) : nullptr;
    }
    Cy_LogSetCallback( pLog->pfnPrevious, pLog->pPreviousUserData );
    pLog->bInstalled = false;
}

void EditorLog_Append( editor_log_t *pLog, log_level_t level, log_channel_t channel, const QString &message, const char *pFile, int line,
                       bool bCommand )
{
    CY_ASSERT( pLog != nullptr );
    editor_log_entry_t entry{};
    entry.msTime = pLog->clock.isValid() ? pLog->clock.elapsed() : 0;
    entry.level = level;
    entry.channel = channel;
    entry.message = message;
    entry.file = pFile != nullptr ? QString::fromUtf8( pFile ) : QString();
    entry.line = line;
    entry.bCommand = bCommand;
    if ( pLog->entries.size() >= EDITOR_LOG_MAX_ENTRIES ) {
        pLog->entries.removeFirst();
        ++pLog->nDropped;
    }
    pLog->entries.append( entry );
    if ( level < log_level_t::Count ) { ++pLog->counts[static_cast<usize>( level )]; }
    // A listener may unsubscribe while being told.
    editor_log_listener_t snapshot[EDITOR_LOG_MAX_LISTENERS];
    const usize nListeners = pLog->nListeners;
    std::copy( pLog->listeners, pLog->listeners + nListeners, snapshot );
    for ( usize i = 0u; i < nListeners; ++i ) { snapshot[i].pfnChanged( snapshot[i].pContext, &pLog->entries.constLast() ); }
}

void EditorLog_Clear( editor_log_t *pLog )
{
    CY_ASSERT( pLog != nullptr );
    pLog->entries.clear();
    pLog->nDropped = 0u;
    std::fill( std::begin( pLog->counts ), std::end( pLog->counts ), usize{ 0u } );
    editor_log_listener_t snapshot[EDITOR_LOG_MAX_LISTENERS];
    const usize nListeners = pLog->nListeners;
    std::copy( pLog->listeners, pLog->listeners + nListeners, snapshot );
    for ( usize i = 0u; i < nListeners; ++i ) { snapshot[i].pfnChanged( snapshot[i].pContext, nullptr ); }
}

usize EditorLog_Count( const editor_log_t *pLog, log_level_t level ) noexcept
{
    return pLog != nullptr && level < log_level_t::Count ? pLog->counts[static_cast<usize>( level )] : 0u;
}

usize EditorLog_ProblemCount( const editor_log_t *pLog ) noexcept
{
    return EditorLog_Count( pLog, log_level_t::Warning ) + EditorLog_Count( pLog, log_level_t::Error ) + EditorLog_Count( pLog, log_level_t::Fatal );
}

bool EditorLog_AddListener( editor_log_t *pLog, editor_log_listener_fn pfnChanged, void *pContext ) noexcept
{
    CY_ASSERT( pLog != nullptr && pfnChanged != nullptr );
    if ( pLog == nullptr || pfnChanged == nullptr || pLog->nListeners >= EDITOR_LOG_MAX_LISTENERS ) { return false; }
    pLog->listeners[pLog->nListeners++] = editor_log_listener_t{ pfnChanged, pContext };
    return true;
}

void EditorLog_RemoveListener( editor_log_t *pLog, editor_log_listener_fn pfnChanged, void *pContext ) noexcept
{
    if ( pLog == nullptr ) { return; }
    for ( usize i = 0u; i < pLog->nListeners; ++i ) {
        if ( pLog->listeners[i].pfnChanged != pfnChanged || pLog->listeners[i].pContext != pContext ) { continue; }
        for ( usize j = i + 1u; j < pLog->nListeners; ++j ) { pLog->listeners[j - 1u] = pLog->listeners[j]; }
        --pLog->nListeners;
        return;
    }
}

const char *EditorLog_LevelTag( log_level_t level ) noexcept
{
    switch ( level ) {
        case log_level_t::Trace: return "TRACE";
        case log_level_t::Debug: return "DEBUG";
        case log_level_t::Info: return "INFO";
        case log_level_t::Warning: return "WARN";
        case log_level_t::Error: return "ERROR";
        case log_level_t::Fatal: return "FATAL";
        case log_level_t::Count: break;
    }
    return "?";
}

} // namespace cypher::editor::gui
