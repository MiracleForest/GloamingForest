# Building (U5), local mode, before the game starts: the showcase functions in the client's deployed
# mod (building.showcase.ps1). Dot-sourced by tools/e2e/client/run.ps1.
. (Join-Path $PSScriptRoot 'building.showcase.ps1')
if ($script:Mode -eq 'local') {
    Install-BuildingShowcase (Join-Path $script:ModsDir $script:ModName)
    Write-Step 'building showcase functions added to the deployed mod'
}
