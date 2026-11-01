-- GloamingForest - `xmake lua tools/families/run.lua <projectdir> <outdir> [stamp]` (tools/families/run.ps1).
--
-- Runs the block family generator (families.lua) on an assembled directory (bin/<target>/
-- GloamingForest, build/assemble-check, or a deployed copy) and checks what it wrote the way
-- tools/xmake/assemble.lua checks the packs: integer states list 0..n, the permutation budget
-- per block, and every geometry a generated block names exists in the resource pack.
-- "stamp" also raises the version of the packs it changed, so games never reuse a cached copy
-- of the pack without the generated blocks.

import("core.base.json")

local TRAIT_STATES = {
    ["minecraft:cardinal_direction"] = 4, ["minecraft:facing_direction"] = 6, ["minecraft:block_face"] = 6,
    ["minecraft:vertical_half"] = 2, ["minecraft:corner_and_cardinal_direction"] = 20,
}

local function try_json(file) return try { function () return json.loadfile(file) end } end

local function check_block(file, doc, geos, errors)
    local block = doc["minecraft:block"]
    local count = 1
    for name, values in pairs(block.description.states or {}) do
        if type(values) == "table" and #values > 0 then
            if type(values[1]) == "number" then
                for i, v in ipairs(values) do
                    if v ~= i - 1 then table.insert(errors, string.format("%s: state %s must list 0..%d", file, name, #values - 1)) break end
                end
            end
            count = count * #values
        end
    end
    for _, trait in pairs(block.description.traits or {}) do
        for _, s in ipairs(trait.enabled_states or {}) do count = count * (TRAIT_STATES[s] or 1) end
    end
    if count > 65536 then table.insert(errors, string.format("%s: %d permutations (budget 65536)", file, count)) end
    local function geo(g)
        local id = type(g) == "table" and g.identifier or g
        if type(id) == "string" and id:match("^geometry%.") and not geos[id] then
            table.insert(errors, string.format("%s: geometry %s is not in the resource pack", file, id))
        end
    end
    geo(block.components["minecraft:geometry"])
    for _, p in ipairs(block.permutations or {}) do geo(p.components["minecraft:geometry"]) end
    return count
end

local function geometries(rpdir)
    local out = {}
    if not os.isdir(rpdir) then return out end
    for _, f in ipairs(os.files(path.join(rpdir, "models", "**.json"))) do
        local doc = try_json(f)
        for _, g in ipairs(doc and doc["minecraft:geometry"] or {}) do
            local id = g.description and g.description.identifier
            if id then out[id] = true end
        end
    end
    return out
end

-- Raises the patch version of a pack manifest by a stamp of `files` (header and modules).
local function stamp_pack(packdir, files)
    local manifest = path.join(packdir, "manifest.json")
    local doc = os.isfile(manifest) and try_json(manifest) or nil
    if not doc or not doc.header or type(doc.header.version) ~= "table" then return end
    table.sort(files)
    local parts = {}
    for _, f in ipairs(files) do table.insert(parts, hash.md5(f)) end
    local tmp = os.tmpfile() .. ".familystamp"
    io.writefile(tmp, table.concat(parts, "\n"))
    local digest = hash.md5(tmp)
    os.tryrm(tmp)
    local add = tonumber(digest:sub(1, 6), 16) % 30000
    local v = doc.header.version
    local patch = ((v[3] or 0) + add) % 30000 + 2
    doc.header.version = {v[1], v[2], patch}
    for _, m in ipairs(doc.modules or {}) do
        if type(m.version) == "table" then m.version = {v[1], v[2], patch} end
    end
    json.savefile(manifest, doc)
end

function main(projectdir, outdir, mode)
    local families = import("families", {rootdir = path.join(projectdir, "tools", "families"), anonymous = true})
    local summary = families.generate(projectdir, outdir)
    local errors = summary.errors
    local perms = 0
    local geos = {}
    for _, rp in ipairs({"GloamingForest_RP", "GloamingForest_Test_RP"}) do
        for id, _ in pairs(geometries(path.join(outdir, "resource_packs", rp))) do geos[id] = true end
    end
    local byPack = {}
    for _, f in ipairs(summary.files) do
        local doc = try_json(f)
        if doc then perms = perms + check_block(f, doc, geos, errors) end
        local pack = f:match("behavior_packs[/\\]([^/\\]+)")
        if pack then
            byPack[pack] = byPack[pack] or {}
            table.insert(byPack[pack], f)
        end
    end
    if mode == "stamp" then
        for pack, files in pairs(byPack) do
            stamp_pack(path.join(outdir, "behavior_packs", pack), files)
            local rp = pack:gsub("_BP$", "_RP")
            stamp_pack(path.join(outdir, "resource_packs", rp), files)
        end
    end
    if #errors > 0 then
        for _, e in ipairs(errors) do cprint("${bright red}[families] %s", e) end
        raise("families: %d error(s)", #errors)
    end
    cprint("${bright green}[families]: ${reset}%s: %d blocks, %d fixtures, %d manual kept, %d permutations",
        outdir, summary.blocks, summary.fixtures, summary.skipped, perms)
end
