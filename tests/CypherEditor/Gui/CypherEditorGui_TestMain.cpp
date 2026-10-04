//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_TestMain.cpp
//  Purpose: Test entry point for the Qt editor framework.
//  Details: Widgets need a QApplication for the whole run, so this replaces
//           Catch2's main. The offscreen platform keeps tests headless;
//           CTest sets it too, this covers running the binary directly.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include <catch2/catch_session.hpp>

#include <QApplication>

int main( int argc, char **argv )
{
    if ( qEnvironmentVariableIsEmpty( "QT_QPA_PLATFORM" ) ) { qputenv( "QT_QPA_PLATFORM", "offscreen" ); }
    QApplication application( argc, argv );
    return Catch::Session().run( argc, argv );
}
