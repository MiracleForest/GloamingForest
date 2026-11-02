# Smoke (F0), remote modes: runs on the BDS host before it boots (server/scenarios/
# client-host.prelaunch.ps1, in the server harness). Leaves a marker in the host folder that
# scenarios/smoke.ps1 checks, so the host hook is proven on every remote smoke run.
Set-Content -Path (Join-Path $env:GF_E2E_HOST_DIR 'host-prelaunch.txt') -Value "smoke $HostMode on server slot $script:Slot" -Encoding UTF8
