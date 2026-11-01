<#
.SYNOPSIS
  Contract "template" (owner F7): builds GloamingForest templates from the DSL scripts and checks
  every template against TF's template definitions.

.DESCRIPTION
  Templates are original designs (rule R19): TF's .nbt files are never read (they are All Rights
  Reserved and not in the TF checkout). A unit writes tools/structures/<unit>/**/*.gfs.ps1 scripts
  with the DSL of tools/structures/lib/Gft.psm1 (the format is documented there); this script runs
  them and writes assets/structures/<unit>/<path>.gft.json (or the path a template names with -Out).

  -Check builds every script in memory and fails when
    - a built template differs from the committed .gft.json (the tree must be reproducible);
    - a .gft.json under assets/structures or assets/test/structures is malformed (format, size,
      palette, run-length blocks, markers, connectors, entities inside the template);
    - two templates share an id;
    - a template references a twilightforest: block, entity or loot table that docs/spec/owners.tsv
      does not know;
    - a template standing in for a TF template (its id has a twilight/template_definition) has
      jigsaw connectors to pools no definition fills, connector targets no template of the pool
      names, or data markers its definition's template_marker_handler_list does not handle.
  The template definitions (205 in TF 4.8) come from the imported data
  (assets/gen/import/*/data/*/twilight/template_definition) or, before the import ran, from the
  shared TF checkout (tools/tf/fetch.ps1). The definitions without a template yet are listed;
  -Strict turns them into failures (W2).

.EXAMPLE
  pwsh tools/structures/build.ps1 -Unit questcamps
  pwsh tools/structures/build.ps1 -Check
  pwsh tools/structures/build.ps1 -Check -Strict
#>
param(
    [string]$Unit,
    [switch]$Check,
    [switch]$Strict,
    # exercises the DSL and the validator on a built-in example (no files written)
    [switch]$SelfTest
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
Import-Module (Join-Path $PSScriptRoot 'lib/Gft.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'lib/Validate.psm1') -Force

$failures = [System.Collections.Generic.List[string]]::new()
$notes    = [System.Collections.Generic.List[string]]::new()

if ($SelfTest) {
    $t = New-Gft -Id 'gf_test:dsl/example' -Size 5, 3, 4
    Set-GftBox $t 0 0 0 4 0 3 'minecraft:stone_bricks'
    Set-GftLayer $t -Y 1 -Key @{ '#' = 'minecraft:cobblestone'; '.' = 'minecraft:air'; 's' = @{ Name = 'minecraft:oak_stairs'; Properties = @{ facing = 'east'; half = 'bottom' } } } -Rows @(
        '#####',
        '#..s#',
        '#...#',
        '#####')
    Set-GftBox $t 0 2 0 4 2 3 'minecraft:oak_planks' -Hollow
    Add-GftMarker $t 2 1 2 'loot' @{ table = 'twilightforest:hill_1' }
    Add-GftDataMarker $t 1 1 1 'seat@north'
    Add-GftJigsaw $t 2 0 0 -Front north -Pool 'minecraft:empty' -Name 'gf_test:door'
    Add-GftEntity $t 2.5 1 2.5 'minecraft:armor_stand'
    Add-GftPainting $t 1 1 2 -Facing south -Variant 'minecraft:baroque'
    $text = ConvertTo-GftJson $t
    $doc  = $text | ConvertFrom-Json -AsHashtable
    $errs = Test-GftDocument $doc
    if ($errs.Count) { $errs | ForEach-Object { Write-Host "[template] selftest: $_" -ForegroundColor Red }; exit 1 }
    # a painting at Java's entity position (its center) without block_pos is refused, as are unknown variants
    $bad = $text | ConvertFrom-Json -AsHashtable
    $bad.entities = @(@{ pos = @(2.0, 2.0, 2.96875); id = 'minecraft:painting'; data = @{ facing = 'north'; variant = 'minecraft:baroque' } },
        @{ pos = @(1, 1, 1); id = 'minecraft:painting'; data = @{ variant = 'minecraft:no_such_painting' } })
    $badErrors = Test-GftDocument $bad
    if ($badErrors.Count -ne 2) { Write-Host "[template] selftest: expected 2 painting errors, got $($badErrors.Count): $($badErrors -join '; ')" -ForegroundColor Red; exit 1 }
    # entity data: Rotation and nbt are applied; a field the kit would lose (Java NBT names) is refused
    $ent = $text | ConvertFrom-Json -AsHashtable
    $ent.entities = @(@{ pos = @(2.5, 1, 2.5); id = 'minecraft:armor_stand'; data = [ordered]@{ Rotation = @(30.0, 0.0); nbt = '{CustomName:"stand"}' } })
    $entErrors = Test-GftDocument $ent
    if ($entErrors.Count) { Write-Host "[template] selftest: an entity with Rotation and nbt is refused: $($entErrors -join '; ')" -ForegroundColor Red; exit 1 }
    $ent.entities[0].data['ArmorItems'] = @()
    $ent.entities[0].data['Rotation']   = @(30.0)
    $entErrors = Test-GftDocument $ent
    if ($entErrors.Count -ne 2) { Write-Host "[template] selftest: expected 2 entity data errors, got $($entErrors.Count): $($entErrors -join '; ')" -ForegroundColor Red; exit 1 }
    if ($doc.palette.Count -ne 6) { Write-Host "[template] selftest: expected 6 palette entries (void, bricks, cobble, air, stairs, planks), got $($doc.palette.Count)" -ForegroundColor Red; exit 1 }
    if ((ConvertTo-GftJson $t) -ne $text) { Write-Host '[template] selftest: the output is not stable' -ForegroundColor Red; exit 1 }
    $total = ($doc.blocks | ForEach-Object { $_[1] } | Measure-Object -Sum).Sum
    if ($total -ne 60) { Write-Host "[template] selftest: run-length blocks cover $total cells, expected 60" -ForegroundColor Red; exit 1 }
    Write-Host '[template] selftest OK' -ForegroundColor Green
    exit 0
}

# ---- build the DSL scripts ------------------------------------------------------------------------------
$scriptsRoot = Join-Path $PSScriptRoot ''
$scripts = Get-ChildItem $scriptsRoot -Recurse -File -Filter '*.gfs.ps1' | Where-Object {
    $rel = [IO.Path]::GetRelativePath($scriptsRoot, $_.FullName) -replace '\\', '/'
    $rel -notlike 'lib/*' -and (-not $Unit -or $rel -like "$Unit/*")
}
$built = @{}
foreach ($s in $scripts) {
    $rel  = [IO.Path]::GetRelativePath($scriptsRoot, $s.FullName) -replace '\\', '/'
    $unit = $rel.Split('/')[0]
    Clear-GftSaved
    try {
        & $s.FullName | Out-Null
    } catch {
        $failures.Add("${rel}: $($_.Exception.Message)")
        continue
    }
    $saved = Get-GftSaved
    if ($saved.Count -eq 0) { $failures.Add("${rel}: the script saved no template (Save-Gft)") }
    foreach ($t in $saved) {
        $out = if ($t.Out) { $t.Out } elseif ($saved.Count -eq 1) { $rel -replace '\.gfs\.ps1$', '' } else { "$unit/" + ($t.Id -replace '^[^:]+:', '') }
        $path = Join-Path $root "assets/structures/$out.gft.json"
        $text = ConvertTo-GftJson $t
        if ($built.ContainsKey($path)) { $failures.Add("$rel and another script both write $out.gft.json") }
        $built[$path] = $text
        if (-not $Check) {
            New-Item -ItemType Directory -Force (Split-Path $path) | Out-Null
            [IO.File]::WriteAllText($path, $text, [Text.UTF8Encoding]::new($false))
        } elseif (-not (Test-Path $path)) {
            $failures.Add("assets/structures/$out.gft.json is missing (run tools/structures/build.ps1 -Unit $unit)")
        } elseif ((Get-Content $path -Raw) -ne $text) {
            $failures.Add("assets/structures/$out.gft.json differs from what $rel builds (run tools/structures/build.ps1 -Unit $unit)")
        }
    }
}
if (-not $Check) {
    Write-Host "[template] built $($built.Count) template(s) from $($scripts.Count) script(s)"
}

# ---- validate every template ------------------------------------------------------------------------------
$templateFiles = @()
foreach ($dir in 'assets/structures', 'assets/test/structures') {
    $d = Join-Path $root $dir
    if (Test-Path $d) { $templateFiles += Get-ChildItem $d -Recurse -File -Filter '*.gft.json' }
}
$owners      = Read-GftOwners (Join-Path $root 'docs/spec/owners.tsv')
$definitionsRead = Read-GftTemplateDefinitions $root
$definitions     = $definitionsRead.Map
$handlers        = (Read-GftMarkerHandlerLists $root).Map
$templates   = @{}
foreach ($f in $templateFiles) {
    $rel = [IO.Path]::GetRelativePath($root, $f.FullName) -replace '\\', '/'
    try {
        $doc = Get-Content $f.FullName -Raw | ConvertFrom-Json -AsHashtable
    } catch {
        $failures.Add("${rel}: not JSON ($($_.Exception.Message))")
        continue
    }
    foreach ($e in (Test-GftDocument $doc)) { $failures.Add("${rel}: $e") }
    foreach ($e in (Test-GftReferences $doc $owners)) { $failures.Add("${rel}: $e") }
    $id = $doc.id
    if ($id) {
        if ($templates.ContainsKey($id)) { $failures.Add("${rel}: id $id is also used by $($templates[$id].rel)") }
        else { $templates[$id] = @{ rel = $rel; doc = $doc } }
    }
}
foreach ($e in (Test-GftAgainstDefinitions $templates $definitions $handlers)) { $failures.Add($e) }

$covered = @($definitions.Keys | Where-Object { $templates.ContainsKey($_) })
$missing = @($definitions.Keys | Where-Object { -not $templates.ContainsKey($_) } | Sort-Object)
$notes.Add("$($templateFiles.Count) template file(s), $($definitions.Count) TF template definitions ($($definitionsRead.Source)), $($covered.Count) designed, $($missing.Count) still to design")
if ($Strict) { foreach ($m in $missing) { $failures.Add("no template for TF template definition $m") } }

foreach ($n in $notes) { Write-Host "[template] $n" }
if ($failures.Count) {
    foreach ($f in $failures) { Write-Host "[template] $f" -ForegroundColor Red }
    Write-Host "[template] FAILED: $($failures.Count) problem(s)" -ForegroundColor Red
    exit 1
}
Write-Host '[template] OK' -ForegroundColor Green
exit 0
