# 34: what ui(11).dll is, who loads it, how it is called

Survey findings for the port. Subject: `game/r2/paks/Win64/ui(11).dll`
(945,664 bytes, x64, DLL, timestamp 0x5a270b32, PDB path under `r2dlc11`).
Scripts: `tools/scout/ui11_exports.py`, `ui11_header.py`, `ui11_shape.py`, `ui11_loader.py`.
Every number below is printed by one of those scripts.

## Summary

- `ui(11).dll` is a bag of 839 UI-element functions with no imports, no relocations and no
  writable state of its own (`.data` is 4 bytes). Each export is `f(a1, a2, a3, a4)` with
  pointer arguments. It calls out only through pointers that its caller hands it.
- The DLL is loaded by `rtech_game.dll` (not checked in engine, client, materialsystem or vguimatsurface, see section 4). The loader
  builds `r2\paks\Win64\<pak>(NN).dll` from the pak name and a version looked up at run time,
  then calls `LoadLibraryA` on it.
- The DLL's exports are keyed by name. The engine builds `<name>` and `<name>__hidden` at run
  time. The code that turns those names into addresses was not located.
- For the port, this DLL must be translated like the others. Within this repo snapshot,
  `ui(11).dll` is the only UI code DLL present.

## 1. Exports (Q1)

- 839 exports, all named, ordinals 1..839 contiguous, every export RVA distinct, none
  forwarded. Every export RVA is in `.text` (code). No export points at data.
- Ordinals follow alphabetical name order, not address order.
- Name length 8 to 42 characters, median 21. All names are lowercase identifiers with
  underscores. 35 names contain a digit (e.g. `r97_reticle`, `hcog_upper`).
- Naming pattern: `<element>_<part>`, with a `__hidden` suffix on 256 names. The most common
  leading tokens are `crosshair`, `gamestate`, `targetinfo`, `minimap`, `postgame`, `store`,
  `credits`, `scoreboard`, `titan`, `callsign`. These are HUD and menu element names.
- Pairs: 256 names `X` and `X__hidden` form exact pairs. Every `__hidden` name has its plain
  sibling and there are no orphans. The two members of a pair are separate functions with
  different code. In 239 pairs the hidden one is shorter.
- There are no init, update or data-blob exports. The role words (init, update, load, data)
  do not appear as export tokens. The pairing is visible/hidden, not a lifecycle.
- 30 representative names (every 27th in sorted order): `ability_hud`,
  `announcement_center__hidden`, `arrow_button_up`, `battery_flyout`, `callsign_icon_button`,
  `combo_button_large__hidden`, `credits_company_logo`, `crosshair_flight_core`,
  `crosshair_smart_pistol`, `customization_pack_button`, `disembarking_progress`,
  `dropship_intro_titan_loadouts`, `friendly_fire_warning__hidden`, `gamestate_info_cq`,
  `gamestate_info_speedball__hidden`, `hcog_upper`, `inventory_hud__hidden`,
  `loadout_button_small__hidden`, `matchmaking_status_compact__hidden`,
  `minimap_indicator__hidden`, `number_stat`, `pilot_speedometer`,
  `postgame_squad_leader__hidden`, `r97_reticle`, `scoreboard_ping`,
  `sp_difficulty_select`, `stats_background`, `store_weapon_description`,
  `targetinfo_spectre_bounty__hidden`, `titan_protocol_text__hidden`.

## 2. What the exports look like (Q2)

Sample: all 839 exports analysed. Extents are the unwind function plus its chained
continuation pieces (hot/cold splits). 658 exports have an unwind entry (225 of them are split
across pieces), and 181 are leaves with no unwind entry (180 of the 181 are `__hidden`).
Every byte of every extent decodes with no gap.

- Size: median 70 instructions, p25 29, p75 137, p90 242, max 1,794. Mean 121.8.
  Buckets: 79 at or under 10, 144 at 11 to 30, 310 at 31 to 100, 239 at 101 to 300,
  67 above 300. Total 102,177 instructions.
- Stack frames: 658 exports (78%) have one, median 80 bytes, max 512.
- Returns: 449 exports (54%) have exactly one `ret`.
- Calls: 189 exports (22.5%) contain no call at all. Only 6 direct calls exist in the
  exports, and all target internal functions that are not exported. No export calls another
  export. The whole image has 34 direct `call rel32` instructions. Indirect calls: 6,493 in
  exports, present in 650 exports (77.5%).
- Where the called pointer comes from: 5,748 of the 6,493 indirect calls (88%) call through a
  pointer that traces back to the first argument. Those calls use 20 distinct offsets from
  0x0 to 0xa0, most often 0x78 (1,785 sites), 0x20 (825), 0x40 (544), 0x30 (419), 0x88 (394),
  0x18 (339). This reads as a host-supplied function table. The other sites use a pointer
  loaded from the stack (411), a pointer loaded from memory (152), a computed pointer (167)
  or the return value of an earlier call (1).
- Memory writes: 753 exports (90%) write memory; 17,998 writes in total. Writes go to the
  fourth argument's copy (5,703), the stack (5,340), memory returned by an earlier call
  (3,561, which is host-allocated), the fourth argument directly (1,727), and loaded
  pointers (164). Writes never go through rcx directly. First-argument pointers are used
  only for calls.
- Loops: 51 exports (6%) have a backward branch. Indirect jumps: 487 exports (58%), 502
  through registers (switch tables) and 162 through a table in memory (tail calls).
- Floats: 92% of exports touch an xmm register and 81% contain a scalar or packed float op
  (add, mul, compare, convert, and so on). Float ops are 14.5% of all export instructions.
  43% of exports have 10 or more.
- Arguments (Microsoft x64 order). Integer positions read before written: rcx in 762
  exports, rdx in 608, r8 in 762, r9 in 831. Float arguments: 2 exports read an xmm register
  (xmm2 or xmm3) before writing it; whether that is a parameter was not checked.
  The most common read-set is `rcx, rdx, r8, r9` (532 exports), then `rcx, r8, r9` (223) and
  `rdx, r9` (73).
  Inferred signature: `f(host_table*, input*, handle, state*)`. The first argument supplies
  the host function table, the second is read-only input, the third is a handle passed to the
  host, and the fourth is the element's writable state. This is inferred from the code
  (rcx copied to a callee-saved register and used as the call base; r9 used as the write
  base), not confirmed against a caller.
- Data: 5,553 RIP-relative references, all into `.rdata`. 203 exports (24%) have none.
- Pair comparison: plain and hidden members both read the same argument registers in 132
  pairs, and share at least one data address in 118. Plain members are all unwind functions
  (256 of 256); hidden members are mostly leaves (180 of 256 have no unwind entry).

## 3. Position independence and external calls (Q3)

- No imports directory, no relocation directory, no TLS, no load config. `.data` is 4 bytes
  of virtual size with no bytes on disk. `.rdata` is 283 KB. The code is `.text` 644 KB, of
  which 94.9% is covered by unwind entries.
- 18,348 RIP-relative memory operands. Absolute disp32 operands (no base, no index): 0.
  Absolute addressing with an index but no base (excluding `lea`): 0.
- Immediates inside the preferred image range 0x180000000..0x1800ea000: 0.
- Qwords in `.rdata`, `.data` and `.pdata` that point into the image: 0. No code pointer is
  stored in data.
- Direct branch and call targets outside an executable section: 0.
- No call or jump goes through a RIP-relative slot (the shape an import call would take):
  0 distinct slots. So the DLL makes no calls to any import.
- Indirect control transfers: 8,592 calls through memory, 162 jumps through memory, 502
  jumps through registers.
- Conclusion: the code is position-independent. It works at any base if the loader maps the
  sections and does not apply relocations, because it has no absolute addresses. It makes no
  external calls in the import sense. Its only outbound calls go through a host-supplied
  table (section 2).
- The DLL has ASLR flag set but no relocation directory. Windows `LoadLibrary` can only map
  it at the preferred base 0x180000000 (inferred; not checked on Windows).
  The entry point (RVA 0x1000) is `mov eax, 1; ret`, so the DLL has no initialisation hook.


## 4. Who loads it (Q4)

Verified:
- `rtech_game.dll` is the loader. The pak-loading function at RVA 0xa030 (its unwind entry
  plus a chained continuation piece, which holds the call) does this in order:
  1. Formats the pak path from the pak name: a `r2\paks\Win64\` prefix (format at 0x2dfd8),
     or `paks\Win64\` (0x2dfdb) when the name has a directory part.
  2. Calls 0x9a00, a binary search over a sorted table filled at run time, which returns a
     version byte for the name. The table is not in the file.
  3. Formats a two-digit version with a `.rpak` extension (0x2dff0) and opens that file
     through 0x1e20.
  4. Checks the header: `RPak` magic at offset 0 (0x6b615052), version word 7 at offset 4,
     and a flag byte at offset 6. The flag bits (flags & 3) gate the next step.
  5. Finds the extension dot (0x7970), copies the path before it, appends `.dll` and a NUL,
     and calls `KERNEL32!LoadLibraryA` (IAT slot 0x2d178, call site 0xa238).
  6. Stores the module handle at object offset 0xa0.
- So the mechanism is `LoadLibraryA`, not a manual map and not a read of pak data. The DLL
  path is the pak name with its version, plus `.dll`. For this build that reads as
  `r2\paks\Win64\ui(11).dll`. The version comes from run-time data, so "11" is inferred
  from the file name, not read from the table.
- The rpak files the loader opens are not in the repo copy, so steps 2 to 4 were checked
  against code, not against a real rpak header.
- `engine.dll` has a patch-rpak path (`paks\Win64\%s_patch.rpak`, read in function 0x15a94b)
  but no `ui` literal (single-pass scan). That path was not traced further.
- Checks in an earlier scan pass (script since overwritten) found no `ui(` or `ui.dll` literal
  in client, materialsystem or vguimatsurface.

Not determined: whether 0xa030 is the only route to a code DLL (other callers of the pak
loader were not traced).

## 5. How exports are reached (Q5)

Verified:
- No `GetProcAddress` call in any host module takes a UI export name as a literal. Call sites
  with a literal name: engine 75, client 39, rtech_game 37, materialsystem_dx11 58,
  vguimatsurface 39. Call sites with a name from a parameter: engine 8 (in generic wrappers
  such as the OpenSSL-style DSO code); none in the others. Immediate (ordinal-style) names: 0
  in every module.
- The loader function 0xa030 makes no `GetProcAddress` call, so the export table is not
  resolved there.
- `engine.dll` function 0xfbad0 builds `<name>` and `<name>__hidden` (format at 0x5f4450, lea
  at 0xfbaf6) and then makes two calls through `[reg+0x58]` on an engine-global interface
  pointer. The names are the lookup keys, and the pair is visible/hidden by name.
- No `ui(11)` export name appears as a NUL-delimited string in `engine.dll` read-only data
  (0 of 839). `client.dll` has one match (`drawtext_default`), not examined.

Not determined: the code that turns a name into an address. It is behind the interface
method at `[reg+0x58]`, which was not identified. A manual export-directory walk was not
located; my structural searches gave many false positives and no conclusion. Ordinal lookup
is ruled out only through `GetProcAddress` (no immediate-name calls).

## 6. Bottom line (Q6)

- Translate it like the other DLLs. It is plain x64 code: no imports, no relocations, 0.61 MB
  of code, 665 unwind functions and 181 leaves, and the host calls it by export name. Each
  export is a function that calls back through a host table, so the port has to supply that
  table (the calling convention in section 2 is inferred, not confirmed).
- `ui(11).dll` is the only UI code DLL in this repo's game copy. `game/r2/paks/Win64/` holds
  just `ui(11).dll`. The brief lists `ui.dll` and `ui(01)` to `ui(10)` as installed, but they
  are not in the repo, so I could not check them.
- The current build loads any pak companion DLL flagged in its header. So other `ui(NN).dll`
  files would load if present. Whether the install has more is not determined from this repo.

## Verification and gaps

- Scripts (all under `tools/scout/`): `ui11_exports.py` (Q1), `ui11_header.py` (header and
  unwind facts), `ui11_shape.py` (Q2 and Q3, all 839 exports), `ui11_loader.py` (Q4 and Q5).
- `ui11_loader.py` finished the engine.dll pass in its final form. The client, rtech_game,
  materialsystem and vguimatsurface passes did not finish, so the GetProcAddress counts for
  those four come from an earlier run of the same origin test, not the final script.
- Several rtech details (0xa030 path logic, the 0x9a00 table, the 0x7970 extension finder)
  came from disassembly dumps in the scratchpad, not from a script in `tools/scout/`.
- Not determined, in one place: the name-to-address lookup; whether other `ui(NN).dll`
  versions load; whether Windows `LoadLibrary` can map the image at its preferred base 0x180000000
  (the image has ASLR set but no relocation directory; inferred, not tested on Windows).
