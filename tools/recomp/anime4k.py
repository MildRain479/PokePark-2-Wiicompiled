# Anime4K's upscaling networks (recomp/third_party/anime4k, MIT), as Vulkan
# compute shaders for vk_backend.c, which runs them on a movie's Y plane.
#
# Each file is a chain of mpv user shaders ("//!" headers, then a hook()):
# convolutions saved to intermediate textures, the last one doubling the
# size and adding its result to MAIN, the picture. Here each pass becomes a
# compute shader writing one pixel an invocation: the textures it binds come
# from the shaders' texture array, in the order of its //!BIND lines, their
# numbers pushed as constants; MAIN is the Y plane, read as (Y, Y, Y, Y).
# The last pass writes the upscaled Y (of an RGB result, its luma) to an
# RGBA8 image, the rest RGBA16F ones, as mpv keeps them.

import os
import re

HEADER = """#version 450
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D textures[16384];
layout(set = 1, binding = 0, %s) uniform writeonly image2D o_img;
layout(push_constant) uniform Pass { uint tex[16]; } pc;
ivec2 at;
vec2 at_uv;
vec4 fetch(uint t, ivec2 p) { return texelFetch(textures[t], clamp(p, ivec2(0), textureSize(textures[t], 0) - 1), 0); }
// Bilinear, whatever the texture's own sampler does.
vec4 bilinear(uint t, vec2 uv) {
    vec2 p = uv * vec2(textureSize(textures[t], 0)) - 0.5, f = fract(p);
    ivec2 i = ivec2(floor(p));
    return mix(mix(fetch(t, i), fetch(t, i + ivec2(1, 0)), f.x), mix(fetch(t, i + ivec2(0, 1)), fetch(t, i + ivec2(1, 1)), f.x), f.y);
}
"""

NAME_MACROS = """#define {n}_texOff(o) fetch(pc.tex[{k}], at + ivec2(o))
#define {n}_tex(uv) bilinear(pc.tex[{k}], uv)
#define {n}_pos at_uv
#define {n}_size vec2(textureSize(textures[pc.tex[{k}]], 0))
#define {n}_pt (1.0 / {n}_size)
"""

MAIN = """void main() {
    ivec2 size = imageSize(o_img);
    at = ivec2(gl_GlobalInvocationID.xy);
    if (at.x >= size.x || at.y >= size.y) return;
    at_uv = (vec2(at) + 0.5) / vec2(size);
    vec4 r = hook();
%s    imageStore(o_img, at, r);
}
"""


def passes(path):
    """The file's passes: (binds, save, doubles size, body)."""
    text = open(path).read()
    out = []
    for chunk in re.split(r"(?m)^(?=//!DESC)", text)[1:]:
        head = re.findall(r"(?m)^//!(\w+) ?(.*)$", chunk)
        body = re.sub(r"(?m)^//!.*\n", "", chunk)
        binds = [v for k, v in head if k == "BIND"]
        save = next(v for k, v in head if k == "SAVE")
        width = next(v for k, v in head if k == "WIDTH")
        out.append((binds, save, width.endswith("2 *"), body))
    return out


def network(path):
    """(shader sources, pass table) for one file. The table's rows: the
    pass's inputs as slots (-1 MAIN, else an intermediate texture), the slot
    it writes (-1 the result), and whether it doubles the size."""
    ps = passes(path)
    slots = []
    for _, save, _, _ in ps:
        if save != "MAIN" and save not in slots:
            slots.append(save)
    sources, table = [], []
    for i, (binds, save, doubles, body) in enumerate(ps):
        last = save == "MAIN"
        src = HEADER % ("rgba8" if last else "rgba16f")
        for k, b in enumerate(binds):
            src += NAME_MACROS.format(n=b, k=k)
        src += body.strip() + "\n"
        src += MAIN % ("    r = vec4(dot(r.rgb, vec3(0.299, 0.587, 0.114)));\n" if last else "")
        sources.append(src)
        table.append(([-1 if b == "MAIN" else slots.index(b) for b in binds], -1 if last else slots.index(save), doubles))
    return sources, table, len(slots)
