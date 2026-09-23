# MiniGit CLI — Technical & User Documentation

**Project Title:** MiniGit: A Lightweight Version Control System Using C++ and Data Structures  
**Course:** Project-Based Learning in Data Structures & C++ (PBL in DS CPP)  
**Team Name:** Blaze (DSCPP-III-2026-T296)  
**Lead & CLI Architect:** Abhinesh Gangwar (2510370192)  
**Staging & Trie Lead:** Sparsh Jain (2510370159)  

---

## Table of Contents
1. [Introduction & Scope](#1-introduction--scope)
2. [Responsibilities & Implementation](#2-responsibilities--implementation)
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
- Querying staging and ignore status in $O(1)$ and $O(L)$ time.

---

## 2. Responsibilities & Implementation

Abhinesh is responsible for **Repository Initialization**, the **CLI Interface**, the **Command Dispatcher**, **Ignore Validation**, and **Result Status Mapping**.

### 2.1 Repository Setup (`minigit init`)
- **Header:** `include/InitCommand.h`
- **Source:** `src/InitCommand.cpp`
- **Behavior:**
  1. Creates the `.minigit` root directory.
  2. Creates `.minigit/objects/` (where commit trees and blob snapshots reside).
  3. Initializes `.minigit/HEAD` pointing to default branch (`ref: refs/heads/main\n`).
  4. Initializes `.minigit/index` (empty file tracking staged paths).
  5. Initializes empty `.minigitignore` at the repository root if not present.
  6. Idempotent: If `.minigit` already exists, reports `Reinitialized existing MiniGit repository` without corrupting user data.

### 2.2 Command Dispatcher Architecture
- **Header:** `include/CLI.h`
- **Source:** `src/CLI.cpp`
- **Function:** `minigit::CLI::dispatch(int argc, char** argv, const std::string& root)`

The dispatcher acts as the central router between terminal inputs and core engine modules:

```
[Terminal Input: argv]
         │
         ▼
 ┌─────────────────┐
 │ CLI::dispatch() │
 └────────┬────────┘
          ├──────────────► initRepo()       ──► InitResult
          ├──────────────► addFile()         ──► AddResult
          ├──────────────► addAll()          ──► AddResult
          ├──────────────► addVerify()       ──► AddResult (IS_STAGED)
          ├──────────────► removeFile()      ──► AddResult
          ├──────────────► ignoreAdd()       ──► IgnoreResult
          ├──────────────► ignoreRemove()    ──► IgnoreResult
          ├──────────────► ignoreVerify()    ──► IgnoreResult
          └──────────────► install()         ──► Auto/Manual Setup
```

### 2.3 Status & Error Mapping (`IS_STAGED`)
The CLI maintains pure separation between business logic and console presentation:
- Core functions return structured result objects (`AddResult`, `IgnoreResult`, `InitResult`).
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
- Rejects empty, whitespace-only, or illegal traversal patterns (`../`).
- Dispatches:
  - `minigit ignore <pattern>`: Validates input, appends to `.minigitignore`, and updates the Trie.
  - `minigit ignore -r <pattern>`: Removes rule from file and Trie; outputs `"'<pattern>' is not present in .minigitignore"` if not ignored.
  - `minigit ignore -v <path>`: Returns whether the path is ignored and prints the **exact line number** in `.minigitignore`.

### 2.5 Zero-Config First-Run Self-Installation
To solve PowerShell's restriction requiring `.\` for current-directory binaries:
- On first execution, `minigit.exe` automatically copies itself to `%LOCALAPPDATA%\Microsoft\WindowsApps\minigit.exe` (or `~/.local/bin/minigit` on Unix).
- Because `WindowsApps` is in every Windows 10/11 user's `PATH` by default, `minigit` becomes runnable globally from any folder or terminal window without administrator privileges.

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
- **In-Memory Cache:** `std::set<std::string>` enforces sorted order and provides $O(1)$ duplicate staging prevention.

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
- `test_cli`: Verifies argument parsing, dispatcher calls, and the `IS_STAGED` print case.
