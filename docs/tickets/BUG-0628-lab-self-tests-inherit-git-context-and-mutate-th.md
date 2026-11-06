---
id: BUG-0628
title: Lab self-tests inherit Git context and mutate the parent checkout
status: open
kind: bug
stage: Validation
reported: 2026-11-05
source: 0.7.8 integration WSL test run with explicit Git context
role: 
versions: 
fixed_in:
branch: sync/upstream-0.7.8-main
commits:
tests: tools/tests/lab_selftest.py::git_context_case
signature:
docs:
related:
---

## Report
A local WSL run supplies Git environment variables to identify a Windows-created worktree. The lab self-test inherits them while creating temporary repositories, so its temporary init/add/commit operates on the parent integration checkout instead.

## Root cause
The git helper and a direct temporary init pass the inherited GIT_DIR/GIT_COMMON_DIR/GIT_WORK_TREE/GIT_INDEX_FILE unchanged. Evidence shows six temporary commits on the unpublished integration branch and CRLF-only blob churn. Original destination branches and personal installation hashes remain unchanged.

## Fix
Clear explicit Git repository/index context for temporary Git commands. Add a regression using two disposable repositories and an intentionally inherited outer context. Retain the old accidental branch under a local safety ref, normalize the index back to the original Git tree, preserve intended source changes, and remove test-only commits from the unpublished review branch. Do not force-push or rewrite upstream/history already published.

## Verification
RED: out/upstream077/git-context-red.log. GREEN: out/upstream077/git-context-green.log. Both use disposable directories. The complete isolated lab self-test additionally checks that the parent HEAD is unchanged. Cleanup audit: out/upstream077/harness-content-audit.json and harness-tree-summary.txt. WSL test Git now translates foreign worktree paths per Git invocation and refuses source-checkout mutations; it supplies no global inherited Git context.

## History
- 2026-11-05: opened.
