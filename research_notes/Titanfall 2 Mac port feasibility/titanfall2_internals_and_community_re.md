# Titanfall 2 (PC, Respawn 2016): technical make-up and community reverse-engineering resources, as of 2026-10-08

Method and reliability notes (read first). Pages I actually opened and read: PCGamingWiki's wikitext for Titanfall 2 and Titanfall (via its MediaWiki API), the Steam store `appdetails` JSON, the Steam Deck compatibility endpoint, a third-party mirror of Steam's app metadata (api.steamcmd.net), the ProtonDB summary JSON, Northstar docs pages, the Northstar launcher source (a shallow clone of `main` at commit 4229f4a, 2026-10-07; line numbers refer to that commit), r1delta/rsx/tf2vpk/RePak READMEs and file listings, and GitHub API metadata for the repos named below (queried 2026-10-08). WebFetch summaries are produced by a small model, so I cross-checked the important ones against raw data where I could. **Blocked (HTTP 403 or bot check, so unverified):** SteamDB, Valve Developer Community wiki (Titanfall / Titanfall engine branch / Titanfall 2), AppleGamingWiki, CodeWeavers compatibility pages, macgamingdb, PCGamingWiki HTML (API worked). Anything sourced only from a search-result summary is labelled "search summary only (unverified)". No game files, cracks or leaked code were downloaded or read. GitHub's unauthenticated rate limit was hit late in the session, so a few repos (Whisky, GPTK builds, Syampuuh/Titanfall2) were not checked.

---

## 1. Binary layout: which executables and DLLs ship, sizes, 64-bit, compiler

### Takeaway
Everything evidence-visible is 64-bit (Windows x64, DirectX 11). Game code lives in `bin\x64_retail\*.dll`, not in `Titanfall2.exe`. The Northstar launcher never runs `Titanfall2.exe`; it loads `tier0.dll` and then `launcher.dll` and calls its entry point. Per-file sizes and the compiler version are not publicly documented anywhere I could open; only aggregate depot sizes are known.

### Cited Findings
- Steam metadata marks the app as `osarch: 64`, `oslist: windows`, installdir `Titanfall2`. — [api.steamcmd.net mirror of Steam app info](https://api.steamcmd.net/v1/info/1237970) (third-party mirror, fetched 2026-10-08)
- Steam store requirements list "Win 7/8/8.1/10 64bit", DirectX 11, 45 GB storage, minimum "Intel Core i3-6300t or equivalent [4 or more hardware threads]", 8 GB RAM, GTX 660 2GB / HD 7850 2GB. — [Steam store API](https://store.steampowered.com/api/appdetails?appids=1237970&cc=us&l=english)
- PCGamingWiki: `windows 64-bit exe = true`, `windows 32-bit exe = false`, `windows arm app = false`, Direct3D version 11 only, no Vulkan/OpenGL/Metal. Original Windows release 2016-10-28. — [PCGamingWiki wikitext via API](https://www.pcgamingwiki.com/w/api.php?action=parse&page=Titanfall_2&prop=wikitext&format=json&formatversion=2)
- Steam depot layout (third-party mirror): main depot 1237971 is 67,602,196,163 bytes installed / 31,644,383,184 bytes download; ten per-language depots (english 763,076,727 B, french 713,687,235 B, german 717,798,880 B, italian 734,621,776 B, spanish 754,619,351 B, polish 721,875,784 B, russian 750,756,738 B, latam 753,310,727 B, japanese 707,710,239 B, brazilian 734,961,241 B, tchinese 676,036,601 B); three shared depots 228983, 228984, 228990 pulled from app 228980; and depot 3340991 (247,209,071 B) pulled from app 3340990 (content unknown to me). — [api.steamcmd.net](https://api.steamcmd.net/v1/info/1237970)
- Note the mismatch: the store page says 45 GB, the depot manifest sums to roughly 67.6 GB plus language packs. — [Steam store API](https://store.steampowered.com/api/appdetails?appids=1237970&cc=us&l=english) vs [api.steamcmd.net](https://api.steamcmd.net/v1/info/1237970)
- The Steam "launch" entry is not an executable but `steam2ea://launchgame/1237970?platform=steam&theme=tf2`, i.e. Steam hands off to the EA app, which starts the game. — [api.steamcmd.net](https://api.steamcmd.net/v1/info/1237970)
- Module names that the Northstar source references (this is what Northstar touches, NOT a full inventory of the install). Counts are my grep of quoted DLL names in `primedev/` excluding thirdparty: `engine.dll` (52), `server.dll` (21), `client.dll` (18), `tier0.dll` (6), `vstdlib.dll` (3), `materialsystem_dx11.dll` (3), `filesystem_stdio.dll` (3), `launcher.dll` (2), `rtech_game.dll` (1), `localize.dll` (1), `mileswin64.dll` (1), `bink2w64.dll` (1), `OriginSDK.dll` (1), plus XInput 9_1_0/1_3/1_4 and `wsock32.dll`. — [NorthstarLauncher source](https://github.com/R2Northstar/NorthstarLauncher/tree/main/primedev)
- Concrete uses: `ON_DLL_LOAD_CLIENT("materialsystem_dx11.dll", ...)` patches the D3D11 material system; `rtech_game.dll` is touched in the RPak filesystem code; `mileswin64.dll` is hooked for audio overrides; `bink2w64.dll` for video overrides; `localize.dll` for localisation. — [nscustomdxbuffer.cpp](https://github.com/R2Northstar/NorthstarLauncher/blob/main/primedev/materialsystem/nscustomdxbuffer.cpp), [rpakfilesystem.cpp](https://github.com/R2Northstar/NorthstarLauncher/blob/main/primedev/core/filesystem/rpakfilesystem.cpp), [audio.cpp](https://github.com/R2Northstar/NorthstarLauncher/blob/main/primedev/client/audio.cpp), [clientvideooverrides.cpp](https://github.com/R2Northstar/NorthstarLauncher/blob/main/primedev/client/clientvideooverrides.cpp), [modlocalisation.cpp](https://github.com/R2Northstar/NorthstarLauncher/blob/main/primedev/client/modlocalisation.cpp)
- Launch sequence in `NorthstarLauncher.exe`: sets `PATH` to include `<game>\bin\x64_retail\`, `LoadLibraryExW(...\bin\x64_retail\tier0.dll)`, optionally loads `Northstar.dll`, `LoadLibraryExW(...\bin\x64_retail\launcher.dll)`, then calls `LauncherMain(HINSTANCE, HINSTANCE, LPSTR, int)` with null/zero arguments ("the parameters aren't really used anyways" per a code comment). `Titanfall2.exe` is referenced only in error-message text. — [primelauncher/main.cpp](https://github.com/R2Northstar/NorthstarLauncher/blob/main/primedev/primelauncher/main.cpp)
- A separate `wsock32.dll` proxy (`primedev/wsockproxy`) is dropped next to `Titanfall2.exe` so the vanilla exe loads Northstar when `-northstar` is on the command line (Steam/EA launch path). Most wsock32 exports are forwarded to `ws2_32` and only a dozen are proxied by hand. — [wsockproxy/dllmain.cpp](https://github.com/R2Northstar/NorthstarLauncher/blob/main/primedev/wsockproxy/dllmain.cpp), [loader.cpp](https://github.com/R2Northstar/NorthstarLauncher/blob/main/primedev/wsockproxy/loader.cpp)
- The dedicated-server path loads community-made stubs for `GFSDK_SSAO.win64.dll`, `GFSDK_TXAA.win64.dll` and `d3d11.dll` from `bin\x64_dedi\` (these are NVIDIA GameWorks libraries plus D3D11 that the game links against). — [main.cpp](https://github.com/R2Northstar/NorthstarLauncher/blob/main/primedev/primelauncher/main.cpp), [NorthstarStubs](https://github.com/R2Northstar/NorthstarStubs) (Zlib licence, last push 2022-02-17)
- Where the game logic lives: the Northstar FAQ says the launcher "re-purposes the existing `server.dll`" shipped with the client, i.e. the retail client install ships a full server module used for single-player and private matches. — [Northstar FAQ](https://docs.northstar.tf/Wiki/faq/)
- Northstar hooks are pinned to the retail build by address. In the tree I read there are ~371 `Offset(0x...)` calls (my grep), 10 `FindPatternSIMD`, 4 `FindPattern` and 3 `FindStringReadOnly` call sites. Hook sites are declared like `engine.dll + 0xNNNN` via an `AUTOHOOK` macro. — [core/hooks.cpp](https://github.com/R2Northstar/NorthstarLauncher/blob/main/primedev/core/hooks.cpp) and `primedev/` tree
- Registry state the game/launcher expects: `HKLM\Software\Respawn\Titanfall2` with `displayname`, `Product GUID`, `locale`, `Install Dir` (documented in a PCGamingWiki fix for the Steam "validating" loop). The Northstar launcher separately reads `HKLM\SOFTWARE\WOW6432Node\Origin\ClientPath`. — [PCGamingWiki](https://www.pcgamingwiki.com/w/api.php?action=parse&page=Titanfall_2&prop=wikitext&format=json&formatversion=2), [main.cpp](https://github.com/R2Northstar/NorthstarLauncher/blob/main/primedev/primelauncher/main.cpp)
- Compiler/runtime: the only hint I found is a Steam forum user guide telling people that the game needs MSVCR120.dll / MSVCP120.dll (Visual C++ 2013 runtime). That is one anecdotal post, not a binary analysis. — [Steam discussion](https://steamcommunity.com/app/1237970/discussions/0/3805027864674031138)
- Default EA-app install path is `C:\Program Files\EA Games\Titanfall2\` (permission-restricted, which breaks mod/log writes). — [Northstar troubleshooting](https://docs.northstar.tf/Wiki/installing-northstar/troubleshooting/)
- Search summary only (unverified): `materialsystem_dx11.dll` listed at 2,801,664 bytes in a search result for a modding page (moddb RTX Remix page returned 403 when opened).

### Inferences
- `Titanfall2.exe` is a thin entry point that delegates to `launcher.dll!LauncherMain` (Source-engine launcher-module pattern). Northstar's ability to call `LauncherMain` directly with null arguments supports this. The real translation workload is the DLLs in `bin\x64_retail`: at minimum `launcher`, `engine`, `client`, `server`, `tier0`, `vstdlib`, `filesystem_stdio`, `materialsystem_dx11`, `rtech_game`, `localize`, `mileswin64`, `bink2w64`, `OriginSDK`.
- Given `server.dll` and `client.dll` both ship in the one install and are hooked as separate modules, single-player is a listen-server style arrangement: the SP campaign runs server logic natively in `server.dll` and presentation in `client.dll` (plus UI scripts), with `engine.dll` bridging them. That is an inference from Northstar's architecture, not a documented statement for the campaign specifically.
- The "retail" naming and the existence of `x64_retail` suggests other build configurations exist internally; no public evidence of any shipped.
- Northstar's hard-coded offsets/signatures are a ready-made, hand-resolved list of roughly 400 function/data addresses in `engine.dll`/`client.dll`/`server.dll`/`materialsystem_dx11.dll`/`rtech_game.dll` that could seed symbol naming for a recompiler (they are only valid for the final retail build the user owns).
- The Source-style modules `studiorender`, `vphysics`, `inputsystem`, `vgui` etc. are NOT referenced anywhere in Northstar. Titanfall 2 may have merged them into `engine.dll`/`client.dll`; I cannot confirm.

### Gaps
- Per-file sizes and the complete DLL list for `bin\x64_retail` (SteamDB depot/file-list pages returned 403; no other public list found). Cannot say whether `studiorender.dll`, `vphysics.dll`, `inputsystem.dll`, `vstdlib.dll` all exist separately other than `vstdlib.dll`, which Northstar names.
- `Titanfall2.exe` size and PE sections.
- MSVC version used to compile the game (one anecdotal VC++ 2013 hint only), and whether `/GL`/LTCG was used.
- Which of the three shared Steam redist depots (228983/228984/228990) correspond to which redistributable (not checked).

---

## 2. Engine lineage: relation to Valve Source, what Respawn replaced, renderer/threading/streaming sources

### Takeaway
Titanfall 2 is built on Respawn's heavily rewritten Source derivative. Respawn engineers have said they replaced the renderer, audio and netcode and that "not a lot of Source" remains. The retail PC renderer is Direct3D 11 only. Public technical talks are sparse: one GDC 2017 talk on texture streaming (TF2) and a 2014 interview on Titanfall 1's engine.

### Cited Findings
- PCGamingWiki lists the engine as Source ("ReSource"), quoting a June 2016 E3 interview with a Titanfall 2 engineer: on the first game they started from Source and replaced the renderer, the audio system and the netcode; for TF2 they kept the "Titanfall engine" but made large rendering improvements and kept ripping out and rewriting chunks ("not a lot of Source left"). — [PCGamingWiki wikitext](https://www.pcgamingwiki.com/w/api.php?action=parse&page=Titanfall_2&prop=wikitext&format=json&formatversion=2) (quoting Hardcore Gamer; the original Hardcore Gamer URL returned 404 when I tried it)
- Wikipedia's Source-engine article confirms Titanfall, Titanfall 2 and Apex Legends use a custom Source-derived engine but gives no branch detail. — [Wikipedia: Source (game engine)](https://en.wikipedia.org/wiki/Source_(game_engine))
- Titanfall wiki (stub): heavy Source modification, new lighting with physically based rendering, VTF/BSP/NUT formats retained in altered form. — [titanfall.wiki.gg/wiki/Source](https://titanfall.wiki.gg/wiki/Source)
- Titanfall 1 interview (PCGamesHardware, 16 March 2014, Respawn technical director Richard Baker; this is TF1, not TF2): Direct3D 11 with compute shaders; 64-bit mandatory on PC; a forward renderer with static lighting rather than deferred; one main thread, one render thread and worker threads scaling with core count; Havok (an early version) for physics; large uncompressed audio on disk. Reported as saying they chose Source for designer iteration speed. The page was summarised by WebFetch, so treat details as approximate. — [PCGH interview](https://www.pcgameshardware.de/Titanfall-Spiel-15918/Specials/Titanfall-Interview-Technik-1112495/)
- GDC 2017 talk "Efficient Texture Streaming in Titanfall 2", speaker Chad Barb (Respawn), Programming track: TF2 added real-time texture streaming; approach relies on offline preprocessing and precomputed data, histograms to prioritise which textures to load or drop, works across PC RAM and resolution variation. GDC Vault lists it as members-only; a GDC Game Developer article says a video is on the GDC YouTube channel. — [GDC Vault](https://www.gdcvault.com/play/1024105/Efficient-Texture-Streaming-in-Titanfall), [Game Developer](https://gamedeveloper.com/programming/video-efficient-texture-streaming-in-i-titanfall-2-i-)
- Titanfall 2 on PC uses a custom VPK variant with `*_dir.vpk` plus numbered chunk files named like `englishclient_<map>.bsp.pak000_dir.vpk` in a `vpk` folder; a community tool author writes the format handling as "Designed for r2 VPKs, should work with r1/r5 as well". RPak files ("Respawn Pak") are the second package format, handled by `rtech_game.dll`. — [Wanty5883/Titanfall2 VPK names](https://github.com/Wanty5883/Titanfall2/blob/main/game-values/file-location/vpk-file-names.md), [tf2vpk README](https://github.com/pg9182/tf2vpk), [RePak README](https://github.com/r-ex/RePak)
- PCGamingWiki's API field lists Direct3D 11 only; Steam requirements say DirectX 11. The Northstar tree names only `materialsystem_dx11.dll`. — [PCGamingWiki](https://www.pcgamingwiki.com/w/api.php?action=parse&page=Titanfall_2&prop=wikitext&format=json&formatversion=2), [Steam store API](https://store.steampowered.com/api/appdetails?appids=1237970&cc=us&l=english)
- Search summary only (unverified; the Valve Developer Community page returned 403/bot check): a search result summarising the "Titanfall engine branch" article says the branch is based on the Portal 2 branch, has D3D11 as default and an engine-level D3D12 option, custom BSP and VPK, and is internally "ReSource". I could not open the page to confirm any of that. The PCGH 2014 summary above says "Half-Life 2" origin, which conflicts, so the base Source branch is **unresolved**.

### Inferences
- Because Respawn says the renderer was replaced and D3D11 is the only shipping API, the Metal work is a D3D11 shim (DXBC shaders, deferred contexts, compute shaders, tiled/forward lighting) rather than anything Source-stock. The TF1 statement of compute-shader use means the shim must support compute (DXBC `cs_5_0`) and likely UAVs.
- A texture-streaming system driven by offline histograms means the runtime streams mips from VPK/RPak at runtime; the Metal runtime must preserve async IO plus GPU upload threading behaviour, but the recompiled code itself will drive this unchanged.
- The TF1 thread model (main, render, workers) is a reasonable prior for TF2 but TF2 may have a task/job system; no public TF2 source describes it.

### Gaps
- Which Valve branch (Portal 2 vs earlier) the engine forked from. Valve wiki unreachable.
- Whether TF2 PC contains a dormant D3D12 renderer (only a search-summary claim).
- Any TF2-specific job-system/threading description, audio-engine description (PCGW says Miles for TF2 vs Wwise/Speex for TF1, which implies an audio swap, but no primary source), or physics (Havok in TF1; PCGW leaves TF2's physics field blank).
- No Respawn engineering blog posts or additional GDC talks found beyond the 2017 streaming talk.

---

## 3. Game logic split: Squirrel scripts vs native code

### Takeaway
A large share of gameplay (mode logic, weapons, AI behaviours, UI) is written in Respawn's modified Squirrel and loaded from the VPKs and compiled to bytecode at load time by native code in `client.dll`/`server.dll`. The scripts are very likely plain text (.nut/.gnut) in the VPKs, but I only have indirect proof. No quantitative native-vs-script split is published.

### Cited Findings
- Squirrel in Titanfall is Respawn's fork, "branched off at version 2.3" of upstream Squirrel (so no generators), with an added optional static type system and often called `squirrel_re` (the name used in Apex Legends); it is "compiled to bytecode when loading." — [Northstar Squirrel docs](https://docs.northstar.tf/Modding/squirrel/) (confirmed from the page text), also [readthedocs mirror](https://r2northstar.readthedocs.io/en/latest/squirrel/index.html)
- Scripts run in three contexts: SERVER, CLIENT and UI; the language is reference-counted with manual GC invocation; coroutines and signals/flags are supported. — [Northstar Squirrel intro](https://docs.northstar.tf/Modding/squirrel/intro/)
- Respawn-specific language features visible in the Northstar headers: an `OT_VECTOR` object type, struct definitions (`SQStructDef`, `SQStructInstance`), typed functions; `SQInteger` is `long` (32-bit on Windows x64), `SQFloat` is `float`. — [vscript/languages/squirrel_re](https://github.com/R2Northstar/NorthstarLauncher/tree/main/primedev/vscript/languages/squirrel_re)
- Script manifest: vanilla scripts are listed in `scripts\vscripts\scripts.rson` inside the game VPK. Northstar reads that manifest, appends its own entries, and writes a merged copy to a compiled-assets directory; each manifest entry pairs a "When" context condition with script paths. — [modscriptsrson.cpp](https://github.com/R2Northstar/NorthstarLauncher/blob/main/primedev/mods/compiled/modscriptsrson.cpp)
- Native entry points Northstar hooks to run scripts: `sq_compilebuffer` (client offset `0x3110` in `client.dll`, as hard-coded) and `sq_compilefile` (`0xF950`), plus `sq_compiler_create`; mod scripts are read from `scripts/vscripts/` on disk as text and compiled by the game's own compiler. — [squirrel/squirrel.cpp](https://github.com/R2Northstar/NorthstarLauncher/blob/main/primedev/squirrel/squirrel.cpp), [squirrelclasstypes.h](https://github.com/R2Northstar/NorthstarLauncher/blob/main/primedev/squirrel/squirrelclasstypes.h)
- Retail single-player maps are separate packages, e.g. `englishclient_sp_crashsite.bsp.pak000_dir.vpk` (BT-7274), `..._sp_sewers1...` (Blood and Rust), `..._sp_boomtown...`, `..._sp_beacon...`, `..._sp_training...` (Pilot's Gauntlet); some mission mappings are marked "unsure" by the wiki. — [Wanty5883/Titanfall2 VPK list](https://github.com/Wanty5883/Titanfall2/blob/main/game-values/file-location/vpk-file-names.md)
- Community-reimplemented server logic: the NorthstarMods repo is "Squirrel scripts used to recreate server-side gamelogic and add custom content" (MIT, last push 2026-09-24). — [R2Northstar org repo list (GitHub API)](https://api.github.com/orgs/R2Northstar/repos?per_page=50)
- Retail trusts the server fully; Northstar's blog describes the Squirrel VM as the binding layer between engine and scripts and notes dev-only script functions (e.g. `DevTextBufferWrite`) existed in retail. — [Northstar blog](https://northstar.tf/blog/vanilla-unrestricted-server-script/)
- A community wiki describes `.nut`/`.gnut` as "NUT & GNUT" Squirrel files with C-like typed syntax (`int function Foo(int x)`, `struct`, `array<T>`, `entity`, `var`). — [Wanty5883 nut/gnut page](https://github.com/Wanty5883/Titanfall2/blob/main/documentation/file-format/nut-and-gnut-squirrel.md)

### Inferences
- Because the shipped native code can compile text buffers/files at runtime (Northstar relies on it), the retail game certainly contains the Squirrel compiler as well as the VM; a recompiled build keeps both. This also means scripts can be extracted, read and (for research) modified, without translating anything.
- Since Northstar's `scripts.rson` approach merges text entries with VPK entries, the vanilla `.nut/.gnut` files are most likely plain text in the VPKs. I did not find anyone stating it outright for the campaign.
- Campaign behaviour (mission flow, AI scripting, titan abilities, UI) lives largely in Squirrel; native code carries engine, AI navigation (`ai_navmesh`), physics, rendering, networking, entity system and the Squirrel natives. Quantification is not published.
- Squirrel VM and compiler are standalone C++ with `long` ints and floats, so they are straightforward static-translation targets (no JIT).

### Gaps
- Percentage or line-count split of native vs script code; no source or doc gives it.
- Whether any shipped script content is precompiled bytecode (I found no evidence either way).
- Exact upstream Squirrel version string inside the binary (docs say "branched off 2.3"; not checked against the binary).

---

## 4. Middleware (separate DLLs vs static) and anti-cheat

### Takeaway
Confirmed separate DLLs: Miles Sound System (`mileswin64.dll`), Bink 2 (`bink2w64.dll`), EA's `OriginSDK.dll`, and NVIDIA GFSDK SSAO/TXAA. PCGamingWiki also lists Qt for UI. OpenSSL is in the process (an Intel-CPU bug report implies it). No anti-cheat appears in any source I opened; Denuvo shipped at launch but was removed in March 2017.

### Cited Findings
- PCGamingWiki middleware fields for TF2: audio Miles Sound System, interface Qt, cutscenes Bink Video; physics blank. — [PCGamingWiki](https://www.pcgamingwiki.com/w/api.php?action=parse&page=Titanfall_2&prop=wikitext&format=json&formatversion=2)
- For Titanfall 1 the same wiki lists physics Havok, audio Wwise and Speex, interface Qt, cutscenes Bink, D3D11, 64-bit. — [PCGamingWiki Titanfall](https://www.pcgamingwiki.com/w/api.php?action=parse&page=Titanfall&prop=wikitext&format=json&formatversion=2)
- Northstar names `mileswin64.dll` and `bink2w64.dll` as game modules it hooks (so Miles and Bink 2 are loaded DLLs). — [audio.cpp](https://github.com/R2Northstar/NorthstarLauncher/blob/main/primedev/client/audio.cpp), [clientvideooverrides.cpp](https://github.com/R2Northstar/NorthstarLauncher/blob/main/primedev/client/clientvideooverrides.cpp)
- `GFSDK_SSAO.win64.dll` and `GFSDK_TXAA.win64.dll` (NVIDIA GameWorks) are dependencies that Northstar must stub for headless servers. A community HUD-removal mod also tells users to drop a replacement `d3d11.dll`, `d3dcompiler_46.dll`, `nvapi64.dll` in `bin\x64_retail` (so the game uses D3DCompiler 46 and nvapi). — [primelauncher/main.cpp](https://github.com/R2Northstar/NorthstarLauncher/blob/main/primedev/primelauncher/main.cpp), [Wanty5883 no_hud_mod](https://github.com/Wanty5883/Titanfall2/blob/main/mods/no_hud_mod.md)
- OpenSSL: PCGamingWiki documents that players on Intel 10th-gen-and-later CPUs cannot join online servers because "the game is affected by the OpenSSL SHA bug"; a community workaround sets the `OPENSSL_ia32cap` environment variable to mask the offending CPU feature. This is a multiplayer-connection issue, but it shows OpenSSL is linked into the game and does CPUID-based dispatch. — [PCGamingWiki](https://www.pcgamingwiki.com/w/api.php?action=parse&page=Titanfall_2&prop=wikitext&format=json&formatversion=2), [starlite0660 fixer README](https://github.com/starlite0660/Online-Feature-Fixer-for-Titanfall)
- Anti-cheat: PCGamingWiki's page lists none; a grep of the Northstar source for EasyAntiCheat/anticheat/Denuvo/drm returns no relevant hits. Northstar adds its own server-side ban/auth logic (`server/auth/bansystem.cpp`) because the vanilla server trusts clients. — [PCGamingWiki](https://www.pcgamingwiki.com/w/api.php?action=parse&page=Titanfall_2&prop=wikitext&format=json&formatversion=2), [NorthstarLauncher tree](https://github.com/R2Northstar/NorthstarLauncher/tree/main/primedev/server)
- Input: Northstar names `XInput9_1_0.dll`, `XInput1_3.dll`, `XInput1_4.dll` (controller support is via XInput). — [NorthstarLauncher source](https://github.com/R2Northstar/NorthstarLauncher/tree/main/primedev)

### Inferences
- Replacement runtime scope: Miles (proprietary, closed) and Bink 2 (proprietary, closed) are external x64 DLLs, so a Mac port can either translate them too (they are x86-64 binaries; Bink 2 and Miles have heavy SIMD) or stub the interfaces and substitute audio/video playback. The cleanest boundary is the DLL export surface. No evidence of Oodle in the sources I opened; Oodle (or LZHAM/zstd) is likely used inside VPK/RPak but unconfirmed.
- Qt appears only in PCGamingWiki's field; Qt may be a leftover in the TF1 lineage or used for tooling. Unverified for the TF2 retail install.
- No anti-cheat means no kernel-driver or integrity-checking obstacle for single-player.
- Statically linked OpenSSL with hand-written asm (AES-NI, SHA-NI, AVX2 paths gated by CPUID) means a CPUID shim that reports a minimal feature set would steer execution to plain-C/SSE2 paths and avoid translating the wide-ISA variants.

### Gaps
- Whether Havok (or another physics SDK) is in TF2; whether Oodle/LZHAM is used; whether Steamworks (`steam_api64.dll`) is shipped; real complete list of third-party DLLs. No primary source.
- Qt usage in TF2 is unverified outside PCGamingWiki.

---

## 6. Community reverse-engineering resources (URL, licence, activity)

### Takeaway
Northstar is the one actively maintained, well-documented RE-adjacent project for TF2 (MIT, weekly commits through 2026-10-08), but it documents hook addresses and a subset of structs, not a symbol map. No public Ghidra/IDA databases or PDB-style symbol maps exist in any repo I could find. Sibling-engine projects (R1Delta for TF1, r5sdk for Apex) are the best secondary struct references. Asset tooling (rsx, RePak, VPK tools) is current.

### Cited Findings
**Northstar family** (all licence data from GitHub API; activity as of 2026-10-08)
- NorthstarLauncher: <https://github.com/R2Northstar/NorthstarLauncher>. MIT. Created 2021-07-08, last push 2026-10-08, 318 stars, 134 forks, 60 open issues, not archived. Releases: v1.31.14-rc2 (2026-09-24), v1.31.14-rc1 (2026-09-04), v1.31.10 (2026-07-09). 82 commits to `main` since 2025-01-01; top authors by commit count: Jack (18), cat_or_not (14), Rémy Raes (12), Allusive (10), NachosChipeados (6). — [GitHub API](https://api.github.com/repos/R2Northstar/NorthstarLauncher), [releases](https://api.github.com/repos/R2Northstar/NorthstarLauncher/releases?per_page=5)
- Source layout (`primedev/`): `client`, `server` (incl. `ai_navmesh`, `ai_helper`, auth/bans), `engine` (`r2engine.h`, hoststate), `materialsystem`, `rtech` (pak files, RUI), `squirrel`, `vscript/languages/squirrel_re`, `tier1` (cmd/convar/cvar), `core` (hooks, tier0/tier1, filesystem, memalloc, math, bitbuf), `origin`, `plugins`, `wsockproxy`, `primelauncher`, `dedicated`, `thirdparty` (libcurl, minhook, minizip, rapidjson, silver-bun, spdlog). — [primedev tree](https://github.com/R2Northstar/NorthstarLauncher/tree/main/primedev)
- What it documents (as C++ declarations): engine command buffer (`Cbuf_AddText`, `cmd_source_t`), `NET_SendPacket`/`NET_SendTo`/`NET_Encrypt` signatures, partial `IFileSystem` and tier0/memalloc vtable layouts, ConVar/ConCommand types, Squirrel VM internals (`SQVM`, `SQObject`, `SQTable`, `SQClosure`, struct types), `CModule` address helpers, and RPak/rtech hooks. Addresses are hand-resolved offsets/patterns per module. — [r2engine.h](https://github.com/R2Northstar/NorthstarLauncher/blob/main/primedev/engine/r2engine.h), [core/filesystem/filesystem.h](https://github.com/R2Northstar/NorthstarLauncher/blob/main/primedev/core/filesystem/filesystem.h), [core/tier0.h](https://github.com/R2Northstar/NorthstarLauncher/blob/main/primedev/core/tier0.h)
- NorthstarMods: <https://github.com/R2Northstar/NorthstarMods>, MIT, Squirrel, 165 stars, last push 2026-09-24. Northstar (packaged releases): <https://github.com/R2Northstar/Northstar>, MIT, 1,787 stars, last push 2026-10-02. NorthstarDocs: MIT, last push 2026-07-04. NorthstarWiki: MIT, last push 2024-10-04. NorthstarStubs: Zlib, last push 2022-02-17. Atlas (Go master server): MIT, last push 2026-01-16. NorthstarMasterServer: archived. — [org repo list (GitHub API)](https://api.github.com/orgs/R2Northstar/repos?per_page=50)
- Docs site <https://docs.northstar.tf/Wiki/> has sections on modding, server hosting, installation, a "Reverse engineering" section, native server APIs (collision, entity creation, debug drawing, cvars), Squirrel C++ API (object manipulation, stack ops), and file-format guides (RPak, VTF, MDL, BIK, shader modding). The RE intro URL I guessed returned 404, so I could not read the RE section's content. — [docs.northstar.tf](https://docs.northstar.tf/Wiki/)
- Wanty5883/Titanfall2 (modding/datamining wiki repo): <https://github.com/Wanty5883/Titanfall2>, no licence, 74 stars, last push 2022-05-25 (stale). Content is modding (VTF/VMT/KeyValues/VPK names/weapon configs/ripping), not binary RE. Contributors list credits p0358, BobTheBob (RE/modding research). — [GitHub search API](https://api.github.com/search/repositories?q=titanfall+reverse+engineering), [contributors.md](https://github.com/Wanty5883/Titanfall2/blob/main/contributors.md)

**Titanfall 1 / Titanfall Online**
- R1Delta (TF1 custom server framework + mods): <https://github.com/r1delta/r1delta>. GPL-3.0. Created 2024-01-05, last push 2026-10-03, 155 stars, C++; README reports "3.0 prerelease 38" (WebFetch summary) and says it builds on the Source SDK, GameNetworkingSockets (BSD-3) and NorthstarLauncher (MIT). Repo tree includes `ida/defs.h` (only the stock Hex-Rays `defs.h` header, NOT a database), `sdk/` (bitbuf, keyvalues, netadr), `engine/` (cvar, dedicated, precache, net, `vtable_remaps/`, `vtable_tools`), `shared/squirrel.cpp`, `shared/persistentdata*`, and stub folders (`bugtrap_stub`, `coherent_noui_stub`, `materialsystem_nodx_proxy`, `northstar_stubs`). Related repos in the org: CORE (Squirrel server scripts/navmeshes), masterserver2 (Go), docs, revpk-mutated (utility), VTFLib fork (LGPL-2.1); r1delta_installer is archived. — [GitHub API org list](https://api.github.com/orgs/r1delta/repos?per_page=30), [README](https://github.com/r1delta/r1delta), [repo tree](https://github.com/r1delta/r1delta)
- TFORevive / Titanfall Online (Nexon Asia F2P variant): the GitHub org `TFORevive` is public but I only saw infra forks there (minhook fork, vaudio_lyra, sentry-native, gperftools fork, webkit-inspector, chii; none with a game-RE database). Related tools: `harmonytf/UnCSO2` (GPL-3.0, extracts CS:Online 2 / Titanfall Online files, last push 2025-11-10), `harmonytf/vscode-squirrel-linter`. Maintainer p0358 also publishes `black_market_edition` (TF1 mod, LGPL-2.1, 67 stars, last push 2026-02-17) and `FakePDB` (Apache-2.0, "Tool for PDB generation from IDA Pro database", last push 2026-02-24). — [TFORevive org (API)](https://api.github.com/orgs/TFORevive/repos?per_page=30), [p0358 repos (API)](https://api.github.com/users/p0358/repos?per_page=40&sort=pushed), [harmonytf org (API)](https://api.github.com/orgs/harmonytf/repos?per_page=30)

**Apex Legends (same engine family, later)**
- r5sdk: <https://github.com/Mauler125/r5sdk>. "The Source SDK for Apex Legends' engine." 600 stars, 97 forks, ~5,471 commits (README page), created 2021-04-13, last push **2025-06-22**, no SPDX licence; the `license/` folder holds Valve's "Source 1 SDK LICENSE" plus third-party notices. Targets the Apex Season 3 build `R5pc_r5launch_N1094_CL456479_2019_10_30_05_20_PM`; warns against use on live servers (ban risk). Activity: quiet on the main repo since mid-2025; an org fork `R5Reloaded/r5sdk` (default branch `p4sync`, 6 stars) was pushed 2026-08-03. — [GitHub page](https://github.com/Mauler125/r5sdk), [API](https://api.github.com/repos/Mauler125/r5sdk), [R5Reloaded/r5sdk](https://api.github.com/repos/R5Reloaded/r5sdk), [license folder](https://api.github.com/repos/Mauler125/r5sdk/contents/license)
- R5Reloaded org also has `bspconv` ("convert Respawn's BSP map files between different versions", AGPL-3.0, last push 2025-06-18), `docs` and `R5RBaker`. — [R5Reloaded org (API)](https://api.github.com/orgs/R5Reloaded/repos?per_page=30&sort=pushed)

**Asset-format tooling**
- rsx ("reSource Xtractor"): <https://github.com/r-ex/rsx>. AGPL-3.0, created 2024-05-08, last push 2026-10-07, 141 stars; releases 2.4.0 (2026-10-07), 2.3.0 (2026-09-15), 2.2.1 (2026-08-11). README: parses and converts RPak files and Titanfall 2 model files for Titanfall, Titanfall 2 and Apex; docs folder is limited. — [API](https://api.github.com/repos/r-ex/rsx), [README](https://github.com/r-ex/rsx)
- Legion+ (`r-ex/LegionPlus`): GPL-3.0, **archived**, last push 2025-06-02, 249 stars; predecessor `dtzxporter/Legion` archived 2021-12-09. Its role is taken over by rsx. — [API](https://api.github.com/repos/r-ex/LegionPlus)
- RePak: <https://github.com/r-ex/RePak>, MIT, last push 2026-06-28, 41 stars; compiles assets into RPak for TF2 (Northstar) and Apex. — [API](https://api.github.com/repos/r-ex/RePak)
- VPK tools: `pg9182/tf2vpk` (MIT, Go, last push 2025-02-16; list/extract/repack with load/texture flags; "designed for r2 VPKs"), `taskinoz/RSPNVPK` (MIT, last push 2026-10-03), `harmonytf/HarmonyVPKTool` (GPL-3.0, last push 2024-08-07), `Unordinal/UnoVPKTool` (abandoned 2020). — [tf2vpk](https://github.com/pg9182/tf2vpk), GitHub search API results for "titanfall vpk"
- Other formats: VTF (Valve texture), BSP (Respawn variant; `bspconv` handles version conversion), MDL (Respawn variant; rsx handles TF2 models), BIK (Bink 2 video), RPak. — [Northstar docs index](https://docs.northstar.tf/Wiki/), [titanfall.wiki.gg](https://titanfall.wiki.gg/wiki/Source)

**Databases / symbol maps**
- GitHub repository searches I ran ("titanfall ghidra", "titanfall ida", "titanfall symbols/pdb/idb", "titanfall sdk reversed") returned no repository containing a published Ghidra project, IDB, or PDB for Titanfall 2. Searches for a Ghidra project returned nothing at all. — [GitHub search API, queries as stated]
- Search summary only (unverified): the Northstar Discord (reported at ~74k members) is where RE knowledge is exchanged. — [search result for northstar.tf](https://northstar.tf/)

### Inferences
- The most reusable public artefact is the Northstar source itself: a curated, MIT-licensed set of roughly 400 hand-resolved code addresses plus struct/vtable shapes for the final retail TF2 build. Its licence permits reading and reuse with attribution.
- R5/Apex structures are newer; layouts may differ from TF2 but class hierarchies (CBaseEntity, CServerGameDLL, IFileSystem, etc.) often align via the Source SDK 2013 lineage.
- No community IDB/Ghidra DB being public means the project would start from the retail binaries with only Northstar's addresses as labels. FakePDB (IDA to PDB) could be a useful export bridge if the user builds their own IDB.
- The r5sdk licence situation is murky (GitHub shows no licence, the folder is Valve's Source 1 SDK licence), so treat it as reference reading, not code to import.

### Gaps
- Content of Northstar's own "Reverse engineering" docs section (could not load).
- What `Syampuuh/Titanfall2` ("Titanfall 2 client tracking", 83 stars, last push 2018-01-19) actually contains (not opened; rate-limited).
- Whether any private/Discord-shared IDBs exist (cannot be determined from public sources).
- Activity data for sub-projects beyond the headline repos (issue/PR cadence not sampled).

---

## 7. Debug symbols and RTTI

### Takeaway
I found no evidence of any legitimately distributed Respawn-engine build with PDBs or symbols, and no public statement about whether the PC retail DLLs keep MSVC RTTI names. The community's workflow (hand-written vtables, hard-coded offsets) suggests symbols are not available, but that is indirect.

### Cited Findings
- Northstar's headers hand-declare vtable structs (e.g. `struct VTable` in `core/tier0.h` and `core/filesystem/filesystem.h`) and call into engine functions through offsets and byte patterns, rather than through exported names or PDB/RTTI lookups. — [core/tier0.h](https://github.com/R2Northstar/NorthstarLauncher/blob/main/primedev/core/tier0.h), [core/filesystem/filesystem.h](https://github.com/R2Northstar/NorthstarLauncher/blob/main/primedev/core/filesystem/filesystem.h)
- R1Delta ships `vtable_tools` and `vtable_remaps/` to adapt vtables between versions, again by structure/offset. — [r1delta/engine](https://github.com/r1delta/r1delta/tree/main/engine)
- Retail dedicated servers: the dedicated-server path in Northstar uses the same client-install DLLs plus community stubs for `d3d11.dll` and GFSDK libraries; no Respawn-shipped Linux or PC dedicated server binary for TF2 is documented in anything I opened. Dedicated hosting on Linux is done through Wine/Proton. — [primelauncher/main.cpp](https://github.com/R2Northstar/NorthstarLauncher/blob/main/primedev/primelauncher/main.cpp), [Northstar FAQ](https://docs.northstar.tf/Wiki/faq/)
- The Source SDK 2013 (Valve's public SDK) is the legitimate public reference for tier0/tier1/vstdlib/filesystem class layouts; r5sdk's licence folder is Valve's Source 1 SDK licence. — [r5sdk license folder](https://api.github.com/repos/Mauler125/r5sdk/contents/license)
- Source-code leaks: I found no reliable report of a Respawn engine source or symbol leak. A search result mentions a reported 2020 leak of Valve's own Team Fortress 2 and CS:GO source (search summary only, unverified). Per the brief, any leaked code or symbols are off the table and I did not look for them.

### Inferences
- If the retail DLLs carried MSVC RTTI, Northstar developers would likely have used `.?AV` type names to find vtables instead of hand-written offsets; the absence in the source of any RTTI-based lookup hints RTTI may be stripped (`/GR-`) or simply that offsets were quicker. Not proof either way.
- Console builds (PS4/Xbox One TF2, Xbox 360 TF1, Titanfall Online client builds) are the usual places such symbols leak or ship, but no public source I opened says any legitimately distributed one carries symbols.

### Gaps
- Direct inspection of retail DLLs for RTTI (`.?AV` strings, `type_info`) is needed; I did not and could not do this from public documents.
- Any statement about console or tech-test build symbols; none found.

---

## 8. Running on non-Windows platforms today

### Takeaway
On Linux the game works under Proton (ProtonDB Gold, Steam Deck "Playable", EA app is the pain point). On Apple Silicon Macs it runs through CrossOver at roughly 40-60 FPS in the campaign at 1080p according to secondhand summaries, with the EA app as the recurring failure; I could not open any of the primary Mac pages. No TF2 data found for Windows-on-Arm or GPTK/Whisky.

### Cited Findings
- ProtonDB summary (API, queried 2026-10-08): tier gold, confidence strong, 815 total reports, trending tier gold, best reported tier platinum, score 0.76. — [ProtonDB API](https://www.protondb.com/api/v1/reports/summaries/1237970.json)
- Steam Deck: category 2 ("Playable"); issues flagged: launcher interaction, text input does not auto-open the keyboard, first-time setup needs internet, single-player needs internet; positives: default controller config works, glyphs match, text legible, default settings perform well. Recommended runtime `proton-experimental`, `requires_internet_for_singleplayer: 1`, tested build 9911828 on 2023-05-12. — [Steam Deck endpoint](https://store.steampowered.com/saleaction/ajaxgetdeckappcompatibilityreport?nAppID=1237970), [api.steamcmd.net](https://api.steamcmd.net/v1/info/1237970)
- Northstar Linux/Deck docs: start with Proton 6.3-8 or 7.0-6 then move to GE-Proton; EA app under Wine can be unstable and can "corrupt your install" (fix is deleting compatdata `1237970`); EA app showing a blank window under Proton Experimental can be fixed by installing `d3dcompiler_43` and `d3dcompiler_47`; DXVK shader compilation causes initial stutter (precompiled caches exist); ReShade/DXVK leftovers in `bin/x64_retail` break init. — [Northstar Steam Deck/Linux page](https://docs.northstar.tf/Wiki/steamdeck-and-linux/installing-on-steamdeck-and-linux/), [Linux troubleshooting](https://docs.northstar.tf/Wiki/steamdeck-and-linux/linux-troubleshooting/)
- DXVK (D3D11 to Vulkan) is therefore the proven translation path for the renderer on Linux, supporting that the D3D11 feature set the game uses is mapped by existing open-source layers. — same pages above
- Mac, search summary only (unverified; AppleGamingWiki, CodeWeavers and macgamingdb all returned 403): CrossOver rated "Playable"; campaign 40-60 FPS at 1080p; multiplayer high framerate with settings turned down; one user reported an M4 Max at Ultra with `D3DMetal` at ~60 FPS, another an M4 base Mac mini at 2560x1440 Ultra with rare stutter; the EA app reportedly works in the CrossOver Preview build but not CrossOver 25 "at the time of writing"; one user saw a blank EA login window; Parallels on an M1 Air is playable at low frame rates and may need Windows 8 compatibility mode. All anecdotal and undated in the summary.
- DoesItARM page (opened): status "Unknown, more info needed"; links only to videos of CrossOver 21 beta 1 (campaign and multiplayer) and Parallels 16.5 on an M1 MacBook Air. — [doesitarm.com/game/titanfall-2](https://doesitarm.com/game/titanfall-2)
- Windows on Arm: no Titanfall 2 compatibility entry found. General context (search summaries, unverified): Microsoft's Prism emulator was updated to translate AVX/AVX2/BMI/FMA/F16C. — [Tom's Guide](https://www.tomsguide.com/computing/laptops/windows-on-snapdragon-breakthrough-new-emulator-update-will-fix-our-biggest-gripe-with-arm) (search result only)
- GPTK/Whisky: no Titanfall 2-specific report found in any source I could open.

### Inferences
- The Mac baseline a native build has to beat is "runs under CrossOver/D3DMetal at ~60 FPS on recent M-series chips, but only if the EA app can be made to work". The EA app is the one thing a native port can eliminate (by stubbing OriginSDK), which is a stronger selling point than raw frame rate.
- Steam Deck runs unmodified x86 so it tells us nothing about translation of the game's threads. The CrossOver reports on M1 through M4 suggest Rosetta 2-class translation copes with the threading and the game's SIMD, but only secondhand.
- Prism's reported AVX support means a Windows-on-Arm baseline is plausible but unmeasured for this title.

### Gaps
- Dated, hardware-specific FPS numbers from primary pages (CodeWeavers, AppleGamingWiki, macgamingdb, r/macgaming) and what breaks in the current CrossOver release.
- GPTK/Whisky status for this title; whether Whisky is still maintained (not checked; rate-limited).
- Any Windows-on-Arm / Snapdragon X report.

---

## 9. Prior attempts at a native port, decompilation or recompilation

### Takeaway
I found no announced or attempted native port, decompilation or static recompilation of Titanfall 2 or Titanfall 1 (including the Xbox 360 PowerPC version). All public work is runtime hooking and reimplementation around the retail binaries.

### Cited Findings
- GitHub repository searches for combinations of "titanfall" with recomp, decomp, recompilation, native port, xenon, XenonRecomp, and "source engine static recompilation" returned no matching Titanfall projects. The only Titanfall repos surfaced were Northstar, R1Delta, asset tools and mods. — [GitHub search API](https://api.github.com/search/repositories?q=titanfall+recomp+OR+decomp+OR+recompilation&per_page=20)
- Web searches for Titanfall 2 static recompilation/decompilation/native port returned only generic recomp news (PS2Recomp, Zelda 64: Recompiled, Vice City Stories port) and nothing Titanfall-specific. — [Time Extension article from the result set](https://www.timeextension.com/news/2026/01/native-pc-ports-of-ps2-games-could-be-on-the-way-thanks-to-new-recompilation-experiment) (search result only)
- Web search for Titanfall 1 on Xbox 360 with Xenia/recompilation found only general Xenia and Xbox 360 recompilation material; no Titanfall 360 project. — [search result set including mintlify Xenia docs and evoweb thread](https://evoweb.uk/threads/xbox-360-recompilations.106908/) (search result only)
- Nearest analogues: Northstar (runtime injection into retail TF2, MIT), R1Delta (the same for TF1, GPL-3.0), and r5sdk (an "SDK" built around Apex's engine from Valve's Source 1 SDK code, GitHub shows no licence). None translates or replaces the engine wholesale. — repos listed in Q6

### Inferences
- The project would be first of its kind for this engine family. The usable prior work is address maps, struct shapes and asset-format tooling, not code lifts.
- The Xbox 360 TF1 route (PowerPC, Xenon GPU, an earlier engine version) has no public groundwork that I could find, and it targets a different game from the TF2 campaign, so it offers no shortcut for this project.

### Gaps
- Private or Discord-only efforts cannot be ruled out. My search was not exhaustive (GitHub rate limit; Reddit and many forums not reachable).
