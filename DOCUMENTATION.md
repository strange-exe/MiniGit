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
3. [Core Data Structures](#3-core-data-structures)
   - [3.1 Trie (Prefix Tree) for Ignored Files](#31-trie-prefix-tree-for-ignored-files)
   - [3.2 Staging Index (`.minigit/index`)](#32-staging-index-minigitindex)
4. [Command Reference & Usage Examples](#4-command-reference--usage-examples)
5. [Compilation, Portability & Testing](#5-compilation-portability--testing)

---

## 1. Introduction & Scope

MiniGit is a local, lightweight version-control tool developed in C++ to demonstrate the real-world utility of fundamental data structures. Professional VCS platforms (e.g., Git) introduce complex workflows (remotes, rebasing, merge conflict resolution) that often obscure core mechanics for students. MiniGit models the essential version-control lifecycle:
- Initializing local repository metadata.
- Tracking and staging files using an index.
- Blocking untracked files through prefix-tree pattern matching (`.minigitignore`).
- Querying staging status in $O(\log n)$ (ordered set) and ignore status in $O(L)$ (Trie) time.
- Recording commits, undoing/redoing them (double-ended queue) and listing history (doubly linked list).

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

### 3.2 Staging Index (`.minigit/index`)
- **Header:** `include/StagingArea.h`
- **Source:** `src/StagingArea.cpp`
- **Disk Format:** Plain text file `.minigit/index` holding one path per line.
- **In-Memory Cache:** `std::set<std::string>` (red-black tree) enforces sorted order and provides $O(\log n)$ duplicate staging prevention.

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

---

## 5. Compilation, Portability & Testing

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
- `test_cli`: Verifies argument parsing, dispatcher calls, generated help, typo suggestions, rejected extra arguments, and the `IS_STAGED` print case.
- `test_commit`: Verifies commits, snapshots, undo/redo (including the undo → redo → undo round trip) and multi-line messages.
- `test_commitlog`: Verifies the doubly linked list on its own.
- `test_log`: Verifies `log` / `log --reverse` over real commits and through the CLI.
