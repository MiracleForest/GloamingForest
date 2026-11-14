// GloamingForest import tool (contract "import", unit F1): the block-state properties of an upstream block,
// read from its Java class chain. Every upstream class that overrides createBlockStateDefinition contributes the
// properties it adds (builder.add(...), chained .add(...), LIST.forEach(builder::add)); a super call
// continues with the superclass, and the first vanilla class contributes its full Minecraft 1.21.1
// property set. Property constants are resolved through the class, its upstream superclasses and interfaces,
// its static imports, and the vanilla constants (BlockStateProperties, PipeBlock, ...): Property.create
// names, integer ranges and enum values (the enum's serialized names, in declaration order).
// Facts only; no Java code is translated here.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text.RegularExpressions;

namespace GfImport
{
    public sealed class JProp
    {
        public string Name;                                  // Java property name ("facing")
        public string Type;                                  // bool | int | enum
        public List<string> Values = new List<string>();     // bool: false, true; int: min..max; enum: serialized names
        public string Origin;                                // where it was resolved ("TrophyBlock.ROTATION")
        public Dictionary<string, string> Consts = new Dictionary<string, string>(StringComparer.Ordinal); // enum constant -> value
        public string Default;                               // the value upstream's registerDefaultState sets (null: not set)
        public string EnumType;                              // the upstream enum of an EnumProperty ("BrazierLight")
    }

    public static class JavaStates
    {
        // ---- vanilla (Minecraft 1.21.1) ------------------------------------------------------------------

        const string H4 = "north,south,west,east";
        const string D6 = "north,east,south,west,up,down";

        /// <summary>Vanilla property constants: "Class.NAME" -> "name:type:values" (int: "min,max").</summary>
        static readonly Dictionary<string, string> VanillaConst = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            { "BlockStateProperties.HORIZONTAL_FACING", "facing:enum:" + H4 }, { "BlockStateProperties.FACING", "facing:enum:" + D6 },
            { "BlockStateProperties.FACING_HOPPER", "facing:enum:down,north,south,west,east" },
            { "BlockStateProperties.AXIS", "axis:enum:x,y,z" }, { "BlockStateProperties.HORIZONTAL_AXIS", "axis:enum:x,z" },
            { "BlockStateProperties.ROTATION_16", "rotation:int:0,15" },
            { "BlockStateProperties.AGE_1", "age:int:0,1" }, { "BlockStateProperties.AGE_2", "age:int:0,2" }, { "BlockStateProperties.AGE_3", "age:int:0,3" },
            { "BlockStateProperties.AGE_4", "age:int:0,4" }, { "BlockStateProperties.AGE_5", "age:int:0,5" }, { "BlockStateProperties.AGE_7", "age:int:0,7" },
            { "BlockStateProperties.AGE_15", "age:int:0,15" }, { "BlockStateProperties.AGE_25", "age:int:0,25" },
            { "BlockStateProperties.BITES", "bites:int:0,6" }, { "BlockStateProperties.CANDLES", "candles:int:1,4" }, { "BlockStateProperties.DELAY", "delay:int:1,4" },
            { "BlockStateProperties.DISTANCE", "distance:int:1,7" }, { "BlockStateProperties.EGGS", "eggs:int:1,4" }, { "BlockStateProperties.HATCH", "hatch:int:0,2" },
            { "BlockStateProperties.LAYERS", "layers:int:1,8" }, { "BlockStateProperties.LEVEL", "level:int:0,15" }, { "BlockStateProperties.MOISTURE", "moisture:int:0,7" },
            { "BlockStateProperties.NOTE", "note:int:0,24" }, { "BlockStateProperties.PICKLES", "pickles:int:1,4" }, { "BlockStateProperties.POWER", "power:int:0,15" },
            { "BlockStateProperties.STAGE", "stage:int:0,1" }, { "BlockStateProperties.STABILITY_DISTANCE", "distance:int:0,7" },
            { "BlockStateProperties.LEVEL_CAULDRON", "level:int:1,3" }, { "BlockStateProperties.LEVEL_COMPOSTER", "level:int:0,8" },
            { "BlockStateProperties.LEVEL_FLOWING", "level:int:1,8" }, { "BlockStateProperties.LEVEL_HONEY", "honey_level:int:0,5" },
            { "BlockStateProperties.DUSTED", "dusted:int:0,3" }, { "BlockStateProperties.FLOWER_AMOUNT", "flower_amount:int:1,4" },
            { "BlockStateProperties.RESPAWN_ANCHOR_CHARGES", "charges:int:0,4" },
            { "BlockStateProperties.DOUBLE_BLOCK_HALF", "half:enum:upper,lower" }, { "BlockStateProperties.HALF", "half:enum:top,bottom" },
            { "BlockStateProperties.CHEST_TYPE", "type:enum:single,left,right" }, { "BlockStateProperties.SLAB_TYPE", "type:enum:top,bottom,double" },
            { "BlockStateProperties.STAIRS_SHAPE", "shape:enum:straight,inner_left,inner_right,outer_left,outer_right" },
            { "BlockStateProperties.ATTACH_FACE", "face:enum:floor,wall,ceiling" }, { "BlockStateProperties.DOOR_HINGE", "hinge:enum:left,right" },
            { "BlockStateProperties.BED_PART", "part:enum:head,foot" },
            { "BlockStateProperties.NORTH_WALL", "north:enum:none,low,tall" }, { "BlockStateProperties.EAST_WALL", "east:enum:none,low,tall" },
            { "BlockStateProperties.SOUTH_WALL", "south:enum:none,low,tall" }, { "BlockStateProperties.WEST_WALL", "west:enum:none,low,tall" },
            { "HorizontalDirectionalBlock.FACING", "facing:enum:" + H4 }, { "DirectionalBlock.FACING", "facing:enum:" + D6 },
            { "RotatedPillarBlock.AXIS", "axis:enum:x,y,z" },
            { "PipeBlock.NORTH", "north:bool" }, { "PipeBlock.EAST", "east:bool" }, { "PipeBlock.SOUTH", "south:bool" },
            { "PipeBlock.WEST", "west:bool" }, { "PipeBlock.UP", "up:bool" }, { "PipeBlock.DOWN", "down:bool" },
            { "CrossCollisionBlock.NORTH", "north:bool" }, { "CrossCollisionBlock.EAST", "east:bool" }, { "CrossCollisionBlock.SOUTH", "south:bool" },
            { "CrossCollisionBlock.WEST", "west:bool" }, { "CrossCollisionBlock.WATERLOGGED", "waterlogged:bool" },
            { "Direction.Axis", "axis:enum:x,y,z" },
            // vanilla class constants whose BlockStateProperties name differs
            { "DoorBlock.HALF", "half:enum:upper,lower" }, { "DoublePlantBlock.HALF", "half:enum:upper,lower" },
            { "SlabBlock.TYPE", "type:enum:top,bottom,double" }, { "ChestBlock.TYPE", "type:enum:single,left,right" },
            { "FaceAttachedHorizontalDirectionalBlock.FACE", "face:enum:floor,wall,ceiling" }, { "ButtonBlock.FACE", "face:enum:floor,wall,ceiling" },
            { "StairBlock.SHAPE", "shape:enum:straight,inner_left,inner_right,outer_left,outer_right" },
            { "StandingSignBlock.ROTATION", "rotation:int:0,15" }, { "CeilingHangingSignBlock.ROTATION", "rotation:int:0,15" },
            { "SkullBlock.ROTATION", "rotation:int:0,15" }, { "BannerBlock.ROTATION", "rotation:int:0,15" },
        };

        /// <summary>Vanilla integer constants used in property ranges.</summary>
        static readonly Dictionary<string, int> VanillaInts = new Dictionary<string, int>(StringComparer.Ordinal)
        {
            { "SnowLayerBlock.MAX_HEIGHT", 8 }, { "CandleBlock.MAX_CANDLES", 4 }, { "CandleBlock.MIN_CANDLES", 1 },
        };

        // boolean BlockStateProperties (name = lower-case constant)
        static readonly string[] VanillaBools =
        {
            "ATTACHED", "BOTTOM", "CONDITIONAL", "DISARMED", "DRAG", "ENABLED", "EXTENDED", "EYE", "FALLING", "HANGING", "HAS_BOTTLE_0",
            "HAS_BOTTLE_1", "HAS_BOTTLE_2", "HAS_RECORD", "HAS_BOOK", "INVERTED", "IN_WALL", "LIT", "LOCKED", "OCCUPIED", "OPEN", "PERSISTENT",
            "POWERED", "SHORT", "SIGNAL_FIRE", "SNOWY", "TRIGGERED", "UNSTABLE", "WATERLOGGED", "BERRIES", "BLOOM", "SHRIEKING", "CAN_SUMMON",
            "UP", "DOWN", "NORTH", "EAST", "SOUTH", "WEST", "CRACKED",
        };

        /// <summary>The complete property set of the vanilla block classes upstream blocks extend (Minecraft 1.21.1).
        /// An empty array: the class declares no state.</summary>
        static readonly Dictionary<string, string[]> VanillaClass = new Dictionary<string, string[]>(StringComparer.Ordinal)
        {
            { "Block", new string[0] }, { "BaseEntityBlock", new string[0] }, { "BushBlock", new string[0] }, { "TransparentBlock", new string[0] },
            { "HalfTransparentBlock", new string[0] }, { "BaseFireBlock", new string[0] }, { "MushroomBlock", new string[0] },
            { "SlimeBlock", new string[0] }, { "WaterlilyBlock", new string[0] }, { "FlowerPotBlock", new string[0] }, { "WoolCarpetBlock", new string[0] },
            { "CarpetBlock", new string[0] }, { "FallingBlock", new string[0] }, { "FlowerBlock", new string[0] }, { "TallGrassBlock", new string[0] },
            // abstract: the subclasses add FACING themselves
            { "HorizontalDirectionalBlock", new string[0] }, { "DirectionalBlock", new string[0] },
            { "RotatedPillarBlock", new[] { "axis:enum:x,y,z" } },
            { "GlazedTerracottaBlock", new[] { "facing:enum:" + H4 } },
            { "LeavesBlock", new[] { "distance:int:1,7", "persistent:bool", "waterlogged:bool" } },
            { "SaplingBlock", new[] { "stage:int:0,1" } },
            { "StairBlock", new[] { "facing:enum:" + H4, "half:enum:top,bottom", "shape:enum:straight,inner_left,inner_right,outer_left,outer_right", "waterlogged:bool" } },
            { "SlabBlock", new[] { "type:enum:top,bottom,double", "waterlogged:bool" } },
            { "FenceBlock", new[] { "north:bool", "east:bool", "west:bool", "south:bool", "waterlogged:bool" } },
            { "IronBarsBlock", new[] { "north:bool", "east:bool", "west:bool", "south:bool", "waterlogged:bool" } },
            { "FenceGateBlock", new[] { "facing:enum:" + H4, "open:bool", "powered:bool", "in_wall:bool" } },
            { "WallBlock", new[] { "up:bool", "north:enum:none,low,tall", "east:enum:none,low,tall", "west:enum:none,low,tall", "south:enum:none,low,tall", "waterlogged:bool" } },
            { "DoorBlock", new[] { "half:enum:upper,lower", "facing:enum:" + H4, "open:bool", "hinge:enum:left,right", "powered:bool" } },
            { "TrapDoorBlock", new[] { "facing:enum:" + H4, "open:bool", "half:enum:top,bottom", "powered:bool", "waterlogged:bool" } },
            { "ButtonBlock", new[] { "facing:enum:" + H4, "powered:bool", "face:enum:floor,wall,ceiling" } },
            { "PressurePlateBlock", new[] { "powered:bool" } },
            { "WeightedPressurePlateBlock", new[] { "power:int:0,15" } },
            { "StandingSignBlock", new[] { "rotation:int:0,15", "waterlogged:bool" } },
            { "WallSignBlock", new[] { "facing:enum:" + H4, "waterlogged:bool" } },
            { "CeilingHangingSignBlock", new[] { "rotation:int:0,15", "attached:bool", "waterlogged:bool" } },
            { "WallHangingSignBlock", new[] { "facing:enum:" + H4, "waterlogged:bool" } },
            { "ChestBlock", new[] { "facing:enum:" + H4, "type:enum:single,left,right", "waterlogged:bool" } },
            { "TrappedChestBlock", new[] { "facing:enum:" + H4, "type:enum:single,left,right", "waterlogged:bool" } },
            { "LadderBlock", new[] { "facing:enum:" + H4, "waterlogged:bool" } },
            { "WallTorchBlock", new[] { "facing:enum:" + H4 } },
            { "DoublePlantBlock", new[] { "half:enum:upper,lower" } },
            { "LanternBlock", new[] { "hanging:bool", "waterlogged:bool" } },
            { "CandleBlock", new[] { "candles:int:1,4", "lit:bool", "waterlogged:bool" } },
            { "AbstractFurnaceBlock", new[] { "facing:enum:" + H4, "lit:bool" } },
            { "HugeMushroomBlock", new[] { "north:bool", "east:bool", "south:bool", "west:bool", "up:bool", "down:bool" } },
            // every face is supported unless the subclass overrides isFaceSupported (checked below)
            { "MultifaceBlock", new[] { "north:bool", "east:bool", "south:bool", "west:bool", "up:bool", "down:bool" } },
            { "ChiseledBookShelfBlock", new[] { "facing:enum:" + H4, "slot_0_occupied:bool", "slot_1_occupied:bool", "slot_2_occupied:bool",
                "slot_3_occupied:bool", "slot_4_occupied:bool", "slot_5_occupied:bool" } },
        };

        /// <summary>Vanilla enums used by EnumProperty.create (serialized names, declaration order).</summary>
        static readonly Dictionary<string, string> VanillaEnums = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            { "Direction.Axis", "x,y,z" }, { "Axis", "x,y,z" }, { "Direction", "down,up,north,south,west,east" },
            { "DoubleBlockHalf", "upper,lower" }, { "Half", "top,bottom" }, { "SlabType", "top,bottom,double" }, { "ChestType", "single,left,right" },
            { "AttachFace", "floor,wall,ceiling" }, { "DoorHingeSide", "left,right" }, { "WallSide", "none,low,tall" },
        };

        static JProp Spec(string spec, string origin)
        {
            var p = spec.Split(':');
            var jp = new JProp { Name = p[0], Type = p[1], Origin = origin };
            if (p[1] == "bool") jp.Values.AddRange(new[] { "false", "true" });
            else if (p[1] == "int")
            {
                var r = p[2].Split(',');
                int lo = int.Parse(r[0], CultureInfo.InvariantCulture), hi = int.Parse(r[1], CultureInfo.InvariantCulture);
                for (int i = lo; i <= hi; i++) jp.Values.Add(i.ToString(CultureInfo.InvariantCulture));
            }
            else foreach (var v in p[2].Split(',')) { jp.Values.Add(v); jp.Consts[v.ToUpperInvariant()] = v; }
            return jp;
        }

        // ---- upstream classes ------------------------------------------------------------------------------------

        sealed class ClassInfo
        {
            public string Name, File, Text, Super;
            public List<string> Interfaces = new List<string>();
            public List<string> StaticImports = new List<string>();   // "Class.NAME" or "Class.*"
        }

        static readonly Dictionary<string, ClassInfo> classes = new Dictionary<string, ClassInfo>(StringComparer.Ordinal);

        public static void Reset() { classes.Clear(); }

        /// <summary>The comment-stripped text of an upstream class (cached), or null for a vanilla class.</summary>
        public static string Text(Ctx ctx, string cls)
        {
            var ci = Info(ctx, cls);
            return ci != null ? ci.Text : null;
        }

        static ClassInfo Info(Ctx ctx, string cls)
        {
            ClassInfo ci;
            if (classes.TryGetValue(cls, out ci)) return ci;
            string file = Java.ClassFile(ctx, cls);
            if (file != null)
            {
                ci = new ClassInfo { Name = cls, File = file, Text = Java.StripComments(Java.SourceText(file)) };
                var m = Regex.Match(ci.Text, @"\b(?:class|interface|enum|record)\s+" + Regex.Escape(cls) + @"\b(?:\s*<[^{]*?>)?([^{]*)\{");
                if (m.Success)
                {
                    string head = m.Groups[1].Value;
                    var em = Regex.Match(head, @"\bextends\s+([\w.]+(?:\s*<[^>]*>)?(?:\s*,\s*[\w.]+(?:\s*<[^>]*>)?)*)");
                    var im = Regex.Match(head, @"\bimplements\s+(.+)$", RegexOptions.Singleline);
                    bool isInterface = m.Value.StartsWith("interface", StringComparison.Ordinal);
                    if (em.Success)
                    {
                        var names = Regex.Replace(em.Groups[1].Value, @"<[^>]*>", "").Split(',').Select(s => s.Trim().Split('.').Last()).Where(s => s.Length > 0).ToList();
                        if (isInterface) ci.Interfaces.AddRange(names);
                        else ci.Super = names.FirstOrDefault();
                    }
                    if (im.Success)
                        ci.Interfaces.AddRange(Regex.Replace(im.Groups[1].Value, @"<[^>]*>", "").Split(',').Select(s => s.Trim().Split('.').Last()).Where(s => s.Length > 0));
                }
                foreach (Match sm in Regex.Matches(ci.Text, @"import\s+static\s+[\w.]*?\.(\w+)\.(\w+|\*)\s*;"))
                    ci.StaticImports.Add(sm.Groups[1].Value + "." + sm.Groups[2].Value);
            }
            classes[cls] = ci;
            return ci;
        }

        /// <summary>The initializer of a field NAME declared in a class text (null when not declared there).</summary>
        static string FieldInit(string text, string name)
        {
            foreach (Match m in Regex.Matches(text, @"[\w>\]]\s+" + Regex.Escape(name) + @"\s*=(?!=)\s*"))
            {
                int i = m.Index + m.Length, depth = 0, start = i;
                bool inStr = false;
                for (; i < text.Length; i++)
                {
                    char c = text[i];
                    if (inStr) { if (c == '\\') i++; else if (c == '"') inStr = false; continue; }
                    if (c == '"') inStr = true;
                    else if (c == '(' || c == '{') depth++;
                    else if (c == ')' || c == '}') depth--;
                    else if (c == ';' && depth <= 0) break;
                }
                return text.Substring(start, i - start).Trim();
            }
            return null;
        }

        /// <summary>Top-level comma-separated arguments up to the unmatched closing parenthesis (or the end):
        /// (), [] and {} nest, strings are skipped; '&lt;' / '&gt;' do not nest (lambdas "->", comparisons).</summary>
        static List<string> Args(string s)
        {
            var res = new List<string>();
            int depth = 0, start = 0;
            bool inStr = false;
            for (int i = 0; i < s.Length; i++)
            {
                char c = s[i];
                if (inStr) { if (c == '\\') i++; else if (c == '"') inStr = false; continue; }
                if (c == '"') inStr = true;
                else if (c == '(' || c == '[' || c == '{') depth++;
                else if (c == ')' || c == ']' || c == '}')
                {
                    if (depth == 0) { res.Add(s.Substring(start, i - start)); return res; }
                    depth--;
                }
                else if (c == ',' && depth == 0) { res.Add(s.Substring(start, i - start)); start = i + 1; }
            }
            res.Add(s.Substring(start));
            return res;
        }

        /// <summary>The body of the first "{ ... }" block starting at or after index (without the braces).</summary>
        static string BraceBody(string text, int index)
        {
            int open = text.IndexOf('{', index);
            if (open < 0) return null;
            int depth = 0;
            bool inStr = false;
            for (int i = open; i < text.Length; i++)
            {
                char c = text[i];
                if (inStr) { if (c == '\\') i++; else if (c == '"') inStr = false; continue; }
                if (c == '"') inStr = true;
                else if (c == '{') depth++;
                else if (c == '}' && --depth == 0) return text.Substring(open + 1, i - open - 1);
            }
            return null;
        }

        sealed class Scope
        {
            public Ctx Ctx;
            public ClassInfo Cls;           // the upstream class the expression is written in
            public List<string> Vanilla;    // vanilla classes of the block's chain (for inherited vanilla constants)
            public Action<string> Warn;
            public int Depth;
        }

        /// <summary>Resolves a field NAME visible from an upstream class: the class, its upstream interfaces and superclasses,
        /// its static imports, then the vanilla classes of the chain.</summary>
        static List<JProp> ResolveName(Scope s, ClassInfo ci, string name, HashSet<string> seen)
        {
            if (ci == null || !seen.Add(ci.Name)) return null;
            string init = FieldInit(ci.Text, name);
            if (init != null) return Eval(new Scope { Ctx = s.Ctx, Cls = ci, Vanilla = s.Vanilla, Warn = s.Warn, Depth = s.Depth + 1 }, init, ci.Name + "." + name);
            // inherited fields (valid Java has no ambiguity between the superclass and the interfaces)
            if (ci.Super != null)
            {
                var si = Info(s.Ctx, ci.Super);
                var r = si != null ? ResolveName(s, si, name, seen) : Vanilla(ci.Super + "." + name);
                if (r != null) return r;
            }
            foreach (var itf in ci.Interfaces)
            {
                var ii = Info(s.Ctx, itf);
                var r = ii != null ? ResolveName(s, ii, name, seen) : Vanilla(itf + "." + name);
                if (r != null) return r;
            }
            foreach (var imp in ci.StaticImports)
            {
                var p = imp.Split('.');
                if (p[1] != name && p[1] != "*") continue;
                var tf = Info(s.Ctx, p[0]);
                if (tf != null) { var r = ResolveName(s, tf, name, seen); if (r != null) return r; }
                var v = Vanilla(p[0] + "." + name);
                if (v != null) return v;
            }
            return null;
        }

        /// <summary>A vanilla property constant ("Class.NAME"): the explicit table, the boolean
        /// BlockStateProperties (and SimpleWaterloggedBlock's WATERLOGGED), then a property of a vanilla block
        /// class named like the constant (LeavesBlock.DISTANCE -> its "distance").</summary>
        static List<JProp> Vanilla(string qualified)
        {
            string spec;
            if (VanillaConst.TryGetValue(qualified, out spec)) return new List<JProp> { Spec(spec, qualified) };
            if (qualified == "ChiseledBookShelfBlock.SLOT_OCCUPIED_PROPERTIES")
                return Enumerable.Range(0, 6).Select(i => Spec("slot_" + i + "_occupied:bool", qualified)).ToList();
            var p = qualified.Split('.');
            if (p.Length != 2) return null;
            if ((p[0] == "BlockStateProperties" || p[0] == "SimpleWaterloggedBlock") && VanillaBools.Contains(p[1]))
                return new List<JProp> { Spec(p[1].ToLowerInvariant() + ":bool", qualified) };
            string[] specs;
            if (VanillaClass.TryGetValue(p[0], out specs))
            {
                spec = specs.FirstOrDefault(x => x.Split(':')[0] == p[1].ToLowerInvariant());
                if (spec != null) return new List<JProp> { Spec(spec, qualified) };
            }
            return null;
        }

        /// <summary>The properties an expression denotes (one, or several for a List.of(...) / list constant).</summary>
        static List<JProp> Eval(Scope s, string expr, string origin)
        {
            expr = expr.Trim();
            if (s.Depth > 12) { s.Warn("property expression too deep: " + expr); return new List<JProp>(); }
            Match m;
            if ((m = Regex.Match(expr, @"^BooleanProperty\.create\(\s*""([^""]+)""\s*\)$")).Success)
                return new List<JProp> { Spec(m.Groups[1].Value + ":bool", origin) };
            if ((m = Regex.Match(expr, @"^IntegerProperty\.create\(\s*""([^""]+)""\s*,\s*([^,]+?)\s*,\s*([^,]+?)\s*\)$")).Success)
            {
                int lo = IntConst(s, m.Groups[2].Value), hi = IntConst(s, m.Groups[3].Value);
                return new List<JProp> { Spec(m.Groups[1].Value + ":int:" + lo + "," + hi, origin) };
            }
            if ((m = Regex.Match(expr, @"^(?:EnumProperty|DirectionProperty)\.create\(\s*""([^""]+)""\s*,\s*([\w.]+)\.class\s*(?:,(.*))?\)$", RegexOptions.Singleline)).Success)
            {
                var consts = EnumConstants(s, m.Groups[2].Value);
                if (consts == null) { s.Warn("unknown enum " + m.Groups[2].Value + " of property " + m.Groups[1].Value + " (" + origin + ")"); return new List<JProp>(); }
                if (m.Groups[3].Success && m.Groups[3].Value.Trim().Length > 0)
                    s.Warn("enum property " + m.Groups[1].Value + " (" + origin + ") is filtered (" + m.Groups[3].Value.Trim() + "): all values kept");
                var jp = new JProp { Name = m.Groups[1].Value, Type = "enum", Origin = origin, EnumType = m.Groups[2].Value };
                foreach (var c in consts) { jp.Values.Add(c.Value); jp.Consts[c.Key] = c.Value; }
                return new List<JProp> { jp };
            }
            if ((m = Regex.Match(expr, @"^(?:List|Set|ImmutableList)\.of\((.*)\)$", RegexOptions.Singleline)).Success)
            {
                var res = new List<JProp>();
                foreach (var a in Args(m.Groups[1].Value + ")")) if (a.Trim().Length > 0) res.AddRange(Eval(s, a, origin));
                return res;
            }
            if ((m = Regex.Match(expr, @"^([A-Z]\w*)\.([A-Z_][A-Z0-9_]*)$")).Success)
            {
                var tf = Info(s.Ctx, m.Groups[1].Value);
                if (tf != null)
                {
                    var r = ResolveName(s, tf, m.Groups[2].Value, new HashSet<string>(StringComparer.Ordinal));
                    if (r != null) return r;
                }
                var v = Vanilla(expr);
                if (v != null) return v;
                s.Warn("unknown property constant " + expr + " (" + origin + ")");
                return new List<JProp>();
            }
            if ((m = Regex.Match(expr, @"^[A-Za-z_]\w*$")).Success)
            {
                var r = ResolveName(s, s.Cls, expr, new HashSet<string>(StringComparer.Ordinal));
                if (r != null) return r;
                foreach (var vc in s.Vanilla)
                {
                    var v = Vanilla(vc + "." + expr);
                    if (v != null) return v;
                }
                s.Warn("unresolved property " + expr + " in " + s.Cls.Name);
                return new List<JProp>();
            }
            s.Warn("unsupported property expression '" + expr + "' (" + origin + ")");
            return new List<JProp>();
        }

        static int IntConst(Scope s, string e)
        {
            e = e.Trim();
            if (s.Depth > 12) { s.Warn("integer constant too deep: " + e); return 0; }
            int v;
            if (int.TryParse(e, NumberStyles.Integer, CultureInfo.InvariantCulture, out v)) return v;
            if (VanillaInts.TryGetValue(e, out v)) return v;
            var seen = new HashSet<string>(StringComparer.Ordinal);
            string name = e.Split('.').Last();
            for (var ci = s.Cls; ci != null && seen.Add(ci.Name); ci = ci.Super != null ? Info(s.Ctx, ci.Super) : null)
            {
                foreach (var c in new[] { ci }.Concat(ci.Interfaces.Select(i => Info(s.Ctx, i)).Where(i => i != null)))
                {
                    var init = FieldInit(c.Text, name);
                    if (init != null && init != e) return IntConst(new Scope { Ctx = s.Ctx, Cls = c, Vanilla = s.Vanilla, Warn = s.Warn, Depth = s.Depth + 1 }, init);
                }
            }
            s.Warn("unresolved integer constant " + e + " in " + s.Cls.Name);
            return 0;
        }

        /// <summary>The constants of an enum (upstream enum, nested upstream enum, or a known vanilla enum) with their
        /// serialized names, in declaration order. A constant's name is its first string argument when it has
        /// one, else its lower-case Java name (upstream's getSerializedName implementations).</summary>
        static List<KeyValuePair<string, string>> EnumConstants(Scope s, string type)
        {
            string v;
            if (VanillaEnums.TryGetValue(type, out v))
                return v.Split(',').Select(x => new KeyValuePair<string, string>(x.ToUpperInvariant(), x)).ToList();
            var parts = type.Split('.');
            string simple = parts.Last();
            // the file that declares it: its own file, or the outer class's file (Outer.Inner), or the current class
            var candidates = new List<ClassInfo>();
            var own = Info(s.Ctx, simple);
            if (own != null) candidates.Add(own);
            if (parts.Length > 1) { var outer = Info(s.Ctx, parts[0]); if (outer != null) candidates.Add(outer); }
            if (s.Cls != null) candidates.Add(s.Cls);
            foreach (var ci in candidates)
            {
                var m = Regex.Match(ci.Text, @"\benum\s+" + Regex.Escape(simple) + @"\b[^{]*\{");
                if (!m.Success) continue;
                // the constants: up to the first ';' (or the closing brace) at depth 0
                int i = m.Index + m.Length, depth = 0, start = i;
                bool inStr = false;
                for (; i < ci.Text.Length; i++)
                {
                    char c = ci.Text[i];
                    if (inStr) { if (c == '\\') i++; else if (c == '"') inStr = false; continue; }
                    if (c == '"') inStr = true;
                    else if (c == '(' || c == '{') depth++;
                    else if (c == ')') depth--;
                    else if (c == '}') { if (depth == 0) break; depth--; }
                    else if (c == ';' && depth == 0) break;
                }
                var res = new List<KeyValuePair<string, string>>();
                foreach (var con in Args(ci.Text.Substring(start, i - start) + ")"))
                {
                    var cm = Regex.Match(con.Trim(), @"^([A-Z][A-Z0-9_]*)\s*(?:\(\s*(""([^""]*)"")?)?");
                    if (!cm.Success) continue;
                    res.Add(new KeyValuePair<string, string>(cm.Groups[1].Value, cm.Groups[3].Success ? cm.Groups[3].Value : cm.Groups[1].Value.ToLowerInvariant()));
                }
                if (res.Count > 0) return res;
            }
            return null;
        }

        /// <summary>Whether a class of the chain (or an interface it implements, transitively) is named iface.</summary>
        public static bool Implements(Ctx ctx, List<string> chain, string iface)
        {
            var todo = new Stack<string>(chain);
            var seen = new HashSet<string>(StringComparer.Ordinal);
            while (todo.Count > 0)
            {
                string c = todo.Pop();
                if (!seen.Add(c)) continue;
                if (c == iface) return true;
                var ci = Info(ctx, c);
                if (ci == null) continue;
                foreach (var i in ci.Interfaces) todo.Push(i);
                if (ci.Super != null) todo.Push(ci.Super);
            }
            return false;
        }

        /// <summary>The properties a constant expression ("LIGHTING", "FireJetBlock.STATE") denotes in an upstream class.</summary>
        public static List<JProp> Resolve(Ctx ctx, string cls, string expr, Action<string> warn)
        {
            var ci = Info(ctx, cls);
            if (ci == null) return new List<JProp>();
            return Eval(new Scope { Ctx = ctx, Cls = ci, Vanilla = new List<string>(), Warn = warn }, expr, cls);
        }

        /// <summary>For an upstream enum property: serialized value -> the int constructor argument named arg (the
        /// parameter its constructor stores in the field of that name), e.g. BrazierLight's "light".</summary>
        public static Dictionary<string, int> EnumArgs(Ctx ctx, string cls, JProp prop, string arg, Action<string> warn)
        {
            if (prop.EnumType == null) return null;
            string simple = prop.EnumType.Split('.').Last();
            foreach (var ci in new[] { Info(ctx, simple), Info(ctx, prop.EnumType.Split('.')[0]), Info(ctx, cls) }.Where(c => c != null))
            {
                var m = Regex.Match(ci.Text, @"\benum\s+" + Regex.Escape(simple) + @"\b[^{]*\{");
                if (!m.Success) continue;
                var ctor = Regex.Match(ci.Text, @"\b" + Regex.Escape(simple) + @"\s*\(([^)]*)\)\s*\{");
                if (!ctor.Success) continue;
                var pars = ctor.Groups[1].Value.Split(',').Select(p => p.Trim().Split(' ').Last()).ToList();
                // the parameter assigned to this.<arg>
                var asg = Regex.Match(ci.Text.Substring(ctor.Index), @"this\." + Regex.Escape(arg) + @"\s*=\s*(\w+)\s*;");
                int idx = pars.IndexOf(asg.Success ? asg.Groups[1].Value : arg);
                if (idx < 0) continue;
                var res = new Dictionary<string, int>(StringComparer.Ordinal);
                int i = m.Index + m.Length;
                int end = ci.Text.IndexOf(';', i);
                foreach (Match cm in Regex.Matches(ci.Text.Substring(i, end - i), @"\b([A-Z][A-Z0-9_]*)\s*\(([^)]*)\)"))
                {
                    var a = Args(cm.Groups[2].Value + ")");
                    int v;
                    string val;
                    if (idx < a.Count && int.TryParse(a[idx].Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out v) && prop.Consts.TryGetValue(cm.Groups[1].Value, out val))
                        res[val] = v;
                }
                if (res.Count > 0) return res;
            }
            warn("enum " + prop.EnumType + ": no int argument " + arg);
            return null;
        }

        /// <summary>The Java block-state properties of a block whose class chain (leaf first) is given.</summary>
        public static List<JProp> Collect(Ctx ctx, List<string> chain, Action<string> warn)
        {
            var res = new List<JProp>();
            var vanilla = chain.Where(c => Info(ctx, c) == null).ToList();
            Action<JProp> add = p =>
            {
                var old = res.FirstOrDefault(x => x.Name == p.Name);
                if (old == null) res.Add(p);
                else if (string.Join(",", old.Values) != string.Join(",", p.Values))
                    warn("property " + p.Name + " declared twice with different values (" + old.Origin + ", " + p.Origin + ")");
            };
            for (int k = 0; k < chain.Count; k++)
            {
                string cls = chain[k];
                var ci = Info(ctx, cls);
                if (ci == null)
                {
                    string[] specs;
                    if (VanillaClass.TryGetValue(cls, out specs)) foreach (var sp in specs) add(Spec(sp, cls));
                    else warn("vanilla block class " + cls + " has no known property set (JavaStates.VanillaClass)");
                    // MultifaceBlock: upstream subclasses that support fewer faces override isFaceSupported
                    if (cls == "MultifaceBlock" && chain.Take(k).Any(c => Info(ctx, c) != null && Info(ctx, c).Text.Contains("isFaceSupported")))
                        warn(chain[0] + " overrides MultifaceBlock.isFaceSupported: every face kept");
                    break;
                }
                var mm = Regex.Match(ci.Text, @"void\s+createBlockStateDefinition\s*\(\s*(?:final\s+)?StateDefinition\.Builder\s*<[^>]*>\s*(\w+)\s*\)");
                if (!mm.Success) continue;
                string body = BraceBody(ci.Text, mm.Index + mm.Length) ?? "";
                string b = mm.Groups[1].Value;
                var scope = new Scope { Ctx = ctx, Cls = ci, Vanilla = vanilla, Warn = warn };
                // builder.add(A, B), chained .add(C), super.createBlockStateDefinition(builder.add(D))
                foreach (Match am in Regex.Matches(body, @"(?:\b" + Regex.Escape(b) + @"|\))\s*\.add\("))
                    foreach (var arg in Args(body.Substring(am.Index + am.Length)))
                        if (arg.Trim().Length > 0) foreach (var p in Eval(scope, arg, cls)) add(p);
                // LIST.forEach(builder::add)
                foreach (Match fm in Regex.Matches(body, @"([\w.]+)\.forEach\(\s*" + Regex.Escape(b) + @"::add\s*\)"))
                    foreach (var p in Eval(scope, fm.Groups[1].Value, cls)) add(p);
                // for (... p : LIST) builder.add(p) is covered by the list constant only when written as forEach
                if (Regex.IsMatch(body, @"\bfor\s*\(")) warn(cls + ".createBlockStateDefinition uses a loop: check its properties");
                if (!Regex.IsMatch(body, @"\bsuper\s*\.\s*createBlockStateDefinition\s*\(")) break;
            }
            Defaults(ctx, chain, vanilla, res, warn);
            return res;
        }

        /// <summary>Vanilla constructors' registerDefaultState values (Minecraft 1.21.1); the properties they do
        /// not set keep StateDefinition.any(): booleans true, integers their minimum, enums their first value.</summary>
        static readonly Dictionary<string, string> VanillaDefaults = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            { "RotatedPillarBlock", "axis=y" }, { "LeavesBlock", "distance=7,persistent=false,waterlogged=false" }, { "SaplingBlock", "stage=0" },
            { "StairBlock", "facing=north,half=bottom,shape=straight,waterlogged=false" }, { "SlabBlock", "type=bottom,waterlogged=false" },
            { "FenceBlock", "north=false,east=false,south=false,west=false,waterlogged=false" },
            { "IronBarsBlock", "north=false,east=false,south=false,west=false,waterlogged=false" },
            { "FenceGateBlock", "open=false,powered=false,in_wall=false" },
            { "WallBlock", "up=true,north=none,east=none,south=none,west=none,waterlogged=false" },
            { "DoorBlock", "facing=north,open=false,hinge=left,powered=false,half=lower" },
            { "TrapDoorBlock", "facing=north,open=false,half=bottom,powered=false,waterlogged=false" },
            { "ButtonBlock", "facing=north,powered=false,face=wall" }, { "PressurePlateBlock", "powered=false" }, { "WeightedPressurePlateBlock", "power=0" },
            { "StandingSignBlock", "rotation=0,waterlogged=false" }, { "WallSignBlock", "facing=north,waterlogged=false" },
            { "CeilingHangingSignBlock", "rotation=0,attached=false,waterlogged=false" }, { "WallHangingSignBlock", "facing=north,waterlogged=false" },
            { "ChestBlock", "facing=north,type=single,waterlogged=false" }, { "TrappedChestBlock", "facing=north,type=single,waterlogged=false" },
            { "LadderBlock", "facing=north,waterlogged=false" }, { "WallTorchBlock", "facing=north" }, { "DoublePlantBlock", "half=lower" },
            { "LanternBlock", "hanging=false,waterlogged=false" }, { "CandleBlock", "candles=1,lit=false,waterlogged=false" },
            { "AbstractFurnaceBlock", "facing=north,lit=false" },
            { "HugeMushroomBlock", "north=true,east=true,south=true,west=true,up=true,down=true" },
            { "MultifaceBlock", "north=false,east=false,south=false,west=false,up=false,down=false" },
            { "ChiseledBookShelfBlock", "facing=north,slot_0_occupied=false,slot_1_occupied=false,slot_2_occupied=false,slot_3_occupied=false,slot_4_occupied=false,slot_5_occupied=false" },
        };

        /// <summary>Sets JProp.Default to upstream's default state (Java's, not Bedrock's): the leaf-most
        /// registerDefaultState call of the class chain wins per property (also through a local BlockState variable
        /// and a "for (p : LIST) state = state.setValue(p, v)" loop); a call built on this.defaultBlockState() keeps
        /// the superclass's values, one built on stateDefinition.any() starts afresh; without an upstream call the vanilla
        /// class's defaults apply; every property still unset has StateDefinition.any()'s value (a boolean is true,
        /// an integer its minimum, an enum its first value).</summary>
        static void Defaults(Ctx ctx, List<string> chain, List<string> vanilla, List<JProp> props, Action<string> warn)
        {
            bool fresh = false;
            foreach (var cls in chain)
            {
                var ci = Info(ctx, cls);
                if (ci == null)
                {
                    string vd;
                    if (!fresh && VanillaDefaults.TryGetValue(cls, out vd))
                        foreach (var kv in vd.Split(',').Select(x => x.Split('=')))
                        {
                            var prop = props.FirstOrDefault(p => p.Name == kv[0]);
                            if (prop != null && prop.Default == null && prop.Values.Contains(kv[1])) prop.Default = kv[1];
                        }
                    break;
                }
                const string recv = @"this\.(getStateDefinition\(\)\.any\(\)|stateDefinition\.any\(\)|defaultBlockState\(\))";
                var m = Regex.Match(ci.Text, @"registerDefaultState\(\s*" + recv);
                string call = null, receiver = null, loopText = null, variable = null;
                if (m.Success) { call = ci.Text.Substring(m.Index + m.Length); receiver = m.Groups[1].Value; }
                else
                {
                    // BlockState state = this.getStateDefinition().any().setValue(...); [for loops]; registerDefaultState(state);
                    var rv = Regex.Match(ci.Text, @"registerDefaultState\(\s*(\w+)\s*\)");
                    if (!rv.Success) continue;
                    variable = rv.Groups[1].Value;
                    var vm = Regex.Match(ci.Text, @"\b" + Regex.Escape(variable) + @"\s*=\s*" + recv);
                    if (!vm.Success || vm.Index > rv.Index) { warn(cls + ": registerDefaultState(" + variable + ") not understood"); continue; }
                    call = ci.Text.Substring(vm.Index + vm.Length);
                    receiver = vm.Groups[1].Value;
                    loopText = ci.Text.Substring(vm.Index, rv.Index - vm.Index);
                }
                var scope = new Scope { Ctx = ctx, Cls = ci, Vanilla = vanilla, Warn = warn };
                Action<JProp, string, string> set = (prop, v, where) =>
                {
                    if (prop == null || prop.Default != null) return;   // a subclass already set it
                    string val;
                    if (prop.Type == "enum")
                    {
                        string c = v.Split('.').Last();
                        val = prop.Consts.TryGetValue(c, out val) ? val : c.ToLowerInvariant();
                    }
                    else val = v.TrimEnd('F', 'f');
                    if (prop.Values.Contains(val)) prop.Default = val;
                    else warn("default " + prop.Name + "=" + v + " (" + where + ") is not a value of the property");
                };
                // .setValue(P, V) links directly after the receiver (stop at the call's closing parenthesis)
                int pos = 0;
                while (true)
                {
                    var sm = Regex.Match(call.Substring(pos), @"^\s*\.setValue\(");
                    if (!sm.Success) break;
                    int argStart = pos + sm.Length;
                    var args = Args(call.Substring(argStart));
                    if (args.Count < 2) break;
                    int len = args.Sum(a => a.Length) + args.Count - 1;
                    pos = argStart + len + 1;
                    var target = Eval(scope, args[0], cls);
                    if (target.Count == 1) set(props.FirstOrDefault(p => p.Name == target[0].Name), args[1].Trim(), cls);
                }
                if (loopText != null)
                    foreach (Match lm in Regex.Matches(loopText, @"for\s*\(\s*\w+\s+(\w+)\s*:\s*([\w.]+)\s*\)\s*\{?\s*" + Regex.Escape(variable) + @"\s*=\s*" + Regex.Escape(variable) + @"\.setValue\(\s*\1\s*,\s*([^)]+)\)"))
                        foreach (var p in Eval(scope, lm.Groups[2].Value, cls)) set(props.FirstOrDefault(x => x.Name == p.Name), lm.Groups[3].Value.Trim(), cls);
                if (receiver != "defaultBlockState()") { fresh = true; break; }
            }
            foreach (var p in props.Where(p => p.Default == null))
                p.Default = p.Type == "bool" ? "true" : p.Values[0];
        }
    }
}