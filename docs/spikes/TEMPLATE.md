# S<n>: <question>

| | |
|---|---|
| Unit | F<k> |
| Status | open / answered / answered with a fallback |
| Date | YYYY-MM-DD |
| Game / LeviLamina | 1.26.51 / 26.51.7 |

## Question

What has to be true on Bedrock for the unit's design to work, stated so the answer is yes or no.

## Why it matters

The upstream behaviour that depends on the answer and the units that consume it.

## What was tried

1. Approach, the headers and functions used (MCAPI only), the code (branch, commit or file).
2. ...

## Evidence

- Selftests (`<unit>.spike_*`), e2e runs and their exit codes, screenshots (attached to the PR), log lines.
- Measurements (tick time, memory, packet sizes) with the setup they were taken on.

## Conclusion

The answer, the chosen design, and its limits on the server build, the client build and for vanilla clients.

## Fallback and follow-ups

What ships if the primary approach breaks after a game update; deviations recorded in the PR; open questions for
later units.
