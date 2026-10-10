# T2 notebook: the generated C++ headers regenerate from scratch

Task T2 (2026-10-10). The owner: "Why can't they be generated from scratch? Can't we patch the generator so that
it generates 64-bit headers by default?" Goal: `Source/<Pkg>/Inc/<Pkg>Classes.h` are pure output of the
script compiler's header exporter (`ucc-bin make -ForceHeads`, Editor's `UClassExporterH`), 64-bit-correct
whatever the host's pointer size, and a check fails the build workflow when they are stale.

Worktree `engine-uc2-t2-headers`, branch `t2-headers` (from main 36267c6). Run trees `<WorkUC2>/run/t2*`, logs
`<WorkUC2>/logs/t2/`. Every run through `<WorkUC2>/bin/capped`.

## Step 1 (2026-10-10): baseline, today's exporter against the committed headers

```
tools/dev/script-build.sh --tree t2-hdr --headers assemble make     # t<N> tree prefix added to script-build.sh
    # ucc-bin make -nobind -CONFORMNOCONFIG -ForceHeads: exit 1 in 16 s, Success - 0 error(s), 85 warning(s)
    # diff: <WorkUC2>/logs/t2/baseline.diff (< committed Source/, > exported)
```

Identical: GameSpyClasses.h, UCElementsClasses.h, UCGameplayClasses.h, UnrealGameClasses.h. No CoreClasses.h or
UWebClasses.h is written or committed (every native class of Core and UWeb is `noexport`, hand-written).
Differences (diff lines, `<` and `>` counted):

| Header | Diff lines | What | Against scripts.md step 2c |
|---|---|---|---|
| EngineClasses.h | 26 | 13 members `PTRINT` ours, `INT` exported: StateInfo.Node/StateNode/Code, Actor.OctreeNodes (`TArrayNoInit<PTRINT>`), FileLog.LogAr, MovieMaterial x5, ScriptedTexture.RenderTarget, ShadowBitmapMaterial.TextureInterfaces[3], ParticleEmitter.Particles (`TArrayNoInit<PTRINT>`) | as listed (the table says "16" for Engine: it counted before M13 step 6 added ScriptedTexture/ShadowBitmapMaterial) |
| GUIClasses.h | 2 | GUIController.Designer | as listed |
| IpDrvClasses.h | 6 | InternetLink.PrivateResolveInfo, MasterServerLink.LinkPtr, MasterServerClient.MSLinkPtr | the table's "8" included the 2 HTTPDownload registrant lines, now gone (below) |
| EngineClasses.h | 6 | 3 comment lines in `UPSDistortionBaseMaterial`'s cpptext (`// UC2 port (M7): ...`) exported as 3 blank lines | as listed |
| EditorClasses.h | 4 | `UmakeCommandlet` (ours, = Epic's leak) vs `UMakeCommandlet` (exported), so also at another sorted position | as listed |
| UnrealEdClasses.h | 16 | 8 `GCC_PACK(4)` on the first member of a class (exporter rule restored at M3, header predates it) | as listed |

**Resolved since step 2c, nothing new**: the `UHTTPDownload`/`UPrefab` registrant placement no longer differs.
e0bce15 (2026-10-10, "IpDrv: HTTPDownload registered as IpDrv.HTTPDownload (Prefab as Editor.Prefab)") changed the
C++ `DECLARE_CLASS(...,Engine)` of both to `IpDrv`/`Editor` (Epic's leak says `Engine`: `epic-sources/IpDrv/Inc/
HTTPDownload.h:24`, `Editor/Inc/UnPrefab.h:21`), so our static build registers them in the package that declares
them and the exporter lists them where the committed headers (and Epic's leaked headers) have them.

