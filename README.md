# MiniGit
### A Lightweight Version Control System Using C++ and Data Structures
#### Project-Based Learning (PBL) in Data Structures & C++ (DS CPP)

[![Language](https://img.shields.io/badge/language-C%2B%2B-blue.svg)]()
[![Course](https://img.shields.io/badge/PBL-DS%20CPP-emerald.svg)]()

**Team Blaze** · DSCPP-III-2026-T296 · CSE (AI&ML)

---

## Overview

Students working on programming projects often save multiple copies of the same folder with names like `final`, `final_new`, or `final_latest` to keep track of changes. This quickly becomes confusing — it's hard to tell which version is correct, compare changes, or recover work after a mistake.

Professional version-control tools like Git solve this problem, but their extensive command sets and advanced concepts (branching, merging, remotes, conflict resolution) can be overwhelming for beginners just trying to understand the core idea.

**MiniGit** is a compact, local, command-line version-control system built to demonstrate the fundamental principles of version control — repository creation, file tracking, committing, history inspection, and restoration — without the complexity of a full-scale tool. It is designed as a **learning project**, not a replacement for Git, Mercurial, or SVN.

## Motivation

- Manual folder duplication is error-prone and hard to navigate.
- Full-scale VCS tools have a steep learning curve for beginners.
- MiniGit isolates the *essential* workflow — track, commit, inspect, restore — so the underlying concepts of version control are easier to observe and understand.
- Building MiniGit lets the team apply **C++, OOP, file handling, and core data structures** in a single, practical application.

## Features

MiniGit supports the following terminal commands:

| Command | Description |
|---|---|
| `init` | Create a new local repository |
| `add` | Select files for tracking |
| `commit` | Save the current version of tracked files with a message |
| `log` | Display the sequence of saved commits |
| `checkout` | Restore a previously saved version |
| `rollback` | Reverse the most recent restoration |

**Planned optional extensions** (time/feasibility permitting):
- Simple branching
- File-version comparison (diffing)

## System Architecture

MiniGit follows a layered, modular architecture:

```
1. CLI Interface        → Parses and validates terminal commands
2. Command Handler      → Dispatches commands, validates state, maps results/errors
3. Repository Manager   → Manages repo state, commit history, staging, and undo
4. Storage Layer        → Handles file I/O for the .minigit/ directory (objects, metadata)
5. Data Structures Layer→ Backs the above with linked lists, hash tables, and stacks
```

All repository data is stored locally inside a `.minigit/` directory, containing:
- `objects/` — commit and blob storage
- `refs/`, `HEAD`, `index` — metadata files

### Core Classes

| Class | Responsibility |
|---|---|
| `Repository` | Initialization, file tracking, commits, history, storage |
| `Commit` | Stores commit ID, message, timestamp, parent reference, and snapshots |
| `FileSnapshot` | Stores file-version details and snapshot location |
| `CommandHandler` | Processes terminal commands and validates arguments |

### Data Structures Used

| Structure | Purpose |
|---|---|
| **Singly Linked List** | Maintains the linear commit history |
| **Hash Table** | Enables fast file and commit lookup |
| **Stack** | Supports rollback after a checkout operation |
| **File Handling (I/O)** | Persists metadata and snapshots in `.minigit/` |

> The core system supports **one user, one local repository, and a linear commit history**. Tree/graph-based branching is an optional extension attempted only after core functionality is stable.

## Project Roadmap

**Phase 1 — Planning & Design**
Define feature scope, command set, repository folder structure, class responsibilities, and chosen data structures. Finalize command flow for `init`, `add`, `commit`, `log`, `checkout`, and `rollback`.

**Phase 2 — Repository & Commit Module**
Implement `init`, `add`, `commit`, and `log`. Create the `.minigit` directory to store metadata and snapshots. Each commit stores an ID, message, timestamp, parent link, and file-snapshot details, backed by a linked list (history) and hash table (lookup).

**Phase 3 — Restore, Test & Document**
Implement `checkout` and `rollback` (backed by a stack). Focus on validation, error handling, testing, documentation, a sample repository, and a final demonstration. Optional features (branching, diffing) are attempted here if time permits.

## Getting Started

> ⚠️ MiniGit is under active development. Build instructions will be finalized as Phase 2 progresses.

### Prerequisites
- A standard C++ compiler (C++11 or later recommended)
- A file system with standard file-I/O support (Linux/macOS/Windows)

### Build (planned)
```bash
git clone <repository-url>
cd minigit
make
```

### Usage (planned)
```bash
./minigit init                     # Initialize a new repository
./minigit add <file>                # Track a file
./minigit commit -m "message"       # Save a version
./minigit log                       # View commit history
./minigit checkout <commit-id>      # Restore a version
./minigit rollback                  # Undo the last checkout
```

## Scope & Assumptions

- Designed for a **single user** on a **single local machine**.
- Handles **text-based files** (`.cpp`, `.h`, `.txt`, etc.) within the repository folder.
- Assumes the user has read/write permissions for tracked files.
- **Out of scope:** remote repositories, collaboration, authentication, merging, conflict resolution, and binary-file optimization.

## Deliverables

- Organized C++ source code
- A sample repository for demonstration
- Test cases
- README / user guide with command examples
- Project report
- Live demonstration

## Team & Phase 1 Contributions

| Name | Role | Phase 1 Contributions (Planning & Design) |
|---|---|---|
| **Abhinesh Gangwar** | Team Lead & Systems Architect | Overall system architecture, modular 5-layer design, project scoping, and command dispatch flow orchestration |
| **Sparsh Jain** | CLI & Staging Designer | Command-line syntax grammar (`init`, `add`, `commit`), argument validation rules, and Hash Table staging index design |
| **Krish Bajaj** | Commit Graph Modeler | Singly Linked List model for commit history graph, pointer transition design, and commit node metadata schema |
| **Snehil Joshi** | State & Storage Lead | Rollback LIFO stack mechanism design, `.minigit/` disk storage layout specification, and Phase 1 project documentation compilation |

**Course:** PBL in DS CPP · DSCPP-III-2026-T296 · CSE (AI&ML)  
**Team Name:** Blaze  
**Current Status:** Phase 1 (Planning & System Design)

## References

1. [Git SCM Documentation](https://git-scm.com/doc)
2. Scott Chacon & Ben Straub, [*Pro Git, 2nd Edition*](https://git-scm.com/book/en/v2)
3. [git-checkout Documentation](https://git-scm.com/docs/git-checkout)
4. [C++ Standard Library Reference — cppreference.com](https://en.cppreference.com/)
5. Pat Morin, [*Open Data Structures (in C++)*](https://opendatastructures.org/ods-cpp/)
6. [NPTEL, IIT Delhi — Introduction to Data Structures and Algorithms](https://nptel.ac.in/courses/106102064)

---

*This project is developed as part of the CSE (AI&ML) course requirements (DSCPP-III-2026-T296) and is intended for educational purposes.*