<#
.SYNOPSIS
  Contract "synth" (CLI owned by F3; this entry point is an F0 placeholder that F3 replaces).

.DESCRIPTION
  Renders original synthesized audio from recipes, never from TF audio (rule R10):
  tools/sounds/recipes/<unit>.json {event: {category, subtitle, variants: [{patch, params, seed,
  volume, pitch}]}} -> assets/gen/audio/<unit>/sounds/**.ogg plus a sound_definitions fragment;
  tools/sounds/music/<track>.json (tracker) -> .ogg.

.EXAMPLE
  pwsh tools/sounds/synth.ps1 -Unit fauna
  pwsh tools/sounds/synth.ps1 -Check      # deterministic output
#>
param(
    [string]$Unit,
    [switch]$Check
)
Write-Host '[synth] not implemented: tools/sounds/synth.ps1 is a placeholder of the "synth" contract (owner F3).' -ForegroundColor Red
exit 1