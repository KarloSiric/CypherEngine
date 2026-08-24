//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/Picasso/Gui/PicassoIcons.h
//  Purpose: Declares Picasso's platform-independent icon factory.
//  Details: Lucide SVG resources are recolored into normal, hover, selected,
//           and disabled pixmaps so every host platform presents the same
//           compact authoring-tool vocabulary.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_PICASSO_ICONS_H
#define CYPHER_TOOLS_PICASSO_ICONS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include <QIcon>
#include <QStringView>

namespace cypher::tools::picasso
{

// Tool families use restrained color cues like traditional level and asset
// editors. Selection state remains orange regardless of the family's idle tone.
enum class picasso_icon_tone_t : unsigned char
{
    STEEL,
    BLUE,
    TEAL,
    GOLD,
    GREEN,
    ORANGE,
    RED
};

// Builds a multi-state icon from a bundled Lucide resource. The logical name
// omits both the resource prefix and the .svg extension.
[[nodiscard]] QIcon PicassoIcon_Create(
    QStringView name,
    picasso_icon_tone_t tone = picasso_icon_tone_t::STEEL );

} // namespace cypher::tools::picasso

#endif // CYPHER_TOOLS_PICASSO_ICONS_H
