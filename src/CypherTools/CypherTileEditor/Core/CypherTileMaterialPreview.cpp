//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Decode existing CYMAT/CYTEX resources into bounded preview textures.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileMaterialPreview.h"

#include "CypherCommon/FileSystem/CypherCommon_VfsDirectory.h"
#include "CypherCommon/Formats/CypherCommon_CookedMaterial.h"
#include "CypherCommon/Formats/CypherCommon_CookedTexture.h"
#include "CypherRender/CypherRender_Public.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <utility>

namespace cypher::tools::tile_editor
{
namespace render = ::cypher::engine::render;
namespace {

std::string_view View( string_view_t text ) noexcept
{
    return { text.pData, text.cchLength };
}

void SetExceptionError( std::string &error, const char *message ) noexcept
{
    try { error = message; } catch ( ... ) { error.clear(); }
}

bool ReadCooked( const vfs_t &vfs, std::string_view path, usize limit,
    blob_t &bytes, std::string &error )
{
    if ( !Blob_Init( &bytes, Allocator_GetSystem() ) ) {
        error = "Could not initialize cooked asset storage.";
        return false;
    }
    const std::string cookedPath = std::string( path ) + "_c";
    const auto result = Vfs_ReadAll( &vfs,
        { cookedPath.data(), cookedPath.size() }, limit, &bytes );
    if ( result != vfs_status_t::OK ) {
        error = "Cannot read " + cookedPath + ": " + Vfs_StatusName( result );
        return false;
    }
    return true;
}

bool DecodeSource( const vfs_t &vfs, std::string_view materialPath,
    tile_material_preview_source_t &pending, std::string &error )
{
    blob_t materialBytes{};
    if ( !ReadCooked( vfs, materialPath, CY_MIB, materialBytes, error ) ) return false;
    cooked_material_view_t material{};
    const auto materialRead = CookedMaterial_Read( Blob_Block( &materialBytes ), &material );
    if ( !CookedMaterial_Succeeded( materialRead ) ) {
        error = "Invalid cooked material " + std::string( materialPath ) + ": " +
            CookedMaterial_StatusName( materialRead.status );
        return false;
    }
    if ( View( material.shader ) != "shaders/tile_surface.cyshader" ) {
        error = "Preview supports shaders/tile_surface.cyshader; " + std::string( materialPath ) +
            " references " + std::string( View( material.shader ) ) + ".";
        return false;
    }
    if ( material.nTextures != 1u || View( material.textures[0].binding ) != "base_color" ) {
        error = "Material " + std::string( materialPath ) +
            " must contain exactly one texture binding named base_color.";
        return false;
    }
    for ( u32 i = 0; i < material.nParameters; ++i ) {
        const auto &parameter = material.parameters[i];
        const auto name = View( parameter.name );
        f32 *values = nullptr;
        u32 count = 0u;
        if ( name == "tint" ) { values = pending.tint; count = 4u; }
        else if ( name == "uv_scale" ) { values = pending.uvScale; count = 2u; }
        else {
            error = "Unsupported preview material parameter: " + std::string( name ) + ".";
            return false;
        }
        if ( parameter.type != render_material_parameter_type_t::VECTOR || parameter.nComponents != count ) {
            error = "Material parameter " + std::string( name ) + " must be a vector with " +
                std::to_string( count ) + " components.";
            return false;
        }
        for ( u32 component = 0; component < count; ++component ) {
            const f64 value = parameter.values[component];
            if ( !std::isfinite( value ) || std::abs( value ) > std::numeric_limits<f32>::max() ) {
                error = "Material parameter " + std::string( name ) + " exceeds the finite float range.";
                return false;
            }
            values[component] = static_cast<f32>( value );
            if ( name == "uv_scale" && values[component] == 0.0f ) {
                error = "Material uv_scale components must be nonzero; negative values mirror the texture.";
                return false;
            }
        }
    }

    const auto texturePath = View( material.textures[0].texture );
    blob_t textureBytes{};
    // A full mip chain needs at most 4/3 of its base image bytes, plus metadata.
    constexpr usize MAX_COOKED_TEXTURE_BYTES = 96u * CY_MIB;
    if ( !ReadCooked( vfs, texturePath, MAX_COOKED_TEXTURE_BYTES, textureBytes, error ) ) return false;
    cooked_texture_view_t texture{};
    const auto textureRead = CookedTexture_Read( Blob_Block( &textureBytes ), &texture );
    if ( !CookedTexture_Succeeded( textureRead ) ) {
        error = "Invalid cooked texture " + std::string( texturePath ) + ": " +
            CookedTexture_StatusName( textureRead.status );
        return false;
    }
    if ( texture.desc.pixelFormat != render_texture_pixel_format_t::RGBA8_UNORM &&
         texture.desc.pixelFormat != render_texture_pixel_format_t::RGBA8_SRGB ) {
        error = "Preview base_color requires RGBA8_UNORM or RGBA8_SRGB: " + std::string( texturePath );
        return false;
    }
    if ( texture.desc.usage != render_texture_usage_t::COLOR ) {
        error = "Preview base_color requires a color texture: " + std::string( texturePath );
        return false;
    }
    const auto &pixels = texture.mips[0].pixels;
    if ( pixels.cbSize > TILE_MATERIAL_PREVIEW_MAX_TEXTURE_BYTES ) {
        error = "Preview texture exceeds the 64 MiB base-image limit: " + std::string( texturePath );
        return false;
    }
    pending.width = texture.desc.nWidth;
    pending.height = texture.desc.nHeight;
    pending.sRGB = texture.desc.pixelFormat == render_texture_pixel_format_t::RGBA8_SRGB;
    pending.generateMips = ( texture.desc.flags & COOKED_TEXTURE_FLAG_GENERATED_MIPS ) != 0u;
    pending.pixels.assign( pixels.pData, pixels.pData + pixels.cbSize );
    return true;
}

} // namespace

bool CypherTileMaterialPreview_Read( const std::filesystem::path &cookedRoot,
    std::string_view materialPath, tile_material_preview_source_t &sourceOut,
    std::string &error ) noexcept
{
    error.clear();
    vfs_directory_t directory{};
    bool mounted = false;
    try {
        if ( cookedRoot.empty() ) {
            error = "Set a cooked asset root before loading material bindings.";
            return false;
        }
        if ( !materialPath.ends_with( ".cymat" ) ||
             !Vfs_IsCanonicalPath( { materialPath.data(), materialPath.size() } ) ) {
            error = "Material binding must be a canonical relative .cymat resource path.";
            return false;
        }
        // VFS native paths are UTF-8 even on Windows; path::string() uses the
        // native narrow encoding and can lose non-ASCII project directories.
        const std::u8string nativeRoot = cookedRoot.u8string();
        const auto mountResult = VfsDirectory_Init( &directory,
            { reinterpret_cast<const char *>( nativeRoot.data() ), nativeRoot.size() } );
        if ( mountResult != vfs_status_t::OK ) {
            error = "Cannot mount cooked asset root: " + std::string( Vfs_StatusName( mountResult ) );
            return false;
        }
        mounted = true;
        const vfs_t vfs = VfsDirectory_Make( &directory );
        tile_material_preview_source_t pending{};
        const bool success = DecodeSource( vfs, materialPath, pending, error );
        VfsDirectory_Shutdown( &directory );
        mounted = false;
        if ( !success ) return false;
        sourceOut = std::move( pending );
        return true;
    } catch ( const std::exception &exception ) {
        if ( mounted ) VfsDirectory_Shutdown( &directory );
        SetExceptionError( error, exception.what() );
        return false;
    } catch ( ... ) {
        if ( mounted ) VfsDirectory_Shutdown( &directory );
        SetExceptionError( error, "Could not allocate cooked material preview storage." );
        return false;
    }
}

void CypherTileMaterialPreview_Shutdown( tile_material_preview_set_t &set ) noexcept
{
    for ( auto &record : set.records ) {
        if ( record.texture.value != 0u ) {
            (void)render::R_DestroyTexture( record.texture );
            record.texture = {};
        }
    }
    set.records.clear();
}

bool CypherTileMaterialPreview_Reload( tile_material_preview_set_t &set,
    const tile_map_document_t &document, const std::filesystem::path &cookedRoot,
    std::string &error ) noexcept
{
    error.clear();
    tile_material_preview_set_t pending{};
    try {
        if ( !render::R_IsInitialized() || render::R_IsFrameActive() ) {
            error = "Material upload requires an initialized renderer outside a frame.";
            return false;
        }
        if ( !CypherTileMapDocument_IsInitialized( &document ) ) {
            error = "Material upload requires an initialized tile map document.";
            return false;
        }
        const usize count = Vector_Count( &document.materialBindings );
        if ( count > TILE_MAP_MAX_MATERIAL_BINDINGS ) {
            error = "Tile map material binding count exceeds the authoring limit.";
            return false;
        }
        pending.records.reserve( count );
        usize totalBytes = 0u;
        for ( usize i = 0u; i < count; ++i ) {
            const auto &binding = document.materialBindings.pData[i];
            const void *terminator = std::memchr( binding.path, '\0', sizeof( binding.path ) );
            if ( terminator == nullptr ) {
                error = "Tile map material path is not NUL terminated.";
                CypherTileMaterialPreview_Shutdown( pending );
                return false;
            }
            tile_material_preview_source_t source{};
            if ( !CypherTileMaterialPreview_Read( cookedRoot, binding.path, source, error ) ) {
                CypherTileMaterialPreview_Shutdown( pending );
                return false;
            }
            if ( source.pixels.size() > TILE_MATERIAL_PREVIEW_MAX_SET_BYTES - totalBytes ) {
                error = "Material preview set exceeds the 512 MiB base-image budget.";
                CypherTileMaterialPreview_Shutdown( pending );
                return false;
            }
            totalBytes += source.pixels.size();
            tile_material_preview_record_t record{};
            record.nSlot = binding.nSlot;
            std::copy_n( source.tint, 4, record.tint );
            std::copy_n( source.uvScale, 2, record.uvScale );
            render::render_texture_desc_t description{};
            description.width = source.width;
            description.height = source.height;
            description.sRGB = source.sRGB;
            description.generateMips = source.generateMips;
            description.repeat = true;
            description.debugName = binding.path;
            const render::render_texture_data_t data{ source.pixels.data(), source.pixels.size() };
            const auto upload = render::R_CreateTexture2D( description, data, &record.texture );
            if ( upload != render::render_error_t::OK ) {
                error = "Upload material slot " + std::to_string( binding.nSlot ) + ": " + render::R_ErrorName( upload );
                CypherTileMaterialPreview_Shutdown( pending );
                return false;
            }
            // Capacity was reserved before GPU creation; this cannot allocate.
            pending.records.push_back( record );
        }
        set.records.swap( pending.records );
        CypherTileMaterialPreview_Shutdown( pending );
        return true;
    } catch ( const std::exception &exception ) {
        CypherTileMaterialPreview_Shutdown( pending );
        SetExceptionError( error, exception.what() );
        return false;
    } catch ( ... ) {
        CypherTileMaterialPreview_Shutdown( pending );
        SetExceptionError( error, "Could not allocate material preview storage." );
        return false;
    }
}

const tile_material_preview_record_t *CypherTileMaterialPreview_Find(
    const tile_material_preview_set_t &set, u16 nSlot ) noexcept
{
    for ( const auto &record : set.records ) if ( record.nSlot == nSlot ) return &record;
    return nullptr;
}

} // namespace cypher::tools::tile_editor
