#include "BranchCommand.h"

#include <algorithm>
#include <filesystem>

#include "CommitGraph.h"
#include "RepoStore.h"
#include "RestoreCommand.h"

namespace fs = std::filesystem;

namespace minigit {

static bool isRepo(const std::string& root) {
    std::error_code ec;
    return fs::is_directory(fs::path(root) / ".minigit", ec);
}

static BranchResult notARepo() {
    BranchResult r;
    r.status = BranchResult::NOT_A_REPO;
    r.errorMessage = "not a minigit repository (or any of the parent directories)";
    return r;
}

BranchResult showBranches(const std::string& root) {
    if (!isRepo(root)) return notARepo();
    BranchResult r;
    r.branches = repo::listBranchNames(root);
    r.current = repo::currentBranch(root);
    r.headCommitId = repo::headCommitId(root);
    // The current branch has no ref file until its first commit; still show it.
    if (!r.current.empty() && std::find(r.branches.begin(), r.branches.end(), r.current) == r.branches.end()) {
        r.branches.push_back(r.current);
        std::sort(r.branches.begin(), r.branches.end());
    }
    r.status = BranchResult::LISTED;
    return r;
}

BranchResult createBranch(const std::string& name, const std::string& root) {
    if (!isRepo(root)) return notARepo();
    BranchResult r;
    r.name = name;

    std::string err;
    if (!repo::isValidBranchName(name, err)) {
        r.status = BranchResult::INVALID_NAME;
        r.errorMessage = err;
        return r;
    }
    if (repo::branchFileExists(root, name)) {
        r.status = BranchResult::ALREADY_EXISTS;
        r.errorMessage = "a branch named '" + name + "' already exists";
        return r;
    }
    std::string head = repo::headCommitId(root);
    if (head.empty()) {
        r.status = BranchResult::NO_COMMITS;
        r.errorMessage = "cannot create a branch before the first commit";
        return r;
    }
    if (!repo::writeBranch(root, name, head)) {
        r.status = BranchResult::ERROR;
        r.errorMessage = "could not write the branch reference";
        return r;
    }
    r.status = BranchResult::CREATED;
    r.commitId = head;
    return r;
}

BranchResult switchBranch(const std::string& name, const std::string& root, bool create) {
    if (!isRepo(root)) return notARepo();
    BranchResult r;
    r.name = name;

    std::string err;
    if (!repo::isValidBranchName(name, err)) {
        r.status = BranchResult::INVALID_NAME;
        r.errorMessage = err;
        return r;
    }

    std::string head = repo::headCommitId(root);

    if (create) {
        if (repo::branchFileExists(root, name)) {
            r.status = BranchResult::ALREADY_EXISTS;
            r.errorMessage = "a branch named '" + name + "' already exists";
            return r;
        }
        // Same commit, same files: only HEAD (and a new ref once there is a commit) change,
        // so the working folder and the index are left exactly as they are.
        if (!head.empty() && !repo::writeBranch(root, name, head)) {
            r.status = BranchResult::ERROR;
            r.errorMessage = "could not write the branch reference";
            return r;
        }
        if (!repo::writeHeadRaw(root, "ref: refs/heads/" + name)) {
            // switch -c is a two-file operation: do not leave an orphaned
            // branch reference behind if HEAD could not be moved to it.
            if (!head.empty()) {
                std::error_code cleanupError;
                fs::remove(fs::path(root) / ".minigit" / "refs" / "heads" / name, cleanupError);
            }
            r.status = BranchResult::ERROR;
            r.errorMessage = "could not update HEAD";
            return r;
        }
        r.status = BranchResult::SWITCHED;
        r.commitId = head;
        return r;
    }

    if (repo::currentBranch(root) == name) {
        r.status = BranchResult::ALREADY_ON;
        r.commitId = head;
        return r;
    }
    if (!repo::branchFileExists(root, name)) {
        r.status = BranchResult::NOT_FOUND;
        r.errorMessage = "no branch named '" + name + "'";
        return r;
    }
    std::string tip = repo::readBranch(root, name);
    if (tip.empty()) {
        r.status = BranchResult::NO_COMMITS;
        r.errorMessage = "branch '" + name + "' has no commits yet";
        return r;
    }

    // Leaving a detached commit that no branch reaches would make it hard to find again.
    if (repo::isDetached(root) && !head.empty()) {
        CommitGraph graph(root);
        if (graph.load(false) && !graph.contains(head)) {  // branch tips only
            r.warning = "you are leaving commit " + repo::shortId(head) +
                        " behind; no branch points to it (use its id with 'checkout' to return)";
        }
    }

    RestoreResult rr = restoreWorkingTree(root, tip, "ref: refs/heads/" + name);
    switch (rr.status) {
        case RestoreResult::SUCCESS:
            r.status = BranchResult::SWITCHED;
            r.commitId = tip;
            r.filesRestored = rr.filesRestored;
            r.filesRemoved = rr.filesRemoved;
            return r;
        case RestoreResult::UNSAVED_CHANGES:
            r.status = BranchResult::UNSAVED_CHANGES;
            r.blockingFiles = rr.blockingFiles;
            r.errorMessage = rr.errorMessage;
            r.warning.clear();
            return r;
        default:
            r.status = BranchResult::ERROR;
            r.errorMessage = rr.errorMessage;
            r.warning.clear();
            return r;
    }
}

}  // namespace minigit
