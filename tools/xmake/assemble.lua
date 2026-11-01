-- Adapted from LiteLDev/Levitate v0.1.0 (tools/xmake/assemble.lua), LGPL-3.0-or-later.
--
-- Contract "assemble" (F0): builds bin/<target_type>/GloamingForest/ after the DLL is linked:
--   GloamingForest.dll/.pdb, manifest.json, lang/<locale>.json, data/**, structures/**,
--   behavior_packs/GloamingForest_BP/, resource_packs/GloamingForest_RP/ (+ the gf_test fixture
--   packs in development builds; GF_NO_TEST_PACKS=1 leaves them out).
--
-- Driven by tools/xmake/merges.json:
--   Layers   gen layer = assets/gen/{import,geo,audio}/<unit>/<root>/** (written by the tools, never
--            by hand), manual layer = assets/pack/<root>/** (and assets/lang, assets/data). The
--            manual layer wins per id: per output path, per BP identifier and per fragment key.
--            Duplicates inside one layer fail the build.
--   Merges   fragment directories ("*.d") are merged into one file: mode "object" (a duplicate key
--            fails), "union" (arrays concatenated, scalars de-duplicated) or "lang" (.lang lines).
--   Lang     assets/lang/<locale>/*.json and the gen layer's lang/<locale>/*.json -> lang/<locale>.json
--            for the 29 Bedrock locales (TF locale names such as de_de are normalised; others are
--            dropped with a note).
--   Tags     assets/tags/<registry>/<ns>/<path>.d/<unit>.json are unioned into the imported tag
--            data/<ns>/tags/<registry>/<path>.json (rule R6).
--   Checks   integer block states 0..n, block permutation budget, dangling twilightforest:
--            references in BP / data, every asset file has an owner (docs/spec/owners.tsv), and the
--            ARR hash guard (no TF .ogg / .nbt content, tools/ci/arr-blobs.txt).
--
-- "${version}" in pack manifests is replaced by the mod version as a JSON array [major, minor, patch]
-- (untagged builds: [0, 0, <stamp of the pack sources>], so games never reuse a stale cached pack).
--
-- Entry points: main(target, target_type) after the DLL build (xmake.lua); check(projectdir, outdir)
-- for `pwsh tools/assemble.ps1 -Check` (everything but the binaries, into a scratch directory).

import("core.base.json")

local errors = {}
local notes = {}

local function fail(fmt, ...)
    table.insert(errors, string.format(fmt, ...))
end

local function note(fmt, ...)
    table.insert(notes, string.format(fmt, ...))
end

local function norm(p)
    return (p:gsub("\\", "/"))
end

local function rel(file, base)
    return norm(path.relative(file, base))
end

-- ---- globs ----------------------------------------------------------------------------------------

-- "a/{b,c}/d" -> {"a/b/d", "a/c/d"}
local function expand_braces(glob)
    local s, e = glob:find("{[^{}]*}")
    if not s then return {glob} end
    local out = {}
    for alt in glob:sub(s + 1, e - 1):gmatch("[^,]+") do
        for _, g in ipairs(expand_braces(glob:sub(1, s - 1) .. alt .. glob:sub(e + 1))) do
            table.insert(out, g)
        end
    end
    return out
end

-- "**/" matches zero or more directories: one variant with "\1" (one or more) and one without.
local function expand_doublestar(glob)
    local s = glob:find("**/", 1, true)
    if not s then return {glob} end
    local prefix, out = glob:sub(1, s - 1), {}
    for _, rest in ipairs(expand_doublestar(glob:sub(s + 3))) do
        table.insert(out, prefix .. "\1" .. rest)
        table.insert(out, prefix .. rest)
    end
    return out
end

-- shell glob (no braces) -> anchored Lua pattern: ** any path, * one segment, ? one char
local function glob_to_pattern(glob)
    local out = {"^"}
    local i = 1
    while i <= #glob do
        local c = glob:sub(i, i)
        if c == "\1" then
            table.insert(out, ".*/")
            i = i + 1
        elseif c == "*" and glob:sub(i + 1, i + 1) == "*" then
            table.insert(out, ".*")
            i = i + 2
        elseif c == "*" then
            table.insert(out, "[^/]*")
            i = i + 1
        elseif c == "?" then
            table.insert(out, "[^/]")
            i = i + 1
        else
            table.insert(out, (c:gsub("[%^%$%(%)%%%.%[%]%+%-]", "%%%0")))
            i = i + 1
        end
    end
    table.insert(out, "$")
    return table.concat(out)
end

local function compile_glob(glob)
    local pats = {}
    for _, g in ipairs(expand_braces(glob)) do
        for _, v in ipairs(expand_doublestar(g)) do
            table.insert(pats, glob_to_pattern(v))
        end
    end
    return pats
end

local function glob_match(pats, s)
    for _, p in ipairs(pats) do
        if s:match(p) then return true end
    end
    return false
end

-- a glob's specificity: its literal characters (more = more specific)
local function glob_score(glob)
    return #(glob:gsub("[%*%?{},]", ""))
end

-- ---- json helpers -----------------------------------------------------------------------------------

-- json.loadfile without raising (nil when the file is not valid JSON)
local function try_json(file)
    return try { function () return json.loadfile(file) end }
end

local function load_json(file)
    local doc = try_json(file)
    if doc == nil then
        fail("unreadable JSON %s", file)
        return nil
    end
    return doc
end

local function is_array(t)
    if type(t) ~= "table" then return false end
    if json.is_marked_as_array and json.is_marked_as_array(t) then return true end
    local n = 0
    for _ in pairs(t) do n = n + 1 end
    return n > 0 and #t == n
end

local function save_json(file, data)
    os.mkdir(path.directory(file))
    json.savefile(file, data)
end

-- replace "$merged" / "$pack" placeholders in a wrapper
local function apply_wrapper(wrapper, merged, packname)
    if wrapper == nil then return merged end
    local out = {}
    for k, v in pairs(wrapper) do
        if k == "$spread" then
            for mk, mv in pairs(merged) do out[mk] = mv end
        elseif v == "$merged" then
            out[k] = merged
        elseif v == "$pack" then
            out[k] = packname
        elseif type(v) == "table" and not is_array(v) then
            out[k] = apply_wrapper(v, merged, packname)
        else
            out[k] = v
        end
    end
    return out
end

-- ---- version ----------------------------------------------------------------------------------------

local function mod_version()
    local tag = try { function () return os.iorun("git describe --tags --abbrev=0 --always") end }
    local major, minor, patch = (tag or ""):match("v(%d+)%.(%d+)%.(%d+)")
    return tonumber(major) or 0, tonumber(minor) or 0, tonumber(patch) or 0
end

-- A positive number (2..30001) derived from the content and paths of every file under `dirs`.
local function pack_stamp(dirs)
    local parts = {}
    for _, dir in ipairs(dirs) do
        if os.isdir(dir) then
            local files = os.files(path.join(dir, "**"))
            table.sort(files)
            for _, file in ipairs(files) do
                table.insert(parts, rel(file, dir) .. "=" .. hash.md5(file))
            end
        end
    end
    local tmp = os.tmpfile() .. ".packstamp"
    io.writefile(tmp, table.concat(parts, "\n"))
    local digest = hash.md5(tmp)
    os.tryrm(tmp)
    -- small: the game rejects a pack whose version parts are too large (seen: 84063262 -> "not found")
    return tonumber(digest:sub(1, 7), 16) % 30000 + 2
end

-- ---- locales ----------------------------------------------------------------------------------------

local function locale_of(name, cfg)
    local lang, region = name:match("^(%a+)[_%-](%a+)$")
    if not lang then return nil end
    local l = lang:lower() .. "_" .. region:upper()
    l = (cfg.localeAliases or {})[l] or l
    for _, known in ipairs(cfg.locales) do
        if known == l then return l end
    end
    return nil
end

-- ---- layers -----------------------------------------------------------------------------------------

-- Files of one root ("bp", "rp", "lang", "data") in the gen layer: {relpath -> {file, unit}}.
local function gen_files(projectdir, cfg, root)
    local out = {}
    for _, layer in ipairs(cfg.genLayers) do
        local base = path.join(projectdir, layer)
        if os.isdir(base) then
            for _, unitdir in ipairs(os.dirs(path.join(base, "*"))) do
                local rootdir = path.join(unitdir, root)
                if os.isdir(rootdir) then
                    for _, file in ipairs(os.files(path.join(rootdir, "**"))) do
                        local r = rel(file, rootdir)
                        if out[r] then
                            fail("duplicate generated file %s/%s in %s and %s", root, r, out[r].file, file)
                        else
                            out[r] = {file = file, unit = path.filename(unitdir)}
                        end
                    end
                end
            end
        end
    end
    return out
end

local function manual_files(dir)
    local out = {}
    if dir and os.isdir(dir) then
        for _, file in ipairs(os.files(path.join(dir, "**"))) do
            out[rel(file, dir)] = {file = file}
        end
    end
    return out
end

local function is_fragment_path(r)
    for part in r:gmatch("[^/]+") do
        if part:match("%.d$") then return true end
    end
    return false
end

-- The BP identifier of a block / item / entity JSON (nil for other files).
local function bp_identifier(file)
    if not file:match("%.json$") then return nil end
    local doc = try_json(file)
    if type(doc) ~= "table" then return nil end
    for _, key in ipairs({"minecraft:block", "minecraft:item", "minecraft:entity", "minecraft:client_entity", "minecraft:attachable"}) do
        local def = doc[key]
        local id = def and def.description and def.description.identifier
        if type(id) == "string" then return key .. "|" .. id end
    end
    return nil
end

-- The layered plain files of a pack: gen files overridden by manual files per path and per BP id.
local function layered_files(gen, manual, root)
    local result = {}
    local manual_ids = {}
    local seen_ids = {}
    for r, m in pairs(manual) do
        if not is_fragment_path(r) then
            result[r] = m.file
            if root == "bp" or root == "rp" then
                local id = bp_identifier(m.file)
                if id then
                    if seen_ids[id] then fail("duplicate identifier %s in %s and %s", id, seen_ids[id], m.file) end
                    seen_ids[id] = m.file
                    manual_ids[id] = true
                end
            end
        end
    end
    local gen_ids = {}
    for r, g in pairs(gen) do
        if not is_fragment_path(r) and not result[r] then
            local id = (root == "bp" or root == "rp") and bp_identifier(g.file) or nil
            if id and manual_ids[id] then
                note("manual %s overrides generated %s", id, g.file)
            else
                if id then
                    if gen_ids[id] then fail("duplicate generated identifier %s in %s and %s", id, gen_ids[id], g.file) end
                    gen_ids[id] = g.file
                end
                result[r] = g.file
            end
        end
    end
    return result
end

-- ---- merges ------------------------------------------------------------------------------------------

local function union_into(acc, value, where)
    if is_array(value) then
        acc.__list = acc.__list or {}
        acc.__seen = acc.__seen or {}
        for _, v in ipairs(value) do
            local key = type(v) == "table" and json.encode(v) or tostring(v)
            if not acc.__seen[key] then
                acc.__seen[key] = true
                table.insert(acc.__list, v)
            end
        end
    elseif type(value) == "table" then
        acc.__map = acc.__map or {}
        for k, v in pairs(value) do
            if type(v) == "table" then
                acc.__map[k] = acc.__map[k] or {}
                union_into(acc.__map[k], v, where .. "." .. k)
            else
                if acc.__map[k] ~= nil and acc.__map[k] ~= v then
                    fail("union conflict for key '%s' in %s", k, where)
                end
                acc.__map[k] = v
            end
        end
    else
        fail("union fragment %s is neither an array nor an object", where)
    end
end

local function union_result(acc)
    if acc.__list then
        return acc.__list
    end
    if acc.__map then
        local out = {}
        for k, v in pairs(acc.__map) do
            out[k] = type(v) == "table" and union_result(v) or v
        end
        return out
    end
    return {}
end

-- Merges the fragments matching `glob` (relative to the root) from both layers.
-- Returns {outpath -> data} (the "{locale}" of lang merges expanded).
local function merge_fragments(spec, gen, manual, cfg)
    local pats = compile_glob(spec.glob)
    local out = {}
    if spec.mode == "lang" then
        local by_locale = {}
        for _, layer in ipairs({{gen, "gen"}, {manual, "manual"}}) do
            local files, lname = layer[1], layer[2]
            local keys = {}
            local relpaths = table.keys(files)
            table.sort(relpaths)
            for _, r in ipairs(relpaths) do
                if glob_match(pats, r) then
                    local localedir = r:match("([^/]+)/[^/]+%.lang$")
                    local locale = localedir and locale_of(localedir, cfg)
                    if not locale then
                        note("dropped %s (not one of the 29 Bedrock locales)", files[r].file)
                    else
                        by_locale[locale] = by_locale[locale] or {lines = {}, order = {}}
                        local L = by_locale[locale]
                        keys[locale] = keys[locale] or {}
                        for line in io.lines(files[r].file) do
                            line = line:gsub("\r$", ""):gsub("^\239\187\191", "")
                            local key = line:match("^([^#=][^=]*)=")
                            if key then
                                if keys[locale][key] then
                                    fail("duplicate texts key '%s' in %s (already in %s)", key, files[r].file, keys[locale][key])
                                end
                                keys[locale][key] = files[r].file
                                if L.lines[key] == nil then table.insert(L.order, key) end
                                L.lines[key] = line -- the manual layer comes second and wins
                            end
                        end
                    end
                end
            end
        end
        for locale, L in pairs(by_locale) do
            local lines = {}
            for _, key in ipairs(L.order) do table.insert(lines, L.lines[key]) end
            out[spec.out:gsub("{locale}", locale)] = {lang = lines, locale = locale}
        end
        return out
    end
    local merged = {}
    local origin = {}
    local acc = {}
    local any = false
    for _, layer in ipairs({{gen, "gen"}, {manual, "manual"}}) do
        local files, lname = layer[1], layer[2]
        local layer_origin = {}
        local relpaths = table.keys(files)
        table.sort(relpaths)
        for _, r in ipairs(relpaths) do
            if glob_match(pats, r) then
                local doc = load_json(files[r].file)
                if doc ~= nil then
                    any = true
                    if spec.mode == "union" then
                        union_into(acc, doc, files[r].file)
                    else
                        if type(doc) ~= "table" or is_array(doc) then
                            fail("fragment %s must be a JSON object", files[r].file)
                        else
                            for k, v in pairs(doc) do
                                if layer_origin[k] then
                                    fail("duplicate key '%s' in %s (already defined in %s)", k, files[r].file, layer_origin[k])
                                end
                                layer_origin[k] = files[r].file
                                if origin[k] and lname == "manual" then
                                    note("manual key '%s' in %s overrides %s", k, files[r].file, origin[k])
                                end
                                merged[k] = v
                                origin[k] = files[r].file
                            end
                        end
                    end
                end
            end
        end
    end
    if not any then return out end
    local data = spec.mode == "union" and union_result(acc) or merged
    if table.empty(data) then return out end
    out[spec.out] = {json = data}
    return out
end

-- ---- checks --------------------------------------------------------------------------------------------

-- Bedrock reads a list-valued integer block state by the value's POSITION in the list, so a state
-- declared [2,3,4,5] reads back 0..3. Every integer state list must be 0..n in order.
-- Also counts the permutations of every block against the budget.
local function check_blocks(bpdirs, cfg)
    local budget = cfg.checks.permutationBudget or {}
    local total = 0
    local worst = {}
    for _, bpdir in ipairs(bpdirs) do
        for _, file in ipairs(os.files(path.join(bpdir, "blocks", "**.json"))) do
            local doc = load_json(file)
            local block = doc and doc["minecraft:block"]
            local states = block and block.description and block.description.states
            local count = 1
            if type(states) == "table" then
                for name, values in pairs(states) do
                    local n = 1
                    if type(values) == "table" and is_array(values) then
                        n = #values
                        if cfg.checks.integerStates and #values > 0 and type(values[1]) == "number" then
                            for i, v in ipairs(values) do
                                if type(v) ~= "number" or v ~= i - 1 then
                                    fail("block state '%s' in %s must list the integers 0..%d in order (Bedrock reads integer states by position), got %s at index %d",
                                        name, file, #values - 1, tostring(v), i - 1)
                                    break
                                end
                            end
                        end
                    elseif type(values) == "table" and values.values then
                        local r = values.values
                        if type(r.min) == "number" and type(r.max) == "number" then n = r.max - r.min + 1 end
                    end
                    count = count * math.max(n, 1)
                end
            end
            total = total + count
            if budget.perBlock and count > budget.perBlock then
                fail("block %s has %d permutations (budget %d per block)", file, count, budget.perBlock)
            end
            table.insert(worst, {file = file, count = count})
        end
    end
    if budget.total and total > budget.total then
        table.sort(worst, function (a, b) return a.count > b.count end)
        local top = {}
        for i = 1, math.min(5, #worst) do table.insert(top, string.format("%s=%d", path.filename(worst[i].file), worst[i].count)) end
        fail("%d block permutations in total exceed the budget of %d (largest: %s)", total, budget.total, table.concat(top, ", "))
    end
    return total
end

-- owners.tsv: {kinds = {kind -> {id -> owner}}, paths = {{pats, owner, glob, score}}}
local function load_owners(projectdir, cfg)
    local file = path.join(projectdir, cfg.checks.owners)
    local owners = {kinds = {}, paths = {}}
    if not os.isfile(file) then
        fail("ownership manifest %s is missing", cfg.checks.owners)
        return owners
    end
    for line in io.lines(file) do
        line = line:gsub("\r$", "")
        if line ~= "" and not line:match("^#") then
            local kind, id, owner = line:match("^([^\t]+)\t([^\t]+)\t([^\t]+)")
            if kind == "path" then
                -- one rule per brace alternative: a rule is as specific as the alternative that matches;
                -- an exact path beats every glob
                for _, alt in ipairs(expand_braces(id)) do
                    local score = glob_score(alt) + (alt:find("[%*%?]") and 0 or 1000)
                    table.insert(owners.paths, {pats = compile_glob(alt), owner = owner, glob = id, score = score})
                end
            elseif kind and kind ~= "kind" then
                owners.kinds[kind] = owners.kinds[kind] or {}
                owners.kinds[kind][id] = owner
            end
        end
    end
    return owners
end

local function owner_of(owners, relpath)
    local best, bestscore, tie = nil, -1, nil
    for _, p in ipairs(owners.paths) do
        if glob_match(p.pats, relpath) then
            if p.score > bestscore then
                best, bestscore, tie = p, p.score, nil
            elseif p.score == bestscore and p.owner ~= best.owner then
                tie = p
            end
        end
    end
    if tie then return nil, string.format("ambiguous: %s (%s) and %s (%s)", best.glob, best.owner, tie.glob, tie.owner) end
    return best and best.owner or nil
end

local function check_owners(projectdir, cfg, owners)
    local count = 0
    for _, root in ipairs(cfg.checks.ownedRoots) do
        local dir = path.join(projectdir, root)
        if os.isdir(dir) then
            for _, file in ipairs(os.files(path.join(dir, "**"))) do
                local r = rel(file, projectdir)
                local owner, why = owner_of(owners, r)
                if not owner then
                    fail("%s has no owner in %s%s", r, cfg.checks.owners, why and (" (" .. why .. ")") or "")
                end
                count = count + 1
            end
        end
    end
    return count
end

-- Every "twilightforest:..." string value in BP / data JSON must name something that exists: a BP
-- identifier, a data file (loot table, recipe, worldgen, ...), a tag, or an id of owners.tsv.
local function check_dangling(jsonfiles, known, cfg)
    local nss = cfg.checks.danglingNamespaces or {}
    local count = 0
    local function visit(v, file)
        if type(v) == "string" then
            local s = v:gsub("^#", "")
            for _, ns in ipairs(nss) do
                local id = s:match("^(" .. ns .. ":[a-z0-9_/%.%-]+)$")
                if id and not known[id] then
                    fail("dangling reference '%s' in %s", v, file)
                end
                if id then count = count + 1 end
            end
        elseif type(v) == "table" then
            for _, x in pairs(v) do visit(x, file) end
        end
    end
    for _, file in ipairs(jsonfiles) do
        local doc = try_json(file)
        if doc then visit(doc, file) end
    end
    return count
end

-- The 40-hex git blob hash of a file ("blob <size>\0<content>").
local function blob_hash(file)
    local content = io.readfile(file, {encoding = "binary"})
    local tmp = os.tmpfile() .. ".blob"
    io.writefile(tmp, "blob " .. #content .. "\0" .. content, {encoding = "binary"})
    local h = hash.sha1(tmp)
    os.tryrm(tmp)
    return h:lower()
end

local function check_arr(projectdir, cfg)
    local listfile = path.join(projectdir, cfg.checks.arrBlobs)
    if not os.isfile(listfile) then
        fail("ARR blob list %s is missing (pwsh tools/ci/arr-guard.ps1 -Refresh)", cfg.checks.arrBlobs)
        return 0
    end
    local arr = {}
    for line in io.lines(listfile) do
        local h = line:match("^(%x+)")
        if h and #h == 40 then arr[h:lower()] = line end
    end
    local exts = {}
    for _, e in ipairs(cfg.checks.arrScan.extensions) do exts[e] = true end
    local dirs = {}
    for _, d in ipairs(cfg.checks.arrScan.dirs) do dirs[d] = true end
    local scanned = 0
    for _, root in ipairs(cfg.checks.ownedRoots) do
        local dir = path.join(projectdir, root)
        if os.isdir(dir) then
            for _, file in ipairs(os.files(path.join(dir, "**"))) do
                local r = rel(file, projectdir)
                local ext = (path.extension(file) or ""):gsub("^%.", ""):lower()
                local insound = false
                for part in r:gmatch("[^/]+") do if dirs[part] then insound = true end end
                if exts[ext] then
                    if ext == "ogg" or ext == "nbt" or ext == "mcstructure" then
                        note("%s: %s file (must be an original, see rule R10/R11)", r, ext)
                    end
                end
                if exts[ext] or insound then
                    scanned = scanned + 1
                    local h = blob_hash(file)
                    if arr[h] then
                        fail("%s is byte-identical to an All Rights Reserved TF file (%s); never copy TF .ogg / .nbt", r, arr[h])
                    end
                end
            end
        end
    end
    return scanned
end

-- ---- assembly ---------------------------------------------------------------------------------------

local function load_config(projectdir)
    local cfg = json.loadfile(path.join(projectdir, "tools", "xmake", "merges.json"))
    return cfg
end

local function copy_manifest(file, target, version)
    local text = io.readfile(file)
    text = text:gsub('"%${version}"', string.format("[%d, %d, %d]", version[1], version[2], version[3]))
    os.mkdir(path.directory(target))
    io.writefile(target, text)
end

-- Builds one pack into outdir/<dir>/<name>. Returns the list of JSON files written (for checks).
local function build_pack(projectdir, cfg, pack, outdir, version, with_gen)
    local dst = path.join(outdir, pack.dir, pack.name)
    os.tryrm(dst)
    os.mkdir(dst)
    local gen = with_gen and gen_files(projectdir, cfg, pack.root) or {}
    local manual = manual_files(path.join(projectdir, pack.manual))
    local files = layered_files(gen, manual, pack.root)
    local jsons = {}
    for r, file in pairs(files) do
        local target = path.join(dst, r)
        if path.filename(file) == "manifest.json" and not r:find("/") then
            copy_manifest(file, target, version)
        else
            os.mkdir(path.directory(target))
            os.cp(file, target)
        end
        if r:match("%.json$") and path.filename(file) ~= "manifest.json" then table.insert(jsons, target) end
    end
    if not files["manifest.json"] then fail("pack %s has no manifest.json", pack.manual) end
    for _, spec in ipairs(cfg.merges) do
        if spec.root == pack.root then
            for outrel, data in pairs(merge_fragments(spec, gen, manual, cfg)) do
                local target = path.join(dst, outrel)
                if data.lang then
                    os.mkdir(path.directory(target))
                    io.writefile(target, table.concat(data.lang, "\n") .. "\n")
                else
                    save_json(target, apply_wrapper(spec.wrapper, data.json, cfg.resourcePackName))
                    table.insert(jsons, target)
                end
            end
        end
    end
    -- texts/languages.json lists the locales that have a texts file
    if pack.root == "rp" and os.isdir(path.join(dst, "texts")) then
        local locales = {}
        for _, f in ipairs(os.files(path.join(dst, "texts", "*.lang"))) do table.insert(locales, path.basename(f)) end
        table.sort(locales)
        if #locales > 0 then save_json(path.join(dst, "texts", "languages.json"), locales) end
    end
    return dst, jsons
end

local function merge_lang(projectdir, cfg, outdir)
    local dst = path.join(outdir, "lang")
    os.tryrm(dst)
    os.mkdir(dst)
    local gen = gen_files(projectdir, cfg, "lang")
    local manual = manual_files(path.join(projectdir, "assets", "lang"))
    local by_locale = {}
    for _, layer in ipairs({{gen, "gen"}, {manual, "manual"}}) do
        local files, lname = layer[1], layer[2]
        local layer_origin = {}
        local relpaths = table.keys(files)
        table.sort(relpaths)
        for _, r in ipairs(relpaths) do
            local localedir = r:match("^([^/]+)/[^/]+%.json$")
            if localedir then
                local locale = locale_of(localedir, cfg)
                if not locale then
                    note("dropped lang file %s (not one of the 29 Bedrock locales)", files[r].file)
                else
                    local doc = load_json(files[r].file)
                    if type(doc) == "table" then
                        by_locale[locale] = by_locale[locale] or {}
                        layer_origin[locale] = layer_origin[locale] or {}
                        for k, v in pairs(doc) do
                            if layer_origin[locale][k] then
                                fail("duplicate lang key '%s' (%s) in %s (already defined in %s)", k, locale, files[r].file, layer_origin[locale][k])
                            end
                            layer_origin[locale][k] = files[r].file
                            by_locale[locale][k] = v
                        end
                    end
                end
            end
        end
    end
    if not by_locale["en_US"] then by_locale["en_US"] = {["gf.lang.empty"] = ""} end
    for locale, data in pairs(by_locale) do
        save_json(path.join(dst, locale .. ".json"), data)
    end
end

-- data/**: imported Java-format data (gen layer "data" roots), unit data, test data, structures.
local function copy_data(projectdir, cfg, outdir, with_tests)
    local datadir = path.join(outdir, "data")
    os.tryrm(datadir)
    os.tryrm(path.join(outdir, "structures"))
    local jsons = {}
    local known = {}
    for r, g in pairs(gen_files(projectdir, cfg, "data")) do
        local target = path.join(datadir, r)
        os.mkdir(path.directory(target))
        os.cp(g.file, target)
        if r:match("%.json$") then table.insert(jsons, target) end
    end
    for _, d in ipairs(cfg.data) do
        if with_tests or not d.test then
            local src = path.join(projectdir, d.from)
            if os.isdir(src) then
                for _, file in ipairs(os.files(path.join(src, "**"))) do
                    local target = path.join(outdir, d.to, rel(file, src))
                    os.mkdir(path.directory(target))
                    os.cp(file, target)
                    if target:match("%.json$") and not d.test then table.insert(jsons, target) end
                end
            end
        end
    end
    -- tag fragments: assets/tags/<registry>/<ns>/<path>.d/<unit>.json
    local tagroot = path.join(projectdir, cfg.tags.fragments)
    if os.isdir(tagroot) then
        local groups = {}
        for _, file in ipairs(os.files(path.join(tagroot, "**.json"))) do
            local r = rel(file, tagroot)
            local registry_ns_path = r:match("^(.+)%.d/[^/]+%.json$")
            if not registry_ns_path then
                fail("tag fragment %s must be assets/tags/<registry>/<namespace>/<path>.d/<unit>.json", r)
            else
                groups[registry_ns_path] = groups[registry_ns_path] or {}
                table.insert(groups[registry_ns_path], file)
            end
        end
        for key, files in pairs(groups) do
            -- registry may contain a slash (worldgen/biome): the namespace is the first segment
            -- that the imported data tree has as data/<ns>/tags/<registry>
            local registry, ns, tagpath = key:match("^(worldgen/[^/]+)/([^/]+)/(.+)$")
            if not registry then registry, ns, tagpath = key:match("^([^/]+)/([^/]+)/(.+)$") end
            if not registry then
                fail("bad tag fragment path %s", key)
            else
                local target = path.join(datadir, ns, "tags", registry, tagpath .. ".json")
                local acc = {}
                if os.isfile(target) then
                    local base = load_json(target)
                    if base then union_into(acc, base, target) end
                end
                table.sort(files)
                for _, f in ipairs(files) do
                    local doc = load_json(f)
                    if doc then union_into(acc, doc, f) end
                end
                local merged = union_result(acc)
                if merged.replace == nil then merged.replace = false end
                save_json(target, merged)
                table.insert(jsons, target)
            end
        end
    end
    return jsons
end

-- Ids that exist: BP identifiers, data files by id, tags, owners.tsv ids.
local function known_ids(bpdirs, outdir, owners)
    local known = {}
    for _, bpdir in ipairs(bpdirs) do
        for _, file in ipairs(os.files(path.join(bpdir, "**.json"))) do
            local id = bp_identifier(file)
            if id then known[id:match("|(.+)$")] = true end
        end
    end
    local datadir = path.join(outdir, "data")
    if os.isdir(datadir) then
        for _, file in ipairs(os.files(path.join(datadir, "**.json"))) do
            local r = rel(file, datadir)
            -- data/<ns>/<kind...>/<path>.json -> <ns>:<path> for every kind prefix
            local ns, rest = r:match("^([^/]+)/(.+)%.json$")
            if ns and rest then
                local parts = {}
                for p in rest:gmatch("[^/]+") do table.insert(parts, p) end
                for i = 1, #parts do
                    known[ns .. ":" .. table.concat(parts, "/", i)] = true
                end
            end
        end
    end
    for _, ids in pairs(owners.kinds) do
        for id, _ in pairs(ids) do
            if id:find(":") and not id:find("[%*%?{]") then known[id] = true end
        end
    end
    return known
end

local function report(label)
    for _, n in ipairs(notes) do
        if os.getenv("GF_ASSEMBLE_VERBOSE") then cprint("${dim}[assemble] %s", n) end
    end
    if #errors > 0 then
        for _, e in ipairs(errors) do cprint("${bright red}[assemble] %s", e) end
        raise("assemble failed with %d error(s) (%s)", #errors, label)
    end
end

-- Packs, lang, data and checks into `outdir` (no binaries).
local function assemble_content(projectdir, outdir, version)
    local cfg = load_config(projectdir)
    errors, notes = {}, {}
    local with_tests = os.getenv("GF_NO_TEST_PACKS") ~= "1"
    if version == nil then
        local major, minor, patch = mod_version()
        version = {major, minor, patch}
        if major == 0 and minor == 0 and patch == 0 then
            local dirs = {path.join(projectdir, "assets")}
            version = {0, 0, pack_stamp(dirs)}
        end
    end
    merge_lang(projectdir, cfg, outdir)
    local datajsons = copy_data(projectdir, cfg, outdir, with_tests)
    local bpdirs, alljsons = {}, {}
    for _, pack in ipairs(cfg.packs) do
        if with_tests or not pack.test then
            local dst, jsons = build_pack(projectdir, cfg, pack, outdir, version, not pack.test)
            if pack.root == "bp" then table.insert(bpdirs, dst) end
            if not pack.test then
                for _, j in ipairs(jsons) do table.insert(alljsons, j) end
            end
        else
            os.tryrm(path.join(outdir, pack.dir, pack.name))
        end
    end
    for _, j in ipairs(datajsons) do table.insert(alljsons, j) end
    local permutations = check_blocks(bpdirs, cfg)
    local owners = load_owners(projectdir, cfg)
    local owned = check_owners(projectdir, cfg, owners)
    local refs = check_dangling(alljsons, known_ids(bpdirs, outdir, owners), cfg)
    local scanned = check_arr(projectdir, cfg)
    report(outdir)
    return string.format("%d block permutations, %d owned files, %d references, %d files hash-checked", permutations, owned, refs, scanned)
end

function main(target, target_type)
    local projectdir = os.projectdir()
    local cfg = load_config(projectdir)
    local outdir = path.join(projectdir, "bin", target_type, cfg.modName)
    os.mkdir(outdir)

    -- Binaries and manifest (same substitutions as levibuildscript's modpacker).
    local targetfile = target:targetfile()
    os.cp(targetfile, path.join(outdir, path.filename(targetfile)))
    local pdb = path.join(path.directory(targetfile), path.basename(targetfile) .. ".pdb")
    if os.isfile(pdb) then os.cp(pdb, path.join(outdir, path.basename(targetfile) .. ".pdb")) end
    local major, minor, patch = mod_version()
    local manifest = io.readfile(path.join(projectdir, "manifest.json"))
    local vars = {
        modName = cfg.modName,
        modFile = path.filename(targetfile),
        modVersion = string.format("%d.%d.%d", major, minor, patch),
        modPlatform = target_type,
    }
    manifest = manifest:gsub("%${(.-)}", function (k) return vars[k] or ("${" .. k .. "}") end)
    io.writefile(path.join(outdir, "manifest.json"), manifest)

    local summary = assemble_content(projectdir, outdir)
    cprint("${bright green}[assemble]: ${reset}%s (%s)", outdir, summary)
end

-- `pwsh tools/assemble.ps1 -Check`: everything but the binaries, into `outdir`.
function check(projectdir, outdir)
    os.tryrm(outdir)
    os.mkdir(outdir)
    local summary = assemble_content(projectdir, outdir)
    cprint("${bright green}[assemble -Check]: ${reset}OK (%s)", summary)
end
