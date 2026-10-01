# Benchmarks — what the charter costs

Machine and methodology: see `benchmarks/README.md`. 1200 sequential `ow.invoke('ow-window','isMaximized')` per run, median of 5 with Latin-square rotation and a discarded warm-up per state.

| state | ops/s | µs/call | overhead vs off |
|---|---:|---:|---:|
| off (no charter) | 2260 | 442.5 | — |
| charter: allow ['ow-window'] | 2226 | 449.2 | +1.5% |
| charter: deny ow-window:isMaximized | 2044 | 489.2 | +10.5% |

## Reference round-trip (other frameworks, no policy)

| framework | ops/s | µs/call |
|---|---:|---:|
| electron | 3192 | 313 |
| owear | 1496 | 668 |
| owear-nom | 1488 | 672 |
| tauri | 1127 | 887 |
| neutralino | 23 | 42611 |

_A denied call is still a full kernel round-trip, so the `allow`/`deny` rows are the policy cost on top of an IPC that already sits between Electron and Tauri. The `deny` row is the most expensive on purpose: every refusal also broadcasts a `charter.denied` audit event to the main process (JSON + control socket write). A granted call only costs a lookup._
