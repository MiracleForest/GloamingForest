# Spike S7 follow-up (F1): how a block of each format_version declares block tags in BDS 1.26.51. The probe blocks
# come from import.s7.prelaunch.ps1; import.s7_probe reports what the game registered (block type and permutation
# tags, block actor type), and the content log says which forms each format rejects. Evidence for
# docs/spikes/S7.md; a rejected form fails the run by design (its content error is the evidence).
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
$r = Invoke-SelfTest 'import.s7_probe'
# every TF block registered with its frozen states, and its tool / tier tags as the game holds them (the family
# blocks from the import's own skeleton at 1.26.50 in this run, see the prelaunch)
$r = Invoke-SelfTest 'import.blocks'
$r = Invoke-SelfTest 'import.harvest_tags'
$r = Invoke-SelfTest 'import.rotations'
$r = Invoke-SelfTest 'import.light'
$r = Invoke-SelfTest 'import.behaviour'
$errors = @(Get-ServerLog | Where-Object { $_ -match '\bERR\b' -and $_ -match '\[(Blocks|Recipes|Items)\]' })
$tf = @($errors | Where-Object { $_ -notmatch 'GloamingForest_S7_BP' })
Write-Step "content-log errors: $($errors.Count) ($($tf.Count) outside the probe pack)"
$errors | ForEach-Object { Write-Step "  $_" }
