# Families (F11), before the server boots: a safety net. tools/xmake/assemble.lua runs the block
# family generator (tools/families) on every build, so the deployed copy already carries the
# generated blocks (families-generated.txt) and this run is a no-op; it only completes a copy
# deployed from a directory the build did not assemble.
$deployed = Join-Path $script:SlotDir "$(Get-ModsDirName $script:BaseDir)\$script:ModName"
Write-Step "families: generating the block families into $deployed"
& pwsh -NoProfile -File (Join-Path $script:RepoRoot 'tools/families/run.ps1') -Out $deployed
if ($LASTEXITCODE -ne 0) { throw 'tools/families/run.ps1 failed on the deployed mod' }
