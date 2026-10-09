# F-2 and F-4: FPS in the log, platform like UT2004 (exceptions, frame yield, arm64, files, docs, switches)

Running notebook, appended after every step so a successor can continue. Worktree
`/home/ion/WorkUC2/engine-uc2-f4-platform`, branch `f4-platform` (base main `1372dec`). Run trees `run/f4*`,
logs `/home/ion/WorkUC2/logs/f4`. Brief: PLAN.md "Owner review of 2026-10-09" tasks F-2 and F-4.

Order: 1 F-2 FPS log line + `tools/dev/fps-bench.sh`; 2 F-4a exceptions off, fail-soft loaders; 3 F-4b frame
yield like UT2004; 4 F-4c arm64 layout hazards; 5 F-4d files and docs; 6 F-4e command-line audit.

Memory ceilings: game/ucc 2G, builds 6G with -j4, Emscripten link 6G, frontend build 4G (coordinator's rules).

## Step 0 - reading (2026-10-09)

Read: PLAN.md owner review, REPORT.md section 10 (features 2, 8, 9, 12, 13, tools inventory), notebooks
web-smoke.md, runtime-port.md. engine-ut2004 template findings are recorded under each step below.
