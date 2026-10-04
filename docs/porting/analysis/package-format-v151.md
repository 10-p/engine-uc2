# UC2 package format (version 151, licensee 0) versus what the frontend tools read (UT2004: 128 / 29)

Needed at M11 for `ufrontend/packages/glue/lib/unreal/reader.cjs` (and its users: `generate-deps`,
`generate-manifest`'s GUID reader, `generate-travel`, `property-codec.cjs`, `writer.cjs`). Established
empirically on the shipped `System/Core.u` (72,051 bytes) and cross-checked against the engine's serializers
(`Source/Core/Src/UnLinker.cpp`, `Source/Core/Src/UnObj.cpp` `FCompactIndex<<`, `Source/Core/Inc/UnLinker.h`).

## Summary (unchanged in shape)

`u32 tag 0x9E2A83C1`, `u16 version` (151) + `u16 licensee` (0 = PC format; 1 = Xbox format, never used by us),
`u32 flags`, `i32 nameCount, nameOffset, exportCount, exportOffset, importCount, importOffset` (36 bytes), then
the 16-byte GUID, `i32 generationCount`, and per generation `i32 exportCount, i32 nameCount`. For `Core.u` the
name table therefore starts at 64.

## What differs from version 128

| Item | v128 (UT2004) | v151 (UC2) |
|---|---|---|
| Compact index (`AR_INDEX`) | 1–5 byte variable-length signed | **plain `i32`** from version 145 (`FCompactIndex<<`: "Load/Save as an INT for speed") |
| Name entry | compact length, bytes incl. NUL, `u32` flags | `i32` length (incl. NUL), bytes, `u32` flags |
| Import record | class package (idx), class name (idx), `i32` package index, object name (idx) | class package `i32`, class name `i32`, **`i16` package index**, object name `i32` = 14 bytes |
| Export record | class (idx), super (idx), `i32` package, name (idx), `u32` flags, size (idx), offset (idx, if size) | **10-byte head** `i16 class, i16 super, i16 super, i16 package, i16 package`, then name `i32`, flags `u32`, size `i32`, offset `i32` (only if size ≠ 0) |

The export head's duplication is not a design: from version 150 the three indices are `SWORD` fields and the
serializer still applies the 32-bit `AR_INDEX` to the first two, so each write covers the field and its
neighbour (`class+super`, then `super+package`, then `package` alone as 16 bits). Read it as five `i16` and
take fields 0, 1 (or 2) and 3 (or 4). Verified: with this layout the export table of `Core.u` ends exactly at
end of file and outer references resolve (`Vector`'s package index is the export of `Object`).

Packages older than 150 that the engine still loads (cut maps in `MapsOnHold`/`TestMaps` are 123–149) use the
wide records; below 145 they also use the variable-length compact index. A reader should switch on `version`:
`< 145` old index; `145–149` `i32` index with v128 record shapes; `>= 150` the shapes above.

## Still to establish (M11)

- Tagged property serialisation changed at version 148 ("tagged properties by reference", `DELTA_DEFAULTS`):
  `property-codec.cjs` needs the new tag layout (read `UObject::SerializeTaggedProperties` in
  `Source/Core/Src/UnObj.cpp` / `UnClass.cpp`).
- Whether anything in the summary beyond generations is read by the tools (heritage table is long gone).
- `writer.cjs` is hard-wired to emit 128/29; whether UC2 needs the writer at all (it is used for the `-mod=`
  conversions in the UT2004 target).
