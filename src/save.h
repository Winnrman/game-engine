#pragma once
// ============================================================================
// save.h : saving and loading worlds.
//
// Each world is a folder under saves/ holding one file, world.dat:
//   header   magic, version, name, seed, time of day, last played, mode
//   player   position, look, health, hunger, breath, flying
//   items    36 inventory slots, the creative palette, the selected slot
//   blocks   every block, run-length encoded (long runs of air and stone)
//   entities dropped items and furnaces
// Light, biome colors and column tops aren't stored: they're rebuilt from
// the blocks and the seed on load. Writes go to a temp file first and are
// then swapped in, so a crash mid-save can't leave a half-written world.
// ============================================================================
#include "game.h"
#include "recipes.h"
#include <cctype>
#include <cstddef>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace game {

constexpr char     SAVE_MAGIC[8] = {'M', 'Y', 'E', 'N', 'G', 'W', 'L', 'D'};
constexpr uint32_t SAVE_VERSION  = 1;
constexpr int      AUTOSAVE_TICKS = 6000;   // every 5 minutes, like Minecraft

// What the world list shows (read from the header only, so it's quick)
struct SaveInfo {
    std::string dir, name;
    uint32_t  seed = 0;
    long long dayTime = 0, lastPlayed = 0;
    bool      creative = false;
};

// Everything about a world that isn't the blocks themselves
struct SaveData {
    std::string name;
    long long dayTime = 1000, lastPlayed = 0;
    Player    player;
    Inventory inv;
    uint8_t   creativeBar[9] = {};
    int       selected = 0;
    std::vector<ItemEntity> items;
    std::map<long long, FurnaceState> furnaces;
};

// ---------------------------------------------------------------------------
// Little binary writer/reader (little-endian, the native order here)
// ---------------------------------------------------------------------------
struct ByteWriter {
    std::vector<uint8_t> bytes;
    template <class T> void Put(const T& v) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
        bytes.insert(bytes.end(), p, p + sizeof(T));
    }
    void Str(const std::string& s) { Put((uint32_t)s.size()); bytes.insert(bytes.end(), s.begin(), s.end()); }
    void PutStack(const Stack& s) { Put(s.item); Put((int32_t)s.count); Put((int32_t)s.damage); }
};

struct ByteReader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok = true;
    template <class T> T Get() {
        T v{};
        if (!ok || end - p < (ptrdiff_t)sizeof(T)) { ok = false; return v; }
        std::memcpy(&v, p, sizeof(T));
        p += sizeof(T);
        return v;
    }
    std::string Str(size_t maxLen = 256) {
        uint32_t n = Get<uint32_t>();
        if (!ok || n > maxLen || end - p < (ptrdiff_t)n) { ok = false; return {}; }
        std::string s((const char*)p, n);
        p += n;
        return s;
    }
    // Unknown item ids (from a newer or damaged save) become empty slots
    Stack GetStack() {
        Stack s;
        s.item = Get<uint8_t>();
        s.count = Get<int32_t>();
        s.damage = Get<int32_t>();
        bool known = s.item < BLOCK_COUNT || ItemData(s.item);
        if (!known || s.count <= 0 || s.count > MaxStack(s.item) || s.damage < 0) return {};
        return s;
    }
};

inline bool ReadFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// ---------------------------------------------------------------------------
// Saving
// ---------------------------------------------------------------------------
inline void WriteHeader(ByteWriter& w, const World& world, const SaveData& d) {
    w.bytes.insert(w.bytes.end(), SAVE_MAGIC, SAVE_MAGIC + 8);
    w.Put(SAVE_VERSION);
    w.Str(d.name);
    w.Put(world.seed);
    w.Put((int64_t)d.dayTime);
    w.Put((int64_t)d.lastPlayed);
    w.Put((uint8_t)d.player.creative);
}

inline bool SaveWorld(const std::string& dir, const World& world, const SaveData& d, std::string& err) {
    ByteWriter w;
    WriteHeader(w, world, d);

    const Player& p = d.player;
    for (double v : {p.x, p.y, p.z, p.vx, p.vy, p.vz, (double)p.yaw, (double)p.pitch,
                     p.health, p.saturation, p.exhaustion, p.fallDistance}) w.Put(v);
    for (int v : {p.food, p.foodTimer, p.air}) w.Put((int32_t)v);
    w.Put((uint8_t)p.flying);

    for (const Stack& s : d.inv.slots) w.PutStack(s);
    for (uint8_t b : d.creativeBar) w.Put(b);
    w.Put((int32_t)d.selected);

    // Blocks: (block, run length) pairs over the whole array
    const std::vector<uint8_t>& b = world.blocks;
    size_t runsAt = w.bytes.size();
    w.Put((uint32_t)0);
    uint32_t runs = 0;
    for (size_t i = 0; i < b.size();) {
        size_t j = i;
        while (j < b.size() && b[j] == b[i] && j - i < 65535) ++j;
        w.Put(b[i]);
        w.Put((uint16_t)(j - i));
        ++runs;
        i = j;
    }
    std::memcpy(&w.bytes[runsAt], &runs, sizeof runs);

    w.Put((uint32_t)d.items.size());
    for (const ItemEntity& e : d.items) {
        for (double v : {e.x, e.y, e.z, e.vx, e.vy, e.vz}) w.Put(v);
        w.PutStack(e.stack);
        w.Put((int32_t)e.age);
        w.Put((int32_t)e.pickupDelay);
    }
    w.Put((uint32_t)d.furnaces.size());
    for (const auto& kv : d.furnaces) {
        w.Put((int64_t)kv.first);
        w.PutStack(kv.second.input);
        w.PutStack(kv.second.fuel);
        w.PutStack(kv.second.output);
        for (int v : {kv.second.litTime, kv.second.litDuration, kv.second.cookTime, kv.second.cookTotal}) w.Put((int32_t)v);
    }

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::string path = dir + "/world.dat", tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { err = "can't write " + tmp; return false; }
        f.write((const char*)w.bytes.data(), (std::streamsize)w.bytes.size());
        if (!f) { err = "write failed for " + tmp; return false; }
    }
    std::filesystem::rename(tmp, path, ec);   // replaces the old save in one step
    if (ec) { err = "can't replace " + path + ": " + ec.message(); return false; }
    return true;
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------
inline bool ReadHeader(ByteReader& r, SaveInfo& info, std::string& err) {
    char magic[8];
    for (char& c : magic) c = (char)r.Get<uint8_t>();
    if (!r.ok || std::memcmp(magic, SAVE_MAGIC, 8) != 0) { err = "not a MyEngine world"; return false; }
    uint32_t version = r.Get<uint32_t>();
    if (version != SAVE_VERSION) { err = "saved by a different version (" + std::to_string(version) + ")"; return false; }
    info.name = r.Str(64);
    info.seed = r.Get<uint32_t>();
    info.dayTime = r.Get<int64_t>();
    info.lastPlayed = r.Get<int64_t>();
    info.creative = r.Get<uint8_t>() != 0;
    if (!r.ok) { err = "header is cut short"; return false; }
    return true;
}

inline bool ReadSaveInfo(const std::string& dir, SaveInfo& info) {
    std::ifstream f(dir + "/world.dat", std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> head(512);
    f.read((char*)head.data(), (std::streamsize)head.size());
    head.resize((size_t)f.gcount());
    ByteReader r{head.data(), head.data() + head.size()};
    std::string err;
    info.dir = dir;
    return ReadHeader(r, info, err);
}

// Rebuilds what isn't saved: biomes and their colors, column tops, light
inline void RebuildDerived(World& w) {
    ClimateNoise noise(w.seed);
    for (int x = 0; x < WX; ++x)
        for (int z = 0; z < WZ; ++z) {
            int h;
            uint8_t biome;
            ShapeColumn(noise.At(x, z), h, biome);
            w.biome[World::Col(x, z)] = biome;
            w.RecomputeTop(x, z);
        }
    BlendBiomeColors(w);
    RelightAll(w);
}

inline bool LoadWorld(const std::string& dir, World& world, SaveData& d, std::string& err) {
    std::vector<uint8_t> bytes;
    if (!ReadFile(dir + "/world.dat", bytes)) { err = "can't read " + dir + "/world.dat"; return false; }
    ByteReader r{bytes.data(), bytes.data() + bytes.size()};
    SaveInfo info;
    if (!ReadHeader(r, info, err)) return false;
    d.name = info.name;
    d.dayTime = info.dayTime;
    d.lastPlayed = info.lastPlayed;
    world.seed = info.seed;

    Player& p = d.player;
    p = Player();
    p.creative = info.creative;
    double* dv[] = {&p.x, &p.y, &p.z, &p.vx, &p.vy, &p.vz};
    for (double* v : dv) *v = r.Get<double>();
    p.yaw = (float)r.Get<double>();
    p.pitch = (float)r.Get<double>();
    p.health = r.Get<double>();
    p.saturation = r.Get<double>();
    p.exhaustion = r.Get<double>();
    p.fallDistance = r.Get<double>();
    p.food = r.Get<int32_t>();
    p.foodTimer = r.Get<int32_t>();
    p.air = r.Get<int32_t>();
    p.flying = r.Get<uint8_t>() != 0;
    p.px = p.x; p.py = p.y; p.pz = p.z;

    for (Stack& s : d.inv.slots) s = r.GetStack();
    for (uint8_t& b : d.creativeBar) { b = r.Get<uint8_t>(); if (b >= BLOCK_COUNT) b = AIR; }
    d.selected = std::clamp(r.Get<int32_t>(), 0, 8);

    uint32_t runs = r.Get<uint32_t>();
    size_t at = 0;
    for (uint32_t i = 0; i < runs && r.ok; ++i) {
        uint8_t b = r.Get<uint8_t>();
        uint16_t n = r.Get<uint16_t>();
        if (!r.ok || at + n > world.blocks.size()) { r.ok = false; break; }
        std::memset(&world.blocks[at], b < BLOCK_COUNT ? b : AIR, n);
        at += n;
    }
    if (!r.ok || at != world.blocks.size()) { err = "block data is damaged"; return false; }

    uint32_t nItems = r.Get<uint32_t>();
    d.items.clear();
    for (uint32_t i = 0; i < nItems && i < 100000 && r.ok; ++i) {
        ItemEntity e;
        double* ev[] = {&e.x, &e.y, &e.z, &e.vx, &e.vy, &e.vz};
        for (double* v : ev) *v = r.Get<double>();
        e.px = e.x; e.py = e.y; e.pz = e.z;
        e.stack = r.GetStack();
        e.age = r.Get<int32_t>();
        e.pickupDelay = r.Get<int32_t>();
        e.bobOffset = (float)(Rand01() * 6.2831853);
        if (e.stack.count > 0) d.items.push_back(e);
    }
    uint32_t nFurnaces = r.Get<uint32_t>();
    d.furnaces.clear();
    for (uint32_t i = 0; i < nFurnaces && i < 100000 && r.ok; ++i) {
        long long key = r.Get<int64_t>();
        FurnaceState f;
        f.input = r.GetStack();
        f.fuel = r.GetStack();
        f.output = r.GetStack();
        f.litTime = r.Get<int32_t>();
        f.litDuration = r.Get<int32_t>();
        f.cookTime = r.Get<int32_t>();
        f.cookTotal = std::max(1, r.Get<int32_t>());
        d.furnaces[key] = f;
    }
    if (!r.ok) { err = "entity data is damaged"; return false; }

    RebuildDerived(world);
    return true;
}

// ---------------------------------------------------------------------------
// The saves folder
// ---------------------------------------------------------------------------
inline std::vector<SaveInfo> ListSaves(const std::string& root) {
    std::vector<SaveInfo> out;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (!entry.is_directory()) continue;
        SaveInfo info;
        if (ReadSaveInfo(entry.path().generic_string(), info)) out.push_back(info);
    }
    std::sort(out.begin(), out.end(), [](const SaveInfo& a, const SaveInfo& b) { return a.lastPlayed > b.lastPlayed; });
    return out;
}

// A folder name for a new world: the name with odd characters swapped out,
// plus "-2", "-3"... if it's taken
inline std::string NewWorldDir(const std::string& root, const std::string& name) {
    std::string base;
    for (char c : name) base += (std::isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_') ? c : '_';
    while (!base.empty() && base.back() == ' ') base.pop_back();
    if (base.empty()) base = "World";
    std::string dir = root + "/" + base;
    for (int n = 2; std::filesystem::exists(dir); ++n) dir = root + "/" + base + "-" + std::to_string(n);
    return dir;
}

// Minecraft's rule for the seed box: a number is used as is, other text is
// hashed (Java's String.hashCode), blank means random
inline bool SeedFromText(const std::string& text, uint32_t& seed) {
    std::string t = text;
    while (!t.empty() && t.back() == ' ') t.pop_back();
    while (!t.empty() && t.front() == ' ') t.erase(t.begin());
    if (t.empty()) return false;
    char* end = nullptr;
    long long v = std::strtoll(t.c_str(), &end, 10);
    if (end && *end == '\0') { seed = (uint32_t)v; return true; }
    uint32_t h = 0;   // same bits as Java's wrapping int arithmetic
    for (char c : t) h = 31 * h + (unsigned char)c;
    seed = h;
    return true;
}

} // namespace game
