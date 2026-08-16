# DOS music selection evidence

Evidence source: unpacked `VGALEMMI.EXE` derived from the manifest-pinned
reference binary. Offsets below are within `vgalemmi.unpacked.raw`.

At `0x4858`, the executable indexes a 17-byte table at `0x4877`. The table is
one-based because it is passed to the loaded Sound Images driver:

```text
06 07 08 0A 0B 04 0C 0D 0E 0F 05 10 11 12 13 14 15
```

After conversion to zero-based `ADLIB.DAT` indices, this is:

```text
Lemming1, Lemming2, Lemming3, Mountain, Ten Lemmings, Can-Can,
Tim1, Tim2, Tim3, Tim4, Doggie, Tim5, Tim6, Tim7, Tim8, Tim9, Tim10
```

The comparison at `0x484C` wraps the rotation at `0x11` (17). The adjacent
table at `0x4888` is indexed by the level's one-based special-graphics ID:

```text
01 09 03 02  ->  Awesome, Menace, Beast II, Beast I
```

This matches special IDs in the level records: Taxing 15 uses 1, Tricky 14
uses 2, Mayhem 22 uses 3, and Fun 22 uses 4. The original executable advances
the normal rotation after successful play rather than storing a song in each
level record. The port anchors that rotation to canonical level order so level
selection is deterministic while preserving the original sequence and special
overrides.
