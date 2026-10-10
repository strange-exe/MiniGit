# MiniGit: A Lightweight Version Control System Using C++ and Data Structures

[![Language](https://img.shields.io/badge/Language-C%2B%2B14%2FC%2B%2B17-blue.svg)]()
[![Build](https://img.shields.io/badge/Build-Passing-brightgreen.svg)]()
[![Course](https://img.shields.io/badge/PBL-DS%20CPP-emerald.svg)]()

**Team Blaze** · DSCPP-III-2026-T296 · CSE (AI&ML)  
Department of Computer Science & Engineering — Graphic Era (Deemed to be University)

---

## 📌 Overview

**MiniGit** is a lightweight, local, terminal-based version control system implemented in C++. Designed as an educational version control system, MiniGit demystifies core VCS concepts—repository initialization, staging areas, ignore rule management, and commit workflows—while directly applying foundational computer science data structures such as **Tries (Prefix Trees)**, an **ordered set**, a **double-ended queue** (undo/redo), and a **doubly linked list** (commit log).

---

## 🌐 Project Website (this branch)

This branch holds the project website: `index.html`, `styles.css` and `script.js`, with no build step. The C++ source, Makefiles and tests described below live on the `main` branch.

- **Sandbox:** a browser port of the C++ modules. It runs the real commands on a small sample project and shows each data structure live: the commit list, the index set, both ignore tries, the undo/redo deques and the `.minigit/` folder. Its output matches the real `minigit.exe` message for message.
- **Downloads:** `minigit.exe`, `DOCUMENTATION.md` and the `Reports/` PDFs are served from this branch.

Preview it locally from the repository root:
```bash
python -m http.server 8000
```
then open `http://localhost:8000`.

---

## 🏗️ System Architecture

MiniGit follows a modular 5-layer architecture:

```
┌─────────────────────────────────────────────────────────┐
│ 1. CLI Interface & Argument Parser                      │
│    (Tokenizes input, validates flags: -r, -v, ., etc.)  │
├─────────────────────────────────────────────────────────┤
│ 2. Command Handler & Dispatcher (Abhinesh's Module)     │
│    (Maps commands -> operations; handles status codes)  │
├─────────────────────────────────────────────────────────┤
│ 3. Repository & Staging Manager                         │
│    (.minigit/ structure, index persistence, undo/redo)  │
├─────────────────────────────────────────────────────────┤
│ 4. Ignore Engine (Trie Data Structure)                  │
│    (Dual Prefix Trees: Path Trie & Reversed Ext Trie)   │
├─────────────────────────────────────────────────────────┤
│ 5. Storage Layer (File I/O)                             │
│    (.minigit/objects, .minigit/index, .minigit/HEAD)    │
└─────────────────────────────────────────────────────────┘
```

---

## 👥 Module Breakdown

### Abhinesh (Team Lead) — CLI, Dispatcher & Repository Setup
- **`minigit init`**: Creates `.minigit/` (`objects/`, `refs/heads/`, `HEAD` = `ref: refs/heads/main`, `index`) and `.minigitignore`. Re-running it never overwrites existing data, and every failed write is reported.
- **Table-Driven Command Dispatcher**: One table row per command holds its name, aliases, usage forms and handler. `minigit help`, `minigit help <command>`, `minigit <command> --help` and the usage printed on wrong arguments are all generated from it, so they cannot go out of date.
- **Typo Suggestions**: An unknown command is matched to the closest real one by Levenshtein edit distance (`minigit comit` → *The most similar command is commit*).
- **Status & Error Mapper**: Turns result structs (`InitResult`, `AddResult`, `IgnoreResult`, `CommitResult`, `LogResult`) into messages and exit codes (0 = success, 1 = error).
- **Ignore CLI Interface**: Validates `ignore`, `ignore -r` and `ignore -v` input; rejects empty patterns and anything outside the repository (`..`, absolute paths, drive letters).
- **First-Run Self-Installation**: Copies the executable into the user's `PATH` on first run, and refreshes that copy when a newer build is run.

### Sparsh — Trie & Staging Area Module
- **Trie (Prefix Tree) Engine**: Dual-Trie architecture for fast $O(L)$ pattern matching without disk scans.
- **Staging Area (`.minigit/index`)**: Holds one normalized relative path per line with duplicate staging prevention.
- **Add Operations**: Supports single file tracking (`add <file>`), batch repository staging (`add .`), and unstaging (`remove <file|dir|.>`).

### Krish — Commit & Undo/Redo Module
- **`minigit commit`**: Saves each staged file's content under `.minigit/objects/<id>/snapshot/`, writes `metadata` (parent, timestamp, author, message, files) and moves `HEAD`. The commit id is a SHA-1 of that content.
- **Undo / Redo (`std::deque`)**: Two deques of repository states. A new commit clears the redo deque; the undo deque is capped at 20 entries with `pop_front()`. Persisted in `.minigit/history`.

### Snehil — Commit Log Module
- **`minigit log`**: Rebuilds the history from `HEAD` by following parent links into a **doubly linked list**, then walks it backward (newest first) or forward with `--reverse`.

---

## 🌳 Data Structures Applied

### 1. Dual Trie (Prefix Tree) — `.minigitignore`
Used for instant, deterministic pattern matching to block ignored files from staging:
- **Forward Path Trie**: Stores exact file paths (`secret.txt`) and directory rules (`build/`). Traverses path tokens in $O(L)$ time.
- **Reversed Extension Trie**: Stores wildcard extension patterns (e.g. `*.log` is stored as reversed key `gol.`). When evaluating a filename, its reversed extension is queried in $O(E)$ time.
- **Line Number Indexing**: Every terminal Trie node stores the 1-based line number of the matching rule from `.minigitignore`, allowing instant rule tracking during verification.

### 2. Staging Index (Ordered Set & Flat File)
- Maintains staged file paths in `.minigit/index` (one path per line).
- Held in memory as `std::set` (a red-black tree): $O(\log n)$ duplicate staging prevention and status checks, and entries stay sorted so the index file has a stable order.

### 3. Double-Ended Queue — undo / redo
- `std::deque` gives $O(1)$ `push_back`/`pop_back` for undo and redo, and $O(1)$ `pop_front` to drop the oldest state once the 20-entry history limit is reached.

### 4. Doubly Linked List — commit log
- Each `CommitNode` links to the older and newer commit, so `log` and `log --reverse` are both a single $O(n)$ walk from one end of the list.

---

## 💻 Supported Commands & Usage

| Command | Action / Dispatcher Function | Description |
|---|---|---|
| `minigit init` | `initRepo()` | Initializes a local repository structure (`.minigit/`, `HEAD`, `refs/heads/`, `index`) |
| `minigit add <file\|dir>` | `addFile(path)` | Stages a file, or every non-ignored file in a directory |
| `minigit add .` | `addAll()` | Recursively stages all unignored files in the repository |
| `minigit add -v <file>` | `addVerify(path)` | Verifies if file is staged (prints `'<file>' is staged` via `IS_STAGED`) |
| `minigit remove <file\|dir>` | `removeFile(path)` | Unstages a file, or every staged file in a directory, without touching the working tree |
| `minigit remove .` | `removeFile(".")` | Clears all staged files from the staging area |
| `minigit ignore` | `IgnoreManager::lines()` | Lists the rules in `.minigitignore` with line numbers |
| `minigit ignore <pattern>` | `ignoreAdd(pattern)` | Validates and appends `<pattern>` to `.minigitignore` and updates the Trie |
| `minigit ignore -r <pattern>` | `ignoreRemove(pattern)` | Removes `<pattern>` from `.minigitignore` (or reports `"not present"`) |
| `minigit ignore -v <path>` | `ignoreVerify(path)` | Checks if `<path>` is ignored; returns exact rule and line number |
| `minigit commit "<message>"` / `commit -m "<message>"` | `commitRepo(message)` | Records the staged files as a new commit and clears the index |
| `minigit commit undo` / `undo` | `undoCommit()` | Moves `HEAD` back one commit and restores its staged files |
| `minigit commit redo` / `redo` | `redoCommit()` | Re-applies the last undone commit |
| `minigit log [--reverse]` | `showLog(reverse)` | Shows the commit history, newest first (oldest first with `--reverse`) |
| `minigit help [<command>]` | command table | Lists every command, or the usage of one (`minigit <command> --help` also works) |
| `minigit install` | `CLI::install()` | Manually copies `minigit` into the user's `PATH` |
| `minigit uninstall` | `CLI::uninstall()` | Removes `minigit` from user `PATH` |

---

## 🛠️ Build & Test Instructions

### Prerequisites
- **Compiler:** `g++` (GCC 6.3+ or modern Clang/MSVC, supports C++14/C++17)
- **Build Tool:** GNU `make` or `mingw32-make`

### Building the Project
From the repository root:
```bash
# Compile standalone executable minigit
make all
```

### Running the Test Suites
The project includes automated test suites covering all modules:
```bash
# Run all unit tests
make test
```
Test coverage includes:
- `test_trie`: Forward/reversed Trie pattern matching, wildcard extensions, and node pruning.
- `test_add`: Staging, duplicate prevention, ignore enforcement, and persistence.
- `test_init`: Repository creation, metadata structure, and reinitialization handling.
- `test_cli`: Full CLI dispatching, argument validation, generated help, typo suggestions, and the `IS_STAGED` print case.
- `test_commit`: Commit ids and metadata, snapshots, undo/redo and the history limit, multi-line messages.
- `test_commitlog`: The doubly linked list on its own (insert, find, cursor movement).
- `test_log`: `log` / `log --reverse` over real commits, after undo, and through the CLI.

### Cleaning Build Files
```bash
make clean
```

---

## ⚡ Global Execution (First-Run Auto-Install)

By default, PowerShell and Unix terminals restrict running local executables without prefixing `.\` or `./`. 

MiniGit includes **built-in zero-config self-installation**:
- The first time `.\minigit` is run on a Windows 10/11 system, it automatically copies itself into `%LOCALAPPDATA%\Microsoft\WindowsApps\minigit.exe`.
- Because this directory is already in the user's `PATH` by default and requires **no administrator privileges**, you can immediately run:
  ```powershell
  minigit init
  minigit add .
  minigit ignore -v secret.txt
  ```
  from **any folder or terminal window** on your machine.

---

## 📂 Repository Structure

```
MiniGit-CLI/
├── .gitignore               # Ignores build artifacts, temporary test files, and local repo state
├── LICENSE                  # MIT License
├── Makefile                 # Top-level build orchestration
├── README.md                # Project documentation
├── Reports/                 # Phase evaluation documentation
│   ├── phase2.pdf           # Phase-II project progress report
│   ├── ppt.pdf              # Phase-I presentation slides
│   └── report.pdf           # Phase-I project proposal & architecture report
└── module/                  # Core source code & unit tests
    ├── Makefile             # Module compilation and test runners
    ├── include/             # Header declarations
    │   ├── AddCommand.h     # add / add . / add -v / remove interface
    │   ├── CLI.h            # CLI parser, dispatcher, and status mapper
    │   ├── CommitCommand.h  # commit / undo / redo interface
    │   ├── CommitHistory.h  # Undo/redo deques (.minigit/history)
    │   ├── CommitLog.h      # Doubly linked list over commits
    │   ├── CommitNode.h     # One commit (list node)
    │   ├── IgnoreManager.h  # .minigitignore parser & Trie bridge
    │   ├── InitCommand.h    # initRepo interface
    │   ├── LogCommand.h     # log / log --reverse interface
    │   ├── PathUtils.h      # Path normalization & safety utilities
    │   ├── StagingArea.h    # .minigit/index persistence manager
    │   ├── Trie.h           # Pure Prefix Tree data structure
    │   ├── filesystem       # Cross-compiler filesystem wrapper
    │   └── ghc/             # Header-only standard filesystem implementation
    ├── src/                 # Implementation files
    │   ├── AddCommand.cpp
    │   ├── CLI.cpp          # Command table, dispatcher and output
    │   ├── CommitCommand.cpp
    │   ├── CommitHistory.cpp
    │   ├── CommitLog.cpp
    │   ├── IgnoreManager.cpp
    │   ├── InitCommand.cpp
    │   ├── LogCommand.cpp
    │   ├── StagingArea.cpp
    │   ├── Trie.cpp
    │   ├── main.cpp         # Production CLI entry point
    │   ├── main_demo.cpp    # Standalone demo interface (add/ignore)
    │   └── main_demo_log.cpp  # Standalone demo interface (log)
    └── tests/               # Automated unit tests
        ├── test_add.cpp
        ├── test_cli.cpp
        ├── test_commit.cpp
        ├── test_commitlog.cpp
        ├── test_init.cpp
        ├── test_log.cpp
        └── test_trie.cpp
```

---

## 📄 License

MiniGit is released under the [MIT License](LICENSE).
