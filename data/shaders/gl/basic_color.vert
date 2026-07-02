#version 410 core
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: data/shaders/gl/basic_color.vert
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

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;

out vec3 vertex_color;

void main()
{
    vertex_color = in_color;
    gl_Position = u_projection * u_view * u_model * vec4( in_position, 1.0 );
}
