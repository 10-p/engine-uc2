# Audio F-6 (2026-10-09): the 19 waves from the `.xwb`, `A_Vehicle`, cue tables inside the Ogg (spike), HRTF

Branch `f6-audio` (worktree `engine-uc2-f6-audio`, base `1372dec`). Run trees `run/f6*`, logs `logs/f6/`, cook
output `logs/f6/cooked` (the coordinator installs it into `assets/uc2-cooked` after review). Brief: PLAN.md "Owner
review of 2026-10-09", F-6. Owner's words: "19 wave entries have no source WAV. We can convert them from the xwb
file" / "A_Vehicle.xsb/.xwb lets cook them in case we can use them in mods." / cue tables inside the Ogg "is a
exploration" / "No HRTF. OpenAL soft comes with this, can't we implement something similar based on how UT2004 or
UT99 work?"

Steps (each a commit and an entry here): 1 xwb decode + cook takes them, 2 A_Vehicle, 3 cue tables in the Ogg
(spike), 4 HRTF ini key, 5 regression.

## Step 0 - reading (2026-10-09)

- Base `1372dec` lacks F-4's `WaveFile=`/`UseEFX=` ini keys (branch `f4-platform`); not redone here.
