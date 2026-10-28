#!/usr/bin/env bash
# Emit a development version derived from main's latest released milestone.
# Successful source revisions on this branch receive a, b, c ... z, aa, ab ...
# A new main release resets the suffix to a. Failed builds do not consume it.
# This script creates no Git tags and never changes the published main version.
set -euo pipefail

: "${GITHUB_TOKEN:?GITHUB_TOKEN is required to count workflow runs}"
: "${GITHUB_REPOSITORY:?GITHUB_REPOSITORY is required}"
: "${GITHUB_RUN_ID:?GITHUB_RUN_ID is required}"

API_ROOT="${GITHUB_API_URL:-https://api.github.com}"
API_ROOT="${API_ROOT%/}"
BASE_BRANCH="${SALSA64_VERSION_BASE_BRANCH:-main}"
BRANCH_NAME="${GITHUB_HEAD_REF:-${GITHUB_REF_NAME:-}}"
if [[ -z "$BRANCH_NAME" ]]; then
	echo "Cannot determine the branch name for the incremental version." >&2
	exit 1
fi

git fetch --no-tags origin "+refs/heads/$BASE_BRANCH:refs/remotes/origin/$BASE_BRANCH"
BASE_REF="refs/remotes/origin/$BASE_BRANCH"
CHANGELOG_REF="$BASE_REF:CHANGELOG.md"

BASE_VERSION="$(git show "$CHANGELOG_REF" |
	awk 'match($0, /^## \[[0-9]+\.[0-9]+\.[0-9]+\]/) {
		v = $0
		sub(/^## \[/, "", v)
		sub(/\].*/, "", v)
		print v
		exit
	}')"
if [[ ! "$BASE_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
	echo "Could not read the latest release version from $CHANGELOG_REF." >&2
	exit 1
fi

MILESTONE_HEADING="## [$BASE_VERSION]"
MILESTONE_COMMIT="$(git log --format=%H --reverse -S"$MILESTONE_HEADING" "$BASE_REF" -- CHANGELOG.md | tail -n 1)"
if [[ -z "$MILESTONE_COMMIT" ]]; then
	echo "Could not locate the changelog commit that introduced $MILESTONE_HEADING." >&2
	exit 1
fi
MILESTONE_EPOCH="$(git show -s --format=%ct "$MILESTONE_COMMIT")"

# Start counting only from the commit that introduced this versioning policy.
# This prevents old builds with arbitrary labels from consuming the first letter.
VERSIONING_MARKER="Derive version from the main milestone and successful revisions"
VERSIONING_COMMIT="$(git log --format=%H --reverse -S"$VERSIONING_MARKER" HEAD -- .github/workflows/dr-mario64-rdram-copy.yml | tail -n 1)"
if [[ -z "$VERSIONING_COMMIT" ]]; then
	echo "Could not locate the versioning-policy activation commit." >&2
	exit 1
fi
VERSIONING_EPOCH="$(git show -s --format=%ct "$VERSIONING_COMMIT")"

CUTOFF_EPOCH="$MILESTONE_EPOCH"
if (( VERSIONING_EPOCH > CUTOFF_EPOCH )); then
	CUTOFF_EPOCH="$VERSIONING_EPOCH"
fi

AUTH_HEADER="Authorization: Bearer $GITHUB_TOKEN"
ACCEPT_HEADER="Accept: application/vnd.github+json"
RUN_JSON="$(curl --fail --silent --show-error \
	-H "$AUTH_HEADER" -H "$ACCEPT_HEADER" \
	"$API_ROOT/repos/$GITHUB_REPOSITORY/actions/runs/$GITHUB_RUN_ID")"
SOURCE_SHA="$(jq -er '.head_sha' <<<"$RUN_JSON")"
BRANCH_QUERY="$(jq -rn --arg value "$BRANCH_NAME" '$value | @uri')"

RUNS_FILE="$(mktemp)"
trap 'rm -f "$RUNS_FILE"' EXIT
PAGE=1
while true; do
	DATA="$(curl --fail --silent --show-error \
		-H "$AUTH_HEADER" -H "$ACCEPT_HEADER" \
		"$API_ROOT/repos/$GITHUB_REPOSITORY/actions/runs?branch=$BRANCH_QUERY&per_page=100&page=$PAGE")"
	PAGE_SIZE="$(jq '.workflow_runs | length' <<<"$DATA")"
	if (( PAGE_SIZE == 0 )); then
		break
	fi

	jq -c '.workflow_runs[]' <<<"$DATA" >> "$RUNS_FILE"

	# The API is newest-first; once a page reaches before the cutoff, older
	# pages cannot contain a successful source revision in this version series.
	OLDEST_EPOCH="$(jq '[.workflow_runs[].created_at | sub("\\.[0-9]+Z$"; "Z") | fromdateiso8601] | min' <<<"$DATA")"
	if (( OLDEST_EPOCH < CUTOFF_EPOCH || PAGE_SIZE < 100 )); then
		break
	fi
	PAGE=$((PAGE + 1))
done

# Count distinct successful source revisions, not raw workflow runs. The
# Dr. Mario and ZIP-ROM workflows therefore share one suffix for the same SHA.
SUCCESS_SHAS="$(jq -s -c --arg branch "$BRANCH_NAME" --argjson cutoff "$CUTOFF_EPOCH" --argjson run_id "$GITHUB_RUN_ID" '
	[.[] 
	 | select(.head_branch == $branch)
	 | select(.id < $run_id)
	 | select(.status == "completed" and .conclusion == "success")
	 | select(((.created_at | sub("\\.[0-9]+Z$"; "Z") | fromdateiso8601) >= $cutoff))
	 | select(.head_sha != null and .head_sha != "")
	 | {id: .id, sha: .head_sha}]
	| sort_by(.id)
	| reduce .[] as $run ([]; if index($run.sha) == null then . + [$run.sha] else . end)
' "$RUNS_FILE")"

# Rebuilding a revision already built successfully reuses its version. A new
# source revision takes the next suffix. Failed/cancelled builds do not count.
EXISTING_INDEX="$(jq --arg sha "$SOURCE_SHA" 'index($sha) // -1' <<<"$SUCCESS_SHAS")"
if (( EXISTING_INDEX >= 0 )); then
	BUILD_NUMBER=$((EXISTING_INDEX + 1))
else
	BUILD_NUMBER=$(( $(jq 'length' <<<"$SUCCESS_SHAS") + 1 ))
fi

ALPHABET="abcdefghijklmnopqrstuvwxyz"
N="$BUILD_NUMBER"
SUFFIX=""
while (( N > 0 )); do
	INDEX=$(((N - 1) % 26))
	SUFFIX="${ALPHABET:INDEX:1}$SUFFIX"
	N=$(((N - 1) / 26))
done

VERSION="v${BASE_VERSION}${SUFFIX}"
echo "Main milestone: $BASE_VERSION; cutoff epoch: $CUTOFF_EPOCH; successful source revisions: $(jq 'length' <<<"$SUCCESS_SHAS"); source: $SOURCE_SHA; label: $VERSION" >&2
printf '%s\n' "$VERSION"
