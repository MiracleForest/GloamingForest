# F12 (ui): the instance's graphics mode and hand option as they were before the run, and the
# first-person camera (the head step switches to third person with F5).
. (Join-Path $PSScriptRoot 'ui.graphics.ps1')
Restore-UiGraphicsModes
Restore-UiOption 'gfx_hidehand'
Reset-CameraPerspective
