# Unit worker guide

One worker implements one unit in its own worktree, on a branch named after the unit (`u20-naga`,
`f4-dimension`), and opens one pull request into `main`. This page is the template the coordinator fills in
for each worker and the checklist the worker follows.

## What the worker gets

- **Goal**, quoted verbatim: 基于LeviLamina，制作Minecraft Java版模组Twilight Forest的基岩版复刻。要求支持客户端和服务端；所有特性和交互和界面必须完全一致，没有任何缺失。可以借助 LiteLDev/BeDD 来调试。
  Decisions taken with it: The Twilight Forest 4.8 at commit `9e40a75721c716b2fdf80c16d1058fe718825e9f` is the
  reference; the CC BY-NC-SA assets are reused through `tools/import`, the All Rights Reserved ones (sounds,
  music, structure templates) are regenerated; client-mod parity comes first, vanilla clients are best effort.
- **The unit record**, verbatim from [units.json](units.json): id, title, paths, content, description, contracts,
  e2e, size.
- **The conventions**: [AGENTS.md](../../AGENTS.md) (rules R1-R25, contracts, Bedrock lessons), the
  [unit table](README.md), the unit's behaviour spec `docs/spec/<unit>.md` (written by F14-F16; until it exists,
  TF's source) and the contract headers.
- **The e2e recipe**: [e2e.md](e2e.md).

## Before coding

1. `pwsh tools/tf/fetch.ps1` (once per machine) and read the TF classes of the unit from `%LOCALAPPDATA%\gf-tf`.
   Never through the GitHub API (rule R24), never open `.ogg` or `.nbt` files (rule R10).
2. Read the contract headers the record lists and the LeviLamina headers you will call
   (`%LOCALAPPDATA%\.xmake\packages\l\levilamina\26.51.7\<hash>\include`). Never call `MCNAPI` functions.
3. List the ids and paths the unit owns: `Select-String "\t<Unit>\t" docs/spec/owners.tsv`.

## While coding

- Edit only owned paths (rule R3); a needed contract extension goes into new files under
  `src/feature/ext/<unit>/` and is called out in the PR (rule R18).
- Translate TF code faithfully and keep the attribution header (AGENTS.md); keep TF's numbers, timings,
  probabilities and texts.
- Reference other units only by string ids (rule R2); attach behaviour to other units' blocks by tag matchers
  (rule R6).
- Register selftests `<unit>.<case>` in the unit's dx range (rule R12) and write the e2e scenarios
  `tools/e2e/{server,client}/scenarios/<unit>.ps1`.
- Commit in small buildable steps with imperative messages.

## Finishing

These steps, verbatim:

```
After you finish implementing the change:
1. **Code review** — Invoke the `Skill` tool with `skill: "code-review"` to find correctness bugs (it reports findings; it does not edit code). Fix any findings it surfaces before continuing.
2. **Run unit tests** — Run the project's test suite (check for package.json scripts, Makefile targets, or common commands like `npm test`, `bun test`, `pytest`, `go test`). If tests fail, fix them.
3. **Test end-to-end** — Follow the e2e test recipe from the coordinator's prompt (below). If the recipe says to skip e2e for this unit, skip it.
4. **Commit and push** — Commit all changes with a clear message, push the branch, and create a PR with `gh pr create`. Use a descriptive title. If `gh` is not available or the push fails, note it in your final message.
5. **Report** — End with a single line: `PR: <url>` so the coordinator can track it. If no PR was created, end with `PR: none — <reason>`.
```

- The test suite is `pwsh tools/build.ps1 -Target all -Tests` followed by `xmake run -P . gf-tests`.
- The PR title is `U<n>: <imperative summary>` / `F<n>: ...` (rule R25); the body follows
  `.github/PULL_REQUEST_TEMPLATE.md`. No Claude or AI attribution anywhere.
- The final message lists what remains unimplemented against the unit's content list, the `src/feature/ext/`
  contract extensions added, and (F units) the spike conclusions, with a report in `docs/spikes/S<n>.md`
  following [the template](../spikes/TEMPLATE.md).
