<!-- Title: "U<n>: <imperative summary>" / "F<n>: ..." (rule R25). No AI attribution. -->

## Summary

## Unit record coverage
<!-- What of the unit's content list is implemented; what remains unimplemented. -->

## Contract extensions
<!-- New files under src/feature/ext/<unit>/ (rule R18), or "none". -->

## Verification
- [ ] `pwsh tools/build.ps1 -Target all -Tests` and `xmake run -P . gf-tests`
- [ ] `pwsh -NoProfile -File tools/e2e/server/run.ps1 -Scenario <unit>`
- [ ] `pwsh -NoProfile -File tools/e2e/client/run.ps1 -Scenario <unit> -Mode local` (screenshots checked)
- [ ] `-Mode remote-vanilla` (when the unit has a vanilla fallback)
- [ ] `pwsh tools/ci/ownership.ps1 -Unit <unit>` and `pwsh tools/ci/arr-guard.ps1`