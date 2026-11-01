<#
.SYNOPSIS
  Contract "geo" (unit F2): writes the Bedrock attachable (and its geometry) of an armor piece,
  a Travellers' Gear piece, a trophy worn on the head or another worn / held item.

.DESCRIPTION
  The attachables come from the "armor" and "extracts" sections of tools/models/catalog.json:

    armor          armor bones carry the player's bone names (head, body, rightArm, leftArm,
                   rightLeg, leftLeg), so the geometry follows the wearer; one geometry per slot with
                   the parts HumanoidArmorLayer shows for it (helmet: head + hat, chestplate: body +
                   arms, leggings: inner layer body + legs, boots: legs); vanilla-model armor uses
                   geometry.humanoid.armor.*; render controller controller.render.armor.
    extracts       parts of a TF model drawn through a chain of PoseStack transforms (CustomHeadLayer,
                   the item's display transform, the ISTER, TrophyRenderer, renderTrophy), hung from
                   the wearer's head / rightArm bone (trophy heads, the mystic crown, the knightmetal
                   shield in the right hand).

  Output: assets/gen/geo/<unit>/rp/attachables/<item>.json (+ the geometry it names). Without -Item
  every attachable is written. -Check compares instead of writing. tools/models/convert.ps1 writes
  the same files as part of a full run.

  Exit code 0 = OK, 1 = an error, an unknown item or (with -Check) a difference.

.EXAMPLE
  pwsh tools/models/attachable.ps1 -Item twilightforest:fiery_helmet
  pwsh tools/models/attachable.ps1 -Item twilightforest:naga_trophy -Check
#>
param(
    [string]$Item,
    [string]$Unit,
    [switch]$Check
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib/Models.psm1') -Force

try {
    $r = Invoke-GfModelsGenerator
} catch {
    Write-Host "[geo] $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
foreach ($e in $r.Errors) { Write-Host "[geo] ERROR $e" -ForegroundColor Red }
# keep the attachables (and the geometries they name) of the selection
$index = $r.Files['assets/gen/geo/models/data/gf/models/index.json'] | ConvertFrom-Json
$atts = @($index.attachables | Where-Object { (-not $Item -or $_.item -eq $Item) -and (-not $Unit -or $_.unit -eq $Unit) })
if (-not $atts) { Write-Host "[geo] no attachable for '$Item'$(if ($Unit) { " in unit $Unit" })" -ForegroundColor Red; exit 1 }
$keep = [System.Collections.Generic.List[string]]::new()
$units = [System.Collections.Generic.HashSet[string]]::new()
foreach ($a in $atts) {
    $p = "assets/gen/geo/$($a.unit)/rp/$($a.file)"
    $keep.Add($p); [void]$units.Add($a.unit)
    foreach ($g in $a.geometry) {
        $geo = $index.geometries | Where-Object identifier -eq $g | Select-Object -First 1
        if ($geo -and -not $geo.vanilla) {
            $gp = "assets/gen/geo/$($geo.unit)/rp/$($geo.file)"
            if ($r.Files.Contains($gp)) { $keep.Add($gp); [void]$units.Add($geo.unit) }
        }
    }
    Write-Host "[geo] $($a.item) -> $p ($($a.geometry -join ', '))"
}
$r.Selected = @($keep)
$problems = 0; $files = 0
foreach ($u in $units) {
    $w = Write-GfModelsFiles $r -Unit $u -Check:$Check
    $problems += $w.Problems; $files += $w.Files
}
if ($Check) {
    if ($problems -or $r.Errors.Count) { Write-Host "[geo] -Check FAILED ($problems difference(s))" -ForegroundColor Red; exit 1 }
    Write-Host "[geo] -Check OK ($files files match)" -ForegroundColor Green
    exit 0
}
if ($r.Errors.Count) { exit 1 }
Write-Host "[geo] $files file(s) up to date" -ForegroundColor Green
exit 0
