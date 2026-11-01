# Synth (F3): the assembled resource pack defines every TF sound event with its variants and every
# synthesized file is a valid mono Ogg Vorbis stream (synth.definitions); every event is played
# once, one per tick, to the players of the world (synth.play_all); the BDS content log stays
# clean (run.ps1 fails on content errors); and the renderer reproduces the committed tree
# byte for byte (tools/sounds/synth.ps1 -Check).
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$r = Invoke-SelfTest 'synth.*' -TimeoutSeconds 300
if ($r.Fail -gt 0) { throw "$($r.Fail) synth selftest(s) failed" }
if ($r.Pass -lt 2) { throw "expected 2 synth selftests to pass, got $($r.Pass)" }
Wait-LogLine 'synth: played \d+ sound events' 30 | Out-Null

$root = (Resolve-Path "$PSScriptRoot/../../../..").Path
Write-Step 'tools/sounds/synth.ps1 -Check'
& pwsh -NoProfile -File (Join-Path $root 'tools/sounds/synth.ps1') -Check
if ($LASTEXITCODE -ne 0) { throw 'tools/sounds/synth.ps1 -Check failed: the rendered audio is not deterministic or not committed' }
