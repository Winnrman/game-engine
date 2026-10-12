#pragma once
// ============================================================================
// atlas.h : procedurally painted 16x16 pixel-art textures, packed into one
// 256x256 texture atlas. No image files needed.
//
// Why an atlas? The whole world is drawn with ONE texture bound, so every
// chunk can be a single draw call. Each block face just points its UVs at
// a different 16x16 square inside the big image.
// ============================================================================
#include "game.h"
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace atlas {

constexpr int SIZE = 256;   // atlas is 256x256 pixels
constexpr int TILE = 16;    // each tile is 16x16
constexpr int GRID = SIZE / TILE;

struct Image {
    std::vector<uint8_t> px = std::vector<uint8_t>((size_t)SIZE * SIZE * 4, 0);
};

struct Col { double r, g, b; };

inline uint8_t Clamp8(double v) { return (uint8_t)std::max(0.0, std::min(255.0, v)); }

// (x, y) are tile-local pixels with y = 0 at the TOP of the tile.
// OpenGL textures start at the bottom row, so we flip here once.
inline void Put(Image& im, int tile, int x, int y, Col c, double a = 255) {
    int tx = tile % GRID, ty = tile / GRID;
    int row = ty * TILE + (TILE - 1 - y);
    int col = tx * TILE + x;
    uint8_t* p = &im.px[((size_t)row * SIZE + col) * 4];
    p[0] = Clamp8(c.r); p[1] = Clamp8(c.g); p[2] = Clamp8(c.b); p[3] = Clamp8(a);
}

inline double Rnd(int tile, int x, int y, int salt = 0) {
    return game::Hash01(x, y, tile * 131 + salt, 0xA71A5u);
}
inline Col Mul(Col c, double k) { return {c.r * k, c.g * k, c.b * k}; }

// Grass, leaves and water are painted in gray and tinted by the biome when
// drawn, the way Minecraft colors them (so a swamp and a jungle can share art)

// Shared looks, reused by several tiles
inline Col DirtAt(int x, int y) {
    double n = Rnd(2, x, y), s = Rnd(2, x, y, 1);
    Col c = Mul({134, 96, 67}, 0.86 + 0.26 * n);
    if (s < 0.12) c = {104, 74, 50};
    else if (s > 0.94) c = {158, 116, 84};
    return c;
}
inline Col GrassAt(int x, int y) {   // gray: multiplied by the biome's grass color
    double n = Rnd(0, x, y), s = Rnd(0, x, y, 1);
    double v = 205 * (0.84 + 0.28 * n);
    if (s > 0.88) v *= 1.15;
    return {v, v, v};
}
inline Col StoneAt(int x, int y, int salt = 0) {
    double n = Rnd(3, x, y, salt), m = Rnd(3, x / 3, y / 2, salt + 5);
    double v = 120 + 26 * (n - 0.5) + 18 * (m - 0.5);
    return {v, v, v};
}

inline void PaintOre(Image& im, int tile, Col ore) {
    int cx[5], cy[5];
    for (int i = 0; i < 5; ++i) {
        cx[i] = 2 + (int)(Rnd(tile, i, 0, 9) * 12);
        cy[i] = 2 + (int)(Rnd(tile, i, 1, 9) * 12);
    }
    for (int y = 0; y < TILE; ++y)
        for (int x = 0; x < TILE; ++x) {
            Col c = StoneAt(x, y, tile);
            for (int i = 0; i < 5; ++i) {
                double dx = x - cx[i], dy = y - cy[i];
                if (dx * dx + dy * dy <= 2.2 && Rnd(tile, x, y, 3) > 0.15)
                    c = Mul(ore, 0.8 + 0.4 * Rnd(tile, x, y, 4));
            }
            Put(im, tile, x, y, c);
        }
}

// Wraps a filled shape in a 1px dark outline, like Minecraft item sprites.
// shape[y][x]: 0 = empty, anything else = filled.
inline void Outline(Image& im, int tile, const int (&shape)[16][16], Col edge) {
    for (int y = 0; y < TILE; ++y)
        for (int x = 0; x < TILE; ++x) {
            if (shape[y][x]) continue;
            bool near = (x > 0 && shape[y][x - 1]) || (x < 15 && shape[y][x + 1]) ||
                        (y > 0 && shape[y - 1][x]) || (y < 15 && shape[y + 1][x]);
            Put(im, tile, x, y, edge, near ? 255 : 0);
        }
}

// Tools and sticks: a handle running diagonally from the bottom-left corner
// to the top-right, with the head drawn in (u, v) coordinates along and
// across the handle. kind: 0 pickaxe, 1 axe, 2 shovel, 3 sword, 4 stick
inline void PaintTool(Image& im, int tile, int kind, Col head) {
    const Col wood = {137, 103, 58};
    int shape[16][16] = {};
    Col col[16][16];
    for (int y = 0; y < TILE; ++y)
        for (int x = 0; x < TILE; ++x) {
            double px = x - 1.0, py = y - 14.0;
            double u = (px - py) * 0.70710678;   // along the handle
            double v = (px + py) * 0.70710678;   // across it (negative = upper left)
            int s = 0;
            double handleEnd = kind == 4 ? 17.0 : (kind == 3 ? 4.6 : 15.0);
            // Handles are a 2-pixel staircase, like Minecraft's sticks
            if (v > -0.1 && v < 0.75 && u > 0.2 && u < handleEnd) s = 1;
            switch (kind) {
                case 0:   // pickaxe: an arc across the end of the handle
                    if (std::fabs(u - (14.6 - 0.1 * v * v)) < 1.05 && std::fabs(v) < 6.4) s = 2;
                    break;
                case 1:   // axe: a blade on one side near the top
                    if (u > 10.3 && u < 15.6 && v > -4.6 && v < 0.6 &&
                        !(v < -3.4 && (u < 11.0 || u > 14.9))) s = 2;
                    break;
                case 2: { // shovel: a rounded scoop
                    double a = (u - 14.4) / 2.5, b = v / 2.0;
                    if (a * a + b * b < 1.0) s = 2;
                    break;
                }
                case 3:   // sword: blade with a point, a cross guard, a short grip
                    if (u > 5.2 && std::fabs(v) < 1.25 && std::fabs(v) < (19.6 - u) * 0.75) s = 2;
                    if (u > 3.8 && u <= 5.2 && std::fabs(v) < 3.3) s = 3;
                    break;
                default: break;
            }
            shape[y][x] = s;
            double light = v < -0.2 ? 1.12 : (v > 0.4 ? 0.82 : 1.0);   // lit from the top left
            double n = 0.9 + 0.2 * Rnd(tile, x, y);
            if (s == 1)      col[y][x] = Mul(wood, light * n);
            else if (s == 2) col[y][x] = Mul(head, light * n);
            else if (s == 3) col[y][x] = Mul(wood, 0.8 * n);
        }
    for (int y = 0; y < TILE; ++y)
        for (int x = 0; x < TILE; ++x)
            if (shape[y][x]) Put(im, tile, x, y, col[y][x]);
    Outline(im, tile, shape, {40, 30, 18});
}

inline void PaintCoalItem(Image& im, int tile) {
    int shape[16][16] = {};
    for (int y = 0; y < TILE; ++y)
        for (int x = 0; x < TILE; ++x) {
            double dx = x - 7.5, dy = y - 8.0;
            double ang = std::atan2(dy, dx);
            double r = 5.2 + 0.9 * std::sin(3 * ang + 1.0) + 0.5 * std::sin(5 * ang);   // lumpy
            if (std::sqrt(dx * dx + dy * dy) > r) continue;
            shape[y][x] = 1;
            double n = Rnd(tile, x, y);
            Col c = Mul({46, 46, 50}, 0.8 + 0.4 * n);
            if (n > 0.86) c = {104, 104, 112};                        // glints
            if (dx + dy < -4) c = Mul(c, 1.35);                       // top-left highlight
            Put(im, tile, x, y, c);
        }
    Outline(im, tile, shape, {16, 16, 18});
}

inline void PaintCraftingTable(Image& im) {
    using namespace game;
    for (int y = 0; y < TILE; ++y)
        for (int x = 0; x < TILE; ++x) {
            // Top: a planked work surface with a dark rim and a 3x3 grid
            Col plank = Mul({166, 134, 82}, 0.9 + 0.12 * Rnd(18, x / 3, y));
            Col top = plank;
            if (x == 0 || x == 15 || y == 0 || y == 15) top = {92, 66, 36};
            else if (x == 5 || x == 10 || y == 5 || y == 10) top = Mul(plank, 0.72);
            Put(im, T_TABLE_TOP, x, y, top);

            // Side: dark top edge, a saw and a hammer hanging, legs at the bottom
            Col side = Mul({150, 116, 70}, 0.88 + 0.14 * Rnd(19, x, y / 4));
            if (y % 4 == 3) side = Mul(side, 0.8);
            if (y < 3) side = Mul({112, 82, 46}, 0.9 + 0.15 * Rnd(19, x, y, 1));
            bool saw    = x >= 2 && x <= 6 && y >= 5 && y <= 10 && !(y == 10 && x % 2 == 1);
            bool sawGrip = x >= 3 && x <= 5 && y == 4;
            bool hammer = (x == 11 && y >= 5 && y <= 11) || (x >= 9 && x <= 13 && y >= 4 && y <= 5);
            if (saw)     side = Mul({196, 196, 200}, 0.85 + 0.15 * Rnd(19, x, y, 2));
            if (sawGrip) side = {92, 62, 30};
            if (hammer)  side = (y <= 5) ? Col{120, 120, 126} : Col{92, 62, 30};
            if (y >= 13 && x >= 3 && x <= 12) side = {60, 42, 22};   // shadow between the legs
            Put(im, T_TABLE_SIDE, x, y, side);
        }
}

// Spruce and birch wood, cactus, snow and ice
inline void PaintBiomeBlocks(Image& im) {
    using namespace game;
    for (int y = 0; y < TILE; ++y)
        for (int x = 0; x < TILE; ++x) {
            // Spruce: dark, deeply grooved bark; dark rings; dark planks
            {
                Col bark = Mul({74, 52, 30}, 0.75 + 0.35 * Rnd(56, x, 0));
                bark = Mul(bark, 0.88 + 0.2 * Rnd(56, x, y));
                if (x % 4 == 0) bark = Mul(bark, 0.72);
                Put(im, T_SPRUCE_LOG, x, y, bark);
                double d = std::max(std::fabs(x - 7.5), std::fabs(y - 7.5));
                Col top = d > 6.5 ? Mul({74, 52, 30}, 0.9) : (((int)d % 2 == 0) ? Col{120, 88, 52} : Col{96, 70, 40});
                Put(im, T_SPRUCE_LOG_TOP, x, y, Mul(top, 0.94 + 0.1 * Rnd(57, x, y)));
                int board = y / 4;
                Col pl = Mul({114, 84, 48}, 0.9 + 0.12 * Rnd(59, x / 3, y, board));
                if (y % 4 == 3) pl = {76, 54, 30};
                if (x == (board * 7 + 2) % 16) pl = Mul(pl, 0.72);
                Put(im, T_SPRUCE_PLANKS, x, y, pl);
            }
            // Birch: white bark with black dashes; pale rings; pale planks
            {
                Col bark = Mul({216, 214, 206}, 0.92 + 0.1 * Rnd(60, x, y));
                double dash = Rnd(60, x / 3, y, 3);
                if (dash > 0.86 && Rnd(60, x, y / 2, 4) > 0.3) bark = {46, 44, 40};
                else if (dash > 0.8) bark = Mul(bark, 0.8);
                Put(im, T_BIRCH_LOG, x, y, bark);
                double d = std::max(std::fabs(x - 7.5), std::fabs(y - 7.5));
                Col top = d > 6.5 ? Col{214, 212, 204} : (((int)d % 2 == 0) ? Col{200, 182, 128} : Col{176, 158, 104});
                Put(im, T_BIRCH_LOG_TOP, x, y, Mul(top, 0.94 + 0.1 * Rnd(61, x, y)));
                int board = y / 4;
                Col pl = Mul({196, 178, 122}, 0.92 + 0.1 * Rnd(63, x / 3, y, board));
                if (y % 4 == 3) pl = {150, 132, 86};
                if (x == (board * 3 + 5) % 16) pl = Mul(pl, 0.8);
                Put(im, T_BIRCH_PLANKS, x, y, pl);
            }
            // Cactus: ribbed green sides with pale spines; the side columns at
            // the very edge are see-through because the model is inset 1px
            {
                Col g = Mul({70, 128, 42}, 0.85 + 0.2 * Rnd(64, x, y));
                if (x % 4 == 1) g = Mul(g, 0.75);                                // ribs
                bool spine = (x % 4 == 3) && (y % 3 == (x / 4) % 3);
                if (spine) g = {214, 220, 180};
                Put(im, T_CACTUS_SIDE, x, y, g, (x == 0 || x == 15) ? 0 : 255);
                double d = std::max(std::fabs(x - 7.5), std::fabs(y - 7.5));
                Col top = d > 6.5 ? Col{0, 0, 0} : (d > 5.5 ? Mul({70, 128, 42}, 0.8) : Mul({96, 150, 60}, 0.9 + 0.15 * Rnd(65, x, y)));
                if (d < 1.5) top = {120, 170, 80};
                Put(im, T_CACTUS_TOP, x, y, top, d > 6.5 ? 0 : 255);
                Put(im, T_CACTUS_BOTTOM, x, y, Mul({70, 128, 42}, 0.7 + 0.15 * Rnd(66, x, y)), d > 6.5 ? 0 : 255);
            }
            // Snow: almost white with a cold blue speckle
            Put(im, T_SNOW, x, y, Mul({242, 250, 252}, 0.93 + 0.07 * Rnd(67, x, y)));
            // Ice: translucent pale blue with long streaks
            {
                Col c = Mul({150, 186, 245}, 0.9 + 0.12 * Rnd(68, x, y));
                if (Rnd(68, (x + y) / 3, 0, 1) > 0.75) c = Mul(c, 1.12);
                Put(im, T_ICE, x, y, c, 165);
            }
        }
}

inline Image Build() {
    using namespace game;
    Image im;

    for (int y = 0; y < TILE; ++y)
    for (int x = 0; x < TILE; ++x) {
        // Grass top
        Put(im, T_GRASS_TOP, x, y, GrassAt(x, y));

        // Dirt
        Put(im, T_DIRT, x, y, DirtAt(x, y));

        // Grass side: plain dirt, with a ragged grassy fringe as a separate
        // overlay tile (tinted, drawn on top) like Minecraft's
        {
            bool green = y < 3 || (y == 3 && Rnd(1, x, 0) < 0.6) || (y == 4 && Rnd(1, x, 1) < 0.25);
            Put(im, T_GRASS_SIDE, x, y, DirtAt(x, y));
            Put(im, T_GRASS_OVERLAY, x, y, GrassAt(x, y), green ? 255 : 0);
            // Under snow the fringe is white
            Col snow = Mul({240, 248, 250}, 0.94 + 0.08 * Rnd(55, x, y));
            bool white = y < 2 || (y == 2 && Rnd(55, x, 0) < 0.7) || (y == 3 && Rnd(55, x, 1) < 0.3);
            Put(im, T_GRASS_SNOW_SIDE, x, y, white ? snow : DirtAt(x, y));
        }

        // Stone
        Put(im, T_STONE, x, y, StoneAt(x, y));

        // Log side: vertical bark stripes
        {
            double stripe = Rnd(5, x, 0), n = Rnd(5, x, y);
            Col c = Mul({104, 82, 50}, 0.75 + 0.35 * stripe);
            c = Mul(c, 0.9 + 0.18 * n);
            if (Rnd(5, x, y / 3, 2) < 0.12) c = Mul(c, 0.7);
            Put(im, T_LOG_SIDE, x, y, c);
        }

        // Log top: square growth rings with bark rim
        {
            double d = std::max(std::fabs(x - 7.5), std::fabs(y - 7.5));
            Col c;
            if (d > 6.5) c = Mul({104, 82, 50}, 0.85 + 0.25 * Rnd(6, x, y));
            else c = ((int)d % 2 == 0) ? Col{182, 148, 94} : Col{152, 120, 74};
            c = Mul(c, 0.94 + 0.1 * Rnd(6, x, y, 1));
            Put(im, T_LOG_TOP, x, y, c);
        }

        // Leaves: gray (tinted per biome / tree) with see-through gaps
        {
            double v = 165 * (0.72 + 0.48 * Rnd(7, x, y));
            Put(im, T_LEAVES, x, y, {v, v, v}, Rnd(7, x, y, 1) < 0.22 ? 0 : 255);
            double sv = 150 * (0.7 + 0.45 * Rnd(58, x, y));                    // spruce: denser needles
            Put(im, T_SPRUCE_LEAVES, x, y, {sv, sv, sv}, Rnd(58, x, y, 1) < 0.14 ? 0 : 255);
            double bv = 175 * (0.75 + 0.4 * Rnd(62, x, y));                    // birch: lighter, airier
            Put(im, T_BIRCH_LEAVES, x, y, {bv, bv, bv}, Rnd(62, x, y, 1) < 0.26 ? 0 : 255);
        }

        // Planks: 4 boards with staggered seams
        {
            int board = y / 4;
            Col c = Mul({166, 134, 82}, 0.9 + 0.12 * Rnd(8, x / 3, y, board));
            if (y % 4 == 3) c = {112, 88, 54};
            if (x == (board * 5 + 3) % 16) c = Mul(c, 0.72);
            Put(im, T_PLANKS, x, y, c);
        }

        // Sand
        {
            Col c = Mul({219, 207, 160}, 0.92 + 0.12 * Rnd(9, x, y));
            if (Rnd(9, x, y, 1) > 0.95) c = Mul(c, 0.85);
            Put(im, T_SAND, x, y, c);
        }

        // Glass: frame plus a couple of glints, clear in the middle
        {
            bool edge = x == 0 || x == 15 || y == 0 || y == 15;
            bool glint = (x - y == 4 && x > 5 && x < 10) || (x - y == 2 && x > 4 && x < 7) ||
                         (y - x == -9 && x > 10 && x < 14);
            if (edge)       Put(im, T_GLASS, x, y, {200, 226, 236}, 255);
            else if (glint) Put(im, T_GLASS, x, y, {236, 248, 255}, 255);
            else            Put(im, T_GLASS, x, y, {255, 255, 255}, 0);
        }

        // Bricks: 8x4 bricks, every other row offset
        {
            int row = y / 4, off = (row % 2) * 4;
            bool mortar = y % 4 == 3 || (x + off) % 8 == 7;
            int id = ((x + off) / 8) * 7 + row * 13;
            Col c = mortar ? Mul({178, 170, 160}, 0.92 + 0.12 * Rnd(11, x, y))
                           : Mul(Mul({150, 72, 56}, 0.84 + 0.28 * Rnd(11, id, 0, 2)), 0.92 + 0.12 * Rnd(11, x, y, 3));
            Put(im, T_BRICKS, x, y, c);
        }

        // Bedrock: harsh dark/light speckle
        {
            double a = Rnd(14, x, y), b = Rnd(14, x / 2, y / 2, 1);
            double v = 28 + 95 * (0.6 * a + 0.4 * b);
            Put(im, T_BEDROCK, x, y, {v, v, v});
        }

        // Water: translucent, gray here and tinted by the biome's water color
        {
            double v = 225 * (0.85 + 0.2 * Rnd(15, x, y));
            if (Rnd(15, x / 3, y, 1) > 0.9) v *= 1.12;
            Put(im, T_WATER, x, y, {v, v, v}, 175);
        }

        // Crate: a nod to the first textured cube in this engine
        {
            bool frame = x < 2 || x > 13 || y < 2 || y > 13;
            bool brace = std::abs(x - y) <= 1 || std::abs(x + y - 15) <= 1;
            Col c;
            if (frame || brace) c = Mul({196, 160, 104}, 0.9 + 0.15 * Rnd(16, x, y));
            else {
                c = Mul({150, 116, 70}, 0.88 + 0.14 * Rnd(16, x, y / 4));
                if (x % 4 == 1) c = Mul(c, 0.75);
            }
            if ((x == 1 || x == 14) && (y == 1 || y == 14)) c = {70, 70, 74};   // nails
            Put(im, T_CRATE, x, y, c);
        }

        // Apple (an item, not a block): red fruit, stem and leaf on transparent
        {
            double dx = x - 7.5, dy = (y - 9.0) * 1.1;
            double d = std::sqrt(dx * dx + dy * dy);
            Col c = {0, 0, 0}; double a = 0;
            if (d < 5.6 && !(y < 5 && std::fabs(dx) < 1.0)) {
                double lit = std::max(0.6, std::min(1.25, 1.0 - 0.06 * (dx + dy)));   // lit from top left
                c = Mul({200, 30, 34}, lit * (0.92 + 0.12 * Rnd(17, x, y)));
                if (d > 4.7) c = Mul(c, 0.72);                                         // darker rim
                if ((x == 4 || x == 5) && (y == 6 || y == 7)) c = {255, 196, 190};     // glint
                a = 255;
            }
            if (x == 7 && y >= 2 && y <= 4) { c = {92, 62, 30}; a = 255; }
            if ((y == 2 && (x == 8 || x == 9)) || (y == 3 && (x == 9 || x == 10))) { c = {72, 150, 40}; a = 255; }
            Put(im, T_APPLE, x, y, c, a);
        }
    }

    // Cobblestone: Voronoi cells (wrapping, so the tile repeats seamlessly)
    {
        const int N = 8;
        double sx[N], sy[N];
        for (int i = 0; i < N; ++i) { sx[i] = Rnd(4, i, 0, 7) * 16; sy[i] = Rnd(4, i, 1, 7) * 16; }
        for (int y = 0; y < TILE; ++y)
            for (int x = 0; x < TILE; ++x) {
                double d1 = 1e9, d2 = 1e9; int best = 0;
                for (int i = 0; i < N; ++i) {
                    double dx = std::fabs(x + 0.5 - sx[i]), dy = std::fabs(y + 0.5 - sy[i]);
                    dx = std::min(dx, 16 - dx); dy = std::min(dy, 16 - dy);
                    double d = std::sqrt(dx * dx + dy * dy);
                    if (d < d1) { d2 = d1; d1 = d; best = i; } else if (d < d2) d2 = d;
                }
                Col c;
                if (d2 - d1 < 1.1) c = Mul({72, 72, 72}, 0.9 + 0.2 * Rnd(4, x, y));
                else {
                    double v = 104 + 44 * Rnd(4, best, 3, 1) + 14 * (Rnd(4, x, y, 2) - 0.5) + (d1 < 1.8 ? 10 : 0);
                    c = {v, v, v};
                }
                Put(im, T_COBBLE, x, y, c);
            }
    }

    PaintOre(im, T_COAL, {40, 40, 42});
    PaintOre(im, T_IRON, {214, 172, 140});
    PaintOre(im, T_DIAMOND_ORE, {98, 232, 226});
    PaintOre(im, T_EMERALD_ORE, {40, 200, 96});

    // Furnace: smooth stone faces with a dark rim; the front has a fire mouth
    // that glows orange while lit
    for (int y = 0; y < TILE; ++y)
        for (int x = 0; x < TILE; ++x) {
            double v = 112 + 22 * (Rnd(45, x, y) - 0.5) + 10 * (Rnd(45, x / 4, y / 4, 1) - 0.5);
            Col stone = {v, v, v};
            bool rim = x == 0 || x == 15 || y == 0 || y == 15;
            Col side = rim ? Mul(stone, 0.7) : stone;
            if (!rim && (y == 4 || y == 11)) side = Mul(stone, 0.85);           // seams
            Put(im, T_FURNACE_SIDE, x, y, side);

            Col top = rim ? Mul(stone, 0.72) : Mul(stone, 1.05);
            if (x >= 3 && x <= 12 && y >= 3 && y <= 12 && (x == 3 || x == 12 || y == 3 || y == 12)) top = Mul(stone, 0.8);
            Put(im, T_FURNACE_TOP, x, y, top);

            bool mouth = x >= 4 && x <= 11 && y >= 8 && y <= 13;
            bool mouthRim = x >= 3 && x <= 12 && y >= 7 && y <= 14 && !mouth;
            bool grate = !mouth && x >= 4 && x <= 11 && y == 4 && x % 2 == 0;    // vent slots up top
            Col front = side;
            if (mouthRim || grate) front = Mul(stone, 0.55);
            Col unlit = mouth ? Col{24, 22, 22} : front;
            Put(im, T_FURNACE_FRONT, x, y, unlit);
            Col lit = front;
            if (mouth) {
                double heat = (y - 8) / 5.0 + 0.35 * (Rnd(46, x, y) - 0.5);     // hotter at the bottom
                lit = heat > 0.75 ? Col{255, 236, 140} : heat > 0.4 ? Col{250, 160, 40} : Col{170, 64, 20};
            }
            Put(im, T_FURNACE_FRONT_LIT, x, y, lit);
        }

    // Iron ingot: a little bar seen from above-front (light top, darker face)
    {
        int shape[16][16] = {};
        for (int y = 5; y <= 11; ++y) {
            int x0 = 2 + std::max(0, 7 - y), x1 = 12 + std::max(0, 7 - y);
            for (int x = x0; x <= std::min(x1, 14); ++x) {
                shape[y][x] = 1;
                double n = 0.94 + 0.1 * Rnd(47, x, y);
                Col c = y <= 7 ? Col{236, 236, 236} : Col{196, 196, 196};
                if (x >= x1 - 1 && y > 7) c = {160, 160, 160};                  // end face in shadow
                Put(im, T_IRON_INGOT, x, y, Mul(c, n));
            }
        }
        Outline(im, T_IRON_INGOT, shape, {70, 70, 74});
    }

    // Gems: a cut diamond (flat top, pointed bottom) and a tall emerald
    for (int k = 0; k < 2; ++k) {
        const int tile = k == 0 ? T_DIAMOND : T_EMERALD;
        int shape[16][16] = {};
        for (int y = 0; y < TILE; ++y)
            for (int x = 0; x < TILE; ++x) {
                double dx = std::fabs(x - 7.5), dy = y - 7.5;
                bool in;
                if (k == 0) in = (y >= 3 && y <= 5 && dx <= y) || (y >= 6 && y <= 12 && dx <= 5.5 - (y - 6) * 0.85);
                else        in = dx <= 3.6 && std::fabs(dy) <= 6.0 && dx + std::fabs(dy) * 0.55 <= 5.2;
                if (!in) continue;
                shape[y][x] = 1;
                double facet = (x < 8 ? 1.1 : 0.88) * (k == 0 && y <= 5 ? 1.15 : 1.0);   // lit from the left
                Col c = k == 0 ? Col{70, 214, 222} : Col{38, 190, 90};
                c = Mul(c, facet * (0.92 + 0.12 * Rnd(tile, x, y)));
                if ((k == 0 && (x - y == 1 || x + y == 14) && y >= 6) || (k == 1 && dx < 0.6 && dy < 1)) c = Mul(c, 1.3);   // facet lines
                if ((x == 5 || x == 6) && (y == 4 || y == 5)) c = {235, 255, 250};                                     // glint
                Put(im, tile, x, y, c);
            }
        Outline(im, tile, shape, k == 0 ? Col{18, 84, 96} : Col{10, 70, 34});
    }

    PaintCraftingTable(im);
    PaintBiomeBlocks(im);

    // Torch: a 2px stick (columns 7-8, rows 8-15) with a 2px flame on top
    // (rows 6-7). The 3D torch model samples exactly this 2x10 strip.
    for (int y = 6; y < TILE; ++y)
        for (int x = 7; x <= 8; ++x) {
            Col c;
            if (y == 6)      c = x == 7 ? Col{255, 255, 200} : Col{255, 232, 130};
            else if (y == 7) c = x == 7 ? Col{255, 214, 90} : Col{236, 160, 50};
            else             c = Mul(x == 7 ? Col{150, 112, 64} : Col{104, 76, 42}, 0.92 + 0.12 * Rnd(30, x, y));
            Put(im, T_TORCH, x, y, c);
        }
    PaintCoalItem(im, T_COAL_ITEM);
    const Col woodHead = {150, 116, 66}, stoneHead = {128, 128, 128}, ironHead = {216, 216, 216};
    PaintTool(im, T_STICK, 4, woodHead);   // a stick is all handle
    for (int k = 0; k < 4; ++k) {
        PaintTool(im, T_WOOD_PICKAXE + k, k, woodHead);
        PaintTool(im, T_STONE_PICKAXE + k, k, stoneHead);
        PaintTool(im, T_IRON_PICKAXE + k, k, ironHead);
    }

    // Crack overlay stages. Gray 128 means "no change" because these are
    // drawn with multiply blending (result = 2 * src * dst).
    for (int s = 0; s < 10; ++s) {
        int tile = T_CRACK0 + s;
        for (int y = 0; y < TILE; ++y)
            for (int x = 0; x < TILE; ++x) Put(im, tile, x, y, {128, 128, 128}, 255);
        const int PATHS = 7;
        for (int p = 0; p < PATHS; ++p) {
            double px = 7.5 + (Rnd(40, p, 0) - 0.5) * 3, py = 7.5 + (Rnd(40, p, 1) - 0.5) * 3;
            double ang = p * 6.2831853 / PATHS + Rnd(40, p, 2) * 0.8;
            int len = 2 + (int)(s * 1.15 * (0.7 + 0.6 * Rnd(40, p, 3)));
            for (int i = 0; i < len; ++i) {
                int ix = (int)std::floor(px), iy = (int)std::floor(py);
                if (ix >= 0 && ix < 16 && iy >= 0 && iy < 16) Put(im, tile, ix, iy, {38, 38, 38}, 255);
                px += std::cos(ang); py += std::sin(ang);
                ang += (Rnd(41, p, i) - 0.5) * 1.1;
            }
        }
    }
    return im;
}

} // namespace atlas
