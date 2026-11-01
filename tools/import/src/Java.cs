// GloamingForest import tool (contract "import", unit F1): reads TF's registration code
// (init/TFBlocks.java, TFItems.java, TFEntities.java, TFCreativeTabs.java) for the facts the data
// files do not carry: block classes and properties, item stack sizes, entity sizes and spawn egg
// colours, and the creative tab order. Facts only; no Java code is translated here.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;

namespace GfImport
{
    public sealed class JBlock
    {
        public string Id, Const, Cls, Stmt;
        public double Hardness = 1.0, Resistance = -1;  // -1 resistance: = hardness
        public int Light = 0;
        public bool DynamicLight, NoCollision, NoOcclusion, Instabreak, Unbreakable, Flammable, Replaceable;
        public string Sound = "STONE", MapColor;
        public double Friction = -1;
        public string CopyOf;      // TF constant or "Blocks.X"
    }

    public sealed class JItem
    {
        public string Id, Const, Cls, Stmt;
        public int StackSize = 64, Durability = 0;
        public bool FireResistant;
        public string Rarity;
        public bool Food;
    }

    public sealed class JEntity
    {
        public string Id, Cls, Category;
        public double Width = 0.6, Height = 1.8;
        public bool HasEgg, FireImmune;
        public int EggPrimary, EggSecondary;
    }

    public static class Java
    {
        public static string StripComments(string text)
        {
            text = Regex.Replace(text, @"/\*.*?\*/", "", RegexOptions.Singleline);
            return Regex.Replace(text, @"(?m)^\s*//.*$|//[^\n""]*$", "");
        }

        /// <summary>Statements "public static final Type NAME = ...;" (parentheses balanced), in file order.</summary>
        public static IEnumerable<Tuple<string, string, string>> Fields(string text, string typePrefix)
        {
            var re = new Regex(@"public\s+static\s+final\s+(" + typePrefix + @"[^=;]*?)\s+(\w+)\s*=\s*", RegexOptions.Compiled);
            foreach (Match m in re.Matches(text))
            {
                int i = m.Index + m.Length, depth = 0;
                bool inStr = false;
                int start = i;
                for (; i < text.Length; i++)
                {
                    char c = text[i];
                    if (inStr) { if (c == '\\') i++; else if (c == '"') inStr = false; continue; }
                    if (c == '"') inStr = true;
                    else if (c == '(' || c == '{') depth++;
                    else if (c == ')' || c == '}') depth--;
                    else if (c == ';' && depth <= 0) break;
                }
                yield return Tuple.Create(m.Groups[1].Value, m.Groups[2].Value, text.Substring(start, i - start));
            }
        }

        static double F(string s) { return double.Parse(s.TrimEnd('F', 'f', 'D', 'd'), CultureInfo.InvariantCulture); }

        public static Dictionary<string, JBlock> Blocks(Ctx ctx)
        {
            string text = StripComments(Ctx.ReadText(Path.Combine(ctx.JavaDir, "init", "TFBlocks.java")));
            var props = new Dictionary<string, string>();
            foreach (var f in Fields(text, @"BlockBehaviour\.Properties")) props[f.Item2] = f.Item3;
            var helpers = new Dictionary<string, string>();
            foreach (Match m in Regex.Matches(text, @"public static DeferredBlock<(\w+)> (\w+)\(String name[^)]*\)\s*\{(.*?)\n\t\}", RegexOptions.Singleline))
                helpers[m.Groups[2].Value] = m.Groups[1].Value + "|" + m.Groups[3].Value;
            var byConst = new Dictionary<string, JBlock>();
            var res = new Dictionary<string, JBlock>(StringComparer.Ordinal);
            foreach (var f in Fields(text, @"DeferredBlock<"))
            {
                var idm = Regex.Match(f.Item3, @"^\s*([\w.]+)\(\s*""([a-z0-9_]+)""");
                if (!idm.Success) continue;
                var b = new JBlock { Id = idm.Groups[2].Value, Const = f.Item2, Stmt = f.Item3 };
                string body = f.Item3;
                string call = idm.Groups[1].Value;
                string helper;
                if (helpers.TryGetValue(call, out helper) && !body.Contains("new "))
                {
                    var hp = helper.Split(new[] { '|' }, 2);
                    b.Cls = hp[0];
                    body = hp[1] + body;
                }
                var cm = Regex.Match(body, @"new\s+(\w+)\s*[(<]");
                if (b.Cls == null) b.Cls = cm.Success ? cm.Groups[1].Value : Regex.Match(f.Item1, @"DeferredBlock<(\w+)").Groups[1].Value;
                // inline named property sets (TWILIGHT_OAK_LOG_PROPS)
                foreach (Match pm in Regex.Matches(body, @"\b([A-Z][A-Z0-9_]*_PROPS)\b"))
                {
                    string p;
                    if (props.TryGetValue(pm.Groups[1].Value, out p)) body = body + " " + p;
                }
                ParseProps(b, body);
                res[b.Id] = b;
                byConst[b.Const] = b;
            }
            // property copies: ofFullCopy(OTHER.get()) / ofFullCopy(OTHER) / Blocks.X; a copied block is
            // resolved before the blocks that copy it (chains: A copies B copies C)
            var resolved = new HashSet<JBlock>();
            Action<JBlock> resolve = null;
            resolve = b =>
            {
                if (b.CopyOf == null || !resolved.Add(b)) return;
                JBlock src;
                int at = b.Stmt.IndexOf("Copy(", StringComparison.Ordinal);
                string after = at >= 0 ? b.Stmt.Substring(at) : "";
                if (byConst.TryGetValue(b.CopyOf, out src) && src != b)
                {
                    resolve(src);
                    b.Hardness = src.Hardness; b.Resistance = src.Resistance; b.Sound = src.Sound; b.MapColor = src.MapColor;
                    b.Light = src.Light; b.Flammable = src.Flammable; b.Unbreakable = src.Unbreakable; b.Instabreak = src.Instabreak;
                    ParseProps(b, after, false); // explicit settings after the copy win
                }
                else if (b.CopyOf.StartsWith("Blocks.", StringComparison.Ordinal))
                {
                    VanillaProps(b, b.CopyOf.Substring(7));
                    ParseProps(b, after, false);
                }
            };
            foreach (var b in res.Values) resolve(b);
            return res;
        }

        static void VanillaProps(JBlock b, string v)
        {
            // the vanilla blocks TF copies properties from (Minecraft 1.21.1 values)
            if (v == "SPAWNER") { b.Hardness = 5; b.Resistance = 5; b.Sound = "METAL"; b.MapColor = "STONE"; }
            else if (v == "FLOWER_POT") { b.Hardness = 0; b.Resistance = 0; b.Instabreak = true; b.Sound = "STONE"; }
            else if (v.EndsWith("_HEAD") || v.EndsWith("_SKULL")) { b.Hardness = 1; b.Resistance = 1; b.Sound = "STONE"; }
            else if (v.EndsWith("_CARPET")) { b.Hardness = 0.1; b.Resistance = 0.1; b.Sound = "WOOL"; b.Flammable = true; }
            else if (v.EndsWith("_PLANKS")) { b.Hardness = 2; b.Resistance = 3; b.Sound = "WOOD"; b.Flammable = true; b.MapColor = "WOOD"; }
            else if (v.EndsWith("_LOG") || v.EndsWith("_WOOD")) { b.Hardness = 2; b.Resistance = 2; b.Sound = "WOOD"; b.Flammable = true; b.MapColor = "WOOD"; }
            else if (v == "OAK_SIGN" || v == "OAK_WALL_SIGN" || v.EndsWith("HANGING_SIGN")) { b.Hardness = 1; b.Resistance = 1; b.Sound = "WOOD"; b.NoCollision = true; b.Flammable = true; }
            else if (v == "CHEST" || v == "TRAPPED_CHEST") { b.Hardness = 2.5; b.Resistance = 2.5; b.Sound = "WOOD"; b.Flammable = true; }
            else if (v == "BOOKSHELF") { b.Hardness = 1.5; b.Resistance = 1.5; b.Sound = "WOOD"; b.Flammable = true; }
            else if (v == "LADDER") { b.Hardness = 0.4; b.Resistance = 0.4; b.Sound = "LADDER"; }
            else if (v == "VINE") { b.Hardness = 0.2; b.Resistance = 0.2; b.Sound = "VINE"; b.NoCollision = true; }
            else if (v == "COBBLESTONE" || v == "STONE" || v == "STONE_BRICKS") { b.Hardness = 2; b.Resistance = 6; b.Sound = "STONE"; }
            else if (v == "OBSIDIAN") { b.Hardness = 50; b.Resistance = 1200; b.Sound = "STONE"; }
            else if (v == "TORCH" || v == "WALL_TORCH") { b.Hardness = 0; b.Instabreak = true; b.Light = 14; b.Sound = "WOOD"; b.NoCollision = true; }
            else if (v == "MOSS_CARPET") { b.Hardness = 0.1; b.Sound = "MOSS_CARPET"; }
            else if (v == "AIR") { b.Hardness = 0; b.NoCollision = true; }
        }

        static void ParseProps(JBlock b, string s, bool resetCopy = true)
        {
            Match m;
            if (resetCopy)
            {
                m = Regex.Match(s, @"of(?:Full|Legacy)Copy\(\s*([\w.]+?)(?:\.get\(\))?\s*\)");
                if (m.Success) b.CopyOf = m.Groups[1].Value.StartsWith("TFBlocks.") ? m.Groups[1].Value.Substring(9) : m.Groups[1].Value;
            }
            foreach (Match sm in Regex.Matches(s, @"\.strength\(\s*(-?[\d.]+F?)\s*(?:,\s*(-?[\d.]+F?))?\s*\)"))
            {
                b.Hardness = F(sm.Groups[1].Value);
                b.Resistance = sm.Groups[2].Success ? F(sm.Groups[2].Value) : b.Hardness;
            }
            m = Regex.Match(s, @"\.destroyTime\(\s*(-?[\d.]+F?)\s*\)");
            if (m.Success) b.Hardness = F(m.Groups[1].Value);
            m = Regex.Match(s, @"\.explosionResistance\(\s*(-?[\d.]+F?)\s*\)");
            if (m.Success) b.Resistance = F(m.Groups[1].Value);
            if (s.Contains(".instabreak()")) { b.Hardness = 0; b.Resistance = 0; b.Instabreak = true; }
            if (b.Hardness < 0) b.Unbreakable = true;
            m = Regex.Match(s, @"\.lightLevel\(\s*\(?\s*\w+\s*\)?\s*->\s*(\d+)\s*\)");
            if (m.Success) { b.Light = int.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture); b.DynamicLight = false; }
            else if (s.Contains(".lightLevel(")) b.DynamicLight = true;
            m = Regex.Match(s, @"\.sound\(\s*(?:SoundType|TFSoundTypes)\.(\w+)\s*\)");
            if (m.Success) b.Sound = m.Groups[1].Value;
            m = Regex.Match(s, @"(?:\.mapColor|logProperties)\(\s*(?:MapColor\.(\w+)|\(state\)[^?]*\?\s*(\w+))");
            if (m.Success) b.MapColor = m.Groups[1].Success ? m.Groups[1].Value : null;
            m = Regex.Match(s, @"\.mapColor\(\s*DyeColor\.(\w+)\s*\)");
            if (m.Success) b.MapColor = "COLOR_" + m.Groups[1].Value;
            if (s.Contains(".noCollission()")) b.NoCollision = true;
            if (s.Contains(".noOcclusion()")) b.NoOcclusion = true;
            if (s.Contains(".ignitedByLava()")) b.Flammable = true;
            if (s.Contains(".replaceable()")) b.Replaceable = true;
            m = Regex.Match(s, @"\.friction\(\s*([\d.]+F?)\s*\)");
            if (m.Success) b.Friction = F(m.Groups[1].Value);
        }

        public static Dictionary<string, JItem> Items(Ctx ctx)
        {
            string text = StripComments(Ctx.ReadText(Path.Combine(ctx.JavaDir, "init", "TFItems.java")));
            var res = new Dictionary<string, JItem>(StringComparer.Ordinal);
            foreach (var f in Fields(text, @"DeferredItem<"))
            {
                var idm = Regex.Match(f.Item3, @"^\s*[\w.]+\(\s*""([a-z0-9_]+)""");
                if (!idm.Success) continue;
                var it = new JItem { Id = idm.Groups[1].Value, Const = f.Item2, Stmt = f.Item3 };
                var cm = Regex.Match(f.Item3, @"new\s+(\w+)\s*\(");
                it.Cls = cm.Success ? cm.Groups[1].Value : "Item";
                var m = Regex.Match(f.Item3, @"\.stacksTo\(\s*(\d+)\s*\)");
                if (m.Success) it.StackSize = int.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture);
                m = Regex.Match(f.Item3, @"\.durability\(\s*(\d+)\s*\)");
                if (m.Success) it.Durability = int.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture);
                m = Regex.Match(f.Item3, @"getDurability\(\s*(\d+)\s*\)");
                if (m.Success)
                {
                    // ArmorItem.Type.X.getDurability(n): helmet 11, chestplate 16, leggings 15, boots 13 (x n)
                    var tm = Regex.Match(f.Item3, @"ArmorItem\.Type\.(\w+)\.getDurability");
                    int mult = 16;
                    if (tm.Success) mult = tm.Groups[1].Value == "HELMET" ? 11 : tm.Groups[1].Value == "CHESTPLATE" ? 16 : tm.Groups[1].Value == "LEGGINGS" ? 15 : 13;
                    it.Durability = mult * int.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture);
                }
                if (it.Durability > 0) it.StackSize = 1;
                if (Regex.IsMatch(it.Cls, "(Armor|Sword|Axe|Pickaxe|Shovel|Hoe|Bow|Scepter|Wand|Shield)Item$")) it.StackSize = 1;
                if (f.Item3.Contains(".fireResistant()")) it.FireResistant = true;
                m = Regex.Match(f.Item3, @"\.rarity\(\s*(?:Rarity\.|TFRarityEnumExtension\.\w*\.|tfRarityEnumExtension\.)?(\w+)");
                if (m.Success) it.Rarity = m.Groups[1].Value;
                if (f.Item3.Contains(".food(")) it.Food = true;
                res[it.Id] = it;
            }
            return res;
        }

        public static Dictionary<string, JEntity> Entities(Ctx ctx)
        {
            var names = new Dictionary<string, string>();
            foreach (Match m in Regex.Matches(Ctx.ReadText(Path.Combine(ctx.JavaDir, "entity", "TFEntityNames.java")), @"ResourceLocation (\w+) = prefix\(""([a-z0-9_]+)""\)"))
                names[m.Groups[1].Value] = m.Groups[2].Value;
            string text = StripComments(Ctx.ReadText(Path.Combine(ctx.JavaDir, "init", "TFEntities.java")));
            var res = new Dictionary<string, JEntity>(StringComparer.Ordinal);
            foreach (var f in Fields(text, @"DeferredHolder<EntityType"))
            {
                var m = Regex.Match(f.Item3, @"^\s*(make|buildNoEgg|build)\(\s*TFEntityNames\.(\w+)\s*,\s*(.*)$", RegexOptions.Singleline);
                if (!m.Success) continue;
                string id;
                if (!names.TryGetValue(m.Groups[2].Value, out id)) continue;
                var e = new JEntity { Id = id };
                string rest = m.Groups[3].Value;
                var cls = Regex.Match(rest, @"(\w+)::new");
                if (cls.Success) e.Cls = cls.Groups[1].Value;
                var cat = Regex.Match(rest, @"MobCategory\.(\w+)");
                if (cat.Success) e.Category = cat.Groups[1].Value;
                if (m.Groups[1].Value == "make")
                {
                    // make(id, factory, category, width, height, [eyeHeight,] [fireproof,] primary, secondary [, ridingOffset])
                    var args = SplitArgs(rest);
                    var nums = new List<string>();
                    for (int i = 2; i < args.Count; i++) nums.Add(args[i].Trim());
                    e.Width = F(nums[0]); e.Height = F(nums[1]);
                    int k = 2;
                    if (k < nums.Count && nums[k].EndsWith("F") && k + 1 < nums.Count && (nums[k + 1] == "true" || nums[k + 1] == "false")) k++;
                    if (k < nums.Count && (nums[k] == "true" || nums[k] == "false")) { e.FireImmune = nums[k] == "true"; k++; }
                    e.EggPrimary = ParseInt(nums[k]); e.EggSecondary = ParseInt(nums[k + 1]);
                    e.HasEgg = true;
                }
                else
                {
                    var sz = Regex.Match(rest, @"(?:makeCastedBuilder\([^,]+,\s*[\w:]+,\s*|makeBuilder\([\w:]+,\s*MobCategory\.\w+,\s*|\.sized\()\s*([\d.]+F?)\s*,\s*([\d.]+F?)");
                    if (sz.Success) { e.Width = F(sz.Groups[1].Value); e.Height = F(sz.Groups[2].Value); }
                    var fp = Regex.Match(rest, @",\s*(true|false)\s*(?:,\s*(0x[0-9a-fA-F]+|\d+)\s*,\s*(0x[0-9a-fA-F]+|\d+))?\s*\)\s*$");
                    if (fp.Success)
                    {
                        e.FireImmune = fp.Groups[1].Value == "true";
                        if (m.Groups[1].Value == "build" && fp.Groups[2].Success) { e.HasEgg = true; e.EggPrimary = ParseInt(fp.Groups[2].Value); e.EggSecondary = ParseInt(fp.Groups[3].Value); }
                    }
                }
                res[e.Id] = e;
            }
            return res;
        }

        static int ParseInt(string s)
        {
            s = s.Trim();
            if (s.StartsWith("0x")) return int.Parse(s.Substring(2), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
            return int.Parse(s, CultureInfo.InvariantCulture);
        }

        /// <summary>Top-level comma-separated arguments of "a, b(c, d), e)" (stops at the closing parenthesis).</summary>
        public static List<string> SplitArgs(string s)
        {
            var res = new List<string>();
            int depth = 0, start = 0;
            for (int i = 0; i < s.Length; i++)
            {
                char c = s[i];
                if (c == '(' || c == '<' || c == '{') depth++;
                else if (c == ')' || c == '>' || c == '}')
                {
                    if (depth == 0) { res.Add(s.Substring(start, i - start)); return res; }
                    depth--;
                }
                else if (c == ',' && depth == 0) { res.Add(s.Substring(start, i - start)); start = i + 1; }
            }
            res.Add(s.Substring(start));
            return res;
        }

        public sealed class Tab
        {
            public string Id, TitleKey, IconConst, Icon;
            public List<string> Items = new List<string>();
        }

        /// <summary>TFCreativeTabs: the 4 tabs, their icon and their item ids in order.</summary>
        public static List<Tab> CreativeTabs(Ctx ctx, Dictionary<string, JBlock> blocks, Dictionary<string, JItem> items, IEnumerable<string> spawnEggs)
        {
            var constId = new Dictionary<string, string>();
            foreach (var b in blocks.Values) constId["TFBlocks." + b.Const] = b.Id;
            foreach (var i in items.Values) constId["TFItems." + i.Const] = i.Id;
            string text = StripComments(Ctx.ReadText(Path.Combine(ctx.JavaDir, "init", "TFCreativeTabs.java")));
            var tabs = new List<Tab>();
            var re = new Regex(@"TABS\.register\(""(\w+)""");
            var starts = re.Matches(text).Cast<Match>().ToList();
            for (int t = 0; t < starts.Count; t++)
            {
                int from = starts[t].Index, to = t + 1 < starts.Count ? starts[t + 1].Index : text.IndexOf("private static void", from, StringComparison.Ordinal);
                if (to < 0) to = text.Length;
                string body = text.Substring(from, to - from);
                var tab = new Tab { Id = "twilightforest:" + starts[t].Groups[1].Value };
                var tk = Regex.Match(body, @"translatable\(""([^""]+)""\)");
                if (tk.Success) tab.TitleKey = tk.Groups[1].Value;
                var ic = Regex.Match(body, @"\.icon\(\(\)\s*->\s*new ItemStack\((TF\w+\.\w+)");
                if (ic.Success) { tab.IconConst = ic.Groups[1].Value; string icon; if (constId.TryGetValue(tab.IconConst, out icon)) tab.Icon = "twilightforest:" + icon; }
                var seen = new HashSet<string>();
                foreach (Match m in Regex.Matches(body, @"output\.accept\((TF(?:Blocks|Items)\.\w+)|(generateGearWithEnchants|createDefaultSkullCandle)\(output,\s*(TF(?:Blocks|Items)\.\w+)|(createSpawnEggsAlphabetical|createCaskets|createGlassSwordAndLoreVer)\(output"))
                {
                    var add = new List<string>();
                    if (m.Groups[1].Success || m.Groups[3].Success)
                    {
                        string c = m.Groups[1].Success ? m.Groups[1].Value : m.Groups[3].Value;
                        string id;
                        if (constId.TryGetValue(c, out id)) add.Add(id);
                        else ctx.Warn("creative tab " + tab.Id + ": unknown constant " + c);
                    }
                    else if (m.Groups[4].Value == "createSpawnEggsAlphabetical") add.AddRange(spawnEggs.OrderBy(x => x, StringComparer.Ordinal));
                    else if (m.Groups[4].Value == "createCaskets") add.Add("keepsake_casket");
                    else if (m.Groups[4].Value == "createGlassSwordAndLoreVer") add.Add("glass_sword");
                    foreach (var a in add) if (seen.Add(a)) tab.Items.Add("twilightforest:" + a);
                }
                if (tab.Icon == null && tab.Items.Count > 0) tab.Icon = tab.Items[0];
                tabs.Add(tab);
            }
            return tabs;
        }

        /// <summary>The superclass chain of a TF block class (TF classes from block/*.java, then the vanilla base).</summary>
        static Dictionary<string, string> classFiles;

        /// <summary>The source file of a TF class by simple name (null for vanilla classes).</summary>
        public static string ClassFile(Ctx ctx, string cls)
        {
            if (classFiles == null)
            {
                classFiles = new Dictionary<string, string>(StringComparer.Ordinal);
                foreach (var f in Directory.GetFiles(ctx.JavaDir, "*.java", SearchOption.AllDirectories).OrderBy(x => x, StringComparer.Ordinal))
                {
                    var n = Path.GetFileNameWithoutExtension(f);
                    if (!classFiles.ContainsKey(n)) classFiles[n] = f;
                }
            }
            string file;
            return classFiles.TryGetValue(cls, out file) ? file : null;
        }

        public static List<string> ClassChain(Ctx ctx, string cls, Dictionary<string, string> extendsCache)
        {
            ClassFile(ctx, cls);
            var chain = new List<string>();
            string c = cls;
            for (int guard = 0; c != null && guard < 12; guard++)
            {
                chain.Add(c);
                string sup;
                if (!extendsCache.TryGetValue(c, out sup))
                {
                    sup = null;
                    string file;
                    if (classFiles.TryGetValue(c, out file))
                    {
                        var m = Regex.Match(Ctx.ReadText(file), @"class\s+" + c + @"\b[^{]*?\bextends\s+([\w.]+)");
                        if (m.Success) sup = m.Groups[1].Value.Split('.').Last();
                    }
                    extendsCache[c] = sup;
                }
                c = sup;
            }
            return chain;
        }
    }
}
