<#
.SYNOPSIS
  Runs the Java oracle (unit F13): upstream 4.8 on a local NeoForge 1.21.1 dedicated server, driven over
  RCON, dumps derived facts into tests/golden (or checks that they reproduce).

.DESCRIPTION
  1. tools/oracle/fetch.ps1: JDK, NeoForge server and the upstream jar in %LOCALAPPDATA%\gf-oracle
     (pinned in tools/oracle/oracle.json, never committed).
  2. Compiles the dumper mod (tools/oracle/dumper, adds /gforacle dump <kind> <file>).
  3. Takes a bds throttle slot (rule R23) and, for each seed of tools/oracle/spec.json, starts a
     fresh server (new world, default upstream config, flat overworld, RCON on a free local port), runs
     the dumps over RCON and stops it. Seed-independent kinds run with the first seed only.
  4. Writes tests/golden/<kind>.json (rng, noise, mobs, loot, recipes, uncrafting, structure_steps) and
     tests/golden/<kind>/seed-<seed>.json (biomes, heights, density, landmarks, surface), and
     records each file's size, FNV-1a 64 and SHA-256 in tests/golden/manifest.json.

  Only derived facts are dumped: never a texture, model, sound, structure template or any other
  asset; no file is ever extracted from the upstream jar (rules R10, R11).

  -Check runs the same dumps into a temporary directory and compares them byte for byte with the
  committed goldens (exit 1 on a difference): the dumps are reproducible. It also fails when a
  committed golden the requested kinds and seeds should produce was not produced, when a committed
  golden belongs to no kind or seed of spec.json any more (stale), and when manifest.json differs
  from the manifest rebuilt from the committed goldens.

  Every run fails when the server logs an ERROR or FATAL line (or writes to stderr) after the first
  dump command, unless the line is in $LogAllowList below (each entry says why it is harmless);
  allowed lines are listed as warnings.

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
    [int]$StartupMinutes = 15,
    # longest a single dump may take before the run fails (the RCON reply timeout)
    [int]$DumpMinutes = 30,
    # oracle work directories kept under <oracle home>\runs (failed runs keep their logs there)
    [int]$KeepRuns = 5
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
if (@($Seeds).Count -eq 0) { Write-Host '[oracle] -Seeds is empty' -ForegroundColor Red; exit 1 }

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

# What a relative golden path holds: @{ kind; seed ($null for a global kind) }, or $null when the
# path is no golden name at all (neither <kind>.json nor <kind>/seed-[m]<n>.json).
function Get-GoldenKey([string]$Rel) {
    if ($Rel -match '^([a-z0-9_]+)\.json$') { return @{ kind = $Matches[1]; seed = $null } }
    if ($Rel -match '^([a-z0-9_]+)/seed-(m?)(\d+)\.json$') {
        $seed = [long]::Parse($(if ($Matches[2]) { '-' } else { '' }) + $Matches[3])
        return @{ kind = $Matches[1]; seed = $seed }
    }
    return $null
}

# The relative paths of the committed goldens (every *.json of tests/golden except manifest.json).
function Get-CommittedGoldens([string]$Dir) {
    if (-not (Test-Path -LiteralPath $Dir)) { return @() }
    $root = (Resolve-Path -LiteralPath $Dir).Path
    return @(Get-ChildItem -LiteralPath $root -Recurse -File -Filter '*.json' | Where-Object Name -ne 'manifest.json' |
        ForEach-Object { $_.FullName.Substring($root.Length + 1).Replace('\', '/') } | Sort-Object)
}

# manifest.json of a golden directory (text, LF line endings): every golden file with its kind,
# seed, size, FNV-1a 64 and SHA-256, plus the pinned sources and the seeds of spec.json.
function Get-GoldenManifestText([string]$Dir) {
    $files = [ordered]@{}
    foreach ($rel in (Get-CommittedGoldens $Dir)) {
        $path = Join-Path $Dir $rel
        $entry = [ordered]@{ kind = ($rel -split '[/.]')[0] }
        $key = Get-GoldenKey $rel
        if ($key -and $null -ne $key.seed) { $entry.seed = $key.seed }
        $entry.bytes = (Get-Item -LiteralPath $path).Length
        $entry.fnv1a64 = Get-OracleFnv1a64 $path
        $entry.sha256 = Get-OracleSha256 $path
        $files[$rel] = $entry
    }
    $manifest = [ordered]@{
        format  = 1
        comment = 'Derived goldens of the Java oracle (unit F13): generated by tools/oracle/run.ps1, never hand-edited; tools/oracle/run.ps1 -Check reproduces them. Formats: tools/oracle/README.md.'
        source  = [ordered]@{
            gloamingforest = $pins.upstream.version; commit = $pins.upstream.commit
            minecraft = $pins.minecraft; neoforge = $pins.neoforge.version; jdk = $pins.jdk.version
        }
        seeds   = $allSeeds
        files   = $files
    }
    return (($manifest | ConvertTo-Json -Depth 10) -replace "`r`n", "`n") + "`n"
}

# Server log lines at ERROR / FATAL (or on stderr) that are known to be harmless. Every other such
# line logged after the first dump command fails the run. Each entry: a regex and why it is allowed.
$LogAllowList = @(
    @{ pattern = 'No key layers in MapLike\[\{\}\]'
       why = 'DedicatedServerProperties parsing the empty generator-settings of level-type=flat (server startup, before any dump)' },
    @{ pattern = 'Failed to request yggdrasil public key'
       why = "authlib's key fetcher cannot reach api.minecraftservices.com (offline machine; online-mode=false, nobody logs in)" },
    @{ pattern = 'Did not find boss room from exit 3 - EPIC FAIL'
       why = "upstream's own StrongholdEntranceComponent#addChildren (LOGGER.fatal) when a Knight Stronghold layout has no boss room; the start still generates and the goldens record upstream as it is" }
)

# The ERROR / FATAL lines of a server log (from byte $Offset on) and every non-blank stderr line
# (from byte $ErrOffset on): @{ allowed = @(lines); errors = @(lines) }.
function Get-LogProblems([string]$Log, [long]$Offset, [long]$ErrOffset) {
    $out = @{ allowed = [Collections.Generic.List[string]]::new(); errors = [Collections.Generic.List[string]]::new() }
    $read = {
        param([string]$Path, [long]$From)
        if (-not (Test-Path -LiteralPath $Path)) { return @() }
        $bytes = [IO.File]::ReadAllBytes($Path)
        if ($From -ge $bytes.Length) { return @() }
        return [Text.Encoding]::UTF8.GetString($bytes, [int]$From, $bytes.Length - [int]$From) -split "`r?`n"
    }
    $lines = @(& $read $Log $Offset | Where-Object { $_ -match '^\[[^\]]*\] \[[^\]]*/(ERROR|FATAL)\]' })
    $lines += @(& $read "$Log.err" $ErrOffset | Where-Object { $_.Trim() } | ForEach-Object { "stderr: $_" })
    foreach ($line in $lines) {
        if (@($LogAllowList | Where-Object { $line -match $_.pattern }).Count) { $out.allowed.Add($line) } else { $out.errors.Add($line) }
    }
    return $out
}

# The plan: seed -> kinds; the seed-independent kinds run with the first seed.
$plan = [ordered]@{}
$wantGlobal = @($Kinds | Where-Object { $_ -in $globalKinds })
$wantSeed = @($Kinds | Where-Object { $_ -in $seedKinds })
foreach ($s in $Seeds) {
    # the seed kinds first: nothing may generate Gloaming Forest chunks before the surface dump
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
    [void](Enable-GfKillOnExit)
    try { $slot = Enter-GfSlot -Pool bds -Unit F13 -TimeoutMinutes $TimeoutMinutes }
    catch [System.TimeoutException] { Write-Host "[oracle] NOT RUN: $($_.Exception.Message)" -ForegroundColor Yellow; exit 2 }
    # one oracle at a time owns the server directory, the JDK and the dumper cache
    $lock = Enter-GfLock -Path (Get-OracleServerLockPath) -What 'oracle server' -TimeoutMinutes $TimeoutMinutes -Unit F13
    & (Join-Path $PSScriptRoot 'fetch.ps1') -Quiet -NoServerLock | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'tools/oracle/fetch.ps1 failed' }
    $dumper = Build-OracleDumper { param($m) Say $m }
    Say "dumper: $dumper"
    Get-ChildItem -LiteralPath (Split-Path $work) -Directory | Where-Object FullName -ne $work |
        Sort-Object LastWriteTime -Descending | Select-Object -Skip ([Math]::Max(0, $KeepRuns - 1)) |
        ForEach-Object { Remove-Item -Recurse -Force -LiteralPath $_.FullName -ErrorAction SilentlyContinue }

    $server = Get-OracleServerDir
    $java = Get-OracleJava
    $argsFile = (Get-OracleServerArgsFile).Substring($server.Length + 1).Replace('\', '/')

    foreach ($seedText in $plan.Keys) {
        $seed = [long]$seedText
        # (not $kinds: PowerShell names are case-insensitive, that would overwrite -Kinds)
        $runKinds = $plan[$seedText]
        Say "seed $seed`: $($runKinds -join ', ')"

        # a fresh server: new world, default configs, our two mods
        foreach ($d in 'world', 'config', 'defaultconfigs', 'logs', 'crash-reports', 'mods', 'oracle-out') {
            $p = Join-Path $server $d
            if (Test-Path $p) { Remove-Item -Recurse -Force $p }
        }
        New-Item -ItemType Directory -Force (Join-Path $server 'mods') | Out-Null
        Copy-Item -LiteralPath (Get-OracleGloamingJar) -Destination (Join-Path $server 'mods')
        Copy-Item -LiteralPath $dumper -Destination (Join-Path $server 'mods')
        $runSpec = [ordered]@{}
        foreach ($k in $spec.Keys) { $runSpec[$k] = $spec[$k] }
        $runSpec['source'] = [ordered]@{
            gloamingforest = $pins.upstream.version; commit = $pins.upstream.commit
            minecraft = $pins.minecraft; neoforge = $pins.neoforge.version
        }
        [IO.File]::WriteAllText((Join-Path $server 'gf-oracle-spec.json'), ($runSpec | ConvertTo-Json -Depth 20))
        [IO.File]::WriteAllText((Join-Path $server 'eula.txt'), "# accepted for the oracle's private server (tools/oracle/run.ps1 -AcceptEula)`neula=true`n")
        $log = Join-Path $work "server-$(Get-SeedName $seed).log"
        $stdin = Join-Path $work 'stdin.txt'
        [IO.File]::WriteAllText($stdin, '')

        $rcon = $null
        for ($attempt = 1; -not $rcon; $attempt++) {
            if (Test-Path (Join-Path $server 'world')) { Remove-Item -Recurse -Force (Join-Path $server 'world') }
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
            $jvm = @("-Xmx${MemoryMB}M", '-Xms1024M', '-Dfile.encoding=UTF-8', '-Dterminal.jline=false', '-Dterminal.ansi=false', "@$argsFile", 'nogui')
            Say "starting the server (seed $seed, RCON on 127.0.0.1:$rconPort)"
            $proc = Start-Process -FilePath $java -ArgumentList $jvm -WorkingDirectory $server -NoNewWindow -PassThru `
                -RedirectStandardOutput $log -RedirectStandardError "$log.err" -RedirectStandardInput $stdin

            $deadline = (Get-Date).AddMinutes($StartupMinutes)
            while (-not $rcon) {
                if ($proc.HasExited) {
                    # another process took a probed port before the server bound it: new ports, new try
                    if ($attempt -lt 3 -and (Select-String -LiteralPath $log -Pattern 'FAILED TO BIND|Address already in use|BindException' -Quiet -ErrorAction SilentlyContinue)) {
                        Say 'a port was taken before the server bound it: retrying with new ports'
                        Copy-Item -LiteralPath $log -Destination "$log.attempt$attempt" -Force
                        $proc = $null
                        break
                    }
                    throw "the server exited during startup (exit $($proc.ExitCode)); see $log"
                }
                if ((Get-Date) -gt $deadline) { throw "the server did not open RCON within $StartupMinutes min; see $log" }
                if (Select-String -LiteralPath $log -Pattern 'RCON running on' -Quiet -ErrorAction SilentlyContinue) {
                    try { $rcon = Connect-OracleRcon -Port $rconPort -Password $password -TimeoutMs ($DumpMinutes * 60000) } catch { Start-Sleep -Seconds 2 }
                } else { Start-Sleep -Seconds 2 }
            }
        }
        try {
            $pong = $rcon.Command('gforacle ping')
            if ($pong -notmatch '^OK') { throw "the dumper mod is not loaded (gforacle ping: '$pong'); see $log" }
            # the log scan below covers everything from the first dump command on
            $logOffset = (Get-Item -LiteralPath $log).Length
            $errOffset = if (Test-Path -LiteralPath "$log.err") { (Get-Item -LiteralPath "$log.err").Length } else { 0 }
            foreach ($kind in $runKinds) {
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
        # NeoForge's layout: "[time] [thread/LEVEL] [logger/MARKER]: message"
        $errs = @(Select-String -LiteralPath $log -Pattern '/ERROR\] \[gf_oracle/|gf_oracle: dump .* failed' -ErrorAction SilentlyContinue)
        if ($errs.Count) { throw "the dumper logged errors: $($errs[0].Line)" }
        # any other ERROR / FATAL line after the first dump: upstream, NeoForge or Minecraft failed during a dump
        $problems = Get-LogProblems $log $logOffset $errOffset
        foreach ($line in $problems.allowed) { Write-Host "[oracle]   allowed log line: $line" -ForegroundColor Yellow }
        if ($problems.errors.Count) {
            Write-Host "[oracle] the server logged $($problems.errors.Count) error line(s) during the dumps (seed $seed):" -ForegroundColor Red
            $problems.errors | Select-Object -First 20 | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
            throw "error lines in the server log during the dumps (allow a harmless one in `$LogAllowList with a reason); see $log"
        }
    }

    # results, under this project's names (the dumper ran against the upstream jar)
    foreach ($f in @(Get-ChildItem -LiteralPath $dumpDir -Recurse -File)) {
        [IO.File]::WriteAllText($f.FullName, (ConvertFrom-OracleUpstreamNames ([IO.File]::ReadAllText($f.FullName))), [Text.UTF8Encoding]::new($false))
    }
    $produced = @(Get-ChildItem -LiteralPath $dumpDir -Recurse -File | ForEach-Object { $_.FullName.Substring($dumpDir.Length + 1).Replace('\', '/') } | Sort-Object)
    $expected = @(@(foreach ($s in $plan.Keys) { foreach ($k in $plan[$s]) { Get-GoldenPath $k ([long]$s) } }) | Sort-Object -Unique)
    $missing = @($expected | Where-Object { $_ -notin $produced })
    if ($missing.Count) { throw "the run did not produce $($missing -join ', ')" }
    if ($Check) {
        $diff = @()
        foreach ($rel in $produced) {
            $committed = Join-Path $golden $rel
            if (-not (Test-Path -LiteralPath $committed)) { $diff += "$rel (produced, not committed)"; continue }
            if ((Get-OracleSha256 $committed) -ne (Get-OracleSha256 (Join-Path $dumpDir $rel))) { $diff += $rel }
        }
        # committed goldens this run should have produced, and goldens no run produces any more
        $requestedSeeds = @($plan.Keys | ForEach-Object { [long]$_ })
        foreach ($rel in (Get-CommittedGoldens $golden)) {
            $key = Get-GoldenKey $rel
            if (-not $key) { $diff += "$rel (committed, not a golden name: stale)"; continue }
            if ($key.kind -in $globalKinds) {
                if ($null -ne $key.seed) { $diff += "$rel (committed, $($key.kind) is seed-independent: stale)"; continue }
            } elseif ($key.kind -in $seedKinds) {
                if ($null -eq $key.seed) { $diff += "$rel (committed, $($key.kind) is per seed: stale)"; continue }
                if ($key.seed -notin $allSeeds) { $diff += "$rel (committed, seed $($key.seed) is not in spec.json: stale)"; continue }
            } else {
                $diff += "$rel (committed, kind '$($key.kind)' is not in spec.json: stale)"; continue
            }
            $wanted = $key.kind -in $Kinds -and ($null -eq $key.seed -or $key.seed -in $requestedSeeds)
            if ($wanted -and $rel -notin $produced) { $diff += "$rel (committed, not produced)" }
        }
        # manifest.json: rebuilt from the committed goldens (the produced ones are byte-identical)
        $manifestPath = Join-Path $golden 'manifest.json'
        $rebuilt = Get-GoldenManifestText $golden
        if (-not (Test-Path -LiteralPath $manifestPath)) { $diff += 'manifest.json (missing)' }
        elseif ([IO.File]::ReadAllText($manifestPath) -cne $rebuilt) {
            $diff += 'manifest.json (differs from the manifest rebuilt from the committed goldens)'
            [IO.File]::WriteAllText((Join-Path $work 'manifest.rebuilt.json'), $rebuilt)
        }
        if ($diff.Count) {
            Write-Host "[oracle] $($diff.Count) difference(s) with tests/golden (new dumps in $dumpDir):" -ForegroundColor Red
            $diff | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
            exit 1
        }
        Say "reproduced: $($produced.Count) golden file(s) identical; manifest.json and the committed set agree"
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
    [IO.File]::WriteAllText((Join-Path $dest 'manifest.json'), (Get-GoldenManifestText $dest))
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
