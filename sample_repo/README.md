# Sample repository and demo walkthrough

`project/` is a tiny C++ notes app used to show every MiniGit command on real files.
It also holds two generated files, `debug.log` and `build/notes.tmp`, that MiniGit is told to ignore.

## Run the walkthrough (Windows)

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File sample_repo\walkthrough.ps1            # pauses after each step
powershell -ExecutionPolicy Bypass -File sample_repo\walkthrough.ps1 -NoPause   # runs straight through
```

The script copies `project/` to `sample_repo/demo/` (ignored by git) and works only there, so the sample
itself never changes. Run it again to start from scratch. It uses `minigit.exe` from the repository root;
pass `-Minigit <path>` to use another build. As with any first run, `minigit.exe` copies itself to
`%LOCALAPPDATA%\Microsoft\WindowsApps` so that `minigit` works from any terminal.

## What each step shows

| Step | Commands | What to point out |
|---|---|---|
| 1 | `init` | `.minigit/` with `HEAD`, `index`, `objects/`, `refs/heads/` |
| 2 | `ignore '*.log'`, `ignore build/`, `ignore`, `ignore -v` | Dual Trie: `*.log` in the reversed-extension trie, `build/` in the path trie; the matching line is reported |
| 3 | `add .`, `add -v`, `add debug.log` | 6 files staged, 2 skipped as ignored; an ignored file is refused |
| 4 | `commit "Initial notes app"` | Snapshot under `objects/<sha1>/`; `main` moves |
| 5 | edit, `add`, `commit` | Only the changed file is stored; the rest is inherited |
| 6 | `log`, `log --reverse` | Doubly linked list walked from either end |
| 7 | `undo`, `log`, `redo` | Two deques, history limit 20 |
| 8 | `branch feature`, `branch`, `checkout feature` | A branch is one file in `refs/heads/` |
| 9 | edit, `add`, `commit` on `feature` | `main` stays where it was |
| 10 | `checkout main`, `log --all --graph` | Files switch back; the commit DAG with branch labels |
| 11 | `checkout <first-commit-id>`, `branch` | Old files restored, `debug.log` untouched, `HEAD` detached |
| 12 | `commit` (refused), `checkout main` | Commands that move a branch refuse on a detached `HEAD` |
| 13 | `rollback <first-commit-id>`, `log` | `main` moves back; newer commit objects are kept |
| 14 | `help`, `chekout` | Help from the command table; typo suggestion |

## Doing it by hand

Copy `project/` anywhere, open a terminal in the copy and type the commands from the table in order.
Commit ids differ on every run: take them from `minigit log`.
