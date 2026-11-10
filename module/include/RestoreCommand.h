#pragma once
// checkout <commit-id>: restore the files of a saved commit (Snehil's Phase 3 module).
//
// Rules (from the Phase 3 report):
//  * Unsaved work is checked FIRST. Staged files, edited or deleted tracked files, and
//    untracked files the restore would overwrite make checkout refuse, changing nothing.
//  * Files that are in the current commit but not in the target are removed (they were tracked).
//  * Ignored files (Dual Trie / .minigitignore) are never written, overwritten or deleted.
//  * checkout leaves HEAD detached at the commit, so no branch is moved or lost.
//
// "Tracked" means: present in the complete effective tree reconstructed from
// the first-parent history of the commit HEAD points to.
// Like every module it returns a result and never prints.
#include <string>
#include <vector>

namespace minigit {

struct RestoreResult {
    enum Status {
        SUCCESS,
        NOT_A_REPO,
        INVALID_ARGUMENTS,
        COMMIT_NOT_FOUND,
        AMBIGUOUS_ID,
        UNSAVED_CHANGES,
        SNAPSHOT_MISSING,
        ERROR
    };

    Status status = ERROR;
    std::string commitId;          // commit that is now checked out
    std::string message;           // its commit message
    std::string previousHead;      // raw HEAD before the restore ("ref: ..." or an id), for rollback
    std::string previousCommitId;  // commit HEAD pointed to before ("" if none)
    int filesRestored = 0;
    int filesRemoved = 0;
    int filesSkippedIgnored = 0;
    std::vector<std::string> blockingFiles;  // UNSAVED_CHANGES: "staged: a.txt", "modified: b.txt", ...
    std::string errorMessage;
};

// minigit checkout <commit-id>   (full id or a unique prefix of 4+ characters)
RestoreResult checkoutCommit(const std::string& commitIdOrPrefix, const std::string& root = ".");

// Shared engine, also used by `switch` and available to rollback/merge:
// checks for unsaved work, rebuilds the complete effective trees of the current
// and target commits, stages replacement files beside their destinations, then
// applies the worktree and HEAD changes with backups so a failed restore can be
// rolled back. `newHead` is either a valid branch reference or `targetId`.
RestoreResult restoreWorkingTree(const std::string& root, const std::string& targetId, const std::string& newHead);

}  // namespace minigit
