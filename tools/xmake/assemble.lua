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
--            fails), "union" (arrays concatenated, scalars de-duplicated), "lang" (.lang lines) or
--            "categories" (the named objects of the file merged per id: rp/sounds.json.d ->
--            sounds.json by block_sounds, entity_sounds.entities, ...; a duplicate id inside a
--            layer fails).
--   Lang     assets/lang/<locale>/*.json and the gen layer's lang/<locale>/*.json -> lang/<locale>.json
--            for the 29 Bedrock locales (TF locale names such as de_de are normalised; others are
--            dropped with a note).
--   Tags     assets/tags/<registry>/<ns>/<path>.d/<unit>.json are unioned into the imported tag
--            data/<ns>/tags/<registry>/<path>.json (rule R6).
--   Checks   integer block states 0..n, block permutation budget, dangling twilightforest:
--            references in BP / data (per kind: a `block` key must name a block; the values of
--            tag-valued keys name tags; the reviewed ids of tools/import/known-ids.json are known
--            by kind, its tfDangling references are warnings), every asset file has an owner
--            (docs/spec/owners.tsv), the pack path probes (checks.packPathProbes: a file in a
--            directory granted to a unit reaches the assembled pack and belongs to that unit), and
--            the ARR hash guard (no TF .ogg / .nbt content, tools/ci/arr-blobs.txt).
--
-- "${version}" in pack manifests is replaced by the mod version as a JSON array [major, minor, patch]
-- (untagged builds: [0, 0, <stamp of the pack sources>], so games never reuse a stale cached pack).
--
-- Entry points: main(target, target_type) after the DLL build (xmake.lua); check(projectdir, outdir)
-- for `pwsh tools/assemble.ps1 -Check` (everything but the binaries, into a scratch directory).

import("core.base.json")

local errors = {}
local warnings = {}
local notes = {}

local function fail(fmt, ...)
    table.insert(errors, string.format(fmt, ...))
end

-- printed on every run, never fails it (e.g. a reference that is dangling in TF itself)
local function warn(fmt, ...)
    table.insert(warnings, string.format(fmt, ...))
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

-- pairs over parsed JSON: the sandbox's pairs calls t:pairs() instead when the object has a "pairs"
-- key (the chest declarations of data/gf/containers do), so such objects go through their keys
local function json_pairs(t)
    if type(t) ~= "table" or t.pairs == nil then return pairs(t) end
    local keys, i = table.keys(t), 0
    return function ()
        i = i + 1
        local k = keys[i]
        if k ~= nil then return k, t[k] end
    end
end

local function is_array(t)
    if type(t) ~= "table" then return false end
    if json.is_marked_as_array and json.is_marked_as_array(t) then return true end
    local n = 0
    for _ in json_pairs(t) do n = n + 1 end
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
    for k, v in json_pairs(wrapper) do
        if k == "$spread" then
            for mk, mv in json_pairs(merged) do out[mk] = mv end
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

-- A positive number (2..30001) derived from the content and paths (relative to `base`) of `files`.
local function pack_stamp(base, files)
    local parts, seen = {}, {}
    for _, file in ipairs(files) do
        local r = rel(file, base)
        if not seen[r] then
            seen[r] = true
            table.insert(parts, r .. "=" .. hash.md5(file))
        end
    end
    table.sort(parts)
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
        for k, v in json_pairs(value) do
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
        for k, v in json_pairs(acc.__map) do
            out[k] = type(v) == "table" and union_result(v) or v
        end
        return out
    end
    return {}
end

-- Calls fn(file, layer, relpath) for every file matching `pats`: the gen layer first, then the manual
-- layer, each in path order.
local function each_fragment(gen, manual, pats, fn)
    for _, layer in ipairs({{gen, "gen"}, {manual, "manual"}}) do
        local files = layer[1]
        local relpaths = table.keys(files)
        table.sort(relpaths)
        for _, r in ipairs(relpaths) do
            if glob_match(pats, r) then fn(files[r].file, layer[2], r) end
        end
    end
end

-- Per-id layering of the "object" and "categories" merges: put(id, value, file, layer) keeps the
-- last value of an id (the manual layer comes second and wins, with a note); an id defined twice
-- inside one layer fails. `what` names the ids in the messages ("key", "block_sounds id").
local function id_layers(what)
    local L = {merged = {}}
    local origin, layer_origin, current = {}, {}, nil
    function L.put(id, value, file, layer)
        if layer ~= current then current, layer_origin = layer, {} end
        if layer_origin[id] then
            fail("duplicate %s '%s' in %s (already defined in %s)", what, id, file, layer_origin[id])
        elseif origin[id] and layer == "manual" then
            note("manual %s '%s' in %s overrides %s", what, id, file, origin[id])
        end
        layer_origin[id] = file
        L.merged[id] = value
        origin[id] = file
    end
    return L
end

-- Mode "categories": every fragment is an object holding some of `spec.categories`, dotted paths
-- of objects keyed by id ("block_sounds", "entity_sounds.entities"); each category is merged per id
-- like an "object" merge (a duplicate id inside a layer fails, the manual layer wins per id). Any
-- other key of a fragment fails: it would be dropped (e.g. entity_sounds.defaults stays vanilla's).
local function merge_categories(spec, gen, manual, pats)
    local cats, prefixes = {}, {}
    for _, c in ipairs(spec.categories or {}) do
        local parts = {}
        for p in c:gmatch("[^%.]+") do table.insert(parts, p) end
        table.insert(cats, {name = c, parts = parts})
        for i = 1, #parts - 1 do prefixes[table.concat(parts, ".", 1, i)] = true end
    end
    local layers = {} -- category -> id_layers
    for _, c in ipairs(cats) do layers[c.name] = id_layers(c.name .. " id") end
    each_fragment(gen, manual, pats, function (file, layer)
        local function walk(node, prefix)
            for k, v in json_pairs(node) do
                local p = prefix and (prefix .. "." .. k) or k
                if layers[p] then
                    if type(v) ~= "table" or (is_array(v) and #v > 0) then
                        fail("%s: '%s' must be an object of ids", file, p)
                    else
                        for id, def in json_pairs(v) do
                            if type(def) ~= "table" or (is_array(def) and #def > 0) then
                                fail("%s: %s '%s' must be an object", file, p, id)
                            end
                            layers[p].put(id, def, file, layer)
                        end
                    end
                elseif prefixes[p] and type(v) == "table" and not is_array(v) then
                    walk(v, p)
                else
                    fail("%s: '%s' is not one of the categories merged into %s (%s)", file, p, spec.out, table.concat(spec.categories, ", "))
                end
            end
        end
        local doc = load_json(file)
        if doc ~= nil then
            if type(doc) ~= "table" or is_array(doc) then
                fail("fragment %s must be a JSON object", file)
            else
                walk(doc, nil)
            end
        end
    end)
    local data = {}
    for _, c in ipairs(cats) do
        local ids = layers[c.name].merged
        if not table.empty(ids) then
            local node = data
            for i = 1, #c.parts - 1 do
                node[c.parts[i]] = node[c.parts[i]] or {}
                node = node[c.parts[i]]
            end
            node[c.parts[#c.parts]] = ids
        end
    end
    if table.empty(data) then return {} end
    return {[spec.out] = {json = data}}
end

-- Merges the fragments matching `glob` (relative to the root) from both layers.
-- Returns {outpath -> data} (the "{locale}" of lang merges expanded).
local function merge_fragments(spec, gen, manual, cfg)
    local pats = compile_glob(spec.glob)
    if spec.mode == "categories" then return merge_categories(spec, gen, manual, pats) end
    local out = {}
    if spec.mode == "lang" then
        local by_locale = {}
        local keys, current = {}, nil -- locale -> {key -> file} of the current layer
        each_fragment(gen, manual, pats, function (file, layer, r)
            if layer ~= current then current, keys = layer, {} end
            local localedir = r:match("([^/]+)/[^/]+%.lang$")
            local locale = localedir and locale_of(localedir, cfg)
            if not locale then
                note("dropped %s (not one of the 29 Bedrock locales)", file)
                return
            end
            by_locale[locale] = by_locale[locale] or {lines = {}, order = {}}
            local L = by_locale[locale]
            keys[locale] = keys[locale] or {}
            for line in io.lines(file) do
                line = line:gsub("\r$", ""):gsub("^\239\187\191", "")
                local key = line:match("^([^#=][^=]*)=")
                if key then
                    if keys[locale][key] then
                        fail("duplicate texts key '%s' in %s (already in %s)", key, file, keys[locale][key])
                    end
                    keys[locale][key] = file
                    if L.lines[key] == nil then table.insert(L.order, key) end
                    L.lines[key] = line -- the manual layer comes second and wins
                end
            end
        end)
        for locale, L in pairs(by_locale) do
            local lines = {}
            for _, key in ipairs(L.order) do table.insert(lines, L.lines[key]) end
            out[spec.out:gsub("{locale}", locale)] = {lang = lines, locale = locale}
        end
        return out
    end
    local keys = id_layers("key")
    local acc = {}
    local any = false
    each_fragment(gen, manual, pats, function (file, layer)
        local doc = load_json(file)
        if doc == nil then return end
        any = true
        if spec.mode == "union" then
            union_into(acc, doc, file)
        elseif type(doc) ~= "table" or is_array(doc) then
            fail("fragment %s must be a JSON object", file)
        else
            for k, v in json_pairs(doc) do keys.put(k, v, file, layer) end
        end
    end)
    if not any then return out end
    local data = spec.mode == "union" and union_result(acc) or keys.merged
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
                for name, values in json_pairs(states) do
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

-- Lang keys with an owner (the lang rows of owners.tsv: TF's keys by import partition, plus the
-- vanilla keys a unit defines, e.g. F11's container.chest) may be defined in assets/lang only by
-- that owner's file (assets/lang/<locale>/<u>.json): rule R9. Keys without a row are the unit's own
-- mod strings. Returns the number of owned keys checked.
local function check_lang_owners(projectdir, owners)
    local keys = owners.kinds["lang"] or {}
    local count = 0
    local dir = path.join(projectdir, "assets", "lang")
    if not os.isdir(dir) then return 0 end
    for _, file in ipairs(os.files(path.join(dir, "*", "*.json"))) do
        local r = rel(file, projectdir)
        local fileowner = owner_of(owners, r)
        local doc = load_json(file)
        if type(doc) == "table" then
            for k, _ in json_pairs(doc) do
                local keyowner = keys[k]
                if keyowner then
                    count = count + 1
                    if keyowner ~= fileowner then
                        fail("lang key '%s' in %s belongs to %s (docs/spec/owners.tsv), not to the file's owner %s: use tr(key, fallback) instead of defining it (R9)",
                            k, r, keyowner, fileowner or "(none)")
                    end
                end
            end
        end
    end
    return count
end

-- ---- dangling references -----------------------------------------------------------------------------
--
-- Known ids are kept per kind: known[kind][id]. Kinds: block, item (blocks, block items and spawn
-- eggs count as items), entity, the data registries by their directory below data/<ns>/ without a
-- "worldgen/" prefix (loot_table, recipe, advancement, configured_feature, placed_feature, biome,
-- structure, template_pool, ..., "twilight/<registry>"), "tag:<registry>" for tags, the owners.tsv
-- kinds (sound_event, particle, damage_type, ...) and "*" (every id of every kind) for references
-- whose kind the key does not tell.

-- The kinds a reference may name, by the JSON key it is the value of (Bedrock BP and Java data keys).
local REF_KEY_KINDS = {
    block = {"block"}, blocks = {"block"}, Name = {"block"},
    item = {"item"}, items = {"item"}, spawn_item = {"item"},
    entity = {"entity"}, entity_type = {"entity"}, entity_types = {"entity"},
    loot_table = {"loot_table"},
    feature = {"configured_feature", "placed_feature"}, features = {"configured_feature", "placed_feature"},
    biome = {"biome"}, biomes = {"biome"},
    structure = {"structure"}, structures = {"structure"},
    sound = {"sound_event"}, sound_event = {"sound_event"},
    particle = {"particle"},
    enchantment = {"enchantment"}, enchantments = {"enchantment"},
    damage_type = {"damage_type"},
    recipe = {"recipe"}, recipes = {"recipe"},
}
-- Keys that define an id instead of referring to one: a BP identifier, and the Bedrock item tags an
-- item declares (the item component minecraft:tags {"tags": [...]}; known_ids adds them to the item
-- tags, so a unit may give an item a Bedrock-only tag and name it in a BP recipe).
local DEFINING_KEYS = {identifier = true, ["minecraft:tags"] = true}
-- Keys whose values name a tag, with or without '#': Java ingredients and block predicates
-- ({"tag": id}, matching_block_tag), TagKey codecs (BerryBushConfig's generates_on) and Bedrock
-- recipe ingredients; tools/import/known-ids.json's tagValuedKeys adds to them. The registry is the
-- key's (TAG_KEY_KINDS), the predicate type's (BLOCK_TAG_TYPES) or the file's (a recipe's tag is an
-- item tag); otherwise a tag of any registry.
local TAG_VALUED_KEYS = {tag = true, tags = true, generates_on = true}
local TAG_KEY_KINDS = {generates_on = {"tag:block"}}
-- Objects {"type" | "predicate_type": T, "tag": id} whose tag is a block tag: the block predicate
-- matching_block_tag and the rule test tag_match.
local BLOCK_TAG_TYPES = {["minecraft:matching_block_tag"] = true, ["minecraft:tag_match"] = true}
-- The "tags" of a Bedrock recipe (minecraft:recipe_shaped {"tags": ["crafting_table"]}) name the
-- crafting stations (a block's minecraft:crafting_table crafting_tags), not tags.
local function is_bp_recipe_key(k)
    return type(k) == "string" and k:match("^minecraft:recipe_") ~= nil
end
-- Loot pool entries {"type": T, "name" | "value": id}: the kind follows the entry type.
local LOOT_ENTRY_KINDS = {
    ["minecraft:item"] = {"item"},
    ["minecraft:loot_table"] = {"loot_table"},
    ["minecraft:tag"] = {"tag:item"},
}
-- The tag registry a "#tag" reference of a kind names.
local TAG_REGISTRY = {
    block = "block", item = "item", entity = "entity_type", biome = "worldgen/biome", structure = "worldgen/structure",
    damage_type = "damage_type", enchantment = "enchantment", configured_feature = "worldgen/configured_feature",
    placed_feature = "worldgen/placed_feature",
}
-- The kind of the values of a tag registry (data/<ns>/tags/<registry>/...).
local REGISTRY_KIND = {
    block = "block", item = "item", entity_type = "entity", ["worldgen/biome"] = "biome", ["worldgen/structure"] = "structure",
    damage_type = "damage_type", enchantment = "enchantment", ["worldgen/configured_feature"] = "configured_feature",
    ["worldgen/placed_feature"] = "placed_feature", banner_pattern = "banner_pattern", painting_variant = "painting_variant",
}
-- The owners.tsv kinds that are also known as another kind.
local OWNER_KIND_ALIASES = {block = {"block", "item"}, block_item = {"item"}, spawn_egg = {"item"}}

local function add_known(known, kind, id)
    known[kind] = known[kind] or {}
    known[kind][id] = true
    if kind:match("^tag:") then known.tag[id] = true else known["*"][id] = true end
end

-- data/<ns>/<rest>.json -> registry ("loot_table", "worldgen/biome", "tags/block", ...) and "<ns>:<path>"
-- with the path below the registry directory only.
local function data_registry(r)
    local ns, rest = r:match("^([^/]+)/(.+)%.json$")
    if not ns then return nil end
    local parts = {}
    for p in rest:gmatch("[^/]+") do table.insert(parts, p) end
    local depth = 1
    if parts[1] == "tags" then
        -- two-segment tag registries: tags/worldgen/<registry>/, tags/twilight/<registry>/
        depth = (parts[2] == "worldgen" or parts[2] == "twilight") and 3 or 2
    elseif parts[1] == "worldgen" or parts[1] == "twilight" or parts[1] == "data_maps" then
        depth = 2
    end
    if #parts <= depth then return nil end
    return table.concat(parts, "/", 1, depth), ns .. ":" .. table.concat(parts, "/", depth + 1)
end

-- The kind name of a data registry directory ("worldgen/biome" -> "biome", "tags/block" -> "tag:block").
local function registry_kind(registry)
    local tag = registry:match("^tags/(.+)$")
    if tag then return "tag:" .. tag end
    return (registry:gsub("^worldgen/", ""))
end

-- The kind of a reviewed entry as known_ids keeps it: a tag ('#' reference or kind "tag:<registry>")
-- becomes "tag:<tag registry>" (biome -> tag:worldgen/biome), any other kind stays.
local function reviewed_kind(ref, kind)
    local reg = kind:match("^tag:(.+)$")
    if not reg and ref:sub(1, 1) == "#" then reg = kind end
    if reg then return "tag:" .. (TAG_REGISTRY[reg] or reg) end
    return kind
end

-- The reviewed references of tools/import/known-ids.json (F1, cfg.checks.knownIds): {ids = {ref ->
-- kind}, dangling = {ref -> {kind, reason}}, tagKeys = {key -> true}}, kinds by reviewed_kind. "ids"
-- are ids TF registers in code (codec types, texture locations, template pools), known by their kind;
-- "tfDangling" are references dangling in TF itself, kept verbatim for parity: a warning, never an error.
local function load_reviewed(projectdir, cfg)
    local reviewed = {ids = {}, dangling = {}, tagKeys = {}}
    local rel_file = cfg.checks.knownIds
    local file = rel_file and path.join(projectdir, rel_file)
    if not file or not os.isfile(file) then return reviewed end
    local doc = load_json(file)
    if type(doc) ~= "table" then return reviewed end
    for ref, e in json_pairs(doc.ids or {}) do
        if type(e) ~= "table" or type(e.kind) ~= "string" then fail("%s: ids.%s has no kind", rel_file, ref)
        else reviewed.ids[ref] = reviewed_kind(ref, e.kind) end
    end
    for ref, e in json_pairs(doc.tfDangling or {}) do
        if type(e) ~= "table" or type(e.kind) ~= "string" then fail("%s: tfDangling.%s has no kind", rel_file, ref)
        else reviewed.dangling[ref] = {kind = reviewed_kind(ref, e.kind), reason = type(e.reason) == "string" and e.reason or ""} end
    end
    for key, _ in json_pairs(doc.tagValuedKeys or {}) do reviewed.tagKeys[key] = true end
    return reviewed
end

-- Every "twilightforest:..." string value in BP / data JSON must name something that exists, of the
-- kind its key implies: `"block": "twilightforest:naga"` fails when naga is only an entity. The value
-- of a tag-valued key names a tag ('#' or not) of the registry its context gives (a recipe's "tag" is
-- an item tag, generates_on a block tag). A reference listed as tfDangling (dangling in TF itself) of
-- the same kind is a warning.
local function check_dangling(jsonfiles, known, cfg, datadir, reviewed)
    local nss = cfg.checks.danglingNamespaces or {}
    local count = 0
    local tagkeys = {}
    for k, _ in pairs(TAG_VALUED_KEYS) do tagkeys[k] = true end
    for k, _ in pairs(reviewed.tagKeys) do tagkeys[k] = true end
    -- the tag registry a kind names as a tag ("tag:item" and "item" -> "item"), nil when unknown
    local function tag_registry(k)
        return k:match("^tag:") and k:sub(5) or TAG_REGISTRY[k]
    end
    local function exists(id, istag, kinds)
        if not kinds then
            return (istag and known.tag or known["*"])[id] ~= nil
        end
        for _, k in ipairs(kinds) do
            local set
            if istag then
                local reg = tag_registry(k)
                set = reg and known["tag:" .. reg] or known.tag
            else
                set = known[k]
            end
            if set and set[id] then return true end
        end
        return false
    end
    local function kinds_text(kinds, istag)
        if not kinds then return istag and "tag" or "id of any kind" end
        if not istag then return table.concat(kinds, " / ") end
        local regs = {}
        for _, k in ipairs(kinds) do table.insert(regs, tag_registry(k) or k) end
        return table.concat(table.unique(regs), " / ") .. " tag"
    end
    -- A tfDangling entry covers a reference of its own kind only: a tag entry ("tag:<registry>")
    -- a tag reference, an id entry an id reference of one of the kinds the key allows.
    local function dangling_entry(v, id, hash, istag, kinds)
        local d = reviewed.dangling[v] or (istag and reviewed.dangling[hash and id or ("#" .. id)]) or nil
        if not d then return nil end
        local dtag = d.kind:match("^tag:(.+)$")
        if (dtag ~= nil) ~= (istag == true) then return nil end
        if not kinds then return d end
        for _, k in ipairs(kinds) do
            if (istag and (tag_registry(k) == nil or tag_registry(k) == dtag)) or (not istag and k == d.kind) then return d end
        end
        return nil
    end
    -- ctx: {file, values = kinds of tag "values", id = kinds of "id" keys, parent = kinds of "parent",
    -- tag = kinds of the "tag" keys of the file (a recipe's ingredients name item tags)};
    -- tagref: the value of a tag-valued key (a tag even without '#'); parentkey: the key v is the value of
    local function visit(v, ctx, kinds, tagref, parentkey)
        if type(v) == "string" then
            local hash = v:sub(1, 1) == "#"
            -- a tag: '#', the value of a tag-valued key, or a value whose kinds are all tags
            -- (a loot entry {"type": "minecraft:tag", "name": id})
            local alltags = kinds ~= nil and #kinds > 0
            for _, k in ipairs(kinds or {}) do
                if not k:match("^tag:") then alltags = false end
            end
            local istag = hash or tagref == true or alltags
            local s = hash and v:sub(2) or v
            for _, ns in ipairs(nss) do
                local id = s:match("^(" .. ns .. ":[a-z0-9_/%.%-]+)$")
                if id then
                    count = count + 1
                    if not exists(id, istag, kinds) then
                        local d = dangling_entry(v, id, hash, istag, kinds)
                        if d then
                            warn("dangling reference '%s' (%s) in %s: dangling in TF itself, kept for parity (tools/import/known-ids.json tfDangling): %s",
                                v, d.kind, ctx.file, d.reason)
                        else
                            fail("dangling reference '%s' (not a known %s) in %s", v, kinds_text(kinds, istag), ctx.file)
                        end
                    end
                end
            end
        elseif type(v) == "table" then
            local entry = type(v.type) == "string" and LOOT_ENTRY_KINDS[v.type] or nil
            local blocktag = (type(v.type) == "string" and BLOCK_TAG_TYPES[v.type])
                or (type(v.predicate_type) == "string" and BLOCK_TAG_TYPES[v.predicate_type])
            for k, x in json_pairs(v) do
                if type(k) == "number" then
                    visit(x, ctx, kinds, tagref, parentkey) -- list elements keep the list's kinds
                elseif DEFINING_KEYS[k] then
                    -- defines ids (see known_ids)
                elseif tagkeys[k] then
                    if not (k == "tags" and is_bp_recipe_key(parentkey)) then
                        local sub = TAG_KEY_KINDS[k]
                        if k == "tag" then sub = blocktag and {"tag:block"} or ctx.tag end
                        visit(x, ctx, sub, true, k)
                    end
                else
                    local sub = REF_KEY_KINDS[k]
                    if entry and (k == "name" or k == "value") then sub = entry
                    elseif k == "values" and ctx.values then sub = ctx.values
                    elseif k == "id" and (ctx.id or kinds) then sub = ctx.id or kinds -- {"id": ...} of a typed list
                    elseif k == "parent" and ctx.parent then sub = ctx.parent end
                    visit(x, ctx, sub, false, k)
                end
            end
        end
    end
    for _, file in ipairs(jsonfiles) do
        local doc = try_json(file)
        if doc then
            local ctx = {file = file}
            local indata = datadir and norm(file):find(norm(datadir) .. "/", 1, true) == 1
            local registry = indata and data_registry(rel(file, datadir)) or nil
            if registry then
                local tagreg = registry:match("^tags/(.+)$")
                -- values of a tag registry without a kind here are checked against every kind
                if tagreg and REGISTRY_KIND[tagreg] then ctx.values = {REGISTRY_KIND[tagreg]} end
                if registry == "recipe" then ctx.id, ctx.tag = {"item"}, {"tag:item"} end
                if registry == "advancement" then ctx.parent = {"advancement"} end
            elseif type(doc) == "table" and not is_array(doc) and norm(file):find("/recipes/", 1, true) then
                -- a Bedrock recipe (a BP's recipes/): its ingredients' tags are item tags
                for k, _ in json_pairs(doc) do
                    if is_bp_recipe_key(k) then ctx.tag = {"tag:item"} end
                end
            end
            visit(doc, ctx, nil, false, nil)
        end
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
    -- Hand-made files derived from TF's CC BY-NC-SA assets live under a "tf-derived/" directory
    -- next to a SOURCES.json naming their TF source files (AGENTS.md, assets/LICENSE). They stay
    -- CC BY-NC-SA 4.0: the pack lists them in tf-derived-sources.txt beside LICENSE-ASSETS.txt.
    local derived = {}
    for _, r in ipairs(table.keys(files)) do
        local file = files[r]
        local prefix, sub = r:match("^(.-tf%-derived)/(.+)$")
        if prefix and sub == "SOURCES.json" then
            files[r] = nil -- attribution data, not pack content
        elseif prefix then
            local srcfile = path.join(projectdir, pack.manual, prefix, "SOURCES.json")
            local sources = os.isfile(srcfile) and load_json(srcfile) or nil
            local from = type(sources) == "table" and sources[sub] or nil
            if type(from) == "string" then from = {from} end
            if type(from) ~= "table" or #from == 0 then
                fail("%s is TF-derived but %s does not name its TF source file(s)", rel(file, projectdir), rel(srcfile, projectdir))
            else
                derived[r] = from
            end
        end
    end
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
                -- the merge writes the whole file: a plain file of the same path would be lost
                if files[outrel] then
                    fail("%s is the merge output of %s fragments: move its content into a fragment (it would be overwritten)",
                        rel(files[outrel], projectdir), spec.glob)
                end
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
    -- attribution: the licences of the pack's content (TF-derived assets are CC BY-NC-SA 4.0)
    if not pack.test then
        for _, l in ipairs(cfg.packLicenseFiles or {}) do
            local src = path.join(projectdir, l.from)
            if os.isfile(src) then os.cp(src, path.join(dst, l.to)) else fail("licence file %s is missing", l.from) end
        end
    end
    if not table.empty(derived) then
        -- plain text: the game would try to read a JSON file at the pack root
        local lines = {"# TF-derived files of this pack (CC BY-NC-SA 4.0, see LICENSE-ASSETS.txt): <file> <- <TF source files>"}
        local rels = table.keys(derived)
        table.sort(rels)
        for _, r in ipairs(rels) do table.insert(lines, r .. " <- " .. table.concat(derived[r], ", ")) end
        io.writefile(path.join(dst, "tf-derived-sources.txt"), table.concat(lines, "\n") .. "\n")
    end
    return dst, jsons
end

-- The pack paths units are granted (checks.packPathProbes of merges.json): each probe writes a
-- file at `file` (a manual-layer path, assets/pack/<root>/...) into a scratch project, builds that
-- pack with build_pack and fails unless the file arrives at `out` in the pack (same bytes) and
-- docs/spec/owners.tsv gives the path to `owner` (the rule tools/ci/ownership.ps1 applies). It proves
-- a granted directory is neither dropped by the assembly (fragment directories, "*.d", are merged,
-- never copied) nor unowned, before the unit's first file lands there. Returns the probe count.
local function probe_pack_paths(projectdir, cfg, owners)
    local probes = cfg.checks.packPathProbes or {}
    if #probes == 0 then return 0 end
    local scratch = os.tmpfile() .. ".packprobe"
    os.tryrm(scratch)
    for i, probe in ipairs(probes) do
        local pack
        for _, p in ipairs(cfg.packs) do
            if not p.test and probe.file:sub(1, #p.manual + 1) == p.manual .. "/" then pack = p end
        end
        if not pack then
            fail("packPathProbes: %s is not under the manual directory of a shipped pack", probe.file)
        else
            local proj = path.join(scratch, tostring(i))
            local manifest = path.join(projectdir, pack.manual, "manifest.json")
            os.mkdir(path.join(proj, pack.manual))
            os.cp(manifest, path.join(proj, pack.manual, "manifest.json"))
            local text = string.format('{"gf_pack_probe": "%s"}\n', probe.file)
            os.mkdir(path.directory(path.join(proj, probe.file)))
            io.writefile(path.join(proj, probe.file), text)
            -- test = true: no licence files (they are copied from the real project, not the scratch one)
            local scratch_pack = {name = pack.name, dir = pack.dir, root = pack.root, manual = pack.manual, test = true}
            local dst = build_pack(proj, cfg, scratch_pack, path.join(proj, "out"), {0, 0, 0}, false)
            local got = path.join(dst, probe.out)
            if not os.isfile(got) then
                fail("packPathProbes: %s did not reach %s/%s in the assembled pack", probe.file, pack.name, probe.out)
            elseif io.readfile(got) ~= text then
                fail("packPathProbes: %s reached %s/%s with other content", probe.file, pack.name, probe.out)
            end
            local owner, why = owner_of(owners, probe.file)
            if owner ~= probe.owner then
                fail("packPathProbes: %s is owned by %s in %s, expected %s", probe.file, owner or why or "nobody",
                    cfg.checks.owners, probe.owner)
            end
        end
    end
    os.tryrm(scratch)
    return #probes
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
                        for k, v in json_pairs(doc) do
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
            -- the two-segment registries (worldgen/biome, twilight/wood_palettes; see data_registry)
            -- keep their slash: the namespace is the segment after the registry
            local registry, ns, tagpath = key:match("^(worldgen/[^/]+)/([^/]+)/(.+)$")
            if not registry then registry, ns, tagpath = key:match("^(twilight/[^/]+)/([^/]+)/(.+)$") end
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

-- Ids that exist, per kind (see check_dangling): BP identifiers, data files by registry, tags,
-- owners.tsv ids, the reviewed ids of tools/import/known-ids.json by their kind.
local BP_KINDS = {["minecraft:block"] = {"block", "item"}, ["minecraft:item"] = {"item"}, ["minecraft:entity"] = {"entity"}}
local function known_ids(bpdirs, outdir, owners, reviewed)
    local known = {["*"] = {}, tag = {}}
    for _, bpdir in ipairs(bpdirs) do
        for _, file in ipairs(os.files(path.join(bpdir, "**.json"))) do
            -- every BP definition ({"minecraft:<type>": {"description": {"identifier": id}}}): blocks,
            -- items and entities by kind, the rest (features, feature rules, ...) as "bp:<type>"
            local doc = try_json(file)
            if type(doc) == "table" then
                for key, def in json_pairs(doc) do
                    local id = type(key) == "string" and key:match("^minecraft:") and type(def) == "table" and type(def.description) == "table"
                        and def.description.identifier or nil
                    if type(id) == "string" then
                        for _, kind in ipairs(BP_KINDS[key] or {"bp:" .. key:sub(11)}) do add_known(known, kind, id) end
                        -- the Bedrock item tags an item declares (minecraft:tags) are item tags
                        local comps = key == "minecraft:item" and type(def.components) == "table" and def.components or nil
                        local tags = comps and type(comps["minecraft:tags"]) == "table" and comps["minecraft:tags"].tags or nil
                        if type(tags) == "table" then
                            for _, t in ipairs(tags) do
                                if type(t) == "string" then add_known(known, "tag:item", (t:gsub("^#", ""))) end
                            end
                        end
                    end
                end
            end
        end
    end
    local datadir = path.join(outdir, "data")
    if os.isdir(datadir) then
        for _, file in ipairs(os.files(path.join(datadir, "**.json"))) do
            -- data/<ns>/<registry>/<path>.json -> <ns>:<path> of that registry's kind (the path below
            -- the registry directory only: loot_table/entities/naga.json is twilightforest:entities/naga)
            local registry, id = data_registry(rel(file, datadir))
            if registry then add_known(known, registry_kind(registry), id) end
        end
    end
    for kind, ids in pairs(owners.kinds) do
        for id, _ in pairs(ids) do
            if not id:find("[%*%?{]") then
                local reg, tagid = id:match("^(.+)/#(.+)$") -- tag rows: "<registry>/#<ns>:<path>"
                if reg then
                    if kind == "tag" then add_known(known, "tag:" .. reg, tagid) end
                else
                    local entryreg, entryid = id:match("^([^:]+)/([^/]+:.+)$") -- "<registry>/<ns>:<path>"
                    if kind == "registry_entry" and entryreg then
                        add_known(known, "twilight/" .. entryreg, entryid)
                    elseif id:find(":") then
                        for _, k in ipairs(OWNER_KIND_ALIASES[kind] or {kind}) do add_known(known, k, id) end
                    elseif kind == "sound_event" then
                        add_known(known, kind, "twilightforest:" .. id) -- registered as twilightforest:<event>
                    end
                end
            end
        end
    end
    for ref, kind in pairs(reviewed.ids) do add_known(known, kind, (ref:gsub("^#", ""))) end
    return known
end

local function report(label)
    for _, n in ipairs(notes) do
        if os.getenv("GF_ASSEMBLE_VERBOSE") then cprint("${dim}[assemble] %s", n) end
    end
    for _, w in ipairs(warnings) do cprint("${yellow}[assemble] warning: %s", w) end
    if #errors > 0 then
        for _, e in ipairs(errors) do cprint("${bright red}[assemble] %s", e) end
        raise("assemble failed with %d error(s) (%s)", #errors, label)
    end
end

-- Packs, lang, data and checks into `outdir` (no binaries).
local function assemble_content(projectdir, outdir, version)
    local cfg = load_config(projectdir)
    errors, warnings, notes = {}, {}, {}
    local with_tests = os.getenv("GF_NO_TEST_PACKS") ~= "1"
    local families_dir = path.join(projectdir, "tools", "families")
    if version == nil then
        local major, minor, patch = mod_version()
        version = {major, minor, patch}
        if major == 0 and minor == 0 and patch == 0 then
            -- what shapes the packs: the assets and the family generator's code and fixtures (a new
            -- version: no stale cached pack); not its docs or its command line (run.ps1, run.lua)
            local files = os.files(path.join(projectdir, "assets", "**"))
            for _, f in ipairs(os.files(path.join(families_dir, "**.lua"))) do
                if rel(f, families_dir) ~= "run.lua" then table.insert(files, f) end
            end
            table.join2(files, os.files(path.join(families_dir, "fixtures", "**")))
            version = {0, 0, pack_stamp(projectdir, files)}
        end
    end
    merge_lang(projectdir, cfg, outdir)
    local datajsons = copy_data(projectdir, cfg, outdir, with_tests)
    local bpdirs, alljsons, shipped_dirs = {}, {}, {}
    for _, pack in ipairs(cfg.packs) do
        if with_tests or not pack.test then
            local dst, jsons = build_pack(projectdir, cfg, pack, outdir, version, not pack.test)
            if pack.root == "bp" then table.insert(bpdirs, dst) end
            if not pack.test then
                table.insert(shipped_dirs, dst)
                for _, j in ipairs(jsons) do table.insert(alljsons, j) end
            end
        else
            os.tryrm(path.join(outdir, pack.dir, pack.name))
        end
    end
    for _, j in ipairs(datajsons) do table.insert(alljsons, j) end
    -- F11's block family generator (tools/families, contract "families+blockevents"): the family
    -- data (data/gf/families) as Bedrock block definitions in the packs just built, and the gf_test
    -- fixture families; checked below with the rest. Nothing happens until tools/families exists.
    local families_summary = "no family generator"
    if os.isfile(path.join(families_dir, "families.lua")) then
        families_summary = "family generator failed"
        -- a raise inside the generator is one more error: the ones collected so far still print
        local r = try {
            function ()
                local families = import("families", {rootdir = families_dir, anonymous = true})
                local res = families.generate(projectdir, outdir, {no_fixtures = not with_tests})
                for _, k in ipairs({"blocks", "skipped", "fixtures"}) do res[k] = res[k] or 0 end
                -- the entries are read again only when nothing was generated, to tell a generator
                -- that silently made nothing from data without entries (generate reported the
                -- data's read errors already)
                res.entries = 0
                if res.blocks + res.skipped == 0 and families.families then res.entries = #(families.families(outdir)) end
                return res
            end,
            catch {
                function (e) fail("families: tools/families/families.lua raised: %s", tostring(e)) end
            }
        }
        if r then
            for _, e in ipairs(r.errors or {}) do fail("%s", e) end
            -- every pack directory was just rebuilt, which drops the generator's "generated" marker:
            -- a marked pack here is a stale output directory, and its blocks went unchecked
            if r.already then fail("families: the packs in %s were marked as generated before this assemble", outdir) end
            -- a generator that silently makes nothing would ship none of the family blocks
            local fixture_files = os.files(path.join(families_dir, "fixtures", "**.json"))
            if not r.already and r.entries > 0 and r.blocks + r.skipped == 0 then
                fail("families: %d family entries in data/gf/families but no block generated", r.entries)
            end
            if not r.already and with_tests and #fixture_files > 0 and r.fixtures == 0 then
                fail("families: %d fixture file(s) in tools/families/fixtures but no fixture block generated", #fixture_files)
            end
            families_summary = string.format("%d family blocks (%d hand-made), %d family fixtures", r.blocks, r.skipped, r.fixtures)
            -- what the generator added to the shipped packs (behavior: blocks, the sign text entity,
            -- loot tables; resource: block models, the sign text client entity, its additions to
            -- blocks.json and terrain_texture.json) and to data/gf (the container declarations F10
            -- reads, the client's connected texture corners) is checked for dangling references too
            local seen = {}
            for _, j in ipairs(alljsons) do seen[norm(j)] = true end
            local dirs = table.join(shipped_dirs, {path.join(outdir, "data", "gf")})
            for _, dir in ipairs(dirs) do
                for _, f in ipairs(os.isdir(dir) and os.files(path.join(dir, "**.json")) or {}) do
                    if not seen[norm(f)] and path.filename(f) ~= "manifest.json" then
                        seen[norm(f)] = true
                        table.insert(alljsons, f)
                    end
                end
            end
        end
    end
    local permutations = check_blocks(bpdirs, cfg)
    local owners = load_owners(projectdir, cfg)
    local owned = check_owners(projectdir, cfg, owners)
    local probed = probe_pack_paths(projectdir, cfg, owners)
    local langkeys = check_lang_owners(projectdir, owners)
    local reviewed = load_reviewed(projectdir, cfg)
    local refs = check_dangling(alljsons, known_ids(bpdirs, outdir, owners, reviewed), cfg, path.join(outdir, "data"), reviewed)
    local scanned = check_arr(projectdir, cfg)
    report(outdir)
    return string.format("%d block permutations, %s, %d owned files, %d pack path probes, %d owned lang keys, %d references (%d warning(s)), %d files hash-checked",
        permutations, families_summary, owned, probed, langkeys, refs, #warnings, scanned)
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
