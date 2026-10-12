#pragma once
// ============================================================================
// game.h : pure game logic (no OpenGL), so it can be tested on its own.
//
// Everything here runs at 20 ticks per second using the same per-tick
// formulas as Minecraft Java Edition. Speeds are NOT hardcoded; they fall
// out of the acceleration/friction math exactly like in the real game:
//
//   walk   0.1  * 0.98 / (1 - 0.6*0.91) * 20 = 4.317 m/s
//   sprint 0.13 * 0.98 / (1 - 0.546)     * 20 = 5.612 m/s
//   sneak  walk * 0.3                          = 1.295 m/s
//   jump   v0 = 0.42, then v = (v - 0.08) * 0.98 per tick -> 1.2522 blocks
//
// Units: 1 block = 1 meter, time in ticks (1 tick = 50 ms).
// ============================================================================
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstring>

namespace game {

// ---------------------------------------------------------------------------
// Constants (Minecraft Java Edition)
// ---------------------------------------------------------------------------
constexpr int    TPS                  = 20;
constexpr double GRAVITY              = 0.08;   // blocks/tick^2
constexpr double VERTICAL_DRAG        = 0.98;
constexpr double AIR_FRICTION         = 0.91;
constexpr double BLOCK_SLIPPERINESS   = 0.6;    // every normal block (ice is 0.98)
constexpr double JUMP_VELOCITY        = 0.42;
constexpr double SPRINT_JUMP_BOOST    = 0.2;
constexpr int    JUMP_DELAY_TICKS     = 10;     // holding space: 0.5 s between jumps
constexpr double MOVE_SPEED           = 0.1;    // movement_speed attribute
constexpr double SPRINT_MULT          = 1.3;    // +30%
constexpr double SNEAK_MULT           = 0.3;
constexpr double INPUT_MULT           = 0.98;
constexpr double AIR_ACCEL            = 0.02;
constexpr double AIR_ACCEL_SPRINT     = 0.026;
constexpr double FLY_SPEED            = 0.05;   // creative flying_speed (x2 sprinting)
constexpr double FLY_VERTICAL_DRAG    = 0.6;
constexpr double WATER_ACCEL          = 0.02;
constexpr double WATER_DRAG           = 0.8;
constexpr double WATER_GRAVITY        = 0.02;
constexpr double WATER_SWIM_UP        = 0.04;
constexpr double PLAYER_WIDTH         = 0.6;
constexpr double PLAYER_HEIGHT        = 1.8;
constexpr double SNEAK_HEIGHT         = 1.5;
constexpr double EYE_HEIGHT           = 1.62;
constexpr double SNEAK_EYE_HEIGHT     = 1.27;   // 0.35 lower
constexpr double STEP_HEIGHT          = 0.6;    // used by sneak edge protection
constexpr double REACH_SURVIVAL       = 4.5;
constexpr double REACH_CREATIVE       = 5.0;
constexpr int    BREAK_DELAY_SURVIVAL = 6;      // ticks after a non-instant break
constexpr int    BREAK_DELAY_CREATIVE = 5;      // repeat rate while holding the button
constexpr int    PLACE_DELAY          = 4;      // repeat rate while holding the button
constexpr int    DOUBLE_TAP_TICKS     = 7;      // double tap W to sprint, space to fly
constexpr double USE_ITEM_SLOWDOWN    = 0.2;    // moving while eating

// Survival (Minecraft Java Edition, Normal difficulty)
constexpr double MAX_HEALTH           = 20.0;   // 10 hearts
constexpr int    MAX_FOOD             = 20;     // 10 shanks
constexpr double START_SATURATION     = 5.0;
constexpr int    MAX_AIR              = 300;    // 15 s of breath
constexpr int    SPRINT_MIN_FOOD      = 7;      // sprinting needs more than 3 shanks
constexpr double SAFE_FALL            = 3.0;    // damage = ceil(blocks fallen - 3)
constexpr double MAX_EXHAUSTION       = 40.0;
constexpr double EXHAUSTION_SPRINT    = 0.1;    // per meter sprinted on the ground
constexpr double EXHAUSTION_SWIM      = 0.01;   // per meter in water
constexpr double EXHAUSTION_JUMP      = 0.05;
constexpr double EXHAUSTION_SPRINT_JUMP = 0.2;
constexpr double EXHAUSTION_BREAK     = 0.005;

// ---------------------------------------------------------------------------
// World dimensions
// ---------------------------------------------------------------------------
constexpr int CHUNK     = 16;
constexpr int CHUNKS_X  = 16;
constexpr int CHUNKS_Z  = 16;
constexpr int WX        = CHUNK * CHUNKS_X;  // 256
constexpr int WZ        = CHUNK * CHUNKS_Z;  // 256
constexpr int WY        = 128;
constexpr int SEA_LEVEL = 62;                // water fills every y below this

// ---------------------------------------------------------------------------
// Blocks
// ---------------------------------------------------------------------------
enum Block : uint8_t {
    AIR, GRASS, DIRT, STONE, COBBLESTONE, LOG, LEAVES, PLANKS, SAND,
    GLASS, BRICKS, COAL_ORE, IRON_ORE, BEDROCK, WATER, CRATE, CRAFTING_TABLE,
    TORCH,                                                    // standing on the floor
    WALL_TORCH_PX, WALL_TORCH_NX, WALL_TORCH_PZ, WALL_TORCH_NZ, // on a wall, leaning toward +X/-X/+Z/-Z
    DIAMOND_ORE, EMERALD_ORE,
    FURNACE_PX, FURNACE_NX, FURNACE_PZ, FURNACE_NZ,                 // front facing +X/-X/+Z/-Z
    LIT_FURNACE_PX, LIT_FURNACE_NX, LIT_FURNACE_PZ, LIT_FURNACE_NZ, // same, while burning
    SPRUCE_LOG, SPRUCE_LEAVES, SPRUCE_PLANKS, BIRCH_LOG, BIRCH_LEAVES, BIRCH_PLANKS,
    CACTUS, SNOW, SNOW_BLOCK, ICE,                                   // SNOW is the thin layer
    BLOCK_COUNT
};

// Texture atlas tile indices (16x16 grid of 16px tiles)
enum Tile {
    T_GRASS_TOP, T_GRASS_SIDE, T_DIRT, T_STONE, T_COBBLE, T_LOG_SIDE, T_LOG_TOP,
    T_LEAVES, T_PLANKS, T_SAND, T_GLASS, T_BRICKS, T_COAL, T_IRON, T_BEDROCK,
    T_WATER, T_CRATE, T_APPLE, T_TABLE_TOP, T_TABLE_SIDE, T_STICK, T_COAL_ITEM,
    T_WOOD_PICKAXE, T_WOOD_AXE, T_WOOD_SHOVEL, T_WOOD_SWORD,
    T_STONE_PICKAXE, T_STONE_AXE, T_STONE_SHOVEL, T_STONE_SWORD, T_TORCH,
    T_DIAMOND_ORE,
    T_CRACK0 = 32,  // 10 crack stages: 32..41
    T_EMERALD_ORE = 42, T_DIAMOND, T_EMERALD,
    T_IRON_INGOT, T_IRON_PICKAXE, T_IRON_AXE, T_IRON_SHOVEL, T_IRON_SWORD,
    T_FURNACE_FRONT, T_FURNACE_FRONT_LIT, T_FURNACE_SIDE, T_FURNACE_TOP,
    T_GRASS_OVERLAY,     // the grassy fringe on grass block sides, tinted by biome
    T_GRASS_SNOW_SIDE, T_SPRUCE_LOG, T_SPRUCE_LOG_TOP, T_SPRUCE_LEAVES, T_SPRUCE_PLANKS,
    T_BIRCH_LOG, T_BIRCH_LOG_TOP, T_BIRCH_LEAVES, T_BIRCH_PLANKS,
    T_CACTUS_SIDE, T_CACTUS_TOP, T_CACTUS_BOTTOM, T_SNOW, T_ICE,
    TILE_COUNT
};

// Which tool mines a block faster (and, for pickaxe blocks, at all)
enum ToolType : uint8_t { NO_TOOL, PICKAXE, AXE, SHOVEL, SWORD };

struct BlockInfo {
    const char* name;
    const char* key;     // id used in data files (recipes.json)
    double hardness;     // < 0 means unbreakable
    bool   needsTool;    // only drops when mined with the right tool
    bool   solid;        // collides with the player
    bool   opaque;       // hides neighbor faces, casts AO
    bool   translucent;  // drawn in the blended pass (water)
    int    tileTop, tileSide, tileBottom;
    ToolType tool;       // effective tool
    int    tier;         // minimum tool tier to harvest (0 wood, 1 stone, 2 iron)
};

inline const BlockInfo& Info(uint8_t b) {
    static const BlockInfo table[BLOCK_COUNT] = {
        // name            key               hard  needs  solid  opaque transl  top          side          bottom     tool     tier
        {"Air",            "air",            0.0,  false, false, false, false, 0,           0,            0,         NO_TOOL, 0},
        {"Grass Block",    "grass_block",    0.6,  false, true,  true,  false, T_GRASS_TOP, T_GRASS_SIDE, T_DIRT,    SHOVEL,  0},
        {"Dirt",           "dirt",           0.5,  false, true,  true,  false, T_DIRT,      T_DIRT,       T_DIRT,    SHOVEL,  0},
        {"Stone",          "stone",          1.5,  true,  true,  true,  false, T_STONE,     T_STONE,      T_STONE,   PICKAXE, 0},
        {"Cobblestone",    "cobblestone",    2.0,  true,  true,  true,  false, T_COBBLE,    T_COBBLE,     T_COBBLE,  PICKAXE, 0},
        {"Oak Log",        "oak_log",        2.0,  false, true,  true,  false, T_LOG_TOP,   T_LOG_SIDE,   T_LOG_TOP, AXE,     0},
        {"Oak Leaves",     "oak_leaves",     0.2,  false, true,  false, false, T_LEAVES,    T_LEAVES,     T_LEAVES,  NO_TOOL, 0},
        {"Oak Planks",     "oak_planks",     2.0,  false, true,  true,  false, T_PLANKS,    T_PLANKS,     T_PLANKS,  AXE,     0},
        {"Sand",           "sand",           0.5,  false, true,  true,  false, T_SAND,      T_SAND,       T_SAND,    SHOVEL,  0},
        {"Glass",          "glass",          0.3,  false, true,  false, false, T_GLASS,     T_GLASS,      T_GLASS,   NO_TOOL, 0},
        {"Bricks",         "bricks",         2.0,  true,  true,  true,  false, T_BRICKS,    T_BRICKS,     T_BRICKS,  PICKAXE, 0},
        {"Coal Ore",       "coal_ore",       3.0,  true,  true,  true,  false, T_COAL,      T_COAL,       T_COAL,    PICKAXE, 0},
        {"Iron Ore",       "iron_ore",       3.0,  true,  true,  true,  false, T_IRON,      T_IRON,       T_IRON,    PICKAXE, 1},
        {"Bedrock",        "bedrock",       -1.0,  false, true,  true,  false, T_BEDROCK,   T_BEDROCK,    T_BEDROCK, NO_TOOL, 0},
        {"Water",          "water",         -1.0,  false, false, false, true,  T_WATER,     T_WATER,      T_WATER,   NO_TOOL, 0},
        {"Crate",          "crate",          2.5,  false, true,  true,  false, T_CRATE,     T_CRATE,      T_CRATE,   AXE,     0},
        {"Crafting Table", "crafting_table", 2.5,  false, true,  true,  false, T_TABLE_TOP, T_TABLE_SIDE, T_PLANKS,  AXE,     0},
        {"Torch",          "torch",          0.0,  false, false, false, false, T_TORCH,     T_TORCH,      T_TORCH,   NO_TOOL, 0},
        {"Torch",          "wall_torch_px",  0.0,  false, false, false, false, T_TORCH,     T_TORCH,      T_TORCH,   NO_TOOL, 0},
        {"Torch",          "wall_torch_nx",  0.0,  false, false, false, false, T_TORCH,     T_TORCH,      T_TORCH,   NO_TOOL, 0},
        {"Torch",          "wall_torch_pz",  0.0,  false, false, false, false, T_TORCH,     T_TORCH,      T_TORCH,   NO_TOOL, 0},
        {"Torch",          "wall_torch_nz",  0.0,  false, false, false, false, T_TORCH,     T_TORCH,      T_TORCH,   NO_TOOL, 0},
        {"Diamond Ore",    "diamond_ore",    3.0,  true,  true,  true,  false, T_DIAMOND_ORE, T_DIAMOND_ORE, T_DIAMOND_ORE, PICKAXE, 2},
        {"Emerald Ore",    "emerald_ore",    3.0,  true,  true,  true,  false, T_EMERALD_ORE, T_EMERALD_ORE, T_EMERALD_ORE, PICKAXE, 2},
        {"Furnace",        "furnace",        3.5,  true,  true,  true,  false, T_FURNACE_TOP, T_FURNACE_SIDE, T_FURNACE_TOP, PICKAXE, 0},
        {"Furnace",        "furnace_nx",     3.5,  true,  true,  true,  false, T_FURNACE_TOP, T_FURNACE_SIDE, T_FURNACE_TOP, PICKAXE, 0},
        {"Furnace",        "furnace_pz",     3.5,  true,  true,  true,  false, T_FURNACE_TOP, T_FURNACE_SIDE, T_FURNACE_TOP, PICKAXE, 0},
        {"Furnace",        "furnace_nz",     3.5,  true,  true,  true,  false, T_FURNACE_TOP, T_FURNACE_SIDE, T_FURNACE_TOP, PICKAXE, 0},
        {"Furnace",        "lit_furnace_px", 3.5,  true,  true,  true,  false, T_FURNACE_TOP, T_FURNACE_SIDE, T_FURNACE_TOP, PICKAXE, 0},
        {"Furnace",        "lit_furnace_nx", 3.5,  true,  true,  true,  false, T_FURNACE_TOP, T_FURNACE_SIDE, T_FURNACE_TOP, PICKAXE, 0},
        {"Furnace",        "lit_furnace_pz", 3.5,  true,  true,  true,  false, T_FURNACE_TOP, T_FURNACE_SIDE, T_FURNACE_TOP, PICKAXE, 0},
        {"Furnace",        "lit_furnace_nz", 3.5,  true,  true,  true,  false, T_FURNACE_TOP, T_FURNACE_SIDE, T_FURNACE_TOP, PICKAXE, 0},
        {"Spruce Log",     "spruce_log",     2.0,  false, true,  true,  false, T_SPRUCE_LOG_TOP, T_SPRUCE_LOG, T_SPRUCE_LOG_TOP, AXE, 0},
        {"Spruce Leaves",  "spruce_leaves",  0.2,  false, true,  false, false, T_SPRUCE_LEAVES, T_SPRUCE_LEAVES, T_SPRUCE_LEAVES, NO_TOOL, 0},
        {"Spruce Planks",  "spruce_planks",  2.0,  false, true,  true,  false, T_SPRUCE_PLANKS, T_SPRUCE_PLANKS, T_SPRUCE_PLANKS, AXE, 0},
        {"Birch Log",      "birch_log",      2.0,  false, true,  true,  false, T_BIRCH_LOG_TOP, T_BIRCH_LOG, T_BIRCH_LOG_TOP, AXE, 0},
        {"Birch Leaves",   "birch_leaves",   0.2,  false, true,  false, false, T_BIRCH_LEAVES, T_BIRCH_LEAVES, T_BIRCH_LEAVES, NO_TOOL, 0},
        {"Birch Planks",   "birch_planks",   2.0,  false, true,  true,  false, T_BIRCH_PLANKS, T_BIRCH_PLANKS, T_BIRCH_PLANKS, AXE, 0},
        {"Cactus",         "cactus",         0.4,  false, true,  false, false, T_CACTUS_TOP, T_CACTUS_SIDE, T_CACTUS_BOTTOM, NO_TOOL, 0},
        {"Snow",           "snow",           0.1,  true,  false, false, false, T_SNOW,      T_SNOW,       T_SNOW,    SHOVEL,  0},
        {"Snow Block",     "snow_block",     0.2,  true,  true,  true,  false, T_SNOW,      T_SNOW,       T_SNOW,    SHOVEL,  0},
        {"Ice",            "ice",            0.5,  false, true,  false, true,  T_ICE,       T_ICE,        T_ICE,     PICKAXE, 0},
    };
    return table[b < BLOCK_COUNT ? b : 0];
}

inline bool IsLeaves(uint8_t b) { return b == LEAVES || b == SPRUCE_LEAVES || b == BIRCH_LEAVES; }
inline bool IsLog(uint8_t b)    { return b == LOG || b == SPRUCE_LOG || b == BIRCH_LOG; }

// Blocks that stop skylight from falling straight down (used for spawning)
inline bool BlocksSky(uint8_t b) { return Info(b).opaque || IsLeaves(b); }

// Ice is slippery; everything else has Minecraft's default friction
inline double Slipperiness(uint8_t b) { return b == ICE ? 0.98 : 0.6; }

// ---------------------------------------------------------------------------
// Torches
// ---------------------------------------------------------------------------
inline bool IsTorch(uint8_t b) { return b >= TORCH && b <= WALL_TORCH_NZ; }

// The block a torch hangs on
inline void TorchSupport(uint8_t b, int x, int y, int z, int& sx, int& sy, int& sz) {
    sx = x; sy = y; sz = z;
    switch (b) {
        case WALL_TORCH_PX: sx = x - 1; break;   // leans toward +X, so the wall is at -X
        case WALL_TORCH_NX: sx = x + 1; break;
        case WALL_TORCH_PZ: sz = z - 1; break;
        case WALL_TORCH_NZ: sz = z + 1; break;
        default:            sy = y - 1; break;   // standing torch
    }
}

// Which torch block goes on the face (nx, ny, nz) of a clicked block; AIR
// if none fits (torches can't hang from ceilings)
inline uint8_t TorchForFace(int nx, int ny, int nz) {
    if (ny > 0)  return TORCH;
    if (nx > 0)  return WALL_TORCH_PX;
    if (nx < 0)  return WALL_TORCH_NX;
    if (nz > 0)  return WALL_TORCH_PZ;
    if (nz < 0)  return WALL_TORCH_NZ;
    return AIR;
}

// Can a torch hang on this block?
inline bool CanHoldTorch(uint8_t b) { return Info(b).solid && !IsLeaves(b) && !IsTorch(b) && b != CACTUS && b != ICE; }

// ---------------------------------------------------------------------------
// Furnaces: 4 facings x lit/unlit. FURNACE_PX doubles as the furnace item.
// Facing index: 0 = +X, 1 = -X, 2 = +Z, 3 = -Z
// ---------------------------------------------------------------------------
constexpr uint8_t FURNACE = FURNACE_PX;
inline bool    IsFurnace(uint8_t b)    { return b >= FURNACE_PX && b <= LIT_FURNACE_NZ; }
inline bool    IsLitFurnace(uint8_t b) { return b >= LIT_FURNACE_PX && b <= LIT_FURNACE_NZ; }
inline int     FurnaceFacing(uint8_t b) { return (b - FURNACE_PX) % 4; }
inline uint8_t FurnaceBlock(int facing, bool lit) { return (uint8_t)((lit ? LIT_FURNACE_PX : FURNACE_PX) + facing); }

// A placed furnace turns its front toward the player (who looks along dx, dz)
inline int FacingToward(double dx, double dz) {
    if (std::fabs(dx) > std::fabs(dz)) return dx > 0 ? 1 : 0;   // looking +X: front faces -X
    return dz > 0 ? 3 : 2;
}

// ---------------------------------------------------------------------------
// Items. Ids below BLOCK_COUNT are the blocks themselves; ids from
// ITEM_FIRST up are things you can hold but not place.
// ---------------------------------------------------------------------------
enum ItemId : uint8_t {
    APPLE = 64, STICK, COAL,
    WOODEN_PICKAXE, WOODEN_AXE, WOODEN_SHOVEL, WOODEN_SWORD,
    STONE_PICKAXE, STONE_AXE, STONE_SHOVEL, STONE_SWORD,
    DIAMOND, EMERALD,
    IRON_INGOT, IRON_PICKAXE, IRON_AXE, IRON_SHOVEL, IRON_SWORD,
    ITEM_END
};
constexpr int ITEM_FIRST = APPLE;
constexpr int STACK_MAX  = 64;

struct ItemInfo {
    const char* name;
    const char* key;
    int      tile;
    int      maxStack;
    ToolType tool;
    int      tier;         // 0 wood, 1 stone, 2 iron
    int      durability;   // uses before it breaks, 0 = not a tool
    double   speed;        // mining speed on blocks it's made for
};

inline const ItemInfo* ItemData(uint8_t id) {
    static const ItemInfo table[ITEM_END - ITEM_FIRST] = {
        // name             key               tile             stack tool     tier uses speed
        {"Apple",           "apple",          T_APPLE,         64,   NO_TOOL, 0,   0,   1.0},
        {"Stick",           "stick",          T_STICK,         64,   NO_TOOL, 0,   0,   1.0},
        {"Coal",            "coal",           T_COAL_ITEM,     64,   NO_TOOL, 0,   0,   1.0},
        {"Wooden Pickaxe",  "wooden_pickaxe", T_WOOD_PICKAXE,  1,    PICKAXE, 0,   59,  2.0},
        {"Wooden Axe",      "wooden_axe",     T_WOOD_AXE,      1,    AXE,     0,   59,  2.0},
        {"Wooden Shovel",   "wooden_shovel",  T_WOOD_SHOVEL,   1,    SHOVEL,  0,   59,  2.0},
        {"Wooden Sword",    "wooden_sword",   T_WOOD_SWORD,    1,    SWORD,   0,   59,  1.0},
        {"Stone Pickaxe",   "stone_pickaxe",  T_STONE_PICKAXE, 1,    PICKAXE, 1,   131, 4.0},
        {"Stone Axe",       "stone_axe",      T_STONE_AXE,     1,    AXE,     1,   131, 4.0},
        {"Stone Shovel",    "stone_shovel",   T_STONE_SHOVEL,  1,    SHOVEL,  1,   131, 4.0},
        {"Stone Sword",     "stone_sword",    T_STONE_SWORD,   1,    SWORD,   1,   131, 1.0},
        {"Diamond",         "diamond",        T_DIAMOND,       64,   NO_TOOL, 0,   0,   1.0},
        {"Emerald",         "emerald",        T_EMERALD,       64,   NO_TOOL, 0,   0,   1.0},
        {"Iron Ingot",      "iron_ingot",     T_IRON_INGOT,    64,   NO_TOOL, 0,   0,   1.0},
        {"Iron Pickaxe",    "iron_pickaxe",   T_IRON_PICKAXE,  1,    PICKAXE, 2,   250, 6.0},
        {"Iron Axe",        "iron_axe",       T_IRON_AXE,      1,    AXE,     2,   250, 6.0},
        {"Iron Shovel",     "iron_shovel",    T_IRON_SHOVEL,   1,    SHOVEL,  2,   250, 6.0},
        {"Iron Sword",      "iron_sword",     T_IRON_SWORD,    1,    SWORD,   2,   250, 1.0},
    };
    return id >= ITEM_FIRST && id < ITEM_END ? &table[id - ITEM_FIRST] : nullptr;
}

inline bool IsPlaceable(uint8_t item) { return item != AIR && item < BLOCK_COUNT; }
inline const char* ItemName(uint8_t item) {
    if (item < BLOCK_COUNT) return Info(item).name;
    const ItemInfo* d = ItemData(item);
    return d ? d->name : "Unknown";
}
inline const char* ItemKey(uint8_t item) {
    if (item < BLOCK_COUNT) return Info(item).key;
    const ItemInfo* d = ItemData(item);
    return d ? d->key : "unknown";
}
inline int ItemTile(uint8_t item) {
    if (item < BLOCK_COUNT) return Info(item).tileSide;
    const ItemInfo* d = ItemData(item);
    return d ? d->tile : 0;
}
inline int MaxStack(uint8_t item)      { const ItemInfo* d = ItemData(item); return d ? d->maxStack : STACK_MAX; }
inline int MaxDurability(uint8_t item) { const ItemInfo* d = ItemData(item); return d ? d->durability : 0; }

// How long an item burns as furnace fuel, in ticks (0 = not fuel).
// Minecraft's values: coal smelts 8 items, wood 1.5, a stick half of one.
inline int FuelTicks(uint8_t item) {
    switch (item) {
        case COAL:                                            return 1600;
        case LOG: case PLANKS: case CRAFTING_TABLE: case CRATE:
        case SPRUCE_LOG: case SPRUCE_PLANKS: case BIRCH_LOG: case BIRCH_PLANKS: return 300;
        case WOODEN_PICKAXE: case WOODEN_AXE: case WOODEN_SHOVEL: case WOODEN_SWORD: return 200;
        case STICK:                                           return 100;
        default:                                              return 0;
    }
}

// Looks up "oak_planks" style ids. Returns false for unknown names.
inline bool ItemByKey(const char* key, uint8_t& out) {
    for (int i = 1; i < BLOCK_COUNT; ++i)
        if (std::strcmp(Info((uint8_t)i).key, key) == 0) { out = (uint8_t)i; return true; }
    for (int i = ITEM_FIRST; i < ITEM_END; ++i)
        if (std::strcmp(ItemData((uint8_t)i)->key, key) == 0) { out = (uint8_t)i; return true; }
    return false;
}

// ---------------------------------------------------------------------------
// Breaking (Minecraft Wiki "Breaking" formula)
//   speed = the tool's speed if it's the right tool type, else 1 (bare hand)
//   damage/tick = speed / hardness / (canHarvest ? 30 : 100)
//   speed /= 5 when airborne, /= 5 again when the eyes are underwater
//   ticks = ceil(1 / damage); damage >= 1 breaks instantly
// ---------------------------------------------------------------------------
inline double ToolSpeed(uint8_t block, uint8_t tool) {
    const ItemInfo* t = ItemData(tool);
    if (!t || t->tool == NO_TOOL) return 1.0;
    if (t->tool == SWORD) return IsLeaves(block) || block == CACTUS ? 1.5 : 1.0;
    return t->tool == Info(block).tool ? t->speed : 1.0;
}

// Pickaxe blocks only drop (and only get the /30 divisor) when mined
// with a pickaxe of a high enough tier
inline bool CanHarvest(uint8_t block, uint8_t tool = AIR) {
    const BlockInfo& bi = Info(block);
    if (!bi.needsTool) return true;
    const ItemInfo* t = ItemData(tool);
    return t && t->tool == bi.tool && t->tier >= bi.tier;
}

inline double BreakDamagePerTick(uint8_t b, bool onGround, bool eyesInWater, uint8_t tool = AIR) {
    const BlockInfo& bi = Info(b);
    if (bi.hardness < 0.0) return 0.0;      // unbreakable
    if (bi.hardness == 0.0) return 1.0;     // instant
    double speed = ToolSpeed(b, tool);
    if (eyesInWater) speed /= 5.0;
    if (!onGround)   speed /= 5.0;
    return speed / bi.hardness / (CanHarvest(b, tool) ? 30.0 : 100.0);
}

inline int BreakTicks(uint8_t b, uint8_t tool = AIR) {
    double d = BreakDamagePerTick(b, true, false, tool);
    if (d <= 0.0) return -1;
    if (d >= 1.0) return 0;
    return (int)std::ceil(1.0 / d - 1e-9);
}

// ---------------------------------------------------------------------------
// Hashing and noise
// ---------------------------------------------------------------------------
inline uint32_t Hash(int x, int y, int z, uint32_t seed) {
    uint32_t h = seed;
    h ^= (uint32_t)x * 0x8da6b343u;
    h ^= (uint32_t)y * 0xd8163841u;
    h ^= (uint32_t)z * 0xcb1ab31fu;
    h ^= h >> 13; h *= 0x5bd1e995u;
    h ^= h >> 15; h *= 0x27d4eb2du;
    h ^= h >> 16;
    return h;
}
inline double Hash01(int x, int y, int z, uint32_t seed) {
    return (Hash(x, y, z, seed) & 0xFFFFFFu) / 16777216.0;
}
// Gameplay randomness (drop chances, item scatter): not tied to the seed
inline double Rand01() {
    static uint32_t s = 0x9E3779B9u;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return (s & 0xFFFFFFu) / 16777216.0;
}

// Classic 2D Perlin gradient noise. Same seed = same world (that's what a
// Minecraft seed is).
struct Perlin {
    uint8_t p[512];
    explicit Perlin(uint32_t seed) {
        uint8_t base[256];
        for (int i = 0; i < 256; ++i) base[i] = (uint8_t)i;
        uint32_t s = seed ? seed : 1u;
        for (int i = 255; i > 0; --i) {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;   // xorshift
            int j = (int)(s % (uint32_t)(i + 1));
            std::swap(base[i], base[j]);
        }
        for (int i = 0; i < 512; ++i) p[i] = base[i & 255];
    }
    static double Fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
    static double Lerp(double a, double b, double t) { return a + (b - a) * t; }
    static double Grad(int h, double x, double y) {
        switch (h & 7) {
            case 0: return  x + y;  case 1: return -x + y;
            case 2: return  x - y;  case 3: return -x - y;
            case 4: return  x;      case 5: return -x;
            case 6: return  y;      default: return -y;
        }
    }
    double Noise(double x, double y) const {
        double fx = std::floor(x), fy = std::floor(y);
        int X = (int)fx & 255, Y = (int)fy & 255;
        x -= fx; y -= fy;
        double u = Fade(x), v = Fade(y);
        int aa = p[p[X] + Y], ab = p[p[X] + Y + 1];
        int ba = p[p[X + 1] + Y], bb = p[p[X + 1] + Y + 1];
        return Lerp(Lerp(Grad(aa, x, y),     Grad(ba, x - 1, y),     u),
                    Lerp(Grad(ab, x, y - 1), Grad(bb, x - 1, y - 1), u), v);
    }
    // Fractal noise: several octaves stacked, each half the size and strength
    double Fbm(double x, double y, int octaves) const {
        double sum = 0, amp = 1, freq = 1, norm = 0;
        for (int i = 0; i < octaves; ++i) {
            sum  += amp * Noise(x * freq, y * freq);
            norm += amp;
            amp  *= 0.5;
            freq *= 2.0;
        }
        return sum / norm;
    }
};

// ---------------------------------------------------------------------------
// Biomes, with Minecraft's grass, foliage and water colors (0xRRGGBB)
// ---------------------------------------------------------------------------
enum Biome : uint8_t {
    OCEAN, FROZEN_OCEAN, BEACH, PLAINS, FOREST, BIRCH_FOREST, TAIGA, SNOWY_PLAINS,
    DESERT, SWAMP, MOUNTAINS,
    BIOME_COUNT
};

struct BiomeInfo {
    const char* name;
    uint32_t grass, foliage, water;
    bool snowy;   // snow lies on the ground and water freezes
};

inline const BiomeInfo& BiomeData(uint8_t b) {
    static const BiomeInfo table[BIOME_COUNT] = {
        {"Ocean",         0x8EB971, 0x71A74D, 0x3F76E4, false},
        {"Frozen Ocean",  0x80B497, 0x60A17B, 0x3938C9, true},
        {"Beach",         0x91BD59, 0x77AB2F, 0x3F76E4, false},
        {"Plains",        0x91BD59, 0x77AB2F, 0x3F76E4, false},
        {"Forest",        0x79C05A, 0x59AE30, 0x3F76E4, false},
        {"Birch Forest",  0x88BB67, 0x6BA941, 0x3F76E4, false},
        {"Taiga",         0x86B783, 0x68A464, 0x287082, false},
        {"Snowy Plains",  0x80B497, 0x60A17B, 0x3D57D6, true},
        {"Desert",        0xBFB755, 0xAEA42A, 0x32A598, false},
        {"Swamp",         0x6A7039, 0x6A7039, 0x617B64, false},
        {"Mountains",     0x8AB689, 0x6DA36B, 0x3F76E4, false},
    };
    return table[b < BIOME_COUNT ? b : PLAINS];
}

// Spruce and birch leaves keep their own color everywhere, like in Minecraft
constexpr uint32_t SPRUCE_FOLIAGE = 0x619961;
constexpr uint32_t BIRCH_FOLIAGE  = 0x80A755;

// ---------------------------------------------------------------------------
// World storage
// ---------------------------------------------------------------------------
struct World {
    std::vector<uint8_t> blocks;   // x-major, then z, then y (columns contiguous)
    std::vector<int16_t> top;      // highest sky-blocking y per column, -1 if none
    std::vector<uint8_t> light;    // per block: sky level << 4 | block-light level (0..15 each)
    std::vector<uint8_t> biome;    // per column
    // Per column colors, blended across nearby biomes so borders fade
    std::vector<uint32_t> grassColor, foliageColor, waterColor;
    uint32_t seed = 0;

    World() : blocks((size_t)WX * WY * WZ, AIR), top((size_t)WX * WZ, -1), light((size_t)WX * WY * WZ, 0xF0),
              biome((size_t)WX * WZ, PLAINS), grassColor((size_t)WX * WZ, 0x91BD59),
              foliageColor((size_t)WX * WZ, 0x77AB2F), waterColor((size_t)WX * WZ, 0x3F76E4) {}

    static size_t Col(int x, int z) { return (size_t)x * WZ + z; }
    uint8_t BiomeAt(int x, int z) const { return InXZ(x, z) ? biome[Col(x, z)] : (uint8_t)OCEAN; }
    // Colors with the world edge clamped
    uint32_t GrassAt(int x, int z) const   { return grassColor[Col(std::clamp(x, 0, WX - 1), std::clamp(z, 0, WZ - 1))]; }
    uint32_t FoliageAt(int x, int z) const { return foliageColor[Col(std::clamp(x, 0, WX - 1), std::clamp(z, 0, WZ - 1))]; }
    uint32_t WaterAt(int x, int z) const   { return waterColor[Col(std::clamp(x, 0, WX - 1), std::clamp(z, 0, WZ - 1))]; }

    static bool InXZ(int x, int z) { return x >= 0 && x < WX && z >= 0 && z < WZ; }
    static size_t Idx(int x, int y, int z) { return ((size_t)x * WZ + z) * WY + y; }

    uint8_t Get(int x, int y, int z) const {
        if (y < 0 || y >= WY || !InXZ(x, z)) return AIR;
        return blocks[Idx(x, y, z)];
    }
    void SetRaw(int x, int y, int z, uint8_t b) {
        if (y < 0 || y >= WY || !InXZ(x, z)) return;
        blocks[Idx(x, y, z)] = b;
    }
    int TopAt(int x, int z) const { return InXZ(x, z) ? top[(size_t)x * WZ + z] : -1; }

    void RecomputeTop(int x, int z) {
        int y = WY - 1;
        while (y >= 0 && !BlocksSky(blocks[Idx(x, y, z)])) --y;
        top[(size_t)x * WZ + z] = (int16_t)y;
    }
    void Set(int x, int y, int z, uint8_t b) {
        if (y < 0 || y >= WY || !InXZ(x, z)) return;
        blocks[Idx(x, y, z)] = b;
        int16_t& t = top[(size_t)x * WZ + z];
        if (BlocksSky(b) && y > t) t = (int16_t)y;
        else if (!BlocksSky(b) && y == t) RecomputeTop(x, z);
    }
    // Collision: the world edge acts as an invisible wall; below y=0 is the void
    bool SolidAt(int x, int y, int z) const {
        if (!InXZ(x, z)) return true;
        if (y < 0 || y >= WY) return false;
        return Info(blocks[Idx(x, y, z)]).solid;
    }

    // Outside the world is open sky; below the bottom is pitch black
    int SkyLight(int x, int y, int z) const {
        if (!InXZ(x, z) || y >= WY) return 15;
        if (y < 0) return 0;
        return light[Idx(x, y, z)] >> 4;
    }
    int BlockLight(int x, int y, int z) const {
        if (y < 0 || y >= WY || !InXZ(x, z)) return 0;
        return light[Idx(x, y, z)] & 15;
    }
};

// ---------------------------------------------------------------------------
// Light, the Minecraft way: two channels of 0..15 per block.
//   sky light   15 falls straight down from the sky until something stops it,
//               then spreads sideways and down, losing 1 per block
//   block light given off by torches (14), spreading the same way
// Opaque blocks stop light. Leaves and water let it through but stop the
// full-strength fall, so the ground under trees and ponds is a bit dimmer.
// ---------------------------------------------------------------------------
inline int LightEmission(uint8_t b) { return IsTorch(b) ? 14 : (IsLitFurnace(b) ? 13 : 0); }

// How much light is lost stepping into this block (15 = none gets in)
inline int LightCost(uint8_t b) { return Info(b).opaque ? 15 : 1; }

// Full skylight only keeps falling through clear blocks
inline bool SkyFallsThrough(uint8_t b) { return b == AIR || b == GLASS || b == SNOW || IsTorch(b); }

struct LightBox {
    int x0 = 0, y0 = 0, z0 = 0, x1 = -1, y1 = -1, z1 = -1;
    bool Empty() const { return x1 < x0; }
};

// Recomputes all light inside the box [x0..x1] x [y0..y1] x [z0..z1],
// treating the light just outside it as fixed. Returns the cells whose
// light actually changed, so only those chunks need re-meshing.
inline LightBox RelightBox(World& w, int x0, int y0, int z0, int x1, int y1, int z1) {
    x0 = std::max(x0, 0); z0 = std::max(z0, 0); y0 = std::max(y0, 0);
    x1 = std::min(x1, WX - 1); z1 = std::min(z1, WZ - 1); y1 = std::min(y1, WY - 1);
    LightBox changed;
    if (x1 < x0 || y1 < y0 || z1 < z0) return changed;

    const int bx = x1 - x0 + 1, by = y1 - y0 + 1, bz = z1 - z0 + 1;
    auto inBox = [&](int x, int y, int z) { return x >= x0 && x <= x1 && y >= y0 && y <= y1 && z >= z0 && z <= z1; };

    // Remember the old values so we can report what changed
    std::vector<uint8_t> old((size_t)bx * by * bz);
    auto boxIdx = [&](int x, int y, int z) { return ((size_t)(x - x0) * bz + (z - z0)) * by + (y - y0); };
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z)
            for (int y = y0; y <= y1; ++y) {
                size_t i = World::Idx(x, y, z);
                old[boxIdx(x, y, z)] = w.light[i];
                w.light[i] = 0;
            }

    // Where full skylight stops falling in each column, with one column of
    // margin so we can tell which sky cells have a darker neighbor
    const int mz = bz + 2;
    std::vector<int> fall((size_t)(bx + 2) * mz, 0);
    auto fallAt = [&](int x, int z) -> int {
        if (!World::InXZ(x, z)) return 0;   // outside the world: open sky all the way down
        return fall[(size_t)(x - x0 + 1) * mz + (z - z0 + 1)];
    };
    for (int x = x0 - 1; x <= x1 + 1; ++x)
        for (int z = z0 - 1; z <= z1 + 1; ++z) {
            if (!World::InXZ(x, z)) continue;
            int y = WY;
            while (y > 0 && SkyFallsThrough(w.blocks[World::Idx(x, y - 1, z)])) --y;
            fall[(size_t)(x - x0 + 1) * mz + (z - z0 + 1)] = y;   // cells y..WY-1 get full sky
        }

    std::vector<uint32_t> skyQ, blockQ;
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z) {
            const int f = fallAt(x, z);
            const int nf = std::max(std::max(fallAt(x - 1, z), fallAt(x + 1, z)),
                                    std::max(fallAt(x, z - 1), fallAt(x, z + 1)));
            for (int y = std::max(f, y0); y <= y1; ++y) {
                size_t i = World::Idx(x, y, z);
                w.light[i] |= 0xF0;
                // Only sky cells next to something darker need to spread
                if (y == f || y < nf) skyQ.push_back((uint32_t)i);
            }
            for (int y = y0; y <= y1; ++y) {
                size_t i = World::Idx(x, y, z);
                if (int e = LightEmission(w.blocks[i])) {
                    w.light[i] = (uint8_t)((w.light[i] & 0xF0) | e);
                    blockQ.push_back((uint32_t)i);
                }
            }
        }

    // Light from just outside the box flows in
    auto seedEdge = [&](int x, int y, int z) {
        if (y < 0 || y >= WY || !World::InXZ(x, z)) return;
        size_t i = World::Idx(x, y, z);
        if ((w.light[i] >> 4) > 1)  skyQ.push_back((uint32_t)i);
        if ((w.light[i] & 15) > 1)  blockQ.push_back((uint32_t)i);
    };
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z) { seedEdge(x, y0 - 1, z); seedEdge(x, y1 + 1, z); }
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y) { seedEdge(x, y, z0 - 1); seedEdge(x, y, z1 + 1); }
    for (int z = z0; z <= z1; ++z)
        for (int y = y0; y <= y1; ++y) { seedEdge(x0 - 1, y, z); seedEdge(x1 + 1, y, z); }

    // Flood fill, one channel at a time (shift 4 = sky, 0 = block light)
    static const int DIRS[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    auto flood = [&](std::vector<uint32_t>& q, int shift) {
        for (size_t head = 0; head < q.size(); ++head) {
            uint32_t i = q[head];
            int x = (int)(i / ((uint32_t)WZ * WY)), z = (int)((i / WY) % WZ), y = (int)(i % WY);
            int level = (w.light[i] >> shift) & 15;
            for (const auto& d : DIRS) {
                int nx = x + d[0], ny = y + d[1], nz = z + d[2];
                if (!inBox(nx, ny, nz)) continue;
                size_t n = World::Idx(nx, ny, nz);
                int nl = level - LightCost(w.blocks[n]);
                if (nl <= ((w.light[n] >> shift) & 15)) continue;
                w.light[n] = (uint8_t)((w.light[n] & ~(15 << shift)) | (nl << shift));
                q.push_back((uint32_t)n);
            }
        }
    };
    flood(skyQ, 4);
    flood(blockQ, 0);

    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z)
            for (int y = y0; y <= y1; ++y)
                if (w.light[World::Idx(x, y, z)] != old[boxIdx(x, y, z)]) {
                    if (changed.Empty()) { changed.x0 = changed.x1 = x; changed.y0 = changed.y1 = y; changed.z0 = changed.z1 = z; continue; }
                    changed.x0 = std::min(changed.x0, x); changed.x1 = std::max(changed.x1, x);
                    changed.y0 = std::min(changed.y0, y); changed.y1 = std::max(changed.y1, y);
                    changed.z0 = std::min(changed.z0, z); changed.z1 = std::max(changed.z1, z);
                }
    return changed;
}

// After one block changes, light can only differ within 16 blocks sideways
// and above, but anywhere below (the sky may now fall further)
inline LightBox RelightAround(World& w, int x, int y, int z) {
    return RelightBox(w, x - 16, 0, z - 16, x + 16, y + 16, z + 16);
}

inline void RelightAll(World& w) { RelightBox(w, 0, 0, 0, WX - 1, WY - 1, WZ - 1); }

// Can this block stay where it is? Torches need the block they hang on,
// cactus needs sand (or more cactus) below and nothing solid beside it,
// a snow layer needs a full block or leaves underneath.
inline bool CanSurvive(const World& w, uint8_t b, int x, int y, int z) {
    if (IsTorch(b)) {
        int sx, sy, sz;
        TorchSupport(b, x, y, z, sx, sy, sz);
        return CanHoldTorch(w.Get(sx, sy, sz));
    }
    if (b == CACTUS) {
        uint8_t below = w.Get(x, y - 1, z);
        if (below != SAND && below != CACTUS) return false;
        return !w.SolidAt(x + 1, y, z) && !w.SolidAt(x - 1, y, z) && !w.SolidAt(x, y, z + 1) && !w.SolidAt(x, y, z - 1);
    }
    if (b == SNOW) {
        uint8_t below = w.Get(x, y - 1, z);
        return (Info(below).opaque || IsLeaves(below)) && below != ICE;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Terrain generation: gentle plains with ponds, oak trees, ores, bedrock
// ---------------------------------------------------------------------------
// Oak (trunk 4..6) and birch (trunk 5..7) share Minecraft's blob canopy
inline void PlaceOakTree(World& w, int x, int baseY, int z, uint32_t seed, bool birch = false) {
    const uint8_t LOGB = birch ? BIRCH_LOG : LOG, LEAF = birch ? BIRCH_LEAVES : LEAVES;
    int h = (birch ? 5 : 4) + (int)(Hash01(x, 1, z, seed + 12) * 3.0);
    if (baseY + h + 1 >= WY) return;
    // Leaves: 4 layers. Bottom two are 5x5, top two are 3x3.
    // Corners are randomly trimmed, and always trimmed on the top layer.
    int topY = baseY + h;
    for (int y = topY - 3; y <= topY; ++y) {
        int rel = y - topY;               // -3..0
        int r = 1 - rel / 2;              // 2,2,1,1
        for (int dx = -r; dx <= r; ++dx)
            for (int dz = -r; dz <= r; ++dz) {
                if (std::abs(dx) == r && std::abs(dz) == r) {
                    if (rel == 0) continue;
                    if (Hash01(x + dx, y, z + dz, seed + 13) < 0.5) continue;
                }
                if (w.Get(x + dx, y, z + dz) == AIR) w.SetRaw(x + dx, y, z + dz, LEAF);
            }
    }
    for (int i = 0; i < h; ++i) w.SetRaw(x, baseY + i, z, LOGB);
    w.SetRaw(x, baseY - 1, z, DIRT);
}

// Spruce: a tall trunk wrapped in stacked, alternating rings of leaves that
// narrow to a point, like Minecraft's spruce foliage
inline void PlaceSpruceTree(World& w, int x, int baseY, int z, uint32_t seed) {
    int h = 6 + (int)(Hash01(x, 2, z, seed + 14) * 4.0);          // trunk 6..9
    int maxR = 2 + (Hash01(x, 3, z, seed + 15) < 0.4 ? 1 : 0);
    int bare = 1 + (int)(Hash01(x, 4, z, seed + 16) * 2.0);        // bare trunk at the bottom
    if (baseY + h + 2 >= WY) return;
    int top = baseY + h + 1;
    int r = 0, ring = 1;
    for (int y = top; y >= baseY + bare; --y) {
        for (int dx = -r; dx <= r; ++dx)
            for (int dz = -r; dz <= r; ++dz) {
                if (r > 0 && std::abs(dx) == r && std::abs(dz) == r) continue;   // round off corners
                if (w.Get(x + dx, y, z + dz) == AIR) w.SetRaw(x + dx, y, z + dz, SPRUCE_LEAVES);
            }
        // Grow the ring each layer; when it reaches its limit, pull back in
        if (r >= ring) { r = (ring > 1 && Hash01(x, y, z, seed + 17) < 0.5) ? 1 : 0; ring = std::min(ring + 1, maxR); }
        else ++r;
    }
    for (int i = 0; i < h; ++i) w.SetRaw(x, baseY + i, z, SPRUCE_LOG);
    w.SetRaw(x, baseY - 1, z, DIRT);
}

// Cactus grows 1-3 tall on sand, with nothing right beside it
inline void PlaceCactus(World& w, int x, int baseY, int z, uint32_t seed) {
    int h = 1 + (int)(Hash01(x, 5, z, seed + 18) * 3.0);
    for (int y = baseY; y < baseY + h; ++y) {
        if (w.Get(x, y, z) != AIR) return;
        if (w.Get(x + 1, y, z) != AIR || w.Get(x - 1, y, z) != AIR || w.Get(x, y, z + 1) != AIR || w.Get(x, y, z - 1) != AIR) return;
        w.SetRaw(x, y, z, CACTUS);
    }
}

// Seeded random numbers for world generation, so a seed always builds the
// same world (like Minecraft's per-chunk java.util.Random)
struct GenRng {
    uint64_t s;
    explicit GenRng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) { if (!s) s = 1; Next(); }
    uint64_t Next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 2685821657736338717ull; }
    double Float() { return (Next() >> 11) * (1.0 / 9007199254740992.0); }
    int    Int(int n) { return n > 0 ? (int)(Float() * n) : 0; }
};
inline uint64_t ChunkSeed(uint32_t seed, int cx, int cz, int salt) {
    return ((uint64_t)seed << 16) ^ ((uint64_t)(uint32_t)cx * 341873128712ull) ^ ((uint64_t)(uint32_t)cz * 132897987541ull) ^ ((uint64_t)salt << 56);
}

// ---------------------------------------------------------------------------
// Caves: Minecraft's classic "worm" carver (pre-1.18). Tunnels wander with a
// smoothly changing heading, swell in the middle, sometimes split in two,
// and sometimes start from a wide, flat room.
// ---------------------------------------------------------------------------
inline bool Carvable(uint8_t b) { return b == STONE || b == DIRT || b == GRASS || b == SAND; }

inline void CarveBlob(World& w, double cx, double cy, double cz, double rh, double rv) {
    const int x0 = (int)std::floor(cx - rh) - 1, x1 = (int)std::floor(cx + rh) + 1;
    const int y0 = std::max(1, (int)std::floor(cy - rv) - 1), y1 = std::min(WY - 1, (int)std::floor(cy + rv) + 1);
    const int z0 = (int)std::floor(cz - rh) - 1, z1 = (int)std::floor(cz + rh) + 1;
    // Never cut into water: the pond would hang over an open cave
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z)
            for (int y = y0; y <= y1; ++y)
                if (w.Get(x, y, z) == WATER) return;
    for (int x = x0 + 1; x < x1; ++x)
        for (int z = z0 + 1; z < z1; ++z) {
            if (!World::InXZ(x, z)) continue;
            for (int y = y0 + 1; y < y1; ++y) {
                double dx = (x + 0.5 - cx) / rh, dy = (y + 0.5 - cy) / rv, dz = (z + 0.5 - cz) / rh;
                if (dy <= -0.7) continue;                      // flat-ish floors
                if (dx * dx + dy * dy + dz * dz >= 1.0) continue;
                if (Carvable(w.Get(x, y, z))) w.SetRaw(x, y, z, AIR);
            }
        }
}

inline void CarveTunnel(World& w, GenRng& r, double x, double y, double z, float thickness,
                        float yaw, float pitch, int length) {
    const float PIf = 3.14159265f;
    float yawD = 0, pitchD = 0;
    const int split = thickness > 1.0f ? r.Int(length / 2) + length / 4 : -1;
    const bool steep = r.Int(6) == 0;
    for (int step = 0; step < length; ++step) {
        double rh = 1.5 + std::sin(PIf * step / length) * thickness;
        x += std::cos(yaw) * std::cos(pitch);
        y += std::sin(pitch);
        z += std::sin(yaw) * std::cos(pitch);
        pitch *= steep ? 0.92f : 0.7f;
        pitch += pitchD * 0.1f;
        yaw += yawD * 0.1f;
        pitchD *= 0.9f;
        yawD *= 0.75f;
        pitchD += (float)((r.Float() - r.Float()) * r.Float() * 2.0);
        yawD += (float)((r.Float() - r.Float()) * r.Float() * 4.0);
        if (step == split) {   // fork into two thinner tunnels going left and right
            CarveTunnel(w, r, x, y, z, (float)(r.Float() * 0.5 + 0.5), yaw - PIf / 2, pitch / 3.0f, length - step);
            CarveTunnel(w, r, x, y, z, (float)(r.Float() * 0.5 + 0.5), yaw + PIf / 2, pitch / 3.0f, length - step);
            return;
        }
        if (r.Int(4) == 0) continue;   // skipping some steps makes the walls lumpy
        CarveBlob(w, x, y, z, rh, rh);
    }
}

inline void CarveCaves(World& w, uint32_t seed) {
    for (int cx = 0; cx < CHUNKS_X; ++cx)
        for (int cz = 0; cz < CHUNKS_Z; ++cz) {
            GenRng r(ChunkSeed(seed, cx, cz, 1));
            if (r.Int(7) != 0) continue;   // about 1 chunk in 7 starts a cave system
            int count = r.Int(r.Int(r.Int(15) + 1) + 1);
            for (int i = 0; i < count; ++i) {
                double x = cx * CHUNK + r.Int(CHUNK), y = r.Int(r.Int(120) + 8), z = cz * CHUNK + r.Int(CHUNK);
                int tunnels = 1;
                if (r.Int(4) == 0) {   // a room: a wide, squashed blob
                    double rad = 1.5 + 1.0 + r.Float() * 6.0;
                    CarveBlob(w, x + 1, y, z, rad, rad * 0.5);
                    tunnels += r.Int(4);
                }
                for (int t = 0; t < tunnels; ++t) {
                    float yaw = (float)(r.Float() * 6.2831853);
                    float pitch = (float)((r.Float() - 0.5) / 4.0);
                    float thickness = (float)(r.Float() * 2.0 + r.Float());
                    if (r.Int(10) == 0) thickness *= (float)(r.Float() * r.Float() * 3.0 + 1.0);   // the odd huge one
                    CarveTunnel(w, r, x, y, z, thickness, yaw, pitch, 112 - r.Int(28));
                }
            }
        }
}

// ---------------------------------------------------------------------------
// Ores: Minecraft's classic vein shape (a chain of blobs along a short line),
// only replacing stone, with the per-chunk counts and heights of the
// 0..128 tall worlds this engine copies.
// ---------------------------------------------------------------------------
inline void PlaceVein(World& w, GenRng& r, int x, int y, int z, int size, uint8_t ore) {
    const double PId = 3.14159265358979;
    double f = r.Float() * PId, f1 = size / 8.0;
    double x0 = x + std::sin(f) * f1, x1 = x - std::sin(f) * f1;
    double z0 = z + std::cos(f) * f1, z1 = z - std::cos(f) * f1;
    double y0 = y + r.Int(3) - 2, y1 = y + r.Int(3) - 2;
    for (int k = 0; k < size; ++k) {
        double t = (double)k / size;
        double cx = x0 + (x1 - x0) * t, cy = y0 + (y1 - y0) * t, cz = z0 + (z1 - z0) * t;
        double rad = ((std::sin(PId * t) + 1.0) * (r.Float() * size / 16.0) + 1.0) / 2.0;
        for (int bx = (int)std::floor(cx - rad); bx <= (int)std::floor(cx + rad); ++bx)
            for (int by = (int)std::floor(cy - rad); by <= (int)std::floor(cy + rad); ++by)
                for (int bz = (int)std::floor(cz - rad); bz <= (int)std::floor(cz + rad); ++bz) {
                    double dx = (bx + 0.5 - cx) / rad, dy = (by + 0.5 - cy) / rad, dz = (bz + 0.5 - cz) / rad;
                    if (dx * dx + dy * dy + dz * dz < 1.0 && w.Get(bx, by, bz) == STONE) w.SetRaw(bx, by, bz, ore);
                }
    }
}

struct OreRule { uint8_t ore; int veinsPerChunk, size, minY, maxY; };
// Coal and iron as in Minecraft 1.17, diamonds deep down, emeralds as rare
// single blocks (Minecraft only puts them in mountains; there are none here)
inline const OreRule* OreRules(int& n) {
    static const OreRule rules[] = {
        {COAL_ORE,    20, 17, 0, 127},
        {IRON_ORE,    20,  9, 0,  63},
        {DIAMOND_ORE,  1,  8, 0,  15},
        {EMERALD_ORE,  1,  1, 4,  31},
    };
    n = (int)(sizeof rules / sizeof rules[0]);
    return rules;
}

inline void PlaceOres(World& w, uint32_t seed) {
    int n;
    const OreRule* rules = OreRules(n);
    for (int cx = 0; cx < CHUNKS_X; ++cx)
        for (int cz = 0; cz < CHUNKS_Z; ++cz) {
            GenRng r(ChunkSeed(seed, cx, cz, 2));
            for (int i = 0; i < n; ++i) {
                const OreRule& o = rules[i];
                for (int v = 0; v < o.veinsPerChunk; ++v) {
                    int x = cx * CHUNK + r.Int(CHUNK), z = cz * CHUNK + r.Int(CHUNK);
                    int y = o.minY + r.Int(o.maxY - o.minY + 1);
                    if (o.size == 1) { if (w.Get(x, y, z) == STONE) w.SetRaw(x, y, z, o.ore); }
                    else PlaceVein(w, r, x, y, z, o.size, o.ore);
                }
            }
        }
}

inline double SmoothStep(double a, double b, double x) {
    double t = std::max(0.0, std::min(1.0, (x - a) / (b - a)));
    return t * t * (3 - 2 * t);
}

// The climate at a column: these smooth noise fields shape the terrain and
// pick the biome, so neighbouring columns always agree and there are no seams
struct Climate {
    double continent, temperature, humidity, mountain, ridge, detail;
};

struct ClimateNoise {
    Perlin cont, temp, humid, mount, ridge, detail;
    explicit ClimateNoise(uint32_t seed)
        : cont(seed), temp(seed * 31u + 7u), humid(seed * 131u + 17u), mount(seed * 977u + 3u),
          ridge(seed * 1931u + 29u), detail(seed * 7919u + 13u) {}
    Climate At(int x, int z) const {
        Climate c;
        c.continent   = cont.Fbm(x / 170.0, z / 170.0, 4);
        c.temperature = temp.Fbm(x / 140.0 + 31.7, z / 140.0, 3);
        c.humidity    = humid.Fbm(x / 120.0, z / 120.0 + 57.3, 3);
        c.mountain    = mount.Fbm(x / 110.0, z / 110.0, 3);
        c.ridge       = 1.0 - std::fabs(ridge.Fbm(x / 55.0, z / 55.0, 4)) * 2.2;   // sharp crests
        c.detail      = detail.Fbm(x / 40.0, z / 40.0, 3);
        return c;
    }
};

// Terrain height (first air block) and biome from the climate
inline void ShapeColumn(const Climate& c, int& outH, uint8_t& outBiome) {
    const double land = SmoothStep(-0.16, -0.02, c.continent);      // 0 in the ocean, 1 inland
    double h = 64.0 + c.continent * 24.0 + c.detail * 3.0;          // gentle plains, ocean basins
    h -= SmoothStep(-0.12, -0.38, c.continent) * 12.0;               // open ocean gets properly deep

    double mtn = SmoothStep(0.10, 0.28, c.mountain) * land;         // mountains rise inland
    h += mtn * (14.0 + std::max(0.0, c.ridge) * 34.0);

    double swamp = SmoothStep(0.12, 0.26, c.humidity) * SmoothStep(-0.02, 0.10, c.temperature) * (1.0 - mtn) * land;
    h += (61.5 + c.detail * 2.5 - h) * swamp;                       // swamps sit at the waterline

    double desert = SmoothStep(0.06, 0.16, c.temperature) * SmoothStep(0.02, -0.08, c.humidity) * (1.0 - mtn) * land;
    h += desert * std::fabs(c.detail) * 4.0;                        // low dunes

    int hi = (int)std::floor(h);
    outH = std::max(8, std::min(WY - 12, hi));

    const bool cold = c.temperature < -0.24;
    if (outH < SEA_LEVEL && c.continent < -0.04) outBiome = cold ? FROZEN_OCEAN : OCEAN;
    else if (mtn > 0.5)                             outBiome = MOUNTAINS;
    else if (outH <= SEA_LEVEL + 1 && land < 0.98 && !cold && desert < 0.5) outBiome = BEACH;
    else if (cold)                                  outBiome = SNOWY_PLAINS;
    else if (c.temperature < -0.13)                 outBiome = TAIGA;
    else if (desert > 0.5)                          outBiome = DESERT;
    else if (swamp > 0.5)                           outBiome = SWAMP;
    else if (c.humidity > 0.06)                     outBiome = c.temperature < 0.04 ? BIRCH_FOREST : FOREST;
    else                                            outBiome = PLAINS;
}

// Averages biome colors over a 9x9 area so grass and water fade between biomes
inline void BlendBiomeColors(World& w) {
    const int R = 4;
    for (int x = 0; x < WX; ++x)
        for (int z = 0; z < WZ; ++z) {
            int sum[3][3] = {};
            int n = 0;
            for (int dx = -R; dx <= R; ++dx)
                for (int dz = -R; dz <= R; ++dz) {
                    const BiomeInfo& b = BiomeData(w.BiomeAt(std::clamp(x + dx, 0, WX - 1), std::clamp(z + dz, 0, WZ - 1)));
                    uint32_t cols[3] = {b.grass, b.foliage, b.water};
                    for (int k = 0; k < 3; ++k)
                        for (int ch = 0; ch < 3; ++ch) sum[k][ch] += (cols[k] >> (16 - 8 * ch)) & 0xFF;
                    ++n;
                }
            auto pack = [&](int k) { return (uint32_t)((sum[k][0] / n) << 16 | (sum[k][1] / n) << 8 | (sum[k][2] / n)); };
            size_t i = World::Col(x, z);
            w.grassColor[i] = pack(0); w.foliageColor[i] = pack(1); w.waterColor[i] = pack(2);
        }
}

inline void Generate(World& w, uint32_t seed) {
    w.seed = seed;
    ClimateNoise noise(seed);
    std::vector<int> heights((size_t)WX * WZ);

    std::vector<double> details((size_t)WX * WZ);
    for (int x = 0; x < WX; ++x)
        for (int z = 0; z < WZ; ++z) {
            Climate c = noise.At(x, z);
            int h;
            uint8_t biome;
            ShapeColumn(c, h, biome);
            heights[World::Col(x, z)] = h;
            w.biome[World::Col(x, z)] = biome;
            details[World::Col(x, z)] = c.detail;
        }
    auto heightAt = [&](int x, int z) { return heights[World::Col(std::clamp(x, 0, WX - 1), std::clamp(z, 0, WZ - 1))]; };

    for (int x = 0; x < WX; ++x)
    for (int z = 0; z < WZ; ++z) {
        const int h = heights[World::Col(x, z)];
        const uint8_t biome = w.biome[World::Col(x, z)];
        Climate c;
        c.detail = details[World::Col(x, z)];
        // Steepness: how far the ground drops to the lowest neighbour
        const int slope = h - std::min(std::min(heightAt(x + 1, z), heightAt(x - 1, z)), std::min(heightAt(x, z + 1), heightAt(x, z - 1)));

        // Surface layers: what the top few blocks are made of
        uint8_t top = GRASS, under = DIRT;
        int depth = 4;
        const bool wet = h <= SEA_LEVEL + 1;   // shores and the bottoms of ponds and seas
        switch (biome) {
            case DESERT: case BEACH: top = under = SAND; depth = 5; break;
            case OCEAN: case FROZEN_OCEAN: top = under = SAND; break;
            case SWAMP: if (h < SEA_LEVEL) top = DIRT; break;      // muddy pools, no sand
            case MOUNTAINS:
                if (h > 84 + (int)(c.detail * 6)) { top = under = STONE; depth = 1; }   // bare rock up high
                break;
            default: if (wet) top = under = SAND; break;
        }
        // Cliffs too steep for soil show bare stone (no walls of dirt)
        if (slope >= 3 && top != SAND) { top = under = STONE; depth = 1; }
        if (top == GRASS && h < SEA_LEVEL) top = DIRT;               // grass doesn't grow underwater

        for (int y = 0; y < h; ++y) {
            uint8_t block;
            if (y == 0) block = BEDROCK;
            else if (y <= 4 && Hash01(x, y, z, seed) < (5 - y) / 5.0) block = BEDROCK;
            else if (y < h - depth) block = STONE;
            else if (y < h - 1)     block = under;
            else                    block = top;
            w.SetRaw(x, y, z, block);
        }
        // Snow-capped peaks
        if (biome == MOUNTAINS && h > 102 + (int)(c.detail * 6))
            for (int y = h - 2; y < h; ++y) w.SetRaw(x, y, z, SNOW_BLOCK);
        for (int y = h; y < SEA_LEVEL; ++y) w.SetRaw(x, y, z, WATER);
        if (BiomeData(biome).snowy && h < SEA_LEVEL) w.SetRaw(x, SEA_LEVEL - 1, z, ICE);   // frozen over
    }
    BlendBiomeColors(w);

    CarveCaves(w, seed);   // caves first, so ores show in the cave walls
    PlaceOres(w, seed);

    // Trees and cactus: each biome has its own mix and density
    for (int x = 3; x < WX - 3; ++x)
    for (int z = 3; z < WZ - 3; ++z) {
        int h = heights[World::Col(x, z)];
        uint8_t ground = w.Get(x, h - 1, z);
        double roll = Hash01(x, 0, z, seed + 11), pick = Hash01(x, 7, z, seed + 19);
        switch (w.biome[World::Col(x, z)]) {
            case DESERT:
                if (ground == SAND && roll < 0.006) PlaceCactus(w, x, h, z, seed);
                break;
            case PLAINS:       if (ground == GRASS && roll < 0.0025) PlaceOakTree(w, x, h, z, seed); break;
            case FOREST:       if (ground == GRASS && roll < 0.035) PlaceOakTree(w, x, h, z, seed, pick < 0.2); break;
            case BIRCH_FOREST: if (ground == GRASS && roll < 0.03) PlaceOakTree(w, x, h, z, seed, true); break;
            case TAIGA:        if (ground == GRASS && roll < 0.03) PlaceSpruceTree(w, x, h, z, seed); break;
            case SNOWY_PLAINS: if (ground == GRASS && roll < 0.003) PlaceSpruceTree(w, x, h, z, seed); break;
            case SWAMP:        if (ground == GRASS && roll < 0.012) PlaceOakTree(w, x, h, z, seed); break;
            case MOUNTAINS:
                if (ground == GRASS && roll < 0.006) {
                    if (pick < 0.6) PlaceSpruceTree(w, x, h, z, seed); else PlaceOakTree(w, x, h, z, seed);
                }
                break;
            default: break;
        }
    }

    // A thin layer of snow on everything in snowy places (and up the mountains)
    for (int x = 0; x < WX; ++x)
        for (int z = 0; z < WZ; ++z) {
            int y = WY - 1;
            while (y > 0 && w.Get(x, y, z) == AIR) --y;
            uint8_t b = w.Get(x, y, z);
            bool snowy = BiomeData(w.biome[World::Col(x, z)]).snowy ||
                         (w.biome[World::Col(x, z)] == MOUNTAINS && y >= 98);
            if (snowy && y + 1 < WY && (Info(b).opaque || IsLeaves(b)) && b != ICE) w.SetRaw(x, y + 1, z, SNOW);
        }

    for (int x = 0; x < WX; ++x)
        for (int z = 0; z < WZ; ++z) w.RecomputeTop(x, z);
    RelightAll(w);
}

// ---------------------------------------------------------------------------
// Player physics
// ---------------------------------------------------------------------------
struct AABB { double minX, minY, minZ, maxX, maxY, maxZ; };

inline AABB Offset(const AABB& b, double dx, double dy, double dz) {
    return {b.minX + dx, b.minY + dy, b.minZ + dz, b.maxX + dx, b.maxY + dy, b.maxZ + dz};
}

struct PlayerInput {
    bool forward = false, back = false, left = false, right = false;
    bool jump = false, sneak = false, sprint = false;
    bool usingItem = false;   // eating: slows you down and stops sprinting
};

struct Player {
    double x = 0, y = 0, z = 0;          // feet, center of the hitbox
    double px = 0, py = 0, pz = 0;       // previous tick (for render interpolation)
    double vx = 0, vy = 0, vz = 0;       // blocks per tick
    float  yaw = 0, pitch = 0;           // radians; forward = (cos y cos p, sin p, sin y cos p)

    bool onGround = false, horizontalCollision = false;
    bool sneaking = false, sprinting = false, flying = false, creative = false;
    bool inWater = false;
    int  jumpDelay = 0, sprintTapTimer = 0, flyTapTimer = 0;
    bool prevForward = false, prevJump = false;

    // View bobbing state (same model Minecraft uses)
    double walkDist = 0, prevWalkDist = 0, bob = 0, prevBob = 0;

    // Survival stats
    double health = MAX_HEALTH, saturation = START_SATURATION, exhaustion = 0, fallDistance = 0;
    int    food = MAX_FOOD, foodTimer = 0, air = MAX_AIR;
    int    hurtTime = 0, invulnerable = 0;   // camera jolt / damage immunity, in ticks

    double Height() const    { return (sneaking && !flying) ? SNEAK_HEIGHT : PLAYER_HEIGHT; }
    double EyeHeight() const { return (sneaking && !flying) ? SNEAK_EYE_HEIGHT : EYE_HEIGHT; }
    AABB   Box() const {
        const double hw = PLAYER_WIDTH * 0.5;
        return {x - hw, y, z - hw, x + hw, y + Height(), z + hw};
    }
};

// Damage, after which you're immune for half a second (creative ignores it)
inline bool Hurt(Player& p, double amount) {
    if (p.creative || amount <= 0 || p.health <= 0 || p.invulnerable > 10) return false;
    p.health = std::max(0.0, p.health - amount);
    p.invulnerable = 20;
    p.hurtTime = 10;
    return true;
}
inline void Heal(Player& p, double amount) {
    if (p.health > 0) p.health = std::min(MAX_HEALTH, p.health + amount);
}
inline void Eat(Player& p, int nutrition, double saturationMod) {
    p.food = std::min(MAX_FOOD, p.food + nutrition);
    p.saturation = std::min((double)p.food, p.saturation + nutrition * saturationMod * 2.0);
}

inline AABB Shrink(const AABB& b, double e) {
    return {b.minX + e, b.minY + e, b.minZ + e, b.maxX - e, b.maxY - e, b.maxZ - e};
}

constexpr double EPS = 1e-7;

inline bool Overlaps(const AABB& a, const AABB& b) {
    return a.maxX > b.minX + EPS && a.minX < b.maxX - EPS &&
           a.maxY > b.minY + EPS && a.minY < b.maxY - EPS &&
           a.maxZ > b.minZ + EPS && a.minZ < b.maxZ - EPS;
}

// The part of a block you bump into (cactus is inset 1/16 like Minecraft's)
inline AABB CollisionBox(uint8_t b, int x, int y, int z) {
    if (b == CACTUS) return {x + 1 / 16.0, (double)y, z + 1 / 16.0, x + 15 / 16.0, y + 15 / 16.0, z + 15 / 16.0};
    return {(double)x, (double)y, (double)z, x + 1.0, y + 1.0, z + 1.0};
}

inline void CollectSolid(const World& w, const AABB& r, std::vector<AABB>& out) {
    out.clear();
    int x0 = (int)std::floor(r.minX), x1 = (int)std::floor(r.maxX);
    int y0 = (int)std::floor(r.minY), y1 = (int)std::floor(r.maxY);
    int z0 = (int)std::floor(r.minZ), z1 = (int)std::floor(r.maxZ);
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z)
                if (w.SolidAt(x, y, z))
                    out.push_back(CollisionBox(w.Get(x, y, z), x, y, z));
}

inline bool AnySolid(const World& w, const AABB& box) {
    std::vector<AABB> tmp;
    CollectSolid(w, box, tmp);
    for (const AABB& o : tmp) if (Overlaps(box, o)) return true;
    return false;
}

// Clip a movement along one axis against one obstacle box
inline double ClipAxis(const AABB& b, const AABB& o, double d, int axis) {
    double bMin[3] = {b.minX, b.minY, b.minZ}, bMax[3] = {b.maxX, b.maxY, b.maxZ};
    double oMin[3] = {o.minX, o.minY, o.minZ}, oMax[3] = {o.maxX, o.maxY, o.maxZ};
    for (int a = 0; a < 3; ++a) {
        if (a == axis) continue;
        if (bMax[a] <= oMin[a] + EPS || bMin[a] >= oMax[a] - EPS) return d;   // no overlap
    }
    if (d > 0 && bMax[axis] <= oMin[axis] + EPS) {
        double room = oMin[axis] - bMax[axis];
        if (room < d) d = room;
    } else if (d < 0 && bMin[axis] >= oMax[axis] - EPS) {
        double room = oMax[axis] - bMin[axis];
        if (room > d) d = room;
    }
    return d;
}

// Move a box through the world, clipping (dx, dy, dz) to what actually fits.
// Same axis order as Minecraft: Y first, then the larger of X/Z.
inline void SweepBox(const World& w, AABB& box, double& dx, double& dy, double& dz) {
    AABB region = {std::min(box.minX, box.minX + dx), std::min(box.minY, box.minY + dy),
                   std::min(box.minZ, box.minZ + dz), std::max(box.maxX, box.maxX + dx),
                   std::max(box.maxY, box.maxY + dy), std::max(box.maxZ, box.maxZ + dz)};
    static std::vector<AABB> obstacles;
    CollectSolid(w, region, obstacles);

    for (const AABB& o : obstacles) dy = ClipAxis(box, o, dy, 1);
    box = Offset(box, 0, dy, 0);
    bool xFirst = !(std::fabs(dx) < std::fabs(dz));
    for (int pass = 0; pass < 2; ++pass) {
        bool doX = (pass == 0) == xFirst;
        if (doX) { for (const AABB& o : obstacles) dx = ClipAxis(box, o, dx, 0); box = Offset(box, dx, 0, 0); }
        else     { for (const AABB& o : obstacles) dz = ClipAxis(box, o, dz, 2); box = Offset(box, 0, 0, dz); }
    }
}

inline void MovePlayer(const World& w, Player& p, double dx, double dy, double dz) {
    AABB box = p.Box();

    // Sneak edge protection: shrink horizontal movement in 0.05 steps
    // until there is still ground within STEP_HEIGHT below the hitbox.
    if (p.sneaking && p.onGround && !p.flying && dy <= 0.0) {
        const double step = 0.05;
        auto noGround = [&](double ox, double oz) {
            return !AnySolid(w, Offset(box, ox, -STEP_HEIGHT, oz));
        };
        auto shrink = [&](double& v) {
            if (std::fabs(v) < step) v = 0; else v -= (v > 0 ? step : -step);
        };
        while (dx != 0 && noGround(dx, 0)) shrink(dx);
        while (dz != 0 && noGround(0, dz)) shrink(dz);
        while (dx != 0 && dz != 0 && noGround(dx, dz)) { shrink(dx); shrink(dz); }
    }

    const double ox = dx, oy = dy, oz = dz;
    SweepBox(w, box, dx, dy, dz);

    p.x = (box.minX + box.maxX) * 0.5;
    p.y = box.minY;
    p.z = (box.minZ + box.maxZ) * 0.5;

    bool hitX = dx != ox, hitY = dy != oy, hitZ = dz != oz;
    p.horizontalCollision = hitX || hitZ;
    p.onGround = hitY && oy < 0.0;
    if (hitX) p.vx = 0;
    if (hitY) p.vy = 0;
    if (hitZ) p.vz = 0;
}

inline bool TouchesBlock(const World& w, const AABB& box, uint8_t type) {
    int x0 = (int)std::floor(box.minX), x1 = (int)std::floor(box.maxX);
    int y0 = (int)std::floor(box.minY), y1 = (int)std::floor(box.maxY);
    int z0 = (int)std::floor(box.minZ), z1 = (int)std::floor(box.maxZ);
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z)
                if (w.Get(x, y, z) == type) return true;
    return false;
}

inline void Spawn(const World& w, Player& p) {
    // Spiral out from the world center until we find dry land
    int cx = WX / 2, cz = WZ / 2;
    for (int r = 0; r < WX / 2; ++r)
        for (int dx = -r; dx <= r; ++dx)
            for (int dz = -r; dz <= r; ++dz) {
                if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                int x = cx + dx, z = cz + dz, y = WY - 1;
                while (y > 0 && (w.Get(x, y, z) == AIR || w.Get(x, y, z) == SNOW)) --y;
                uint8_t b = w.Get(x, y, z);
                if (b != GRASS && b != SAND) continue;   // not water, trees, or the bottom of a cave mouth
                bool clear = true;   // no tree canopy within 2 blocks: spawn in the open
                for (int ax = -2; ax <= 2 && clear; ++ax)
                    for (int az = -2; az <= 2 && clear; ++az)
                        if (w.TopAt(x + ax, z + az) > y) clear = false;
                if (!clear) continue;
                p.x = x + 0.5; p.y = y + 1.0; p.z = z + 0.5;
                p.px = p.x; p.py = p.y; p.pz = p.z;
                p.vx = p.vy = p.vz = 0;
                p.flying = false;
                p.fallDistance = 0;
                return;
            }
    p.x = cx + 0.5; p.y = WY - 10; p.z = cz + 0.5;
}

// One 50 ms game tick for the player. Mirrors LivingEntity.aiStep + travel.
inline void TickPlayer(const World& w, Player& p, const PlayerInput& in) {
    p.px = p.x; p.py = p.y; p.pz = p.z;
    p.prevWalkDist = p.walkDist;
    p.prevBob = p.bob;

    AABB shrunk = p.Box();
    shrunk.minX += 0.001; shrunk.minY += 0.001; shrunk.minZ += 0.001;
    shrunk.maxX -= 0.001; shrunk.maxY -= 0.001; shrunk.maxZ -= 0.001;
    p.inWater = TouchesBlock(w, shrunk, WATER);

    // --- Creative flight toggle: double tap space ---
    bool jumpPressed = in.jump && !p.prevJump;
    if (!p.creative) p.flying = false;
    if (p.flyTapTimer > 0) --p.flyTapTimer;
    if (p.creative && jumpPressed) {
        if (p.flyTapTimer > 0) { p.flying = !p.flying; p.flyTapTimer = 0; }
        else p.flyTapTimer = DOUBLE_TAP_TICKS;
    }

    p.sneaking = in.sneak && !p.flying;

    // --- Movement impulse ---
    double fwd = (in.forward ? 1.0 : 0.0) - (in.back ? 1.0 : 0.0);
    double str = (in.right ? 1.0 : 0.0) - (in.left ? 1.0 : 0.0);
    if (p.sneaking) { fwd *= SNEAK_MULT; str *= SNEAK_MULT; }
    if (in.usingItem) { fwd *= USE_ITEM_SLOWDOWN; str *= USE_ITEM_SLOWDOWN; }

    // --- Sprinting: Ctrl or double tap W, needs real forward input and food ---
    bool canSprint = fwd >= 0.8 && !p.sneaking && (p.creative || p.food >= SPRINT_MIN_FOOD);
    bool fwdPressed = in.forward && !p.prevForward;
    if (p.sprintTapTimer > 0) --p.sprintTapTimer;
    if (fwdPressed) {
        if (p.sprintTapTimer > 0 && canSprint) p.sprinting = true;
        p.sprintTapTimer = DOUBLE_TAP_TICKS;
    }
    if (in.sprint && canSprint) p.sprinting = true;
    if (!canSprint || p.horizontalCollision) p.sprinting = false;
    p.prevForward = in.forward;
    p.prevJump = in.jump;

    fwd *= INPUT_MULT;
    str *= INPUT_MULT;

    // Tiny velocities snap to zero
    if (std::fabs(p.vx) < 0.003) p.vx = 0;
    if (std::fabs(p.vy) < 0.003) p.vy = 0;
    if (std::fabs(p.vz) < 0.003) p.vz = 0;

    const double fx = std::cos(p.yaw), fz = std::sin(p.yaw);   // flat forward
    const double rx = -fz, rz = fx;                            // flat right

    // --- Jumping / swimming / flying vertical input ---
    if (p.jumpDelay > 0) --p.jumpDelay;
    if (p.flying) {
        if (in.sneak) p.vy -= FLY_SPEED * 3.0;
        if (in.jump)  p.vy += FLY_SPEED * 3.0;
    } else if (in.jump) {
        if (p.inWater) {
            p.vy += WATER_SWIM_UP;
        } else if (p.onGround && p.jumpDelay == 0) {
            p.vy = JUMP_VELOCITY;
            if (p.sprinting) { p.vx += fx * SPRINT_JUMP_BOOST; p.vz += fz * SPRINT_JUMP_BOOST; }
            p.jumpDelay = JUMP_DELAY_TICKS;
            if (!p.creative) p.exhaustion += p.sprinting ? EXHAUSTION_SPRINT_JUMP : EXHAUSTION_JUMP;
        }
    } else {
        p.jumpDelay = 0;
    }

    // --- Accelerate along the input direction ("moveRelative") ---
    auto moveRelative = [&](double accel) {
        double len2 = fwd * fwd + str * str;
        if (len2 < 1e-7) return;
        double f = fwd, s = str;
        if (len2 > 1.0) { double l = std::sqrt(len2); f /= l; s /= l; }  // diagonal quirk
        f *= accel; s *= accel;
        p.vx += fx * f + rx * s;
        p.vz += fz * f + rz * s;
    };

    double oldX = p.x, oldY = p.y, oldZ = p.z;

    if (p.inWater && !p.flying) {
        moveRelative(WATER_ACCEL);
        MovePlayer(w, p, p.vx, p.vy, p.vz);
        p.vx *= WATER_DRAG; p.vy *= WATER_DRAG; p.vz *= WATER_DRAG;
        p.vy -= WATER_GRAVITY;
    } else {
        uint8_t under = w.Get((int)std::floor(p.x), (int)std::floor(p.y - 0.5000001), (int)std::floor(p.z));
        double slip = p.onGround ? Slipperiness(under) : 1.0;
        double friction = p.onGround ? slip * AIR_FRICTION : AIR_FRICTION;
        double speed = MOVE_SPEED * (p.sprinting ? SPRINT_MULT : 1.0);
        double accel;
        if (p.onGround)    accel = speed * (0.21600002 / (slip * slip * slip));
        else if (p.flying) accel = FLY_SPEED * (p.sprinting ? 2.0 : 1.0);
        else               accel = p.sprinting ? AIR_ACCEL_SPRINT : AIR_ACCEL;

        moveRelative(accel);
        double savedVy = p.vy;
        MovePlayer(w, p, p.vx, p.vy, p.vz);
        if (p.flying) {
            p.vy = savedVy * FLY_VERTICAL_DRAG;
            if (p.onGround) p.flying = false;   // touching down ends flight
        } else {
            p.vy -= GRAVITY;
            p.vy *= VERTICAL_DRAG;
        }
        p.vx *= friction;
        p.vz *= friction;
    }

    // --- View bobbing bookkeeping ---
    double moved = std::sqrt((p.x - oldX) * (p.x - oldX) + (p.z - oldZ) * (p.z - oldZ));
    p.walkDist += moved * 0.6;
    double target = (p.onGround && !p.flying) ? std::min(0.1, moved) : 0.0;
    p.bob += (target - p.bob) * 0.4;

    // --- Fall damage: water and flight forgive it, landing cashes it in ---
    if (p.flying || TouchesBlock(w, Shrink(p.Box(), 0.001), WATER)) {
        p.fallDistance = 0;
    } else if (p.onGround) {
        if (p.fallDistance > 0) Hurt(p, std::ceil(p.fallDistance - SAFE_FALL - 1e-6));
        p.fallDistance = 0;
    } else if (p.y < oldY) {
        p.fallDistance += oldY - p.y;
    }

    // --- Hunger cost of moving (walking is free) ---
    if (!p.creative) {
        if (p.inWater)                        p.exhaustion += EXHAUSTION_SWIM * moved;
        else if (p.onGround && p.sprinting)   p.exhaustion += EXHAUSTION_SPRINT * moved;
        p.exhaustion = std::min(p.exhaustion, MAX_EXHAUSTION);
    }

    // Fell into the void: creative pops back to spawn, survival takes void damage
    if (p.y < -64.0 && p.creative) Spawn(w, p);
}

inline bool EyesInWater(const World& w, const Player& p) {
    return w.Get((int)std::floor(p.x), (int)std::floor(p.y + p.EyeHeight()), (int)std::floor(p.z)) == WATER;
}

// One tick of survival bookkeeping: breath, void, hunger, regeneration.
// Mirrors Minecraft's FoodData.tick on Normal difficulty.
inline void TickSurvival(const World& w, Player& p) {
    if (p.hurtTime > 0) --p.hurtTime;
    if (p.invulnerable > 0) --p.invulnerable;
    if (p.creative || p.health <= 0) { p.air = MAX_AIR; return; }

    // Breath: 15 s underwater, then 1 heart of damage every second
    if (EyesInWater(w, p)) {
        if (--p.air <= -20) { p.air = 0; Hurt(p, 2); }
    } else {
        p.air = std::min(MAX_AIR, p.air + 4);
    }

    if (p.y < -64.0) Hurt(p, 4);   // the void
    if (TouchesBlock(w, p.Box(), CACTUS)) Hurt(p, 1);

    // Every 4 exhaustion eats a point of saturation, then a point of food
    if (p.exhaustion > 4.0) {
        p.exhaustion -= 4.0;
        if (p.saturation > 0) p.saturation = std::max(0.0, p.saturation - 1.0);
        else p.food = std::max(0, p.food - 1);
    }

    bool hurt = p.health < MAX_HEALTH;
    if (p.saturation > 0 && hurt && p.food >= MAX_FOOD) {
        // Saturated: fast healing, paid for with saturation
        if (++p.foodTimer >= 10) {
            double f = std::min(p.saturation, 6.0);
            Heal(p, f / 6.0);
            p.exhaustion = std::min(MAX_EXHAUSTION, p.exhaustion + f);
            p.foodTimer = 0;
        }
    } else if (p.food >= 18 && hurt) {
        // Well fed: half a heart every 4 seconds
        if (++p.foodTimer >= 80) {
            Heal(p, 1.0);
            p.exhaustion = std::min(MAX_EXHAUSTION, p.exhaustion + 6.0);
            p.foodTimer = 0;
        }
    } else if (p.food <= 0) {
        // Starving: down to half a heart (Normal difficulty never kills)
        if (++p.foodTimer >= 80) {
            if (p.health > 1.0) Hurt(p, 1.0);
            p.foodTimer = 0;
        }
    } else {
        p.foodTimer = 0;
    }
}

inline void Respawn(const World& w, Player& p) {
    Spawn(w, p);
    p.health = MAX_HEALTH; p.food = MAX_FOOD; p.saturation = START_SATURATION;
    p.exhaustion = 0; p.foodTimer = 0; p.air = MAX_AIR;
    p.hurtTime = p.invulnerable = 0;
    p.sprinting = p.sneaking = false;
}

// ---------------------------------------------------------------------------
// Block raycast (Amanatides & Woo DDA): walks the grid cell by cell
// ---------------------------------------------------------------------------
struct RayHit {
    bool   hit = false;
    int    x = 0, y = 0, z = 0;     // block that was hit
    int    nx = 0, ny = 0, nz = 0;  // face normal (where a new block would go)
    double dist = 0;
};

// The box you aim at and see outlined. Full cube for normal blocks; a small
// box for torches (same sizes as Minecraft's torch outlines).
inline AABB BlockBox(uint8_t b, int x, int y, int z) {
    const double p = 1.0 / 16.0;
    switch (b) {
        case TORCH:         return {x + 6 * p, (double)y, z + 6 * p, x + 10 * p, y + 10 * p, z + 10 * p};
        case WALL_TORCH_PX: return {(double)x, y + 3 * p, z + 5.5 * p, x + 5 * p, y + 13 * p, z + 10.5 * p};
        case WALL_TORCH_NX: return {x + 11 * p, y + 3 * p, z + 5.5 * p, x + 1.0, y + 13 * p, z + 10.5 * p};
        case WALL_TORCH_PZ: return {x + 5.5 * p, y + 3 * p, (double)z, x + 10.5 * p, y + 13 * p, z + 5 * p};
        case WALL_TORCH_NZ: return {x + 5.5 * p, y + 3 * p, z + 11 * p, x + 10.5 * p, y + 13 * p, z + 1.0};
        case SNOW:          return {(double)x, (double)y, (double)z, x + 1.0, y + 2 * p, z + 1.0};
        case CACTUS:        return {x + p, (double)y, z + p, x + 15 * p, y + 1.0, z + 15 * p};
        default:            return {(double)x, (double)y, (double)z, x + 1.0, y + 1.0, z + 1.0};
    }
}

// Ray vs box (slab test). On a hit, t is the entry distance and n the face normal.
inline bool RayBox(double ox, double oy, double oz, double dx, double dy, double dz, const AABB& b,
                   double& t, int& nx, int& ny, int& nz) {
    double o[3] = {ox, oy, oz}, d[3] = {dx, dy, dz};
    double lo[3] = {b.minX, b.minY, b.minZ}, hi[3] = {b.maxX, b.maxY, b.maxZ};
    double tNear = -1e30, tFar = 1e30;
    int axis = -1, sign = 0;
    for (int a = 0; a < 3; ++a) {
        if (std::fabs(d[a]) < 1e-12) {
            if (o[a] < lo[a] || o[a] > hi[a]) return false;
            continue;
        }
        double t1 = (lo[a] - o[a]) / d[a], t2 = (hi[a] - o[a]) / d[a];
        int s1 = -1;
        if (t1 > t2) { std::swap(t1, t2); s1 = 1; }
        if (t1 > tNear) { tNear = t1; axis = a; sign = s1; }
        tFar = std::min(tFar, t2);
        if (tNear > tFar || tFar < 0) return false;
    }
    t = std::max(0.0, tNear);
    nx = ny = nz = 0;
    if (axis == 0) nx = sign; else if (axis == 1) ny = sign; else if (axis == 2) nz = sign;
    return true;
}

inline RayHit Raycast(const World& w, double ox, double oy, double oz,
                      double dx, double dy, double dz, double maxDist,
                      bool stopAtWater = false) {
    RayHit r;
    int x = (int)std::floor(ox), y = (int)std::floor(oy), z = (int)std::floor(oz);
    int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1, sz = dz > 0 ? 1 : -1;
    const double BIG = 1e30;
    double tdx = dx != 0 ? std::fabs(1.0 / dx) : BIG;
    double tdy = dy != 0 ? std::fabs(1.0 / dy) : BIG;
    double tdz = dz != 0 ? std::fabs(1.0 / dz) : BIG;
    double tmx = dx != 0 ? (dx > 0 ? (x + 1 - ox) : (ox - x)) * tdx : BIG;
    double tmy = dy != 0 ? (dy > 0 ? (y + 1 - oy) : (oy - y)) * tdy : BIG;
    double tmz = dz != 0 ? (dz > 0 ? (z + 1 - oz) : (oz - z)) * tdz : BIG;
    int nx = 0, ny = 0, nz = 0;
    double t = 0;
    while (t <= maxDist) {
        uint8_t b = w.Get(x, y, z);
        if (IsTorch(b) || b == SNOW || b == CACTUS) {
            // Small hitbox: the ray can pass beside a torch or over thin snow
            double tt; int hx, hy, hz;
            if (RayBox(ox, oy, oz, dx, dy, dz, BlockBox(b, x, y, z), tt, hx, hy, hz) && tt <= maxDist) {
                r.hit = true; r.x = x; r.y = y; r.z = z;
                r.nx = hx; r.ny = hy; r.nz = hz; r.dist = tt;
                return r;
            }
        } else if (b != AIR && (b != WATER || stopAtWater)) {
            r.hit = true; r.x = x; r.y = y; r.z = z;
            r.nx = nx; r.ny = ny; r.nz = nz; r.dist = t;
            return r;
        }
        if (tmx < tmy && tmx < tmz) { x += sx; t = tmx; tmx += tdx; nx = -sx; ny = 0; nz = 0; }
        else if (tmy < tmz)         { y += sy; t = tmy; tmy += tdy; nx = 0; ny = -sy; nz = 0; }
        else                        { z += sz; t = tmz; tmz += tdz; nx = 0; ny = 0; nz = -sz; }
    }
    return r;
}

// ---------------------------------------------------------------------------
// Item stacks, drops and the inventory
// ---------------------------------------------------------------------------
constexpr int     APPLE_EAT_TICKS  = 32;     // 1.6 s
constexpr int     APPLE_NUTRITION  = 4;      // 2 shanks
constexpr double  APPLE_SATURATION = 0.3;    // +2.4 saturation
constexpr double  APPLE_CHANCE     = 0.005;  // per oak leaves block broken
constexpr int     ITEM_LIFETIME    = 6000;   // despawn after 5 minutes
constexpr double  ITEM_SIZE        = 0.25;
constexpr double  ITEM_GRAVITY     = 0.04;

struct Stack {
    uint8_t item = AIR;
    int     count = 0;
    int     damage = 0;   // tool wear
};

// Two stacks can share a slot if they're the same, undamaged-equal, and stackable
inline bool CanMerge(const Stack& a, const Stack& b) {
    return a.item == b.item && a.damage == b.damage && MaxStack(a.item) > 1;
}

// Using a tool wears it down. Returns true when it breaks.
inline bool WearTool(Stack& s, int amount) {
    int max = MaxDurability(s.item);
    if (max <= 0 || s.count <= 0) return false;
    s.damage += amount;
    if (s.damage >= max) { s = {}; return true; }
    return false;
}

// What a block drops when mined with `tool` (AIR = bare hand). Pickaxe
// blocks drop nothing without the right pickaxe, like in Minecraft.
inline Stack BlockDrop(uint8_t b, uint8_t tool = AIR) {
    if (!CanHarvest(b, tool)) return {};
    switch (b) {
        case AIR: case WATER: case BEDROCK: case GLASS: case ICE: case SNOW: return {};   // snow needs snowballs, ice melts
        case SPRUCE_LEAVES: case BIRCH_LEAVES: return {};
        case GRASS:    return {DIRT, 1};
        case STONE:    return {COBBLESTONE, 1};
        case COAL_ORE:    return {COAL, 1};
        case DIAMOND_ORE: return {DIAMOND, 1};
        case EMERALD_ORE: return {EMERALD, 1};
        case FURNACE_NX: case FURNACE_PZ: case FURNACE_NZ:
        case LIT_FURNACE_PX: case LIT_FURNACE_NX: case LIT_FURNACE_PZ: case LIT_FURNACE_NZ: return {FURNACE, 1};
        case WALL_TORCH_PX: case WALL_TORCH_NX: case WALL_TORCH_PZ: case WALL_TORCH_NZ: return {TORCH, 1};
        case LEAVES:   return Rand01() < APPLE_CHANCE ? Stack{APPLE, 1} : Stack{};
        default:       return {b, 1};
    }
}

// 36 slots like Minecraft: 0-8 are the hotbar, 9-35 the main inventory
constexpr int HOTBAR_SLOTS    = 9;
constexpr int INVENTORY_SLOTS = 36;

struct Inventory {
    Stack slots[INVENTORY_SLOTS];

    // Moves as much of `s` as fits into slots [from, to): tops up matching
    // stacks first, then fills empty slots. Whatever doesn't fit stays in s.
    void MoveInto(Stack& s, int from, int to) {
        for (int i = from; i < to && s.count > 0; ++i) {
            Stack& d = slots[i];
            if (d.count > 0 && CanMerge(d, s) && d.count < MaxStack(d.item)) {
                int n = std::min(s.count, MaxStack(d.item) - d.count);
                d.count += n; s.count -= n;
            }
        }
        for (int i = from; i < to && s.count > 0; ++i) {
            Stack& d = slots[i];
            if (d.count == 0) {
                int n = std::min(s.count, MaxStack(s.item));
                d = s; d.count = n; s.count -= n;
            }
        }
        if (s.count <= 0) s = {};
    }
    // How many of this stack would fit anywhere
    int Room(const Stack& s) const {
        int room = 0;
        for (const Stack& d : slots) {
            if (d.count == 0) room += MaxStack(s.item);
            else if (CanMerge(d, s)) room += MaxStack(d.item) - d.count;
        }
        return room;
    }
    // Picks up a stack (hotbar first). Returns how many fit.
    int Add(Stack s) {
        int before = s.count;
        MoveInto(s, 0, INVENTORY_SLOTS);
        return before - s.count;
    }
    int Add(uint8_t item, int count) { return Add(Stack{item, count}); }
    void Take(int slot, int n) {
        Stack& s = slots[slot];
        s.count -= n;
        if (s.count <= 0) s = {};
    }
};

// A dropped item lying in the world (Minecraft's ItemEntity)
struct ItemEntity {
    double x = 0, y = 0, z = 0, px = 0, py = 0, pz = 0, vx = 0, vy = 0, vz = 0;
    Stack  stack;
    int    age = 0, pickupDelay = 10;
    float  bobOffset = 0;
    bool   onGround = false;

    AABB Box() const {
        const double h = ITEM_SIZE * 0.5;
        return {x - h, y, z - h, x + h, y + ITEM_SIZE, z + h};
    }
};

inline void SpawnItem(std::vector<ItemEntity>& items, double x, double y, double z,
                      double vx, double vy, double vz, Stack s, int pickupDelay) {
    ItemEntity e;
    e.x = e.px = x; e.y = e.py = y; e.z = e.pz = z;
    e.vx = vx; e.vy = vy; e.vz = vz;
    e.stack = s;
    e.pickupDelay = pickupDelay;
    e.bobOffset = (float)(Rand01() * 6.2831853);
    items.push_back(e);
}

// Pops out of a broken block with a little hop
inline void DropFromBlock(std::vector<ItemEntity>& items, int bx, int by, int bz, Stack s) {
    SpawnItem(items, bx + 0.5 + (Rand01() - 0.5) * 0.5,
              by + 0.5 - ITEM_SIZE * 0.5 + (Rand01() - 0.5) * 0.5,
              bz + 0.5 + (Rand01() - 0.5) * 0.5,
              Rand01() * 0.2 - 0.1, 0.2, Rand01() * 0.2 - 0.1, s, 10);
}

// Item physics, merging nearby stacks, pickup and despawning
inline void TickItems(const World& w, std::vector<ItemEntity>& items, const Player& p,
                      Inventory& inv, bool canPickUp) {
    AABB reach = p.Box();   // pickup range: hitbox grown 1 block sideways, 0.5 up/down
    reach.minX -= 1; reach.maxX += 1; reach.minY -= 0.5; reach.maxY += 0.5; reach.minZ -= 1; reach.maxZ += 1;

    for (ItemEntity& e : items) {
        e.px = e.x; e.py = e.y; e.pz = e.z;
        ++e.age;
        if (e.pickupDelay > 0) --e.pickupDelay;

        // A block placed on top of an item pushes it up out of the way
        int bx = (int)std::floor(e.x), by = (int)std::floor(e.y + ITEM_SIZE * 0.5), bz = (int)std::floor(e.z);
        if (w.SolidAt(bx, by, bz)) { e.y = by + 1.0; e.vy = 0; }

        if (w.Get(bx, (int)std::floor(e.y + 0.1), bz) == WATER) {
            e.vx *= 0.99; e.vz *= 0.99;                 // float up to the surface
            if (e.vy < 0.06) e.vy += 5e-4;
        } else {
            e.vy -= ITEM_GRAVITY;
        }

        double dx = e.vx, dy = e.vy, dz = e.vz;
        AABB box = e.Box();
        SweepBox(w, box, dx, dy, dz);
        e.x = (box.minX + box.maxX) * 0.5; e.y = box.minY; e.z = (box.minZ + box.maxZ) * 0.5;
        e.onGround = dy != e.vy && e.vy < 0;
        if (dx != e.vx) e.vx = 0;
        if (dy != e.vy) e.vy = 0;
        if (dz != e.vz) e.vz = 0;

        uint8_t under = w.Get((int)std::floor(e.x), (int)std::floor(e.y - 0.5000001), (int)std::floor(e.z));
        double f = e.onGround ? Slipperiness(under) * 0.98 : 0.98;
        e.vx *= f; e.vy *= 0.98; e.vz *= f;

        if (canPickUp && e.pickupDelay == 0 && Overlaps(reach, e.Box()))
            e.stack.count -= inv.Add(e.stack);
    }

    // Matching stacks lying next to each other combine into one
    for (size_t i = 0; i < items.size(); ++i)
        for (size_t j = i + 1; j < items.size(); ++j) {
            ItemEntity &a = items[i], &b = items[j];
            if (a.stack.count <= 0 || b.stack.count <= 0 || !CanMerge(a.stack, b.stack)) continue;
            if (a.stack.count + b.stack.count > MaxStack(a.stack.item)) continue;
            if (std::fabs(a.x - b.x) > 0.75 || std::fabs(a.z - b.z) > 0.75 || std::fabs(a.y - b.y) > 0.25) continue;
            a.stack.count += b.stack.count;
            a.pickupDelay = std::max(a.pickupDelay, b.pickupDelay);
            a.age = std::min(a.age, b.age);
            b.stack.count = 0;
        }

    items.erase(std::remove_if(items.begin(), items.end(), [](const ItemEntity& e) {
        return e.stack.count <= 0 || e.age >= ITEM_LIFETIME || e.y < -64.0;
    }), items.end());
}

} // namespace game
