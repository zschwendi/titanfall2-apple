# EA SDK stand-ins: what each one answers

Titanfall 2 links against EA's SDK (`OriginSDK.dll`). This project never reads, loads, disassembles or runs
that DLL, nor `Titanfall2.exe`. When the game calls one of 15 SDK functions, the runtime answers the call
itself. Most answers are the ones the SDK gives when EA's service cannot be reached: nobody is signed in,
the user is not online, or a request is accepted and never completes.

Three stand-ins are different. For the placeholder user, which is the default user of `gate3 run`, stand-ins
6, 7 and 14 answer with a made-up local user: id 1, persona 1 and the name "Pilot". That data is invented here.
It is not the owner's account and is taken from nowhere. No stand-in gives anything else of an account: no
entitlement, no authorisation code, no friend or blocked-user list. Nothing is sent anywhere; the runtime opens
no network sockets. Any other SDK call stops the game with a report. Each answer was decided by the project
owner before its code was written. You still need your own purchased copy of the game.

"Placeholder only" below means the stand-in answers only when the placeholder user is selected. With
`GATE3_USER=nobody` the run has no placeholder user, and each of these calls stops the game, except where the
table says otherwise.

| No. | Function | What the game asks | Answer | Switch to turn it off |
|----:|----------|--------------------|--------|-----------------------|
| 2 | `OriginIsProgressiveInstallationAvailableSync` | Whether progressive installation is available | Result 0, the byte it points to left at 0: "the call worked; progressive installation is not available" | `GATE3_NO_STANDIN_PROGRESSIVE_INSTALL=1` |
| 3 | `OriginGetSettingSync` | Setting 1 (the Origin environment), and for the placeholder, setting 2 (is the overlay on) | Setting 1: result 0 and the text "production". Setting 2 (placeholder only): result 0 and an empty text. Any other setting halts | `GATE3_NO_STANDIN_SETTING_ENVIRONMENT=1` |
| 4 | `OriginRegisterEventCallback` | Registration of the engine's callback for nine event numbers | Result 0. The registration is counted; nothing is stored and nothing is ever called back | `GATE3_NO_STANDIN_EVENT_CALLBACKS=1` |
| 5 | `OriginUpdate` | The SDK's per-frame update | Result 0 ("no error this frame"). Nothing is written or called back | `GATE3_NO_STANDIN_UPDATE=1` |
| 6 | `OriginGetDefaultUser` | Which user is signed in, every frame | `GATE3_USER=nobody`: result 0, "nobody is signed in". Placeholder (default): result 1, the made-up id 1 | `GATE3_NO_STANDIN_DEFAULT_USER=1` |
| 7 | `OriginGetProfile` | The profile of the user, one request | Placeholder only: result 0, accepted. The callback is then called once with the made-up profile (id 1, name "Pilot", no country). A second request halts | `GATE3_NO_STANDIN_PROFILE=1` |
| 8 | `OriginRequestAuthCode` | An authorisation code for `TITANFALL2-PC-SERVER` | Placeholder only: result 0, accepted. It never completes; no code exists and the callback is never called | `GATE3_NO_STANDIN_AUTH_CODE=1` |
| 9 | `OriginQueryEntitlements` | The entitlements for `Titanfall2PC` | Placeholder only: result 0, accepted. It never completes; no entitlement is claimed | `GATE3_NO_STANDIN_ENTITLEMENTS=1` |
| 10 | `OriginQueryBlockedUsers` | The list of blocked users | Placeholder only: result 0, accepted; the handle the engine asks for is written as 0. It never completes; no list exists | `GATE3_NO_STANDIN_BLOCKED_USERS=1` |
| 11 | `OriginQueryFriends` | The friends list | Placeholder only: result 0, accepted; the handle is written as 0. It never completes; no list exists | `GATE3_NO_STANDIN_FRIENDS=1` |
| 12 | `OriginCheckOnline` | Whether the user is online | Placeholder only: result 0, the byte left at 0, meaning "not online" (true here: there is no connection to EA) | `GATE3_NO_STANDIN_CHECK_ONLINE=1` |
| 13 | `OriginSetPresence` | Status to tell EA's service (the texts) | Placeholder only: result 0, accepted. Nothing is done with the texts; no service is contacted | `GATE3_NO_STANDIN_PRESENCE=1` |
| 14 | `OriginGetDefaultPersona` | The default persona of the user | Placeholder only: the made-up persona 1 (the same number as its user id). Nothing is written | `GATE3_NO_STANDIN_PERSONA=1` |
| 15 | `OriginGrantAchievement` | Granting an achievement | Placeholder only: result 0, accepted. Nothing is recorded, nothing is written, the callback is never called | `GATE3_NO_STANDIN_ACHIEVEMENT=1` |
| 16 | `OriginShutdown` | Shutting the SDK down when the engine unloads | For both users: result 0, "no error". Nothing is done at all | `GATE3_NO_STANDIN_SHUTDOWN=1` |

Stand-ins 2 to 5 and 16 are not tied to a user. Stand-in 6 answers every user, and stand-ins 7 to 15 apply only to
the placeholder. Every stand-in logs its first answer and counts the others; `gate3 run` prints the counts.
Stand-ins 2 to 16 are handed to the runtime only by `gate3 run`. Every other program stops at these imports.

**What the game does with the answers.** These statements come from the engine's own code as the files read it;
the SDK itself was not read. While the engine has no user id, a result of 0 from stand-in 6 ends its per-frame
account step for that frame, and it asks again in the next frame; none of its account calls is reached. With the
placeholder, the engine stores id 1, asks for the profile in the same frame, and passes persona 1 to the
achievement call. The engine takes a byte left at 0 by stand-in 12 as "not online" and stores that answer.
Stand-in 2 leaves its byte at 0, so the engine's function returns and that thread ends; a non-zero byte would
lead to three more SDK imports, which halt. The engine compares the setting 1 text with "production" before it
marks itself as a retail build. It asks setting 3 only if setting 2 reads "true", which the empty text prevents.

**The command line.** The default command line of `gate3 run` contains the game's own `-noorigin` switch, which
makes the launcher skip its call into EA's SDK. It is the first entry of the list in `gate3/run.c`:

    static const char *const run_default_args[] = {
        "-noorigin", "-nosound",
