# MiniGit: A Lightweight Version Control System Using C++ and Data Structures

[![Language](https://img.shields.io/badge/Language-C%2B%2B14%2FC%2B%2B17-blue.svg)]()
[![Build](https://img.shields.io/badge/Build-Passing-brightgreen.svg)]()
[![Course](https://img.shields.io/badge/PBL-DS%20CPP-emerald.svg)]()

**Team Blaze** · DSCPP-III-2026-T296 · CSE (AI&ML)  
Department of Computer Science & Engineering — Graphic Era (Deemed to be University)

---

## 📌 Overview

**MiniGit** is a lightweight, local, terminal-based version control system implemented in C++. Designed as an educational version control system, MiniGit demystifies core VCS concepts—repository initialization, staging areas, ignore rule management, and commit workflows—while directly applying foundational computer science data structures such as **Tries (Prefix Trees)**, **Sets**, **Linked Lists**, and **Stacks**.

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
- **`minigit init`**: Creates `.minigit/` directory structure (`objects/`, `HEAD` initialized to `ref: refs/heads/main`, and `index`). Handles reinitialization safely.
- **Command Dispatcher**: Parses CLI tokens and dispatches execution directly to underlying module functions:
  - `initRepo()`
  - `addFile()`
  - `addAll()`
  - `addVerify()`
  - `removeFile()`
  - `ignoreAdd()`
  - `ignoreRemove()`
  - `ignoreVerify()`
- **Status & Error Mapper**: Standardizes execution codes into user-facing output messages, including the print case for the new `IS_STAGED` status.
- **Ignore CLI Interface**: Input validation and command syntax parsing for `ignore`, `ignore -r`, and `ignore -v`.
- **First-Run Self-Installation**: Automatically registers the executable into user `PATH` on first run so users can type `minigit` directly from any terminal without `./` or `.\`.

### Sparsh — Trie & Staging Area Module
- **Trie (Prefix Tree) Engine**: Dual-Trie architecture for fast $O(L)$ pattern matching without disk scans.
- **Staging Area (`.minigit/index`)**: Holds one normalized relative path per line with duplicate staging prevention.
- **Add Operations**: Supports single file tracking (`add <file>`), batch repository staging (`add .`), and unstaging (`remove <file>`).

---

## 🌳 Data Structures Applied

### 1. Dual Trie (Prefix Tree) — `.minigitignore`
Used for instant, deterministic pattern matching to block ignored files from staging:
- **Forward Path Trie**: Stores exact file paths (`secret.txt`) and directory rules (`build/`). Traverses path tokens in $O(L)$ time.
- **Reversed Extension Trie**: Stores wildcard extension patterns (e.g. `*.log` is stored as reversed key `gol.`). When evaluating a filename, its reversed extension is queried in $O(E)$ time.
- **Line Number Indexing**: Every terminal Trie node stores the 1-based line number of the matching rule from `.minigitignore`, allowing instant rule tracking during verification.

### 2. Staging Index (Hash Set & Flat File)
- Maintains staged file paths in `.minigit/index` (one path per line).
- Guarantees $O(1)$ duplicate staging prevention and fast status checks.

---

## 💻 Supported Commands & Usage

| Command | Action / Dispatcher Function | Description |
|---|---|---|
| `minigit init` | `initRepo()` | Initializes a local repository structure (`.minigit/`, `HEAD`, `index`) |
| `minigit add <file>` | `addFile(path)` | Stages `<file>` if not blocked by `.minigitignore` |
| `minigit add .` | `addAll()` | Recursively stages all tracked, unignored files in the repository |
| `minigit add -v <file>` | `addVerify(path)` | Verifies if file is staged (prints `'<file>' is staged` via `IS_STAGED`) |
| `minigit remove <file>` | `removeFile(path)` | Unstages file from `.minigit/index` without modifying working tree |
| `minigit remove .` | `removeFile(".")` | Clears all staged files from the staging area |
| `minigit ignore <pattern>` | `ignoreAdd(pattern)` | Validates and appends `<pattern>` to `.minigitignore` and updates the Trie |
| `minigit ignore -r <pattern>` | `ignoreRemove(pattern)` | Removes `<pattern>` from `.minigitignore` (or reports `"not present"`) |
| `minigit ignore -v <path>` | `ignoreVerify(path)` | Checks if `<path>` is ignored; returns exact rule and line number |
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
- `test_cli`: Full CLI dispatching, argument validation, and the `IS_STAGED` print case.

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
├── Makefile                 # Top-level build orchestration
├── README.md                # Project documentation
├── Reports/                 # Phase evaluation documentation
│   ├── ppt.pdf              # Phase-I presentation slides
│   └── report.pdf           # Phase-I project proposal & architecture report
└── module/                  # Core source code & unit tests
    ├── Makefile             # Module compilation and test runners
    ├── include/             # Header declarations
    │   ├── AddCommand.h     # add / add . / add -v / remove interface
    │   ├── CLI.h            # CLI parser, dispatcher, and status mapper
    │   ├── IgnoreManager.h  # .minigitignore parser & Trie bridge
    │   ├── InitCommand.h    # initRepo interface
    │   ├── PathUtils.h      # Path normalization & safety utilities
    │   ├── StagingArea.h    # .minigit/index persistence manager
    │   ├── Trie.h           # Pure Prefix Tree data structure
    │   ├── filesystem       # Cross-compiler filesystem wrapper
    │   └── ghc/             # Header-only standard filesystem implementation
    ├── src/                 # Implementation files
    │   ├── AddCommand.cpp
    │   ├── CLI.cpp
    │   ├── IgnoreManager.cpp
    │   ├── InitCommand.cpp
    │   ├── StagingArea.cpp
    │   ├── Trie.cpp
    │   ├── main.cpp         # Production CLI entry point
    │   └── main_demo.cpp    # Standalone demo interface
    └── tests/               # Automated unit tests
        ├── test_add.cpp
        ├── test_cli.cpp
        ├── test_init.cpp
        └── test_trie.cpp
```
