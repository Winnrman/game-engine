#pragma once
// ============================================================================
// recipes.h : crafting recipes, loaded from assets/recipes.json.
//
// Same idea as Minecraft's data pack recipes:
//   shaped     a pattern of up to 3x3 characters plus a key mapping each
//              character to an item. The pattern can sit anywhere in the
//              grid and may be mirrored left-to-right.
//   shapeless  a list of ingredients in any arrangement.
//   Anywhere an item goes, "#tag" means any item listed under that tag.
//   smelting   one ingredient cooked in a furnace (200 ticks unless
//              "cookingtime" says otherwise).
// ============================================================================
#include "game.h"
#include "json.h"
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <functional>
#include <algorithm>

namespace game {

// What may go in one slot: a single item, or any item of a tag like "#planks"
using Ingredient = std::vector<uint8_t>;
using TagMap = std::map<std::string, Ingredient>;

inline bool Accepts(const Ingredient& in, uint8_t item) {
    return std::find(in.begin(), in.end(), item) != in.end();
}

struct Recipe {
    std::string name;
    bool smelting = false;              // furnace recipe: `input` -> result
    Ingredient input;
    int  cookTime = 200;
    bool shaped = true;
    int  w = 0, h = 0;
    Ingredient cells[9];                // shaped: row-major w*h, empty = nothing there
    std::vector<Ingredient> ingredients;   // shapeless
    Stack result;
};

inline bool ParseItemRef(const json::Value* v, uint8_t& out, std::string& why) {
    if (!v || !v->IsString()) { why = "expected an item id string"; return false; }
    if (!ItemByKey(v->string.c_str(), out)) { why = "unknown item \"" + v->string + "\""; return false; }
    return true;
}

// "oak_planks" or "#planks" (any item listed under that tag)
inline bool ParseIngredient(const json::Value* v, const TagMap& tags, Ingredient& out, std::string& why) {
    out.clear();
    if (v && v->IsString() && !v->string.empty() && v->string[0] == '#') {
        auto it = tags.find(v->string.substr(1));
        if (it == tags.end()) { why = "unknown tag \"" + v->string + "\""; return false; }
        out = it->second;
        return true;
    }
    uint8_t id;
    if (!ParseItemRef(v, id, why)) return false;
    out.push_back(id);
    return true;
}

inline bool ParseRecipe(const json::Value& v, const TagMap& tags, Recipe& r, std::string& why) {
    if (!v.IsObject()) { why = "recipe must be an object"; return false; }

    // "result": "stick"  or  {"item": "stick", "count": 4}
    const json::Value* res = v.Find("result");
    const json::Value* resItem = res && res->IsObject() ? res->Find("item") : res;
    if (!ParseItemRef(resItem, r.result.item, why)) { why = "result: " + why; return false; }
    r.result.count = 1;
    if (res && res->IsObject()) {
        if (const json::Value* c = res->Find("count")) {
            if (!c->IsNumber()) { why = "result count must be a number"; return false; }
            r.result.count = (int)c->number;
        }
    }
    if (r.result.count < 1 || r.result.count > MaxStack(r.result.item)) {
        why = "result count must be 1.." + std::to_string(MaxStack(r.result.item));
        return false;
    }

    const json::Value* name = v.Find("name");
    r.name = name && name->IsString() ? name->string : ItemKey(r.result.item);

    const json::Value* type = v.Find("type");
    std::string t = type && type->IsString() ? type->string : (v.Find("pattern") ? "shaped" : "shapeless");

    if (t == "smelting") {
        r.smelting = true;
        if (!ParseIngredient(v.Find("ingredient"), tags, r.input, why)) { why = "ingredient: " + why; return false; }
        if (const json::Value* c = v.Find("cookingtime")) {
            if (!c->IsNumber() || c->number < 1) { why = "cookingtime must be a positive number of ticks"; return false; }
            r.cookTime = (int)c->number;
        }
        return true;
    }
    if (t == "shapeless") {
        r.shaped = false;
        const json::Value* list = v.Find("ingredients");
        if (!list || !list->IsArray() || list->array.empty() || list->array.size() > 9) {
            why = "shapeless recipes need 1-9 \"ingredients\"";
            return false;
        }
        for (const json::Value& e : list->array) {
            Ingredient in;
            if (!ParseIngredient(&e, tags, in, why)) { why = "ingredients: " + why; return false; }
            r.ingredients.push_back(in);
        }
        return true;
    }
    if (t != "shaped") { why = "type must be \"shaped\", \"shapeless\" or \"smelting\""; return false; }

    const json::Value* pattern = v.Find("pattern");
    const json::Value* key = v.Find("key");
    if (!pattern || !pattern->IsArray() || pattern->array.empty() || pattern->array.size() > 3) {
        why = "shaped recipes need a \"pattern\" of 1-3 rows";
        return false;
    }
    if (!key || !key->IsObject()) { why = "shaped recipes need a \"key\" object"; return false; }

    // Fill a 3x3 grid, then trim empty rows/columns so the pattern can float
    Ingredient grid[3][3];
    size_t width = 0;
    for (size_t r0 = 0; r0 < pattern->array.size(); ++r0) {
        const json::Value& row = pattern->array[r0];
        if (!row.IsString() || row.string.size() > 3) { why = "pattern rows must be strings of at most 3 characters"; return false; }
        if (r0 > 0 && row.string.size() != width) { why = "pattern rows must all be the same width"; return false; }
        width = row.string.size();
        for (size_t c = 0; c < row.string.size(); ++c) {
            char ch = row.string[c];
            if (ch == ' ') continue;
            const json::Value* item = key->Find(std::string(1, ch));
            if (!item) { why = std::string("pattern uses '") + ch + "' but the key doesn't define it"; return false; }
            if (!ParseIngredient(item, tags, grid[r0][c], why)) { why = std::string("key '") + ch + "': " + why; return false; }
        }
    }
    int x0 = 3, y0 = 3, x1 = -1, y1 = -1;
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            if (!grid[y][x].empty()) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
    if (x1 < 0) { why = "pattern is empty"; return false; }
    r.w = x1 - x0 + 1;
    r.h = y1 - y0 + 1;
    for (int y = 0; y < r.h; ++y)
        for (int x = 0; x < r.w; ++x) r.cells[y * r.w + x] = grid[y0 + y][x0 + x];
    return true;
}

// Can every item be given its own ingredient? (tiny backtracking search;
// tags mean one item could fit several ingredients)
inline bool MatchShapeless(const std::vector<Ingredient>& want, const std::vector<uint8_t>& items) {
    if (want.size() != items.size()) return false;
    std::vector<bool> used(want.size(), false);
    std::function<bool(size_t)> place = [&](size_t i) {
        if (i == items.size()) return true;
        for (size_t k = 0; k < want.size(); ++k)
            if (!used[k] && Accepts(want[k], items[i])) {
                used[k] = true;
                if (place(i + 1)) return true;
                used[k] = false;
            }
        return false;
    };
    return place(0);
}

struct RecipeBook {
    std::vector<Recipe> recipes;
    TagMap tags;

    // Problems (bad recipes are skipped, not fatal) are appended to `log`.
    // Returns false if the file can't be read or isn't valid JSON.
    bool Load(const std::string& path, std::string& log) {
        std::ifstream f(path, std::ios::binary);
        if (!f) { log += "can't open " + path + "\n"; return false; }
        std::stringstream ss;
        ss << f.rdbuf();

        json::Value root;
        std::string err;
        if (!json::Parse(ss.str(), root, err)) { log += path + ": " + err + "\n"; return false; }
        const json::Value* list = root.Find("recipes");
        if (!list || !list->IsArray()) { log += path + ": expected a top-level \"recipes\" array\n"; return false; }

        // "tags": {"planks": ["oak_planks", "spruce_planks", ...]}
        TagMap loadedTags;
        if (const json::Value* t = root.Find("tags")) {
            for (size_t i = 0; t->IsObject() && i < t->keys.size(); ++i) {
                Ingredient members;
                for (const json::Value& e : t->array[i].array) {
                    uint8_t id;
                    std::string why;
                    if (ParseItemRef(&e, id, why)) members.push_back(id);
                    else log += path + ": tag #" + t->keys[i] + ": " + why + " (skipped)\n";
                }
                loadedTags[t->keys[i]] = members;
            }
        }

        std::vector<Recipe> loaded;
        for (size_t i = 0; i < list->array.size(); ++i) {
            Recipe r;
            std::string why;
            if (ParseRecipe(list->array[i], loadedTags, r, why)) loaded.push_back(r);
            else log += path + ": recipe #" + std::to_string(i + 1) + ": " + why + " (skipped)\n";
        }
        recipes = std::move(loaded);
        tags = std::move(loadedTags);
        return true;
    }

    // What an n x n crafting grid (row-major) makes, or nullptr
    const Recipe* Match(const Stack* grid, int n) const {
        auto at = [&](int y, int x) { const Stack& s = grid[y * n + x]; return s.count > 0 ? s.item : (uint8_t)AIR; };
        int x0 = n, y0 = n, x1 = -1, y1 = -1;
        std::vector<uint8_t> items;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                if (uint8_t it = at(y, x)) {
                    items.push_back(it);
                    x0 = std::min(x0, x); x1 = std::max(x1, x);
                    y0 = std::min(y0, y); y1 = std::max(y1, y);
                }
        if (items.empty()) return nullptr;
        const int w = x1 - x0 + 1, h = y1 - y0 + 1;

        auto fits = [](const Ingredient& in, uint8_t g) { return in.empty() ? g == AIR : Accepts(in, g); };
        for (const Recipe& r : recipes) {
            if (r.smelting) continue;
            if (!r.shaped) {
                if (MatchShapeless(r.ingredients, items)) return &r;
                continue;
            }
            if (r.w != w || r.h != h) continue;
            bool same = true, mirrored = true;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    uint8_t g = at(y0 + y, x0 + x);
                    if (!fits(r.cells[y * w + x], g))           same = false;
                    if (!fits(r.cells[y * w + (w - 1 - x)], g)) mirrored = false;
                }
            if (same || mirrored) return &r;
        }
        return nullptr;
    }

    // The furnace recipe that cooks this item, or nullptr
    const Recipe* Smelting(uint8_t item) const {
        for (const Recipe& r : recipes)
            if (r.smelting && Accepts(r.input, item)) return &r;
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// A furnace's contents and fire, ticked like Minecraft's furnace block
// entity: fuel is only lit when there's something it can smelt, an item
// takes cookTime ticks, and progress slips back when the fire goes out.
// ---------------------------------------------------------------------------
struct FurnaceState {
    Stack input, fuel, output;
    int litTime = 0, litDuration = 0;   // ticks of fire left / total for the current fuel
    int cookTime = 0, cookTotal = 200;  // progress on the current item
    bool Lit() const { return litTime > 0; }
};

// Can the furnace put this recipe's result in its output slot?
inline bool CanSmelt(const FurnaceState& f, const Recipe* r) {
    if (!r || f.input.count <= 0) return false;
    if (f.output.count == 0) return true;
    return CanMerge(f.output, r->result) && f.output.count + r->result.count <= MaxStack(f.output.item);
}

// One tick. Returns true if the lit state changed (the block should swap).
inline bool TickFurnace(FurnaceState& f, const RecipeBook& book) {
    const bool wasLit = f.Lit();
    if (f.Lit()) --f.litTime;

    const Recipe* r = f.input.count > 0 ? book.Smelting(f.input.item) : nullptr;
    if (r) f.cookTotal = r->cookTime;
    if (f.Lit() || (f.fuel.count > 0 && f.input.count > 0)) {
        if (!f.Lit() && CanSmelt(f, r)) {
            int burn = FuelTicks(f.fuel.item);
            if (burn > 0) {
                f.litTime = f.litDuration = burn;
                if (--f.fuel.count == 0) f.fuel = {};
            }
        }
        if (f.Lit() && CanSmelt(f, r)) {
            if (++f.cookTime >= f.cookTotal) {
                f.cookTime = 0;
                if (f.output.count == 0) f.output = r->result;
                else f.output.count += r->result.count;
                if (--f.input.count == 0) f.input = {};
            }
        } else {
            f.cookTime = 0;
        }
    } else if (f.cookTime > 0) {
        f.cookTime = std::max(0, f.cookTime - 2);   // cools off without fire
    }
    return wasLit != f.Lit();
}

} // namespace game
