// CypherEngine Source Code. Copyright (c) 2026 Karlo Siric. All rights reserved.
#ifndef CYPHER_MASON_BRANDING_H
#define CYPHER_MASON_BRANDING_H

#include "CypherEditorGui_Style.h"
#include <QIcon>
#include <QPixmap>

class QSplashScreen;

namespace cypher::mason
{
const char *Mason_Version() noexcept;
QIcon Mason_ApplicationIcon();
// Supplied Mason artwork and a theme-derived construction drawing. No
// startup timer or artificial delay: the screen shows real startup stages.
QPixmap Mason_StartupImage( const editor::gui::editor_style_t &style, qreal devicePixelRatio = 1.0 );
QSplashScreen *Mason_StartupScreen( const editor::gui::editor_style_t &style );
}
#endif
