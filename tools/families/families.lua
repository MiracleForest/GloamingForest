-- GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
-- Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
--   src/main/java/twilightforest/client/model/block/connected/ConnectionLogic.java
--   src/main/java/twilightforest/client/model/block/connected/ConnectedTextureModel.java
--   src/main/java/twilightforest/client/model/block/connected/UnbakedConnectedTextureModel.java
--   src/main/java/twilightforest/block/WallPillarBlock.java
--   src/main/java/twilightforest/block/ConnectableRotatedPillarBlock.java
--   src/main/java/twilightforest/block/SnowLoggable.java
--   src/main/java/twilightforest/client/renderer/block/TFChestRenderer.java
--   src/main/java/twilightforest/client/event/ColorHandler.java
--   src/main/java/twilightforest/util/woods/TFWoodTypes.java
--   src/main/java/twilightforest/init/TFSoundTypes.java
--   src/main/java/twilightforest/init/TFSounds.java
--   src/main/java/twilightforest/init/TFBlocks.java
-- Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
-- Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
-- of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
--
-- Contract "families+blockevents" (F11): the assemble-time block family generator.
--
-- About 400 TF blocks are variations of vanilla shapes (stairs, slabs, fences, doors, logs,
-- leaves, signs, chests, ...). Units declare them as data (assets/data/families/<u>.json; F1
-- imports TF's as data/gf/families/<u>.json) and this module completes their Bedrock block
-- definitions after the packs are assembled (the data schema: tools/families/README.md):
--
-- * A hand-made BP file (assets/pack/bp/**, assets/test/bp/**) is kept: the manual layer wins.
-- * An F1 skeleton (assets/gen/import/<u>/bp/blocks/<id>.json) keeps its states (frozen,
--   docs/spec/blockstates.md: Java's properties as twilightforest:<p> with Java's values) and, for
--   the families Java draws with block models, its geometry and permutations (F1 converts TF's
--   models). The kit adds what the block does: collision and selection boxes per state (Java's
--   getShape / getCollisionShape), redstone output, connection rules, liquid detection, light,
--   tint, snow, block sounds, and the states it adds (rule R1: additions only).
-- * The families Java draws with block entity renderers (signs, hanging signs, chests: F1 has a
--   placeholder cube) and blocks without a skeleton (the gf_test fixtures, units' own blocks) get
--   their model from here: block models converted as F1 does (Java block space, x mirrored into
--   Bedrock's geometry), entity models (Java's ModelPart layouts, SignRenderer, HangingSignRenderer,
--   ChestRenderer) turned into per-face UVs.
-- * No placement traits: the runtime (src/feature/families) sets the states on placement as Java's
--   getStateForPlacement does.
-- * Extras: shared geometries (RP models/blocks/gf_families/), the sign text display entity,
--   entity textures in terrain_texture.json, block sounds (blocks.json; TF's sound types as
--   sounds.json block_sounds), double slab loot tables.
--
-- Entry points: generate(projectdir, outdir, opts) (tools/xmake/assemble.lua after the packs are
-- built, or tools/families/run.lua on an assembled directory); families() for the checks.

import("core.base.json")

local NS = "twilightforest:"
-- the states the kit adds that no Java block declares (chests' lid and open_count, connected textures):
-- TF's blocks keep exactly their frozen twilightforest:* states (docs/spec/blockstates.md)
local KIT = "gloamingforest:"
local GEO_DIR = "models/blocks/gf_families"
local FORMAT = "1.26.0"
-- Spike S7 (F1, docs/spikes/S7.md): from format 1.26.20 (the v1_26_20 block schema) a block declares
-- its tags as one "minecraft:tags" array and may carry minecraft:block_entity; up to 1.26.10 it
-- declares them as "tag:<id>": {} components and takes neither. The chests are written in
-- BLOCK_ENTITY_FORMAT: minecraft:block_entity gives them a data-driven block actor, which F10's
-- inventory contract fills with a game container (its native backend, spike S3).
local TAGS_ARRAY_FORMAT = "1.26.20"
local BLOCK_ENTITY_FORMAT = "1.26.50"
local GEO_FORMAT = "1.21.0"
local SIGN_TEXT_ENTITY = "gloamingforest:sign_text"

local FACES6 = {"down", "up", "north", "south", "west", "east"}
local DIRS4 = {"north", "east", "south", "west"}
local DX = {north = 0, east = 1, south = 0, west = -1, up = 0, down = 0}
local DY = {north = 0, east = 0, south = 0, west = 0, up = 1, down = -1}
local DZ = {north = -1, east = 0, south = 1, west = 0, up = 0, down = 0}
local OPP = {north = "south", south = "north", east = "west", west = "east", up = "down", down = "up"}
local CCW = {north = "west", west = "south", south = "east", east = "north"}
local D2 = {south = 0, west = 1, north = 2, east = 3} -- Java Direction.get2DDataValue
-- Java blockstate y rotations (clockwise quarter turns) of a model that faces south, and the
-- Bedrock transformation (counter-clockwise degrees) doing the same
local QUARTERS = {south = 0, west = 1, north = 2, east = 3}
local TURN = {south = 0, west = -90, north = 180, east = 90}

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

local function list(...) return arr({...}) end
local function BOOL() return list(false, true) end

local function q(state) return "q.block_state('" .. state .. "')" end
-- one state value as Molang (booleans q / !q, strings == 'v', integers == n)
local function is(state, v)
    if type(v) == "boolean" then return v and q(state) or ("!" .. q(state)) end
    if type(v) == "string" then return q(state) .. " == '" .. v .. "'" end
    return q(state) .. " == " .. tostring(v)
end
local function all(...)
    local parts = {}
    for _, p in ipairs({...}) do if p and p ~= "" and p ~= true then table.insert(parts, "(" .. p .. ")") end end
    if #parts == 0 then return "1" end
    return table.concat(parts, " && ")
end
local function any(items)
    local parts = {}
    for _, p in ipairs(items) do table.insert(parts, "(" .. p .. ")") end
    if #parts == 0 then return "0" end
    if #parts == 1 then return items[1] end
    return table.concat(parts, " || ")
end

local function try_json(file)
    return try { function () return json.loadfile(file) end }
end

local function contains(t, v)
    for _, x in ipairs(t or {}) do if x == v then return true end end
    return false
end

-- ---- Java block space -> Bedrock geometry --------------------------------------------------------
--
-- Boxes are {x0, y0, z0, x1, y1, z1} in block pixels in Java's frame (x east, y up, z south).
-- Bedrock's block geometry has x mirrored (its +x is west): a cube's origin is 8 - x1 (Blockbench
-- and F1's converter do the same), face keys stay the world directions, the per-face UVs are Java's
-- (up and down are flipped by Bedrock: their uv and uv_size are reversed), rotations become
-- [-x, -y, z] about the mirrored pivot. Collision and selection boxes are not mirrored (x - 8).

-- Java's default UV of a face of an element (BlockElement.uvsByFace): {u0, v0, u1, v1}
local function face_rect(b, face)
    local x0, y0, z0, x1, y1, z1 = b[1], b[2], b[3], b[4], b[5], b[6]
    if face == "down" then return {x0, 16 - z1, x1, 16 - z0} end
    if face == "up" then return {x0, z0, x1, z1} end
    if face == "north" then return {16 - x1, 16 - y1, 16 - x0, 16 - y0} end
    if face == "south" then return {x0, 16 - y1, x1, 16 - y0} end
    if face == "west" then return {z0, 16 - y1, z1, 16 - y0} end
    return {16 - z1, 16 - y1, 16 - z0, 16 - y0} -- east
end

local function bedrock_uv(face, r)
    if face == "up" or face == "down" then
        return {uv = {r[3], r[4]}, uv_size = {r[1] - r[3], r[2] - r[4]}}
    end
    return {uv = {r[1], r[2]}, uv_size = {r[3] - r[1], r[4] - r[2]}}
end

local function bedrock_rotation(axis, angle)
    if axis == "x" then return {-angle, 0, 0} end
    if axis == "y" then return {0, -angle, 0} end
    return {0, 0, angle}
end

-- A Bedrock cube of the Java box b. opts: mat (material instance of every face), only (set of
-- faces), faces ({face = {rect = {u0, v0, u1, v1}, mat = name}}: explicit UVs; with
-- faces_only, the other faces are left out), rot ({axis, angle, origin, rescale}: Java's element
-- rotation, angle in degrees counter-clockwise about the axis).
local function cube(b, opts)
    opts = opts or {}
    local box = {b[1], b[2], b[3], b[4], b[5], b[6]}
    local r = opts.rot
    if r and r.rescale then
        -- Java BlockElementRotation.rescale: stretch the two other axes by 1 / cos(angle)
        local s = 1 / math.cos(math.rad(math.abs(r.angle)))
        for k, ax in ipairs({"x", "y", "z"}) do
            if ax ~= r.axis then
                box[k] = r.origin[k] + (box[k] - r.origin[k]) * s
                box[k + 3] = r.origin[k] + (box[k + 3] - r.origin[k]) * s
            end
        end
    end
    local c = {origin = {8 - box[4], box[2], box[3] - 8}, size = {box[4] - box[1], box[5] - box[2], box[6] - box[3]}, uv = {}}
    for _, f in ipairs(FACES6) do
        local spec = opts.faces and opts.faces[f]
        if (not opts.only or opts.only[f]) and (spec or not opts.faces_only) then
            local u = bedrock_uv(f, spec and spec.rect or face_rect(b, f))
            local m = spec and spec.mat or opts.mat
            if m then u.material_instance = m end
            c.uv[f] = u
        end
    end
    if r then
        c.pivot = {8 - r.origin[1], r.origin[2], r.origin[3] - 8}
        c.rotation = bedrock_rotation(r.axis, r.angle)
    end
    return c
end

local function bone(name, cubes, opts)
    opts = opts or {}
    local b = {name = name, pivot = opts.pivot or {0, 0, 0}, cubes = arr(cubes)}
    if opts.rotation then b.rotation = opts.rotation end
    return b
end

local function geo_doc(id, bones, tw, th)
    return {
        format_version = GEO_FORMAT,
        ["minecraft:geometry"] = arr({{
            description = {
                identifier = id, texture_width = tw or 16, texture_height = th or 16,
                visible_bounds_width = 3, visible_bounds_height = 3, visible_bounds_offset = {0, 0.75, 0},
            },
            bones = arr(bones),
        }}),
    }
end

-- A box as a collision / selection box component (world aligned; Bedrock caps the height at 16).
local function cbox(b)
    return {origin = {b[1] - 8, b[2], b[3] - 8}, size = {b[4] - b[1], math.min(b[5], 16) - b[2], b[6] - b[3]}}
end
local function cboxes(boxes)
    if #boxes == 0 then return false end
    if #boxes == 1 then return cbox(boxes[1]) end
    local out = arr({})
    for _, b in ipairs(boxes) do table.insert(out, cbox(b)) end
    return out
end
-- A box of the block's world frame (Java's AABB for that facing) in the frame of the south-facing
-- model, which a transformation of `quarters` clockwise quarter turns (QUARTERS) turns back into
-- place: Bedrock's transformation turns the selection and collision boxes with the geometry, so a
-- block turned by a transformation declares its boxes unturned.
local function unturn(b, quarters)
    local x1, y1, z1, x2, y2, z2 = b[1], b[2], b[3], b[4], b[5], b[6]
    for _ = 1, (quarters or 0) % 4 do
        x1, z1, x2, z2 = z1, 16 - x2, z2, 16 - x1
    end
    return {x1, y1, z1, x2, y2, z2}
end
local function bounds(boxes)
    local o = {16, 16, 16, 0, 0, 0}
    for _, b in ipairs(boxes) do
        for i = 1, 3 do o[i] = math.min(o[i], b[i]) end
        for i = 4, 6 do o[i] = math.max(o[i], b[i]) end
    end
    return o
end

-- rotate a Java box clockwise (seen from above) about the block centre, n quarter turns:
-- north -> east -> south -> west (Java blockstate "y": 90)
local function rot_y(b, n)
    n = n % 4
    for _ = 1, n do b = {16 - b[6], b[2], b[1], 16 - b[3], b[5], b[4]} end
    return b
end

-- ---- Java ModelPart (block entity renderers) -----------------------------------------------------
--
-- Affine maps of block pixels: {m11, m12, m13, m21, m22, m23, m31, m32, m33, tx, ty, tz}.

local function mat3(a, b, c, d, e, f, g, h, i, tx, ty, tz) return {a, b, c, d, e, f, g, h, i, tx or 0, ty or 0, tz or 0} end
local function mmul(A, B)
    local r = {}
    for i = 0, 2 do
        for j = 0, 2 do
            local s = 0
            for k = 0, 2 do s = s + A[i * 3 + k + 1] * B[k * 3 + j + 1] end
            r[i * 3 + j + 1] = s
        end
    end
    for i = 0, 2 do
        local s = A[10 + i]
        for k = 0, 2 do s = s + A[i * 3 + k + 1] * B[10 + k] end
        r[10 + i] = s
    end
    return r
end
local function mapply(A, p)
    return {A[1] * p[1] + A[2] * p[2] + A[3] * p[3] + A[10], A[4] * p[1] + A[5] * p[2] + A[6] * p[3] + A[11], A[7] * p[1] + A[8] * p[2] + A[9] * p[3] + A[12]}
end
local function mtrans(x, y, z) return mat3(1, 0, 0, 0, 1, 0, 0, 0, 1, x, y, z) end
local function mscale(x, y, z) return mat3(x, 0, 0, 0, y, 0, 0, 0, z) end
local function cos_sin(deg)
    local r = deg % 360
    if r == 0 then return 1, 0 elseif r == 90 then return 0, 1 elseif r == 180 then return -1, 0 elseif r == 270 then return 0, -1 end
    return math.cos(math.rad(deg)), math.sin(math.rad(deg))
end
-- right-handed rotation (Java Axis.XP / YP / ZP.rotationDegrees)
local function mrot(axis, deg)
    local c, s = cos_sin(deg)
    if axis == "x" then return mat3(1, 0, 0, 0, c, -s, 0, s, c) end
    if axis == "y" then return mat3(c, 0, s, 0, 1, 0, -s, 0, c) end
    return mat3(c, -s, 0, s, c, 0, 0, 0, 1)
end
local function mdet(A)
    return A[1] * (A[5] * A[9] - A[6] * A[8]) - A[2] * (A[4] * A[9] - A[6] * A[7]) + A[3] * (A[4] * A[8] - A[5] * A[7])
end
local function mmulall(...)
    local out = mat3(1, 0, 0, 0, 1, 0, 0, 0, 1)
    for _, m in ipairs({...}) do out = mmul(out, m) end
    return out
end

-- Java ModelPart.Cube: the faces of a box (origin x0 y0 z0, size dx dy dz) with the texture at
-- (u, v), each a quad of {p = vertex, uv = {u, v}} (Polygon: vertex 1 gets (u2, v1), 2 (u1, v1),
-- 3 (u1, v2), 4 (u2, v2)).
local function modelpart_faces(x0, y0, z0, dx, dy, dz, u, v)
    local x1, y1, z1 = x0 + dx, y0 + dy, z0 + dz
    local v7, v0, v1, v2 = {x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}
    local v3, v4, v5, v6 = {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}
    local f4, f5, f6, f7 = u, u + dz, u + dz + dx, u + dz + dx + dx
    local f8, f9 = u + dz + dx + dz, u + dz + dx + dz + dx
    local f10, f11, f12 = v, v + dz, v + dz + dy
    local function poly(normal, vs, u1, vv1, u2, vv2)
        return {normal = normal, {p = vs[1], uv = {u2, vv1}}, {p = vs[2], uv = {u1, vv1}}, {p = vs[3], uv = {u1, vv2}}, {p = vs[4], uv = {u2, vv2}}}
    end
    return {
        poly({0, -1, 0}, {v4, v3, v7, v0}, f5, f10, f6, f11),
        poly({0, 1, 0}, {v1, v2, v6, v5}, f6, f11, f7, f10),
        poly({-1, 0, 0}, {v7, v3, v6, v2}, f4, f11, f5, f12),
        poly({0, 0, -1}, {v0, v7, v2, v1}, f5, f11, f6, f12),
        poly({1, 0, 0}, {v4, v0, v1, v5}, f6, f11, f8, f12),
        poly({0, 0, 1}, {v3, v4, v5, v6}, f8, f11, f9, f12),
    }
end

-- The block-model corners of a world face: {left / top selector}: for each world face, which
-- vertex is the top-left (u0, v0) and which the bottom-right (u1, v1) of Java's face UVs.
local function face_corners(face, lo, hi)
    -- returns predicates on a vertex p for top-left and bottom-right
    local function eq(a, b) return math.abs(a - b) < 1e-6 end
    if face == "north" then return function (p) return eq(p[1], hi[1]) and eq(p[2], hi[2]) end, function (p) return eq(p[1], lo[1]) and eq(p[2], lo[2]) end end
    if face == "south" then return function (p) return eq(p[1], lo[1]) and eq(p[2], hi[2]) end, function (p) return eq(p[1], hi[1]) and eq(p[2], lo[2]) end end
    if face == "west" then return function (p) return eq(p[3], lo[3]) and eq(p[2], hi[2]) end, function (p) return eq(p[3], hi[3]) and eq(p[2], lo[2]) end end
    if face == "east" then return function (p) return eq(p[3], hi[3]) and eq(p[2], hi[2]) end, function (p) return eq(p[3], lo[3]) and eq(p[2], lo[2]) end end
    if face == "up" then return function (p) return eq(p[1], lo[1]) and eq(p[3], lo[3]) end, function (p) return eq(p[1], hi[1]) and eq(p[3], hi[3]) end end
    return function (p) return eq(p[1], lo[1]) and eq(p[3], hi[3]) end, function (p) return eq(p[1], hi[1]) and eq(p[3], lo[3]) end -- down
end

-- Bedrock cubes of ModelPart parts drawn through `static` (block pixels; it must keep the axes,
-- e.g. Java's scale(f, -f, -f)). A part: {box = {x0, y0, z0, dx, dy, dz}, uv = {u, v}, offset =
-- {x, y, z}, rot = {axis, degrees}} (Java's PartPose: offset and one rotation, which becomes the
-- cube's rotation about the part's pivot).
local function part_cubes(parts, static, mat)
    local out = {}
    for _, part in ipairs(parts) do
        local o = part.offset or {0, 0, 0}
        local m = mmul(static, mtrans(o[1], o[2], o[3]))
        local b = part.box
        local polys = modelpart_faces(b[1], b[2], b[3], b[4], b[5], b[6], part.uv[1], part.uv[2])
        -- world corners
        local lo, hi = {math.huge, math.huge, math.huge}, {-math.huge, -math.huge, -math.huge}
        local world = {}
        for i, poly in ipairs(polys) do
            -- the world face: the polygon's normal through the map (Java's Polygon.normal)
            local n = poly.normal
            local w = {m[1] * n[1] + m[2] * n[2] + m[3] * n[3], m[4] * n[1] + m[5] * n[2] + m[6] * n[3], m[7] * n[1] + m[8] * n[2] + m[9] * n[3]}
            local face
            for k, names in ipairs({{"west", "east"}, {"down", "up"}, {"north", "south"}}) do
                if math.abs(w[k]) > 1e-6 then face = w[k] < 0 and names[1] or names[2] end
            end
            world[i] = {face = face}
            for j, vx in ipairs(poly) do
                local p = mapply(m, vx.p)
                world[i][j] = {p = p, uv = vx.uv}
                for k = 1, 3 do lo[k] = math.min(lo[k], p[k]); hi[k] = math.max(hi[k], p[k]) end
            end
        end
        local faces = {}
        for _, quad in ipairs(world) do
            local face = quad.face
            -- a face without area (the sides of a flat chain) is not drawn
            local area = true
            if face == "west" or face == "east" then area = hi[2] - lo[2] > 1e-6 and hi[3] - lo[3] > 1e-6
            elseif face == "down" or face == "up" then area = hi[1] - lo[1] > 1e-6 and hi[3] - lo[3] > 1e-6
            else area = hi[1] - lo[1] > 1e-6 and hi[2] - lo[2] > 1e-6 end
            if face and area and not faces[face] then
                local tl, br = face_corners(face, lo, hi)
                local a, z
                for _, vx in ipairs(quad) do
                    if tl(vx.p) then a = vx end
                    if br(vx.p) then z = vx end
                end
                if a and z then faces[face] = {rect = {a.uv[1], a.uv[2], z.uv[1], z.uv[2]}, mat = mat} end
            end
        end
        local opts = {faces = faces, faces_only = true}
        if part.rot then
            -- the part's rotation in the world: conjugated by the static map (axis and handedness)
            local ax = ({x = {1, 0, 0}, y = {0, 1, 0}, z = {0, 0, 1}})[part.rot[1]]
            local w = {m[1] * ax[1] + m[2] * ax[2] + m[3] * ax[3], m[4] * ax[1] + m[5] * ax[2] + m[6] * ax[3], m[7] * ax[1] + m[8] * ax[2] + m[9] * ax[3]}
            local k, s = 1, 1
            for i = 1, 3 do if math.abs(w[i]) > 1e-6 then k, s = i, (w[i] > 0 and 1 or -1) end end
            local det = mdet(m) > 0 and 1 or -1
            local pivot = mapply(m, {0, 0, 0})
            opts.rot = {axis = ({"x", "y", "z"})[k], angle = part.rot[2] * s * det, origin = pivot}
        end
        table.insert(out, cube({lo[1], lo[2], lo[3], hi[1], hi[2], hi[3]}, opts))
    end
    return out
end

-- ---- TF's data (generator side) ------------------------------------------------------------------

-- TFChestRenderer: the chest texture folder of each chest block
local CHEST_TEXTURE_DIRS = {
    twilight_oak = "twilight", canopy = "canopy", mangrove = "mangrove", dark = "darkwood",
    time = "time", transformation = "transformation", mining = "mining", sorting = "sorting",
}


-- ColorHandler: the leaves Bedrock tints by biome. Biome foliage (twilight oak, dark leaves) as
-- Java; canopy and mangrove leaves blend the biome foliage with 0x469A66 / 0xC0E694
-- (CANOPY_COLORIZER / MANGROVE_COLORIZER), which Bedrock's tint methods can't: they take the tint
-- of Java's colour without a world (FoliageColor.getEvergreenColor / getBirchColor).
-- Time, transformation, mining, sorting and rainbow oak leaves: ColorHandler colours them by
-- position alone. They get a tint method so the client mod's tint (gf::client::tint, F12's
-- PositionDependentTintResolver hook, which only runs for tinted materials) draws Java's colour
-- per position (src/client/families); a vanilla client tints them with the biome's foliage.
local LEAF_TINTS = {
    ["twilightforest:twilight_oak_leaves"] = "default_foliage", ["twilightforest:dark_leaves"] = "default_foliage",
    ["twilightforest:canopy_leaves"] = "evergreen_foliage", ["twilightforest:mangrove_leaves"] = "birch_foliage",
    ["twilightforest:time_leaves"] = "default_foliage", ["twilightforest:transformation_leaves"] = "default_foliage",
    ["twilightforest:mining_leaves"] = "default_foliage", ["twilightforest:sorting_leaves"] = "default_foliage",
    ["twilightforest:rainbow_oak_leaves"] = "default_foliage",
}
-- The leaves Java always colours the same (FoliageColor.getEvergreenColor): their grey textures
-- pre-tinted with it (terrain_texture.json tint_color), the same on every client.
local LEAF_COLORS = {
    ["twilightforest:beanstalk_leaves"] = "#619961", ["twilightforest:thorn_leaves"] = "#619961",
}

-- The wood of a sign / chest (TFWoodTypes names: twilight_oak, canopy, mangrove, dark, time,
-- transformation, mining, sorting): params.wood, F1's woodType ("canopy_wood_type"), or the id.
local function wood_of(e)
    local p = e.params
    if type(p.wood) == "string" then return p.wood end
    if type(p.woodType) == "string" then
        local w = p.woodType:gsub("_wood_type$", ""):gsub("_wood_set$", ""):gsub("_set$", "")
        return w
    end
    local name = e.id:match(":(.+)$") or e.id
    for _, suffix in ipairs({"_wall_hanging_sign", "_hanging_sign", "_wall_sign", "_sign", "_trapped_chest", "_chest"}) do
        if name:sub(-#suffix) == suffix then return name:sub(1, #name - #suffix) end
    end
    return name
end

-- ---- families: states, visuals, behaviour --------------------------------------------------------

local WALL_SIDE = {"none", "low", "tall"}
local STAIR_SHAPES = {"straight", "inner_left", "inner_right", "outer_left", "outer_right"}
local FACING4 = {"north", "south", "west", "east"}
local FACING6 = {"north", "south", "west", "east", "up", "down"}
local AXES = {"y", "x", "z"}

local function sides(values) -- the four horizontal sides as states
    return {{"east", values}, {"north", values}, {"south", values}, {"west", values}}
end

-- Java's properties of each family (docs/spec/blockstates.md), plus the states the kit adds.
local FAMILY_STATES = {
    stairs = {{"facing", FACING4}, {"half", {"bottom", "top"}}, {"shape", STAIR_SHAPES}},
    slab = {{"type", {"bottom", "top", "double"}}},
    fence = sides("bool"), pane = sides("bool"),
    wall = {{"east", WALL_SIDE}, {"north", WALL_SIDE}, {"south", WALL_SIDE}, {"up", "bool"}, {"west", WALL_SIDE}},
    fence_gate = {{"facing", FACING4}, {"in_wall", "bool"}, {"open", "bool"}, {"powered", "bool"}},
    door = {{"facing", FACING4}, {"half", {"lower", "upper"}}, {"hinge", {"left", "right"}}, {"open", "bool"}, {"powered", "bool"}},
    trapdoor = {{"facing", FACING4}, {"half", {"bottom", "top"}}, {"open", "bool"}, {"powered", "bool"}},
    button = {{"face", {"wall", "floor", "ceiling"}}, {"facing", FACING4}, {"powered", "bool"}},
    pressure_plate = {{"powered", "bool"}},
    pillar = {{"axis", AXES}}, log = {{"axis", AXES}}, wood = {{"axis", AXES}},
    leaves = {{"distance", 7}, {"persistent", "bool"}},
    sapling = {{"stage", 1}},
    sign = {{"rotation", 15}},
    wall_sign = {{"facing", FACING4}},
    hanging_sign = {{"attached", "bool"}, {"rotation", 15}},
    wall_hanging_sign = {{"facing", FACING4}},
    chest = {{"facing", FACING4}, {"type", {"single", "left", "right"}}, {"lid", 4, KIT}},
    trapped_chest = {{"facing", FACING4}, {"type", {"single", "left", "right"}}, {"lid", 4, KIT}, {"open_count", 15, KIT}},
    horizontal = {{"facing", FACING4}},
    directional = {{"facing", FACING6}},
    binary_rotated = {{"rotated", "bool"}},
    wall_pillar = {{"axis", AXES}, {"down", "bool"}, {"east", "bool"}, {"north", "bool"}, {"south", "bool"}, {"up", "bool"}, {"west", "bool"}},
    connectable_pillar = {{"axis", AXES}, {"down", "bool"}, {"east", "bool"}, {"north", "bool"}, {"south", "bool"}, {"up", "bool"}, {"west", "bool"}},
    connected_texture = {{"down", "bool", KIT}, {"east", "bool", KIT}, {"north", "bool", KIT}, {"south", "bool", KIT}, {"up", "bool", KIT}, {"west", "bool", KIT}},
}

-- Families Java draws with block entity renderers: the kit makes their model even over a skeleton.
local ENTITY_MODEL = {sign = true, wall_sign = true, hanging_sign = true, wall_hanging_sign = true, chest = true, trapped_chest = true, connected_texture = true}

local function state_values(spec)
    if spec == "bool" then return BOOL() end
    if type(spec) == "number" then return ints(spec) end
    return arr(copy(spec))
end

local function block_of(doc) return doc["minecraft:block"] end
local function comps(doc) return block_of(doc).components end

-- ---- block tags (spike S7) -------------------------------------------------------------------------

-- "1.26.50" >= "1.26.20"
local function version_at_least(v, min)
    local a, b = {}, {}
    for n in tostring(v or "0"):gmatch("%d+") do table.insert(a, tonumber(n)) end
    for n in tostring(min):gmatch("%d+") do table.insert(b, tonumber(n)) end
    for i = 1, math.max(#a, #b) do
        local x, y = a[i] or 0, b[i] or 0
        if x ~= y then return x > y end
    end
    return true
end
-- the document's format takes the minecraft:tags array (else "tag:<id>" components)
local function tags_as_array(doc) return version_at_least(doc.format_version, TAGS_ARRAY_FORMAT) end

-- The tags a components table carries, in either form (in order, once each).
local function tags_in(components, out, seen)
    out, seen = out or {}, seen or {}
    local function add(t)
        if type(t) == "string" and t ~= "" and not seen[t] then seen[t] = true; table.insert(out, t) end
    end
    local array = components and components["minecraft:tags"]
    if type(array) == "table" then for _, t in ipairs(array.tags or array) do add(t) end end
    local legacy = {}
    for k, _ in pairs(components or {}) do
        if type(k) == "string" and k:match("^tag:") then table.insert(legacy, k:sub(5)) end
    end
    table.sort(legacy)
    for _, t in ipairs(legacy) do add(t) end
    return out, seen
end

-- Drops both tag forms from a components table.
local function strip_tags(components)
    if type(components) ~= "table" then return end
    for k, _ in pairs(components) do
        if type(k) == "string" and k:match("^tag:") then components[k] = nil end
    end
    components["minecraft:tags"] = nil
end

-- The Bedrock block tags of `entry`, free of any format: F1's params.tags (the tool tags and the tier
-- tag of a block a tool mines, data/gf/families/<u>.json), else the tags of the document the block
-- starts from (the skeleton or the base block, in either form); params.tool adds its tool's tag.
local function block_tags(entry, source)
    local p = entry.params
    local out, seen
    if type(p.tags) == "table" then
        out, seen = {}, {}
        for _, t in ipairs(p.tags) do
            if type(t) == "string" and t ~= "" and not seen[t] then seen[t] = true; table.insert(out, t) end
        end
    else
        local scomps = source and source["minecraft:block"] and source["minecraft:block"].components or {}
        out, seen = tags_in(scomps)
    end
    if p.tool then
        local t = "minecraft:is_" .. p.tool .. "_item_destructible"
        if not seen[t] then seen[t] = true; table.insert(out, t) end
    end
    return out
end

-- Writes `tags` in the form the document's format takes (whatever tag form it carried goes).
local function write_tags(doc, tags)
    local c = comps(doc)
    strip_tags(c)
    if #tags == 0 then return end
    if tags_as_array(doc) then
        c["minecraft:tags"] = arr(copy(tags))
    else
        for _, t in ipairs(tags) do c["tag:" .. t] = {} end
    end
end
local function states_of(doc) return block_of(doc).description.states or {} end

-- adds a state unless the block has it (a skeleton's states are frozen)
local function ensure_state(doc, name, values)
    local d = block_of(doc).description
    d.states = d.states or {}
    if d.states[name] == nil then d.states[name] = values end
end
local function has_state(doc, name) return states_of(doc)[name] ~= nil end

local function perm(doc, condition, components)
    block_of(doc).permutations = block_of(doc).permutations or arr({})
    table.insert(block_of(doc).permutations, {condition = condition, components = components})
end

local function geometry(doc, id, vis)
    if vis then
        comps(doc)["minecraft:geometry"] = {identifier = id, bone_visibility = vis}
    else
        comps(doc)["minecraft:geometry"] = id
    end
end

local function mat(texture, method, extra)
    local m = {texture = texture, render_method = method or "opaque"}
    if extra then for k, v in pairs(extra) do m[k] = v end end
    return m
end

-- The textures of an entry: params.textures (string or {key = texture}) over the skeleton's
-- material instances (base and permutations: F1 names them after Java's texture variables).
local function textures_of(entry, skeleton)
    local t = {}
    local function scan(mi)
        if type(mi) ~= "table" then return end
        for k, v in pairs(mi) do
            if type(v) == "table" and type(v.texture) == "string" and t[k] == nil then t[k] = v.texture end
        end
    end
    local b = skeleton and skeleton["minecraft:block"]
    if b then
        scan(b.components and b.components["minecraft:material_instances"])
        for _, p in ipairs(b.permutations or {}) do scan(p.components and p.components["minecraft:material_instances"]) end
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
    return t["*"] or t.side or t.texture or t.up or "missing"
end

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

-- shared geometries
local function geo(ctx, id, make)
    if not ctx.geos[id] then ctx.geos[id] = make() end
    return id
end

-- box faces with top / bottom / side material instances (stairs, slabs)
local function tbs(b) return cube(b, {faces = {
    up = {rect = face_rect(b, "up"), mat = "top"}, down = {rect = face_rect(b, "down"), mat = "bottom"},
    north = {rect = face_rect(b, "north"), mat = "side"}, south = {rect = face_rect(b, "south"), mat = "side"},
    west = {rect = face_rect(b, "west"), mat = "side"}, east = {rect = face_rect(b, "east"), mat = "side"}}}) end

local function tbs_materials(t, method)
    return {
        ["*"] = mat(tex(t, "side", "*"), method), side = mat(tex(t, "side", "*"), method),
        top = mat(tex(t, "top", "up", "end", "*"), method), bottom = mat(tex(t, "bottom", "down", "end", "top", "*"), method),
    }
end

local visual, behaviour = {}, {}

-- stairs (Java StairBlock): facing / half / shape; the runtime keeps the shape (getStairsShape).
-- Shapes: the half slab and quarters, StairBlock.SHAPE_BY_STATE (bits nw 1, ne 2, sw 4, se 8 by
-- shape * 4 + facing.get2DDataValue).
local STAIR_BITS = {12, 5, 3, 10, 14, 13, 7, 11, 13, 7, 11, 14, 8, 4, 1, 2, 4, 1, 2, 8}
local QUARTER = {nw = {1, 0, 0}, ne = {2, 8, 0}, sw = {4, 0, 8}, se = {8, 8, 8}}
local function stair_pairs(bit) -- the (facing, shape) pairs whose step has quarter `bit`
    local out = {}
    for si, shape in ipairs(STAIR_SHAPES) do
        for _, f in ipairs(FACING4) do
            if (STAIR_BITS[(si - 1) * 4 + D2[f] + 1] & bit) ~= 0 then table.insert(out, {f, shape}) end
        end
    end
    return out
end
local function stair_cond(pairs)
    local parts = {}
    for _, p in ipairs(pairs) do table.insert(parts, all(is(NS .. "facing", p[1]), is(NS .. "shape", p[2]))) end
    return any(parts)
end
visual.stairs = function (doc, e, t, ctx)
    local g = geo(ctx, "geometry.gf_families.stairs", function ()
        local bones = {bone("slab_bottom", {tbs({0, 0, 0, 16, 8, 16})}), bone("slab_top", {tbs({0, 8, 0, 16, 16, 16})})}
        for _, qn in ipairs({"nw", "ne", "sw", "se"}) do
            local p = QUARTER[qn]
            table.insert(bones, bone("upper_" .. qn, {tbs({p[2], 8, p[3], p[2] + 8, 16, p[3] + 8})}))
            table.insert(bones, bone("lower_" .. qn, {tbs({p[2], 0, p[3], p[2] + 8, 8, p[3] + 8})}))
        end
        return geo_doc("geometry.gf_families.stairs", bones)
    end)
    local vis = {slab_bottom = is(NS .. "half", "bottom"), slab_top = is(NS .. "half", "top")}
    for qn, p in pairs(QUARTER) do
        local c = stair_cond(stair_pairs(p[1]))
        vis["upper_" .. qn] = all(is(NS .. "half", "bottom"), c)
        vis["lower_" .. qn] = all(is(NS .. "half", "top"), c)
    end
    geometry(doc, g, vis)
    comps(doc)["minecraft:material_instances"] = tbs_materials(t, e.params.render_method)
end
behaviour.stairs = function (doc)
    for _, half in ipairs({"bottom", "top"}) do
        local by_mask = {}
        for si, shape in ipairs(STAIR_SHAPES) do
            for _, f in ipairs(FACING4) do
                local m = STAIR_BITS[(si - 1) * 4 + D2[f] + 1]
                by_mask[m] = by_mask[m] or {}
                table.insert(by_mask[m], {f, shape})
            end
        end
        for _, m in ipairs(sorted_keys(by_mask)) do
            local boxes = {half == "bottom" and {0, 0, 0, 16, 8, 16} or {0, 8, 0, 16, 16, 16}}
            for _, p in pairs(QUARTER) do
                if (m & p[1]) ~= 0 then
                    local y0 = half == "bottom" and 8 or 0
                    table.insert(boxes, {p[2], y0, p[3], p[2] + 8, y0 + 8, p[3] + 8})
                end
            end
            perm(doc, all(is(NS .. "half", half), stair_cond(by_mask[m])),
                {["minecraft:collision_box"] = cboxes(boxes), ["minecraft:selection_box"] = true})
        end
    end
end

-- slab (Java SlabBlock): type bottom / top / double; the double slab drops two.
visual.slab = function (doc, e, t, ctx)
    local g = geo(ctx, "geometry.gf_families.slab", function ()
        return geo_doc("geometry.gf_families.slab", {bone("bottom", {tbs({0, 0, 0, 16, 8, 16})}), bone("top", {tbs({0, 8, 0, 16, 16, 16})})})
    end)
    geometry(doc, g, {bottom = any({is(NS .. "type", "bottom"), is(NS .. "type", "double")}), top = any({is(NS .. "type", "top"), is(NS .. "type", "double")})})
    comps(doc)["minecraft:material_instances"] = tbs_materials(t, e.params.render_method)
end
behaviour.slab = function (doc, e, ctx)
    perm(doc, is(NS .. "type", "bottom"), {["minecraft:collision_box"] = cbox({0, 0, 0, 16, 8, 16}), ["minecraft:selection_box"] = cbox({0, 0, 0, 16, 8, 16})})
    perm(doc, is(NS .. "type", "top"), {["minecraft:collision_box"] = cbox({0, 8, 0, 16, 16, 16}), ["minecraft:selection_box"] = cbox({0, 8, 0, 16, 16, 16})})
    local double = {["minecraft:collision_box"] = true, ["minecraft:selection_box"] = true}
    if ctx.loot_double then double["minecraft:loot"] = ctx.loot_double(e) end
    perm(doc, is(NS .. "type", "double"), double)
end

-- fence (Java FenceBlock / CrossCollisionBlock(2, 2, 16, 16, 24)) and pane (IronBarsBlock(1, 1,
-- 16, 16, 16)): north / east / south / west connections, kept by the runtime.
local function cross_shapes(node, ext, height)
    local f, f1, f2, f3 = 8 - node, 8 + node, 8 - ext, 8 + ext
    return {f, 0, f, f1, height, f1}, {
        north = {f2, 0, 0, f3, height, f3}, south = {f2, 0, f2, f3, height, 16},
        west = {0, 0, f2, f3, height, f3}, east = {f2, 0, f2, 16, height, f3},
    }
end
local function cross_behaviour(doc, node, ext)
    local post, arms = cross_shapes(node, ext, 16)
    for mask = 0, 15 do
        local boxes, cond = {post}, {}
        for i, d in ipairs(DIRS4) do
            local on = (mask >> (i - 1)) & 1 == 1
            table.insert(cond, is(NS .. d, on))
            if on then table.insert(boxes, arms[d]) end
        end
        perm(doc, all(table.unpack(cond)), {["minecraft:collision_box"] = cboxes(boxes), ["minecraft:selection_box"] = cbox(bounds(boxes))})
    end
    -- vanilla fences, walls and panes connect to these (Bedrock's connection rule)
    comps(doc)["minecraft:connection_rule"] = {accepts_connections_from = node == 2 and "only_fences" or "all", enabled_directions = arr({"north", "east", "south", "west"})}
end
visual.fence = function (doc, e, t, ctx)
    local g = geo(ctx, "geometry.gf_families.fence", function ()
        -- Java fence_post and fence_side (north, turned with uvlock for the others)
        local bones = {bone("post", {cube({6, 0, 6, 10, 16, 10})})}
        for _, d in ipairs(DIRS4) do
            local n = ({north = 0, east = 1, south = 2, west = 3})[d]
            table.insert(bones, bone(d, {cube(rot_y({7, 12, 0, 9, 15, 9}, n)), cube(rot_y({7, 6, 0, 9, 9, 9}, n))}))
        end
        return geo_doc("geometry.gf_families.fence", bones)
    end)
    local vis = {post = true}
    for _, d in ipairs(DIRS4) do vis[d] = is(NS .. d, true) end
    geometry(doc, g, vis)
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(tex(t, "*", "texture", "side"), e.params.render_method)}
end
behaviour.fence = function (doc) cross_behaviour(doc, 2, 2) end
visual.pane = function (doc, e, t, ctx)
    local g = geo(ctx, "geometry.gf_families.pane", function ()
        local bones = {bone("post", {cube({7, 0, 7, 9, 16, 9})})}
        for _, d in ipairs(DIRS4) do
            local n = ({north = 0, east = 1, south = 2, west = 3})[d]
            table.insert(bones, bone(d, {cube(rot_y({7, 0, 0, 9, 16, 7}, n))}))
        end
        return geo_doc("geometry.gf_families.pane", bones)
    end)
    local vis = {post = true}
    for _, d in ipairs(DIRS4) do vis[d] = is(NS .. d, true) end
    geometry(doc, g, vis)
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(tex(t, "*", "pane", "side"), e.params.render_method or "blend")}
end
behaviour.pane = function (doc)
    cross_behaviour(doc, 1, 1)
    comps(doc)["minecraft:light_dampening"] = 0
end

-- wall (Java WallBlock: makeShapes(4, 3, 16, 0, 14, 16), collision 24): up, and north / east /
-- south / west none / low / tall, kept by the runtime.
local WALL_LOW = {north = {5, 0, 0, 11, 14, 11}, south = {5, 0, 5, 11, 14, 16}, west = {0, 0, 5, 11, 14, 11}, east = {5, 0, 5, 16, 14, 11}}
visual.wall = function (doc, e, t, ctx)
    local g = geo(ctx, "geometry.gf_families.wall", function ()
        local bones = {bone("post", {cube({4, 0, 4, 12, 16, 12})})}
        for _, d in ipairs(DIRS4) do
            local n = ({north = 0, east = 1, south = 2, west = 3})[d]
            -- Java template_wall_side / template_wall_side_tall
            table.insert(bones, bone(d .. "_low", {cube(rot_y({5, 0, 0, 11, 14, 8}, n))}))
            table.insert(bones, bone(d .. "_tall", {cube(rot_y({5, 0, 0, 11, 16, 8}, n))}))
        end
        return geo_doc("geometry.gf_families.wall", bones)
    end)
    local vis = {post = is(NS .. "up", true)}
    for _, d in ipairs(DIRS4) do
        vis[d .. "_low"] = is(NS .. d, "low")
        vis[d .. "_tall"] = is(NS .. d, "tall")
    end
    geometry(doc, g, vis)
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(tex(t, "*", "wall", "side"), e.params.render_method)}
end
behaviour.wall = function (doc)
    for _, up in ipairs({false, true}) do
        for mask = 0, 80 do
            local boxes, cond = {}, {is(NS .. "up", up)}
            if up then table.insert(boxes, {4, 0, 4, 12, 16, 12}) end
            local m = mask
            for _, d in ipairs(DIRS4) do
                local v = WALL_SIDE[m % 3 + 1]
                m = m // 3
                table.insert(cond, is(NS .. d, v))
                if v ~= "none" then
                    local b = WALL_LOW[d]
                    table.insert(boxes, {b[1], b[2], b[3], b[4], v == "tall" and 16 or 14, b[6]})
                end
            end
            local sel = #boxes > 0 and cbox(bounds(boxes)) or cbox({4, 0, 4, 12, 16, 12})
            perm(doc, all(table.unpack(cond)), {["minecraft:collision_box"] = cboxes(boxes), ["minecraft:selection_box"] = sel})
        end
    end
    comps(doc)["minecraft:connection_rule"] = {accepts_connections_from = "all", enabled_directions = arr({"north", "east", "south", "west"})}
end

-- fence gate (Java FenceGateBlock): facing / open / in_wall / powered. Java's template_fence_gate
-- and its open / wall variants, turned by the facing (blockstate y with uvlock).
local GATE_CLOSED = {{0, 5, 7, 2, 16, 9}, {14, 5, 7, 16, 16, 9}, {6, 6, 7, 8, 15, 9}, {8, 6, 7, 10, 15, 9},
    {2, 6, 7, 6, 9, 9}, {2, 12, 7, 6, 15, 9}, {10, 6, 7, 14, 9, 9}, {10, 12, 7, 14, 15, 9}}
local GATE_OPEN = {{0, 5, 7, 2, 16, 9}, {14, 5, 7, 16, 16, 9}, {0, 6, 13, 2, 15, 15}, {14, 6, 13, 16, 15, 15},
    {0, 6, 9, 2, 9, 13}, {0, 12, 9, 2, 15, 13}, {14, 6, 9, 16, 9, 13}, {14, 12, 9, 16, 15, 13}}
visual.fence_gate = function (doc, e, t, ctx)
    local g = geo(ctx, "geometry.gf_families.fence_gate", function ()
        local bones = {}
        for _, low in ipairs({false, true}) do
            local dy = low and 3 or 0
            for _, f in ipairs(FACING4) do
                for _, open in ipairs({false, true}) do
                    local cubes = {}
                    for _, b in ipairs(open and GATE_OPEN or GATE_CLOSED) do
                        table.insert(cubes, cube(rot_y({b[1], b[2] - dy, b[3], b[4], b[5] - dy, b[6]}, QUARTERS[f])))
                    end
                    table.insert(bones, bone(f .. (open and "_open" or "_closed") .. (low and "_low" or ""), cubes))
                end
            end
        end
        return geo_doc("geometry.gf_families.fence_gate", bones)
    end)
    local vis = {}
    for _, low in ipairs({false, true}) do
        for _, f in ipairs(FACING4) do
            for _, open in ipairs({false, true}) do
                vis[f .. (open and "_open" or "_closed") .. (low and "_low" or "")] = all(is(NS .. "facing", f), is(NS .. "open", open), is(NS .. "in_wall", low))
            end
        end
    end
    geometry(doc, g, vis)
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(tex(t, "*", "texture", "side"), e.params.render_method)}
end
behaviour.fence_gate = function (doc)
    local axis_z = any({is(NS .. "facing", "north"), is(NS .. "facing", "south")})
    local axis_x = any({is(NS .. "facing", "east"), is(NS .. "facing", "west")})
    for _, a in ipairs({{axis_z, {0, 0, 6, 16, 16, 10}}, {axis_x, {6, 0, 0, 10, 16, 16}}}) do
        for _, wall in ipairs({false, true}) do
            local b = a[2]
            local sel = {b[1], b[2], b[3], b[4], wall and 13 or 16, b[6]} -- Java X_SHAPE_LOW / Z_SHAPE_LOW
            for _, open in ipairs({false, true}) do
                perm(doc, all(a[1], is(NS .. "in_wall", wall), is(NS .. "open", open)),
                    {["minecraft:collision_box"] = (not open) and cbox(b) or false, ["minecraft:selection_box"] = cbox(sel)})
            end
        end
    end
end

-- door (Java DoorBlock): facing / half / hinge / open / powered; the runtime places the upper half
-- and keeps both together.
local DOOR_AABB = {SOUTH = {0, 0, 0, 16, 16, 3}, NORTH = {0, 0, 13, 16, 16, 16}, WEST = {13, 0, 0, 16, 16, 16}, EAST = {0, 0, 0, 3, 16, 16}}
local function door_box(facing, open, right) -- DoorBlock.getShape
    if facing == "east" then return open and (right and "NORTH" or "SOUTH") or "EAST" end
    if facing == "south" then return open and (right and "EAST" or "WEST") or "SOUTH" end
    if facing == "west" then return open and (right and "SOUTH" or "NORTH") or "WEST" end
    return open and (right and "WEST" or "EAST") or "NORTH"
end
visual.door = function (doc, e, t, ctx)
    local g = geo(ctx, "geometry.gf_families.door", function ()
        local bones = {}
        for name, b in pairs(DOOR_AABB) do
            table.insert(bones, bone(name:lower(), {cube(b)}))
            -- the other hinge shows the texture mirrored (Java door_*_right / *_open models)
            local faces = {}
            for _, f in ipairs(FACES6) do
                local r = face_rect(b, f)
                faces[f] = {rect = {r[3], r[2], r[1], r[4]}}
            end
            table.insert(bones, bone(name:lower() .. "_mirrored", {cube(b, {faces = faces})}))
        end
        return geo_doc("geometry.gf_families.door", bones)
    end)
    local by_box = {}
    for _, f in ipairs(FACING4) do
        for _, open in ipairs({false, true}) do
            for _, right in ipairs({false, true}) do
                local key = door_box(f, open, right):lower() .. ((right ~= open) and "_mirrored" or "")
                by_box[key] = by_box[key] or {}
                table.insert(by_box[key], all(is(NS .. "facing", f), is(NS .. "open", open), is(NS .. "hinge", right and "right" or "left")))
            end
        end
    end
    local vis = {}
    for name, _ in pairs(DOOR_AABB) do vis[name:lower()] = false; vis[name:lower() .. "_mirrored"] = false end
    for key, conds in pairs(by_box) do vis[key] = any(conds) end
    geometry(doc, g, vis)
    local m = e.params.render_method or "alpha_test"
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(tex(t, "bottom", "lower", "*"), m)}
    -- the upper half's texture: a separate permutation (Bedrock applies every matching one)
    perm(doc, is(NS .. "half", "upper"), {["minecraft:material_instances"] = {["*"] = mat(tex(t, "top", "upper", "*"), m)}})
end
behaviour.door = function (doc)
    -- Java's door loot drops only for half=lower (block_state_property): the upper half has none,
    -- the lower half drops the door when it goes with the upper one (the runtime's updateShape)
    perm(doc, is(NS .. "half", "upper"), {["minecraft:loot"] = "loot_tables/empty.json"})
    for _, f in ipairs(FACING4) do
        for _, open in ipairs({false, true}) do
            for _, right in ipairs({false, true}) do
                local b = DOOR_AABB[door_box(f, open, right)]
                perm(doc, all(is(NS .. "facing", f), is(NS .. "open", open), is(NS .. "hinge", right and "right" or "left")),
                    {["minecraft:collision_box"] = cbox(b), ["minecraft:selection_box"] = cbox(b)})
            end
        end
    end
end

-- trapdoor (Java TrapDoorBlock): facing / half / open / powered.
local TRAP = {east = {0, 0, 0, 3, 16, 16}, west = {13, 0, 0, 16, 16, 16}, south = {0, 0, 0, 16, 16, 3}, north = {0, 0, 13, 16, 16, 16}, bottom = {0, 0, 0, 16, 3, 16}, top = {0, 13, 0, 16, 16, 16}}
visual.trapdoor = function (doc, e, t, ctx)
    local g = geo(ctx, "geometry.gf_families.trapdoor", function ()
        local bones = {}
        for name, b in pairs(TRAP) do table.insert(bones, bone(name, {cube(b)})) end
        return geo_doc("geometry.gf_families.trapdoor", bones)
    end)
    local vis = {bottom = all(is(NS .. "open", false), is(NS .. "half", "bottom")), top = all(is(NS .. "open", false), is(NS .. "half", "top"))}
    for _, d in ipairs(DIRS4) do vis[d] = all(is(NS .. "open", true), is(NS .. "facing", d)) end
    geometry(doc, g, vis)
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(tex(t, "*", "texture", "side"), e.params.render_method or "alpha_test")}
end
behaviour.trapdoor = function (doc)
    for _, half in ipairs({"bottom", "top"}) do
        perm(doc, all(is(NS .. "open", false), is(NS .. "half", half)), {["minecraft:collision_box"] = cbox(TRAP[half]), ["minecraft:selection_box"] = cbox(TRAP[half])})
    end
    for _, d in ipairs(DIRS4) do
        perm(doc, all(is(NS .. "open", true), is(NS .. "facing", d)), {["minecraft:collision_box"] = cbox(TRAP[d]), ["minecraft:selection_box"] = cbox(TRAP[d])})
    end
end

-- button (Java ButtonBlock): face wall / floor / ceiling, facing, powered. Pressed buttons power
-- vanilla redstone through minecraft:redstone_producer (15, strongly the block they hang on).
local function button_box(face, facing, pressed)
    local d = pressed and 1 or 2
    local x = facing == "east" or facing == "west"
    if face == "floor" then return x and {6, 0, 5, 10, d, 11} or {5, 0, 6, 11, d, 10} end
    if face == "ceiling" then return x and {6, 16 - d, 5, 10, 16, 11} or {5, 16 - d, 6, 11, 16, 10} end
    return ({north = {5, 6, 16 - d, 11, 10, 16}, south = {5, 6, 0, 11, 10, d}, west = {16 - d, 6, 5, 16, 10, 11}, east = {0, 6, 5, d, 10, 11}})[facing]
end
local function button_variants()
    local out = {}
    for _, face in ipairs({"floor", "ceiling", "wall"}) do
        for _, f in ipairs(FACING4) do
            for _, pressed in ipairs({false, true}) do table.insert(out, {face = face, facing = f, pressed = pressed}) end
        end
    end
    return out
end
visual.button = function (doc, e, t, ctx)
    local g = geo(ctx, "geometry.gf_families.button", function ()
        local bones = {}
        for _, v in ipairs(button_variants()) do
            table.insert(bones, bone(v.face .. "_" .. v.facing .. (v.pressed and "_pressed" or ""), {cube(button_box(v.face, v.facing, v.pressed))}))
        end
        return geo_doc("geometry.gf_families.button", bones)
    end)
    local vis = {}
    for _, v in ipairs(button_variants()) do
        vis[v.face .. "_" .. v.facing .. (v.pressed and "_pressed" or "")] = all(is(NS .. "face", v.face), is(NS .. "facing", v.facing), is(NS .. "powered", v.pressed))
    end
    geometry(doc, g, vis)
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(tex(t, "*", "texture", "side"), e.params.render_method)}
end
behaviour.button = function (doc)
    comps(doc)["minecraft:collision_box"] = false
    comps(doc)["minecraft:redstone_conductivity"] = {redstone_conductor = false, allows_wire_to_step_down = true}
    -- every permutation is a producer (0 unpressed) so the circuit follows the press and release
    comps(doc)["minecraft:redstone_producer"] = {power = 0}
    for _, v in ipairs(button_variants()) do
        local c = {["minecraft:selection_box"] = cbox(button_box(v.face, v.facing, v.pressed)), ["minecraft:collision_box"] = false}
        if v.pressed then
            -- ButtonBlock.getDirectSignal: the block it hangs on (getConnectedDirection)
            local attached = v.face == "floor" and "down" or (v.face == "ceiling" and "up" or OPP[v.facing])
            c["minecraft:redstone_producer"] = {power = 15, strongly_powered_face = attached}
        end
        perm(doc, all(is(NS .. "face", v.face), is(NS .. "facing", v.facing), is(NS .. "powered", v.pressed)), c)
    end
end

-- pressure plate (Java PressurePlateBlock): powered; 15 while pressed, strongly into the block below.
visual.pressure_plate = function (doc, e, t, ctx)
    local g = geo(ctx, "geometry.gf_families.pressure_plate", function ()
        return geo_doc("geometry.gf_families.pressure_plate", {bone("up", {cube({1, 0, 1, 15, 1, 15})}), bone("down", {cube({1, 0, 1, 15, 0.5, 15})})})
    end)
    geometry(doc, g, {up = is(NS .. "powered", false), down = is(NS .. "powered", true)})
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(tex(t, "*", "texture", "side"), e.params.render_method)}
end
behaviour.pressure_plate = function (doc)
    comps(doc)["minecraft:collision_box"] = false
    comps(doc)["minecraft:redstone_conductivity"] = {redstone_conductor = false, allows_wire_to_step_down = true}
    comps(doc)["minecraft:redstone_producer"] = {power = 0}
    perm(doc, is(NS .. "powered", false), {["minecraft:selection_box"] = cbox({1, 0, 1, 15, 1, 15})})
    perm(doc, is(NS .. "powered", true), {["minecraft:selection_box"] = cbox({1, 0, 1, 15, 0.5, 15}),
        ["minecraft:redstone_producer"] = {power = 15, strongly_powered_face = "down"}})
end

-- pillar / log / wood (Java RotatedPillarBlock): axis (Java cube_column, turned x = 90 / z).
local function pillar_visual(doc, e, t, ends_too)
    full_block(doc, {["*"] = tex(t, "side", "*"), up = ends_too and tex(t, "side", "*") or tex(t, "end", "up", "top", "*"),
        down = ends_too and tex(t, "side", "*") or tex(t, "end", "down", "bottom", "up", "*")}, e.params.render_method)
    perm(doc, is(NS .. "axis", "z"), {["minecraft:transformation"] = {rotation = {90, 0, 0}}})
    perm(doc, is(NS .. "axis", "x"), {["minecraft:transformation"] = {rotation = {0, 0, 90}}})
end
visual.pillar = function (doc, e, t) pillar_visual(doc, e, t, false) end
visual.log = visual.pillar
visual.wood = function (doc, e, t) pillar_visual(doc, e, t, true) end

-- leaves (Java LeavesBlock): distance 1..7 (0: not computed yet) and persistent.
visual.leaves = function (doc, e, t) full_block(doc, t, e.params.render_method or "alpha_test") end
-- The image of the terrain texture `name` (nil when unknown). F1 maps Java's vanilla leaves to
-- Bedrock's "_carried" images, which are coloured already (the inventory's); Java tints the grey
-- leaves, so a tinted leaf takes Bedrock's grey image of the same leaves.
local function terrain_image(ctx, name)
    local entry = ctx.rp_terrain and ctx.rp_terrain[name]
    local t = type(entry) == "table" and entry.textures or nil
    if type(t) == "table" then t = t.path or t[1] end
    if type(t) == "table" then t = t.path end
    if type(t) ~= "string" then return nil end
    return (t:gsub("^(textures/blocks/.+)_carried$", "%1"))
end

-- The terrain texture `name` tinted with `color` ("#rrggbb"): a terrain_texture.json entry of the
-- grey image with tint_color (the game tints it in the atlas); `name` when its image is unknown.
local function tinted_texture(ctx, name, color)
    local key = name .. "_gf_" .. color:gsub("#", "")
    if ctx.terrain[name] and type(ctx.terrain[name]) == "table" and ctx.terrain[name].tint_color then return name end -- tinted already
    local image = terrain_image(ctx, name)
    if not image then
        table.insert(ctx.missing, "terrain texture " .. name .. " (to tint " .. color .. ")")
        return name
    end
    ctx.terrain[key] = {path = image, tint_color = color}
    return key
end

-- The terrain texture `name` for a biome tint method: its grey image (a "_carried" image would be
-- tinted twice).
local function grey_texture(ctx, name)
    local entry = ctx.rp_terrain and ctx.rp_terrain[name]
    local image = terrain_image(ctx, name)
    local t = type(entry) == "table" and entry.textures or nil
    if not image or image == t or (type(t) == "table" and (t.path == image or t[1] == image)) then return name end
    local key = name .. "_gf_grey"
    ctx.terrain[key] = image
    return key
end
behaviour.leaves = function (doc, e, ctx)
    comps(doc)["minecraft:light_dampening"] = 1
    local color = e.params.tint_color or LEAF_COLORS[e.id]
    local tint = e.params.tint or LEAF_TINTS[e.id]
    if color then
        local function pretint(mi)
            if type(mi) ~= "table" then return end
            for _, m in pairs(mi) do
                if type(m) == "table" and type(m.texture) == "string" then
                    m.texture = tinted_texture(ctx, m.texture, color)
                    m.tint_method = nil
                end
            end
        end
        pretint(comps(doc)["minecraft:material_instances"])
        for _, p in ipairs(block_of(doc).permutations or {}) do pretint(p.components["minecraft:material_instances"]) end
    elseif tint then
        local function tinted(mi)
            if type(mi) ~= "table" then return end
            for _, m in pairs(mi) do
                if type(m) == "table" then
                    m.tint_method = tint
                    if type(m.texture) == "string" then m.texture = grey_texture(ctx, m.texture) end
                end
            end
        end
        tinted(comps(doc)["minecraft:material_instances"])
        for _, p in ipairs(block_of(doc).permutations or {}) do tinted(p.components["minecraft:material_instances"]) end
    end
end

-- cross plants (sapling, TF bushes): Java's block/cross model (two planes, rescaled at 45 degrees).
local function cross_cubes(x0, x1, y0, y1, material)
    local rot = {axis = "y", angle = 45, origin = {8, 8, 8}, rescale = true}
    local faces1 = {north = {rect = {0, 16 - (y1 - y0), 16, 16}, mat = material}, south = {rect = {0, 16 - (y1 - y0), 16, 16}, mat = material}}
    local faces2 = {west = {rect = {0, 16 - (y1 - y0), 16, 16}, mat = material}, east = {rect = {0, 16 - (y1 - y0), 16, 16}, mat = material}}
    return {cube({x0, y0, 8, x1, y1, 8}, {faces = faces1, faces_only = true, rot = rot}), cube({8, y0, x0, 8, y1, x1}, {faces = faces2, faces_only = true, rot = rot})}
end
visual.cross = function (doc, e, t, ctx)
    local g = geo(ctx, "geometry.gf_families.cross", function ()
        return geo_doc("geometry.gf_families.cross", {bone("cross", cross_cubes(0.8, 15.2, 0, 16, nil))})
    end)
    geometry(doc, g)
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(tex(t, "*", "cross"), "alpha_test", {ambient_occlusion = false, face_dimming = false})}
end
behaviour.cross = function (doc, e)
    comps(doc)["minecraft:collision_box"] = false
    comps(doc)["minecraft:selection_box"] = cbox(e.params.shape or {2, 0, 2, 14, 13, 14})
    comps(doc)["minecraft:light_dampening"] = 0
end
visual.sapling = visual.cross
behaviour.sapling = function (doc)
    comps(doc)["minecraft:collision_box"] = false
    comps(doc)["minecraft:selection_box"] = cbox({2, 0, 2, 14, 12, 14}) -- SaplingBlock.SHAPE
    comps(doc)["minecraft:light_dampening"] = 0
end

-- potted plant (Java FlowerPotBlock / TF SpecialFlowerPotBlock): block/flower_pot_cross.
visual.potted = function (doc, e, t, ctx)
    local g = geo(ctx, "geometry.gf_families.potted", function ()
        local pot = {
            cube({5, 0, 5, 6, 6, 11}, {mat = "pot"}), cube({10, 0, 5, 11, 6, 11}, {mat = "pot"}),
            cube({6, 0, 5, 10, 6, 6}, {mat = "pot"}), cube({6, 0, 10, 10, 6, 11}, {mat = "pot"}),
            cube({6, 0, 6, 10, 4, 10}, {faces = {up = {rect = {6, 6, 10, 10}, mat = "soil"}, down = {rect = {6, 12, 10, 16}, mat = "pot"}}, faces_only = true}),
        }
        return geo_doc("geometry.gf_families.potted", {bone("pot", pot), bone("plant", cross_cubes(2.6, 13.4, 4, 16, "plant"))})
    end)
    geometry(doc, g)
    comps(doc)["minecraft:material_instances"] = {
        -- (one render method per block: Bedrock refuses opaque and transparent instances together)
        ["*"] = mat(tex(t, "pot", "flowerpot"), "alpha_test"), pot = mat(tex(t, "pot", "flowerpot"), "alpha_test"),
        soil = mat(tex(t, "soil", "dirt"), "alpha_test"), plant = mat(tex(t, "plant", "*"), "alpha_test", {ambient_occlusion = false, face_dimming = false}),
    }
end
behaviour.potted = function (doc)
    comps(doc)["minecraft:collision_box"] = cbox({5, 0, 5, 11, 6, 11}) -- FlowerPotBlock.SHAPE
    comps(doc)["minecraft:selection_box"] = cbox({5, 0, 5, 11, 6, 11})
    comps(doc)["minecraft:light_dampening"] = 0
end

-- signs (Java StandingSignBlock / WallSignBlock / CeilingHangingSignBlock / WallHangingSignBlock),
-- drawn by SignRenderer / HangingSignRenderer: their ModelPart models (64 x 32 entity textures)
-- through the renderers' transforms. Standing and hanging signs turn in 16 steps (rotation):
-- Bedrock turns blocks by quarter turns, so four model variants turned 0 / 22.5 / 45 / 67.5
-- degrees clockwise, and the quarter turns as a transformation. The text lives in block data
-- (gf::signs) and shows through the display entity.
local SIGN_SCALE = 2 / 3
local SIGN_PARTS = {
    board = {box = {-12, -14, -1, 24, 12, 2}, uv = {0, 0}},
    stick = {box = {-1, -2, -1, 2, 14, 2}, uv = {0, 14}},
}
local HANGING_PARTS = {
    board = {{box = {-7, 0, -1, 14, 10, 2}, uv = {0, 12}}},
    plank = {{box = {-8, -6, -2, 16, 2, 4}, uv = {0, 0}}},
    chains = {
        {box = {-1.5, 0, 0, 3, 6, 0}, uv = {0, 6}, offset = {-5, -6, 0}, rot = {"y", -45}},
        {box = {-1.5, 0, 0, 3, 6, 0}, uv = {6, 6}, offset = {-5, -6, 0}, rot = {"y", 45}},
        {box = {-1.5, 0, 0, 3, 6, 0}, uv = {0, 6}, offset = {5, -6, 0}, rot = {"y", -45}},
        {box = {-1.5, 0, 0, 3, 6, 0}, uv = {6, 6}, offset = {5, -6, 0}, rot = {"y", 45}},
    },
    vchains = {{box = {-6, -6, 0, 12, 6, 0}, uv = {14, 6}}},
}
-- SignRenderer.translateSign (standing: y 0.75 * scale) and renderSign's scale(f, -f, -f)
local function sign_static(wall)
    local f = SIGN_SCALE
    local m = mtrans(8, 12 * f, 8)
    if wall then m = mmul(m, mtrans(0, -5, -7)) end -- (0, -0.3125, -0.4375) blocks
    return mmul(m, mscale(f, -f, -f))
end
-- HangingSignRenderer.translateSign (0.5, 0.9375, 0.5) then (0, -0.3125, 0), scale(1, -1, -1)
local HANGING_STATIC = mmul(mtrans(8, 10, 8), mscale(1, -1, -1))

local function variant_bones(name, cubes)
    -- one bone per 22.5 degree step (clockwise in the world: Bedrock's y rotation of the
    -- mirrored geometry, about the block centre)
    local out = {}
    for k = 0, 3 do table.insert(out, bone(name .. "_r" .. k, copy(cubes), {pivot = {0, 0, 0}, rotation = {0, k * 22.5, 0}})) end
    return out
end
-- the visibility of each step's bones (bones not listed are visible: every one is listed)
local function step_visibility(parts, step, cond_of)
    local v = {}
    for _, part in ipairs(parts) do
        for k = 0, 3 do v[part .. "_r" .. k] = k == step and (cond_of and cond_of(part) or true) or false end
    end
    return v
end
-- per rotation r: the step's bones and the quarter turns
local function rotation_perms(doc, g, parts, cond_of)
    for r = 0, 15 do
        local quarter, step = r // 4, r % 4
        perm(doc, is(NS .. "rotation", r), {["minecraft:geometry"] = {identifier = g, bone_visibility = step_visibility(parts, step, cond_of)},
            ["minecraft:transformation"] = {rotation = {0, -90 * quarter, 0}}})
    end
end
local function facing_perms(doc)
    for _, d in ipairs(FACING4) do
        perm(doc, is(NS .. "facing", d), {["minecraft:transformation"] = {rotation = {0, TURN[d], 0}}})
    end
end

visual.sign = function (doc, e, t, ctx)
    local g = geo(ctx, "geometry.gf_families.sign", function ()
        local bones = {}
        for _, part in ipairs({"board", "stick"}) do
            for _, b in ipairs(variant_bones(part, part_cubes({SIGN_PARTS[part]}, sign_static(false)))) do table.insert(bones, b) end
        end
        return geo_doc("geometry.gf_families.sign", bones, 64, 32)
    end)
    geometry(doc, g, step_visibility({"board", "stick"}, 0))
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(t.entity or tex(t, "*"), "alpha_test")}
    rotation_perms(doc, g, {"board", "stick"})
end
behaviour.sign = function (doc)
    comps(doc)["minecraft:collision_box"] = false
    comps(doc)["minecraft:selection_box"] = cbox({4, 0, 4, 12, 16, 12}) -- SignBlock.SHAPE
end
visual.wall_sign = function (doc, e, t, ctx)
    local g = geo(ctx, "geometry.gf_families.wall_sign", function ()
        return geo_doc("geometry.gf_families.wall_sign", {bone("board", part_cubes({SIGN_PARTS.board}, sign_static(true)))}, 64, 32)
    end)
    geometry(doc, g)
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(t.entity or tex(t, "*"), "alpha_test")}
    facing_perms(doc)
end
-- WallSignBlock.AABBS
local WALL_SIGN_AABB = {north = {0, 4.5, 14, 16, 12.5, 16}, south = {0, 4.5, 0, 16, 12.5, 2}, east = {0, 4.5, 0, 2, 12.5, 16}, west = {14, 4.5, 0, 16, 12.5, 16}}
behaviour.wall_sign = function (doc)
    comps(doc)["minecraft:collision_box"] = false
    for _, d in ipairs(FACING4) do perm(doc, is(NS .. "facing", d), {["minecraft:selection_box"] = cbox(unturn(WALL_SIGN_AABB[d], QUARTERS[d]))}) end
end
visual.hanging_sign = function (doc, e, t, ctx)
    local g = geo(ctx, "geometry.gf_families.hanging_sign", function ()
        local bones = {}
        for _, part in ipairs({"board", "chains", "vchains"}) do
            for _, b in ipairs(variant_bones(part, part_cubes(HANGING_PARTS[part], HANGING_STATIC))) do table.insert(bones, b) end
        end
        return geo_doc("geometry.gf_families.hanging_sign", bones, 64, 32)
    end)
    -- HangingSignModel.evaluateVisibleParts: attached ceiling signs hang on V chains
    local function chains(part)
        if part == "chains" then return is(NS .. "attached", false) end
        if part == "vchains" then return is(NS .. "attached", true) end
        return true
    end
    geometry(doc, g, step_visibility({"board", "chains", "vchains"}, 0, chains))
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(t.entity or tex(t, "*"), "alpha_test")}
    rotation_perms(doc, g, {"board", "chains", "vchains"}, chains)
end
behaviour.hanging_sign = function (doc)
    comps(doc)["minecraft:collision_box"] = false
    comps(doc)["minecraft:selection_box"] = cbox({3, 0, 3, 13, 16, 13}) -- CeilingHangingSignBlock.SHAPE
    -- CeilingHangingSignBlock.AABBS for the four straight rotations
    -- (rotation_perms turns rotation r by r // 4 quarters)
    for r, b in pairs({[0] = {1, 0, 7, 15, 10, 9}, [4] = {7, 0, 1, 9, 10, 15}, [8] = {1, 0, 7, 15, 10, 9}, [12] = {7, 0, 1, 9, 10, 15}}) do
        perm(doc, is(NS .. "rotation", r), {["minecraft:selection_box"] = cbox(unturn(b, r // 4))})
    end
end
visual.wall_hanging_sign = function (doc, e, t, ctx)
    local g = geo(ctx, "geometry.gf_families.wall_hanging_sign", function ()
        local cubes = part_cubes(HANGING_PARTS.board, HANGING_STATIC)
        for _, c in ipairs(part_cubes(HANGING_PARTS.plank, HANGING_STATIC)) do table.insert(cubes, c) end
        for _, c in ipairs(part_cubes(HANGING_PARTS.chains, HANGING_STATIC)) do table.insert(cubes, c) end
        return geo_doc("geometry.gf_families.wall_hanging_sign", {bone("sign", cubes)}, 64, 32)
    end)
    geometry(doc, g)
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(t.entity or tex(t, "*"), "alpha_test")}
    facing_perms(doc)
end
behaviour.wall_hanging_sign = function (doc)
    -- WallHangingSignBlock: the plank collides, the plank and board are selected
    for _, d in ipairs(FACING4) do
        local ns = d == "north" or d == "south"
        local plank = ns and {0, 14, 6, 16, 16, 10} or {6, 14, 0, 10, 16, 16}
        local board = ns and {1, 0, 7, 15, 10, 9} or {7, 0, 1, 9, 10, 15}
        local q = QUARTERS[d]
        perm(doc, is(NS .. "facing", d), {["minecraft:collision_box"] = cbox(unturn(plank, q)), ["minecraft:selection_box"] = cbox(unturn(bounds({plank, board}), q))})
    end
end

-- chest / trapped chest (Java ChestBlock, ChestRenderer's single / left / right models, 64 x 64):
-- facing / type, the lid (gloamingforest:lid 0..4: the lid's angle in 22.5 degree steps, which the
-- runtime moves as ChestLidController's openness, src/feature/families/Chests.cpp) and, for trapped
-- chests, open_count (the players inside: their signal). params.layout "legacy": the pre-1.15
-- texture layout (Bedrock's own chest textures, used by the fixtures).
local CHEST_PARTS = {
    single = {bottom = {1, 0, 1, 14, 10, 14}, lid = {1, 0, 0, 14, 5, 14}, lock = {7, -2, 14, 2, 4, 1}},
    right = {bottom = {1, 0, 1, 15, 10, 14}, lid = {1, 0, 0, 15, 5, 14}, lock = {15, -2, 14, 1, 4, 1}},
    left = {bottom = {0, 0, 1, 15, 10, 14}, lid = {0, 0, 0, 15, 5, 14}, lock = {0, -2, 14, 1, 4, 1}},
}
-- the lid's positions: Bedrock turns the bones of block geometry in 22.5 degree steps
local CHEST_LID_STEPS = 4
local function chest_part_list(kind, angle, legacy)
    local p = CHEST_PARTS[kind]
    -- ChestRenderer: lid.xRot = -(openness * PI / 2) (degrees here)
    local rot = angle ~= 0 and {"x", -angle} or nil
    local parts = {
        {box = p.bottom, uv = {0, 19}},
        {box = p.lid, uv = {0, 0}, offset = {0, 9, 1}, rot = rot},
        {box = p.lock, uv = {0, 0}, offset = {0, 9, 1}, rot = rot},
    }
    if legacy then
        -- the pre-1.15 renderer drew the same boxes through scale(1, -1, -1) (y down): mirror the
        -- parts into that frame, the static map turns them back
        for _, part in ipairs(parts) do
            local o = part.offset or {0, 0, 0}
            local b = part.box
            part.offset = {o[1], 16 - o[2], 16 - o[3]}
            part.box = {b[1], -b[2] - b[5], -b[3] - b[6], b[4], b[5], b[6]}
        end
    end
    return parts
end
local function chest_visual(doc, e, t, ctx)
    local legacy = e.params.layout == "legacy"
    local g = "geometry.gf_families.chest" .. (legacy and "_legacy" or "")
    geo(ctx, g, function ()
        local static = legacy and mmul(mtrans(0, 16, 16), mscale(1, -1, -1)) or mat3(1, 0, 0, 0, 1, 0, 0, 0, 1)
        local bones = {}
        for _, kind in ipairs({"single", "left", "right"}) do
            local closed = part_cubes(chest_part_list(kind, 0, legacy), static)
            table.insert(bones, bone(kind .. "_bottom", {closed[1]}))
            for k = 0, CHEST_LID_STEPS do
                local lid = part_cubes(chest_part_list(kind, k * 90 / CHEST_LID_STEPS, legacy), static)
                table.insert(bones, bone(kind .. "_lid_" .. k, {lid[2], lid[3]}))
            end
        end
        return geo_doc(g, bones, 64, 64)
    end)
    local vis = {}
    for _, kind in ipairs({"single", "left", "right"}) do
        vis[kind .. "_bottom"] = is(NS .. "type", kind)
        for k = 0, CHEST_LID_STEPS do vis[kind .. "_lid_" .. k] = all(is(NS .. "type", kind), is(KIT .. "lid", k)) end
    end
    geometry(doc, g, vis)
    local m = e.params.render_method or "alpha_test"
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(t.single or tex(t, "*"), m)}
    perm(doc, is(NS .. "type", "left"), {["minecraft:material_instances"] = {["*"] = mat(t.left or t.single or tex(t, "*"), m)}})
    perm(doc, is(NS .. "type", "right"), {["minecraft:material_instances"] = {["*"] = mat(t.right or t.single or tex(t, "*"), m)}})
    facing_perms(doc)
end
visual.chest = chest_visual
visual.trapped_chest = chest_visual
-- ChestBlock.getShape: single AABB, a double chest reaches its partner
local CHEST_AABB = {single = {1, 0, 1, 15, 14, 15}, north = {1, 0, 0, 15, 14, 15}, south = {1, 0, 1, 15, 14, 16}, west = {0, 0, 1, 15, 14, 15}, east = {1, 0, 1, 16, 14, 15}}
local function chest_behaviour(doc, e)
    -- ChestBlockEntity: the slots are a game container in the block's data-driven block actor (the
    -- inventory contract's native backend, F10 / spike S3), saved with the chunk, seen by hoppers,
    -- hopper minecarts and droppers (src/feature/families/Hoppers.cpp). minecraft:block_entity needs
    -- the v1_26_20 block schema, where the block's tags are the minecraft:tags array (spike S7;
    -- write_tags writes them in that form)
    doc.format_version = BLOCK_ENTITY_FORMAT
    comps(doc)["minecraft:block_entity"] = {}
    for _, f in ipairs(FACING4) do
        for _, kind in ipairs({"single", "left", "right"}) do
            local b = CHEST_AABB.single
            if kind ~= "single" then
                -- ChestBlock.getConnectedDirection: LEFT -> clockwise, RIGHT -> counter-clockwise
                local cw = ({north = "east", east = "south", south = "west", west = "north"})[f]
                b = CHEST_AABB[kind == "left" and cw or CCW[f]]
            end
            b = unturn(b, QUARTERS[f]) -- facing_perms turns the chest
            perm(doc, all(is(NS .. "facing", f), is(NS .. "type", kind)), {["minecraft:collision_box"] = cbox(b), ["minecraft:selection_box"] = cbox(b)})
        end
    end
end
behaviour.chest = chest_behaviour
behaviour.trapped_chest = function (doc, e)
    chest_behaviour(doc, e)
    -- TrappedChestBlock.getSignal: the players inside (open_count 0..15, the runtime's count of the
    -- inventory contract's openers), strongly into the block below
    comps(doc)["minecraft:redstone_conductivity"] = {redstone_conductor = false}
    comps(doc)["minecraft:redstone_producer"] = {power = 0}
    for n = 1, 15 do perm(doc, is(KIT .. "open_count", n), {["minecraft:redstone_producer"] = {power = n, strongly_powered_face = "down"}}) end
end

-- horizontal (TF TFHorizontalBlock, HorizontalDirectionalBlock): facing; the front (params
-- textures.front) faces the facing. binary rotated (TF BinaryRotatedBlock): rotated turns the
-- model a quarter. directional (Java DirectionalBlock): facing of six; the front is up.
visual.horizontal = function (doc, e, t)
    full_block(doc, {["*"] = tex(t, "side", "*"), up = tex(t, "up", "top", "*"), down = tex(t, "down", "bottom", "up", "*"),
        south = tex(t, "front", "south", "*"), north = tex(t, "back", "north", "side", "*"), east = tex(t, "side", "*"), west = tex(t, "side", "*")}, e.params.render_method)
    facing_perms(doc)
end
visual.binary_rotated = function (doc, e, t)
    full_block(doc, t, e.params.render_method)
    perm(doc, is(NS .. "rotated", true), {["minecraft:transformation"] = {rotation = {0, 90, 0}}})
end
visual.directional = function (doc, e, t)
    full_block(doc, {["*"] = tex(t, "side", "*"), up = tex(t, "front", "up", "*"), down = tex(t, "back", "down", "side", "*"),
        north = tex(t, "side", "*"), south = tex(t, "side", "*"), east = tex(t, "side", "*"), west = tex(t, "side", "*")}, e.params.render_method)
    local rots = {down = {180, 0, 0}, north = {-90, 0, 0}, south = {90, 0, 0}, west = {0, 0, 90}, east = {0, 0, -90}}
    for f, r in pairs(rots) do perm(doc, is(NS .. "facing", f), {["minecraft:transformation"] = {rotation = r}}) end
end

-- connectable pillar (TF ConnectableRotatedPillarBlock, e.g. thorns: one box per state from
-- getShape) and wall pillar (TF WallPillarBlock: the 12 px core, 2 px arms, 3 px flats on the axis
-- ends). axis and six connections, kept by the runtime.
-- ConnectableRotatedPillarBlock's width: params.width, TF's ThornsBlock(properties, 10)
local function pillar_width(e)
    if e.params.width then return e.params.width end
    local c = e.params.class
    if c == "ThornsBlock" or c == "BurntThornsBlock" then return 10 end
    return 16
end
local function pillar_bounds(width)
    if width >= 16 then return 0, 16 end
    local lo = 8 - width / 2
    return lo, 16 - lo
end
local function connectable_shape(axis, c, lo, hi)
    -- ConnectableRotatedPillarBlock.getShape (TF's mapping of the connections, as written)
    if axis == "x" then
        return {0, c.north and 0 or lo, c.west and 0 or lo, 16, c.south and 16 or hi, c.east and 16 or hi}
    elseif axis == "z" then
        return {c.east and 0 or lo, c.south and 0 or lo, 0, c.west and 16 or hi, c.north and 16 or hi, 16}
    end
    return {c.west and 0 or lo, 0, c.north and 0 or lo, c.east and 16 or hi, 16, c.south and 16 or hi}
end
local WALL_PILLAR = {
    base = {2, 2, 2, 14, 14, 14},
    arm = {west = {0, 2, 2, 2, 14, 14}, east = {14, 2, 2, 16, 14, 14}, down = {2, 0, 2, 14, 2, 14}, up = {2, 14, 2, 14, 16, 14}, north = {2, 2, 0, 14, 14, 2}, south = {2, 2, 14, 14, 14, 16}},
    flat = {west = {0, 0, 0, 3, 16, 16}, east = {13, 0, 0, 16, 16, 16}, down = {0, 0, 0, 16, 3, 16}, up = {0, 13, 0, 16, 16, 16}, north = {0, 0, 0, 16, 16, 3}, south = {0, 0, 13, 16, 16, 16}},
}
local AXIS_OF = {up = "y", down = "y", north = "z", south = "z", west = "x", east = "x"}
local function wall_pillar_boxes(axis, c)
    -- WallPillarBlock.getShape
    local boxes = {WALL_PILLAR.base}
    for _, d in ipairs({"down", "up", "north", "south", "west", "east"}) do
        if c[d] then table.insert(boxes, WALL_PILLAR.arm[d]) elseif AXIS_OF[d] == axis then table.insert(boxes, WALL_PILLAR.flat[d]) end
    end
    return boxes
end
local function pillar_conn_visual(doc, e, t, ctx, kind)
    local width = pillar_width(e)
    local lo, hi = pillar_bounds(width)
    local g = "geometry.gf_families." .. kind .. (kind == "connectable_pillar" and ("_" .. tostring(width):gsub("%.", "_")) or "")
    geo(ctx, g, function ()
        local bones = {}
        if kind == "wall_pillar" then
            table.insert(bones, bone("core", {cube(WALL_PILLAR.base)}))
            for d, b in pairs(WALL_PILLAR.arm) do table.insert(bones, bone(d, {cube(b)})) end
            for d, b in pairs(WALL_PILLAR.flat) do table.insert(bones, bone(d .. "_flat", {cube(b)})) end
        else
            table.insert(bones, bone("axis_y", {cube({lo, 0, lo, hi, 16, hi})}))
            table.insert(bones, bone("axis_z", {cube({lo, lo, 0, hi, hi, 16})}))
            table.insert(bones, bone("axis_x", {cube({0, lo, lo, 16, hi, hi})}))
            local stubs = {down = {lo, 0, lo, hi, lo, hi}, up = {lo, hi, lo, hi, 16, hi}, north = {lo, lo, 0, hi, hi, lo},
                south = {lo, lo, hi, hi, hi, 16}, west = {0, lo, lo, lo, hi, hi}, east = {hi, lo, lo, 16, hi, hi}}
            for d, b in pairs(stubs) do table.insert(bones, bone(d, {cube(b)})) end
        end
        return geo_doc(g, bones)
    end)
    local vis = {}
    if kind == "wall_pillar" then
        vis.core = true
        for _, d in ipairs(FACES6) do
            vis[d] = is(NS .. d, true)
            vis[d .. "_flat"] = all(is(NS .. d, false), is(NS .. "axis", AXIS_OF[d]))
        end
    else
        for _, a in ipairs(AXES) do vis["axis_" .. a] = is(NS .. "axis", a) end
        for _, d in ipairs(FACES6) do vis[d] = all(is(NS .. d, true), "!(" .. is(NS .. "axis", AXIS_OF[d]) .. ")") end
    end
    geometry(doc, g, vis)
    comps(doc)["minecraft:material_instances"] = {["*"] = mat(tex(t, "side", "*"), e.params.render_method), up = mat(tex(t, "end", "up", "*"), e.params.render_method),
        down = mat(tex(t, "end", "down", "*"), e.params.render_method)}
end
local function pillar_conn_behaviour(doc, e, kind)
    local width = pillar_width(e)
    local lo, hi = pillar_bounds(width)
    for _, axis in ipairs(AXES) do
        for mask = 0, 63 do
            local c, cond = {}, {is(NS .. "axis", axis)}
            for i, d in ipairs(FACES6) do
                c[d] = (mask >> (i - 1)) & 1 == 1
                table.insert(cond, is(NS .. d, c[d]))
            end
            local boxes = kind == "wall_pillar" and wall_pillar_boxes(axis, c) or {connectable_shape(axis, c, lo, hi)}
            perm(doc, all(table.unpack(cond)), {["minecraft:collision_box"] = cboxes(boxes), ["minecraft:selection_box"] = cbox(bounds(boxes))})
        end
    end
    if kind == "wall_pillar" then comps(doc)["minecraft:light_dampening"] = 0 end
end
visual.connectable_pillar = function (doc, e, t, ctx) pillar_conn_visual(doc, e, t, ctx, "connectable_pillar") end
visual.wall_pillar = function (doc, e, t, ctx) pillar_conn_visual(doc, e, t, ctx, "wall_pillar") end
behaviour.connectable_pillar = function (doc, e) pillar_conn_behaviour(doc, e, "connectable_pillar") end
behaviour.wall_pillar = function (doc, e) pillar_conn_behaviour(doc, e, "wall_pillar") end

-- connected texture (TF ConnectedTextureModel / UnbakedConnectedTextureModel / ConnectionLogic):
-- every face is four 8 x 8 quadrants; each quadrant draws the overlay (TF's overlay_texture) or a
-- piece of the connected sheet (overlay_connected: cornerless / vertical / horizontal / corner) by
-- the connections to its two sides and the block in their corner (twilightforest:<direction> and
-- gloamingforest:corner_<a>_<b>, kept by the runtime). Bedrock's per-block budget (65536 states)
-- holds the corners only for blocks whose enabled faces need few of them (six sides and twelve
-- corners are 2^18): otherwise two connected sides count as a filled corner (cornerless), as in a
-- flat wall.
local CT_PLANE = { -- ConnectionLogic.AXIS_PLANE_DIRECTIONS by the face's axis
    x = {"up", "north", "down", "south"}, y = {"north", "east", "south", "west"}, z = {"up", "east", "down", "west"},
}
local NORMAL = {down = {0, -1, 0}, up = {0, 1, 0}, north = {0, 0, -1}, south = {0, 0, 1}, west = {-1, 0, 0}, east = {1, 0, 0}}
local CT_LOGIC = { -- ConnectionLogic: sheet (overlay / connected), u0, v0, u1, v1
    none = {"overlay", 0, 0, 16, 16}, cornerless = {"connected", 0, 0, 8, 8}, vertical = {"connected", 0, 8, 8, 16},
    horizontal = {"connected", 8, 0, 16, 8}, corner = {"connected", 8, 8, 16, 16},
}
local function ct_of(horizontal, vertical, corner) -- ConnectionLogic.of
    if corner then return "cornerless" end
    if horizontal then return vertical and "corner" or "horizontal" end
    return vertical and "vertical" or "none"
end
local function corner_key(a, b) -- the state of the diagonal of sides a and b (sorted names)
    if a > b then a, b = b, a end
    return "corner_" .. a .. "_" .. b
end

-- The overlay quad of quadrant i (1..4) of face f drawn with `logic` (UnbakedConnectedTextureModel:
-- the octant from the centre to the face's corner, its face UVs remapped by the logic).
local function ct_quad(f, i, logic, lift)
    local dirs = CT_PLANE[AXIS_OF[f]]
    local a, b, n = NORMAL[dirs[i]], NORMAL[dirs[i % 4 + 1]], NORMAL[f]
    local corner, box = {}, {}
    for k = 1, 3 do corner[k] = (n[k] + a[k] + b[k] + 1) * 8 end
    for k = 1, 3 do box[k] = math.min(8, corner[k]); box[k + 3] = math.max(8, corner[k]) end
    local l = CT_LOGIC[logic]
    local r = face_rect(box, f)
    local function ru(u) return l[2] + (l[4] - l[2]) * u / 16 end
    local function rv(v) return l[3] + (l[5] - l[3]) * v / 16 end
    local rect = {ru(r[1]), rv(r[2]), ru(r[3]), rv(r[4])}
    -- the quad on the face plane, lifted 0.01 px off the base face (no z-fighting; the client mod's
    -- corner patches 0.02 px, over the overlay)
    local k = n[1] ~= 0 and 1 or (n[2] ~= 0 and 2 or 3)
    local plane = (n[k] > 0 and 16 or 0) + n[k] * (lift or 0.01)
    box[k], box[k + 3] = plane, plane
    return cube(box, {faces = {[f] = {rect = rect, mat = l[1]}}, faces_only = true})
end

-- The sides the enabled faces look at, and the corner states the block can hold within the budget
-- (nil: none): the six side states and the others multiply up to 65536 permutations, and one
-- geometry per combination of the bits that matter stays at 256 at most.
local function ct_bits(faces, other_states)
    local sides_, corners = {}, {}
    for _, f in ipairs(FACES6) do
        if faces[f] then
            local dirs = CT_PLANE[AXIS_OF[f]]
            for i = 1, 4 do
                sides_[dirs[i]] = true
                corners[corner_key(dirs[i], dirs[i % 4 + 1])] = true
            end
        end
    end
    local s, c = {}, sorted_keys(corners)
    for _, f in ipairs(FACES6) do if sides_[f] then table.insert(s, f) end end
    if (1 << (6 + #c)) * other_states > 65536 or (1 << (#s + #c)) > 256 then c = nil end
    return s, c
end

local function ct_logic_for(f, i, conn, corners)
    local dirs = CT_PLANE[AXIS_OF[f]]
    local s1, s2 = dirs[i], dirs[i % 4 + 1]
    local side1, side2 = conn[s1], conn[s2]
    local corner
    if corners then corner = side1 and side2 and conn[corner_key(s1, s2)] else corner = side1 and side2 end
    return (i - 1) % 2 == 0 and ct_of(side1, side2, corner) or ct_of(side2, side1, corner)
end

visual.connected_texture = function (doc, e, t, ctx)
    local method = e.params.render_method or "alpha_test"
    if not (t.overlay and t.connected) then
        full_block(doc, t, e.params.render_method) -- no overlay sheets: the plain block
        return
    end
    local faces = {}
    for _, f in ipairs(e.params.faces or FACES6) do faces[f] = true end
    local disabled = e.params.render_on_disabled_faces ~= false
    local others = 1
    for name, values in pairs(states_of(doc)) do
        if not name:match("^gloamingforest:[a-z]+$") or not NORMAL[name:sub(#KIT + 1)] then others = others * #values end
    end
    local sides_, corners = ct_bits(faces, others)
    -- the bits: the sides the faces look at, then the corners; one geometry and one permutation per
    -- combination (the other sides change nothing)
    local bits = {}
    for _, f in ipairs(sides_) do table.insert(bits, f) end
    for _, c in ipairs(corners or {}) do table.insert(bits, c); ensure_state(doc, KIT .. c, BOOL()) end
    local fkey = {}
    for _, f in ipairs(FACES6) do table.insert(fkey, faces[f] and "1" or "0") end
    local prefix = "geometry.gf_families.ct_" .. table.concat(fkey) .. (disabled and "o" or "") .. (corners and "c" or "")
    for mask = 0, (1 << #bits) - 1 do
        local conn = {}
        for i, name in ipairs(bits) do conn[name] = (mask >> (i - 1)) & 1 == 1 end
        local g = geo(ctx, prefix .. "_" .. mask, function ()
            local cubes = {}
            for _, f in ipairs(FACES6) do
                for i = 1, 4 do
                    local logic
                    if faces[f] then logic = ct_logic_for(f, i, conn, corners) elseif disabled then logic = "none" end
                    if logic then table.insert(cubes, ct_quad(f, i, logic)) end
                end
            end
            return geo_doc(prefix .. "_" .. mask, {bone("base", {cube({0, 0, 0, 16, 16, 16})}), bone("overlay", cubes)})
        end)
        if mask == 0 then geometry(doc, g) end
        if mask > 0 then
            local cond = {}
            for i, name in ipairs(bits) do table.insert(cond, is(KIT .. name, conn[name])) end
            perm(doc, all(table.unpack(cond)), {["minecraft:geometry"] = g})
        end
    end
    comps(doc)["minecraft:material_instances"] = {
        ["*"] = mat(tex(t, "base", "*"), method), overlay = mat(t.overlay, method), connected = mat(t.connected, method),
    }
    -- no room for the corners in the states: the client mod draws the corner piece over a quadrant
    -- whose two sides connect while its diagonal block is missing (ConnectionLogic: corner, where the
    -- pack drew cornerless), through the client.render contract (src/client/families/CtCorners.cpp)
    -- with these patches and the connected sheet's image
    if not corners then
        local list = arr({})
        for _, f in ipairs(FACES6) do
            if faces[f] then
                table.insert(list, f)
                for i = 1, 4 do
                    local gid = "geometry.gf_families.ct_corner_" .. f .. "_" .. i
                    geo(ctx, gid, function () return geo_doc(gid, {bone("corner", {ct_quad(f, i, "corner", 0.02)})}) end)
                end
            end
        end
        local image = terrain_image(ctx, t.connected)
        if image then ctx.ct_corners[e.id] = {texture = image, faces = list} end
    end
end

-- ---- modifiers -----------------------------------------------------------------------------------
--
-- Families that add to any shape: waterloggable, snowloggable, climbable, lightable.

local modifiers = {}

-- Java SimpleWaterloggedBlock: Bedrock's liquid detection keeps the water layer (Java's
-- waterlogged property is not a block state on Bedrock, docs/spec/blockstates.md).
modifiers.waterloggable = function (doc)
    comps(doc)["minecraft:liquid_detection"] = {detection_rules = arr({{liquid_type = "water", can_contain_liquid = true, on_liquid_touches = "no_reaction"}})}
end

-- TF SnowLoggable: layers 0..8 of snow in the block (SNOW_SHAPE_BY_LAYER: 2 px per layer): snow
-- bones in a copy of the geometry, the snow in the selection box.
modifiers.snowloggable = function (doc, e, t, ctx)
    ensure_state(doc, NS .. "layers", ints(8))
    local g = comps(doc)["minecraft:geometry"]
    local id = type(g) == "table" and g.identifier or g
    if type(id) == "string" and ctx.geos[id] then
        local sid = id .. "_snowy"
        geo(ctx, sid, function ()
            local d = copy(ctx.geos[id])
            local geom = d["minecraft:geometry"][1]
            geom.description.identifier = sid
            for n = 1, 8 do table.insert(geom.bones, bone("snow_" .. n, {cube({0, 0, 0, 16, n * 2, 16}, {mat = "snow"})})) end
            return d
        end)
        local vis = type(g) == "table" and copy(g.bone_visibility) or {}
        for n = 1, 8 do vis["snow_" .. n] = is(NS .. "layers", n) end
        comps(doc)["minecraft:geometry"] = {identifier = sid, bone_visibility = vis}
        for _, p in ipairs(block_of(doc).permutations or {}) do
            local pg = p.components["minecraft:geometry"]
            if type(pg) == "table" and pg.identifier == id then
                pg.identifier = sid
                for n = 1, 8 do pg.bone_visibility["snow_" .. n] = is(NS .. "layers", n) end
            end
        end
        -- the snow takes the block's render method (Bedrock refuses opaque and transparent together)
        local mi = comps(doc)["minecraft:material_instances"] or {}
        local base = mi["*"]
        mi.snow = mat(e.params.snow_texture or "snow", base and base.render_method or "opaque",
            base and base.ambient_occlusion == false and {ambient_occlusion = true, face_dimming = true} or nil)
        comps(doc)["minecraft:material_instances"] = mi
    end
    -- the snow is part of the block's shape (SnowLoggable shapes). F1's skeleton of a TF block has
    -- Java's shape per state already (TFBushBlock.getShape / getCollisionShape with the snow of each
    -- layer count, per age): those stay, the kit's layer boxes would override them
    for _, p in ipairs(block_of(doc).permutations or {}) do
        local c = p.components or {}
        if type(p.condition) == "string" and p.condition:find(NS .. "layers", 1, true) and (c["minecraft:selection_box"] ~= nil or c["minecraft:collision_box"] ~= nil) then
            return
        end
    end
    local sel = comps(doc)["minecraft:selection_box"]
    local sb = type(sel) == "table" and sel.origin and {sel.origin[1] + 8, sel.origin[2], sel.origin[3] + 8, sel.origin[1] + 8 + sel.size[1], sel.origin[2] + sel.size[2], sel.origin[3] + 8 + sel.size[3]} or {2, 0, 2, 14, 13, 14}
    for n = 1, 8 do
        local snow = {0, 0, 0, 16, n * 2, 16}
        perm(doc, is(NS .. "layers", n), {["minecraft:selection_box"] = cbox(bounds({sb, snow})), ["minecraft:collision_box"] = n > 1 and cbox({0, 0, 0, 16, (n - 1) * 2, 16}) or false})
    end
end

-- Java ladders / scaffolding (TF climbable hollow logs, rope, iron ladder): the runtime marks the
-- block type climbable (BlockProperty::Climbable) on the server and in the client mod.
modifiers.climbable = function () end

-- TF LightableBlock: lighting none / normal / dim / ominous; light per level.
modifiers.lightable = function (doc, e)
    ensure_state(doc, NS .. "lighting", list("none", "normal", "dim", "ominous"))
    -- F1's skeleton already carries TF's own light per state (the block's lightLevel lambda: skull
    -- candles give 3 per candle, ...): it wins unless the data names light_levels
    if not e.params.light_levels then
        local has = comps(doc)["minecraft:light_emission"] ~= nil
        for _, p in ipairs(block_of(doc).permutations or {}) do
            if p.components and p.components["minecraft:light_emission"] ~= nil then has = true end
        end
        if has then return end
    end
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

-- F1 names the Java class: it refines the family (as src/feature/families/Registry.cpp does)
local function family_of(name, class)
    if name == "sign" and class == "WallSignBlock" then return "wall_sign" end
    if name == "hanging_sign" and class == "WallHangingSignBlock" then return "wall_hanging_sign" end
    if name == "pillar" then
        if class == "WallPillarBlock" then return "wall_pillar" end
        if class == "ThornsBlock" or class == "BurntThornsBlock" or class == "ConnectableRotatedPillarBlock" then return "connectable_pillar" end
    end
    return name
end

-- Default Bedrock block sounds per family (RP blocks.json) when params.sound is absent.
local DEFAULT_SOUND = {
    leaves = "grass", sapling = "grass", potted = "stone", pane = "glass", sign = "wood", wall_sign = "wood",
    hanging_sign = "hanging_sign", wall_hanging_sign = "hanging_sign", chest = "wood", trapped_chest = "wood",
}

-- Families whose Java class sets the sound type in its constructor, over the properties' one (and
-- over the import's sound for the id): CeilingHangingSignBlock and WallHangingSignBlock call
-- properties.sound(woodType.hangingSignSoundType()), and TF's wood types (TFWoodTypes: new
-- WoodType(name, set)) take WoodType's default SoundType.HANGING_SIGN, although TFBlocks copies the
-- planks' properties (SoundType.WOOD) into them. A params.sound still wins.
local FORCED_SOUND = {hanging_sign = "hanging_sign", wall_hanging_sign = "hanging_sign"}

-- ---- data ----------------------------------------------------------------------------------------

-- Normalises one data file's entries: [{id, family, params}] (or {"entries": [...]}).
local function read_entries(file, out, errors)
    local doc = try_json(file)
    if doc == nil then
        table.insert(errors, "unreadable family data " .. file)
        return
    end
    local items = doc
    if type(doc) == "table" and doc.entries then items = doc.entries end
    if type(items) ~= "table" then return end
    for i, e in ipairs(items) do
        if type(e) ~= "table" or type(e.id) ~= "string" or type(e.family) ~= "string" then
            table.insert(errors, string.format("%s: entry %d needs an id and a family", file, i))
        elseif not SHAPES[e.family] and not MODIFIERS[e.family] then
            table.insert(errors, string.format("%s: %s has the unknown family '%s'", file, e.id, e.family))
        else
            local params = type(e.params) == "table" and e.params or {}
            table.insert(out, {id = e.id, family = family_of(e.family, params.class), params = params, file = file})
        end
    end
end

-- Groups entries per id: one shape family (nil for modifier-only ids) and its modifiers.
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
        -- F1's form: the entry's modifiers in params.traits ({"family": "slab", "params": {"traits":
        -- ["waterloggable"]}}); connected_texture there only repeats the family
        if type(e.params.traits) == "table" then
            for _, t in ipairs(e.params.traits) do
                if MODIFIERS[t] then g.modifiers[t] = true end
            end
        end
        for k, v in pairs(e.params) do g.params[k] = v end
        table.insert(g.files, e.file)
    end
    for _, id in ipairs(order) do
        local g = by_id[id]
        if g.params.waterlogged or g.params.waterloggable then g.modifiers.waterloggable = true end
    end
    table.sort(order)
    return by_id, order
end

local function json_files(dir)
    if not os.isdir(dir) then return {} end
    local files = os.files(path.join(dir, "**.json"))
    table.sort(files)
    return files
end

-- BP definitions of a pack: identifier -> {file, doc}.
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

-- Block sound types the hand-made resource pack layer sets (`<dir>/blocks.d/*.json`, the manual
-- layer of the blocks.json merge): id -> {sound, file}. The manual layer wins per id, so the kit
-- never replaces one of these.
local function manual_block_sounds(dirs)
    local out = {}
    for _, dir in ipairs(dirs) do
        if os.isdir(path.join(dir, "blocks.d")) then
            for _, f in ipairs(os.files(path.join(dir, "blocks.d", "*.json"))) do
                local doc = try_json(f)
                if type(doc) == "table" then
                    for id, b in pairs(doc) do
                        if type(b) == "table" and type(b.sound) == "string" then out[id] = {sound = b.sound, file = f} end
                    end
                end
            end
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

-- ---- block documents -----------------------------------------------------------------------------

local KEEP = {
    "minecraft:destructible_by_mining", "minecraft:destructible_by_explosion", "minecraft:map_color",
    "minecraft:loot", "minecraft:display_name", "minecraft:flammable", "minecraft:light_emission",
    "minecraft:friction", "minecraft:crafting_table", "minecraft:destruction_particles",
}

-- The starting document of `entry`: the skeleton (whole, or its base components when the kit
-- draws the block), or the params / defaults. The skeleton's states are always kept.
local function base_doc(entry, skeleton, basedoc, keep_visual)
    local p = entry.params
    local sblock = skeleton and skeleton["minecraft:block"]
    local doc
    if sblock and keep_visual then
        doc = copy(skeleton)
        doc.format_version = FORMAT
        doc["minecraft:block"].description.traits = nil
        if doc["minecraft:block"].permutations == nil then doc["minecraft:block"].permutations = arr({}) end
        -- the skeleton's tags in its own format's form: write_tags writes them again in this one's
        strip_tags(doc["minecraft:block"].components)
    else
        local comps_ = {}
        local src = skeleton or basedoc
        local scomps = src and src["minecraft:block"] and src["minecraft:block"].components or {}
        for _, k in ipairs(KEEP) do
            if scomps[k] ~= nil then comps_[k] = copy(scomps[k]) end
        end
        if skeleton == nil and basedoc ~= nil then comps_["minecraft:loot"] = nil end -- the base block's loot is not ours
        local desc = {identifier = entry.id}
        local sdesc = sblock and sblock.description
        desc.menu_category = (sdesc and copy(sdesc.menu_category)) or p.menu_category or {category = "construction"}
        if sdesc and sdesc.states then desc.states = copy(sdesc.states) end
        doc = {format_version = FORMAT, ["minecraft:block"] = {description = desc, components = comps_, permutations = arr({})}}
    end
    local comps_ = doc["minecraft:block"].components
    if p.hardness then comps_["minecraft:destructible_by_mining"] = {seconds_to_destroy = p.hardness} end
    if p.resistance then comps_["minecraft:destructible_by_explosion"] = {explosion_resistance = p.resistance} end
    if p.map_color then comps_["minecraft:map_color"] = p.map_color end
    if p.light then comps_["minecraft:light_emission"] = p.light end
    if p.loot then comps_["minecraft:loot"] = p.loot end
    if comps_["minecraft:destructible_by_mining"] == nil then comps_["minecraft:destructible_by_mining"] = {seconds_to_destroy = 1} end
    return doc
end

-- ---- extras --------------------------------------------------------------------------------------

-- The display entity of sign text (spike S3: custom blocks hold no block entity, the text shows as
-- the name of an invisible entity at the sign; src/feature/families/Signs.cpp places it).
local function sign_text_entity(bpdir, rpdir)
    local bp = {
        format_version = "1.16.0",
        ["minecraft:entity"] = {
            description = {identifier = SIGN_TEXT_ENTITY, is_spawnable = false, is_summonable = true, is_experimental = false},
            components = {
                ["minecraft:type_family"] = {family = arr({"inanimate", "gloamingforest_sign_text"})},
                ["minecraft:collision_box"] = {width = 0.01, height = 0.01},
                ["minecraft:physics"] = {has_gravity = false, has_collision = false},
                ["minecraft:pushable"] = {is_pushable = false, is_pushable_by_piston = false},
                ["minecraft:damage_sensor"] = {triggers = {cause = "all", deals_damage = false}},
                ["minecraft:knockback_resistance"] = {value = 1.0},
                ["minecraft:health"] = {value = 1, max = 1},
                ["minecraft:fire_immune"] = {},
                ["minecraft:nameable"] = {always_show = true, allow_name_tag_renaming = false},
                ["minecraft:persistent"] = {},
                ["minecraft:conditional_bandwidth_optimization"] = {},
            },
        },
    }
    local file = path.join(bpdir, "entities", "gf_families_sign_text.json")
    os.mkdir(path.directory(file))
    json.savefile(file, bp)
    local rp = {
        format_version = "1.10.0",
        ["minecraft:client_entity"] = {
            description = {
                identifier = SIGN_TEXT_ENTITY,
                materials = {default = "entity_alphatest"},
                textures = {default = "textures/entity/sign"},
                geometry = {default = "geometry.gf_families.empty"},
                render_controllers = arr({"controller.render.default"}),
            },
        },
    }
    local rfile = path.join(rpdir, "entity", "gf_families_sign_text.entity.json")
    os.mkdir(path.directory(rfile))
    json.savefile(rfile, rp)
    return {
        format_version = GEO_FORMAT,
        ["minecraft:geometry"] = arr({{
            description = {identifier = "geometry.gf_families.empty", texture_width = 16, texture_height = 16, visible_bounds_width = 1, visible_bounds_height = 1},
            bones = arr({{name = "root", pivot = {0, 0, 0}}}),
        }}),
    }
end

-- ---- generation ----------------------------------------------------------------------------------

-- The entity textures of signs and chests (the assembled RP's TF textures, registered in
-- terrain_texture.json under tf_entity_*), or the params' textures.
local function entity_textures(g, rpdir, ctx)
    local function register(key, rel)
        ctx.terrain[key] = rel
        if not os.isfile(path.join(rpdir, rel .. ".png")) and not os.isfile(path.join(rpdir, rel .. ".tga")) then
            table.insert(ctx.missing, rel)
        end
        return key
    end
    local t = {}
    local shape = g.shape
    if shape == "sign" or shape == "wall_sign" then
        local w = wood_of({id = g.id, params = g.params})
        t.entity = register("tf_entity_sign_" .. w, "textures/twilightforest/entity/signs/" .. w)
    elseif shape == "hanging_sign" or shape == "wall_hanging_sign" then
        local w = wood_of({id = g.id, params = g.params})
        t.entity = register("tf_entity_hanging_sign_" .. w, "textures/twilightforest/entity/signs/hanging/" .. w)
    elseif shape == "chest" or shape == "trapped_chest" then
        local w = wood_of({id = g.id, params = g.params})
        local dir = CHEST_TEXTURE_DIRS[w] or w
        local trapped = shape == "trapped_chest"
        for _, k in ipairs({"single", "left", "right"}) do
            local name = trapped and (k == "single" and "trapped" or ("trapped_" .. k)) or k
            t[k] = register("tf_entity_chest_" .. dir .. "_" .. name, "textures/twilightforest/entity/chest/" .. dir .. "/" .. name)
        end
    end
    return t
end

-- ---- block sound events the families need ----------------------------------------------------------
--
-- The game picks a block's sounds by its sound type (RP blocks.json "sound") in sounds.json
-- block_sounds; an event the type does not define is reported in the content log ("[Sound][verbose]
-- No sound found for block type 'wood' and event type 'place_in_water'") and not played from the
-- type. Two events of the families' blocks are missing from the game's own types:
-- - place_in_water: the game's LevelSoundEvent PlaceInWater when a block is placed into water and
--   keeps it (a waterloggable family block). Java's BlockItem#place plays the block's place sound
--   wherever it is placed (SoundType#getPlaceSound), so the type's place_in_water is its place event.
-- - chest.open / chest.closed: the game's chest events played with the chest's block (vanilla's
--   chests have type wood too, which defines neither, so the game falls back to its
--   individual_event_sounds chest.open / chest.closed). TF's chests are vanilla ChestBlocks of their
--   wood (ChestBlockEntity#playSound: CHEST_OPEN / CHEST_CLOSE, volume 0.5, pitch 0.9..1.0, which
--   the inventory contract plays itself, F10): the chest family's types define the same values.
-- Packs merge sounds.json per event (the game's own vanilla_1.26.10 pack redefines only wood's place
-- event), so the events are added to the type as it is: its other events stay the game's (or the
-- import's, for TF's own types).
-- Side effect on vanilla content, intended: the types are the game's own (wood, stone, metal, glass,
-- grass, sand, cloth, ...), which vanilla blocks share, so a vanilla waterloggable block of such a
-- type placed into water plays its type's place event through this place_in_water too, and vanilla
-- chests (type wood) play chest.open / chest.closed through these definitions. Both carry vanilla's
-- own values (VANILLA_PLACE is the game's place event of each type, CHEST_EVENTS the game's chest
-- events), so vanilla blocks sound as Java's do and as the game's fallback would; only the content
-- log's "No sound found" verbose lines go away for them as well.

-- The place events of the game's block sound types (Minecraft Bedrock 1.26.51, the vanilla resource
-- packs' sounds.json block_sounds, the last pack's definition of each).
local VANILLA_PLACE = {
    amethyst_block = {sound = "place.amethyst_block", volume = 1.0, pitch = 0.8},
    anvil = {sound = "random.anvil_land", volume = 0.5, pitch = {0.8, 1.0}},
    azalea_leaves = {sound = "place.azalea_leaves", volume = 1.0, pitch = {0.8, 1.0}},
    bamboo_wood = {sound = "place.bamboo_wood", volume = 1.0, pitch = {0.8, 0.8}},
    bamboo_wood_hanging_sign = {sound = "place.bamboo_wood_hanging_sign", volume = 1.0, pitch = {0.8, 1.0}},
    basalt = {sound = "place.basalt", volume = 1.0, pitch = {0.8, 1.0}},
    bone_block = {sound = "place.bone_block", volume = 1.0, pitch = {0.8, 1.0}},
    calcite = {sound = "place.calcite", volume = 1.0},
    candle = {sound = "place.candle", volume = 1.0, pitch = 1.0},
    chain = {sound = "place.chain", volume = 1.0, pitch = {0.8, 1.0}},
    cherry_leaves = {sound = "place.cherry_leaves", volume = 1.0},
    cherry_wood = {sound = "place.cherry_wood", volume = 1.0, pitch = {0.8, 0.8}},
    cherry_wood_hanging_sign = {sound = "place.cherry_wood_hanging_sign", volume = 1.0, pitch = {0.8, 1.0}},
    cloth = {sound = "place.cloth", volume = 1.0, pitch = {0.8, 1.0}},
    copper = {sound = "place.copper", volume = 1.0, pitch = {0.8, 1.0}},
    deepslate = {sound = "place.deepslate", volume = 1.0, pitch = {0.8, 1.0}},
    deepslate_bricks = {sound = "place.deepslate_bricks", volume = 1.0, pitch = {0.8, 1.0}},
    glass = {sound = "place.stone", volume = 1.0, pitch = {0.8, 1.0}},
    grass = {sound = "place.grass", volume = 0.8, pitch = {0.8, 1.0}},
    gravel = {sound = "place.gravel", volume = 1.0, pitch = {0.8, 1.0}},
    hanging_sign = {sound = "place.hanging_sign", volume = 1.0, pitch = {0.8, 1.0}},
    ladder = {sound = "place.wood", volume = 1.0, pitch = {0.8, 1.0}},
    lantern = {sound = "block.lantern.place", volume = 1.0, pitch = {0.8, 1.0}},
    metal = {sound = "place.stone", volume = 1.0, pitch = {1.2, 1.25}},
    moss_block = {sound = "place.moss", volume = 0.93},
    moss_carpet = {sound = "place.moss", volume = 0.93},
    mud_bricks = {sound = "block.mud_bricks.place", volume = 0.3, pitch = {0.6, 0.8}},
    nether_brick = {sound = "place.nether_brick", volume = 1.0, pitch = {0.8, 1.0}},
    nether_wood = {sound = "place.nether_wood", volume = 1.0, pitch = {0.8, 0.8}},
    nether_wood_hanging_sign = {sound = "place.nether_wood_hanging_sign", volume = 1.0, pitch = {0.8, 1.0}},
    netherite = {sound = "place.netherite", volume = 1.0, pitch = {0.8, 1.0}},
    normal = {sound = "place.stone", volume = 1.0, pitch = {0.8, 1.0}},
    sand = {sound = "place.sand", volume = 1.0, pitch = {0.8, 1.0}},
    scaffolding = {sound = "block.scaffolding.place", volume = 1.0, pitch = {0.8, 0.9}},
    slime = {sound = "place.slime.big", volume = 1.0, pitch = {0.8, 1.0}},
    snow = {sound = "place.snow", volume = 1.0, pitch = {0.8, 1.0}},
    stem = {sound = "place.stem", volume = 1.0, pitch = {0.8, 1.0}},
    stone = {sound = "place.stone", volume = 1.0, pitch = {0.8, 1.0}},
    tuff = {sound = "place.tuff", volume = 1.0, pitch = 0.8},
    vines = {sound = "place.vines", volume = 1.0, pitch = {0.8, 1.0}},
    wood = {sound = "place.wood", volume = 1.0, pitch = {0.8, 0.8}},
}

-- The game's own chest events (individual_event_sounds chest.open / chest.closed of the vanilla
-- pack: ChestBlockEntity's CHEST_OPEN / CHEST_CLOSE at volume 0.5, pitch 0.9..1.0).
local CHEST_EVENTS = {
    ["chest.open"] = {sound = "random.chestopen", volume = 0.5, pitch = {0.9, 1.0}},
    ["chest.closed"] = {sound = "random.chestclosed", volume = 0.5, pitch = {0.9, 1.0}},
}

-- A sound event definition written to sounds.json (arrays marked as arrays).
local function sound_event(def)
    local out = {}
    for k, v in pairs(def) do out[k] = type(v) == "table" and arr(copy(v)) or v end
    return out
end

-- Adds place_in_water to the sound type of every waterloggable family block of `result`, and the
-- chest events to the sound type of every chest family block, in the pack's sounds.json.
local function add_family_sound_events(rpdir, result, by_id, errors)
    local blocks = try_json(path.join(rpdir, "blocks.json")) or {}
    local function type_of(id)
        local b = blocks[id]
        return type(b) == "table" and type(b.sound) == "string" and b.sound or nil
    end
    local in_water, chests = {}, {}
    for id, _ in pairs(result.waterloggable) do
        local t = type_of(id)
        if t then in_water[t] = in_water[t] or id end
    end
    for id, _ in pairs(result.chests) do
        local t = by_id[id] and type_of(id)
        if t then chests[t] = chests[t] or id end
    end
    if table.empty(in_water) and table.empty(chests) then return end
    merge_json_file(path.join(rpdir, "sounds.json"), function (doc)
        doc = doc or {}
        doc.block_sounds = doc.block_sounds or {}
        local function events_of(t)
            doc.block_sounds[t] = doc.block_sounds[t] or {}
            doc.block_sounds[t].events = doc.block_sounds[t].events or {}
            return doc.block_sounds[t].events
        end
        for _, t in ipairs(sorted_keys(in_water)) do
            local own = doc.block_sounds[t] and doc.block_sounds[t].events and doc.block_sounds[t].events.place
            local place = own or VANILLA_PLACE[t]
            if place == nil then
                table.insert(errors, string.format("families: %s is waterloggable, but its block sound type '%s' has no place event to play when it is placed into water (tools/families/families.lua VANILLA_PLACE)", in_water[t], t))
            else
                local events = events_of(t)
                if events.place_in_water == nil then events.place_in_water = type(place) == "table" and sound_event(place) or place end
            end
        end
        for _, t in ipairs(sorted_keys(chests)) do
            local events = events_of(t)
            for _, name in ipairs(sorted_keys(CHEST_EVENTS)) do
                if events[name] == nil then events[name] = sound_event(CHEST_EVENTS[name]) end
            end
        end
        return doc
    end)
end

-- Generates the family blocks of `entries` into one BP / RP pair. Returns {blocks, skipped, files}.
local function generate_set(entries, bpdir, rpdir, opts, errors)
    local by_id, order = group(entries, errors)
    local existing = bp_blocks(bpdir)
    local manual = opts.manual or {}
    local ctx = {geos = {}, terrain = {}, missing = {}, ct_corners = {}}
    -- the resource pack's own terrain textures (the images of the textures the kit tints)
    local rp_terrain = try_json(path.join(rpdir, "textures", "terrain_texture.json"))
    ctx.rp_terrain = type(rp_terrain) == "table" and rp_terrain.texture_data or {}
    -- textures the caller adds to the pack afterwards (the fixtures' vanilla paths)
    for k, v in pairs(opts.textures or {}) do if ctx.rp_terrain[k] == nil then ctx.rp_terrain[k] = {textures = v} end end
    ctx.loot_double = function (e)
        local rel = "loot_tables/gf_families/" .. safe_name(e.id) .. "_double.json"
        local file = path.join(bpdir, rel)
        os.mkdir(path.directory(file))
        json.savefile(file, {pools = arr({{rolls = 1, entries = arr({{type = "item", name = e.id, weight = 1,
            functions = arr({{["function"] = "set_count", count = 2}})}})}})})
        return rel
    end
    local result = {blocks = 0, skipped = 0, files = {}, sounds = {}, forced_sounds = {}, waterloggable = {}, chests = {}, tags = {}}
    for _, id in ipairs(order) do
        local g = by_id[id]
        if manual[id] then
            result.skipped = result.skipped + 1
        else
            local skel = existing[id]
            local base = g.params.base and existing[g.params.base] or nil
            local e = {id = id, params = g.params}
            local shape = g.shape
            local skel_geo = skel and skel.doc["minecraft:block"] and skel.doc["minecraft:block"].components["minecraft:geometry"] ~= nil
            -- F1's skeleton models stay (TF's own block models); the kit draws block entity models,
            -- connected textures and blocks without a skeleton model; modifier-only ids keep theirs
            local keep_visual = skel_geo and (shape == nil or not ENTITY_MODEL[shape]) and not g.params.textures
            if shape == nil and not skel then
                shape = "cube"
            end
            local doc = base_doc(e, skel and skel.doc or nil, base and base.doc or nil, keep_visual or (shape == nil))
            for _, s in ipairs(shape and FAMILY_STATES[shape] or {}) do ensure_state(doc, (s[3] or NS) .. s[1], state_values(s[2])) end
            local t = textures_of(e, skel and skel.doc or (base and base.doc) or nil)
            if shape and ENTITY_MODEL[shape] and not g.params.textures then
                for k, v in pairs(entity_textures(g, rpdir, ctx)) do t[k] = v end
            end
            if shape and not keep_visual then
                local v = visual[shape]
                if v then v(doc, e, t, ctx) elseif shape == "cube" then full_block(doc, t, g.params.render_method) end
            end
            if shape and behaviour[shape] then behaviour[shape](doc, e, ctx) end
            for _, m in ipairs(sorted_keys(g.modifiers)) do modifiers[m](doc, e, t, ctx) end
            -- the tags last: a behaviour may have moved the document to another format (chests). A
            -- tag the entry's own components carry (either form) joins them, so the components
            -- copied below never put a tag form back that this document's format refuses
            local tags = block_tags(e, (skel and skel.doc) or (base and base.doc) or nil)
            local extra = type(g.params.components) == "table" and g.params.components or nil
            if extra then
                local seen = {}
                for _, t in ipairs(tags) do seen[t] = true end
                tags_in(extra, tags, seen)
            end
            write_tags(doc, tags)
            result.tags[id] = tags
            if extra then
                for k, v in pairs(extra) do
                    local tag_form = type(k) == "string" and (k:match("^tag:") or k == "minecraft:tags")
                    if not tag_form then comps(doc)[k] = v end
                end
            end
            if block_of(doc).permutations and #block_of(doc).permutations == 0 then block_of(doc).permutations = nil end
            local file = skel and skel.file or path.join(bpdir, "blocks", "gf_families", safe_name(id) .. ".json")
            os.mkdir(path.directory(file))
            json.savefile(file, doc)
            table.insert(result.files, file)
            if g.modifiers.waterloggable then result.waterloggable[id] = file end
            result.blocks = result.blocks + 1
            local forced = g.params.sound == nil and shape and FORCED_SOUND[shape] or nil
            local sound = g.params.sound or forced or (shape and DEFAULT_SOUND[shape])
            if sound then result.sounds[id] = sound end
            if forced then result.forced_sounds[id] = true end
        end
    end
    result.ct_corners = ctx.ct_corners
    -- the chest family blocks (hand-made BP files too: the container is the runtime's)
    for id, g in pairs(by_id) do
        if g.shape == "chest" or g.shape == "trapped_chest" then result.chests[id] = g.shape end
    end
    -- the sign text display entity, when there are signs
    for _, g in pairs(by_id) do
        if g.shape == "sign" or g.shape == "wall_sign" or g.shape == "hanging_sign" or g.shape == "wall_hanging_sign" then
            ctx.geos["geometry.gf_families.empty"] = sign_text_entity(bpdir, rpdir)
            break
        end
    end
    -- geometries
    for _, gid in ipairs(sorted_keys(ctx.geos)) do
        local file = path.join(rpdir, GEO_DIR, gid:gsub("^geometry%.gf_families%.", "") .. ".geo.json")
        os.mkdir(path.directory(file))
        json.savefile(file, ctx.geos[gid])
    end
    -- entity textures used as block textures
    if not table.empty(ctx.terrain) then
        merge_json_file(path.join(rpdir, "textures", "terrain_texture.json"), function (doc)
            doc = doc or {resource_pack_name = "gloamingforest", texture_name = "atlas.terrain", padding = 8, num_mip_levels = 4, texture_data = {}}
            doc.texture_data = doc.texture_data or {}
            -- a tinted texture as vanilla's waterlily: [{path, tint_color}]
            for k, v in pairs(ctx.terrain) do if doc.texture_data[k] == nil then doc.texture_data[k] = {textures = type(v) == "table" and arr({v}) or v} end end
            return doc
        end)
    end
    for _, m in ipairs(ctx.missing) do
        if opts.strict_textures then table.insert(errors, "families: texture " .. m .. " is not in the resource pack") end
    end
    -- block sounds (RP blocks.json): the kit's default per family where the block has no sound yet.
    -- An id that has one keeps it: TF's sound types (the jars' twilightforest.jar, TFSoundTypes) are
    -- F1's import, its rp/blocks.d and rp/sounds.json.d fragments (block_sounds and the step, fall,
    -- jump and land interactive_sounds), except where the family's Java class sets the sound type
    -- itself (FORCED_SOUND: the hanging signs), which replaces the one their properties give (the
    -- import's). A sound the hand-made RP layer sets (opts.manual_sounds) always stays: the manual
    -- layer wins per id; a forced sound it differs from is reported in result.notes
    local manual_sounds = opts.manual_sounds or {}
    result.notes = {}
    if not table.empty(result.sounds) then
        merge_json_file(path.join(rpdir, "blocks.json"), function (doc)
            doc = doc or {format_version = "1.21.40"}
            for _, id in ipairs(sorted_keys(result.sounds)) do
                local sound = result.sounds[id]
                local by_hand = manual_sounds[id]
                if by_hand then
                    if result.forced_sounds[id] and by_hand.sound ~= sound then
                        table.insert(result.notes, string.format("families: %s keeps the sound '%s' of %s (the manual layer), not the family's '%s'", id, by_hand.sound, by_hand.file, sound))
                    end
                elseif doc[id] == nil then doc[id] = {sound = sound}
                elseif type(doc[id]) == "table" and (doc[id].sound == nil or result.forced_sounds[id]) then doc[id].sound = sound end
            end
            return doc
        end)
    end
    add_family_sound_events(rpdir, result, by_id, errors)
    return result
end

-- ---- containers ----------------------------------------------------------------------------------
--
-- Chests and trapped chests keep their slots, screens, sounds, opening stat and comparator output
-- in the inventory contract (F10, spike S3: src/feature/inventory): the generator declares every
-- chest family block as a container there (data/gf/containers/<name>, read at mod load) with
-- ChestBlock's rules: 27 slots, a left / right pair opening as one 54-slot chest, not opened under
-- a redstone conductor or a sitting cat, the opening stat (TrappedChestBlock#getOpenChestStat:
-- trigger_trapped_chest) and the comparator output. A unit's own declaration of the block
-- (assets/data/containers/<u>.json) wins. The runtime (src/feature/families/Chests.cpp) keeps the
-- pairing, the lid and the trapped chest's signal.
local CONTAINERS_DIR = path.join("data", "gf", "containers")

-- The blocks the other container files of `outdir` declare.
local function declared_elsewhere(outdir, own)
    local out = {}
    for _, f in ipairs(json_files(path.join(outdir, CONTAINERS_DIR))) do
        if path.filename(f) ~= own then
            local doc = try_json(f)
            for _, e in ipairs(type(doc) == "table" and doc or {}) do
                if type(e) == "table" and type(e.block) == "string" then out[e.block] = f end
            end
        end
    end
    return out
end

-- Declares `chests` (id -> "chest" | "trapped_chest") in data/gf/containers/<name>.
local function declare_chests(outdir, chests, name, files)
    if table.empty(chests) then return end
    local others = declared_elsewhere(outdir, name)
    local list = arr({})
    for _, id in ipairs(sorted_keys(chests)) do
        if not others[id] then
            -- ChestBlockEntity#getDefaultName / CompoundContainer: container.chest(Double). F11 owns
            -- these two keys (assets/lang/<locale>/families.json, rule R9): the chest family names
            -- its containers with them; the inventory contract (F10) only falls back to the same
            -- key and must not define it again (a duplicate fails the lang merge).
            table.insert(list, {block = id, rows = 3, title = "container.chest", chest = true, pairs = true, double_title = "container.chestDouble",
                stat = chests[id] == "trapped_chest" and "minecraft:trigger_trapped_chest" or "minecraft:open_chest", comparator = true})
        end
    end
    local file = path.join(outdir, CONTAINERS_DIR, name)
    os.mkdir(path.directory(file))
    json.savefile(file, list)
    table.insert(files, file)
end

-- The connected texture blocks whose corners the client mod draws (data/gf/families_render/<name>):
-- {id: {texture, faces}} (visual.connected_texture).
local function write_ct_corners(outdir, corners, name, files)
    if table.empty(corners) then return end
    local file = path.join(outdir, "data", "gf", "families_render", name)
    os.mkdir(path.directory(file))
    json.savefile(file, {ct_corners = corners})
    table.insert(files, file)
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
-- ---- checks --------------------------------------------------------------------------------------
--
-- What the generator wrote, checked the way tools/xmake/assemble.lua checks the packs (integer
-- states list 0..n, the permutation budget per block) plus what only it knows: every geometry a
-- generated block names is in the resource packs, every waterloggable modifier (a family of its
-- own, params.waterlogged or F1's params.traits) has Bedrock's liquid detection.

local TRAIT_STATES = {
    ["minecraft:cardinal_direction"] = 4, ["minecraft:facing_direction"] = 6, ["minecraft:block_face"] = 6,
    ["minecraft:vertical_half"] = 2, ["minecraft:corner_and_cardinal_direction"] = 20,
}

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
    local function geo_ok(g)
        local id = type(g) == "table" and g.identifier or g
        if type(id) == "string" and id:match("^geometry%.") and not geos[id] then
            table.insert(errors, string.format("%s: geometry %s is not in the resource pack", file, id))
        end
    end
    geo_ok(block.components["minecraft:geometry"])
    for _, p in ipairs(block.permutations or {}) do geo_ok(p.components["minecraft:geometry"]) end
    -- spike S7: a block format refuses the other tag form, and minecraft:block_entity before 1.26.20
    local legacy, array = false, block.components["minecraft:tags"] ~= nil
    for k, _ in pairs(block.components) do
        if type(k) == "string" and k:match("^tag:") then legacy = true end
    end
    if tags_as_array(doc) and legacy then
        table.insert(errors, string.format("%s: format %s takes the minecraft:tags array, not tag:* components", file, tostring(doc.format_version)))
    elseif not tags_as_array(doc) and array then
        table.insert(errors, string.format("%s: format %s takes tag:* components, not minecraft:tags", file, tostring(doc.format_version)))
    end
    if block.components["minecraft:block_entity"] ~= nil and not tags_as_array(doc) then
        table.insert(errors, string.format("%s: format %s refuses minecraft:block_entity", file, tostring(doc.format_version)))
    end
    return count
end

local function rp_geometries(rpdir, out)
    if not os.isdir(rpdir) then return end
    for _, f in ipairs(os.files(path.join(rpdir, "models", "**.json"))) do
        local doc = try_json(f)
        for _, g in ipairs(doc and doc["minecraft:geometry"] or {}) do
            local id = g.description and g.description.identifier
            if id then out[id] = true end
        end
    end
end

-- Checks the files of `summary` (generate's); returns the permutation count.
local function verify(outdir, summary, errors)
    local geos = {}
    for _, rp in ipairs({"GloamingForest_RP", "GloamingForest_Test_RP"}) do rp_geometries(path.join(outdir, "resource_packs", rp), geos) end
    local perms = 0
    for _, f in ipairs(summary.files) do
        local doc = try_json(f)
        if doc and doc["minecraft:block"] then perms = perms + check_block(f, doc, geos, errors) end
    end
    for id, f in pairs(summary.waterloggable) do
        local doc = try_json(f)
        local comps_ = doc and doc["minecraft:block"] and doc["minecraft:block"].components or {}
        local rules = comps_["minecraft:liquid_detection"] and comps_["minecraft:liquid_detection"].detection_rules
        if not (rules and rules[1] and rules[1].can_contain_liquid) then
            table.insert(errors, string.format("%s: %s is waterloggable but has no liquid detection", f, id))
        end
    end
    -- the chests carry their block actor (F10's native container backend, spike S3) and every tag
    -- of the entry (spike S7: the minecraft:tags array of their format)
    for _, f in ipairs(summary.files) do
        local doc = try_json(f)
        local block = doc and doc["minecraft:block"]
        local id = block and block.description and block.description.identifier
        if id and summary.chests[id] then
            if block.components["minecraft:block_entity"] == nil then
                table.insert(errors, string.format("%s: the chest %s has no minecraft:block_entity (its slots would be block data, which hoppers do not see)", f, id))
            end
            local want = summary.chest_tags[id] or {}
            local have, has = tags_in(block.components)
            local same = #have == #want
            for _, t in ipairs(want) do if not has[t] then same = false end end
            if not same then
                table.insert(errors, string.format("%s: the chest %s does not carry its tags [%s]", f, id, table.concat(want, ", ")))
            end
        end
    end
    -- the fixture in F1's form ({"family": "slab", "params": {"traits": ["waterloggable"]}})
    if summary.fixtures > 0 and not summary.waterloggable["gf_test:family_slab"] then
        table.insert(errors, "gf_test:family_slab: params.traits [waterloggable] did not make it waterloggable")
    end
    return perms
end

-- A pack the generator completed has this file in its root (with LICENSE-ASSETS.txt): running it
-- again on the same output is a no-op (it would stack permutations on the kept skeletons).
-- tools/xmake/assemble.lua rebuilds every pack directory from scratch, which drops it.
local MARKER = "families-generated.txt"

local function marked(bpdir) return os.isfile(path.join(bpdir, MARKER)) end
local function mark(bpdir, count)
    io.writefile(path.join(bpdir, MARKER), string.format("Block families generated by tools/families (F11): %d blocks.\n", count))
end

-- opts: {manual_dirs = {...}} (default: assets/pack/bp and assets/test/bp of `projectdir`),
-- {manual_rp_dirs = {...}} (default: assets/pack/rp and assets/test/rp), no_fixtures.
-- Returns {blocks, skipped, fixtures, files = {...}, waterloggable = {id -> file}, permutations,
-- already (both sets were generated before: nothing done), errors = {...}}.
function generate(projectdir, outdir, opts)
    opts = opts or {}
    local errors = {}
    local manual = manual_ids(opts.manual_dirs or {path.join(projectdir, "assets", "pack", "bp"), path.join(projectdir, "assets", "test", "bp")})
    -- the hand-made RP layer's block sounds (assets/pack/rp, assets/test/rp: blocks.d fragments)
    local manual_sounds = manual_block_sounds(opts.manual_rp_dirs or {path.join(projectdir, "assets", "pack", "rp"), path.join(projectdir, "assets", "test", "rp")})
    -- waterloggable: id -> generated file (every waterloggable modifier, by family or params.traits)
    local summary = {blocks = 0, skipped = 0, fixtures = 0, files = {}, waterloggable = {}, chests = {}, chest_tags = {}, permutations = 0, errors = errors}
    local sets, done = 0, 0

    local bp = path.join(outdir, "behavior_packs", "GloamingForest_BP")
    local rp = path.join(outdir, "resource_packs", "GloamingForest_RP")
    local entries, ferrors = families(outdir)
    for _, e in ipairs(ferrors) do table.insert(errors, e) end
    if os.isdir(bp) and os.isdir(rp) and #entries > 0 then
        sets = sets + 1
        if marked(bp) then
            done = done + 1
        else
            local r = generate_set(entries, bp, rp, {manual = manual, manual_sounds = manual_sounds, strict_textures = true}, errors)
            for _, n in ipairs(r.notes or {}) do print("warning: " .. n) end
            summary.blocks, summary.skipped = r.blocks, r.skipped
            for _, f in ipairs(r.files) do table.insert(summary.files, f) end
            for id, f in pairs(r.waterloggable) do summary.waterloggable[id] = f end
            for id, shape in pairs(r.chests) do summary.chests[id] = shape; summary.chest_tags[id] = r.tags[id] end
            declare_chests(outdir, r.chests, "families.json", summary.files)
            write_ct_corners(outdir, r.ct_corners, "families.json", summary.files)
            mark(bp, r.blocks)
        end
    end

    -- fixtures into the gf_test packs (development builds)
    local tbp = path.join(outdir, "behavior_packs", "GloamingForest_Test_BP")
    local trp = path.join(outdir, "resource_packs", "GloamingForest_Test_RP")
    if os.isdir(tbp) and os.isdir(trp) and not opts.no_fixtures then
        local fx = fixtures(projectdir, errors)
        if #fx.entries > 0 then sets = sets + 1 end
        if #fx.entries > 0 and marked(tbp) then
            done = done + 1
        elseif #fx.entries > 0 then
            local r = generate_set(fx.entries, tbp, trp, {manual = manual, manual_sounds = manual_sounds, textures = fx.textures}, errors)
            for _, n in ipairs(r.notes or {}) do print("warning: " .. n) end
            summary.fixtures = r.blocks
            for _, f in ipairs(r.files) do table.insert(summary.files, f) end
            for id, f in pairs(r.waterloggable) do summary.waterloggable[id] = f end
            for id, shape in pairs(r.chests) do summary.chests[id] = shape; summary.chest_tags[id] = r.tags[id] end
            declare_chests(outdir, r.chests, "families_test.json", summary.files)
            write_ct_corners(outdir, r.ct_corners, "families_test.json", summary.files)
            mark(tbp, r.blocks)
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
    summary.already = sets > 0 and done == sets
    summary.permutations = verify(outdir, summary, errors)
    return summary
end
