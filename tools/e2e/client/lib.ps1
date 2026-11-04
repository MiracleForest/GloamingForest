# Adapted from LiteLDev/Levitate v0.1.0 (tools/e2e/client/lib.ps1), LGPL-3.0-or-later.
# GloamingForest client e2e helpers: deploy the mod into the LeviLauncher game instance,
# drive Minecraft Bedrock with input (the BeDD mod's control pipe by default, real
# SendInput with run.ps1 -Foreground) and capture screenshots.
# Adapted from BEI's tools/e2e/lib.ps1 (same machine, same game instance, same lock).
#
# Dot-sourced by run.ps1. Coordinates passed to Click/Move are client-area pixels of the game
# window, or fractions (0..1) of the client area with -Relative.

. "$PSScriptRoot/../common/common.ps1"
Import-Module (Join-Path $PSScriptRoot '../../lib/Throttle.psm1') -Force

$script:ClientDir    = $PSScriptRoot
$script:ModName      = 'GloamingForest'
$script:Launcher     = if ($env:LV_E2E_LAUNCHER) { $env:LV_E2E_LAUNCHER } else { Join-Path $env:USERPROFILE 'Desktop\LeviLauncher.exe' }
$script:LauncherData = Join-Path $env:APPDATA 'levilauncher.exe'
$script:BaseVersion  = if ($env:LV_E2E_CLIENT_VERSION) { $env:LV_E2E_CLIENT_VERSION } else { '1.26.51.01' }
# Mods that stay loaded during our runs. BeDD (headless input, see run.ps1) is
# installed fresh by every headless run and removed after it: never park it aside (a restored copy
# would make a normally started game ignore the real mouse and keyboard).
$script:KeepMods     = @('LeviLamina', 'CrashLogger', $script:ModName, 'BeDD')
$script:OutRoot      = Join-Path $PSScriptRoot 'out'
# BeDD (https://github.com/LiteLDev/BeDD, docs https://docs.levimc.org/BeDD/): a test-only client
# mod that lets run.ps1 drive the game through a control pipe instead of the real mouse and
# keyboard. Its files (DLL, BeDD.psm1, harness-shim.ps1, Start-BeDDGame.ps1, clone-instance.ps1)
# are taken from $env:BEDD_DIR, a local build in %USERPROFILE%\Desktop\BeDD\bin\BeDD, or the
# latest release, downloaded once into %LOCALAPPDATA%\gf-e2e\BeDD (Install-BeDDRelease).
function Install-BeDDRelease {
    $home_ = Join-Path $env:LOCALAPPDATA 'gf-e2e'
    $dest = Join-Path $home_ 'BeDD'
    if (Test-Path (Join-Path $dest 'harness-shim.ps1')) { return $dest }
    New-Item -ItemType Directory -Force $home_ | Out-Null
    $lock = Enter-FileLock (Join-Path $home_ 'bedd-install.lock') -TimeoutMinutes 30 -What 'BeDD install lock'
    try {
        if (Test-Path (Join-Path $dest 'harness-shim.ps1')) { return $dest }
        $url = if ($env:GF_BEDD_URL) { $env:GF_BEDD_URL } else { 'https://github.com/LiteLDev/BeDD/releases/latest/download/bedd-client-windows-x64.zip' }
        $tmp = Join-Path $home_ "bedd-$PID"
        if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
        New-Item -ItemType Directory $tmp | Out-Null
        Write-Step "downloading BeDD from $url"
        Invoke-WebRequest -Uri $url -OutFile (Join-Path $tmp 'bedd.zip') -UseBasicParsing
        Expand-Archive (Join-Path $tmp 'bedd.zip') -DestinationPath (Join-Path $tmp 'x')
        $src = Get-ChildItem (Join-Path $tmp 'x') -Recurse -Filter 'harness-shim.ps1' | Select-Object -First 1
        if (-not $src) { throw "the BeDD release has no harness-shim.ps1 ($url)" }
        if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
        Move-Item $src.DirectoryName $dest
        Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
        Write-Step "installed BeDD into $dest"
        return $dest
    } finally { Exit-FileLock $lock }
}
$script:BeDDDir = if ($env:BEDD_DIR) { $env:BEDD_DIR }
    elseif (Test-Path (Join-Path $env:USERPROFILE 'Desktop\BeDD\bin\BeDD\harness-shim.ps1')) { Join-Path $env:USERPROFILE 'Desktop\BeDD\bin\BeDD' }
    else { Join-Path $env:LOCALAPPDATA 'gf-e2e\BeDD' }

# Game instances ("slots"). Slot 1 is the LeviLauncher instance itself; its lock is shared with
# BEI's and BeetrootSkin's harnesses on purpose (they drive the same instance). Slots 2..N are
# clones made by BeDD's clone-instance.ps1 (versions\<base>-hi<N>: hard links + their
# own mods\, logs\ and game data) with a lock each, so headless runs can use several games at once.
# Foreground (real input) runs always use slot 1. Set-E2EInstance points every instance-specific
# path at a slot; run.ps1 picks the slot (Enter-E2ELock -Slots).
function Set-E2EInstance([int]$Slot) {
    $script:InstanceSlot = $Slot
    $script:VersionName  = if ($Slot -le 1) { $script:BaseVersion } else { "$($script:BaseVersion)-hi$Slot" }
    $script:VersionDir   = Join-Path $script:LauncherData "versions\$script:VersionName"
    $script:ModsDir      = Join-Path $script:VersionDir 'mods'
    # Other mods are moved here for the duration of a run (outside mods\, so LeviLamina ignores them).
    $script:AsideDir     = Join-Path $script:VersionDir 'mods.lv-e2e-aside'
    $script:LockFile     = if ($Slot -le 1) { Join-Path $script:LauncherData 'bei-e2e.lock' } else { Join-Path $script:LauncherData "hi$Slot-e2e.lock" }
    # BeDD control pipe of this run's game: a name of this run only. BeDD's own tools address an
    # instance's game by "BeDD-<instance>" (Initialize-BeDDInstance), and a client of that name
    # that is still retrying (BeDD.psm1 resends a request after a broken pipe and Connect-BeDD
    # waits for the pipe to come back) would reach our game: on clone hi2 a leftover `join
    # 127.0.0.1:19140` of another harness took the game off its world (docs/spec/e2e.md, Pending joins).
    $script:BeDDPipe = "BeDD-$script:VersionName-gf$PID"
    # Child pwsh processes that source this file (helpers, scenario scripts) start on the same slot.
    $env:GF_E2E_INSTANCE_SLOT = "$Slot"
}
Set-E2EInstance $(if ($env:GF_E2E_INSTANCE_SLOT -match '^\d+$') { [int]$env:GF_E2E_INSTANCE_SLOT } else { 1 })
$script:WorldName    = 'GF-Test'
$script:Unit         = 'scratch'

if (-not ('BsE2E.Native' -as [type])) {
    Add-Type -AssemblyName System.Drawing
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace BsE2E {
public static class Native {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)] struct KEYBDINPUT { public ushort wVk, wScan; public uint dwFlags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Explicit)] struct UNION { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
    [StructLayout(LayoutKind.Sequential)] struct INPUT { public uint type; public UNION u; }

    [DllImport("user32.dll")] static extern uint SendInput(uint n, INPUT[] inputs, int size);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint mapType);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
    public static void ForceForeground(IntPtr h) {
        IntPtr fg = GetForegroundWindow();
        uint fgThread = GetWindowThreadProcessId(fg, IntPtr.Zero);
        uint me = GetCurrentThreadId();
        AttachThreadInput(me, fgThread, true);
        keybd_event(0x12, 0, 0, IntPtr.Zero); keybd_event(0x12, 0, 2, IntPtr.Zero);
        ShowWindow(h, 9);
        BringWindowToTop(h);
        SetForegroundWindow(h);
        AttachThreadInput(me, fgThread, false);
    }

    const uint INPUT_MOUSE = 0, INPUT_KEYBOARD = 1;
    const uint KEYEVENTF_KEYUP = 2, KEYEVENTF_UNICODE = 4, KEYEVENTF_EXTENDED = 1;
    const uint MOUSEEVENTF_MOVE = 1, MOUSEEVENTF_LEFTDOWN = 2, MOUSEEVENTF_LEFTUP = 4,
               MOUSEEVENTF_RIGHTDOWN = 8, MOUSEEVENTF_RIGHTUP = 16, MOUSEEVENTF_WHEEL = 0x800;

    static void Send(INPUT i) { SendInput(1, new[] { i }, Marshal.SizeOf(typeof(INPUT))); }

    // Set by BeDD's harness-shim.ps1 (Register-BeDDNative): the input methods
    // below then go to the game through the mod's control pipe instead of SendInput.
    public static Action<ushort, bool> KeyRedirect;
    public static Action<int, bool> MouseButtonRedirect;
    public static Action<int> WheelRedirect;
    public static Action<char> UnicodeRedirect;
    public static Action<int, int> MoveRelativeRedirect;

    public static void Key(ushort vk, bool up) {
        if (KeyRedirect != null) { KeyRedirect(vk, up); return; }
        var i = new INPUT { type = INPUT_KEYBOARD };
        i.u.ki.wVk = vk;
        i.u.ki.wScan = (ushort)MapVirtualKey(vk, 0);
        i.u.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0);
        if (vk == 0x25 || vk == 0x26 || vk == 0x27 || vk == 0x28 || vk == 0x2E || vk == 0x24 || vk == 0x23) i.u.ki.dwFlags |= KEYEVENTF_EXTENDED;
        Send(i);
    }
    public static void Unicode(char c) {
        if (UnicodeRedirect != null) { UnicodeRedirect(c); return; }
        var down = new INPUT { type = INPUT_KEYBOARD }; down.u.ki.wScan = c; down.u.ki.dwFlags = KEYEVENTF_UNICODE;
        var up = new INPUT { type = INPUT_KEYBOARD }; up.u.ki.wScan = c; up.u.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        Send(down); Send(up);
    }
    public static void MouseButton(int button, bool up) {
        if (MouseButtonRedirect != null) { MouseButtonRedirect(button, up); return; }
        var i = new INPUT { type = INPUT_MOUSE };
        if (button == 0) i.u.mi.dwFlags = up ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_LEFTDOWN;
        else i.u.mi.dwFlags = up ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_RIGHTDOWN;
        Send(i);
    }
    public static void Wheel(int delta) {
        if (WheelRedirect != null) { WheelRedirect(delta); return; }
        var i = new INPUT { type = INPUT_MOUSE }; i.u.mi.dwFlags = MOUSEEVENTF_WHEEL; i.u.mi.mouseData = (uint)delta; Send(i);
    }
    public static void MoveRelative(int dx, int dy) {
        if (MoveRelativeRedirect != null) { MoveRelativeRedirect(dx, dy); return; }
        var i = new INPUT { type = INPUT_MOUSE }; i.u.mi.dwFlags = MOUSEEVENTF_MOVE; i.u.mi.dx = dx; i.u.mi.dy = dy; Send(i);
    }
}
}
'@
}

# Physical pixels everywhere (the desktop uses display scaling).
[BsE2E.Native]::SetProcessDpiAwarenessContext([IntPtr]::new(-4)) | Out-Null

# ---------------------------------------------------------------------------------------
# Watchdog: no call into the game may block the harness for ever
# ---------------------------------------------------------------------------------------
# A BeDD pipe call waits for the game's answer with no timeout of its own (BeDD.psm1 Invoke-BeDD:
# StreamReader.ReadLine on the pipe): a game hung in "Enabling mods" held a run for more than
# 10 minutes. Every pipe call (Register-PipeWatchdog) and every window capture (Invoke-Guarded)
# arms this timer; when it runs out, the timer thread kills the process tree of this run's game
# (Minecraft.Windows.exe of the instance folder), which breaks the pipe and ends the blocked call,
# and the run ends NOT RUN (exit 2) with a "WATCHDOG" line. It kills a game only while this run
# holds the instance's game lock (Enter-E2ELock sets InstanceDir, Exit-E2ELock clears it): without
# the lock the game is somebody else's.
if (-not ('GfE2E.Watchdog' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
namespace GfE2E {
public static class Watchdog {
    class Guard { public DateTime Deadline; public string What; public int Ms; }
    static readonly object Gate = new object();
    static readonly List<Guard> guards = new List<Guard>(); // nested calls: innermost last
    static Timer timer;
    static long armed;
    /// The game's folder (versions\<instance>\): only games started from it are killed.
    public static string InstanceDir;
    /// What hung and what was killed, once the watchdog fired (null until then).
    public static volatile string Fired;
    /// Starts guarding a call of at most `ms` (calls nest: Disarm ends the innermost).
    public static void Arm(int ms, string what) {
        ms = Math.Max(ms, 1000);
        lock (Gate) {
            guards.Add(new Guard { Deadline = DateTime.UtcNow.AddMilliseconds(ms), What = what, Ms = ms });
            Schedule();
        }
    }
    public static void Disarm() {
        lock (Gate) {
            if (guards.Count > 0) guards.RemoveAt(guards.Count - 1);
            Schedule();
        }
    }
    public static void Reset() {
        lock (Gate) { guards.Clear(); Dumps.Clear(); Schedule(); }
        Fired = null;
    }
    // The timer for the earliest deadline of the guarded calls (Gate held).
    static void Schedule() {
        long id = ++armed;
        if (timer != null) { timer.Dispose(); timer = null; }
        if (guards.Count == 0) return;
        Guard first = guards[0];
        foreach (var g in guards) if (g.Deadline < first.Deadline) first = g;
        long due = Math.Max(0L, (long)(first.Deadline - DateTime.UtcNow).TotalMilliseconds);
        timer = new Timer(_ => Fire(id, first.What, first.Ms), null, due, Timeout.Infinite);
    }
    static void Fire(long id, string what, int ms) {
        lock (Gate) {
            if (id != armed) return;
            guards.Clear();
            if (timer != null) { timer.Dispose(); timer = null; }
        }
        KillAndRecord(what + " got no answer within " + (ms / 1000) + " s");
    }
    /// Fires now, from the harness itself (a hang it saw without a pipe call, e.g. a game that
    /// never finishes enabling mods): kills this instance's games and sets Fired.
    public static void Trip(string what) {
        lock (Gate) {
            ++armed;
            guards.Clear();
            if (timer != null) { timer.Dispose(); timer = null; }
        }
        KillAndRecord(what);
    }
    [System.Runtime.InteropServices.DllImport("ntdll.dll")]
    static extern int NtSuspendProcess(IntPtr handle);
    /// Freezes a process (every thread): the harness's test switch $env:GF_E2E_TEST_HANG makes a
    /// real hang with it, so the NOT RUN paths can be exercised on purpose.
    public static void Suspend(int pid) {
        using (var p = Process.GetProcessById(pid)) { NtSuspendProcess(p.Handle); }
    }
    /// Where the watchdog writes a minidump of the hung game before it kills it (out/hangs, which
    /// no run clears; null: no dump), and the file name's prefix (scenario, mode, instance, time):
    /// <DumpDir>/<DumpPrefix>-<pid>.dmp.
    public static string DumpDir;
    public static string DumpPrefix = "hang";
    /// The dumps written so far in this run (run.ps1 saves the install trace next to them).
    public static readonly List<string> Dumps = new List<string>();
    [System.Runtime.InteropServices.DllImport("dbghelp.dll", SetLastError = true)]
    static extern bool MiniDumpWriteDump(IntPtr process, uint pid, Microsoft.Win32.SafeHandles.SafeFileHandle file,
        uint type, IntPtr exception, IntPtr userStream, IntPtr callback);
    // MiniDumpWithUnloadedModules | MiniDumpWithProcessThreadData | MiniDumpWithThreadInfo: every
    // thread's stack and state, the module list; a few MB, no heap.
    const uint DumpType = 0x20 | 0x100 | 0x1000;
    static string WriteDump(Process p) {
        if (DumpDir == null) return null;
        string path = System.IO.Path.Combine(DumpDir, DumpPrefix + "-" + p.Id + ".dmp");
        try {
            System.IO.Directory.CreateDirectory(DumpDir);
            using (var fs = new System.IO.FileStream(path, System.IO.FileMode.Create, System.IO.FileAccess.ReadWrite, System.IO.FileShare.None)) {
                if (MiniDumpWriteDump(p.Handle, (uint)p.Id, fs.SafeFileHandle, DumpType, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero)) {
                    lock (Gate) { Dumps.Add(path); }
                    return path;
                }
            }
            int err = System.Runtime.InteropServices.Marshal.GetLastWin32Error();
            try { System.IO.File.Delete(path); } catch (Exception) { }
            return "failed (error " + err + ")";
        } catch (Exception e) { return "failed (" + e.Message + ")"; }
    }
    static void KillAndRecord(string what) {
        var killed = new List<string>();
        var dumps = new List<string>();
        foreach (var p in Process.GetProcessesByName("Minecraft.Windows")) {
            try {
                string exe = p.MainModule.FileName;
                if (InstanceDir == null || !exe.StartsWith(InstanceDir, StringComparison.OrdinalIgnoreCase)) continue;
                string dump = WriteDump(p);
                if (dump != null) dumps.Add(dump);
                p.Kill(true);
                killed.Add(p.Id.ToString());
            } catch (Exception) { }
        }
        Fired = what + "; "
            + (killed.Count > 0 ? "killed the game's process tree (pid " + string.Join(", ", killed) + ")"
               : InstanceDir == null ? "killed nothing (this run holds no game lock)" : "no game of " + InstanceDir + " was running")
            + (dumps.Count > 0 ? "; minidump " + string.Join(", ", dumps) : "");
        Console.WriteLine("[e2e] WATCHDOG: " + Fired);
    }
}
}
'@
}

# Seconds a pipe call or window capture may take before the watchdog kills the game (on top of the
# call's own hold, delay and timeout). $env:GF_E2E_PIPE_TIMEOUT_SEC overrides the default.
$script:PipeTimeoutSec = if ($env:GF_E2E_PIPE_TIMEOUT_SEC -match '^\d+$') { [int]$env:GF_E2E_PIPE_TIMEOUT_SEC } else { 120 }

# Runs $Action with the watchdog armed (-What names it in the WATCHDOG line).
function Invoke-Guarded([string]$What, [scriptblock]$Action, [int]$Seconds = $script:PipeTimeoutSec) {
    [GfE2E.Watchdog]::Arm($Seconds * 1000, $What)
    try { & $Action } finally { [GfE2E.Watchdog]::Disarm() }
}

# Wraps BeDD.psm1's Invoke-BeDD (the one function every pipe call goes through) in the watchdog,
# inside the module, so its own functions (Send-BeDDKey, Connect-BeDD, ...) use the wrapper too.
# A call's budget is $script:PipeTimeoutSec plus what the request asks the game to wait for
# (timeoutMs, holdMs, delayMs per character of text).
function Register-PipeWatchdog {
    $bedd = Get-Module BeDD
    if (-not $bedd) { throw 'Register-PipeWatchdog: the BeDD module is not loaded (harness-shim.ps1 first)' }
    & $bedd {
        param([int]$BaseSeconds)
        $script:GfPipeBaseMs = $BaseSeconds * 1000
        if (Get-Variable -Name GfInvokeBeDD -Scope Script -ErrorAction SilentlyContinue) { return }
        $script:GfInvokeBeDD = ${function:Invoke-BeDD}
        # BeDD.psm1's Connect-BeDD loop, re-checking the watchdog between attempts: once it killed
        # the game (a ping that hung inside this loop, or a Trip), the loop stops at once instead of
        # retrying the pipe until its own deadline.
        function script:Connect-BeDD([int]$TimeoutSeconds = 180, [string]$PipeName) {
            if ($PipeName) { Set-BeDDPipe $PipeName }
            $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
            $last = $null
            while ((Get-Date) -lt $deadline) {
                if ([GfE2E.Watchdog]::Fired) { Disconnect-BeDD; throw "BeDD: not connecting: the watchdog killed the game ($([GfE2E.Watchdog]::Fired))" }
                try {
                    if (-not $script:Pipe -or -not $script:Pipe.IsConnected) { Open-BeDDPipe 2000 }
                    $r = Invoke-BeDD @{ op = 'ping' } -NoThrow
                    if ($r.ok -and ($r.hwnd -ne 0 -or $r.target -eq 'server')) { return $r }
                    $last = 'game window not created yet'
                } catch {
                    $last = $_.Exception.Message
                    Disconnect-BeDD
                }
                Start-Sleep -Milliseconds 500
            }
            throw "BeDD did not answer within $TimeoutSeconds s ($last). Is the mod deployed and the game running?"
        }
        function script:Invoke-BeDD([Parameter(Mandatory)][hashtable]$Request, [switch]$NoThrow) {
            if ([GfE2E.Watchdog]::Fired) { throw "BeDD: $($Request['op']) not sent: the watchdog killed the game ($([GfE2E.Watchdog]::Fired))" }
            $ms = $script:GfPipeBaseMs
            foreach ($k in 'timeoutMs', 'holdMs') { if ($Request[$k] -is [ValueType]) { $ms += [int]$Request[$k] } }
            if ($Request['text'] -is [string] -and $Request['delayMs'] -is [ValueType]) { $ms += $Request['text'].Length * [int]$Request['delayMs'] }
            [GfE2E.Watchdog]::Arm($ms, "the BeDD pipe call '$($Request['op'])'")
            try { & $script:GfInvokeBeDD -Request $Request -NoThrow:$NoThrow } finally { [GfE2E.Watchdog]::Disarm() }
        }
    } $script:PipeTimeoutSec
    # The commands the module exported to the global scope still name the original functions: point
    # them at the wrappers too (the harness and harness-shim.ps1 call Connect-BeDD from out there).
    Set-Item -Path function:global:Invoke-BeDD -Value (& $bedd { ${function:Invoke-BeDD} })
    Set-Item -Path function:global:Connect-BeDD -Value (& $bedd { ${function:Connect-BeDD} })
    Write-Step "pipe watchdog: every BeDD call gets $script:PipeTimeoutSec s (+ its own waits); then the game is killed and the run is NOT RUN"
}

# The WATCHDOG line once it fired ($null before).
function Get-WatchdogFired { [GfE2E.Watchdog]::Fired }

# A hang is NOT RUN, so that a stuck machine or instance is retried, but a hang the mod causes
# itself (a deadlock in client code) must not be retried for ever. The record lives in the e2e home
# per scenario, mode and hash of the mod DLL, with the instance of every hung run; a run that does
# not hang clears it. Consecutive hung runs fail (exit 1) once they happened on two different
# instances (the same build hangs wherever it runs: the mod's own making), or three times on one
# instance (a broken instance gets one more retry than the build, but never an endless one).
# A hang before a test switch's freeze point ($env:GF_E2E_TEST_HANG) is a real one and counts.
# Returns @{ Count; Instances; Fails } (Count 0 when -Hung is not set).
function Update-HangRecord([string]$Scenario, [string]$Mode, [string]$Dll, [string]$Instance, [switch]$Hung) {
    $hash = if ($Dll -and (Test-Path $Dll)) { (Get-FileHash $Dll -Algorithm SHA256).Hash.Substring(0, 16) } else { 'nodll' }
    $file = Join-Path (Get-E2EHome) "gf-hang.$Scenario.$Mode.$hash.json"
    if (-not $Hung) { Remove-Item $file -Force -ErrorAction SilentlyContinue; return @{ Count = 0; Instances = @(); Fails = $false } }
    $runs = @()
    if (Test-Path $file) {
        try {
            $old = Get-Content $file -Raw | ConvertFrom-Json
            # (a record of an older harness has only a count: those runs' instances are unknown)
            $runs = if ($old.runs) { @($old.runs | ForEach-Object { "$_" }) } else { @(1..[int]$old.count | ForEach-Object { '?' }) }
        } catch { $runs = @() }
    }
    $runs += $Instance
    @{ count = $runs.Count; runs = $runs; last = (Get-Date).ToString('o'); what = "$(Get-WatchdogFired)" } | ConvertTo-Json | Set-Content $file -Encoding UTF8
    $distinct = @($runs | Where-Object { $_ -ne '?' } | Sort-Object -Unique)
    return @{ Count = $runs.Count; Instances = $runs; Fails = ($runs.Count -ge 3 -or ($runs.Count -ge 2 -and $distinct.Count -ge 2)) }
}

# What the mod said about its module installs before a hang (src/mod/Module.cpp: "module X still
# running after N ms" every 20 s while one install() runs, "N modules installed in M ms" when done).
function Get-ModEnableTrace { try { @(Get-LeviLog | Select-String -Pattern "\[$script:ModName\].*(still running after \d+ ms|modules installed in|installing \d+ modules|module \S+ (installed|loaded) in \d+ ms \(slow\))" | ForEach-Object { $_.Line }) } catch { @() } }

# ---------------------------------------------------------------------------------------
# Lock (one per game instance; slot 1's is shared with BEI's and BeetrootSkin's harnesses)
# ---------------------------------------------------------------------------------------
# Takes the lock of the first free slot in -Slots (default: the current one, see Set-E2EInstance)
# and switches every instance path to that slot.
function Enter-E2ELock([int]$TimeoutMinutes = 120, [int[]]$Slots = @($script:InstanceSlot)) {
    if ($Slots.Count -eq 1) {
        Set-E2EInstance $Slots[0]
        $script:LockStream = Enter-FileLock $script:LockFile -TimeoutMinutes $TimeoutMinutes -PollSeconds 2 -What 'game lock'
    } else {
        $script:LockTimedOut = $false
        $deadline = if ($TimeoutMinutes -gt 0) { (Get-Date).AddMinutes($TimeoutMinutes) } else { [datetime]::MaxValue }
        $nextReport = Get-Date
        while ($true) {
            foreach ($s in $Slots) {
                Set-E2EInstance $s
                $script:LockStream = Enter-FileLockOnce $script:LockFile
                if ($script:LockStream) { break }
            }
            if ($script:LockStream) { break }
            if ((Get-Date) -ge $nextReport) { Write-Step "waiting for a game lock (slots $($Slots -join ', '))"; $nextReport = (Get-Date).AddSeconds(60) }
            if ((Get-Date) -gt $deadline) { $script:LockTimedOut = $true; throw "Timed out after $TimeoutMinutes min waiting for a game lock (slots $($Slots -join ', '))" }
            Start-Sleep -Seconds 2
        }
    }
    Write-Step "game lock acquired (slot $script:InstanceSlot, instance $script:VersionName, $script:LockFile)"
    # the watchdog may kill this instance's game now (and only now: see Invoke-Guarded)
    [GfE2E.Watchdog]::InstanceDir = $script:VersionDir.TrimEnd('\') + '\'
}

function Exit-E2ELock {
    [GfE2E.Watchdog]::InstanceDir = $null
    if ($script:LockStream) { Exit-FileLock $script:LockStream; $script:LockStream = $null; Write-Step "game lock released" }
}

# ---------------------------------------------------------------------------------------
# Deploy / other mods / world fixture
# ---------------------------------------------------------------------------------------
function Get-GameDataDir {
    # LeviLauncher isolation: per-version data under the version folder.
    $iso = Join-Path $script:VersionDir 'Minecraft Bedrock'
    if (Test-Path $iso) { return $iso }
    return Join-Path $env:APPDATA 'Minecraft Bedrock'
}

# Puts the camera back to first person (options.txt `game_thirdperson:0`) so a scenario that
# switched views (F5 / camera presets) can't leave the shared game in third person for later runs.
function Reset-CameraPerspective {
    $old = Set-GameOption 'game_thirdperson' '0'
    foreach ($p in $old.Keys) { if ($null -ne $old[$p] -and $old[$p] -ne '0') { Write-Step "reset camera perspective to first person in $p" } }
}

# The game's options files of this instance (one per signed-in user, and "Shared").
function Get-OptionsFiles {
    @(Get-ChildItem -Recurse -Path (Get-GameDataDir) -Filter 'options.txt' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\minecraftpe\\options\.txt$' })
}

# Sets `key:value` in every options file (appended when missing); returns the old values
# (path -> line value, $null when the key was missing).
function Set-GameOption([string]$Key, [string]$Value) {
    $old = [ordered]@{}
    foreach ($f in Get-OptionsFiles) {
        $lines = @(Get-Content $f.FullName)
        $line = $lines | Where-Object { $_ -match "^$([regex]::Escape($Key)):" } | Select-Object -First 1
        $old[$f.FullName] = if ($line) { $line.Substring($Key.Length + 1) } else { $null }
        if ($old[$f.FullName] -eq $Value) { continue }
        $lines = if ($line) { $lines -replace "^$([regex]::Escape($Key)):.*$", "${Key}:$Value" } else { $lines + "${Key}:$Value" }
        Set-Content -Path $f.FullName -Value $lines
    }
    return $old
}

# Options a scenario may toggle in the instance's options.txt, put to these values before every
# launch (Reset-GameOptions), so a killed or failed run (or another harness) cannot leave the
# instance in a state the scenarios do not expect:
#   gfx_hidehud      F1 hides the HUD (models, ui and families' name-tag shots; the game then writes
#                    gfx_hidehud:1 itself): toasts, the hotbar and name tags are not drawn
#   gfx_hidehand     ui.prelaunch.ps1 shows the hand for its held-map step (0, restored by ui.restore.ps1)
#   game_thirdperson F5 (ui's head step); first person
#   graphics_mode    ui switches between Classic (1) and Vibrant Visuals (2) (restored by ui.restore.ps1);
#                    2 is what every scenario's screenshots expect
# content_log_file is on for the run itself (Enable-ContentLog).
# Every option the harness sets (Set-RunOption) is put back to the instance's own value when the run
# ends (Restore-RunOptions): slot 1 is shared with BEI's and BeetrootSkin's harnesses and a person's
# own play, so a person's own hidden HUD or hand, or a Classic graphics mode, survives our runs.
# HUD safety net: while a run plays these options always have the values above, whatever an earlier
# run left. After a KILLED run (its backup file is still there, Initialize-OptionsBackup) the value
# that run's backup recorded is the instance's own one even when the option changed since: a scenario
# toggles these in game (models hides the HUD with F1, and the game writes that value itself), so the
# toggled value is never taken for a person's choice, and a killed models run can never leave the
# instance without its HUD past the next run.
$script:ForcedOptions = [ordered]@{ gfx_hidehud = '0'; gfx_hidehand = '0'; game_thirdperson = '0'; graphics_mode = '2' }

function Reset-GameOptions {
    $files = Get-OptionsFiles
    if (-not $files) { Write-Step "warning: no options.txt below $(Get-GameDataDir) (the game writes it on its first start)"; return }
    foreach ($k in $script:ForcedOptions.Keys) {
        $old = Set-RunOption $k $script:ForcedOptions[$k]
        foreach ($p in $old.Keys) { if ($old[$p] -ne $script:ForcedOptions[$k]) { Write-Step "options: ${k}:$($old[$p]) -> $($script:ForcedOptions[$k]) in $p (the instance's own value is put back after the run)" } }
    }
}

# The instance's own values of the options this run set: path -> {key -> value ($null: the line was
# missing)}, and the values the run set ($script:OptionsSet, same shape). Kept in the e2e home until
# they are put back, so a killed run's backup survives and the next run restores the instance's
# values, not the ones the killed run left behind; an option whose value is no longer the one the
# killed run set was changed since (by a person or another harness) and keeps that newer value,
# except the options a scenario toggles in game ($script:ForcedOptions: the HUD safety net).
$script:OptionsBackup = $null
$script:OptionsSet = $null
function Get-OptionsBackupPath { Join-Path (Get-E2EHome) "game-options.$($script:VersionName).json" }

# Loads what a killed run left (the backup file, and the content-log-only backup of older harnesses).
function Initialize-OptionsBackup {
    if ($null -ne $script:OptionsBackup) { return }
    $script:OptionsBackup = [ordered]@{}
    $script:OptionsSet = [ordered]@{}
    $file = Get-OptionsBackupPath
    if (Test-Path $file) {
        try {
            $saved = Get-Content $file -Raw | ConvertFrom-Json -AsHashtable
            foreach ($part in @(@('old', $script:OptionsBackup), @('set', $script:OptionsSet))) {
                $src = $saved[$part[0]]
                if (-not $src) { continue }
                foreach ($p in $src.Keys) { $part[1][$p] = [ordered]@{}; foreach ($k in $src[$p].Keys) { $part[1][$p][$k] = $src[$p][$k] } }
            }
            Write-Step "options: a killed run left $file; its values are what this run puts back (an option changed since keeps that newer value, except the ones a scenario toggles in game: `$script:ForcedOptions)"
        } catch { Write-Step "warning: unreadable $file ($_)" }
    }
    $legacy = Join-Path (Get-E2EHome) "content-log-option.$($script:VersionName).json"
    if (Test-Path $legacy) {
        try {
            $saved = Get-Content $legacy -Raw | ConvertFrom-Json -AsHashtable
            foreach ($p in $saved.Keys) {
                if (-not $script:OptionsBackup.Contains($p)) { $script:OptionsBackup[$p] = [ordered]@{} }
                if (-not $script:OptionsBackup[$p].Contains('content_log_file')) { $script:OptionsBackup[$p]['content_log_file'] = $saved[$p] }
            }
        } catch {}
        Save-OptionsBackup
        Remove-Item $legacy -Force -ErrorAction SilentlyContinue
    }
}

function Save-OptionsBackup { @{ old = $script:OptionsBackup; set = $script:OptionsSet } | ConvertTo-Json -Depth 5 | Set-Content -Path (Get-OptionsBackupPath) -Encoding UTF8 }

# Set-GameOption for the run: remembers each file's own value first (once per key), returns the
# values it replaced.
function Set-RunOption([string]$Key, [string]$Value) {
    Initialize-OptionsBackup
    $old = Set-GameOption $Key $Value
    foreach ($p in $old.Keys) {
        foreach ($m in $script:OptionsBackup, $script:OptionsSet) { if (-not $m.Contains($p)) { $m[$p] = [ordered]@{} } }
        $known = $script:OptionsBackup[$p].Contains($Key)
        # a value from a killed run counts only while the option still has the value that run set
        # (else a person or another harness changed it since), except for the options a scenario
        # toggles in game ($script:ForcedOptions): the killed run's scenario changed those
        if ($known -and $script:OptionsSet[$p].Contains($Key) -and $old[$p] -ne $script:OptionsSet[$p][$Key] -and
            -not $script:ForcedOptions.Contains($Key)) { $known = $false }
        if (-not $known) { $script:OptionsBackup[$p][$Key] = $old[$p] }
        $script:OptionsSet[$p][$Key] = $Value
    }
    Save-OptionsBackup
    return $old
}

# Puts back every option the run set (after the scenario's own restore script).
function Restore-RunOptions {
    if ($null -eq $script:OptionsBackup) { return }
    foreach ($p in $script:OptionsBackup.Keys) {
        if (-not (Test-Path $p)) { continue }
        $lines = @(Get-Content $p)
        $changed = $false
        foreach ($k in $script:OptionsBackup[$p].Keys) {
            $v = $script:OptionsBackup[$p][$k]
            $re = "^$([regex]::Escape($k)):"
            $cur = $lines | Where-Object { $_ -match $re } | Select-Object -First 1
            if ($null -ne $v) {
                if ($cur -eq "${k}:$v") { continue }
                $lines = if ($cur) { $lines -replace "$re.*$", "${k}:$v" } else { $lines + "${k}:$v" }
            } else {
                if (-not $cur) { continue }
                $lines = @($lines | Where-Object { $_ -notmatch $re })
            }
            $changed = $true
        }
        if ($changed) { Set-Content -Path $p -Value $lines }
    }
    $script:OptionsBackup = $null
    $script:OptionsSet = $null
    Remove-Item (Get-OptionsBackupPath) -Force -ErrorAction SilentlyContinue
    Write-Step 'options: the instance''s own values are back'
}

# ---------------------------------------------------------------------------------------
# Content log: the game's own report on the packs (every client run)
# ---------------------------------------------------------------------------------------
# options.txt content_log_file:1 makes the game write <game data>\logs\ContentLog<date>_<time>_1.txt,
# one file per game session. Every run turns it on before the first launch (Restore-RunOptions puts
# the old value back at the end) and checks the files of its sessions after the game closed
# (Get-ContentLogProblems): an error fails the run, warnings are listed (and fail when the scenario
# sets $script:ContentLogStrict = $true, or with run.ps1 -StrictContentLog), and every other level
# (verbose, info, inform) is listed in the summary and never fails.
#
# The canary. The game creates the file only on its first entry, so a clean session writes none, and
# "no file" could mean "clean" as well as "logging is broken". Every run therefore loads one
# deliberate, harmless bad line: the test resource pack GloamingForest_Test_RP (assets/test, in every
# development build: the client mod's global pack in local and remote-mod runs, and the pack the BDS
# host sends in remote modes, so a vanilla client loads it too) has an animation whose Molang does
# not parse (animations/gf_e2e_content_log_canary.animation.json, used by nothing). The client
# reports it as it loads the pack ("[Molang][error] ... gf_e2e_content_log_canary ..."). Its bone is
# named gfpreview_canary, so the models scenario's own filter (models.ps1, F2's) counts it among its
# canaries. Every game session of the run that closed normally (Stop-Game closed it, not killed)
# must have written a file that shows that line: no file, or files without it, mean the check cannot
# see that session's content log and fail the run; a file with only the canary means clean. The
# check is per session (a run may start several games: relaunches, ui's Vibrant Visuals half), so a
# session that wrote nothing is caught even when another session's file holds the canary, and a
# killed session waives the canary for itself only. This is more robust than counting a missing
# file as clean once content_log_file:1 was verified: it also catches a log written elsewhere
# (another data folder or file name), a game that never loaded the packs, and a game that stopped
# writing the file, none of which the options check can see.
$script:ContentLogSince  = $null
# The run's game sessions (Start-Game adds one, Stop-Game says how it ended).
$script:GameSessions = [System.Collections.Generic.List[object]]::new()
$script:ContentLogCanary = 'gf_e2e_content_log_canary'
# Entries that are not about this build's packs (regexes over the entry's text and continuation lines):
#   gf_e2e_content_log_canary  the harness canary above (required, never a failure)
#   gfpreview_canary  the models scenario's deliberately broken canary files (test-only preview
#                     pack; models.ps1 checks that the content log reports them)
#   diving_helmet     the game's own cached diving-helmet fog definitions (a vanilla pack in the
#                     packcache that fails the fog schema)
# A scenario may add its own ($script:ContentLogExpected += 'regex') for lines it causes on purpose.
$script:ContentLogExpected = @($script:ContentLogCanary, 'gfpreview_canary', 'fogs/(netherite_)?diving_helmet\.json')
# The run's summary lines (run.ps1): warnings, and the entries of the other levels (verbose, info).
$script:ContentLogWarnings = @()
$script:ContentLogNotes = @()

function Enable-ContentLog {
    $script:ContentLogSince = (Get-Date).AddSeconds(-2)
    $old = Set-RunOption 'content_log_file' '1'
    if ($old.Count -eq 0) { Write-Step 'warning: no options.txt to turn the content log on in' }
    else { Write-Step "content log on ($(Join-Path (Get-GameDataDir) 'logs'))" }
}

# The content-log files this run's game sessions wrote.
function Get-ContentLogFiles {
    if (-not $script:ContentLogSince) { return @() }
    $dir = Join-Path (Get-GameDataDir) 'logs'
    @(Get-ChildItem $dir -Filter 'ContentLog*.txt' -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -ge $script:ContentLogSince } | Sort-Object Name)
}

# The parser runs in C#: a broken pack can log hundreds of thousands of entries (one per particle
# frame), which took about 45 s through PowerShell objects. It keeps one record per distinct entry
# (category, level, text and continuation lines) with its count, plus the first entries as written.
if (-not ('GfE2E.ContentLog' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
namespace GfE2E {
public sealed class ContentLogEntry {
    public string File, Time, Category, Level, Text, More;
    public long Count;
    public bool Expected;
}
public sealed class ContentLogResult {
    public List<ContentLogEntry> Distinct = new List<ContentLogEntry>();
    public List<string> Raw = new List<string>();
    public long Total;
    // per file (full path): the entries that match the marker regex (the canary), 0 when none
    public Dictionary<string, long> MarkerByFile = new Dictionary<string, long>(StringComparer.OrdinalIgnoreCase);
}
public static class ContentLog {
    static readonly Regex Head = new Regex(@"^(\d\d:\d\d:\d\d)\[([^\]]*)\]\[(\w+)\]-(.*)$", RegexOptions.Compiled);
    public static ContentLogResult Parse(string[] files, string[] expected, int rawCap) { return Parse(files, expected, rawCap, null); }
    public static ContentLogResult Parse(string[] files, string[] expected, int rawCap, string marker) {
        var res = new ContentLogResult();
        var byKey = new Dictionary<string, ContentLogEntry>();
        var exp = new List<Regex>();
        foreach (var e in expected) exp.Add(new Regex(e));
        var mark = string.IsNullOrEmpty(marker) ? null : new Regex(marker);
        string file = null, path = null, time = null, cat = null, level = null, text = null;
        var more = new StringBuilder();
        Action flush = () => {
            if (text == null) return;
            string m = more.ToString().TrimEnd();
            res.Total++;
            if (mark != null && mark.IsMatch(text + " " + m)) res.MarkerByFile[path]++;
            if (res.Raw.Count < rawCap) res.Raw.Add(file + " " + time + " [" + cat + "][" + level + "] " + text + (m.Length > 0 ? "\n" + m : ""));
            string key = cat + "\u0001" + level + "\u0001" + text + "\u0001" + m;
            ContentLogEntry ent;
            if (!byKey.TryGetValue(key, out ent)) {
                ent = new ContentLogEntry { File = file, Time = time, Category = cat, Level = level, Text = text, More = m };
                string all = text + " " + m;
                foreach (var r in exp) if (r.IsMatch(all)) { ent.Expected = true; break; }
                byKey[key] = ent;
                res.Distinct.Add(ent);
            }
            ent.Count++;
            text = null;
            more.Clear();
        };
        foreach (var f in files) {
            path = f;
            file = Path.GetFileName(path);
            res.MarkerByFile[path] = 0;
            using (var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
            using (var rd = new StreamReader(fs, Encoding.UTF8)) {
                string line;
                while ((line = rd.ReadLine()) != null) {
                    var hm = Head.Match(line);
                    if (hm.Success) {
                        flush();
                        time = hm.Groups[1].Value; cat = hm.Groups[2].Value; level = hm.Groups[3].Value.ToLowerInvariant(); text = hm.Groups[4].Value;
                    } else if (text != null && line.Trim().Length > 0) {
                        more.AppendLine(line.TrimEnd());
                    }
                }
            }
            flush();
        }
        return res;
    }
}
}
'@
}

# This run's content logs: {Distinct = [{File, Time, Category, Level (error|warning|inform|...), Text,
# More, Count, Expected}] (first occurrence's file and time), Raw = the first entries as written, Total}.
$script:ContentLogRawCap = 2000
# MarkerByFile = per file, the entries that name the canary ($script:ContentLogCanary).
function Get-ContentLogEntries([string[]]$Files = @(Get-ContentLogFiles | ForEach-Object FullName)) {
    $files = @($Files | Where-Object { $_ })
    [GfE2E.ContentLog]::Parse([string[]]$files, [string[]]@($script:ContentLogExpected), $script:ContentLogRawCap, $script:ContentLogCanary)
}

# The game session (Start-Game's records, in launch order) a content-log file belongs to: the last
# session that started before the file was created (sessions never overlap: Stop-Game waits for the
# game to exit before the next launch). A file older than every session belongs to the first one.
function Get-ContentLogSession([System.IO.FileInfo]$File, [object[]]$Sessions) {
    $t = if ($File.CreationTime -lt $File.LastWriteTime) { $File.CreationTime } else { $File.LastWriteTime }
    $owner = $Sessions[0]
    # (no slack: Start is taken before the launch, so a session's file is never older than its Start,
    # while the previous session's file may be written just before it)
    foreach ($s in $Sessions) { if ($s.Start -le $t) { $owner = $s } }
    $owner
}

# One line per distinct entry text: "[Category][level] text (xN)" (continuation lines not shown).
function Format-ContentLogEntries($Entries) {
    $sum = [ordered]@{}
    foreach ($e in $Entries) { $k = "[$($e.Category)][$($e.Level)] $($e.Text)"; $sum[$k] = [long]$sum[$k] + $e.Count }
    @(foreach ($k in $sum.Keys) { if ($sum[$k] -gt 1) { "$k (x$($sum[$k]))" } else { $k } })
}

# Sum of the Count of entries.
function Measure-ContentLogCount($Entries) { [long](@($Entries) | Measure-Object -Property Count -Sum).Sum }

# Checks this run's content logs once the game closed: writes <out>\client-content-log.txt (every
# distinct entry with its count, then the first $script:ContentLogRawCap entries as written), prints
# a summary and returns the run's failures:
# - per game session (Start-Game's records): a session that closed normally ('closed') must have
#   written a content-log file that reports the canary; no file, or files without it, mean the check
#   cannot see that session's log. A session that was killed, or had exited by itself (a crash, the
#   watchdog), or never closed is listed and needs no canary (its file may be missing or cut short);
#   one killed session never waives the check for the run's other sessions;
# - every error (and with -Strict every warning) that is not in $script:ContentLogExpected.
# Warnings go to $script:ContentLogWarnings and the entries of the other levels (verbose, info) to
# $script:ContentLogNotes, for the run's summary; they never fail a run.
# -Files and -Sessions ([{Launch, Start, End, Ended (closed|killed|gone|open), Pid}]) replace this
# run's files from the game's logs folder and its sessions (the harness self-check, selfcheck.ps1).
function Get-ContentLogProblems([string]$OutDir, [switch]$Strict, [string[]]$Files, [object[]]$Sessions) {
    $script:ContentLogWarnings = @()
    $script:ContentLogNotes = @()
    $given = $PSBoundParameters.ContainsKey('Files')
    if (-not $PSBoundParameters.ContainsKey('Sessions')) {
        if ($given) { throw 'Get-ContentLogProblems: -Files needs -Sessions (the game sessions the files belong to)' }
        $Sessions = @($script:GameSessions)
    }
    $Sessions = @($Sessions | Where-Object { $_ })
    if (-not $given) {
        if (-not $script:ContentLogSince -or $Sessions.Count -eq 0) { return @() }
        $Files = @(Get-ContentLogFiles | ForEach-Object FullName)
    }
    if ($Sessions.Count -eq 0) { return @() }
    $files = @($Files | Where-Object { $_ })
    $where = if ($given) { 'the given files' } else { Join-Path (Get-GameDataDir) 'logs' }
    $log = Get-ContentLogEntries -Files $files
    $problems = [System.Collections.Generic.List[string]]::new()

    # ---- the canary, per session ----
    $bySession = @{}
    foreach ($f in $files) {
        $s = Get-ContentLogSession (Get-Item -LiteralPath $f) $Sessions
        if (-not $bySession.Contains($s.Launch)) { $bySession[$s.Launch] = [System.Collections.Generic.List[string]]::new() }
        $bySession[$s.Launch].Add($f)
    }
    $sessionLines = foreach ($s in $Sessions) {
        $mine = @(if ($bySession.Contains($s.Launch)) { $bySession[$s.Launch] })
        $n = [long]0
        foreach ($f in $mine) { $n += $log.MarkerByFile[$f] }
        $what = "game session $($s.Launch)$(if ($s.Pid) { " (pid $($s.Pid))" }), $($s.Ended)"
        $names = if ($mine) { ($mine | ForEach-Object { Split-Path -Leaf $_ }) -join ', ' } else { 'no file' }
        $line = "$what`: $names, canary x$n"
        Write-Step "content log: $line"
        $line
        if ($n -gt 0) { continue }
        if ($s.Ended -eq 'closed') {
            if (-not $mine) {
                $problems.Add("$what normally but wrote no content log in $where, not even the canary of GloamingForest_Test_RP ($script:ContentLogCanary): its content log is not captured (options.txt content_log_file:1 did not take effect, or the game never loaded the packs)")
            } else {
                $problems.Add("$what normally but its content log ($names) does not report the canary of GloamingForest_Test_RP ($script:ContentLogCanary): the game did not load the test pack or did not log it, so the check cannot see that session's pack errors")
            }
        } else {
            Write-Step "content log: game session $($s.Launch) was not closed normally (its log may be missing or cut short): no canary needed"
        }
    }

    # ---- the entries ----
    $distinct = @($log.Distinct)
    $isCanary = { param($e) "$($e.Text) $($e.More)" -match $script:ContentLogCanary }
    $canary   = @($distinct | Where-Object { & $isCanary $_ })
    $errors   = @($distinct | Where-Object { $_.Level -in 'error', 'fatal' -and -not $_.Expected })
    $warnings = @($distinct | Where-Object { $_.Level -eq 'warning' -and -not $_.Expected })
    $notes    = @($distinct | Where-Object { $_.Level -notin 'error', 'fatal', 'warning' -and -not $_.Expected })
    $expected = @($distinct | Where-Object { $_.Expected -and -not (& $isCanary $_) })
    if ($OutDir) {
        New-Item -ItemType Directory -Force $OutDir | Out-Null
        $out = [System.Collections.Generic.List[string]]::new()
        $out.Add("# $($log.Total) entries in $($files.Count) file(s): $(($files | ForEach-Object { Split-Path -Leaf $_ }) -join ', ')")
        foreach ($l in $sessionLines) { $out.Add("# $l") }
        $out.Add("# $($distinct.Count) distinct entries: count, first file and time, [category][level], text")
        foreach ($d in $distinct) {
            $tag = if (& $isCanary $d) { ' (canary)' } elseif ($d.Expected) { ' (expected)' } else { '' }
            $out.Add("x$($d.Count) $($d.File) $($d.Time) [$($d.Category)][$($d.Level)]$tag $($d.Text)$(if ($d.More) { "`n$($d.More)" })")
        }
        $out.Add("# the first $($log.Raw.Count) of $($log.Total) entries as written")
        foreach ($r in $log.Raw) { $out.Add($r) }
        Set-Content -Path (Join-Path $OutDir 'client-content-log.txt') -Value $out -Encoding UTF8
    }
    $nErr = Measure-ContentLogCount $errors; $nWarn = Measure-ContentLogCount $warnings; $nExp = Measure-ContentLogCount $expected
    $nNote = Measure-ContentLogCount $notes; $nCanary = Measure-ContentLogCount $canary
    Write-Step ("content log: {0} session(s), {1} file(s), {2} entries: canary x{3}, {4} error(s), {5} warning(s), {6} verbose/info{7}" -f $Sessions.Count, $files.Count, $log.Total, $nCanary, $nErr, $nWarn, $nNote,
        $(if ($nExp) { ", $nExp other expected (`$script:ContentLogExpected)" } else { '' }))
    $script:ContentLogWarnings = @(Format-ContentLogEntries $warnings)
    $script:ContentLogNotes = @(Format-ContentLogEntries $notes)
    if ($errors) {
        $lines = @(Format-ContentLogEntries $errors)
        $lines | Select-Object -First 20 | ForEach-Object { Write-Host "  ! content log: $_" -ForegroundColor Red }
        $problems.Add("$nErr content-log error(s) ($($lines.Count) distinct; see client-content-log.txt): $($lines[0])")
    }
    if ($Strict -and $warnings) {
        $problems.Add("$nWarn content-log warning(s) in a strict run ($($script:ContentLogWarnings.Count) distinct): $($script:ContentLogWarnings[0])")
    }
    return $problems.ToArray()
}

# ---------------------------------------------------------------------------------------
# Run output folder, and the processes a run starts (the remote modes' BDS host harness)
# ---------------------------------------------------------------------------------------
# Thrown by Clear-RunOutDir when a file of the folder stays in use (run.ps1: NOT RUN, exit 2).
if (-not ('GfE2E.OutDirLockedException' -as [type])) {
    Add-Type -TypeDefinition @'
namespace GfE2E {
public class OutDirLockedException : System.Exception {
    public OutDirLockedException(string message) : base(message) { }
}
}
'@
}

# Deletes a run's output folder. It is first renamed aside (<dir>.trash-<pid>-<n>-<ticks>, one atomic
# step that fails while any file in it is open), then the renamed copy is deleted: a folder that
# cannot be cleared keeps every file of the previous run (client.log, screenshots, the content-log
# report), never half of them. A file still in use (a host harness that is still exiting holds its
# redirected host-harness.log / .err, a viewer or another process reads a file) is retried for
# -TimeoutSec; then [GfE2E.OutDirLockedException]: the run could not start, NOT RUN (exit 2), never a
# failure of the scenario. Copies a run could not delete (a file opened with delete sharing) are
# deleted by a later run.
function Clear-RunOutDir([string]$Dir, [int]$TimeoutSec = 20) {
    $parent = Split-Path -Parent $Dir
    $leaf = Split-Path -Leaf $Dir
    foreach ($old in @(Get-ChildItem -LiteralPath $parent -Directory -Filter "$leaf.trash-*" -ErrorAction SilentlyContinue)) {
        Remove-Item -LiteralPath $old.FullName -Recurse -Force -ErrorAction SilentlyContinue
    }
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $tries = 0
    $aside = $null
    while (Test-Path -LiteralPath $Dir) {
        $tries++
        $to = Join-Path $parent "$leaf.trash-$PID-$tries-$([DateTime]::UtcNow.Ticks)"
        try { [IO.Directory]::Move($Dir, $to); $aside = $to; break }
        catch {
            if (-not (Test-Path -LiteralPath $Dir)) { break }
            if ((Get-Date) -ge $deadline) {
                throw [GfE2E.OutDirLockedException]::new("could not clear $Dir in $TimeoutSec s ($tries tries; a file is still in use; the folder was left as it is): $($_.Exception.Message)")
            }
            Start-Sleep -Milliseconds 500
        }
    }
    if ($tries -gt 1) { Write-Step "cleared $Dir after $tries tries (a file was still in use)" }
    if ($aside) {
        try { Remove-Item -LiteralPath $aside -Recurse -Force -ErrorAction Stop }
        catch { Write-Step "warning: could not delete $aside ($($_.Exception.Message)); a later run deletes it" }
    }
}

# The live descendants of a process (children, grandchildren, ...) as Process objects: a Process
# object keeps its handle, so waiting on it later is safe even when the id is reused meanwhile.
function Get-ProcessDescendants([int]$ProcessId) {
    # (a child is never older than its parent: an older process whose recorded parent id was reused
    # by a process of this tree is not ours, and must never be killed)
    $all = @(Get-CimInstance Win32_Process -Property ProcessId, ParentProcessId, CreationDate -ErrorAction SilentlyContinue)
    $born = @{}
    foreach ($c in $all) { $born[[int]$c.ProcessId] = $c.CreationDate }
    $seen = [System.Collections.Generic.HashSet[int]]::new()
    $queue = [System.Collections.Generic.Queue[int]]::new()
    $queue.Enqueue($ProcessId)
    $out = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()
    while ($queue.Count -gt 0) {
        $parent = $queue.Dequeue()
        foreach ($c in $all) {
            $id = [int]$c.ProcessId
            if ([int]$c.ParentProcessId -ne $parent -or $id -eq $ProcessId -or $id -eq 0) { continue }
            if ($born[$parent] -and $c.CreationDate -and $c.CreationDate -lt $born[$parent]) { continue }
            if (-not $seen.Add($id)) { continue }
            $queue.Enqueue($id)
            try { $out.Add([System.Diagnostics.Process]::GetProcessById($id)) } catch {}
        }
    }
    $out.ToArray()
}

# Kills a process and everything it started, and waits until all of them have exited (up to
# -TimeoutSec). Returns $true when the whole tree is gone. The descendants are listed before the
# kill: Process.Kill($true) does not wait for them, and one that is still exiting keeps the handles
# it inherited (a redirected output pipe, an open log file).
function Stop-ProcessTree([System.Diagnostics.Process]$Process, [int]$TimeoutSec = 30) {
    if (-not $Process) { return $true }
    $tree = @(Get-ProcessDescendants $Process.Id)
    try { if (-not $Process.HasExited) { $Process.Kill($true) } } catch {}
    # (a child started between the listing and the kill: Kill($true) killed it, wait for it too)
    $known = @($tree | ForEach-Object Id)
    foreach ($p in @(Get-ProcessDescendants $Process.Id)) { if ($p.Id -notin $known) { $tree += $p } }
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $ok = $true
    foreach ($p in @($Process) + $tree) {
        try {
            if (-not $p.HasExited) { try { $p.Kill() } catch {} }
            # (at least 2 s each: one slow process must not leave the others unwaited for)
            $left = [int][Math]::Max(2000, ($deadline - (Get-Date)).TotalMilliseconds)
            if (-not $p.WaitForExit($left)) { $ok = $false; Write-Step "warning: process $($p.Id) ($($p.ProcessName)) did not exit in $TimeoutSec s" }
        } catch {}
    }
    return $ok
}

# Redirects a started process's stdout and stderr (UseShellExecute = $false, both redirected) into
# files, unbuffered and readable meanwhile. Returns the copies for Close-RedirectedOutput.
function Start-RedirectedOutput([System.Diagnostics.Process]$Process, [string]$StdOutFile, [string]$StdErrFile) {
    @(foreach ($s in @(@($Process.StandardOutput, $StdOutFile), @($Process.StandardError, $StdErrFile))) {
        $fs = [IO.FileStream]::new($s[1], 'Create', 'Write', 'ReadWrite', 1)
        [pscustomobject]@{ Reader = $s[0]; Stream = $fs; Task = $s[0].BaseStream.CopyToAsync($fs) }
    })
}

# Closes what Start-RedirectedOutput opened: lets each copy finish (the process has exited: its pipe
# ends) for up to -TimeoutSec, then closes the pipe and the file, so the files can be deleted.
function Close-RedirectedOutput($Outputs, [int]$TimeoutSec = 10) {
    foreach ($o in @($Outputs)) {
        if (-not $o) { continue }
        try { [void]$o.Task.Wait([TimeSpan]::FromSeconds($TimeoutSec)) } catch {}
        try { $o.Reader.Dispose() } catch {}
        try { $o.Stream.Dispose() } catch {}
    }
}

# Copies of our mod that another harness parked in an aside folder of this instance
# (mods.<harness>-aside\GloamingForest, e.g. mods.bedd-it-aside on clone hi2) are stale builds: that
# harness would put them back into mods\ later. Every run deploys a fresh copy, so they are deleted.
function Remove-StaleModCopies {
    foreach ($d in Get-ChildItem $script:VersionDir -Directory -ErrorAction SilentlyContinue | Where-Object Name -match '^mods\..+-aside$') {
        if ($d.FullName -eq $script:AsideDir) { continue }
        $copy = Join-Path $d.FullName $script:ModName
        if (-not (Test-Path (Join-Path $copy "$script:ModName.dll"))) { continue }
        Remove-Item -Recurse -Force $copy -ErrorAction SilentlyContinue
        if (Test-Path $copy) { Write-Step "warning: could not delete the stale copy $copy" } else { Write-Step "deleted a stale copy of the mod: $copy" }
    }
}

function Get-WorldsDir {
    $data = Get-GameDataDir
    $dirs = @(Get-ChildItem (Join-Path $data 'Users') -Directory -ErrorAction SilentlyContinue |
        ForEach-Object { Join-Path $_.FullName 'games\com.mojang\minecraftWorlds' } |
        Where-Object { Test-Path $_ })
    # Prefer a real user folder over "Shared".
    $user = $dirs | Where-Object { $_ -notmatch '\\Shared\\' } | Select-Object -First 1
    if ($user) { return $user }
    return $dirs | Select-Object -First 1
}

function Deploy-Mod([string]$BinDir) {
    if (-not (Test-Path (Join-Path $BinDir "$script:ModName.dll"))) { throw "Build output not found: $BinDir (run 'pwsh tools/build.ps1 -Target client' or pass -Build)" }
    $dest = Join-Path $script:ModsDir $script:ModName
    # Fresh copy every run (no config/data carried over), so runs are reproducible. A game of
    # another harness that is still exiting can hold the old DLL for a while (Clear-InstanceGames
    # ran first): retry, and when it stays open nothing of the scenario can run (NOT RUN).
    for ($i = 0; $i -lt 15 -and (Test-Path $dest); $i++) {
        try { Remove-Item -Recurse -Force $dest -ErrorAction Stop } catch { $lastErr = $_; Start-Sleep -Seconds 2 }
    }
    if (Test-Path $dest) {
        $script:NotRunReason = "$dest is held open by another process (a game of another harness?): $lastErr"
        throw "could not replace the deployed mod: $lastErr"
    }
    Copy-Item -Recurse -Force $BinDir $dest
    Set-TestModConfig $dest
    Write-Step "deployed $BinDir -> $dest"
}

function Remove-DeployedMod {
    $dest = Join-Path $script:ModsDir $script:ModName
    if (Test-Path $dest) { Remove-Item -Recurse -Force $dest -ErrorAction SilentlyContinue; Write-Step "removed $dest" }
}

# Remove mods\BeDD (a leftover of a killed run, or this run's copy once the game is
# closed), retrying while the game still holds the DLL open.
function Remove-BeDDLeftover {
    $dest = Join-Path $script:ModsDir 'BeDD'
    for ($i = 0; $i -lt 10 -and (Test-Path $dest); $i++) {
        try { Remove-Item -Recurse -Force $dest -ErrorAction Stop } catch { Start-Sleep -Seconds 2 }
    }
    if (Test-Path $dest) { Write-Host "[e2e] FAILED to remove $dest; delete it by hand" -ForegroundColor Red }
}

# Move every mod except LeviLamina/CrashLogger/ours (e.g. BEI) out of mods\ so it cannot draw on
# or react to our screens. Restore-OtherMods puts them back; it also runs at the start of each
# run, so a run that was killed half-way is repaired by the next one.
function Move-OtherModsAside {
    New-Item -ItemType Directory -Force $script:AsideDir | Out-Null
    foreach ($m in Get-ChildItem $script:ModsDir -Directory) {
        if ($m.Name -in $script:KeepMods) { continue }
        Move-Item $m.FullName (Join-Path $script:AsideDir $m.Name)
        Write-Step "moved mod '$($m.Name)' aside"
    }
}

function Restore-OtherMods {
    if (-not (Test-Path $script:AsideDir)) { return }
    foreach ($m in Get-ChildItem $script:AsideDir -Directory | Where-Object Name -ne '.stale') {
        $dst = Join-Path $script:ModsDir $m.Name
        if (Test-Path $dst) {
            # Somebody (e.g. BEI's harness) re-deployed the mod while our copy was aside: the
            # aside copy is stale. Keep it out of the way instead of deleting it.
            $stale = Join-Path $script:AsideDir ".stale\$($m.Name)-$(Get-Date -Format yyyyMMddHHmmss)"
            New-Item -ItemType Directory -Force (Split-Path $stale) | Out-Null
            Move-Item $m.FullName $stale
            Write-Step "warning: mods\$($m.Name) was re-created meanwhile; moved the old aside copy to $stale"
            continue
        }
        Move-Item $m.FullName $dst
        Write-Step "restored mod '$($m.Name)'"
    }
    if (-not (Get-ChildItem $script:AsideDir -Force)) { Remove-Item $script:AsideDir -Force }
}

# The game caches server packs by uuid + version (games\com.mojang\...\packcache). Builds of other
# branches can carry the same version, so a cached GloamingForest pack could hide this build's: drop every
# cached copy of our packs (matched by the header uuids of assets/pack/*/manifest.json) before a launch.
function Clear-CachedGfPacks {
    $uuids = @(foreach ($m in @(Get-ChildItem (Join-Path $script:RepoRoot 'assets/pack'), (Join-Path $script:RepoRoot 'assets/test') -Directory -ErrorAction SilentlyContinue | ForEach-Object { Join-Path $_.FullName 'manifest.json' })) {
        if (Test-Path $m) { (Get-Content $m -Raw | ConvertFrom-Json).header.uuid }
    })
    if (-not $uuids) { return }
    $data = Get-GameDataDir
    if (-not $data -or -not (Test-Path $data)) { return }
    foreach ($cache in Get-ChildItem -Recurse -Directory -Path $data -Filter 'packcache' -ErrorAction SilentlyContinue) {
        foreach ($pack in Get-ChildItem -Directory -Path $cache.FullName -ErrorAction SilentlyContinue | Get-ChildItem -Directory -ErrorAction SilentlyContinue) {
            $manifest = Join-Path $pack.FullName 'manifest.json'
            if (-not (Test-Path $manifest)) { continue }
            try { $uuid = (Get-Content $manifest -Raw | ConvertFrom-Json).header.uuid } catch { continue }
            if ($uuids -contains $uuid) {
                Remove-Item -Recurse -Force $pack.FullName -ErrorAction SilentlyContinue
                Write-Step "dropped cached GloamingForest pack $($pack.FullName)"
            }
        }
    }
}

# Install the GF-Test world from fixtures\GF-Test (a level.dat template; the game generates the
# terrain on first load). LastPlayed is set to now so it is the first world in the list.
function Restore-TestWorld {
    . "$PSScriptRoot/../common/nbt.ps1"
    $src = Join-Path $PSScriptRoot "fixtures\$script:WorldName"
    $worlds = Get-WorldsDir
    if (-not $worlds) { throw "worlds dir not found under $(Get-GameDataDir) (start the game once by hand and sign in)" }
    Remove-TestWorld
    $target = Join-Path $worlds $script:WorldName
    if (Test-Path $target) { throw "could not delete the previous $target (is a file still open?)" }
    Copy-Item -Recurse $src $target
    $ld = Read-LevelDat (Join-Path $target 'level.dat')
    Set-NbtValue $ld.Root 'LastPlayed' 4 ([long][DateTimeOffset]::UtcNow.ToUnixTimeSeconds())
    Write-LevelDat $ld (Join-Path $target 'level.dat')
    Write-Step "restored world fixture to $target"
}

# Delete every world called GF-Test. Run after each run so that it never shows up as the most
# recently played world in BEI's harness (which opens the first world in the list).
function Remove-TestWorld {
    $worlds = Get-WorldsDir
    if (-not $worlds) { return }
    Get-ChildItem $worlds -Directory | Where-Object {
        $_.Name -eq $script:WorldName -or (Get-Content (Join-Path $_.FullName 'levelname.txt') -ErrorAction SilentlyContinue) -eq $script:WorldName
    } | ForEach-Object { Remove-Item -Recurse -Force $_.FullName -ErrorAction SilentlyContinue }
}

# ---------------------------------------------------------------------------------------
# Game process / window
# ---------------------------------------------------------------------------------------
# The games of this slot's instance only (other slots may run games of their own): matched by the
# exe's folder, since every instance runs a Minecraft.Windows.exe.
function Get-InstanceProcesses {
    $dir = $script:VersionDir.TrimEnd('\') + '\'
    Get-Process -Name 'Minecraft.Windows' -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -and $_.Path.StartsWith($dir, [StringComparison]::OrdinalIgnoreCase) }
}
function Get-GameProcess { Get-InstanceProcesses | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1 }

# Before a launch, with the instance's game lock held: a game of this instance still running (another
# harness's, closing after it gave the lock back, or one left by a killed run) keeps the mods' DLLs
# open. Close it, give it 60 s to exit, then kill its process tree.
function Clear-InstanceGames {
    $procs = @(Get-InstanceProcesses)
    if (-not $procs) { return }
    Write-Step "a game of this instance is still running (pid $(($procs | ForEach-Object Id) -join ', ')); closing it first"
    foreach ($p in $procs) {
        try { $p.CloseMainWindow() | Out-Null } catch {}
        if (-not $p.WaitForExit(60000)) {
            try { $p.Kill($true) } catch {}
            $p.WaitForExit(15000) | Out-Null
            Write-Step "killed pid $($p.Id) (it did not exit within 60 s)"
        }
    }
}

function Start-Game([int]$TimeoutSeconds = 180) {
    if (Get-GameProcess) { throw "Minecraft is already running; close it first" }
    # latest.log is appended across sessions: remember where this session starts.
    Start-LogSession
    # one record per game session, for the content-log check (Stop-Game says how it ended)
    $script:GameSessions.Add([pscustomobject]@{ Launch = $script:GameSessions.Count + 1; Start = (Get-Date); End = $null; Ended = 'open'; Pid = $null })
    if ($script:Headless) {
        # Straight from the instance folder (the exe loads PreLoader/LeviLamina itself), with a
        # window that is shown without being activated; no launcher window either.
        Write-Step "launching $script:VersionName in the background"
        & (Join-Path $script:BeDDDir 'Start-BeDDGame.ps1') -Instance $script:VersionName | Out-Null
    } else {
        if (-not (Test-Path $script:Launcher)) { throw "LeviLauncher not found at $script:Launcher (set `$env:LV_E2E_LAUNCHER)" }
        # A leftover launcher instance swallows --launch (single-instance hand-off) without
        # starting the game, so close any running launcher first.
        Get-Process -Name 'LeviLauncher' -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 1
        Write-Step "launching $script:VersionName via LeviLauncher"
        Start-Process -FilePath $script:Launcher -ArgumentList "--launch=`"$script:VersionName`""
    }
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $p = Get-GameProcess
        if ($p) { $script:Game = $p; $script:GameSessions[-1].Pid = $p.Id; Write-Step "game window found (pid $($p.Id))"; return $p }
        Start-Sleep -Seconds 2
    }
    throw "Game window did not appear within $TimeoutSeconds s"
}

function Stop-Game {
    # Closed, not killed, when it can be: the game writes its content log (Get-ContentLogEntries)
    # only as it closes. The session's record (Start-Game) says how it ended, for the content-log
    # check: 'closed' (it must have logged the canary), 'killed' (its file may be missing or cut
    # short), or 'gone' (it had exited by itself: a crash or the watchdog).
    $procs = @(Get-InstanceProcesses)
    $killed = $false
    foreach ($p in $procs) {
        $p.CloseMainWindow() | Out-Null
        if (-not $p.WaitForExit(30000)) { $p.Kill($true); $killed = $true; Write-Step "the game did not close within 30 s; killed it" }
    }
    $s = $script:GameSessions | Where-Object { $_.Ended -eq 'open' } | Select-Object -Last 1
    if ($s) {
        $s.End = Get-Date
        $s.Ended = if ($killed) { 'killed' } elseif ($procs.Count -eq 0 -or (Get-WatchdogFired)) { 'gone' } else { 'closed' }
    }
    if (-not $script:Headless) { Get-Process -Name 'LeviLauncher' -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue }
    Write-Step "game stopped"
}

function Get-GameWindow {
    $p = Get-GameProcess
    if (-not $p) { throw "Minecraft is not running" }
    return $p.MainWindowHandle
}

# Fixed window size so GUI scale and click coordinates are reproducible across runs.
$script:WindowW = 1600
$script:WindowH = 900

function Focus-Game {
    $h = Get-GameWindow
    if ([BsE2E.Native]::GetForegroundWindow() -eq $h) { return }
    # Keep the game above other windows so synthesized clicks land on it, then click its
    # title bar: a (synthesized) user click is allowed to change the foreground window even
    # when SetForegroundWindow is blocked by the focus-stealing rules.
    [BsE2E.Native]::SetWindowPos($h, [IntPtr]::new(-1), 0, 0, 0, 0, 0x0001 -bor 0x0002 -bor 0x0040) | Out-Null
    $wr = New-Object BsE2E.Native+RECT; [BsE2E.Native]::GetWindowRect($h, [ref]$wr) | Out-Null
    $c = Get-ClientRect
    if ($c.Y - $wr.Top -gt 4) {
        [BsE2E.Native]::SetCursorPos([int](($wr.Left + $wr.Right) / 2), [int](($wr.Top + $c.Y) / 2)) | Out-Null
        [BsE2E.Native]::MouseButton(0, $false); Start-Sleep -Milliseconds 50; [BsE2E.Native]::MouseButton(0, $true)
        Start-Sleep -Milliseconds 300
    }
    for ($i = 0; $i -lt 5; $i++) {
        [BsE2E.Native]::ForceForeground($h)
        Start-Sleep -Milliseconds 300
        if ([BsE2E.Native]::GetForegroundWindow() -eq $h) { return }
    }
    Write-Step "warning: could not bring the game window to the foreground"
}

function Set-GameWindowSize([int]$ClientW = $script:WindowW, [int]$ClientH = $script:WindowH) {
    $h = Get-GameWindow
    [BsE2E.Native]::ShowWindow($h, 9) | Out-Null  # restore (un-maximize)
    $wr = New-Object BsE2E.Native+RECT; [BsE2E.Native]::GetWindowRect($h, [ref]$wr) | Out-Null
    $c = Get-ClientRect
    $extraW = ($wr.Right - $wr.Left) - $c.W; $extraH = ($wr.Bottom - $wr.Top) - $c.H
    [BsE2E.Native]::SetWindowPos($h, [IntPtr]::Zero, 40, 40, $ClientW + $extraW, $ClientH + $extraH, 0x0040) | Out-Null
    Start-Sleep -Milliseconds 800
}

function Get-ClientRect {
    $h = Get-GameWindow
    $r = New-Object BsE2E.Native+RECT
    [BsE2E.Native]::GetClientRect($h, [ref]$r) | Out-Null
    $pt = New-Object BsE2E.Native+POINT
    [BsE2E.Native]::ClientToScreen($h, [ref]$pt) | Out-Null
    [pscustomobject]@{ X = $pt.X; Y = $pt.Y; W = $r.Right - $r.Left; H = $r.Bottom - $r.Top }
}

function ConvertTo-Screen([double]$x, [double]$y, [switch]$Relative) {
    $c = Get-ClientRect
    if ($Relative) { $x = $x * $c.W; $y = $y * $c.H }
    [pscustomobject]@{ X = [int]($c.X + $x); Y = [int]($c.Y + $y) }
}

# ---------------------------------------------------------------------------------------
# Input
# ---------------------------------------------------------------------------------------
# Safety: never send synthesized input unless Minecraft is the foreground window, so keys and
# clicks can never land in another application.
function Assert-GameForeground {
    $h = Get-GameWindow
    if ([BsE2E.Native]::GetForegroundWindow() -ne $h) {
        Focus-Game
        if ([BsE2E.Native]::GetForegroundWindow() -ne $h) { throw "Minecraft is not the foreground window; refusing to send input (a UAC/firewall dialog may be in front)" }
    }
}

function Move-Mouse([double]$x, [double]$y, [switch]$Relative) {
    Assert-GameForeground
    $p = ConvertTo-Screen $x $y -Relative:$Relative
    [BsE2E.Native]::SetCursorPos($p.X, $p.Y) | Out-Null
    # A tiny relative move makes the game register the new position.
    [BsE2E.Native]::MoveRelative(1, 0); [BsE2E.Native]::MoveRelative(-1, 0)
    Start-Sleep -Milliseconds 120
}

function Click([double]$x, [double]$y, [switch]$Relative, [switch]$Right) {
    Move-Mouse $x $y -Relative:$Relative
    $b = if ($Right) { 1 } else { 0 }
    [BsE2E.Native]::MouseButton($b, $false); Start-Sleep -Milliseconds 60; [BsE2E.Native]::MouseButton($b, $true)
    Start-Sleep -Milliseconds 250
}

function Scroll([int]$notches) { Assert-GameForeground; [BsE2E.Native]::Wheel(120 * $notches); Start-Sleep -Milliseconds 200 }

$script:VK = @{
    ESC = 0x1B; ENTER = 0x0D; BACK = 0x08; TAB = 0x09; SPACE = 0x20; SHIFT = 0x10; CTRL = 0x11
    LEFT = 0x25; UP = 0x26; RIGHT = 0x27; DOWN = 0x28; F1 = 0x70; F2 = 0x71; F3 = 0x72
}

function Press-Key([string]$key, [int]$holdMs = 60) {
    Assert-GameForeground
    $vk = if ($script:VK.ContainsKey($key.ToUpper())) { $script:VK[$key.ToUpper()] } else { [int][char]$key.ToUpper() }
    [BsE2E.Native]::Key([uint16]$vk, $false); Start-Sleep -Milliseconds $holdMs; [BsE2E.Native]::Key([uint16]$vk, $true)
    Start-Sleep -Milliseconds 200
}

function Type-Text([string]$text) {
    Assert-GameForeground
    foreach ($c in $text.ToCharArray()) { [BsE2E.Native]::Unicode($c); Start-Sleep -Milliseconds 40 }
    Start-Sleep -Milliseconds 200
}

# ---------------------------------------------------------------------------------------
# Screenshots and logs
# ---------------------------------------------------------------------------------------
# Saves out\<unit>\<name>.png (unit defaults to the running scenario), downscaled to 1280 wide.
function Save-Screenshot([string]$name, [string]$unit = $script:Unit) {
    # PrintWindow waits for the game's window: never for ever (a hung game is killed, see Invoke-Guarded)
    Invoke-Guarded "the screenshot '$name'" { Save-ScreenshotUnguarded $name $unit }
}

function Save-ScreenshotUnguarded([string]$name, [string]$unit) {
    $c = Get-ClientRect
    $dir = Join-Path $script:OutRoot $unit
    New-Item -ItemType Directory -Force $dir | Out-Null
    # Capture the window itself (works even when covered by other windows), then crop to
    # the client area.
    $h = Get-GameWindow
    $wr = New-Object BsE2E.Native+RECT; [BsE2E.Native]::GetWindowRect($h, [ref]$wr) | Out-Null
    $full = New-Object System.Drawing.Bitmap ($wr.Right - $wr.Left), ($wr.Bottom - $wr.Top)
    $gf = [System.Drawing.Graphics]::FromImage($full)
    $hdc = $gf.GetHdc()
    [BsE2E.Native]::PrintWindow($h, $hdc, 2) | Out-Null   # PW_RENDERFULLCONTENT
    $gf.ReleaseHdc($hdc); $gf.Dispose()
    $bmp = New-Object System.Drawing.Bitmap $c.W, $c.H
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.DrawImage($full, (New-Object System.Drawing.Rectangle 0, 0, $c.W, $c.H), (New-Object System.Drawing.Rectangle ($c.X - $wr.Left), ($c.Y - $wr.Top), $c.W, $c.H), 'Pixel')
    $full.Dispose()
    $scale = [Math]::Min(1.0, 1280.0 / $c.W)
    $out = New-Object System.Drawing.Bitmap ([int]($c.W * $scale)), ([int]($c.H * $scale))
    $g2 = [System.Drawing.Graphics]::FromImage($out)
    $g2.InterpolationMode = 'HighQualityBicubic'
    $g2.DrawImage($bmp, 0, 0, $out.Width, $out.Height)
    $path = Join-Path $dir "$name.png"
    $out.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $g2.Dispose(); $bmp.Dispose(); $out.Dispose()
    Write-Step "screenshot $path"
    return $path
}

# Session log: LeviLamina appends logs\latest.log across game sessions. Start-LogSession (called
# by Start-Game) remembers the current end of the file; Get-LeviLog then reads only what was
# appended since, incrementally (byte offset), into $script:LogLines.
$script:LogFile    = Join-Path $script:VersionDir 'logs\latest.log'
$script:LogLines   = [System.Collections.Generic.List[string]]::new()
$script:LogPos     = 0L
$script:LogTail    = ''
$script:LogDecoder = [Text.Encoding]::UTF8.GetDecoder()

function Start-LogSession {
    $script:LogFile = Join-Path $script:VersionDir 'logs\latest.log'   # this slot's instance
    $script:ClientScreen = $null
    $script:LogLines.Clear(); $script:LogTail = ''; $script:LogDecoder = [Text.Encoding]::UTF8.GetDecoder()
    $script:LogPos = if (Test-Path $script:LogFile) { (Get-Item $script:LogFile).Length } else { 0L }
}

# Remote modes: the BDS console log (tools/e2e/server/out/client-host-<unit>/server.log) is merged into
# the session log (lines prefixed "[host] "), so scenarios wait for server-side lines the same
# way in every mode.
$script:HostLog    = $null
$script:HostLogPos = 0L
$script:HostTail   = ''

function Update-HostLog {
    if (-not $script:HostLog -or -not (Test-Path $script:HostLog)) { return }
    $fs = [System.IO.File]::Open($script:HostLog, 'Open', 'Read', 'ReadWrite')
    try {
        if ($fs.Length -lt $script:HostLogPos) { $script:HostLogPos = 0L }
        if ($fs.Length -eq $script:HostLogPos) { return }
        $fs.Position = $script:HostLogPos
        $buf = New-Object byte[] ($fs.Length - $script:HostLogPos)
        $n = $fs.Read($buf, 0, $buf.Length)
        $script:HostLogPos += $n
    } finally { $fs.Dispose() }
    $parts = ($script:HostTail + [Text.Encoding]::UTF8.GetString($buf, 0, $n)) -split "`n"
    $script:HostTail = $parts[-1]
    for ($i = 0; $i -lt $parts.Count - 1; $i++) {
        $l = $parts[$i].TrimEnd("`r")
        if ($l -ne '') { $script:LogLines.Add("[host] $l") }
    }
}

function Update-LeviLog {
    Update-HostLog
    if (-not (Test-Path $script:LogFile)) { return }
    # The game keeps the file open for writing: read with FileShare.ReadWrite.
    $fs = [System.IO.File]::Open($script:LogFile, 'Open', 'Read', 'ReadWrite')
    try {
        # A shorter file means LeviLamina started a new one (rotation): read it from the start.
        if ($fs.Length -lt $script:LogPos) { $script:LogPos = 0L; $script:LogDecoder = [Text.Encoding]::UTF8.GetDecoder() }
        if ($fs.Length -eq $script:LogPos) { return }
        $fs.Position = $script:LogPos
        $buf = New-Object byte[] ($fs.Length - $script:LogPos)
        $n = $fs.Read($buf, 0, $buf.Length)
        $script:LogPos += $n
    } finally { $fs.Dispose() }
    $chars = New-Object char[] ($script:LogDecoder.GetCharCount($buf, 0, $n))
    $script:LogDecoder.GetChars($buf, 0, $n, $chars, 0) | Out-Null
    $parts = ($script:LogTail + [string]::new($chars)) -split "`n"
    $script:LogTail = $parts[-1]
    for ($i = 0; $i -lt $parts.Count - 1; $i++) {
        $l = $parts[$i].TrimEnd("`r")
        if ($l -eq '') { continue }
        $script:LogLines.Add($l)
        if ($l -match 'client screen: (\S+)') { $script:ClientScreen = $Matches[1] }
    }
}

# Lines of latest.log written by the current game session (-Since <mark>: only lines after
# Get-LogMark returned <mark>; -All: the whole file, all sessions).
function Get-LeviLog([switch]$All, [int]$Since = 0) {
    if ($All) {
        if (-not (Test-Path $script:LogFile)) { return @() }
        $fs = [System.IO.File]::Open($script:LogFile, 'Open', 'Read', 'ReadWrite')
        try { $text = (New-Object System.IO.StreamReader($fs, [Text.Encoding]::UTF8)).ReadToEnd() } finally { $fs.Dispose() }
        return @($text -split "`r?`n" | Where-Object { $_ -ne '' })
    }
    Update-LeviLog
    if ($Since -ge $script:LogLines.Count) { return @() }
    return $script:LogLines.GetRange($Since, $script:LogLines.Count - $Since).ToArray()
}

# Same name as the server helper, so scenarios read alike.
function Get-ServerLog([int]$Since = 0) { Get-LeviLog -Since $Since }

# Current position in this session's log; pass it to Wait-LogLine/Get-LeviLog -Since.
function Get-LogMark { Update-LeviLog; $script:LogLines.Count }

# Wait for a line matching the regex in this session's log (from -Since). Returns the last match.
function Wait-LogLine([string]$pattern, [double]$TimeoutSeconds = 120, [int]$Since = 0, [int]$PollMs = 1000) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ($true) {
        $hit = Get-LeviLog -Since $Since | Select-String -Pattern $pattern | Select-Object -Last 1
        if ($hit) { return $hit.Line }
        if (-not (Get-GameProcess)) { throw "Game exited while waiting for log '$pattern'" }
        if ((Get-Date) -gt $deadline) { throw "Timed out after $TimeoutSeconds s waiting for log line '$pattern'" }
        Start-Sleep -Milliseconds $PollMs
    }
}

# Client trace lines (src/client/trace/ClientTrace.cpp): the client mod logs the top screen when it
# changes ("client screen: chat_screen") and every teleport of the local player ("client teleported
# to x y z"). Not available in remote-vanilla (no client mod): helpers fall back to fixed waits.
function Test-ClientTrace { $script:Mode -ne 'remote-vanilla' }

# The client's top screen as last traced ($null: unknown).
function Get-ClientScreen { Update-LeviLog; $script:ClientScreen }
$script:ClientScreen = $null

# Waits until the HUD shows (the world is ready for chat and clicks) before a scenario starts.
function Wait-HudReady([int]$TimeoutSeconds = 60) {
    if (-not (Test-ClientTrace)) { Start-Sleep -Seconds 3; return }
    try {
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while ((Get-ClientScreen) -ne 'hud_screen') {
            if ((Get-Date) -gt $deadline) { throw "the HUD did not show within $TimeoutSeconds s (screen: $(Get-ClientScreen))" }
            if (-not (Get-GameProcess)) { throw 'the game exited' }
            Start-Sleep -Milliseconds 250
        }
        Write-Step 'HUD ready'
    } catch { Write-Step "warning: $_" }
    Start-Sleep -Milliseconds 500
}

# Used by Invoke-SelfTest (common.ps1): commands are typed into chat.
function Send-SelfTestCommand([string]$Command) { Invoke-Chat "/$Command" }
function Assert-NoErrors {
    $bad = Get-ErrorLines
    if ($bad) { $bad | Select-Object -First 20 | ForEach-Object { Write-Host "  ! $_" -ForegroundColor Red }; throw "$($bad.Count) ERR/FTL line(s) in the log" }
    Write-Step "no errors in log"
}

# ---------------------------------------------------------------------------------------
# Menu navigation (calibrated for a 1600x900 client area; coordinates are fractions)
# ---------------------------------------------------------------------------------------
# Seconds a game may take from its window to "GloamingForest enabled" ($env:GF_E2E_ENABLE_TIMEOUT_SEC).
$script:EnableTimeoutSec = if ($env:GF_E2E_ENABLE_TIMEOUT_SEC -match '^\d+$') { [int]$env:GF_E2E_ENABLE_TIMEOUT_SEC } else { 180 }

# A hang the harness saw itself (no pipe call blocked): kills this instance's game through the
# watchdog, so the run ends NOT RUN with a "WATCHDOG:" line like a hung pipe call.
function Stop-HungGame([string]$What) { [GfE2E.Watchdog]::Trip($What) }

# CPU seconds the game used during a sample of -Seconds ($null: no game).
function Measure-GameCpu([int]$Seconds = 10) {
    $p = Get-GameProcess
    if (-not $p) { return $null }
    $t0 = $p.TotalProcessorTime
    Start-Sleep -Seconds $Seconds
    $p.Refresh()
    if ($p.HasExited) { return $null }
    return [Math]::Round(($p.TotalProcessorTime - $t0).TotalSeconds, 2)
}

# Waits for "GloamingForest enabled" (or LeviLamina's failure line). A game that is still running
# when the time is up is hung in "Enabling mods" (W0c: one sat there with no CPU use and held the
# harness for more than 10 minutes): idle (under 0.5 s of CPU in 10 s) it is killed at once; busy
# (a loaded machine) it gets 120 s more, then it is killed too. Either way the run is NOT RUN.
function Wait-ModEnabled([int]$TimeoutSeconds, [int]$Since = 0) {
    $pattern = "$script:ModName enabled|Failed to (load|enable) mod $script:ModName"
    $isTimeout = { param($e) "$e" -match '^Timed out after' -and (Get-GameProcess) }
    try { return Wait-LogLine $pattern $TimeoutSeconds -Since $Since } catch { if (-not (& $isTimeout $_)) { throw } }
    $cpu = Measure-GameCpu 10
    if ($null -eq $cpu) { throw "the game exited while enabling mods" }
    if ($cpu -lt 0.5) {
        Stop-HungGame "game hung while enabling mods: no '$script:ModName enabled' within $TimeoutSeconds s, and the game used $cpu s of CPU in 10 s"
        throw "the game hung while enabling mods"
    }
    Write-Step "no '$script:ModName enabled' within $TimeoutSeconds s, but the game is busy ($cpu s of CPU in 10 s); waiting 120 s more"
    try { return Wait-LogLine $pattern 120 -Since $Since } catch { if (-not (& $isTimeout $_)) { throw } }
    Stop-HungGame "game hung while enabling mods: no '$script:ModName enabled' within $($TimeoutSeconds + 130) s (busy)"
    throw "the game hung while enabling mods"
}

# Test switch: $env:GF_E2E_TEST_HANG = 'enable' (freeze the game before "GloamingForest enabled" is
# awaited) or 'pipe' (freeze it right before the scenario, so its first pipe call or capture hangs).
# The game is suspended (every thread stops: a real hang), so the watchdog's NOT RUN paths can be
# run on purpose (docs/spec/e2e.md, Hangs). Update-HangRecord leaves out only a run that reached
# the freeze point ($script:TestHang); a real hang before it counts like any other.
function Invoke-TestHang([string]$Stage) {
    if ($env:GF_E2E_TEST_HANG -ne $Stage) { return }
    $script:TestHang = $true
    foreach ($p in @(Get-InstanceProcesses)) {
        Write-Step "TEST (GF_E2E_TEST_HANG=$Stage): freezing the game (pid $($p.Id))"
        [GfE2E.Watchdog]::Suspend($p.Id)
    }
}

function Wait-GameReady([int]$TimeoutSeconds = $script:EnableTimeoutSec, [switch]$WithoutMod) {
    try { Set-GameWindowSize }
    catch {
        # BeDD (a mod too) never answered: a game that is still running is stuck while loading
        if (-not (Get-WatchdogFired) -and "$_" -match 'did not answer within' -and (Get-GameProcess)) { Stop-HungGame "the game still running but BeDD's pipe never answered while loading ($($_.Exception.Message))" }
        throw
    }
    if ($WithoutMod) {
        # With no mods at all LeviLamina logs "0 mod(s) loaded" and never "enabled"; that early line
        # is sometimes missed after a game restart, so a running game counts as ready after 90 s.
        try {
            Wait-LogLine '\[LeviLamina\] (\d+ mod\(s\) enabled|0 mod\(s\) loaded)' ([Math]::Min(90, $TimeoutSeconds)) | Out-Null
        } catch {
            if (-not (Get-GameProcess)) { throw }
            Write-Step "no LeviLamina mod line in the log; the game is running, continuing"
        }
    } else {
        $since = 0
        if ($env:GF_E2E_TEST_HANG -eq 'enable') { $since = Get-LogMark; Invoke-TestHang 'enable' }
        $hit = Wait-ModEnabled $TimeoutSeconds $since
        if ($hit -notmatch "$script:ModName enabled") { throw "LeviLamina failed to load $($script:ModName): $hit" }
    }
    # The title screen takes a while after mods are enabled.
    Start-Sleep -Seconds 15
    Focus-Game
    # First-launch sign-in nag: "Maybe later" (harmless click if absent: empty area).
    Click 0.781 0.775 -Relative
    Start-Sleep -Seconds 2
}

# Whether the title screen is showing: the green Play button at its place (headless runs read the
# game's picture; without one, $null = unknown).
function Test-TitleScreen {
    if (-not (Get-Command Get-ClientBitmap -ErrorAction SilentlyContinue)) { return $null }
    try { $bmp = Invoke-Guarded 'the title screen capture' { Get-ClientBitmap } } catch { return $null }
    if (-not $bmp) { return $null }
    try {
        $green = 0
        foreach ($fx in 0.42, 0.46, 0.54, 0.58) {
            foreach ($fy in 0.455, 0.49) {
                $c = $bmp.GetPixel([int]($bmp.Width * $fx), [int]($bmp.Height * $fy))
                if ($c.G -gt 90 -and $c.G -gt 1.4 * $c.R -and $c.G -gt 1.4 * $c.B) { $green++ }
            }
        }
        return $green -ge 6
    } finally { $bmp.Dispose() }
}

# Clicks Play on the title screen until the title screen is gone. A click while the title screen
# is still loading (or before the game has any mouse reading) is ignored by the game: runs on
# busy machines used to stay on the title screen with Play highlighted.
function Enter-PlayScreen([int]$Attempts = 6) {
    # the title screen first (it takes a while after "enabled" on a busy machine)
    $deadline = (Get-Date).AddSeconds(60)
    while ((Test-TitleScreen) -eq $false -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 1 }
    for ($i = 1; $i -le $Attempts; $i++) {
        $title = Test-TitleScreen
        if ($i -gt 1 -and $title -eq $false) { return }
        if ($i % 2 -eq 0) {
            # Play is highlighted (keyboard focus) when the game ignores the virtual mouse:
            # Enter activates it; a small camera move first gives GameInput a fresh mouse reading
            if (Get-Command Send-BeDDRaw -ErrorAction SilentlyContinue) { try { Send-BeDDRaw move -Dx 2 | Out-Null } catch {} }
            Press-Key ENTER
        } else {
            Move-Mouse 0.498 0.474 -Relative
            Start-Sleep -Milliseconds 300
            Click 0.498 0.474 -Relative     # Play
        }
        Start-Sleep -Seconds 3
        $title = Test-TitleScreen
        if ($null -eq $title -or -not $title) { return }  # unknown (foreground runs) or left the title screen
        Write-Step "still on the title screen after clicking Play (attempt $i/$Attempts); clicking again"
    }
    $state = try { Get-BeDDState | ConvertTo-Json -Compress -Depth 4 } catch { "unavailable: $_" }
    Write-Step "BeDD state: $state"
    throw "the title screen ignored $Attempts clicks / Enter on Play (game input, not the mod; BeDD state above)"
}

# Thrown by Enter-TestWorld when the game is in another world than the scenario's GF-Test (run.ps1
# then restarts the game once and reports NOT RUN the second time: nothing of the scenario ran).
if (-not ('GfE2E.WrongWorldException' -as [type])) {
    Add-Type -TypeDefinition @'
namespace GfE2E {
public class WrongWorldException : System.Exception {
    public WrongWorldException(string message) : base(message) { }
}
}
'@
}

# Where the game went since log mark $Since: $null when it is the GF-Test world, else why not. The mod
# logs "world: level '<LevelName>' opened" as the integrated server opens a world (src/game/WorldDb.cpp);
# a "[BeDD] join: connecting to" line means a pipe client of another harness sent the game to a server
# (no local-mode run joins one).
function Get-WrongWorld([int]$Since, [int]$TimeoutSeconds = 15) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ($true) {
        $log = @(Get-LeviLog -Since $Since)
        # any time in this game session (a stray join may come before the harness clicked anything)
        $join = @(Get-LeviLog) | Select-String -Pattern '\[BeDD\] join: connecting to (\S+)' | Select-Object -Last 1
        if ($join) { return [pscustomobject]@{ StrayJoin = $true; Reason = "the game was sent to the server $($join.Matches[0].Groups[1].Value) (a BeDD 'join' this run did not send)" } }
        $level = $log | Select-String -Pattern "world: level '(.*)' opened" | Select-Object -Last 1
        if ($level) {
            $name = $level.Matches[0].Groups[1].Value
            if ($name -eq $script:WorldName) { return $null }
            return [pscustomobject]@{ StrayJoin = $false; Reason = "the game entered the world '$name', not $($script:WorldName)" }
        }
        if ((Get-Date) -gt $deadline) { return [pscustomobject]@{ StrayJoin = $false; Reason = "the game is in a world that never logged ""world: level '...' opened"" (not the local $($script:WorldName))" } }
        Start-Sleep -Milliseconds 500
    }
}

function Enter-TestWorld([int]$LoadSeconds = 10, [int]$Attempts = 3) {
    for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
        Focus-Game
        $mark = Get-LogMark
        Enter-PlayScreen | Out-Null
        Start-Sleep -Seconds 4
        Click 0.173 0.43 -Relative      # first world thumbnail (GF-Test: LastPlayed = now)
        Start-Sleep -Seconds $LoadSeconds
        $spawned = $false
        try {
            Wait-LogLine 'Player Spawned' 75 -Since $mark | Out-Null
            $spawned = $true
        } catch {
            # a game sent to another server never spawns in a local world: say so instead of retrying
            $wrong = Get-WrongWorld $mark 0
            if ($wrong -and $wrong.StrayJoin) { Save-Screenshot "wrong-world-$attempt" | Out-Null; throw [GfE2E.WrongWorldException]::new($wrong.Reason) }
            if ($attempt -eq $Attempts) { throw }
            # The game sometimes fails to join its own local world ("Connection timed out").
            # Leave the error dialog and try again.
            Write-Step "world join failed (attempt $attempt): $_; retrying"
            Save-Screenshot "join-failed-$attempt" | Out-Null
            Focus-Game
            Click 0.24 0.68 -Relative   # "Back to menu" on the connection error dialog
            Start-Sleep -Seconds 5
        }
        if ($spawned) {
            $wrong = Get-WrongWorld $mark
            if ($wrong) { Save-Screenshot "wrong-world-$attempt" | Out-Null; throw [GfE2E.WrongWorldException]::new($wrong.Reason) }
            Start-Sleep -Seconds 3
            Focus-Game
            Write-Step "in world $($script:WorldName)"
            return
        }
    }
}

# ---------------------------------------------------------------------------------------
# Remote modes: join a BDS (tools/e2e/server, scenario client-host) instead of the local world
# ---------------------------------------------------------------------------------------
$script:ExternalServerName = 'GF-E2E'

# The external server list of the signed-in user (games\com.mojang\minecraftpe\external_servers.txt).
function Get-ExternalServersFile {
    $worlds = Get-WorldsDir
    if (-not $worlds) { throw "game data not found under $(Get-GameDataDir)" }
    return Join-Path (Split-Path $worlds) 'minecraftpe\external_servers.txt' # minecraftWorlds -> com.mojang
}

# Backs up the game's server list and drops stale entries of ours; Enter-RemoteServer adds the
# server through the game's "Add server" dialog, Restore-ExternalServers puts the list back.
function Set-ExternalServer([int]$Port) {
    $file = Get-ExternalServersFile
    New-Item -ItemType Directory -Force (Split-Path $file) | Out-Null
    $script:ExternalServersBackup = if (Test-Path $file) { Get-Content $file -Raw } else { $null }
    Clear-OurServerEntries
    $script:ExternalServerPort = $Port
    Write-Step "external server $script:ExternalServerName -> 127.0.0.1:$Port (added in game; list backed up from $file)"
}

# Drops our entries from the game's server list (the game must not be running: it rewrites the
# file from memory when it exits).
function Clear-OurServerEntries {
    $file = Get-ExternalServersFile
    if (-not (Test-Path $file)) { return }
    $content = Get-Content $file -Raw
    $keep    = @(Remove-OurServerEntries $content)
    $removed = @($content -split "`r?`n" | Where-Object { $_ }).Count - $keep.Count
    if ($removed -le 0) { return }
    if ($keep.Count -eq 0) { Remove-Item $file -Force } else { Set-Content -Path $file -Value $keep -Encoding UTF8 }
    Write-Step "removed $removed stale $script:ExternalServerName server entr$(if ($removed -eq 1) { 'y' } else { 'ies' }) from $file"
}

# The list without our entries (also ones an interrupted run left behind).
function Remove-OurServerEntries([string]$content) {
    if (-not $content) { return @() }
    return @($content -split "`r?`n" | Where-Object { $_ -and $_ -notmatch ":$([regex]::Escape($script:ExternalServerName)):" })
}

function Restore-ExternalServers {
    if (-not (Get-Variable -Name ExternalServersBackup -Scope Script -ErrorAction SilentlyContinue)) { return }
    $file = Get-ExternalServersFile
    $keep = @(Remove-OurServerEntries $script:ExternalServersBackup)
    if ($keep.Count -eq 0) { Remove-Item $file -ErrorAction SilentlyContinue }
    else { Set-Content -Path $file -Value $keep -Encoding UTF8 }
    Remove-Variable -Name ExternalServersBackup -Scope Script
}

# Play -> Servers -> "+ Add server" -> name / 127.0.0.1 / port -> "Add and play"; waits for the
# BDS to log the spawn. (User-added servers are listed below the featured ones, out of view, so the
# dialog is the deterministic path; the entry it adds is undone by Restore-ExternalServers.)
# Coordinates are fractions of the client area (calibrated on 1.26.51 at 1280x720 and 1600x900).
function Enter-RemoteServer([int]$Attempts = 3) {
    if (-not $script:ExternalServerPort) { throw 'Enter-RemoteServer needs Set-ExternalServer first' }
    for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
        Focus-Game
        $mark = Get-LogMark
        Enter-PlayScreen | Out-Null
        Start-Sleep -Seconds 4
        Save-Screenshot "join-1-play-$attempt" | Out-Null
        Click $script:ServersTab[0] $script:ServersTab[1] -Relative
        Start-Sleep -Seconds 3
        Save-Screenshot "join-2-servers-$attempt" | Out-Null
        Click $script:AddServer[0] $script:AddServer[1] -Relative
        Start-Sleep -Seconds 2
        # The dialog keeps the text of an earlier attempt: clear each field before typing.
        $fill = {
            param($at, [string]$text)
            Click $at[0] $at[1] -Relative
            for ($i = 0; $i -lt 24; $i++) { Press-Key BACK 20 }
            Type-Text $text
        }
        & $fill $script:NameField $script:ExternalServerName
        # The dialog refuses an address/port that is already listed ("already been added"):
        # Clear-OurServerEntries runs before every game launch, and run.ps1 joins once per launch.
        & $fill $script:AddressField '127.0.0.1'
        & $fill $script:PortField "$script:ExternalServerPort"
        Start-Sleep -Milliseconds 500
        Save-Screenshot "join-3-add-$attempt" | Out-Null
        Click $script:AddAndPlay[0] $script:AddAndPlay[1] -Relative
        try {
            Wait-LogLine '\[host\].*Player Spawned' 90 -Since $mark | Out-Null
            Start-Sleep -Seconds 4
            Focus-Game
            Write-Step "joined the remote server"
            return
        } catch {
            if ($attempt -eq $Attempts) { throw }
            Write-Step "remote join failed (attempt $attempt): $_; retrying"
            Save-Screenshot "join-failed-$attempt" | Out-Null
            # Back to the title screen: connecting dialog / Play screen (never a third ESC: exit dialog).
            Press-Key ESC; Start-Sleep -Seconds 3
            Save-Screenshot "join-reset-$attempt" | Out-Null
        }
    }
}
$script:ServersTab   = @(0.832, 0.13)   # "Servers" tab on the Play screen
$script:AddServer    = @(0.176, 0.253)  # "+ Add server" at the top of the server list
$script:NameField    = @(0.5, 0.20)     # "Add a new server" dialog fields
$script:AddressField = @(0.5, 0.366)
$script:PortField    = @(0.5, 0.532)
$script:AddAndPlay   = @(0.693, 0.663)  # "Add and play"

# Runs a console command on the remote host (remote modes).
function Send-HostConsole([string]$Command) {
    if (-not $script:HostDir) { throw 'Send-HostConsole needs a remote mode' }
    $name = 'cmd-{0:D6}.txt' -f (++$script:HostCmd)
    # Written under another name and renamed, so the host never reads a half-written file.
    $tmp = Join-Path $script:HostDir "$name.tmp"
    Set-Content -Path $tmp -Value $Command -Encoding UTF8
    Move-Item -Force $tmp (Join-Path $script:HostDir $name)
}
$script:HostCmd = 0

# Open the survival inventory (E) and wait for it to render.
function Open-Inventory { Focus-Game; Press-Key E; Start-Sleep -Milliseconds 1200 }
function Close-Screen { Press-Key ESC; Start-Sleep -Milliseconds 800 }

# Run a chat command, e.g. Invoke-Chat '/give @s diamond 16'
# With the client mod, the chat is confirmed open (client screen trace) before the text is typed,
# and T is pressed again when the game missed it (busy machine, HUD not ready yet); the command is
# sent once the chat closed again. Without it (remote-vanilla): fixed waits.
function Invoke-Chat([string]$text, [int]$Attempts = 4) {
    Focus-Game
    if (-not (Test-ClientTrace)) {
        Press-Key T; Start-Sleep -Milliseconds 900
        Type-Text $text
        Press-Key ENTER; Start-Sleep -Milliseconds 800
        return
    }
    $open = $false
    $closed = $false
    for ($i = 1; $i -le $Attempts -and -not $open; $i++) {
        $screen = Get-ClientScreen
        if ($screen -eq 'chat_screen' -and -not $closed) {
            # a chat left open (a lost Enter): close it once, so its old text is not sent with ours
            # (never a second Esc: on the HUD it would open the pause menu)
            $mark = Get-LogMark
            Press-Key ESC
            try { Wait-LogLine 'client screen: (?!chat_screen)' 3 -Since $mark -PollMs 100 | Out-Null } catch {}
            $closed = $true
            continue
        }
        if ($screen -and $screen -ne 'hud_screen') { Write-Step "chat: the top screen is '$screen', pressing T anyway" }
        $mark = Get-LogMark
        Press-Key T
        try { Wait-LogLine 'client screen: chat_screen' 3 -Since $mark -PollMs 100 | Out-Null; $open = $true }
        catch {
            # the line may come late on a loaded machine: look at the screen before pressing T again
            if ((Get-ClientScreen) -eq 'chat_screen') { $open = $true }
            else { Write-Step "chat did not open (attempt $i/$Attempts) for '$text'" }
        }
    }
    if (-not $open) { Write-Step "warning: the chat never opened for '$text' (screen: $(Get-ClientScreen)); typing anyway" }
    Start-Sleep -Milliseconds 200   # the text box takes the keyboard focus
    Type-Text $text
    $mark = Get-LogMark
    Press-Key ENTER
    try { Wait-LogLine 'client screen: (?!chat_screen)' 3 -Since $mark -PollMs 100 | Out-Null }
    catch { Write-Step "warning: the chat stayed open after '$text'" }
    Start-Sleep -Milliseconds 300
}

# Teleports the player with a chat command (e.g. '/tp @s 0 -60 0 90 30') and waits until the client
# applied it, so the next click aims at the new target; the command is sent again when no teleport
# arrives. Without the client mod: a fixed wait.
function Invoke-Teleport([string]$command, [int]$Attempts = 3) {
    if (-not (Test-ClientTrace)) { Invoke-Chat $command; Start-Sleep -Milliseconds 1200; return }
    # a relative teleport sent twice would go twice as far: one try, with more patience
    $relative = $command -match '[~^]'
    if ($relative) { $Attempts = 1 }
    for ($i = 1; $i -le $Attempts; $i++) {
        $mark = Get-LogMark
        Invoke-Chat $command
        try {
            Wait-LogLine '^(?!\[host\]).*client teleported to' $(if ($relative) { 12 } else { 6 }) -Since $mark -PollMs 100 | Out-Null
            Start-Sleep -Milliseconds 400   # a few frames at the new place
            return
        } catch { Write-Step "no teleport after '$command' (attempt $i/$Attempts)" }
    }
    throw "the teleport '$command' never arrived"
}

# Sends a chat command until a log line confirms it (a lost or not applied command is sent again).
# Returns the matching line.
function Invoke-ChatConfirmed([string]$command, [string]$pattern, [int]$TimeoutSeconds = 10, [int]$Attempts = 3) {
    for ($i = 1; $i -le $Attempts; $i++) {
        $mark = Get-LogMark
        Invoke-Chat $command
        try { return Wait-LogLine $pattern $TimeoutSeconds -Since $mark }
        catch { Write-Step "'$command' not confirmed (attempt $i/$Attempts)" }
    }
    throw "'$command' never confirmed by '$pattern'"
}
