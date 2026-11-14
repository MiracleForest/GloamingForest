# GloamingForest - the Gloaming Forest for Minecraft: Bedrock Edition.
# The upstream reference (rule R24): the pinned Java source tree every tool reads, in two forms.
# Part of GloamingForest, LGPL-3.0-or-later.
#
#   clone  %LOCALAPPDATA%\gf-upstream\clone ($env:GF_UPSTREAM_CLONE): the blob-less, sparse git checkout
#          of the commit pinned in NOTICE (tools/upstream/fetch.ps1). Only fetch.ps1 and the ARR guard
#          (its tree listing) read it.
#   view   %LOCALAPPDATA%\gf-upstream\view ($env:GF_UPSTREAM_HOME): a copy of the clone under this
#          project's names, which every other tool reads. Its paths and texts use this project's
#          namespace (tools/upstream/names.json) in place of the upstream mod id, and the localized mod
#          names of names.json in the matching lang files; links in lang texts are dropped. Binary
#          files are copied unchanged. The view is rebuilt whenever the clone or these rules change.
#
# Nothing here spells the upstream names: they are read from the clone (its gradle.properties mod
# id, and the lang value of its root advancement for the localized names).

$script:Root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path

function Get-UpstreamPin {
    # The pinned source is the one NOTICE attributes: "(<url>, branch <branch>, commit <sha>)".
    $notice = Get-Content (Join-Path $script:Root 'NOTICE') -Raw
    $m = [regex]::Match($notice, '\((https://github\.com/[^,\s]+),\s+branch\s+([^,\s]+),\s+commit\s+([0-9a-f]{40})\)')
    if (-not $m.Success) { throw 'NOTICE names no pinned upstream source "(<url>, branch <b>, commit <sha>)"' }
    $url = if ($env:GF_UPSTREAM_URL) { $env:GF_UPSTREAM_URL } else { $m.Groups[1].Value + '.git' }
    return [pscustomobject]@{ Url = $url; Branch = $m.Groups[2].Value; Commit = $m.Groups[3].Value }
}

function Get-UpstreamBase { if ($env:GF_UPSTREAM_BASE) { $env:GF_UPSTREAM_BASE } else { Join-Path $env:LOCALAPPDATA 'gf-upstream' } }
function Get-UpstreamClone { if ($env:GF_UPSTREAM_CLONE) { $env:GF_UPSTREAM_CLONE } else { Join-Path (Get-UpstreamBase) 'clone' } }
function Get-UpstreamHome { if ($env:GF_UPSTREAM_HOME) { $env:GF_UPSTREAM_HOME } else { Join-Path (Get-UpstreamBase) 'view' } }

# The view, fetched and built when missing. Returns its path.
function Get-UpstreamView {
    $view = Get-UpstreamHome
    if (-not (Test-Path (Join-Path $view '.gf-view'))) {
        & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'fetch.ps1') -Quiet | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'the upstream reference is missing: run pwsh tools/upstream/fetch.ps1' }
    }
    return $view
}

function Get-CommonSuffixLength([string]$a, [string]$b) {
    $n = 0
    while ($n -lt $a.Length -and $n -lt $b.Length -and $a[$a.Length - 1 - $n] -eq $b[$b.Length - 1 - $n]) { $n++ }
    return $n
}

function Get-StemPair([string]$old, [string]$new) {
    $s = Get-CommonSuffixLength $old $new
    $old = $old.Substring(0, $old.Length - $s); $new = $new.Substring(0, $new.Length - $s)
    $p = 0
    while ($p -lt $old.Length -and $p -lt $new.Length -and $old[$p] -eq $new[$p]) { $p++ }
    return , @($old.Substring($p).Trim(), $new.Substring($p).Trim())
}

# The renaming rules: Pairs (every path and text) and Locales (locale -> pairs for its lang files).
function Get-UpstreamRebrand([string]$Clone = (Get-UpstreamClone)) {
    $names = Get-Content (Join-Path $PSScriptRoot 'names.json') -Raw | ConvertFrom-Json -AsHashtable
    $props = Join-Path $Clone 'gradle.properties'
    if (-not (Test-Path $props)) { throw "no upstream checkout at $Clone (pwsh tools/upstream/fetch.ps1)" }
    $mod = ([regex]::Match((Get-Content $props -Raw), '(?m)^mod_id=(\S+)')).Groups[1].Value
    if (-not $mod) { throw "$props has no mod_id" }
    $own = $names.namespace
    $stem = Get-StemPair $mod $own
    if (-not $stem[0] -or -not $stem[1]) { throw "cannot derive the renaming from '$mod' and '$own'" }
    $title = { param($s) $s.Substring(0, 1).ToUpperInvariant() + $s.Substring(1) }
    $pairs = [Collections.Generic.List[string[]]]::new()
    $pairs.Add([string[]]@($stem[0], $stem[1]))
    $pairs.Add([string[]]@((& $title $stem[0]), (& $title $stem[1])))
    $pairs.Add([string[]]@($stem[0].ToUpperInvariant(), $stem[1].ToUpperInvariant()))
    $locales = [ordered]@{}
    $langDir = Join-Path $Clone "src/main/resources/assets/$mod/lang"
    foreach ($loc in ($names.names.Keys | Sort-Object)) {
        $file = Join-Path $langDir "$loc.json"
        if (-not (Test-Path $file)) { continue }
        $lang = Get-Content $file -Raw | ConvertFrom-Json -AsHashtable
        $old = $lang["advancement.$mod.root"]
        if (-not $old) { continue }
        $p = Get-StemPair $old $names.names[$loc]
        if ($p[0] -and $p[1] -and $p[0] -ne $p[1]) { $locales[$loc] = [string[][]]@(, [string[]]@($p[0], $p[1])) }
    }
    # the upstream's abbreviation of its English name (its initials) reads as this project's name in lang texts
    $abbrev = $null
    $en = Join-Path $Clone "src/generated/resources/assets/$mod/lang/en_us.json"
    if ((Test-Path $en) -and $names.names['en_us']) {
        $enName = (Get-Content $en -Raw | ConvertFrom-Json -AsHashtable)["advancement.$mod.root"]
        if ($enName) {
            $initials = -join @($enName -split '\s+' | Where-Object { $_ } | ForEach-Object { $_.Substring(0, 1).ToUpperInvariant() })
            if ($initials.Length -ge 2) { $abbrev = [string[]]@($initials, $names.names['en_us']) }
        }
    }
    return [pscustomobject]@{ Mod = $mod; Namespace = $own; Pairs = $pairs; Locales = $locales; Abbrev = $abbrev }
}

function ConvertTo-OwnNames([string]$Text, $Rebrand) {
    foreach ($p in $Rebrand.Pairs) { $Text = $Text.Replace($p[0], $p[1]) }
    return $Text
}

$script:ViewSource = @'
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
namespace GfUpstream {
    public static class View {
        static readonly HashSet<string> BinaryExt = new HashSet<string>(StringComparer.OrdinalIgnoreCase) {
            ".png", ".jpg", ".gif", ".ico", ".jar", ".zip", ".class", ".ogg", ".nbt", ".ttf", ".otf", ".bin" };
        static readonly Regex Link = new Regex(@"\s*https?://(?!minecraft\.wiki/)[^\s""\\~]+", RegexOptions.Compiled);
        static string Apply(string s, string[][] pairs) { foreach (var p in pairs) s = s.Replace(p[0], p[1]); return s; }
        public static int Build(string clone, string dest, string[][] pairs, Dictionary<string, string[][]> locales, string[] abbrev, string stamp) {
            Regex abbr = abbrev == null ? null : new Regex(@"\b" + Regex.Escape(abbrev[0]) + @"\b", RegexOptions.Compiled);
            var utf8 = new UTF8Encoding(false);
            int n = 0;
            var files = Directory.GetFiles(clone, "*", SearchOption.AllDirectories)
                .Where(f => !f.Substring(clone.Length).Replace('\\', '/').StartsWith("/.git/") && !f.Substring(clone.Length).Replace('\\', '/').Equals("/.git"))
                .OrderBy(f => f, StringComparer.Ordinal);
            foreach (var f in files) {
                string rel = f.Substring(clone.Length).TrimStart('\\', '/').Replace('\\', '/');
                string outRel = Apply(rel, pairs);
                string target = Path.Combine(dest, outRel);
                Directory.CreateDirectory(Path.GetDirectoryName(target));
                string ext = Path.GetExtension(f);
                byte[] bytes = File.ReadAllBytes(f);
                if (BinaryExt.Contains(ext) || Array.IndexOf(bytes, (byte)0) >= 0) {
                    if (File.Exists(target)) File.SetAttributes(target, FileAttributes.Normal);
                    File.WriteAllBytes(target, bytes); n++; continue;
                }
                string text = utf8.GetString(bytes);
                text = Apply(text, pairs);
                bool lang = rel.Contains("/lang/") && ext.Equals(".json", StringComparison.OrdinalIgnoreCase);
                if (lang) {
                    string loc = Path.GetFileNameWithoutExtension(f).ToLowerInvariant();
                    string[][] lp;
                    if (locales.TryGetValue(loc, out lp)) text = Apply(text, lp);
                    text = Link.Replace(text, "");
                    if (abbr != null) text = abbr.Replace(text, abbrev[1]);
                }
                if (File.Exists(target)) File.SetAttributes(target, FileAttributes.Normal);
                File.WriteAllText(target, text, utf8);
                n++;
            }
            File.WriteAllText(Path.Combine(dest, ".gf-view"), stamp + "\n", utf8);
            return n;
        }
    }
}
'@

function Get-UpstreamViewStamp([string]$Commit) {
    $sha = [Security.Cryptography.SHA256]::Create()
    $text = (Get-Content (Join-Path $PSScriptRoot 'Upstream.psm1') -Raw) + (Get-Content (Join-Path $PSScriptRoot 'names.json') -Raw)
    $h = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($text))) -replace '-', '').Substring(0, 16)
    return "$Commit $h"
}

# (Re)builds the view from the clone when its stamp (commit + rules) is stale. Returns the view path.
function Update-UpstreamView([string]$Clone = (Get-UpstreamClone), [string]$Commit, [switch]$Force) {
    $view = Get-UpstreamHome
    $stamp = Get-UpstreamViewStamp $Commit
    $stampFile = Join-Path $view '.gf-view'
    if (-not $Force -and (Test-Path $stampFile) -and (Get-Content $stampFile -Raw).Trim() -eq $stamp) { return $view }
    if (-not ('GfUpstream.View' -as [type])) { Add-Type -TypeDefinition $script:ViewSource -Language CSharp }
    $r = Get-UpstreamRebrand $Clone
    if (Test-Path $view) {
        Get-ChildItem -LiteralPath $view -Recurse -File -Force | ForEach-Object { $_.IsReadOnly = $false }
        Remove-Item -LiteralPath $view -Recurse -Force
    }
    New-Item -ItemType Directory -Force $view | Out-Null
    $pairs = $r.Pairs.ToArray()
    $loc = [Collections.Generic.Dictionary[string, string[][]]]::new()
    foreach ($k in $r.Locales.Keys) { $loc[$k] = $r.Locales[$k] }
    $clonePath = (Resolve-Path $Clone).Path.TrimEnd('\', '/')
    [void][GfUpstream.View]::Build($clonePath, $view, $pairs, $loc, $r.Abbrev, $stamp)
    return $view
}

Export-ModuleMember -Function Get-UpstreamPin, Get-UpstreamBase, Get-UpstreamClone, Get-UpstreamHome, Get-UpstreamView,
    Get-UpstreamRebrand, ConvertTo-OwnNames, Update-UpstreamView
