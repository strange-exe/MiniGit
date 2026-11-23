# Contributing to MiniGit

MiniGit is a teaching project: every module exists to show one data structure at work. Keep changes small, tested and easy to explain.

## Build and test

Prerequisites: `g++` (C++14) and GNU `make` (`mingw32-make` on Windows).

```bash
make all      # builds module/minigit
make test     # builds and runs every test program in module/tests
make clean    # removes binaries and test executables
```

Every test program must pass before a change is merged. A test stops at the first failed `CHECK` and prints `FAIL <file>:<line>: <expression>`.

## Branches and commits

- Work on a branch named after you or the feature (`krish-commit`, `snehil-log`) and open a pull request into `main`.
- One logical change per commit. Do not mix a refactor with a behaviour change.
- Commit messages follow the style already in the history: `type(scope): summary`, in the imperative, for example `fix(commit): undo/redo history corruption and unchecked writes`.

| Type | Use for |
|---|---|
| `feat` | a new command, option or module |
| `fix` | a defect; add a case to `tests/test_regression.cpp` |
| `docs` | README, DOCUMENTATION, reports, comments only |
| `test` | tests only |
| `chore` | build files, ignore rules, tooling |
| `ci` | automation configuration |

## Code conventions

- **Core code never prints.** Command functions return a result struct (`InitResult`, `AddResult`, `CommitResult` ...). Only `src/CLI.cpp` turns results into messages and exit codes.
- **Paths go through `normalizePath()`** (`PathUtils.h`). A path read from a repository file must also pass `isSafeRepoPath()`.
- **Repository files are written with `writeFileAtomic()`** (`FileUtils.h`), so a failure never leaves a half-written file.
- **New command = one new row** in the command table in `src/CLI.cpp`. Help, usage and typo suggestions come from that table.
- **Check every write.** Report a failed write; do not assume it worked.
- Match the existing style: 4-space indent, `namespace minigit`, headers with `#pragma once`.

## Tests

- Unit tests sit next to the module they cover (`tests/test_<module>.cpp`). Use the `CHECK` and `PASS` macros from `tests/test_common.h`.
- Use a throw-away directory from `tc::freshDir()` and remove it at the end. Never touch the real project folder.
- A bug fix comes with a test that fails without the fix.
- New test programs need a target and a place in `TESTS` and in the `test` recipe of `module/Makefile`.

## Documentation

Update `README.md` (command table, test list, repository tree) and `DOCUMENTATION.md` when a command or a test program is added or changed. If the on-disk format changes, update `docs/REPOSITORY_FORMAT.md` too.
