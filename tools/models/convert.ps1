<#
.SYNOPSIS
  Contract "geo" (unit F2): converts The Twilight Forest's Java entity models to Bedrock geometry.

.DESCRIPTION
  Reads every LayerDefinition TF registers (ClientRegistrationEvents.registerLayerDefinitions, LGPL
  code) from the shared checkout (tools/tf/fetch.ps1, rule R24), runs the builder code
  (PartDefinition.addOrReplaceChild / CubeListBuilder / PartPose, loops, helper methods, seeded
  randoms) in a small Java interpreter (tools/models/lib) and writes, partitioned by the owning
  unit of tools/models/catalog.json (rule R3):

    assets/gen/geo/<unit>/rp/models/entity/<name>.geo.json   geometry.twilightforest.<name>
    assets/gen/geo/<unit>/rp/animations/<name>.base.json     animation.twilightforest.<name>.base
                                                             (setupAnim statements translated to Molang)
    assets/gen/geo/<unit>/bones/<name>.json                  Java part -> bone, pivots, mirror, and the
                                                             setupAnim statements left to translate by hand
    assets/gen/geo/<unit>/rp/attachables/<item>.json         armor, Travellers' Gear, trophy heads, ...
                                                             (also tools/models/attachable.ps1)
    assets/gen/geo/models/data/gf/models/index.json          every output (selftests, e2e preview)
    assets/gen/geo/models/rp/render_controllers/...          shared render controllers of attachables

  Scope: every partition by default (stale files and partitions are removed); -Unit <unit> only
  that unit's partition (a W1 unit regenerates its own); -Java / -Item only the files of one model
  class or attachable item; -Out <dir> writes below another root. Only this tool writes
  assets/gen/geo (rule R3; F2, the tool unit of the layer, commits every partition). -Check
  regenerates in memory and fails when a committed file of the scope differs, is missing or is
  stale. The output is deterministic. Format and coordinate conversion: docs/spec/models.md.

  Exit code 0 = OK, 1 = an error or (with -Check) a difference.

.EXAMPLE
  pwsh tools/models/convert.ps1                                # every partition
  pwsh tools/models/convert.ps1 -Unit hydra                    # unit U24 regenerates its partition
  pwsh tools/models/convert.ps1 -Java HydraModel -Unit hydra
  pwsh tools/models/convert.ps1 -Check                         # CI / review
  pwsh tools/models/convert.ps1 -Out build/geo                 # outside the repository
#>
param(
    [string]$Java,
    [string]$Unit,
    [string]$Item,
    [string]$Out,
    [switch]$Check,
    [switch]$Quiet
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib/Models.psm1') -Force

try {
    $r = Invoke-GfModelsGenerator -Java $Java -Item $Item
} catch {
    Write-Host "[geo] $($_.Exception.Message)" -ForegroundColor Red
    if ($_.Exception.InnerException) { Write-Host "[geo]   $($_.Exception.InnerException.Message)" -ForegroundColor Red }
    exit 1
}
if (-not $Quiet) { $r.Log | ForEach-Object { Write-Host "[geo] $_" } }
foreach ($e in $r.Errors) { Write-Host "[geo] ERROR $e" -ForegroundColor Red }
if ($null -ne $r.Selected -and $r.Selected.Count -eq 0) { Write-Host '[geo] nothing matched -Java / -Item' -ForegroundColor Yellow; exit 1 }
if ($Out) { New-Item -ItemType Directory -Force $Out | Out-Null; $Out = (Resolve-Path $Out).Path }
$w = Write-GfModelsFiles $r -Unit $Unit -Check:$Check -Out $Out
$scope = $w.Partitions -join ', '
if ($Check) {
    if ($w.Problems -or $r.Errors.Count) { Write-Host "[geo] -Check FAILED: $($w.Problems) difference(s), $($r.Errors.Count) error(s) in $scope (regenerate: pwsh tools/models/convert.ps1 [-Unit <unit>])" -ForegroundColor Red; exit 1 }
    Write-Host "[geo] -Check OK ($($w.Files) files match in $scope)" -ForegroundColor Green
    exit 0
}
if ($r.Errors.Count -or $w.Problems) { Write-Host "[geo] $($w.Files) file(s) written with $($r.Errors.Count + $w.Problems) error(s)" -ForegroundColor Red; exit 1 }
Write-Host "[geo] $($w.Files) file(s) up to date in $scope" -ForegroundColor Green
exit 0
