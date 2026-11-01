<#
.SYNOPSIS
  Contract "synth" (F3): renders GloamingForest's original sounds and music from recipes.

.DESCRIPTION
  Every sound event of The Twilight Forest (docs/spec/owners.tsv, kind sound_event: 328) gets a
  Bedrock sound definition rendered by the procedural synthesizer (src/core/synth, compiled
  into tools/sounds/cli). TF's own audio is All Rights Reserved and is never read (rule R10):
  everything is synthesized from recipes, or refers to Bedrock's vanilla sounds where TF itself
  plays a vanilla event.

  Recipes, per owning unit <u> (the unit's name, e.g. fauna):
    tools/sounds/baseline/<u>.json   F3's baseline (generated once from TF's event inventory)
    tools/sounds/recipes/<u>.json    the owner's refinements (optional; wins per event, file and patch)
  Shared patches: tools/sounds/patches/**/*.json ("creature/growl"). Music: tracker tracks
  tools/sounds/music/<track>.json (U10) or tools/sounds/baseline/music/<track>.json.

  Output (generated layer, never edit by hand, rule R3):
    assets/gen/audio/<u>/rp/sounds/twilightforest/<u>/<file>.ogg
    assets/gen/audio/<u>/rp/sounds.d/<u>.json          sound_definitions fragment
  and src/feature/ext/synth/SoundEvents.inc (the event list of the synth.* selftests).

  The recipe format, the patch language and the tracker format are documented in
  tools/sounds/README.md.

  The renderer is compiled with clang-cl from tools/sounds/cli/SynthCli.cpp and
  src/core/synth/*.cpp (standard library only) into %LOCALAPPDATA%\gf-synth\<hash>\ the first time
  a source changes: a few seconds of a single compiler process.

.PARAMETER Unit
  Only this owner (unit id or name: U16, fauna). Default: every owner.
.PARAMETER Check
  Renders everything into a temporary directory and fails unless the committed tree is
  byte-identical (deterministic output) and every event is covered.
.PARAMETER Event
  With -Preview: only this event (or a wildcard).
.PARAMETER Preview
  Renders .wav previews into %TEMP%\gf-synth-preview\ instead of writing the tree.

.EXAMPLE
  pwsh tools/sounds/synth.ps1                       # every owner
  pwsh tools/sounds/synth.ps1 -Unit fauna           # one owner
  pwsh tools/sounds/synth.ps1 -Check                # CI: deterministic and complete
  pwsh tools/sounds/synth.ps1 -Preview -Event 'entity.twilightforest.naga.*'
#>
param(
    [string]$Unit,
    [switch]$Check,
    [string]$Event,
    [switch]$Preview,
    [int]$Threads = 4
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = (Resolve-Path "$PSScriptRoot/../..").Path

function Fail([string]$msg) { Write-Host "[synth] $msg" -ForegroundColor Red; exit 1 }

. (Join-Path $PSScriptRoot 'lib/SynthJson.ps1')

function Read-JsonFile([string]$path) {
    try { return Get-Content -LiteralPath $path -Raw | ConvertFrom-Json -Depth 100 }
    catch { Fail "bad JSON in $($path.Substring($root.Length + 1)): $($_.Exception.Message)" }
}

function Get-Prop($obj, [string]$name, $default = $null) {
    if ($null -eq $obj) { return $default }
    $p = $obj.PSObject.Properties[$name]
    if ($p) { return $p.Value }
    return $default
}

# ---- units, owners, events ----------------------------------------------------------------------------
$rules = Get-Content (Join-Path $root 'tools/spec/owners.rules.json') -Raw | ConvertFrom-Json -AsHashtable
$unitName = @{}
foreach ($k in $rules.units.Keys) { $unitName[$k] = $rules.units[$k].name }
$eventOwner = [ordered]@{}
foreach ($line in Get-Content (Join-Path $root 'docs/spec/owners.tsv')) {
    $c = $line -split "`t"
    if ($c.Count -ge 3 -and $c[0] -eq 'sound_event') { $eventOwner[$c[1]] = $unitName[$c[2]] }
}
if ($eventOwner.Count -eq 0) { Fail 'docs/spec/owners.tsv lists no sound_event rows' }
$owners = @($eventOwner.Values | Sort-Object -Unique)

if ($Unit) {
    $u = $null
    foreach ($k in $unitName.Keys) { if ($k -ieq $Unit -or $unitName[$k] -ieq $Unit) { $u = $unitName[$k] } }
    if (-not $u) { Fail "unknown unit '$Unit'" }
    if ($owners -notcontains $u) { Fail "unit '$u' owns no sound event" }
    $owners = @($u)
}

# ---- patch library --------------------------------------------------------------------------------------
$library = [ordered]@{}
$patchDir = Join-Path $root 'tools/sounds/patches'
if (Test-Path $patchDir) {
    foreach ($f in Get-ChildItem $patchDir -Recurse -Filter *.json | Sort-Object FullName) {
        $name = $f.FullName.Substring($patchDir.Length + 1).Replace('\', '/') -replace '\.json$', ''
        $library[$name] = Read-JsonFile $f.FullName
    }
}

function Get-Track([string]$name) {
    foreach ($dir in 'tools/sounds/music', 'tools/sounds/baseline/music') {
        $p = Join-Path $root "$dir/$name.json"
        if (Test-Path $p) { return Read-JsonFile $p }
    }
    Fail "unknown track '$name' (tools/sounds/music/$name.json or tools/sounds/baseline/music/$name.json)"
}

# ---- recipes: baseline + owner refinements --------------------------------------------------------------
function Merge-Section($into, $from) {
    if ($null -eq $from) { return }
    foreach ($p in $from.PSObject.Properties) { $into[$p.Name] = $p.Value }
}

$recipes = @{}
function Get-Recipe([string]$u) {
    if ($recipes.ContainsKey($u)) { return $recipes[$u] }
    $r = @{ patches = [ordered]@{}; files = [ordered]@{}; events = [ordered]@{}; sources = @() }
    foreach ($rel in "tools/sounds/baseline/$u.json", "tools/sounds/recipes/$u.json") {
        $p = Join-Path $root $rel
        if (-not (Test-Path $p)) { continue }
        $doc = Read-JsonFile $p
        Merge-Section $r.patches (Get-Prop $doc 'patches')
        Merge-Section $r.files (Get-Prop $doc 'files')
        Merge-Section $r.events (Get-Prop $doc 'events')
        $r.sources += $rel
    }
    $recipes[$u] = $r
    return $r
}

$validCategories = 'ambient', 'block', 'bottle', 'bucket', 'hostile', 'music', 'neutral', 'player', 'record', 'ui', 'weather'
$problems = [Collections.Generic.List[string]]::new()

# every event owned by its unit and covered
foreach ($u in @($eventOwner.Values | Sort-Object -Unique)) {
    $r = Get-Recipe $u
    foreach ($e in $r.events.Keys) {
        if (-not $eventOwner.Contains($e)) { $problems.Add("$($r.sources -join ', '): '$e' is not a TF sound event") }
        elseif ($eventOwner[$e] -ne $u) { $problems.Add("$($r.sources -join ', '): '$e' belongs to $($eventOwner[$e])") }
    }
}
foreach ($e in $eventOwner.Keys) {
    if (-not (Get-Recipe $eventOwner[$e]).events.Contains($e)) { $problems.Add("no recipe for $e (owner $($eventOwner[$e]))") }
}

# variants of an event, aliases ({"event": id}) resolved; returns objects with kind/name/fields
function Resolve-Variants([string]$e, [int]$depth = 0) {
    if ($depth -gt 8) { $problems.Add("alias loop at $e"); return @() }
    $u   = $eventOwner[$e]
    $def = (Get-Recipe $u).events[$e]
    $out = @()
    $i   = 0
    foreach ($v in @(Get-Prop $def 'variants' @())) {
        $i++
        $alias = Get-Prop $v 'event'
        if ($alias) {
            $alias = $alias -replace '^twilightforest:', ''
            if (-not $eventOwner.Contains($alias)) { $problems.Add("${e}: alias to unknown event $alias"); continue }
            foreach ($x in Resolve-Variants $alias ($depth + 1)) {
                $y = $x.Clone()
                foreach ($k in 'volume', 'pitch', 'weight') {
                    $m = Get-Prop $v $k
                    if ($null -ne $m) { $y[$k] = [double]$(if ($null -ne $y[$k]) { $y[$k] * $m } else { $m }) }
                }
                $out += $y
            }
            continue
        }
        $entry = @{ owner = $u; event = $e }
        foreach ($k in 'volume', 'pitch', 'weight') { $val = Get-Prop $v $k; if ($null -ne $val) { $entry[$k] = [double]$val } }
        foreach ($k in 'stream', 'is3D', 'load_on_low_memory') { $val = Get-Prop $v $k; if ($null -ne $val) { $entry[$k] = [bool]$val } }
        $vanilla = Get-Prop $v 'vanilla'
        $file    = Get-Prop $v 'file'
        if ($vanilla) {
            $entry.kind = 'vanilla'; $entry.name = $vanilla
        } elseif ($file) {
            if (-not (Get-Recipe $u).files.Contains($file)) { $problems.Add("${e}: unknown file '$file' (define it under ""files"" in $u's recipe)") }
            $entry.kind = 'file'; $entry.file = $file
        } elseif (Get-Prop $v 'patch') {
            $entry.kind = 'inline'; $entry.file = "inline/$e/$i"; $entry.spec = $v
        } else {
            $problems.Add("${e}: variant $i needs patch, file, vanilla or event")
            continue
        }
        $out += $entry
    }
    if ($out.Count -eq 0 -and $depth -eq 0) { $problems.Add("$e has no variants") }
    return $out
}

$resolved = [ordered]@{}
foreach ($e in $eventOwner.Keys) { if ((Get-Recipe $eventOwner[$e]).events.Contains($e)) { $resolved[$e] = @(Resolve-Variants $e) } }

if ($problems.Count) {
    $problems | Select-Object -First 50 | ForEach-Object { Write-Host "[synth] $_" -ForegroundColor Red }
    Fail "$($problems.Count) recipe problem(s)"
}

# ---- render jobs ----------------------------------------------------------------------------------------
function Get-RenderSpec([string]$u, [string]$file, $entry) {
    if ($entry.kind -eq 'inline') { return $entry.spec }
    return (Get-Recipe $u).files[$file]
}

function New-Job([string]$u, [string]$file, $spec, [string]$outPath) {
    $name  = "$u/$file"
    $track = Get-Prop $spec 'track'
    $job   = [ordered]@{ name = $name; out = $outPath }
    if ($track) {
        $t = if ($track -is [string]) { Get-Track $track } else { $track }
        $job.kind    = 'track'
        $job.track   = $t
        $job.rate    = [int](Get-Prop $spec 'rate' (Get-Prop $t 'rate' 22050))
        $job.quality = [double](Get-Prop $spec 'quality' (Get-Prop $t 'quality' 0.2))
    } else {
        $patch = Get-Prop $spec 'patch'
        if ($patch -is [string]) {
            $local = (Get-Recipe $u).patches
            if ($local.Contains($patch)) { $patch = $local[$patch] }
            elseif (-not $library.Contains($patch)) { Fail "${name}: unknown patch '$patch'" }
        }
        $job.kind    = 'patch'
        $job.patch   = $patch
        $job.params  = Get-Prop $spec 'params' ([pscustomobject]@{})
        $job.seed    = [long](Get-Prop $spec 'seed' (Get-NameSeed $name))
        $job.rate    = [int](Get-Prop $spec 'rate' 44100)
        $job.quality = [double](Get-Prop $spec 'quality' 0.4)
    }
    return $job
}

function Get-CategoryDefault([string]$e) {
    switch -Regex ($e) {
        '^music_disc\.' { return 'record' }
        '^music\.' { return 'music' }
        '^environment\.' { return 'weather' }
        '^block\.' { return 'block' }
        '^item\.' { return 'player' }
        default { return 'neutral' }
    }
}

# per owner: files to render and the sound_definitions fragment
$plans = [ordered]@{}
foreach ($u in $owners) {
    $files = [ordered]@{}
    $defs  = [ordered]@{}
    foreach ($e in ($resolved.Keys | Where-Object { $eventOwner[$_] -eq $u } | Sort-Object)) {
        $def    = (Get-Recipe $u).events[$e]
        $cat    = Get-Prop $def 'category' (Get-CategoryDefault $e)
        if ($validCategories -notcontains $cat) { Fail "${e}: category '$cat' is not a Bedrock sound category ($($validCategories -join ', '))" }
        $sounds = @()
        foreach ($x in $resolved[$e]) {
            $s = [ordered]@{}
            if ($x.kind -eq 'vanilla') {
                $s.name = $x.name
            } else {
                $s.name = "sounds/twilightforest/$($x.owner)/$($x.file)"
                if ($x.owner -eq $u -and -not $files.Contains($x.file)) { $files[$x.file] = Get-RenderSpec $u $x.file $x }
                elseif ($x.owner -ne $u) {
                    # an alias to another owner's file plays that owner's rendered file
                    $s.name = "sounds/twilightforest/$($x.owner)/$($x.file)"
                }
            }
            foreach ($k in 'volume', 'pitch') { if ($x.ContainsKey($k) -and $x[$k] -ne 1.0) { $s[$k] = $x[$k] } }
            if ($x.ContainsKey('weight') -and $x.weight -ne 1.0) { $s.weight = [int][math]::Round($x.weight) }
            foreach ($k in 'stream', 'is3D', 'load_on_low_memory') { if ($x.ContainsKey($k)) { $s[$k] = $x[$k] } }
            $sounds += $s
        }
        $d = [ordered]@{ category = $cat }
        foreach ($k in 'min_distance', 'max_distance') { $val = Get-Prop $def $k; if ($null -ne $val) { $d[$k] = [double]$val } }
        $d.sounds = $sounds
        $defs[$e] = $d
    }
    $plans[$u] = @{ files = $files; defs = $defs }
}

# ---- the renderer ---------------------------------------------------------------------------------------
function Get-Renderer {
    $env:Path = $env:Path + ';' + [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
    if ($env:LLVM_PATH -and (Test-Path (Join-Path $env:LLVM_PATH 'bin\clang-cl.exe'))) { $env:Path = (Join-Path $env:LLVM_PATH 'bin') + ";$env:Path" }
    $cc = Get-Command clang-cl -ErrorAction SilentlyContinue
    if (-not $cc) {
        foreach ($dir in "$env:ProgramFiles\LLVM\bin", "${env:ProgramFiles(x86)}\LLVM\bin") {
            if (Test-Path (Join-Path $dir 'clang-cl.exe')) { $cc = Get-Command (Join-Path $dir 'clang-cl.exe'); break }
        }
    }
    if (-not $cc) { Fail 'clang-cl not found (install LLVM or put it on PATH)' }
    $sources = @(Join-Path $root 'tools/sounds/cli/SynthCli.cpp') + @(Get-ChildItem (Join-Path $root 'src/core/synth') -Filter *.cpp | Sort-Object Name | ForEach-Object FullName)
    $headers = @(Get-ChildItem (Join-Path $root 'src/core/synth') -Filter *.h | Sort-Object Name | ForEach-Object FullName)
    $flags   = @('/nologo', '/std:c++20', '/O2', '/EHsc', '/utf-8', '/clang:-ffp-contract=off', '-fuse-ld=lld')
    $version = (& $cc.Source --version | Select-Object -First 1)
    $sha     = [Security.Cryptography.SHA256]::Create()
    $text    = ($flags -join ' ') + "`n$version`n" + (($sources + $headers) | ForEach-Object { [IO.Path]::GetFileName($_) + "`n" + ((Get-Content -LiteralPath $_ -Raw) -replace "`r`n", "`n") }) -join "`n"
    $key     = -join ($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($text))[0..7] | ForEach-Object { $_.ToString('x2') })
    $cache   = Join-Path $env:LOCALAPPDATA "gf-synth\$key"
    $exe     = Join-Path $cache 'gf-synth.exe'
    if (Test-Path $exe) { return $exe }
    $tmp = Join-Path $env:LOCALAPPDATA "gf-synth\build-$PID"
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $tmp | Out-Null
    Write-Host "[synth] compiling the renderer ($version)"
    $out = & $cc.Source @flags "/I$(Join-Path $root 'src')" @sources "/Fe$(Join-Path $tmp 'gf-synth.exe')" "/Fo$tmp\" 2>&1
    if ($LASTEXITCODE -ne 0) { $out | ForEach-Object { Write-Host $_ }; Fail 'renderer compilation failed' }
    New-Item -ItemType Directory -Force $cache | Out-Null
    try { Move-Item (Join-Path $tmp 'gf-synth.exe') $exe -ErrorAction Stop } catch { if (-not (Test-Path $exe)) { throw } }
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    return $exe
}

function Invoke-Render($jobs) {
    if ($jobs.Count -eq 0) { return }
    $exe     = Get-Renderer
    $jobFile = Join-Path ([IO.Path]::GetTempPath()) "gf-synth-jobs-$PID.json"
    $doc     = [ordered]@{ threads = $Threads; library = $library; jobs = $jobs }
    [IO.File]::WriteAllText($jobFile, ($doc | ConvertTo-Json -Depth 100 -Compress), [Text.UTF8Encoding]::new($false))
    $failed = $false
    & $exe render $jobFile | ForEach-Object {
        if ($_ -like 'ERR *') { Write-Host "[synth] $_" -ForegroundColor Red; $failed = $true }
    }
    $code = $LASTEXITCODE
    Remove-Item $jobFile -ErrorAction SilentlyContinue
    if ($failed -or $code -ne 0) { Fail 'rendering failed' }
}

# ---- preview ----------------------------------------------------------------------------------------------
if ($Preview) {
    $dir = Join-Path ([IO.Path]::GetTempPath()) 'gf-synth-preview'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $jobs = @()
    foreach ($u in $owners) {
        foreach ($e in $plans[$u].defs.Keys) {
            if ($Event -and $e -notlike $Event) { continue }
            foreach ($x in $resolved[$e]) {
                if ($x.kind -eq 'vanilla' -or $x.owner -ne $u) { continue }
                $spec = Get-RenderSpec $u $x.file $x
                $base = Join-Path $dir (($u + '_' + $x.file) -replace '[\\/]', '_')
                $job  = New-Job $u $x.file $spec "$base.ogg"
                $job.wav = "$base.wav"
                if (-not ($jobs | Where-Object { $_.name -eq $job.name })) { $jobs += $job }
            }
        }
    }
    Invoke-Render $jobs
    Write-Host "[synth] $($jobs.Count) preview(s) in $dir" -ForegroundColor Green
    exit 0
}

# ---- write (or check) the tree -------------------------------------------------------------------------
$audioRoot = Join-Path $root 'assets/gen/audio'
$target    = if ($Check) { Join-Path ([IO.Path]::GetTempPath()) "gf-synth-check-$PID" } else { $audioRoot }
if ($Check) { Remove-Item -Recurse -Force $target -ErrorAction SilentlyContinue }

$jobs = @()
foreach ($u in $owners) {
    $unitDir = Join-Path $target "$u/rp"
    $sndDir  = Join-Path $unitDir "sounds/twilightforest/$u"
    New-Item -ItemType Directory -Force (Join-Path $unitDir 'sounds.d') | Out-Null
    $fragment = Format-Json $plans[$u].defs
    [IO.File]::WriteAllText((Join-Path $unitDir "sounds.d/$u.json"), $fragment + "`n", [Text.UTF8Encoding]::new($false))
    $wanted = @{}
    foreach ($file in $plans[$u].files.Keys) { $wanted[[IO.Path]::GetFullPath((Join-Path $sndDir "$file.ogg"))] = $true }
    if (-not $Check -and (Test-Path $sndDir)) {
        # stale files of removed variants, then empty directories
        Get-ChildItem $sndDir -Recurse -File | Where-Object { -not $wanted.ContainsKey($_.FullName) } | Remove-Item -Force
        Get-ChildItem $sndDir -Recurse -Directory | Sort-Object { $_.FullName.Length } -Descending |
            Where-Object { -not (Get-ChildItem $_.FullName -Force) } | Remove-Item -Force
    }
    foreach ($file in $plans[$u].files.Keys) {
        $out = Join-Path $sndDir "$file.ogg"
        New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null
        $jobs += New-Job $u $file $plans[$u].files[$file] $out
    }
}
Invoke-Render $jobs

# the selftests' event list (src/feature/ext/synth/SoundEvents.inc)
$incLines = @('// Generated by tools/sounds/synth.ps1 from docs/spec/owners.tsv (kind sound_event): do not edit.',
    '// GF_SOUND_EVENT(event, owner unit, variants)')
foreach ($e in ($eventOwner.Keys | Sort-Object)) { $incLines += "GF_SOUND_EVENT(""$e"", ""$($eventOwner[$e])"", $(@($resolved[$e]).Count))" }
$incText = ($incLines -join "`n") + "`n"
$incPath = Join-Path $root 'src/feature/ext/synth/SoundEvents.inc'

if ($Check) {
    $diffs = [Collections.Generic.List[string]]::new()
    foreach ($u in $owners) {
        $a = Join-Path $target $u
        $b = Join-Path $audioRoot $u
        $fa = @{}; $fb = @{}
        if (Test-Path $a) { Get-ChildItem $a -Recurse -File | ForEach-Object { $fa[$_.FullName.Substring($a.Length + 1)] = $_.FullName } }
        if (Test-Path $b) { Get-ChildItem $b -Recurse -File | ForEach-Object { $fb[$_.FullName.Substring($b.Length + 1)] = $_.FullName } }
        foreach ($k in $fa.Keys) {
            if (-not $fb.ContainsKey($k)) { $diffs.Add("missing assets/gen/audio/$u/$($k.Replace('\', '/'))"); continue }
            $ha = (Get-FileHash $fa[$k] -Algorithm SHA256).Hash
            $hb = (Get-FileHash $fb[$k] -Algorithm SHA256).Hash
            if ($ha -ne $hb) { $diffs.Add("differs  assets/gen/audio/$u/$($k.Replace('\', '/'))") }
        }
        foreach ($k in $fb.Keys) { if (-not $fa.ContainsKey($k)) { $diffs.Add("stale    assets/gen/audio/$u/$($k.Replace('\', '/'))") } }
    }
    if (-not $Unit) {
        foreach ($d in Get-ChildItem $audioRoot -Directory) {
            if ($owners -notcontains $d.Name) { $diffs.Add("stale    assets/gen/audio/$($d.Name)/ (owns no sound event)") }
        }
        $cur = if (Test-Path $incPath) { (Get-Content $incPath -Raw) -replace "`r`n", "`n" } else { '' }
        if ($cur -ne $incText) { $diffs.Add('differs  src/feature/ext/synth/SoundEvents.inc') }
    }
    Remove-Item -Recurse -Force $target -ErrorAction SilentlyContinue
    if ($diffs.Count) {
        $diffs | Select-Object -First 40 | ForEach-Object { Write-Host "[synth] $_" -ForegroundColor Red }
        Fail "$($diffs.Count) difference(s): run pwsh tools/sounds/synth.ps1 and commit the result"
    }
    Write-Host "[synth] -Check OK: $($resolved.Count) events, $($jobs.Count) rendered files reproduce the committed tree" -ForegroundColor Green
    exit 0
}

if (-not $Unit) {
    New-Item -ItemType Directory -Force (Split-Path $incPath) | Out-Null
    [IO.File]::WriteAllText($incPath, $incText, [Text.UTF8Encoding]::new($false))
}
Write-Host "[synth] $($owners.Count) owner(s), $($resolved.Count) events, $($jobs.Count) files rendered into assets/gen/audio" -ForegroundColor Green
