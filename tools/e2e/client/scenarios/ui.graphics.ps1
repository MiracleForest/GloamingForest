# Helpers of the F12 (ui) client scenario: game options of the instance (options.txt): the graphics
# mode (`graphics_mode`: 1 = Fancy, the "Classic" look; 2 = Advanced, "Vibrant Visuals") and the
# hand (`gfx_hidehand`: 0 so the held map shows). Dot-sourced by ui.prelaunch.ps1, ui.ps1 and
# ui.restore.ps1. The original values of this run's instance are kept in the e2e home
# (Get-E2EHome) and put back by ui.restore.ps1, so other harnesses sharing the instance keep their
# settings.

function Get-UiOptionsFiles {
    $data = Get-GameDataDir
    @(Get-ChildItem -Recurse -Path $data -Filter 'options.txt' -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match 'minecraftpe' })
}

# Outside out/ (cleaned at the start of every run): a killed run's backup survives until restored.
function Get-UiOptionBackupPath([string]$Key) {
    $name = if ($Key -eq 'graphics_mode') { 'ui-graphics-mode' } else { "ui-option-$Key" }
    Join-Path (Get-E2EHome) "$name.$($script:VersionName).json"
}

function Save-UiOption([string]$Key) {
    $backup = Get-UiOptionBackupPath $Key
    if (Test-Path $backup) { return } # the first value of the run is the one to restore
    $orig = @{}
    foreach ($f in Get-UiOptionsFiles) {
        $line = Get-Content $f.FullName | Where-Object { $_ -match "^${Key}:" } | Select-Object -First 1
        $orig[$f.FullName] = if ($line) { $line } else { '' }
    }
    New-Item -ItemType Directory -Force (Split-Path $backup) | Out-Null
    $orig | ConvertTo-Json | Set-Content -Path $backup -Encoding UTF8
}

function Set-UiOption([string]$Key, [string]$Value) {
    Save-UiOption $Key
    foreach ($f in Get-UiOptionsFiles) {
        $lines = @(Get-Content $f.FullName)
        if ($lines -match "^${Key}:") { $lines = $lines -replace "^${Key}:.*$", "${Key}:$Value" }
        else { $lines += "${Key}:$Value" }
        $lines | Set-Content $f.FullName
        Write-Step "${Key}:$Value in $($f.FullName)"
    }
}

function Restore-UiOption([string]$Key) {
    $backup = Get-UiOptionBackupPath $Key
    if (-not (Test-Path $backup)) { return }
    $orig = Get-Content $backup -Raw | ConvertFrom-Json
    foreach ($p in $orig.PSObject.Properties) {
        if (-not (Test-Path $p.Name)) { continue }
        $lines = @(Get-Content $p.Name)
        if ($p.Value) { $lines = $lines -replace "^${Key}:.*$", $p.Value }
        else { $lines = @($lines | Where-Object { $_ -notmatch "^${Key}:" }) }
        $lines | Set-Content $p.Name
        Write-Step "restored $(if ($p.Value) { $p.Value } else { "no ${Key}" }) in $($p.Name)"
    }
    Remove-Item $backup -Force
}

function Set-UiGraphicsMode([int]$Mode) { Set-UiOption 'graphics_mode' "$Mode" }
function Restore-UiGraphicsModes { Restore-UiOption 'graphics_mode' }

# A key by its Windows virtual-key code (F5 = 0x74 and other keys Press-Key has no name for).
function Press-VK([int]$vk, [int]$holdMs = 60) {
    Assert-GameForeground
    [BsE2E.Native]::Key([uint16]$vk, $false); Start-Sleep -Milliseconds $holdMs; [BsE2E.Native]::Key([uint16]$vk, $true)
    Start-Sleep -Milliseconds 200
}
