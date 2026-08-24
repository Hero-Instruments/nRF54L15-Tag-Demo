#!/usr/bin/env bash
#
# Cut a release: bump VERSION, commit, tag, push.
#
# Pushing a vX.Y.Z tag is what triggers .github/workflows/release.yml, which
# builds the firmware and publishes a public GitHub Release. That workflow
# hard-fails if the tag disagrees with VERSION, so the two must move together —
# which is the whole reason this script exists.

set -euo pipefail

die() {
	echo "release: $*" >&2
	exit 1
}

usage() {
	cat <<'EOF'
Cut a release: bump VERSION, commit, tag, push.

Usage: scripts/release.sh [patch|minor|major|X.Y.Z[-suffix]] [options]

  scripts/release.sh                 0.0.3 -> 0.0.4 (the default)
  scripts/release.sh minor           0.0.3 -> 0.1.0
  scripts/release.sh major           0.0.3 -> 1.0.0
  scripts/release.sh 1.2.0           explicit version
  scripts/release.sh 0.0.4-rc1       VERSION 0.0.4, tag v0.0.4-rc1 (pre-release)

Options:
  -n, --dry-run   show what would happen and exit without changing anything
  -y, --yes       skip the confirmation prompt
      --no-push   commit and tag locally, print the push command instead
  -h, --help      show this help
EOF
}

bump=""
dry_run=false
assume_yes=false
push=true

while [ $# -gt 0 ]; do
	case "$1" in
	-n | --dry-run) dry_run=true ;;
	-y | --yes) assume_yes=true ;;
	--no-push) push=false ;;
	-h | --help)
		usage
		exit 0
		;;
	-*) die "unknown option '$1' (try --help)" ;;
	*)
		[ -z "${bump}" ] || die "unexpected extra argument '$1'"
		bump="$1"
		;;
	esac
	shift
done

[ -n "${bump}" ] || bump=patch

# Work from the repo root so the script runs correctly from any subdirectory.
cd "$(git rev-parse --show-toplevel)" || die "not inside a git repository"

version_file=VERSION
[ -f "${version_file}" ] || die "no ${version_file} at the repo root"

# Read a numeric field out of VERSION. Deliberately identical to the parser in
# .github/workflows/release.yml, so this script and CI can never disagree about
# what the file says.
field() {
	sed -n "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*\([0-9][0-9]*\).*/\1/p" "${version_file}"
}

major="$(field VERSION_MAJOR)"
minor="$(field VERSION_MINOR)"
patch="$(field PATCHLEVEL)"

if [ -z "${major}" ] || [ -z "${minor}" ] || [ -z "${patch}" ]; then
	die "could not parse VERSION_MAJOR/VERSION_MINOR/PATCHLEVEL from ${version_file}"
fi

old_version="${major}.${minor}.${patch}"

# A -rc1 style suffix lives in the tag only, never in VERSION: release.yml
# strips it before comparing, and uses its presence to mark the GitHub Release
# as a pre-release.
suffix=""

case "${bump}" in
patch) patch=$((patch + 1)) ;;
minor)
	minor=$((minor + 1))
	patch=0
	;;
major)
	major=$((major + 1))
	minor=0
	patch=0
	;;
*)
	base="${bump%%-*}"
	if [ "${base}" != "${bump}" ]; then
		suffix="${bump#"${base}"}"
	fi
	if ! [[ "${base}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
		die "'${bump}' is not patch|minor|major or MAJOR.MINOR.PATCH[-suffix]"
	fi
	IFS=. read -r major minor patch <<<"${base}"
	;;
esac

new_version="${major}.${minor}.${patch}"
tag="v${new_version}${suffix}"

# --- Preconditions -----------------------------------------------------------
#
# All of these run before anything is mutated, so a failure here leaves the
# working tree exactly as it was — there is never a half-made release to unwind.

branch="$(git rev-parse --abbrev-ref HEAD)"
[ "${branch}" = main ] || die "on branch '${branch}', but releases are cut from main"

[ -z "$(git status --porcelain)" ] || die "working tree is dirty; commit or stash first"

echo "release: fetching origin..." >&2
git fetch --quiet origin main || die "could not fetch from origin"

# Releasing a stale tree ships something other than what was tested; releasing
# with unpushed commits puts the tag on a commit origin does not have.
local_head="$(git rev-parse HEAD)"
remote_head="$(git rev-parse FETCH_HEAD)"
if [ "${local_head}" != "${remote_head}" ]; then
	if git merge-base --is-ancestor "${local_head}" "${remote_head}"; then
		die "main is behind origin/main; pull first"
	elif git merge-base --is-ancestor "${remote_head}" "${local_head}"; then
		die "main has unpushed commits; push them first, then release"
	else
		die "main and origin/main have diverged"
	fi
fi

if git rev-parse -q --verify "refs/tags/${tag}" >/dev/null; then
	die "tag ${tag} already exists locally"
fi
if [ -n "$(git ls-remote --tags origin "refs/tags/${tag}")" ]; then
	die "tag ${tag} already exists on origin"
fi

# --- Plan --------------------------------------------------------------------

commit_subject="bump version to ${new_version}"

cat <<EOF

  VERSION  ${old_version} -> ${new_version}
  commit   "${commit_subject}"
  tag      ${tag}
EOF
if [ "${push}" = true ]; then
	echo "  push     origin main ${tag}  (triggers the Release workflow)"
else
	echo "  push     skipped (--no-push)"
fi
echo

if [ "${dry_run}" = true ]; then
	echo "release: dry run, nothing changed"
	exit 0
fi

if [ "${assume_yes}" != true ]; then
	# Read the answer from the terminal directly, so the prompt still works if
	# the script's stdin is a pipe.
	read -r -p "Continue? [y/N] " reply </dev/tty || die "aborted"
	case "${reply}" in
	y | Y | yes | Yes) ;;
	*) die "aborted" ;;
	esac
fi

# --- Mutate ------------------------------------------------------------------

# Rewrite the three fields in place, preserving the file's KEY = value shape.
# VERSION_TWEAK is left alone; CI ignores it.
sed -i.bak \
	-e "s/^\([[:space:]]*VERSION_MAJOR[[:space:]]*=[[:space:]]*\)[0-9][0-9]*/\1${major}/" \
	-e "s/^\([[:space:]]*VERSION_MINOR[[:space:]]*=[[:space:]]*\)[0-9][0-9]*/\1${minor}/" \
	-e "s/^\([[:space:]]*PATCHLEVEL[[:space:]]*=[[:space:]]*\)[0-9][0-9]*/\1${patch}/" \
	"${version_file}"
rm -f "${version_file}.bak"

# Re-read through the same parser CI uses, to be sure the rewrite produced what
# the workflow will compare the tag against.
written="$(field VERSION_MAJOR).$(field VERSION_MINOR).$(field PATCHLEVEL)"
if [ "${written}" != "${new_version}" ]; then
	git checkout -- "${version_file}"
	die "VERSION rewrite produced '${written}', expected '${new_version}'; reverted"
fi

git commit --quiet "${version_file}" -m "${commit_subject}"
# Lightweight tag, matching the tags already in this repo. release.yml passes
# --generate-notes, so a tag message would go unused anyway.
git tag "${tag}"

if [ "${push}" != true ]; then
	echo "release: committed and tagged ${tag} locally (not pushed)"
	echo
	echo "To release, run:"
	echo "  git push --atomic origin main ${tag}"
	echo "To undo:"
	echo "  git tag -d ${tag} && git reset --hard HEAD~1"
	exit 0
fi

# --atomic so the commit and the tag land together: a tag that arrives without
# its commit gives CI a ref that origin/main does not contain.
if ! git push --atomic origin main "${tag}"; then
	echo
	echo "To undo the local commit and tag:" >&2
	echo "  git tag -d ${tag} && git reset --hard HEAD~1" >&2
	die "push failed; nothing was released"
fi

# Turn the remote into a browsable URL for the run that just started.
repo_url="$(git remote get-url origin)"
repo_url="${repo_url%.git}"
repo_url="${repo_url/git@github.com:/https://github.com/}"

echo
echo "release: pushed ${tag}"
echo "  gh run watch"
echo "  ${repo_url}/actions"
