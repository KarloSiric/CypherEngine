//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Decode bounded material thumbnails once, then draw without asset I/O.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileOrthoMaterials.h"

#include "Core/CypherTileMapMaterials.h"
#include "Core/CypherTileMaterialPreview.h"

#include <QBrush>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>

namespace cypher::tools::tile_editor
{
namespace {

QColor PaletteColor( u16 slot )
{
    const auto material = CypherTileMapMaterial_Resolve( slot );
    return QColor::fromRgbF( material.colorR, material.colorG, material.colorB );
}

float SrgbToLinear( float value )
{
    return value <= 0.04045f ? value / 12.92f
        : std::pow( ( value + 0.055f ) / 1.055f, 2.4f );
}

int LinearToSrgbByte( float value )
{
    value = std::clamp( value, 0.0f, 1.0f );
    const float encoded = value <= 0.0031308f ? 12.92f * value
        : 1.055f * std::pow( value, 1.0f / 2.4f ) - 0.055f;
    return std::clamp( static_cast<int>( std::lround( encoded * 255.0f ) ), 0, 255 );
}

tile_ortho_material_t ReadMaterial( const QString &root, const QString &path )
{
    tile_ortho_material_t result{};
    result.bound = true;
    result.path = path;
    result.label = QFileInfo( path ).completeBaseName();
    result.color = QColor( 218, 35, 183 );
    tile_material_preview_source_t source{};
    std::string error;
    const QByteArray rootBytes = root.toUtf8();
    const QByteArray pathBytes = path.toUtf8();
    const auto nativeRoot = std::filesystem::path( std::u8string(
        reinterpret_cast<const char8_t *>( rootBytes.constData() ),
        static_cast<std::size_t>( rootBytes.size() ) ) );
    if ( !CypherTileMaterialPreview_Read( nativeRoot,
            { pathBytes.constData(), static_cast<usize>( pathBytes.size() ) }, source, error ) ) {
        result.error = QString::fromStdString( error );
        return result;
    }
    result.textureWidth = source.width;
    result.textureHeight = source.height;
    result.sRGB = source.sRGB;
    result.generateMips = source.generateMips;
    std::copy_n( source.tint, 4, result.tint );
    std::copy_n( source.uvScale, 2, result.uvScale );
    const u64 requiredBytes = static_cast<u64>( source.width ) * source.height * 4u;
    if ( source.width == 0u || source.height == 0u ||
         source.width > static_cast<u32>( std::numeric_limits<int>::max() / 4 ) ||
         source.height > static_cast<u32>( std::numeric_limits<int>::max() ) ||
         requiredBytes != source.pixels.size() ) {
        result.error = QStringLiteral( "Cooked material did not provide a complete RGBA8 base image." );
        return result;
    }

    // The borrowed QImage avoids another full-resolution allocation. Only its
    // bounded thumbnail becomes owned; the decoder's large buffer dies here.
    const QImage borrowed( reinterpret_cast<const uchar *>( source.pixels.data() ),
        static_cast<int>( source.width ), static_cast<int>( source.height ),
        static_cast<qsizetype>( source.width ) * 4, QImage::Format_RGBA8888 );
    const QImage thumbnail = source.width > TILE_ORTHO_MATERIAL_MAX_IMAGE_SIZE ||
            source.height > TILE_ORTHO_MATERIAL_MAX_IMAGE_SIZE
        ? borrowed.scaled( TILE_ORTHO_MATERIAL_MAX_IMAGE_SIZE,
              TILE_ORTHO_MATERIAL_MAX_IMAGE_SIZE, Qt::KeepAspectRatio, Qt::SmoothTransformation )
        : borrowed;
    result.image = thumbnail.convertToFormat( QImage::Format_ARGB32 );
    if ( result.image.isNull() ) {
        result.error = QStringLiteral( "Could not allocate the orthographic material thumbnail." );
        return result;
    }

    // Material tint is linear, matching the tile shader. Convert cooked sRGB
    // texels before tinting, then encode for QPainter's display image.
    int channels[3][256]{};
    for ( int channel = 0; channel < 3; ++channel ) {
        for ( int value = 0; value < 256; ++value ) {
            const float normalized = static_cast<float>( value ) / 255.0f;
            channels[channel][value] = LinearToSrgbByte(
                ( source.sRGB ? SrgbToLinear( normalized ) : normalized ) * source.tint[channel] );
        }
    }
    u64 sumR = 0u, sumG = 0u, sumB = 0u, sumA = 0u;
    const float alphaTint = std::clamp( source.tint[3], 0.0f, 1.0f );
    for ( int y = 0; y < result.image.height(); ++y ) {
        auto *row = reinterpret_cast<QRgb *>( result.image.scanLine( y ) );
        for ( int x = 0; x < result.image.width(); ++x ) {
            const QRgb original = row[x];
            const int r = channels[0][qRed( original )];
            const int g = channels[1][qGreen( original )];
            const int b = channels[2][qBlue( original )];
            const int a = static_cast<int>( std::lround( qAlpha( original ) * alphaTint ) );
            row[x] = qRgba( r, g, b, a );
            sumR += static_cast<u64>( r ) * a;
            sumG += static_cast<u64>( g ) * a;
            sumB += static_cast<u64>( b ) * a;
            sumA += a;
        }
    }
    result.color = sumA != 0u
        ? QColor( static_cast<int>( sumR / sumA ), static_cast<int>( sumG / sumA ),
              static_cast<int>( sumB / sumA ) )
        : QColor( Qt::transparent );
    result.textureBrush = QBrush( result.image );
    return result;
}

const QBrush &MissingMaterialPattern()
{
    static const QBrush brush = [] {
        QImage checker( 16, 16, QImage::Format_ARGB32 );
        for ( int y = 0; y < checker.height(); ++y ) {
            auto *row = reinterpret_cast<QRgb *>( checker.scanLine( y ) );
            for ( int x = 0; x < checker.width(); ++x )
                row[x] = ( ( x / 8 + y / 8 ) & 1 ) != 0
                    ? qRgb( 218, 35, 183 ) : qRgb( 48, 24, 49 );
        }
        return QBrush( checker );
    }();
    return brush;
}

} // namespace

bool TileOrthoMaterials_Refresh( tile_ortho_material_cache_t &cache,
    const tile_map_document_t &document, const QString &cookedRoot, bool force )
{
    const QString root = cookedRoot.isEmpty() ? QString()
        : QDir::cleanPath( QDir( cookedRoot ).absolutePath() );
    const usize count = std::min( Vector_Count( &document.materialBindings ),
        TILE_MAP_MAX_MATERIAL_BINDINGS );
    QByteArray signature;
    signature.reserve( static_cast<qsizetype>( count * ( TILE_MAP_MATERIAL_PATH_CAPACITY + 2u ) ) );
    for ( usize i = 0u; i < count; ++i ) {
        const auto &binding = document.materialBindings.pData[i];
        signature.append( reinterpret_cast<const char *>( &binding.nSlot ), sizeof( binding.nSlot ) );
        signature.append( binding.path, sizeof( binding.path ) );
    }
    if ( cache.initialized && !force && cache.cookedRoot == root &&
         cache.bindingSignature == signature ) return false;

    // Reuse unchanged paths across slot changes. Copies share QImage storage;
    // neither the temporary path map nor duplicate slots copy pixel buffers.
    QHash<QString, tile_ortho_material_t> byPath;
    if ( !force && cache.cookedRoot == root ) {
        for ( auto entry = cache.entries.cbegin(); entry != cache.entries.cend(); ++entry )
            byPath.insert( entry.value().path, entry.value() );
    }
    QHash<u16, tile_ortho_material_t> pending;
    pending.reserve( static_cast<qsizetype>( count ) );
    for ( usize i = 0u; i < count; ++i ) {
        const auto &binding = document.materialBindings.pData[i];
        const char *terminator = static_cast<const char *>(
            std::memchr( binding.path, '\0', sizeof( binding.path ) ) );
        if ( terminator == nullptr ) {
            tile_ortho_material_t invalid{};
            invalid.bound = true;
            invalid.color = QColor( 218, 35, 183 );
            invalid.label = QStringLiteral( "Invalid material binding" );
            invalid.error = QStringLiteral( "Material path is not NUL terminated." );
            pending.insert( binding.nSlot, std::move( invalid ) );
            continue;
        }
        const QString path = QString::fromUtf8( binding.path, terminator - binding.path );
        auto existing = byPath.constFind( path );
        if ( existing == byPath.cend() ) {
            byPath.insert( path, ReadMaterial( root, path ) );
            existing = byPath.constFind( path );
        }
        pending.insert( binding.nSlot, existing.value() );
    }
    cache.entries.swap( pending );
    cache.cookedRoot = root;
    cache.bindingSignature = std::move( signature );
    cache.initialized = true;
    return true;
}

const tile_ortho_material_t *TileOrthoMaterials_Find(
    const tile_ortho_material_cache_t &cache, u16 slot )
{
    const auto entry = cache.entries.constFind( slot );
    return entry == cache.entries.cend() ? nullptr : &entry.value();
}

QString TileOrthoMaterials_Describe( const tile_ortho_material_cache_t *cache, u16 slot )
{
    const auto *material = cache != nullptr ? TileOrthoMaterials_Find( *cache, slot ) : nullptr;
    if ( material == nullptr ) {
        const auto fallback = CypherTileMapMaterial_Resolve( slot );
        return QStringLiteral( "Slot %1 — %2 (blockout palette)" )
            .arg( slot ).arg( QString::fromUtf8( fallback.pDisplayName ) );
    }
    QString description = QStringLiteral( "Slot %1 — %2\n%3" )
        .arg( slot ).arg( material->label, material->path );
    if ( !material->error.isEmpty() ) description += QStringLiteral( "\nUnavailable: " ) + material->error;
    return description;
}

void TileOrthoMaterials_Paint( QPainter &painter, const QRectF &rect,
    const tile_ortho_material_cache_t *cache, u16 slot, qreal opacity )
{
    if ( rect.isEmpty() || !std::isfinite( rect.x() ) || !std::isfinite( rect.y() ) ||
         !std::isfinite( rect.width() ) || !std::isfinite( rect.height() ) ||
         !std::isfinite( opacity ) || opacity <= 0.0 ) return;
    const auto *material = cache != nullptr ? TileOrthoMaterials_Find( *cache, slot ) : nullptr;
    painter.save();
    painter.setOpacity( painter.opacity() * std::clamp( opacity, 0.0, 1.0 ) );
    if ( material == nullptr ) {
        painter.fillRect( rect, PaletteColor( slot ) );
    } else if ( material->image.isNull() ) {
        QBrush missing( MissingMaterialPattern() );
        QTransform origin;
        origin.translate( rect.x(), rect.y() );
        missing.setTransform( origin );
        painter.fillRect( rect, missing );
    } else {
        // A texture brush repeats without generating a tiled image or a loop per
        // repeat. Negative scales reflect the brush around the rectangle origin.
        const qreal scaleX = material->uvScale[0];
        const qreal scaleY = material->uvScale[1];
        if ( std::isfinite( scaleX ) && std::isfinite( scaleY ) && scaleX != 0.0 && scaleY != 0.0 ) {
            QBrush texture = material->textureBrush;
            QTransform mapping;
            mapping.translate( rect.x(), rect.y() );
            mapping.scale( rect.width() / ( material->image.width() * scaleX ),
                rect.height() / ( material->image.height() * scaleY ) );
            texture.setTransform( mapping );
            painter.fillRect( rect, texture );
        } else {
            painter.fillRect( rect, material->color );
        }
    }
    painter.restore();
}

} // namespace cypher::tools::tile_editor
