#requires -Version 7
<#
.SYNOPSIS
    Post-merge closeout for the solo-agent PR workflow.

.DESCRIPTION
    Runs after the user approves a merge and the agent has fast-forwarded
    master and pushed it to origin. Verifies master with a cold build and
    ctest in WSL, fast-forwards the WSL main repo, then cleans up the task
    worktree, the local and remote task branch, and the verification build
    directory. Any step that fails stops the script; report and ask the user.

.PARAMETER Task
    The task/branch name that was just merged into master.
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$Task
)

$ErrorActionPreference = 'Stop'
$env:WSL_UTF8 = '1'

$CloneRoot  = 'D:\wsl_codebase\simex'
$WslBuildDir = '\\wsl.localhost\Ubuntu\home\wangyuk\build\simex\master'
$WslMainRepo = '/home/wangyuk/coding_projects/quant_dev_projects/simex'

function Assert-LastExit([string]$What) {
    if ($LASTEXITCODE -ne 0) {
        throw "$What failed with exit code $LASTEXITCODE."
    }
}

# 1) Mirror the master checkout into the WSL verification build directory.
#    tmp/ holds untracked reference material and must not reach the build dir.
New-Item -ItemType Directory -Force -Path $WslBuildDir | Out-Null
robocopy $CloneRoot $WslBuildDir /MIR /XD .git .worktrees build tmp /XF .git /NFL /NDL /NJH /NJS /NP
if ($LASTEXITCODE -ge 8) { throw "robocopy failed with exit code $LASTEXITCODE." }

# 2) Cold build and test master in WSL.
wsl.exe -d Ubuntu --cd /home/wangyuk/build/simex/master --exec bash -lc 'cmake --preset ci-gcc && cmake --build --preset ci-gcc && ctest --preset test-ci-gcc'
Assert-LastExit 'WSL build/test'

# 3) Fast-forward the WSL main repo to the pushed master.
wsl.exe -d Ubuntu --cd $WslMainRepo --exec bash -c 'git fetch /mnt/d/wsl_codebase/simex master && git merge --ff-only FETCH_HEAD'
Assert-LastExit 'WSL main repo fast-forward'

# 4) Clean up worktree, branches, and the verification build directory.
$worktreePath = Join-Path $CloneRoot ".worktrees\$Task"
if (Test-Path -LiteralPath $worktreePath) {
    git -C $CloneRoot worktree remove ".worktrees/$Task" --force
    Assert-LastExit 'git worktree remove'
}
git -C $CloneRoot branch -d $Task
Assert-LastExit 'git branch -d'
git -C $CloneRoot push origin --delete $Task
Assert-LastExit 'git push origin --delete'
Remove-Item -LiteralPath $WslBuildDir -Recurse -Force

Write-Host "Merge closeout for '$Task' complete."
