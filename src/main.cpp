// ============================================================================
// MyEngine : a Minecraft-style voxel sandbox
//
// Files:
//   game.h   world, terrain, player physics, raycast (no OpenGL)
//   atlas.h  procedural 16x16 textures packed in one atlas
//   main.cpp rendering, input, game loop (this file)
//
// Controls
//   Mouse         look                 Left click    break (hold)
//   WASD          move                 Right click   place / eat (hold)
//   Space         jump / swim / fly up Middle click  pick block
//   Left Shift    sneak / fly down     1-9, wheel    hotbar
//   Left Ctrl     sprint (or double tap W)
//   Q             drop one (Ctrl+Q: whole stack)
//   E             inventory (2x2 crafting)   F6  reload assets/recipes.json
//   F7            skip ahead 3 hours (a full day is 20 minutes)
//   Esc           pause menu (save and quit from there)
//
// The game opens on a title screen: pick a saved world or create one.
// Worlds live in saves/<name>/world.dat and autosave every 5 minutes.
// Run with a number (MyEngine.exe 1337) to jump straight into a new world
// with that seed.
//   Right click a crafting table for 3x3 crafting, a furnace to smelt
//   (sneak to place a block against either)
//   Double Space  toggle flight (creative)
//   G             survival / creative  F5           third person
//   F3            debug info in title  Esc          release mouse (again to quit)
//
// Survival starts with an empty hotbar: punch trees, turn logs into planks
// in your inventory, make a crafting table, then tools. Blocks drop as
// items you walk over to pick up. Hearts, hunger and breath work like
// Minecraft.
//
// In a crafting/inventory screen: left click takes or places a stack,
// right click takes half or places one, shift click moves it across,
// 1-9 over a slot swaps it with that hotbar slot, clicking outside drops.
// Hold right click and drag across slots to drop one item into each.
// ============================================================================
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <initializer_list>
#include <cstring>
#include <random>
#include <map>
#include <unordered_map>
#include <memory>
#include <ctime>
#include <functional>
#include <chrono>

#include "game.h"
#include "atlas.h"
#include "recipes.h"
#include "save.h"
#define STB_IMAGE_IMPLEMENTATION
#include "pack.h"

#ifndef MYENGINE_ASSETS
#define MYENGINE_ASSETS "assets"   // CMake points this at the source tree
#endif

using namespace game;

static const float PI = 3.14159265358979f;

// ============================================================================
// Math
// ============================================================================
struct Vec3 { float x, y, z; };
static Vec3  operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static Vec3  operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static Vec3  operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
static float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3  Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static Vec3  Normalize(Vec3 v) { float l = std::sqrt(Dot(v, v)); return l > 0 ? v * (1.0f / l) : v; }
static float Lerp(float a, float b, float t) { return a + (b - a) * t; }

struct Mat4 { float m[16]; };
static float& At(Mat4& M, int c, int r) { return M.m[c * 4 + r]; }
static float  At(const Mat4& M, int c, int r) { return M.m[c * 4 + r]; }

static Mat4 Identity() { Mat4 r = {}; At(r, 0, 0) = At(r, 1, 1) = At(r, 2, 2) = At(r, 3, 3) = 1; return r; }
static Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r = {};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row)
            At(r, c, row) = At(a, 0, row) * At(b, c, 0) + At(a, 1, row) * At(b, c, 1) +
                            At(a, 2, row) * At(b, c, 2) + At(a, 3, row) * At(b, c, 3);
    return r;
}
static Mat4 Translate(Vec3 t) { Mat4 r = Identity(); At(r, 3, 0) = t.x; At(r, 3, 1) = t.y; At(r, 3, 2) = t.z; return r; }
static Mat4 Perspective(float fovY, float aspect, float zn, float zf) {
    Mat4 r = {};
    float f = 1.0f / std::tan(fovY / 2.0f);
    At(r, 0, 0) = f / aspect;
    At(r, 1, 1) = f;
    At(r, 2, 2) = (zf + zn) / (zn - zf);
    At(r, 2, 3) = -1.0f;
    At(r, 3, 2) = (2.0f * zf * zn) / (zn - zf);
    return r;
}
static Mat4 LookAt(Vec3 eye, Vec3 target, Vec3 up) {
    Vec3 f = Normalize(target - eye), s = Normalize(Cross(f, up)), u = Cross(s, f);
    Mat4 v = Identity();
    At(v, 0, 0) = s.x;  At(v, 1, 0) = s.y;  At(v, 2, 0) = s.z;
    At(v, 0, 1) = u.x;  At(v, 1, 1) = u.y;  At(v, 2, 1) = u.z;
    At(v, 0, 2) = -f.x; At(v, 1, 2) = -f.y; At(v, 2, 2) = -f.z;
    At(v, 3, 0) = -Dot(s, eye); At(v, 3, 1) = -Dot(u, eye); At(v, 3, 2) = Dot(f, eye);
    return v;
}
static Mat4 Ortho(float l, float r, float b, float t) {
    Mat4 m = Identity();
    At(m, 0, 0) = 2 / (r - l); At(m, 1, 1) = 2 / (t - b); At(m, 2, 2) = -1;
    At(m, 3, 0) = -(r + l) / (r - l); At(m, 3, 1) = -(t + b) / (t - b);
    return m;
}
static Vec3 DirFromAngles(float yaw, float pitch) {
    return {std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)};
}

// ============================================================================
// Shaders
// ============================================================================
static GLuint CompileProgram(const char* name, const char* vsSrc, const char* fsSrc) {
    auto compile = [&](GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) { char log[1024]; glGetShaderInfoLog(s, 1024, nullptr, log); std::fprintf(stderr, "[%s] shader error:\n%s\n", name, log); }
        return s;
    };
    GLuint vs = compile(GL_VERTEX_SHADER, vsSrc), fs = compile(GL_FRAGMENT_SHADER, fsSrc);
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[1024]; glGetProgramInfoLog(p, 1024, nullptr, log); std::fprintf(stderr, "[%s] link error:\n%s\n", name, log); }
    glDeleteShader(vs); glDeleteShader(fs);
    return p;
}

// World blocks: atlas texture, sun, sky + block light, per-vertex ambient occlusion, fog
static const char* BLOCK_VS = R"(
#version 330 core
layout(location=0) in vec3  aPos;
layout(location=1) in vec2  aUV;
layout(location=2) in vec3  aNormal;
layout(location=3) in float aAO;
layout(location=4) in vec2  aLight;      // (sky, block) light levels, 0..15
layout(location=5) in vec3  aTint;       // biome color for grass, leaves and water; white otherwise
uniform mat4 uViewProj;
out vec2 vUV; out vec3 vNormal; out float vAO; out vec2 vLight; out vec3 vWorld; out vec3 vTint;
void main() {
    gl_Position = uViewProj * vec4(aPos, 1.0);
    vUV = aUV; vNormal = aNormal; vAO = aAO; vLight = aLight; vWorld = aPos; vTint = aTint;
}
)";
static const char* BLOCK_FS = R"(
#version 330 core
in vec2 vUV; in vec3 vNormal; in float vAO; in vec2 vLight; in vec3 vWorld; in vec3 vTint;
out vec4 FragColor;
uniform sampler2D uAtlas;
uniform vec3  uSunDir;
uniform vec3  uCamPos;
uniform vec3  uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform int   uTranslucent;
uniform float uDaylight;     // 1 at noon, 0 at night

// Minecraft's lightmap: light levels fall off steeply, torch light is warm,
// and the default brightness setting lifts the darks a little.
vec3 LightColor(vec2 level) {
    float s = level.x / 15.0, b = level.y / 15.0;
    float sky = s / (4.0 - 3.0 * s);
    float blk = b / (4.0 - 3.0 * b);
    vec3 torch = vec3(blk, blk * ((blk * 0.6 + 0.4) * 0.6 + 0.4), blk * (blk * blk * 0.6 + 0.4));
    vec3 skyTint = mix(vec3(0.10, 0.12, 0.22), vec3(1.0), uDaylight);   // moonlight is dim and blue
    vec3 c = max(vec3(sky) * skyTint, torch);
    c = mix(c, vec3(0.75), 0.04);
    return mix(c, 1.0 - pow(1.0 - c, vec3(4.0)), 0.5);
}

void main() {
    vec4 tex = texture(uAtlas, vUV);
    if (uTranslucent == 0 && tex.a < 0.5) discard;          // leaves / glass cutout

    vec3  n      = normalize(vNormal);
    float sun    = max(dot(n, uSunDir), 0.0);                // the dot product from day one
    float skyF   = vLight.x / 15.0;
    // Directional sun shading where the sky reaches, flat shading in the dark
    float facing = 0.56 + 0.12 * n.y + 0.42 * mix(0.6, sun, skyF * uDaylight);
    vec3  light  = LightColor(vLight) * facing * vAO;

    vec3 col = tex.rgb * vTint * light;
    float d   = length(vWorld.xz - uCamPos.xz);              // cylindrical fog, like Minecraft
    float fog = clamp((d - uFogStart) / (uFogEnd - uFogStart), 0.0, 1.0);
    col = mix(col, uFogColor, fog);
    FragColor = vec4(col, uTranslucent == 1 ? tex.a : 1.0);
}
)";

// Lines, overlays, sun, clouds, player model: position + uv + color
static const char* SIMPLE_VS = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aColor;
uniform mat4 uMVP;
out vec2 vUV; out vec4 vColor;
void main() { gl_Position = uMVP * vec4(aPos, 1.0); vUV = aUV; vColor = aColor; }
)";
static const char* SIMPLE_FS = R"(
#version 330 core
in vec2 vUV; in vec4 vColor;
out vec4 FragColor;
uniform sampler2D uTex;
uniform int uUseTex;
uniform vec4 uTint;
void main() {
    vec4 c = vColor * uTint;
    if (uUseTex == 1) c *= texture(uTex, vUV);
    if (c.a < 0.1) discard;                                  // item sprite cutout
    FragColor = c;
}
)";

// 2D HUD in pixel coordinates. uv.x < 0 means "solid color, no texture"
static const char* HUD_VS = R"(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aColor;
uniform mat4 uProj;
out vec2 vUV; out vec4 vColor;
void main() { gl_Position = uProj * vec4(aPos, 0.0, 1.0); vUV = aUV; vColor = aColor; }
)";
static const char* HUD_FS = R"(
#version 330 core
in vec2 vUV; in vec4 vColor;
out vec4 FragColor;
uniform sampler2D uTex;
void main() {
    if (vUV.x < 0.0) { FragColor = vColor; return; }
    vec4 t = texture(uTex, vUV);
    if (t.a < 0.1) discard;
    FragColor = t * vColor;
}
)";

// ============================================================================
// Small dynamic vertex buffer helper
// ============================================================================
struct DynMesh {
    GLuint vao = 0, vbo = 0;
    int stride = 0;          // floats per vertex
    GLsizei uploaded = 0;

    void Init(std::initializer_list<int> comps) {
        stride = 0;
        for (int c : comps) stride += c;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        int i = 0, off = 0;
        for (int c : comps) {
            glVertexAttribPointer(i, c, GL_FLOAT, GL_FALSE, stride * sizeof(float), (void*)(off * sizeof(float)));
            glEnableVertexAttribArray(i);
            ++i; off += c;
        }
        glBindVertexArray(0);
    }
    void Upload(const std::vector<float>& v, GLenum usage = GL_STREAM_DRAW) {
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.empty() ? nullptr : v.data(), usage);
        uploaded = (GLsizei)(v.size() / stride);
    }
    void DrawUploaded(GLenum mode) const {
        if (!uploaded) return;
        glBindVertexArray(vao);
        glDrawArrays(mode, 0, uploaded);
    }
    void Draw(const std::vector<float>& v, GLenum mode) { Upload(v); DrawUploaded(mode); }
    void Free() {
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        vao = vbo = 0;
        uploaded = 0;
    }
};

// ============================================================================
// Chunk meshing
//
// Each 16x16 column of the world becomes one vertex buffer. A face is only
// emitted when it borders something you can see through. Each vertex also
// gets:
//   AO  : how many solid blocks crowd that corner (Minecraft "smooth lighting")
//   sky : how much open sky that corner sees (shade under trees and overhangs)
// ============================================================================
struct FaceDef { int n[3]; int c[4][3]; };   // corners: bottom-left, bottom-right, top-right, top-left
static const FaceDef FACES[6] = {
    {{ 1, 0, 0}, {{1,0,1},{1,0,0},{1,1,0},{1,1,1}}},   // +X
    {{-1, 0, 0}, {{0,0,0},{0,0,1},{0,1,1},{0,1,0}}},   // -X
    {{ 0, 1, 0}, {{0,1,1},{1,1,1},{1,1,0},{0,1,0}}},   // +Y top
    {{ 0,-1, 0}, {{0,0,0},{1,0,0},{1,0,1},{0,0,1}}},   // -Y bottom
    {{ 0, 0, 1}, {{0,0,1},{1,0,1},{1,1,1},{0,1,1}}},   // +Z
    {{ 0, 0,-1}, {{1,0,0},{0,0,0},{0,1,0},{1,1,0}}},   // -Z
};
static const float AO_LEVELS[4] = {0.42f, 0.62f, 0.81f, 1.0f};   // vertex: pos3 uv2 normal3 ao1 sky1

struct ChunkMesh {
    DynMesh solid, water;
    bool dirty = true;
    ChunkMesh() { solid.Init({3, 2, 3, 1, 2, 3}); water.Init({3, 2, 3, 1, 2, 3}); }
    ~ChunkMesh() { solid.Free(); water.Free(); }
};
// A mesh for every chunk that's lit and in view, by World::Key
static std::unordered_map<uint64_t, std::unique_ptr<ChunkMesh>> gMeshes;

static bool Occludes(uint8_t b) { return Info(b).opaque || IsLeaves(b); }

static bool ShouldDrawFace(uint8_t self, uint8_t nb, int ny) {
    if (ny < 0) return false;                 // nobody sees under the bedrock floor
    if (Info(nb).opaque) return false;
    if (self == WATER) return nb != WATER;
    if (self == GLASS) return nb != GLASS;
    if (self == ICE)   return nb != ICE;
    return true;                              // includes leaves next to leaves (fancy leaves)
}

static void TileUV(int tile, float& u0, float& v0, float& u1, float& v1) {
    const float t = 1.0f / atlas::GRID, e = 0.0005f;   // small inset avoids bleeding
    u0 = (tile % atlas::GRID) * t + e; v0 = (tile / atlas::GRID) * t + e;
    u1 = u0 + t - 2 * e;               v1 = v0 + t - 2 * e;
}

// UVs of a pixel rectangle inside a tile; (px0, py0) top-left, y down like the art
static void SubUV(int tile, float px0, float py0, float px1, float py1, float& u0, float& v0, float& u1, float& v1) {
    const float t = 1.0f / atlas::GRID, e = 0.0005f;
    float bu = (tile % atlas::GRID) * t, bv = (tile / atlas::GRID) * t;
    u0 = bu + px0 / 16.0f * t + e; u1 = bu + px1 / 16.0f * t - e;
    v0 = bv + (16.0f - py1) / 16.0f * t + e; v1 = bv + (16.0f - py0) / 16.0f * t - e;   // v0 = bottom edge
}

// One block-vertex: pos3 uv2 normal3 ao1 light2 tint3
static void PushBlockVert(std::vector<float>& out, Vec3 p, float u, float v, Vec3 n, float ao, float sky, float blk,
                          Vec3 tint = {1, 1, 1}) {
    out.insert(out.end(), {p.x, p.y, p.z, u, v, n.x, n.y, n.z, ao, sky, blk, tint.x, tint.y, tint.z});
}

static Vec3 Rgb(uint32_t c) { return {((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f}; }

// The biome tint a block's face gets (white = untinted). face: FACES index
static Vec3 FaceTint(const World& w, uint8_t b, int face, int x, int z) {
    if (b == GRASS && face == 2) return Rgb(w.GrassAt(x, z));
    if (b == SHORT_GRASS || b == FERN) return Rgb(w.GrassAt(x, z));
    if (b == LEAVES)             return Rgb(w.FoliageAt(x, z));
    if (b == SPRUCE_LEAVES)      return Rgb(SPRUCE_FOLIAGE);
    if (b == BIRCH_LEAVES)       return Rgb(BIRCH_FOLIAGE);
    if (b == WATER)              return Rgb(w.WaterAt(x, z));
    return {1, 1, 1};
}

// A quad (corners bottom-left, bottom-right, top-right, top-left of its
// texture), wound so it faces along n whatever order the corners came in
// Torches and plants are shown as flat sprites rather than little cubes
static bool DrawsFlat(uint8_t item) { return IsTorch(item) || IsPlant(item); }

// Colors for blocks shown outside the world (icons, dropped items): plains
static Vec3 IconTint(uint8_t b) {
    if (b == GRASS || b == SHORT_GRASS || b == FERN) return Rgb(BiomeData(PLAINS).grass);
    if (b == LEAVES)        return Rgb(BiomeData(PLAINS).foliage);
    if (b == SPRUCE_LEAVES) return Rgb(SPRUCE_FOLIAGE);
    if (b == BIRCH_LEAVES)  return Rgb(BIRCH_FOLIAGE);
    if (b == WATER)         return Rgb(BiomeData(PLAINS).water);
    return {1, 1, 1};
}

static void PushBlockQuad(std::vector<float>& out, Vec3 q[4], float u0, float v0, float u1, float v1, Vec3 n,
                          float sky, float blk, Vec3 tint = {1, 1, 1}) {
    const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
    int order[6] = {0, 1, 2, 2, 3, 0};
    if (Dot(Cross(q[1] - q[0], q[2] - q[0]), n) < 0) { order[1] = 2; order[2] = 1; order[4] = 0; order[5] = 3; }
    for (int k : order) PushBlockVert(out, q[k], uv[k][0], uv[k][1], n, 1.0f, sky, blk, tint);
}

// Blocks that aren't full cubes: the snow layer (2px tall) and cactus
// (sides pulled in 1px). Lit flat by their own cell; faces against solid
// blocks are skipped like normal faces.
static void AddShapeMesh(std::vector<float>& out, const World& w, uint8_t b, int x, int y, int z) {
    const float p = 1.0f / 16.0f;
    const BlockInfo& bi = Info(b);
    const float sky = (float)w.SkyLight(x, y, z), blk = (float)w.BlockLight(x, y, z);
    // Box in block-local units, and how far the sides sit in from the cell edge
    float top = b == SNOW ? 2 * p : 1.0f, inset = b == CACTUS ? p : 0.0f;
    for (int f = 0; f < 6; ++f) {
        const FaceDef& F = FACES[f];
        const bool onEdge = (F.n[1] != 0) ? (F.n[1] < 0 || top >= 1.0f) : inset == 0.0f;
        if (onEdge && !ShouldDrawFace(b, w.Get(x + F.n[0], y + F.n[1], z + F.n[2]), y + F.n[1])) continue;
        int tile = f == 2 ? bi.tileTop : (f == 3 ? bi.tileBottom : bi.tileSide);
        float u0, v0, u1, v1;
        if (F.n[1] == 0) SubUV(tile, 0, 16 - top * 16, 16, 16, u0, v0, u1, v1);   // sides show the bottom strip
        else             TileUV(tile, u0, v0, u1, v1);
        Vec3 q[4];
        for (int i = 0; i < 4; ++i) {
            const int* c = F.c[i];
            float lx = c[0] ? 1.0f : 0.0f, ly = c[1] ? top : 0.0f, lz = c[2] ? 1.0f : 0.0f;
            if (F.n[0] == 0) lx = c[0] ? 1.0f - inset : inset;    // pull side faces in...
            else             lx = F.n[0] > 0 ? 1.0f - inset : inset;
            if (F.n[2] == 0) lz = c[2] ? 1.0f - inset : inset;
            else             lz = F.n[2] > 0 ? 1.0f - inset : inset;
            if (F.n[1] != 0) { lx = (float)c[0]; lz = (float)c[2]; }  // ...but tops and bottoms stay full size
            q[i] = {x + lx, y + ly, z + lz};
        }
        PushBlockQuad(out, q, u0, v0, u1, v1, {(float)F.n[0], (float)F.n[1], (float)F.n[2]}, sky, blk);
    }
}

// Minecraft's torch model: a 2x10-pixel stick. Wall torches lean 22.5 degrees
// out from the wall with their foot against it.
static void AddTorchMesh(std::vector<float>& out, const World& w, uint8_t b, int x, int y, int z) {
    const float p = 1.0f / 16.0f;
    Vec3 base = {x + 0.5f, (float)y, z + 0.5f}, up = {0, 1, 0}, f = {1, 0, 0}, side = {0, 0, 1};
    if (b != TORCH) {
        Vec3 dir = b == WALL_TORCH_PX ? Vec3{1, 0, 0} : b == WALL_TORCH_NX ? Vec3{-1, 0, 0}
                 : b == WALL_TORCH_PZ ? Vec3{0, 0, 1} : Vec3{0, 0, -1};
        const float a = 22.5f * PI / 180.0f;
        base = Vec3{x + 0.5f, y + 3.5f * p, z + 0.5f} - dir * 0.5f;
        up   = Vec3{0, std::cos(a), 0} + dir * std::sin(a);
        f    = dir * std::cos(a) - Vec3{0, std::sin(a), 0};
        side = {dir.z, 0, -dir.x};
    }
    auto C = [&](int i, int j, int k) {
        return base + f * (i ? p : -p) + up * (j ? 10 * p : 0.0f) + side * (k ? p : -p);
    };
    const float sky = (float)w.SkyLight(x, y, z), blk = (float)w.BlockLight(x, y, z);
    float u0, v0, u1, v1;
    SubUV(T_TORCH, 7, 6, 9, 16, u0, v0, u1, v1);   // the stick and flame
    Vec3 q[4];
    q[0] = C(1, 0, 0); q[1] = C(1, 0, 1); q[2] = C(1, 1, 1); q[3] = C(1, 1, 0); PushBlockQuad(out, q, u0, v0, u1, v1, f, sky, blk);
    q[0] = C(0, 0, 1); q[1] = C(0, 0, 0); q[2] = C(0, 1, 0); q[3] = C(0, 1, 1); PushBlockQuad(out, q, u0, v0, u1, v1, f * -1.0f, sky, blk);
    q[0] = C(0, 0, 1); q[1] = C(1, 0, 1); q[2] = C(1, 1, 1); q[3] = C(0, 1, 1); PushBlockQuad(out, q, u0, v0, u1, v1, side, sky, blk);
    q[0] = C(1, 0, 0); q[1] = C(0, 0, 0); q[2] = C(0, 1, 0); q[3] = C(1, 1, 0); PushBlockQuad(out, q, u0, v0, u1, v1, side * -1.0f, sky, blk);
    SubUV(T_TORCH, 7, 6, 9, 8, u0, v0, u1, v1);    // flame tip on top
    q[0] = C(0, 1, 0); q[1] = C(1, 1, 0); q[2] = C(1, 1, 1); q[3] = C(0, 1, 1); PushBlockQuad(out, q, u0, v0, u1, v1, up, sky, blk);
    SubUV(T_TORCH, 7, 14, 9, 16, u0, v0, u1, v1);  // end of the stick underneath
    q[0] = C(0, 0, 0); q[1] = C(1, 0, 0); q[2] = C(1, 0, 1); q[3] = C(0, 0, 1); PushBlockQuad(out, q, u0, v0, u1, v1, up * -1.0f, sky, blk);
}

// Grass and flowers: two quads crossing diagonally through the block, seen
// from both sides, like Minecraft's "cross" model
static void AddPlantMesh(std::vector<float>& out, const World& w, uint8_t b, int x, int y, int z) {
    const float sky = (float)w.SkyLight(x, y, z), blk = (float)w.BlockLight(x, y, z);
    const Vec3 tint = FaceTint(w, b, 2, x, z);
    float u0, v0, u1, v1;
    TileUV(Info(b).tileSide, u0, v0, u1, v1);
    const float a = 0.15f, c = 0.85f;   // Minecraft's cross spans 0.8 of the block
    Vec3 quads[2][4] = {
        {{x + a, (float)y, z + a}, {x + c, (float)y, z + c}, {x + c, y + 1.0f, z + c}, {x + a, y + 1.0f, z + a}},
        {{x + a, (float)y, z + c}, {x + c, (float)y, z + a}, {x + c, y + 1.0f, z + a}, {x + a, y + 1.0f, z + c}},
    };
    const Vec3 normals[2] = {Normalize({1, 0, -1}), Normalize({1, 0, 1})};
    for (int k = 0; k < 2; ++k) {
        PushBlockQuad(out, quads[k], u0, v0, u1, v1, normals[k], sky, blk, tint);
        PushBlockQuad(out, quads[k], u0, v0, u1, v1, normals[k] * -1.0f, sky, blk, tint);
    }
}

static void BuildChunk(const World& w, int cx, int cz, std::vector<float>& solid, std::vector<float>& water) {
    solid.clear(); water.clear();
    const int x0 = cx * CHUNK, z0 = cz * CHUNK;
    const Chunk* chunk = w.GetChunk(cx, cz);
    if (!chunk) return;
    for (int x = x0; x < x0 + CHUNK; ++x)
    for (int z = z0; z < z0 + CHUNK; ++z)
    for (int y = 0; y < WY; ++y) {
        uint8_t b = chunk->blocks[Chunk::Idx(x - x0, y, z - z0)];
        if (b == AIR) continue;
        if (IsTorch(b)) { AddTorchMesh(solid, w, b, x, y, z); continue; }
        if (b == SNOW || b == CACTUS) { AddShapeMesh(solid, w, b, x, y, z); continue; }
        if (IsPlant(b)) { AddPlantMesh(solid, w, b, x, y, z); continue; }
        const BlockInfo& bi = Info(b);
        const bool isWater = b == WATER;
        const bool translucent = bi.translucent;          // water and ice go in the blended pass
        const bool snowCapped = b == GRASS && w.Get(x, y + 1, z) == SNOW;
        const float topH = (isWater && w.Get(x, y + 1, z) != WATER) ? 0.875f : 1.0f;

        for (int f = 0; f < 6; ++f) {
            const FaceDef& F = FACES[f];
            int nb[3] = {x + F.n[0], y + F.n[1], z + F.n[2]};
            if (!ShouldDrawFace(b, w.Get(nb[0], nb[1], nb[2]), nb[1])) continue;

            int tile = f == 2 ? bi.tileTop : (f == 3 ? bi.tileBottom : bi.tileSide);
            static const int FRONT_FACE[4] = {0, 1, 4, 5};   // facing +X, -X, +Z, -Z -> FACES index
            if (IsFurnace(b) && f == FRONT_FACE[FurnaceFacing(b)]) tile = IsLitFurnace(b) ? T_FURNACE_FRONT_LIT : T_FURNACE_FRONT;
            if (snowCapped && F.n[1] == 0) tile = T_GRASS_SNOW_SIDE;
            float u0, v0, u1, v1; TileUV(tile, u0, v0, u1, v1);
            const Vec3 tint = FaceTint(w, b, f, x, z);

            int na = F.n[0] ? 0 : (F.n[1] ? 1 : 2);
            int a1 = (na + 1) % 3, a2 = (na + 2) % 3;
            // Smooth lighting: each corner averages the light of the open cells around it
            float ao[4], sky[4], blk[4];
            for (int i = 0; i < 4; ++i) {
                if (isWater) {
                    ao[i] = 1.0f;
                    sky[i] = (float)w.SkyLight(nb[0], nb[1], nb[2]);
                    blk[i] = (float)w.BlockLight(nb[0], nb[1], nb[2]);
                    continue;
                }
                int s1 = F.c[i][a1] ? 1 : -1, s2 = F.c[i][a2] ? 1 : -1;
                int p1[3] = {nb[0], nb[1], nb[2]}; p1[a1] += s1;
                int p2[3] = {nb[0], nb[1], nb[2]}; p2[a2] += s2;
                int pc[3] = {nb[0], nb[1], nb[2]}; pc[a1] += s1; pc[a2] += s2;
                bool o1 = Occludes(w.Get(p1[0], p1[1], p1[2]));
                bool o2 = Occludes(w.Get(p2[0], p2[1], p2[2]));
                bool oc = Occludes(w.Get(pc[0], pc[1], pc[2]));
                int level = (o1 && o2) ? 0 : 3 - ((int)o1 + (int)o2 + (int)oc);
                ao[i] = AO_LEVELS[level];
                float ss = (float)w.SkyLight(nb[0], nb[1], nb[2]), bs = (float)w.BlockLight(nb[0], nb[1], nb[2]);
                int cnt = 1;
                auto add = [&](const int* q) { ss += w.SkyLight(q[0], q[1], q[2]); bs += w.BlockLight(q[0], q[1], q[2]); ++cnt; };
                if (!o1) add(p1);
                if (!o2) add(p2);
                if (!oc && !(o1 && o2)) add(pc);
                sky[i] = ss / cnt;
                blk[i] = bs / cnt;
            }

            // Flip the quad's diagonal when shading is uneven, so it stays symmetric
            static const int ORDER_A[6] = {0, 1, 2, 2, 3, 0};
            static const int ORDER_B[6] = {1, 2, 3, 3, 0, 1};
            float br[4];
            for (int i = 0; i < 4; ++i) br[i] = ao[i] * (std::max(sky[i], blk[i]) + 1.0f);
            const int* order = (br[0] + br[2] < br[1] + br[3]) ? ORDER_B : ORDER_A;

            std::vector<float>& out = translucent ? water : solid;
            auto emit = [&](float eu0, float ev0, float eu1, float ev1, Vec3 t) {
                for (int k = 0; k < 6; ++k) {
                    int i = order[k];
                    const int* c = F.c[i];
                    Vec3 pos = {(float)(x + c[0]), y + (c[1] ? topH : 0.0f), (float)(z + c[2])};
                    PushBlockVert(out, pos, (i == 0 || i == 3) ? eu0 : eu1, i < 2 ? ev0 : ev1,
                                  {(float)F.n[0], (float)F.n[1], (float)F.n[2]}, ao[i], sky[i], blk[i], t);
                }
            };
            emit(u0, v0, u1, v1, tint);
            // Grass sides: the tinted grassy fringe goes on top of the dirt
            if (b == GRASS && F.n[1] == 0 && !snowCapped) {
                float ou0, ov0, ou1, ov1; TileUV(T_GRASS_OVERLAY, ou0, ov0, ou1, ov1);
                emit(ou0, ov0, ou1, ov1, Rgb(w.GrassAt(x, z)));
            }
        }
    }
}

static void MarkMeshDirty(int cx, int cz) {
    auto it = gMeshes.find(World::Key(cx, cz));
    if (it != gMeshes.end()) it->second->dirty = true;
}

// A block changed: re-mesh its chunk, and the neighbour if it's on the edge
static void MarkDirtyAround(int x, int z) {
    int cx = ChunkCoord(x), cz = ChunkCoord(z), lx = x & 15, lz = z & 15;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz) {
            bool okx = dx == 0 || (dx == -1 && lx == 0) || (dx == 1 && lx == CHUNK - 1);
            bool okz = dz == 0 || (dz == -1 && lz == 0) || (dz == 1 && lz == CHUNK - 1);
            if (okx && okz) MarkMeshDirty(cx + dx, cz + dz);
        }
}

// Re-mesh every chunk touching the box (faces sample light one block out)
static void MarkDirtyBox(int x0, int z0, int x1, int z1) {
    for (int cx = ChunkCoord(x0 - 1); cx <= ChunkCoord(x1 + 1); ++cx)
        for (int cz = ChunkCoord(z0 - 1); cz <= ChunkCoord(z1 + 1); ++cz) MarkMeshDirty(cx, cz);
}

static void RebuildMesh(const World& w, int cx, int cz, ChunkMesh& m) {
    static std::vector<float> solid, water;
    BuildChunk(w, cx, cz, solid, water);
    m.solid.Upload(solid, GL_DYNAMIC_DRAW);
    m.water.Upload(water, GL_DYNAMIC_DRAW);
    m.dirty = false;
}

// ============================================================================
// Simple geometry helpers (pos3 uv2 color4)
// ============================================================================
static void PushV(std::vector<float>& v, Vec3 p, float u, float t, float r, float g, float b, float a) {
    v.insert(v.end(), {p.x, p.y, p.z, u, t, r, g, b, a});
}

// Box rotated around Y by yaw (for the third person player model)
static void AddBox(std::vector<float>& v, Vec3 base, float lx0, float lx1, float y0, float y1,
                   float lz0, float lz1, float yaw, float r, float g, float b) {
    const float fx = std::cos(yaw), fz = std::sin(yaw), rx = -fz, rz = fx;
    static const float SHADE[6] = {0.8f, 0.8f, 1.0f, 0.5f, 0.65f, 0.65f};
    for (int f = 0; f < 6; ++f) {
        Vec3 pts[4];
        for (int i = 0; i < 4; ++i) {
            const int* c = FACES[f].c[i];
            // local: x = right axis, z = forward axis
            float lr = c[0] ? lx1 : lx0, lf = c[2] ? lz1 : lz0, ly = c[1] ? y1 : y0;
            pts[i] = {base.x + rx * lr + fx * lf, base.y + ly, base.z + rz * lr + fz * lf};
        }
        float s = SHADE[f];
        static const int ORDER[6] = {0, 1, 2, 2, 3, 0};
        for (int k : ORDER) PushV(v, pts[k], 0, 0, r * s, g * s, b * s, 1);
    }
}

// Dropped block item: a small textured cube spinning around Y
static void AddItemCube(std::vector<float>& v, Vec3 c, float h, float yaw, uint8_t block, float light) {
    const BlockInfo& bi = Info(block);
    const float cs = std::cos(yaw), sn = std::sin(yaw);
    static const float SHADE[6] = {0.8f, 0.8f, 1.0f, 0.5f, 0.65f, 0.65f};
    static const int ORDER[6] = {0, 1, 2, 2, 3, 0};
    for (int f = 0; f < 6; ++f) {
        int tile = f == 2 ? bi.tileTop : (f == 3 ? bi.tileBottom : bi.tileSide);
        float u0, v0, u1, v1; TileUV(tile, u0, v0, u1, v1);
        Vec3 pts[4]; float uv[4][2];
        for (int i = 0; i < 4; ++i) {
            const int* k = FACES[f].c[i];
            float lx = k[0] ? h : -h, ly = k[1] ? h : -h, lz = k[2] ? h : -h;
            pts[i] = {c.x + lx * cs - lz * sn, c.y + ly, c.z + lx * sn + lz * cs};
            uv[i][0] = (i == 0 || i == 3) ? u0 : u1;
            uv[i][1] = i < 2 ? v0 : v1;
        }
        float s = SHADE[f] * light;
        Vec3 t = (f == 2 || block != GRASS) ? IconTint(block) : Vec3{1, 1, 1};
        for (int k : ORDER) PushV(v, pts[k], uv[k][0], uv[k][1], s * t.x, s * t.y, s * t.z, 1);
    }
}

// Dropped flat item (apple): a spinning two-sided sprite
static void AddItemSprite(std::vector<float>& v, Vec3 c, float h, float yaw, int tile, float light) {
    float u0, v0, u1, v1; TileUV(tile, u0, v0, u1, v1);
    Vec3 r = {std::cos(yaw) * h, 0, std::sin(yaw) * h}, up = {0, h, 0};
    Vec3 q[4] = {c - r - up, c + r - up, c + r + up, c - r + up};
    float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
    for (int k : {0, 1, 2, 2, 3, 0}) PushV(v, q[k], uv[k][0], uv[k][1], light, light, light, 1);
}

// ============================================================================
// HUD pixel art: each character is one GUI pixel, '.' is transparent
// ============================================================================
static const char* const HEART[8] = {
    ".KKK.KKK.",
    "KRRRKRRRK",
    "KRWRRRRRK",
    "KRRRRRRRK",
    ".KRRRRRK.",
    "..KRRRK..",
    "...KRK...",
    "....K....",
};
static const char* const SHANK[9] = {
    ".....KKK.",
    "....KMMMK",
    "...KMWMMK",
    "..KMMMMMK",
    ".KMMMMMK.",
    "KBKMMMK..",
    "KBBKKK...",
    ".KBK.....",
    "..K......",
};
static const char* const BUBBLE[9] = {
    "..KKKKK..",
    ".KWWBBBK.",
    "KWWBBBBBK",
    "KWBBBBBBK",
    "KBBBBBBBK",
    "KBBBBBBBK",
    "KBBBBBBBK",
    ".KBBBBBK.",
    "..KKKKK..",
};
static const char* const DIGITS[10][5] = {
    {"###", "#.#", "#.#", "#.#", "###"}, {".#.", "##.", ".#.", ".#.", "###"},
    {"###", "..#", "###", "#..", "###"}, {"###", "..#", ".##", "..#", "###"},
    {"#.#", "#.#", "###", "..#", "..#"}, {"###", "#..", "###", "..#", "###"},
    {"###", "#..", "###", "#.#", "###"}, {"###", "..#", "..#", ".#.", ".#."},
    {"###", "#.#", "###", "#.#", "###"}, {"###", "#.#", "###", "..#", "###"},
};

// A 3x5 pixel font for labels and tooltips (uppercase only)
static const char* const LETTERS[26][5] = {
    {".#.", "#.#", "###", "#.#", "#.#"}, {"##.", "#.#", "##.", "#.#", "##."}, // A B
    {".##", "#..", "#..", "#..", ".##"}, {"##.", "#.#", "#.#", "#.#", "##."}, // C D
    {"###", "#..", "##.", "#..", "###"}, {"###", "#..", "##.", "#..", "#.."}, // E F
    {".##", "#..", "#.#", "#.#", ".##"}, {"#.#", "#.#", "###", "#.#", "#.#"}, // G H
    {"###", ".#.", ".#.", ".#.", "###"}, {"..#", "..#", "..#", "#.#", ".#."}, // I J
    {"#.#", "#.#", "##.", "#.#", "#.#"}, {"#..", "#..", "#..", "#..", "###"}, // K L
    {"#.#", "###", "###", "#.#", "#.#"}, {"##.", "#.#", "#.#", "#.#", "#.#"}, // M N
    {".#.", "#.#", "#.#", "#.#", ".#."}, {"##.", "#.#", "##.", "#..", "#.."}, // O P
    {".#.", "#.#", "#.#", "##.", ".##"}, {"##.", "#.#", "##.", "#.#", "#.#"}, // Q R
    {".##", "#..", ".#.", "..#", "##."}, {"###", ".#.", ".#.", ".#.", ".#."}, // S T
    {"#.#", "#.#", "#.#", "#.#", "###"}, {"#.#", "#.#", "#.#", "#.#", ".#."}, // U V
    {"#.#", "#.#", "###", "###", "#.#"}, {"#.#", "#.#", ".#.", "#.#", "#.#"}, // W X
    {"#.#", "#.#", ".#.", ".#.", ".#."}, {"###", "..#", ".#.", "#..", "###"}, // Y Z
};
static const char* const* Glyph(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return LETTERS[c - 'A'];
    if (c >= '0' && c <= '9') return DIGITS[c - '0'];
    static const char* const SLASH[5] = {"..#", "..#", ".#.", "#..", "#.."};
    if (c == '/') return SLASH;
    static const struct { char c; const char* rows[5]; } PUNCT[] = {
        {'-', {"...", "...", "###", "...", "..."}}, {'_', {"...", "...", "...", "...", "###"}},
        {':', {"...", ".#.", "...", ".#.", "..."}}, {'.', {"...", "...", "...", "...", ".#."}},
        {',', {"...", "...", "...", ".#.", "#.."}}, {'!', {".#.", ".#.", ".#.", "...", ".#."}},
        {'?', {"##.", "..#", ".#.", "...", ".#."}}, {'\'', {".#.", ".#.", "...", "...", "..."}},
        {'(', {"..#", ".#.", ".#.", ".#.", "..#"}}, {')', {"#..", ".#.", ".#.", ".#.", "#.."}},
        {'+', {"...", ".#.", "###", ".#.", "..."}}, {'#', {"#.#", "###", "#.#", "###", "#.#"}},
        {'"', {"#.#", "#.#", "...", "...", "..."}}, {'=', {"...", "###", "...", "###", "..."}},
    };
    for (const auto& g : PUNCT) if (g.c == c) return g.rows;
    return nullptr;   // space and anything else: blank
}

struct Rgba { float r, g, b, a; };

// ============================================================================
// HUD batch (pos2 uv2 color4, pixel coordinates, origin top-left)
// ============================================================================
struct Hud {
    std::vector<float> v;
    void Vert(float x, float y, float u, float t, float r, float g, float b, float a) {
        v.insert(v.end(), {x, y, u, t, r, g, b, a});
    }
    void Rect(float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
        Vert(x0, y0, -1, 0, r, g, b, a); Vert(x1, y0, -1, 0, r, g, b, a); Vert(x1, y1, -1, 0, r, g, b, a);
        Vert(x1, y1, -1, 0, r, g, b, a); Vert(x0, y1, -1, 0, r, g, b, a); Vert(x0, y0, -1, 0, r, g, b, a);
    }
    void Frame(float x0, float y0, float x1, float y1, float t, float r, float g, float b, float a) {
        Rect(x0, y0, x1, y0 + t, r, g, b, a); Rect(x0, y1 - t, x1, y1, r, g, b, a);
        Rect(x0, y0 + t, x0 + t, y1 - t, r, g, b, a); Rect(x1 - t, y0 + t, x1, y1 - t, r, g, b, a);
    }
    // Textured quad from 4 points: bottom-left, bottom-right, top-right, top-left (of the texture)
    void TileQuad(const float p[4][2], int tile, float shade, Vec3 tint = {1, 1, 1}) {
        float u0, v0, u1, v1; TileUV(tile, u0, v0, u1, v1);
        const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
        static const int ORDER[6] = {0, 1, 2, 2, 3, 0};
        for (int k : ORDER) Vert(p[k][0], p[k][1], uv[k][0], uv[k][1], shade * tint.x, shade * tint.y, shade * tint.z, 1);
    }
    // Little isometric cube, like inventory icons
    void BlockIcon(uint8_t b, float cx, float cy, float s) {
        const BlockInfo& bi = Info(b);
        float hw = s * 0.9f, hh = s * 0.5f;
        float topC[2] = {cx, cy - s}, rT[2] = {cx + hw, cy - s + hh}, mid[2] = {cx, cy - s + 2 * hh};
        float lT[2] = {cx - hw, cy - s + hh}, lB[2] = {cx - hw, cy + hh}, bot[2] = {cx, cy + s}, rB[2] = {cx + hw, cy + hh};
        const float top[4][2]   = {{lT[0], lT[1]}, {mid[0], mid[1]}, {rT[0], rT[1]}, {topC[0], topC[1]}};
        const float left[4][2]  = {{lB[0], lB[1]}, {bot[0], bot[1]}, {mid[0], mid[1]}, {lT[0], lT[1]}};
        const float right[4][2] = {{bot[0], bot[1]}, {rB[0], rB[1]}, {rT[0], rT[1]}, {mid[0], mid[1]}};
        Vec3 tint = IconTint(b);
        TileQuad(left, IsFurnace(b) ? (IsLitFurnace(b) ? T_FURNACE_FRONT_LIT : T_FURNACE_FRONT) : bi.tileSide, 0.78f,
                 b == GRASS ? Vec3{1, 1, 1} : tint);
        TileQuad(right, bi.tileSide, 0.6f, b == GRASS ? Vec3{1, 1, 1} : tint);
        if (b == GRASS) {   // grassy fringe over the dirt sides
            TileQuad(left, T_GRASS_OVERLAY, 0.78f, tint);
            TileQuad(right, T_GRASS_OVERLAY, 0.6f, tint);
        }
        TileQuad(top, bi.tileTop, 1.0f, tint);
    }
    // Blocks get the cube, everything else a flat sprite
    void ItemIcon(uint8_t item, float cx, float cy, float s) {
        if (item == AIR) return;
        if (IsPlaceable(item) && !DrawsFlat(item)) { BlockIcon(item, cx, cy, s); return; }
        const float p[4][2] = {{cx - s, cy + s}, {cx + s, cy + s}, {cx + s, cy - s}, {cx - s, cy - s}};
        TileQuad(p, ItemTile(item), 1.0f, IconTint(item));
    }
    // Pixel art; pal maps a character to a color (false = skip). Only
    // columns c0..c1 are drawn, which is how half hearts are made.
    template <class Pal>
    void Pixels(const char* const* rows, int n, float x, float y, float s, Pal pal, int c0 = 0, int c1 = 99) {
        for (int r = 0; r < n; ++r)
            for (int c = 0; rows[r][c]; ++c) {
                Rgba col;
                if (c < c0 || c > c1 || !pal(rows[r][c], col)) continue;
                Rect(x + c * s, y + r * s, x + (c + 1) * s, y + (r + 1) * s, col.r, col.g, col.b, col.a);
            }
    }
    static float TextWidth(const char* text, float s) { return std::max(0.0f, (std::strlen(text) * 4.0f - 1.0f) * s); }
    // Text in the 3x5 font, top-left at (x, y), optionally with a drop shadow
    void Text(const char* text, float x, float y, float s, Rgba color, bool shadow) {
        auto ink = [&](Rgba c) { return [c](char ch, Rgba& out) { out = c; return ch == '#'; }; };
        for (int i = 0; text[i]; ++i) {
            const char* const* g = Glyph(text[i]);
            if (!g) continue;
            float gx = x + i * 4 * s;
            if (shadow) Pixels(g, 5, gx + s, y + s, s, ink({color.r * 0.25f, color.g * 0.25f, color.b * 0.25f, color.a}));
            Pixels(g, 5, gx, y, s, ink(color));
        }
    }
    // Stack count, right-aligned at (right, bottom)
    void Number(int n, float right, float bottom, float s) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%d", n);
        Text(buf, right - TextWidth(buf, s), bottom - 5 * s, s, {1, 1, 1, 1}, true);
    }
    // An item in a 16x16 GUI-pixel slot with top-left (x, y): icon, count,
    // and a durability bar for worn tools
    void DrawStack(const Stack& st, float x, float y, float s) {
        if (st.count <= 0) return;
        float cx = x + 8 * s, cy = y + 8 * s;
        if (IsPlaceable(st.item) && !DrawsFlat(st.item)) ItemIcon(st.item, cx, cy, 6.2f * s);
        else                      ItemIcon(st.item, cx, cy, 7.5f * s);
        if (st.count > 1) Number(st.count, x + 17 * s, y + 17 * s, s);
        int max = MaxDurability(st.item);
        if (max > 0 && st.damage > 0) {
            float f = 1.0f - (float)st.damage / max;
            float w = std::round(13.0f * f);
            Rect(x + 2 * s, y + 13 * s, x + 15 * s, y + 15 * s, 0, 0, 0, 1);
            Rect(x + 2 * s, y + 13 * s, x + 2 * s + w * s, y + 14 * s, 1.0f - f, f, 0, 1);   // green to red
        }
    }
};

// ============================================================================
// Game state
// ============================================================================
static World*      gWorld = nullptr;
static Player      gPlayer;
static GLFWwindow* gWindow = nullptr;
static int         gFbW = 1280, gFbH = 720;

// Creative uses an infinite block palette; survival uses the real inventory
static const uint8_t DEFAULT_CREATIVE_BAR[9] = {GRASS, DIRT, STONE, COBBLESTONE, LOG, PLANKS, GLASS, TORCH, CRATE};
static uint8_t   gCreativeBar[9] = {GRASS, DIRT, STONE, COBBLESTONE, LOG, PLANKS, GLASS, TORCH, CRATE};
static Inventory gInv;
static int       gSelected = 0;
static std::vector<ItemEntity> gItems;

static bool   gCaptured = false, gFirstMouse = true, gThirdPerson = false, gDebug = true;
static double gLastMouseX = 0, gLastMouseY = 0;
static bool   gLmbLatch = false, gRmbLatch = false, gMmbLatch = false;
static bool   gDropLatch = false, gDropStack = false;

static int gTicks = 0;

// ---- Day and night (Minecraft's clock: 24000 ticks = 20 minutes) ----
static const int DAY_TICKS = 24000;
static long long gDayTime = 1000;   // 0 = sunrise, 6000 noon, 12000 sunset, 18000 midnight
static float     gDaylight = 1.0f;  // this frame's daylight, also used for CPU-lit things

// Minecraft's celestial angle: 0 = noon, 0.25 = sunset, 0.5 = midnight, 0.75 = sunrise.
// It lingers a bit around noon and midnight, like the real game.
static float CelestialAngle(double dayTime) {
    double d0 = dayTime / DAY_TICKS - 0.25;
    d0 -= std::floor(d0);
    double d1 = 0.5 - std::cos(d0 * 3.14159265358979) / 2.0;
    return (float)((d0 * 2.0 + d1) / 3.0);
}
// How bright the sky is: full by mid-morning, dark from dusk to dawn
static float DaylightFor(float angle) {
    return std::max(0.0f, std::min(1.0f, std::cos(angle * 2.0f * PI) * 2.0f + 0.2f));
}
static int gEatTicks = 0;                 // how long right click has been held on food
static int gDeadTicks = 0;                // > 0 while the "you died" screen shows
static const int DEATH_SCREEN_TICKS = 40;

// Mining / placing state
static int    gBreakDelay = 0, gPlaceDelay = 0;
static bool   gBreaking = false;
static int    gBX = 0, gBY = 0, gBZ = 0;
static double gBreakProgress = 0;

// Smoothed camera values (updated per tick, interpolated per frame)
static float gFovMod = 1.0f, gPrevFovMod = 1.0f;
static float gEyeH = (float)EYE_HEIGHT, gPrevEyeH = (float)EYE_HEIGHT;

// Minecraft's default mouse sensitivity works out to 0.15 degrees per pixel
static const float MOUSE_RADIANS_PER_PIXEL = 0.15f * PI / 180.0f;
static const float BASE_FOV_DEGREES = 70.0f;

static void SetCapture(bool on) {
    gCaptured = on;
    glfwSetInputMode(gWindow, GLFW_CURSOR, on ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
#ifdef GLFW_RAW_MOUSE_MOTION
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(gWindow, GLFW_RAW_MOUSE_MOTION, on ? GLFW_TRUE : GLFW_FALSE);
#endif
    glfwGetCursorPos(gWindow, &gLastMouseX, &gLastMouseY);   // first real movement counts
    gFirstMouse = false;
}

static uint8_t SelectedItem() {
    if (gPlayer.creative) return gCreativeBar[gSelected];
    const Stack& s = gInv.slots[gSelected];
    return s.count > 0 ? s.item : (uint8_t)AIR;
}

// ============================================================================
// Particles: block bits while mining, torch flames and smoke
// ============================================================================
struct Particle {
    double x, y, z, px, py, pz, vx, vy, vz;
    float  r, g, b, size;        // size = half width
    int    age = 0, life = 20;
    float  gravity = 1.0f;       // 1 = falls like block bits, negative rises
    float  friction = 0.98f;
    bool   collide = true, emissive = false, grow = false, shrink = false;
};
static std::vector<Particle> gParticles;
static atlas::Image gAtlasPixels;           // CPU copy of the atlas, for particle colors
static const size_t MAX_PARTICLES = 4000;

// The color of a random opaque pixel of one of the block's textures
static void BlockPixelColor(uint8_t b, float& r, float& g, float& bl, int wx = 0, int wz = 0) {
    const BlockInfo& bi = Info(b);
    int tiles[3] = {bi.tileTop, bi.tileSide, bi.tileBottom};
    int pickFace = (int)(Rand01() * 3) % 3;
    int tile = tiles[pickFace];
    Vec3 tint = (gWorld && (b != GRASS || pickFace == 0)) ? FaceTint(*gWorld, b, pickFace == 0 ? 2 : 0, wx, wz) : Vec3{1, 1, 1};
    r = g = bl = 0.5f;
    for (int tries = 0; tries < 12; ++tries) {
        int col = (tile % atlas::GRID) * atlas::TILE + (int)(Rand01() * atlas::TILE);
        int row = (tile / atlas::GRID) * atlas::TILE + (int)(Rand01() * atlas::TILE);
        const uint8_t* px = &gAtlasPixels.px[((size_t)row * atlas::SIZE + col) * 4];
        if (px[3] < 128) continue;
        r = px[0] / 255.0f * tint.x; g = px[1] / 255.0f * tint.y; bl = px[2] / 255.0f * tint.z;
        return;
    }
}

// A bit of block, as in Minecraft's TerrainParticle: a random kick in roughly
// the (xa, ya, za) direction plus a small hop
static Particle& AddBlockParticle(double x, double y, double z, double xa, double ya, double za, uint8_t b) {
    Particle q;
    q.x = q.px = x; q.y = q.py = y; q.z = q.pz = z;
    double vx = xa + (Rand01() * 2 - 1) * 0.4, vy = ya + (Rand01() * 2 - 1) * 0.4, vz = za + (Rand01() * 2 - 1) * 0.4;
    double speed = (Rand01() + Rand01() + 1.0) * 0.15;
    double len = std::sqrt(vx * vx + vy * vy + vz * vz);
    if (len < 1e-6) len = 1;
    q.vx = vx / len * speed * 0.4; q.vy = vy / len * speed * 0.4 + 0.1; q.vz = vz / len * speed * 0.4;
    q.size = 0.1f * (float)(Rand01() * 0.5 + 0.5);
    q.life = (int)(4.0 / (Rand01() * 0.9 + 0.1));
    BlockPixelColor(b, q.r, q.g, q.b, (int)std::floor(x), (int)std::floor(z));
    if (gParticles.size() >= MAX_PARTICLES) gParticles.erase(gParticles.begin());
    gParticles.push_back(q);
    return gParticles.back();
}

// A broken block bursts into a 4x4x4 cloud of bits
static void SpawnBreakParticles(int x, int y, int z, uint8_t b) {
    AABB box = BlockBox(b, x, y, z);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k) {
                double fx = (i + 0.5) / 4, fy = (j + 0.5) / 4, fz = (k + 0.5) / 4;
                AddBlockParticle(box.minX + fx * (box.maxX - box.minX), box.minY + fy * (box.maxY - box.minY),
                                 box.minZ + fz * (box.maxZ - box.minZ), fx - 0.5, fy - 0.5, fz - 0.5, b);
            }
}

// While you mine, chips fly off the face you're hitting
static void SpawnHitParticle(int x, int y, int z, int nx, int ny, int nz, uint8_t b) {
    AABB box = BlockBox(b, x, y, z);
    double px = box.minX + Rand01() * (box.maxX - box.minX - 0.2) + 0.1;
    double py = box.minY + Rand01() * (box.maxY - box.minY - 0.2) + 0.1;
    double pz = box.minZ + Rand01() * (box.maxZ - box.minZ - 0.2) + 0.1;
    if (nx > 0) px = box.maxX + 0.1; if (nx < 0) px = box.minX - 0.1;
    if (ny > 0) py = box.maxY + 0.1; if (ny < 0) py = box.minY - 0.1;
    if (nz > 0) pz = box.maxZ + 0.1; if (nz < 0) pz = box.minZ - 0.1;
    Particle& q = AddBlockParticle(px, py, pz, 0, 0, 0, b);
    q.vx *= 0.2; q.vy = (q.vy - 0.1) * 0.2 + 0.1; q.vz *= 0.2;
    q.size *= 0.6f;
}

static void TorchTip(uint8_t b, int x, int y, int z, double& tx, double& ty, double& tz) {
    tx = x + 0.5; ty = y + 0.7; tz = z + 0.5;
    if (b == WALL_TORCH_PX) tx += 0.27; else if (b == WALL_TORCH_NX) tx -= 0.27;
    if (b == WALL_TORCH_PZ) tz += 0.27; else if (b == WALL_TORCH_NZ) tz -= 0.27;
    if (b != TORCH) ty += 0.22;
}

// A torch puffs a little flame and smoke now and then
static void SpawnTorchParticles(uint8_t b, int x, int y, int z) {
    double tx, ty, tz;
    TorchTip(b, x, y, z, tx, ty, tz);
    Particle smoke;
    smoke.x = smoke.px = tx; smoke.y = smoke.py = ty; smoke.z = smoke.pz = tz;
    smoke.vx = (Rand01() - 0.5) * 0.01; smoke.vy = 0.01; smoke.vz = (Rand01() - 0.5) * 0.01;
    float grey = 0.32f + (float)(Rand01() * 0.18);   // soft gray puffs
    smoke.r = smoke.g = smoke.b = grey;
    smoke.size = 0.03f; smoke.grow = true;
    smoke.life = (int)(8.0 / (Rand01() * 0.8 + 0.2));
    smoke.gravity = -0.1f; smoke.friction = 0.96f; smoke.collide = false;
    Particle flame = smoke;
    flame.r = 1.0f; flame.g = 0.78f + (float)Rand01() * 0.15f; flame.b = 0.3f;
    flame.vx = flame.vz = 0; flame.vy = 0.002;
    flame.size = 0.045f; flame.grow = false; flame.shrink = true; flame.emissive = true;
    flame.gravity = 0; flame.life = (int)(8.0 / (Rand01() * 0.8 + 0.2)) + 4;
    if (gParticles.size() + 2 > MAX_PARTICLES) return;
    gParticles.push_back(smoke);
    gParticles.push_back(flame);
}

// Smoke and a flicker of flame at the mouth of a lit furnace
static void SpawnFurnaceParticles(int facing, int x, int y, int z) {
    static const int FX[4] = {1, -1, 0, 0}, FZ[4] = {0, 0, 1, -1};
    double side = (Rand01() - 0.5) * 0.6;
    double px = x + 0.5 + FX[facing] * 0.52 + (FZ[facing] ? side : 0);
    double pz = z + 0.5 + FZ[facing] * 0.52 + (FX[facing] ? side : 0);
    double py = y + Rand01() * 6.0 / 16.0;
    size_t before = gParticles.size();
    SpawnTorchParticles(TORCH, 0, 0, 0);                    // reuse the torch puff...
    if (gParticles.size() != before + 2) return;
    for (size_t i = before; i < gParticles.size(); ++i) {   // ...moved to the furnace mouth
        Particle& q = gParticles[i];
        q.x = q.px = px; q.y = q.py = py; q.z = q.pz = pz;
    }
}

static void TickParticles(const World& w) {
    for (Particle& q : gParticles) {
        q.px = q.x; q.py = q.y; q.pz = q.z;
        ++q.age;
        q.vy -= 0.04 * q.gravity;
        if (q.collide) {
            double dx = q.vx, dy = q.vy, dz = q.vz;
            AABB box = {q.x - 0.1, q.y - 0.1, q.z - 0.1, q.x + 0.1, q.y + 0.1, q.z + 0.1};
            SweepBox(w, box, dx, dy, dz);
            bool onGround = dy != q.vy && q.vy < 0;
            if (dx != q.vx) q.vx = 0;
            if (dy != q.vy) q.vy = 0;
            if (dz != q.vz) q.vz = 0;
            q.x = (box.minX + box.maxX) * 0.5; q.y = (box.minY + box.maxY) * 0.5; q.z = (box.minZ + box.maxZ) * 0.5;
            q.vx *= q.friction; q.vy *= q.friction; q.vz *= q.friction;
            if (onGround) { q.vx *= 0.7; q.vz *= 0.7; }
        } else {
            q.x += q.vx; q.y += q.vy; q.z += q.vz;
            q.vx *= q.friction; q.vy *= q.friction; q.vz *= q.friction;
        }
    }
    gParticles.erase(std::remove_if(gParticles.begin(), gParticles.end(),
                                    [](const Particle& q) { return q.age >= q.life; }), gParticles.end());
}

// The same light curve as the block shader, for things drawn on the CPU side
static Vec3 LightColorAt(const World& w, double x, double y, double z) {
    int bx = (int)std::floor(x), by = (int)std::floor(y), bz = (int)std::floor(z);
    float s = w.SkyLight(bx, by, bz) / 15.0f, b = w.BlockLight(bx, by, bz) / 15.0f;
    float sky = s / (4.0f - 3.0f * s), blk = b / (4.0f - 3.0f * b);
    const float tr = Lerp(0.10f, 1.0f, gDaylight), tg = Lerp(0.12f, 1.0f, gDaylight), tb = Lerp(0.22f, 1.0f, gDaylight);
    Vec3 c = {std::max(sky * tr, blk), std::max(sky * tg, blk * ((blk * 0.6f + 0.4f) * 0.6f + 0.4f)),
              std::max(sky * tb, blk * (blk * blk * 0.6f + 0.4f))};
    auto finish = [](float v) {
        v = v + (0.75f - v) * 0.04f;
        float bright = 1.0f - std::pow(1.0f - v, 4.0f);
        return (v + bright) * 0.5f;
    };
    return {finish(c.x), finish(c.y), finish(c.z)};
}

// Torches in the world, for their flame and smoke
struct TorchPos { int x, y, z; };
static std::vector<TorchPos> gTorches;

// Throws a stack out in front of the player, like pressing Q
static void Toss(const Stack& s) {
    if (s.count <= 0) return;
    const Player& p = gPlayer;
    Vec3 d = DirFromAngles(p.yaw, p.pitch);
    SpawnItem(gItems, p.x, p.y + p.EyeHeight() - 0.3, p.z,
              d.x * 0.3 + (Rand01() - 0.5) * 0.04, d.y * 0.3 + 0.1, d.z * 0.3 + (Rand01() - 0.5) * 0.04, s, 40);
}

static void GiveOrToss(Stack s) {
    gInv.MoveInto(s, 0, INVENTORY_SLOTS);
    Toss(s);
}

// Every furnace's contents, by position (Minecraft's block entities)
static std::map<long long, FurnaceState> gFurnaces;
static long long FurnaceKey(int x, int y, int z) { return BlockKey(x, y, z); }
static FurnaceState* gOpenFurnace = nullptr;

static void CloseScreen(bool recapture = true);

// Every block change in the game goes through here: it re-lights the area,
// re-meshes what changed, tracks torches, and pops off torches that lost
// the block they were hanging on
static void SetBlock(int x, int y, int z, uint8_t b) {
    World& w = *gWorld;
    uint8_t old = w.Get(x, y, z);
    if (old == b || y < 0 || y >= WY || !w.Loaded(x, z)) return;
    w.Set(x, y, z, b);
    MarkDirtyAround(x, z);
    LightBox lb = RelightAround(w, x, y, z);
    if (!lb.Empty()) MarkDirtyBox(lb.x0, lb.z0, lb.x1, lb.z1);

    if (IsTorch(old))
        gTorches.erase(std::remove_if(gTorches.begin(), gTorches.end(),
                       [&](const TorchPos& t) { return t.x == x && t.y == y && t.z == z; }), gTorches.end());
    if (IsTorch(b)) gTorches.push_back({x, y, z});

    // Breaking a furnace spills what was inside it
    if (IsFurnace(old) && !IsFurnace(b)) {
        auto it = gFurnaces.find(FurnaceKey(x, y, z));
        if (it != gFurnaces.end()) {
            if (gOpenFurnace == &it->second) CloseScreen();
            for (Stack* st : {&it->second.input, &it->second.fuel, &it->second.output})
                if (st->count > 0) DropFromBlock(gItems, x, y, z, *st);
            gFurnaces.erase(it);
        }
    }
    if (IsFurnace(b)) gFurnaces[FurnaceKey(x, y, z)];   // make sure it has a state

    // Neighbors that can't stay without this block pop off (torches, cactus, snow)
    static const int N[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const auto& n : N) {
        int tx = x + n[0], ty = y + n[1], tz = z + n[2];
        uint8_t t = w.Get(tx, ty, tz);
        if (t == AIR || CanSurvive(w, t, tx, ty, tz)) continue;
        SpawnBreakParticles(tx, ty, tz, t);
        SetBlock(tx, ty, tz, AIR);
        Stack drop = BlockDrop(t);   // cactus drops itself, torches a torch, snow nothing
        if (drop.count > 0) DropFromBlock(gItems, tx, ty, tz, drop);
    }
}

// ============================================================================
// Container screens (inventory and crafting table)
//
// Laid out in Minecraft's 176 x 166 GUI-pixel panel. Slot positions are the
// top-left of the 16 x 16 item area, in GUI pixels from the panel corner.
// ============================================================================
enum class Screen { None, Inventory, Crafting, Furnace };
static Screen     gScreen = Screen::None;
static Stack      gCursor;       // the stack carried on the mouse
static Stack      gGrid[9];      // crafting grid, row-major, GridN() wide
static Stack      gOutput;       // what the grid currently makes
static int        gOpenX = 0, gOpenY = 0, gOpenZ = 0;   // the table or furnace the screen belongs to

static RecipeBook gRecipes;
static std::string gRecipesPath;

static const int PANEL_W = 176, PANEL_H = 166;

static int GridN() { return gScreen == Screen::Crafting ? 3 : 2; }

static void UpdateOutput() {
    const Recipe* r = gRecipes.Match(gGrid, GridN());
    gOutput = r ? r->result : Stack{};
}

enum SlotKind { SLOT_INV, SLOT_GRID, SLOT_OUTPUT, SLOT_FURNACE_IN, SLOT_FURNACE_FUEL, SLOT_FURNACE_OUT };
static bool BigSlot(SlotKind k) { return (k == SLOT_OUTPUT && gScreen == Screen::Crafting) || k == SLOT_FURNACE_OUT; }

// Can this stack be put into the slot by hand?
static bool SlotAccepts(SlotKind k, const Stack& st) {
    if (k == SLOT_OUTPUT || k == SLOT_FURNACE_OUT) return false;
    if (k == SLOT_FURNACE_FUEL) return FuelTicks(st.item) > 0;
    return true;
}
struct UiSlot { int x, y; Stack* s; SlotKind kind; int index; };

static std::vector<UiSlot> UiSlots() {
    std::vector<UiSlot> v;
    for (int i = 0; i < 27; ++i)
        v.push_back({8 + (i % 9) * 18, 84 + (i / 9) * 18, &gInv.slots[HOTBAR_SLOTS + i], SLOT_INV, HOTBAR_SLOTS + i});
    for (int i = 0; i < HOTBAR_SLOTS; ++i)
        v.push_back({8 + i * 18, 142, &gInv.slots[i], SLOT_INV, i});
    if (gScreen == Screen::Furnace) {
        v.push_back({56, 17, &gOpenFurnace->input, SLOT_FURNACE_IN, 0});
        v.push_back({56, 53, &gOpenFurnace->fuel, SLOT_FURNACE_FUEL, 0});
        v.push_back({116, 35, &gOpenFurnace->output, SLOT_FURNACE_OUT, 0});
        return v;
    }
    const int n = GridN();
    const int gx = n == 3 ? 30 : 98, gy = n == 3 ? 17 : 18;
    for (int r = 0; r < n; ++r)
        for (int c = 0; c < n; ++c)
            v.push_back({gx + c * 18, gy + r * 18, &gGrid[r * n + c], SLOT_GRID, r * n + c});
    v.push_back({n == 3 ? 124 : 154, n == 3 ? 35 : 28, &gOutput, SLOT_OUTPUT, 0});
    return v;
}

static float GuiScale() { return (float)std::max(1, std::min(gFbH / 240, gFbW / 320)); }

static void PanelOrigin(float& ox, float& oy) {
    const float S = GuiScale();
    ox = std::floor((gFbW - PANEL_W * S) / 2);
    oy = std::floor((gFbH - PANEL_H * S) / 2);
}

// Mouse position in framebuffer pixels (differs from window coords on high DPI)
static void MouseFb(float& mx, float& my) {
    double cx, cy;
    int ww, wh;
    glfwGetCursorPos(gWindow, &cx, &cy);
    glfwGetWindowSize(gWindow, &ww, &wh);
    mx = (float)(cx * (ww > 0 ? (double)gFbW / ww : 1.0));
    my = (float)(cy * (wh > 0 ? (double)gFbH / wh : 1.0));
}

// Which slot the mouse is over (-1 none); also reports whether it's inside the panel
static int HoveredSlot(const std::vector<UiSlot>& slots, bool* insidePanel = nullptr) {
    float mx, my, ox, oy;
    MouseFb(mx, my);
    PanelOrigin(ox, oy);
    const float S = GuiScale();
    float gx = (mx - ox) / S, gy = (my - oy) / S;
    if (insidePanel) *insidePanel = gx >= 0 && gy >= 0 && gx < PANEL_W && gy < PANEL_H;
    for (size_t i = 0; i < slots.size(); ++i) {
        int pad = BigSlot(slots[i].kind) ? 5 : 1;   // output slots are the big ones
        if (gx >= slots[i].x - pad && gx < slots[i].x + 16 + pad && gy >= slots[i].y - pad && gy < slots[i].y + 16 + pad)
            return (int)i;
    }
    return -1;
}

static void ConsumeGrid() {
    const int n = GridN();
    for (int i = 0; i < n * n; ++i)
        if (gGrid[i].count > 0 && --gGrid[i].count == 0) gGrid[i] = {};
}

static void CraftClick(bool shift) {
    if (gOutput.count <= 0) return;
    if (shift) {
        // Craft as many as the grid and the inventory allow
        for (int guard = 0; guard < 64 && gOutput.count > 0 && gInv.Room(gOutput) >= gOutput.count; ++guard) {
            Stack made = gOutput;
            gInv.MoveInto(made, HOTBAR_SLOTS, INVENTORY_SLOTS);
            gInv.MoveInto(made, 0, HOTBAR_SLOTS);
            ConsumeGrid();
            UpdateOutput();
        }
        return;
    }
    if (gCursor.count == 0) gCursor = gOutput;
    else if (CanMerge(gCursor, gOutput) && gCursor.count + gOutput.count <= MaxStack(gOutput.item)) gCursor.count += gOutput.count;
    else return;
    ConsumeGrid();
    UpdateOutput();
}

// Moves as much of src as fits onto dst (a single slot)
static void MergeInto(Stack& dst, Stack& src) {
    if (src.count <= 0) return;
    if (dst.count == 0) { dst = src; src = {}; return; }
    if (!CanMerge(dst, src)) return;
    int n = std::min(src.count, MaxStack(dst.item) - dst.count);
    dst.count += n;
    if ((src.count -= n) == 0) src = {};
}

static void ClickSlot(const UiSlot& sl, int button, bool shift) {
    if (sl.kind == SLOT_OUTPUT) { CraftClick(shift); return; }
    Stack& s = *sl.s;

    if (shift && button == GLFW_MOUSE_BUTTON_LEFT) {
        if (s.count > 0) {
            if (sl.kind != SLOT_INV) {
                // Grid / furnace -> inventory
                gInv.MoveInto(s, HOTBAR_SLOTS, INVENTORY_SLOTS);
                gInv.MoveInto(s, 0, HOTBAR_SLOTS);
            } else if (gScreen == Screen::Furnace && gRecipes.Smelting(s.item)) {
                MergeInto(gOpenFurnace->input, s);              // smeltables go in the top
            } else if (gScreen == Screen::Furnace && FuelTicks(s.item) > 0) {
                MergeInto(gOpenFurnace->fuel, s);               // fuel goes underneath
            } else if (sl.index < HOTBAR_SLOTS) {
                gInv.MoveInto(s, HOTBAR_SLOTS, INVENTORY_SLOTS);
            } else {
                gInv.MoveInto(s, 0, HOTBAR_SLOTS);
            }
        }
    } else if (!SlotAccepts(sl.kind, gCursor) && gCursor.count > 0) {
        // Output slots (and fuel slots for non-fuel) only let you take
        if (s.count > 0 && CanMerge(gCursor, s)) MergeInto(gCursor, s);
    } else if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (gCursor.count == 0)          { gCursor = s; s = {}; }
        else if (s.count == 0)           { s = gCursor; gCursor = {}; }
        else if (CanMerge(s, gCursor)) {
            int n = std::min(gCursor.count, MaxStack(s.item) - s.count);
            s.count += n;
            if ((gCursor.count -= n) == 0) gCursor = {};
        } else std::swap(s, gCursor);
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (gCursor.count == 0) {
            if (s.count > 0) {                       // pick up half (rounded up)
                gCursor = s;
                gCursor.count = (s.count + 1) / 2;
                if ((s.count -= gCursor.count) == 0) s = {};
            }
        } else if (s.count == 0) {                   // put one down
            s = gCursor; s.count = 1;
            if (--gCursor.count == 0) gCursor = {};
        } else if (CanMerge(s, gCursor) && s.count < MaxStack(s.item)) {
            ++s.count;
            if (--gCursor.count == 0) gCursor = {};
        } else std::swap(s, gCursor);
    }
    if (sl.kind == SLOT_GRID) UpdateOutput();
}

// Right-drag: while the button is held, every new slot the mouse crosses
// gets one item from the cursor (empty or matching slots only)
static bool gRightDrag = false;
static std::vector<const Stack*> gDragVisited;

static void UiDragMove() {
    if (!gRightDrag || gCursor.count <= 0) return;
    std::vector<UiSlot> slots = UiSlots();
    int i = HoveredSlot(slots);
    if (i < 0 || !SlotAccepts(slots[i].kind, gCursor)) return;
    Stack& s = *slots[i].s;
    if (std::find(gDragVisited.begin(), gDragVisited.end(), &s) != gDragVisited.end()) return;
    gDragVisited.push_back(&s);
    if (s.count == 0)                                              { s = gCursor; s.count = 1; }
    else if (CanMerge(s, gCursor) && s.count < MaxStack(s.item))   ++s.count;
    else return;
    if (--gCursor.count == 0) gCursor = {};
    if (slots[i].kind == SLOT_GRID) UpdateOutput();
}

static void UiMouseButton(int button, bool shift) {
    if (button != GLFW_MOUSE_BUTTON_LEFT && button != GLFW_MOUSE_BUTTON_RIGHT) return;
    std::vector<UiSlot> slots = UiSlots();
    bool inside = false;
    int i = HoveredSlot(slots, &inside);
    if (i >= 0) {
        // A right press while carrying something starts a drag from this slot
        if (button == GLFW_MOUSE_BUTTON_RIGHT && !shift && gCursor.count > 0 && SlotAccepts(slots[i].kind, gCursor)) {
            gRightDrag = true;
            gDragVisited.assign(1, slots[i].s);
        }
        ClickSlot(slots[i], button, shift);
        return;
    }
    if (!inside && gCursor.count > 0) {             // clicking outside drops what you carry
        Stack t = gCursor;
        if (button == GLFW_MOUSE_BUTTON_RIGHT) t.count = 1;
        Toss(t);
        if ((gCursor.count -= t.count) <= 0) gCursor = {};
    }
}

// 1-9 over a slot swaps it with that hotbar slot
static void UiNumberKey(int hotbar) {
    std::vector<UiSlot> slots = UiSlots();
    int i = HoveredSlot(slots);
    if (i < 0) return;
    const UiSlot& sl = slots[i];
    Stack& hb = gInv.slots[hotbar];
    if (sl.kind == SLOT_OUTPUT) {
        if (gOutput.count > 0 && hb.count == 0) { hb = gOutput; ConsumeGrid(); UpdateOutput(); }
        return;
    }
    if (sl.kind == SLOT_FURNACE_OUT) { if (hb.count == 0) std::swap(*sl.s, hb); return; }
    if (hb.count > 0 && !SlotAccepts(sl.kind, hb)) return;
    if (sl.s != &hb) std::swap(*sl.s, hb);
    if (sl.kind == SLOT_GRID) UpdateOutput();
}

static void OpenScreen(Screen s) {
    gScreen = s;
    gBreaking = false; gBreakProgress = 0; gEatTicks = 0;
    gLmbLatch = gRmbLatch = gMmbLatch = gDropLatch = false;
    SetCapture(false);
    UpdateOutput();
}

// Closing hands everything in the grid and on the cursor back (or drops it)
static void CloseScreen(bool recapture) {
    if (gScreen == Screen::None) return;
    for (Stack& s : gGrid) { GiveOrToss(s); s = {}; }
    GiveOrToss(gCursor);
    gCursor = {};
    gOutput = {};
    gRightDrag = false;
    gOpenFurnace = nullptr;   // a furnace keeps its contents
    gScreen = Screen::None;
    if (recapture) SetCapture(true);
}

static void LoadRecipes() {
    std::string log;
    RecipeBook book;
    const std::string candidates[] = {"assets/recipes.json", std::string(MYENGINE_ASSETS) + "/recipes.json"};
    for (const std::string& path : candidates) {
        std::string attempt;
        if (book.Load(path, attempt)) {
            gRecipes = book;
            gRecipesPath = path;
            std::printf("%sLoaded %d recipes from %s\n", attempt.c_str(), (int)gRecipes.recipes.size(), path.c_str());
            if (gScreen != Screen::None) UpdateOutput();
            return;
        }
        log += attempt;
    }
    std::printf("%sNo recipes loaded: crafting won't make anything.\n", log.c_str());
}

// ============================================================================
// Chunk streaming: chunks around the player are loaded or generated,
// decorated, lit and meshed nearest-first on a per-frame time budget, and
// the ones left far behind are saved and dropped.
// ============================================================================
static int  gRenderDistance = 10;          // chunks
static int  gSpawnX = 0, gSpawnZ = 0;      // the world spawn column
static bool gNeedsSpawn = false;           // put the player on the ground once it's loaded
static std::string gWorldDir;              // where the world being played is saved

static void TrackTorches(const Chunk& c, bool add) {
    const int x0 = c.cx * CHUNK, z0 = c.cz * CHUNK;
    if (!add) {
        gTorches.erase(std::remove_if(gTorches.begin(), gTorches.end(), [&](const TorchPos& t) {
            return t.x >= x0 && t.x < x0 + CHUNK && t.z >= z0 && t.z < z0 + CHUNK; }), gTorches.end());
        return;
    }
    for (int lx = 0; lx < CHUNK; ++lx)
        for (int lz = 0; lz < CHUNK; ++lz)
            for (int y = 0; y < WY; ++y)
                if (IsTorch(c.blocks[Chunk::Idx(lx, y, lz)])) gTorches.push_back({x0 + lx, y, z0 + lz});
}

// Chunk offsets within a radius, nearest first
static const std::vector<std::pair<int, int>>& OffsetsByDistance(int radius) {
    static int cached = -1;
    static std::vector<std::pair<int, int>> list;
    if (cached != radius) {
        list.clear();
        for (int dx = -radius; dx <= radius; ++dx)
            for (int dz = -radius; dz <= radius; ++dz)
                if (dx * dx + dz * dz <= radius * radius) list.push_back({dx, dz});
        std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
            return a.first * a.first + a.second * a.second < b.first * b.first + b.second * b.second; });
        cached = radius;
    }
    return list;
}

// Where streaming is centred: the player, or the spawn point while respawning
static void StreamCenter(int& cx, int& cz) {
    if (gNeedsSpawn) { cx = ChunkCoord(gSpawnX); cz = ChunkCoord(gSpawnZ); return; }
    cx = ChunkCoord((int)std::floor(gPlayer.x));
    cz = ChunkCoord((int)std::floor(gPlayer.z));
}

// Is the area around (cx, cz) ready to play in (lit and meshed)?
static bool AreaReady(int cx, int cz, int radius) {
    for (int dx = -radius; dx <= radius; ++dx)
        for (int dz = -radius; dz <= radius; ++dz) {
            const Chunk* c = gWorld->GetChunk(cx + dx, cz + dz);
            if (!c || !c->lit) return false;
            auto it = gMeshes.find(World::Key(cx + dx, cz + dz));
            if (it == gMeshes.end() || it->second->dirty) return false;
        }
    return true;
}

static void UnloadChunk(Chunk& c) {
    if (!gWorldDir.empty() && c.modified) {
        std::string err;
        if (!SaveChunk(gWorldDir, c, err)) std::printf("Couldn't save chunk %d,%d: %s\n", c.cx, c.cz, err.c_str());
    }
    TrackTorches(c, false);
    gMeshes.erase(World::Key(c.cx, c.cz));
    gWorld->RemoveChunk(c.cx, c.cz);
}

// One round of streaming work, stopping once `budget` seconds are used
static void UpdateChunks(double budget) {
    if (!gWorld) return;
    World& w = *gWorld;
    const double start = glfwGetTime();
    auto outOfTime = [&] { return glfwGetTime() - start > budget; };
    int pcx, pcz;
    StreamCenter(pcx, pcz);
    const int R = gRenderDistance;

    // Edits near the player re-mesh right away, whatever the budget
    for (int dx = -2; dx <= 2; ++dx)
        for (int dz = -2; dz <= 2; ++dz) {
            auto it = gMeshes.find(World::Key(pcx + dx, pcz + dz));
            if (it != gMeshes.end() && it->second->dirty) RebuildMesh(w, pcx + dx, pcz + dz, *it->second);
        }

    for (const auto& off : OffsetsByDistance(R + 2)) {
        if (outOfTime()) break;
        const int cx = pcx + off.first, cz = pcz + off.second;
        const int d2 = off.first * off.first + off.second * off.second;
        Chunk* c = w.GetChunk(cx, cz);
        if (!c) {                                             // 1. load it, or make its terrain
            std::string problem;
            if (!gWorldDir.empty() && LoadChunk(gWorldDir, w, cx, cz, &problem)) {
                TrackTorches(*w.GetChunk(cx, cz), true);
            } else {
                if (!problem.empty()) std::printf("%s; generating it again\n", problem.c_str());
                GenerateTerrain(w, w.EnsureChunk(cx, cz));
            }
            continue;
        }
        if (d2 > (R + 1) * (R + 1)) continue;
        if (c->stage < STAGE_DECORATED) {                     // 2. trees and the rest
            if (NeighboursAt(w, cx, cz, STAGE_TERRAIN)) DecorateChunk(w, *c);
            continue;
        }
        if (d2 > R * R) continue;
        if (!c->lit) {                                        // 3. light
            if (!NeighboursAt(w, cx, cz, STAGE_DECORATED)) continue;
            ComputeChunkTops(*c);
            LightBox changed = RelightChunk(w, cx, cz);
            c->lit = true;
            if (!changed.Empty()) MarkDirtyBox(changed.x0, changed.z0, changed.x1, changed.z1);
            gMeshes[World::Key(cx, cz)] = std::make_unique<ChunkMesh>();
            continue;
        }
        auto it = gMeshes.find(World::Key(cx, cz));          // 4. mesh
        if (it == gMeshes.end()) it = gMeshes.emplace(World::Key(cx, cz), std::make_unique<ChunkMesh>()).first;
        if (it->second->dirty) RebuildMesh(w, cx, cz, *it->second);
    }

    // Save and drop chunks far behind (a little past the loading ring, so
    // walking back and forth over a border doesn't thrash)
    std::vector<Chunk*> far;
    for (auto& kv : w.chunks) {
        int dx = kv.second->cx - pcx, dz = kv.second->cz - pcz;
        if (dx * dx + dz * dz > (R + 4) * (R + 4)) far.push_back(kv.second.get());
    }
    for (Chunk* c : far) UnloadChunk(*c);
    // Meshes beyond the view distance aren't needed either
    for (auto it = gMeshes.begin(); it != gMeshes.end();) {
        int cx = (int)(int32_t)(it->first >> 32), cz = (int)(int32_t)(it->first & 0xFFFFFFFF);
        int dx = cx - pcx, dz = cz - pcz;
        it = (dx * dx + dz * dz > (R + 1) * (R + 1)) ? gMeshes.erase(it) : std::next(it);
    }

    // Respawning: once the spawn area exists, stand on its ground
    if (gNeedsSpawn && AreaReady(pcx, pcz, 1)) {
        if (!SpawnNear(w, gPlayer, gSpawnX, gSpawnZ)) {
            int y = WY - 1;
            while (y > 0 && !w.SolidAt(gSpawnX, y, gSpawnZ)) --y;
            gPlayer.x = gSpawnX + 0.5; gPlayer.y = y + 1.0; gPlayer.z = gSpawnZ + 0.5;
            gPlayer.px = gPlayer.x; gPlayer.py = gPlayer.y; gPlayer.pz = gPlayer.z;
        }
        gNeedsSpawn = false;
    }
}

// Is the ground under the player loaded and lit (so physics can run)?
static bool PlayerAreaReady() {
    if (gNeedsSpawn) return false;
    const Chunk* c = gWorld->ChunkAt((int)std::floor(gPlayer.x), (int)std::floor(gPlayer.z));
    return c && c->lit;
}

// ============================================================================
// Worlds: start, load, save, leave
// ============================================================================
enum class App { Title, Worlds, Create, ConfirmDelete, Playing };
static App         gApp = App::Title;
static bool        gPaused = false;
static std::string gWorldName;                     // the world being played
static int         gAutosave = 0;                  // ticks since the last save
static double      gSavedAt = -100;                // when "Saved" was last shown
static std::string gMenuMessage;                   // e.g. why a world wouldn't load
static std::function<void(const char*)> gShowMessage;   // draws a "Loading..." frame
static const char* SAVES_ROOT = "saves";

static void Pause() {
    if (gApp != App::Playing || gPaused) return;
    gPaused = true;
    gLmbLatch = gRmbLatch = gMmbLatch = gDropLatch = false;
    gBreaking = false; gBreakProgress = 0; gEatTicks = 0;
    SetCapture(false);
}
static void Resume() {
    if (!gPaused) return;
    gPaused = false;
    SetCapture(true);
}

// Back to a blank slate between worlds
static void ResetSession() {
    if (gScreen != Screen::None) CloseScreen(false);
    gInv = Inventory();
    gItems.clear();
    gFurnaces.clear();
    gOpenFurnace = nullptr;
    gTorches.clear();
    gParticles.clear();
    gCursor = {};
    for (Stack& st : gGrid) st = {};
    gOutput = {};
    gPlayer = Player();
    gSelected = 0;
    std::memcpy(gCreativeBar, DEFAULT_CREATIVE_BAR, sizeof gCreativeBar);
    gDayTime = 1000;
    gDeadTicks = gEatTicks = 0;
    gBreaking = false; gBreakProgress = 0; gBreakDelay = gPlaceDelay = 0;
    gLmbLatch = gRmbLatch = gMmbLatch = gDropLatch = false;
    gFovMod = gPrevFovMod = 1.0f;
    gEyeH = gPrevEyeH = (float)EYE_HEIGHT;
    gPaused = false;
    gAutosave = 0;
    gNeedsSpawn = false;
    gMeshes.clear();
}

// Writes the world being played. Items in a crafting grid or on the cursor
// are saved as if they'd been put back in the inventory.
static bool SaveGame() {
    if (!gWorld || gWorldDir.empty()) return false;
    SaveData d;
    d.name = gWorldName;
    d.dayTime = gDayTime;
    d.lastPlayed = (long long)std::time(nullptr);
    d.player = gPlayer;
    d.spawnX = gSpawnX;
    d.spawnZ = gSpawnZ;
    d.inv = gInv;
    std::memcpy(d.creativeBar, gCreativeBar, sizeof gCreativeBar);
    d.selected = gSelected;
    d.items = gItems;
    d.furnaces = gFurnaces;
    std::vector<Stack> loose(std::begin(gGrid), std::end(gGrid));
    loose.push_back(gCursor);
    for (Stack st : loose) {
        d.inv.MoveInto(st, 0, INVENTORY_SLOTS);
        if (st.count > 0) SpawnItem(d.items, gPlayer.x, gPlayer.y + 1.0, gPlayer.z, 0, 0, 0, st, 10);
    }
    std::string err;
    if (!SaveWorld(gWorldDir, *gWorld, d, err)) {
        std::printf("Saving failed: %s\n", err.c_str());
        return false;
    }
    gSavedAt = glfwGetTime();
    std::printf("Saved \"%s\" to %s\n", gWorldName.c_str(), gWorldDir.c_str());
    return true;
}

// The world is set up in gWorld and the globals: stream in the area around
// the player (showing progress), then start playing
static void EnterWorld() {
    gTorches.clear();
    for (auto& kv : gWorld->chunks) TrackTorches(*kv.second, true);
    double t0 = glfwGetTime(), shown = 0;
    int cx, cz;
    StreamCenter(cx, cz);
    while (!AreaReady(cx, cz, 2) && glfwGetTime() - t0 < 30.0) {
        UpdateChunks(0.05);
        StreamCenter(cx, cz);
        if (gShowMessage && glfwGetTime() - shown > 0.1) {
            int ready = 0;
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz) {
                    const Chunk* c = gWorld->GetChunk(cx + dx, cz + dz);
                    ready += c && c->lit;
                }
            char msg[64];
            std::snprintf(msg, sizeof msg, "PREPARING SPAWN AREA: %d%%", ready * 100 / 25);
            gShowMessage(msg);
            glfwPollEvents();
            shown = glfwGetTime();
        }
    }
    std::printf("Spawn area ready in %.2fs\n", glfwGetTime() - t0);
    gApp = App::Playing;
    gPaused = false;
    gMenuMessage.clear();
    SetCapture(true);
}

static void CreateNewWorld(const std::string& name, uint32_t seed, bool creative) {
    if (gShowMessage) gShowMessage("GENERATING WORLD...");
    ResetSession();
    gWorld = new World();
    gWorld->seed = seed;
    FindSpawnColumn(seed, gSpawnX, gSpawnZ);
    gPlayer.x = gSpawnX + 0.5; gPlayer.z = gSpawnZ + 0.5; gPlayer.y = WY;
    gPlayer.yaw = PI * 0.25f;
    gPlayer.creative = creative;
    gNeedsSpawn = true;
    gWorldName = name;
    gWorldDir = NewWorldDir(SAVES_ROOT, name);
    std::printf("Creating \"%s\" (seed %u), spawn near %d, %d\n", name.c_str(), seed, gSpawnX, gSpawnZ);
    EnterWorld();
    SaveGame();
}

static void OpenWorld(const SaveInfo& info) {
    if (gShowMessage) gShowMessage("LOADING WORLD...");
    ResetSession();
    World* w = new World();
    SaveData d;
    std::string err;
    if (!LoadWorld(info.dir, *w, d, err)) {
        delete w;
        gMenuMessage = "COULDN'T LOAD " + info.name + ": " + err;
        std::printf("Couldn't load %s: %s\n", info.dir.c_str(), err.c_str());
        return;
    }
    if (d.convertedFromV1) {   // keep the original file: the next save replaces it with the new format
        std::error_code ec;
        std::filesystem::copy_file(info.dir + "/world.dat", info.dir + "/world_v1_backup.dat",
                                   std::filesystem::copy_options::skip_existing, ec);
        std::printf("Converting \"%s\" from the old 256x256 format (original kept as world_v1_backup.dat)\n", d.name.c_str());
    }
    gWorld = w;
    gPlayer = d.player;
    gInv = d.inv;
    std::memcpy(gCreativeBar, d.creativeBar, sizeof gCreativeBar);
    gSelected = d.selected;
    gItems = d.items;
    gFurnaces = d.furnaces;
    gDayTime = d.dayTime;
    gSpawnX = d.spawnX;
    gSpawnZ = d.spawnZ;
    gWorldName = d.name;
    gWorldDir = info.dir;
    std::printf("Loaded \"%s\" from %s\n", gWorldName.c_str(), gWorldDir.c_str());
    EnterWorld();
    SaveGame();   // writes out anything converted from an old save or generated while loading
}

static void LeaveWorld() {
    SaveGame();
    std::vector<Chunk*> all;
    for (auto& kv : gWorld->chunks) all.push_back(kv.second.get());
    for (Chunk* c : all) UnloadChunk(*c);
    ResetSession();
    delete gWorld;
    gWorld = nullptr;
    gWorldDir.clear();
    gApp = App::Title;
    SetCapture(false);
}

// ============================================================================
// Menus: title screen, world list, create world, delete, pause
//
// Immediate mode: each frame the menu is drawn and its clickable widgets
// are recorded; clicks are matched against last frame's widgets.
// ============================================================================
enum MenuId {
    M_NONE, M_SINGLEPLAYER, M_QUIT, M_PLAY, M_GO_CREATE, M_DELETE, M_BACK, M_CREATE, M_MODE,
    M_CONFIRM_DELETE, M_RESUME, M_QUIT_TO_TITLE, M_QUIT_GAME, M_FIELD_NAME, M_FIELD_SEED,
    M_WORLD_ROW = 1000   // + index into gSaves
};
struct Widget { float x0, y0, x1, y1; int id; bool enabled; };
static std::vector<Widget>   gWidgets;
static std::vector<SaveInfo> gSaves;
static int         gSelectedSave = -1, gSaveScroll = 0;
static double      gLastRowClick = -1;
static std::string gFieldName = "New World", gFieldSeed;
static int         gFocusField = 0;               // 0 name, 1 seed
static bool        gCreateCreative = false;
static const int   VISIBLE_ROWS = 5;

static const char* const SPLASHES[] = {
    "NOW WITH FURNACES!", "PUNCH TREES!", "100% PROCEDURAL!", "MIND THE CACTUS!", "TORCHES SOLD SEPARATELY",
    "THE DOT PRODUCT FROM DAY ONE!", "ALSO TRY MINECRAFT!", "DIG DOWN (CAREFULLY)", "BIOMES!", "SAVES NOW!",
};
static int gSplash = 0;

static void RefreshSaves() {
    gSaves = ListSaves(SAVES_ROOT);
    if (gSelectedSave >= (int)gSaves.size()) gSelectedSave = -1;
    gSaveScroll = std::max(0, std::min(gSaveScroll, (int)gSaves.size() - VISIBLE_ROWS));
}

static bool MouseIn(float x0, float y0, float x1, float y1) {
    float mx, my;
    MouseFb(mx, my);
    return mx >= x0 && mx < x1 && my >= y0 && my < y1;
}

static void MenuBackground(Hud& hud) {
    // Minecraft's classic menu backdrop: dark tiled dirt
    const float S = GuiScale(), tile = 32 * S;
    for (float y = 0; y < gFbH; y += tile)
        for (float x = 0; x < gFbW; x += tile) {
            const float q[4][2] = {{x, y + tile}, {x + tile, y + tile}, {x + tile, y}, {x, y}};
            hud.TileQuad(q, T_DIRT, 0.25f);
        }
}

static void CenteredText(Hud& hud, const char* text, float cy, float scale, Rgba color) {
    hud.Text(text, std::floor(gFbW / 2.0f - Hud::TextWidth(text, scale) / 2), cy, scale, color, true);
}

// A Minecraft-style button, w x 20 GUI pixels, centered on cx
static void Button(Hud& hud, float cx, float y, float w, const char* label, int id, bool enabled = true) {
    const float S = GuiScale();
    float x0 = std::floor(cx - w * S / 2), x1 = x0 + w * S, y1 = y + 20 * S;
    bool hover = enabled && MouseIn(x0, y, x1, y1);
    gWidgets.push_back({x0, y, x1, y1, id, enabled});
    hud.Rect(x0, y, x1, y1, 0, 0, 0, 1);
    Rgba fill = !enabled ? Rgba{0.2f, 0.2f, 0.2f, 1} : hover ? Rgba{0.42f, 0.47f, 0.68f, 1} : Rgba{0.44f, 0.44f, 0.44f, 1};
    hud.Rect(x0 + S, y + S, x1 - S, y1 - S, fill.r, fill.g, fill.b, 1);
    hud.Rect(x0 + S, y + S, x1 - S, y + 2 * S, fill.r * 1.35f, fill.g * 1.35f, fill.b * 1.35f, 1);   // lit top edge
    hud.Rect(x0 + S, y1 - 3 * S, x1 - S, y1 - S, fill.r * 0.7f, fill.g * 0.7f, fill.b * 0.7f, 1);    // shaded bottom
    if (hover) hud.Frame(x0, y, x1, y1, S, 1, 1, 1, 1);
    Rgba ink = !enabled ? Rgba{0.63f, 0.63f, 0.63f, 1} : hover ? Rgba{1, 1, 0.63f, 1} : Rgba{0.88f, 0.88f, 0.88f, 1};
    hud.Text(label, std::floor(cx - Hud::TextWidth(label, S) / 2), y + 8 * S, S, ink, true);
}

static void TextField(Hud& hud, float cx, float y, float w, const std::string& text, bool focused, int id) {
    const float S = GuiScale();
    float x0 = std::floor(cx - w * S / 2), x1 = x0 + w * S, y1 = y + 20 * S;
    gWidgets.push_back({x0, y, x1, y1, id, true});
    hud.Rect(x0, y, x1, y1, focused ? 1.0f : 0.63f, focused ? 1.0f : 0.63f, focused ? 1.0f : 0.63f, 1);
    hud.Rect(x0 + S, y + S, x1 - S, y1 - S, 0, 0, 0, 1);
    std::string shown = text;
    if (focused && std::fmod(glfwGetTime(), 1.0) < 0.5) shown += "_";   // blinking caret
    hud.Text(shown.c_str(), x0 + 4 * S, y + 8 * S, S, {0.88f, 0.88f, 0.88f, 1}, true);
}

static std::string DateText(long long t) {
    if (t <= 0) return "NEVER PLAYED";
    std::time_t tt = (std::time_t)t;
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
    return buf;
}

static void BuildMenu(Hud& hud) {
    gWidgets.clear();
    const float S = GuiScale(), cx = gFbW / 2.0f;
    const Rgba white = {1, 1, 1, 1}, gray = {0.63f, 0.63f, 0.63f, 1}, red = {1, 0.4f, 0.4f, 1};
    if (gApp == App::Playing) {   // pause menu over the frozen world
        hud.Rect(0, 0, (float)gFbW, (float)gFbH, 0.06f, 0.06f, 0.08f, 0.6f);
        float y = gFbH / 4.0f;
        CenteredText(hud, "GAME MENU", y, S, white);
        y += 24 * S;
        Button(hud, cx, y, 200, "BACK TO GAME", M_RESUME);
        Button(hud, cx, y + 24 * S, 200, "SAVE AND QUIT TO TITLE", M_QUIT_TO_TITLE);
        Button(hud, cx, y + 48 * S, 200, "SAVE AND QUIT GAME", M_QUIT_GAME);
        return;
    }

    MenuBackground(hud);
    if (gApp == App::Title) {
        float y = gFbH / 4.0f;
        CenteredText(hud, "MYENGINE", y, 4 * S, white);
        // Splash text: a yellow one-liner under the title, like Minecraft's, gently pulsing
        float glow = 0.8f + 0.2f * (float)std::sin(glfwGetTime() * 6.0);
        CenteredText(hud, SPLASHES[gSplash], y + 26 * S, 2 * S, {1, 1, 0.25f * glow, 1});
        y = gFbH / 2.0f;
        Button(hud, cx, y, 200, "SINGLEPLAYER", M_SINGLEPLAYER);
        Button(hud, cx, y + 24 * S, 200, "QUIT GAME", M_QUIT);
        hud.Text("MYENGINE - A VOXEL SANDBOX", 2 * S, gFbH - 8 * S, S, gray, true);
    } else if (gApp == App::Worlds) {
        CenteredText(hud, "SELECT WORLD", 12 * S, S, white);
        const float rowW = 240 * S, rowH = 32 * S, x0 = std::floor(cx - rowW / 2);
        float y = 28 * S;
        if (gSaves.empty()) CenteredText(hud, "NO WORLDS YET. CREATE ONE!", y + 40 * S, S, gray);
        for (int i = gSaveScroll; i < (int)gSaves.size() && i < gSaveScroll + VISIBLE_ROWS; ++i, y += rowH + 2 * S) {
            const SaveInfo& w = gSaves[i];
            gWidgets.push_back({x0, y, x0 + rowW, y + rowH, M_WORLD_ROW + i, true});
            if (i == gSelectedSave) {
                hud.Rect(x0, y, x0 + rowW, y + rowH, 0.5f, 0.5f, 0.5f, 1);
                hud.Rect(x0 + S, y + S, x0 + rowW - S, y + rowH - S, 0, 0, 0, 1);
            } else if (MouseIn(x0, y, x0 + rowW, y + rowH)) {
                hud.Rect(x0, y, x0 + rowW, y + rowH, 1, 1, 1, 0.08f);
            }
            hud.Text(w.name.c_str(), x0 + 6 * S, y + 5 * S, S, white, true);
            std::string folder = std::filesystem::path(w.dir).filename().string() + " (" + DateText(w.lastPlayed) + ")";
            hud.Text(folder.c_str(), x0 + 6 * S, y + 14 * S, S, gray, false);
            char line[96];
            std::snprintf(line, sizeof line, "%s MODE, DAY %lld, SEED %u", w.creative ? "CREATIVE" : "SURVIVAL",
                          w.dayTime / DAY_TICKS + 1, w.seed);
            hud.Text(line, x0 + 6 * S, y + 23 * S, S, gray, false);
        }
        if ((int)gSaves.size() > VISIBLE_ROWS) {
            char more[48];
            std::snprintf(more, sizeof more, "SCROLL FOR MORE (%d WORLDS)", (int)gSaves.size());
            CenteredText(hud, more, y, S, gray);
        }
        if (!gMenuMessage.empty()) CenteredText(hud, gMenuMessage.c_str(), gFbH - 62 * S, S, red);
        bool sel = gSelectedSave >= 0;
        float by = gFbH - 52 * S;
        Button(hud, cx - 77 * S, by, 150, "PLAY SELECTED WORLD", M_PLAY, sel);
        Button(hud, cx + 77 * S, by, 150, "CREATE NEW WORLD", M_GO_CREATE);
        Button(hud, cx - 77 * S, by + 24 * S, 150, "DELETE", M_DELETE, sel);
        Button(hud, cx + 77 * S, by + 24 * S, 150, "CANCEL", M_BACK);
    } else if (gApp == App::Create) {
        float y = 20 * S;
        CenteredText(hud, "CREATE NEW WORLD", y, S, white);
        y += 22 * S;
        hud.Text("WORLD NAME", std::floor(cx - 100 * S), y, S, gray, true);
        TextField(hud, cx, y + 9 * S, 200, gFieldName, gFocusField == 0, M_FIELD_NAME);
        y += 38 * S;
        hud.Text("SEED FOR THE WORLD GENERATOR (BLANK = RANDOM)", std::floor(cx - 100 * S), y, S, gray, true);
        TextField(hud, cx, y + 9 * S, 200, gFieldSeed, gFocusField == 1, M_FIELD_SEED);
        y += 40 * S;
        Button(hud, cx, y, 200, gCreateCreative ? "GAME MODE: CREATIVE" : "GAME MODE: SURVIVAL", M_MODE);
        hud.Text(gCreateCreative ? "FLY, BUILD FREELY, NO DAMAGE" : "GATHER, CRAFT, STAY ALIVE",
                 std::floor(cx - Hud::TextWidth(gCreateCreative ? "FLY, BUILD FREELY, NO DAMAGE" : "GATHER, CRAFT, STAY ALIVE", S) / 2),
                 y + 24 * S, S, gray, false);
        float by = gFbH - 28 * S;
        bool named = gFieldName.find_first_not_of(' ') != std::string::npos;
        Button(hud, cx - 77 * S, by, 150, "CREATE NEW WORLD", M_CREATE, named);
        Button(hud, cx + 77 * S, by, 150, "CANCEL", M_BACK);
    } else if (gApp == App::ConfirmDelete && gSelectedSave >= 0) {
        float y = gFbH / 3.0f;
        CenteredText(hud, "ARE YOU SURE YOU WANT TO DELETE THIS WORLD?", y, S, white);
        std::string line = "'" + gSaves[gSelectedSave].name + "' WILL BE LOST FOREVER! (A LONG TIME!)";
        CenteredText(hud, line.c_str(), y + 14 * S, S, gray);
        Button(hud, cx - 77 * S, y + 40 * S, 150, "DELETE", M_CONFIRM_DELETE);
        Button(hud, cx + 77 * S, y + 40 * S, 150, "CANCEL", M_BACK);
    }
}

static void StartCreate() {
    gFieldName = "New World";
    gFieldSeed.clear();
    gFocusField = 0;
    gCreateCreative = false;
    gApp = App::Create;
}

static void MenuAction(int id) {
    if (id >= M_WORLD_ROW) {
        int i = id - M_WORLD_ROW;
        double now = glfwGetTime();
        bool twice = i == gSelectedSave && now - gLastRowClick < 0.4;   // double click plays
        gSelectedSave = i;
        gLastRowClick = now;
        if (twice) OpenWorld(gSaves[i]);
        return;
    }
    switch (id) {
        case M_SINGLEPLAYER: gMenuMessage.clear(); RefreshSaves(); gApp = App::Worlds; break;
        case M_QUIT:         glfwSetWindowShouldClose(gWindow, GLFW_TRUE); break;
        case M_PLAY:         if (gSelectedSave >= 0) OpenWorld(gSaves[gSelectedSave]); break;
        case M_GO_CREATE:    StartCreate(); break;
        case M_DELETE:       if (gSelectedSave >= 0) gApp = App::ConfirmDelete; break;
        case M_CONFIRM_DELETE: {
            std::error_code ec;
            std::filesystem::remove_all(gSaves[gSelectedSave].dir, ec);
            gSelectedSave = -1;
            RefreshSaves();
            gApp = App::Worlds;
            break;
        }
        case M_BACK:
            gApp = (gApp == App::Worlds) ? App::Title : App::Worlds;
            if (gApp == App::Worlds) RefreshSaves();
            break;
        case M_MODE:        gCreateCreative = !gCreateCreative; break;
        case M_FIELD_NAME:  gFocusField = 0; break;
        case M_FIELD_SEED:  gFocusField = 1; break;
        case M_CREATE: {
            uint32_t seed;
            if (!SeedFromText(gFieldSeed, seed))
                seed = (uint32_t)(std::random_device{}() ^ (uint32_t)std::chrono::steady_clock::now().time_since_epoch().count());
            std::string name = gFieldName;
            while (!name.empty() && name.back() == ' ') name.pop_back();
            CreateNewWorld(name, seed, gCreateCreative);
            break;
        }
        case M_RESUME:        Resume(); break;
        case M_QUIT_TO_TITLE: LeaveWorld(); gSplash = (int)(Rand01() * (sizeof SPLASHES / sizeof SPLASHES[0])); break;
        case M_QUIT_GAME:     glfwSetWindowShouldClose(gWindow, GLFW_TRUE); break;   // saves on the way out
        default: break;
    }
}

static void MenuClick() {
    float mx, my;
    MouseFb(mx, my);
    std::vector<Widget> widgets = gWidgets;   // an action may rebuild the menu
    for (const Widget& w : widgets)
        if (w.enabled && mx >= w.x0 && mx < w.x1 && my >= w.y0 && my < w.y1) { MenuAction(w.id); return; }
}

static void MenuKey(int key) {
    if (key == GLFW_KEY_ESCAPE) {
        if (gApp == App::Worlds) gApp = App::Title;
        else if (gApp == App::Create || gApp == App::ConfirmDelete) { gApp = App::Worlds; RefreshSaves(); }
        return;
    }
    if (gApp == App::Create) {
        std::string& f = gFocusField == 0 ? gFieldName : gFieldSeed;
        if (key == GLFW_KEY_BACKSPACE && !f.empty()) f.pop_back();
        if (key == GLFW_KEY_TAB) gFocusField = 1 - gFocusField;
        if ((key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) && gFieldName.find_first_not_of(' ') != std::string::npos)
            MenuAction(M_CREATE);
    } else if (gApp == App::Worlds) {
        if ((key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) && gSelectedSave >= 0) OpenWorld(gSaves[gSelectedSave]);
        if (key == GLFW_KEY_DOWN && !gSaves.empty()) gSelectedSave = std::min((int)gSaves.size() - 1, gSelectedSave + 1);
        if (key == GLFW_KEY_UP && !gSaves.empty()) gSelectedSave = std::max(0, gSelectedSave - 1);
        if (gSelectedSave >= 0) {   // keep the selection in view
            if (gSelectedSave < gSaveScroll) gSaveScroll = gSelectedSave;
            if (gSelectedSave >= gSaveScroll + VISIBLE_ROWS) gSaveScroll = gSelectedSave - VISIBLE_ROWS + 1;
        }
    }
}

static void OnChar(GLFWwindow*, unsigned int cp) {
    if (gApp != App::Create || cp < 32 || cp > 126) return;
    std::string& f = gFocusField == 0 ? gFieldName : gFieldSeed;
    if (f.size() < (gFocusField == 0 ? 32u : 24u)) f += (char)cp;
}

// ============================================================================
// GLFW callbacks
// ============================================================================
static void OnError(int, const char* d) { std::fprintf(stderr, "GLFW error: %s\n", d); }

static void OnFramebufferSize(GLFWwindow*, int w, int h) { gFbW = w; gFbH = h; }

static void OnCursor(GLFWwindow*, double x, double y) {
    if (gApp != App::Playing || gPaused) return;
    if (gScreen != Screen::None) { UiDragMove(); return; }
    if (!gCaptured) return;
    if (gFirstMouse) { gLastMouseX = x; gLastMouseY = y; gFirstMouse = false; return; }
    float dx = (float)(x - gLastMouseX), dy = (float)(y - gLastMouseY);
    gLastMouseX = x; gLastMouseY = y;
    gPlayer.yaw += dx * MOUSE_RADIANS_PER_PIXEL;
    gPlayer.pitch -= dy * MOUSE_RADIANS_PER_PIXEL;
    const float lim = PI / 2 - 0.001f;   // full look up/down, stop just short of straight
    gPlayer.pitch = std::max(-lim, std::min(lim, gPlayer.pitch));
}

static void OnScroll(GLFWwindow*, double, double yoff) {
    if (gApp == App::Worlds) {
        gSaveScroll = std::max(0, std::min(gSaveScroll - (int)yoff, (int)gSaves.size() - VISIBLE_ROWS));
        return;
    }
    if (gApp != App::Playing || gPaused) return;
    if (yoff > 0) gSelected = (gSelected + 8) % 9;
    else if (yoff < 0) gSelected = (gSelected + 1) % 9;
}

static void OnMouseButton(GLFWwindow*, int button, int action, int mods) {
    if (action == GLFW_RELEASE && button == GLFW_MOUSE_BUTTON_RIGHT) gRightDrag = false;
    if (action != GLFW_PRESS) return;
    if (gApp != App::Playing || gPaused) { if (button == GLFW_MOUSE_BUTTON_LEFT) MenuClick(); return; }
    if (gScreen != Screen::None) { UiMouseButton(button, (mods & GLFW_MOD_SHIFT) != 0); return; }
    if (!gCaptured) { if (button == GLFW_MOUSE_BUTTON_LEFT) SetCapture(true); return; }
    if (button == GLFW_MOUSE_BUTTON_LEFT)   gLmbLatch = true;
    if (button == GLFW_MOUSE_BUTTON_RIGHT)  gRmbLatch = true;
    if (button == GLFW_MOUSE_BUTTON_MIDDLE) gMmbLatch = true;
}

static void OnKey(GLFWwindow*, int key, int, int action, int mods) {
    if (gApp != App::Playing) { if (action != GLFW_RELEASE) MenuKey(key); return; }
    if (gPaused) { if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE) Resume(); return; }
    if (key == GLFW_KEY_Q && gCaptured && action != GLFW_RELEASE) {   // holding Q keeps dropping
        gDropLatch = true;
        gDropStack = (mods & GLFW_MOD_CONTROL) != 0;
    }
    if (action != GLFW_PRESS) return;
    if (gScreen != Screen::None) {
        if (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_E) CloseScreen();
        else if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9) UiNumberKey(key - GLFW_KEY_1);
        else if (key == GLFW_KEY_F6) LoadRecipes();
        return;
    }
    if (key == GLFW_KEY_ESCAPE) { Pause(); return; }
    if (key == GLFW_KEY_E && gCaptured && gDeadTicks == 0) OpenScreen(Screen::Inventory);
    if (key == GLFW_KEY_F6) LoadRecipes();
    if (key == GLFW_KEY_F7) gDayTime += 3000;
    if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9) gSelected = key - GLFW_KEY_1;
    if (key == GLFW_KEY_F3) gDebug = !gDebug;
    if (key == GLFW_KEY_F5) gThirdPerson = !gThirdPerson;
    if (key == GLFW_KEY_G) {
        gPlayer.creative = !gPlayer.creative;
        if (!gPlayer.creative) gPlayer.flying = false;
        gBreakProgress = 0; gBreaking = false; gEatTicks = 0;
        std::printf("Game mode: %s\n", gPlayer.creative ? "Creative" : "Survival");
    }
}

// Switching to another window pauses the game, like Minecraft
static void OnFocus(GLFWwindow*, int focused) {
    if (focused) return;
    if (gApp == App::Playing && gScreen == Screen::None) Pause();
    else if (gCaptured) SetCapture(false);
}

// ============================================================================
// Game tick (20 per second)
// ============================================================================
static void TickInteraction() {
    World& w = *gWorld;
    Player& p = gPlayer;
    Vec3 d = DirFromAngles(p.yaw, p.pitch);
    double reach = p.creative ? REACH_CREATIVE : REACH_SURVIVAL;
    RayHit h = Raycast(w, p.x, p.y + p.EyeHeight(), p.z, d.x, d.y, d.z, reach);

    bool lmb = gCaptured && (gLmbLatch || glfwGetMouseButton(gWindow, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
    bool rmbHeld = gCaptured && glfwGetMouseButton(gWindow, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

    // ---- Breaking ----
    if (gBreakDelay > 0) --gBreakDelay;
    if (lmb && h.hit) {
        uint8_t b = w.Get(h.x, h.y, h.z);
        if (p.creative) {
            gBreaking = false;
            if (gBreakDelay == 0) {
                SpawnBreakParticles(h.x, h.y, h.z, b);
                SetBlock(h.x, h.y, h.z, AIR);
                gBreakDelay = BREAK_DELAY_CREATIVE;
            }
        } else {
            if (!gBreaking || h.x != gBX || h.y != gBY || h.z != gBZ) {
                gBreaking = true; gBX = h.x; gBY = h.y; gBZ = h.z; gBreakProgress = 0;
            }
            uint8_t tool = SelectedItem();
            double dmg = BreakDamagePerTick(b, p.onGround, EyesInWater(w, p), tool);
            if (gBreakDelay == 0 && dmg > 0) {
                gBreakProgress += dmg;
                if (dmg < 1.0) SpawnHitParticle(h.x, h.y, h.z, h.nx, h.ny, h.nz, b);
                if (gBreakProgress >= 1.0 - 1e-6) {
                    SpawnBreakParticles(h.x, h.y, h.z, b);
                    SetBlock(h.x, h.y, h.z, b == ICE ? (uint8_t)WATER : (uint8_t)AIR);   // ice melts in survival
                    Stack drop = BlockDrop(b, tool);
                    if (drop.count > 0) DropFromBlock(gItems, h.x, h.y, h.z, drop);
                    // Tools wear 1 per block (2 for swords), except on instant-break blocks
                    const ItemInfo* t = ItemData(tool);
                    if (Info(b).hardness > 0 && WearTool(gInv.slots[gSelected], t && t->tool == SWORD ? 2 : 1))
                        std::printf("Your %s broke!\n", ItemName(tool));
                    p.exhaustion = std::min(MAX_EXHAUSTION, p.exhaustion + EXHAUSTION_BREAK);
                    gBreaking = false; gBreakProgress = 0;
                    gBreakDelay = dmg >= 1.0 ? 0 : BREAK_DELAY_SURVIVAL;
                }
            }
        }
    } else {
        gBreaking = false; gBreakProgress = 0;
    }
    gLmbLatch = false;

    // ---- Right click a crafting table to use it (sneak to place against it) ----
    if (gRmbLatch && h.hit && w.Get(h.x, h.y, h.z) == CRAFTING_TABLE && !p.sneaking) {
        gOpenX = h.x; gOpenY = h.y; gOpenZ = h.z;
        OpenScreen(Screen::Crafting);
        return;
    }
    if (gRmbLatch && h.hit && IsFurnace(w.Get(h.x, h.y, h.z)) && !p.sneaking) {
        gOpenX = h.x; gOpenY = h.y; gOpenZ = h.z;
        gOpenFurnace = &gFurnaces[FurnaceKey(h.x, h.y, h.z)];
        OpenScreen(Screen::Furnace);
        return;
    }

    // ---- Eating: hold right click with food while hungry (survival) ----
    uint8_t held = SelectedItem();
    if (!p.creative && held == APPLE && rmbHeld && p.food < MAX_FOOD) {
        if (++gEatTicks >= APPLE_EAT_TICKS) {
            Eat(p, APPLE_NUTRITION, APPLE_SATURATION);
            gInv.Take(gSelected, 1);
            gEatTicks = 0;
        }
    } else {
        gEatTicks = 0;
    }

    // ---- Placing: immediately on click, then every 4 ticks while held ----
    if (gPlaceDelay > 0) --gPlaceDelay;
    if (h.hit && IsPlaceable(held) && (gRmbLatch || (rmbHeld && gPlaceDelay == 0)) && (h.nx | h.ny | h.nz)) {
        int tx = h.x + h.nx, ty = h.y + h.ny, tz = h.z + h.nz;
        if (IsReplaceable(w.Get(h.x, h.y, h.z))) { tx = h.x; ty = h.y; tz = h.z; }   // placing on snow or grass replaces it
        uint8_t cur = w.Get(tx, ty, tz);
        AABB cell = {(double)tx, (double)ty, (double)tz, tx + 1.0, ty + 1.0, tz + 1.0};
        bool blocked = Info(held).solid && Overlaps(p.Box(), cell);
        uint8_t place = held;
        if (IsTorch(held)) {
            // Torches stand on floors or hang on walls of the block you clicked
            place = CanHoldTorch(w.Get(h.x, h.y, h.z)) ? TorchForFace(h.nx, h.ny, h.nz) : (uint8_t)AIR;
        } else if (IsFurnace(held)) {
            place = FurnaceBlock(FacingToward(d.x, d.z), false);   // front faces you
        }
        if (place != AIR && !IsTorch(place) && !CanSurvive(w, place, tx, ty, tz)) place = AIR;   // e.g. cactus off sand
        if (place != AIR && w.Loaded(tx, tz) && ty >= 0 && ty < WY && IsReplaceable(cur) && !blocked) {
            SetBlock(tx, ty, tz, place);
            if (!p.creative) gInv.Take(gSelected, 1);
        }
        gPlaceDelay = PLACE_DELAY;
    }
    gRmbLatch = false;

    // ---- Pick block: creative swaps it into the palette, survival only
    //      selects it if you're already carrying some ----
    if (gMmbLatch && h.hit) {
        uint8_t b = w.Get(h.x, h.y, h.z);
        if (IsTorch(b)) b = TORCH;
        if (IsFurnace(b)) b = FURNACE;
        int found = -1;
        if (p.creative) {
            for (int i = 0; i < 9; ++i) if (gCreativeBar[i] == b) found = i;
            if (found >= 0) gSelected = found; else gCreativeBar[gSelected] = b;
        } else {
            for (int i = 0; i < 9; ++i) if (gInv.slots[i].count > 0 && gInv.slots[i].item == b) found = i;
            if (found >= 0) gSelected = found;
        }
    }
    gMmbLatch = false;

    // ---- Q: toss the held item forward ----
    if (gDropLatch && !p.creative && gInv.slots[gSelected].count > 0) {
        Stack& s = gInv.slots[gSelected];
        Stack t = s;
        t.count = gDropStack ? s.count : 1;
        Toss(t);
        gInv.Take(gSelected, t.count);
    }
    gDropLatch = false;
}

static void GameTick() {
    ++gTicks;
    ++gDayTime;
    const bool dead = gDeadTicks > 0;
    PlayerInput in;
    if (gCaptured && !dead) {
        auto down = [](int k) { return glfwGetKey(gWindow, k) == GLFW_PRESS; };
        in.forward = down(GLFW_KEY_W); in.back = down(GLFW_KEY_S);
        in.left = down(GLFW_KEY_A);    in.right = down(GLFW_KEY_D);
        in.jump = down(GLFW_KEY_SPACE);
        in.sneak = down(GLFW_KEY_LEFT_SHIFT);
        in.sprint = down(GLFW_KEY_LEFT_CONTROL);
        in.usingItem = gEatTicks > 0;
    }
    if (PlayerAreaReady()) {
        TickPlayer(*gWorld, gPlayer, in);
        TickSurvival(*gWorld, gPlayer);
    }

    // ---- Death: the hotbar spills out, then you respawn ----
    if (!dead && !gPlayer.creative && gPlayer.health <= 0) {
        CloseScreen(false);   // grid and cursor items go back to the inventory first
        for (Stack& s : gInv.slots) {
            if (s.count <= 0) continue;
            double f = Rand01() * 0.5, a = Rand01() * 2.0 * PI;
            SpawnItem(gItems, gPlayer.x, gPlayer.y + gPlayer.EyeHeight() - 0.3, gPlayer.z,
                      -std::sin(a) * f, 0.2, std::cos(a) * f, s, 40);
            s = {};
        }
        gDeadTicks = 1;
        gBreaking = false; gBreakProgress = 0; gEatTicks = 0;
        std::printf("You died!\n");
    } else if (dead && ++gDeadTicks > DEATH_SCREEN_TICKS) {
        Respawn(gPlayer);
        gPlayer.x = gSpawnX + 0.5; gPlayer.z = gSpawnZ + 0.5; gPlayer.y = WY;
        gPlayer.px = gPlayer.x; gPlayer.py = gPlayer.y; gPlayer.pz = gPlayer.z;
        gNeedsSpawn = true;   // streaming moves to the spawn, then places you on the ground
        gDeadTicks = 0;
    }
    TickItems(*gWorld, gItems, gPlayer, gInv, !gPlayer.creative && gDeadTicks == 0);

    // Torches near you flicker out a flame and a wisp of smoke now and then
    for (const TorchPos& t : gTorches) {
        double dx = t.x - gPlayer.x, dy = t.y - gPlayer.y, dz = t.z - gPlayer.z;
        if (dx * dx + dy * dy + dz * dz < 32.0 * 32.0 && Rand01() < 0.1)
            SpawnTorchParticles(gWorld->Get(t.x, t.y, t.z), t.x, t.y, t.z);
    }
    for (const auto& kv : gFurnaces) {
        if (!kv.second.Lit() || Rand01() >= 0.1) continue;
        int x, y, z;
        UnpackBlockKey(kv.first, x, y, z);
        double dx = x - gPlayer.x, dz = z - gPlayer.z;
        if (dx * dx + dz * dz > 32.0 * 32.0) continue;
        SpawnFurnaceParticles(FurnaceFacing(gWorld->Get(x, y, z)), x, y, z);
    }
    TickParticles(*gWorld);

    // Furnaces keep burning whether or not anyone's looking
    for (auto& kv : gFurnaces) {
        if (!TickFurnace(kv.second, gRecipes)) continue;
        int x, y, z;
        UnpackBlockKey(kv.first, x, y, z);
        if (!gWorld->Loaded(x, z)) continue;   // only loaded furnaces burn
        uint8_t b = gWorld->Get(x, y, z);
        if (IsFurnace(b)) SetBlock(x, y, z, FurnaceBlock(FurnaceFacing(b), kv.second.Lit()));   // swap lit/unlit
    }

    // A table or furnace screen closes if the block is gone or you wander off
    if (gScreen == Screen::Crafting || gScreen == Screen::Furnace) {
        double dx = gPlayer.x - (gOpenX + 0.5), dy = gPlayer.y + 0.9 - (gOpenY + 0.5), dz = gPlayer.z - (gOpenZ + 0.5);
        uint8_t b = gWorld->Get(gOpenX, gOpenY, gOpenZ);
        bool still = gScreen == Screen::Crafting ? b == CRAFTING_TABLE : IsFurnace(b);
        if (!still || dx * dx + dy * dy + dz * dz > 64.0) CloseScreen();
    }

    // FOV widens 15% while sprinting and 10% while flying, eased in
    gPrevFovMod = gFovMod;
    float target = 1.0f;
    if (gPlayer.flying) target *= 1.1f;
    if (gPlayer.sprinting) target *= 1.15f;
    gFovMod += (target - gFovMod) * 0.5f;

    // Eye height eases when you start/stop sneaking
    gPrevEyeH = gEyeH;
    gEyeH += ((float)gPlayer.EyeHeight() - gEyeH) * 0.5f;

    if (gDeadTicks == 0) {
        TickInteraction();
    } else {
        gLmbLatch = gRmbLatch = gMmbLatch = gDropLatch = false;
    }

    if (++gAutosave >= AUTOSAVE_TICKS) {   // every 5 minutes
        gAutosave = 0;
        SaveGame();
    }
}

// ============================================================================
// Container screen drawing (Minecraft's gray panel with beveled slots)
// ============================================================================
static void Bevel(Hud& hud, float x0, float y0, float x1, float y1, float t, Rgba fill, Rgba tl, Rgba br) {
    hud.Rect(x0, y0, x1, y1, fill.r, fill.g, fill.b, fill.a);
    hud.Rect(x0, y0, x1 - t, y0 + t, tl.r, tl.g, tl.b, tl.a);
    hud.Rect(x0, y0 + t, x0 + t, y1 - t, tl.r, tl.g, tl.b, tl.a);
    hud.Rect(x0 + t, y1 - t, x1, y1, br.r, br.g, br.b, br.a);
    hud.Rect(x1 - t, y0, x1, y1 - t, br.r, br.g, br.b, br.a);
}

static void DrawScreen(Hud& hud) {
    const float S = GuiScale();
    float ox, oy;
    PanelOrigin(ox, oy);
    auto X = [&](float gx) { return ox + gx * S; };
    auto Y = [&](float gy) { return oy + gy * S; };
    const Rgba white = {1, 1, 1, 1}, gray = {0.776f, 0.776f, 0.776f, 1}, dark = {0.333f, 0.333f, 0.333f, 1};
    const Rgba slotFill = {0.545f, 0.545f, 0.545f, 1}, slotDark = {0.216f, 0.216f, 0.216f, 1};
    const Rgba label = {0.25f, 0.25f, 0.25f, 1};

    hud.Rect(0, 0, (float)gFbW, (float)gFbH, 0.06f, 0.06f, 0.08f, 0.55f);     // dim the world
    hud.Frame(X(-1), Y(-1), X(PANEL_W + 1), Y(PANEL_H + 1), S, 0, 0, 0, 1);    // black outline
    Bevel(hud, X(0), Y(0), X(PANEL_W), Y(PANEL_H), 2 * S, gray, white, dark);

    std::vector<UiSlot> slots = UiSlots();
    for (const UiSlot& sl : slots) {
        float pad = BigSlot(sl.kind) ? 5.0f : 1.0f;
        Bevel(hud, X(sl.x - pad), Y(sl.y - pad), X(sl.x + 16 + pad), Y(sl.y + 16 + pad), S, slotFill, slotDark, white);
    }

    // Arrow from the grid (or furnace input) to the output; a furnace fills it as it cooks
    const bool table = gScreen == Screen::Crafting, furnace = gScreen == Screen::Furnace;
    float ax = table ? 90.0f : (furnace ? 80.0f : 135.0f), ay = table ? 35.0f : (furnace ? 35.0f : 28.0f);
    const int shaft = (table || furnace) ? 13 : 8;   // the head is 8 wide either way
    float cooked = 0;
    if (furnace && gOpenFurnace->cookTotal > 0) cooked = (float)gOpenFurnace->cookTime / gOpenFurnace->cookTotal;
    auto arrowCol = [&](float gx) {
        bool done = furnace && (gx - ax) < cooked * (shaft + 8.0f);
        return done ? Rgba{1, 1, 1, 1} : slotFill;
    };
    for (int i = 0; i < shaft; ++i) { Rgba c = arrowCol(ax + i); hud.Rect(X(ax + i), Y(ay + 6), X(ax + i + 1), Y(ay + 10), c.r, c.g, c.b, 1); }
    for (int i = 0; i < 8; ++i) {
        Rgba c = arrowCol(ax + shaft + i);
        hud.Rect(X(ax + shaft + i), Y(ay + i), X(ax + shaft + 1 + i), Y(ay + 16 - i), c.r, c.g, c.b, 1);
    }

    if (furnace) {
        // Flame between input and fuel: fills from the bottom with the fuel left
        static const char* const FLAME[13] = {
            "......#......", ".....##......", ".....###.....", "....####.#...", "...#####.##..",
            "...########..", "..#########..", "..##########.", ".###########.", ".############",
            "#############", "#############", ".###########."};
        float lit = gOpenFurnace->litDuration > 0 ? (float)gOpenFurnace->litTime / gOpenFurnace->litDuration : 0;
        int litRows = gOpenFurnace->Lit() ? std::max(1, (int)std::ceil(lit * 13)) : 0;
        for (int r = 0; r < 13; ++r)
            for (int c = 0; FLAME[r][c]; ++c) {
                if (FLAME[r][c] != '#') continue;
                bool on = r >= 13 - litRows;
                Rgba col = on ? (r < 5 ? Rgba{1, 0.85f, 0.3f, 1} : Rgba{0.95f, 0.45f, 0.1f, 1}) : slotFill;
                hud.Rect(X(57 + c), Y(37 + r), X(58 + c), Y(38 + r), col.r, col.g, col.b, col.a);
            }
        hud.Text("FURNACE", X(PANEL_W / 2.0f) - Hud::TextWidth("FURNACE", S) / 2, Y(6), S, label, false);
    } else if (table) {
        hud.Text("CRAFTING", X(28), Y(6), S, label, false);
    } else {
        hud.Rect(X(26), Y(8), X(77), Y(69), 0, 0, 0, 1);                       // where a player model would go
        hud.Text("CRAFTING", X(98), Y(8), S, label, false);
    }
    hud.Text("INVENTORY", X(8), Y(74), S, label, false);

    for (const UiSlot& sl : slots) hud.DrawStack(*sl.s, X(sl.x), Y(sl.y), S);

    int hover = HoveredSlot(slots);
    if (hover >= 0) {
        const UiSlot& sl = slots[hover];
        hud.Rect(X(sl.x), Y(sl.y), X(sl.x + 16), Y(sl.y + 16), 1, 1, 1, 0.45f);
    }

    float mx, my;
    MouseFb(mx, my);
    if (gCursor.count > 0) {
        hud.DrawStack(gCursor, mx - 8 * S, my - 8 * S, S);
    } else if (hover >= 0 && slots[hover].s->count > 0) {
        // Tooltip: item name, plus uses left for tools
        const Stack& st = *slots[hover].s;
        char text[64];
        int max = MaxDurability(st.item);
        if (max > 0) std::snprintf(text, sizeof text, "%s  %d/%d", ItemName(st.item), max - st.damage, max);
        else         std::snprintf(text, sizeof text, "%s", ItemName(st.item));
        float tw = Hud::TextWidth(text, S);
        float tx = mx + 12 * S, ty = my - 12 * S;
        if (tx + tw + 4 * S > gFbW) tx = mx - 12 * S - tw;
        hud.Rect(tx - 3 * S, ty - 3 * S, tx + tw + 3 * S, ty + 8 * S, 0.06f, 0.0f, 0.06f, 0.94f);
        hud.Frame(tx - 3 * S, ty - 3 * S, tx + tw + 3 * S, ty + 8 * S, S, 0.31f, 0.0f, 0.9f, 0.6f);
        hud.Text(text, tx, ty, S, {1, 1, 1, 1}, true);
    }
}

// ============================================================================
// Main
// ============================================================================
int main(int argc, char** argv) {

    glfwSetErrorCallback(OnError);
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    gWindow = glfwCreateWindow(1280, 720, "MyEngine", nullptr, nullptr);
    if (!gWindow) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(gWindow);
    glfwSwapInterval(1);
    if (!gladLoadGL()) { std::fprintf(stderr, "GLAD failed\n"); return -1; }

    glfwSetFramebufferSizeCallback(gWindow, OnFramebufferSize);
    glfwSetCursorPosCallback(gWindow, OnCursor);
    glfwSetScrollCallback(gWindow, OnScroll);
    glfwSetMouseButtonCallback(gWindow, OnMouseButton);
    glfwSetKeyCallback(gWindow, OnKey);
    glfwSetWindowFocusCallback(gWindow, OnFocus);
    glfwSetCharCallback(gWindow, OnChar);
    glfwGetFramebufferSize(gWindow, &gFbW, &gFbH);

    glClearColor(0.1f, 0.08f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glfwSwapBuffers(gWindow);

    std::printf("Controls: WASD move | Mouse look | Space jump | Shift sneak | Ctrl sprint\n"
                "          LMB break | RMB place/eat | MMB pick | Q drop | 1-9/wheel hotbar | G game mode\n"
                "          Double tap Space fly (creative) | F5 third person | F3 debug | Esc pause\n"
                "Survival: your hotbar starts empty, so go punch a tree. Press G for creative.\n");

    // ---- GPU resources ----
    GLuint blockProg  = CompileProgram("block", BLOCK_VS, BLOCK_FS);
    GLuint simpleProg = CompileProgram("simple", SIMPLE_VS, SIMPLE_FS);
    GLuint hudProg    = CompileProgram("hud", HUD_VS, HUD_FS);

    const GLint bViewProj = glGetUniformLocation(blockProg, "uViewProj");
    const GLint bSunDir   = glGetUniformLocation(blockProg, "uSunDir");
    const GLint bCamPos   = glGetUniformLocation(blockProg, "uCamPos");
    const GLint bFogColor = glGetUniformLocation(blockProg, "uFogColor");
    const GLint bFogStart = glGetUniformLocation(blockProg, "uFogStart");
    const GLint bFogEnd   = glGetUniformLocation(blockProg, "uFogEnd");
    const GLint bTransl   = glGetUniformLocation(blockProg, "uTranslucent");
    const GLint bDaylight = glGetUniformLocation(blockProg, "uDaylight");
    const GLint sMVP      = glGetUniformLocation(simpleProg, "uMVP");
    const GLint sUseTex   = glGetUniformLocation(simpleProg, "uUseTex");
    const GLint sTint     = glGetUniformLocation(simpleProg, "uTint");
    const GLint hProj     = glGetUniformLocation(hudProg, "uProj");

    glUseProgram(blockProg);  glUniform1i(glGetUniformLocation(blockProg, "uAtlas"), 0);
    glUseProgram(simpleProg); glUniform1i(glGetUniformLocation(simpleProg, "uTex"), 0);
    glUniform4f(sTint, 1, 1, 1, 1);
    glUseProgram(hudProg);    glUniform1i(glGetUniformLocation(hudProg, "uTex"), 0);

    // Texture atlas: nearest filtering keeps the pixel art crisp
    GLuint atlasTex;
    {
        atlas::Image img = atlas::Build();
        for (const std::string& dir : {std::string("assets/pack"), std::string(MYENGINE_ASSETS) + "/pack"}) {
            int n = pack::Load(img, dir);
            if (n > 0) { std::printf("Texture pack: %d textures from %s\n", n, dir.c_str()); break; }
        }
        gAtlasPixels = img;
        glGenTextures(1, &atlasTex);
        glBindTexture(GL_TEXTURE_2D, atlasTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlas::SIZE, atlas::SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.px.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 4);   // stop at 1 pixel per tile
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenerateMipmap(GL_TEXTURE_2D);
    }

    LoadRecipes();

    DynMesh lines, overlay, clouds, hudMesh, crossMesh;
    lines.Init({3, 2, 4});
    overlay.Init({3, 2, 4});
    clouds.Init({3, 2, 4});
    hudMesh.Init({2, 2, 4});
    crossMesh.Init({2, 2, 4});

    // Clouds: a flat layer of 12x12 cells that drifts east, like Minecraft's
    const float CLOUD_Y = 108.0f, CLOUD_CELL = 12.0f;
    const int   CLOUD_N = 64;                       // 64 cells * 12 = 768 block repeat
    const float CLOUD_PERIOD = CLOUD_N * CLOUD_CELL;
    {
        std::vector<float> cv;
        Perlin cn(99);
        for (int i = 0; i < CLOUD_N; ++i)
            for (int j = 0; j < CLOUD_N; ++j) {
                if (cn.Fbm(i * 0.16, j * 0.16, 2) < 0.12) continue;
                float x0 = -256 + i * CLOUD_CELL, z0 = -256 + j * CLOUD_CELL;
                Vec3 a = {x0, CLOUD_Y, z0}, b = {x0 + CLOUD_CELL, CLOUD_Y, z0};
                Vec3 c = {x0 + CLOUD_CELL, CLOUD_Y, z0 + CLOUD_CELL}, d = {x0, CLOUD_Y, z0 + CLOUD_CELL};
                for (Vec3 p : {a, b, c, c, d, a}) PushV(cv, p, 0, 0, 1, 1, 1, 0.78f);
            }
        clouds.Upload(cv, GL_STATIC_DRAW);
    }

    // Stars: fixed points on the sky sphere that turn with the sun
    struct Star { Vec3 dir; float size, bright; };
    std::vector<Star> stars;
    {
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> U(-1.0f, 1.0f);
        while (stars.size() < 1500) {
            Vec3 d = {U(rng), U(rng), U(rng)};
            float l = std::sqrt(Dot(d, d));
            if (l < 0.1f || l > 1.0f) continue;
            float u = (U(rng) + 1) * 0.5f;
            stars.push_back({d * (1.0f / l), 0.25f + 0.35f * u, 0.4f + 0.6f * (U(rng) + 1) * 0.5f});
        }
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_MULTISAMPLE);

    // Draws one frame with a message on the menu backdrop (while loading)
    gShowMessage = [&](const char* text) {
        glViewport(0, 0, gFbW, gFbH);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glUseProgram(hudProg);
        Mat4 ortho = Ortho(0, (float)gFbW, (float)gFbH, 0);
        glUniformMatrix4fv(hProj, 1, GL_FALSE, ortho.m);
        glBindTexture(GL_TEXTURE_2D, atlasTex);
        Hud hud;
        MenuBackground(hud);
        CenteredText(hud, text, gFbH / 2.0f, GuiScale(), {1, 1, 1, 1});
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        hudMesh.Draw(hud.v, GL_TRIANGLES);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glfwSwapBuffers(gWindow);
    };

    gSplash = (int)(std::random_device{}() % (sizeof SPLASHES / sizeof SPLASHES[0]));
    if (argc > 1) {   // straight into a fresh world with this seed
        uint32_t seed = (uint32_t)std::strtoul(argv[1], nullptr, 10);
        CreateNewWorld("Seed " + std::to_string(seed), seed, false);
    }

    // ---- Fixed 20 TPS simulation, rendered as fast as vsync allows ----
    const double TICK = 1.0 / TPS;
    double last = glfwGetTime(), acc = 0, titleTimer = 0;
    int frames = 0, fps = 0;
    std::vector<float> scratch;

    while (!glfwWindowShouldClose(gWindow)) {
        double now = glfwGetTime();
        double frameDt = std::min(now - last, 0.25);
        last = now;
        acc += frameDt;

        glfwPollEvents();
        if (gFbW == 0 || gFbH == 0) { glfwWaitEvents(); continue; }   // minimized

        if (gApp != App::Playing) {
            acc = 0;
            glViewport(0, 0, gFbW, gFbH);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glUseProgram(hudProg);
            Mat4 ortho = Ortho(0, (float)gFbW, (float)gFbH, 0);
            glUniformMatrix4fv(hProj, 1, GL_FALSE, ortho.m);
            glBindTexture(GL_TEXTURE_2D, atlasTex);
            Hud hud;
            BuildMenu(hud);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            hudMesh.Draw(hud.v, GL_TRIANGLES);
            glDisable(GL_BLEND);
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
            glfwSetWindowTitle(gWindow, "MyEngine");
            glfwSwapBuffers(gWindow);
            continue;
        }

        if (gPaused) acc = 0;   // the world stands still while paused
        while (acc >= TICK) { GameTick(); acc -= TICK; }
        if (gApp != App::Playing) continue;   // a tick can't leave the world, but be safe
        UpdateChunks(0.006);                  // about a third of a 60 fps frame
        const float alpha = (float)(acc / TICK);
        const Player& p = gPlayer;

        // ---- Camera (interpolated between ticks) ----
        Vec3 feet = {(float)(p.px + (p.x - p.px) * alpha), (float)(p.py + (p.y - p.py) * alpha),
                     (float)(p.pz + (p.z - p.pz) * alpha)};
        float eyeH = Lerp(gPrevEyeH, gEyeH, alpha);
        Vec3 eye = {feet.x, feet.y + eyeH, feet.z};
        float pitch = p.pitch;
        float roll = 0;
        Vec3 worldUp = {0, 1, 0};

        // View bobbing: same shape as Minecraft's (side sway, dip, small roll)
        if (!gThirdPerson) {
            float phase = (float)(p.prevWalkDist + (p.walkDist - p.prevWalkDist) * alpha) * PI;
            float b = (float)(p.prevBob + (p.bob - p.prevBob) * alpha);
            Vec3 fwd0 = DirFromAngles(p.yaw, 0);
            Vec3 right0 = Normalize(Cross(fwd0, worldUp));
            eye = eye + right0 * (std::sin(phase) * b * 0.5f) + worldUp * (-std::fabs(std::cos(phase) * b));
            roll = std::sin(phase) * b * 3.0f * PI / 180.0f;
            pitch -= std::fabs(std::cos(phase - 0.2f) * b) * 5.0f * PI / 180.0f;
        }
        // Taking damage jolts the camera sideways (Minecraft's hurt tilt)
        if (p.hurtTime > 0) {
            float t = std::max(0.0f, (p.hurtTime - alpha) / 10.0f);
            roll -= std::sin(t * t * t * t * PI) * 14.0f * PI / 180.0f;
        }
        if (gDeadTicks > 0) roll = std::min(1.0f, gDeadTicks / 20.0f) * 40.0f * PI / 180.0f;   // keel over
        Vec3 fwd = DirFromAngles(p.yaw, pitch);
        Vec3 camRight = Normalize(Cross(fwd, worldUp));
        Vec3 camUp = Cross(camRight, fwd);
        camUp = Normalize(camUp * std::cos(roll) + camRight * std::sin(roll));

        Vec3 camPos = eye;
        if (gThirdPerson) {
            RayHit back = Raycast(*gWorld, eye.x, eye.y, eye.z, -fwd.x, -fwd.y, -fwd.z, 4.0);
            float dist = back.hit ? std::max(0.2f, (float)back.dist - 0.25f) : 4.0f;
            camPos = eye - fwd * dist;
        }

        float fov = BASE_FOV_DEGREES * Lerp(gPrevFovMod, gFovMod, alpha) * PI / 180.0f;
        Mat4 proj = Perspective(fov, (float)gFbW / gFbH, 0.05f, 600.0f);
        Mat4 view = LookAt(camPos, camPos + fwd, camUp);
        Mat4 viewProj = proj * view;

        bool underwater = gWorld->Get((int)std::floor(camPos.x), (int)std::floor(camPos.y),
                                      (int)std::floor(camPos.z)) == WATER;
        // ---- Sky: sun and moon follow Minecraft's clock, east to west ----
        const double dayTime = (double)gDayTime + alpha;
        const float angle = CelestialAngle(dayTime);
        const float theta = angle * 2.0f * PI;
        gDaylight = DaylightFor(angle);
        const Vec3 sunDir = Normalize({-std::sin(theta), std::cos(theta), 0.3f});
        const Vec3 lightDir = sunDir.y >= 0 ? sunDir : sunDir * -1.0f;   // the moon lights the night

        Vec3 skyColor = {Lerp(0.015f, 0.70f, gDaylight), Lerp(0.02f, 0.82f, gDaylight), Lerp(0.05f, 1.0f, gDaylight)};
        {
            // Orange glow on the side of the sky the sun is rising or setting on
            float low = std::max(0.0f, 1.0f - std::fabs(sunDir.y) * 3.5f);
            Vec3 fh = Normalize({fwd.x, 0, fwd.z}), sh = Normalize({sunDir.x, 0, sunDir.z});
            float toward = std::max(0.0f, Dot(fh, sh));
            float k = 0.6f * low * toward * toward;
            skyColor = skyColor + (Vec3{0.98f, 0.52f, 0.22f} - skyColor) * k;
        }
        Vec3 fogColor = underwater ? Vec3{0.05f, 0.16f, 0.42f} * Lerp(0.15f, 1.0f, gDaylight) : skyColor;
        const float viewDist = (gRenderDistance - 1) * (float)CHUNK;
        float fogStart = underwater ? 0.0f : viewDist * 0.65f, fogEnd = underwater ? 22.0f : viewDist;

        glViewport(0, 0, gFbW, gFbH);
        glClearColor(fogColor.x, fogColor.y, fogColor.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glBindTexture(GL_TEXTURE_2D, atlasTex);
        glActiveTexture(GL_TEXTURE0);

        // ---- Stars, sun and moon (squares, of course) ----
        if (!underwater) {
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            scratch.clear();
            auto skyQuad = [&](Vec3 dir, float dist, float size, float r, float g, float b, float a) {
                Vec3 c = camPos + dir * dist;
                Vec3 ref = std::fabs(dir.y) > 0.99f ? Vec3{1, 0, 0} : worldUp;
                Vec3 sr = Normalize(Cross(dir, ref)), su = Cross(sr, dir);
                Vec3 q[4] = {c - sr * size - su * size, c + sr * size - su * size, c + sr * size + su * size, c - sr * size + su * size};
                for (int k : {0, 1, 2, 2, 3, 0}) PushV(scratch, q[k], 0, 0, r, g, b, a);
            };
            float night = (1.0f - gDaylight) * (1.0f - gDaylight);
            if (night > 0.01f) {
                const float cs = std::cos(theta), sn = std::sin(theta);
                for (const Star& st : stars) {
                    Vec3 d = {st.dir.x * cs - st.dir.y * sn, st.dir.x * sn + st.dir.y * cs, st.dir.z};   // turns with the sun
                    if (d.y < -0.05f) continue;
                    skyQuad(d, 280.0f, st.size, 1, 1, 1, night * st.bright);
                }
            }
            Vec3 moonDir = sunDir * -1.0f;
            if (sunDir.y > -0.2f)  skyQuad(sunDir, 300.0f, 24.0f, 1.0f, 0.97f, 0.78f, 1);
            if (moonDir.y > -0.2f) skyQuad(moonDir, 300.0f, 16.0f, 0.85f, 0.88f, 0.98f, 1);
            glUseProgram(simpleProg);
            glUniformMatrix4fv(sMVP, 1, GL_FALSE, viewProj.m);
            glUniform1i(sUseTex, 0);
            overlay.Draw(scratch, GL_TRIANGLES);
            glDisable(GL_BLEND);
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
        }

        // ---- Opaque world ----
        glUseProgram(blockProg);
        glUniformMatrix4fv(bViewProj, 1, GL_FALSE, viewProj.m);
        glUniform3f(bSunDir, lightDir.x, lightDir.y, lightDir.z);
        glUniform1f(bDaylight, gDaylight);
        glUniform3f(bCamPos, camPos.x, camPos.y, camPos.z);
        glUniform3f(bFogColor, fogColor.x, fogColor.y, fogColor.z);
        glUniform1f(bFogStart, fogStart);
        glUniform1f(bFogEnd, fogEnd);
        glUniform1i(bTransl, 0);
        glDepthFunc(GL_LEQUAL);   // grass overlays sit exactly on their dirt faces
        for (const auto& kv : gMeshes) {
            int cx = (int)(int32_t)(kv.first >> 32), cz = (int)(int32_t)(kv.first & 0xFFFFFFFF);
            float dx = cx * CHUNK + 8 - camPos.x, dz = cz * CHUNK + 8 - camPos.z;
            if (dx * dx + dz * dz > (fogEnd + 24) * (fogEnd + 24)) continue;   // hidden by fog anyway
            kv.second->solid.DrawUploaded(GL_TRIANGLES);
        }

        // ---- Dropped items: spinning, bobbing mini blocks (flat sprites for food) ----
        if (!gItems.empty()) {
            scratch.clear();
            for (const ItemEntity& e : gItems) {
                float t = e.age + alpha;
                float bobY = std::sin(t / 10.0f + e.bobOffset) * 0.1f + 0.1f;
                float spin = t / 20.0f + e.bobOffset;
                Vec3 c = {Lerp((float)e.px, (float)e.x, alpha), Lerp((float)e.py, (float)e.y, alpha) + bobY,
                          Lerp((float)e.pz, (float)e.z, alpha)};
                Vec3 lc = LightColorAt(*gWorld, c.x, c.y + 0.1f, c.z);
                float light = std::max(lc.x, std::max(lc.y, lc.z));
                int copies = e.stack.count > 16 ? 3 : (e.stack.count > 1 ? 2 : 1);   // bigger stacks look bigger
                for (int k = 0; k < copies; ++k) {
                    Vec3 o = {k * 0.06f * std::cos(e.bobOffset + k), k * 0.05f, k * 0.06f * std::sin(e.bobOffset + k)};
                    if (IsPlaceable(e.stack.item) && !DrawsFlat(e.stack.item))
                        AddItemCube(scratch, c + o + Vec3{0, 0.125f, 0}, 0.125f, spin, e.stack.item, light);
                    else
                        AddItemSprite(scratch, c + o + Vec3{0, 0.25f, 0}, 0.25f, spin, ItemTile(e.stack.item), light);
                }
            }
            glUseProgram(simpleProg);
            glUniformMatrix4fv(sMVP, 1, GL_FALSE, viewProj.m);
            glUniform1i(sUseTex, 1);
            glDisable(GL_CULL_FACE);
            overlay.Draw(scratch, GL_TRIANGLES);
            glEnable(GL_CULL_FACE);
        }

        // ---- Particles ----
        if (!gParticles.empty()) {
            scratch.clear();
            for (const Particle& q : gParticles) {
                Vec3 c = {Lerp((float)q.px, (float)q.x, alpha), Lerp((float)q.py, (float)q.y, alpha), Lerp((float)q.pz, (float)q.z, alpha)};
                float t = (q.age + alpha) / q.life;
                float sz = q.size * (q.grow ? 1.0f + 1.2f * t : 1.0f) * (q.shrink ? 1.0f - 0.5f * t * t : 1.0f);
                Vec3 lc = q.emissive ? Vec3{1, 1, 1} : LightColorAt(*gWorld, c.x, c.y, c.z);
                Vec3 r = camRight * sz, up = camUp * sz;
                Vec3 v4[4] = {c - r - up, c + r - up, c + r + up, c - r + up};
                for (int k : {0, 1, 2, 2, 3, 0}) PushV(scratch, v4[k], 0, 0, q.r * lc.x, q.g * lc.y, q.b * lc.z, 1);
            }
            glUseProgram(simpleProg);
            glUniformMatrix4fv(sMVP, 1, GL_FALSE, viewProj.m);
            glUniform1i(sUseTex, 0);
            glDisable(GL_CULL_FACE);
            overlay.Draw(scratch, GL_TRIANGLES);
            glEnable(GL_CULL_FACE);
        }

        // ---- Third person player model ----
        if (gThirdPerson) {
            scratch.clear();
            const float u = 1.8f / 32.0f;   // one "model pixel"
            float sneakDrop = (p.sneaking && !p.flying) ? 0.25f : 0.0f;
            float legTop = 12 * u, bodyTop = 24 * u - sneakDrop;
            AddBox(scratch, feet, -4 * u, 4 * u, 0, legTop - sneakDrop * 0.4f, -2 * u, 2 * u, p.yaw, 0.24f, 0.26f, 0.55f);
            AddBox(scratch, feet, -4 * u, 4 * u, legTop - sneakDrop * 0.4f, bodyTop, -2 * u, 2 * u, p.yaw, 0.15f, 0.62f, 0.66f);
            AddBox(scratch, feet, -8 * u, -4 * u, bodyTop - 12 * u, bodyTop, -2 * u, 2 * u, p.yaw, 0.82f, 0.64f, 0.50f);
            AddBox(scratch, feet, 4 * u, 8 * u, bodyTop - 12 * u, bodyTop, -2 * u, 2 * u, p.yaw, 0.82f, 0.64f, 0.50f);
            AddBox(scratch, feet, -4 * u, 4 * u, bodyTop, bodyTop + 8 * u, -4 * u, 4 * u, p.yaw, 0.85f, 0.66f, 0.52f);
            Vec3 lc = LightColorAt(*gWorld, feet.x, feet.y + 1.2, feet.z);
            for (size_t i = 0; i < scratch.size(); i += 9) { scratch[i + 5] *= lc.x; scratch[i + 6] *= lc.y; scratch[i + 7] *= lc.z; }
            glUseProgram(simpleProg);
            glUniformMatrix4fv(sMVP, 1, GL_FALSE, viewProj.m);
            glUniform1i(sUseTex, 0);
            overlay.Draw(scratch, GL_TRIANGLES);
        }

        // ---- Target outline and crack overlay ----
        RayHit target;
        if (gCaptured) {
            Vec3 d = DirFromAngles(p.yaw, p.pitch);
            target = Raycast(*gWorld, feet.x, feet.y + eyeH, feet.z, d.x, d.y, d.z,
                             p.creative ? REACH_CREATIVE : REACH_SURVIVAL);
        }
        if (target.hit) {
            glEnable(GL_BLEND);
            glUseProgram(simpleProg);
            glUniformMatrix4fv(sMVP, 1, GL_FALSE, viewProj.m);

            if (gBreaking && gBreakProgress > 0 && target.x == gBX && target.y == gBY && target.z == gBZ) {
                int stage = std::min(9, (int)(gBreakProgress * 10.0));
                float u0, v0, u1, v1; TileUV(T_CRACK0 + stage, u0, v0, u1, v1);
                scratch.clear();
                const float e = 0.002f;
                for (const FaceDef& F : FACES) {
                    Vec3 q[4]; float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
                    for (int i = 0; i < 4; ++i)
                        q[i] = {gBX + F.c[i][0] * (1 + 2 * e) - e, gBY + F.c[i][1] * (1 + 2 * e) - e,
                                gBZ + F.c[i][2] * (1 + 2 * e) - e};
                    for (int k : {0, 1, 2, 2, 3, 0}) PushV(scratch, q[k], uv[k][0], uv[k][1], 1, 1, 1, 1);
                }
                glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);   // multiply: gray 0.5 leaves color unchanged
                glDepthMask(GL_FALSE);
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(-1.0f, -1.0f);
                glUniform1i(sUseTex, 1);
                overlay.Draw(scratch, GL_TRIANGLES);
                glDisable(GL_POLYGON_OFFSET_FILL);
                glDepthMask(GL_TRUE);
            }

            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            scratch.clear();
            const float e = 0.002f;
            AABB tb = BlockBox(gWorld->Get(target.x, target.y, target.z), target.x, target.y, target.z);
            float x0 = (float)tb.minX - e, y0 = (float)tb.minY - e, z0 = (float)tb.minZ - e;
            float x1 = (float)tb.maxX + e, y1 = (float)tb.maxY + e, z1 = (float)tb.maxZ + e;
            Vec3 c[8] = {{x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1},{x0,y1,z0},{x1,y1,z0},{x1,y1,z1},{x0,y1,z1}};
            const int edges[24] = {0,1,1,2,2,3,3,0, 4,5,5,6,6,7,7,4, 0,4,1,5,2,6,3,7};
            for (int i : edges) PushV(scratch, c[i], 0, 0, 0, 0, 0, 0.45f);
            glUniform1i(sUseTex, 0);
            glLineWidth(2.0f);
            lines.Draw(scratch, GL_LINES);
            glDisable(GL_BLEND);
        }

        // ---- Water (translucent pass: blended, no depth writes, both sides) ----
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glUseProgram(blockProg);
        glUniform1i(bTransl, 1);
        for (const auto& kv : gMeshes) kv.second->water.DrawUploaded(GL_TRIANGLES);

        // ---- Clouds ----
        if (!underwater) {
            glUseProgram(simpleProg);
            glUniform1i(sUseTex, 0);
            float cl = Lerp(0.1f, 1.0f, gDaylight);   // clouds go gray-blue at night
            glUniform4f(sTint, cl, cl, Lerp(0.16f, 1.0f, gDaylight), 1);
            float drift = std::fmod((float)now * 0.6f, CLOUD_PERIOD);
            float baseX = std::floor((camPos.x - drift + 256) / CLOUD_PERIOD) * CLOUD_PERIOD + drift;
            float baseZ = std::floor((camPos.z + 256) / CLOUD_PERIOD) * CLOUD_PERIOD;
            for (int i = -1; i <= 1; ++i)
                for (int j = -1; j <= 1; ++j) {
                    Mat4 mvp = viewProj * Translate({baseX + i * CLOUD_PERIOD, 0, baseZ + j * CLOUD_PERIOD});
                    glUniformMatrix4fv(sMVP, 1, GL_FALSE, mvp.m);
                    clouds.DrawUploaded(GL_TRIANGLES);
                }
            glUniform4f(sTint, 1, 1, 1, 1);
        }
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);

        // ---- HUD ----
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glUseProgram(hudProg);
        Mat4 ortho = Ortho(0, (float)gFbW, (float)gFbH, 0);
        glUniformMatrix4fv(hProj, 1, GL_FALSE, ortho.m);

        const float S = (float)std::max(1, std::min(gFbH / 240, gFbW / 320));   // GUI scale
        Hud hud;
        if (underwater) hud.Rect(0, 0, (float)gFbW, (float)gFbH, 0.05f, 0.15f, 0.45f, 0.25f);
        if (!gCaptured && gScreen == Screen::None && !gPaused) hud.Rect(0, 0, (float)gFbW, (float)gFbH, 0, 0, 0, 0.45f);

        // Hotbar: 182 x 22 GUI pixels, 9 slots 20 pixels apart
        float hx = std::floor((gFbW - 182 * S) / 2), hy = gFbH - 22 * S;
        hud.Rect(hx, hy, hx + 182 * S, hy + 22 * S, 0.08f, 0.08f, 0.08f, 0.62f);
        for (int i = 0; i < 9; ++i) {
            float sx = hx + (1 + i * 20) * S, sy = hy + S;
            hud.Frame(sx, sy, sx + 20 * S, sy + 20 * S, S, 0.42f, 0.42f, 0.42f, 0.9f);
            if (p.creative) hud.ItemIcon(gCreativeBar[i], sx + 10 * S, sy + 10 * S, 6.2f * S);
            else            hud.DrawStack(gInv.slots[i], sx + 2 * S, sy + 2 * S, S);
        }
        float selX = hx + (gSelected * 20 - 1) * S, selY = hy - S;
        hud.Frame(selX, selY, selX + 24 * S, selY + 24 * S, 2 * S, 1, 1, 1, 1);

        // Hearts (left) and hunger (right) above the hotbar, breath above hunger
        if (!p.creative) {
            const float rowY = hy - 11 * S;
            const int hp = (int)std::ceil(p.health);
            const bool blink = p.invulnerable >= 10 && (p.invulnerable / 3) % 2 == 1;   // flashes when hit
            const Rgba outline = blink ? Rgba{1, 1, 1, 1} : Rgba{0.12f, 0.02f, 0.02f, 1};
            auto heartEmpty = [&](char ch, Rgba& c) {
                if (ch == '.') return false;
                c = ch == 'K' ? outline : Rgba{0.2f, 0.08f, 0.08f, 0.85f};
                return true;
            };
            auto heartFull = [&](char ch, Rgba& c) {
                if (ch == '.') return false;
                c = ch == 'K' ? outline : ch == 'W' ? Rgba{1, 0.72f, 0.72f, 1} : Rgba{0.86f, 0.1f, 0.1f, 1};
                return true;
            };
            for (int i = 0; i < 10; ++i) {
                float x = hx + i * 8 * S, y = rowY;
                if (hp <= 4) y += std::floor(Hash01(i, gTicks, 0, 77) * 2.0) * S;   // low health shiver
                hud.Pixels(HEART, 8, x, y, S, heartEmpty);
                if (hp >= i * 2 + 2)      hud.Pixels(HEART, 8, x, y, S, heartFull);
                else if (hp == i * 2 + 1) hud.Pixels(HEART, 8, x, y, S, heartFull, 0, 4);
            }

            auto shankEmpty = [](char ch, Rgba& c) {
                if (ch == '.') return false;
                c = ch == 'K' ? Rgba{0.18f, 0.1f, 0.04f, 1} : Rgba{0.2f, 0.14f, 0.1f, 0.8f};
                return true;
            };
            auto shankFull = [](char ch, Rgba& c) {
                switch (ch) {
                    case 'K': c = {0.18f, 0.1f, 0.04f, 1}; return true;
                    case 'M': c = {0.74f, 0.42f, 0.18f, 1}; return true;
                    case 'W': c = {0.96f, 0.72f, 0.46f, 1}; return true;
                    case 'B': c = {0.93f, 0.89f, 0.8f, 1};  return true;
                    default:  return false;
                }
            };
            for (int i = 0; i < 10; ++i) {
                float x = hx + (182 - 9 - i * 8) * S, y = rowY;
                if (p.saturation <= 0 && gTicks % (p.food * 3 + 1) == 0)          // hungry twitch
                    y += std::floor(Hash01(i, gTicks, 1, 77) * 3.0 - 1.0) * S;
                hud.Pixels(SHANK, 9, x, y, S, shankEmpty);
                if (p.food >= i * 2 + 2)      hud.Pixels(SHANK, 9, x, y, S, shankFull);
                else if (p.food == i * 2 + 1) hud.Pixels(SHANK, 9, x, y, S, shankFull, 4, 9);
            }

            if (EyesInWater(*gWorld, p) || p.air < MAX_AIR) {
                int bubbles = (int)std::ceil((p.air - 2) * 10.0 / MAX_AIR);
                auto bubble = [](char ch, Rgba& c) {
                    switch (ch) {
                        case 'K': c = {0.1f, 0.25f, 0.62f, 1}; return true;
                        case 'B': c = {0.38f, 0.62f, 1.0f, 0.9f}; return true;
                        case 'W': c = {0.88f, 0.96f, 1, 1}; return true;
                        default:  return false;
                    }
                };
                for (int i = 0; i < bubbles; ++i)
                    hud.Pixels(BUBBLE, 9, hx + (182 - 9 - i * 8) * S, rowY - 10 * S, S, bubble);
            }
        }

        // Eating progress under the crosshair
        if (gEatTicks > 0) {
            float cx = std::floor(gFbW / 2.0f), cy = std::floor(gFbH / 2.0f) + 10 * S;
            float f = (float)gEatTicks / APPLE_EAT_TICKS;
            hud.Rect(cx - 8 * S, cy, cx + 8 * S, cy + 2 * S, 0, 0, 0, 0.6f);
            hud.Rect(cx - 8 * S, cy, cx - 8 * S + 16 * S * f, cy + 2 * S, 0.4f, 0.9f, 0.3f, 1);
        }

        if (gScreen != Screen::None) DrawScreen(hud);
        if (gDeadTicks > 0) hud.Rect(0, 0, (float)gFbW, (float)gFbH, 0.55f, 0.0f, 0.0f, 0.45f);
        if (now - gSavedAt < 2.0) hud.Text("SAVED", gFbW - Hud::TextWidth("SAVED", S) - 4 * S, 4 * S, S, {1, 1, 1, 0.8f}, true);
        if (gPaused) BuildMenu(hud);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        hudMesh.Draw(hud.v, GL_TRIANGLES);

        // Crosshair inverts whatever is behind it
        if (gCaptured && !gThirdPerson) {
            Hud cross;
            float cx = std::floor(gFbW / 2.0f), cy = std::floor(gFbH / 2.0f);
            cross.Rect(cx - 5 * S, cy - S * 0.5f, cx + 5 * S, cy + S * 0.5f, 1, 1, 1, 1);
            cross.Rect(cx - S * 0.5f, cy - 5 * S, cx + S * 0.5f, cy - S * 0.5f, 1, 1, 1, 1);
            cross.Rect(cx - S * 0.5f, cy + S * 0.5f, cx + S * 0.5f, cy + 5 * S, 1, 1, 1, 1);
            glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ZERO);
            crossMesh.Draw(cross.v, GL_TRIANGLES);
        }
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);

        // ---- Title bar as a mini F3 screen ----
        ++frames;
        titleTimer += frameDt;
        if (titleTimer >= 0.25) {
            fps = (int)(frames / titleTimer);
            frames = 0; titleTimer = 0;
            char buf[512];
            const char* mode = p.creative ? "Creative" : "Survival";
            uint8_t heldItem = SelectedItem();
            const char* held = heldItem == AIR ? "nothing" : ItemName(heldItem);
            if (gDebug) {
                Vec3 f = DirFromAngles(p.yaw, 0);
                const char* facing = std::fabs(f.x) > std::fabs(f.z) ? (f.x > 0 ? "east (+X)" : "west (-X)")
                                                                    : (f.z > 0 ? "south (+Z)" : "north (-Z)");
                double hs = std::sqrt(p.vx * p.vx + p.vz * p.vz) * TPS;
                std::string state;
                if (p.flying) state += " flying";
                if (p.sprinting) state += " sprinting";
                if (p.sneaking) state += " sneaking";
                if (p.inWater) state += " swimming";
                char look[96] = "";
                if (target.hit) std::snprintf(look, sizeof look, " | Looking at %s (%d %d %d)",
                                              Info(gWorld->Get(target.x, target.y, target.z)).name, target.x, target.y, target.z);
                char stats[96] = "";
                if (!p.creative) std::snprintf(stats, sizeof stats, " | HP %.1f Food %d Sat %.1f Exh %.2f",
                                               p.health, p.food, p.saturation, p.exhaustion);
                long long tod = gDayTime % DAY_TICKS;                        // 0 = 6:00
                int hour = (int)((tod / 1000 + 6) % 24), minute = (int)(tod % 1000 * 60 / 1000);
                const char* biomeName = BiomeData(gWorld->BiomeAt((int)std::floor(p.x), (int)std::floor(p.z))).name;
                std::snprintf(buf, sizeof buf, "MyEngine | %d fps | Day %lld %02d:%02d | %s | XYZ %.2f / %.2f / %.2f | Facing %s | %.2f m/s%s | %s%s | Holding %s | Items %d%s | Seed %u",
                              fps, gDayTime / DAY_TICKS + 1, hour, minute, biomeName, p.x, p.y, p.z, facing, hs, state.c_str(), mode, stats, held,
                              (int)gItems.size(), look, gWorld->seed);
            } else {
                std::snprintf(buf, sizeof buf, "MyEngine | %s | Holding %s", mode, held);
            }
            if (gDeadTicks > 0) std::snprintf(buf, sizeof buf, "MyEngine | You died! Respawning...");
            if (gPaused) std::snprintf(buf, sizeof buf, "MyEngine | %s | Paused", gWorldName.c_str());
            if (!PlayerAreaReady()) std::snprintf(buf, sizeof buf, "MyEngine | Loading terrain...");
            glfwSetWindowTitle(gWindow, buf);
        }

        glfwSwapBuffers(gWindow);
    }

    if (gWorld) SaveGame();   // closing the window saves the world
    delete gWorld;
    glfwDestroyWindow(gWindow);
    glfwTerminate();
    return 0;
}
