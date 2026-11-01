// GloamingForest import tool (contract "import", unit F1): the 523 TF blocks as Bedrock behaviour-pack
// blocks (frozen states, permutations from the Java blockstates, geometry and material instances from
// the Java models, hardness / light / map colour from TFBlocks), RP block sounds, the block geometry
// files and docs/spec/blockstates.md.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;

namespace GfImport
{
    public sealed class BState
    {
        public string Java;          // "facing"
        public string Bedrock;       // "twilightforest:facing"
        public string Type;          // bool | int | enum
        public List<string> Values = new List<string>();
        public string Source;        // blockstate | class
    }

    public sealed class BlockInfo
    {
        public string Id;            // "naga_stone"
        public string Owner;         // unit id
        public JBlock J;
        public List<string> Chain = new List<string>();
        public List<BState> States = new List<BState>();
        public long Permutations = 1;
        public string Family;        // families kind (stairs, slab, ...)
        public bool InCatalog;
        public Model ItemModel;
        public bool FlatItem;        // the item is a 2D icon (block_placer item)
        public string ItemIcon;      // item atlas key
        public string Particle;      // texture id
    }

    public sealed partial class Ctx
    {
        public readonly SortedDictionary<string, BlockInfo> BlockInfos = new SortedDictionary<string, BlockInfo>(StringComparer.Ordinal);
        public Models ModelLib;
        public Dictionary<string, JBlock> JBlocks;
        public Dictionary<string, JItem> JItems;
        public Dictionary<string, JEntity> JEntities;
        public List<Java.Tab> Tabs;
        readonly Dictionary<string, string> extendsCache = new Dictionary<string, string>(StringComparer.Ordinal);
        // geometry identifier -> (file JSON, users)
        readonly SortedDictionary<string, J> geometries = new SortedDictionary<string, J>(StringComparer.Ordinal);
        readonly Dictionary<string, SortedSet<string>> geometryUsers = new Dictionary<string, SortedSet<string>>(StringComparer.Ordinal);

        // the properties of the vanilla block classes TF blocks extend (Minecraft 1.21.1), default value first
        static readonly Dictionary<string, string[]> ClassStates = new Dictionary<string, string[]>(StringComparer.Ordinal)
        {
            { "LeavesBlock", new[] { "distance:int:7", "persistent:bool" } },
            { "SaplingBlock", new[] { "stage:int:1" } },
            { "StairBlock", new[] { "facing:enum:north,south,west,east", "half:enum:bottom,top", "shape:enum:straight,inner_left,inner_right,outer_left,outer_right" } },
            { "SlabBlock", new[] { "type:enum:bottom,top,double" } },
            { "FenceBlock", new[] { "north:bool", "east:bool", "south:bool", "west:bool" } },
            { "IronBarsBlock", new[] { "north:bool", "east:bool", "south:bool", "west:bool" } },
            { "FenceGateBlock", new[] { "facing:enum:north,south,west,east", "open:bool", "powered:bool", "in_wall:bool" } },
            { "WallBlock", new[] { "up:bool", "north_wall:enum:none,low,tall", "east_wall:enum:none,low,tall", "south_wall:enum:none,low,tall", "west_wall:enum:none,low,tall" } },
            { "DoorBlock", new[] { "facing:enum:north,south,west,east", "half:enum:lower,upper", "hinge:enum:left,right", "open:bool", "powered:bool" } },
            { "TrapDoorBlock", new[] { "facing:enum:north,south,west,east", "half:enum:bottom,top", "open:bool", "powered:bool" } },
            { "ButtonBlock", new[] { "face:enum:wall,floor,ceiling", "facing:enum:north,south,west,east", "powered:bool" } },
            { "PressurePlateBlock", new[] { "powered:bool" } },
            { "WeightedPressurePlateBlock", new[] { "power:int:15" } },
            { "RotatedPillarBlock", new[] { "axis:enum:y,x,z" } },
            { "StandingSignBlock", new[] { "rotation:int:15" } },
            { "WallSignBlock", new[] { "facing:enum:north,south,west,east" } },
            { "CeilingHangingSignBlock", new[] { "rotation:int:15", "attached:bool" } },
            { "WallHangingSignBlock", new[] { "facing:enum:north,south,west,east" } },
            { "ChestBlock", new[] { "facing:enum:north,south,west,east", "type:enum:single,left,right" } },
            { "HorizontalDirectionalBlock", new[] { "facing:enum:north,south,west,east" } },
            { "DirectionalBlock", new[] { "facing:enum:north,south,west,east,up,down" } },
            { "CandleBlock", new[] { "candles:int:4", "lit:bool" } },
            { "LadderBlock", new[] { "facing:enum:north,south,west,east" } },
            { "WallTorchBlock", new[] { "facing:enum:north,south,west,east" } },
            { "DoublePlantBlock", new[] { "half:enum:lower,upper" } },
            { "LanternBlock", new[] { "hanging:bool" } },
            { "AbstractFurnaceBlock", new[] { "facing:enum:north,south,west,east", "lit:bool" } },
        };

        static readonly Dictionary<string, string[]> PreferredOrder = new Dictionary<string, string[]>(StringComparer.Ordinal)
        {
            { "facing", new[] { "north", "south", "west", "east", "up", "down" } },
            { "axis", new[] { "y", "x", "z" } },
            { "half", new[] { "bottom", "top", "lower", "upper" } },
            { "type", new[] { "bottom", "top", "double", "single", "left", "right" } },
            { "shape", new[] { "straight", "inner_left", "inner_right", "outer_left", "outer_right" } },
            { "face", new[] { "wall", "floor", "ceiling" } },
            { "hinge", new[] { "left", "right" } },
        };

        // Java states that are not Bedrock block states: waterlogging is Bedrock's liquid layer
        static readonly HashSet<string> DroppedStates = new HashSet<string> { "waterlogged" };

        public void PrepareBlocks()
        {
            ModelLib = new Models(this);
            JBlocks = Java.Blocks(this);
            JItems = Java.Items(this);
            JEntities = Java.Entities(this);
            Tabs = Java.CreativeTabs(this, JBlocks, JItems, Ids("spawn_egg").Select(x => x.Substring("twilightforest:".Length)));
            var inTabs = new HashSet<string>(Tabs.SelectMany(t => t.Items), StringComparer.Ordinal);
            string bsDir = Path.Combine(GenRes, "assets", "twilightforest", "blockstates");
            foreach (var full in Ids("block"))
            {
                string id = full.Substring("twilightforest:".Length);
                var bi = new BlockInfo { Id = id, Owner = OwnerOf("block", full) };
                JBlock jb;
                if (!JBlocks.TryGetValue(id, out jb)) { Warn("block " + id + " not found in TFBlocks.java"); jb = new JBlock { Id = id, Cls = "Block" }; }
                bi.J = jb;
                bi.Chain = Java.ClassChain(this, jb.Cls, extendsCache);
                bi.InCatalog = inTabs.Contains(full);
                var bs = File.Exists(Path.Combine(bsDir, id + ".json")) ? ReadJson(Path.Combine(bsDir, id + ".json")) : null;
                CollectStates(bi, bs);
                BlockInfos[id] = bi;
            }
        }

        void CollectStates(BlockInfo bi, J bs)
        {
            var values = new Dictionary<string, List<string>>(StringComparer.Ordinal);
            var order = new List<string>();
            Action<string, string> add = (p, v) =>
            {
                List<string> l;
                if (!values.TryGetValue(p, out l)) { values[p] = l = new List<string>(); order.Add(p); }
                if (!l.Contains(v)) l.Add(v);
            };
            if (bs != null && bs["variants"] != null)
            {
                foreach (var key in bs["variants"].Keys)
                    foreach (var kv in ParseVariantKey(key)) add(kv.Key, kv.Value);
            }
            if (bs != null && bs["multipart"] != null)
            {
                foreach (var part in bs["multipart"].A)
                    foreach (var cond in WhenList(part["when"]))
                        foreach (var p in cond.Keys)
                            foreach (var v in cond[p].ToCompact().Trim('"').Split('|')) add(p, v);
            }
            // class-declared states the blockstate file does not list
            var classStates = new List<string[]>();
            foreach (var c in bi.Chain) { string[] s; if (ClassStates.TryGetValue(c, out s)) { classStates.Add(s); break; } }
            var fromClass = new HashSet<string>();
            foreach (var spec in classStates.SelectMany(x => x))
            {
                var p = spec.Split(':');
                if (p[1] == "bool") { add(p[0], "false"); add(p[0], "true"); }
                else if (p[1] == "int") { for (int i = 0; i <= int.Parse(p[2], CultureInfo.InvariantCulture); i++) add(p[0], i.ToString(CultureInfo.InvariantCulture)); }
                else foreach (var v in p[2].Split(',')) add(p[0], v);
                fromClass.Add(p[0]);
            }
            foreach (var p in order)
            {
                if (DroppedStates.Contains(p)) continue;
                var vals = values[p];
                var st = new BState { Java = p, Bedrock = "twilightforest:" + p, Source = fromClass.Contains(p) ? "class" : "blockstate" };
                if (vals.All(v => v == "true" || v == "false")) { st.Type = "bool"; st.Values = new List<string> { "false", "true" }; }
                else if (vals.All(v => v.All(char.IsDigit)))
                {
                    st.Type = "int";
                    int max = vals.Max(v => int.Parse(v, CultureInfo.InvariantCulture));
                    for (int i = 0; i <= max; i++) st.Values.Add(i.ToString(CultureInfo.InvariantCulture));
                }
                else
                {
                    st.Type = "enum";
                    string[] pref;
                    if (!PreferredOrder.TryGetValue(p, out pref)) pref = new string[0];
                    st.Values = vals.OrderBy(v => Array.IndexOf(pref, v) < 0 ? 100 : Array.IndexOf(pref, v)).ThenBy(v => v, StringComparer.Ordinal).ToList();
                }
                if (st.Values.Count > 16) Warn("block " + bi.Id + ": state " + p + " has " + st.Values.Count + " values (Bedrock allows 16)");
                bi.States.Add(st);
                bi.Permutations *= st.Values.Count;
            }
            bi.States = bi.States.OrderBy(s => s.Java, StringComparer.Ordinal).ToList();
        }

        public static List<KeyValuePair<string, string>> ParseVariantKey(string key)
        {
            var res = new List<KeyValuePair<string, string>>();
            if (string.IsNullOrEmpty(key)) return res;
            foreach (var kv in key.Split(','))
            {
                var p = kv.Split('=');
                if (p.Length == 2) res.Add(new KeyValuePair<string, string>(p[0], p[1]));
            }
            return res;
        }

        static IEnumerable<J> WhenList(J when)
        {
            if (when == null) yield break;
            if (when["OR"] != null) { foreach (var c in when["OR"].A) yield return c; }
            else if (when["AND"] != null) { foreach (var c in when["AND"].A) yield return c; }
            else yield return when;
        }

        string StateExpr(BlockInfo bi, string prop, string value)
        {
            var st = bi.States.FirstOrDefault(s => s.Java == prop);
            if (st == null) return null;
            string q = "q.block_state('" + st.Bedrock + "')";
            if (st.Type == "bool") return value == "true" ? q : "!" + q;
            if (st.Type == "int") return q + " == " + value;
            return q + " == '" + value + "'";
        }

        string WhenExpr(BlockInfo bi, J when)
        {
            if (when == null) return null;
            Func<J, string> one = c =>
            {
                var parts = new List<string>();
                foreach (var p in c.Keys)
                {
                    var alts = c[p].ToCompact().Trim('"').Split('|').Select(v => StateExpr(bi, p, v)).Where(x => x != null).ToList();
                    if (alts.Count == 0) continue;
                    parts.Add(alts.Count == 1 ? alts[0] : "(" + string.Join(" || ", alts) + ")");
                }
                return parts.Count == 0 ? "1.0" : string.Join(" && ", parts);
            };
            if (when["OR"] != null) return string.Join(" || ", when["OR"].A.Select(c => "(" + one(c) + ")"));
            if (when["AND"] != null) return string.Join(" && ", when["AND"].A.Select(c => "(" + one(c) + ")"));
            return one(when);
        }

        // ---- geometry / materials of one model ----------------------------------------------------------

        sealed class Visual
        {
            public J Geometry;           // the minecraft:geometry component value
            public J Materials;          // minecraft:material_instances
            public double[] Bounds;      // Java block space, or null for a full cube
            public bool Full;
        }

        string RenderMethod(Model m, IEnumerable<string> texIds)
        {
            string rt = m.RenderType;
            if (rt == "translucent") return "blend";
            if (rt == "cutout" || rt == "cutout_mipped") return "alpha_test";
            if (texIds.Any(t => t != null && HasAlpha(t))) return "alpha_test";
            return "opaque";
        }

        Visual VisualOf(Model m, string ownerId, string blockId)
        {
            var v = new Visual();
            var mats = J.Obj();
            if (m.Elements == null || m.Elements.Count == 0)
            {
                // no shape in the model (block entity renderers, custom loaders): a cube with the particle texture
                string tex = m.Particle;
                string key = UseTexture(tex, ownerId, "terrain");
                var inst = J.Obj().Set("texture", key ?? "tf_missing").Set("render_method", RenderMethod(m, new[] { tex }));
                mats.Set("*", inst);
                v.Geometry = J.Str("minecraft:geometry.full_block");
                v.Materials = mats;
                v.Full = true;
                if (m.BuiltinEntity && (blockId.EndsWith("_chest", StringComparison.Ordinal) || blockId.EndsWith("chest", StringComparison.Ordinal)))
                    v.Bounds = new double[] { 1, 0, 1, 15, 14, 15 };
                return v;
            }
            if (Models.IsFullCube(m))
            {
                var faces = m.Elements[0].Faces;
                var texs = faces.Select(f => m.Resolve(f.Tex)).ToList();
                string rm = RenderMethod(m, texs);
                string particle = m.Particle ?? texs.FirstOrDefault(t => t != null);
                mats.Set("*", J.Obj().Set("texture", UseTexture(particle, ownerId, "terrain") ?? "tf_missing").Set("render_method", rm));
                foreach (var f in faces.OrderBy(f => Array.IndexOf(new[] { "up", "down", "north", "south", "east", "west" }, f.Dir)))
                {
                    var key = UseTexture(m.Resolve(f.Tex), ownerId, "terrain") ?? "tf_missing";
                    mats.Set(f.Dir, J.Obj().Set("texture", key).Set("render_method", rm));
                }
                v.Geometry = J.Str("minecraft:geometry.full_block");
                v.Materials = mats;
                v.Full = true;
                return v;
            }
            // custom geometry shared by every model with the same elements
            string geoId = "geometry.tf." + m.ElementsFrom.Replace(':', '.').Replace('/', '.');
            if (!geometries.ContainsKey(geoId))
            {
                var bone = J.Obj().Set("name", "root").Set("pivot", J.Arr(new[] { J.Num(0), J.Num(0), J.Num(0) })).Set("cubes", J.Arr(Models.Cubes(m.Elements, "")));
                geometries[geoId] = Models.GeometryFile(geoId, new List<J> { bone });
                geometryUsers[geoId] = new SortedSet<string>(StringComparer.Ordinal);
            }
            geometryUsers[geoId].Add(ownerId);
            var instances = new SortedDictionary<string, string>(StringComparer.Ordinal);
            foreach (var f in m.Elements.SelectMany(e => e.Faces)) instances[Models.InstanceName(f.Tex, "")] = f.Tex.StartsWith("#", StringComparison.Ordinal) ? m.Resolve(f.Tex) : f.Tex;
            string rmode = RenderMethod(m, instances.Values);
            string part = m.Particle ?? instances.Values.FirstOrDefault(t => t != null);
            mats.Set("*", J.Obj().Set("texture", UseTexture(part, ownerId, "terrain") ?? "tf_missing").Set("render_method", rmode));
            foreach (var kv in instances)
                mats.Set(kv.Key, J.Obj().Set("texture", UseTexture(kv.Value, ownerId, "terrain") ?? "tf_missing").Set("render_method", rmode));
            v.Geometry = J.Str(geoId);
            v.Materials = mats;
            v.Bounds = Models.Bounds(m.Elements);
            return v;
        }

        static double[] RotateBounds(double[] b, int x, int y)
        {
            if (b == null) return null;
            var pts = new List<double[]>();
            for (int i = 0; i < 8; i++)
                pts.Add(new[] { (i & 1) == 0 ? b[0] : b[3], (i & 2) == 0 ? b[1] : b[4], (i & 4) == 0 ? b[2] : b[5] });
            Func<double[], double[]> rx = p => { double yy = p[1] - 8, zz = p[2] - 8; return new[] { p[0], 8 + zz, 8 - yy }; };      // Java x90 (y -> -z)
            Func<double[], double[]> ry = p => { double xx = p[0] - 8, zz = p[2] - 8; return new[] { 8 - zz, p[1], 8 + xx }; };      // Java y90 (north -> east)
            for (int i = 0; i < (x / 90) % 4; i++) pts = pts.Select(rx).ToList();
            for (int i = 0; i < (y / 90) % 4; i++) pts = pts.Select(ry).ToList();
            return new[] { pts.Min(p => p[0]), pts.Min(p => p[1]), pts.Min(p => p[2]), pts.Max(p => p[0]), pts.Max(p => p[1]), pts.Max(p => p[2]) };
        }

        static J Box(double[] b, bool selection)
        {
            double[] lo = { b[0], b[1], b[2] }, size = { b[3] - b[0], b[4] - b[1], b[5] - b[2] };
            for (int i = 0; i < 3; i++)
                if (size[i] < 1 && selection) { double c = Math.Min(15, Math.Max(0, lo[i] + size[i] / 2 - 0.5)); lo[i] = c; size[i] = 1; }
            return J.Obj().Set("origin", J.Arr(new[] { J.Num(Math.Round(lo[0] - 8, 4)), J.Num(Math.Round(lo[1], 4)), J.Num(Math.Round(lo[2] - 8, 4)) }))
                          .Set("size", J.Arr(size.Select(s => J.Num(Math.Round(s, 4)))));
        }

        void SetShape(J comps, Visual v, BlockInfo bi, int x, int y, bool root)
        {
            comps.Set("minecraft:geometry", v.Geometry.Clone());
            comps.Set("minecraft:material_instances", v.Materials.Clone());
            if (x != 0 || y != 0)
                comps.Set("minecraft:transformation", J.Obj().Set("rotation", J.Arr(new[] { J.Num((360 - x) % 360), J.Num((360 - y) % 360), J.Num(0) })));
            var b = RotateBounds(v.Bounds, x, y);
            if (b != null && !(b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 16 && b[4] == 16 && b[5] == 16))
            {
                bool flat = b[3] - b[0] < 0.5 || b[4] - b[1] < 0.5 || b[5] - b[2] < 0.5;
                if (!bi.J.NoCollision) comps.Set("minecraft:collision_box", flat ? J.Bool(false) : Box(b, false));
                comps.Set("minecraft:selection_box", Box(b, true));
            }
            else if (!root)
            {
                if (!bi.J.NoCollision) comps.Set("minecraft:collision_box", J.Bool(true));
                comps.Set("minecraft:selection_box", J.Bool(true));
            }
        }

        // ---- the "blockstates" / "models" kinds -------------------------------------------------------

        public void ImportBlocks()
        {
            string bsDir = Path.Combine(GenRes, "assets", "twilightforest", "blockstates");
            foreach (var bi in BlockInfos.Values)
            {
                string full = "twilightforest:" + bi.Id;
                var bs = File.Exists(Path.Combine(bsDir, bi.Id + ".json")) ? ReadJson(Path.Combine(bsDir, bi.Id + ".json")) : null;
                var desc = J.Obj().Set("identifier", full);
                desc.Set("menu_category", J.Obj().Set("category", bi.InCatalog ? CatalogCategory(full) : "none"));
                if (bi.States.Count > 0)
                {
                    var states = J.Obj();
                    foreach (var st in bi.States)
                    {
                        if (st.Type == "bool") states.Set(st.Bedrock, J.Arr(new[] { J.Bool(false), J.Bool(true) }));
                        else if (st.Type == "int") states.Set(st.Bedrock, J.Arr(st.Values.Select(x => J.Num(long.Parse(x, CultureInfo.InvariantCulture)))));
                        else states.Set(st.Bedrock, J.Strs(st.Values));
                    }
                    desc.Set("states", states);
                }
                var comps = J.Obj();
                var perms = J.Arr();
                var jb = bi.J;
                if (jb.Unbreakable) comps.Set("minecraft:destructible_by_mining", false);
                else comps.Set("minecraft:destructible_by_mining", J.Obj().Set("seconds_to_destroy", Math.Round(jb.Hardness, 4)));
                double res = jb.Resistance < 0 ? jb.Hardness : jb.Resistance;
                comps.Set("minecraft:destructible_by_explosion", jb.Unbreakable || res >= 3600000 ? (J)J.Bool(false) : J.Obj().Set("explosion_resistance", Math.Round(Math.Max(0, res), 4)));
                if (jb.Light > 0) comps.Set("minecraft:light_emission", Math.Min(15, jb.Light));
                string hex;
                if (jb.MapColor != null && MapColors.TryGetValue(jb.MapColor, out hex)) comps.Set("minecraft:map_color", hex);
                if (jb.Flammable) comps.Set("minecraft:flammable", J.Obj().Set("catch_chance_modifier", 5).Set("destroy_chance_modifier", 20));
                if (jb.Friction > 0 && Math.Abs(jb.Friction - 0.6) > 1e-6) comps.Set("minecraft:friction", Math.Round(Math.Max(0, Math.Min(0.9, 1 - jb.Friction)), 4));
                if (jb.NoCollision) comps.Set("minecraft:collision_box", false);

                Visual rootVisual = null;
                if (bs != null && bs["variants"] != null)
                {
                    var keys = bs["variants"].Keys.ToList();
                    var seenConds = new HashSet<string>();
                    var variantVisuals = new List<Tuple<string, Visual, int, int>>();
                    foreach (var key in keys)
                    {
                        var vj = bs["variants"][key];
                        if (vj.IsArr) vj = vj.A[0];
                        var m = ModelLib.Get(vj["model"].AsString());
                        int x = vj["x"] != null ? (int)vj["x"].AsDouble() : 0, y = vj["y"] != null ? (int)vj["y"].AsDouble() : 0;
                        var vis = VisualOf(m, bi.Owner, bi.Id);
                        var conds = ParseVariantKey(key).Select(kv => StateExpr(bi, kv.Key, kv.Value)).Where(c => c != null).ToList();
                        string cond = conds.Count == 0 ? "" : string.Join(" && ", conds);
                        if (!seenConds.Add(cond)) continue;
                        variantVisuals.Add(Tuple.Create(cond, vis, x, y));
                        if (bi.Particle == null) bi.Particle = m.Particle;
                    }
                    bool uniform = variantVisuals.Select(t => t.Item2.Geometry.ToCompact() + t.Item2.Materials.ToCompact() + t.Item3 + "," + t.Item4).Distinct().Count() == 1;
                    if (variantVisuals.Count > 0)
                    {
                        var first = variantVisuals[0];
                        rootVisual = first.Item2;
                        SetShape(comps, first.Item2, bi, first.Item3, first.Item4, true);
                        if (!uniform)
                        {
                            foreach (var t in variantVisuals)
                            {
                                if (t.Item1 == "") continue;
                                var pc = J.Obj();
                                SetShape(pc, t.Item2, bi, t.Item3, t.Item4, false);
                                perms.Add(J.Obj().Set("condition", t.Item1).Set("components", pc));
                            }
                        }
                    }
                }
                else if (bs != null && bs["multipart"] != null)
                {
                    // one geometry, a bone per part; parts are shown by Molang bone visibility
                    var bones = new List<J>();
                    var vis = J.Obj();
                    var mats = J.Obj();
                    double[] bounds = null;
                    int i = 0;
                    string particle = null;
                    var allTex = new List<string>();
                    Model firstModel = null;
                    foreach (var part in bs["multipart"].A)
                    {
                        var apply = part["apply"].IsArr ? part["apply"].A[0] : part["apply"];
                        var m = ModelLib.Get(apply["model"].AsString());
                        if (firstModel == null) firstModel = m;
                        int x = apply["x"] != null ? (int)apply["x"].AsDouble() : 0, y = apply["y"] != null ? (int)apply["y"].AsDouble() : 0;
                        if (m.Elements == null) { i++; continue; }
                        var els = m.Elements.Select(e => RetargetForPart(e, m)).ToList();
                        string name = "part" + i;
                        var bone = J.Obj().Set("name", name).Set("pivot", J.Arr(new[] { J.Num(0), J.Num(8), J.Num(0) }));
                        // the part's model rotation as a bone rotation (Bedrock geometry convention, see Models.Cubes)
                        if (x != 0 || y != 0) bone.Set("rotation", J.Arr(new[] { J.Num(x), J.Num(y), J.Num(0) }));
                        bone.Set("cubes", J.Arr(Models.Cubes(els, "")));
                        bones.Add(bone);
                        string when = WhenExpr(bi, part["when"]);
                        vis.Set(name, when == null ? (J)J.Bool(true) : J.Str(when));
                        foreach (var f in els.SelectMany(e => e.Faces)) { allTex.Add(f.Tex); }
                        if (particle == null) particle = m.Particle;
                        var pb = RotateBounds(Models.Bounds(m.Elements), x, y);
                        if (pb != null && part["when"] == null) bounds = bounds == null ? pb : new[] { Math.Min(bounds[0], pb[0]), Math.Min(bounds[1], pb[1]), Math.Min(bounds[2], pb[2]), Math.Max(bounds[3], pb[3]), Math.Max(bounds[4], pb[4]), Math.Max(bounds[5], pb[5]) };
                        i++;
                    }
                    bi.Particle = particle;
                    string geoId = "geometry.tf.twilightforest.block." + bi.Id + ".multipart";
                    geometries[geoId] = Models.GeometryFile(geoId, bones);
                    geometryUsers[geoId] = new SortedSet<string>(StringComparer.Ordinal) { bi.Owner };
                    string rm = firstModel != null ? RenderMethod(firstModel, allTex.Distinct()) : "opaque";
                    mats.Set("*", J.Obj().Set("texture", UseTexture(particle, bi.Owner, "terrain") ?? "tf_missing").Set("render_method", rm));
                    foreach (var t in allTex.Distinct().OrderBy(x => x, StringComparer.Ordinal))
                        mats.Set(Models.InstanceName(t, ""), J.Obj().Set("texture", UseTexture(t, bi.Owner, "terrain") ?? "tf_missing").Set("render_method", rm));
                    comps.Set("minecraft:geometry", J.Obj().Set("identifier", geoId).Set("bone_visibility", vis));
                    comps.Set("minecraft:material_instances", mats);
                    if (bounds != null)
                    {
                        if (!jb.NoCollision) comps.Set("minecraft:collision_box", Box(bounds, false));
                        comps.Set("minecraft:selection_box", Box(bounds, true));
                    }
                    rootVisual = new Visual { Bounds = bounds };
                }
                bool isFull = rootVisual != null && rootVisual.Full && rootVisual.Bounds == null;
                if (jb.NoOcclusion || !isFull) comps.Set("minecraft:light_dampening", 0);

                var block = J.Obj().Set("description", desc).Set("components", comps);
                if (perms.Count > 0) block.Set("permutations", perms);
                var doc = J.Obj().Set("format_version", BlockFormat).Set("minecraft:block", block);
                EmitJson("blockstates", bi.Owner, "bp/blocks/" + bi.Id + ".json", doc);
                Fragment("blockstates", bi.Owner, "rp/blocks.d/import-" + NameOf(bi.Owner) + ".json", full, J.Obj().Set("sound", BedrockSound(jb.Sound)));
            }
            foreach (var kv in geometries)
            {
                string owner = SharedOwner(geometryUsers[kv.Key]);
                string file = kv.Key.Substring("geometry.tf.".Length);
                EmitJson("models", owner, "rp/models/blocks/twilightforest/" + file + ".geo.json", kv.Value);
            }
        }

        public const string BlockFormat = "1.21.80";

        // multipart parts carry block-specific textures: name instances after the resolved texture
        static Element RetargetForPart(Element e, Model m)
        {
            var c = new Element { From = e.From, To = e.To, RotOrigin = e.RotOrigin, RotAxis = e.RotAxis, RotAngle = e.RotAngle, Rescale = e.Rescale };
            foreach (var f in e.Faces)
                c.Faces.Add(new Face { Dir = f.Dir, Uv = f.Uv, Rotation = f.Rotation, TintIndex = f.TintIndex, Tex = m.Resolve(f.Tex) ?? "twilightforest:missing" });
            return c;
        }

        // ---- docs/spec/blockstates.md -------------------------------------------------------------------

        public string BlockstatesDoc()
        {
            var sb = new StringBuilder();
            sb.Append("# Block states (frozen)\n\n");
            sb.Append("Generated by `tools/import/run.ps1` (F1) from TF 4.8 (`9e40a757`): the Bedrock block states of every TF block. Rule R1: these\n");
            sb.Append("names and values are frozen; later units may only **add** states (or values), never rename or remove one.\n\n");
            sb.Append("Mapping rules (see [import.md](import.md)):\n\n");
            sb.Append("- Every Java block-state property `p` becomes the Bedrock state `twilightforest:p` with the same values.\n");
            sb.Append("- Booleans are `[false, true]`. Integer properties are listed as `[0, 1, ..., max]` (Bedrock reads an integer state by\n");
            sb.Append("  its position, so the list always starts at 0); the Java value equals the Bedrock value, and values below the Java\n");
            sb.Append("  minimum (for example `distance` 0, `candles` 0) are unused.\n");
            sb.Append("- Enum values keep Java's names; the first value is the default state.\n");
            sb.Append("- `waterlogged` is not a block state on Bedrock (water is a separate liquid layer): it is dropped.\n");
            sb.Append("- No placement traits (`minecraft:placement_direction` / `placement_position`) are used: the families kit (F11)\n");
            sb.Append("  and the owning units set the states on placement, exactly as TF's `getStateForPlacement`.\n");
            sb.Append("- Source `blockstate`: the property appears in TF's blockstate file; `class`: declared by the vanilla block class\n");
            sb.Append("  the TF block extends (for example `LeavesBlock`: `distance`, `persistent`).\n\n");
            long total = BlockInfos.Values.Sum(b => b.Permutations);
            sb.AppendFormat(CultureInfo.InvariantCulture, "{0} blocks, {1} with states, {2} permutations in total.\n\n", BlockInfos.Count, BlockInfos.Values.Count(b => b.States.Count > 0), total);
            sb.Append("| Block | Owner | Java class | States | Permutations |\n|---|---|---|---|---|\n");
            foreach (var bi in BlockInfos.Values)
            {
                string states = bi.States.Count == 0 ? "-" : string.Join("<br>", bi.States.Select(s =>
                    "`" + s.Bedrock + "`: " + string.Join(", ", s.Values) + (s.Source == "class" ? " (class)" : "")));
                string cls = bi.Chain.Count > 1 ? bi.Chain[0] + " < " + bi.Chain.Last() : bi.Chain.FirstOrDefault() ?? "?";
                sb.AppendFormat(CultureInfo.InvariantCulture, "| `twilightforest:{0}` | {1} | {2} | {3} | {4} |\n", bi.Id, bi.Owner, cls, states, bi.Permutations);
            }
            return sb.ToString();
        }

        // ---- tables -------------------------------------------------------------------------------------

        static string BedrockSound(string javaSound)
        {
            string s;
            return SoundMap.TryGetValue(javaSound ?? "STONE", out s) ? s : "stone";
        }

        static readonly Dictionary<string, string> SoundMap = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            { "STONE", "stone" }, { "WOOD", "wood" }, { "GRAVEL", "gravel" }, { "GRASS", "grass" }, { "METAL", "metal" }, { "GLASS", "glass" },
            { "WOOL", "cloth" }, { "SAND", "sand" }, { "SNOW", "snow" }, { "POWDER_SNOW", "snow" }, { "LADDER", "ladder" }, { "ANVIL", "anvil" },
            { "SLIME_BLOCK", "slime" }, { "HONEY_BLOCK", "honey_block" }, { "WET_GRASS", "grass" }, { "CORAL_BLOCK", "coral" }, { "BAMBOO", "bamboo" },
            { "SCAFFOLDING", "scaffolding" }, { "SWEET_BERRY_BUSH", "sweet_berry_bush" }, { "CROP", "grass" }, { "HARD_CROP", "grass" },
            { "VINE", "vines" }, { "LANTERN", "lantern" }, { "STEM", "stem" }, { "NYLIUM", "nylium" }, { "FUNGUS", "fungus" }, { "ROOTS", "roots" },
            { "SHROOMLIGHT", "shroomlight" }, { "SOUL_SAND", "soul_sand" }, { "BASALT", "basalt" }, { "NETHERRACK", "netherrack" },
            { "NETHER_BRICKS", "nether_brick" }, { "BONE_BLOCK", "bone_block" }, { "NETHERITE_BLOCK", "netherite" }, { "CHAIN", "chain" },
            { "CANDLE", "candle" }, { "AMETHYST", "amethyst_block" }, { "COPPER", "copper" }, { "MOSS_CARPET", "moss_carpet" }, { "MOSS", "moss_block" },
            { "HANGING_ROOTS", "hanging_roots" }, { "AZALEA_LEAVES", "azalea_leaves" }, { "AZALEA", "azalea" }, { "DEEPSLATE", "deepslate" },
            { "DEEPSLATE_BRICKS", "deepslate_bricks" }, { "MUD", "mud" }, { "MUD_BRICKS", "mud_bricks" }, { "PACKED_MUD", "packed_mud" },
            { "HANGING_SIGN", "hanging_sign" }, { "NETHER_WOOD", "nether_wood" }, { "CHERRY_WOOD", "cherry_wood" }, { "CHERRY_LEAVES", "cherry_leaves" },
            { "CHISELED_BOOKSHELF", "chiseled_bookshelf" }, { "LILY_PAD", "grass" }, { "SPONGE", "sponge" }, { "CALCITE", "calcite" }, { "TUFF", "tuff" },
            { "DRIPSTONE_BLOCK", "dripstone_block" }, { "MANGROVE_ROOTS", "mangrove_roots" }, { "FROGLIGHT", "froglight" },
            // TFSoundTypes
            { "JAR", "glass" }, { "CASKET", "metal" }, { "SKULL_CANDLE", "candle" }, { "THORNS", "wood" }, { "BOSS_SPAWNER", "metal" },
        };

        /// <summary>Java MapColor constants (Minecraft 1.21.1 RGB values).</summary>
        static readonly Dictionary<string, string> MapColors = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            { "GRASS", "#7fb238" }, { "SAND", "#f7e9a3" }, { "WOOL", "#c7c7c7" }, { "FIRE", "#ff0000" }, { "ICE", "#a0a0ff" }, { "METAL", "#a7a7a7" },
            { "PLANT", "#007c00" }, { "SNOW", "#ffffff" }, { "CLAY", "#a4a8b8" }, { "DIRT", "#976d4d" }, { "STONE", "#707070" }, { "WATER", "#4040ff" },
            { "WOOD", "#8f7748" }, { "QUARTZ", "#fffcf5" }, { "COLOR_ORANGE", "#d87f33" }, { "COLOR_MAGENTA", "#b24cd8" }, { "COLOR_LIGHT_BLUE", "#6699d8" },
            { "COLOR_YELLOW", "#e5e533" }, { "COLOR_LIGHT_GREEN", "#7fcc19" }, { "COLOR_PINK", "#f27fa5" }, { "COLOR_GRAY", "#4c4c4c" },
            { "COLOR_LIGHT_GRAY", "#999999" }, { "COLOR_CYAN", "#4c7f99" }, { "COLOR_PURPLE", "#7f3fb2" }, { "COLOR_BLUE", "#334cb2" },
            { "COLOR_BROWN", "#664c33" }, { "COLOR_GREEN", "#667f33" }, { "COLOR_RED", "#993333" }, { "COLOR_BLACK", "#191919" }, { "GOLD", "#faee4d" },
            { "DIAMOND", "#5cdbd5" }, { "LAPIS", "#4a80ff" }, { "EMERALD", "#00d93a" }, { "PODZOL", "#815631" }, { "NETHER", "#700200" },
            { "TERRACOTTA_WHITE", "#d1b1a1" }, { "TERRACOTTA_ORANGE", "#9f5224" }, { "TERRACOTTA_MAGENTA", "#95576c" }, { "TERRACOTTA_LIGHT_BLUE", "#706c8a" },
            { "TERRACOTTA_YELLOW", "#ba8524" }, { "TERRACOTTA_LIGHT_GREEN", "#677535" }, { "TERRACOTTA_PINK", "#a04d4e" }, { "TERRACOTTA_GRAY", "#392923" },
            { "TERRACOTTA_LIGHT_GRAY", "#876b62" }, { "TERRACOTTA_CYAN", "#575c5c" }, { "TERRACOTTA_PURPLE", "#7a4958" }, { "TERRACOTTA_BLUE", "#4c3e5c" },
            { "TERRACOTTA_BROWN", "#4c3223" }, { "TERRACOTTA_GREEN", "#4c522a" }, { "TERRACOTTA_RED", "#8e3c2e" }, { "TERRACOTTA_BLACK", "#251610" },
            { "CRIMSON_NYLIUM", "#bd3031" }, { "CRIMSON_STEM", "#943f61" }, { "CRIMSON_HYPHAE", "#5c191d" }, { "WARPED_NYLIUM", "#167e86" },
            { "WARPED_STEM", "#3a8e8c" }, { "WARPED_HYPHAE", "#562c3e" }, { "WARPED_WART_BLOCK", "#14b485" }, { "DEEPSLATE", "#646464" },
            { "RAW_IRON", "#d8af93" }, { "GLOW_LICHEN", "#7fa796" },
        };
    }
}
