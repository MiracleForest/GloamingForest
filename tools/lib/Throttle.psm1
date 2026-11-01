# Machine-wide throttle for builds and e2e runs (docs/spec/README.md, "Build throttle").
#
# Many agents share one machine (20 CPUs, 47 GB RAM). Every build and every e2e run takes a slot
# of a pool first; tools/build.ps1 and tools/e2e/*/run.ps1 are the only entry points (rule R23).
#
#   Import-Module "$PSScriptRoot/../lib/Throttle.psm1"
#   $slot = Enter-GfSlot -Pool build -Unit f0
#   try { ... } finally { Exit-GfSlot $slot }
#
# Pools (lock files <home>/<pool>/<n>.lock, <home> = $env:GF_THROTTLE_HOME or
# %LOCALAPPDATA%\gf-throttle):
#   build   3 slots, each building with xmake -j6. A slot is granted only while at least
#           10 GB of physical memory is free ($env:GF_BUILD_MIN_FREE_GB overrides).
#   bds     5 slots. Slot n also takes Levitate's server slot lock <lv-e2e>\slots\<n>.lock, so
#           both projects share the BDS slot directories (<lv-e2e>\slots\<n>\) without clashing.
#   client  2 slots. The client harness then takes a game instance lock shared with Levitate
#           (%APPDATA%\levilauncher.exe\bei-e2e.lock for the base instance, hi<N>-e2e.lock for the
#           clone instance <version>-hi<N>); see tools/e2e/client/lib.ps1.
#
# A lock file is opened with FileShare.None, so the OS releases it when the holding process dies:
# there is no stale state. The holder writes its pid, worktree and unit into <n>.lock.owner
# (diagnostics only: the lock file itself cannot be read while held). Waiters poll every 10 s.

Set-StrictMode -Version 3.0

$script:Pools = @{
    build  = @{ Slots = 3; Poll = 10 }
    bds    = @{ Slots = 5; Poll = 10 }
    client = @{ Slots = 2; Poll = 10 }
}

function Get-GfThrottleHome {
    $h = if ($env:GF_THROTTLE_HOME) { $env:GF_THROTTLE_HOME } else { Join-Path $env:LOCALAPPDATA 'gf-throttle' }
    New-Item -ItemType Directory -Force $h | Out-Null
    return $h
}

# Levitate's e2e home (BDS base install and server slots), shared on purpose.
function Get-LvE2EHome {
    $h = if ($env:LV_E2E_HOME) { $env:LV_E2E_HOME } else { Join-Path $env:LOCALAPPDATA 'lv-e2e' }
    New-Item -ItemType Directory -Force $h | Out-Null
    return $h
}

function Get-GfPoolSize([ValidateSet('build', 'bds', 'client')][string]$Pool) {
    $override = [Environment]::GetEnvironmentVariable("GF_THROTTLE_$($Pool.ToUpper())_SLOTS")
    if ($override -match '^\d+$' -and [int]$override -gt 0) { return [int]$override }
    return $script:Pools[$Pool].Slots
}

# "pid=<pid> worktree=<root> unit=<unit> at=<time>" of this process.
function Get-GfHolderInfo([string]$Unit) {
    $root = try { (Resolve-Path "$PSScriptRoot/../..").Path } catch { '?' }
    if (-not $Unit) { $Unit = if ($env:GF_UNIT) { $env:GF_UNIT } else { '?' } }
    return "pid=$PID worktree=$root unit=$Unit at=$(Get-Date -Format o)"
}

# Opens $Path exclusively; returns the stream or $null when somebody else holds it.
function Open-GfLockFile([string]$Path, [string]$Info) {
    New-Item -ItemType Directory -Force (Split-Path $Path) | Out-Null
    try {
        $s = [System.IO.File]::Open($Path, 'OpenOrCreate', 'ReadWrite', 'None')
    } catch [System.IO.IOException] { return $null } catch [System.UnauthorizedAccessException] { return $null }
    if ($Info) {
        $bytes = [Text.Encoding]::UTF8.GetBytes($Info)
        $s.SetLength(0); $s.Write($bytes, 0, $bytes.Length); $s.Flush()
        try { Set-Content -Path "$Path.owner" -Value $Info -ErrorAction Stop } catch {}
    }
    return $s
}

function Get-GfLockOwner([string]$Path) {
    try { return (Get-Content "$Path.owner" -Raw -ErrorAction Stop).Trim() } catch { return '?' }
}

# Free physical memory in GB, or $null when it cannot be read (CIM failed twice): callers treat
# that as a closed gate and poll again, never as "plenty free".
function Get-GfFreeMemoryGB {
    for ($attempt = 1; $attempt -le 2; $attempt++) {
        try {
            $os = Get-CimInstance Win32_OperatingSystem -ErrorAction Stop
            return [Math]::Round($os.FreePhysicalMemory / 1MB, 1)
        } catch {
            if ($attempt -eq 1) { Start-Sleep -Seconds 1 }
        }
    }
    return $null
}

$script:GfJobHandle = [IntPtr]::Zero

<#
.SYNOPSIS
  Puts the current process into a Windows Job Object with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE, so
  every process it starts afterwards (xmake, clang-cl, BDS, ...) is killed when it dies. A slot is
  released by the OS when its holder dies; without the job, a killed tools/build.ps1 would free its
  build slot while its xmake -j6 compiles keep running and oversubscribe the machine.

  The handle stays open for the life of the process (closing it kills the job). Call it only from a
  script that runs as its own process (`pwsh tools/build.ps1`), never from an interactive shell.
  Returns $true when the job is in place.
#>
function Enable-GfKillOnExit {
    if ($script:GfJobHandle -ne [IntPtr]::Zero) { return $true }
    if (-not $IsWindows) { return $false }
    if (-not ('GfJobNative' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class GfJobNative {
    [StructLayout(LayoutKind.Sequential)]
    struct BasicLimits {
        public long PerProcessUserTimeLimit; public long PerJobUserTimeLimit; public uint LimitFlags;
        public UIntPtr MinimumWorkingSetSize; public UIntPtr MaximumWorkingSetSize; public uint ActiveProcessLimit;
        public UIntPtr Affinity; public uint PriorityClass; public uint SchedulingClass;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct IoCounters { public ulong R, W, O, RB, WB, OB; }
    [StructLayout(LayoutKind.Sequential)]
    struct ExtendedLimits {
        public BasicLimits Basic; public IoCounters Io; public UIntPtr ProcessMemoryLimit; public UIntPtr JobMemoryLimit;
        public UIntPtr PeakProcessMemoryUsed; public UIntPtr PeakJobMemoryUsed;
    }
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern IntPtr CreateJobObjectW(IntPtr attributes, string name);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool SetInformationJobObject(IntPtr job, int infoClass, ref ExtendedLimits info, uint length);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
    [DllImport("kernel32.dll")]
    static extern IntPtr GetCurrentProcess();
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr handle);
    // A job with KILL_ON_JOB_CLOSE (0x2000) holding the current process; IntPtr.Zero on failure.
    public static IntPtr KillOnClose(out int error) {
        error = 0;
        IntPtr job = CreateJobObjectW(IntPtr.Zero, null);
        if (job == IntPtr.Zero) { error = Marshal.GetLastWin32Error(); return IntPtr.Zero; }
        var info = new ExtendedLimits();
        info.Basic.LimitFlags = 0x2000;
        if (!SetInformationJobObject(job, 9, ref info, (uint)Marshal.SizeOf(typeof(ExtendedLimits)))
            || !AssignProcessToJobObject(job, GetCurrentProcess())) {
            error = Marshal.GetLastWin32Error();
            CloseHandle(job);
            return IntPtr.Zero;
        }
        return job;
    }
}
'@
    }
    $err = 0
    $h = [GfJobNative]::KillOnClose([ref]$err)
    if ($h -eq [IntPtr]::Zero) {
        Write-Host "[throttle] warning: could not create the kill-on-exit job (Win32 error $err); child processes may outlive this one" -ForegroundColor Yellow
        return $false
    }
    $script:GfJobHandle = $h
    return $true
}

<#
.SYNOPSIS
  Takes a generic exclusive lock file (waits; throws after -TimeoutMinutes, 0 = forever).
  Returns an object for Exit-GfLock.
#>
function Enter-GfLock {
    param(
        [Parameter(Mandatory)][string]$Path,
        [string]$What = 'lock',
        [int]$TimeoutMinutes = 120,
        [int]$PollSeconds = 10,
        [string]$Unit
    )
    $info = Get-GfHolderInfo $Unit
    $start = Get-Date
    $deadline = if ($TimeoutMinutes -gt 0) { $start.AddMinutes($TimeoutMinutes) } else { [datetime]::MaxValue }
    $nextReport = $start
    while ($true) {
        $s = Open-GfLockFile $Path $info
        if ($s) { return [pscustomobject]@{ Path = $Path; Streams = @($s); What = $What } }
        $now = Get-Date
        if ($now -ge $nextReport) {
            Write-Host ("[throttle] waiting for {0} ({1:N1} min), held by {2}" -f $What, ($now - $start).TotalMinutes, (Get-GfLockOwner $Path))
            $nextReport = $now.AddSeconds(60)
        }
        if ($now -gt $deadline) {
            $e = [System.TimeoutException]::new("timed out after $TimeoutMinutes min waiting for $What ($Path, held by $(Get-GfLockOwner $Path))")
            throw $e
        }
        Start-Sleep -Seconds $PollSeconds
    }
}

function Exit-GfLock($Lock) {
    if (-not $Lock) { return }
    foreach ($s in @($Lock.Streams)) { if ($s) { $s.Dispose() } }
    $Lock.Streams = @()
}

# The extra (shared with Levitate) lock of a bds slot.
function Get-GfBdsSharedLockPath([int]$Index) {
    return Join-Path (Join-Path (Get-LvE2EHome) 'slots') "$Index.lock"
}

<#
.SYNOPSIS
  Takes a slot of a throttle pool and returns it ({Pool, Index, Streams, Path}). Waits (polling
  every 10 s) until a slot is free; build slots also wait for the free-memory gate. Throws a
  TimeoutException after -TimeoutMinutes (0 = wait forever): callers report that as NOT RUN.
#>
function Enter-GfSlot {
    param(
        [Parameter(Mandatory)][ValidateSet('build', 'bds', 'client')][string]$Pool,
        [int]$TimeoutMinutes = 180,
        [string]$Unit,
        [int]$Index = -1
    )
    $size = Get-GfPoolSize $Pool
    $dir = Join-Path (Get-GfThrottleHome) $Pool
    New-Item -ItemType Directory -Force $dir | Out-Null
    $info = Get-GfHolderInfo $Unit
    $minFree = if ($env:GF_BUILD_MIN_FREE_GB -match '^\d+(\.\d+)?$') { [double]$env:GF_BUILD_MIN_FREE_GB } else { 10.0 }
    $poll = $script:Pools[$Pool].Poll
    $start = Get-Date
    $deadline = if ($TimeoutMinutes -gt 0) { $start.AddMinutes($TimeoutMinutes) } else { [datetime]::MaxValue }
    $nextReport = $start
    $candidates = if ($Index -ge 0) { @($Index) } else { 0..($size - 1) }
    while ($true) {
        $why = 'all slots busy'
        $gateOk = $true
        if ($Pool -eq 'build') {
            $free = Get-GfFreeMemoryGB
            if ($null -eq $free) { $gateOk = $false; $why = 'free memory unknown (CIM query failed; retrying)' }
            elseif ($free -lt $minFree) { $gateOk = $false; $why = "only $free GB of memory free (gate: $minFree GB)" }
        }
        if ($gateOk) {
            foreach ($n in $candidates) {
                $path = Join-Path $dir "$n.lock"
                $s = Open-GfLockFile $path $info
                if (-not $s) { continue }
                $streams = @($s)
                if ($Pool -eq 'bds') {
                    # Levitate's harness takes the same slot directory with this lock.
                    $shared = Open-GfLockFile (Get-GfBdsSharedLockPath $n) $info
                    if (-not $shared) { $s.Dispose(); $why = "slot $n is used by another project"; continue }
                    $streams += $shared
                }
                $waited = ((Get-Date) - $start).TotalMinutes
                $msg = "[throttle] acquired $Pool slot $n"
                if ($waited -ge 0.2) { $msg += (" after {0:N1} min" -f $waited) }
                Write-Host $msg
                return [pscustomobject]@{ Pool = $Pool; Index = $n; Streams = $streams; Path = $path }
            }
        }
        $now = Get-Date
        if ($now -ge $nextReport) {
            $holders = @(foreach ($n in $candidates) { "$n=" + (Get-GfLockOwner (Join-Path $dir "$n.lock")) }) -join '; '
            Write-Host ("[throttle] waiting for a {0} slot ({1:N1} min): {2} [{3}]" -f $Pool, ($now - $start).TotalMinutes, $why, $holders)
            $nextReport = $now.AddSeconds(60)
        }
        if ($now -gt $deadline) {
            throw [System.TimeoutException]::new("timed out after $TimeoutMinutes min waiting for a $Pool slot ($why)")
        }
        Start-Sleep -Seconds $poll
    }
}

function Exit-GfSlot($Slot) {
    if (-not $Slot) { return }
    foreach ($s in @($Slot.Streams)) { if ($s) { $s.Dispose() } }
    $Slot.Streams = @()
    Write-Host "[throttle] released $($Slot.Pool) slot $($Slot.Index)"
}

# One line per pool slot: who holds it (diagnostics).
function Get-GfSlotStatus {
    foreach ($pool in 'build', 'bds', 'client') {
        $dir = Join-Path (Get-GfThrottleHome) $pool
        for ($n = 0; $n -lt (Get-GfPoolSize $pool); $n++) {
            $path = Join-Path $dir "$n.lock"
            $s = Open-GfLockFile $path $null
            $busy = -not $s
            if ($s) { $s.Dispose() }
            [pscustomobject]@{ Pool = $pool; Slot = $n; Busy = $busy; Owner = if ($busy) { Get-GfLockOwner $path } else { '' } }
        }
    }
}

Export-ModuleMember -Function Get-GfThrottleHome, Get-LvE2EHome, Get-GfPoolSize, Get-GfFreeMemoryGB, Enter-GfLock, Exit-GfLock,
    Enter-GfSlot, Exit-GfSlot, Get-GfSlotStatus, Get-GfBdsSharedLockPath, Get-GfHolderInfo, Enable-GfKillOnExit
