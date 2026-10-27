#!/usr/bin/env bash
# Emit a development version based on the latest released version in main.
# Each build run on this branch since that version milestone consumes the next
# suffix: a, b, c ... z, aa, ab ... . A new main release resets the suffix to a.
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
	awk 'match($0, /^## \\[[0-9]+\\.[0-9]+\\.[0-9]+\\]/) {
		v = $0
		sub(/^## \\[/, "", v)
		sub(/\\].*/, "", v)
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

AUTH_HEADER="Authorization: Bearer $GITHUB_TOKEN"
ACCEPT_HEADER="Accept: application/vnd.github+json"
RUN_JSON="$(curl --fail --silent --show-error \
	-H "$AUTH_HEADER" -H "$ACCEPT_HEADER" \
	"$API_ROOT/repos/$GITHUB_REPOSITORY/actions/runs/$GITHUB_RUN_ID")"
WORKFLOW_ID="$(jq -er '.workflow_id' <<<"$RUN_JSON")"
BRANCH_QUERY="$(jq -rn --arg value "$BRANCH_NAME" '$value | @uri')"

BUILD_COUNT=0
CURRENT_RUN_COUNTED=0
PAGE=1
while true; do
	DATA="$(curl --fail --silent --show-error \
		-H "$AUTH_HEADER" -H "$ACCEPT_HEADER" \
		"$API_ROOT/repos/$GITHUB_REPOSITORY/actions/workflows/$WORKFLOW_ID/runs?branch=$BRANCH_QUERY&per_page=100&page=$PAGE")"
	PAGE_SIZE="$(jq '.workflow_runs | length' <<<"$DATA")"
	if (( PAGE_SIZE == 0 )); then
		break
	fi

	COUNT_PAGE="$(jq --arg branch "$BRANCH_NAME" --argjson milestone "$MILESTONE_EPOCH" '
		[.workflow_runs[]
		 | select(.head_branch == $branch)
		 | select(((.created_at | sub("\\\\.[0-9]+Z$"; "Z") | fromdateiso8601) >= $milestone))]
		| length
	' <<<"$DATA")"
	BUILD_COUNT=$((BUILD_COUNT + COUNT_PAGE))

	if [[ "$(jq --arg id "$GITHUB_RUN_ID" --arg branch "$BRANCH_NAME" --argjson milestone "$MILESTONE_EPOCH" '
		any(.workflow_runs[];
			((.id | tostring) == $id)
			and (.head_branch == $branch)
			and ((.created_at | sub("\\\\.[0-9]+Z$"; "Z") | fromdateiso8601) >= $milestone))
	' <<<"$DATA")" == "true" ]]; then
		CURRENT_RUN_COUNTED=1
	fi

	# Runs are returned newest first. Once the oldest run in a page predates
	# the milestone, older pages cannot contribute to this branch's suffix.
	OLDEST_EPOCH="$(jq '[.workflow_runs[].created_at | sub("\\\\.[0-9]+Z$"; "Z") | fromdateiso8601] | min' <<<"$DATA")"
	if (( OLDEST_EPOCH < MILESTONE_EPOCH || PAGE_SIZE < 100 )); then
		break
	fi
	PAGE=$((PAGE + 1))
done

# GitHub may not expose the currently queued run in its run-list endpoint yet.
# Count it exactly once so the first build after a milestone is always suffix a.
if (( CURRENT_RUN_COUNTED == 0 )); then
	BUILD_COUNT=$((BUILD_COUNT + 1))
fi
if (( BUILD_COUNT < 1 )); then
	BUILD_COUNT=1
fi

ALPHABET="abcdefghijklmnopqrstuvwxyz"
N="$BUILD_COUNT"
SUFFIX=""
while (( N > 0 )); do
	INDEX=$(((N - 1) % 26))
	SUFFIX="${ALPHABET:INDEX:1}$SUFFIX"
	N=$(((N - 1) / 26))
done

VERSION="v${BASE_VERSION}${SUFFIX}"
echo "Version milestone: $BASE_VERSION; branch builds since milestone: $BUILD_COUNT; label: $VERSION" >&2
printf '%s\\n' "$VERSION"
