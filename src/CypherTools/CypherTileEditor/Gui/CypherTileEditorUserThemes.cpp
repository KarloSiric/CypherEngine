//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code
// Copyright (c) 2026 Karlo Siric. All rights reserved.
// Purpose: Validates and atomically stores portable color themes.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileEditorUserThemes.h"

#include "CypherTileEditorPreferenceFields.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QRegularExpression>

#include <algorithm>

namespace cypher::tools::tile_editor
{
namespace
{
constexpr qint64 THEME_MAX_BYTES = 64 * 1024;
constexpr int THEME_NAME_MAX_LENGTH = 80;
const QRegularExpression THEME_COLOR_PATTERN(
    QStringLiteral( "^#[0-9A-Fa-f]{6}$" ) );

QString QuotedValue( QString value )
{
    value.replace( QLatin1Char( '\\' ), QStringLiteral( "\\\\" ) );
    value.replace( QLatin1Char( '"' ), QStringLiteral( "\\\"" ) );
    value.replace( QLatin1Char( '\n' ), QStringLiteral( "\\n" ) );
    value.replace( QLatin1Char( '\r' ), QStringLiteral( "\\r" ) );
    return QLatin1Char( '"' ) + value + QLatin1Char( '"' );
}

QString NormalizedThemeName( const QString &name )
{
    return name.simplified().left( THEME_NAME_MAX_LENGTH );
}

bool IsAcceptableThemeName( const QString &name )
{
    const QString normalized = NormalizedThemeName( name );
    return !normalized.isEmpty() && normalized == name.simplified() &&
        name.size() <= THEME_NAME_MAX_LENGTH &&
        !name.contains( QChar::Null );
}
} // namespace

void TileEditorTheme_CopyColors(
    tile_editor_preferences_t &destination,
    const tile_editor_preferences_t &source )
{
    const auto normalized = TileEditorPreferences_Normalize( source );
    for ( const auto &field : preference_fields::colors )
        destination.*field.member = normalized.*field.member;
}

QString TileEditorUserThemes_DefaultDirectory()
{
    return QDir( QStandardPaths::writableLocation( QStandardPaths::AppConfigLocation ) )
        .filePath( QStringLiteral( "themes" ) );
}

QString TileEditorUserThemes_PathForName(
    const QString &directory,
    const QString &name )
{
    QString stem = NormalizedThemeName( name ).toLower();
    QString safe;
    safe.reserve( stem.size() );
    bool previousWasSeparator = false;
    for ( const QChar character : stem ) {
        const bool allowed = character.isLetterOrNumber();
        if ( allowed ) {
            safe += character;
            previousWasSeparator = false;
        } else if ( !previousWasSeparator && !safe.isEmpty() ) {
            safe += QLatin1Char( '-' );
            previousWasSeparator = true;
        }
    }
    while ( safe.endsWith( QLatin1Char( '-' ) ) ) safe.chop( 1 );
    if ( safe.isEmpty() ) safe = QStringLiteral( "custom-theme" );
    return QDir( directory ).filePath( safe + QStringLiteral( ".cytheme" ) );
}

bool TileEditorUserTheme_Load(
    const QString &path,
    tile_editor_user_theme_t &outTheme,
    QString &error )
{
    error.clear();
    if ( path.isEmpty() ) {
        error = QStringLiteral( "Theme path is empty." );
        return false;
    }
    QFile file( path );
    const QFileInfo info( file );
    if ( !info.exists() || !info.isFile() ) {
        error = QStringLiteral( "Theme is not a regular file: %1" ).arg( path );
        return false;
    }
    if ( !file.open( QIODevice::ReadOnly ) ) {
        error = QStringLiteral( "Cannot read theme %1: %2" ).arg( path, file.errorString() );
        return false;
    }
    if ( file.size() > THEME_MAX_BYTES ) {
        error = QStringLiteral( "Theme must be smaller than 64 KiB: %1" ).arg( path );
        return false;
    }
    file.close();

    QSettings settings( path, QSettings::IniFormat );
    settings.setFallbacksEnabled( false );
    (void)settings.allKeys();
    if ( settings.status() != QSettings::NoError ) {
        error = QStringLiteral( "Malformed or unreadable theme: %1" ).arg( path );
        return false;
    }
    bool validVersion = false;
    const int version = settings.value( QStringLiteral( "Theme/schemaVersion" ) )
                            .toInt( &validVersion );
    if ( !validVersion || version != 1 ) {
        error = QStringLiteral( "Theme requires [Theme] schemaVersion=1: %1" ).arg( path );
        return false;
    }
    const QString name = settings.value( QStringLiteral( "Theme/name" ) ).toString();
    if ( !IsAcceptableThemeName( name ) ) {
        error = QStringLiteral( "Theme name must contain 1 to 80 visible characters: %1" ).arg( path );
        return false;
    }

    tile_editor_user_theme_t candidate;
    candidate.name = name;
    candidate.path = info.absoluteFilePath();
    for ( const auto &field : preference_fields::colors ) {
        const QString key = QStringLiteral( "Colors/" ) + QString::fromLatin1( field.key );
        if ( !settings.contains( key ) ) {
            error = QStringLiteral( "Theme is missing required color %1: %2" ).arg( key, path );
            return false;
        }
        const QString encoded = settings.value( key ).toString().trimmed();
        if ( !THEME_COLOR_PATTERN.match( encoded ).hasMatch() ) {
            error = QStringLiteral( "Theme color %1 is invalid; expected opaque #RRGGBB." )
                        .arg( key );
            return false;
        }
        const QColor color( encoded );
        candidate.colors.*field.member = color;
    }
    outTheme = candidate;
    return true;
}

bool TileEditorUserTheme_Save(
    const QString &path,
    const QString &name,
    const tile_editor_preferences_t &preferences,
    QString &error )
{
    error.clear();
    const QString normalizedName = name.simplified();
    if ( path.isEmpty() ) {
        error = QStringLiteral( "Theme path is empty." );
        return false;
    }
    if ( !IsAcceptableThemeName( name ) ) {
        error = QStringLiteral( "Theme name must contain 1 to 80 visible characters." );
        return false;
    }
    if ( !QDir().mkpath( QFileInfo( path ).absolutePath() ) ) {
        error = QStringLiteral( "Cannot create the theme directory: %1" )
                    .arg( QFileInfo( path ).absolutePath() );
        return false;
    }

    const auto colors = TileEditorPreferences_Normalize( preferences );
    QString output = QStringLiteral(
        "; CypherTileEditor color theme. Colors use opaque #RRGGBB values.\n"
        "; This file intentionally contains no shortcuts, camera, layout, or map settings.\n\n"
        "[Theme]\nschemaVersion=1\nname=%1\n\n[Colors]\n" )
        .arg( QuotedValue( normalizedName ) );
    for ( const auto &field : preference_fields::colors ) {
        output += QString::fromLatin1( field.key ) + QLatin1Char( '=' ) +
            QuotedValue( ( colors.*field.member ).name( QColor::HexRgb ) ) + QLatin1Char( '\n' );
    }

    QSaveFile file( path );
    file.setDirectWriteFallback( false );
    if ( !file.open( QIODevice::WriteOnly ) ) {
        error = QStringLiteral( "Cannot save theme %1: %2" ).arg( path, file.errorString() );
        return false;
    }
    const QByteArray bytes = output.toUtf8();
    if ( file.write( bytes ) != bytes.size() || !file.commit() ) {
        error = QStringLiteral( "Cannot commit theme %1: %2" ).arg( path, file.errorString() );
        return false;
    }
    return true;
}

QList<tile_editor_user_theme_t> TileEditorUserThemes_Discover(
    const QString &directory,
    QStringList *pErrors )
{
    if ( pErrors != nullptr ) pErrors->clear();
    QList<tile_editor_user_theme_t> themes;
    const QDir dir( directory );
    const QFileInfoList entries = dir.entryInfoList(
        { QStringLiteral( "*.cytheme" ) },
        QDir::Files | QDir::Readable | QDir::NoSymLinks,
        QDir::Name | QDir::IgnoreCase );
    for ( const QFileInfo &entry : entries ) {
        tile_editor_user_theme_t theme;
        QString error;
        if ( TileEditorUserTheme_Load( entry.absoluteFilePath(), theme, error ) )
            themes.push_back( theme );
        else if ( pErrors != nullptr )
            pErrors->push_back( error );
    }
    std::sort( themes.begin(), themes.end(), []( const auto &left, const auto &right ) {
        const int byName = QString::localeAwareCompare( left.name, right.name );
        return byName == 0 ? left.path < right.path : byName < 0;
    } );
    return themes;
}

} // namespace cypher::tools::tile_editor
