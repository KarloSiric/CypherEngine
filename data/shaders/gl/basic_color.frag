#version 410 core
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: data/shaders/gl/basic_color.frag
//  Purpose: Defines the basic color shader stage.
//  Details: This shader is loaded by the rendering bootstrap for GPU-side drawing
//           work. Keep inputs and uniforms explicit so engine-side binding code stays
//           simple.
//
//  History:
//  - Created by Karlo Siric on 2026-05-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

in vec3 vertex_color;

out vec4 out_color;

void main()
{
    out_color = vec4( vertex_color, 1.0 );
}
