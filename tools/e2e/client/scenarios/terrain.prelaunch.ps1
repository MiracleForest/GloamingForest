# Client scenario 'terrain' (unit U2), before the game starts: give terrain.showcase a hold time so
# each generated sample (highlands, dark forest canopy, glacier) stays in the world long enough for
# the screenshots (config section 'terrain', showcaseHoldTicks).
# Dot-sourced by tools/e2e/client/run.ps1.
$gfTerrainConfig = Join-Path (Join-Path $script:ModsDir $script:ModName) 'config'
New-Item -ItemType Directory -Force -Path $gfTerrainConfig | Out-Null
[ordered]@{ version = 1; skylight = $false; chunkBudgetMs = 50; showcaseHoldTicks = 400 } | ConvertTo-Json |
    Set-Content -Path (Join-Path $gfTerrainConfig 'terrain.json') -Encoding UTF8
Write-Step 'terrain: showcaseHoldTicks 400'
