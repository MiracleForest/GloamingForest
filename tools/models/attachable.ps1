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
    extracts       parts of a TF model drawn through a chain of PoseStack transforms (CustomHeadLayer or
                   ItemInHandLayer, the display transform of TF's item model, the ISTER, TrophyRenderer,
                   renderTrophy), hung from the wearer's head / rightArm / leftArm bone: one geometry per
                   slot the item is drawn in (trophies and the mystic crown on the head and in either
                   hand, the knightmetal shield in either hand), each drawn only in its slot.

  Output: assets/gen/geo/<unit>/rp/attachables/<item>.json and the files written for the item (its
  geometries, bone maps and animations). Without -Item every attachable is written; -Unit keeps to
  that unit's partition. -Check compares instead of writing. tools/models/convert.ps1 writes the
  same files as part of a full run.

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
    # every file written for the item (its geometries, bone maps, animations and the attachable)
    $r = Invoke-GfModelsGenerator -Item $(if ($Item) { $Item } else { '*' })
} catch {
    Write-Host "[geo] $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
foreach ($e in $r.Errors) { Write-Host "[geo] ERROR $e" -ForegroundColor Red }
$index = $r.Files['assets/gen/geo/models/data/gf/models/index.json'] | ConvertFrom-Json
$atts = @($index.attachables | Where-Object { (-not $Item -or $_.item -eq $Item) -and (-not $Unit -or $_.unit -eq $Unit) })
if (-not $atts) { Write-Host "[geo] no attachable for '$Item'$(if ($Unit) { " in unit $Unit" })" -ForegroundColor Red; exit 1 }
foreach ($a in $atts) { Write-Host "[geo] $($a.item) -> assets/gen/geo/$($a.unit)/rp/$($a.file) ($($a.slots -join ', '): $($a.geometry -join ', '))" }
if ($Unit) { $r.Selected = @($r.Selected | Where-Object { $_ -like "assets/gen/geo/$Unit/*" }) }
# the selection limits the scope; -Unit keeps it to that unit's partition (no shared files)
$w = Write-GfModelsFiles $r -Unit $Unit -Check:$Check
if ($Check) {
    if ($w.Problems -or $r.Errors.Count) { Write-Host "[geo] -Check FAILED ($($w.Problems) difference(s))" -ForegroundColor Red; exit 1 }
    Write-Host "[geo] -Check OK ($($w.Files) files match)" -ForegroundColor Green
    exit 0
}
if ($r.Errors.Count -or $w.Problems) { exit 1 }
Write-Host "[geo] $($w.Files) file(s) up to date" -ForegroundColor Green
exit 0