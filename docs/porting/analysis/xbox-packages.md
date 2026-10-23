Record — kept as history; the current state is in tools/README.md and the converters' headers.

# The Xbox disc's packages: what they hold and how they become PC packages

Two ways were weighed: a cook tool that writes PC packages (branch A, built, below) and loaders in the engine that
read the Xbox packages as they are (branch B, "Branch B" at the end). Recommendation: branch A.

The retail disc's maps, textures, static meshes and animations are Xbox-format packages (version 151, licensee 1,
Epic's `XBOX_ASSET_FORMAT`). The engine loads only the PC format (licensee 0) and never defines the Xbox macros, so
`tools/cook-xbox-packages.py` converts them, and `tools/cook.py --source xbox` runs it in place of `--pc-packages`.
Sources read: `XboxAssetConversion/Src/Launch.cpp` (Epic's PC-to-Xbox conversion), the engine's
`XBOX_ASSET_FORMAT`/`LicenseeVer() == XBOX_PACKAGE_FORMAT` serialisers (Core, Engine), `Core/Inc/FFileManagerLinear.h`
(`LIN_SAVE`/`LIN_LOAD`), `Engine/Src/UnGame.cpp` (`USE_LINEAR_LOADING`), `XboxDrv/Src/XBResource.cpp` (XPR),
`Editor/Src/UnEdExp.cpp` (the RDF exporters). Oracles: Epic's tree has the PC packages and, next to the disc's files
(byte-identical), the Xbox intermediates: `XboxMaps/*.unr` (the maps before linearisation) and the bundler recipes
`XboxTextures/*.rdf`.

**Verdict: feasible, and done.** Every map, texture, static mesh and animation package of the disc converts (180
packages, about 10 s with 8 jobs). The 64-bit dedicated server loads all 57 maps and the menu; the client renders
DM-Legacy, DM-Azure (terrain) and the main menu from the converted packages with the same pictures as from Epic's PC
packages (below). What is approximate is quantised vertex data; what is lost is editor data, data the Xbox never
shipped, and a few PC-only caches the engine rebuilds.

## What Epic's conversion did

`ConvertMaps` loads each PC map, moves every non-code asset it references into the level (renamed `cla<N>`, outer
`MyLevel`), clips static projectors, saves the map with licensee 1, exports the bulk data into three bundles per map
and saves again with the data stripped. Texture packages are saved with their mips emptied
(`StripAllTextureData`), static mesh and animation packages with their vertex streams marked "in XPR" and the static
meshes' index buffers emptied. Characters, weapons and hands get their own bundles (`<Name>.xpr`, `_uma`, `_pb`),
animation sets `<Set>_a_uma.xpr`, natives `Natives*.xpr`. A map is then linearised on the Xbox itself (`LIN_SAVE`).

## The formats

### .LIN (`XboxMaps/<Map>.LIN`, the entry level in `System/Script.lin`)

A recording of every byte the Xbox loader read while it loaded the map (`FArchiveFileReaderSaveLinear`): the
summary, names, imports and exports of the map, then the exports as the loader serialised them, in its order. No
seeks, no offsets. The order is `ULinkerLoad::LoadAllObjects` (table order, an export's outer created first), then
`UObject::EndLoad`, which does not sort by file offset on the Xbox (`#ifndef _XBOX`); nested preloads put one
export inside another (`ULevel::Serialize` preloads its Model at its end, `UModel::Serialize` its Polys after the
zones). Between exports are reads from packages that were already open: classes of the script packages being loaded
(runs of `System/*.u`, often partial), and objects of packages not on the disc as files. Missing from the stream:

- exports flagged neither `RF_LoadForClient` nor `RF_LoadForServer` (brushes, cameras, brush models and polys: 54,175
  exports over the 57 maps), which the PC game never creates either;
- the contents of lazy arrays the Xbox serialiser leaves lazy (`USkeletalMesh::Serialize` forces `GLazyLoad`):
  only each array's 4-byte end offset is there.

`uc2xpkg.read_lin` replays that order and finds each export by a strict parse of its state frame and tags against
the map's tables (plus structural checks for Level, Model, Polys and the mesh classes, and a walker for skeletal
meshes), skipping foreign bytes. Against Epic's `XboxMaps/*.unr`: all 57 maps cut, 120,162 runtime exports
byte-identical, 1 different (a 5-byte empty tag block written the other way, equivalent), 19 skeletal mesh streams
expanded back exactly. The entry level is the last package in `Script.lin` (18 exports, identical).

### XPR bundles (`XboxTextures/*.xpr`)

`XPR1`: u32 magic, total size, header size; the header holds a tag table (name offset, resource offset), the D3D
resource headers and the names; video memory follows. Resources are looked up by object name, so the `.rdf` recipes
are not needed.

- **Textures** (`<Map>.xpr` by `cla<N>`, `Natives.xpr` and the character/weapon bundles by texture name): D3DTexture
  (Common, Data, Lock, Format, Size). DXT1/3/5 in block order, not swizzled: the PC mips verbatim (`Raw 1`), the
  chain down to 1x1. RGBA8 as `D3DFMT_B8G8R8A8`, swizzled (Morton, u first), top mip only from the PC; the bundler
  made the lower mips. L8/A8 would be DDS; P8, G16, V8U8, L6V5U5, V16U16, RRRGGGBBB were never exported.
- **`_uma`**: TUmaArray contents as raw memory dumps of the Xbox structs (field order in memory, not in the
  serialiser's; FColor B,G,R,A), header u32 Count, u32 Offset: `<mesh>_VS/_CS/_AS/_UV<n>`, `<model>_vs_<section>`
  (BSP), `<instance>_CS`, `<mesh>_vs_<lod>` (skeletal), `<prim>_pvs` (projectors).
- **`_pb`**: push buffers, D3DPushBuffer (Common, Size, AllocationSize, Data; commands in system memory at
  header+Data when Common bit 31 is set). NV2A `SET_BEGIN_END` + `ARRAY_ELEMENT16/32`: the static mesh sections'
  strip indices, `<mesh>_<section>_pb`.
- **`_a_uma`**: animation motion blocks, `<set>_anim`.

### Serialisers that differ (licensee 1 against 0)

| Class | Xbox | Conversion (tool) |
|---|---|---|
| UModel | FBspNode 82 bytes (16-bit leaves, section, first vertex, light map; 32-bit index fields keep the next field in their high half), FBspSurf 16-bit indices, sections' vertices in `_uma`, `LightMaps` emptied, light map textures' second mip emptied | nodes and surfaces re-encoded, vertices unpacked, one FLightMap per used index rebuilt (texture from the section, world-to-light-map transform fitted to the vertices' light map coordinates) because the PC rebuilds the level's sections from them (`ULevel::PostLoad` → `UModel::BuildRenderData`) (`uc2xbsp.py`) |
| UTexture | Mips emptied | mips rebuilt from the bundles (`uc2xtex.py`) |
| UStaticMesh | packed positions/normals in `_uma`, index buffer in `_pb`, no wireframe, no RawTriangles | streams unpacked, indices from the push buffers, wireframe rebuilt, RawTriangles empty (`uc2xmesh.py`) |
| UStaticMeshInstance | colours in `_uma`, no Lights | colours back, Lights empty |
| UStaticProjectorClippedPrim | Xbox-clipped static projectors | left out, with the projectors' `StaticPrims`: the PC clips them at attach |
| USkeletalMesh | packed GPU vertices in `_uma`; CPU-skinning data (SkinningStream, SmoothVerts, SmoothSections, SmoothIndexBuffer) emptied | CPU-skinning data rebuilt from the raw lazy arrays (Epic's `CreateSkinningStream`), GPU vertices rebuilt (`uc2xskel.py`) |
| UMeshAnimation | motion block in `_a_uma`, notifies without `Function` | block back, `Function` None |
| ATerrainInfo, UTerrainSector | Xbox render data appended, Vertices/FaceNormals/VertexColors empty; `UTerrainSectorBlock` (Xbox only) holds the vertices | tails cut, heightmaps and vertex colours rebuilt from the blocks, blocks left out (`uc2xterrain.py`) |
| everything else | tagged properties, the same in both | copied |

## DM-Legacy, Xbox against PC

PC `Maps/DM-Legacy.unr`: 1,368 exports, 140 imports, its assets in `T_NK_Tech`, `S_NK_Tech`, `T_Sky`, `K_Environ`
and others. Xbox `DM-Legacy.LIN` (1,967,247 bytes): 1,602 exports, 74 imports, with 72 textures, 34 static meshes,
1 skeletal mesh, 1 animation and their materials merged in; `DM-Legacy.xpr` (72 textures, 6.7 MB),
`DM-Legacy_uma.xpr` (248 arrays), `DM-Legacy_pb.xpr` (41 push buffers).

- Read from the LIN: 1,137 runtime exports, byte-identical to Epic's Xbox `.unr`; 464 editor-only exports absent.
- BSP (Model100): vectors, points, nodes, surfaces (but for object references), vertex pool, bounds, leaves, light
  map texture pixels identical to the PC model; the rebuilt light maps place every BSP vertex within 0.02 texels of
  the PC's (the 16-bit coordinates' precision). Over the 56 game maps: every light map texture identical, the worst
  light map coordinate 0.022 texels over 128,507 lit nodes.
- Textures: all 72 top mips and all 434 levels of 4x4 or more identical to the PC textures.
- Static meshes: positions within half a quantisation step, colours, UVs, indices, kDOP identical.
- The flag skeletal mesh (`cla189` = `K_Environ.NK_Tech_Deco_Standard_Flags01`): identical but for three section
  fields that Epic's PC file holds uninitialised.

## Exact, approximate, lost (whole disc)

**Exact**
- every tagged property; BSP geometry; light map textures (first mip); collision models (515 compared: identical
  geometry); kDOP trees; static mesh colours, UVs, indices; instance colours; animations (64 of 64 compared
  byte-identical); terrain heightmaps and vertex colours (identical to the PC's on every terrain compared);
- textures with data: DXT top mip and every level of 4x4 or more (32,044 of 32,045 levels; one texture's copy in
  Natives is an older revision), RGBA8 top mip (169 of 169), RGBA8 lower mips rebuilt with `CreateMips` (149 of 160).

**Approximate**
- static mesh positions: within 0.52 of a quantisation step (CompressionScale/32767 per axis; at most 1.46 units on
  the largest mesh); normals within 0.124 degrees;
- skeletal GPU vertices where unpacked from the disc (7 LODs): position ≤ 0.005, weights ≤ 1/255, UVs outside
  [-1,1] keep only their fraction; the CPU-skinning data is rebuilt from the raw arrays, smooth normals off by 1/511
  on 87 of 189,362;
- DXT levels narrower than 4 texels (11,343 levels: D3DX re-encoded them; 95% within 8/255);
- BSP light map coordinates: 16-bit (0.02 texel);
- static mesh wireframe: the same segments in another order.

**Lost**
- editor-only map exports (brushes, cameras, brush models/polys): never created by the PC game either;
- static mesh RawTriangles (620,824 triangles; only the editor's rebuild reads them), instance Lights (editor
  relighting), FLightMap light bitmaps and the light maps' second mip;
- 1,810 textures with no pixels anywhere on the disc (texture-package textures the Xbox never loaded, 44 P8 font
  pages, the A8 and L6V5U5 ones; the 40 G16 heightmaps are rebuilt): placeholders, 312 MB of the 418 MB of textures;
- 1,119 texture-package-only static meshes with no streams on the disc (written without geometry); 859 others are
  recovered from the maps' copies, 73 share their data with other meshes and are refused;
- `Maps/ComboMatinees/*.unr` (9 packages in Epic's tree) are not on the disc and no shipped script references them.

## Results with the engine

- 64-bit dedicated server (`ucc-bin Engine.ServerCommandlet <map>`): all 57 maps and the menu bring their level up
  for play with no critical error; DM-Legacy loads the same script-referenced packages as with Epic's PC packages.
- Client (`SDL_VIDEODRIVER=offscreen`, 1280x720, the same camera placements on both): DM-Legacy differs from the
  PC-package picture by 0.06 to 0.85 of 255 on average per pixel; DM-Azure (terrain, characters) by 0.06 to 0.59;
  the main menu renders.

## Notes

- The converted packages keep the Xbox packages' GUIDs: a server and a client have to run the same cook (a game
  folder with Epic's PC packages has other GUIDs and other export tables).
- `XboxTextures/*.rdf` and Epic's `XboxMaps/*.unr` are not on the disc and not needed; the converter reads only the
  disc's files.

## Branch B: loaders in the engine for the Xbox packages as they are

The idea: a package with licensee 1 takes Xbox serialisation paths at run time (many serialisers already branch on
`Ar.LicenseeVer() == XBOX_PACKAGE_FORMAT`), its bulk data comes from the `.xpr` bundles, and the D3D resources become
what the GL renderer uploads; Epic's sources document the format. What that takes, checked against the code and the
disc:

**The maps cannot be read as they are.** A `.LIN` is not a package: it is the bytes one Xbox session read, and
`LIN_LOAD` (`FFileReaderLinear`) only works by repeating that session read for read. The stream holds the reads
from every package already open at recording time, interleaved with the map's: over DM-Legacy, Menu and OD-Cancer,
310 KB from `UCMenu.u`, 68 KB from `UCGameplay.u`, 5 KB from `GUI.u`, 31 KB from `UCEffects.u`, reads from `S_Team`,
`S_FX`, `T_Portrait` and others, and 175 KB found in no file of the disc (objects that exist only inside other
streams, such as `Script.lin`). A replay therefore needs, byte for byte, the Xbox's script packages and load
history: the port rebuilds `UCMenu.u`, `UCGameplay.u` and `GUI.u` from patched sources, applies `ScriptPatches.h`
at load, loads script packages from files instead of `Script.lin`, and sorts `EndLoad` by offset (`#ifndef _XBOX`).
Any difference reads the next export from the wrong bytes. Reading a map in the engine means cutting the stream
first, which is what `uc2xpkg.read_lin` does by replaying the order and searching each export; Epic's code has no
such reader, because the Xbox never needed one.

**The existing licensee-1 branches assume the Xbox structures.** 19 serialisers branch on the licensee; 10 are
compiled only under `XBOX_ASSET_FORMAT`/`_XBOX` (TUmaArray's `bIsInXPR`, the skeletal GPU vertex, the motion block,
terrain, projectors), and of the 9 compiled on the PC, `FBspNode` and `FBspSurf` read wrongly with the PC layout
(`iLeaf`, `iSection`, `iFirstVertex`, `iLightMap` are 16-bit on the Xbox and `INT` on the PC, so 4 bytes are read
where 2 were written; the 32-bit index fields keep the next field's bytes in their high half). Each class needs a
licensee-1 reader into the PC structures, unpacking as it goes.

**The bulk data needs formats that are not in Epic's code.** Epic's drop calls the XDK for them and does not hold
it: texture swizzling (`XGSwizzleRect`, `XGUnswizzleRect` in `XboxDrv/Src/XBUtil.cpp`, `Bundler/basetexture.cpp`),
the D3D texture header (`XGSetTextureHeader`), the index push buffers (`XGCompileDrawIndexedVertices`,
`D3DDrv/Src/D3DPushBuffer.h`: NV2A methods, which GL cannot run and which have to be decoded back to indices). The
`_uma` arrays are memory images of the Xbox structures (packed positions over a per-mesh box, 11:11:10 normals, BGRA
colours), not their serialised form. The run time also needs an index of all 599 bundles, since the Xbox loaded them
in groups named by script (`LoadXPRGroup`, `RequiredXPRs`; stubs on the PC).

**What the PC paths need, the Xbox conversion removed.** The port skins on the CPU (`UsePCSkeletalShader=False`)
from SkinningStream, SmoothVerts, SmoothSections and SmoothIndexBuffer, which the Xbox emptied (rebuild at load from
the raw lazy arrays with `CreateSkinningStream`, or a GPU skinning path in UC2GLDrv; merged map meshes have no raw
arrays, so they need their twin in a `.ukx`, found by content). The PC builds terrain from the G16 heightmap, which
was never bundled (rebuild from the sector blocks, or the Xbox block renderer and its vertex shader in GL). The
level's BSP render data is rebuilt from FLightMaps the Xbox left out (here the engine is simpler than the tool: skip
`BuildRenderData` for licensee-1 levels, Epic's own Xbox behaviour, through a named capability). Static projector
prims, layer weight maps (cleared with their mips not loaded on a server), and texture-package textures whose pixels
exist only in a map's copy all need handling as in branch A.

**What it would take.** A C++ stream cutter (or a cook step for the maps only), about 20 licensee-1 readers and
PostLoad fix-ups across Core and Engine (Model, StaticMesh, StaticMeshInstance, StaticProjectorClippedPrim,
SkeletalMesh and its LOD model, MeshAnimation, Texture, the terrain classes), a bundle manager in the engine, texture
unswizzling and format conversion and push-buffer decoding in the render path, the rebuilders above, and the test
of every target (Linux, Windows, the Web build, which would stream 447 MB of bundles, Android). It reuses no less
reverse-engineered knowledge than branch A (the stream order, the XDK formats, the content matching), and it puts
it into Epic's code instead of a tool.

**Recommendation: branch A.** The format knowledge both branches rely on is the same: branch A's converters are
written from those serialisers, and the parts Epic's code does not document (the stream cut, the XDK formats) are
needed by either. A is built and checked on every map; it leaves the engine as it is; its output is plain PC
packages, so every target loads it with no run-time cost; and the disc already needs a cook step (audio, movies).
Branch B adds engine surface on every target for no gain in fidelity: the data lost on the disc is lost to both. If
loading Xbox packages in the engine is wanted anyway, the smallest version is a hybrid: the tool cuts the `.LIN`
streams into licensee-1 `.unr`, and the engine reads licensee-1 packages and the bundles, which is still all of the
engine work above but the stream cutter.
