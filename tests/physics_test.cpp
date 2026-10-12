// Checks that game.h reproduces the Minecraft Wiki numbers.
// Build: g++ -std=c++17 -O2 -I../src physics_test.cpp -o physics_test
#include "game.h"
#include "recipes.h"
#include <cstdio>
#include <fstream>
#include <initializer_list>

using namespace game;

static void FlatWorld(World& w) {
    for (int x = 0; x < WX; ++x)
        for (int z = 0; z < WZ; ++z) {
            for (int y = 0; y < 64; ++y) w.SetRaw(x, y, z, STONE);
            w.RecomputeTop(x, z);
        }
}

static Player Fresh(const World& w) {
    Player p; p.x = 20.5; p.y = 64; p.z = 128.5; p.yaw = 0; // facing +X
    PlayerInput none;
    for (int i = 0; i < 5; ++i) TickPlayer(w, p, none);    // settle onto the ground
    return p;
}

static double MeasureSpeed(const World& w, PlayerInput in, bool creativeFly = false) {
    Player p = Fresh(w);
    if (creativeFly) { p.creative = true; p.flying = true; p.y = 80; }
    if (in.sprint) { TickPlayer(w, p, in); }
    for (int i = 0; i < 100; ++i) TickPlayer(w, p, in);
    double x0 = p.x, z0 = p.z;
    for (int i = 0; i < 20; ++i) TickPlayer(w, p, in);
    return std::sqrt((p.x - x0) * (p.x - x0) + (p.z - z0) * (p.z - z0));   // per second
}

int main() {
    World w; FlatWorld(w);
    int fails = 0;
    auto check = [&](const char* name, double got, double want, double tol) {
        bool ok = std::fabs(got - want) <= tol;
        std::printf("%-28s %9.4f  (wiki %.4f)  %s\n", name, got, want, ok ? "OK" : "FAIL");
        if (!ok) ++fails;
    };

    PlayerInput walk; walk.forward = true;
    check("walk m/s", MeasureSpeed(w, walk), 4.317, 0.01);

    PlayerInput sprint = walk; sprint.sprint = true;
    check("sprint m/s", MeasureSpeed(w, sprint), 5.612, 0.01);

    PlayerInput sneak = walk; sneak.sneak = true;
    check("sneak m/s", MeasureSpeed(w, sneak), 1.295, 0.01);

    PlayerInput diag = walk; diag.right = true;
    check("walk diagonal m/s", MeasureSpeed(w, diag), 4.405, 0.01);

    PlayerInput sj = sprint; sj.jump = true;
    check("sprint-jump avg m/s", MeasureSpeed(w, sj), 7.127, 0.08);

    check("creative fly m/s", MeasureSpeed(w, walk, true), 10.89, 0.02);

    // Jump apex
    {
        Player p = Fresh(w);
        double y0 = p.y, maxY = p.y;
        PlayerInput j; j.jump = true;
        TickPlayer(w, p, j);
        PlayerInput none;
        int air = 1;
        for (int i = 0; i < 40; ++i) { TickPlayer(w, p, none); maxY = std::max(maxY, p.y); if (!p.onGround) ++air; else break; }
        check("jump height blocks", maxY - y0, 1.2522, 0.001);
        std::printf("%-28s %9d ticks\n", "jump airtime", air);
    }

    // Sneak edge protection: walk toward a cliff, must stop at the edge
    {
        World c; for (int x = 0; x < 30; ++x) for (int z = 0; z < WZ; ++z) { for (int y = 0; y < 64; ++y) c.SetRaw(x, y, z, STONE); c.RecomputeTop(x, z); }
        Player p = Fresh(c); p.x = 25.5;
        for (int i = 0; i < 200; ++i) TickPlayer(c, p, sneak);
        bool ok = p.y == 64.0 && p.x > 29.5 && p.x < 30.31;
        std::printf("%-28s x=%.3f y=%.3f  %s\n", "sneak edge stop", p.x, p.y, ok ? "OK" : "FAIL");
        if (!ok) ++fails;
    }

    // Break times by hand, seconds (wiki values)
    struct { uint8_t b; double s; } bt[] = {
        {DIRT, 0.75}, {GRASS, 0.9}, {STONE, 7.5}, {COBBLESTONE, 10.0}, {LOG, 3.0},
        {PLANKS, 3.0}, {SAND, 0.75}, {GLASS, 0.45}, {LEAVES, 0.3}, {BRICKS, 10.0},
        {COAL_ORE, 15.0}, {IRON_ORE, 15.0}};
    for (auto& e : bt) {
        char name[64]; std::snprintf(name, sizeof name, "break %s s", Info(e.b).name);
        check(name, BreakTicks(e.b) / 20.0, e.s, 1e-9);
    }
    std::printf("%-28s %9d (should be -1)\n", "break Bedrock ticks", BreakTicks(BEDROCK));

    // ---- Survival ----
    PlayerInput none;

    // Fall damage = ceil(blocks fallen - 3)
    for (int h : {3, 4, 10, 22}) {
        Player p = Fresh(w);
        p.y = 64.0 + h;
        for (int i = 0; i < 100; ++i) TickPlayer(w, p, none);
        char name[64]; std::snprintf(name, sizeof name, "fall %d blocks: damage", h);
        check(name, MAX_HEALTH - p.health, std::max(0, h - 3), 1e-9);
    }

    // Landing in water cancels fall damage
    {
        World wt;
        for (int x = 0; x < WX; ++x)
            for (int z = 0; z < WZ; ++z) {
                for (int y = 0; y < 64; ++y) wt.SetRaw(x, y, z, STONE);
                for (int y = 64; y < 67; ++y) wt.SetRaw(x, y, z, WATER);
                wt.RecomputeTop(x, z);
            }
        Player p; p.x = 128.5; p.y = 90; p.z = 128.5;
        for (int i = 0; i < 200; ++i) TickPlayer(wt, p, none);
        check("fall 26 into water: damage", MAX_HEALTH - p.health, 0, 1e-9);
    }

    // Drowning: 15 s of air, then 2 damage at 16 s
    {
        World wt;
        for (int x = 0; x < WX; ++x)
            for (int z = 0; z < WZ; ++z) {
                for (int y = 0; y < 64; ++y) wt.SetRaw(x, y, z, STONE);
                for (int y = 64; y < 80; ++y) wt.SetRaw(x, y, z, WATER);
                wt.RecomputeTop(x, z);
            }
        Player p; p.x = 128.5; p.y = 64; p.z = 128.5;
        for (int i = 0; i < 319; ++i) { TickPlayer(wt, p, none); TickSurvival(wt, p); }
        check("drown: health at 15.95 s", p.health, 20, 1e-9);
        TickPlayer(wt, p, none); TickSurvival(wt, p);
        check("drown: health at 16 s", p.health, 18, 1e-9);
    }

    // Hunger: 30 s of sprinting (~168 m * 0.1 = 16.8 exhaustion) burns 4 saturation
    {
        Player p = Fresh(w);
        for (int i = 0; i < 600; ++i) { TickPlayer(w, p, sprint); TickSurvival(w, p); }
        check("sprint 30 s: saturation", p.saturation, 1.0, 1e-9);
        check("sprint 30 s: food", p.food, 20, 1e-9);
        // Walking is free
        Player q = Fresh(w);
        for (int i = 0; i < 600; ++i) { TickPlayer(w, q, walk); TickSurvival(w, q); }
        check("walk 30 s: exhaustion", q.exhaustion, 0.0, 1e-9);
    }

    // Low food stops sprinting; starving stops at half a heart
    {
        Player p = Fresh(w); p.food = 6;
        for (int i = 0; i < 20; ++i) TickPlayer(w, p, sprint);
        bool ok = !p.sprinting;
        std::printf("%-28s %9s  %s\n", "no sprint at food 6", p.sprinting ? "sprinting" : "walking", ok ? "OK" : "FAIL");
        if (!ok) ++fails;

        Player s = Fresh(w); s.food = 0; s.saturation = 0;
        for (int i = 0; i < 80 * 30; ++i) { TickPlayer(w, s, none); TickSurvival(w, s); }
        check("starve 2 min: health", s.health, 1.0, 1e-9);
    }

    // Apple: +4 food, +2.4 saturation
    {
        Player p; p.food = 10; p.saturation = 0;
        Eat(p, APPLE_NUTRITION, APPLE_SATURATION);
        check("apple: food", p.food, 14, 1e-9);
        check("apple: saturation", p.saturation, 2.4, 1e-9);
    }

    // ---- Drops and items ----
    {
        bool ok = BlockDrop(GRASS).item == DIRT && BlockDrop(LOG).item == LOG && BlockDrop(LOG).count == 1 &&
                  BlockDrop(PLANKS).item == PLANKS && BlockDrop(SAND).item == SAND &&
                  BlockDrop(STONE).count == 0 && BlockDrop(GLASS).count == 0 && BlockDrop(BEDROCK).count == 0;
        int apples = 0;
        for (int i = 0; i < 100000; ++i) { Stack s = BlockDrop(LEAVES); if (s.count) { ++apples; ok = ok && s.item == APPLE; } }
        std::printf("%-28s %9s  %s\n", "drop table", ok ? "as wiki" : "wrong", ok ? "OK" : "FAIL");
        if (!ok) ++fails;
        check("leaves apple chance %", apples / 1000.0, 0.5, 0.1);

        Inventory inv;
        inv.Add(DIRT, 70);
        ok = inv.slots[0].item == DIRT && inv.slots[0].count == 64 && inv.slots[1].count == 6;
        std::printf("%-28s %9s  %s\n", "stacking 70 dirt", ok ? "64 + 6" : "wrong", ok ? "OK" : "FAIL");
        if (!ok) ++fails;

        // Break a log next to the player: it falls, lands, and gets picked up
        Player p = Fresh(w);
        Inventory hand;
        std::vector<ItemEntity> items;
        DropFromBlock(items, (int)p.x, 64, (int)p.z, {LOG, 1});
        int pickedAt = -1;
        for (int i = 0; i < 40 && pickedAt < 0; ++i) {
            TickItems(w, items, p, hand, true);
            if (items.empty()) pickedAt = i + 1;
        }
        ok = pickedAt == 10 && hand.slots[0].item == LOG && hand.slots[0].count == 1;
        std::printf("%-28s %9d ticks  %s\n", "log pickup", pickedAt, ok ? "OK" : "FAIL");
        if (!ok) ++fails;

        // Tools never stack
        Inventory tools;
        tools.Add(WOODEN_PICKAXE, 1); tools.Add(WOODEN_PICKAXE, 1);
        ok = tools.slots[0].count == 1 && tools.slots[1].count == 1;
        std::printf("%-28s %9s  %s\n", "tools don't stack", ok ? "1 + 1" : "merged", ok ? "OK" : "FAIL");
        if (!ok) ++fails;

        // Two stacks dropped on the same spot merge into one entity
        std::vector<ItemEntity> pile;
        for (int i = 0; i < 2; ++i) SpawnItem(pile, 50.5, 64, 50.5, 0, 0, 0, {DIRT, 3}, 10);
        TickItems(w, pile, p, hand, false);
        ok = pile.size() == 1 && pile[0].stack.count == 6;
        std::printf("%-28s %9d left  %s\n", "item merge", (int)pile.size(), ok ? "OK" : "FAIL");
        if (!ok) ++fails;
    }

    // ---- Tools (wiki break times, seconds) ----
    {
        struct { uint8_t b, tool; double s; } tt[] = {
            {STONE, WOODEN_PICKAXE, 1.15}, {STONE, STONE_PICKAXE, 0.6}, {COBBLESTONE, WOODEN_PICKAXE, 1.5},
            {COAL_ORE, WOODEN_PICKAXE, 2.25}, {IRON_ORE, WOODEN_PICKAXE, 7.5}, {IRON_ORE, STONE_PICKAXE, 1.15},
            {LOG, WOODEN_AXE, 1.5}, {LOG, STONE_AXE, 0.75}, {PLANKS, WOODEN_AXE, 1.5},
            {DIRT, WOODEN_SHOVEL, 0.4}, {GRASS, WOODEN_SHOVEL, 0.45}, {SAND, STONE_SHOVEL, 0.2},
            {LEAVES, WOODEN_SWORD, 0.2}, {CRAFTING_TABLE, WOODEN_AXE, 1.9}, {CRAFTING_TABLE, AIR, 3.75},
            {DIRT, WOODEN_PICKAXE, 0.75},   // wrong tool: same as bare hand
        };
        for (auto& e : tt) {
            char name[64];
            std::snprintf(name, sizeof name, "%s: %s s", e.tool ? ItemName(e.tool) : "Hand", Info(e.b).name);
            check(name, BreakTicks(e.b, e.tool) / 20.0, e.s, 1e-9);
        }

        bool ok = BlockDrop(STONE, WOODEN_PICKAXE).item == COBBLESTONE && BlockDrop(COAL_ORE, WOODEN_PICKAXE).item == COAL &&
                  BlockDrop(IRON_ORE, WOODEN_PICKAXE).count == 0 && BlockDrop(IRON_ORE, STONE_PICKAXE).item == IRON_ORE &&
                  BlockDrop(STONE, WOODEN_AXE).count == 0 && BlockDrop(CRAFTING_TABLE).item == CRAFTING_TABLE;
        std::printf("%-28s %9s  %s\n", "drops with tools", ok ? "as wiki" : "wrong", ok ? "OK" : "FAIL");
        if (!ok) ++fails;

        Stack pick{WOODEN_PICKAXE, 1};
        int uses = 0;
        while (pick.count > 0 && uses < 1000) { WearTool(pick, 1); ++uses; }
        check("wooden pickaxe uses", uses, 59, 0);
        Stack spick{STONE_PICKAXE, 1};
        uses = 0;
        while (spick.count > 0 && uses < 1000) { WearTool(spick, 1); ++uses; }
        check("stone pickaxe uses", uses, 131, 0);
    }

    // ---- Crafting (loads the real assets/recipes.json) ----
    {
        RecipeBook book;
        std::string log;
        bool loaded = book.Load(std::string(MYENGINE_ASSETS) + "/recipes.json", log);
        std::printf("%s", log.c_str());
        bool ok = loaded && log.empty() && book.recipes.size() == 23;
        std::printf("%-28s %9d recipes  %s\n", "recipes.json loads", (int)book.recipes.size(), ok ? "OK" : "FAIL");
        if (!ok) ++fails;

        // Fills an n x n grid from rows of characters using a little legend
        auto craft = [&](int n, std::initializer_list<const char*> rows) {
            Stack grid[9];
            int r = 0;
            for (const char* row : rows) {
                for (int c = 0; row[c]; ++c) {
                    uint8_t it = AIR;
                    switch (row[c]) {
                        case 'L': it = LOG; break;   case 'P': it = PLANKS; break;
                        case 'S': it = STICK; break; case 'C': it = COBBLESTONE; break;
                        case 'D': it = DIRT; break;  case 'K': it = COAL; break;
                        default: break;
                    }
                    if (it) grid[r * n + c] = {it, 1};
                }
                ++r;
            }
            const Recipe* rc = book.Match(grid, n);
            return rc ? rc->result : Stack{};
        };
        auto expect = [&](const char* name, Stack got, uint8_t item, int count) {
            bool good = got.item == item && got.count == count;
            std::printf("%-28s %9s x%d  %s\n", name, got.count ? ItemKey(got.item) : "nothing", got.count, good ? "OK" : "FAIL");
            if (!good) ++fails;
        };
        expect("log -> planks (2x2)",       craft(2, {".L", ".."}), PLANKS, 4);
        expect("planks -> sticks (2x2)",    craft(2, {"P.", "P."}), STICK, 4);
        expect("sticks, right column 3x3",  craft(3, {"...", "..P", "..P"}), STICK, 4);
        expect("crafting table",            craft(2, {"PP", "PP"}), CRAFTING_TABLE, 1);
        expect("torches (coal over stick)", craft(2, {".K", ".S"}), TORCH, 4);
        expect("wooden pickaxe",            craft(3, {"PPP", ".S.", ".S."}), WOODEN_PICKAXE, 1);
        expect("wooden axe",                craft(3, {"PP.", "PS.", ".S."}), WOODEN_AXE, 1);
        expect("wooden axe, mirrored",      craft(3, {".PP", ".SP", ".S."}), WOODEN_AXE, 1);
        expect("wooden shovel",             craft(3, {".P.", ".S.", ".S."}), WOODEN_SHOVEL, 1);
        expect("wooden sword",              craft(3, {"..P", "..P", "..S"}), WOODEN_SWORD, 1);
        expect("stone pickaxe",             craft(3, {"CCC", ".S.", ".S."}), STONE_PICKAXE, 1);
        expect("pickaxe needs 3x3",         craft(2, {"PP", "S."}), AIR, 0);
        expect("mixed head: nothing",       craft(3, {"PCP", ".S.", ".S."}), AIR, 0);
        expect("extra item: nothing",       craft(3, {"PPP", ".S.", "DS."}), AIR, 0);
        expect("empty grid: nothing",       craft(3, {"...", "...", "..."}), AIR, 0);

        // Broken data is reported, not fatal
        const char* bad = "{\"recipes\": [{\"pattern\": [\"#\"], \"key\": {\"#\": \"netherite\"}, \"result\": \"stick\"},"
                          " {\"type\": \"shapeless\", \"ingredients\": [\"dirt\"], \"result\": {\"item\": \"stick\", \"count\": 2}}]}";
        std::string path = "physics_test_bad_recipes.json";
        { std::ofstream f(path); f << bad; }
        RecipeBook b2;
        std::string log2;
        b2.Load(path, log2);
        std::remove(path.c_str());
        ok = b2.recipes.size() == 1 && log2.find("unknown item \"netherite\"") != std::string::npos;
        std::printf("%-28s %9s  %s\n", "bad recipe skipped", ok ? "reported" : "missed", ok ? "OK" : "FAIL");
        if (!ok) ++fails;
    }

    // ---- Light ----
    {
        // Solid stone up to y=63 everywhere, a sealed 9x4x9 room at y 40..43
        World lw;
        for (int x = 0; x < WX; ++x)
            for (int z = 0; z < WZ; ++z) {
                for (int y = 0; y < 64; ++y) lw.SetRaw(x, y, z, STONE);
                lw.RecomputeTop(x, z);
            }
        for (int x = 100; x < 109; ++x)
            for (int z = 100; z < 109; ++z)
                for (int y = 40; y < 44; ++y) lw.Set(x, y, z, AIR);
        RelightAll(lw);
        check("sky light on the surface", lw.SkyLight(50, 64, 50), 15, 0);
        check("sky light in a sealed room", lw.SkyLight(104, 41, 104), 0, 0);

        // A torch in the room: 14 at the torch, one less per block
        lw.Set(104, 40, 104, TORCH);
        RelightAround(lw, 104, 40, 104);
        check("torch: light at torch", lw.BlockLight(104, 40, 104), 14, 0);
        check("torch: 1 block away", lw.BlockLight(105, 40, 104), 13, 0);
        check("torch: 4 blocks away", lw.BlockLight(104, 40, 108), 10, 0);
        check("torch: 3 + 2 + 1 away", lw.BlockLight(107, 42, 105), 8, 0);
        check("torch: none through rock", lw.BlockLight(110, 40, 104), 0, 0);

        // Dig a shaft from the surface down into the room, one block at a time
        for (int y = 63; y >= 44; --y) {
            lw.Set(100, y, 100, AIR);
            RelightAround(lw, 100, y, 100);
        }
        check("shaft: full sky at the bottom", lw.SkyLight(100, 40, 100), 15, 0);
        check("shaft: sky spreads into room", lw.SkyLight(102, 40, 100), 13, 0);

        // Remove the torch, add a wall torch, block the shaft again...
        lw.Set(104, 40, 104, AIR);              RelightAround(lw, 104, 40, 104);
        lw.Set(108, 42, 104, WALL_TORCH_NX);    RelightAround(lw, 108, 42, 104);
        lw.Set(100, 50, 100, DIRT);             RelightAround(lw, 100, 50, 100);
        lw.Set(101, 40, 101, PLANKS);           RelightAround(lw, 101, 40, 101);
        check("blocked shaft: room dark again", lw.SkyLight(104, 41, 104), 0, 0);
        check("wall torch lights the room", lw.BlockLight(107, 42, 104), 13, 0);

        // ...and after all that, local updates must match a full relight
        std::vector<uint8_t> incremental = lw.light;
        RelightAll(lw);
        size_t diff = 0;
        for (size_t i = 0; i < incremental.size(); ++i) diff += incremental[i] != lw.light[i];
        check("local relight == full relight", (double)diff, 0, 0);

        // Leaves and water dim light but don't block it
        World tw;
        for (int x = 0; x < WX; ++x)
            for (int z = 0; z < WZ; ++z) { for (int y = 0; y < 64; ++y) tw.SetRaw(x, y, z, STONE); tw.RecomputeTop(x, z); }
        for (int x = 120; x < 137; ++x)
            for (int z = 120; z < 137; ++z) tw.SetRaw(x, 70, z, LEAVES);
        RelightAll(tw);
        check("under a leaf canopy (center)", tw.SkyLight(128, 64, 128), 8, 0);   // 15 -1 into the leaves, -6 down to the ground
    }

    // ---- Torches ----
    {
        bool ok = TorchForFace(0, 1, 0) == TORCH && TorchForFace(1, 0, 0) == WALL_TORCH_PX &&
                  TorchForFace(0, 0, -1) == WALL_TORCH_NZ && TorchForFace(0, -1, 0) == AIR;
        int sx, sy, sz;
        TorchSupport(WALL_TORCH_PX, 10, 20, 30, sx, sy, sz);
        ok = ok && sx == 9 && sy == 20 && sz == 30;
        TorchSupport(TORCH, 10, 20, 30, sx, sy, sz);
        ok = ok && sx == 10 && sy == 19 && sz == 30;
        ok = ok && BlockDrop(WALL_TORCH_NZ).item == TORCH && BreakTicks(TORCH) == 0 && !CanHoldTorch(LEAVES);
        std::printf("%-28s %9s  %s\n", "torch placement rules", ok ? "as wiki" : "wrong", ok ? "OK" : "FAIL");
        if (!ok) ++fails;

        // The ray passes beside a torch but hits it dead on
        World rw;
        rw.SetRaw(10, 10, 10, TORCH);
        RayHit beside = Raycast(rw, 10.1, 10.3, 5.0, 0, 0, 1, 10);
        RayHit center = Raycast(rw, 10.5, 10.3, 5.0, 0, 0, 1, 10);
        ok = !beside.hit && center.hit && center.x == 10 && center.nz == -1 && std::fabs(center.dist - 5.375) < 1e-9;
        std::printf("%-28s %9s  %s\n", "torch hitbox", ok ? "small" : "wrong", ok ? "OK" : "FAIL");
        if (!ok) ++fails;
    }

    // ---- World generation: ores and caves ----
    {
        World g1; Generate(g1, 1337);
        long count[BLOCK_COUNT] = {};
        int lo[BLOCK_COUNT], hi[BLOCK_COUNT];
        for (int i = 0; i < BLOCK_COUNT; ++i) { lo[i] = WY; hi[i] = -1; }
        long caveAir = 0, hangingWater = 0;
        for (int x = 0; x < WX; ++x)
            for (int z = 0; z < WZ; ++z)
                for (int y = 0; y < WY; ++y) {
                    uint8_t b = g1.Get(x, y, z);
                    ++count[b];
                    lo[b] = std::min(lo[b], y); hi[b] = std::max(hi[b], y);
                    if (b == AIR && y < 50) ++caveAir;
                    if (b == WATER && g1.Get(x, y - 1, z) == AIR) ++hangingWater;
                }
        for (uint8_t b : {COAL_ORE, IRON_ORE, DIAMOND_ORE, EMERALD_ORE})
            std::printf("%-28s %9ld blocks, y %d..%d\n", Info(b).name, count[b], lo[b], hi[b]);
        std::printf("%-28s %9ld blocks below y=50\n", "cave air", caveAir);

        bool ok = count[COAL_ORE] > 20000 && count[IRON_ORE] > 10000 && count[DIAMOND_ORE] > 300 && count[EMERALD_ORE] > 50;
        ok = ok && hi[IRON_ORE] <= 66 && hi[DIAMOND_ORE] <= 19 && lo[EMERALD_ORE] >= 4 && hi[EMERALD_ORE] <= 31;
        std::printf("%-28s %9s  %s\n", "ore amounts and depths", ok ? "in range" : "wrong", ok ? "OK" : "FAIL");
        if (!ok) ++fails;
        ok = caveAir > 50000;
        std::printf("%-28s %9s  %s\n", "caves carved", ok ? "yes" : "no", ok ? "OK" : "FAIL");
        if (!ok) ++fails;
        check("water hanging over a cave", (double)hangingWater, 0, 0);

        // Same seed, same world; different seed, different world
        World g2; Generate(g2, 1337);
        World g3; Generate(g3, 1338);
        ok = g1.blocks == g2.blocks && g1.blocks != g3.blocks;
        std::printf("%-28s %9s  %s\n", "seeds are repeatable", ok ? "yes" : "no", ok ? "OK" : "FAIL");
        if (!ok) ++fails;

        // Diamonds and emeralds need an iron pickaxe (Minecraft's rule)
        ok = BlockDrop(DIAMOND_ORE, STONE_PICKAXE).count == 0 && BlockDrop(EMERALD_ORE, STONE_PICKAXE).count == 0;
        std::printf("%-28s %9s  %s\n", "diamond needs iron pickaxe", ok ? "yes" : "no", ok ? "OK" : "FAIL");
        if (!ok) ++fails;
        check("Stone Pickaxe: Diamond Ore s", BreakTicks(DIAMOND_ORE, STONE_PICKAXE) / 20.0, 3.75, 1e-9);
        check("Hand: Diamond Ore s", BreakTicks(DIAMOND_ORE) / 20.0, 15.0, 1e-9);
    }

    // ---- Iron tools and the furnace ----
    {
        struct { uint8_t b, tool; double s; } tt[] = {
            {DIAMOND_ORE, IRON_PICKAXE, 0.75}, {EMERALD_ORE, IRON_PICKAXE, 0.75}, {STONE, IRON_PICKAXE, 0.4},
            {LOG, IRON_AXE, 0.5}, {DIRT, IRON_SHOVEL, 0.15}, {FURNACE, WOODEN_PICKAXE, 2.65}, {FURNACE, AIR, 17.5},
        };
        for (auto& e : tt) {
            char name[64];
            std::snprintf(name, sizeof name, "%s: %s s", e.tool ? ItemName(e.tool) : "Hand", Info(e.b).name);
            check(name, BreakTicks(e.b, e.tool) / 20.0, e.s, 1e-9);
        }
        bool ok = BlockDrop(DIAMOND_ORE, IRON_PICKAXE).item == DIAMOND && BlockDrop(EMERALD_ORE, IRON_PICKAXE).item == EMERALD &&
                  BlockDrop(LIT_FURNACE_NZ, WOODEN_PICKAXE).item == FURNACE && BlockDrop(FURNACE).count == 0;
        std::printf("%-28s %9s  %s\n", "iron pickaxe gets diamonds", ok ? "yes" : "no", ok ? "OK" : "FAIL");
        if (!ok) ++fails;
        Stack ip{IRON_PICKAXE, 1};
        int uses = 0;
        while (ip.count > 0 && uses < 1000) { WearTool(ip, 1); ++uses; }
        check("iron pickaxe uses", uses, 250, 0);
        ok = FacingToward(1, 0.2) == 1 && FacingToward(-1, 0) == 0 && FacingToward(0.1, 1) == 3 && FacingToward(0, -1) == 2;
        std::printf("%-28s %9s  %s\n", "furnace faces the player", ok ? "yes" : "no", ok ? "OK" : "FAIL");
        if (!ok) ++fails;

        RecipeBook book;
        std::string log;
        book.Load(std::string(MYENGINE_ASSETS) + "/recipes.json", log);

        // 1 coal, 8 iron ore: an ingot every 10 s, 8 ingots from one coal
        FurnaceState f;
        f.input = {IRON_ORE, 8};
        f.fuel = {COAL, 1};
        int firstIngot = -1, ticks = 0;
        for (; ticks < 20000 && f.input.count > 0; ++ticks) {
            TickFurnace(f, book);
            if (firstIngot < 0 && f.output.count == 1) firstIngot = ticks + 1;
        }
        check("smelt: first ingot (ticks)", firstIngot, 200, 0);
        ok = f.output.item == IRON_INGOT && f.output.count == 8 && f.fuel.count == 0 && ticks == 1600;
        std::printf("%-28s %9d ingots  %s\n", "1 coal smelts 8 iron ore", f.output.count, ok ? "OK" : "FAIL");
        if (!ok) ++fails;

        // Planks burn for 1.5 items; with nothing to smelt, fuel isn't wasted
        FurnaceState g;
        g.input = {SAND, 5};
        g.fuel = {PLANKS, 1};
        for (int i = 0; i < 1000; ++i) TickFurnace(g, book);
        ok = g.output.item == GLASS && g.output.count == 1 && g.input.count == 4 && !g.Lit();
        std::printf("%-28s %9d glass  %s\n", "1 plank smelts 1.5 sand", g.output.count, ok ? "OK" : "FAIL");
        if (!ok) ++fails;
        FurnaceState idle;
        idle.fuel = {COAL, 3};
        for (int i = 0; i < 500; ++i) TickFurnace(idle, book);
        ok = idle.fuel.count == 3 && !idle.Lit();
        std::printf("%-28s %9s  %s\n", "no input: fuel kept", ok ? "kept" : "burned", ok ? "OK" : "FAIL");
        if (!ok) ++fails;
        // A full output slot stops the furnace
        FurnaceState full;
        full.input = {COBBLESTONE, 2}; full.fuel = {COAL, 1}; full.output = {STONE, 64};
        for (int i = 0; i < 400; ++i) TickFurnace(full, book);
        ok = full.input.count == 2 && full.fuel.count == 1;
        std::printf("%-28s %9s  %s\n", "full output: furnace waits", ok ? "waits" : "runs", ok ? "OK" : "FAIL");
        if (!ok) ++fails;

        // A lit furnace glows (light 13), an unlit one doesn't
        World lw;
        for (int x = 0; x < WX; ++x)
            for (int z = 0; z < WZ; ++z) { for (int y = 0; y < 64; ++y) lw.SetRaw(x, y, z, STONE); lw.RecomputeTop(x, z); }
        for (int x = 100; x < 105; ++x) for (int z = 100; z < 105; ++z) for (int y = 40; y < 43; ++y) lw.SetRaw(x, y, z, AIR);
        lw.SetRaw(102, 40, 102, LIT_FURNACE_PX);
        RelightAll(lw);
        check("lit furnace: light next to it", lw.BlockLight(103, 40, 102), 12, 0);
        lw.Set(102, 40, 102, FURNACE_PX);
        RelightAround(lw, 102, 40, 102);
        check("unlit furnace: no light", lw.BlockLight(103, 40, 102), 0, 0);
    }

    // Real terrain generation sanity
    World g; Generate(g, 1337);
    Player sp; Spawn(g, sp);
    std::printf("spawn at %.1f %.1f %.1f, block below: %s\n", sp.x, sp.y, sp.z,
                Info(g.Get((int)sp.x, (int)sp.y - 1, (int)sp.z)).name);
    RayHit h = Raycast(g, sp.x, sp.y + EYE_HEIGHT, sp.z, 0, -1, 0, 4.5);
    std::printf("raycast down: hit=%d block=%s dist=%.3f normal=(%d,%d,%d)\n", h.hit,
                Info(g.Get(h.x, h.y, h.z)).name, h.dist, h.nx, h.ny, h.nz);

    std::printf("\n%s (%d failures)\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", fails);
    return fails ? 1 : 0;
}
