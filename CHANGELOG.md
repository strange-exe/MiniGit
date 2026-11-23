# Changelog

What changed in each phase of the project. Dates come from the git history.

## Phase 3 (November 2026)

- `rollback <commit-id> [--force]`: reset-style rollback that keeps every later commit object.
- `branch`, `branch <name>`, `branch -d <name>` and `checkout <branch> [--force]`: lightweight branches stored as files in `.minigit/refs/heads/`.
- `log --all --graph`: ASCII drawing of the commit DAG across all branches, built with `CommitGraph` (adjacency list, cycle check, topological order).
- `checkout <commit-id>`: restores a saved commit and leaves `HEAD` detached; refuses first if work would be lost.
- `sample_repo/` with a step-by-step demo script, `walkthrough.ps1`.
- Phase 2 and Phase 3 reports and presentations added under `Reports/`.

## Phase 2 (September to October 2026)

- `init`, `add`, `remove`, `ignore`: repository setup, the staging index and the dual-Trie ignore engine.
- `commit` with undo and redo on two `std::deque`s (history limit 20), and `log` / `log --reverse` over a doubly linked list.
- Table-driven command dispatcher: `help`, `help <command>`, `<command> --help`, stricter argument checks and typo suggestions by edit distance.
- Fixes: undo/redo history corruption and unchecked writes in `commit`; argument handling, install refresh and wildcard patterns in the CLI; the built-in `.minigit/` rule is named instead of printing line -1.
- Test programs for the modules, tracked under `module/tests/`.

## Phase 1 (September 2026)

- Initial repository, project proposal report (`Reports/report.pdf`) and presentation.
- An interactive web page that visualizes the data structures (`index.html`, `script.js`, `styles.css`), part of the Phase 1 deliverables; it is not part of the current tree.
