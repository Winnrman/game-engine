#pragma once
// ============================================================================
// pack.h : optional Minecraft-style texture pack.
//
// Put PNGs in assets/pack/ using Minecraft's file names and folders, e.g.
//   assets/pack/block/grass_block_top.png      assets/pack/item/stick.png
// (an unzipped resource pack also works: assets/pack/assets/minecraft/textures/...)
// Any texture found replaces the built-in painted one; anything missing
// keeps the painted version. Bigger textures (32x, 64x) are scaled to 16x16,
// and animated strips use their first frame. Grass, leaves and water should
// be gray like Minecraft's, since the game tints them by biome.
//
// The folder is ignored by git: textures you don't own the rights to stay
// on your machine.
// ============================================================================
#include "atlas.h"
#include "../stb_image.h"
#include <string>
#include <vector>
#include <cstdio>

namespace pack {

enum Kind { NORMAL, WATER, CRACK };
struct Entry { int tile; const char* file; Kind kind; };

inline const std::vector<Entry>& Entries() {
    using namespace game;
    static const std::vector<Entry> e = {
        {T_GRASS_TOP, "block/grass_block_top", NORMAL},       {T_GRASS_SIDE, "block/grass_block_side", NORMAL},
        {T_GRASS_OVERLAY, "block/grass_block_side_overlay", NORMAL}, {T_GRASS_SNOW_SIDE, "block/grass_block_snow", NORMAL},
        {T_DIRT, "block/dirt", NORMAL},                       {T_STONE, "block/stone", NORMAL},
        {T_COBBLE, "block/cobblestone", NORMAL},              {T_LOG_SIDE, "block/oak_log", NORMAL},
        {T_LOG_TOP, "block/oak_log_top", NORMAL},             {T_LEAVES, "block/oak_leaves", NORMAL},
        {T_PLANKS, "block/oak_planks", NORMAL},               {T_SAND, "block/sand", NORMAL},
        {T_GLASS, "block/glass", NORMAL},                     {T_BRICKS, "block/bricks", NORMAL},
        {T_COAL, "block/coal_ore", NORMAL},                   {T_IRON, "block/iron_ore", NORMAL},
        {T_BEDROCK, "block/bedrock", NORMAL},                 {T_WATER, "block/water_still", WATER},
        {T_TABLE_TOP, "block/crafting_table_top", NORMAL},    {T_TABLE_SIDE, "block/crafting_table_front", NORMAL},
        {T_TORCH, "block/torch", NORMAL},                     {T_DIAMOND_ORE, "block/diamond_ore", NORMAL},
        {T_EMERALD_ORE, "block/emerald_ore", NORMAL},         {T_FURNACE_FRONT, "block/furnace_front", NORMAL},
        {T_FURNACE_FRONT_LIT, "block/furnace_front_on", NORMAL}, {T_FURNACE_SIDE, "block/furnace_side", NORMAL},
        {T_FURNACE_TOP, "block/furnace_top", NORMAL},         {T_SPRUCE_LOG, "block/spruce_log", NORMAL},
        {T_SPRUCE_LOG_TOP, "block/spruce_log_top", NORMAL},   {T_SPRUCE_LEAVES, "block/spruce_leaves", NORMAL},
        {T_SPRUCE_PLANKS, "block/spruce_planks", NORMAL},     {T_BIRCH_LOG, "block/birch_log", NORMAL},
        {T_BIRCH_LOG_TOP, "block/birch_log_top", NORMAL},     {T_BIRCH_LEAVES, "block/birch_leaves", NORMAL},
        {T_BIRCH_PLANKS, "block/birch_planks", NORMAL},       {T_CACTUS_SIDE, "block/cactus_side", NORMAL},
        {T_CACTUS_TOP, "block/cactus_top", NORMAL},           {T_CACTUS_BOTTOM, "block/cactus_bottom", NORMAL},
        {T_SNOW, "block/snow", NORMAL},                       {T_ICE, "block/ice", NORMAL},
        {T_APPLE, "item/apple", NORMAL},                      {T_STICK, "item/stick", NORMAL},
        {T_COAL_ITEM, "item/coal", NORMAL},                   {T_DIAMOND, "item/diamond", NORMAL},
        {T_EMERALD, "item/emerald", NORMAL},                  {T_IRON_INGOT, "item/iron_ingot", NORMAL},
        {T_WOOD_PICKAXE, "item/wooden_pickaxe", NORMAL},      {T_WOOD_AXE, "item/wooden_axe", NORMAL},
        {T_WOOD_SHOVEL, "item/wooden_shovel", NORMAL},        {T_WOOD_SWORD, "item/wooden_sword", NORMAL},
        {T_STONE_PICKAXE, "item/stone_pickaxe", NORMAL},      {T_STONE_AXE, "item/stone_axe", NORMAL},
        {T_STONE_SHOVEL, "item/stone_shovel", NORMAL},        {T_STONE_SWORD, "item/stone_sword", NORMAL},
        {T_IRON_PICKAXE, "item/iron_pickaxe", NORMAL},        {T_IRON_AXE, "item/iron_axe", NORMAL},
        {T_IRON_SHOVEL, "item/iron_shovel", NORMAL},          {T_IRON_SWORD, "item/iron_sword", NORMAL},
        {T_CRACK0 + 0, "block/destroy_stage_0", CRACK},       {T_CRACK0 + 1, "block/destroy_stage_1", CRACK},
        {T_CRACK0 + 2, "block/destroy_stage_2", CRACK},       {T_CRACK0 + 3, "block/destroy_stage_3", CRACK},
        {T_CRACK0 + 4, "block/destroy_stage_4", CRACK},       {T_CRACK0 + 5, "block/destroy_stage_5", CRACK},
        {T_CRACK0 + 6, "block/destroy_stage_6", CRACK},       {T_CRACK0 + 7, "block/destroy_stage_7", CRACK},
        {T_CRACK0 + 8, "block/destroy_stage_8", CRACK},       {T_CRACK0 + 9, "block/destroy_stage_9", CRACK},
    };
    return e;
}

// Copies one PNG into its atlas tile. Returns false if the file isn't there.
inline bool LoadTile(atlas::Image& im, const std::string& path, const Entry& e) {
    int w, h, n;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!px) return false;
    const int frame = std::min(w, h);   // animated textures are a vertical strip of square frames
    for (int y = 0; y < atlas::TILE; ++y)
        for (int x = 0; x < atlas::TILE; ++x) {
            int sx = (x * frame + frame / 2) / atlas::TILE, sy = (y * frame + frame / 2) / atlas::TILE;
            const unsigned char* p = px + ((size_t)sy * w + sx) * 4;
            atlas::Col c = {(double)p[0], (double)p[1], (double)p[2]};
            double a = p[3];
            if (e.kind == CRACK) {          // our crack overlay multiplies: 128 gray = no change
                if (a < 128) c = {128, 128, 128};
                a = 255;
            }
            if (e.kind == WATER) a = std::min(a, 180.0);
            atlas::Put(im, e.tile, x, y, c, a);
        }
    stbi_image_free(px);
    return true;
}

// Looks for each texture under a few layouts of `dir`. Returns how many loaded.
inline int Load(atlas::Image& im, const std::string& dir) {
    const char* layouts[] = {"/", "/textures/", "/assets/minecraft/textures/"};
    int loaded = 0;
    for (const Entry& e : Entries())
        for (const char* l : layouts)
            if (LoadTile(im, dir + l + e.file + ".png", e)) { ++loaded; break; }
    return loaded;
}

} // namespace pack
