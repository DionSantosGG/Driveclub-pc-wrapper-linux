// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 450 core
#extension GL_EXT_samplerless_texture_functions : require

#if defined(SRC_MSAA)
layout (binding = 0, set = 0) uniform texture2DMS in_tex;
#else
layout (binding = 0, set = 0) uniform texture2D in_tex;
#endif

layout (location = 0) in vec2 uv;
layout (location = 0) out vec4 out_color;

void main()
{
#if defined(PACK_DEPTH)
    // driveclub-pc: inverse of color_to_ms_depth; keep each sample in its own channel.
    ivec2 coord = ivec2(gl_FragCoord.xy);
    out_color = vec4(texelFetch(in_tex, coord, 0).r,
                     texelFetch(in_tex, coord, 1).r,
                     texelFetch(in_tex, coord, 2).r,
                     texelFetch(in_tex, coord, 3).r);
#elif defined(SRC_MSAA)
    out_color = texelFetch(in_tex, ivec2(gl_FragCoord.xy), gl_SampleID);
#else
    out_color = texelFetch(in_tex, ivec2(gl_FragCoord.xy), 0);
#endif
}
