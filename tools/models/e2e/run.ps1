<#
.SYNOPSIS
  Runs the e2e scenario 'models' of unit F2 through the shared harnesses.

.DESCRIPTION
  F2 owns no path under tools/e2e/** (docs/spec/owners.tsv), so its scenario files live in
  tools/models/e2e/{server,client}/ and this wrapper copies them into the harness'
  scenarios/ folder for the duration of one run (untracked, removed afterwards), then runs
  tools/e2e/<server|client>/run.ps1 -Scenario models with the remaining arguments (throttle slots,
  rule R23). Exit code: the harness' (0 pass, 1 fail, 2 NOT RUN).

.EXAMPLE
  pwsh -NoProfile -File tools/models/e2e/run.ps1 server
  pwsh -NoProfile -File tools/models/e2e/run.ps1 client -Mode local
#>
param(
    [Parameter(Mandatory, Position = 0)][ValidateSet('server', 'client')][string]$Harness,
    [Parameter(ValueFromRemainingArguments)][object[]]$Rest
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
$src = Join-Path $PSScriptRoot $Harness
$dst = Join-Path $root "tools/e2e/$Harness/scenarios"
$copied = @()
$code = 1
try {
    foreach ($f in Get-ChildItem $src -Filter 'models*.ps1') {
        $target = Join-Path $dst $f.Name
        if ((Test-Path $target) -and -not ((Get-Content $target -Raw) -eq (Get-Content $f.FullName -Raw))) { throw "$target exists and differs from $($f.FullName)" }
        Copy-Item $f.FullName $target -Force
        $copied += $target
    }
    & pwsh -NoProfile -File (Join-Path $root "tools/e2e/$Harness/run.ps1") -Scenario models @Rest
    $code = $LASTEXITCODE
} finally {
    foreach ($c in $copied) { Remove-Item $c -Force -ErrorAction SilentlyContinue }
}
exit $code
