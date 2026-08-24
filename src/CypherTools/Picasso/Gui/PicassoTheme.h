//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/Picasso/Gui/PicassoTheme.h
//  Purpose: Declares Picasso's shared Qt visual theme.
//  Details: The theme centralizes the compact dark palette used by the standalone
//           application and, later, the same workspace embedded inside Mason.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_PICASSO_THEME_H
#define CYPHER_TOOLS_PICASSO_THEME_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

class QApplication;

namespace cypher::tools::picasso
{

void PicassoTheme_Apply( QApplication &application );

} // namespace cypher::tools::picasso

#endif // CYPHER_TOOLS_PICASSO_THEME_H
