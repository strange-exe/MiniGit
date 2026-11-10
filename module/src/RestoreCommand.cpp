#include "RestoreCommand.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "BranchCommand.h"
#include "IgnoreManager.h"
#include "PathUtils.h"
#include "RepoUtils.h"
#include "StagingArea.h"

namespace fs = std::filesystem;

namespace minigit {
namespace {

struct FileOperation {
    fs::path destination;
    fs::path temporary;
    fs::path backup;
    bool moveOriginal = false;
    bool installedNew = false;
};

static fs::file_status noFollowStatus(const fs::path& p, std::error_code& ec) {
    ec.clear();
    fs::file_status st = fs::symlink_status(p, ec);
    // A missing path is reported as type not_found plus an error code. Test the type: on
    // MinGW with ghc::filesystem the Windows "file not found" code does not compare equal
    // to std::errc::no_such_file_or_directory, so a deleted file looked uninspectable.
    if (st.type() == fs::file_type::not_found || ec == std::errc::no_such_file_or_directory) {
        ec.clear();
        return fs::file_status(fs::file_type::not_found);
    }
    return st;
}

static bool existsNoFollow(const fs::path& p, std::error_code& ec) {
    return fs::exists(noFollowStatus(p, ec));
}

static bool readWorkingFile(const fs::path& p, std::string& content) {
    std::error_code ec;
    fs::file_status st = noFollowStatus(p, ec);
    if (ec || fs::is_symlink(st) || !fs::is_regular_file(st)) return false;
    std::ifstream in(p.string(), std::ios::binary);
    if (!in) return false;
    content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

static fs::path uniqueSiblingPath(const fs::path& destination) {
    static std::atomic<unsigned long long> counter(0);
    const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 1000; ++attempt) {
        const unsigned long long serial = counter.fetch_add(1);
        fs::path candidate = destination.parent_path() /
            (".minigit-restore-" + std::to_string(ticks) + "-" + std::to_string(serial) + ".tmp");
        std::error_code ec;
        if (!existsNoFollow(candidate, ec) && !ec) return candidate;
    }
    return fs::path();
}

static bool stageBytes(const fs::path& p, const std::string& content) {
    std::ofstream out(p.string(), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.flush();
    out.close();
    return static_cast<bool>(out);
}

static bool ensureParentDirectories(const fs::path& root, const fs::path& destination,
                                    std::vector<fs::path>& created, std::string& error) {
    fs::path relativeParent = destination.parent_path().lexically_relative(root);
    if (relativeParent.empty() && destination.parent_path() != root) {
        error = "destination parent is outside the repository";
        return false;
    }

    fs::path current = root;
    for (const fs::path& component : relativeParent) {
        if (component == "." || component.empty()) continue;
        if (component == "..") {
            error = "destination parent escapes the repository";
            return false;
        }
        current /= component;
        std::error_code ec;
        fs::file_status st = noFollowStatus(current, ec);
        if (ec) {
            error = "could not inspect directory '" + current.string() + "': " + ec.message();
            return false;
        }
        if (fs::exists(st)) {
            if (fs::is_symlink(st) || !fs::is_directory(st)) {
                error = "path component '" + current.string() + "' is not a safe directory";
                return false;
            }
            continue;
        }
        if (!fs::create_directory(current, ec) || ec) {
            error = "could not create directory '" + current.string() + "'";
            if (ec) error += ": " + ec.message();
            return false;
        }
        created.push_back(current);
    }
    return true;
}

static void removeEmptyCreatedDirs(const std::vector<fs::path>& dirs) {
    for (auto it = dirs.rbegin(); it != dirs.rend(); ++it) {
        std::error_code ec;
        if (fs::is_directory(*it, ec) && fs::is_empty(*it, ec)) fs::remove(*it, ec);
    }
}

static void pruneEmptyDirs(fs::path dir, const fs::path& root) {
    std::error_code ec;
    while (!dir.empty() && dir != root && fs::is_directory(dir, ec) && fs::is_empty(dir, ec)) {
        fs::remove(dir, ec);
        if (ec) break;
        dir = dir.parent_path();
    }
}

static bool checkParentPathSafety(const fs::path& root, const fs::path& destination,
                                  std::vector<std::string>& blockers) {
    fs::path relative = destination.lexically_relative(root);
    if (relative.empty() || isOutsideRepo(relative.generic_string())) {
        blockers.push_back("unsafe path: " + destination.string());
        return false;
    }

    fs::path current = root;
    fs::path parent = relative.parent_path();
    for (const fs::path& component : parent) {
        if (component == "." || component.empty()) continue;
        if (component == "..") {
            blockers.push_back("unsafe path: " + destination.string());
            return false;
        }
        current /= component;
        std::error_code ec;
        fs::file_status st = noFollowStatus(current, ec);
        if (ec) {
            blockers.push_back("cannot inspect path: " + current.string());
            return false;
        }
        if (!fs::exists(st)) break;  // remaining parent components do not exist yet
        if (fs::is_symlink(st) || !fs::is_directory(st)) {
            blockers.push_back("path conflict: '" + current.string() + "' is not a safe directory");
            return false;
        }
    }
    return true;
}

static bool validateHeadTarget(const std::string& newHead, const std::string& targetId,
                               std::string& error) {
    if (newHead.rfind("ref:", 0) == 0) {
        const std::string ref = newHead.substr(4);
        const std::string prefix = " refs/heads/";
        if (ref.rfind(prefix, 0) != 0) {
            error = "invalid branch reference for HEAD";
            return false;
        }
        const std::string name = ref.substr(prefix.size());
        if (!isValidBranchName(name)) {
            error = "invalid branch reference for HEAD: '" + name + "'";
            return false;
        }
        return true;
    }
    if (newHead != targetId) {
        error = "detached HEAD must point to the restored commit";
        return false;
    }
    return true;
}

static bool rollbackOperations(std::vector<FileOperation>& operations, std::string& error) {
    bool ok = true;
    for (auto it = operations.rbegin(); it != operations.rend(); ++it) {
        std::error_code ec;
        if (it->installedNew) {
            fs::remove(it->destination, ec);
            if (ec) {
                ok = false;
                if (error.empty()) error = "could not remove partially restored file '" + it->destination.string() + "'";
            }
        }
        if (it->moveOriginal) {
            ec.clear();
            fs::rename(it->backup, it->destination, ec);
            if (ec) {
                ok = false;
                if (error.empty()) error = "could not restore backup '" + it->backup.string() + "'";
            }
        }
        if (!it->temporary.empty()) {
            ec.clear();
            fs::remove(it->temporary, ec);
        }
    }
    return ok;
}

static bool moveToBackup(FileOperation& op, std::string& error) {
    std::error_code ec;
    if (!existsNoFollow(op.destination, ec)) {
        if (ec) {
            error = "could not inspect '" + op.destination.string() + "': " + ec.message();
            return false;
        }
        return true;
    }
    op.backup = uniqueSiblingPath(op.destination);
    if (op.backup.empty()) {
        error = "could not allocate a backup path for '" + op.destination.string() + "'";
        return false;
    }
    fs::rename(op.destination, op.backup, ec);
    if (ec) {
        error = "could not back up '" + op.destination.string() + "': " + ec.message();
        return false;
    }
    op.moveOriginal = true;
    return true;
}

// The complete file tree of a commit, path -> contents. Built on the shared reader in
// RepoUtils.h (the same one commit, rollback and branch use), so 'deleted' entries are
// honoured. Fails if any commit or snapshot in the chain is missing, or if one path is
// both a file and a folder (a restore would then fail half-way).
static bool effectiveTree(const std::string& root, const std::string& commitId,
                          std::map<std::string, std::string>& tree, std::string* errorMessage) {
    tree.clear();
    bool complete = true;
    std::string problem;
    auto tracked = getTrackedFilesAtCommit(root, commitId, &complete, &problem);
    if (!complete) {
        if (errorMessage) *errorMessage = problem;
        return false;
    }
    for (const auto& item : tracked) {
        std::string content;
        if (!readWorkingFile(fs::path(item.second), content)) {
            tree.clear();
            if (errorMessage) *errorMessage = "snapshot of '" + item.first + "' is unreadable";
            return false;
        }
        tree[item.first] = content;
    }
    for (const auto& item : tree) {
        const std::string& path = item.first;
        for (std::size_t slash = path.find('/'); slash != std::string::npos; slash = path.find('/', slash + 1)) {
            if (tree.count(path.substr(0, slash))) {
                if (errorMessage) *errorMessage = "commit tree has a file/folder conflict at '" + path.substr(0, slash) + "'";
                tree.clear();
                return false;
            }
        }
    }
    return true;
}

}  // namespace

RestoreResult restoreWorkingTree(const std::string& root, const std::string& targetId, const std::string& newHead) {
    RestoreResult r;
    std::error_code ec;
    fs::path rootPath = fs::weakly_canonical(fs::path(root), ec);
    if (ec) {
        r.status = RestoreResult::ERROR;
        r.errorMessage = "could not resolve repository path: " + ec.message();
        return r;
    }
    if (!fs::is_directory(rootPath / ".minigit", ec) || ec) {
        r.status = RestoreResult::NOT_A_REPO;
        r.errorMessage = "not a minigit repository (or any of the parent directories)";
        return r;
    }

    const HeadState head = readHeadState(root);
    if (!head.valid) {
        r.status = RestoreResult::ERROR;
        r.errorMessage = head.error;
        return r;
    }
    r.previousHead = head.detached ? head.commitId : "ref: refs/heads/" + head.branch;
    r.previousCommitId = head.commitId;

    std::string headError;
    if (!validateHeadTarget(newHead, targetId, headError)) {
        r.status = RestoreResult::INVALID_ARGUMENTS;
        r.errorMessage = headError;
        return r;
    }

    CommitMetadata target;
    if (!readCommitMeta(root, targetId, target)) {
        r.status = RestoreResult::COMMIT_NOT_FOUND;
        r.errorMessage = "commit '" + targetId + "' not found or has invalid metadata";
        return r;
    }

    // Rebuild both complete trees before inspecting or changing the worktree.
    std::map<std::string, std::string> currentTree;
    if (!r.previousCommitId.empty()) {
        std::string treeError;
        if (!effectiveTree(root, r.previousCommitId, currentTree, &treeError)) {
            r.status = RestoreResult::SNAPSHOT_MISSING;
            r.errorMessage = "cannot build the current commit tree: " + treeError;
            return r;
        }
    }

    std::map<std::string, std::string> targetTree;
    std::string targetTreeError;
    if (!effectiveTree(root, targetId, targetTree, &targetTreeError)) {
        r.status = RestoreResult::SNAPSHOT_MISSING;
        r.errorMessage = "cannot build the target commit tree: " + targetTreeError;
        return r;
    }

    // Use the currently active worktree's ignore rules while switching. This
    // protects local ignored content even when the target branch has different
    // .minigitignore rules; the target version becomes active after restoration.
    IgnoreManager ignore(root);
    StagingArea sa(root);
    for (const std::string& f : sa.getStagedFiles()) r.blockingFiles.push_back("staged: " + f);

    // A clean tracked worktree is required before any file or HEAD changes.
    for (const auto& item : currentTree) {
        const std::string& f = item.first;
        if (ignore.isIgnored(f)) continue;
        fs::path p = rootPath / fs::path(f);
        std::error_code statusError;
        fs::file_status st = noFollowStatus(p, statusError);
        if (statusError) {
            r.blockingFiles.push_back("cannot inspect: " + f);
        } else if (!fs::exists(st)) {
            r.blockingFiles.push_back("deleted: " + f);
        } else {
            std::string now;
            if (fs::is_symlink(st) || !fs::is_regular_file(st) || !readWorkingFile(p, now) || now != item.second)
                r.blockingFiles.push_back("modified: " + f);
        }
    }

    std::map<std::string, std::string> targetContent;
    std::set<std::string> targetFiles;
    for (const auto& item : targetTree) {
        const std::string& f = item.first;
        targetFiles.insert(f);
        if (ignore.isIgnored(f)) {
            ++r.filesSkippedIgnored;
            continue;  // ignored paths are deliberately never written or removed
        }

        fs::path destination = rootPath / fs::path(f);
        if (!checkParentPathSafety(rootPath, destination, r.blockingFiles)) continue;

        std::error_code statusError;
        fs::file_status st = noFollowStatus(destination, statusError);
        if (statusError) {
            r.blockingFiles.push_back("cannot inspect: " + f);
            continue;
        }
        if (fs::exists(st)) {
            if (fs::is_symlink(st) || !fs::is_regular_file(st)) {
                r.blockingFiles.push_back("path conflict: '" + f + "' is not a regular file");
                continue;
            }
            if (!currentTree.count(f)) {
                // Even identical bytes belong to an untracked path. Do not
                // overwrite user-owned data merely because its contents match.
                r.blockingFiles.push_back("untracked: " + f + " would be overwritten");
                continue;
            }
        }
        targetContent[f] = item.second;
    }

    // HEAD itself must be a regular non-symlink file before the transaction starts.
    const fs::path headPath = rootPath / ".minigit" / "HEAD";
    std::error_code headStatusError;
    fs::file_status headStatus = noFollowStatus(headPath, headStatusError);
    if (headStatusError || !fs::is_regular_file(headStatus) || fs::is_symlink(headStatus)) {
        r.blockingFiles.push_back("repository HEAD is not a regular file");
    }

    if (!r.blockingFiles.empty()) {
        r.status = RestoreResult::UNSAVED_CHANGES;
        r.errorMessage = "unsaved changes or path conflicts would prevent a safe restore";
        return r;
    }

    // Stage every new file beside its destination before replacing anything.
    // Same-directory renames keep the final replacement on the same filesystem.
    std::vector<fs::path> createdDirs;
    std::vector<FileOperation> operations;
    std::string failure;

    for (const auto& item : targetContent) {
        FileOperation op;
        op.destination = rootPath / fs::path(item.first);
        if (!ensureParentDirectories(rootPath, op.destination, createdDirs, failure)) break;
        op.temporary = uniqueSiblingPath(op.destination);
        if (op.temporary.empty() || !stageBytes(op.temporary, item.second)) {
            if (!op.temporary.empty()) {
                std::error_code cleanupError;
                fs::remove(op.temporary, cleanupError);
            }
            failure = "could not stage restored contents for '" + item.first + "'";
            break;
        }
        operations.push_back(op);
    }

    // Add tracked files that need to disappear as reversible rename operations.
    if (failure.empty()) {
        for (const auto& item : currentTree) {
            const std::string& f = item.first;
            if (targetFiles.count(f) || ignore.isIgnored(f)) continue;
            FileOperation op;
            op.destination = rootPath / fs::path(f);
            operations.push_back(op);
        }
    }

    // Stage the new HEAD file in .minigit before applying the worktree plan.
    FileOperation headOperation;
    if (failure.empty()) {
        headOperation.destination = headPath;
        headOperation.temporary = uniqueSiblingPath(headPath);
        if (headOperation.temporary.empty() || !stageBytes(headOperation.temporary, newHead + "\n"))
            failure = "could not stage the new HEAD reference";
    }

    if (!failure.empty()) {
        for (auto& op : operations) {
            if (!op.temporary.empty()) {
                std::error_code cleanupError;
                fs::remove(op.temporary, cleanupError);
            }
        }
        if (!headOperation.temporary.empty()) {
            std::error_code cleanupError;
            fs::remove(headOperation.temporary, cleanupError);
        }
        removeEmptyCreatedDirs(createdDirs);
        r.status = RestoreResult::ERROR;
        r.errorMessage = failure;
        return r;
    }

    // Apply each operation with a backup. Any failure rolls the entire operation
    // list back in reverse order, so checkout never leaves a half-restored tree.
    for (auto& op : operations) {
        if (!moveToBackup(op, failure)) break;
        if (!op.temporary.empty()) {
            std::error_code renameError;
            fs::rename(op.temporary, op.destination, renameError);
            if (renameError) {
                failure = "could not install '" + op.destination.string() + "': " + renameError.message();
                break;
            }
            op.temporary.clear();
            op.installedNew = true;
        }
    }

    if (failure.empty()) {
        if (!moveToBackup(headOperation, failure)) {
            // leave HEAD untouched and roll back the worktree below
        } else {
            std::error_code renameError;
            fs::rename(headOperation.temporary, headOperation.destination, renameError);
            if (renameError) {
                failure = "could not update HEAD: " + renameError.message();
            } else {
                headOperation.temporary.clear();
                headOperation.installedNew = true;
            }
        }
    }

    if (!failure.empty()) {
        std::string rollbackError;
        bool restored = rollbackOperations(operations, rollbackError);
        std::vector<FileOperation> headRollback(1, headOperation);
        if (!rollbackOperations(headRollback, rollbackError)) restored = false;
        removeEmptyCreatedDirs(createdDirs);
        r.status = RestoreResult::ERROR;
        r.filesRestored = 0;
        r.filesRemoved = 0;
        r.errorMessage = failure;
        if (!restored) r.errorMessage += "; rollback incomplete: " + rollbackError;
        return r;
    }

    // The transaction is committed. Remove backups and prune directories left
    // empty by tracked-file deletions; ignored files are never touched.
    for (const auto& op : operations) {
        if (op.moveOriginal) {
            std::error_code cleanupError;
            fs::remove(op.backup, cleanupError);
        }
        if (!op.temporary.empty()) {
            std::error_code cleanupError;
            fs::remove(op.temporary, cleanupError);
        }
    }
    if (headOperation.moveOriginal) {
        std::error_code cleanupError;
        fs::remove(headOperation.backup, cleanupError);
    }

    for (const auto& item : currentTree) {
        const std::string& f = item.first;
        if (targetFiles.count(f) || ignore.isIgnored(f)) continue;
        pruneEmptyDirs((rootPath / fs::path(f)).parent_path(), rootPath);
        ++r.filesRemoved;
    }
    r.filesRestored = static_cast<int>(targetContent.size());

    // The staged index was verified empty before the transaction, so it needs
    // no rewrite here (which could otherwise fail after HEAD has moved).

    r.status = RestoreResult::SUCCESS;
    r.commitId = targetId;
    r.message = target.message;
    return r;
}

RestoreResult checkoutCommit(const std::string& commitIdOrPrefix, const std::string& root) {
    RestoreResult r;
    std::error_code ec;
    if (!fs::is_directory(fs::path(root) / ".minigit", ec)) {
        r.status = RestoreResult::NOT_A_REPO;
        r.errorMessage = "not a minigit repository (run 'minigit init')";
        return r;
    }
    if (commitIdOrPrefix.empty()) {
        r.status = RestoreResult::INVALID_ARGUMENTS;
        r.errorMessage = "commit id cannot be empty";
        return r;
    }

    if (commitIdOrPrefix.size() < 4) {
        r.status = RestoreResult::INVALID_ARGUMENTS;
        r.errorMessage = "commit id '" + commitIdOrPrefix + "' is too short (use at least 4 characters)";
        return r;
    }
    bool ambiguous = false;
    const std::string fullId = resolveCommitId(root, commitIdOrPrefix, &ambiguous);
    if (ambiguous) {
        r.status = RestoreResult::AMBIGUOUS_ID;
        r.errorMessage = "commit id '" + commitIdOrPrefix + "' matches more than one commit";
        return r;
    }
    if (fullId.empty()) {
        r.status = RestoreResult::COMMIT_NOT_FOUND;
        r.errorMessage = "no commit matches '" + commitIdOrPrefix + "'";
        if (isValidBranchName(commitIdOrPrefix) &&
            fs::is_regular_file(fs::path(root) / ".minigit" / "refs" / "heads" / commitIdOrPrefix, ec))
            r.errorMessage += " ('" + commitIdOrPrefix + "' is a branch: use 'minigit checkout " + commitIdOrPrefix + "')";
        return r;
    }

    return restoreWorkingTree(root, fullId, fullId);  // HEAD becomes the raw commit id (detached)
}

}  // namespace minigit
