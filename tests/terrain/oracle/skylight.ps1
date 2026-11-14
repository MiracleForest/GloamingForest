# Skylight goldens (unit U2): F13's Java oracle (upstream 4.8 on NeoForge 1.21.1) dumps the density router
# and the noise heights of the Gloaming Forest generated with upstream's Skylight noise settings
# (gloamingforest:skylight_noise_gen), which F13's goldens do not cover (tests/golden is gloaming_noise_gen).
#
# It reuses F13's oracle installation, dumper and RCON client unchanged (tools/oracle/lib/Oracle.psm1)
# and differs from tools/oracle/run.ps1 in one thing: each fresh world gets a world datapack that
# points the gloamingforest:gloaming_forest dimension at skylight_noise_gen, as upstream's "Skylight"
# world preset does (GloamingDimensionData#SKYLIGHT_NOISE_GEN). The vanilla server loads a pack it finds
# in world/datapacks when it creates the world ("Found new data pack file/gf_skylight, loading it
# automatically"), before the dimension registry is built.
#
#     pwsh tests/terrain/oracle/skylight.ps1 -AcceptEula            # -> tests/terrain/golden/skylight/{density,heights}/seed-*.json
#     pwsh tests/terrain/oracle/skylight.ps1 -AcceptEula -Check     # reproducible? (exit 1 on a difference)
#
# Exit 2 = NOT RUN (no bds slot in time). Only derived numbers are written (no upstream asset, R10/R11).
#     pwsh tests/terrain/oracle/skylight.ps1 -MarkGoldens    # adds source.settings to the committed goldens (no server)
param(
    [long[]]$Seeds,
    [switch]$Check,
    [switch]$AcceptEula,
    [int]$MemoryMB = 4096,
    [int]$TimeoutMinutes = 180,
    [int]$StartupMinutes = 15,
    [int]$DumpMinutes = 30,
    [switch]$MarkGoldens
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
Import-Module (Join-Path $repo 'tools/lib/Throttle.psm1') -Force
Import-Module (Join-Path $repo 'tools/oracle/lib/Oracle.psm1') -Force

function Say([string]$m) { Write-Host "[skylight-oracle] $m" }

$kinds = @('density', 'heights')
$spec = Get-Content (Join-Path $repo 'tools/oracle/spec.json') -Raw | ConvertFrom-Json -AsHashtable
$pins = Get-OraclePins
if (-not $Seeds) { $Seeds = @($spec.seeds | ForEach-Object { [long]$_ }) }
$golden = Join-Path $PSScriptRoot '../golden/skylight'
function Get-SeedName([long]$Seed) { if ($Seed -lt 0) { "seed-m$(-[decimal]$Seed)" } else { "seed-$Seed" } }
$settingsId = 'gloamingforest:skylight_noise_gen'
# The dumper writes its own "source" object (upstream, Minecraft and NeoForge versions) and ignores the
# spec's: the settings the dimension used are added to it here, so every golden names them.
function Add-SourceSettings([string]$Path) {
    $text = [IO.File]::ReadAllText($Path)
    if ($text -match '"source": \{[^\n}]*"settings":') { return }
    $re = [regex]'("source": \{[^\n}]*)\}'
    if (-not $re.IsMatch($text)) { throw "no source object in $Path" }
    [IO.File]::WriteAllText($Path, $re.Replace($text, "`$1, `"settings`": `"$settingsId`"}", 1))
}
if ($MarkGoldens) {
    Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot '../golden/skylight') -Recurse -Filter '*.json' | ForEach-Object { Add-SourceSettings $_.FullName }
    Say 'the committed goldens name their settings'
    exit 0
}
if (-not ($AcceptEula -or $env:GF_ORACLE_ACCEPT_EULA -eq '1')) {
    Write-Host '[skylight-oracle] this starts a Minecraft server: pass -AcceptEula (https://aka.ms/MinecraftEULA)' -ForegroundColor Red
    exit 1
}

# The datapack: upstream's own dimension JSON with the Skylight settings.
$dimension = [ordered]@{
    type      = 'gloamingforest:gloaming_forest_type'
    generator = [ordered]@{
        type         = 'minecraft:noise'
        biome_source = [ordered]@{ type = 'gloamingforest:gloaming_biomes'; terrain_data = 'gloamingforest:biome_grid' }
        settings     = 'gloamingforest:skylight_noise_gen'
    }
}
$mcmeta = [ordered]@{ pack = [ordered]@{ pack_format = 48; description = 'GloamingForest U2: the Gloaming Forest with skylight_noise_gen' } }

$work = Join-Path (Get-OracleHome) "runs/skylight-$(Get-Date -Format 'yyyyMMdd-HHmmss')-$PID"
$dumpDir = Join-Path $work 'out'
New-Item -ItemType Directory -Force $dumpDir | Out-Null
$slot = $null; $lock = $null; $proc = $null
try {
    [void](Enable-GfKillOnExit)
    try { $slot = Enter-GfSlot -Pool bds -Unit U2 -TimeoutMinutes $TimeoutMinutes }
    catch [System.TimeoutException] { Write-Host "[skylight-oracle] NOT RUN: $($_.Exception.Message)" -ForegroundColor Yellow; exit 2 }
    $lock = Enter-GfLock -Path (Get-OracleServerLockPath) -What 'oracle server' -TimeoutMinutes $TimeoutMinutes -Unit U2
    & (Join-Path $repo 'tools/oracle/fetch.ps1') -Quiet -NoServerLock | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'tools/oracle/fetch.ps1 failed' }
    $dumper = Build-OracleDumper { param($m) Say $m }
    $server = Get-OracleServerDir
    $java = Get-OracleJava
    $argsFile = (Get-OracleServerArgsFile).Substring($server.Length + 1).Replace('\', '/')

    foreach ($seed in $Seeds) {
        Say "seed $seed`: $($kinds -join ', ')"
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
            gloamingforest = $pins.gloamingforest.version; commit = $pins.gloamingforest.commit
            minecraft = $pins.minecraft; neoforge = $pins.neoforge.version; settings = 'gloamingforest:skylight_noise_gen'
        }
        [IO.File]::WriteAllText((Join-Path $server 'gf-oracle-spec.json'), ($runSpec | ConvertTo-Json -Depth 20))
        [IO.File]::WriteAllText((Join-Path $server 'eula.txt'), "# accepted for the oracle's private server (tests/terrain/oracle/skylight.ps1 -AcceptEula)`neula=true`n")
        $log = Join-Path $work "server-$(Get-SeedName $seed).log"
        $stdin = Join-Path $work 'stdin.txt'
        [IO.File]::WriteAllText($stdin, '')

        $rcon = $null
        for ($attempt = 1; -not $rcon; $attempt++) {
            $world = Join-Path $server 'world'
            if (Test-Path $world) { Remove-Item -Recurse -Force $world }
            $pack = Join-Path $world 'datapacks/gf_skylight'
            New-Item -ItemType Directory -Force (Join-Path $pack 'data/gloamingforest/dimension') | Out-Null
            [IO.File]::WriteAllText((Join-Path $pack 'pack.mcmeta'), ($mcmeta | ConvertTo-Json -Depth 5))
            [IO.File]::WriteAllText((Join-Path $pack 'data/gloamingforest/dimension/gloaming_forest.json'), ($dimension | ConvertTo-Json -Depth 5))
            $port = Get-OracleFreePort
            $rconPort = Get-OracleFreePort
            while ($rconPort -eq $port) { $rconPort = Get-OracleFreePort }
            $password = [guid]::NewGuid().ToString('N')
            $props = @(
                '# written by tests/terrain/oracle/skylight.ps1 (GloamingForest unit U2)',
                "level-seed=$seed", 'level-name=world', 'level-type=minecraft\:flat', 'generate-structures=true',
                'online-mode=false', 'enforce-secure-profile=false', 'server-ip=127.0.0.1', "server-port=$port",
                'enable-rcon=true', "rcon.port=$rconPort", "rcon.password=$password", 'broadcast-rcon-to-ops=false',
                'enable-query=false', 'max-tick-time=-1', 'spawn-protection=0', 'view-distance=3', 'simulation-distance=3',
                'sync-chunk-writes=false', 'difficulty=normal', 'gamemode=survival', 'max-players=1', 'spawn-monsters=true',
                'motd=GloamingForest skylight oracle'
            )
            [IO.File]::WriteAllLines((Join-Path $server 'server.properties'), [string[]]$props)
            $jvm = @("-Xmx${MemoryMB}M", '-Xms1024M', '-Dfile.encoding=UTF-8', '-Dterminal.jline=false', '-Dterminal.ansi=false', "@$argsFile", 'nogui')
            Say "starting the server (seed $seed, RCON on 127.0.0.1:$rconPort)"
            $proc = Start-Process -FilePath $java -ArgumentList $jvm -WorkingDirectory $server -NoNewWindow -PassThru `
                -RedirectStandardOutput $log -RedirectStandardError "$log.err" -RedirectStandardInput $stdin
            $deadline = (Get-Date).AddMinutes($StartupMinutes)
            while (-not $rcon) {
                if ($proc.HasExited) {
                    if ($attempt -lt 3 -and (Select-String -LiteralPath $log -Pattern 'FAILED TO BIND|Address already in use|BindException' -Quiet -ErrorAction SilentlyContinue)) {
                        Say 'a port was taken before the server bound it: retrying with new ports'
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
            if (-not (Select-String -LiteralPath $log -Pattern 'gf_skylight' -Quiet)) { throw "the server did not load the gf_skylight datapack; see $log" }
            foreach ($kind in $kinds) {
                $file = Join-Path $dumpDir "$kind/$(Get-SeedName $seed).json"
                New-Item -ItemType Directory -Force (Split-Path $file) | Out-Null
                $reply = $rcon.Command("gforacle dump $kind $($file.Replace('\', '/'))")
                if ($reply -notmatch '^OK' -or -not (Test-Path -LiteralPath $file)) { throw "dump $kind (seed $seed) failed: $reply" }
                Say ("  {0,-8} {1:N0} KB" -f $kind, ((Get-Item -LiteralPath $file).Length / 1KB))
            }
            $rcon.SendOnly('stop')
        } finally { $rcon.Close() }
        if (-not $proc.WaitForExit(300000)) { $proc.Kill($true) }
        $proc = $null
        $errs = @(Select-String -LiteralPath $log -Pattern '/ERROR\] \[gf_oracle/|gf_oracle: dump .* failed' -ErrorAction SilentlyContinue)
        if ($errs.Count) { throw "the dumper logged errors: $($errs[0].Line)" }
        foreach ($kind in $kinds) { Add-SourceSettings (Join-Path $dumpDir "$kind/$(Get-SeedName $seed).json") }
        # the dumps really are Skylight's (upstream's skylight_terrain): neither the final density nor the
        # heights equal gloaming_noise_gen's (F13's goldens)
        $density = Get-Content (Join-Path $dumpDir "density/$(Get-SeedName $seed).json") -Raw | ConvertFrom-Json
        $gloaming = Get-Content (Join-Path $repo "tests/golden/density/$(Get-SeedName $seed).json") -Raw | ConvertFrom-Json
        if (($density.router.final_density.values -join ',') -eq ($gloaming.router.final_density.values -join ',')) {
            throw "seed $seed`: the final density equals gloaming_noise_gen's: the datapack did not apply"
        }
        $heights = Get-Content (Join-Path $dumpDir "heights/$(Get-SeedName $seed).json") -Raw | ConvertFrom-Json
        $gloamingHeights = Get-Content (Join-Path $repo "tests/golden/heights/$(Get-SeedName $seed).json") -Raw | ConvertFrom-Json
        foreach ($field in 'worldSurfaceWg', 'oceanFloorWg') {
            if ((($heights.$field | ForEach-Object { $_ -join ',' }) -join ';') -eq (($gloamingHeights.$field | ForEach-Object { $_ -join ',' }) -join ';')) {
                throw "seed $seed`: the $field heights equal gloaming_noise_gen's: the datapack did not apply"
            }
        }
    }

    $produced = @(Get-ChildItem -LiteralPath $dumpDir -Recurse -File | ForEach-Object { $_.FullName.Substring($dumpDir.Length + 1).Replace('\', '/') } | Sort-Object)
    if ($Check) {
        $diff = @($produced | Where-Object {
            $c = Join-Path $golden $_
            -not (Test-Path -LiteralPath $c) -or (Get-OracleSha256 $c) -ne (Get-OracleSha256 (Join-Path $dumpDir $_))
        })
        if ($diff.Count) { Write-Host "[skylight-oracle] differences: $($diff -join ', ') (new dumps in $dumpDir)" -ForegroundColor Red; exit 1 }
        Say "reproduced: $($produced.Count) file(s) identical"
        Remove-Item -Recurse -Force $work
        exit 0
    }
    foreach ($rel in $produced) {
        $target = Join-Path $golden $rel
        New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
        Copy-Item -LiteralPath (Join-Path $dumpDir $rel) -Destination $target -Force
    }
    Say "wrote $($produced.Count) file(s) to $((Resolve-Path $golden).Path)"
    Remove-Item -Recurse -Force $work
    exit 0
} catch {
    Write-Host "[skylight-oracle] FAILED: $_" -ForegroundColor Red
    Write-Host "[skylight-oracle] logs: $work" -ForegroundColor Red
    exit 1
} finally {
    if ($proc -and -not $proc.HasExited) { try { $proc.Kill($true) } catch {} }
    Exit-GfLock $lock
    Exit-GfSlot $slot
}
