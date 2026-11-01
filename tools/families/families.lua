-- GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
-- Contract "families+blockevents" (F11): the assemble-time block family generator.
--
-- About 400 TF blocks are plain variations of vanilla shapes (stairs, slabs, fences, doors, logs,
-- leaves, signs, chests, ...). Units declare them as data in assets/data/families/<u>.json
-- (assembled into data/gf/families/) and this module turns every entry into a Bedrock block
-- definition, after the packs are assembled:
--
--     [{"id": "twilightforest:canopy_stairs", "family": "stairs",
--       "params": {"base": "twilightforest:canopy_planks", "textures": "twilightforest_canopy_planks"}}]
--
-- * The BP definition of an id that a hand-made file defines (assets/pack/bp/**, assets/test/bp/**)
--   is kept: the manual layer wins. A generated skeleton (F1, assets/gen/import/<u>/bp/**) is
--   reshaped in place: its identifier, menu category and base components (mining, explosion,
--   map colour, loot, display name, flammability, light) are kept, the family adds the geometry,
--   material instances, collision, states, traits and permutations.
-- * Shared family geometries go to the RP (models/blocks/gf_families/*.geo.json), block sounds to
--   blocks.json (only ids without an entry), double slab loot tables to the BP.
-- * The fixture families of the selftests (tools/families/fixtures/*.json, gf_test:* ids) go to
--   the gf_test packs of development builds, with their fixture data in data/gf_test/families/.
--
-- The block state names are the schema the runtime (src/feature/families/Families.h) reads:
--   traits     minecraft:cardinal_direction, minecraft:block_face, minecraft:vertical_half,
--              minecraft:corner (stairs), minecraft:facing_direction
--   custom     twilightforest:<java property> as integers 0..n (rule: integer states list 0..n),
--              except the string enums type (chests) and lighting (lightable)
--
-- Entry points: generate(projectdir, outdir, opts) (tools/xmake/assemble.lua after the packs are
-- built, or tools/families/run.lua on an assembled directory); families() for the checks.

import("core.base.json")

local NS = "twilightforest:"
local GEO_DIR = "models/blocks/gf_families"
local FORMAT = "1.26.0"

local DIRS = {"north", "east", "south", "west"}
local DX = {north = 0, east = 1, south = 0, west = -1, up = 0, down = 0}
local DZ = {north = -1, east = 0, south = 1, west = 0, up = 0, down = 0}
local OPP = {north = "south", south = "north", east = "west", west = "east", up = "down", down = "up"}
local CW = {north = "east", east = "south", south = "west", west = "north"}
local CCW = {north = "west", west = "south", south = "east", east = "north"}
local FACES6 = {"down", "up", "north", "south", "west", "east"}

-- ---- small helpers -------------------------------------------------------------------------------

local function arr(t)
    t = t or {}
    if json.mark_as_array then json.mark_as_array(t) end
    return t
end

local function copy(t)
    if type(t) ~= "table" then return t end
    local out = {}
    for k, v in pairs(t) do out[k] = copy(v) end
    if json.is_marked_as_array and json.is_marked_as_array(t) then arr(out) end
    return out
end

local function sorted_keys(t)
    local keys = table.keys(t)
    table.sort(keys, function (a, b) return tostring(a) < tostring(b) end)
    return keys
end

local function ints(n) -- [0, 1, ..., n]
    local out = arr({})
    for i = 0, n do table.insert(out, i) end
    return out
end

local function q(state) return "q.block_state('" .. state .. "')" end
local function is(state, v)
    if type(v) == "string" then return q(state) .. " == '" .. v .. "'" end
    if type(v) == "boolean" then v = v and 1 or 0 end
    return q(state) .. " == " .. tostring(v)
end
local function all(...)
    local parts = {}
    for _, p in ipairs({...}) do if p and p ~= "" then table.insert(parts, "(" .. p .. ")") end end
    if #parts == 0 then return "1" end
    return table.concat(parts, " && ")
end
local function any(list)
    local parts = {}
    for _, p in ipairs(list) do table.insert(parts, "(" .. p .. ")") end
    if #parts == 0 then return "0" end
    return table.concat(parts, " || ")
end

local function try_json(file)
    return try { function () return json.loadfile(file) end }
end

local function norm(p) return (p:gsub("\\", "/")) end

-- ---- geometry ------------------------------------------------------------------------------------

-- Per-face UVs of a box (x0..x1, y0..y1, z0..z1 in block pixels) on a 16x16 texture: every face
-- shows the part of the texture it covers in the world (Java's default element UVs).
local function face_uvs(b, mat, only)
    local x0, y0, z0, x1, y1, z1 = b[1], b[2], b[3], b[4], b[5], b[6]
    local f = {
        north = {uv = {16 - x1, 16 - y1}, uv_size = {x1 - x0, y1 - y0}},
        south = {uv = {x0, 16 - y1}, uv_size = {x1 - x0, y1 - y0}},
        east = {uv = {16 - z1, 16 - y1}, uv_size = {z1 - z0, y1 - y0}},
        west = {uv = {z0, 16 - y1}, uv_size = {z1 - z0, y1 - y0}},
        up = {uv = {x0, z0}, uv_size = {x1 - x0, z1 - z0}},
        down = {uv = {x0, 16 - z1}, uv_size = {x1 - x0, z1 - z0}},
    }
    local out = {}
    for name, uv in pairs(f) do
        if not only or only[name] then
            if mat then uv.material_instance = mat end
            out[name] = uv
        end
    end
    return out
end

-- A cube of the box b = {x0, y0, z0, x1, y1, z1} (pixels, world aligned).
local function cube(b, mat, only)
    return {origin = {b[1] - 8, b[2], b[3] - 8}, size = {b[4] - b[1], b[5] - b[2], b[6] - b[3]}, uv = face_uvs(b, mat, only)}
end

-- A cube with Java's box UV layout (entity textures: chests, signs) at texture offset (u, v).
local function box_cube(b, u, v, mirror)
    local c = {origin = {b[1] - 8, b[2], b[3] - 8}, size = {b[4] - b[1], b[5] - b[2], b[6] - b[3]}, uv = {u, v}}
    if mirror then c.mirror = true end
    return c
end

local function bone(name, cubes, pivot)
    return {name = name, pivot = pivot or {0, 0, 0}, cubes = arr(cubes)}
end

local function geo_doc(id, bones, tw, th)
    return {
        format_version = "1.16.0",
        ["minecraft:geometry"] = arr({{
            description = {
                identifier = id, texture_width = tw or 16, texture_height = th or 16,
                visible_bounds_width = 3, visible_bounds_height = 3, visible_bounds_offset = {0, 0.5, 0},
            },
            bones = arr(bones),
        }}),
    }
end

-- A box as a collision / selection box component.
local function cbox(b)
    return {origin = {b[1] - 8, b[2], b[3] - 8}, size = {b[4] - b[1], math.min(b[5], 16) - b[2], b[6] - b[3]}}
end

-- the bounding box of several boxes
local function bounds(boxes)
    local o = {16, 16, 16, 0, 0, 0}
    for _, b in ipairs(boxes) do
        for i = 1, 3 do o[i] = math.min(o[i], b[i]) end
        for i = 4, 6 do o[i] = math.max(o[i], b[i]) end
    end
    return o
end

-- rotate a box (pixels) clockwise about the vertical axis seen from above, `n` quarter turns:
-- north -> east -> south -> west
local function rot_y(b, n)
    n = n % 4
    for _ = 1, n do
        -- (x, z) -> (16 - z, x)
        b = {16 - b[6], b[2], b[1], 16 - b[3], b[5], b[4]}
    end
    return b
end
local QUARTERS = {north = 0, east = 1, south = 2, west = 3}

-- ---- textures ------------------------------------------------------------------------------------

-- params.textures (string or {"*", up, down, north, south, east, west, side, end, top, bottom, ...})
-- merged over the textures of the skeleton definition (its material instances).
local function textures_of(entry, skeleton)
    local t = {}
    local mi = skeleton and skeleton["minecraft:block"] and skeleton["minecraft:block"].components
        and skeleton["minecraft:block"].components["minecraft:material_instances"]
    if type(mi) == "table" then
        for k, v in pairs(mi) do
            if type(v) == "table" and type(v.texture) == "string" then t[k] = v.texture end
        end
    end
    local p = entry.params.textures
    if type(p) == "string" then
        t = {["*"] = p}
    elseif type(p) == "table" then
        for k, v in pairs(p) do t[k] = v end
    end
    return t
end

local function tex(t, ...)
    for _, k in ipairs({...}) do
        if t[k] then return t[k] end
    end
    return t["*"] or t.side or t.up or "missing"
end

local function mat(texture, method, extra)
    local m = {texture = texture, render_method = method or "opaque"}
    if extra then for k, v in pairs(extra) do m[k] = v end end
    return m
end

-- ---- block documents -----------------------------------------------------------------------------

local KEEP = {
    "minecraft:destructible_by_mining", "minecraft:destructible_by_explosion", "minecraft:map_color",
    "minecraft:loot", "minecraft:display_name", "minecraft:flammable", "minecraft:light_emission",
    "minecraft:friction", "minecraft:crafting_table", "minecraft:destruction_particles",
}

-- The starting document of `entry`: the skeleton's base components, or the params / defaults.
local function base_doc(entry, skeleton, basedoc)
    local id = entry.id
    local p = entry.params
    local comps = {}
    local src = skeleton or basedoc
    local scomps = src and src["minecraft:block"] and src["minecraft:block"].components or {}
    for _, k in ipairs(KEEP) do
        if scomps[k] ~= nil then comps[k] = copy(scomps[k]) end
    end
    for k, v in pairs(scomps) do
        if type(k) == "string" and k:match("^tag:") then comps[k] = copy(v) end
    end
    if skeleton == nil and basedoc ~= nil then comps["minecraft:loot"] = nil end -- the base block's loot is not ours
    if p.hardness then comps["minecraft:destructible_by_mining"] = {seconds_to_destroy = p.hardness} end
    if p.resistance then comps["minecraft:destructible_by_explosion"] = {explosion_resistance = p.resistance} end
    if p.map_color then comps["minecraft:map_color"] = p.map_color end
    if p.light then comps["minecraft:light_emission"] = p.light end
    if p.loot then comps["minecraft:loot"] = p.loot end
    if p.tool then comps["tag:minecraft:is_" .. p.tool .. "_item_destructible"] = {} end
    if comps["minecraft:destructible_by_mining"] == nil then comps["minecraft:destructible_by_mining"] = {seconds_to_destroy = 1} end
    local desc = {identifier = id}
    local sdesc = skeleton and skeleton["minecraft:block"] and skeleton["minecraft:block"].description
    desc.menu_category = (sdesc and copy(sdesc.menu_category)) or p.menu_category or {category = "construction"}
    return {
        format_version = FORMAT,
        ["minecraft:block"] = {description = desc, components = comps, permutations = arr({})},
    }
end

local function block_of(doc) return doc["minecraft:block"] end
local function add_state(doc, name, values)
    local d = block_of(doc).description
    d.states = d.states or {}
    d.states[name] = values
end
local function add_trait(doc, name, value)
    local d = block_of(doc).description
    d.traits = d.traits or {}
    d.traits[name] = value
end
local function perm(doc, condition, components)
    table.insert(block_of(doc).permutations, {condition = condition, components = components})
end
local function comps(doc) return block_of(doc).components end

local function geometry(doc, id, vis)
    if vis then
        comps(doc)["minecraft:geometry"] = {identifier = id, bone_visibility = vis}
    else
        comps(doc)["minecraft:geometry"] = id
    end
end

-- ---- families ------------------------------------------------------------------------------------
--
-- Each builder fills `doc` for `entry` (textures `t`) and registers the geometries it needs in
-- `ctx.geos`. `ctx.all` lists every entry by family (stairs corner with each other).

local builders = {}

local function full_block(doc, t, method, extra)
    geometry(doc, "minecraft:geometry.full_block")
    comps(doc)["minecraft:material_instances"] = {
        ["*"] = mat(tex(t, "*", "side"), method, extra),
        up = mat(tex(t, "up", "end", "top", "*"), method, extra),
        down = mat(tex(t, "down", "end", "bottom", "*"), method, extra),
        north = mat(tex(t, "north", "side", "*"), method, extra),
        south = mat(tex(t, "south", "side", "*"), method, extra),
        east = mat(tex(t, "east", "side", "*"), method, extra),
        west = mat(tex(t, "west", "side", "*"), method, extra),
    }
end

builders.cube = function (doc, e, t) full_block(doc, t, e.params.render_method) end

-- stairs (Java StairBlock): minecraft:vertical_half + minecraft:corner_and_cardinal_direction; the
-- game keeps the corner shape. Geometry: two slab halves and eight quarters (Levitate's layout).
local QUARTER_POS = {nw = {0, 0}, ne = {8, 0}, sw = {0, 8}, se = {8, 8}}
local CORNERS = {
    none = {{1, 1}, {1, -1}}, inner_left = {{1, 1}, {1, -1}, {-1, 1}}, inner_right = {{1, 1}, {1, -1}, {-1, -1}},
    outer_left = {{1, 1}}, outer_right = {{1, -1}},
}
local function quarter(dir, back, left)
    local x = back * DX[dir] + left * DX[CCW[dir]]
    local z = back * DZ[dir] + left * DZ[CCW[dir]]
    return (z < 0 and "n" or "s") .. (x < 0 and "w" or "e")
end

local function stairs_geo(ctx)
    local id = "geometry.gf_families.stairs"
    if not ctx.geos[id] then
        local bones = {bone("slab_bottom", {cube({0, 0, 0, 16, 8, 16})}), bone("slab_top", {cube({0, 8, 0, 16, 16, 16})})}
        for _, qn in ipairs({"nw", "ne", "sw", "se"}) do
            local p = QUARTER_POS[qn]
            table.insert(bones, bone("upper_" .. qn, {cube({p[1], 8, p[2], p[1] + 8, 16, p[2] + 8})}))
            table.insert(bones, bone("lower_" .. qn, {cube({p[1], 0, p[2], p[1] + 8, 8, p[2] + 8})}))
        end
        ctx.geos[id] = geo_doc(id, bones)
    end
    return id
end

builders.stairs = function (doc, e, t, ctx)
    local g = stairs_geo(ctx)
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(tex(t, "*", "side"), e.params.render_method)}
    geometry(doc, g)
    comps(doc)["minecraft:selection_box"] = cbox({0, 0, 0, 16, 16, 16})
    local corner_with = arr({})
    for _, other in ipairs(ctx.all.stairs or {}) do table.insert(corner_with, other) end
    add_trait(doc, "minecraft:placement_direction", {enabled_states = arr({"minecraft:corner_and_cardinal_direction"}), blocks_to_corner_with = corner_with})
    add_trait(doc, "minecraft:placement_position", {enabled_states = arr({"minecraft:vertical_half"})})
    for _, half in ipairs({"bottom", "top"}) do
        for _, dir in ipairs(DIRS) do
            for _, corner in ipairs({"none", "inner_left", "inner_right", "outer_left", "outer_right"}) do
                local vis = {slab_bottom = half == "bottom", slab_top = half == "top"}
                for qn, _ in pairs(QUARTER_POS) do vis["upper_" .. qn] = false; vis["lower_" .. qn] = false end
                local boxes = {half == "bottom" and {0, 0, 0, 16, 8, 16} or {0, 8, 0, 16, 16, 16}}
                for _, bl in ipairs(CORNERS[corner]) do
                    local qn = quarter(dir, bl[1], bl[2])
                    local level = half == "bottom" and "upper" or "lower"
                    vis[level .. "_" .. qn] = true
                    local p = QUARTER_POS[qn]
                    local y0 = half == "bottom" and 8 or 0
                    table.insert(boxes, {p[1], y0, p[2], p[1] + 8, y0 + 8, p[2] + 8})
                end
                local cboxes = arr({})
                for _, b in ipairs(boxes) do table.insert(cboxes, cbox(b)) end
                perm(doc, all(is("minecraft:vertical_half", half), is("minecraft:cardinal_direction", dir), is("minecraft:corner", corner)),
                    {["minecraft:geometry"] = {identifier = g, bone_visibility = vis}, ["minecraft:collision_box"] = cboxes})
            end
        end
    end
end

-- slab (Java SlabBlock): minecraft:vertical_half + twilightforest:double; the double slab is a
-- full block that drops two (its own loot table).
builders.slab = function (doc, e, t, ctx)
    local g = "geometry.gf_families.slab"
    ctx.geos[g] = ctx.geos[g] or geo_doc(g, {bone("bottom", {cube({0, 0, 0, 16, 8, 16})}), bone("top", {cube({0, 8, 0, 16, 16, 16})})})
    local side, top = tex(t, "side", "*"), tex(t, "up", "top", "end", "*")
    local m = e.params.render_method
    geometry(doc, g, {bottom = is("minecraft:vertical_half", "bottom"), top = is("minecraft:vertical_half", "top")})
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(side, m), up = mat(top, m), down = mat(tex(t, "down", "bottom", "end", "up", "*"), m)}
    comps(doc)["minecraft:collision_box"] = cbox({0, 0, 0, 16, 8, 16})
    comps(doc)["minecraft:selection_box"] = cbox({0, 0, 0, 16, 8, 16})
    add_state(doc, NS .. "double", ints(1))
    add_trait(doc, "minecraft:placement_position", {enabled_states = arr({"minecraft:vertical_half"})})
    perm(doc, all(is("minecraft:vertical_half", "top"), is(NS .. "double", 0)),
        {["minecraft:collision_box"] = cbox({0, 8, 0, 16, 16, 16}), ["minecraft:selection_box"] = cbox({0, 8, 0, 16, 16, 16})})
    local double = {
        ["minecraft:geometry"] = "minecraft:geometry.full_block",
        ["minecraft:collision_box"] = true, ["minecraft:selection_box"] = true,
        ["minecraft:material_instances"] = {["*"] = mat(tex(t, "double_side", "side", "*"), m), up = mat(top, m), down = mat(tex(t, "down", "bottom", "end", "up", "*"), m)},
    }
    if ctx.loot_double then double["minecraft:loot"] = ctx.loot_double(e) end
    perm(doc, is(NS .. "double", 1), double)
end

-- fence (Java FenceBlock): twilightforest:north/east/south/west, kept by the runtime with Java's
-- connection rules (src/feature/families/Shapes.cpp).
builders.fence = function (doc, e, t, ctx)
    local g = "geometry.gf_families.fence"
    if not ctx.geos[g] then
        ctx.geos[g] = geo_doc(g, {
            bone("post", {cube({6, 0, 6, 10, 16, 10})}),
            bone("north", {cube({7, 12, 0, 9, 15, 6}), cube({7, 6, 0, 9, 9, 6})}),
            bone("south", {cube({7, 12, 10, 9, 15, 16}), cube({7, 6, 10, 9, 9, 16})}),
            bone("west", {cube({0, 12, 7, 6, 15, 9}), cube({0, 6, 7, 6, 9, 9})}),
            bone("east", {cube({10, 12, 7, 16, 15, 9}), cube({10, 6, 7, 16, 9, 9})}),
        })
    end
    local vis = {post = true}
    for _, d in ipairs(DIRS) do
        add_state(doc, NS .. d, ints(1))
        vis[d] = is(NS .. d, 1)
    end
    geometry(doc, g, vis)
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(tex(t, "*", "side"), e.params.render_method)}
    -- Java's collision: the post and arms, 4 px wide (24 px high: the runtime's collision hook)
    for mask = 0, 15 do
        local boxes = {{6, 0, 6, 10, 16, 10}}
        local cond = {}
        for i, d in ipairs(DIRS) do
            local on = (mask >> (i - 1)) & 1 == 1
            table.insert(cond, is(NS .. d, on and 1 or 0))
            if on then
                local arm = ({north = {6, 0, 0, 10, 16, 6}, south = {6, 0, 10, 10, 16, 16}, west = {0, 0, 6, 6, 16, 10}, east = {10, 0, 6, 16, 16, 10}})[d]
                table.insert(boxes, arm)
            end
        end
        local cb = arr({})
        for _, b in ipairs(boxes) do table.insert(cb, cbox(b)) end
        perm(doc, all(table.unpack(cond)), {["minecraft:collision_box"] = cb, ["minecraft:selection_box"] = cbox(bounds(boxes))})
    end
end

-- pane (Java IronBarsBlock): post and arms 2 px thick.
builders.pane = function (doc, e, t, ctx)
    local g = "geometry.gf_families.pane"
    if not ctx.geos[g] then
        ctx.geos[g] = geo_doc(g, {
            bone("post", {cube({7, 0, 7, 9, 16, 9})}),
            bone("north", {cube({7, 0, 0, 9, 16, 7})}),
            bone("south", {cube({7, 0, 9, 9, 16, 16})}),
            bone("west", {cube({0, 0, 7, 7, 16, 9})}),
            bone("east", {cube({9, 0, 7, 16, 16, 9})}),
        })
    end
    local vis = {post = true}
    for _, d in ipairs(DIRS) do
        add_state(doc, NS .. d, ints(1))
        vis[d] = is(NS .. d, 1)
    end
    geometry(doc, g, vis)
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(tex(t, "*", "side"), e.params.render_method or "blend")}
    comps(doc)["minecraft:light_dampening"] = 0
    for mask = 0, 15 do
        local boxes = {{7, 0, 7, 9, 16, 9}}
        local cond = {}
        for i, d in ipairs(DIRS) do
            local on = (mask >> (i - 1)) & 1 == 1
            table.insert(cond, is(NS .. d, on and 1 or 0))
            if on then
                table.insert(boxes, ({north = {7, 0, 0, 9, 16, 9}, south = {7, 0, 7, 9, 16, 16}, west = {0, 0, 7, 9, 16, 9}, east = {7, 0, 7, 16, 16, 9}})[d])
            end
        end
        local cb = arr({})
        for _, b in ipairs(boxes) do table.insert(cb, cbox(b)) end
        perm(doc, all(table.unpack(cond)), {["minecraft:collision_box"] = cb, ["minecraft:selection_box"] = cbox(bounds(boxes))})
    end
end

-- wall (Java WallBlock): twilightforest:up and twilightforest:<dir>_wall (0 none, 1 low, 2 tall),
-- kept by the runtime.
builders.wall = function (doc, e, t, ctx)
    local g = "geometry.gf_families.wall"
    local sides = {north = {5, 0, 0, 11, 14, 8}, south = {5, 0, 8, 11, 14, 16}, west = {0, 0, 5, 8, 14, 11}, east = {8, 0, 5, 16, 14, 11}}
    if not ctx.geos[g] then
        local bones = {bone("post", {cube({4, 0, 4, 12, 16, 12})})}
        for _, d in ipairs(DIRS) do
            local b = sides[d]
            table.insert(bones, bone(d .. "_low", {cube(b)}))
            table.insert(bones, bone(d .. "_tall", {cube({b[1], b[2], b[3], b[4], 16, b[6]})}))
        end
        ctx.geos[g] = geo_doc(g, bones)
    end
    add_state(doc, NS .. "up", ints(1))
    local vis = {post = is(NS .. "up", 1)}
    for _, d in ipairs(DIRS) do
        add_state(doc, NS .. d .. "_wall", ints(2))
        vis[d .. "_low"] = is(NS .. d .. "_wall", 1)
        vis[d .. "_tall"] = is(NS .. d .. "_wall", 2)
    end
    geometry(doc, g, vis)
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(tex(t, "*", "side"), e.params.render_method)}
    -- collision (Java: 24 px high, the runtime's collision hook; the JSON boxes cap at 16)
    for up = 0, 1 do
        for mask = 0, 15 do
            local boxes, cond = {}, {is(NS .. "up", up)}
            if up == 1 or mask == 0 then table.insert(boxes, {4, 0, 4, 12, 16, 12}) end
            local conn = {}
            for i, d in ipairs(DIRS) do
                local on = (mask >> (i - 1)) & 1 == 1
                conn[d] = on
                table.insert(cond, on and (q(NS .. d .. "_wall") .. " > 0") or is(NS .. d .. "_wall", 0))
                if on then
                    local b = sides[d]
                    table.insert(boxes, {b[1], 0, b[3], b[4], 16, b[6]})
                end
            end
            local cb = arr({})
            for _, b in ipairs(boxes) do table.insert(cb, cbox(b)) end
            perm(doc, all(table.unpack(cond)), {["minecraft:collision_box"] = cb, ["minecraft:selection_box"] = cbox(bounds(boxes))})
        end
    end
end

-- fence gate (Java FenceGateBlock): minecraft:cardinal_direction (Java's facing), twilightforest:
-- open / in_wall / powered. Bones per orientation (world aligned, so the collision matches).
local function gate_bones(suffix, dy)
    local function sh(b) return {b[1], b[2] - dy, b[3], b[4], b[5] - dy, b[6]} end
    -- facing north / south: posts on x, the leaves across x when closed
    local posts_z = {cube(sh({0, 5, 7, 2, 16, 9})), cube(sh({14, 5, 7, 16, 16, 9}))}
    local closed_z = {cube(sh({6, 6, 7, 8, 15, 9})), cube(sh({2, 6, 7, 6, 9, 9})), cube(sh({2, 12, 7, 6, 15, 9})),
        cube(sh({8, 6, 7, 10, 15, 9})), cube(sh({10, 6, 7, 14, 9, 9})), cube(sh({10, 12, 7, 14, 15, 9}))}
    local function open_leaves(towards) -- leaves swung towards +z (south) or -z (north)
        local function l(x0, x1)
            if towards == "south" then
                return {cube(sh({x0, 6, 13, x1, 15, 15})), cube(sh({x0, 6, 9, x1, 9, 13})), cube(sh({x0, 12, 9, x1, 15, 13}))}
            end
            return {cube(sh({x0, 6, 1, x1, 15, 3})), cube(sh({x0, 6, 3, x1, 9, 7})), cube(sh({x0, 12, 3, x1, 15, 7}))}
        end
        local out = {}
        for _, c in ipairs(l(0, 2)) do table.insert(out, c) end
        for _, c in ipairs(l(14, 16)) do table.insert(out, c) end
        return out
    end
    local function turn(cubes) -- the same cubes for facing east / west (swap x and z)
        local out = {}
        for _, c in ipairs(cubes) do
            local o, s = c.origin, c.size
            local x0, z0 = o[1] + 8, o[3] + 8
            table.insert(out, cube({z0, o[2], x0, z0 + s[3], o[2] + s[2], x0 + s[1]}))
        end
        return out
    end
    local bones = {
        bone("posts_z" .. suffix, posts_z), bone("posts_x" .. suffix, turn(posts_z)),
        bone("closed_z" .. suffix, closed_z), bone("closed_x" .. suffix, turn(closed_z)),
        bone("open_south" .. suffix, open_leaves("south")), bone("open_north" .. suffix, open_leaves("north")),
        bone("open_east" .. suffix, turn(open_leaves("south"))), bone("open_west" .. suffix, turn(open_leaves("north"))),
    }
    return bones
end

builders.fence_gate = function (doc, e, t, ctx)
    local g = "geometry.gf_families.fence_gate"
    if not ctx.geos[g] then
        local bones = gate_bones("", 0)
        for _, b in ipairs(gate_bones("_low", 3)) do table.insert(bones, b) end
        ctx.geos[g] = geo_doc(g, bones)
    end
    for _, s in ipairs({"open", "in_wall", "powered"}) do add_state(doc, NS .. s, ints(1)) end
    add_trait(doc, "minecraft:placement_direction", {enabled_states = arr({"minecraft:cardinal_direction"})})
    local axis_z = any({is("minecraft:cardinal_direction", "north"), is("minecraft:cardinal_direction", "south")})
    local axis_x = any({is("minecraft:cardinal_direction", "east"), is("minecraft:cardinal_direction", "west")})
    local vis = {}
    for _, low in ipairs({false, true}) do
        local sfx = low and "_low" or ""
        local w = is(NS .. "in_wall", low and 1 or 0)
        vis["posts_z" .. sfx] = all(axis_z, w)
        vis["posts_x" .. sfx] = all(axis_x, w)
        vis["closed_z" .. sfx] = all(axis_z, w, is(NS .. "open", 0))
        vis["closed_x" .. sfx] = all(axis_x, w, is(NS .. "open", 0))
        for _, d in ipairs(DIRS) do
            vis["open_" .. d .. sfx] = all(is("minecraft:cardinal_direction", d), w, is(NS .. "open", 1))
        end
    end
    geometry(doc, g, vis)
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(tex(t, "*", "side"), e.params.render_method)}
    -- Java: closed 4 px across the gate (24 px high: the runtime), open no collision
    perm(doc, all(axis_z, is(NS .. "open", 0)), {["minecraft:collision_box"] = cbox({0, 0, 6, 16, 16, 10}), ["minecraft:selection_box"] = cbox({0, 0, 6, 16, 16, 10})})
    perm(doc, all(axis_x, is(NS .. "open", 0)), {["minecraft:collision_box"] = cbox({6, 0, 0, 10, 16, 16}), ["minecraft:selection_box"] = cbox({6, 0, 0, 10, 16, 16})})
    perm(doc, all(axis_z, is(NS .. "open", 1)), {["minecraft:collision_box"] = false, ["minecraft:selection_box"] = cbox({0, 0, 6, 16, 16, 10})})
    perm(doc, all(axis_x, is(NS .. "open", 1)), {["minecraft:collision_box"] = false, ["minecraft:selection_box"] = cbox({6, 0, 0, 10, 16, 16})})
end

-- door (Java DoorBlock): minecraft:cardinal_direction (Java facing), twilightforest:upper / open /
-- hinge (1 = right) / powered. The runtime places the upper half and keeps both halves together.
local DOOR_AABB = {SOUTH = {0, 0, 0, 16, 16, 3}, NORTH = {0, 0, 13, 16, 16, 16}, WEST = {13, 0, 0, 16, 16, 16}, EAST = {0, 0, 0, 3, 16, 16}}
local function door_box(facing, open, right)
    if facing == "east" then return open and (right and "NORTH" or "SOUTH") or "EAST" end
    if facing == "south" then return open and (right and "EAST" or "WEST") or "SOUTH" end
    if facing == "west" then return open and (right and "SOUTH" or "NORTH") or "WEST" end
    return open and (right and "WEST" or "EAST") or "NORTH"
end

builders.door = function (doc, e, t, ctx)
    local g = "geometry.gf_families.door"
    if not ctx.geos[g] then
        local bones = {}
        for name, b in pairs(DOOR_AABB) do
            local c = cube(b)
            table.insert(bones, bone(name:lower(), {c}))
            local m = cube(b)
            for _, f in pairs(m.uv) do f.uv = {f.uv[1] + f.uv_size[1], f.uv[2]}; f.uv_size = {-f.uv_size[1], f.uv_size[2]} end
            table.insert(bones, bone(name:lower() .. "_mirrored", {m}))
        end
        ctx.geos[g] = geo_doc(g, bones)
    end
    for _, s in ipairs({"upper", "open", "hinge", "powered"}) do add_state(doc, NS .. s, ints(1)) end
    add_trait(doc, "minecraft:placement_direction", {enabled_states = arr({"minecraft:cardinal_direction"})})
    local vis = {}
    for name, _ in pairs(DOOR_AABB) do vis[name:lower()] = false; vis[name:lower() .. "_mirrored"] = false end
    local by_box = {}
    for _, facing in ipairs(DIRS) do
        for _, open in ipairs({false, true}) do
            for _, right in ipairs({false, true}) do
                local key = door_box(facing, open, right):lower() .. ((right ~= open) and "_mirrored" or "")
                by_box[key] = by_box[key] or {}
                table.insert(by_box[key], all(is("minecraft:cardinal_direction", facing), is(NS .. "open", open), is(NS .. "hinge", right)))
                local b = DOOR_AABB[door_box(facing, open, right)]
                perm(doc, all(is("minecraft:cardinal_direction", facing), is(NS .. "open", open), is(NS .. "hinge", right)),
                    {["minecraft:collision_box"] = cbox(b), ["minecraft:selection_box"] = cbox(b)})
            end
        end
    end
    for key, conds in pairs(by_box) do vis[key] = any(conds) end
    geometry(doc, g, vis)
    local m = e.params.render_method or "alpha_test"
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(tex(t, "bottom", "lower", "*"), m)}
    perm(doc, is(NS .. "upper", 1), {["minecraft:material_instances"] = {["*"] = mat(tex(t, "top", "upper", "*"), m)}})
end

-- trapdoor (Java TrapDoorBlock): facing from the clicked face (horizontal) or the player
-- (vertical clicks), half from the click; twilightforest:open / powered. The runtime sets
-- minecraft:cardinal_direction to Java's facing on placement.
local TRAP = {EAST = {0, 0, 0, 3, 16, 16}, WEST = {13, 0, 0, 16, 16, 16}, SOUTH = {0, 0, 0, 16, 16, 3}, NORTH = {0, 0, 13, 16, 16, 16}, BOTTOM = {0, 0, 0, 16, 3, 16}, TOP = {0, 13, 0, 16, 16, 16}}
builders.trapdoor = function (doc, e, t, ctx)
    local g = "geometry.gf_families.trapdoor"
    if not ctx.geos[g] then
        local bones = {}
        for name, b in pairs(TRAP) do table.insert(bones, bone(name:lower(), {cube(b)})) end
        ctx.geos[g] = geo_doc(g, bones)
    end
    for _, s in ipairs({"open", "powered"}) do add_state(doc, NS .. s, ints(1)) end
    add_trait(doc, "minecraft:placement_direction", {enabled_states = arr({"minecraft:cardinal_direction"})})
    add_trait(doc, "minecraft:placement_position", {enabled_states = arr({"minecraft:vertical_half"})})
    local vis = {
        bottom = all(is(NS .. "open", 0), is("minecraft:vertical_half", "bottom")),
        top = all(is(NS .. "open", 0), is("minecraft:vertical_half", "top")),
    }
    for _, d in ipairs(DIRS) do vis[d] = all(is(NS .. "open", 1), is("minecraft:cardinal_direction", d)) end
    geometry(doc, g, vis)
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(tex(t, "*", "side"), e.params.render_method or "alpha_test")}
    perm(doc, all(is(NS .. "open", 0), is("minecraft:vertical_half", "bottom")), {["minecraft:collision_box"] = cbox(TRAP.BOTTOM), ["minecraft:selection_box"] = cbox(TRAP.BOTTOM)})
    perm(doc, all(is(NS .. "open", 0), is("minecraft:vertical_half", "top")), {["minecraft:collision_box"] = cbox(TRAP.TOP), ["minecraft:selection_box"] = cbox(TRAP.TOP)})
    for _, d in ipairs(DIRS) do
        local b = TRAP[d:upper()]
        perm(doc, all(is(NS .. "open", 1), is("minecraft:cardinal_direction", d)), {["minecraft:collision_box"] = cbox(b), ["minecraft:selection_box"] = cbox(b)})
    end
end

-- button (Java ButtonBlock): minecraft:block_face is the clicked face (up = floor, down = ceiling,
-- otherwise a wall button facing that way), minecraft:cardinal_direction the player's direction
-- (floor / ceiling axis); twilightforest:powered. Pressed buttons power vanilla redstone through
-- minecraft:redstone_producer (15, strongly the block they hang on).
local function button_box(face, axis_or_dir, pressed)
    local d = pressed and 1 or 2
    if face == "up" then -- floor
        return axis_or_dir == "x" and {6, 0, 5, 10, d, 11} or {5, 0, 6, 11, d, 10}
    elseif face == "down" then -- ceiling
        return axis_or_dir == "x" and {6, 16 - d, 5, 10, 16, 11} or {5, 16 - d, 6, 11, 16, 10}
    end
    return ({north = {5, 6, 16 - d, 11, 10, 16}, south = {5, 6, 0, 11, 10, d}, west = {16 - d, 6, 5, 16, 10, 11}, east = {0, 6, 5, d, 10, 11}})[face]
end
builders.button = function (doc, e, t, ctx)
    local g = "geometry.gf_families.button"
    local variants = {}
    for _, pressed in ipairs({false, true}) do
        for _, face in ipairs({"up", "down"}) do
            for _, axis in ipairs({"x", "z"}) do table.insert(variants, {face = face, sub = axis, pressed = pressed}) end
        end
        for _, d in ipairs(DIRS) do table.insert(variants, {face = d, sub = d, pressed = pressed}) end
    end
    local function vname(v) return v.face .. "_" .. v.sub .. (v.pressed and "_pressed" or "") end
    if not ctx.geos[g] then
        local bones = {}
        for _, v in ipairs(variants) do table.insert(bones, bone(vname(v), {cube(button_box(v.face, v.sub, v.pressed))})) end
        ctx.geos[g] = geo_doc(g, bones)
    end
    add_state(doc, NS .. "powered", ints(1))
    add_trait(doc, "minecraft:placement_position", {enabled_states = arr({"minecraft:block_face"})})
    add_trait(doc, "minecraft:placement_direction", {enabled_states = arr({"minecraft:cardinal_direction"})})
    local axis = {
        x = any({is("minecraft:cardinal_direction", "east"), is("minecraft:cardinal_direction", "west")}),
        z = any({is("minecraft:cardinal_direction", "north"), is("minecraft:cardinal_direction", "south")}),
    }
    local vis = {}
    for _, v in ipairs(variants) do
        local c = {is("minecraft:block_face", v.face), is(NS .. "powered", v.pressed)}
        if v.face == "up" or v.face == "down" then table.insert(c, axis[v.sub]) end
        vis[vname(v)] = all(table.unpack(c))
        local b = button_box(v.face, v.sub, v.pressed)
        local components = {["minecraft:selection_box"] = cbox(b), ["minecraft:collision_box"] = false}
        if v.pressed then
            local attached = v.face == "up" and "down" or (v.face == "down" and "up" or OPP[v.face])
            components["minecraft:redstone_producer"] = {power = 15, strongly_powered_face = attached}
        end
        perm(doc, all(table.unpack(c)), components)
    end
    geometry(doc, g, vis)
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(tex(t, "*", "side"), e.params.render_method)}
    comps(doc)["minecraft:collision_box"] = false
    comps(doc)["minecraft:redstone_conductivity"] = {redstone_conductor = false, allows_wire_to_step_down = true}
    -- every permutation is a producer (0 unpressed) so the circuit follows the press and release
    comps(doc)["minecraft:redstone_producer"] = {power = 0}
end

-- pressure plate (Java PressurePlateBlock): twilightforest:powered; 15 while pressed.
builders.pressure_plate = function (doc, e, t, ctx)
    local g = "geometry.gf_families.pressure_plate"
    ctx.geos[g] = ctx.geos[g] or geo_doc(g, {bone("up", {cube({1, 0, 1, 15, 1, 15})}), bone("down", {cube({1, 0, 1, 15, 0.5, 15})})})
    add_state(doc, NS .. "powered", ints(1))
    geometry(doc, g, {up = is(NS .. "powered", 0), down = is(NS .. "powered", 1)})
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(tex(t, "*", "side"), e.params.render_method)}
    comps(doc)["minecraft:collision_box"] = false
    comps(doc)["minecraft:selection_box"] = cbox({1, 0, 1, 15, 1, 15})
    comps(doc)["minecraft:redstone_conductivity"] = {redstone_conductor = false, allows_wire_to_step_down = true}
    -- every permutation is a producer (0 unpressed) so the circuit follows the press and release
    comps(doc)["minecraft:redstone_producer"] = {power = 0}
    perm(doc, is(NS .. "powered", 1), {["minecraft:selection_box"] = cbox({1, 0, 1, 15, 1, 15}),
        ["minecraft:redstone_producer"] = {power = 15, strongly_powered_face = "down"}})
end

-- pillar / log / wood (Java RotatedPillarBlock): the axis from the clicked face.
local AXIS_TURN = {
    {cond = any({is("minecraft:block_face", "north"), is("minecraft:block_face", "south")}), rot = {90, 0, 0}},
    {cond = any({is("minecraft:block_face", "east"), is("minecraft:block_face", "west")}), rot = {0, 0, 90}},
}
local function pillar(doc, e, t, ends_too)
    full_block(doc, {["*"] = tex(t, "side", "*"), up = ends_too and tex(t, "side", "*") or tex(t, "end", "up", "top", "*"),
        down = ends_too and tex(t, "side", "*") or tex(t, "end", "down", "bottom", "up", "*")}, e.params.render_method)
    add_trait(doc, "minecraft:placement_position", {enabled_states = arr({"minecraft:block_face"})})
    for _, a in ipairs(AXIS_TURN) do perm(doc, a.cond, {["minecraft:transformation"] = {rotation = a.rot}}) end
end
builders.pillar = function (doc, e, t) pillar(doc, e, t, false) end
builders.log = builders.pillar
builders.wood = function (doc, e, t) pillar(doc, e, t, true) end

-- leaves (Java LeavesBlock): twilightforest:distance 1..7 (0: not computed yet) and persistent.
builders.leaves = function (doc, e, t)
    local extra = e.params.tint and {tint_method = e.params.tint} or nil
    full_block(doc, t, e.params.render_method or "alpha_test", extra)
    add_state(doc, NS .. "distance", ints(7))
    add_state(doc, NS .. "persistent", ints(1))
    comps(doc)["minecraft:light_dampening"] = 1
end

-- cross plants (sapling, potted plant)
local function cross_cubes(b, mat_name)
    local cx = (b[1] + b[4]) / 2
    local cz = (b[3] + b[6]) / 2
    local w = (b[4] - b[1])
    local h = b[5] - b[2]
    local c1 = {origin = {cx - w / 2 - 8, b[2], cz - 8}, size = {w, h, 0}, pivot = {cx - 8, b[2], cz - 8}, rotation = {0, 45, 0},
        uv = {north = {uv = {0, 16 - h}, uv_size = {16, h}}, south = {uv = {0, 16 - h}, uv_size = {16, h}}}}
    local c2 = {origin = {cx - w / 2 - 8, b[2], cz - 8}, size = {w, h, 0}, pivot = {cx - 8, b[2], cz - 8}, rotation = {0, -45, 0},
        uv = {north = {uv = {0, 16 - h}, uv_size = {16, h}}, south = {uv = {0, 16 - h}, uv_size = {16, h}}}}
    if mat_name then
        for _, c in ipairs({c1, c2}) do for _, f in pairs(c.uv) do f.material_instance = mat_name end end
    end
    return {c1, c2}
end

-- a cross plant (TF bushes, plants): no collision.
builders.cross = function (doc, e, t, ctx)
    local g = "geometry.gf_families.cross"
    ctx.geos[g] = ctx.geos[g] or geo_doc(g, {bone("cross", cross_cubes({0, 0, 0, 16, 16, 16}))})
    geometry(doc, g)
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(tex(t, "*"), "alpha_test", {ambient_occlusion = false, face_dimming = false})}
    comps(doc)["minecraft:collision_box"] = false
    comps(doc)["minecraft:selection_box"] = cbox(e.params.shape or {2, 0, 2, 14, 13, 14})
    comps(doc)["minecraft:light_dampening"] = 0
end

-- sapling (Java SaplingBlock): twilightforest:stage; the runtime grows params.feature.
builders.sapling = function (doc, e, t, ctx)
    builders.cross(doc, e, t, ctx)
    comps(doc)["minecraft:selection_box"] = cbox({2, 0, 2, 14, 12, 14})
    add_state(doc, NS .. "stage", ints(1))
end

-- potted plant (Java FlowerPotBlock / TF SpecialFlowerPotBlock): pot, soil and the plant.
builders.potted = function (doc, e, t, ctx)
    local g = "geometry.gf_families.potted"
    if not ctx.geos[g] then
        local pot = {
            cube({5, 0, 5, 6, 6, 11}, "pot"), cube({10, 0, 5, 11, 6, 11}, "pot"),
            cube({6, 0, 5, 10, 6, 6}, "pot"), cube({6, 0, 10, 10, 6, 11}, "pot"),
            cube({6, 0, 6, 10, 4, 10}, "soil", {up = true}),
        }
        local plant = cross_cubes({4, 4, 4, 12, 20, 12}, "plant")
        ctx.geos[g] = geo_doc(g, {bone("pot", pot), bone("plant", plant)})
    end
    geometry(doc, g)
    comps(doc)["minecraft:material_instances"] = {
        ["*"] = mat(tex(t, "pot"), "alpha_test"), pot = mat(tex(t, "pot"), "alpha_test"),
        -- (one render method per block: Bedrock refuses opaque and transparent instances together)
        soil = mat(tex(t, "soil"), "alpha_test"), plant = mat(tex(t, "plant", "*"), "alpha_test", {ambient_occlusion = false, face_dimming = false}),
    }
    comps(doc)["minecraft:collision_box"] = cbox({5, 0, 5, 11, 6, 11})
    comps(doc)["minecraft:selection_box"] = cbox({5, 0, 5, 11, 6, 11})
    comps(doc)["minecraft:light_dampening"] = 0
end

-- signs (Java StandingSignBlock / WallSignBlock / CeilingHangingSignBlock / WallHangingSignBlock):
-- entity-texture models (64x32, Java's SignRenderer / HangingSignRenderer layout). Standing and
-- hanging signs turn in 16 steps (twilightforest:rotation, set by the runtime), wall signs face
-- minecraft:cardinal_direction. The text lives in block data (gf::signs).
-- Java's 16 sign rotations (22.5 degrees each): a quarter-turn transformation (Bedrock turns blocks
-- by multiples of 90 only) plus a model variant turned by 0, -22.5, -45 or 22.5 degrees.
local SIGN_STEPS = {{name = "r0", angle = 0}, {name = "r1", angle = -22.5}, {name = "r2", angle = -45}, {name = "r3", angle = 22.5}}
local function rotation_split(r) -- quarter turns and the step variant (1..4) of rotation r
    local quarter, rest = r // 4, r % 4
    if rest == 3 then return (quarter + 1) % 4, 4 end
    return quarter, rest + 1
end
local function rotated(cubes, angle)
    local out = {}
    for _, c in ipairs(cubes) do
        local d = copy(c)
        if angle ~= 0 then d.pivot = {0, 0, 0}; d.rotation = {0, angle, 0} end
        table.insert(out, d)
    end
    return out
end
local function rotation_bones(parts)
    local bones = {}
    for _, step in ipairs(SIGN_STEPS) do
        for _, part in ipairs(parts) do table.insert(bones, bone(part.name .. "_" .. step.name, rotated(part.cubes, step.angle))) end
    end
    return bones
end
local function rotation_perms(doc, g, parts)
    for r = 0, 15 do
        local quarter, variant = rotation_split(r)
        local v = {}
        for i, step in ipairs(SIGN_STEPS) do
            for _, part in ipairs(parts) do v[part.name .. "_" .. step.name] = i == variant and (part.cond or true) or false end
        end
        perm(doc, is(NS .. "rotation", r), {["minecraft:geometry"] = {identifier = g, bone_visibility = v},
            ["minecraft:transformation"] = {rotation = {0, -90 * quarter, 0}}})
    end
end
local function facing_perms(doc)
    local turns = {north = 0, east = -90, south = 180, west = 90}
    for _, d in ipairs(DIRS) do
        perm(doc, is("minecraft:cardinal_direction", d), {["minecraft:transformation"] = {rotation = {0, turns[d], 0}}})
    end
end
builders.sign = function (doc, e, t, ctx)
    local g = "geometry.gf_families.sign"
    -- Java SignRenderer: the board 24x12x2 over a 2x14x2 stick, scaled 2/3
    local s = 2 / 3
    local function sc(b) return {8 + (b[1] - 8) * s, b[2] * s, 8 + (b[3] - 8) * s, 8 + (b[4] - 8) * s, b[5] * s, 8 + (b[6] - 8) * s} end
    local parts = {
        {name = "board", cubes = {box_cube(sc({-4, 14, 7, 20, 26, 9}), 0, 0)}},
        {name = "stick", cubes = {box_cube(sc({7, 0, 7, 9, 14, 9}), 0, 14)}},
    }
    ctx.geos[g] = ctx.geos[g] or geo_doc(g, rotation_bones(parts), 64, 32)
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(tex(t, "*"), "alpha_test")}
    comps(doc)["minecraft:collision_box"] = false
    comps(doc)["minecraft:selection_box"] = cbox({4, 0, 4, 12, 16, 12})
    add_state(doc, NS .. "rotation", ints(15))
    geometry(doc, g, {board_r0 = true, stick_r0 = true})
    rotation_perms(doc, g, parts)
end
builders.wall_sign = function (doc, e, t, ctx)
    local g = "geometry.gf_families.wall_sign"
    if not ctx.geos[g] then
        local s = 2 / 3
        -- the board against the south side of the block, facing north (Java: y offset -0.3125)
        local b = {8 - 12 * s, 8 - 6 * s + 1.0, 16 - 2 * s, 8 + 12 * s, 8 + 6 * s + 1.0, 16}
        ctx.geos[g] = geo_doc(g, {bone("board", {box_cube(b, 0, 0)})}, 64, 32)
    end
    geometry(doc, g)
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(tex(t, "*"), "alpha_test")}
    comps(doc)["minecraft:collision_box"] = false
    comps(doc)["minecraft:selection_box"] = cbox({0, 4.5, 14, 16, 12.5, 16})
    add_trait(doc, "minecraft:placement_direction", {enabled_states = arr({"minecraft:cardinal_direction"})})
    facing_perms(doc)
end
builders.hanging_sign = function (doc, e, t, ctx)
    local g = "geometry.gf_families.hanging_sign"
    -- Java HangingSignRenderer: board 14x10x2 (uv 0,12), chains (uv 0,6 / 6,6), V chains (14,6)
    local parts = {
        {name = "board", cubes = {box_cube({1, 0, 7, 15, 10, 9}, 0, 12)}},
        {name = "chains", cubes = {box_cube({2, 10, 8, 5, 16, 8}, 0, 6), box_cube({11, 10, 8, 14, 16, 8}, 6, 6)}, cond = is(NS .. "attached", 0)},
        {name = "vchains", cubes = {box_cube({4.5, 10, 8, 11.5, 16, 8}, 14, 6)}, cond = is(NS .. "attached", 1)},
    }
    ctx.geos[g] = ctx.geos[g] or geo_doc(g, rotation_bones(parts), 64, 32)
    add_state(doc, NS .. "rotation", ints(15))
    add_state(doc, NS .. "attached", ints(1))
    geometry(doc, g, {board_r0 = true, chains_r0 = is(NS .. "attached", 0), vchains_r0 = is(NS .. "attached", 1)})
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(tex(t, "*"), "alpha_test")}
    comps(doc)["minecraft:collision_box"] = false
    comps(doc)["minecraft:selection_box"] = cbox({3, 0, 3, 13, 16, 13})
    rotation_perms(doc, g, parts)
end
builders.wall_hanging_sign = function (doc, e, t, ctx)
    local g = "geometry.gf_families.wall_hanging_sign"
    if not ctx.geos[g] then
        ctx.geos[g] = geo_doc(g, {
            bone("plank", {box_cube({0, 14, 6, 16, 16, 10}, 0, 0)}),
            bone("board", {box_cube({1, 0, 7, 15, 10, 9}, 0, 12)}),
            bone("chains", {box_cube({2, 10, 8, 5, 14, 8}, 0, 6), box_cube({11, 10, 8, 14, 14, 8}, 6, 6)}),
        }, 64, 32)
    end
    geometry(doc, g)
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(tex(t, "*"), "alpha_test")}
    comps(doc)["minecraft:collision_box"] = cbox({0, 14, 6, 16, 16, 10})
    comps(doc)["minecraft:selection_box"] = cbox({0, 0, 6, 16, 16, 10})
    add_trait(doc, "minecraft:placement_direction", {enabled_states = arr({"minecraft:cardinal_direction"})})
    facing_perms(doc)
end

-- chest / trapped chest (Java ChestBlock): minecraft:cardinal_direction (Java facing) and
-- twilightforest:type single / left / right; the runtime pairs neighbours into double chests.
-- Models on Java's chest texture layout (64x64: single.png, left.png, right.png).
local function chest_bones(kind)
    -- facing north, the latch on the north side (Java ChestRenderer, block pixels)
    local x0, x1 = 1, 15
    if kind == "left" then x0, x1 = 1, 16 elseif kind == "right" then x0, x1 = 0, 15 end
    local w = x1 - x0
    local bones = {
        bone(kind .. "_bottom", {box_cube({x0, 0, 1, x1, 10, 15}, 0, 19)}),
        bone(kind .. "_lid", {box_cube({x0, 9, 1, x1, 14, 15}, 0, 0)}),
    }
    if kind == "single" then
        table.insert(bones, bone(kind .. "_lock", {box_cube({7, 7, 0, 9, 11, 1}, 0, 0)}))
    elseif kind == "left" then
        table.insert(bones, bone(kind .. "_lock", {box_cube({15, 7, 0, 16, 11, 1}, 0, 0)}))
    else
        table.insert(bones, bone(kind .. "_lock", {box_cube({0, 7, 0, 1, 11, 1}, 0, 0)}))
    end
    return bones
end
builders.chest = function (doc, e, t, ctx)
    local g = "geometry.gf_families.chest"
    if not ctx.geos[g] then
        local bones = {}
        for _, k in ipairs({"single", "left", "right"}) do for _, b in ipairs(chest_bones(k)) do table.insert(bones, b) end end
        ctx.geos[g] = geo_doc(g, bones, 64, 64)
    end
    add_state(doc, NS .. "type", arr({"single", "left", "right"}))
    add_trait(doc, "minecraft:placement_direction", {enabled_states = arr({"minecraft:cardinal_direction"})})
    local vis = {}
    for _, k in ipairs({"single", "left", "right"}) do
        for _, part in ipairs({"bottom", "lid", "lock"}) do vis[k .. "_" .. part] = is(NS .. "type", k) end
    end
    geometry(doc, g, vis)
    local m = e.params.render_method or "alpha_test"
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(tex(t, "single", "*"), m)}
    perm(doc, is(NS .. "type", "left"), {["minecraft:material_instances"] = {["*"] = mat(tex(t, "left", "single", "*"), m)}})
    perm(doc, is(NS .. "type", "right"), {["minecraft:material_instances"] = {["*"] = mat(tex(t, "right", "single", "*"), m)}})
    local turns = {north = 0, east = -90, south = 180, west = 90}
    for _, d in ipairs(DIRS) do
        perm(doc, is("minecraft:cardinal_direction", d), {["minecraft:transformation"] = {rotation = {0, turns[d], 0}}})
    end
    comps(doc)["minecraft:collision_box"] = cbox({1, 0, 1, 15, 14, 15})
    comps(doc)["minecraft:selection_box"] = cbox({1, 0, 1, 15, 14, 15})
end
builders.trapped_chest = builders.chest

-- horizontal (TF TFHorizontalBlock): the front faces the player (minecraft:cardinal_direction is
-- Java's facing, set by the runtime); binary rotated (TF BinaryRotatedBlock): turned when the
-- player looked along z; directional (Java DirectionalBlock): minecraft:facing_direction.
builders.horizontal = function (doc, e, t)
    full_block(doc, {["*"] = tex(t, "side", "*"), up = tex(t, "up", "top", "*"), down = tex(t, "down", "bottom", "up", "*"),
        north = tex(t, "front", "north", "*"), south = tex(t, "back", "south", "side", "*"), east = tex(t, "side", "*"), west = tex(t, "side", "*")}, e.params.render_method)
    add_trait(doc, "minecraft:placement_direction", {enabled_states = arr({"minecraft:cardinal_direction"})})
    local turns = {north = 0, east = -90, south = 180, west = 90}
    for _, d in ipairs(DIRS) do
        if turns[d] ~= 0 then perm(doc, is("minecraft:cardinal_direction", d), {["minecraft:transformation"] = {rotation = {0, turns[d], 0}}}) end
    end
end
builders.binary_rotated = function (doc, e, t)
    full_block(doc, t, e.params.render_method)
    add_trait(doc, "minecraft:placement_direction", {enabled_states = arr({"minecraft:cardinal_direction"})})
    perm(doc, any({is("minecraft:cardinal_direction", "north"), is("minecraft:cardinal_direction", "south")}),
        {["minecraft:transformation"] = {rotation = {0, 90, 0}}})
end
builders.directional = function (doc, e, t)
    full_block(doc, {["*"] = tex(t, "side", "*"), up = tex(t, "front", "up", "*"), down = tex(t, "back", "down", "side", "*"),
        north = tex(t, "side", "*"), south = tex(t, "side", "*"), east = tex(t, "side", "*"), west = tex(t, "side", "*")}, e.params.render_method)
    add_trait(doc, "minecraft:placement_direction", {enabled_states = arr({"minecraft:facing_direction"})})
    local rots = {down = {180, 0, 0}, north = {-90, 0, 0}, south = {90, 0, 0}, west = {0, 0, 90}, east = {0, 0, -90}}
    for f, r in pairs(rots) do perm(doc, is("minecraft:facing_direction", f), {["minecraft:transformation"] = {rotation = r}}) end
end

-- connectable pillar (TF ConnectableRotatedPillarBlock, e.g. thorns) and wall pillar (TF
-- WallPillarBlock): an axis and six connections (twilightforest:<direction>, kept by the runtime).
local function pillar_conn(doc, e, t, ctx, kind)
    local width = e.params.width or (kind == "wall_pillar" and 12 or 16)
    local lo = width >= 16 and 0 or 8 - width / 2
    local hi = 16 - lo
    local g = "geometry.gf_families." .. kind .. "_" .. tostring(width):gsub("%.", "_")
    if not ctx.geos[g] then
        local bones
        if kind == "wall_pillar" then
            -- TF WallPillarBlock shapes: the 12 px core, 2 px arms, 3 px flats on the axis ends
            bones = {
                bone("core", {cube({2, 2, 2, 14, 14, 14})}),
                bone("down", {cube({2, 0, 2, 14, 2, 14})}), bone("up", {cube({2, 14, 2, 14, 16, 14})}),
                bone("north", {cube({2, 2, 0, 14, 14, 2})}), bone("south", {cube({2, 2, 14, 14, 14, 16})}),
                bone("west", {cube({0, 2, 2, 2, 14, 14})}), bone("east", {cube({14, 2, 2, 16, 14, 14})}),
                bone("down_flat", {cube({0, 0, 0, 16, 3, 16})}), bone("up_flat", {cube({0, 13, 0, 16, 16, 16})}),
                bone("north_flat", {cube({0, 0, 0, 16, 16, 3})}), bone("south_flat", {cube({0, 0, 13, 16, 16, 16})}),
                bone("west_flat", {cube({0, 0, 0, 3, 16, 16})}), bone("east_flat", {cube({13, 0, 0, 16, 16, 16})}),
            }
        else
            bones = {
                bone("core", {cube({lo, lo, lo, hi, hi, hi})}),
                bone("down", {cube({lo, 0, lo, hi, lo, hi})}), bone("up", {cube({lo, hi, lo, hi, 16, hi})}),
                bone("north", {cube({lo, lo, 0, hi, hi, lo})}), bone("south", {cube({lo, lo, hi, hi, hi, 16})}),
                bone("west", {cube({0, lo, lo, lo, hi, hi})}), bone("east", {cube({hi, lo, lo, 16, hi, hi})}),
                bone("axis_y", {cube({lo, 0, lo, hi, 16, hi})}), bone("axis_z", {cube({lo, lo, 0, hi, hi, 16})}), bone("axis_x", {cube({0, lo, lo, 16, hi, hi})}),
            }
        end
        ctx.geos[g] = geo_doc(g, bones)
    end
    add_trait(doc, "minecraft:placement_position", {enabled_states = arr({"minecraft:block_face"})})
    local axis = {
        y = any({is("minecraft:block_face", "up"), is("minecraft:block_face", "down")}),
        z = any({is("minecraft:block_face", "north"), is("minecraft:block_face", "south")}),
        x = any({is("minecraft:block_face", "east"), is("minecraft:block_face", "west")}),
    }
    local axis_of = {up = "y", down = "y", north = "z", south = "z", west = "x", east = "x"}
    local vis = {core = true}
    for _, f in ipairs(FACES6) do
        add_state(doc, NS .. f, ints(1))
        vis[f] = is(NS .. f, 1)
        if kind == "wall_pillar" then vis[f .. "_flat"] = all(is(NS .. f, 0), axis[axis_of[f]]) end
    end
    if kind ~= "wall_pillar" then for a, c in pairs(axis) do vis["axis_" .. a] = c end end
    geometry(doc, g, vis)
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(tex(t, "side", "*"), e.params.render_method), up = mat(tex(t, "end", "up", "*"), e.params.render_method), down = mat(tex(t, "end", "down", "*"), e.params.render_method)}
    if kind == "wall_pillar" then comps(doc)["minecraft:light_dampening"] = 0 end
end
builders.connectable_pillar = function (doc, e, t, ctx) pillar_conn(doc, e, t, ctx, "connectable_pillar") end
builders.wall_pillar = function (doc, e, t, ctx) pillar_conn(doc, e, t, ctx, "wall_pillar") end

-- connected texture (TF ConnectedTextureModel / ConnectionLogic): every face is four 8x8
-- quadrants; each quadrant draws the overlay (textures.overlay, TF's overlay_texture) or a piece
-- of the connected sheet (textures.connected, overlay_connected: cornerless / vertical /
-- horizontal / corner quarters) by the connections to its two sides (twilightforest:<direction>,
-- kept by the runtime). Bedrock's state budget leaves out TF's diagonal check: two connected
-- sides count as a filled corner (cornerless), as in a flat wall.
local CT_PLANE = { -- ConnectionLogic.AXIS_PLANE_DIRECTIONS by the face's axis
    x = {"up", "north", "down", "south"}, y = {"north", "east", "south", "west"}, z = {"up", "east", "down", "west"},
}
local FACE_AXIS = {down = "y", up = "y", north = "z", south = "z", west = "x", east = "x"}
local NORMAL = {down = {0, -1, 0}, up = {0, 1, 0}, north = {0, 0, -1}, south = {0, 0, 1}, west = {-1, 0, 0}, east = {1, 0, 0}}
local CT_LOGIC = { -- name, sheet (overlay / connected), u0, v0, u1, v1
    none = {"overlay", 0, 0, 16, 16}, cornerless = {"connected", 0, 0, 8, 8}, vertical = {"connected", 0, 8, 8, 16},
    horizontal = {"connected", 8, 0, 16, 8}, corner = {"connected", 8, 8, 16, 16},
}
local function ct_of(horizontal, vertical, corner) -- ConnectionLogic.of
    if corner then return "cornerless" end
    if horizontal then return vertical and "corner" or "horizontal" end
    return vertical and "vertical" or "none"
end

-- The overlay quad of quadrant i (1..4) of face f drawn with `logic`.
local function ct_quad(f, i, logic)
    local dirs = CT_PLANE[FACE_AXIS[f]]
    local a, b, n = NORMAL[dirs[i]], NORMAL[dirs[i % 4 + 1]], NORMAL[f]
    local corner, box = {}, {}
    for k = 1, 3 do corner[k] = (n[k] + a[k] + b[k] + 1) * 8 end
    for k = 1, 3 do box[k] = math.min(8, corner[k]); box[k + 3] = math.max(8, corner[k]) end
    -- the quadrant on the face plane, lifted 0.01 px off the base
    local k     = n[1] ~= 0 and 1 or (n[2] ~= 0 and 2 or 3)
    local plane = (n[k] > 0 and 16 or 0) + n[k] * 0.01
    box[k], box[k + 3] = plane, plane
    local l  = CT_LOGIC[logic]
    local c  = cube(box, l[1], {[f] = true})
    local uv = c.uv[f]
    -- TF remapUVs: the quadrant's own UV scaled into the logic's piece of the sheet
    local function ru(u) return l[2] + (l[4] - l[2]) * u / 16 end
    local function rv(v) return l[3] + (l[5] - l[3]) * v / 16 end
    local u0, v0 = ru(uv.uv[1]), rv(uv.uv[2])
    local u1, v1 = ru(uv.uv[1] + uv.uv_size[1]), rv(uv.uv[2] + uv.uv_size[2])
    uv.uv, uv.uv_size = {u0, v0}, {u1 - u0, v1 - v0}
    return c
end

-- One geometry per connection mask (bit d of FACES6 order: the connection on that side), each
-- holding the base cube and the 24 quadrant overlays that mask shows: no bone visibility (Bedrock
-- caps its entries) and one permutation per mask.
local function ct_geos(ctx, faces, disabled_overlay)
    local key = {}
    for _, f in ipairs(FACES6) do table.insert(key, faces[f] and "1" or "0") end
    local suffix = table.concat(key) .. (disabled_overlay and "o" or "")
    local ids = {}
    for mask = 0, 63 do
        local g = "geometry.gf_families.ct_" .. suffix .. "_" .. mask
        ids[mask] = g
        if not ctx.geos[g] then
            local conn = {}
            for d, f in ipairs(FACES6) do conn[f] = (mask >> (d - 1)) & 1 == 1 end
            local cubes = {}
            for _, f in ipairs(FACES6) do
                local dirs = CT_PLANE[FACE_AXIS[f]]
                for i = 1, 4 do
                    local logic
                    if faces[f] then
                        local side1, side2 = conn[dirs[i]], conn[dirs[i % 4 + 1]]
                        local corner = side1 and side2 -- see above: a filled corner
                        logic = (i - 1) % 2 == 0 and ct_of(side1, side2, corner) or ct_of(side2, side1, corner)
                    elseif disabled_overlay then
                        logic = "none"
                    end
                    if logic then table.insert(cubes, ct_quad(f, i, logic)) end
                end
            end
            ctx.geos[g] = geo_doc(g, {bone("base", {cube({0, 0, 0, 16, 16, 16})}), bone("overlay", cubes)})
        end
    end
    return ids
end

builders.connected_texture = function (doc, e, t, ctx)
    for _, f in ipairs(FACES6) do add_state(doc, NS .. f, ints(1)) end
    local method = e.params.render_method or "alpha_test"
    if not (t.overlay and t.connected) then
        full_block(doc, t, e.params.render_method) -- no overlay sheets: the plain block
        return
    end
    local faces = {}
    for _, f in ipairs(e.params.faces or FACES6) do faces[f] = true end
    local ids = ct_geos(ctx, faces, e.params.render_on_disabled_faces ~= false)
    geometry(doc, ids[0])
    comps(doc)["minecraft:material_instances"] = {
        ["*"] = mat(tex(t, "base", "*"), method), overlay = mat(t.overlay, method), connected = mat(t.connected, method),
    }
    for mask = 1, 63 do
        local cond = {}
        for d, f in ipairs(FACES6) do table.insert(cond, is(NS .. f, (mask >> (d - 1)) & 1)) end
        perm(doc, all(table.unpack(cond)), {["minecraft:geometry"] = ids[mask]})
    end
end

-- ---- modifiers -----------------------------------------------------------------------------------
--
-- Families that add to any shape: waterloggable, snowloggable, climbable, lightable.

local modifiers = {}

-- Java SimpleWaterloggedBlock: Bedrock's liquid detection keeps the water layer.
modifiers.waterloggable = function (doc)
    comps(doc)["minecraft:liquid_detection"] = {detection_rules = arr({{liquid_type = "water", can_contain_liquid = true, on_liquid_touches = "no_reaction"}})}
end

-- TF SnowLoggable: twilightforest:layers 0..8 of snow in the block (a snow layer bone).
modifiers.snowloggable = function (doc, e, t, ctx)
    add_state(doc, NS .. "layers", ints(8))
    local g = comps(doc)["minecraft:geometry"]
    local id = type(g) == "table" and g.identifier or g
    if type(id) == "string" and id:match("^geometry%.gf_families") then
        -- a snow bone per layer in a copy of the geometry
        local sid = id .. "_snowy"
        if not ctx.geos[sid] and ctx.geos[id] then
            local d = copy(ctx.geos[id])
            local geo = d["minecraft:geometry"][1]
            geo.description.identifier = sid
            for n = 1, 8 do table.insert(geo.bones, bone("snow_" .. n, {cube({0, 0, 0, 16, n * 2, 16}, "snow")})) end
            ctx.geos[sid] = d
        end
        local vis = type(g) == "table" and copy(g.bone_visibility) or {}
        for n = 1, 8 do vis["snow_" .. n] = is(NS .. "layers", n) end
        comps(doc)["minecraft:geometry"] = {identifier = sid, bone_visibility = vis}
        -- the snow takes the block's render method (Bedrock refuses opaque and transparent together)
        local base = comps(doc)["minecraft:material_instances"]["*"]
        comps(doc)["minecraft:material_instances"].snow = mat(e.params.snow_texture or "snow", base and base.render_method or "opaque")
        for _, p in ipairs(block_of(doc).permutations) do
            local pg = p.components["minecraft:geometry"]
            if type(pg) == "table" and pg.identifier == id then
                pg.identifier = sid
                for n = 1, 8 do pg.bone_visibility["snow_" .. n] = is(NS .. "layers", n) end
            end
        end
    end
end

-- Java ladders / scaffolding (TF climbable hollow logs, rope, iron ladder): the runtime marks the
-- block type climbable (BlockProperty::Climbable) on the server and in the client mod.
modifiers.climbable = function () end

-- TF LightableBlock: twilightforest:lighting none / normal / dim / ominous; light per level.
modifiers.lightable = function (doc, e)
    add_state(doc, NS .. "lighting", arr({"none", "normal", "dim", "ominous"}))
    local light = e.params.light_levels or {none = 0, normal = 15, dim = 7, ominous = 15}
    for _, l in ipairs({"normal", "dim", "ominous"}) do
        if light[l] and light[l] > 0 then perm(doc, is(NS .. "lighting", l), {["minecraft:light_emission"] = light[l]}) end
    end
end

local SHAPES = {
    cube = true, stairs = true, slab = true, fence = true, fence_gate = true, wall = true, door = true, trapdoor = true,
    button = true, pressure_plate = true, pane = true, pillar = true, log = true, wood = true, leaves = true,
    cross = true, sapling = true, potted = true, sign = true, wall_sign = true, hanging_sign = true, wall_hanging_sign = true,
    chest = true, trapped_chest = true, horizontal = true, directional = true, binary_rotated = true,
    wall_pillar = true, connectable_pillar = true, connected_texture = true,
}
local MODIFIERS = {waterloggable = true, snowloggable = true, climbable = true, lightable = true}

-- Default Bedrock block sounds per family (RP blocks.json) when params.sound is absent.
local DEFAULT_SOUND = {
    leaves = "grass", sapling = "grass", potted = "stone", pane = "glass", sign = "wood", wall_sign = "wood",
    hanging_sign = "hanging_sign", wall_hanging_sign = "hanging_sign", chest = "wood", trapped_chest = "wood",
}

-- ---- data ----------------------------------------------------------------------------------------

-- Normalises one data file's entries: [{id, family, params}] (or {"entries": [...]}).
local function read_entries(file, out, errors)
    local doc = try_json(file)
    if doc == nil then
        table.insert(errors, "unreadable family data " .. file)
        return
    end
    local list = doc
    if type(doc) == "table" and doc.entries then list = doc.entries end
    if type(list) ~= "table" then return end
    for i, e in ipairs(list) do
        if type(e) ~= "table" or type(e.id) ~= "string" or type(e.family) ~= "string" then
            table.insert(errors, string.format("%s: entry %d needs an id and a family", file, i))
        elseif not SHAPES[e.family] and not MODIFIERS[e.family] then
            table.insert(errors, string.format("%s: %s has the unknown family '%s'", file, e.id, e.family))
        else
            table.insert(out, {id = e.id, family = e.family, params = type(e.params) == "table" and e.params or {}, file = file})
        end
    end
end

-- Groups entries per id: one shape family (or "cube" for modifier-only ids) and its modifiers.
local function group(entries, errors)
    local by_id, order = {}, {}
    for _, e in ipairs(entries) do
        local g = by_id[e.id]
        if not g then
            g = {id = e.id, shape = nil, modifiers = {}, params = {}, files = {}}
            by_id[e.id] = g
            table.insert(order, e.id)
        end
        if SHAPES[e.family] then
            if g.shape and g.shape ~= e.family then
                table.insert(errors, string.format("%s has two shape families (%s, %s)", e.id, g.shape, e.family))
            end
            g.shape = e.family
        else
            g.modifiers[e.family] = true
        end
        for k, v in pairs(e.params) do g.params[k] = v end
        table.insert(g.files, e.file)
    end
    for _, id in ipairs(order) do
        local g = by_id[id]
        g.shape = g.shape or "cube"
        if g.params.waterlogged or g.params.waterloggable then g.modifiers.waterloggable = true end
    end
    table.sort(order)
    return by_id, order
end

-- Every JSON file under `dir` (recursively), sorted.
local function json_files(dir)
    if not os.isdir(dir) then return {} end
    local files = os.files(path.join(dir, "**.json"))
    table.sort(files)
    return files
end

-- BP definitions of a pack: identifier -> file.
local function bp_blocks(bpdir)
    local out = {}
    for _, f in ipairs(json_files(path.join(bpdir, "blocks"))) do
        local doc = try_json(f)
        local id = doc and doc["minecraft:block"] and doc["minecraft:block"].description and doc["minecraft:block"].description.identifier
        if type(id) == "string" then out[id] = {file = f, doc = doc} end
    end
    return out
end

-- Identifiers defined by hand-made BP files (the manual layer).
local function manual_ids(dirs)
    local out = {}
    for _, dir in ipairs(dirs) do
        for _, f in ipairs(json_files(dir)) do
            local doc = try_json(f)
            local id = doc and doc["minecraft:block"] and doc["minecraft:block"].description and doc["minecraft:block"].description.identifier
            if type(id) == "string" then out[id] = f end
        end
    end
    return out
end

local function merge_json_file(file, fn)
    local doc = os.isfile(file) and try_json(file) or nil
    doc = fn(doc)
    if doc ~= nil then
        os.mkdir(path.directory(file))
        json.savefile(file, doc)
    end
end

local function safe_name(id) return (id:gsub(":", "_"):gsub("/", "_")) end

-- ---- generation ----------------------------------------------------------------------------------

-- Generates the family blocks of `entries` into one BP / RP pair. Returns {blocks, skipped, files}.
local function generate_set(entries, bpdir, rpdir, opts, errors)
    local by_id, order = group(entries, errors)
    local existing = bp_blocks(bpdir)
    local manual = opts.manual or {}
    local ctx = {geos = {}, all = {}}
    for _, id in ipairs(order) do
        local g = by_id[id]
        ctx.all[g.shape] = ctx.all[g.shape] or {}
        table.insert(ctx.all[g.shape], id)
    end
    ctx.loot_double = function (e)
        local rel = "loot_tables/gf_families/" .. safe_name(e.id) .. "_double.json"
        local file = path.join(bpdir, rel)
        os.mkdir(path.directory(file))
        json.savefile(file, {pools = arr({{rolls = 1, entries = arr({{type = "item", name = e.id, weight = 1,
            functions = arr({{["function"] = "set_count", count = 2}})}})}})})
        return rel
    end
    local result = {blocks = 0, skipped = 0, files = {}, sounds = {}}
    for _, id in ipairs(order) do
        local g = by_id[id]
        if manual[id] then
            result.skipped = result.skipped + 1
        else
            local skel = existing[id]
            local base = g.params.base and existing[g.params.base] or nil
            local e = {id = id, params = g.params}
            local doc = base_doc(e, skel and skel.doc or nil, base and base.doc or nil)
            local t = textures_of(e, skel and skel.doc or (base and base.doc) or nil)
            local build = builders[g.shape]
            build(doc, e, t, ctx)
            for _, m in ipairs(sorted_keys(g.modifiers)) do modifiers[m](doc, e, t, ctx) end
            if g.params.components then
                for k, v in pairs(g.params.components) do comps(doc)[k] = v end
            end
            if #block_of(doc).permutations == 0 then block_of(doc).permutations = nil end
            local file = skel and skel.file or path.join(bpdir, "blocks", "gf_families", safe_name(id) .. ".json")
            os.mkdir(path.directory(file))
            json.savefile(file, doc)
            table.insert(result.files, file)
            result.blocks = result.blocks + 1
            local sound = g.params.sound or DEFAULT_SOUND[g.shape]
            if sound then result.sounds[id] = sound end
        end
    end
    -- geometries
    for _, gid in ipairs(sorted_keys(ctx.geos)) do
        local file = path.join(rpdir, GEO_DIR, gid:gsub("^geometry%.gf_families%.", "") .. ".geo.json")
        os.mkdir(path.directory(file))
        json.savefile(file, ctx.geos[gid])
    end
    -- block sounds (RP blocks.json; an id that has an entry keeps it)
    if not table.empty(result.sounds) then
        merge_json_file(path.join(rpdir, "blocks.json"), function (doc)
            doc = doc or {format_version = "1.21.40"}
            for id, sound in pairs(result.sounds) do
                if doc[id] == nil then doc[id] = {sound = sound} elseif type(doc[id]) == "table" and doc[id].sound == nil then doc[id].sound = sound end
            end
            return doc
        end)
    end
    return result
end

-- The fixture families (tools/families/fixtures/*.json): {entries, textures, names}.
local function fixtures(projectdir, errors)
    local out = {entries = {}, textures = {}, names = {}}
    for _, f in ipairs(json_files(path.join(projectdir, "tools", "families", "fixtures"))) do
        local doc = try_json(f)
        if type(doc) ~= "table" then
            table.insert(errors, "unreadable fixture file " .. f)
        else
            read_entries(f, out.entries, errors)
            for k, v in pairs(doc.textures or {}) do out.textures[k] = v end
            for k, v in pairs(doc.names or {}) do out.names[k] = v end
        end
    end
    return out
end

-- Every family entry of an assembled output (data/gf/families/**.json).
function families(outdir)
    local entries, errors = {}, {}
    for _, f in ipairs(json_files(path.join(outdir, "data", "gf", "families"))) do read_entries(f, entries, errors) end
    return entries, errors
end

-- Generates the family blocks into the packs of the assembled directory `outdir`.
-- opts: {manual_dirs = {...}} (default: assets/pack/bp and assets/test/bp of `projectdir`).
-- Returns {blocks, skipped, fixtures, files = {...}, errors = {...}}.
function generate(projectdir, outdir, opts)
    opts = opts or {}
    local errors = {}
    local manual = manual_ids(opts.manual_dirs or {path.join(projectdir, "assets", "pack", "bp"), path.join(projectdir, "assets", "test", "bp")})
    local summary = {blocks = 0, skipped = 0, fixtures = 0, files = {}, errors = errors}

    local bp = path.join(outdir, "behavior_packs", "GloamingForest_BP")
    local rp = path.join(outdir, "resource_packs", "GloamingForest_RP")
    local entries, ferrors = families(outdir)
    for _, e in ipairs(ferrors) do table.insert(errors, e) end
    if os.isdir(bp) and os.isdir(rp) and #entries > 0 then
        local r = generate_set(entries, bp, rp, {manual = manual}, errors)
        summary.blocks, summary.skipped = r.blocks, r.skipped
        for _, f in ipairs(r.files) do table.insert(summary.files, f) end
    end

    -- fixtures into the gf_test packs (development builds)
    local tbp = path.join(outdir, "behavior_packs", "GloamingForest_Test_BP")
    local trp = path.join(outdir, "resource_packs", "GloamingForest_Test_RP")
    if os.isdir(tbp) and os.isdir(trp) and not opts.no_fixtures then
        local fx = fixtures(projectdir, errors)
        if #fx.entries > 0 then
            local r = generate_set(fx.entries, tbp, trp, {manual = manual}, errors)
            summary.fixtures = r.blocks
            for _, f in ipairs(r.files) do table.insert(summary.files, f) end
            -- fixture textures (vanilla texture paths) and names
            merge_json_file(path.join(trp, "textures", "terrain_texture.json"), function (doc)
                doc = doc or {resource_pack_name = "gloamingforest", texture_name = "atlas.terrain", padding = 8, num_mip_levels = 4, texture_data = {}}
                doc.texture_data = doc.texture_data or {}
                for k, v in pairs(fx.textures) do doc.texture_data[k] = {textures = v} end
                return doc
            end)
            local lines = {}
            for _, id in ipairs(sorted_keys(fx.names)) do table.insert(lines, "tile." .. id .. ".name=" .. fx.names[id]) end
            for _, locale in ipairs({"en_US", "zh_CN"}) do
                local file = path.join(trp, "texts", locale .. ".lang")
                local text = os.isfile(file) and io.readfile(file) or ""
                local add = {}
                for _, l in ipairs(lines) do
                    local key = l:match("^([^=]+)=")
                    if not text:find("\n" .. key .. "=", 1, true) and not text:find("^" .. key:gsub("%p", "%%%0") .. "=") then table.insert(add, l) end
                end
                if #add > 0 then
                    if #text > 0 and not text:match("\n$") then text = text .. "\n" end
                    os.mkdir(path.directory(file))
                    io.writefile(file, text .. table.concat(add, "\n") .. "\n")
                end
            end
            -- the runtime reads the fixture families like any unit's
            local data = {}
            for _, e in ipairs(fx.entries) do table.insert(data, {id = e.id, family = e.family, params = e.params}) end
            local dfile = path.join(outdir, "data", "gf_test", "families", "fixtures.json")
            os.mkdir(path.directory(dfile))
            json.savefile(dfile, data)
        end
    end
    return summary
end
