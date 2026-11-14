<#
.SYNOPSIS
  Downloads and installs the Java oracle's runtime: a JDK, a NeoForge 1.21.1 dedicated server and
  the upstream 4.8 jar (unit F13).

.DESCRIPTION
  Everything is pinned in tools/oracle/oracle.json (URL + SHA-256) and lands in
  $env:GF_ORACLE_HOME or %LOCALAPPDATA%\gf-oracle, outside every repository:

    downloads/   the JDK zip, the NeoForge installer, gloamingforest-<version>-universal.jar
    jdk/<ver>/   the extracted JDK (Eclipse Temurin 21)
    server/      the NeoForge server installed by the installer (libraries/, run scripts)

  The upstream jar is only ever loaded by the server (and put on javac's class path to compile the
  dumper mod): no file is ever extracted from it, in particular none of its All Rights Reserved
  .ogg sounds or .nbt structure templates (rules R10, R11).

  Idempotent and safe for concurrent callers: holds <oracle home>\fetch.lock, and the oracle
  server lock (server.lock) so a reinstall never replaces the JDK or the server under a running
  oracle; tools/oracle/run.ps1 takes that lock itself and passes -NoServerLock. Prints the oracle
  home on success. Exit code 0 = ready, 1 = failed.

.EXAMPLE
  pwsh tools/oracle/fetch.ps1
  pwsh tools/oracle/fetch.ps1 -Force      # verify the downloads and reinstall the server
#>
param(
    [switch]$Force,
    [switch]$Quiet,
    # the caller already holds the oracle server lock (tools/oracle/run.ps1)
    [switch]$NoServerLock,
    [int]$TimeoutMinutes = 60
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '../lib/Throttle.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'lib/Oracle.psm1') -Force

function Say([string]$m) { if (-not $Quiet) { Write-Host "[oracle] $m" } }

$pins = Get-OraclePins
$home_ = Get-OracleHome
$downloads = Join-Path $home_ 'downloads'
$serverDir = Get-OracleServerDir
$stamp = Join-Path $serverDir '.gf-oracle-installed'
$wantStamp = "neoforge=$($pins.neoforge.version) minecraft=$($pins.minecraft)"

$lock = $null
$serverLock = $null
try {
    $lock = Enter-GfLock -Path (Join-Path $home_ 'fetch.lock') -What 'oracle fetch lock' -TimeoutMinutes $TimeoutMinutes -Unit F13
    if (-not $NoServerLock) {
        $serverLock = Enter-GfLock -Path (Get-OracleServerLockPath) -What 'oracle server' -TimeoutMinutes $TimeoutMinutes -Unit F13
    }

    # 1. JDK
    $jdkDir = Get-OracleJdkDir
    if ($Force -or -not (Test-Path (Join-Path $jdkDir 'bin/java.exe'))) {
        $zip = Get-OracleDownload $pins.jdk.url (Join-Path $downloads (Split-Path $pins.jdk.url -Leaf)) $pins.jdk.sha256 { param($m) Say $m }
        $tmp = "$jdkDir.tmp"
        if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
        Say "extracting the JDK $($pins.jdk.version)"
        Expand-Archive -LiteralPath $zip -DestinationPath $tmp
        $inner = @(Get-ChildItem -LiteralPath $tmp -Directory)
        if ($inner.Count -ne 1 -or -not (Test-Path (Join-Path $inner[0].FullName 'bin/java.exe'))) { throw "unexpected JDK archive layout in $zip" }
        if (Test-Path $jdkDir) { Remove-Item -Recurse -Force $jdkDir }
        New-Item -ItemType Directory -Force (Split-Path $jdkDir) | Out-Null
        Move-Item -LiteralPath $inner[0].FullName -Destination $jdkDir
        Remove-Item -Recurse -Force $tmp
    }
    $java = Get-OracleJava
    Say "JDK: $java"

    # 2. upstream jar (loaded by the server; never extracted)
    $upUrl = if ($env:GF_ORACLE_UPSTREAM_URL) { $env:GF_ORACLE_UPSTREAM_URL } else { $pins.upstream.url }
    if (-not $upUrl) { throw 'set GF_ORACLE_UPSTREAM_URL to the download URL of the pinned upstream jar (tools/oracle/oracle.json, upstream.provenance)' }
    $gloamingJar = Get-OracleDownload $upUrl (Get-OracleGloamingJar) $pins.upstream.sha256 { param($m) Say $m }
    Say "upstream $($pins.upstream.version): $gloamingJar"

    # 3. NeoForge server
    $installed = (Test-Path $stamp) -and ((Get-Content $stamp -Raw).Trim() -eq $wantStamp) -and (Test-Path (Get-OracleServerArgsFile))
    if ($Force -or -not $installed) {
        $installer = Get-OracleDownload $pins.neoforge.url (Join-Path $downloads (Split-Path $pins.neoforge.url -Leaf)) $pins.neoforge.sha256 { param($m) Say $m }
        if (Test-Path $serverDir) { Remove-Item -Recurse -Force $serverDir }
        New-Item -ItemType Directory -Force $serverDir | Out-Null
        Say "installing NeoForge $($pins.neoforge.version) (Minecraft $($pins.minecraft)) into $serverDir"
        $log = Join-Path $home_ 'installer.log'
        $javaArgs = @(Get-OracleJavaProxyArgs) + @('-jar', "`"$installer`"", '--installServer', "`"$serverDir`"")
        $p = Start-Process -FilePath $java -ArgumentList $javaArgs `
            -WorkingDirectory $serverDir -NoNewWindow -Wait -PassThru -RedirectStandardOutput $log -RedirectStandardError "$log.err"
        if ($p.ExitCode -ne 0 -or -not (Test-Path (Get-OracleServerArgsFile))) {
            throw "the NeoForge installer failed (exit $($p.ExitCode)); see $log"
        }
        Set-Content -Path $stamp -Value $wantStamp -Encoding ascii
    }
    Say "server: $serverDir"
    Say "ready: $home_"
    if ($Quiet) { $home_ }
    exit 0
} catch {
    Write-Host "[oracle] FAILED: $_" -ForegroundColor Red
    exit 1
} finally {
    Exit-GfLock $serverLock
    Exit-GfLock $lock
}
