# Solo-agent PR workflow with local fast-forward merges

Simex is developed by one person working with several AI agents, so the trust model differs from an open-source project: there is no need for PR labels, vouching, or branch protection, but there is a hard requirement that no merge happens without explicit human consent. Every task runs in a dedicated git worktree on its own branch named after the task. The agent builds and tests the branch in WSL, commits, pushes, and opens a GitHub pull request, but never merges on its own.

Merges happen locally, not through GitHub's merge button: after CI is green and the user consents, the agent fast-forwards `master` in the Windows clone and pushes it. This keeps `master` linear, which the WSL main repo relies on when it follows `master` with `fetch` plus `merge --ff-only`, and it makes the user's consent the only merge trigger. A closeout script (`scripts/merge-sync.ps1`) then verifies the merged `master` with a cold build and ctest in WSL, fast-forwards the WSL main repo, and removes the worktree, the local and remote branch, and the verification build directory.

**Status:** accepted

**Consequences:** Verification build directories are deleted after every run, so each verification is a cold build and no stale artifacts accumulate. Feature branches accept `--force-with-lease` only and `master` is never force-pushed. A failed fast-forward or a rebase conflict stops the agent and hands the problem to the user. The pre-PR WSL build and GitHub CI (gcc and clang) gate every PR; the post-merge WSL run is a smoke check, not a gate. Parallel open pull requests are allowed; the second one to merge rebase onto the advanced `master` first.
