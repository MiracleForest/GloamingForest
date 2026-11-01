// GloamingForest import tool (contract "import", unit F1): TF's textures (CC BY-NC-SA 4.0) copied into
// the resource pack, atlas fragments (terrain / item / flipbook) per owning unit, references to the
// vanilla textures TF models reuse, and the generated chest-boat textures (TextureGeneratorReloadListener).
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace GfImport
{
    public sealed partial class Ctx
    {
        // texture id ("twilightforest:block/x") -> owners that use it, and the atlases it is used in
        public readonly Dictionary<string, SortedSet<string>> TexUsers = new Dictionary<string, SortedSet<string>>(StringComparer.Ordinal);
        public readonly Dictionary<string, SortedSet<string>> TexAtlases = new Dictionary<string, SortedSet<string>>(StringComparer.Ordinal);
        public readonly SortedSet<string> MissingTextures = new SortedSet<string>(StringComparer.Ordinal);
        Dictionary<string, string> tfTextureFiles;
        readonly Dictionary<string, bool> alphaCache = new Dictionary<string, bool>(StringComparer.Ordinal);

        public string TexturesDir { get { return Path.Combine(MainRes, "assets", "twilightforest", "textures"); } }

        /// <summary>TF texture id -> file (textures/<path>.png).</summary>
        public Dictionary<string, string> TfTextures
        {
            get
            {
                if (tfTextureFiles == null)
                {
                    tfTextureFiles = new Dictionary<string, string>(StringComparer.Ordinal);
                    foreach (var rel in Files(TexturesDir, "*.png"))
                        tfTextureFiles["twilightforest:" + StripExt(rel)] = Path.Combine(TexturesDir, rel);
                }
                return tfTextureFiles;
            }
        }

        /// <summary>The atlas key of a texture id ("tf_block_naga_stone", "tf_vanilla_oak_planks").</summary>
        public static string TexKey(string texId)
        {
            int c = texId.IndexOf(':');
            string ns = texId.Substring(0, c), path = texId.Substring(c + 1);
            return (ns == "twilightforest" ? "tf_" : "tf_vanilla_") + path.Replace('/', '_');
        }

        /// <summary>The resource pack path (no extension) of a texture id.</summary>
        public string TexPath(string texId)
        {
            int c = texId.IndexOf(':');
            string ns = texId.Substring(0, c), path = texId.Substring(c + 1);
            if (ns == "twilightforest") return "textures/twilightforest/" + path;
            string v;
            return VanillaTextures.TryGetValue(path, out v) ? v : null;
        }

        /// <summary>Records that unit `ownerId` uses texture `texId` in `atlas` (terrain, item, path). Returns its atlas key, or null when it does not exist.</summary>
        public string UseTexture(string texId, string ownerId, string atlas)
        {
            if (texId == null) return null;
            if (!texId.Contains(":")) texId = "minecraft:" + texId;
            if (texId.StartsWith("twilightforest:", StringComparison.Ordinal) && !TfTextures.ContainsKey(texId))
            {
                MissingTextures.Add(texId);
                return null;
            }
            if (!texId.StartsWith("twilightforest:", StringComparison.Ordinal) && TexPath(texId) == null)
            {
                MissingTextures.Add(texId);
                return null;
            }
            SortedSet<string> s;
            if (!TexUsers.TryGetValue(texId, out s)) TexUsers[texId] = s = new SortedSet<string>(StringComparer.Ordinal);
            s.Add(ownerId ?? ImportUnit);
            if (!TexAtlases.TryGetValue(texId, out s)) TexAtlases[texId] = s = new SortedSet<string>(StringComparer.Ordinal);
            s.Add(atlas);
            return TexKey(texId);
        }

        /// <summary>Whether a TF texture has any transparent pixel (decides alpha_test for opaque-looking models).</summary>
        public bool HasAlpha(string texId)
        {
            bool r;
            if (alphaCache.TryGetValue(texId, out r)) return r;
            string file;
            r = false;
            if (TfTextures.TryGetValue(texId, out file))
            {
                try { r = Png.Decode(File.ReadAllBytes(file)).Px.Any(p => (p >> 24) < 255); }
                catch (Exception e) { Warn("cannot decode " + file + ": " + e.Message); }
            }
            alphaCache[texId] = r;
            return r;
        }

        /// <summary>One owner for a set of users: the single user, else F1 (shared).</summary>
        public static string SharedOwner(IEnumerable<string> users)
        {
            var l = users.Distinct().ToList();
            return l.Count == 1 ? l[0] : ImportUnit;
        }

        // ---- the "textures" kind ----------------------------------------------------------------------

        public void ImportTextures()
        {
            var flip = new Dictionary<string, List<J>>(StringComparer.Ordinal);
            var generated = ChestBoatTextures();
            foreach (var kv in TfTextures.OrderBy(k => k.Key, StringComparer.Ordinal))
            {
                string texId = kv.Key, file = kv.Value;
                string rel = texId.Substring("twilightforest:".Length);
                string owner = TextureOwner(texId);
                byte[] bytes;
                if (!generated.TryGetValue(texId, out bytes)) bytes = File.ReadAllBytes(file);
                Emit("textures", owner, "rp/textures/twilightforest/" + rel + ".png", bytes);
                SortedSet<string> atlases;
                TexAtlases.TryGetValue(texId, out atlases);
                string key = TexKey(texId);
                var entry = J.Obj().Set("textures", "textures/twilightforest/" + rel);
                if (atlases != null && atlases.Contains("terrain")) Fragment("textures", owner, "rp/terrain_texture.d/import-" + NameOf(owner) + ".json", key, entry);
                if (atlases != null && atlases.Contains("item")) Fragment("textures", owner, "rp/item_texture.d/import-" + NameOf(owner) + ".json", key, entry);
                // animated block textures: Java .png.mcmeta -> Bedrock flipbook
                string meta = file + ".mcmeta";
                if (File.Exists(meta) && atlases != null && atlases.Contains("terrain"))
                {
                    var m = ReadJson(meta)["animation"];
                    if (m != null)
                    {
                        var fb = J.Obj().Set("flipbook_texture", "textures/twilightforest/" + rel).Set("atlas_tile", key);
                        fb.Set("ticks_per_frame", m["frametime"] != null ? m["frametime"].AsLong(1) : 1);
                        if (m["frames"] != null)
                        {
                            var frames = J.Arr();
                            foreach (var f in m["frames"].A) frames.Add(f.IsObj ? f["index"].Clone() : f.Clone());
                            fb.Set("frames", frames);
                        }
                        if (m["interpolate"] != null && m["interpolate"].Kind == JKind.Bool && m["interpolate"].B) fb.Set("blend_frames", true);
                        string fk = NameOf(owner);
                        List<J> l;
                        if (!flip.TryGetValue(fk, out l)) flip[fk] = l = new List<J>();
                        l.Add(fb);
                    }
                }
            }
            // vanilla textures reused by TF models: atlas keys pointing at the game's own files
            foreach (var texId in TexUsers.Keys.Where(t => !t.StartsWith("twilightforest:", StringComparison.Ordinal)).OrderBy(x => x, StringComparer.Ordinal))
            {
                string owner = TextureOwner(texId);
                var entry = J.Obj().Set("textures", TexPath(texId));
                var atl = TexAtlases[texId];
                if (atl.Contains("terrain")) Fragment("textures", owner, "rp/terrain_texture.d/import-" + NameOf(owner) + ".json", TexKey(texId), entry);
                if (atl.Contains("item")) Fragment("textures", owner, "rp/item_texture.d/import-" + NameOf(owner) + ".json", TexKey(texId), entry);
            }
            foreach (var kv in flip)
            {
                string unitId = UnitNames.First(u => u.Value == kv.Key).Key;
                EmitJson("textures", unitId, "rp/flipbook_textures.d/import-" + kv.Key + ".json", J.Arr(kv.Value));
            }
        }

        /// <summary>The owner of a texture: its users; unused textures by their path (entity / gui / ...).</summary>
        public string TextureOwner(string texId)
        {
            SortedSet<string> users;
            if (TexUsers.TryGetValue(texId, out users) && users.Count > 0) return SharedOwner(users);
            string path = texId.Substring(texId.IndexOf(':') + 1);
            var parts = path.Split('/');
            string name = parts.Last();
            // textures/entity/<entity>[/...] and textures/item|block/<id>
            if (parts[0] == "entity" && parts.Length > 1)
            {
                foreach (var cand in new[] { parts[1], name, StripSuffixes(name) })
                {
                    var o = OwnerOf("entity", "twilightforest:" + cand);
                    if (o != null) return o;
                }
            }
            foreach (var cand in new[] { name, StripSuffixes(name) })
            {
                var o = ItemOwner("twilightforest:" + cand);
                if (o != null) return o;
            }
            return ImportUnit;
        }

        static string StripSuffixes(string n)
        {
            foreach (var s in new[] { "_overlay", "_top", "_side", "_bottom", "_front", "_back", "_end", "_inner", "_outer", "_glow", "_active", "_inactive", "_open", "_closed", "_on", "_off", "_lit", "_eyes", "_saddle", "_armor", "_layer_1", "_layer_2" })
                if (n.EndsWith(s, StringComparison.Ordinal)) return n.Substring(0, n.Length - s.Length);
            return n;
        }

        // ---- chest boats ------------------------------------------------------------------------------

        /// <summary>
        /// TF's TextureGeneratorReloadListener copies the chest region (x 0..48, y 58..96 of a 128-wide
        /// texture) of the vanilla oak chest boat into TF's chest boat textures when that region is
        /// empty. The vanilla texture is Mojang's (not redistributable), so the region is painted from
        /// TF's own twilight oak chest texture instead: each face of the boat's chest boxes (lid 12x4x12
        /// at uv 0,59, bottom 12x8x12 at uv 0,76, lock 2x2x1 at uv 0,59) is sampled from the matching
        /// face of the chest model boxes (lid 14x5x14 at 0,0, bottom 14x10x14 at 0,19, lock 2x4x1 at 0,0).
        /// </summary>
        Dictionary<string, byte[]> ChestBoatTextures()
        {
            var res = new Dictionary<string, byte[]>(StringComparer.Ordinal);
            string chestId = "twilightforest:entity/chest/twilight/single";
            string chestFile;
            if (!TfTextures.TryGetValue(chestId, out chestFile)) { Warn("chest boats: " + chestId + " missing"); return res; }
            var chest = Png.Decode(File.ReadAllBytes(chestFile));
            foreach (var kv in TfTextures.Where(k => k.Key.StartsWith("twilightforest:entity/chest_boat/", StringComparison.Ordinal)).OrderBy(k => k.Key, StringComparer.Ordinal))
            {
                var img = Png.Decode(File.ReadAllBytes(kv.Value));
                int scale = img.W / 128;
                if (scale < 1) continue;
                bool empty = true;
                for (int x = 0; x < 48 * scale && empty; x++)
                    for (int y = 58 * scale; y < 96 * scale && y < img.H; y++)
                        if (img.Get(x, y) != 0) { empty = false; break; }
                if (!empty) continue;
                int cs = chest.W / 64;
                // lid, bottom, lock: (boat u, v, w, h, d) <- (chest u, v, w, h, d)
                PaintBox(img, scale, 0, 59, 12, 4, 12, chest, cs, 0, 0, 14, 5, 14);
                PaintBox(img, scale, 0, 76, 12, 8, 12, chest, cs, 0, 19, 14, 10, 14);
                PaintBox(img, scale, 0, 59, 2, 2, 1, chest, cs, 0, 0, 2, 4, 1);
                res[kv.Key] = Png.Encode(img);
            }
            return res;
        }

        // Java box UV layout at (u, v) for a w x h x d box: row 1 (height d): top at u+d, bottom at u+d+w;
        // row 2 (height h): right u, front u+d, left u+d+w, back u+2d+w.
        static void PaintBox(Image dst, int ds, int du, int dv, int dw, int dh, int dd, Image src, int ss, int su, int sv, int sw, int sh, int sd)
        {
            var dstRects = BoxFaces(du, dv, dw, dh, dd);
            var srcRects = BoxFaces(su, sv, sw, sh, sd);
            for (int f = 0; f < dstRects.Length; f++)
            {
                var d = dstRects[f]; var s = srcRects[f];
                for (int y = 0; y < d[3] * ds; y++)
                    for (int x = 0; x < d[2] * ds; x++)
                    {
                        int sx = (s[0] * ss) + (x * s[2] * ss) / (d[2] * ds);
                        int sy = (s[1] * ss) + (y * s[3] * ss) / (d[3] * ds);
                        int px = d[0] * ds + x, py = d[1] * ds + y;
                        if (px < dst.W && py < dst.H && sx < src.W && sy < src.H) dst.Set(px, py, src.Get(sx, sy));
                    }
            }
        }

        static int[][] BoxFaces(int u, int v, int w, int h, int d)
        {
            return new[]
            {
                new[] { u + d, v, w, d },             // top
                new[] { u + d + w, v, w, d },         // bottom
                new[] { u, v + d, d, h },             // right
                new[] { u + d, v + d, w, h },         // front
                new[] { u + d + w, v + d, d, h },     // left
                new[] { u + 2 * d + w, v + d, w, h }, // back
            };
        }

        // ---- vanilla textures --------------------------------------------------------------------------

        /// <summary>Java vanilla texture paths TF models use -> the game's own Bedrock texture paths (never copied).</summary>
        public static readonly Dictionary<string, string> VanillaTextures = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            { "block/oak_planks", "textures/blocks/planks_oak" },
            { "block/spruce_planks", "textures/blocks/planks_spruce" },
            { "block/birch_planks", "textures/blocks/planks_birch" },
            { "block/jungle_planks", "textures/blocks/planks_jungle" },
            { "block/acacia_planks", "textures/blocks/planks_acacia" },
            { "block/dark_oak_planks", "textures/blocks/planks_big_oak" },
            { "block/mangrove_planks", "textures/blocks/mangrove_planks" },
            { "block/cherry_planks", "textures/blocks/cherry_planks" },
            { "block/bamboo_planks", "textures/blocks/bamboo_planks" },
            { "block/crimson_planks", "textures/blocks/huge_fungus/crimson_planks" },
            { "block/warped_planks", "textures/blocks/huge_fungus/warped_planks" },
            { "block/oak_log", "textures/blocks/log_oak" },
            { "block/oak_log_top", "textures/blocks/log_oak_top" },
            { "block/stone", "textures/blocks/stone" },
            { "block/cobblestone", "textures/blocks/cobblestone" },
            { "block/mossy_cobblestone", "textures/blocks/cobblestone_mossy" },
            { "block/stone_bricks", "textures/blocks/stonebrick" },
            { "block/mossy_stone_bricks", "textures/blocks/stonebrick_mossy" },
            { "block/cracked_stone_bricks", "textures/blocks/stonebrick_cracked" },
            { "block/chiseled_stone_bricks", "textures/blocks/stonebrick_carved" },
            { "block/obsidian", "textures/blocks/obsidian" },
            { "block/gold_block", "textures/blocks/gold_block" },
            { "block/diamond_block", "textures/blocks/diamond_block" },
            { "block/iron_block", "textures/blocks/iron_block" },
            { "block/dirt", "textures/blocks/dirt" },
            { "block/coarse_dirt", "textures/blocks/coarse_dirt" },
            { "block/podzol_top", "textures/blocks/dirt_podzol_top" },
            { "block/grass_block_top", "textures/blocks/grass_top" },
            { "block/grass_block_side", "textures/blocks/grass_side_carried" },
            { "block/flower_pot", "textures/blocks/flower_pot" },
            { "block/spawner", "textures/blocks/mob_spawner" },
            { "block/slime_block", "textures/blocks/slime" },
            { "block/oak_leaves", "textures/blocks/leaves_oak_carried" },
            { "block/azalea_leaves", "textures/blocks/azalea_leaves" },
            { "block/furnace_front", "textures/blocks/furnace_front_off" },
            { "block/furnace_front_on", "textures/blocks/furnace_front_on" },
            { "block/furnace_side", "textures/blocks/furnace_side" },
            { "block/furnace_top", "textures/blocks/furnace_top" },
            { "block/chiseled_bookshelf_empty", "textures/blocks/chiseled_bookshelf_empty" },
            { "block/chiseled_bookshelf_side", "textures/blocks/chiseled_bookshelf_side" },
            { "block/chiseled_bookshelf_top", "textures/blocks/chiseled_bookshelf_top" },
            { "block/chiseled_bookshelf_occupied", "textures/blocks/chiseled_bookshelf_occupied" },
            { "block/bookshelf", "textures/blocks/bookshelf" },
            { "block/glass", "textures/blocks/glass" },
            { "block/ice", "textures/blocks/ice" },
            { "block/packed_ice", "textures/blocks/ice_packed" },
            { "block/blue_ice", "textures/blocks/blue_ice" },
            { "block/snow", "textures/blocks/snow" },
            { "block/sand", "textures/blocks/sand" },
            { "block/gravel", "textures/blocks/gravel" },
            { "block/moss_block", "textures/blocks/moss_block" },
            { "block/vine", "textures/blocks/vine_carried" },
            { "block/fire_0", "textures/blocks/fire_0" },
            { "block/fire_1", "textures/blocks/fire_1" },
            { "block/soul_fire_0", "textures/blocks/soul_fire_0" },
            { "block/lantern", "textures/blocks/lantern" },
            { "block/chain", "textures/blocks/chain1" },
            { "block/iron_bars", "textures/blocks/iron_bars" },
            { "block/redstone_block", "textures/blocks/redstone_block" },
            { "block/netherrack", "textures/blocks/netherrack" },
            { "block/deepslate", "textures/blocks/deepslate/deepslate" },
            { "block/water_still", "textures/blocks/water_still_grey" },
            { "block/lava_still", "textures/blocks/lava_still" },
            { "block/torch", "textures/blocks/torch_on" },
            { "block/red_wool", "textures/blocks/wool_colored_red" },
            { "block/white_wool", "textures/blocks/wool_colored_white" },
            { "block/hay_block_side", "textures/blocks/hay_block_side" },
            { "block/hay_block_top", "textures/blocks/hay_block_top" },
            { "block/candle", "textures/blocks/candles/candle" },
            { "block/stripped_oak_log", "textures/blocks/stripped_oak_log" },
            { "block/acacia_log", "textures/blocks/log_acacia" }, { "block/acacia_log_top", "textures/blocks/log_acacia_top" },
            { "block/birch_log", "textures/blocks/log_birch" }, { "block/birch_log_top", "textures/blocks/log_birch_top" },
            { "block/spruce_log", "textures/blocks/log_spruce" }, { "block/spruce_log_top", "textures/blocks/log_spruce_top" },
            { "block/jungle_log", "textures/blocks/log_jungle" }, { "block/jungle_log_top", "textures/blocks/log_jungle_top" },
            { "block/dark_oak_log", "textures/blocks/log_big_oak" }, { "block/dark_oak_log_top", "textures/blocks/log_big_oak_top" },
            { "block/mangrove_log", "textures/blocks/mangrove_log_side" }, { "block/mangrove_log_top", "textures/blocks/mangrove_log_top" },
            { "block/cherry_log", "textures/blocks/cherry_log_side" }, { "block/cherry_log_top", "textures/blocks/cherry_log_top" },
            { "block/crimson_stem", "textures/blocks/huge_fungus/crimson_log_side" }, { "block/crimson_stem_top", "textures/blocks/huge_fungus/crimson_log_top" },
            { "block/warped_stem", "textures/blocks/huge_fungus/warped_stem_side" }, { "block/warped_stem_top", "textures/blocks/huge_fungus/warped_stem_top" },
            { "block/stripped_acacia_log", "textures/blocks/stripped_acacia_log" }, { "block/stripped_birch_log", "textures/blocks/stripped_birch_log" },
            { "block/stripped_spruce_log", "textures/blocks/stripped_spruce_log" }, { "block/stripped_jungle_log", "textures/blocks/stripped_jungle_log" },
            { "block/stripped_dark_oak_log", "textures/blocks/stripped_dark_oak_log" },
            { "block/stripped_mangrove_log", "textures/blocks/stripped_mangrove_log_side" },
            { "block/stripped_cherry_log", "textures/blocks/stripped_cherry_log_side" },
            { "block/stripped_crimson_stem", "textures/blocks/huge_fungus/stripped_crimson_stem_side" },
            { "block/stripped_warped_stem", "textures/blocks/huge_fungus/stripped_warped_stem_side" },
            { "block/birch_leaves", "textures/blocks/leaves_birch_carried" }, { "block/spruce_leaves", "textures/blocks/leaves_spruce_carried" },
            { "block/basalt_side", "textures/blocks/basalt_side" }, { "block/basalt_top", "textures/blocks/basalt_top" },
            { "block/black_concrete_powder", "textures/blocks/concrete_powder_black" },
            { "block/brown_mushroom_block", "textures/blocks/mushroom_block_skin_brown" }, { "block/red_mushroom_block", "textures/blocks/mushroom_block_skin_red" },
            { "block/candle_lit", "textures/blocks/candles/candle_lit" },
            { "block/composter_compost", "textures/blocks/compost" },
            { "block/grass_block_side_overlay", "textures/blocks/grass_side_carried" },
            { "block/ladder", "textures/blocks/ladder" },
            { "block/mycelium_top", "textures/blocks/mycelium_top" },
            { "block/nether_portal", "textures/blocks/portal" },
            { "block/netherite_block", "textures/blocks/netherite_block" },
            { "block/poppy", "textures/blocks/flower_rose" },
            { "block/short_grass", "textures/blocks/tallgrass_carried" },
            { "block/smooth_stone", "textures/blocks/stone_slab_top" }, { "block/smooth_stone_slab_side", "textures/blocks/stone_slab_side" },
            { "block/soul_sand", "textures/blocks/soul_sand" },
            { "item/bowl", "textures/items/bowl" },
            { "item/bread", "textures/items/bread" },
            { "item/oak_chest_boat", "textures/items/chest_boat_oak" },
            { "item/stick", "textures/items/stick" },
            { "item/bow", "textures/items/bow_standby" },
            { "item/goat_horn", "textures/items/goat_horn" },
            { "item/stone_sword", "textures/items/stone_sword" },
            { "item/stone_pickaxe", "textures/items/stone_pickaxe" },
            { "item/spawn_egg", "textures/items/spawn_egg" },
            { "item/spawn_egg_overlay", "textures/items/spawn_egg_overlay" },
            { "item/barrier", "textures/blocks/barrier" },
            { "item/paper", "textures/items/paper" },
            { "item/book", "textures/items/book_normal" },
            { "item/filled_map", "textures/items/map_filled" },
            { "item/map", "textures/items/map_empty" },
            { "item/ender_eye", "textures/items/ender_eye" },
            { "item/glass_bottle", "textures/items/potion_bottle_empty" },
            { "item/feather", "textures/items/feather" },
            { "item/string", "textures/items/string" },
        };
    }
}
