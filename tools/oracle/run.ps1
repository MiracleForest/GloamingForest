<#
.SYNOPSIS
  Runs the Java oracle (unit F13): TF 4.8 on a local NeoForge 1.21.1 dedicated server, driven over
  RCON, dumps derived facts into tests/golden (or checks that they reproduce).

.DESCRIPTION
  1. tools/oracle/fetch.ps1: JDK, NeoForge server and the TF jar in %LOCALAPPDATA%\gf-oracle
     (pinned in tools/oracle/oracle.json, never committed).
  2. Compiles the dumper mod (tools/oracle/dumper, adds /gforacle dump <kind> <file>).
  3. Takes a bds throttle slot (rule R23) and, for each seed of tools/oracle/spec.json, starts a
     fresh server (new world, default TF config, flat overworld, RCON on a free local port), runs
     the dumps over RCON and stops it. Seed-independent kinds run with the first seed only.
  4. Writes tests/golden/<kind>.json (rng, noise, mobs, loot, recipes, uncrafting) and
     tests/golden/<kind>/seed-<seed>.json (biomes, heights, density, landmarks, surface), and
     records each file's size, FNV-1a 64 and SHA-256 in tests/golden/manifest.json.

  Only derived facts are dumped: never a texture, model, sound, structure template or any other
  asset; no file is ever extracted from the TF jar (rules R10, R11).

  -Check runs the same dumps into a temporary directory and compares them byte for byte with the
  committed goldens (exit 1 on a difference): the dumps are reproducible.

  Running a Minecraft server requires accepting the Minecraft EULA (https://aka.ms/MinecraftEULA):
  pass -AcceptEula (or set GF_ORACLE_ACCEPT_EULA=1); the script then writes eula=true for the
  oracle's private server.

  Exit codes: 0 = done (or reproduced), 1 = failed (or differs), 2 = NOT RUN (no bds slot in time).

.EXAMPLE
  pwsh tools/oracle/run.ps1 -AcceptEula                         # every kind, every seed
  pwsh tools/oracle/run.ps1 -AcceptEula -Kinds biomes,landmarks -Seeds 0
  pwsh tools/oracle/run.ps1 -AcceptEula -Check                  # reproducible?
#>
param(
    [string[]]$Kinds,
    [long[]]$Seeds,
    [switch]$Check,
    [switch]$AcceptEula,
    [string]$Out,
    [int]$MemoryMB = 4096,
    [int]$TimeoutMinutes = 180,
    [int]$StartupMinutes = 15
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '../lib/Throttle.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'lib/Oracle.psm1') -Force

function Say([string]$m) { Write-Host "[oracle] $m" }

$repo = Get-OracleRepoRoot
$golden = Join-Path $repo 'tests/golden'
$spec = Get-Content (Join-Path $PSScriptRoot 'spec.json') -Raw | ConvertFrom-Json -AsHashtable
$pins = Get-OraclePins
$globalKinds = @($spec.globalKinds)
$seedKinds = @($spec.seedKinds)
if (-not $Kinds) { $Kinds = $globalKinds + $seedKinds }
$Kinds = @($Kinds | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim().ToLowerInvariant() } | Where-Object { $_ })
foreach ($k in $Kinds) { if ($k -notin ($globalKinds + $seedKinds)) { Write-Host "[oracle] unknown kind '$k' (known: $(($globalKinds + $seedKinds) -join ', '))" -ForegroundColor Red; exit 1 } }
$allSeeds = @($spec.seeds | ForEach-Object { [long]$_ })
if (-not $PSBoundParameters.ContainsKey('Seeds')) { $Seeds = $allSeeds }

if (-not ($AcceptEula -or $env:GF_ORACLE_ACCEPT_EULA -eq '1')) {
    Write-Host '[oracle] running the oracle starts a Minecraft server: pass -AcceptEula (or set GF_ORACLE_ACCEPT_EULA=1) to accept the Minecraft EULA (https://aka.ms/MinecraftEULA)' -ForegroundColor Red
    exit 1
}

function Get-SeedName([long]$Seed) { if ($Seed -lt 0) { "seed-m$(-[decimal]$Seed)" } else { "seed-$Seed" } }

# The relative golden path of a dump.
function Get-GoldenPath([string]$Kind, [long]$Seed) {
    if ($Kind -in $globalKinds) { return "$Kind.json" }
    return "$Kind/$(Get-SeedName $Seed).json"
}

# The plan: seed -> kinds; the seed-independent kinds run with the first seed.
$plan = [ordered]@{}
$wantGlobal = @($Kinds | Where-Object { $_ -in $globalKinds })
$wantSeed = @($Kinds | Where-Object { $_ -in $seedKinds })
foreach ($s in $Seeds) {
    # the seed kinds first: nothing may generate Twilight Forest chunks before the surface dump
    $k = @() + $wantSeed
    if ($plan.Count -eq 0) { $k += $wantGlobal }
    if ($k.Count) { $plan["$s"] = $k }
}
if ($wantGlobal.Count -and -not $plan.Count) { $plan["$($Seeds[0])"] = $wantGlobal }

$runId = Get-Date -Format 'yyyyMMdd-HHmmss'
$work = Join-Path (Get-OracleHome) "runs/$runId-$PID"
$dumpDir = Join-Path $work 'out'
New-Item -ItemType Directory -Force $dumpDir | Out-Null

$slot = $null
$lock = $null
$proc = $null
try {
    & (Join-Path $PSScriptRoot 'fetch.ps1') -Quiet | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'tools/oracle/fetch.ps1 failed' }
    $dumper = Build-OracleDumper { param($m) Say $m }
    Say "dumper: $dumper"

    [void](Enable-GfKillOnExit)
    try { $slot = Enter-GfSlot -Pool bds -Unit F13 -TimeoutMinutes $TimeoutMinutes }
    catch [System.TimeoutException] { Write-Host "[oracle] NOT RUN: $($_.Exception.Message)" -ForegroundColor Yellow; exit 2 }
    $lock = Enter-GfLock -Path (Join-Path (Get-OracleHome) 'server.lock') -What 'oracle server' -TimeoutMinutes $TimeoutMinutes -Unit F13

    $server = Get-OracleServerDir
    $java = Get-OracleJava
    $argsFile = (Get-OracleServerArgsFile).Substring($server.Length + 1).Replace('\', '/')

    foreach ($seedText in $plan.Keys) {
        $seed = [long]$seedText
        $kinds = $plan[$seedText]
        Say "seed $seed`: $($kinds -join ', ')"

        # a fresh server: new world, default configs, our two mods
        foreach ($d in 'world', 'config', 'defaultconfigs', 'logs', 'crash-reports', 'mods', 'oracle-out') {
            $p = Join-Path $server $d
            if (Test-Path $p) { Remove-Item -Recurse -Force $p }
        }
        New-Item -ItemType Directory -Force (Join-Path $server 'mods') | Out-Null
        Copy-Item -LiteralPath (Get-OracleTfJar) -Destination (Join-Path $server 'mods')
        Copy-Item -LiteralPath $dumper -Destination (Join-Path $server 'mods')
        $port = Get-OracleFreePort
        $rconPort = Get-OracleFreePort
        while ($rconPort -eq $port) { $rconPort = Get-OracleFreePort }
        $password = [guid]::NewGuid().ToString('N')
        $props = @(
            '# written by tools/oracle/run.ps1 (GloamingForest unit F13)',
            "level-seed=$seed", 'level-name=world', 'level-type=minecraft\:flat', 'generate-structures=true',
            'online-mode=false', 'enforce-secure-profile=false', 'server-ip=127.0.0.1', "server-port=$port",
            'enable-rcon=true', "rcon.port=$rconPort", "rcon.password=$password", 'broadcast-rcon-to-ops=false',
            'enable-query=false', 'max-tick-time=-1', 'spawn-protection=0', 'view-distance=3', 'simulation-distance=3',
            'sync-chunk-writes=false', 'difficulty=normal', 'gamemode=survival', 'max-players=1', 'spawn-monsters=true',
            'motd=GloamingForest oracle'
        )
        [IO.File]::WriteAllLines((Join-Path $server 'server.properties'), [string[]]$props)
        [IO.File]::WriteAllText((Join-Path $server 'eula.txt'), "# accepted for the oracle's private server (tools/oracle/run.ps1 -AcceptEula)`neula=true`n")
        $runSpec = [ordered]@{}
        foreach ($k in $spec.Keys) { $runSpec[$k] = $spec[$k] }
        $runSpec['source'] = [ordered]@{
            twilightforest = $pins.twilightforest.version; commit = $pins.twilightforest.commit
            minecraft = $pins.minecraft; neoforge = $pins.neoforge.version
        }
        [IO.File]::WriteAllText((Join-Path $server 'gf-oracle-spec.json'), ($runSpec | ConvertTo-Json -Depth 20))

        $log = Join-Path $work "server-$(Get-SeedName $seed).log"
        $stdin = Join-Path $work 'stdin.txt'
        [IO.File]::WriteAllText($stdin, '')
        $jvm = @("-Xmx${MemoryMB}M", '-Xms1024M', '-Dfile.encoding=UTF-8', '-Dterminal.jline=false', '-Dterminal.ansi=false', "@$argsFile", 'nogui')
        Say "starting the server (seed $seed, RCON on 127.0.0.1:$rconPort)"
        $proc = Start-Process -FilePath $java -ArgumentList $jvm -WorkingDirectory $server -NoNewWindow -PassThru `
            -RedirectStandardOutput $log -RedirectStandardError "$log.err" -RedirectStandardInput $stdin

        $rcon = $null
        $deadline = (Get-Date).AddMinutes($StartupMinutes)
        while (-not $rcon) {
            if ($proc.HasExited) { throw "the server exited during startup (exit $($proc.ExitCode)); see $log" }
            if ((Get-Date) -gt $deadline) { throw "the server did not open RCON within $StartupMinutes min; see $log" }
            if (Select-String -LiteralPath $log -Pattern 'RCON running on' -Quiet -ErrorAction SilentlyContinue) {
                try { $rcon = Connect-OracleRcon -Port $rconPort -Password $password } catch { Start-Sleep -Seconds 2 }
            } else { Start-Sleep -Seconds 2 }
        }
        try {
            $pong = $rcon.Command('gforacle ping')
            if ($pong -notmatch '^OK') { throw "the dumper mod is not loaded (gforacle ping: '$pong'); see $log" }
            foreach ($kind in $kinds) {
                $rel = Get-GoldenPath $kind $seed
                $file = Join-Path $dumpDir $rel
                New-Item -ItemType Directory -Force (Split-Path $file) | Out-Null
                $sw = [Diagnostics.Stopwatch]::StartNew()
                $reply = $rcon.Command("gforacle dump $kind $($file.Replace('\', '/'))")
                if ($reply -notmatch '^OK' -or -not (Test-Path -LiteralPath $file)) { throw "dump $kind (seed $seed) failed: $reply" }
                Say ("  {0,-11} {1} ({2:N0} KB, {3:N1} s)" -f $kind, $rel, ((Get-Item -LiteralPath $file).Length / 1KB), $sw.Elapsed.TotalSeconds)
            }
            $rcon.SendOnly('stop')
        } finally { $rcon.Close() }
        if (-not $proc.WaitForExit(300000)) { Say 'the server did not stop in 5 min: killing it'; $proc.Kill($true) }
        $proc = $null
        $errs = @(Select-String -LiteralPath $log -Pattern '\[gf_oracle/ERROR\]|gf_oracle: dump .* failed' -ErrorAction SilentlyContinue)
        if ($errs.Count) { throw "the dumper logged errors: $($errs[0].Line)" }
    }

    # results
    $produced = @(Get-ChildItem -LiteralPath $dumpDir -Recurse -File | ForEach-Object { $_.FullName.Substring($dumpDir.Length + 1).Replace('\', '/') } | Sort-Object)
    if ($Check) {
        $diff = @()
        foreach ($rel in $produced) {
            $committed = Join-Path $golden $rel
            if (-not (Test-Path -LiteralPath $committed)) { $diff += "$rel (not committed)"; continue }
            if ((Get-OracleSha256 $committed) -ne (Get-OracleSha256 (Join-Path $dumpDir $rel))) { $diff += $rel }
        }
        if ($diff.Count) {
            Write-Host "[oracle] $($diff.Count) dump(s) differ from tests/golden (new dumps in $dumpDir):" -ForegroundColor Red
            $diff | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
            exit 1
        }
        Say "reproduced: $($produced.Count) golden file(s) identical"
        Remove-Item -Recurse -Force $work
        exit 0
    }

    $dest = if ($Out) { $Out } else { $golden }
    New-Item -ItemType Directory -Force $dest | Out-Null
    foreach ($rel in $produced) {
        $target = Join-Path $dest $rel
        New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
        Copy-Item -LiteralPath (Join-Path $dumpDir $rel) -Destination $target -Force
    }
    # manifest: every golden file of $dest (kept entries for files this run did not touch)
    $files = [ordered]@{}
    $all = @(Get-ChildItem -LiteralPath $dest -Recurse -File -Filter '*.json' | Where-Object Name -ne 'manifest.json' |
        ForEach-Object { $_.FullName.Substring((Resolve-Path $dest).Path.Length + 1).Replace('\', '/') } | Sort-Object)
    foreach ($rel in $all) {
        $path = Join-Path $dest $rel
        $kind = ($rel -split '[/.]')[0]
        $entry = [ordered]@{ kind = $kind }
        if ($rel -match '/seed-(m?)(\d+)\.json$') { $entry.seed = [long]::Parse($(if ($Matches[1]) { '-' } else { '' }) + $Matches[2]) }
        $entry.bytes = (Get-Item -LiteralPath $path).Length
        $entry.fnv1a64 = Get-OracleFnv1a64 $path
        $entry.sha256 = Get-OracleSha256 $path
        $files[$rel] = $entry
    }
    $manifest = [ordered]@{
        format  = 1
        comment = 'Derived goldens of the Java oracle (unit F13): generated by tools/oracle/run.ps1, never hand-edited; tools/oracle/run.ps1 -Check reproduces them. Formats: tools/oracle/README.md.'
        source  = [ordered]@{
            twilightforest = $pins.twilightforest.version; commit = $pins.twilightforest.commit
            minecraft = $pins.minecraft; neoforge = $pins.neoforge.version; jdk = $pins.jdk.version
        }
        seeds   = $allSeeds
        files   = $files
    }
    $json = ($manifest | ConvertTo-Json -Depth 10) -replace "`r`n", "`n"
    [IO.File]::WriteAllText((Join-Path $dest 'manifest.json'), $json + "`n")
    Say "wrote $($produced.Count) golden file(s) and manifest.json to $dest"
    Remove-Item -Recurse -Force $work
    exit 0
} catch {
    Write-Host "[oracle] FAILED: $_" -ForegroundColor Red
    Write-Host "[oracle] logs: $work" -ForegroundColor Red
    exit 1
} finally {
    if ($proc -and -not $proc.HasExited) { try { $proc.Kill($true) } catch {} }
    Exit-GfLock $lock
    Exit-GfSlot $slot
}
