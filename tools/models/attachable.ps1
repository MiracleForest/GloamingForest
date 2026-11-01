<#
.SYNOPSIS
  Contract "geo" (CLI owned by F2; this entry point is an F0 placeholder that F2 replaces).

.DESCRIPTION
  Generates the Bedrock attachable of an armor / Travellers' Gear / trophy head item from the
  converted geometry: assets/gen/geo/<unit>/attachables/<id>.json.

.EXAMPLE
  pwsh tools/models/attachable.ps1 -Item twilightforest:fiery_helmet
#>
param(
    [Parameter(Mandatory)][string]$Item,
    [string]$Unit
)
Write-Host "[geo] not implemented: tools/models/attachable.ps1 is a placeholder of the `"geo`" contract (owner F2); asked for $Item." -ForegroundColor Red
exit 1