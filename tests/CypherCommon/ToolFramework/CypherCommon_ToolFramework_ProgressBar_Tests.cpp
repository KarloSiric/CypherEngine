//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/ToolFramework/CypherCommon_ToolFramework_ProgressBar_Tests.cpp
//  Purpose: Verifies shared tool progress-state helpers.
//  Details: Tests initialization, bounded progress, indeterminate totals,
//           completion, and null-state handling.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ProgressBar.h"

#include <catch2/catch_test_macros.hpp>

namespace cypher::common
{

TEST_CASE( "ProgressBar tracks bounded work" )
{
    progress_bar_t progress{};

    ProgressBar_Begin( &progress, "Cook assets", 100u );
    CHECK( progress.pTitle != nullptr );
    CHECK( progress.pTitle[0] == 'C' );
    CHECK( progress.total_work == 100u );
    CHECK( progress.completed_work == 0u );

    ProgressBar_Update( &progress, 40u );
    CHECK( progress.completed_work == 40u );

    ProgressBar_Update( &progress, 120u );
    CHECK( progress.completed_work == 100u );

    ProgressBar_End( &progress );
    CHECK( progress.completed_work == progress.total_work );
}

TEST_CASE( "ProgressBar accepts unknown totals and null calls" )
{
    progress_bar_t progress{};

    ProgressBar_Begin( &progress, nullptr, 0u );
    CHECK( progress.pTitle != nullptr );
    CHECK( progress.pTitle[0] == '\0' );

    ProgressBar_Update( &progress, 25u );
    CHECK( progress.completed_work == 25u );

    CHECK_NOTHROW( ProgressBar_Begin( nullptr, nullptr, 0u ) );
    CHECK_NOTHROW( ProgressBar_Update( nullptr, 0u ) );
    CHECK_NOTHROW( ProgressBar_End( nullptr ) );
}

} // namespace cypher::common
