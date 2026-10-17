//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Falloff.cpp
//  Purpose: Implements the Displacement brush falloff presets and curves.
//
//  History:
//  - Created by Karlo Siric on 2026-10-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Falloff.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace cypher::editor::map
{
using namespace common;

namespace
{
bool Is( string_view_t text, const char *name ) noexcept { return StringView_Equals( text, StringView_FromCString( name ) ); }

// Fritsch-Carlson: secant slopes, zeroed at extrema, limited so each span
// stays monotone.
std::vector<f64> Slopes( const std::vector<map_curve_point_t> &p )
{
    const usize n = p.size();
    std::vector<f64> m( n, 0.0 ), d( n > 0 ? n - 1 : 0, 0.0 );
    for ( usize i = 0; i + 1 < n; ++i ) { d[i] = ( p[i + 1].y - p[i].y ) / std::max( 1e-9, p[i + 1].x - p[i].x ); }
    if ( n < 2 ) { return m; }
    m[0] = d[0]; m[n - 1] = d[n - 2];
    for ( usize i = 1; i + 1 < n; ++i ) { m[i] = d[i - 1] * d[i] <= 0.0 ? 0.0 : 0.5 * ( d[i - 1] + d[i] ); }
    for ( usize i = 0; i + 1 < n; ++i ) {
        if ( std::fabs( d[i] ) < 1e-12 ) { m[i] = m[i + 1] = 0.0; continue; }
        const f64 a = m[i] / d[i], b = m[i + 1] / d[i], h = a * a + b * b;
        if ( h > 9.0 ) { const f64 s = 3.0 / std::sqrt( h ); m[i] = s * a * d[i]; m[i + 1] = s * b * d[i]; }
    }
    return m;
}
} // namespace

std::vector<map_curve_point_t> MapFalloff_ParseCurve( string_view_t text )
{
    std::vector<map_curve_point_t> points;
    std::string buffer( text.pData != nullptr ? std::string( text.pData, text.cchLength ) : std::string() );
    const char *at = buffer.c_str();
    while ( *at != '\0' && points.size() < MAP_FALLOFF_CURVE_MAX_POINTS ) {
        while ( *at == ' ' || *at == ',' || *at == ';' ) { ++at; }
        if ( *at == '\0' ) { break; }
        char *end = nullptr;
        const f64 x = std::strtod( at, &end );
        if ( end == at || *end != ':' ) { points.clear(); break; }
        at = end + 1;
        const f64 y = std::strtod( at, &end );
        if ( end == at || !std::isfinite( x ) || !std::isfinite( y ) ) { points.clear(); break; }
        at = end;
        points.push_back( { std::clamp( x, 0.0, 1.0 ), std::clamp( y, 0.0, 1.0 ) } );
    }
    if ( points.empty() ) { return MapFalloff_ParseCurve( StringView_FromCString( MAP_FALLOFF_CURVE_DEFAULT ) ); }
    std::stable_sort( points.begin(), points.end(), []( const auto &a, const auto &b ) { return a.x < b.x; } );
    // One point per x: the later one wins.
    std::vector<map_curve_point_t> unique;
    for ( const auto &point : points ) {
        if ( !unique.empty() && std::fabs( unique.back().x - point.x ) < 1e-6 ) { unique.back() = point; } else { unique.push_back( point ); }
    }
    if ( unique.front().x > 0.0 ) { unique.insert( unique.begin(), { 0.0, unique.front().y } ); }
    if ( unique.back().x < 1.0 ) { unique.push_back( { 1.0, unique.back().y } ); }
    return unique;
}

std::string MapFalloff_FormatCurve( const std::vector<map_curve_point_t> &points )
{
    std::string text;
    char item[48];
    for ( const auto &point : points ) {
        std::snprintf( item, sizeof( item ), "%s%.4g:%.4g", text.empty() ? "" : " ", point.x, point.y );
        text += item;
    }
    return text;
}

map_falloff_t MapFalloff_Make( string_view_t name, string_view_t curve )
{
    map_falloff_t falloff{};
    if ( Is( name, "linear" ) ) { falloff.kind = map_falloff_kind_t::LINEAR; }
    else if ( Is( name, "sharp" ) ) { falloff.kind = map_falloff_kind_t::SHARP; }
    else if ( Is( name, "constant" ) ) { falloff.kind = map_falloff_kind_t::CONSTANT; }
    else if ( Is( name, "custom" ) ) {
        falloff.kind = map_falloff_kind_t::CUSTOM;
        falloff.points = MapFalloff_ParseCurve( curve );
        falloff.slopes = Slopes( falloff.points );
    }
    return falloff;
}

f64 MapFalloff_At( const map_falloff_t &falloff, f64 t ) noexcept
{
    t = std::clamp( t, 0.0, 1.0 );
    switch ( falloff.kind ) {
        case map_falloff_kind_t::LINEAR: return 1.0 - t;
        case map_falloff_kind_t::SHARP: return ( 1.0 - t ) * ( 1.0 - t );
        case map_falloff_kind_t::CONSTANT: return 1.0;
        case map_falloff_kind_t::SMOOTH: return 1.0 - t * t * ( 3.0 - 2.0 * t );
        case map_falloff_kind_t::CUSTOM: break;
    }
    const auto &p = falloff.points;
    if ( p.empty() ) { return 1.0 - t; }
    if ( p.size() == 1 ) { return p[0].y; }
    usize i = 0;
    while ( i + 2 < p.size() && t > p[i + 1].x ) { ++i; }
    const f64 h = std::max( 1e-9, p[i + 1].x - p[i].x ), s = std::clamp( ( t - p[i].x ) / h, 0.0, 1.0 );
    const f64 s2 = s * s, s3 = s2 * s;
    const f64 y = ( 2 * s3 - 3 * s2 + 1 ) * p[i].y + ( s3 - 2 * s2 + s ) * h * falloff.slopes[i] + ( -2 * s3 + 3 * s2 ) * p[i + 1].y + ( s3 - s2 ) * h * falloff.slopes[i + 1];
    return std::clamp( y, 0.0, 1.0 );
}

} // namespace cypher::editor::map
