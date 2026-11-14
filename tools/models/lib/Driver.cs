// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Driver of the model converter (contract "geo", unit F2): reads tools/models/catalog.json,
// evaluates every LayerDefinition upstream registers (ClientRegistrationEvents.registerLayerDefinitions)
// and writes, per owning unit,
//   assets/gen/geo/<unit>/rp/models/entity/<name>.geo.json     geometry.gloamingforest.<name>
//   assets/gen/geo/<unit>/rp/animations/<name>.base.json       animation.gloamingforest.<name>.base
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
        public string Name;          // upstream model layer name (ModelLayerLocation path + "_" + layer unless "main")
        public string Field;         // TFModelLayers field
        public string Supplier;      // Java source of the supplier
        public string SupplierClass; // the upstream class of a Cls::method supplier
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
        // upstream's item models (display transforms): the datagen output first, then the hand-written ones
        readonly string[] itemModelDirs;
        public string TfRevision = "9e40a757";

        // What a file was written for (the -Java / -Item selection of a full run, Select).
        sealed class Origin { public LayerInfo Info; public string Unit, AnimClass, Item; }
        Origin cur;
        readonly Dictionary<string, Origin> origins = new Dictionary<string, Origin>(StringComparer.Ordinal);

        public Generator(string tfJavaRoot, string catalogJson)
        {
            ClassPath = new ClassPath(tfJavaRoot);
            Interp = new Interp(ClassPath);
            cat = JsonDocument.Parse(catalogJson, new JsonDocumentOptions { CommentHandling = JsonCommentHandling.Skip, AllowTrailingCommas = true }).RootElement;
            texPrefix = Str(cat, "texturePrefix", "textures/gloamingforest/");
            geoPrefix = Str(cat, "geometryPrefix", "geometry.gloamingforest.");
            animPrefix = Str(cat, "animationPrefix", "animation.gloamingforest.");
            tfPrefix = "src/main/resources/assets/gloamingforest/textures/";
            string src = Path.GetFullPath(Path.Combine(tfJavaRoot, "..", ".."));
            itemModelDirs = new[] {
                Path.Combine(src, "generated", "resources", "assets", "gloamingforest", "models", "item"),
                Path.Combine(src, "main", "resources", "assets", "gloamingforest", "models", "item"),
            };
            BaseCases = ParseBaseCases(Prop(cat, "baseCase"), Errors);
        }

        // catalog.json "baseCase": {"values": [{scope, expr, value, fixed?, why}]} (see BaseCaseValue).
        public readonly List<BaseCaseValue> BaseCases;

        public static List<BaseCaseValue> ParseBaseCases(JsonElement section, List<string> errors)
        {
            var list = new List<BaseCaseValue>();
            var values = Prop(section, "values");
            if (values.ValueKind != JsonValueKind.Array) return list;
            int k = 0;
            foreach (var v in values.EnumerateArray())
            {
                k++;
                var b = new BaseCaseValue { Scope = Str(v, "scope"), Expr = Str(v, "expr"), Fixed = Bool(v, "fixed"), Why = Str(v, "why") };
                var val = Prop(v, "value");
                switch (val.ValueKind)
                {
                    case JsonValueKind.String: b.Value = val.GetString(); break;
                    case JsonValueKind.Number: b.Value = val.GetDouble(); break;
                    case JsonValueKind.True: b.Value = true; break;
                    case JsonValueKind.False: b.Value = false; break;
                    case JsonValueKind.Null: b.Value = "null"; break;
                }
                if (b.Scope == null || b.Expr == null || b.Value == null || string.IsNullOrEmpty(b.Why))
                {
                    errors.Add("catalog baseCase entry " + k + ": scope, expr, value and why are required");
                    continue;
                }
                if (list.Any(x => x.Scope == b.Scope && x.Expr == b.Expr)) { errors.Add("catalog baseCase entry " + k + ": " + b.Scope + " / " + b.Expr + " is listed twice"); continue; }
                list.Add(b);
            }
            return list;
        }

        // A setupAnim translator with the catalog's base-case values.
        public AnimTranslator NewTranslator() { return new AnimTranslator(ClassPath, Interp) { BaseCases = BaseCases }; }

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
            if (cls == null) throw new JavaEvalException("ClientRegistrationEvents.java not found in the upstream checkout");
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
                catch (Exception ex) when (ex is InvalidCastException || ex is NullReferenceException || ex is ArgumentException || ex is IndexOutOfRangeException || ex is KeyNotFoundException)
                {
                    info.Error = ex.GetType().Name + " (a builtin got arguments it does not expect): " + ex.Message;
                }
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
            attachables.Clear(); attachableOrder.Clear(); origins.Clear();
            var layersCat = Prop(cat, "layers");
            // every registered layer must be described in the catalog (no silent gaps)
            foreach (var l in Layers.Values)
            {
                if (layersCat.ValueKind != JsonValueKind.Object || !layersCat.TryGetProperty(l.Name, out _))
                    Errors.Add("layer '" + l.Name + "' (TFModelLayers." + l.Field + ") is not in tools/models/catalog.json");
            }
            foreach (var p in layersCat.EnumerateObject())
            {
                if (!Layers.ContainsKey(p.Name)) { Errors.Add("catalog layer '" + p.Name + "' is not registered by upstream"); continue; }
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
                foreach (var x in extracts.EnumerateArray())
                    foreach (var spec in ExtractSpecs(x)) Extract(spec, onlyClass, onlyUnit, onlyItem);
            // one attachable per item, with the geometry of every slot it is drawn in
            foreach (var acc in attachableOrder)
                if (acc.Failed) Errors.Add("attachable " + acc.Item + ": a form failed, not written"); else WriteExtractAttachable(acc);
            cur = null;
            if (onlyClass == null && onlyUnit == null && onlyItem == null) WriteShared();
        }

        // The files of a full run that a -Java (model class) / -Item filter selects: the files written for
        // a matching layer class or animation class, or for the item ("*": any item).
        public List<string> Select(string onlyClass, string onlyItem)
        {
            if (string.IsNullOrEmpty(onlyClass)) onlyClass = null;
            if (string.IsNullOrEmpty(onlyItem)) onlyItem = null;
            var list = new List<string>();
            foreach (var kv in origins)
            {
                var o = kv.Value;
                if (onlyItem != null && (o.Item == null || (onlyItem != "*" && o.Item != onlyItem))) continue;
                if (onlyClass != null && (o.Info == null || !Matches(onlyClass, null, o.Info, o.Unit, o.AnimClass))) continue;
                list.Add(kv.Key);
            }
            list.Sort(StringComparer.Ordinal);
            return list;
        }

        bool Matches(string onlyClass, string onlyUnit, LayerInfo info, string unit, string animClass = null)
        {
            if (onlyUnit != null && !string.Equals(onlyUnit, unit, StringComparison.OrdinalIgnoreCase)) return false;
            if (onlyClass != null)
            {
                bool hit = string.Equals(info.SupplierClass, onlyClass, StringComparison.Ordinal) || string.Equals(animClass, onlyClass, StringComparison.Ordinal)
                           || SupplierNames(info.Supplier, onlyClass);
                if (!hit) return false;
            }
            return true;
        }

        // The supplier names the class as a whole identifier ("YetiModel::create", "() -> YetiModel.x(...)"),
        // not as the end of a longer one ("AlphaYetiModel::create", "TFGhastModel::create" for GhastModel).
        public static bool SupplierNames(string supplier, string cls)
        {
            if (supplier == null || string.IsNullOrEmpty(cls)) return false;
            return System.Text.RegularExpressions.Regex.IsMatch(supplier, @"(^|[^A-Za-z0-9_$])" + System.Text.RegularExpressions.Regex.Escape(cls) + @"(::|\.)");
        }

        // An upstream texture (path below assets/gloamingforest/textures/) as a Bedrock texture path; "minecraft:<path>" names a vanilla one.
        string Tex(string tfPath)
        {
            if (tfPath == null) return null;
            if (tfPath.StartsWith("minecraft:", StringComparison.Ordinal)) return "textures/" + Path.ChangeExtension(tfPath.Substring(10), null).Replace('\\', '/');
            return texPrefix + Path.ChangeExtension(tfPath, null).Replace('\\', '/');
        }
        string TfTex(string tfPath) { return tfPath != null && tfPath.StartsWith("minecraft:", StringComparison.Ordinal) ? "(vanilla) " + tfPath : tfPrefix + tfPath; }

        static string UnitDir(string unit) { return "assets/gen/geo/" + unit + "/"; }

        void Put(string path, object json)
        {
            Files[path] = Json.Write(json);
            if (cur != null) origins[path] = cur; else origins.Remove(path);
        }

        // ---- entity / block / item layers ----------------------------------------------------------------

        void ConvertLayer(string name, JsonElement spec, LayerInfo info)
        {
            string unit = Str(spec, "unit");
            string kind = Str(spec, "kind", "entity");
            string id = geoPrefix + name;
            var opt = new ConvertOptions { Identifier = id };
            string animClass = Str(spec, "animClass") ?? info.SupplierClass;
            // the vanilla skeleton names only in the vanilla family of the layer's model class
            var modelCls = animClass != null ? ClassPath.Find(animClass, null) : null;
            foreach (var kv in Converter.RenamesFor(modelCls != null ? NewTranslator().VanillaBaseOf(modelCls) : null)) opt.Rename[kv.Key] = kv.Value;
            var def = info.Def;
            var size = Prop(spec, "textureSize");
            if (size.ValueKind == JsonValueKind.Array)
            {
                // the Bedrock texture the layer is drawn with has another size (UVs stay in pixels)
                def = new LayerDef { Mesh = def.Mesh, TexWidth = size[0].GetInt32(), TexHeight = size[1].GetInt32() };
            }
            var items = StrList(spec, "item");
            cur = new Origin { Info = info, Unit = unit, AnimClass = Str(spec, "animClass"), Item = items.Count > 0 ? items[0] : null };
            var geo = Converter.Convert(def, opt);
            string geoFile = "models/entity/" + name + ".geo.json";
            Put(UnitDir(unit) + "rp/" + geoFile, geo.ToJson());

            // setupAnim -> base animation: a stub for every entity layer (the owning unit translates the
            // rest of setupAnim by hand on top of it), with the statements the translator could do
            AnimResult anim = null;
            string animId = null;
            if (kind == "entity")
            {
                if (animClass != null && !Bool(spec, "noAnim"))
                {
                    var ac = ClassPath.Find(animClass, null);
                    if (ac == null) Errors.Add("layer '" + name + "': animClass " + animClass + " not found");
                    else anim = NewTranslator().Translate(ac, geo);
                }
                if (anim == null)
                {
                    anim = new AnimResult { Method = "none" };
                    anim.Notes.Add(Bool(spec, "noAnim")
                        ? "no setupAnim to translate: the layer is posed by its renderer (see the notes and docs/spec/models.md)"
                        : "no model class with a setupAnim");
                }
                if (anim.Bones.Count == 0 && geo.Bones.Count > 0)
                {
                    // a stub: Bedrock rejects an animation whose "bones" is empty ("Required child ... not
                    // found"), so it holds a zero rotation on the first bone (no visible effect)
                    anim.Bones[geo.Bones[0].Name] = new Dictionary<string, MNode[]> { { "rotation", new MNode[] { M.Num(0), M.Num(0), M.Num(0) } } };
                    anim.Notes.Add("stub: a zero rotation on " + geo.Bones[0].Name + " (Bedrock needs at least one bone in an animation)");
                }
                animId = animPrefix + name + ".base";
                Put(UnitDir(unit) + "rp/animations/" + name + ".base.json", AnimTranslator.AnimationJson(animId, anim));
                indexAnimations.Add(new JObj().Add("identifier", animId).Add("file", "animations/" + name + ".base.json").Add("unit", unit)
                    .Add("bones", anim.Bones.Keys.Cast<object>().ToList()));
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
                .Add("comment", "Generated by tools/models/convert.ps1 (F2) from the upstream 4.8 @ " + TfRevision + " (LGPL-2.1-or-later code). Never edit: regenerate.")
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
                a.Add("translated", anim.Statements.Where(s => s.Translated).Select(s => (object)((s.Class != null ? s.Class + ": " : "") + s.Java)).ToList());
                a.Add("todo", anim.Statements.Where(s => !s.Translated).Select(s => (object)TodoJson(s)).ToList());
                if (anim.OverridesVanilla.Count > 0)
                    a.Add("overrides_vanilla", anim.OverridesVanilla.Select(kv => (object)new JObj()
                        .Add("bone", kv.Key.Substring(0, kv.Key.LastIndexOf('.'))).Add("property", kv.Key.Substring(kv.Key.LastIndexOf('.') + 1))
                        .Add("lines", kv.Value.Cast<object>().ToList())).ToList());
                o.Add("setupAnim", a);
            }
            var notes = StrList(spec, "notes");
            if (notes.Count > 0) o.Add("notes", notes.Cast<object>().ToList());
            return o;
        }

        // A todo entry: the Java line(s), why, and for a case entry the channels that differ when it holds.
        static JObj TodoJson(AnimStatement s)
        {
            var o = new JObj().Add("line", s.Line);
            if (s.EndLine > s.Line) o.Add("end_line", s.EndLine);
            if (s.Class != null) o.Add("class", s.Class);
            o.Add("java", s.Java).Add("why", s.Reason);
            if (s.Case) o.Add("affects", s.Affects.Cast<object>().ToList());
            return o;
        }

        void AddVanilla(string name, JsonElement spec, LayerInfo info)
        {
            string unit = Str(spec, "unit");
            cur = new Origin { Info = info, Unit = unit, AnimClass = Str(spec, "animClass") };
            var o = new JObj()
                .Add("comment", "Generated by tools/models/convert.ps1 (F2). This upstream layer uses a vanilla Minecraft model: use the vanilla Bedrock geometry.")
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
            if (vanilla && onlyClass != null) return; // vanilla-model armor has no upstream model class
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
                cur = new Origin { Unit = unit, Item = item };
                if (vanilla) geoId = VanillaArmorGeo[slot];
                else
                {
                    string layerName = Str(piece, "layer") ?? (slot == "leggings" ? Str(a, "inner") : Str(a, "outer"));
                    LayerInfo info;
                    if (layerName == null || !Layers.TryGetValue(layerName, out info) || info.Def == null) { Errors.Add("armor " + material + " " + slot + ": layer '" + layerName + "' missing"); continue; }
                    if (onlyClass != null && !Matches(onlyClass, null, info, unit)) continue;
                    cur.Info = info;
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
                        .Add("comment", "Generated by tools/models/convert.ps1 (F2) from the upstream 4.8 @ " + TfRevision + " (LGPL-2.1-or-later code). Armor bones carry the player's bone names, so the attachable follows the wearer.")
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
                indexAttachables.Add(new JObj().Add("item", item).Add("file", afile).Add("unit", unit).Add("slot", slot).Add("slots", Json.Arr(slot))
                    .Add("geometry", Json.Arr(geoId)).Add("textures", Json.Arr(Tex(tex))));
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
                // the wearer's left arm part (pivot 5, 2, 0)
                case "leftArm": return new FrameSpec { A = M3.Diag(16, -16, 16), C = new V3(5, 22, 0), Parent = "leftArm" };
            }
            throw new JavaEvalException("unknown extract frame '" + frame + "'");
        }

        // One extract to write: a catalog entry, or one item form of it (catalog "itemForms": worn on the
        // head, held in the right hand, held in the left hand), named <entry>_<form suffix>.
        sealed class ExtractSpec
        {
            public JsonElement X;          // the catalog entry: layer, pose, groups, notes, animation
            public string Name, Unit, Layer, Frame, Item, ItemModel, Slot, SlotCondition, Kind, Source, FormKey;
            public JsonElement Prefix;     // the PoseStack ops before the groups' own
        }

        List<ExtractSpec> ExtractSpecs(JsonElement x)
        {
            var list = new List<ExtractSpec>();
            string name = Str(x, "name"), item = Str(x, "item");
            string itemModel = Str(x, "itemModel") ?? (item != null ? item.Substring(item.IndexOf(':') + 1) : null);
            string formsName = Str(x, "forms");
            if (formsName == null)
            {
                string slot = Str(x, "slot");
                list.Add(new ExtractSpec {
                    X = x, Name = name, Unit = Str(x, "unit"), Layer = Str(x, "layer"), Frame = Str(x, "frame", "world"), Item = item, ItemModel = itemModel,
                    Slot = slot, SlotCondition = slot != null ? "c.item_slot == '" + slot + "'" : null, Kind = Str(x, "kind", "extract"),
                    Source = Str(x, "source"), Prefix = Prop(x, "prefix"),
                });
                return list;
            }
            var forms = Prop(Prop(cat, "itemForms"), formsName);
            if (forms.ValueKind != JsonValueKind.Array) throw new JavaEvalException("extract " + name + ": unknown item forms '" + formsName + "'");
            foreach (var f in forms.EnumerateArray())
            {
                string suffix = Str(f, "suffix"), slot = Str(f, "slot");
                list.Add(new ExtractSpec {
                    X = x, Name = name + "_" + suffix, FormKey = suffix, Unit = Str(x, "unit"), Layer = Str(x, "layer"), Frame = Str(f, "frame"),
                    Item = item, ItemModel = itemModel, Slot = slot, SlotCondition = "c.item_slot == '" + slot + "'", Kind = Str(f, "kind"),
                    Source = Str(f, "source") + " + " + Str(x, "source"), Prefix = Prop(f, "prefix"),
                });
            }
            return list;
        }

        // ---- item display transforms (ItemTransform) from upstream's item models ----

        sealed class ItemDisplay { public string Model, File, Context, From; public V3 Rotation, Translation, Scale; }

        readonly Dictionary<string, JsonElement> itemModels = new Dictionary<string, JsonElement>(StringComparer.Ordinal);

        // models/item/<name>.json of upstream (the datagen output first); null when upstream has none.
        JsonElement? ItemModel(string name, out string file)
        {
            file = null;
            foreach (var dir in itemModelDirs)
            {
                string p = Path.Combine(dir, name + ".json");
                if (!File.Exists(p)) continue;
                file = p;
                JsonElement e;
                if (!itemModels.TryGetValue(p, out e))
                    itemModels[p] = e = JsonDocument.Parse(File.ReadAllText(p), new JsonDocumentOptions { CommentHandling = JsonCommentHandling.Skip, AllowTrailingCommas = true }).RootElement;
                return e;
            }
            return null;
        }

        static V3 Vec3(JsonElement o, string key, V3 def)
        {
            var v = Prop(o, key);
            return v.ValueKind == JsonValueKind.Array ? new V3(v[0].GetDouble(), v[1].GetDouble(), v[2].GetDouble()) : def;
        }

        // The display transform BlockModel.getTransforms() resolves for a context: the model's own, else
        // its parent's (gloamingforest:item/... parents); a missing *_lefthand falls back to the same
        // model's *_righthand (ItemTransforms.Deserializer). Null = ItemTransform.NO_TRANSFORM.
        ItemDisplay Display(string modelName, string context)
        {
            string name = modelName;
            for (int depth = 0; depth < 8 && name != null; depth++)
            {
                string file;
                var m = ItemModel(name, out file);
                if (m == null) throw new JavaEvalException("upstream has no item model models/item/" + name + ".json (src/generated or src/main resources)");
                var display = Prop(m.Value, "display");
                string from = context;
                var t = Prop(display, context);
                if (t.ValueKind != JsonValueKind.Object && context.EndsWith("_lefthand", StringComparison.Ordinal))
                {
                    from = context.Replace("_lefthand", "_righthand");
                    t = Prop(display, from);
                }
                if (t.ValueKind == JsonValueKind.Object)
                {
                    if (Prop(t, "right_rotation").ValueKind != JsonValueKind.Undefined) throw new JavaEvalException("models/item/" + name + ".json: right_rotation is not supported");
                    var tr = Vec3(t, "translation", new V3(0, 0, 0));
                    var sc = Vec3(t, "scale", new V3(1, 1, 1));
                    Func<double, double, double> clamp = (v, lim) => Math.Max(-lim, Math.Min(lim, v));
                    return new ItemDisplay {
                        Model = name, Context = context, From = from,
                        File = Path.GetRelativePath(Path.GetFullPath(Path.Combine(itemModelDirs[0], "..", "..", "..", "..", "..", "..", "..")), file).Replace('\\', '/'),
                        Rotation = Vec3(t, "rotation", new V3(0, 0, 0)),
                        // ItemTransform.Deserializer: translation / 16 clamped to +-5 blocks, scale clamped to +-4
                        Translation = new V3(clamp(tr.X / 16, 5), clamp(tr.Y / 16, 5), clamp(tr.Z / 16, 5)),
                        Scale = new V3(clamp(sc.X, 4), clamp(sc.Y, 4), clamp(sc.Z, 4)),
                    };
                }
                string parent = Str(m.Value, "parent");
                if (parent == null || !parent.StartsWith("gloamingforest:item/", StringComparison.Ordinal)) return null;
                name = parent.Substring("gloamingforest:item/".Length);
            }
            return null;
        }

        // ItemTransform.apply(leftHand, poseStack): translate (x mirrored for the left hand), rotationXYZ
        // (y and z negated for the left hand), scale.
        static void ApplyDisplay(Pose p, ItemDisplay d, bool leftHand)
        {
            if (d == null) return;
            double rx = d.Rotation.X, ry = leftHand ? -d.Rotation.Y : d.Rotation.Y, rz = leftHand ? -d.Rotation.Z : d.Rotation.Z;
            p.Translate((leftHand ? -1 : 1) * d.Translation.X, d.Translation.Y, d.Translation.Z);
            p.Rotate(M3.RotX(rx * Math.PI / 180) * M3.RotY(ry * Math.PI / 180) * M3.RotZ(rz * Math.PI / 180));
            p.Scale(d.Scale.X, d.Scale.Y, d.Scale.Z);
        }

        // Applies a list of ops: [name, args...] or the name of a chain in "opChains". ["display", context]
        // is the display transform of the extract's item model for that context.
        Pose ApplyOps(JsonElement ops, Pose p, ExtractSpec spec, List<ItemDisplay> displays)
        {
            if (ops.ValueKind != JsonValueKind.Array) return p;
            foreach (var op in ops.EnumerateArray())
            {
                if (op.ValueKind == JsonValueKind.String)
                {
                    var chain = Prop(Prop(cat, "opChains"), op.GetString());
                    if (chain.ValueKind != JsonValueKind.Array) throw new JavaEvalException("unknown op chain '" + op.GetString() + "'");
                    p = ApplyOps(chain, p, spec, displays);
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
                    case "display":
                    {
                        if (spec.ItemModel == null) throw new JavaEvalException("extract " + spec.Name + ": 'display' needs an item (model)");
                        string ctx = a[1].GetString();
                        var d = Display(spec.ItemModel, ctx);
                        ApplyDisplay(p, d, ctx.EndsWith("_lefthand", StringComparison.Ordinal));
                        if (displays != null) displays.Add(d ?? new ItemDisplay { Model = spec.ItemModel, Context = ctx });
                        break;
                    }
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

        static readonly string[] RotAxes = { "xRot", "yRot", "zRot" };

        // The pose entries of an extract, expanded over their "range" (variable i): part path -> axis -> Java source.
        static List<KeyValuePair<string, string[]>> PoseEntries(JsonElement x)
        {
            var list = new List<KeyValuePair<string, string[]>>();
            var pose = Prop(x, "pose");
            if (pose.ValueKind != JsonValueKind.Array) return list;
            foreach (var p in pose.EnumerateArray())
            {
                var range = Prop(p, "range");
                int from = 0, to = 0;
                if (range.ValueKind == JsonValueKind.Array) { from = range[0].GetInt32(); to = range[1].GetInt32(); }
                for (int i = from; i <= to; i++)
                {
                    string si = i.ToString(CultureInfo.InvariantCulture);
                    var src = new string[3];
                    for (int k = 0; k < 3; k++)
                    {
                        string e = Str(p, RotAxes[k]);
                        // "i" is the loop variable (a whole word, so "xRot" or "Mth.sin" stay)
                        if (e != null) src[k] = System.Text.RegularExpressions.Regex.Replace(e, @"\bi\b", si);
                    }
                    list.Add(new KeyValuePair<string, string[]>(Str(p, "part").Replace("{i}", si), src));
                }
            }
            return list;
        }

        // The bind pose: ageInTicks (the trophy animation's clock) is 0.
        Dictionary<string, double[]> PoseOverrides(JsonElement x)
        {
            var map = new Dictionary<string, double[]>();
            var vars = new Dictionary<string, object> { { "ageInTicks", 0.0f } };
            foreach (var kv in PoseEntries(x))
            {
                double[] cur;
                if (!map.TryGetValue(kv.Key, out cur)) map[kv.Key] = cur = new[] { double.NaN, double.NaN, double.NaN };
                for (int k = 0; k < 3; k++) if (kv.Value[k] != null) cur[k] = EvalJava(kv.Value[k], vars);
            }
            return map;
        }

        // The pose expressions that use ageInTicks, as a Molang animation of the extract's bones (the
        // catalog's "animation": ageInTicks = query * scale, e.g. the Ur-Ghast trophy's waving tentacles).
        AnimResult ExtractAnimation(JsonElement x, Dictionary<string, BoneOut> bonesByPath)
        {
            var spec = Prop(x, "animation");
            if (spec.ValueKind != JsonValueKind.Object) return null;
            var age = Prop(spec, "ageInTicks");
            MNode ageNode = M.Mul(new MAtom(Str(age, "query")), M.Num(Prop(age, "scale").GetDouble()));
            var tr = new AnimTranslator(ClassPath, Interp);
            var res = new AnimResult { Method = "pose" };
            foreach (var kv in PoseEntries(x))
            {
                BoneOut bone;
                if (!bonesByPath.TryGetValue(kv.Key, out bone)) continue;
                for (int k = 0; k < 3; k++)
                {
                    string src = kv.Value[k];
                    if (src == null || !System.Text.RegularExpressions.Regex.IsMatch(src, @"\bageInTicks\b")) continue;
                    string why;
                    var value = tr.TranslateExpression(src, new Dictionary<string, MNode> { { "ageInTicks", ageNode } }, out why);
                    if (value == null) throw new JavaEvalException("extract animation " + kv.Key + ": " + why);
                    double def = k == 0 ? bone.Rot.X : k == 1 ? bone.Rot.Y : bone.Rot.Z;
                    var rel = M.Add(M.Mul(value, M.Num(180.0 / Math.PI)), M.Num(GeometryOut.Deg(def)), -1);
                    res.Statements.Add(new AnimStatement { Java = kv.Key + "." + RotAxes[k] + " = " + src, Bone = bone.Name, Channel = "rotation", Axis = k, Translated = true });
                    Dictionary<string, MNode[]> ch;
                    if (!res.Bones.TryGetValue(bone.Name, out ch)) res.Bones[bone.Name] = ch = new Dictionary<string, MNode[]>();
                    MNode[] axes;
                    if (!ch.TryGetValue("rotation", out axes)) ch["rotation"] = axes = new MNode[3];
                    axes[k] = rel;
                }
            }
            var note = Str(spec, "source");
            if (note != null) res.Notes.Add(note);
            return res;
        }

        // Attachables of the extracts, one per item: every slot form adds its geometries (in catalog order).
        sealed class AttachablePart { public string Geometry, Texture, Material, Condition; }
        sealed class AttachableAnim { public string Key, Id, Condition; }
        sealed class AttachableAcc
        {
            public string Item, Unit;
            public Origin Origin;
            public bool Failed;
            public List<string> Slots = new List<string>();
            public List<AttachablePart> Parts = new List<AttachablePart>();
            public List<AttachableAnim> Animations = new List<AttachableAnim>();
        }
        readonly Dictionary<string, AttachableAcc> attachables = new Dictionary<string, AttachableAcc>(StringComparer.Ordinal);
        readonly List<AttachableAcc> attachableOrder = new List<AttachableAcc>();

        // An extract; a form that fails marks its item's attachable as failed (never written with a slot missing).
        void Extract(ExtractSpec spec, string onlyClass, string onlyUnit, string onlyItem)
        {
            int errors = Errors.Count;
            ExtractBody(spec, onlyClass, onlyUnit, onlyItem);
            AttachableAcc acc;
            if (Errors.Count > errors && spec.Item != null)
            {
                if (!attachables.TryGetValue(spec.Item, out acc)) { attachables[spec.Item] = acc = new AttachableAcc { Item = spec.Item, Unit = spec.Unit, Origin = cur }; attachableOrder.Add(acc); }
                acc.Failed = true;
            }
        }

        void ExtractBody(ExtractSpec spec, string onlyClass, string onlyUnit, string onlyItem)
        {
            string name = spec.Name, unit = spec.Unit, layerName = spec.Layer;
            var x = spec.X;
            if (onlyUnit != null && !string.Equals(onlyUnit, unit, StringComparison.OrdinalIgnoreCase)) return;
            LayerInfo info;
            if (!Layers.TryGetValue(layerName, out info) || info.Def == null) { Errors.Add("extract " + name + ": layer '" + layerName + "' missing"); return; }
            if (onlyClass != null && !Matches(onlyClass, null, info, unit)) return;
            string item = spec.Item;
            if (onlyItem != null && item != onlyItem) return;
            cur = new Origin { Info = info, Unit = unit, Item = item };
            string frameName = spec.Frame;
            var frame = FrameOf(frameName);
            var overrides = PoseOverrides(x);
            var geoTex = new Dictionary<string, string>();
            var geoMaterial = new Dictionary<string, string>();
            var boneMaps = new List<object>();
            var byGeo = new Dictionary<string, List<JsonElement>>();
            var order = new List<string>();
            var displays = new List<ItemDisplay>();
            var mainBones = new Dictionary<string, BoneOut>(StringComparer.Ordinal);
            foreach (var g in Prop(x, "groups").EnumerateArray())
            {
                string suffix = Str(g, "suffix");
                string gname = Str(g, "geometry") ?? (suffix != null ? name + "_" + suffix : name);
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
                    var pose = ApplyOps(spec.Prefix, new Pose(), spec, gi == 0 && gname == name ? displays : null);
                    pose = ApplyOps(Prop(g, "ops"), pose, spec, null);
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
                    var opt = new ConvertOptions
                    {
                        Identifier = geoPrefix + gname, RootFilter = new HashSet<string>(StrList(g, "parts")), BoneParentOfRoots = wrapper, RotOverride = overrides,
                        BonePrefix = frame.Parent != null ? wrapper + "_" : null,
                    };
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
                        if (gname == name && !mainBones.ContainsKey(b.JavaPath)) mainBones[b.JavaPath] = b;
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
                var entry = new JObj().Add("identifier", geoPrefix + gname).Add("file", file).Add("unit", unit).Add("kind", spec.Kind)
                    .Add("textures", Json.Arr(Tex(geoTex[gname]))).Add("texture_width", first.TexWidth).Add("texture_height", first.TexHeight)
                    .Add("bones", boneCount).Add("cubes", cubes)
                    .Add("visible_bounds", Json.Vec(Convert.ToDouble(desc.Get("visible_bounds_width")), Convert.ToDouble(desc.Get("visible_bounds_height")), Convert.ToDouble(((List<object>)desc.Get("visible_bounds_offset"))[1])))
                    .Add("frame", frameName);
                if (frame.Parent != null) entry.Add("external_parents", Json.Arr(frame.Parent));
                if (spec.Slot != null) entry.Add("slot", spec.Slot);
                foreach (var key in new[] { "block" })
                {
                    var refs = StrList(x, key);
                    if (refs.Count > 0) entry.Add(key, refs.Cast<object>().ToList());
                }
                if (item != null) entry.Add("item", Json.Arr(item));
                var preview = Prop(x, "preview");
                if (preview.ValueKind == JsonValueKind.Object) entry.Add("preview", ToJObj(preview));
                indexGeometries.Add(entry);
                Log.Add(string.Format("{0,-28} {1,-14} {2,3} bones {3,4} cubes (extract of {4})", gname, unit, boneCount, cubes, layerName));
            }

            // the pose expressions that run on a clock (ageInTicks) animate the main geometry
            var anim = ExtractAnimation(x, mainBones);
            string animId = null;
            if (anim != null && anim.Bones.Count > 0)
            {
                animId = animPrefix + name + ".base";
                string afile = "animations/" + name + ".base.json";
                Put(UnitDir(unit) + "rp/" + afile, AnimTranslator.AnimationJson(animId, anim));
                indexAnimations.Add(new JObj().Add("identifier", animId).Add("file", afile).Add("unit", unit).Add("bones", anim.Bones.Keys.Cast<object>().ToList()));
                foreach (var e in indexGeometries.Cast<JObj>()) if ((string)e.Get("identifier") == geoPrefix + name) e.Add("animation", animId);
            }

            var bm = new JObj()
                .Add("comment", "Generated by tools/models/convert.ps1 (F2) from the upstream 4.8 @ " + TfRevision + " (LGPL-2.1-or-later code): parts of an upstream model drawn with the PoseStack transforms of its renderer.")
                .Add("source", spec.Source)
                .Add("layer", new JObj().Add("name", layerName).Add("field", "TFModelLayers." + info.Field).Add("supplier", info.Supplier))
                .Add("frame", frameName);
            if (spec.Slot != null) bm.Add("slot", spec.Slot);
            if (displays.Count > 0)
                bm.Add("item_display", displays.Select(dd => (object)(dd.File == null
                    ? new JObj().Add("model", "models/item/" + dd.Model + ".json").Add("context", dd.Context).Add("transform", "none (ItemTransform.NO_TRANSFORM)")
                    : new JObj().Add("model", "models/item/" + dd.Model + ".json").Add("file", dd.File).Add("context", dd.Context).Add("from", dd.From)
                        .Add("rotation", Json.Vec(dd.Rotation.X, dd.Rotation.Y, dd.Rotation.Z))
                        .Add("translation", Json.Vec(dd.Translation.X * 16, dd.Translation.Y * 16, dd.Translation.Z * 16))
                        .Add("scale", Json.Vec(dd.Scale.X, dd.Scale.Y, dd.Scale.Z)))).ToList());
            bm.Add("geometries", order.Select(gn => (object)new JObj().Add("identifier", geoPrefix + gn).Add("texture", new JObj().Add("bedrock", Tex(geoTex[gn])).Add("tf", TfTex(geoTex[gn]))).Add("material", geoMaterial[gn])).ToList())
              .Add("bones", boneMaps);
            if (anim != null && animId != null)
                bm.Add("animation", new JObj().Add("identifier", animId).Add("notes", anim.Notes.Cast<object>().ToList())
                    .Add("translated", anim.Statements.Select(st => (object)st.Java).ToList()));
            var notes = StrList(x, "notes");
            if (notes.Count > 0) bm.Add("notes", notes.Cast<object>().ToList());
            Put(UnitDir(unit) + "bones/" + name + ".json", bm);

            if (item == null) return;
            AttachableAcc acc;
            if (!attachables.TryGetValue(item, out acc))
            {
                attachables[item] = acc = new AttachableAcc { Item = item, Unit = unit, Origin = cur };
                attachableOrder.Add(acc);
            }
            if (acc.Unit != unit) { Errors.Add("attachable " + item + ": forms in units " + acc.Unit + " and " + unit); return; }
            if (spec.Slot != null && !acc.Slots.Contains(spec.Slot)) acc.Slots.Add(spec.Slot);
            foreach (var gname in order)
                acc.Parts.Add(new AttachablePart { Geometry = gname, Texture = geoTex[gname], Material = geoMaterial[gname], Condition = spec.SlotCondition });
            if (animId != null) acc.Animations.Add(new AttachableAnim { Key = spec.FormKey ?? "base", Id = animId, Condition = spec.SlotCondition });
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

        // One attachable per item: geometry "default" (controller.render.item_default) and "part<N>"
        // (controller.render.gloamingforest.models.part<N>), each drawn only in its slot (c.item_slot), plus
        // the enchantment glint (variable.is_enchanted) and the animations of the forms.
        void WriteExtractAttachable(AttachableAcc acc)
        {
            cur = acc.Origin;
            var materials = new JObj(); var textures = new JObj(); var geometry = new JObj();
            var controllers = new List<object>();
            for (int i = 0; i < acc.Parts.Count; i++)
            {
                var part = acc.Parts[i];
                string key = i == 0 ? "default" : "part" + i;
                materials.Add(key, part.Material);
                textures.Add(key, Tex(part.Texture));
                geometry.Add(key, geoPrefix + part.Geometry);
                string rc = i == 0 ? "controller.render.item_default" : "controller.render.gloamingforest.models." + key;
                if (i > 0 && !renderControllers.ContainsKey(rc))
                {
                    renderControllers[rc] = new JObj()
                        .Add("geometry", "Geometry." + key)
                        .Add("materials", Json.Arr(new JObj().Add("*", "variable.is_enchanted ? Material.enchanted : Material." + key)))
                        .Add("textures", Json.Arr("Texture." + key, "Texture.enchanted"));
                }
                controllers.Add(part.Condition != null ? (object)new JObj().Add(rc, part.Condition) : rc);
            }
            materials.Add("enchanted", "entity_alphatest_glint");
            textures.Add("enchanted", "textures/misc/enchanted_item_glint");
            var desc = new JObj().Add("identifier", acc.Item).Add("materials", materials).Add("textures", textures).Add("geometry", geometry);
            if (acc.Animations.Count > 0)
            {
                var anims = new JObj();
                var animate = new List<object>();
                foreach (var a in acc.Animations)
                {
                    anims.Add(a.Key, a.Id);
                    animate.Add(a.Condition != null ? (object)new JObj().Add(a.Key, a.Condition) : a.Key);
                }
                desc.Add("animations", anims).Add("scripts", new JObj().Add("animate", animate));
            }
            desc.Add("render_controllers", controllers);
            string afile = "attachables/" + acc.Item.Substring(acc.Item.IndexOf(':') + 1) + ".json";
            Put(UnitDir(acc.Unit) + "rp/" + afile, new JObj().Add("format_version", "1.10.0").Add("minecraft:attachable", new JObj().Add("description", desc)));
            var entry = new JObj().Add("item", acc.Item).Add("file", afile).Add("unit", acc.Unit).Add("slot", acc.Slots.Count > 0 ? acc.Slots[0] : "head")
                .Add("slots", acc.Slots.Cast<object>().ToList())
                .Add("geometry", acc.Parts.Select(p => (object)(geoPrefix + p.Geometry)).ToList()).Add("textures", acc.Parts.Select(p => (object)Tex(p.Texture)).ToList());
            if (acc.Animations.Count > 0) entry.Add("animations", acc.Animations.Select(a => (object)a.Id).ToList());
            indexAttachables.Add(entry);
        }

        // ---- shared files ----------------------------------------------------------------------------------

        void WriteShared()
        {
            if (renderControllers.Count > 0)
            {
                var rcs = new JObj();
                foreach (var kv in renderControllers) rcs.Add(kv.Key, kv.Value);
                Put(UnitDir("models") + "rp/render_controllers/gloamingforest.models.render_controllers.json", new JObj().Add("format_version", "1.10.0").Add("render_controllers", rcs));
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
