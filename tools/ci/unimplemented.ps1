<#
.SYNOPSIS
  The registered-but-unimplemented report (rule R17, W2): upstream blocks, items and entities that need
  behaviour of their own and have none in GloamingForest beyond the imported skeleton JSON.

.DESCRIPTION
  How it decides (static: the game's registries do not record which code handles an id, so the
  report reads upstream and this repository instead):

  1. Registered ids: upstream's own registrations at 9e40a757 from the shared checkout (rule R24,
     pwsh tools/upstream/fetch.ps1): init/GloamingBlocks.java and init/GloamingItems.java (`register*("<path>", () ->
     new <Class>(...))`) and init/GloamingEntities.java (`make*(GloamingEntityNames.<NAME>, <Class>::new, ...)`
     with the names of entity/GloamingEntityNames.java). Commented-out registrations do not count.
  2. Needs behaviour: the id's class is one of upstream's own (a src/main/java/gloamingforest/**/<Class>.java
     exists; for a nested class such as JarItem.MasonJarItem the outer one). Ids built from vanilla
     classes (Block, StairBlock, Item, ArmorItem, ...) are complete with the imported skeleton plus
     the block families (F11) and are not listed. Every upstream entity class is upstream's own.
  3. Implemented: the id has any of
       - a string literal "gloamingforest:<path>" or "<path>" in the mod's code (src/**/*.cpp|h,
         without selftests: src/selftest/**, *Test*.cpp, *Tests.cpp, *Demo*.cpp);
       - an entry in the block family data (assets/data/families/*.json "id") or the container
         declarations (assets/data/containers/*.json);
       - a hand-made Bedrock definition (assets/pack/bp/**: "description.identifier");
       - membership of an upstream tag (imported data/gloamingforest/tags/{block,item,entity_type}/**, plus
         assets/tags fragments) that the mod's code names as "gloamingforest:<tag>" (R6 matchers).
  4. Accounted for: listed in docs/spec/not-ported.md (sections before the appendices name it as
     gloamingforest:<path>) or reviewed in tools/ci/unimplemented-reviewed.json
     ({"gloamingforest:<path>": "why its skeleton is complete"}).

  Everything else is reported, per owner (docs/spec/owners.tsv). It is a heuristic: a mention is
  not proof of parity (the unit audits check that), and a reviewed entry needs a reason.

  Exit code 0 (or 1 with -Fail when the report is not empty).

.EXAMPLE
  pwsh tools/ci/unimplemented.ps1                           # markdown report on stdout
  pwsh tools/ci/unimplemented.ps1 -Owner U5 -Fail           # one unit; exit 1 when it has entries
  pwsh tools/ci/unimplemented.ps1 -Json out.json            # machine-readable
#>
param(
    [ValidateSet('block', 'item', 'entity')][string[]]$Kind = @('block', 'item', 'entity'),
    [string[]]$Owner = @(),
    [string]$Json,
    [string]$Markdown,
    [switch]$Fail,
    [switch]$ShowImplemented
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$upsrc = if ($env:GF_UPSTREAM_HOME) { $env:GF_UPSTREAM_HOME } else { Join-Path $env:LOCALAPPDATA 'gf-upstream/view' }
$java = Join-Path $upsrc 'src/main/java/gloamingforest'
if (-not (Test-Path $java)) { throw "upstream checkout not found at $upsrc (pwsh tools/upstream/fetch.ps1)" }

# ---- 1-2: upstream registrations and their classes ------------------------------------------------------
$gloamingClasses = [Collections.Generic.HashSet[string]]::new()
Get-ChildItem $java -Recurse -Filter *.java -File | ForEach-Object { [void]$gloamingClasses.Add($_.BaseName) }

function Get-JavaLines([string]$file) {
    # drop // comments and /* */ blocks: commented-out registrations do not count
    $text = Get-Content (Join-Path $java $file) -Raw
    $text = [regex]::Replace($text, '(?s)/\*.*?\*/', '')
    return @($text -split "`n" | ForEach-Object { $_ -replace '^\s*//.*$', '' })
}

$registered = [Collections.Generic.List[object]]::new()
function Add-Registered([string]$kind, [string]$path, [string]$class) {
    $outer = ($class -split '\.')[0]
    $registered.Add([pscustomobject]@{ Kind = $kind; Id = "gloamingforest:$path"; Path = $path; Class = $class; Custom = $gloamingClasses.Contains($outer) })
}
# `DeferredBlock<T> NAME = <register or helper>("<path>", ... new <Class>(...))`: the constructed class,
# else (a helper such as ominousCandle("<path>", ...)) the declared type T
$declRx = '(?s)Deferred(?:Block|Item)<([\w.]+)>\s+\w+\s*=\s*[\w.]+\(\s*"([a-z0-9_/]+)"(.*?);'
function Add-Declared([string]$kind, [string]$file) {
    foreach ($m in [regex]::Matches((Get-JavaLines $file) -join "`n", $declRx)) {
        $new = [regex]::Match($m.Groups[3].Value, '\bnew\s+([\w.]+)\s*[(<]')
        Add-Registered $kind $m.Groups[2].Value $(if ($new.Success) { $new.Groups[1].Value } else { $m.Groups[1].Value })
    }
}
if ($Kind -contains 'block') { Add-Declared block 'init/GloamingBlocks.java' }
if ($Kind -contains 'item') { Add-Declared item 'init/GloamingItems.java' }
if ($Kind -contains 'entity') {
    $names = @{}
    foreach ($m in [regex]::Matches((Get-JavaLines 'entity/GloamingEntityNames.java') -join "`n", '(\w+)\s*=\s*prefix\("([a-z0-9_/]+)"\)')) { $names[$m.Groups[1].Value] = $m.Groups[2].Value }
    # make(GloamingEntityNames.X, Class::new, ...), build*(GloamingEntityNames.X, makeCastedBuilder(Class.class, Class::new, ...)) ...
    foreach ($m in [regex]::Matches((Get-JavaLines 'init/GloamingEntities.java') -join "`n", '(?:make|build)\w*\(\s*GloamingEntityNames\.(\w+)\s*,[^;]*?([\w.]+)::new')) {
        $path = $names[$m.Groups[1].Value]
        if ($path) { Add-Registered entity $path $m.Groups[2].Value }
        else { Write-Warning "GloamingEntityNames.$($m.Groups[1].Value) not found" }
    }
}

# ---- 3: evidence of an implementation -------------------------------------------------------------
$literals = [Collections.Generic.HashSet[string]]::new()
$srcFiles = Get-ChildItem (Join-Path $root 'src') -Recurse -File -Include *.cpp, *.h |
    Where-Object { $_.FullName -notmatch '[\\/]src[\\/]selftest[\\/]' -and $_.Name -notmatch 'Tests?\.cpp$|Test\w*\.cpp$|Demo\w*\.cpp$' }
# A selftest body (GF_REGISTER_SELFTEST(..., [](...) { ... })) in a file with other code is cut out:
# the scan skips string and char literals and // comments while it matches the braces.
function Remove-SelftestBodies([string]$text) {
    $sb = [Text.StringBuilder]::new()
    $pos = 0
    while (($at = $text.IndexOf('GF_REGISTER_SELFTEST(', $pos, [StringComparison]::Ordinal)) -ge 0) {
        [void]$sb.Append($text, $pos, $at - $pos)
        $i = $text.IndexOf('{', $at); if ($i -lt 0) { $pos = $text.Length; break }
        # the test's name (before the body) stays out too; skip to the body's matching brace
        $depth = 0
        for (; $i -lt $text.Length; $i++) {
            $c = $text[$i]
            if ($c -eq '"' -or $c -eq "'") {
                for ($i++; $i -lt $text.Length -and $text[$i] -ne $c; $i++) { if ($text[$i] -eq '\') { $i++ } }
            } elseif ($c -eq '/' -and $i + 1 -lt $text.Length -and $text[$i + 1] -eq '/') {
                $i = $text.IndexOf("`n", $i); if ($i -lt 0) { $i = $text.Length }
            } elseif ($c -eq '{') { $depth++ }
            elseif ($c -eq '}') { $depth--; if ($depth -eq 0) { break } }
        }
        $pos = [Math]::Min($i + 1, $text.Length)
    }
    if ($pos -lt $text.Length) { [void]$sb.Append($text, $pos, $text.Length - $pos) }
    return $sb.ToString()
}
foreach ($f in $srcFiles) {
    $text = [IO.File]::ReadAllText($f.FullName)
    if ($text.Contains('GF_REGISTER_SELFTEST(')) { $text = Remove-SelftestBodies $text }
    foreach ($m in [regex]::Matches($text, '"((?:[^"\\\r\n]|\\.)*)"')) { [void]$literals.Add($m.Groups[1].Value) }
}
$evidence = @{}   # id -> first reason
function Add-Evidence([string]$id, [string]$why) { if (-not $evidence.ContainsKey($id)) { $evidence[$id] = $why } }
foreach ($r in $registered) {
    if ($literals.Contains($r.Id)) { Add-Evidence $r.Id 'code (id)' }
    # a bare path counts only when it cannot be an ordinary word ("naga", "fiery"): ids built at
    # runtime ("gloamingforest:" + "ominous_red_candle") are compound paths
    elseif ($r.Path -match '[_/]' -and $literals.Contains($r.Path)) { Add-Evidence $r.Id 'code (path)' }
}
function Get-JsonStrings($node) {
    if ($node -is [string]) { return , $node }
    $out = @()
    if ($node -is [System.Collections.IEnumerable] -and $node -isnot [string] -and $node -isnot [pscustomobject]) { foreach ($x in $node) { $out += Get-JsonStrings $x } }
    elseif ($node -is [pscustomobject]) { foreach ($p in $node.PSObject.Properties) { $out += Get-JsonStrings $p.Value } }
    return $out
}
foreach ($dir in 'families', 'containers') {
    foreach ($f in Get-ChildItem (Join-Path $root "assets/data/$dir") -Filter *.json -File -ErrorAction SilentlyContinue) {
        # an entry's own id ("id" / "block"), not the ids its params name (a potted plant's plant)
        $doc = Get-Content $f.FullName -Raw | ConvertFrom-Json
        $entries = if ($doc -is [array]) { $doc } elseif ($doc.entries) { $doc.entries } elseif ($doc.containers) { $doc.containers } elseif ($doc.blocks) { $doc.blocks } else { @($doc) }
        foreach ($e in @($entries)) {
            if ($e -isnot [pscustomobject]) { continue }
            foreach ($k in 'id', 'block') { $s = $e.$k; if ($s -is [string] -and $s -like 'gloamingforest:*') { Add-Evidence $s "data/$dir/$($f.Name)" } }
        }
    }
}
foreach ($f in Get-ChildItem (Join-Path $root 'assets/pack/bp') -Recurse -Filter *.json -File -ErrorAction SilentlyContinue) {
    $doc = try { Get-Content $f.FullName -Raw | ConvertFrom-Json } catch { $null }
    if (-not $doc -or $doc -isnot [pscustomobject]) { continue }
    foreach ($p in $doc.PSObject.Properties) {
        $id = if ($p.Value -is [pscustomobject] -and $p.Value.description) { $p.Value.description.identifier }
        if ($id -is [string] -and $id -like 'gloamingforest:*') { Add-Evidence $id "pack/bp/$([IO.Path]::GetRelativePath((Join-Path $root 'assets/pack/bp'), $f.FullName).Replace('\', '/'))" }
    }
}
# tags the code names: their members (one level of nested tags at a time, until nothing changes)
$tagMembers = @{}   # "<registry>|gloamingforest:<tag>" -> values
$tagFiles = @(Get-ChildItem (Join-Path $root 'assets/gen/import') -Recurse -File -Filter *.json -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '[\\/]data[\\/]gloamingforest[\\/]tags[\\/](block|item|entity_type)[\\/]' })
foreach ($f in $tagFiles) {
    $m = [regex]::Match($f.FullName.Replace('\', '/'), '/data/gloamingforest/tags/(block|item|entity_type)/(.+)\.json$')
    $key = "$($m.Groups[1].Value)|gloamingforest:$($m.Groups[2].Value)"
    $doc = Get-Content $f.FullName -Raw | ConvertFrom-Json
    $tagMembers[$key] = @($tagMembers[$key]) + @(Get-JsonStrings $doc.values | Where-Object { $_ }) | Where-Object { $_ }
}
foreach ($f in Get-ChildItem (Join-Path $root 'assets/tags') -Recurse -File -Filter *.json -ErrorAction SilentlyContinue) {
    $m = [regex]::Match($f.FullName.Replace('\', '/'), '/assets/tags/(block|item|entity_type)/gloamingforest/(.+)\.d/[^/]+\.json$')
    if (-not $m.Success) { continue }
    $key = "$($m.Groups[1].Value)|gloamingforest:$($m.Groups[2].Value)"
    $doc = Get-Content $f.FullName -Raw | ConvertFrom-Json
    $tagMembers[$key] = @($tagMembers[$key]) + @(Get-JsonStrings $doc.values) | Where-Object { $_ }
}
foreach ($key in $tagMembers.Keys) {
    $reg, $tag = $key -split '\|', 2
    if (-not $literals.Contains($tag) -and -not $literals.Contains("#$tag")) { continue }
    $seen = @{}; $queue = [Collections.Generic.Queue[string]]::new(); $queue.Enqueue($key)
    while ($queue.Count) {
        $k = $queue.Dequeue(); if ($seen[$k]) { continue }; $seen[$k] = $true
        foreach ($v in @($tagMembers[$k] | Where-Object { $_ -is [string] })) {
            if ($v.StartsWith('#')) { $queue.Enqueue("$reg|$($v.Substring(1))") }
            elseif ($v -like 'gloamingforest:*') { Add-Evidence $v "tag $tag" }
        }
    }
}

# ---- 4: accounted for ---------------------------------------------------------------------------------
$accounted = @{}
$np = Join-Path $root 'docs/spec/not-ported.md'
if (Test-Path $np) {
    $text = (Get-Content $np -Raw) -split '(?m)^## Appendix', 2 | Select-Object -First 1
    foreach ($m in [regex]::Matches($text, 'gloamingforest:([a-z0-9_/]+)')) { $accounted["gloamingforest:$($m.Groups[1].Value)"] = 'docs/spec/not-ported.md' }
}
$reviewedFile = Join-Path $PSScriptRoot 'unimplemented-reviewed.json'
if (Test-Path $reviewedFile) {
    $rev = Get-Content $reviewedFile -Raw | ConvertFrom-Json
    foreach ($p in $rev.PSObject.Properties) {
        if ($p.Name -like 'gloamingforest:*') {
            if (-not ($p.Value -is [string] -and $p.Value.Trim())) { throw "unimplemented-reviewed.json: $($p.Name) needs a reason" }
            $accounted[$p.Name] = "reviewed: $($p.Value)"
        }
    }
}

# ---- report ---------------------------------------------------------------------------------------------
$owners = @{}
foreach ($line in Get-Content (Join-Path $root 'docs/spec/owners.tsv')) {
    $c = $line -split "`t"
    if ($c.Count -ge 3 -and $c[0] -in 'block', 'item', 'entity', 'block_item', 'spawn_egg') { if (-not $owners.ContainsKey("$($c[0])|$($c[1])")) { $owners["$($c[0])|$($c[1])"] = $c[2] } }
}
$rows = foreach ($r in $registered) {
    $own = $owners["$($r.Kind)|$($r.Id)"]; if (-not $own) { $own = $owners["block|$($r.Id)"] }; if (-not $own) { $own = '?' }
    $status = if (-not $r.Custom) { 'skeleton' } elseif ($evidence.ContainsKey($r.Id)) { 'implemented' } elseif ($accounted.ContainsKey($r.Id)) { 'accounted' } else { 'unimplemented' }
    $why = switch ($status) { 'implemented' { $evidence[$r.Id] } 'accounted' { $accounted[$r.Id] } default { '' } }
    [pscustomobject]@{ Kind = $r.Kind; Id = $r.Id; Class = $r.Class; Owner = $own; Status = $status; Evidence = $why }
}
if ($Owner) { $rows = @($rows | Where-Object { $Owner -contains $_.Owner }) }
$open = @($rows | Where-Object Status -eq 'unimplemented')

$md = [Collections.Generic.List[string]]::new()
$md.Add('# Registered but unimplemented (R17)')
$md.Add('')
$counts = $rows | Group-Object Status | ForEach-Object { "$($_.Count) $($_.Name)" }
$md.Add("upstream registrations checked: $(@($rows).Count) ($($Kind -join ', ')): $($counts -join ', '). How it decides: tools/ci/unimplemented.ps1.")
$md.Add('')
if ($open.Count -eq 0) { $md.Add('**The report is empty.**') }
foreach ($g in $open | Group-Object Owner | Sort-Object Name) {
    $md.Add("## $($g.Name) ($($g.Count))")
    $md.Add('')
    $md.Add('| Kind | Id | upstream class |')
    $md.Add('|---|---|---|')
    foreach ($r in $g.Group | Sort-Object Kind, Id) { $md.Add("| $($r.Kind) | ``$($r.Id)`` | $($r.Class) |") }
    $md.Add('')
}
if ($ShowImplemented) {
    $md.Add('## Implemented / accounted for')
    $md.Add('')
    $md.Add('| Kind | Id | Owner | Status | Evidence |')
    $md.Add('|---|---|---|---|---|')
    foreach ($r in $rows | Where-Object { $_.Status -in 'implemented', 'accounted' } | Sort-Object Owner, Kind, Id) { $md.Add("| $($r.Kind) | ``$($r.Id)`` | $($r.Owner) | $($r.Status) | $($r.Evidence) |") }
}
$text = $md -join "`n"
if ($Markdown) { Set-Content -Path $Markdown -Value $text -Encoding UTF8 } else { $text }
if ($Json) { $rows | ConvertTo-Json -Depth 3 | Set-Content -Path $Json -Encoding UTF8 }
Write-Host "[unimplemented] $($open.Count) unimplemented of $(@($rows | Where-Object Status -ne 'skeleton').Count) ids that need behaviour" -ForegroundColor $(if ($open.Count) { 'Yellow' } else { 'Green' })
if ($Fail -and $open.Count) { exit 1 }
exit 0
