# Families (F11), before the server boots: the block family generator (tools/families) on the
# deployed mod copy, so the gf_test fixture families exist in the gf_test packs. tools/xmake/
# assemble.lua will run the generator itself once its hook is in (see tools/families/run.ps1);
# running it again here is harmless.
$deployed = Join-Path $script:SlotDir "$(Get-ModsDirName $script:BaseDir)\$script:ModName"
Write-Step "families: generating the block families into $deployed"
& pwsh -NoProfile -File (Join-Path $script:RepoRoot 'tools/families/run.ps1') -Out $deployed
if ($LASTEXITCODE -ne 0) { throw 'tools/families/run.ps1 failed on the deployed mod' }
