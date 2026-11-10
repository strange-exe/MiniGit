#pragma once
// Branch management and branch switching (DAG-based branch references).
#include <string>
#include <vector>

namespace minigit {

struct BranchInfo {
    std::string name;
    std::string commitId;
    std::string shortId;
    std::string commitMessage;
    bool isActive = false;
};

struct BranchResult {
    enum Status {
        LIST_SUCCESS,
        CREATE_SUCCESS,
        DELETE_SUCCESS,
        NOT_A_REPO,
        NO_COMMITS,
        ALREADY_EXISTS,
        INVALID_NAME,
        NOT_FOUND,
        CANNOT_DELETE_ACTIVE,
        ERROR
    };

    Status status = ERROR;
    std::vector<BranchInfo> branches;
    std::string detachedAt;  // LIST_SUCCESS: commit HEAD is detached at, "" when on a branch
    std::string branchName;
    std::string commitId;
    std::string shortId;
    std::string errorMessage;
};

struct CheckoutResult {
    enum Status {
        SUCCESS,
        ALREADY_ON_BRANCH,
        NOT_A_REPO,
        NO_COMMITS,
        NOT_FOUND,
        INVALID_NAME,
        UNCOMMITTED_CHANGES,
        ERROR
    };

    Status status = ERROR;
    std::string branchName;
    std::string commitId;
    std::string shortId;
    std::string errorMessage;
};

BranchResult listBranches(const std::string& root = ".");
BranchResult createBranch(const std::string& name, const std::string& root = ".");
BranchResult deleteBranch(const std::string& name, const std::string& root = ".");
CheckoutResult checkoutBranch(const std::string& name, bool force = false, const std::string& root = ".");

std::string getCurrentBranch(const std::string& root = ".");
std::string getBranchCommitId(const std::string& branch, const std::string& root = ".");
bool isValidBranchName(const std::string& name);

}  // namespace minigit
