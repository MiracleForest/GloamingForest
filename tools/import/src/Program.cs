// GloamingForest import tool (contract "import", unit F1): entry point called by tools/import/run.ps1.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;

namespace GfImport
{
    public static class Program
    {
        public static readonly string[] AllKinds =
        {
            "textures", "models", "blockstates", "lang", "tags", "loot", "recipes", "advancements", "worldgen", "datamaps",
            "registries", "particles", "sounds", "skeleton", "catalog", "families",
        };

        /// <summary>Runs the import. Returns 0 (ok), 1 (failure or -Check differences).</summary>
        public static int Run(string root, string tf, string[] kinds, string unit, bool check, Action<string> log)
        {
            var sw = Stopwatch.StartNew();
            var sel = new HashSet<string>(kinds.Length == 0 ? AllKinds : kinds, StringComparer.Ordinal);
            foreach (var k in sel) if (!AllKinds.Contains(k)) { log("unknown kind '" + k + "' (kinds: " + string.Join(",", AllKinds) + ")"); return 1; }
            Java.Reset();                 // static caches of an earlier run in the same session
            var ctx = new Ctx(root, tf);
            if (string.IsNullOrEmpty(unit)) unit = null;
            if (unit != null)
            {
                if (ctx.UnitNames.ContainsKey(unit)) unit = ctx.UnitNames[unit];
                if (!ctx.UnitNames.ContainsValue(unit)) { log("unknown unit '" + unit + "'"); return 1; }
            }
            // Everything is computed (cheap); -Kinds / -Unit only select what is written or checked, so
            // shared decisions (texture owners, atlas keys, geometry owners) never depend on the selection.
            ctx.ComputeItemTags();
            ctx.PrepareBlocks();
            ctx.ComputeHarvest();         // tools, tier and drop rule of every block (TF's tool tags)
            ctx.CheckJavaStates();        // against the hand-written golden (tools/import/expected-states.json)
            ctx.ImportBlocks();           // blockstates + models (records texture uses)
            ctx.ImportItems();            // skeleton items (records icon uses)
            ctx.ImportEntities();         // skeleton entities
            ctx.ImportParticles();
            ctx.ImportTextures();         // after every texture user is known
            ctx.ImportLang();
            ctx.ImportRecipes();
            ctx.ImportData(new HashSet<string>(AllKinds));
            ctx.ImportSounds();
            ctx.ImportCatalog();
            ctx.ImportFamilies();
            ctx.FlushFragments();
            ctx.ApplyOverrides();
            ctx.CheckEmittedBlocks();     // states and variant rotations of the final BP block files
            ctx.CheckBehaviour();         // their behaviour values against tools/import/expected-behaviour.json
            ctx.ImportSkeletonIndex();    // the id list the import.* selftests check
            ctx.ImportHarvestIndex();     // the drop rule the runtime applies, tools and tiers for the selftests
            ctx.ImportKnownIndex();       // after every data file is final
            if (unit == null && sel.Contains("blockstates")) ctx.Docs["docs/spec/blockstates.md"] = ctx.BlockstatesDoc();

            foreach (var m in ctx.ModelLib.UnknownParents) ctx.Warn("unknown parent model " + m);
            foreach (var t in ctx.MissingTextures) ctx.Warn("texture not found: " + t);
            int files = ctx.Out.Values.Sum(u => u.Count);
            log(string.Format("generated {0} files for {1} units in {2:F1}s", files, ctx.Out.Count, sw.Elapsed.TotalSeconds));
            int diffs = ctx.Sync(sel, unit, check);
            foreach (var l in ctx.Log) log(l);
            foreach (var w in ctx.Warnings.Distinct().OrderBy(x => x, StringComparer.Ordinal)) log("warning: " + w);
            foreach (var e in ctx.Errors.Distinct()) log("error: " + e);
            if (check && diffs > 0) { log("the committed tree differs from the import (" + diffs + " files): run pwsh tools/import/run.ps1"); return 1; }
            if (ctx.Errors.Count > 0) { log(ctx.Errors.Distinct().Count() + " check(s) failed"); return 1; }
            return 0;
        }
    }
}
