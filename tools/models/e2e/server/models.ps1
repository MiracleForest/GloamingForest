# Server scenario 'models' (unit F2): the converted geometry, base animations and attachables are in
# the deployed pack (models.prelaunch.ps1 put every partition there), they are well-formed and match
# the converter's index (models.* selftests), and BDS logged no content error loading them (run.ps1
# fails the run on any). Dot-sourced by tools/e2e/server/run.ps1.
$r = Invoke-SelfTest 'models.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) models selftest(s) failed" }
if ($r.Pass -lt 4) { throw "expected 4 models selftests, got $($r.Pass)" }
$log = Get-ServerLog
foreach ($what in 'geometries', 'base animations', 'attachables') {
    $hit = $log | Select-String -Pattern "SELFTEST INFO models\.\w+: $what present (\d+)/(\d+)" | Select-Object -Last 1
    if (-not $hit) { throw "no '$what present' line from the models selftests" }
    $g = $hit.Matches[0].Groups
    if ($g[1].Value -ne $g[2].Value -or [int]$g[2].Value -lt 1) { throw "not every converted file is in the pack: $($hit.Line)" }
    Write-Step "models: $what present $($g[1].Value)/$($g[2].Value)"
}
