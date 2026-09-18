//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code
// Copyright (c) 2026 Karlo Siric. All rights reserved.
// Purpose: Validates readable editor.ini preferences and saves them atomically.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileEditorConfig.h"
#include "CypherTileEditorPreferenceFields.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <cmath>

namespace cypher::tools::tile_editor
{
namespace
{
constexpr qint64 CONFIG_MAX_BYTES = 256 * 1024;

QString ConfigKey( const char *section, const char *key )
{
    return QString::fromLatin1( section ) + QLatin1Char( '/' ) + QString::fromLatin1( key );
}

QString QuotedValue( QString value )
{
    value.replace( QLatin1Char( '\\' ), QStringLiteral( "\\\\" ) );
    value.replace( QLatin1Char( '"' ), QStringLiteral( "\\\"" ) );
    value.replace( QLatin1Char( '\n' ), QStringLiteral( "\\n" ) );
    value.replace( QLatin1Char( '\r' ), QStringLiteral( "\\r" ) );
    return QLatin1Char( '"' ) + value + QLatin1Char( '"' );
}
} // namespace

QString TileEditorConfig_DefaultPath()
{
    return QDir( QStandardPaths::writableLocation( QStandardPaths::AppConfigLocation ) )
        .filePath( QStringLiteral( "editor.ini" ) );
}

bool TileEditorConfig_Load(
    const QString &path, tile_editor_preferences_t &inOutPreferences, QString &error )
{
    error.clear();
    if ( path.isEmpty() ) {
        error = QStringLiteral( "Editor configuration path is empty." );
        return false;
    }
    if ( !QFileInfo::exists( path ) ) return true;
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        error = QStringLiteral( "Cannot read editor configuration %1: %2" ).arg( path, file.errorString() );
        return false;
    }
    if ( file.size() > CONFIG_MAX_BYTES || !QFileInfo( file ).isFile() ) {
        error = QStringLiteral( "Editor configuration must be a regular file smaller than 256 KiB: %1" ).arg( path );
        return false;
    }
    file.close();
    QSettings settings( path, QSettings::IniFormat );
    settings.setFallbacksEnabled( false );
    // Force the full INI parse before accepting any fields.
    (void)settings.allKeys();
    if ( settings.status() != QSettings::NoError ) {
        error = QStringLiteral( "Malformed or unreadable editor configuration: %1" ).arg( path );
        return false;
    }
    bool valid = false;
    const int version = settings.value( QStringLiteral( "Editor/schemaVersion" ) ).toInt( &valid );
    if ( !valid || version != 1 ) {
        error = QStringLiteral( "Editor configuration requires [Editor] schemaVersion=1: %1" ).arg( path );
        return false;
    }

    auto candidate = inOutPreferences;
    auto reject = [&]( const QString &key, const QString &expected ) {
        error = QStringLiteral( "Invalid editor setting %1; expected %2. Configuration was not applied." )
            .arg( key, expected );
        return false;
    };
    for ( const auto &field : preference_fields::colors ) {
        const QString key = ConfigKey( field.section, field.key );
        if ( !settings.contains( key ) ) continue;
        const QColor color( settings.value( key ).toString().trimmed() );
        if ( !color.isValid() ) return reject( key, QStringLiteral( "a color such as #19242f" ) );
        candidate.*field.member = color;
    }
    for ( const auto &field : preference_fields::integers ) {
        const QString key = ConfigKey( field.section, field.key );
        if ( !settings.contains( key ) ) continue;
        const int value = settings.value( key ).toInt( &valid );
        if ( !valid ) return reject( key, QStringLiteral( "an integer" ) );
        candidate.*field.member = value;
    }
    for ( const auto &field : preference_fields::reals ) {
        const QString key = ConfigKey( field.section, field.key );
        if ( !settings.contains( key ) ) continue;
        const double value = settings.value( key ).toDouble( &valid );
        if ( !valid || !std::isfinite( value ) ) return reject( key, QStringLiteral( "a finite number" ) );
        candidate.*field.member = value;
    }
    for ( const auto &field : preference_fields::booleans ) {
        const QString key = ConfigKey( field.section, field.key );
        if ( !settings.contains( key ) ) continue;
        const QString value = settings.value( key ).toString().trimmed().toLower();
        if ( value == QStringLiteral( "true" ) || value == QStringLiteral( "1" ) )
            candidate.*field.member = true;
        else if ( value == QStringLiteral( "false" ) || value == QStringLiteral( "0" ) )
            candidate.*field.member = false;
        else return reject( key, QStringLiteral( "true or false" ) );
    }
    for ( const auto &definition : TileEditorShortcutDefinitions() ) {
        const QString key = QStringLiteral( "Shortcuts/" ) + definition.id;
        if ( !settings.contains( key ) ) continue;
        const QString portable = settings.value( key ).toString();
        const QKeySequence sequence = QKeySequence::fromString( portable, QKeySequence::PortableText );
        if ( !portable.isEmpty() && ( sequence.isEmpty() || sequence.toString( QKeySequence::PortableText ).isEmpty() ) )
            return reject( key, QStringLiteral( "a portable shortcut such as Ctrl+Shift+F, or an empty string" ) );
        for ( int i = 0; i < sequence.count(); ++i ) {
            if ( sequence[i].key() == Qt::Key_unknown )
                return reject( key, QStringLiteral( "a valid keyboard key" ) );
        }
        candidate.shortcuts.insert( definition.id, sequence );
    }
    QMap<QString, QString> assignedShortcuts;
    for ( const auto &definition : TileEditorShortcutDefinitions() ) {
        const auto sequence = candidate.shortcuts.value( definition.id, definition.defaultSequence );
        if ( sequence.isEmpty() ) continue;
        const QString portable = sequence.toString( QKeySequence::PortableText );
        if ( assignedShortcuts.contains( portable ) ) {
            error = QStringLiteral( "Shortcut %1 is assigned to both %2 and %3. Configuration was not applied." )
                .arg( portable, assignedShortcuts.value( portable ), definition.id );
            return false;
        }
        assignedShortcuts.insert( portable, definition.id );
    }
    inOutPreferences = TileEditorPreferences_Normalize( candidate );
    return true;
}

bool TileEditorConfig_Save(
    const QString &path, const tile_editor_preferences_t &preferences, QString &error )
{
    error.clear();
    if ( path.isEmpty() ) {
        error = QStringLiteral( "Editor configuration path is empty." );
        return false;
    }
    if ( !QDir().mkpath( QFileInfo( path ).absolutePath() ) ) {
        error = QStringLiteral( "Cannot create the editor configuration directory: %1" ).arg( path );
        return false;
    }
    const auto normalized = TileEditorPreferences_Normalize( preferences );
    QString output = QStringLiteral(
        "; CypherTileEditor preferences. Edit, then use Edit > Reload Editor Configuration.\n"
        "; Saving Settings rewrites this file. Native dock/window layout is stored separately.\n"
        "; Colors accept #RRGGBB or #AARRGGBB. Numeric values clamp to supported ranges.\n"
        "; Adaptive grid changes displayed spacing only; the map editing cell size is unchanged.\n\n"
        "[Editor]\nschemaVersion=1\n" );
    for ( const char *section : { "Appearance", "Viewport", "Grid", "Camera", "Workspace", "Map" } ) {
        output += QStringLiteral( "\n[%1]\n" ).arg( QString::fromLatin1( section ) );
        const QString sectionName = QString::fromLatin1( section );
        auto line = [&]( const char *key, const QString &value ) {
            output += QString::fromLatin1( key ) + QLatin1Char( '=' ) + value + QLatin1Char( '\n' );
        };
        for ( const auto &field : preference_fields::colors ) {
            if ( sectionName == QLatin1String( field.section ) )
                line( field.key, QuotedValue( ( normalized.*field.member ).name( QColor::HexArgb ) ) );
        }
        for ( const auto &field : preference_fields::integers ) {
            if ( sectionName == QLatin1String( field.section ) ) {
                output += QStringLiteral( "; Range %1..%2\n" ).arg( field.minimum ).arg( field.maximum );
                line( field.key, QString::number( normalized.*field.member ) );
            }
        }
        for ( const auto &field : preference_fields::reals ) {
            if ( sectionName == QLatin1String( field.section ) ) {
                output += QStringLiteral( "; Range %1..%2\n" ).arg( field.minimum ).arg( field.maximum );
                line( field.key, QString::number( normalized.*field.member, 'g', 15 ) );
            }
        }
        for ( const auto &field : preference_fields::booleans ) {
            if ( sectionName == QLatin1String( field.section ) )
                line( field.key, normalized.*field.member ? QStringLiteral( "true" ) : QStringLiteral( "false" ) );
        }
    }
    output += QStringLiteral( "\n[Shortcuts]\n; Portable Qt shortcut names. Empty quotes disable a command shortcut.\n" );
    for ( const auto &definition : TileEditorShortcutDefinitions() ) {
        const auto sequence = normalized.shortcuts.value( definition.id, definition.defaultSequence );
        output += definition.id + QLatin1Char( '=' ) +
            QuotedValue( sequence.toString( QKeySequence::PortableText ) ) + QLatin1Char( '\n' );
    }

    QSaveFile file( path );
    // Keep atomic replacement mandatory; never truncate an existing config as fallback.
    file.setDirectWriteFallback( false );
    if ( !file.open( QIODevice::WriteOnly ) ) {
        error = QStringLiteral( "Cannot save editor configuration %1: %2" ).arg( path, file.errorString() );
        return false;
    }
    const QByteArray bytes = output.toUtf8();
    if ( file.write( bytes ) != bytes.size() || !file.commit() ) {
        error = QStringLiteral( "Cannot commit editor configuration %1: %2" ).arg( path, file.errorString() );
        return false;
    }
    return true;
}

bool TileEditorConfig_MigrateIndependentOrthographicCameras(
    QSettings &nativeSettings,
    const QString &configurationPath,
    tile_editor_preferences_t &inOutPreferences,
    bool &outMigrated,
    QString &error )
{
    static const QString marker = QStringLiteral(
        "TileEditor/independentOrthographicCamerasV1" );
    outMigrated = false;
    error.clear();
    if ( nativeSettings.value( marker, false ).toBool() ) return true;

    // Refuse to replace a configuration that cannot be parsed. Main-window
    // startup already performs this check, but keeping the migration itself
    // transactional prevents future callers from silently repairing a user's
    // invalid file by overwriting it.
    auto candidate = inOutPreferences;
    if ( !TileEditorConfig_Load( configurationPath, candidate, error ) )
        return false;
    candidate.linkOrthographicCameras = false;
    if ( !TileEditorConfig_Save( configurationPath, candidate, error ) )
        return false;

    // The readable INI is canonical. Publish the same value to Qt's native
    // cache only after its atomic replacement succeeds.
    inOutPreferences = candidate;
    TileEditorPreferences_Save( nativeSettings, candidate );
    nativeSettings.setValue( marker, true );
    nativeSettings.sync();
    if ( nativeSettings.status() != QSettings::NoError ) {
        error = QStringLiteral(
            "The independent-view migration updated %1, but Qt could not update its native preferences cache." )
                    .arg( configurationPath );
        return false;
    }
    outMigrated = true;
    return true;
}
} // namespace cypher::tools::tile_editor
