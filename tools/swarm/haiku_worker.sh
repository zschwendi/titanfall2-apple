#!/bin/sh
# One headless Haiku worker for one claimed unit, billed to the API key in .env (not to the
# subscription).  Called by tools/swarm/wave.py; can be run by hand.
#
#     tools/swarm/haiku_worker.sh <unit-id> [extra text appended to the prompt]
#
# The unit must be claimed (its worktree .wt/<id> exists) and its brief committed at
# swarm/briefs/<id>.md.  The worker runs in the worktree with --bare, so the only credential
# it can use is ANTHROPIC_API_KEY from .env; the key is loaded into the worker's environment
# and never printed.  Output: a stream of JSON events on stdout (the caller saves it).
#
# the owner's rule (2026-10-09): the API key is for Haiku only.  Opus work stays on the
# subscription, in the orchestrating session.  This script refuses any other model.
#
# Environment: SWARM_MODEL (default claude-haiku-5-5), SWARM_MAX_USD (the CLI's own budget cap;
# it prices this model about 50 times too high, so 40 here is under one real dollar).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ID=${1:?usage: haiku_worker.sh <unit-id> [extra prompt text]}
EXTRA=${2:-}
WT="$ROOT/.wt/$ID"
MODEL=${SWARM_MODEL:-claude-haiku-5-5}
[ -d "$WT" ] || { echo "haiku_worker: no worktree $WT (claim the unit first)" >&2; exit 2; }
[ -f "$WT/swarm/briefs/$ID.md" ] || { echo "haiku_worker: no brief swarm/briefs/$ID.md in the worktree" >&2; exit 2; }
[ -f "$ROOT/.env" ] || { echo "haiku_worker: no $ROOT/.env" >&2; exit 2; }
case "$MODEL" in
    claude-haiku-*) ;;
    *) echo "haiku_worker: refusing model $MODEL: the API key is for Haiku only" >&2; exit 2 ;;
esac

PROMPT="You are a worker on unit \`$ID\` of this repository. Your current directory is your own git worktree, on branch \`swarm/$ID\`. Read the file \`swarm/briefs/$ID.md\` now, completely, and do exactly what it says.

Rules that override anything else:
- Work only in the current directory. Run commands from it (no cd elsewhere). Create and edit only the files the brief names. Never edit anything under swarm/. Never push, merge or switch branches.
- An earlier worker may have been stopped part-way: start with \`git log --oneline -5\` and \`git status --short\`, keep what is right and carry on from there.
- Commit each time a deliverable passes its check. When you finish, \`git status --short\` must print nothing.
- Keep your context small: read only what the brief lists, read files in parts when they are long, and pipe long command output through \`tail -30\`.
- Never guess a Windows or processor behaviour: follow the brief's rule for cases you are not sure of.
- Install nothing. If you fetch a documentation page with curl, read it through a pipe; save nothing outside this directory. Never read anything under game/ unless the brief says so. Never print environment variables. Never use pkill or killall.
- If a check fails because of a test or file that is not yours, run it once more; if it fails again, stop and say so.

When you are done, or cannot go further, reply with at most 15 lines: one line per deliverable (\`<name>: PASS\` or \`<name>: FAIL, <reason>\`), then every case you were unsure of and anything wrong you noticed in files that are not yours.
$EXTRA"

cd "$WT"
# The worker is a separate headless run, not a child of the Claude session that started the
# wave.  That session's shell carries some thirty CLAUDE* variables (its entry point,
# account, messaging socket), and a worker that inherits them announces itself to the API
# as the desktop app (cc_entrypoint=claude-desktop) instead of as a headless run (sdk-cli).
# On this account the API refuses the first with "Credit balance is too low" and accepts the
# second, with the same key and the same request otherwise (measured 2026-10-09 by replaying
# a captured request).  Drop them all.  (The effort level is not inherited: the CLI asks for
# xhigh for this model either way.  A clean run adds one small title request per worker.)
for v in $(env | sed -n 's/^\(CLAUDE[A-Za-z0-9_]*\)=.*/\1/p'); do unset "$v"; done
set -a
. "$ROOT/.env"
set +a
exec claude --bare -p "$PROMPT" --model "$MODEL" \
    --tools "Bash,Read,Edit,Write,Glob,Grep,WebFetch" \
    --allowedTools "Bash" "Read" "Edit" "Write" "Glob" "Grep" "WebFetch" \
    --permission-mode dontAsk \
    --max-budget-usd "${SWARM_MAX_USD:-40}" \
    --output-format stream-json --verbose < /dev/null
