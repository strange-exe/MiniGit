#include "RollbackCommand.h"

#include <filesystem>
#include <fstream>
#include <set>

#include "BranchCommand.h"
#include "RepoUtils.h"
#include "StagingArea.h"

namespace fs = std::filesystem;

namespace minigit {

RollbackResult rollbackRepo(const std::string& commitId, bool force, const std::string& root) {
    RollbackResult r;
    std::error_code ec;
    fs::path gitDir = fs::path(root) / ".minigit";
    if (!fs::is_directory(gitDir, ec)) {
        r.status = RollbackResult::NOT_A_REPO;
        r.errorMessage = "not a minigit repository (run 'minigit init')";
        return r;
    }

    r.commitId = commitId;

    HeadState head = readHeadState(root);
    if (!head.valid) {
        r.status = RollbackResult::ERROR;
        r.errorMessage = head.error;
        return r;
    }
    if (head.detached) {  // rollback moves the active branch, and there is none
        r.status = RollbackResult::ERROR;
        r.errorMessage = detachedHeadError(head);
        return r;
    }

    const std::string currentCommitId = head.commitId;
    if (currentCommitId.empty()) {
        r.status = RollbackResult::NO_COMMITS;
        r.errorMessage = "cannot rollback: repository has no commits";
        return r;
    }

    // Resolve short or full commit ID
    bool ambiguous = false;
    std::string targetId = resolveCommitId(root, commitId, &ambiguous);
    if (targetId.empty()) {
        r.status = RollbackResult::INVALID_COMMIT;
        r.errorMessage = ambiguous ? "commit id '" + commitId + "' is ambiguous (give more digits)"
                                   : "commit '" + commitId + "' does not exist";
        return r;
    }

    CommitMetadata targetMeta;
    if (!readCommitMeta(root, targetId, targetMeta)) {
        r.status = RollbackResult::INVALID_COMMIT;
        r.errorMessage = "could not read metadata for commit '" + targetId + "'";
        return r;
    }

    // Protect uncommitted and staged changes unless force override is passed
    if (!force) {
        std::string err;
        if (checkUncommittedChanges(root, currentCommitId, err)) {
            r.status = RollbackResult::UNCOMMITTED_CHANGES;
            r.errorMessage = err + " (use --force to discard)";
            return r;
        }
    }

    std::string currBranch = head.branch;

    // Atomicity check: the whole target history (every commit and every snapshot file) must be
    // present before any modification, so a damaged commit can never leave a half-restored tree.
    bool targetComplete = true;
    std::string targetProblem;
    auto targetTracked = getTrackedFilesAtCommit(root, targetId, &targetComplete, &targetProblem);
    if (!targetComplete) {
        r.status = RollbackResult::ERROR;
        r.errorMessage = "cannot rollback: " + targetProblem;
        return r;
    }

    auto curTracked = getTrackedFilesAtCommit(root, currentCommitId);

    // 1. Remove files introduced after target commit (exist in current but not target)
    int removedCount = 0;
    for (const auto& kv : curTracked) {
        if (targetTracked.find(kv.first) == targetTracked.end()) {
            removeFileAndEmptyDirs(root, kv.first);
            ++removedCount;
        }
    }

    // 2. Restore all files from target commit snapshot to working tree
    int restoredCount = 0;
    for (const auto& kv : targetTracked) {
        fs::path dest = fs::path(root) / kv.first;
        fs::create_directories(dest.parent_path(), ec);
        fs::copy_file(kv.second, dest, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            r.status = RollbackResult::ERROR;
            r.errorMessage = "could not restore '" + kv.first +
                             "'; the branch was not moved but the working tree may be partly restored (retry with --force)";
            return r;
        }
        ++restoredCount;
    }

    // 3. Clear staging area to ensure a clean state
    StagingArea sa(root);
    sa.clear();

    // 4. Update the active branch reference to point to target commit
    if (!setHEADCommitId(root, targetId)) {
        r.status = RollbackResult::ERROR;
        r.errorMessage = "could not update branch reference to commit '" + targetId + "'";
        return r;
    }

    r.status = RollbackResult::SUCCESS;
    r.commitId = targetId;
    r.shortId = targetId.size() >= 7 ? targetId.substr(0, 7) : targetId;
    r.message = targetMeta.message;
    r.branch = currBranch;
    r.filesRestored = restoredCount;
    r.filesRemoved = removedCount;
    return r;
}

}  // namespace minigit
