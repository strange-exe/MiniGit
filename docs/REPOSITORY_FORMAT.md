# MiniGit on-disk format

Reference for everything MiniGit stores in a repository. All state is plain text, so a repository can be inspected with any editor. Source of truth: `module/include/RepoUtils.h` (HEAD, refs, commit metadata), `StagingArea.h` (index), `CommitHistory.h` (history) and `IgnoreManager.h` (ignore rules).

## Layout

```
<project>/
├── .minigitignore            # ignore rules, one per line
└── .minigit/
    ├── HEAD                  # "ref: refs/heads/<branch>" or a full commit id (detached)
    ├── index                 # staged paths, one per line
    ├── history               # undo / redo deques
    ├── refs/
    │   └── heads/<branch>    # one file per branch: the tip commit id
    └── objects/
        └── <commit-id>/
            ├── metadata      # commit header and file lists
            └── snapshot/     # stored file contents, same relative paths as the project
```

## HEAD

| Content | Meaning |
|---|---|
| `ref: refs/heads/main` | On branch `main`. The reference must start with `refs/heads/`, contain no `..` or `\`, and not end with `/`. |
| 40 lowercase hex digits | Detached HEAD, created by `checkout <commit-id>`. The commit must exist. |
| anything else | Damaged. Commands stop and report it instead of guessing. |

## Branch references

`.minigit/refs/heads/<branch>` holds the 40-character commit id and a newline. An empty file means a branch with no commits yet. Branch names can contain `/`, so `feature/login` is stored as `refs/heads/feature/login`. Files are replaced atomically: the text goes to a hidden `.<name>.tmp` file that is then renamed over the target. Branch names cannot start with `.`, so a leftover temporary file is never mistaken for a branch.

## Index

One normalized, `/`-separated, repository-relative path per line, kept sorted. A tracked file that was deleted from disk is staged for deletion and written as `deleted:<path>`. The `:` cannot occur in a Windows file name, so the marker cannot collide with a real path there. Entries that are absolute, contain `..` or point into `.minigit/` are dropped when the file is read.

## Commit objects

`.minigit/objects/<commit-id>/metadata`:

```
commit <id>
parent <id | none>
timestamp <unix seconds>
author <name>
message <one line>
files <N>
<N path lines>
deleted <M>               # only present when the commit removes tracked files
<M path lines>
```

- A commit stores only the files that were staged. The complete file set is rebuilt by following `parent` links; a path in `deleted` stops being tracked from that commit on.
- The file lists are read by position and count, not by keyword, so a path such as `message 1.txt` is safe.
- A metadata file is rejected as damaged if a header line or part of a list is missing, or if a listed path is unsafe.
- `metadata` is written last. A commit directory without it was never completed.
- `parents <id> <id>` is also accepted when reading, which keeps the reader ready for multi-parent commits; `commit` currently writes a single `parent` line.

The commit id is the SHA-1 of the parent id, timestamp, message, each staged file's path, size and content, and each staged deletion.

## History (undo / redo)

```
UNDO_COUNT <n>
<entry> x n
REDO_COUNT <m>
<entry> x m
```

Each entry is:

```
<commit> <parent> <timestamp> <stagedCount> <branch> <expectedHead>
<message>
<stagedCount lines: staged paths or deleted:<path>>
```

`-` stands for an empty value. `branch` and `expectedHead` guard against applying an entry on another branch or after the branch moved; files from older versions lack them and are checked by parentage. The undo deque holds at most 20 entries. A truncated or hand-edited file is discarded as a whole.

## `.minigitignore`

One rule per line; blank lines and comments are kept as written. Three kinds of rule are supported:

| Rule | Kind | Stored in |
|---|---|---|
| `secret.txt`, `src/config.h` | exact file | forward path Trie |
| `build/` | directory prefix | forward path Trie, as a prefix rule |
| `*.log` | extension | reversed-extension Trie, stored as `gol.` |

Every Trie node that ends a rule keeps its 1-based line number, which `minigit ignore -v <path>` prints. Ignore rules only keep untracked files out: a file that is already tracked stays stageable.
