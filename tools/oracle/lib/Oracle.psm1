# Shared helpers of the Java oracle (unit F13): paths, pinned downloads, the JDK, the NeoForge
# server install and the RCON client. See tools/oracle/README.md.

Set-StrictMode -Version 3.0

$script:OracleRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$script:RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
Import-Module (Join-Path $script:RepoRoot 'tools/upstream/Upstream.psm1') -Force

# The dumper and the dumps use this project's names, the upstream jar its own: the renaming of the
# upstream view (tools/upstream/Upstream.psm1), forwards for the dumps and backwards for the sources.
function ConvertTo-OracleUpstreamNames([string]$Text) {
    $r = Get-UpstreamRebrand
    foreach ($p in $r.Pairs) { $Text = $Text.Replace($p[1], $p[0]) }
    return $Text
}
function ConvertFrom-OracleUpstreamNames([string]$Text) { return ConvertTo-OwnNames $Text (Get-UpstreamRebrand) }

function Get-OracleRoot { return $script:OracleRoot }
function Get-OracleRepoRoot { return $script:RepoRoot }

# %LOCALAPPDATA%\gf-oracle (or $env:GF_ORACLE_HOME): downloads, JDK, server install, runs. Outside
# every repository; never committed.
function Get-OracleHome {
    $h = if ($env:GF_ORACLE_HOME) { $env:GF_ORACLE_HOME } else { Join-Path $env:LOCALAPPDATA 'gf-oracle' }
    New-Item -ItemType Directory -Force $h | Out-Null
    return (Resolve-Path $h).Path
}

function Get-OraclePins {
    return Get-Content (Join-Path $script:OracleRoot 'oracle.json') -Raw | ConvertFrom-Json
}

function Get-OracleSha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

# Downloads $Url to $Dest (atomically, through a .part file) unless $Dest exists with the pinned
# SHA-256; fails on a mismatch.
function Get-OracleDownload([string]$Url, [string]$Dest, [string]$Sha256, [scriptblock]$Say = { param($m) Write-Host $m }) {
    if ((Test-Path -LiteralPath $Dest) -and (Get-OracleSha256 $Dest) -eq $Sha256) { return $Dest }
    New-Item -ItemType Directory -Force (Split-Path $Dest) | Out-Null
    $part = "$Dest.part"
    & $Say "downloading $Url"
    $old = $ProgressPreference
    $ProgressPreference = 'SilentlyContinue'
    try {
        for ($attempt = 1; ; $attempt++) {
            try { Invoke-WebRequest -Uri $Url -OutFile $part -TimeoutSec 900 -UseBasicParsing; break }
            catch { if ($attempt -ge 3) { throw }; & $Say "download failed ($($_.Exception.Message)); retrying"; Start-Sleep -Seconds (5 * $attempt) }
        }
    } finally { $ProgressPreference = $old }
    $got = Get-OracleSha256 $part
    if ($got -ne $Sha256) { Remove-Item -LiteralPath $part -Force; throw "SHA-256 mismatch for $Url`: got $got, pinned $Sha256" }
    Move-Item -LiteralPath $part -Destination $Dest -Force
    return $Dest
}

function Get-OracleJdkDir {
    $pins = Get-OraclePins
    return Join-Path (Get-OracleHome) ("jdk/" + ($pins.jdk.version -replace '[^0-9A-Za-z.]', '_'))
}

function Get-OracleJava([string]$Tool = 'java') {
    $exe = Join-Path (Get-OracleJdkDir) "bin/$Tool.exe"
    if (-not (Test-Path -LiteralPath $exe)) { throw "the oracle JDK is missing ($exe): run pwsh tools/oracle/fetch.ps1" }
    return $exe
}

function Get-OracleServerDir { return Join-Path (Get-OracleHome) 'server' }

# Held while the server directory, the JDK or the dumper cache is used or replaced (run.ps1 for a
# whole run, fetch.ps1 while it installs).
function Get-OracleServerLockPath { return Join-Path (Get-OracleHome) 'server.lock' }

# The NeoForge launch arguments file of the installed server.
function Get-OracleServerArgsFile {
    $pins = Get-OraclePins
    return Join-Path (Get-OracleServerDir) "libraries/net/neoforged/neoforge/$($pins.neoforge.version)/win_args.txt"
}

function Get-OracleGloamingJar {
    $pins = Get-OraclePins
    return Join-Path (Get-OracleHome) "downloads/upstream-$($pins.upstream.version)-universal.jar"
}

# Java system properties for the proxy in $env:HTTPS_PROXY / $env:HTTP_PROXY (Java ignores those
# variables; PowerShell's downloads use them).
function Get-OracleJavaProxyArgs {
    $out = @()
    foreach ($pair in @(@('https', $env:HTTPS_PROXY), @('http', $env:HTTP_PROXY))) {
        $scheme, $value = $pair
        if (-not $value) { continue }
        try { $u = [Uri]$value } catch { continue }
        if (-not $u.Host) { continue }
        $port = if ($u.Port -gt 0) { $u.Port } else { 80 }
        $out += "-D$scheme.proxyHost=$($u.Host)", "-D$scheme.proxyPort=$port"
    }
    if ($out.Count -and $env:NO_PROXY) {
        # NO_PROXY ".corp.example" (a domain suffix) is "*.corp.example" for Java
        $hosts = ($env:NO_PROXY -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ } |
            ForEach-Object { if ($_.StartsWith('.')) { "*$_" } else { $_ } }) -join '|'
        if ($hosts) { $out += "-Dhttp.nonProxyHosts=$hosts" }
    }
    return $out
}

# Every jar of the installed server (the javac class path of the dumper mod).
#  - NeoForge's patched Minecraft classes first, then the Mojang-mapped Minecraft jar (NeoForge
#    1.21.1 runs with Mojang names), then every other library;
#  - the obfuscated Minecraft jars (server, slim, unpacked, extra) are left out;
#  - the upstream jar last (compile-time only: upstream classes the dumper calls).
function Get-OracleCompileClasspath {
    $pins = Get-OraclePins
    $libs = Join-Path (Get-OracleServerDir) 'libraries'
    $patched = Join-Path $libs "net/neoforged/neoforge/$($pins.neoforge.version)/neoforge-$($pins.neoforge.version)-server.jar"
    $mc = @(Get-ChildItem -LiteralPath (Join-Path $libs "net/minecraft/server") -Recurse -Filter '*-srg.jar' | ForEach-Object FullName)
    if (-not (Test-Path -LiteralPath $patched) -or $mc.Count -ne 1) { throw "unexpected NeoForge server layout in ${libs}: run pwsh tools/oracle/fetch.ps1 -Force" }
    $mcDir = (Resolve-Path (Join-Path $libs 'net/minecraft/server')).Path
    $rest = @(Get-ChildItem -LiteralPath $libs -Recurse -Filter '*.jar' | Sort-Object FullName | ForEach-Object FullName |
        Where-Object { $_ -ne $patched -and -not $_.StartsWith($mcDir) })
    return @($patched, $mc[0]) + $rest + @(Get-OracleGloamingJar)
}

# Compiles tools/oracle/dumper (javac against the server's libraries and the upstream jar) into
# <oracle home>/dumper/<hash>/gf-oracle-dumper.jar, cached by the hash of its sources and pins.
function Build-OracleDumper([scriptblock]$Say = { param($m) Write-Host $m }) {
    $src = Join-Path $script:OracleRoot 'dumper'
    $files = @(Get-ChildItem -LiteralPath $src -Recurse -File | Sort-Object FullName)
    $sha = [Security.Cryptography.SHA256]::Create()
    $acc = [Text.StringBuilder]::new()
    foreach ($f in $files) {
        $rel = $f.FullName.Substring($src.Length).Replace('\', '/')
        [void]$acc.Append($rel).Append('=').Append([BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($f.FullName)))).Append(';')
    }
    [void]$acc.Append((Get-Content (Join-Path $script:OracleRoot 'oracle.json') -Raw))
    $hash = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($acc.ToString()))) -replace '-', '').Substring(0, 16).ToLowerInvariant()
    $dir = Join-Path (Get-OracleHome) "dumper/$hash"
    $jar = Join-Path $dir 'gf-oracle-dumper.jar'
    if (Test-Path -LiteralPath $jar) { return $jar }

    & $Say "compiling the dumper mod ($hash)"
    $tmp = "$dir.tmp"
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
    $classes = Join-Path $tmp 'classes'
    New-Item -ItemType Directory -Force $classes | Out-Null
    # the sources under the upstream names (ConvertTo-OracleUpstreamNames), next to the classes
    $metaRoot = Join-Path $tmp 'src'
    foreach ($f in $files) {
        $rel = $f.FullName.Substring($src.Length).TrimStart('\', '/')
        $target = Join-Path $metaRoot $rel
        New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
        [IO.File]::WriteAllText($target, (ConvertTo-OracleUpstreamNames ([IO.File]::ReadAllText($f.FullName))), [Text.UTF8Encoding]::new($false))
    }
    $cp = (Get-OracleCompileClasspath | ForEach-Object { $_.Replace('\', '/') }) -join ';'
    $sources = @(Get-ChildItem -LiteralPath $metaRoot -Recurse -File -Filter '*.java' | Sort-Object FullName | ForEach-Object { '"' + $_.FullName.Replace('\', '/') + '"' })
    $argFile = Join-Path $tmp 'javac.args'
    $lines = @('--release', '21', '-encoding', 'UTF-8', '-proc:none', '-Xlint:-options', '-cp', "`"$cp`"", '-d', "`"$($classes.Replace('\', '/'))`"") + $sources
    [IO.File]::WriteAllLines($argFile, [string[]]$lines)
    $out = & (Get-OracleJava 'javac') "@$argFile" 2>&1
    if ($LASTEXITCODE -ne 0) { throw "javac failed:`n$($out -join "`n")" }
    $out = & (Get-OracleJava 'jar') --create --file (Join-Path $tmp 'gf-oracle-dumper.jar') -C $classes . -C $metaRoot 'META-INF/neoforge.mods.toml' 2>&1
    if ($LASTEXITCODE -ne 0) { throw "jar failed:`n$($out -join "`n")" }
    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
    Move-Item -LiteralPath $tmp -Destination $dir
    return $jar
}

# FNV-1a 64 of a file's bytes as 16 lowercase hex digits (gf::core::fnv1a64, src/core/VolumeHash.h).
function Get-OracleFnv1a64([string]$Path) {
    if (-not ('GfOracleFnv' -as [type])) {
        Add-Type -TypeDefinition @'
public static class GfOracleFnv {
    public static ulong Hash(byte[] data) {
        ulong h = 0xcbf29ce484222325UL;
        foreach (byte b in data) { h ^= b; h *= 0x100000001b3UL; }
        return h;
    }
}
'@
    }
    return [GfOracleFnv]::Hash([IO.File]::ReadAllBytes($Path)).ToString('x16')
}

# A free local TCP port.
function Get-OracleFreePort {
    $l = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $l.Start()
    try { return $l.LocalEndpoint.Port } finally { $l.Stop() }
}

# ---------------------------------------------------------------------------------------------
# RCON (https://wiki.vg/RCON): little-endian int32 length, request id, type, ASCII body, two NULs.

class OracleRcon {
    [System.Net.Sockets.TcpClient]$Client
    [System.IO.Stream]$Stream
    [int]$NextId = 1

    OracleRcon([string]$HostName, [int]$Port, [string]$Password, [int]$TimeoutMs) {
        $this.Client = [System.Net.Sockets.TcpClient]::new()
        $this.Client.ReceiveTimeout = $TimeoutMs
        $this.Client.SendTimeout = 30000
        $this.Client.NoDelay = $true
        $this.Client.Connect($HostName, $Port)
        $this.Stream = $this.Client.GetStream()
        $id = $this.Send(3, $Password)
        $reply = $this.Read()
        if ($reply.Id -ne $id) { throw 'RCON authentication failed' }
    }

    [int] Send([int]$Type, [string]$Body) {
        $id = $this.NextId++
        $bytes = [Text.Encoding]::UTF8.GetBytes($Body)
        $ms = [System.IO.MemoryStream]::new()
        $bw = [System.IO.BinaryWriter]::new($ms)
        $bw.Write([int](10 + $bytes.Length))
        $bw.Write([int]$id)
        $bw.Write([int]$Type)
        $bw.Write($bytes)
        $bw.Write([byte]0); $bw.Write([byte]0)
        $bw.Flush()
        $buf = $ms.ToArray()
        $this.Stream.Write($buf, 0, $buf.Length)
        $this.Stream.Flush()
        return $id
    }

    [byte[]] ReadExactly([int]$Count) {
        $buf = [byte[]]::new($Count)
        $off = 0
        while ($off -lt $Count) {
            $n = $this.Stream.Read($buf, $off, $Count - $off)
            if ($n -le 0) { throw 'RCON connection closed' }
            $off += $n
        }
        return $buf
    }

    [pscustomobject] Read() {
        $len = [BitConverter]::ToInt32($this.ReadExactly(4), 0)
        $data = $this.ReadExactly($len)
        $id = [BitConverter]::ToInt32($data, 0)
        $type = [BitConverter]::ToInt32($data, 4)
        $body = [Text.Encoding]::UTF8.GetString($data, 8, [Math]::Max(0, $len - 10))
        return [pscustomobject]@{ Id = $id; Type = $type; Body = $body }
    }

    # Runs a command and returns its output. One request at a time: Minecraft's RCON server reads
    # one packet per socket read and drops the connection when a read holds more (or less) than
    # exactly one packet, so nothing is pipelined. Replies longer than 4096 characters arrive in
    # several packets; only the first is returned (the dumper's replies are one short line).
    [string] Command([string]$Cmd) {
        $id = $this.Send(2, $Cmd)
        while ($true) {
            $p = $this.Read()
            if ($p.Id -eq $id) { return $p.Body }
        }
        return ''
    }

    # Sends a command without waiting for its reply (stop closes the connection first).
    [void] SendOnly([string]$Cmd) { [void]$this.Send(2, $Cmd) }

    [void] Close() { if ($this.Client) { $this.Client.Dispose(); $this.Client = $null } }
}

function Connect-OracleRcon([int]$Port, [string]$Password, [int]$TimeoutMs = 3600000) {
    return [OracleRcon]::new('127.0.0.1', $Port, $Password, $TimeoutMs)
}

Export-ModuleMember -Function Get-OracleRoot, Get-OracleRepoRoot, Get-OracleHome, Get-OraclePins, Get-OracleSha256,
    Get-OracleDownload, Get-OracleJdkDir, Get-OracleJava, Get-OracleJavaProxyArgs, Get-OracleServerDir, Get-OracleServerLockPath, Get-OracleServerArgsFile,
    Get-OracleGloamingJar, Get-OracleCompileClasspath, Build-OracleDumper, ConvertTo-OracleUpstreamNames, ConvertFrom-OracleUpstreamNames, Get-OracleFnv1a64, Get-OracleFreePort, Connect-OracleRcon
