<#
MiniGit demo walkthrough.

Copies sample_repo/project into sample_repo/demo (the original is never changed),
then runs every MiniGit command on it, one step at a time.

    powershell -ExecutionPolicy Bypass -File sample_repo\walkthrough.ps1            # pauses after each step
    powershell -ExecutionPolicy Bypass -File sample_repo\walkthrough.ps1 -NoPause   # runs straight through

-Minigit <path> picks another build (default: minigit.exe in the repository root).
Note: the first run of minigit.exe copies itself to %LOCALAPPDATA%\Microsoft\WindowsApps
(MiniGit's normal first-run install), so 'minigit' then works from any terminal.
#>
param(
    [switch]$NoPause,
    [string]$Minigit = (Join-Path (Split-Path $PSScriptRoot -Parent) 'minigit.exe')
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path $Minigit)) { throw "minigit.exe not found at '$Minigit' (build it with 'make' or pass -Minigit)" }
$Minigit = (Resolve-Path $Minigit).Path

$project = Join-Path $PSScriptRoot 'project'
$demo = Join-Path $PSScriptRoot 'demo'
if (Test-Path $demo) { Remove-Item -LiteralPath $demo -Recurse -Force }
Copy-Item -LiteralPath $project -Destination $demo -Recurse
Set-Location -LiteralPath $demo

$script:step = 0
function Step([string]$title, [string]$why) {
    $script:step++
    Write-Host ''
    Write-Host ("=== Step {0}: {1}" -f $script:step, $title) -ForegroundColor Cyan
    if ($why) { Write-Host $why -ForegroundColor DarkGray }
}
function Mg {
    Write-Host ('> minigit ' + ($args -join ' ')) -ForegroundColor Yellow
    & $Minigit @args
    Write-Host ("  (exit code {0})" -f $LASTEXITCODE) -ForegroundColor DarkGray
}
function Note([string]$text) { Write-Host ('# ' + $text) -ForegroundColor Green }
function Pause-Step { if (-not $NoPause) { [void](Read-Host 'Press Enter for the next step') } }
function Head-Id { (Get-Content -LiteralPath '.minigit\refs\heads\main' -TotalCount 1).Trim() }

Step 'Create a repository' 'init writes .minigit/ with HEAD, index, objects/ and refs/heads/.'
Mg init
Pause-Step

Step 'Ignore generated files' 'Rules go into .minigitignore and the Dual Trie: *.log in the reversed-extension trie, build/ in the path trie.'
Mg ignore '*.log'
Mg ignore 'build/'
Mg ignore
Mg ignore -v debug.log
Mg ignore -v build/notes.tmp
Pause-Step

Step 'Stage the project' 'add . stages every file that is not ignored (std::set index, no duplicates).'
Mg add .
Mg add -v src/main.cpp
Mg add debug.log
Pause-Step

Step 'First commit' 'commit stores a snapshot under objects/<sha1>/ and moves the main branch.'
Mg commit 'Initial notes app'
$first = Head-Id
Pause-Step

Step 'Change a file and commit again' 'Only the changed file is stored in the new snapshot; the rest is inherited from the parent.'
Add-Content -LiteralPath 'src\notes.cpp' -Value '// TODO: search notes by keyword'
Note 'appended a line to src/notes.cpp'
Mg add src/notes.cpp
Mg commit 'Plan note search'
$second = Head-Id
Pause-Step

Step 'History' 'log rebuilds the commits into a doubly linked list: newest first, or oldest first with --reverse.'
Mg log
Mg log --reverse
Pause-Step

Step 'Undo and redo' 'Two deques hold the states (up to 20). undo steps HEAD back, redo puts it forward again.'
Mg undo
Mg log
Mg redo
Pause-Step

Step 'Create a branch and switch to it' 'A branch is one file in refs/heads/ holding a commit id.'
Mg branch feature
Mg branch
Mg checkout feature
Pause-Step

Step 'Commit on the branch' 'main does not move; only feature does.'
Add-Content -LiteralPath 'docs\usage.md' -Value 'Use notes add "<text>" to add a note.'
Note 'appended a line to docs/usage.md'
Mg add docs/usage.md
Mg commit 'Document adding a note'
Pause-Step

Step 'Back to main, then the whole graph' 'checkout <branch> restores that branch''s files. log --all --graph walks the commit DAG.'
Mg checkout main
Mg log --all --graph
Pause-Step

Step 'Restore an old commit' 'checkout <commit-id> restores the files of that commit and detaches HEAD; no branch moves. Ignored files stay untouched.'
Mg checkout $first.Substring(0, 7)
Note ('src/notes.cpp is back to its first version: ' + ((Get-Content 'src\notes.cpp') -join ' | '))
Note ('debug.log is still there (ignored): ' + (Test-Path 'debug.log'))
Mg branch
Pause-Step

Step 'Detached HEAD protects branches' 'commit, undo, redo and rollback refuse here, because they move a branch.'
Mg commit 'this is refused'
Mg checkout main
Pause-Step

Step 'Roll back main' 'rollback moves main back to an older commit and restores its files; the newer commit objects are kept.'
Mg rollback $first.Substring(0, 7)
Mg log
Note ("the commit '$($second.Substring(0, 7))' is still stored in .minigit\objects: " + (Test-Path ".minigit\objects\$second"))
Pause-Step

Step 'Help' 'Every command, generated from the command table.'
Mg help
Mg chekout
Write-Host ''
Write-Host "Walkthrough finished. The demo repository is in $demo" -ForegroundColor Cyan
