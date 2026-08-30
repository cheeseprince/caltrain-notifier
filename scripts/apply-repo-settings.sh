#!/usr/bin/env bash
# Re-apply this repository's GitHub settings, so they are defined in the tree
# rather than only in somebody's browser history.
#
# The point of this file is a recreate. If the repository is ever deleted and
# made again -- which is the plan for going public, to start from a clean
# object store -- every setting below is lost. Clicking them back one at a time
# is how a repo quietly ends up configured differently from how its own docs
# describe it.
#
#   gh auth status && bash scripts/apply-repo-settings.sh
#
# Settings are matched to the sibling repository cheeseprince/obd-gauge-cluster,
# which is the settled convention for this account.
set -euo pipefail
REPO="cheeseprince/caltrain-notifier"

echo "== description + topics =="
gh repo edit "$REPO" \
  --description "Desk sign showing the next three Caltrain departures, with a border that escalates as the train nears. ESP32 + 3.5\" ILI9488; timetable compiled into flash, live data from 511 SF Bay." \
  --add-topic esp32 --add-topic platformio --add-topic caltrain \
  --add-topic transit --add-topic gtfs --add-topic siri \
  --add-topic 511 --add-topic ili9488 --add-topic departure-board

echo "== merge strategy: SQUASH ONLY =="
# Public history stays one-commit-per-PR: easy to read, easy to revert, and no
# WIP or fixup commits leaking into main. Merge and rebase merges are disabled
# so the choice cannot be made by accident in the UI. Squash title and body come
# from the PR rather than the branch's commit messages -- concatenating those
# back in is exactly the noise squashing is meant to remove.
gh api -X PATCH "repos/$REPO" \
  -F allow_squash_merge=true \
  -F allow_merge_commit=false \
  -F allow_rebase_merge=false \
  -f squash_merge_commit_title=PR_TITLE \
  -f squash_merge_commit_message=PR_BODY \
  -F delete_branch_on_merge=true \
  --silent

echo "== Dependabot alerts + security updates =="
gh api -X PUT "repos/$REPO/vulnerability-alerts"
gh api -X PUT "repos/$REPO/automated-security-fixes"

echo "== workflow token: read-only by default =="
gh api -X PUT "repos/$REPO/actions/permissions/workflow" \
  -f default_workflow_permissions=read \
  -F can_approve_pull_request_reviews=false --silent

# ---------------------------------------------------------------------------
# Everything below requires the repository to be PUBLIC (or a paid plan). On a
# free private repo these endpoints return 403 or 404, so they are attempted and
# reported rather than being allowed to abort the run. Re-run this script after
# going public and they will apply.
# ---------------------------------------------------------------------------

echo "== private vulnerability reporting (public repos only) =="
gh api -X PUT "repos/$REPO/private-vulnerability-reporting" >/dev/null 2>&1 \
  && echo "   enabled" || echo "   SKIPPED - needs a public repository"

echo "== secret scanning + push protection (public repos only) =="
gh api -X PATCH "repos/$REPO" -F security_and_analysis[secret_scanning][status]=enabled \
  -F security_and_analysis[secret_scanning_push_protection][status]=enabled --silent 2>/dev/null \
  && echo "   enabled" || echo "   SKIPPED - needs a public repository"

echo "== branch protection on main (public repos only) =="
# Required status checks are the four this project's CI actually produces. Names
# must match the job names in .github/workflows/ci.yml exactly, including the
# matrix suffix, or the check simply never becomes required and the gate is
# decorative.
#
# NO required approving reviews, deliberately. On a solo repository you cannot
# approve your own pull request, so requiring one turns every merge into an
# admin bypass -- a rubber stamp that is worse than an honest open gate.
if gh api -X PUT "repos/$REPO/branches/main/protection" --input - >/dev/null 2>&1 <<'JSON'
{
  "required_status_checks": {
    "strict": false,
    "contexts": [
      "Secret scan",
      "Timetable freshness",
      "Host tests",
      "Device build (caltrain)",
      "Device build (caltrain_v20)",
      "Fuzz smoke test (siri_parse, ~90s, not a fuzzing campaign)"
    ]
  },
  "enforce_admins": false,
  "required_pull_request_reviews": null,
  "restrictions": null,
  "allow_force_pushes": false,
  "allow_deletions": false
}
JSON
then echo "   applied"; else echo "   SKIPPED - needs a public repository or GitHub Pro"; fi

echo "== ruleset: protect release tags (public repos only) =="
if gh api -X POST "repos/$REPO/rulesets" --input - >/dev/null 2>&1 <<'JSON'
{
  "name": "Protect release tags",
  "target": "tag",
  "enforcement": "active",
  "conditions": {"ref_name": {"include": ["refs/tags/v*"], "exclude": []}},
  "rules": [{"type": "deletion"}, {"type": "non_fast_forward"}]
}
JSON
then echo "   applied"; else echo "   SKIPPED - needs a public repository, or already exists"; fi

echo "== actions: require SHA-pinned action references =="
# Every action in .github/workflows/ is already pinned to a 40-char commit SHA
# by hand. This makes GitHub enforce it, so a future workflow that reaches for a
# floating tag is refused rather than merged and noticed later -- the sibling
# obd-gauge-cluster repo has exactly that floating-tag pattern in its release
# job, which is what prompted checking here.
gh api -X PUT "repos/$REPO/actions/permissions" --input - <<'JSON' >/dev/null 2>&1 \
  && echo "   enforced" || echo "   SKIPPED - needs a public repository"
{"enabled": true, "allowed_actions": "all", "sha_pinning_required": true}
JSON

echo "== GitHub Pages (OTA release channel) =="
# The gh-pages branch (published by release.yml / tools/publish_ota.sh) serves
# manifest.txt, manifest.sig and the .bin files that devices fetch over the
# air (OTA_BASE_URL in platformio.ini). Without this, OTA_BASE_URL 404s and no
# device can ever update. Public-repo-only, same as the block above -- GitHub
# Pages serves a private repository only on Pro/Team/Enterprise.
gh api -X POST "repos/$REPO/pages" -f "source[branch]=gh-pages" -f "source[path]=/" \
  >/dev/null 2>&1 \
  && echo "   enabled" || echo "   SKIPPED - needs a public repository, or already configured"

cat <<'NOTE'

Manual, UI-only steps this script cannot do:
  1. Settings -> General -> UNCHECK "Include this code in the GitHub Archive Program"
  2. Settings -> Emails (account level) -> CHECK "Block command line pushes that
     expose my email". Account-wide, so it only needs doing once ever.
NOTE

# ---------------------------------------------------------------------------
# MANUAL STEP -- OTA_SIGNING_KEY does NOT survive a repository recreate.
# ---------------------------------------------------------------------------
# Actions secrets are deleted along with the repository. release.yml fails
# BY DESIGN (exit 1, not a silent unsigned publish) the first time it runs
# afterward, until this is restored from the off-machine backup:
echo
echo "== MANUAL STEP: restore OTA_SIGNING_KEY =="
echo "Actions secrets do NOT survive a repository recreate. Before cutting a"
echo "release, restore it from the off-machine backup:"
echo
echo "  gh secret set OTA_SIGNING_KEY --repo $REPO < /path/to/caltrain-ota-signing-key.pem"
echo
echo "Without it, release.yml's 'Sign the manifest' step fails on purpose"
echo "rather than publishing a release every device would silently refuse."
