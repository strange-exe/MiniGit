# MiniGit
### A Lightweight, Zero-Dependency Version Control System Using C++ and Core Data Structures
#### Project-Based Learning (PBL) in Data Structures & C++ (DS CPP) · v1.0.0 Release

[![Language](https://img.shields.io/badge/language-C%2B%2B14-blue.svg)]()
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-green.svg)]()
[![Course](https://img.shields.io/badge/PBL-DS%20CPP-emerald.svg)]()
[![Website](https://img.shields.io/badge/website-git--scm%20style-orange.svg)](index.html)

**Team Blaze** · DSCPP-III-2026-T296 · CSE (AI&ML) · Graphic Era University

---

## Overview

Students working on programming projects often save multiple copies of the same folder with names like `final`, `final_new`, or `final_latest` to keep track of changes. This quickly becomes confusing — it's hard to tell which version is correct, compare changes, or recover work after a mistake.

Professional version-control tools like Git solve this problem, but their vast command sets and internals can be overwhelming for students trying to understand the core mechanics.

**MiniGit** is a fast, local command-line version-control system engineered to demonstrate the fundamental principles of version control:
- **Dual Trie prefix trees** for $O(L)$ `.minigitignore` pattern filtering
- **Hash Table staging index** (`.minigit/index`) for file status tracking and staged verification
- **Singly Linked List** for linear, immutable commit histories
- **LIFO Stack** for non-destructive checkout state rollbacks
- **100% Standalone C++ binary** (`minigit.exe`) with zero runtime dependencies and automatic PATH installation

MiniGit includes an interactive **Git-SCM-inspired Web Visualizer & Terminal Sandbox** (`index.html`) allowing users to visualize data structures updating live in the browser.

---

## Key Features

| Command | Category | Description | Data Structure |
|---|---|---|---|
| `minigit init` | Setup | Initializes `.minigit/`, `objects/`, `HEAD`, `index`, and default `.minigitignore` | Filesystem Layout |
| `minigit add <file>` | Staging | Stages files into index; checks against `.minigitignore` | Hash Table / Index |
| `minigit add .` | Staging | Recursively stages all tracked files in working tree | Directory Traversal |
| `minigit add -v <file>` | Verification | Verifies file staged status (`'<file>' is staged`) | Hash Table Lookup |
| `minigit remove <file>` | Staging | Unstages tracked files from index without deleting disk files | Hash Table Removal |
| `minigit ignore <pattern>` | Ignore Engine | Adds pattern to `.minigitignore` and Dual Trie index | Dual Prefix Trie |
| `minigit ignore -r <pattern>` | Ignore Engine | Removes pattern from `.minigitignore` and rebalances Trie | Prefix Tree Deletion |
| `minigit ignore -v <path>` | Ignore Engine | Diagnostic test checking which pattern/line ignores the path | $O(L)$ Trie Search |
| `minigit commit -m "..."` | Commit | Seals staged files into immutable commit snapshot node | Singly Linked List |
| `minigit log` | Inspection | Traverses commits from `HEAD` back to root | Linked List Traversal |
| `minigit checkout <hash>` | Restore | Restores workspace files to snapshot; pushes state to rollback stack | LIFO Stack Push |
| `minigit rollback` | Restore | Undoes checkout and pops top state from rollback stack | LIFO Stack Pop |
| `minigit install` | System | Auto-installs binary to user PATH (`%LOCALAPPDATA%\Microsoft\WindowsApps`) | Windows AppPath |

---

## Dual Trie `.minigitignore` Architecture

Instead of iterating through raw strings on every file check, MiniGit utilizes a **Dual Trie**:
1. **Directory Prefix Trie**: Handles path prefix patterns like `build/` or `logs/`. Traversal completes in $O(L)$ where $L$ is the directory path length.
2. **Reversed Extension Trie**: Handles extension wildcard patterns like `*.log` or `*.tmp`. Strings are reversed so file extensions are evaluated from right to left in $O(K)$ time.
3. **Exact Path Matching**: Handles precise file ignores like `secret.env`.

Each terminal node stores the pattern and its 1-indexed line number in `.minigitignore` for instant diagnostic tracking.

---

## 5-Layer Modular Architecture

MiniGit decouples system components cleanly:

```
1. CLI Interface (CLI.cpp)           → Parses terminal arguments, flags, handles autoInstall
2. Ignore Module (Trie.cpp)          → Dual prefix tree pattern matching ($O(L)$)
3. Repository Manager (Repository.h)  → Coordinates staging index, blobs, and commit graph
4. Storage Layer (InitCommand.cpp)   → Manages .minigit/ directory, objects/, and metadata
5. Data Structures Layer             → Pure C++ Singly Linked List, Hash Table, and LIFO Stack
```

---

## Getting Started

### 1. Download Standalone Executable (Windows)
Download precompiled `minigit.exe` (2.7 MB). It has **zero dependencies** (requires only standard Windows core DLLs `KERNEL32.dll` and `msvcrt.dll`).

### 2. Auto-Installation
On first run, typing `./minigit` or double-clicking `minigit.exe` automatically registers itself into your user PATH (`%LOCALAPPDATA%\Microsoft\WindowsApps\minigit.exe`).
After that, you can run `minigit` globally from any PowerShell or CMD terminal without typing `.\`:

```powershell
minigit init
minigit ignore *.log build/
minigit add .
minigit add -v main.cpp
minigit commit -m "feat: initial commit"
minigit log
```

### 3. Interactive Web Sandbox
Open `index.html` in any web browser to explore:
- Interactive Terminal Simulator with command history
- Live Singly Linked List commit graph
- Hash Table staging area visualizer
- Dual Trie prefix tree node graph
- LIFO Rollback stack inspection
- Embedded PDF viewer for project reports and presentation decks

---

## Team Blaze Contributors (Phase 1 Delivered · Phase 2 in Motion)

| Contributor | Command(s) | Module / Focus | Key Implementation Tasks | Integration & Deliverables |
|---|---|---|---|---|
| **Abhinesh Gangwar** *(Team Lead & Systems Architect)* | `init` + `ignore` | CLI / basic repository setup · **Cross-Module Supervision** | Implement `minigit init`; create `.minigit` directory structure; initialize `HEAD` and `index`; implement `minigit ignore <pattern>` command interface; validate ignore input; connect command to ignore module. **Supervised & guided all peer module implementations** (Trie, Deque, Doubly Linked List). | **Cross-Module Supervision**, basic CLI integration, command error messages, final module integration, and Phase 2 roadmap direction |
| **Sparsh Jain** | `add` | Trie — `.minigitignore` | Implement Trie; `insert()`, `search()`, `remove()`, `isIgnored()`; support file-path and extension matching; implement `minigit add <file>` and `minigit add .`; skip ignored files; prevent duplicate staging | Staging/index handling and tests for `add` + `ignore` |
| **Krish Bajaj** | `commit` | Deque — Undo/Redo | Implement `minigit commit "<message>"`; generate commit ID; create commit metadata; implement commit insertion; implement Deque for operation history; `undo`, `redo`, history-limit handling | Commit-related tests; connect commit state with storage |
| **Snehil Joshi** | `log` | Doubly Linked List — Commit Navigation | Implement Doubly Linked List; `insert`, `find`, `next`, `prev`, traversal; implement `minigit log`; display commit ID, message and timestamp; support forward/backward history traversal | History loading, log tests, persistence integration |

**Course:** PBL in DS CPP · DSCPP-III-2026-T296 · CSE (AI&ML)  
**Institution:** Graphic Era University, Dehradun  

---

## References

1. [Git SCM Official Website](https://git-scm.com)
2. Scott Chacon & Ben Straub, [*Pro Git (2nd Edition)*](https://git-scm.com/book/en/v2)
3. Pat Morin, [*Open Data Structures (in C++)*](https://opendatastructures.org/ods-cpp/)
4. [NPTEL, IIT Delhi — Data Structures and Algorithms](https://nptel.ac.in/courses/106102064)