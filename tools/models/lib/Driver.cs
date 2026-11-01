// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Driver of the model converter (contract "geo", unit F2): reads tools/models/catalog.json,
// evaluates every LayerDefinition TF registers (ClientRegistrationEvents.registerLayerDefinitions)
// and writes, per owning unit,
//   assets/gen/geo/<unit>/rp/models/entity/<name>.geo.json     geometry.twilightforest.<name>
//   assets/gen/geo/<unit>/rp/animations/<name>.base.json       animation.twilightforest.<name>.base
//   assets/gen/geo/<unit>/rp/attachables/<item>.json           armor, Travellers' Gear, trophy heads
//   assets/gen/geo/<unit>/bones/<name>.json                    Java part -> bone map, setupAnim report
// plus assets/gen/geo/models/data/gf/models/index.json (every output, for the selftests and the
// preview) and the shared render controllers of multi-part attachables. Part of GloamingForest,
// LGPL-3.0-or-later.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;

namespace Gf.Models
{
    public sealed class LayerInfo
    {
        public string Name;          // TF model layer name (ModelLayerLocation path + "_" + layer unless "main")
        public string Field;         // TFModelLayers field
        public string Supplier;      // Java source of the supplier
        public string SupplierClass; // the TF class of a Cls::method supplier
        public LayerDef Def;
        public string Error;
        public int Line;
    }

    public sealed class Generator
    {
        public readonly ClassPath ClassPath;
        public readonly Interp Interp;
        readonly JsonElement cat;
        public readonly SortedDictionary<string, string> Files = new SortedDictionary<string, string>(StringComparer.Ordinal);
        public readonly List<string> Log = new List<string>();
        public readonly List<string> Errors = new List<string>();
        public readonly Dictionary<string, LayerInfo> Layers = new Dictionary<string, LayerInfo>(StringComparer.Ordinal);
        readonly List<object> indexGeometries = new List<object>();
        readonly List<object> indexAttachables = new List<object>();
        readonly List<object> indexAnimations = new List<object>();
        readonly SortedDictionary<string, JObj> renderControllers = new SortedDictionary<string, JObj>(StringComparer.Ordinal);
        readonly string texPrefix, geoPrefix, animPrefix, tfPrefix;
        public string TfRevision = "9e40a757";

        public Generator(string tfJavaRoot, string catalogJson)
        {
            ClassPath = new ClassPath(tfJavaRoot);
            Interp = new Interp(ClassPath);
            cat = JsonDocument.Parse(catalogJson, new JsonDocumentOptions { CommentHandling = JsonCommentHandling.Skip, AllowTrailingCommas = true }).RootElement;
            texPrefix = Str(cat, "texturePrefix", "textures/twilightforest/");
            geoPrefix = Str(cat, "geometryPrefix", "geometry.twilightforest.");
            animPrefix = Str(cat, "animationPrefix", "animation.twilightforest.");
            tfPrefix = "src/main/resources/assets/twilightforest/textures/";
        }

        static string Str(JsonElement e, string key, string def = null)
        {
            JsonElement v;
            return e.ValueKind == JsonValueKind.Object && e.TryGetProperty(key, out v) && v.ValueKind == JsonValueKind.String ? v.GetString() : def;
        }
        static bool Bool(JsonElement e, string key)
        {
            JsonElement v;
            return e.ValueKind == JsonValueKind.Object && e.TryGetProperty(key, out v) && v.ValueKind == JsonValueKind.True;
        }
        static JsonElement Prop(JsonElement e, string key)
        {
            JsonElement v;
            return e.ValueKind == JsonValueKind.Object && e.TryGetProperty(key, out v) ? v : default(JsonElement);
        }
        static List<string> StrList(JsonElement e, string key)
        {
            var v = Prop(e, key);
            var l = new List<string>();
            if (v.ValueKind == JsonValueKind.Array) foreach (var x in v.EnumerateArray()) l.Add(x.GetString());
            else if (v.ValueKind == JsonValueKind.String) l.Add(v.GetString());
            return l;
        }

        // ---- the layer registry ------------------------------------------------------------------------

        public void LoadLayers()
        {
            var cls = ClassPath.Find("ClientRegistrationEvents", null);
            if (cls == null) throw new JavaEvalException("ClientRegistrationEvents.java not found in the TF checkout");
            var m = cls.Methods.First(x => x.Name == "registerLayerDefinitions");
            var body = Parser.ParseBody(cls.Unit, m);
            var lines = cls.Unit.Source.Split('\n');
            foreach (var s in body.Stmts)
            {
                var call = (s as ExprStmt)?.E as CallExpr;
                if (call == null || call.Name != "registerLayerDefinition" || call.Args.Count != 2) continue;
                var info = new LayerInfo { Line = s.Line, Supplier = lines[s.Line - 1].Trim() };
                var frame = new Frame(cls);
                try
                {
                    var loc = Interp.Eval(call.Args[0], frame) as ModelLayerLocationObj;
                    info.Name = loc.Name;
                    info.Field = (call.Args[0] as FieldExpr)?.Name;
                }
                catch (Exception ex) { Errors.Add("layer at ClientRegistrationEvents.java:" + s.Line + ": " + ex.Message); continue; }
                if (call.Args[1] is MethodRefExpr mr && mr.Target is NameExpr tn) info.SupplierClass = tn.Name;
                try
                {
                    var supplier = Interp.Eval(call.Args[1], frame);
                    info.Def = Interp.Invoke(supplier, new List<object>()) as LayerDef;
                    if (info.Def == null) info.Error = "the supplier returned no LayerDefinition";
                }
                catch (JavaEvalException ex) { info.Error = ex.Message; }
                catch (JavaSyntaxException ex) { info.Error = "syntax: " + ex.Message; }
                Layers[info.Name] = info;
            }
        }

        // ---- run -----------------------------------------------------------------------------------------

        public void Run(string onlyClass, string onlyUnit, string onlyItem)
        {
            // PowerShell passes $null as "" to string parameters
            if (string.IsNullOrEmpty(onlyClass)) onlyClass = null;
            if (string.IsNullOrEmpty(onlyUnit)) onlyUnit = null;
            if (string.IsNullOrEmpty(onlyItem)) onlyItem = null;
            LoadLayers();
            var layersCat = Prop(cat, "layers");
            // every registered layer must be described in the catalog (no silent gaps)
            foreach (var l in Layers.Values)
            {
                if (layersCat.ValueKind != JsonValueKind.Object || !layersCat.TryGetProperty(l.Name, out _))
                    Errors.Add("layer '" + l.Name + "' (TFModelLayers." + l.Field + ") is not in tools/models/catalog.json");
            }
            foreach (var p in layersCat.EnumerateObject())
            {
                if (!Layers.ContainsKey(p.Name)) { Errors.Add("catalog layer '" + p.Name + "' is not registered by TF"); continue; }
                var spec = p.Value;
                var info = Layers[p.Name];
                string kind = Str(spec, "kind", "entity");
                // armor layers are written per slot ("armor"), trophy layers through "extracts"
                if (kind == "armor" || kind == "source" || Str(spec, "skip") != null) continue;
                string unit = Str(spec, "unit");
                if (Str(spec, "vanilla") != null)
                {
                    if (Matches(onlyClass, onlyUnit, info, unit)) AddVanilla(p.Name, spec, info);
                    continue;
                }
                if (info.Error != null) { Errors.Add("layer '" + p.Name + "': " + info.Error); continue; }
                if (!Matches(onlyClass, onlyUnit, info, unit, Str(spec, "animClass"))) continue;
                ConvertLayer(p.Name, spec, info);
            }
            var armor = Prop(cat, "armor");
            if (armor.ValueKind == JsonValueKind.Array)
                foreach (var a in armor.EnumerateArray()) Gear(a, onlyClass, onlyUnit, onlyItem);
            var extracts = Prop(cat, "extracts");
            if (extracts.ValueKind == JsonValueKind.Array)
                foreach (var x in extracts.EnumerateArray()) Extract(x, onlyClass, onlyUnit, onlyItem);
            if (onlyClass == null && onlyUnit == null && onlyItem == null) WriteShared();
        }

        bool Matches(string onlyClass, string onlyUnit, LayerInfo info, string unit, string animClass = null)
        {
            if (onlyUnit != null && !string.Equals(onlyUnit, unit, StringComparison.OrdinalIgnoreCase)) return false;
            if (onlyClass != null)
            {
                bool hit = string.Equals(info.SupplierClass, onlyClass, StringComparison.Ordinal) || string.Equals(animClass, onlyClass, StringComparison.Ordinal)
                           || (info.Supplier != null && info.Supplier.Contains(onlyClass + ".")) || (info.Supplier != null && info.Supplier.Contains(onlyClass + "::"));
                if (!hit) return false;
            }
            return true;
        }

        // A TF texture (path below assets/twilightforest/textures/) as a Bedrock texture path; "minecraft:<path>" names a vanilla one.
        string Tex(string tfPath)
        {
            if (tfPath == null) return null;
            if (tfPath.StartsWith("minecraft:", StringComparison.Ordinal)) return "textures/" + Path.ChangeExtension(tfPath.Substring(10), null).Replace('\\', '/');
            return texPrefix + Path.ChangeExtension(tfPath, null).Replace('\\', '/');
        }
        string TfTex(string tfPath) { return tfPath != null && tfPath.StartsWith("minecraft:", StringComparison.Ordinal) ? "(vanilla) " + tfPath : tfPrefix + tfPath; }

        static string UnitDir(string unit) { return "assets/gen/geo/" + unit + "/"; }

        void Put(string path, object json) { Files[path] = Json.Write(json); }

        // ---- entity / block / item layers ----------------------------------------------------------------

        void ConvertLayer(string name, JsonElement spec, LayerInfo info)
        {
            string unit = Str(spec, "unit");
            string kind = Str(spec, "kind", "entity");
            string id = geoPrefix + name;
            var opt = new ConvertOptions { Identifier = id };
            foreach (var kv in Converter.HumanoidNames) opt.Rename[kv.Key] = kv.Value;
            foreach (var kv in Converter.QuadrupedNames) opt.Rename[kv.Key] = kv.Value;
            var geo = Converter.Convert(info.Def, opt);
            string geoFile = "models/entity/" + name + ".geo.json";
            Put(UnitDir(unit) + "rp/" + geoFile, geo.ToJson());

            // setupAnim -> base animation
            string animClass = Str(spec, "animClass") ?? info.SupplierClass;
            AnimResult anim = null;
            string animId = null;
            if (animClass != null && kind == "entity" && !Bool(spec, "noAnim"))
            {
                var ac = ClassPath.Find(animClass, null);
                if (ac == null) Errors.Add("layer '" + name + "': animClass " + animClass + " not found");
                else
                {
                    anim = new AnimTranslator(ClassPath, Interp).Translate(ac, geo);
                    if (anim.Bones.Count > 0)
                    {
                        animId = animPrefix + name + ".base";
                        Put(UnitDir(unit) + "rp/animations/" + name + ".base.json", AnimTranslator.AnimationJson(animId, anim));
                        indexAnimations.Add(new JObj().Add("identifier", animId).Add("file", "animations/" + name + ".base.json").Add("unit", unit)
                            .Add("bones", anim.Bones.Keys.Cast<object>().ToList()));
                    }
                }
            }
            Put(UnitDir(unit) + "bones/" + name + ".json", BoneMap(name, spec, info, geo, anim, animId, animClass));
            var textures = StrList(spec, "textures");
            var entry = new JObj().Add("identifier", id).Add("file", geoFile).Add("unit", unit).Add("kind", kind);
            foreach (var key in new[] { "entity", "block", "item" })
            {
                var refs = StrList(spec, key);
                if (refs.Count > 0) entry.Add(key, refs.Cast<object>().ToList());
            }
            entry.Add("textures", textures.Select(t => (object)Tex(t)).ToList())
                 .Add("texture_width", geo.TexWidth).Add("texture_height", geo.TexHeight)
                 .Add("bones", geo.Bones.Count).Add("cubes", geo.Bones.Sum(b => b.Cubes.Count))
                 .Add("visible_bounds", Json.Vec(geo.BoundsWidth, geo.BoundsHeight, geo.BoundsOffsetY));
            if (animId != null) entry.Add("animation", animId);
            var preview = Prop(spec, "preview");
            if (preview.ValueKind == JsonValueKind.Object) entry.Add("preview", ToJObj(preview));
            indexGeometries.Add(entry);
            Log.Add(string.Format(CultureInfo.InvariantCulture, "{0,-28} {1,-14} {2,3} bones {3,4} cubes ({4} per-face UV){5}", name, unit, geo.Bones.Count,
                geo.Bones.Sum(b => b.Cubes.Count), geo.PerFaceCubes, anim != null ? string.Format(", setupAnim {0}/{1} statements", anim.Statements.Count(x => x.Translated), anim.Statements.Count) : ""));
        }

        static object ToJObj(JsonElement e)
        {
            switch (e.ValueKind)
            {
                case JsonValueKind.Object: { var o = new JObj(); foreach (var p in e.EnumerateObject()) o.Add(p.Name, ToJObj(p.Value)); return o; }
                case JsonValueKind.Array: return e.EnumerateArray().Select(ToJObj).ToList();
                case JsonValueKind.String: return e.GetString();
                case JsonValueKind.Number: return e.GetDouble();
                case JsonValueKind.True: return true;
                case JsonValueKind.False: return false;
            }
            return null;
        }

        JObj BoneMap(string name, JsonElement spec, LayerInfo info, GeometryOut geo, AnimResult anim, string animId, string animClass)
        {
            var o = new JObj()
                .Add("comment", "Generated by tools/models/convert.ps1 (F2) from The Twilight Forest 4.8 @ " + TfRevision + " (LGPL-2.1-or-later code). Never edit: regenerate.")
                .Add("geometry", geo.Identifier)
                .Add("layer", new JObj().Add("name", name).Add("field", "TFModelLayers." + info.Field).Add("supplier", info.Supplier))
                .Add("texture_size", Json.Arr(geo.TexWidth, geo.TexHeight));
            var textures = StrList(spec, "textures");
            if (textures.Count > 0)
                o.Add("textures", textures.Select(t => (object)new JObj().Add("bedrock", Tex(t)).Add("tf", TfTex(t))).ToList());
            var bones = new List<object>();
            foreach (var b in geo.Bones)
            {
                var bo = new JObj().Add("java", b.JavaPath).Add("bone", b.Name);
                if (b.Parent != null) bo.Add("parent", b.Parent);
                var piv = geo.MapPoint(b.JavaPivot);
                bo.Add("pivot", Json.Vec(piv.X, piv.Y, piv.Z));
                bo.Add("rotation", Json.Vec(GeometryOut.Deg(b.Rot.X), GeometryOut.Deg(b.Rot.Y), GeometryOut.Deg(b.Rot.Z)));
                bo.Add("java_offset", Json.Vec(b.Pose.X, b.Pose.Y, b.Pose.Z));
                bo.Add("java_rotation", Json.Vec(b.Pose.XRot, b.Pose.YRot, b.Pose.ZRot));
                bo.Add("cubes", b.Cubes.Count);
                if (b.Cubes.Any(c => c.Mirror)) bo.Add("mirror", b.Cubes.All(c => c.Mirror) ? (object)true : "some");
                bones.Add(bo);
            }
            o.Add("bones", bones);
            if (anim != null)
            {
                var a = new JObj().Add("class", animClass).Add("identifier", animId);
                if (anim.Notes.Count > 0) a.Add("notes", anim.Notes.Cast<object>().ToList());
                a.Add("translated", anim.Statements.Where(s => s.Translated).Select(s => (object)s.Java).ToList());
                a.Add("todo", anim.Statements.Where(s => !s.Translated).Select(s => (object)new JObj().Add("line", s.Line).Add("java", s.Java).Add("why", s.Reason)).ToList());
                o.Add("setupAnim", a);
            }
            var notes = StrList(spec, "notes");
            if (notes.Count > 0) o.Add("notes", notes.Cast<object>().ToList());
            return o;
        }

        void AddVanilla(string name, JsonElement spec, LayerInfo info)
        {
            string unit = Str(spec, "unit");
            var o = new JObj()
                .Add("comment", "Generated by tools/models/convert.ps1 (F2). This TF layer uses a vanilla Minecraft model: use the vanilla Bedrock geometry.")
                .Add("geometry", Str(spec, "vanilla"))
                .Add("layer", new JObj().Add("name", name).Add("field", "TFModelLayers." + info.Field).Add("supplier", info.Supplier));
            var textures = StrList(spec, "textures");
            if (textures.Count > 0) o.Add("textures", textures.Select(t => (object)new JObj().Add("bedrock", Tex(t)).Add("tf", TfTex(t))).ToList());
            var notes = StrList(spec, "notes");
            if (notes.Count > 0) o.Add("notes", notes.Cast<object>().ToList());
            Put(UnitDir(unit) + "bones/" + name + ".json", o);
            var entry = new JObj().Add("identifier", Str(spec, "vanilla")).Add("vanilla", true).Add("unit", unit).Add("kind", "entity");
            var ents = StrList(spec, "entity");
            if (ents.Count > 0) entry.Add("entity", ents.Cast<object>().ToList());
            entry.Add("textures", textures.Select(t => (object)Tex(t)).ToList());
            var preview = Prop(spec, "preview");
            if (preview.ValueKind == JsonValueKind.Object) entry.Add("preview", ToJObj(preview));
            entry.Add("layer", name);
            indexGeometries.Add(entry);
            Log.Add(string.Format("{0,-28} {1,-14} vanilla {2}", name, unit, Str(spec, "vanilla")));
        }

        // ---- armor and Travellers' Gear ---------------------------------------------------------------------

        static readonly Dictionary<string, string[]> SlotParts = new Dictionary<string, string[]> {
            { "helmet", new[] { "head", "hat" } }, { "chestplate", new[] { "body", "right_arm", "left_arm" } },
            { "leggings", new[] { "body", "right_leg", "left_leg" } }, { "boots", new[] { "right_leg", "left_leg" } },
        };
        static readonly Dictionary<string, string> SlotVar = new Dictionary<string, string> {
            { "helmet", "helmet_layer_visible" }, { "chestplate", "chest_layer_visible" }, { "leggings", "leg_layer_visible" }, { "boots", "boot_layer_visible" },
        };
        static readonly Dictionary<string, string> VanillaArmorGeo = new Dictionary<string, string> {
            { "helmet", "geometry.humanoid.armor.helmet" }, { "chestplate", "geometry.humanoid.armor.chestplate" },
            { "leggings", "geometry.humanoid.armor.leggings" }, { "boots", "geometry.humanoid.armor.boots" },
        };

        void Gear(JsonElement a, string onlyClass, string onlyUnit, string onlyItem)
        {
            string unit = Str(a, "unit");
            if (onlyUnit != null && !string.Equals(onlyUnit, unit, StringComparison.OrdinalIgnoreCase)) return;
            string material = Str(a, "material");
            var slots = StrList(a, "slots");
            if (slots.Count == 0) slots = new List<string> { "helmet", "chestplate", "leggings", "boots" };
            bool vanilla = Bool(a, "vanilla");
            var pieces = Prop(a, "pieces");
            if (pieces.ValueKind == JsonValueKind.Object && StrList(a, "slots").Count == 0) slots = pieces.EnumerateObject().Select(x => x.Name).ToList();
            foreach (var key in slots)
            {
                var piece = Prop(pieces, key);
                string slot = Str(piece, "slot") ?? key; // a piece name (Travellers' Gear) or the slot itself
                string item = Str(piece, "item") ?? Str(a, "items")?.Replace("{slot}", slot);
                if (onlyItem != null && item != onlyItem) continue;
                string layerNo = slot == "leggings" ? "2" : "1";
                string tex = Str(piece, "texture") ?? Str(a, "texture")?.Replace("{layer}", layerNo);
                string geoId;
                string geoName = Str(piece, "name") ?? material + "." + key;
                if (vanilla) geoId = VanillaArmorGeo[slot];
                else
                {
                    string layerName = Str(piece, "layer") ?? (slot == "leggings" ? Str(a, "inner") : Str(a, "outer"));
                    LayerInfo info;
                    if (layerName == null || !Layers.TryGetValue(layerName, out info) || info.Def == null) { Errors.Add("armor " + material + " " + slot + ": layer '" + layerName + "' missing"); continue; }
                    if (onlyClass != null && !Matches(onlyClass, null, info, unit)) continue;
                    var parts = StrList(piece, "parts");
                    if (parts.Count == 0) parts = SlotParts[slot].ToList();
                    var opt = new ConvertOptions { Identifier = geoPrefix + geoName, RootFilter = new HashSet<string>(parts) };
                    foreach (var kv in Converter.HumanoidNames) opt.Rename[kv.Key] = kv.Value;
                    var def = Filtered(info.Def, StrList(piece, "exclude"), StrList(piece, "skipCubes"));
                    var geo = Converter.Convert(def, opt);
                    geoId = geo.Identifier;
                    string file = "models/entity/" + geoName + ".geo.json";
                    Put(UnitDir(unit) + "rp/" + file, geo.ToJson());
                    var bm = new JObj()
                        .Add("comment", "Generated by tools/models/convert.ps1 (F2) from The Twilight Forest 4.8 @ " + TfRevision + " (LGPL-2.1-or-later code). Armor bones carry the player's bone names, so the attachable follows the wearer.")
                        .Add("geometry", geoId)
                        .Add("layer", new JObj().Add("name", layerName).Add("field", "TFModelLayers." + info.Field).Add("supplier", info.Supplier))
                        .Add("slot", slot).Add("parts", parts.Cast<object>().ToList())
                        .Add("bones", geo.Bones.Select(b => (object)new JObj().Add("java", b.JavaPath).Add("bone", b.Name).Add("parent", b.Parent).Add("cubes", b.Cubes.Count)).ToList());
                    var gnotes = StrList(piece, "notes").Concat(StrList(a, "notes")).ToList();
                    if (gnotes.Count > 0) bm.Add("notes", gnotes.Cast<object>().ToList());
                    Put(UnitDir(unit) + "bones/" + geoName + ".json", bm);
                    indexGeometries.Add(new JObj().Add("identifier", geoId).Add("file", file).Add("unit", unit).Add("kind", "armor")
                        .Add("textures", Json.Arr(Tex(tex))).Add("texture_width", geo.TexWidth).Add("texture_height", geo.TexHeight)
                        .Add("bones", geo.Bones.Count).Add("cubes", geo.Bones.Sum(b => b.Cubes.Count))
                        .Add("visible_bounds", Json.Vec(geo.BoundsWidth, geo.BoundsHeight, geo.BoundsOffsetY)));
                    Log.Add(string.Format("{0,-28} {1,-14} {2,3} bones {3,4} cubes", geoName, unit, geo.Bones.Count, geo.Bones.Sum(b => b.Cubes.Count)));
                }
                if (item == null) continue;
                var desc = new JObj()
                    .Add("identifier", item)
                    .Add("materials", new JObj().Add("default", "armor").Add("enchanted", "armor_enchanted"))
                    .Add("textures", new JObj().Add("default", Tex(tex)).Add("enchanted", "textures/misc/enchanted_item_glint"))
                    .Add("geometry", new JObj().Add("default", geoId))
                    .Add("scripts", new JObj().Add("parent_setup", "variable." + SlotVar[slot] + " = 0.0;"))
                    .Add("render_controllers", Json.Arr("controller.render.armor"));
                string afile = "attachables/" + item.Substring(item.IndexOf(':') + 1) + ".json";
                Put(UnitDir(unit) + "rp/" + afile, new JObj().Add("format_version", "1.10.0").Add("minecraft:attachable", new JObj().Add("description", desc)));
                indexAttachables.Add(new JObj().Add("item", item).Add("file", afile).Add("unit", unit).Add("slot", slot).Add("geometry", Json.Arr(geoId)).Add("textures", Json.Arr(Tex(tex))));
            }
        }

        // A copy of a layer without some parts (exclude) / without the cubes of some parts (skipCubes).
        static LayerDef Filtered(LayerDef def, List<string> exclude, List<string> skipCubes)
        {
            if (exclude.Count == 0 && skipCubes.Count == 0) return def;
            Func<PartDef, string, PartDef> copy = null;
            copy = (p, path) =>
            {
                var n = new PartDef { Pose = p.Pose, Cubes = skipCubes.Contains(path) ? new List<CubeDef>() : p.Cubes };
                foreach (var kv in p.Children)
                {
                    string cp = path == "" ? kv.Key : path + "/" + kv.Key;
                    if (exclude.Contains(cp)) continue;
                    n.Children.Add(new KeyValuePair<string, PartDef>(kv.Key, copy(kv.Value, cp)));
                }
                return n;
            };
            return new LayerDef { Mesh = new MeshDef { Root = copy(def.Mesh.Root, "") }, TexWidth = def.TexWidth, TexHeight = def.TexHeight };
        }

        // ---- extracts: parts rendered with PoseStack transforms (trophies, worn and held items) -----------

        // The frame of an extract: how a point of the PoseStack's space (blocks) lands in Bedrock geometry
        // coordinates (pixels): b = A q + c, and the bone it hangs from.
        sealed class FrameSpec { public M3 A; public V3 C; public string Parent; }

        static FrameSpec FrameOf(string frame)
        {
            switch (frame)
            {
                // TrophyRenderer (blocks, y up) for an entity / block facing south at yaw 0
                case "world": return new FrameSpec { A = M3.Diag(16, 16, -16), C = new V3(0, 0, 0) };
                // the wearer's head part (HumanoidModel head pivot 0, 0, 0)
                case "head": return new FrameSpec { A = M3.Diag(16, -16, 16), C = new V3(0, 24, 0), Parent = "head" };
                // the wearer's right arm part (pivot -5, 2, 0)
                case "rightArm": return new FrameSpec { A = M3.Diag(16, -16, 16), C = new V3(-5, 22, 0), Parent = "rightArm" };
            }
            throw new JavaEvalException("unknown extract frame '" + frame + "'");
        }

        Pose OpsPose(JsonElement chainHolder, string key, Pose start)
        {
            var p = start ?? new Pose();
            var ops = Prop(chainHolder, key);
            if (ops.ValueKind != JsonValueKind.Array) return p;
            foreach (var op in ops.EnumerateArray())
            {
                if (op.ValueKind == JsonValueKind.String)
                {
                    // a named chain from "opChains"
                    p = OpsPose(Prop(cat, "opChains"), op.GetString(), p);
                    continue;
                }
                var a = op.EnumerateArray().ToList();
                string name = a[0].GetString();
                Func<int, double> n = i => a[i].GetDouble();
                switch (name)
                {
                    case "translate": p.Translate(n(1), n(2), n(3)); break;
                    case "scale": p.Scale(n(1), n(2), n(3)); break;
                    case "rotateX": p.Rotate(M3.RotX(n(1) * Math.PI / 180)); break;
                    case "rotateY": p.Rotate(M3.RotY(n(1) * Math.PI / 180)); break;
                    case "rotateZ": p.Rotate(M3.RotZ(n(1) * Math.PI / 180)); break;
                    // Quaternionf.rotationXYZ: Rx * Ry * Rz (ItemTransform)
                    case "rotateXYZ": p.Rotate(M3.RotX(n(1) * Math.PI / 180) * M3.RotY(n(2) * Math.PI / 180) * M3.RotZ(n(3) * Math.PI / 180)); break;
                    default: throw new JavaEvalException("unknown op '" + name + "'");
                }
            }
            return p;
        }

        // Java expression (float semantics) with optional variables.
        double EvalJava(string src, Dictionary<string, object> vars)
        {
            var e = Parser.ParseExpression(src);
            var f = new Frame(null);
            if (vars != null) foreach (var kv in vars) f.Declare(kv.Key, null, kv.Value);
            return Interp.ToDouble(Interp.Eval(e, f));
        }

        Dictionary<string, double[]> PoseOverrides(JsonElement x)
        {
            var map = new Dictionary<string, double[]>();
            var pose = Prop(x, "pose");
            if (pose.ValueKind != JsonValueKind.Array) return map;
            foreach (var p in pose.EnumerateArray())
            {
                var range = Prop(p, "range");
                int from = 0, to = 0;
                bool hasRange = range.ValueKind == JsonValueKind.Array;
                if (hasRange) { from = range[0].GetInt32(); to = range[1].GetInt32(); }
                for (int i = from; i <= to; i++)
                {
                    var vars = new Dictionary<string, object> { { "i", i } };
                    string part = Str(p, "part").Replace("{i}", i.ToString(CultureInfo.InvariantCulture));
                    double[] cur;
                    if (!map.TryGetValue(part, out cur)) map[part] = cur = new[] { double.NaN, double.NaN, double.NaN };
                    string[] axes = { "xRot", "yRot", "zRot" };
                    for (int k = 0; k < 3; k++)
                    {
                        string expr = Str(p, axes[k]);
                        if (expr != null) cur[k] = EvalJava(expr, vars);
                    }
                }
            }
            return map;
        }

        void Extract(JsonElement x, string onlyClass, string onlyUnit, string onlyItem)
        {
            string name = Str(x, "name");
            string unit = Str(x, "unit");
            string layerName = Str(x, "layer");
            if (onlyUnit != null && !string.Equals(onlyUnit, unit, StringComparison.OrdinalIgnoreCase)) return;
            LayerInfo info;
            if (!Layers.TryGetValue(layerName, out info) || info.Def == null) { Errors.Add("extract " + name + ": layer '" + layerName + "' missing"); return; }
            if (onlyClass != null && !Matches(onlyClass, null, info, unit)) return;
            string item = Str(x, "item");
            if (onlyItem != null && item != onlyItem) return;
            string frameName = Str(x, "frame", "world");
            var frame = FrameOf(frameName);
            var overrides = PoseOverrides(x);
            var geoTex = new Dictionary<string, string>();
            var geoMaterial = new Dictionary<string, string>();
            var boneMaps = new List<object>();
            var byGeo = new Dictionary<string, List<JsonElement>>();
            var order = new List<string>();
            foreach (var g in Prop(x, "groups").EnumerateArray())
            {
                string gname = Str(g, "geometry") ?? name;
                List<JsonElement> l;
                if (!byGeo.TryGetValue(gname, out l)) { byGeo[gname] = l = new List<JsonElement>(); order.Add(gname); }
                l.Add(g);
            }
            foreach (var gname in order)
            {
                var used = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                var bonesJson = new List<object>();
                GeometryOut first = null;
                int cubes = 0, boneCount = 0, gi = 0;
                foreach (var g in byGeo[gname])
                {
                    var pose = OpsPose(x, "prefix", null);
                    pose = OpsPose(g, "ops", pose);
                    // b = A (L p / 16 + T) + C = K p + D;  K = s R E  (E = diag(1, -1, 1))
                    var K = (1.0 / 16) * (frame.A * pose.L);
                    var D = frame.A * pose.T + frame.C;
                    double s = Math.Pow(Math.Abs(K.Det()), 1.0 / 3.0);
                    var R = (1.0 / s) * (K * M3.Diag(1, -1, 1));
                    if (R.Det() < 0) { Errors.Add("extract " + name + ": the transform mirrors the model (not supported)"); return; }
                    var check = R * R.T();
                    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++)
                        if (Math.Abs(check.A[r, c] - (r == c ? 1 : 0)) > 1e-4) { Errors.Add("extract " + name + ": the transform is not a uniform scale and a rotation"); return; }
                    var d = R.T() * D;
                    string wrapper = Str(g, "bone") ?? (gname.Replace('.', '_') + (gi > 0 ? "_" + gi : ""));
                    used.Add(wrapper);
                    var wb = new JObj().Add("name", wrapper);
                    if (frame.Parent != null) wb.Add("parent", frame.Parent);
                    wb.Add("pivot", Json.Vec(0, 0, 0));
                    var eul = Converter.BedrockEuler(R);
                    if (Math.Abs(eul.X) > 1e-4 || Math.Abs(eul.Y) > 1e-4 || Math.Abs(eul.Z) > 1e-4) wb.Add("rotation", Json.Vec(eul.X, eul.Y, eul.Z));
                    bonesJson.Add(wb);
                    boneCount++;
                    var opt = new ConvertOptions { Identifier = geoPrefix + gname, RootFilter = new HashSet<string>(StrList(g, "parts")), BoneParentOfRoots = wrapper, RotOverride = overrides };
                    double sc = s;
                    Func<V3, V3> map = p => new V3(sc * p.X + d.X, -sc * p.Y + d.Y, sc * p.Z + d.Z);
                    var geo = ConvertWith(info.Def, opt, map, sc, used);
                    if (first == null) first = geo;
                    var gj = geo.ToJson();
                    bonesJson.AddRange((List<object>)((JObj)((List<object>)gj.Get("minecraft:geometry"))[0]).Get("bones"));
                    boneCount += geo.Bones.Count;
                    cubes += geo.Bones.Sum(b => b.Cubes.Count);
                    foreach (var b in geo.Bones)
                    {
                        var piv = geo.MapPoint(b.JavaPivot);
                        boneMaps.Add(new JObj().Add("geometry", geoPrefix + gname).Add("java", b.JavaPath).Add("bone", b.Name).Add("parent", b.Parent)
                            .Add("pivot", Json.Vec(piv.X, piv.Y, piv.Z)).Add("rotation", Json.Vec(GeometryOut.Deg(b.Rot.X), GeometryOut.Deg(b.Rot.Y), GeometryOut.Deg(b.Rot.Z)))
                            .Add("cubes", b.Cubes.Count));
                    }
                    geoTex[gname] = Str(g, "texture");
                    geoMaterial[gname] = Str(g, "material", "entity_alphatest");
                    gi++;
                }
                var json = first.ToJson();
                var geoObj = (JObj)((List<object>)json.Get("minecraft:geometry"))[0];
                geoObj.Set("bones", bonesJson);
                var desc = (JObj)geoObj.Get("description");
                if (frame.Parent != null)
                {
                    // attachables: generous bounds around the wearer
                    desc.Set("visible_bounds_width", 3.0);
                    desc.Set("visible_bounds_height", 3.5);
                    desc.Set("visible_bounds_offset", Json.Vec(0, 1.25, 0));
                }
                string file = "models/entity/" + gname + ".geo.json";
                Put(UnitDir(unit) + "rp/" + file, json);
                var entry = new JObj().Add("identifier", geoPrefix + gname).Add("file", file).Add("unit", unit).Add("kind", Str(x, "kind", "extract"))
                    .Add("textures", Json.Arr(Tex(geoTex[gname]))).Add("texture_width", first.TexWidth).Add("texture_height", first.TexHeight)
                    .Add("bones", boneCount).Add("cubes", cubes)
                    .Add("visible_bounds", Json.Vec(Convert.ToDouble(desc.Get("visible_bounds_width")), Convert.ToDouble(desc.Get("visible_bounds_height")), Convert.ToDouble(((List<object>)desc.Get("visible_bounds_offset"))[1])))
                    .Add("frame", frameName);
                if (frame.Parent != null) entry.Add("external_parents", Json.Arr(frame.Parent));
                foreach (var key in new[] { "block", "item" })
                {
                    var refs = StrList(x, key);
                    if (refs.Count > 0) entry.Add(key, refs.Cast<object>().ToList());
                }
                var preview = Prop(x, "preview");
                if (preview.ValueKind == JsonValueKind.Object) entry.Add("preview", ToJObj(preview));
                indexGeometries.Add(entry);
                Log.Add(string.Format("{0,-28} {1,-14} {2,3} bones {3,4} cubes (extract of {4})", gname, unit, boneCount, cubes, layerName));
            }
            var bm = new JObj()
                .Add("comment", "Generated by tools/models/convert.ps1 (F2) from The Twilight Forest 4.8 @ " + TfRevision + " (LGPL-2.1-or-later code): parts of a TF model drawn with the PoseStack transforms of its renderer.")
                .Add("source", Str(x, "source"))
                .Add("layer", new JObj().Add("name", layerName).Add("field", "TFModelLayers." + info.Field).Add("supplier", info.Supplier))
                .Add("frame", frameName)
                .Add("geometries", order.Select(gn => (object)new JObj().Add("identifier", geoPrefix + gn).Add("texture", new JObj().Add("bedrock", Tex(geoTex[gn])).Add("tf", TfTex(geoTex[gn]))).Add("material", geoMaterial[gn])).ToList())
                .Add("bones", boneMaps);
            var notes = StrList(x, "notes");
            if (notes.Count > 0) bm.Add("notes", notes.Cast<object>().ToList());
            Put(UnitDir(unit) + "bones/" + name + ".json", bm);

            if (item != null) ExtractAttachable(x, unit, item, order, geoTex, geoMaterial);
        }

        GeometryOut ConvertWith(LayerDef def, ConvertOptions opt, Func<V3, V3> map, double scale, HashSet<string> used)
        {
            var geo = Converter.Convert(def, opt, map, scale);
            // keep bone names unique across the groups of one geometry
            foreach (var b in geo.Bones)
            {
                if (used.Contains(b.Name))
                {
                    string n = b.Name;
                    for (int k = 2; used.Contains(n); k++) n = b.Name + "_" + k;
                    foreach (var c in geo.Bones) if (c.Parent == b.Name) c.Parent = n;
                    b.Name = n;
                }
                used.Add(b.Name);
            }
            return geo;
        }

        void ExtractAttachable(JsonElement x, string unit, string item, List<string> geos, Dictionary<string, string> geoTex, Dictionary<string, string> geoMaterial)
        {
            string slotCond = Str(x, "slotCondition");
            var materials = new JObj(); var textures = new JObj(); var geometry = new JObj();
            var controllers = new List<object>();
            for (int i = 0; i < geos.Count; i++)
            {
                string key = i == 0 ? "default" : "part" + i;
                materials.Add(key, geoMaterial[geos[i]]);
                textures.Add(key, Tex(geoTex[geos[i]]));
                geometry.Add(key, geoPrefix + geos[i]);
                string rc = i == 0 ? "controller.render.item_default" : "controller.render.twilightforest.models." + key;
                if (i > 0 && !renderControllers.ContainsKey(rc))
                {
                    renderControllers[rc] = new JObj()
                        .Add("geometry", "Geometry." + key)
                        .Add("materials", Json.Arr(new JObj().Add("*", "Material." + key)))
                        .Add("textures", Json.Arr("Texture." + key));
                }
                controllers.Add(slotCond != null ? (object)new JObj().Add(rc, slotCond) : rc);
            }
            var desc = new JObj().Add("identifier", item).Add("materials", materials).Add("textures", textures).Add("geometry", geometry).Add("render_controllers", controllers);
            string afile = "attachables/" + item.Substring(item.IndexOf(':') + 1) + ".json";
            Put(UnitDir(unit) + "rp/" + afile, new JObj().Add("format_version", "1.10.0").Add("minecraft:attachable", new JObj().Add("description", desc)));
            indexAttachables.Add(new JObj().Add("item", item).Add("file", afile).Add("unit", unit).Add("slot", Str(x, "slot", "head"))
                .Add("geometry", geos.Select(g => (object)(geoPrefix + g)).ToList()).Add("textures", geos.Select(g => (object)Tex(geoTex[g])).ToList()));
        }

        // ---- shared files ----------------------------------------------------------------------------------

        void WriteShared()
        {
            if (renderControllers.Count > 0)
            {
                var rcs = new JObj();
                foreach (var kv in renderControllers) rcs.Add(kv.Key, kv.Value);
                Put(UnitDir("models") + "rp/render_controllers/twilightforest.models.render_controllers.json", new JObj().Add("format_version", "1.10.0").Add("render_controllers", rcs));
            }
            var index = new JObj()
                .Add("comment", "Generated by tools/models/convert.ps1 (F2): every geometry, base animation and attachable of assets/gen/geo, for the models.* selftests and the e2e preview. Never edit.")
                .Add("format", 1)
                .Add("tf", TfRevision)
                .Add("texture_prefix", texPrefix)
                .Add("geometries", indexGeometries)
                .Add("animations", indexAnimations)
                .Add("attachables", indexAttachables);
            Put(UnitDir("models") + "data/gf/models/index.json", index);
        }
    }
}
