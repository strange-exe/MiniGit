#pragma once
// Commit command, metadata storage, and undo/redo operations (Krish's module).
#include <string>
#include <vector>

namespace minigit {

struct CommitResult {
    enum Status {
        SUCCESS,
        NOTHING_TO_COMMIT,
        NOT_A_REPO,
        INVALID_ARGUMENTS,
        FILE_NOT_FOUND,
        NOTHING_TO_UNDO,
        NOTHING_TO_REDO,
        ERROR
    };

    Status status = ERROR;
    std::string commitId;
    std::string parentId;
    std::string message;
    int filesCommitted = 0;
    std::string errorMessage;
};

// Core commit functions (do not print directly)
CommitResult commitRepo(const std::string& message, const std::string& root = ".");
CommitResult undoCommit(const std::string& root = ".");
CommitResult redoCommit(const std::string& root = ".");

}  // namespace minigit
