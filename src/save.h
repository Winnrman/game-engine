#pragma once
// ============================================================================
// save.h : saving and loading worlds.
//
// Each world is a folder under saves/:
//   world.dat            name, seed, time, spawn point, the player, their
//                        items, dropped items and furnaces
//   chunks/c.X.Z.dat     one file per generated chunk (blocks, run-length
//                        encoded); chunks stream in and out as you move
// Light, biome colors and column tops aren't stored: they're rebuilt from
// the blocks and the seed. Every file is written to a temp file first and
// then swapped in, so a crash mid-save can't leave a half-written world.
//
// Version 1 saves (the old fixed 256x256 worlds) are converted on load:
// their blocks become chunks 0..15, and the land keeps going past them.
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

constexpr char     SAVE_MAGIC[8]  = {'M', 'Y', 'E', 'N', 'G', 'W', 'L', 'D'};
constexpr char     CHUNK_MAGIC[8] = {'M', 'Y', 'E', 'N', 'G', 'C', 'H', 'K'};
constexpr uint32_t SAVE_VERSION   = 2;
constexpr int      AUTOSAVE_TICKS = 6000;   // every 5 minutes, like Minecraft

// What the world list shows (read from the header only, so it's quick)
struct SaveInfo {
    std::string dir, name;
    uint32_t  seed = 0;
    long long dayTime = 0, lastPlayed = 0;
    bool      creative = false;
};

// Everything about a world that isn't its chunks
struct SaveData {
    std::string name;
    long long dayTime = 1000, lastPlayed = 0;
    int       spawnX = 0, spawnZ = 0;
    Player    player;
    Inventory inv;
    uint8_t   creativeBar[9] = {};
    int       selected = 0;
    std::vector<ItemEntity> items;
    std::map<long long, FurnaceState> furnaces;   // by BlockKey
    bool      convertedFromV1 = false;             // loaded from an old 256x256 save
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
    void Raw(const char* p, size_t n) { bytes.insert(bytes.end(), p, p + n); }
    void Str(const std::string& s) { Put((uint32_t)s.size()); bytes.insert(bytes.end(), s.begin(), s.end()); }
    void PutStack(const Stack& s) { Put(s.item); Put((int32_t)s.count); Put((int32_t)s.damage); }
    // Run-length encoded bytes: (value, run length) pairs
    void Runs(const uint8_t* b, size_t n) {
        size_t at = bytes.size();
        Put((uint32_t)0);
        uint32_t runs = 0;
        for (size_t i = 0; i < n;) {
            size_t j = i;
            while (j < n && b[j] == b[i] && j - i < 65535) ++j;
            Put(b[i]);
            Put((uint16_t)(j - i));
            ++runs;
            i = j;
        }
        std::memcpy(&bytes[at], &runs, sizeof runs);
    }
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
    bool Magic(const char* m) {
        if (!ok || end - p < 8 || std::memcmp(p, m, 8) != 0) return false;
        p += 8;
        return true;
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
    // Decodes runs into out[0..n); false if they don't add up to exactly n
    bool Runs(uint8_t* out, size_t n, uint8_t maxValue) {
        uint32_t runs = Get<uint32_t>();
        size_t at = 0;
        for (uint32_t i = 0; i < runs && ok; ++i) {
            uint8_t b = Get<uint8_t>();
            uint16_t len = Get<uint16_t>();
            if (!ok || at + len > n) return ok = false;
            std::memset(out + at, b < maxValue ? b : 0, len);
            at += len;
        }
        return ok && at == n;
    }
};

inline bool ReadFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// Writes through a temp file so a crash can't leave a half-written file
inline bool WriteFileAtomic(const std::string& path, const std::vector<uint8_t>& bytes, std::string& err) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { err = "can't write " + tmp; return false; }
        f.write((const char*)bytes.data(), (std::streamsize)bytes.size());
        if (!f) { err = "write failed for " + tmp; return false; }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) { err = "can't replace " + path + ": " + ec.message(); return false; }
    return true;
}

// ---------------------------------------------------------------------------
// Chunks
// ---------------------------------------------------------------------------
inline std::string ChunkPath(const std::string& dir, int cx, int cz) {
    return dir + "/chunks/c." + std::to_string(cx) + "." + std::to_string(cz) + ".dat";
}

inline bool SaveChunk(const std::string& dir, Chunk& c, std::string& err) {
    ByteWriter w;
    w.Raw(CHUNK_MAGIC, 8);
    w.Put((uint32_t)1);
    w.Put((int32_t)c.cx);
    w.Put((int32_t)c.cz);
    w.Put(c.stage);
    w.Runs(c.blocks, sizeof c.blocks);
    std::error_code ec;
    std::filesystem::create_directories(dir + "/chunks", ec);
    if (!WriteFileAtomic(ChunkPath(dir, c.cx, c.cz), w.bytes, err)) return false;
    c.modified = false;
    return true;
}

// Biomes, colors and column tops for a chunk whose blocks just came from disk
inline void PrepareLoadedChunk(const World& w, Chunk& c) {
    ComputeChunkBiomes(ClimateNoise(w.seed), c);
    ComputeChunkTops(c);
    c.lit = false;
    c.modified = false;
}

// Loads a saved chunk into the world. False if there's no file (the chunk
// was never generated) or it's damaged (it's then generated afresh).
inline bool LoadChunk(const std::string& dir, World& w, int cx, int cz, std::string* problem = nullptr) {
    std::vector<uint8_t> bytes;
    if (!ReadFile(ChunkPath(dir, cx, cz), bytes)) return false;
    ByteReader r{bytes.data(), bytes.data() + bytes.size()};
    auto chunk = std::make_unique<Chunk>();
    bool ok = r.Magic(CHUNK_MAGIC) && r.Get<uint32_t>() == 1 && r.Get<int32_t>() == cx && r.Get<int32_t>() == cz;
    chunk->stage = r.Get<uint8_t>();
    ok = ok && r.ok && chunk->stage <= STAGE_DECORATED && r.Runs(chunk->blocks, sizeof chunk->blocks, BLOCK_COUNT);
    if (!ok) {
        if (problem) *problem = ChunkPath(dir, cx, cz) + " is damaged";
        return false;
    }
    Chunk& c = w.EnsureChunk(cx, cz);
    std::memcpy(c.blocks, chunk->blocks, sizeof c.blocks);
    c.stage = chunk->stage;
    PrepareLoadedChunk(w, c);
    return true;
}

// ---------------------------------------------------------------------------
// world.dat
// ---------------------------------------------------------------------------
inline void WriteHeader(ByteWriter& w, uint32_t seed, const SaveData& d) {
    w.Raw(SAVE_MAGIC, 8);
    w.Put(SAVE_VERSION);
    w.Str(d.name);
    w.Put(seed);
    w.Put((int64_t)d.dayTime);
    w.Put((int64_t)d.lastPlayed);
    w.Put((uint8_t)d.player.creative);
}

// Saves world.dat and every chunk changed since it was last saved
inline bool SaveWorld(const std::string& dir, World& world, const SaveData& d, std::string& err) {
    ByteWriter w;
    WriteHeader(w, world.seed, d);
    w.Put((int32_t)d.spawnX);
    w.Put((int32_t)d.spawnZ);

    const Player& p = d.player;
    for (double v : {p.x, p.y, p.z, p.vx, p.vy, p.vz, (double)p.yaw, (double)p.pitch,
                     p.health, p.saturation, p.exhaustion, p.fallDistance}) w.Put(v);
    for (int v : {p.food, p.foodTimer, p.air}) w.Put((int32_t)v);
    w.Put((uint8_t)p.flying);

    for (const Stack& s : d.inv.slots) w.PutStack(s);
    for (uint8_t b : d.creativeBar) w.Put(b);
    w.Put((int32_t)d.selected);

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
    if (!WriteFileAtomic(dir + "/world.dat", w.bytes, err)) return false;
    for (auto& kv : world.chunks)
        if (kv.second->modified && !SaveChunk(dir, *kv.second, err)) return false;
    return true;
}

inline bool ReadHeader(ByteReader& r, SaveInfo& info, uint32_t& version, std::string& err) {
    if (!r.Magic(SAVE_MAGIC)) { err = "not a MyEngine world"; return false; }
    version = r.Get<uint32_t>();
    if (version != 1 && version != SAVE_VERSION) { err = "saved by a different version (" + std::to_string(version) + ")"; return false; }
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
    uint32_t version;
    info.dir = dir;
    return ReadHeader(r, info, version, err);
}

// Loads world.dat. Chunks are loaded later, as they're needed (except for a
// version 1 save, whose 256x256 blocks are turned into chunks right away).
inline bool LoadWorld(const std::string& dir, World& world, SaveData& d, std::string& err) {
    std::vector<uint8_t> bytes;
    if (!ReadFile(dir + "/world.dat", bytes)) { err = "can't read " + dir + "/world.dat"; return false; }
    ByteReader r{bytes.data(), bytes.data() + bytes.size()};
    SaveInfo info;
    uint32_t version;
    if (!ReadHeader(r, info, version, err)) return false;
    d.name = info.name;
    d.dayTime = info.dayTime;
    d.lastPlayed = info.lastPlayed;
    world.seed = info.seed;
    if (version >= 2) {
        d.spawnX = r.Get<int32_t>();
        d.spawnZ = r.Get<int32_t>();
    } else {
        FindSpawnColumn(world.seed, d.spawnX, d.spawnZ);
    }

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

    d.convertedFromV1 = version == 1;
    if (version == 1) {
        // The old fixed world: 256 x 256 x 128 blocks, x-major then z then y
        const int OLD = 256;
        std::vector<uint8_t> old((size_t)OLD * OLD * WY);
        if (!r.Runs(old.data(), old.size(), BLOCK_COUNT)) { err = "block data is damaged"; return false; }
        for (int cx = 0; cx < OLD / CHUNK; ++cx)
            for (int cz = 0; cz < OLD / CHUNK; ++cz) {
                Chunk& c = world.EnsureChunk(cx, cz);
                for (int lx = 0; lx < CHUNK; ++lx)
                    for (int lz = 0; lz < CHUNK; ++lz)
                        std::memcpy(&c.blocks[Chunk::Idx(lx, 0, lz)],
                                    &old[((size_t)(cx * CHUNK + lx) * OLD + (cz * CHUNK + lz)) * WY], WY);
                c.stage = STAGE_DECORATED;
                PrepareLoadedChunk(world, c);
                c.modified = true;   // written out in the new format on the next save
            }
    }

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
        if (version == 1) {   // old key: (x * 256 + z) * 128 + y
            int y = (int)(key % WY), z = (int)((key / WY) % 256), x = (int)(key / WY / 256);
            key = BlockKey(x, y, z);
        }
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
