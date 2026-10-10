# D-3 stale text and D-4 test hygiene (2026-10-10)

Tasks D-3 and D-4 of PLAN.md "Owner review of 2026-10-10". Worktree `engine-uc2-d34`, branch `d34-docs-tests`
from main 2ed330f. Logs `/home/ion/WorkUC2/logs/d34`, run trees `run/d34*`.

## Step 1: `tools/dev/report-appendices.py` (D-3 1)

The coordinator's generator of 2026-10-08 was a scratch script (`inidiff.py` for Appendix C, an inline git loop
for A and B). Rewritten as `tools/dev/report-appendices.py [TIP]`: A and B from `git diff --name-status/--numstat
7633696 TIP -- Source` and `git log --reverse 7633696..TIP -- <file>` (commit lines cut at 150 characters, as the
original); C from `git show TIP:Engine/Config/*.ini` against `epic-sources/System/*.ini` (the coordinator's parser
unchanged: multi-valued keys as +/- values, Epic's D3DDrv/WinDrv sections as the base of ours).

Proof: `report-appendices.py 4813dd9` is byte-identical to the appendices in REPORT.md (sha256 a932129c...,
`logs/d34/appendices-4813dd9.md` vs `appendices-in-report.md`).
