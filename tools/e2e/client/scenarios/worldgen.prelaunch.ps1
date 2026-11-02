# Worldgen (F5): before the game starts, put the custom probe biome (spike S2) into the deployed
# test packs (behavior pack biome + its client look), so the client can show it.
if ($Mode -ne 'remote-vanilla') {
    . "$PSScriptRoot/../../server/scenarios/worldgen.probe.ps1"
    Install-WorldgenProbe (Join-Path $script:ModsDir $script:ModName)
}
