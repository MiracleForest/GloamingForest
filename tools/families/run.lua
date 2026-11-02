-- GloamingForest - `xmake lua tools/families/run.lua <projectdir> <outdir> [stamp]` (tools/families/run.ps1).
--
-- Runs the block family generator (families.lua) on an assembled directory (bin/<target>/
-- GloamingForest, build/assemble-check, or a deployed copy) when the build has not run it already
-- (tools/xmake/assemble.lua's hook), and reports its checks (families.lua: integer states list
-- 0..n, the permutation budget per block, every geometry a generated block names exists in the
-- resource pack, every waterloggable block keeps water).
-- "stamp" also raises the version of the packs it changed, so games never reuse a cached copy
-- of the pack without the generated blocks.

import("core.base.json")

local function try_json(file) return try { function () return json.loadfile(file) end } end

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
    if summary.already then
        -- the packs are done; problems in the family data still show
        for _, e in ipairs(errors) do cprint("${bright red}[families] %s", e) end
        if #errors > 0 then raise("families: %d error(s)", #errors) end
        cprint("${bright green}[families]: ${reset}%s: already generated (by the build's assemble or an earlier run)", outdir)
        return
    end
    local wet = 0
    for _, _ in pairs(summary.waterloggable) do wet = wet + 1 end
    local byPack = {}
    for _, f in ipairs(summary.files) do
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
    cprint("${bright green}[families]: ${reset}%s: %d blocks, %d fixtures, %d manual kept, %d permutations, %d waterloggable",
        outdir, summary.blocks, summary.fixtures, summary.skipped, summary.permutations, wet)
end
