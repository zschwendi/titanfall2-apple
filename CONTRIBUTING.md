# Contributing

Thank you for looking at this project. This file says what helps most now, how to run the tests,
and the rules every change has to keep.

The project is not affiliated with Electronic Arts or Respawn Entertainment. To play, you need your
own copy of the game's PC files (Titanfall 2, 2016). The repository contains none of them.

## What helps most now

- **Reports from people with a free Apple ID.** Please try `./tfrecomp signing` with a free Apple ID's
  Personal Team and tell us what happens. Use the "Free Apple ID test" issue template. This mode is
  untested, and it is the open question for players without a paid Apple Developer Program membership.
- **Bug reports with the stop report.** When the runtime stops, it prints a stop report. Include it in
  the bug report, with the Mac model, the macOS version and the output of `./tfrecomp doctor`. Use the
  "Bug report" issue template.
- **Speed numbers from other Macs.** Say which Mac you have (model, chip, memory), which macOS version
  you run, the window size and any `--upscale` value, and whether the signing is free, paid or ad hoc.
  Results from one Mac model are not enough to know how the project runs elsewhere.

## How to run the tests

Run `make test` from the top of the checkout. It needs no game files. Please run it before you open a
pull request; it must pass.

## Hard rules for every change

- Never commit game files, or anything taken from them. That covers DLLs, lifted C code, dumps of game
  data, and shader text or strings from the game.
- Never read, load or disassemble `OriginSDK.dll` or `Titanfall2.exe`.
- The runtime opens no network sockets. Do not add any.
- Any new answer to an EA SDK call is discussed in an issue first. See `docs/EA-STANDINS.md`.
- Before a pull request, `make test` passes.

Please keep personal information out of commits and documents: no home paths, host names or account
ids. `tools/publish/check.py` looks for them.

## Pull requests

Keep a change small and say in the description what it does and how you checked it. If a change touches
the signing, the runtime's stand-ins or the publishing checks, say so in the first line.
