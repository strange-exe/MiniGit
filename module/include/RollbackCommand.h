#pragma once
// Dedicated reset-style rollback command.
// Moves active branch pointer to a historical commit and restores its snapshot.
#include <string>

namespace minigit {

struct RollbackResult {
    enum Status {
        SUCCESS,
        NOT_A_REPO,
        NO_COMMITS,
        INVALID_COMMIT,
        UNCOMMITTED_CHANGES,
        ERROR
    };

    Status status = ERROR;
    std::string commitId;
    std::string shortId;
    std::string message;
    std::string branch;
    std::string errorMessage;
    int filesRestored = 0;
    int filesRemoved = 0;
};

// Rolls back currently active branch to target commit, restoring working tree.
// Subsequent commits are preserved. Uncommitted changes are protected unless force=true.
RollbackResult rollbackRepo(const std::string& commitId, bool force = false, const std::string& root = ".");

}  // namespace minigit

