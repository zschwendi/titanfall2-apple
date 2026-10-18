# Usually you do not write this file: `./tfrecomp signing --team <TEAMID> --bundle-id <ID> [--paid]`
# (tools/signing/make_profile.sh) writes it after testing the profile.  APP_SIGN := free for a free
# Apple ID's Personal Team (the -unmanaged entitlement), dev for a paid team.
#
# Copy to app/signing.local.mk (git-ignored) to sign with your own Apple Development identity, so
# the game can run with the hardware x86 memory-ordering mode (RT_TSO=require).  Without this file
# app/app.mk signs ad hoc, which needs no Apple account: use the GUEST_MEM=seqcst build then
# (docs/LIMITS.md says what that costs).
#
# APP_ID: an explicit App ID registered in your developer account with the capability
# "Cross-Architecture Support" (com.apple.developer.cross-architecture-support).
# APP_TEAM: your team id (10 characters, shown in the developer account's membership page).
# APP_PROFILE: a development provisioning profile for that App ID and this Mac.
APP_ID      := com.example.tfrecomp
APP_TEAM    := ABCDE12345
APP_PROFILE := /path/to/development.provisionprofile
