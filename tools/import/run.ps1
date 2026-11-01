<#
.SYNOPSIS
  Contract "import" (unit F1): imports The Twilight Forest 4.8 into assets/gen/import/<unit>/**.

.DESCRIPTION
  Reads the shared TF checkout (tools/tf/fetch.ps1; fetched when missing) and writes, partitioned by
  docs/spec/owners.tsv into assets/gen/import/<unit>/{bp,rp,data,lang}/** (rule R3, deterministic):

    textures      TF textures (CC BY-NC-SA 4.0) -> rp/textures/twilightforest/**, terrain / item / flipbook
                  atlas fragments, the generated chest-boat textures
    models        Java block models -> Bedrock block geometry (rp/models/blocks/twilightforest/**)
    blockstates   the 523 blocks -> bp/blocks/*.json (frozen states, permutations, materials),
                  rp/blocks.d sounds, docs/spec/blockstates.md
    lang          en_us + translations -> lang/<locale>/<unit>.json and rp/texts.d/<locale>/<unit>.lang
    tags, loot, recipes, advancements, worldgen, datamaps, registries
                  TF data -> data/<namespace>/** (Java format, kept by id); vanilla-type recipes also
                  -> bp/recipes/** with the unlock items of their recipe advancement
    particles     particle JSON -> rp/particles/*.json skeletons + data/gf/particles
    sounds        sounds.json event names (never the .ogg files) -> data/gf/sound_events
    skeleton      items, flat block items, entities (+ placeholder visuals, spawn eggs), id index
    catalog       TFCreativeTabs -> bp/item_catalog.d/import.json + data/gf/catalog
    families      block families from the TF block classes -> data/gf/families/<unit>.json

  Overrides: tools/import/overrides/<unit>.json {"drop": [globs], "patch": {"<path>": [ops]},
  "rename": {"<path>": "<new path>"}} are applied on regeneration (docs/spec/import.md).

  -Kinds and -Unit select what is written (or checked); everything is always computed, so a
  partial run produces exactly the files a full run would. -Check writes nothing and fails when the
  committed tree differs from the import (byte for byte). Never opens .ogg / .nbt files.

  Exit code 0 = OK, 1 = failed (or -Check found differences).

.EXAMPLE
  pwsh tools/import/run.ps1                                   # everything
  pwsh tools/import/run.ps1 -Kinds textures,models,lang -Unit trees
  pwsh tools/import/run.ps1 -Check                            # the committed tree is reproduced byte for byte
#>
param(
    [string[]]$Kinds = @('textures', 'models', 'blockstates', 'lang', 'tags', 'loot', 'recipes', 'advancements', 'worldgen', 'datamaps',
        'registries', 'particles', 'sounds', 'skeleton', 'catalog', 'families'),
    [string]$Unit,
    [switch]$Check,
    [string]$TfDir
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path

if (-not $TfDir) { $TfDir = if ($env:GF_TF_HOME) { $env:GF_TF_HOME } else { Join-Path $env:LOCALAPPDATA 'gf-tf' } }
if (-not (Test-Path (Join-Path $TfDir 'src/main/java/twilightforest'))) {
    Write-Host "[import] TF checkout not found at ${TfDir}: fetching it (tools/tf/fetch.ps1)"
    & pwsh -NoProfile -File (Join-Path $root 'tools/tf/fetch.ps1') -Dest $TfDir -Quiet | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Host '[import] tools/tf/fetch.ps1 failed' -ForegroundColor Red; exit 1 }
}

# The importer is C# (tools/import/src/*.cs), compiled once per source hash and cached.
$sources = @(Get-ChildItem (Join-Path $PSScriptRoot 'src') -Filter '*.cs' | Sort-Object Name)
$sha = [Security.Cryptography.SHA256]::Create()
$all = [Text.StringBuilder]::new()
foreach ($s in $sources) { [void]$all.Append([IO.File]::ReadAllText($s.FullName)) }
[void]$all.Append($PSVersionTable.PSVersion.ToString())
$hash = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($all.ToString()))) -replace '-', '').Substring(0, 16)
$cacheDir = Join-Path ([IO.Path]::GetTempPath()) 'gf-import'
$dll = Join-Path $cacheDir "GfImport-$hash.dll"
$refs = @('System.Text.Json', 'System.Text.Encodings.Web', 'System.Memory', 'System.IO.Compression', 'System.Collections', 'System.Linq',
    'System.Text.RegularExpressions', 'System.Runtime', 'System.Console', 'System.Runtime.Extensions', 'netstandard', 'System.Private.CoreLib',
    'System.Collections.Concurrent', 'System.Diagnostics.Process', 'System.Runtime.InteropServices', 'System.Text.Encoding.Extensions',
    'System.Threading', 'System.IO.FileSystem', 'System.Security.Cryptography', 'System.ComponentModel.Primitives')
if (-not ('GfImport.Program' -as [type])) {
    if (Test-Path $dll) {
        Add-Type -Path $dll
    } else {
        New-Item -ItemType Directory -Force $cacheDir | Out-Null
        $tmp = "$dll.$PID.tmp.dll"
        try {
            Add-Type -Path $sources.FullName -ReferencedAssemblies $refs -OutputAssembly $tmp -OutputType Library -IgnoreWarnings
            Move-Item -Force $tmp $dll
        } catch {
            Write-Host "[import] compiling tools/import/src failed: $_" -ForegroundColor Red
            exit 1
        }
        Add-Type -Path $dll
    }
}

$kindList = @($Kinds | ForEach-Object { $_ -split ',' } | Where-Object { $_ } | ForEach-Object { $_.Trim() })
$logger = [Action[string]] { param($m) Write-Host "[import] $m" }
try {
    $code = [GfImport.Program]::Run($root, (Resolve-Path $TfDir).Path, [string[]]$kindList, $(if ($Unit) { $Unit } else { $null }), [bool]$Check, $logger)
} catch {
    $e = $_.Exception
    while ($e.InnerException) { $e = $e.InnerException }
    Write-Host "[import] failed: $($e.Message)" -ForegroundColor Red
    Write-Host $e.StackTrace
    exit 1
}
if ($code -eq 0) { Write-Host $(if ($Check) { '[import] -Check OK: the committed tree matches the import' } else { '[import] done' }) -ForegroundColor Green }
exit $code
