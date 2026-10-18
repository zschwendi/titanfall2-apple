# The profile stub

A tiny macOS app whose only job is to make Xcode's automatic signing produce a development provisioning
profile that carries a cross-architecture entitlement, for your team and a bundle id of your own.
`tools/signing/make_profile.sh` (or `./tfrecomp signing`) builds it, takes the profile out of it for
`app/app.mk`, and runs it: `main.c` is a memory-ordering litmus test (a writer stores `x` then `y`, a reader
loads `y` then `x`; with the x86 mode it must never see `x < y`).

- `TFRecompProfile-free.entitlements`: `com.apple.developer.cross-architecture-support-unmanaged`, for a free
  Apple ID's Personal Team (untested).
- `TFRecompProfile-paid.entitlements`: `com.apple.developer.cross-architecture-support`, for a paid team.

The project carries no team and the bundle id `local.tfrecomp.player`; the script sets both on the command line.
