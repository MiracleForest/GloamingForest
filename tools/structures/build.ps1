<#
.SYNOPSIS
  Contract "template" (CLI owned by F7; this entry point is an F0 placeholder that F7 replaces).

.DESCRIPTION
  Builds GloamingForest templates (assets/structures/<unit>/<path>.gft.json: size, palette, RLE
  blocks, markers, jigsaw connectors, entities) from the *.gfs.ps1 DSL scripts in
  tools/structures/<unit>/, and with -Check validates every template against the 205 imported
  TF template definitions (sizes, connectors, markers, spawners, chests). Templates are original
  designs (rule R19); TF's .nbt files are never read.

.EXAMPLE
  pwsh tools/structures/build.ps1 -Unit lich
  pwsh tools/structures/build.ps1 -Check
#>
param(
    [string]$Unit,
    [switch]$Check
)
Write-Host '[template] not implemented: tools/structures/build.ps1 is a placeholder of the "template" contract (owner F7).' -ForegroundColor Red
exit 1