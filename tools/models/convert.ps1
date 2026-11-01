<#
.SYNOPSIS
  Contract "geo" (CLI owned by F2; this entry point is an F0 placeholder that F2 replaces).

.DESCRIPTION
  Converts a TF Java model class (LayerDefinition / createBodyLayer, LGPL code) to Bedrock
  geometry: assets/gen/geo/<unit>/models/entity/<id>.geo.json, bones/<id>.json (Java part -> bone,
  pivots, mirror) and animations/<id>.base.json (a base animation stub for setupAnim).

.EXAMPLE
  pwsh tools/models/convert.ps1 -Java HydraModel -Unit hydra
#>
param(
    [Parameter(Mandatory)][string]$Java,
    [string]$Unit,
    [switch]$Check
)
Write-Host "[geo] not implemented: tools/models/convert.ps1 is a placeholder of the `"geo`" contract (owner F2); asked for $Java." -ForegroundColor Red
exit 1