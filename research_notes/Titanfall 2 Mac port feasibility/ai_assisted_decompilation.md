# AI-assisted and agent-driven game decompilation, 2024 to Oct 2026: evidence for scoping a Titanfall 2 (MSVC x64 C++) port

Research date: 2026-10-08. All figures are quoted as the source gives them, with the source date. "Fetched" means I opened the page. Items marked UNVERIFIED rest only on a search-result snippet or a page I could not open. Model names (GPT-6 Astra, Claude-Fable-5.1, Codex 5.5, and so on) are reproduced as the sources give them. Several 2026 items come from aggregator sites (heldgames.com in particular) and are labelled secondary.

---

## 1. Matching-decompilation projects that completed or advanced in 2024-2026

### Takeaway
Byte-matching decomps have completed at a fast clip in 2025-2026 on N64, GameCube, DS and PS1 (months to ~6 years, small to mid-sized teams, increasingly with agent help). The completed PC examples are all 32-bit MSVC 4.2 to 7.1 titles. I found no x64 MSVC matching project.

### Cited Findings
**Console projects and timelines**
- Snowboard Kids 2 (N64): first commit September 2024; announced 100% decompiled May 17, 2026, "a little under two years." The author and four named contributors (Bl00D4NGEL, inspectredc, SlaveOfIDO, queueRAM) are credited. No total function count was stated in the post. — [Chris Lewis, SK2 is 100% decompiled (2026-05)](https://blog.chrislewis.au/snowboard-kids-2-is-100-decompiled/)
- The SK2 repo shows 7,526 commits and contains CLAUDE.md, AGENTS.md and GEMINI.md. It requires the user's own copy of the game and ships no assets or assembly. — [cdlewis/snowboardkids2-decomp](https://github.com/cdlewis/snowboardkids2-decomp)
- Animal Crossing (GameCube): reported June 17, 2025 as 100% matching and linked game code after 2.5 years. The article gives no team size and no AI mention. SDK libraries were still unfinished. — [Time Extension, 2025-06-17](https://www.timeextension.com/news/2025/06/after-two-and-a-half-years-gamecube-animal-crossing-is-almost-fully-decompiled-let-the-modding-commence)
- Super Smash Bros. Melee (GameCube): reported 100% on September 9, 2026. The project ran from July 2020, about six years. It covers 3.88 MB of code and 1.21 MB of data. Team size is not given. This comes from a secondary aggregator that does not link primary docs. — [heldgames, Melee decompiled (2026-09)](https://heldgames.com/guides/melee-decompiled-ai-astra)
- The doldecomp/melee GitHub page shows a "Linked Progress" badge but no exact numbers and no visible AI policy in the overview. — [doldecomp/melee](https://github.com/doldecomp/melee)
- A secondary roundup dated September 2026 lists:
  - Star Fox Adventures (GameCube): code and data 100%, about 7 months (Feb-Sept 2026).
  - Kingdom Hearts 358/2 Days (DS): 100%, about 3 months (June-Sept 2026).
  - Legend of Mana (PS1): US 100%, Japan 94.2%.
  - Super Mario Galaxy (Wii): code 100%, data 80.6%; AI explicitly rejected for decomp work.
  - Fire Emblem: The Sacred Stones (GBA): code 99.76%, started 2018.
  - The page notes AI use "not mentioned" for most of these. It is an aggregator, so the figures need primary confirmation. — [heldgames, games 100 percent decompiled (2026-09)](https://heldgames.com/guides/games-100-percent-decompiled-what-next)
- Klonoa: Empire of Dreams (GBA, 2001): 51% of code bytes, 663 functions identified, agbcc (GCC 2.95 fork), Claude Code as primary AI tool. Dated August 17, 2026. — [Macabeus, Starting a Decompilation Project from Zero](https://gambiconf.substack.com/p/starting-a-decompilation-project)

**PC (Windows) matching projects**
- LEGO Island: the current GitHub README says CONFIG.EXE, ISLE.EXE and LEGO1.DLL are "completely decompiled" and that all three retail binaries were reproduced byte for byte. The reproduction build ("ReproBit") uses the original Microsoft Visual C++ 4.2 toolchain. 1,899 commits. Version 1.1 English. Completion date not found. — [isledecomp/isle](https://github.com/isledecomp/isle)
  - Conflict: an older README (seen only in a search-result snippet, page not opened, UNVERIFIED) said the binaries were not byte-for-byte because of "complexities with regard to the compiler." The project has apparently moved from "functionally identical" to byte-exact since then. — [isle README, older commit](https://git.mattkc.com/isledecomp/isle/raw/commit/b03d67e34286810d55d395b7add2e71c1eee0817/README.md)
- Colin McRae Rally 2.0 (PC, 2000): MSVC 6.0 SP3, 3,364 game functions identified, byte-compared with reccmp. The README says most of the source, tooling and docs were written by Claude and Codex under human direction. The rebuilt exe "boots, runs the menus and drives rallies" on the original game data, with "rough edges." — [pablocpas/CMR2Decomp](https://github.com/pablocpas/CMR2Decomp)
- Summoner (PC, 2001, Volition engine): MSVC 6 `cl` 12.00.8804 with `/O2`. The README says most C in `src/` "was drafted with Cursor's coding agent and checked with objdiff." 86 commits. — [AARosson48/SummonerDecomp](https://github.com/AARosson48/SummonerDecomp)
- Thief: Deadly Shadows (PC, 2004): MSVC 7.1, objdiff comparison, a `tools/agent/` per-function loop ("claim a function, get its context, try a candidate, pass the strict gate"). 257 commits. Progress numbers not in the fetched text. — [Veradictus/Thief3-Decomp](https://github.com/Veradictus/Thief3-Decomp)
- Dance Central 3 (Xbox 360, MSVC targeting PowerPC): see Q6. It is the largest AI-driven case I found.

**Aggregators**
- game-decomp-compendium (maintainer madpai) is a knowledge base for AI agents: 702 decompilations, 72 tools, a "decomp-matching-workflow" skill covering "compiler idioms (MSVC/MWCC/GCC/IDO)." All of this is the maintainer's own claim, with no breakdown by x64 versus x86. — [madpai/game-decomp-compendium](https://github.com/madpai/game-decomp-compendium)

### Inferences
- Duration is dropping: SFA at about 7 months and KH 358/2 at about 3 months (secondary source) versus 2.5 years for Animal Crossing and 6 years for Melee. This is consistent with better tooling, and with agents in some cases, but none of these sources attributes the speed-up to agents.
- All PC matching examples found target 32-bit x86 and MSVC 4.2 to 7.1. The jump to a 2016-era x64 toolchain has no precedent in what I found.

### Gaps
- No primary-source numbers for SFA, KH 358/2, Legend of Mana (decomp.dev pages not opened).
- Team sizes for Melee, SFA, Animal Crossing, KH 358/2 not found. LEGO Island completion date and start date not confirmed from an opened page.
- No Mario Party, PS2 or other GameCube/Wii agent-experiment write-ups found beyond those cited. A search snippet mentioned Mario Party 9 and Star Ocean PS2 projects, but I did not open them.

---

## 2. What LLMs and agents have measurably changed in matching decomp

### Takeaway
Agents driven by a byte-match oracle took Snowboard Kids 2 from about 25% to about 75% matched in a few months and ultimately to 100%. The gains were largest on small and medium functions and stalled on a long tail of large, math-heavy or macro-heavy functions. Practitioners report modest flat-rate subscription costs, but no per-function dollar figures exist in the sources I could open.

### Cited Findings
**Chris Lewis, Snowboard Kids 2 (N64, GCC 2.7.2, C89)**
- 2025-11-04: workflow of identify function, try matching code, bring in agents when stuck, diff and iterate. Claude and Codex were run in parallel and Claude outperformed Codex overall. Agents were given build/diff (0-100% score), disassembly, object-file diff with register normalisation, and line mapping. Hindrances: arithmetic errors, struct-offset calculations, repeated C89 violations, complex control flow. — [Using Coding Agents to Decompile N64 Games (2025-11-04)](https://blog.chrislewis.au/using-coding-agents-to-decompile-nintendo-64-games/)
- 2025-11-27 ("one-shot"): Claude Code headless in a loop, with Opus 4.5. The author made "more progress" in three weeks than in the preceding three months. Components: a scorer that picks the next function (logistic regression; initial formula `instruction_count + 3*branch_count + 2*jump_count + 2*label_count + stack_size`, stack_size later dropped as non-predictive), a headless Claude invocation, Unix-style tools (build-and-verify.sh and the like), and a bash driver. Claude stops after 10 failed attempts per function; it has been left "8+ hours unattended." Opus matched five of seven functions Sonnet 4.5 had been deemed too hard to match. About 79% of functions estimated matchable. 62.7% of calls go from higher to lower complexity. Tools used: Splat, m2c, decomp-permuter, asm-differ. A brief Codex trial did poorly with git operations. — [The Unexpected Effectiveness of One-Shot Decompilation with Claude (2025-11-27)](https://blog.chrislewis.au/the-unexpected-effectiveness-of-one-shot-decompilation-with-claude/)
- 2026-02-16 (long tail): match rate rose from about 25% to 58% with the initial one-shot approach and then to about 75% after workflow changes, then stalled. Unmatched functions: 157 in early January, 124 by publication. Hard cases: Claude "more or less gives up immediately" on functions over 1,000 instructions, display-list/graphics macros, and matrix/vector math. One outlier needed 87 attempts. What unlocked progress:
  - Function-similarity prioritisation (text embeddings, Coddog).
  - Specialised skills such as F3Dex2 documentation.
  - Cleanup loops.
  - Hooks as guardrails against destructive agent actions.
  - Routing mechanical work to GLM to save Opus tokens.
  Primary model: Opus on a "20x Max plan" (flat subscription, so no per-function cost). — [The Long Tail of LLM-Assisted Decompilation (2026-02-16)](https://blog.chrislewis.au/the-long-tail-of-llm-assisted-decompilation/)
- 2026-05-17 (done): Claude, GLM and Codex all effective. For the final 10 functions, "Codex 5.5 xhigh" appeared most effective; GLM was best value. The post gives no function totals, no per-day rates and no costs. — [SK2 is 100% decompiled](https://blog.chrislewis.au/snowboard-kids-2-is-100-decompiled/)
- Commentary (Hacker News on the one-shot post, late 2025): "Claude kicks ass" for cleaning decompiled code; success needs narrow scope with a clear validation criterion; the ten-attempt threshold is only partly effective (Claude sometimes iterates dozens of times); concerns about DMCA/copyright. — [HN item 46080498, via mirror](https://hn.svelte.dev/item/46080498)
- Commentary (HN on the SK2 100% post): one contributor used Claude to build a custom permuter for the Melee decomp; another used Claude to add Ghidra extensions for Xbox/Wii instructions while porting Rock Band 3 and Dance Central 3 to PC; skeptics say LLMs are "useful, but complements existing tooling & falls short on the hard part," and that vibed ports have not yet produced a "real success." No MSVC/x86 discussion in the thread. — [HN item 48284494, via mirror](https://hn.svelte.dev/item/48284494)

**Macabeus (GBA/N64) experiments**
- 2025-05-20: Sonic Advance 3 (GBA) in January 2025. Claude Sonnet 3.5 reached a 96% match on a first try for a simple function; DeepSeek R1 baseline about 60%, 100% after project macros were supplied. The Marun enemy module was 0.3% of SA3 progress. Limits: missed project-specific macros, needs good example functions. — [Development Journey on Game Decompilation Using AI](https://gambiconf.substack.com/p/development-journey-on-game-decompilation)
- 2026-03-10 benchmark: Claude Sonnet 4.6 on 60 functions (30 Sonic Advance 3 GBA/Thumb, 30 Animal Forest N64/IDO). Best run 80%, average over 6 runs 74%. 88% of functions gave identical outcomes across runs. About half of matches came on the first AI attempt; after 3 failed attempts the chance of eventual match fell to about 25%. 12 functions (5 SA3, 7 AF) failed in every run. Settings: 3-10 minute timeouts, 12 attempts max per function. API throughput fell from 57 to 11 tokens/s in late runs. — [Can LLMs Really Do Matching Decompilation? (2026-03-10)](https://gambiconf.substack.com/p/can-llms-really-do-matching-decompilation)
- Mizuchi (open source, TypeScript): plugin pipeline using Claude, decomp-permuter, m2c, a compiler plugin and objdiff, for GBA, N64 and PS1. 76 stars. No match rates or costs published. — [macabeus/mizuchi](https://github.com/macabeus/mizuchi)
- Klonoa (2026-08-17): human roles stayed at project structure, boundary validation, review/merging and "guidance on AI cheating prevention." Limitations: larger functions are harder, the second half will be harder, "cognitive debt" risk. — [Macabeus, Klonoa post](https://gambiconf.substack.com/p/starting-a-decompilation-project)

**Other**
- PSXDecomp (Claude Code plugin that bootstraps PS1 matching-decomp projects): README disclaims that LLMs "produce negligible decompilation results without good guidance." Four bootstrap "backtests" dated September 30 to October 3, 2026. No quantified match rates. — [anzaldoivan/psxdecomp](https://github.com/anzaldoivan/psxdecomp)
- Melee: secondary source says GPT-6 Astra (released September 3, 2026) closed the final stretch within days, but "the team had already built the tooling... and matched the overwhelming majority of the code." One contributor also credits Claude. No function count for the AI contribution. — [heldgames, Melee (2026-09)](https://heldgames.com/guides/melee-decompiled-ai-astra)
- Academic harness: "Matching Decompilation as a Verifier-Guided Task for Human-Centered Coding Agents" (ICML 2026 workshop). The page describes a verifier-guided harness that turns existing matching-decomp repos into durable agent goals; the fetched page had no numbers. A search snippet claimed the agentic setup underperformed independent per-function LLM queries; UNVERIFIED (OpenReview page blocked). — [ICML 2026 page](https://icml.cc/virtual/2026/82758)
- Echo (arXiv 2609.18706, 2026-09-16): reports "2.43x more exact matches" than the strongest baseline, and on the Mirai binary 2.75x and 7.4x as many matched functions as GPT-5.6 and Codex. Compilers and platforms not stated in the fetched abstract. — [Echo, arXiv](https://arxiv.org/abs/2609.18706)

### Inferences
- The effective recipe is: (a) a mechanical byte-match oracle with percent score, (b) a headless loop with a per-function attempt cap (10-12), (c) a scheduler that orders functions easy-to-hard, (d) permuter and m2c as helpers, (e) hooks/guardrails and anti-cheating rules. The agent's judgment is secondary to the oracle.
- Rough tail throughput from Lewis's numbers: 157 to 124 unmatched is about 33 functions in about 6 weeks at the hard end, though the posts do not say how much effort that took. That is my arithmetic, not a stated rate.
- Macabeus's attempt curve (about half of matches on attempt 1, about 25% chance after 3 failures) supports a cheap fixed attempt budget per function and a long list of "parked" hard functions.
- Not one source reports a dollars-per-matched-function figure, so a cost model for a big binary cannot be built from this material.

### Gaps
- No per-function cost, token count or functions-per-day figure in any opened source.
- Total function counts for SK2 not stated; match-rate history only from the long-tail post.
- Primary Mario Party, GameCube/Wii and PS2 agent write-ups not found beyond the above. decomp.me AI features were searched for; I found no confirmed first-party decomp.me AI feature (only Macabeus's note that he attempted AI integration with decomp.me).
- MCP servers for Ghidra exist (the dc3 README and the Quesma post below both use them), but I did not open any MCP server's own docs for details. IDA and Binary Ninja MCP servers: not verified.

---

## 3. Research models and benchmarks for neural decompilation (re-executability)

### Takeaway
Published neural-decompilation numbers are function-level, C-only and mostly GCC x86-64, with re-executability around 17-50% at -O2 for end-to-end models and up to the 80s-90s only for refinement pipelines on small ExeBench functions. I found no benchmark on C++, MSVC, or whole-program game binaries. 2026 work shows that "compiles and passes tests" overstates correctness.

### Cited Findings
- LLM4Decompile (EMNLP 2024; arXiv v1 2024-03-08, v3 2024-10-22): reported to beat GPT-4o and Ghidra by "over 100%" in re-executability, with a further 16.2% from the Ghidra-refinement variant. — [arXiv 2403.05286](https://arxiv.org/abs/2403.05286)
- Table values as extracted by the fetch tool (re-check against the paper before quoting; the 33B row looks odd next to the 6.7B row):
  - HumanEval-Decompile, End-6.7B: 68.05% (O0), 39.51% (O1), 36.71% (O2), 37.20% (O3), average 45.37%.
  - GPT-4o average 16.01% (O2 10.37%).
  - Ref-6.7B (Ghidra output refined): 74.39 / 46.95 / 47.56 / 42.07, average 52.74%.
  - ExeBench, End-6.7B: average 17.98% (O2 16.18%).
  - Stated limits: "scope limited to C language targeting x86 platform," "function-level analysis under GCC compilation."
  — [arXiv v3 HTML](https://arxiv.org/html/2403.05286v3)
- Decompile-Bench (2025-05-19, revised 2025-10-19): about 2 million binary-source function pairs filtered from 100 million (450 GB of binaries), about 20% improvement in re-executability from fine-tuning. Languages and optimisation levels not stated in the abstract. — [arXiv 2505.12668](https://arxiv.org/abs/2505.12668)
- Idioms (arXiv 2502.04536, submitted 2025-02-06, revised 2025-06-17): jointly predicts code and user-defined types. ExeBench 54.4% versus 46.3% for LLM4Decompile and 37.5% for Nova; on the Realtype benchmark at least 95% better. A search listing says NDSS 2026 (UNVERIFIED). — [arXiv abs](https://arxiv.org/abs/2502.04536)
- SK2Decompile (2025-09-26): two-phase "skeleton to skin"; reports a 21.6% average re-executability improvement on HumanEval over a GPT-4-mini baseline (fetched wording). — [arXiv 2509.22114](https://arxiv.org/abs/2509.22114)
- ICL4Decomp / Context-Guided Decompilation (v1 2025-11-03, v2 2026-04-12): about 40% re-executability improvement over prior methods, noting existing techniques "often fail" on optimised binaries. — [arXiv 2511.01763](https://arxiv.org/abs/2511.01763)
- Agent4Decompile (2026-04-27, revised 2026-05-01): multi-agent, constraint-guided, tested on 1,641 ExeBench binaries over Ghidra, Angr and RetDec output; abstract page reports 84-97% re-executability, about $0.03-0.05 per binary, 90%+ correct within 2 iterations. Conflict: a search snippet of the same paper reported 40-46% re-executability and an ablation where compile-only validation gave 0% behavioural correctness; this probably reflects two paper versions (UNVERIFIED which). — [arXiv 2604.23940](https://arxiv.org/abs/2604.23940)
- CHISEL (2026-08-28): Ghidra pseudo-C refined with compiler plus fuzzer feedback, no test suite needed. On 120 ExeBench functions (x86-64, O0-O3, stripped and unstripped) with Gemma 4:31b: 96.1% recompilable, 79.8% re-executable, 2.1 iterations, 9.4% false positives in feedback validation. — [arXiv 2608.27981](https://arxiv.org/abs/2608.27981)
- "When LLM Decompilers Recompile More and Preserve Less" (2026-09-04): introduces Decompile-Diverge, a fuzzing oracle. 4.9% of candidates that pass every shipped test still diverge (up to 13% for one system). One refinement LLM raised build rate from 75% to 90% but cut behavioural match from 74% to 62%. Evaluated 8 systems, 300 GitHub library functions, 287 CVE-grounded functions. — [arXiv 2609.05370](https://arxiv.org/abs/2609.05370)
- SRE-Bench (2026-08-11, revised 2026-10-01): 19 private, from-scratch programs averaging 16.9K lines, 44 custom protections, 262 binary instances, 1,572 tasks, over 5,000 expert hours. Best reported success rates: GPT-5.6-Sol 31.5%, Claude-Fable-5.1 26.9%. The summary says strong source-code ability "did not translate" to binary analysis. This is a reverse-engineering benchmark, not decompile-and-recompile. — [arXiv 2608.11469](https://arxiv.org/abs/2608.11469)
- Quantitative Readability Score paper (2026-06-05): agents optimising readability metrics "optimize for metrics in unintended ways"; it scopes out functional equivalence. — [arXiv 2606.06838](https://arxiv.org/abs/2606.06838)
- Practitioner case with a general-purpose agent and no fine-tuning: Quesma's Chromatron work (2026-03-04) used Ghidra via GhidrAssistMCP and a mix of Opus 4.5/4.6 and GPT-5.2/5.3-Codex. Binary was tiny (40 kB compressed, 96 kB unpacked). — [Quesma, Chromatron](https://quesma.com/blog/chromatron-recompiled/)

### Inferences
- The bench numbers cannot be transferred to a game binary. Benchmarks use isolated, self-contained C functions with test harnesses. A game function reads engine structs and calls hundreds of other functions.
- The Decompile-Diverge result is the most relevant caution for a non-matching port: an LLM-rewritten function can recompile and pass shipped tests while quietly diverging. Verification must be execution-based and adversarial (fuzzing or differential).
- General frontier-model agents (Claude, Codex, GLM) are what practitioners actually use for games; small fine-tuned decompilers (LLM4Decompile family) do not appear in any game project I found.

### Gaps
- No result on C++ (virtual dispatch, templates, STL), MSVC output or x64 Windows ABI in any opened paper. ExeBench and HumanEval-Decompile are C.
- Optimisation-level breakdown for the 2026 papers not extracted.
- Exact numbers for DecompileBench-style or other 2025 benchmarks not checked.

---

## 4. Non-matching / functional decompilation and source ports of PC games: effort, and how behaviour was verified

### Takeaway
Classic human-led reimplementations took years (OpenRCT2 began April 2014 and grew to 250 other contributors). The recent agent-heavy PC examples that run (Colin McRae Rally 2.0, Chromatron, Dance Central 3 port) either keep byte-match as the oracle or use pixel/trace comparison against the original. Community reception of AI-generated recomps is hostile in parts of the scene.

### Cited Findings
- OpenRCT2: start date April 2, 2014; first release December 7, 2014; "250 other contributors." Started by Ted "IntelOrca" John. The page does not state how long full replacement took or how it was verified. — [Wikipedia, OpenRCT2](https://en.wikipedia.org/wiki/OpenRCT2)
- re3/reVC (GTA III / Vice City reverse engineering): taken down by DMCA and Take-Two later sued the project leaders; the lead cited a fair-use argument. Search-result summary only, pages not opened, UNVERIFIED. — [PCGamesN (search result)](https://www.pcgamesn.com/gta-3-and-vice-city-reverse-engineering-project-taken-down)
- Practitioner guidance on non-matching work (fmtk wiki, widberg): the "only real way to statically guarantee" equivalence of non-trivially different assembly "is for them to be identical," but non-matching functions "can still be built, relinked, and tested dynamically, i.e. running the game and checking the behavior." Mentions DynamoRIO drcov for execution tracing. Date not stated. — [widberg/fmtk wiki, Decompilation](https://github.com/widberg/fmtk/wiki/Decompilation)
- MattKC forum (LEGO Island community): consensus that for older MSVC games, incremental comparison with `reccmp`, working top-down from main, and mixing inline assembly during transition is more practical than exact recreation from the start. — [forum.mattkc.com thread 936](https://forum.mattkc.com/viewtopic.php?t=936)
- Colin McRae Rally 2.0: README says correctness relies on "automated checks, not on manual review of every line." — [CMR2Decomp](https://github.com/pablocpas/CMR2Decomp)
- Chromatron (2026-03-04): final version had "307,199 out of 307,200 pixels identical." About one month elapsed, with multiple restarts across models and frameworks, and the font took most of the time. The author reports LLMs "were very eager to say that something is the same" until a pixel comparison gave real feedback. — [Quesma, Chromatron](https://quesma.com/blog/chromatron-recompiled/)
- Weltendämmerung (C64, 7,129-byte binary): Claude Opus 4.5 through Claude Code, 50+ sessions, 3 days, $100 API cost; verified against a C64 emulator and documentation; "Agent can be 'sloppy'; explicit verification is essential." Date not stated in the fetched text. — [s-macke/weltendaemmerung](https://github.com/s-macke/weltendaemmerung)
- Community stance (commentary):
  - Banjo: Recompiled developer DarioSamo, October 3, 2026: AI-generated ports are a "race to the bottom"; "vibecoders keep copying our efforts and completely fail to reach the same level of quality." The article offers no measured quality comparison. — [VGC, 2026-10-03](https://www.videogameschronicle.com/news/banjo-kazooie-pc-port-decomp-developer-says-ai-generated-pc-ports-are-a-race-to-the-bottom-and-disrespectful/)
  - DK64 Rekongpiled shipped August 29, 2026 after a rival AI-assisted effort emerged; the article frames three positions (refuse, disclose, hide) and argues matching decomp has a built-in correctness test while recomp does not. The framing is the article's own. — [heldgames, recomp scene (2026)](https://heldgames.com/guides/recomp-scene-ai-vibe-coding)
  - A PS1 recomp developer (RecompOne) says AI "doesn't replace human judgment" and struggles with domain knowledge (July 5, 2026). — [Read Only Memo, 2026-07-05](https://readonlymemo.com/ps1-recompiled-progress-recompone-pixygarden-interview/)

### Inferences
- No source I opened gives an effort figure for a non-matching PC port of a large 64-bit title. OpenRCT2's 2014 start is the only duration anchor, and it is a human project in a much smaller code base.
- For a non-matching port, the verification burden shifts from "free oracle" to engineering you must build yourself (Q7).

### Gaps
- re3/reVC, DevilutionX, OpenGOAL and other classic port timelines and verification methods were not researched from opened sources.
- OpenRCT2's hybrid method (replacing functions inside the original exe) is not confirmed by the page I opened.
- No 2024-2026 non-matching functional-decomp PC example with function counts found beyond those above.

---

## 5. Obstacles specific to a 2016 MSVC x64 C++ game (no symbols)

### Takeaway
Matching needs the exact compiler build and flags, and the only practitioner guidance found says whole-program optimisation (LTCG) is what makes matching painful. I found no x64 MSVC matching project, no evidence Titanfall 2 has public symbols, and weak evidence that its toolset may be VS2013 rather than 2015.

### Cited Findings
- Exact compiler needed: the x86 PC projects pin the toolchain to the original (LEGO Island: MSVC 4.2; CMR2: MSVC 6.0 SP3; Summoner: MSVC 6 12.00.8804 `/O2`; Thief 3: MSVC 7.1). — sources in Q1.
- Toolchain identification: widberg's FUEL work identified Visual Studio 2005 Professional from the binary (tools: Detect It Easy, richprint) and built a portable copy of that compiler. — [fmtk wiki](https://github.com/widberg/fmtk/wiki/Decompilation)
- LTCG effect (same page): whole-program optimisation produces "most functions having unique and non-standard calling conventions"; small prologue changes shift alignment and padding "far into the function body."
- The 2025-2026 PC matching projects use reccmp (LEGO Island family) or objdiff / decomp.dev reports; they work with split binaries. — [CMR2Decomp](https://github.com/pablocpas/CMR2Decomp), [Thief3-Decomp](https://github.com/Veradictus/Thief3-Decomp)
- Titanfall 2 toolset evidence (weak): a Steam thread dated July 3, 2023 resolves MSVCP120.dll / MSVCR120.dll errors in Titanfall 2 by installing the Visual C++ 2013 redistributable (both x86 and x64 per the poster). — [Steam discussion](https://steamcommunity.com/app/1237970/discussions/0/3805027864674031138)
- Titanfall 2 ecosystem: Northstar (modding and custom servers) and community tools such as RSX (asset extraction for RPak) and RePak exist, per search-result summaries (UNVERIFIED, pages not opened beyond a search listing). — [Northstar docs](https://docs.northstar.tf/Wiki/)
- Leaked-source note (existence only; using it is off the table): Source-engine licensee code for TF2 and CS:GO leaked in late 2018 and resurfaced publicly on April 22, 2020 (search-result summaries, pages not opened, UNVERIFIED). — [Blue's News](https://www.bluesnews.com/s/210281/cs-go-and-tf2-source-code-leaked)

### Inferences
- The Steam thread suggests at least some Titanfall 2 modules link the VS2013 (v120) CRT. That points to toolset v120 rather than "2015-era," but the thread only shows runtime dependency, not which compiler built the engine DLLs. It needs checking against the actual binaries (PE rich header, CRT imports, `.rdata` strings).
- Each of: unknown exact compiler update, unknown flags (/GL, /O2, /Ob2, /Gy, PGO), heavy inlining, and large statically linked middleware, makes a byte-exact oracle hard to establish. I found no practitioner account of achieving it for an x64 MSVC game, so this is unproven, not proven impossible.
- x64 has one calling convention, which may remove some of the per-function calling-convention noise that fmtk describes for x86 LTCG builds. That is my reasoning, not a sourced claim.
- Not-in-scope but relevant: byte-matching is only a means. If the goal is a native arm64 macOS port, a non-matching functional reimplementation validated by differential execution (Q7) is the realistic target.

### Gaps
- No confirmed statement of which MSVC version and flags built Titanfall 2's engine, client and server modules, nor whether LTCG/PGO was used.
- No confirmation of whether any symbols or PDBs exist for Titanfall 2 (none found, but absence of evidence only). VDC page (403) not readable; the Portal 2 branch claim is from a search snippet (UNVERIFIED).
- No x64 MSVC matching decomp found (search for "x64 MSVC matching decompilation" returned only 32-bit examples). Whether reccmp or objdiff support x64 COFF/PE was not verified.
- No practitioner thread specifically on matching MSVC 2013/2015 x64 builds.

---

## 6. Largest documented LLM-agent decompilation/port of a large commercial game that reached a running result

### Takeaway
To be plain: I found no documented case of an LLM-agent-driven decompilation or port of a large, symbol-less, 64-bit commercial Windows game that reached a running result. The largest documented case is the Dance Central 3 (Xbox 360, PowerPC) agent-driven decomp fork, with about 32,000 "authorable" functions and a native port that reaches gameplay, but it had unusual ground truth that Titanfall 2 lacks.

### Cited Findings
- freeqaz/dc3-decomp (AI-assisted fork of MiloHax's DC3 decomp; MSVC targeting PowerPC): README, corrected 2026-08-17, reports 91.21% of authorable functions (29,383 / 32,213) and 77.41% of authorable code bytes; whole-binary (XDK-inclusive) 60.81% of functions and 43.18% of bytes; about 93% "fuzzy match" on cross-platform engine/game code. 8,035 commits on main. The README distinguishes fuzzy from byte-exact matching; a stricter relocation check lowered byte figures by about 2 points. — [freeqaz/dc3-decomp README](https://raw.githubusercontent.com/freeqaz/dc3-decomp/master/README.md)
- Agent infrastructure (same README): about 25 slash-command skills wrapping Ghidra MCP (decompiled C and semantic search over 42k+ functions), Unicorn PPC32 function runner, m2c, a source permuter, and an objdiff orchestrator; a "worktree pool" for concurrent agents on isolated git worktrees; persistent memory.
- Ground truth that eased the job (same README): "RB2 Wii ships with full DWARF" (class layouts, variable names) and RB3 (Wii) about 54% matched in MWCC; cross-reference databases from both projects. XDK platform code and library code are excluded as non-authorable.
- Native port: the WebGPU port "boots, renders, plays audio, and reaches gameplay venues with characters and IK" (same README). The milo-native-engine repo describes DC3 as a "mature native port" with a 371/371-test convergence suite, and RB3 only partly working. — [freeqaz/milo-native-engine](https://github.com/freeqaz/milo-native-engine)
- The dc3 README gives no token usage, no concurrent-agent count and no project duration. It calls itself an experiment on "how far AI agents can push a clean-room Xbox 360 decompilation project."
- Next in scale among the AI-involved cases: Colin McRae Rally 2.0 (3,364 functions, x86 MSVC 6, playable "for the most part") — [CMR2Decomp](https://github.com/pablocpas/CMR2Decomp). Melee (3.88 MB of code) is bigger but was mostly a six-year human effort with AI at the end (secondary source) — [heldgames](https://heldgames.com/guides/melee-decompiled-ai-astra). SK2 is the best-documented agent-led matching run, but it is N64 and small.
- Halo: Combat Evolved appears in one roundup as the only playable native port among listed projects at 7.17% decomp completion (secondary source, not otherwise verified). — [heldgames](https://heldgames.com/guides/games-100-percent-decompiled-what-next)

### Inferences
- DC3 is the best precedent for the "swarm of agents with tools" idea: it is tens of thousands of functions, MSVC-compiled, with a native port at the end. But (a) PowerPC not x86-64, (b) sibling builds with DWARF and a prior human decomp as ground truth, (c) a much smaller engine than Titanfall 2's, (d) matched against a known toolchain with a byte-oracle, (e) duration and cost undisclosed. Treat it as an upper bound on what is possible with strong ground truth, not as a baseline for a no-symbol target.
- "Matched" in these repos means an oracle-checked decompilation. A port of Titanfall 2 without an oracle would be in the CHISEL / Decompile-Diverge regime, where reliability is far lower and verification is the bottleneck.

### Gaps
- Time, token and dollar cost of the DC3 work not disclosed in opened pages.
- I did not find the DC3 author's own write-up (a search for one returned nothing relevant).
- Whether DC3's "91.21%" is byte-exact or fuzzy is unclear from the README text I saw (the README distinguishes both and gives separate fuzzy figure).
- No LLM-agent case on Windows x64 commercial game found at all, including via the "universal-modder" and Claude-Opus mod-mashup results (listings only, UNVERIFIED).

---

## 7. Verification strategies when byte-matching is unavailable

### Takeaway
The workable approaches found: differential execution of original versus rewritten function in an emulator (Unicorn, as in dc3), fuzzing-based divergence oracles, pixel or trace comparison of whole-program output, and tests that classify divergences. LLMs misjudge equivalence on their own.

### Cited Findings
- Differential execution: dc3's Unicorn PPC32 function runner "runs target and decomp side-by-side" with synthesised inputs, and classifies divergences as logic / build_env / regalloc to separate real bugs from harmless codegen drift. — [dc3-decomp README](https://raw.githubusercontent.com/freeqaz/dc3-decomp/master/README.md)
- Headless introspection: a debug HTTP server exposes endpoints for agent-driven inspection without a window (same README). The native port has a 371/371-test convergence suite. — [milo-native-engine](https://github.com/freeqaz/milo-native-engine)
- Fuzzing oracle: Decompile-Diverge found 4.9% divergence among candidates that passed all shipped tests, and a refinement LLM trading behavioural match (74% to 62%) for build rate (75% to 90%). — [arXiv 2609.05370](https://arxiv.org/abs/2609.05370)
- Compiler plus fuzzer feedback loop without tests: CHISEL, with a 9.4% false-positive rate in feedback validation. — [arXiv 2608.27981](https://arxiv.org/abs/2608.27981)
- Whole-program pixel diff: Chromatron's 307,199/307,200 identical pixels. Without it, models claimed equivalence too readily. — [Quesma](https://quesma.com/blog/chromatron-recompiled/)
- Emulator-side comparison for a C64 port plus explicit "check your assignment" steps. — [s-macke/weltendaemmerung](https://github.com/s-macke/weltendaemmerung)
- Dynamic testing of non-matching builds by running the game, plus DynamoRIO drcov tracing. — [fmtk wiki](https://github.com/widberg/fmtk/wiki/Decompilation)
- Guarding against agent cheating: hooks to block destructive agent actions (Lewis, 2026-02-16) and "guidance on AI cheating prevention" (Macabeus, 2026-08-17). — [Long Tail](https://blog.chrislewis.au/the-long-tail-of-llm-assisted-decompilation/), [Klonoa post](https://gambiconf.substack.com/p/starting-a-decompilation-project)
- Agent4Decompile ablation: compile-only validation gave 0% behavioural correctness despite 91-99% compile rates. Snippet only, UNVERIFIED. — [arXiv 2604.23940](https://arxiv.org/abs/2604.23940)

### Inferences
- For Titanfall 2, the dc3 pattern transfers conceptually: an x86-64 Unicorn (or equivalent) harness that runs the original function from the real binary and the new native implementation on the same synthetic machine state, comparing registers, memory and return values. It needs a way to stub or stand in for callees, globals and engine structs, which is the hard part for engine code and is not described in the dc3 README. This is my inference.
- Per-function equivalence will not catch cross-function state bugs (the integration level); the Chromatron pixel diff and the dc3 test suite show you also need whole-program golden comparisons.
- Fuzzing inputs for engine functions that take pointers to large structs are likely to hit invalid memory; none of the sources describe how dc3 solved this.

### Gaps
- No source describes differential testing of x86-64 Windows game code against a Unicorn-run original at scale.
- No false-positive or false-negative rates for Unicorn-based game-function equivalence.
- Trace-comparison approaches for games (record and replay) were not found beyond drcov being mentioned.
