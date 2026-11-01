// GloamingForest import tool (contract "import", unit F1): the content skeleton (items, block items with
// 2D icons, entities with placeholder visuals and spawn eggs), the creative catalog (TFCreativeTabs),
// block families, particle and sound-event skeletons, and the id list the in-game selftests check.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;

namespace GfImport
{
    public sealed partial class Ctx
    {
        public const string ItemFormat = "1.21.60";
        public const string EntityFormat = "1.21.40";
        readonly SortedSet<string> skeletonItems = new SortedSet<string>(StringComparer.Ordinal);

        // item id (no namespace) -> TF item tags ("twilightforest:fiery_vial", "c:ingots/iron")
        public Dictionary<string, SortedSet<string>> ItemTags = new Dictionary<string, SortedSet<string>>(StringComparer.Ordinal);

        string ItemIcon(string id, Model m, string owner)
        {
            string tex = null;
            if (m != null && m.ItemGenerated) tex = m.Resolve("#layer0");
            if (tex == null && TfTextures.ContainsKey("twilightforest:item/" + id)) tex = "twilightforest:item/" + id;
            if (tex == null && m != null) tex = m.Particle;
            return tex == null ? null : UseTexture(tex, owner, "item");
        }

        Model ItemModel(string id)
        {
            string mid = "twilightforest:item/" + id;
            return ModelLib.Raw.ContainsKey(mid) ? ModelLib.Get(mid) : null;
        }

        J ItemDoc(string id, string owner, Model m, JItem ji, bool blockPlacer, bool inCatalog, string nameKey)
        {
            var desc = J.Obj().Set("identifier", "twilightforest:" + id);
            desc.Set("menu_category", J.Obj().Set("category", inCatalog ? CatalogCategory("twilightforest:" + id) : "none"));
            var comps = J.Obj();
            string icon = ItemIcon(id, m, owner);
            if (icon != null) comps.Set("minecraft:icon", J.Obj().Set("textures", J.Obj().Set("default", icon)));
            comps.Set("minecraft:display_name", J.Obj().Set("value", nameKey));
            int stack = ji != null ? ji.StackSize : 64;
            if (stack != 64) comps.Set("minecraft:max_stack_size", stack);
            if (ji != null && ji.Durability > 0) comps.Set("minecraft:durability", J.Obj().Set("max_durability", ji.Durability));
            if (m != null && m.Chain.Contains("minecraft:item/handheld")) comps.Set("minecraft:hand_equipped", true);
            if (blockPlacer) comps.Set("minecraft:block_placer", J.Obj().Set("block", "twilightforest:" + id).Set("replace_block_item", true));
            // the TF item tags it is in (recipes use them: only TF items can carry tags on Bedrock)
            SortedSet<string> tags;
            // (minecraft-namespace tags carry engine meaning on Bedrock, e.g. trim materials: left to the owners)
            if (!blockPlacer && ItemTags.TryGetValue(id, out tags))
            {
                var own = tags.Where(t => !t.StartsWith("minecraft:", StringComparison.Ordinal)).ToList();
                if (own.Count > 0) comps.Set("minecraft:tags", J.Obj().Set("tags", J.Strs(own)));
            }
            return J.Obj().Set("format_version", ItemFormat).Set("minecraft:item", J.Obj().Set("description", desc).Set("components", comps));
        }

        public void ImportItems()
        {
            var inTabs = new HashSet<string>(Tabs.SelectMany(t => t.Items), StringComparer.Ordinal);
            // items registered in TFItems (some share their id with the block they place)
            foreach (var full in Ids("item"))
            {
                string id = full.Substring("twilightforest:".Length);
                string owner = OwnerOf("item", full);
                JItem ji;
                JItems.TryGetValue(id, out ji);
                bool isBlock = BlockInfos.ContainsKey(id);
                var m = ItemModel(id);
                skeletonItems.Add(full);
                // an item with its block's id replaces the block's own item only for a flat (2D) icon;
                // otherwise Bedrock's block item (the block geometry) is the item
                if (isBlock && (m == null || !m.ItemGenerated || m.Resolve("#layer0") == null)) continue;
                EmitJson("skeleton", owner, "bp/items/" + id + ".json", ItemDoc(id, owner, m, ji, isBlock, inTabs.Contains(full), (isBlock ? "tile." : "item.") + full + ".name"));
            }
            // block items whose Java item model is a flat icon get an item with the block's id (block_placer)
            foreach (var full in Ids("block_item"))
            {
                string id = full.Substring("twilightforest:".Length);
                BlockInfo bi;
                if (!BlockInfos.TryGetValue(id, out bi)) continue;
                var m = ItemModel(id);
                bi.ItemModel = m;
                if (m == null || !m.ItemGenerated || m.Resolve("#layer0") == null) continue;
                bi.FlatItem = true;
                JItem ji;
                JItems.TryGetValue(id, out ji);
                EmitJson("skeleton", bi.Owner, "bp/items/" + id + ".json", ItemDoc(id, bi.Owner, m, ji, true, bi.InCatalog, "tile." + full + ".name"));
                skeletonItems.Add(full);
            }
        }

        // ---- entities ------------------------------------------------------------------------------------

        double EntityHealth(JEntity e)
        {
            if (e.Cls == null) return 0;
            foreach (var cls in Java.ClassChain(this, e.Cls, extendsCache))
            {
                var file = GfImport.Java.ClassFile(this, cls);
                if (file == null) continue;
                var text = Java.StripComments(ReadText(file));
                var m = Regex.Match(text, @"Attributes\.MAX_HEALTH,\s*([\w.]+)");
                if (!m.Success) continue;
                double v;
                string s = m.Groups[1].Value.TrimEnd('D', 'd', 'F', 'f');
                if (double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out v)) return v;
                var c = Regex.Match(text, @"\b" + Regex.Escape(m.Groups[1].Value) + @"\s*=\s*([\d.]+)");
                if (c.Success && double.TryParse(c.Groups[1].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out v)) return v;
            }
            return 0;
        }

        List<string> entityTextures, topEntityTextures;

        string EntityTexture(string id)
        {
            string squash = id.Replace("_", "");
            if (entityTextures == null)
            {
                entityTextures = TfTextures.Keys.Where(k => k.StartsWith("twilightforest:entity/", StringComparison.Ordinal)).OrderBy(k => k, StringComparer.Ordinal).ToList();
                // entity textures sit directly in textures/entity (or in a folder named after the entity);
                // banner, boat, chest, shield and sign folders hold block / item textures
                topEntityTextures = entityTextures.Where(k => k.IndexOf('/', "twilightforest:entity/".Length) < 0).ToList();
            }
            var all = entityTextures;
            var top = topEntityTextures;
            Func<string, string> sq = k => k.Substring(k.LastIndexOf('/') + 1).Replace("_", "").Replace("-", "");
            var hit = all.FirstOrDefault(k => k == "twilightforest:entity/" + id)
                ?? all.FirstOrDefault(k => k.StartsWith("twilightforest:entity/" + id + "/", StringComparison.Ordinal))
                ?? top.FirstOrDefault(k => sq(k) == squash)
                ?? top.FirstOrDefault(k => sq(k).StartsWith(squash, StringComparison.Ordinal))
                ?? top.FirstOrDefault(k => sq(k).Contains(squash));
            return hit;
        }

        public void ImportEntities()
        {
            foreach (var full in Ids("entity"))
            {
                string id = full.Substring("twilightforest:".Length);
                string owner = OwnerOf("entity", full);
                JEntity e;
                if (!JEntities.TryGetValue(id, out e)) { Warn("entity " + id + " not found in TFEntities.java"); e = new JEntity { Id = id }; }
                // TF registers a spawn egg only for the entities owners.tsv lists one for (make(..., 0, 0) has none)
                e.HasEgg = OwnerOf("spawn_egg", full + "_spawn_egg") != null;
                var comps = J.Obj();
                var fam = J.Arr().Add("twilightforest").Add(id);
                string cat = (e.Category ?? "MISC").ToLowerInvariant();
                if (cat == "monster") fam.Add("monster");
                else if (cat == "creature") fam.Add("creature");
                else if (cat == "ambient") fam.Add("ambient");
                if (cat != "misc") fam.Add("mob");
                comps.Set("minecraft:type_family", J.Obj().Set("family", fam));
                comps.Set("minecraft:collision_box", J.Obj().Set("width", Math.Max(0.1, Math.Round(e.Width, 4))).Set("height", Math.Max(0.1, Math.Round(e.Height, 4))));
                double hp = EntityHealth(e);
                if (hp > 0) comps.Set("minecraft:health", J.Obj().Set("value", hp).Set("max", hp));
                comps.Set("minecraft:physics", J.Obj());
                comps.Set("minecraft:pushable", J.Obj().Set("is_pushable", true).Set("is_pushable_by_piston", true));
                if (e.FireImmune) comps.Set("minecraft:fire_immune", true);
                var desc = J.Obj().Set("identifier", full).Set("is_spawnable", e.HasEgg).Set("is_summonable", true).Set("is_experimental", false);
                EmitJson("skeleton", owner, "bp/entities/" + id + ".json",
                    J.Obj().Set("format_version", EntityFormat).Set("minecraft:entity", J.Obj().Set("description", desc).Set("components", comps)));

                // client entity: a placeholder box of the entity's size with its TF texture (F2 / the owner replace it)
                string tex = EntityTexture(id);
                int tw = 64, th = 32;
                if (tex != null)
                {
                    UseTexture(tex, owner, "path");
                    int w, h;
                    if (Png.TrySize(File.ReadAllBytes(TfTextures[tex]), out w, out h)) { tw = w; th = h; }
                }
                string geoId = "geometry.tf.skeleton." + id;
                double bw = Math.Max(1, Math.Round(e.Width * 16)), bh = Math.Max(1, Math.Round(e.Height * 16));
                var cube = J.Obj().Set("origin", J.Arr(new[] { J.Num(-bw / 2), J.Num(0), J.Num(-bw / 2) }))
                    .Set("size", J.Arr(new[] { J.Num(bw), J.Num(bh), J.Num(bw) }))
                    .Set("uv", J.Arr(new[] { J.Num(0), J.Num(0) }));
                var bone = J.Obj().Set("name", "body").Set("pivot", J.Arr(new[] { J.Num(0), J.Num(0), J.Num(0) })).Set("cubes", J.Arr(new[] { cube }));
                var gdesc = J.Obj().Set("identifier", geoId).Set("texture_width", tw).Set("texture_height", th)
                    .Set("visible_bounds_width", Math.Ceiling(e.Width + 1)).Set("visible_bounds_height", Math.Ceiling(e.Height + 1))
                    .Set("visible_bounds_offset", J.Arr(new[] { J.Num(0), J.Num(Math.Round(e.Height / 2, 2)), J.Num(0) }));
                EmitJson("skeleton", owner, "rp/models/entity/skeleton/" + id + ".geo.json",
                    J.Obj().Set("format_version", "1.12.0").Set("minecraft:geometry", J.Arr(new[] { J.Obj().Set("description", gdesc).Set("bones", J.Arr(new[] { bone })) })));
                var cdesc = J.Obj().Set("identifier", full)
                    .Set("materials", J.Obj().Set("default", "entity_alphatest"))
                    .Set("textures", J.Obj().Set("default", tex != null ? TexPath(tex) : "textures/misc/missing_texture"))
                    .Set("geometry", J.Obj().Set("default", geoId))
                    .Set("render_controllers", J.Arr().Add("controller.render.default"));
                if (e.HasEgg)
                    cdesc.Set("spawn_egg", J.Obj().Set("base_color", "#" + e.EggPrimary.ToString("x6", CultureInfo.InvariantCulture)).Set("overlay_color", "#" + e.EggSecondary.ToString("x6", CultureInfo.InvariantCulture)));
                EmitJson("skeleton", owner, "rp/entity/" + id + ".entity.json",
                    J.Obj().Set("format_version", "1.10.0").Set("minecraft:client_entity", J.Obj().Set("description", cdesc)));
            }
        }

        // ---- creative catalog ----------------------------------------------------------------------------

        // TF's 4 tabs -> Bedrock catalog groups (the closest Bedrock category for each)
        static readonly Dictionary<string, string> TabCategory = new Dictionary<string, string>
        {
            { "twilightforest:blocks", "construction" }, { "twilightforest:items", "items" }, { "twilightforest:equipment", "equipment" }, { "twilightforest:food", "items" },
        };

        /// <summary>The Bedrock creative category of an item listed in a TF tab (its first tab).</summary>
        public string CatalogCategory(string full)
        {
            var tab = Tabs.FirstOrDefault(t => t.Items.Contains(full));
            string c;
            return tab != null && TabCategory.TryGetValue(tab.Id, out c) ? c : "none";
        }

        public void ImportCatalog()
        {
            var category = TabCategory;
            var cats = new List<J>();
            var data = J.Arr();
            foreach (var catName in new[] { "construction", "equipment", "items", "nature" })
            {
                var groups = J.Arr();
                foreach (var tab in Tabs.Where(t => category.ContainsKey(t.Id) && category[t.Id] == catName))
                {
                    var items = tab.Items.Where(ItemExists).ToList();
                    string icon = tab.Icon != null && ItemExists(tab.Icon) ? tab.Icon : items.FirstOrDefault();
                    var gid = J.Obj().Set("name", "twilightforest:" + (tab.TitleKey ?? tab.Id));
                    if (icon != null) gid.Set("icon", icon);
                    groups.Add(J.Obj().Set("group_identifier", gid).Set("items", J.Strs(items)));
                }
                if (groups.Count > 0) cats.Add(J.Obj().Set("category_name", catName).Set("groups", groups));
            }
            foreach (var tab in Tabs)
            {
                data.Add(J.Obj().Set("id", tab.Id).Set("iconItem", tab.Icon ?? "").Set("nameKey", tab.TitleKey ?? "")
                    .Set("category", category.ContainsKey(tab.Id) ? category[tab.Id] : "items").Set("items", J.Strs(tab.Items)));
            }
            EmitJson("catalog", ImportUnit, "bp/item_catalog.d/import.json", J.Arr(cats));
            EmitJson("catalog", ImportUnit, "data/gf/catalog/import.json", data);
        }

        bool ItemExists(string full)
        {
            return skeletonItems.Contains(full) || OwnerOf("block", full) != null || OwnerOf("spawn_egg", full) != null || OwnerOf("item", full) != null;
        }

        // ---- families -------------------------------------------------------------------------------------

        static readonly string[][] FamilyClasses =
        {
            new[] { "StairBlock", "stairs" }, new[] { "SlabBlock", "slab" }, new[] { "FenceGateBlock", "fence_gate" }, new[] { "FenceBlock", "fence" },
            new[] { "WallBlock", "wall" }, new[] { "DoorBlock", "door" }, new[] { "TrapDoorBlock", "trapdoor" }, new[] { "ButtonBlock", "button" },
            new[] { "PressurePlateBlock", "pressure_plate" }, new[] { "WeightedPressurePlateBlock", "pressure_plate" }, new[] { "IronBarsBlock", "pane" },
            new[] { "LeavesBlock", "leaves" }, new[] { "SaplingBlock", "sapling" }, new[] { "FlowerPotBlock", "potted" },
            new[] { "StandingSignBlock", "sign" }, new[] { "WallSignBlock", "sign" }, new[] { "CeilingHangingSignBlock", "hanging_sign" },
            new[] { "WallHangingSignBlock", "hanging_sign" }, new[] { "TrappedChestBlock", "trapped_chest" }, new[] { "ChestBlock", "chest" },
            new[] { "RotatedPillarBlock", "pillar" }, new[] { "HorizontalDirectionalBlock", "horizontal" }, new[] { "DirectionalBlock", "directional" },
        };

        public void ImportFamilies()
        {
            var perUnit = new SortedDictionary<string, J>(StringComparer.Ordinal);
            foreach (var bi in BlockInfos.Values)
            {
                string fam = null;
                foreach (var c in bi.Chain)
                {
                    if (c.Contains("Trapped") && bi.Chain.Any(x => x.Contains("Chest"))) { fam = "trapped_chest"; break; }
                    var hit = FamilyClasses.FirstOrDefault(f => f[0] == c);
                    if (hit != null) { fam = hit[1]; break; }
                }
                if (fam == "pillar" && Regex.IsMatch(bi.Id, "_(log|wood|stem|hyphae)$")) fam = bi.Id.EndsWith("_wood", StringComparison.Ordinal) ? "wood" : "log";
                if (fam == null && bi.Id.StartsWith("potted_", StringComparison.Ordinal)) fam = "potted";
                if (fam == null) continue;
                bi.Family = fam;
                var p = J.Obj().Set("class", bi.Chain[0]);
                if (bi.Chain.Count > 1) p.Set("vanillaClass", bi.Chain.Last());
                var bm = Regex.Match(bi.J.Stmt ?? "", @"new\s+\w*Stair\w*\(\s*(\w+)\.get\(\)\.defaultBlockState\(\)");
                if (bm.Success)
                {
                    var src = JBlocks.Values.FirstOrDefault(b => b.Const == bm.Groups[1].Value);
                    if (src != null) p.Set("base", "twilightforest:" + src.Id);
                }
                var cm = Regex.Match(bi.J.Stmt ?? "", @"of(?:Full|Legacy)Copy\(\s*(\w+)\.get\(\)");
                if (cm.Success && !p.Has("base"))
                {
                    var src = JBlocks.Values.FirstOrDefault(b => b.Const == cm.Groups[1].Value);
                    if (src != null) p.Set("copyOf", "twilightforest:" + src.Id);
                }
                var wm = Regex.Match(bi.J.Stmt ?? "", @"TFWoodTypes\.(\w+)");
                if (wm.Success) p.Set("woodType", wm.Groups[1].Value.ToLowerInvariant());
                var pot = Regex.Match(bi.J.Stmt ?? "", @"SpecialFlowerPotBlock\([^,]+,\s*(\w+)\s*,");
                if (pot.Success)
                {
                    var src = JBlocks.Values.FirstOrDefault(b => b.Const == pot.Groups[1].Value);
                    if (src != null) p.Set("plant", "twilightforest:" + src.Id);
                }
                // Java property names; the Bedrock state is "twilightforest:<name>" (docs/spec/blockstates.md)
                if (bi.States.Count > 0) p.Set("states", J.Strs(bi.States.Select(s => s.Java)));
                J arr;
                string unit = NameOf(bi.Owner);
                if (!perUnit.TryGetValue(bi.Owner, out arr)) perUnit[bi.Owner] = arr = J.Arr();
                arr.Add(J.Obj().Set("id", "twilightforest:" + bi.Id).Set("family", fam).Set("params", p));
            }
            foreach (var kv in perUnit) EmitJson("families", kv.Key, "data/gf/families/" + NameOf(kv.Key) + ".json", kv.Value);
        }

        // ---- particles ------------------------------------------------------------------------------------

        public void ImportParticles()
        {
            string dir = Path.Combine(GenRes, "assets", "twilightforest", "particles");
            foreach (var rel in Files(dir, "*.json"))
            {
                string id = StripExt(rel);
                string full = "twilightforest:" + id;
                string owner = OwnerOf("particle", full) ?? ImportUnit;
                var j = ReadJson(Path.Combine(dir, rel));
                var texs = j["textures"] != null ? j["textures"].A.Select(t => t.AsString()).ToList() : new List<string>();
                string first = texs.Select(t => (t.Contains(":") ? t : "minecraft:" + t)).Select(t => t.Insert(t.IndexOf(':') + 1, "particle/")).FirstOrDefault();
                string path = null;
                int w = 8, h = 8;
                if (first != null && first.StartsWith("twilightforest:", StringComparison.Ordinal) && TfTextures.ContainsKey(first))
                {
                    UseTexture(first, owner, "path");
                    path = TexPath(first);
                    int pw, ph;
                    if (Png.TrySize(File.ReadAllBytes(TfTextures[first]), out pw, out ph)) { w = pw; h = Math.Min(ph, pw); }
                }
                var desc = J.Obj().Set("identifier", full).Set("basic_render_parameters", J.Obj().Set("material", "particles_alpha").Set("texture", path ?? "textures/particle/particles"));
                var comps = J.Obj()
                    .Set("minecraft:emitter_rate_instant", J.Obj().Set("num_particles", 1))
                    .Set("minecraft:emitter_lifetime_once", J.Obj().Set("active_time", 0))
                    .Set("minecraft:emitter_shape_point", J.Obj())
                    .Set("minecraft:particle_lifetime_expression", J.Obj().Set("max_lifetime", 1))
                    .Set("minecraft:particle_initial_speed", 0)
                    .Set("minecraft:particle_motion_dynamic", J.Obj())
                    .Set("minecraft:particle_appearance_billboard", J.Obj()
                        .Set("size", J.Arr(new[] { J.Num(0.1), J.Num(0.1) }))
                        .Set("facing_camera_mode", "rotate_xyz")
                        .Set("uv", J.Obj().Set("texture_width", w).Set("texture_height", h)
                            .Set("uv", J.Arr(new[] { J.Num(0), J.Num(0) })).Set("uv_size", J.Arr(new[] { J.Num(w), J.Num(h) }))));
                EmitJson("particles", owner, "rp/particles/" + id + ".json",
                    J.Obj().Set("format_version", "1.10.0").Set("particle_effect", J.Obj().Set("description", desc).Set("components", comps)));
                // the Java description (every frame, as resource pack paths) for the native particle factory (F12) and the owner
                var frames = texs.Select(t => t.Contains(":") ? t : "minecraft:" + t).Select(t => t.Insert(t.IndexOf(':') + 1, "particle/"))
                    .Select(t => t.StartsWith("twilightforest:", StringComparison.Ordinal) ? TexPath(t) : "minecraft:" + t.Substring(t.IndexOf(':') + 1));
                Fragment("particles", owner, "data/gf/particles/" + NameOf(owner) + ".json", full, J.Obj().Set("textures", J.Strs(frames)));
            }
        }

        // ---- sound events -----------------------------------------------------------------------------------

        public void ImportSounds()
        {
            var sj = ReadJson(Path.Combine(GenRes, "assets", "twilightforest", "sounds.json"));
            foreach (var ev in sj.Keys)
            {
                string owner = OwnerOf("sound_event", ev) ?? ImportUnit;
                var src = sj[ev];
                var entry = J.Obj().Set("category", SoundCategory(ev));
                if (src["subtitle"] != null) entry.Set("subtitle", src["subtitle"].AsString());
                var sounds = J.Arr();
                if (src["sounds"] != null)
                    foreach (var s in src["sounds"].A)
                    {
                        // names only: TF's .ogg files are never opened (rule R10); "file" is the path below
                        // TF's sounds/ directory (TF namespace), "event" a vanilla Java sound event
                        if (s.IsStr) sounds.Add(J.Obj().Set("file", SoundFile(s.AsString())));
                        else
                        {
                            var o = J.Obj();
                            if (s["type"] != null && s["type"].AsString() == "event") o.Set("event", s["name"].AsString());
                            else o.Set("file", SoundFile(s["name"].AsString()));
                            foreach (var k in new[] { "volume", "pitch", "weight", "stream", "attenuation_distance" })
                                if (s[k] != null) o.Set(k, s[k].Clone());
                            sounds.Add(o);
                        }
                    }
                entry.Set("sounds", sounds);
                Fragment("sounds", owner, "data/gf/sound_events/" + NameOf(owner) + ".json", "twilightforest:" + ev, entry);
            }
        }

        static string SoundFile(string name)
        {
            return name.StartsWith("twilightforest:", StringComparison.Ordinal) ? name.Substring("twilightforest:".Length) : name;
        }

        static string SoundCategory(string ev)
        {
            if (ev.StartsWith("music_disc.", StringComparison.Ordinal)) return "record";
            if (ev.StartsWith("music.", StringComparison.Ordinal)) return "music";
            if (ev.StartsWith("block.", StringComparison.Ordinal)) return "block";
            if (ev.StartsWith("item.", StringComparison.Ordinal)) return "player";
            if (ev.StartsWith("environment.", StringComparison.Ordinal) || ev.StartsWith("ambient.", StringComparison.Ordinal)) return "ambient";
            return "neutral";
        }

        // ---- the id list of the skeleton (for the import.* selftests) --------------------------------------

        public void ImportSkeletonIndex()
        {
            // blocks with their frozen states (the first value is the default state)
            var blocks = J.Obj();
            foreach (var bi in BlockInfos.Values)
            {
                var st = J.Obj();
                foreach (var s in bi.States) st.Set(s.Bedrock, J.Strs(s.Values));
                blocks.Set("twilightforest:" + bi.Id, st);
            }
            var data = J.Obj();
            foreach (var reg in new[] { "loot_table", "recipe", "advancement", "tags", "worldgen", "twilight", "data_maps" })
            {
                int n = Out.Values.SelectMany(u => u.Keys).Count(r => r.StartsWith("data/twilightforest/" + reg + "/", StringComparison.Ordinal));
                data.Set("twilightforest/" + reg, n);
            }
            var doc = J.Obj()
                .Set("blocks", blocks)
                .Set("data", data)
                .Set("items", J.Strs(skeletonItems))
                .Set("entities", J.Strs(Ids("entity")))
                .Set("spawnEggs", J.Strs(Ids("spawn_egg")));
            EmitJson("skeleton", ImportUnit, "data/gf/import/skeleton.json", doc);
        }
    }
}
