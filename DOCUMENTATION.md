# MiniGit CLI — Technical & User Documentation

**Project Title:** MiniGit: A Lightweight Version Control System Using C++ and Data Structures  
**Course:** Project-Based Learning in Data Structures & C++ (PBL in DS CPP)  
**Team Name:** Blaze (DSCPP-III-2026-T296)  
**Lead & CLI Architect:** Abhinesh Gangwar (2510370192)  
**Staging & Trie Lead:** Sparsh Jain (2510370159)  

---

## Table of Contents
1. [Introduction & Scope](#1-introduction--scope)
2. [Abhinesh's Responsibilities & Implementation](#2-abhineshs-responsibilities--implementation)
   - [2.1 Repository Setup (`minigit init`)](#21-repository-setup-minigit-init)
   - [2.2 Command Dispatcher Architecture](#22-command-dispatcher-architecture)
   - [2.3 Status & Error Mapping (`IS_STAGED`)](#23-status--error-mapping-is_staged)
   - [2.4 `.minigitignore` CLI & Validation](#24-minigitignore-cli--validation)
   - [2.5 Zero-Config First-Run Self-Installation](#25-zero-config-first-run-self-installation)

2B. [Sparsh's Responsibilities & Implementation](#2b-sparshs-responsibilities--implementation)

2C. [Snehil's Responsibilities & Implementation](#2c-snehils-responsibilities--implementation)
   - [2C.1 Commit Log (Doubly Linked List)](#2c1-commit-log-doubly-linked-list)
   - [2C.2 The `log` Command](#2c2-the-log-command)
   - [2C.3 Restoring a Commit (`checkout <commit-id>`)](#2c3-restoring-a-commit-checkout-commit-id)
   - [2C.4 Tests](#2c4-tests)

3. [Core Data Structures](#3-core-data-structures)
   - [3.1 Trie (Prefix Tree) for Ignored Files](#31-trie-prefix-tree-for-ignored-files)
   - [3.2 Staging Index (`.minigit/index`)](#32-staging-index-minigitindex)
   - [3.3 Directed Acyclic Graph (DAG) for Commits and Branches](#33-directed-acyclic-graph-dag-for-commits-and-branches)
   - [3.4 Double-Ended Queue for Undo and Redo](#34-double-ended-queue-for-undo-and-redo)
   - [3.5 Doubly Linked List for the Commit Log](#35-doubly-linked-list-for-the-commit-log)
   - [3.6 Commit Object and History File Formats](#36-commit-object-and-history-file-formats)
4. [Command Reference & Usage Examples](#4-command-reference--usage-examples)
5. [Rollback Architecture & Semantics](#5-rollback-architecture--semantics)
6. [DAG-Based Branch Management](#6-dag-based-branch-management)
7. [Compilation, Portability & Testing](#7-compilation-portability--testing)

---

## 1. Introduction & Scope

MiniGit is a local, lightweight version-control tool developed in C++ to demonstrate the real-world utility of fundamental data structures. Professional VCS platforms (e.g., Git) introduce complex workflows (remotes, rebasing, merge conflict resolution) that often obscure core mechanics for students. MiniGit models the essential version-control lifecycle:
- Initializing local repository metadata.
- Tracking and staging files using an index.
- Blocking untracked files through prefix-tree pattern matching (`.minigitignore`).
- Querying staging status in $O(\log n)$ (ordered set) and ignore status in $O(L)$ (Trie) time.
- Recording commits, undoing/redoing them (double-ended queue) and listing history (doubly linked list).
- Restoring any saved commit, branching on a commit graph (DAG) and rolling a branch back to an earlier commit.

---

## 2. Abhinesh's Responsibilities & Implementation

Abhinesh is responsible for **Repository Initialization**, the **CLI Interface**, the **Command Dispatcher**, **Ignore Validation**, and **Result Status Mapping**.

### 2.1 Repository Setup (`minigit init`)
- **Header:** `include/InitCommand.h`
- **Source:** `src/InitCommand.cpp`
- **Behavior:**
  1. Creates the `.minigit` root directory.
  2. Creates `.minigit/objects/` (where commit snapshots and metadata reside).
  3. Creates `.minigit/refs/heads/`, the folder for the branch file that `HEAD` points to.
  4. Initializes `.minigit/HEAD` pointing to default branch (`ref: refs/heads/main\n`).
  5. Initializes `.minigit/index` (empty file tracking staged paths).
  6. Initializes empty `.minigitignore` at the repository root if not present.
  7. Idempotent: If `.minigit` already exists, reports `Reinitialized existing MiniGit repository`. Files are only written when missing (`ensureFile`), so the index, `HEAD` and ignore rules are never overwritten.
  8. Every directory and file creation is checked. For example, a regular file named `.minigit` makes init fail with `fatal: cannot create directory ...` instead of reporting success.

### 2.2 Command Dispatcher Architecture
- **Header:** `include/CLI.h`
- **Source:** `src/CLI.cpp`
- **Function:** `minigit::CLI::dispatch(int argc, char** argv, const std::string& root)`

The dispatcher acts as the central router between terminal inputs and core engine modules:

```
[Terminal Input: argv]
         │
         ▼
 ┌─────────────────┐   findCommand(argv[1])   ┌──────────────────────────────┐
 │ CLI::dispatch() │ ───────────────────────► │ command table (one row each) │
 └────────┬────────┘                          │ name · aliases · forms · run │
          │                                   └──────────────────────────────┘
          ├── init ─────────────► initRepo()                      ──► InitResult
          ├── add ──────────────► addFile() / addAll() / addVerify() ─► AddResult (IS_STAGED)
          ├── remove, rm ───────► removeFile()                    ──► AddResult
          ├── ignore ───────────► ignoreAdd() / ignoreRemove() / ignoreVerify() ─► IgnoreResult
          ├── commit, undo, redo ► commitRepo() / undoCommit() / redoCommit() ─► CommitResult
          ├── log ──────────────► showLog()                       ──► LogResult
          └── install, uninstall, help
```

**Why a table instead of an `if`/`else` chain?** Each command is one row holding its name, aliases, usage forms and handler function. Everything users see is generated from those rows: the `minigit help` list, `minigit help <command>`, `minigit <command> --help`, and the usage printed when arguments are wrong. Adding a command means adding one row, and the help text cannot go out of date. (Before, the usage strings were copied by hand into several places and had already drifted, e.g. help advertised `remove <dir>`, which did nothing.)

**Argument checking:** a handler returns an exit code, or `kUsage` when its arguments do not match any of its forms. The dispatcher then prints that command's usage and exits with 1. Extra arguments are therefore rejected rather than silently ignored (`minigit remove a.txt b.txt`, `minigit commit "msg" extra`).

**Typo suggestions:** for an unknown command, the dispatcher computes the Levenshtein edit distance (dynamic programming with two rolling rows, $O(|a| \cdot |b|)$ time, $O(|b|)$ space) to every command name and suggests the closest one within 2 edits:
```
$ minigit comit
minigit: 'comit' is not a minigit command. See 'minigit --help'.

The most similar command is
    commit
```

### 2.3 Status & Error Mapping (`IS_STAGED`)
The CLI maintains pure separation between business logic and console presentation:
- Core functions return structured result objects (`InitResult`, `AddResult`, `IgnoreResult`, `CommitResult`, `LogResult`) and never print.
- The CLI formats user messages and maps exit codes (0 for success, 1 for errors).
- **New `IS_STAGED` Status Case:**
  When checking staging status (`minigit add -v <file>`), if the path is found in `.minigit/index`, the status returns `AddResult::IS_STAGED`:
  ```cpp
  case AddResult::IS_STAGED:
      std::cout << "'" << p << "' is staged\n";
      return 0;
  ```

### 2.4 `.minigitignore` CLI & Validation
The ignore interface handles input sanitation before updating rules:
- Rejects empty or whitespace-only patterns, and any pattern outside the repository: `..`, `a/../../x`, absolute paths and drive letters. This reuses the same `isOutsideRepo(normalizePath(...))` check as the add and ignore modules, so the rule lives in one place.
- `-r`/`--remove` or `-v`/`--verify` without a pattern is an error, never saved as a rule.
- Patterns reach MiniGit exactly as typed: the MinGW runtime's wildcard expansion is turned off (`_CRT_glob = 0` in `main.cpp`), so `minigit ignore *.log` stores `*.log`, not the names of the `.log` files in the current folder.
- Dispatches:
  - `minigit ignore`: Lists the rules with their line numbers.
  - `minigit ignore <pattern>`: Validates input, appends to `.minigitignore`, and updates the Trie.
  - `minigit ignore -r <pattern>`: Removes rule from file and Trie; outputs `"'<pattern>' is not present in .minigitignore"` if not ignored.
  - `minigit ignore -v <path>`: Returns whether the path is ignored and prints the **exact line number** in `.minigitignore`.

### 2.5 Zero-Config First-Run Self-Installation
To solve PowerShell's restriction requiring `.\` for current-directory binaries:
- On first execution, `minigit.exe` automatically copies itself to `%LOCALAPPDATA%\Microsoft\WindowsApps\minigit.exe` (or `~/.local/bin/minigit` on Unix).
- Because `WindowsApps` is in every Windows 10/11 user's `PATH` by default, `minigit` becomes runnable globally from any folder or terminal window without administrator privileges.
- **Refresh:** when a newer build is run (its modification time is later than the installed copy's), the installed copy is replaced. Otherwise a rebuilt `minigit.exe` would never reach the global `minigit` command.
- **Where it runs:** self-install is called from `main()`, not from `CLI::dispatch()`. The test programs call `dispatch()` directly, so they can never install themselves as `minigit`.
- On Linux/macOS the running binary is located through `/proc/self/exe`, not the current working directory.

## 2B. Sparsh's Responsibilities & Implementation

Sparsh is responsible for the **Ignore Engine (Dual Trie)**, **`.minigitignore` management**, and the **Staging Area** with the `add` and `remove` commands.

### 2B.1 Ignore Engine
- **Headers:** `include/Trie.h`, `include/IgnoreManager.h`
- **Sources:** `src/Trie.cpp`, `src/IgnoreManager.cpp`
- **`Trie`:** a pure data structure with no file I/O. It provides `insert`, `search`, `find`, `remove` (with pruning) and `findMatch`. See section 3.1 for its internals.
- **`IgnoreManager`:** loads `.minigitignore`, parses each line into an extension, file or directory rule, and keeps one forward Trie and one reversed-extension Trie in sync with the file.
  - `addPattern` rejects invalid or unsupported patterns (negation `!`, wildcards other than `*.ext`) with a clear message, and reports a rule that already exists with its line number.
  - `removePattern` deletes the line and rebuilds both Tries so stored line numbers stay correct. If the path is ignored through a different rule, it names that rule.
  - `verify` / `isIgnored` return the matching rule and its line number, and apply the built-in `.minigit/` rule.
- **File handling:** CRLF line endings are tolerated, comments (`#`) and blank lines are skipped, and the file is written atomically so a failed write keeps the old rules.

### 2B.2 Staging Area
- **Header / Source:** `include/StagingArea.h`, `src/StagingArea.cpp`
- Keeps staged files and staged deletions in two `std::set` containers and persists them to `.minigit/index`. The file format and safe-loading rules are in section 3.2.
- **Path normalization:** `normalizePath()` in `include/PathUtils.h` converts `\` to `/`, removes `./`, and makes absolute paths repository-relative. Every module that handles paths uses it.

### 2B.3 Add and Remove
- **Header / Source:** `include/AddCommand.h`, `src/AddCommand.cpp`
- `addFile` stages one file, or a directory recursively. `addAll` stages the whole repository. `addVerify` backs `add -v`. `removeFile` unstages a file, a directory or everything (`remove .`).
- During a recursive walk, each path is checked against the ignore rules first. An ignored directory is skipped as a whole (`disable_recursion_pending`), so its contents are never visited. A file that is already tracked stays stageable even if a later rule would ignore it.
- A tracked file that was deleted from disk is staged as a deletion (`deleted:<path>`), so the next commit removes it from the snapshot.
- Results come back as `AddResult` / `IgnoreResult` structs, and only the CLI prints them.

### 2B.4 Tests
- `test_trie`: insert/search, duplicate and empty keys, directory boundaries, exact-path rules, extension rules and node pruning.
- `test_add`: staging, duplicate prevention, ignore enforcement and persistence across runs.

## 2C. Snehil's Responsibilities & Implementation

Snehil is responsible for the **commit log** (a doubly linked list), the **`log` command**, and **`checkout <commit-id>`**, which restores the files of a saved commit.

### 2C.1 Commit Log (Doubly Linked List)
- **Headers:** `include/CommitNode.h`, `include/CommitLog.h`
- **Source:** `src/CommitLog.cpp`
- **`CommitNode`:** one commit, filled from `.minigit/objects/<id>/metadata`. It holds `id`, `parentId` (empty for the first commit), `message`, `timestamp` and the list of `files`, plus two pointers: `prev` (the older commit) and `next` (the newer commit).
- **`CommitLog`:** owns the list and a cursor. The oldest commit is `head()` and the newest is `tail()`.
  - `insert(node)` appends a commit as the newest node in $O(1)$.
  - `find(id)` looks a commit up by exact id, walking from the oldest node in $O(n)$.
  - `next()` and `prev()` move the cursor toward newer or older commits and return `nullptr` (without moving) at either end. `resetToHead()` and `resetToTail()` position the cursor before a walk.
  - `load()` rebuilds the list from disk. It reads `HEAD` (a branch reference or a raw commit id), then follows each `parent` line in the metadata from the newest commit back to the first, and inserts the commits oldest-first.
- **Safety:** `load()` keeps a visited set, so a damaged parent cycle cannot loop forever, and it stops at the first unreadable commit instead of crashing. A repository with no commits loads as an empty list.
- **Why a doubly linked list:** `log` needs the history in both directions. Because every node links to its older and newer neighbour, newest-first and oldest-first are each a single $O(n)$ walk from one end, with no sorting and no copy into an array.

### 2C.2 The `log` Command
- **Header / Source:** `include/LogCommand.h`, `src/LogCommand.cpp`
- `showLog(root, reverse)` returns a `LogResult` with a status (`OK`, `EMPTY`, `NOT_A_REPO`, `ERROR`) and a list of `LogEntry` values (`commitId`, `parentId`, `message`, `timestamp`, `fileCount`). Like every other module it never prints; only the CLI does.
- **Newest first (default):** the cursor starts at `tail()` and walks backward with `prev()`.
- **Oldest first (`--reverse`):** the cursor starts at `head()` and walks forward with `next()`.
- `formatTimestamp()` turns the stored unix time into `YYYY-MM-DD HH:MM:SS` (local time).
- The graph form of the log (`log --all --graph`) is described in section 6.2.

### 2C.3 Restoring a Commit (`checkout <commit-id>`)
- **Header / Source:** `include/RestoreCommand.h`, `src/RestoreCommand.cpp`
- Restores the working folder to the snapshot stored in `.minigit/objects/<commit-id>/snapshot/`. The argument may be a full 40-character id or a unique prefix of at least 4 characters.
- The safety checks, the temporary-copy-and-backup restore, the handling of ignored files and the detached-HEAD rules are described step by step in section 6.2, item 5.
- It is the other half of rollback: `checkout <commit-id>` leaves every branch where it is, whereas `rollback <commit-id>` moves the active branch (section 5).

### 2C.4 Tests
- `test_commitlog`: the list on its own: insert, find, next, prev, and behaviour at both ends.
- `test_log`: `log` and `log --reverse` over real commits, after an undo, on an empty repository, and through the CLI.
- `test_checkout`: exact restore, refusal when work would be lost, ignored files untouched, deletions honoured, detached-HEAD rules, and damaged or unsafe commits rejected without changing anything.

---

---

## 3. Core Data Structures

### 3.1 Trie (Prefix Tree) for Ignored Files
- **Header:** `include/Trie.h`
- **Source:** `src/Trie.cpp`
- **Class:** `minigit::Trie`
- **Why Trie?** Linear string searching against dozens of ignore patterns takes $O(N \cdot L)$ time per file. A Trie checks ignore status in $O(L)$ where $L$ is the length of the file path, regardless of how many patterns exist in `.minigitignore`.

#### Dual-Trie Design:
1. **Path Trie (Forward Trie):**
   - Stores exact file paths (`passwords.txt`, `src/config.h`) and directory prefix rules (`build/`, `logs/`).
   - Suffix `/` flags directory prefix rules: any path starting with `build/` (e.g. `build/output.o`) matches immediately upon traversing the prefix nodes.
2. **Extension Trie (Reversed Trie):**
   - Wildcard extension rules like `*.log` or `*.exe` cannot be stored directly in a prefix tree.
   - MiniGit reverses the pattern (`*.exe` becomes key `exe.`) and stores it in the Extension Trie.
   - File extensions are reversed and matched from right to left in $O(E)$ time.
3. **Line Number Storage:**
   - Each terminal node retains the 1-based line number of `.minigitignore`.
   - `minigit ignore -v` queries the Trie and prints the exact line that blocked the file.

#### How the Trie Works Internally
- **Node layout:** each node holds a `std::map<char, unique_ptr<Node>>` of children, a `terminal` flag, a `prefixRule` flag, the 1-based `line`, and the rule text as written (`pattern`).
- **`insert(key, pattern, line, prefixRule)`:** walks or creates one node per character and marks the last node terminal. It returns `false` for an empty key or a key that already exists, so the same rule is never stored twice.
- **`search(key)` / `find(key)`:** exact lookup. A prefix of a stored key is not a match, so `secret` does not match the rule `secret.txt`.
- **`findMatch(text)`:** walks `text` once. It matches as soon as it passes a terminal node marked `prefixRule` (a `build/` directory rule), or when it reaches the end of the text on a terminal node (an exact file rule). Cost is $O(L)$ and does not depend on how many rules exist.
- **`remove(key)`:** clears the terminal node, then walks back up and deletes every node left with no children and no rule. The Trie never keeps dead branches after `ignore -r`.

#### Matching Rules and Precedence
- **Directory boundaries:** directory rules are stored with the trailing `/`, so `build/` matches `build/x.o` but not `buildings/a.cpp`.
- **Extension rules:** the rule `*.log` is stored as the reversed key `gol.`, and the path being checked is reversed too. The same prefix walk then finds the extension, at any depth.
- **Both Tries can match:** a path such as `build/app.log` may match a directory rule and an extension rule. `ignore -v` reports the rule with the **lower line number**, so the answer is deterministic.
- **Built-in rule:** `.minigit/` is always ignored, even though it is not written in `.minigitignore`. `ignore -v` reports it as `built-in rule`.
- **Line numbers stay correct:** removing a rule shifts later lines up, so `IgnoreManager` rebuilds both Tries from the file after every removal.

### 3.2 Staging Index (`.minigit/index`)
- **Header:** `include/StagingArea.h`
- **Source:** `src/StagingArea.cpp`
- **Disk Format:** plain text file `.minigit/index` holding one normalized, repository-relative path per line (always `/` separators, so `.\src\a.txt` and `src/a.txt` become the same entry). Files staged for deletion are written as `deleted:<path>`.
- **In-Memory Cache:** two `std::set<std::string>` containers (red-black trees), one for staged files and one for staged deletions. They enforce sorted order and give $O(\log n)$ duplicate prevention and status checks.
- **Staging and unstaging:** `stage()` and `unstage()` change memory only, and `save()` writes the file. Staging a path that is already staged returns `false` instead of adding a duplicate. Staging a file that was marked for deletion cancels the deletion.
- **Safe loading:** `load()` tolerates CRLF line endings and skips blank lines. It also skips any entry that is absolute, contains `..`, or points inside `.minigit/`, so a hand-edited or damaged index cannot reach outside the working tree.
- **Safe saving:** `save()` writes through `writeFileAtomic`, so a failed write leaves the previous index intact.

### 3.3 Directed Acyclic Graph (DAG) for Commits and Branches
- **Header:** `include/CommitGraph.h`
- **Source:** `src/CommitGraph.cpp`
- **Class:** `minigit::CommitGraph`
- **Conceptual Model:** Commits form a Directed Acyclic Graph (DAG) where nodes represent individual commits and directed edges connect each commit to its parent(s). Branches are named references (stored in `.minigit/refs/heads/<branch>`) pointing to tip commit IDs rather than physical repository copies.
- **Data Structures Applied:**
  - `std::unordered_map<std::string, CommitGraphNode>`: $O(1)$ vertex lookup and adjacency storage.
  - Cycle detection using 3-color Depth First Search (DFS): guarantees the graph is strictly acyclic ($O(V + E)$).
  - Topological sorting via Kahn's in-degree reduction algorithm with priority queue: linearizes commits such that children always precede ancestors, ensuring deterministic traversal for ASCII graph visualization.
  - Reachability traversal via Breadth First Search (BFS) with `std::unordered_set<std::string>` visited set: prevents duplicate traversal when branches share common ancestors.

### 3.4 Double-Ended Queue for Undo and Redo
- **Header:** `include/CommitHistory.h`
- **Source:** `src/CommitHistory.cpp`
- **Class:** `minigit::CommitHistory`
- **Structure:** two `std::deque<HistoryEntry>` containers, one for undo states and one for redo states. A `HistoryEntry` holds the commit id, the parent commit id, a snapshot of the staged files, the message and the timestamp.
- **Recording (`recordOperation`):** called by `commit` before `HEAD` moves. It saves the current state in the undo deque and clears the redo deque, because a new commit ends the redo path. When the undo deque grows past the limit (20, `DEFAULT_HISTORY_LIMIT`), `pop_front()` drops the oldest state.
- **Undo:** `pop_back()` takes the newest state from the undo deque, and the current state is pushed onto the redo deque.
- **Redo:** the mirror image: `pop_back()` from the redo deque, and the current state goes back onto the undo deque.
- **Why a deque:** undo and redo work like a stack at the back ($O(1)$ `push_back` and `pop_back`), but the oldest state must also be dropped from the front ($O(1)$ `pop_front`). A `std::stack` cannot drop its bottom element, and erasing the front of a `std::vector` costs $O(n)$.
- **Consistency:** each entry stores the commit id together with the staged files, so `HEAD` and the index always change together when a commit is undone or redone.
- **Persistence:** the deques are saved to `.minigit/history` after every operation (format in section 3.6).

### 3.5 Doubly Linked List for the Commit Log
- **Headers:** `include/CommitNode.h`, `include/CommitLog.h`
- **Source:** `src/CommitLog.cpp`
- **Structure:** a hand-written list in which every `CommitNode` points to the next older commit (`prev`) and the next newer commit (`next`). `head()` is the oldest commit and `tail()` is the newest, which is the commit `HEAD` points to.

```
nullptr <- [c1] <-> [c2] <-> [c3] <-> [c4] -> nullptr
           head (oldest)               tail (newest)
```

| Operation | Cost | Used by |
|---|---|---|
| `insert(node)` (append at the tail) | $O(1)$ | `load()` |
| `find(id)` | $O(n)$ | lookup by exact commit id |
| `next()` / `prev()` (move the cursor one step) | $O(1)$ | `log --reverse` / `log` |
| Walk the whole list | $O(n)$ | `log`, `log --reverse` |
| `load()` | $O(n)$ | every `log` call |

- **Why not a singly linked list:** the first design (Phase 1) used one. With a single direction, one of the two log orders would need an extra reversal or a stack. Two pointers per node make both orders a plain walk.
- **Building the list:** commits are discovered newest-first by following `parent` links, but inserted oldest-first, so the finished list reads from the first commit to the latest.

### 3.6 Commit Object and History File Formats

**Repository layout**
```
.minigit/
├── HEAD                     # "ref: refs/heads/main", or a raw commit id when detached
├── index                    # staged paths, one per line
├── history                  # undo/redo deques (format below)
├── refs/heads/<branch>      # one file per branch: the tip commit id
└── objects/<commit-id>/
    ├── metadata             # commit information (format below)
    └── snapshot/<path>      # the content of every committed file
```

**`objects/<commit-id>/metadata`**
```
commit <40-character SHA-1 id>
parent <parent id, or "none" for the first commit>
timestamp <unix time in seconds>
author <MINIGIT_AUTHOR, USERNAME or USER>
message <one-line message>
files <N>
<path 1>
<path 2>
...
```
- The commit id is the SHA-1 of the parent id, the timestamp, the message, and every file's name, size and content, so the same input always produces the same id.
- A multi-line message is flattened to one line (each run of line breaks becomes one space) because the file is read line by line.
- The `files` line gives the number of path lines that follow, so a file whose name starts with `parent ` cannot be mistaken for a header.
- `log` rebuilds the list by reading `parent` from each metadata file.

**`.minigit/history`**
```
UNDO_COUNT <n>
<commit id | -> <parent id | -> <timestamp | -> <number of staged files>
<message line (may be empty)>
<staged file 1>
...
REDO_COUNT <m>
<same entry layout, m times>
```
- An empty field is written as `-`.
- If the file is truncated or edited by hand, loading discards it and starts with an empty history instead of crashing.

---

## 4. Command Reference & Usage Examples

### 1. Initialize a Repository
```powershell
minigit init
```
**Output:**
```
Initialized empty MiniGit repository in A:\MyProject\.minigit
```

### 2. Track / Stage Files
```powershell
minigit add main.cpp
minigit add .
```
**Output:**
```
Staged 'main.cpp'
staged 3 file(s), 1 already staged, skipped 2 ignored
```

### 3. Verify Staging Status (`IS_STAGED`)
```powershell
minigit add -v main.cpp
minigit add -v untracked.cpp
```
**Output:**
```
'main.cpp' is staged
'untracked.cpp' is not staged
```

### 4. Unstage Files
```powershell
minigit remove main.cpp
minigit remove .
```
**Output:**
```
Unstaged 'main.cpp'
Unstaged 3 files
```

### 5. Ignore Management
```powershell
# Add ignore rules
minigit ignore *.log
minigit ignore build/
minigit ignore secret.env

# Verify ignore rule and line number
minigit ignore -v build/test.o
minigit ignore -v app.log

# Remove ignore rule
minigit ignore -r *.log
minigit ignore -r non_existent.txt
```
**Output:**
```
Ignored '*.log' (line 1)
Ignored 'build/' (line 2)
Ignored 'secret.env' (line 3)

'build/test.o' is ignored by 'build/' (line 2)
'app.log' is ignored by '*.log' (line 1)

Removed '*.log' (was line 1)
'non_existent.txt' is not present in .minigitignore
```

### 6. Help
```powershell
minigit help              # every command
minigit help ignore       # one command
minigit add --help        # same, from the command itself
```
**Output (`minigit help ignore`):**
```
usage: minigit ignore | minigit ignore <pattern> | minigit ignore -r <pattern> | minigit ignore -v <path>

   ignore               List the rules in .minigitignore with line numbers
   ignore <pattern>     Add a rule to .minigitignore
   ignore -r <pattern>  Remove a rule from .minigitignore
   ignore -v <path>     Show whether a path is ignored, and by which line
```

### 7. Commit
```powershell
minigit add notes.txt
minigit commit "Add notes"       # or: minigit commit -m "Add notes"
```
**Output:**
```
[main 1a2b3c4] Add notes
1 file(s) committed
```
The staged files are saved as a snapshot under `.minigit/objects/<commit-id>/`, `HEAD`'s branch is moved to the new commit, and the index is cleared. A message must not be empty, and there must be at least one staged file (otherwise: `fatal: nothing to commit`).

### 8. Undo and Redo
```powershell
minigit undo        # same as: minigit commit undo
minigit redo        # same as: minigit commit redo
```
`undo` moves `HEAD` back one commit and restores the staged files from before that commit; `redo` re-applies the commit that was undone. Up to 20 states are kept, and making a new commit clears the redo history. With nothing to undo or redo the command prints `fatal: nothing to undo` or `fatal: nothing to redo`.

### 9. Log
```powershell
minigit log             # newest commit first
minigit log --reverse   # oldest commit first
```
**Output (one block per commit):**
```
commit 1a2b3c4
parent  9f8e7d6
date    2026-05-02 14:03:11

    Add notes
    (1 file(s))
```
The first commit has no `parent` line. An empty repository prints `No commits yet`.

### 10. Restore a Commit
```powershell
minigit checkout 1a2b3c4      # a full id or a unique prefix of at least 4 characters
```
Replaces the tracked files in the working folder with the snapshot of that commit and detaches `HEAD` at it. Nothing is changed if staged files, edited or deleted tracked files, or untracked files in the way would be lost. Ignored files are never touched. Full rules: section 2C.3 and section 6.2, item 5.

---

## 5. Rollback Architecture & Semantics

### 5.1 Rollback Semantics & Operation Distinction
MiniGit implements a reset-style rollback operation (`minigit rollback <commit-id> [--force]`).

It is crucial to understand the architectural distinction between **Rollback**, **Undo/Redo**, and **Revert**:

| Dimension | `rollback <commit-id>` | `commit undo` / `redo` | `revert` (Git-style) |
|---|---|---|---|
| **Mechanism** | Reset-style pointer movement | Deque history stack manipulation | Inverse patch creation |
| **Branch Target** | Any reachable historical commit ID or prefix | Exactly one step backward/forward | Appends a brand-new commit |
| **Object Preservation** | All commit objects remain stored | Commits remain stored | All commits remain stored |
| **Subsequent Commits** | New commits branch off target commit | Subsequent undo deque is re-cleared | Extends linear branch forward |
| **Working Tree** | Restored to exact snapshot of target commit | Index restored, working tree preserved | Working tree modified with inverse diff |
| **Data Safety** | Rejects uncommitted/staged changes unless `--force` | Stack-based state restoration | Conflict detection |

### 5.2 Atomic Snapshot Restoration
When rolling back active branch from commit $D$ to historical commit $B$:
1. **Safety Verification:** MiniGit inspects the staging index (`.minigit/index`) and compares working tree tracked files against $D$'s snapshot. If uncommitted modifications or staged changes are detected without `--force`, the operation aborts immediately.
2. **Post-Target File Cleanup:** All tracked files introduced after target commit $B$ (e.g. introduced in $C$ or $D$) are removed from the working tree.
3. **Exact Snapshot Copy:** Every tracked file present at commit $B$ is restored from `.minigit/objects/<commit-B>/snapshot/` into the working tree.
4. **Reference Update:** The active branch reference file (`.minigit/refs/heads/<active_branch>`) is atomically updated to point to commit $B$'s ID. `HEAD` remains pointed to the active branch.
5. **Continuous History:** Any subsequent `minigit commit` uses commit $B$'s ID as its parent, establishing a new diverging line of history.

---

## 6. DAG-Based Branch Management

### 6.1 Conceptual Model & Reference Architecture
In MiniGit's DAG branching architecture:
- Commits are immutable nodes with unique 40-character SHA-1 identifiers.
- Directed edges point from each child commit to its parent commit(s).
- Branches are lightweight named references stored under `.minigit/refs/heads/<branch-name>`.
- `.minigit/HEAD` stores a symbolic reference (e.g. `ref: refs/heads/main`) identifying the current active branch.
- Branches share common ancestors without duplicating commit snapshots or metadata.

```
       [c1] Initial commit (Common Ancestor)
       /  \
     /      \
   [c2]    [c3] Divergent feature work
    |       |
  (main)  (feature)
```

### 6.2 Supported Branch Operations
1. **List Branches:** `minigit branch`
   - Scans `.minigit/refs/heads/`.
   - Distinguishes the active branch with an asterisk (`* main`) and prints the short 7-character commit ID.
2. **Create Branch:** `minigit branch <branch-name>`
   - Validates branch name against POSIX/Git rules (no control chars, spaces, `..`, prefixes `-`).
   - Writes the current HEAD commit ID to `.minigit/refs/heads/<branch-name>` without duplicating commit objects.
3. **Switch Branch:** `minigit checkout <branch-name> [--force]`
   - Protects local uncommitted modifications and staged changes from being overwritten.
   - Reconciles working-tree files: removes files not tracked in the destination branch, and restores destination branch files from snapshot.
   - Updates `.minigit/HEAD` to point to `refs/heads/<branch-name>`.
4. **Delete Branch:** `minigit branch -d <branch-name>`
   - Validates existence and protects against deleting the currently active branch.
5. **Restore a Commit (Snehil):** `minigit checkout <commit-id>`
   - Used when the argument is not an existing branch and is 4-40 hex digits (a full id or a unique prefix).
   - Refuses first, changing nothing, if anything would be lost: staged files, edited or deleted tracked files, untracked files in the way.
   - Writes every target file to a temporary copy, then swaps each in with a backup; if any step fails, all of them are undone.
   - Never touches ignored files. Leaves `.minigit/HEAD` holding the commit id (detached), so no branch moves.
   - While detached, `commit`, `undo`, `redo` and `rollback` refuse (they move a branch); `branch <name>` + `checkout <name>` continue from there.
6. **DAG ASCII Visualization:** `minigit log --all --graph`
   - Builds `CommitGraph` across all branches, performs cycle verification, sorts nodes topologically, and renders a visual ASCII DAG indicating branch tips and merge/fork points.

---

## 7. Compilation, Portability & Testing

### 100% Standalone Executable
`minigit.exe` is compiled with `-static` (`-static-libgcc -static-libstdc++`):
- Depends only on `KERNEL32.dll` and `msvcrt.dll` (standard Windows OS libraries).
- Requires no GCC runtime, no MinGW installation, and no extra DLLs.

### Automated Unit Test Suites
Run all automated test suites:
```bash
make test
```
- `test_trie`: Verifies Trie prefix search, reversed extension matching, and node pruning.
- `test_add`: Verifies staging, duplicate prevention, and `.minigitignore` enforcement.
- `test_init`: Verifies repository initialization, directory structure, and `HEAD` metadata.
- `test_cli`: Verifies argument parsing, dispatcher calls, generated help, typo suggestions, rejected extra arguments, and `IS_STAGED`.
- `test_commit`: Verifies commits, snapshots, undo/redo (including the undo → redo → undo round trip) and multi-line messages.
- `test_commitlog`: Verifies the doubly linked list on its own.
- `test_log`: Verifies `log` / `log --reverse` over real commits and through the CLI.
- `test_rollback`: Verifies reset-style rollback, exact snapshot restoration, post-target file deletion, object preservation, parent pointer continuity, staged and dirty-tree safety checks.
- `test_branch`: Verifies branch listing, creation, switching, deletion, active-branch protection, and snapshot reconciliation across branches.
- `test_checkout`: Verifies `checkout <commit-id>`: exact restore, unsaved-work refusal, ignored files untouched, deletions honoured, detached-HEAD rules, and damaged or unsafe commits rejected without changes.
- `test_dag`: Verifies 5-node diverging graph (A -> B/C -> D/E), shared ancestor verification, cycle detection, topological ordering, duplicate-free traversal, and ASCII graph rendering.
