#include "BranchCommand.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "CommitNode.h"
#include "FileUtils.h"
#include "RepoUtils.h"
#include "StagingArea.h"

namespace fs = std::filesystem;

namespace minigit {

// Windows treats CON, PRN, AUX, NUL, COM1-9 and LPT1-9 (with or without an extension)
// as devices, so a branch file with such a name cannot be created or deleted normally.
static bool isReservedDeviceName(std::string part) {
    std::size_t dot = part.find('.');
    if (dot != std::string::npos) part.erase(dot);
    for (char& c : part) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (part == "con" || part == "prn" || part == "aux" || part == "nul") return true;
    return part.size() == 4 && (part.compare(0, 3, "com") == 0 || part.compare(0, 3, "lpt") == 0) &&
           part[3] >= '1' && part[3] <= '9';
}

bool isValidBranchName(const std::string& name) {
    if (name.empty()) return false;
    if (name[0] == '-' || name[0] == '/' || name[0] == '.') return false;
    if (name == "HEAD") return false;

    const std::string invalidChars = " \t\r\n~^:?*[\\]\"<>|";
    if (name.find_first_of(invalidChars) != std::string::npos) return false;
    for (char c : name)
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) return false;
    if (name.find("..") != std::string::npos || name.find("//") != std::string::npos) return false;
    if (name.find("@{") != std::string::npos) return false;
    if (name.back() == '/' || name.back() == '.') return false;

    // "a/b" is a branch file inside a folder: every part must itself be a safe file name.
    std::size_t start = 0;
    while (start <= name.size()) {
        std::size_t slash = name.find('/', start);
        std::string part = name.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (part.empty() || part[0] == '.') return false;
        if (part.size() > 5 && part.compare(part.size() - 5, 5, ".lock") == 0) return false;
        if (isReservedDeviceName(part)) return false;
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return true;
}

std::string getCurrentBranch(const std::string& root) {
    std::string b = getHEADBranch(root);
    return b.empty() ? "main" : b;
}

std::string getBranchCommitId(const std::string& branch, const std::string& root) {
    if (!isValidBranchName(branch)) return "";
    fs::path branchFile = fs::path(root) / ".minigit" / "refs" / "heads" / branch;
    std::ifstream in(branchFile.string());
    if (!in) return "";
    std::string cid;
    std::getline(in, cid);
    return trim(cid);
}

static std::string shortOf(const std::string& id) { return id.size() >= 7 ? id.substr(0, 7) : id; }

// Removes now-empty folders left behind by a nested branch name ("feat/login"), never refs/heads itself.
static void pruneEmptyBranchDirs(const fs::path& headsDir, fs::path dir) {
    std::error_code ec;
    while (!dir.empty() && dir != headsDir && fs::is_directory(dir, ec) && fs::is_empty(dir, ec)) {
        fs::remove(dir, ec);
        dir = dir.parent_path();
    }
}

BranchResult listBranches(const std::string& root) {
    BranchResult r;
    std::error_code ec;
    fs::path gitDir = fs::path(root) / ".minigit";
    if (!fs::is_directory(gitDir, ec)) {
        r.status = BranchResult::NOT_A_REPO;
        r.errorMessage = "not a minigit repository (run 'minigit init')";
        return r;
    }

    HeadState head = readHeadState(root);
    if (!head.valid) {
        r.status = BranchResult::ERROR;
        r.errorMessage = head.error;
        return r;
    }
    const std::string& currBranch = head.branch;
    fs::path headsDir = gitDir / "refs" / "heads";

    std::vector<BranchInfo> list;
    bool foundCurrent = false;

    if (fs::is_directory(headsDir, ec)) {
        // Recursive: a branch named "feat/login" lives in refs/heads/feat/login.
        fs::recursive_directory_iterator it(headsDir, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            std::error_code e2;
            if (!it->is_regular_file(e2)) continue;
            std::string bName = it->path().lexically_relative(headsDir).generic_string();
            if (!isValidBranchName(bName)) continue;  // skips ".name.tmp" leftovers and stray files

            std::ifstream in(it->path().string());
            std::string cid;
            std::getline(in, cid);
            cid = trim(cid);

            BranchInfo info;
            info.name = bName;
            info.commitId = cid;
            info.shortId = shortOf(cid);
            info.isActive = (bName == currBranch);
            if (info.isActive) foundCurrent = true;

            if (!cid.empty()) {
                CommitMetadata meta;
                if (readCommitMeta(root, cid, meta)) {
                    info.commitMessage = meta.message;
                }
            }
            list.push_back(info);
        }
    }

    // In a fresh repository before any commit, refs/heads/ may be empty but HEAD points to main
    if (!foundCurrent) {
        BranchInfo info;
        info.name = currBranch;
        info.isActive = true;
        list.push_back(info);
    }

    std::sort(list.begin(), list.end(), [](const BranchInfo& a, const BranchInfo& b) {
        return a.name < b.name;
    });

    r.status = BranchResult::LIST_SUCCESS;
    r.branches = std::move(list);
    return r;
}

BranchResult createBranch(const std::string& name, const std::string& root) {
    BranchResult r;
    std::error_code ec;
    fs::path gitDir = fs::path(root) / ".minigit";
    if (!fs::is_directory(gitDir, ec)) {
        r.status = BranchResult::NOT_A_REPO;
        r.errorMessage = "not a minigit repository (run 'minigit init')";
        return r;
    }

    r.branchName = name;
    if (!isValidBranchName(name)) {
        r.status = BranchResult::INVALID_NAME;
        r.errorMessage = "'" + name + "' is not a valid branch name";
        return r;
    }

    HeadState head = readHeadState(root);
    if (!head.valid) {
        r.status = BranchResult::ERROR;
        r.errorMessage = head.error;
        return r;
    }
    const std::string headCommitId = head.commitId;
    if (headCommitId.empty()) {
        r.status = BranchResult::NO_COMMITS;
        r.errorMessage = "cannot create branch: repository has no commits";
        return r;
    }
    CommitMetadata tip;
    if (!readCommitMeta(root, headCommitId, tip)) {
        r.status = BranchResult::ERROR;
        r.errorMessage = "branch '" + head.branch + "' points to commit '" + headCommitId +
                         "' which is missing or damaged (repository is damaged)";
        return r;
    }

    fs::path headsDir = gitDir / "refs" / "heads";
    fs::path branchFile = headsDir / name;

    // A branch file and a branch folder of the same name cannot coexist ("feat" vs "feat/login").
    fs::path partial = headsDir;
    std::size_t start = 0;
    while (true) {
        std::size_t slash = name.find('/', start);
        if (slash == std::string::npos) break;
        partial /= name.substr(start, slash - start);
        if (fs::is_regular_file(partial, ec)) {
            r.status = BranchResult::ALREADY_EXISTS;
            r.errorMessage = "cannot create '" + name + "': it conflicts with the existing branch '" +
                             partial.lexically_relative(headsDir).generic_string() + "'";
            return r;
        }
        start = slash + 1;
    }
    if (fs::is_directory(branchFile, ec)) {
        if (fs::is_empty(branchFile, ec)) {
            fs::remove(branchFile, ec);  // leftover folder of a deleted nested branch
        } else {
            r.status = BranchResult::ALREADY_EXISTS;
            r.errorMessage = "cannot create '" + name + "': existing branches live under '" + name + "/'";
            return r;
        }
    }
    if (fs::exists(branchFile, ec)) {
        r.status = BranchResult::ALREADY_EXISTS;
        r.errorMessage = "a branch named '" + name + "' already exists";
        return r;
    }

    fs::create_directories(branchFile.parent_path(), ec);
    if (!writeFileAtomic(branchFile.string(), headCommitId + "\n")) {
        r.status = BranchResult::ERROR;
        r.errorMessage = "could not write branch reference '" + branchFile.string() + "'";
        return r;
    }

    r.status = BranchResult::CREATE_SUCCESS;
    r.commitId = headCommitId;
    r.shortId = shortOf(headCommitId);
    return r;
}

BranchResult deleteBranch(const std::string& name, const std::string& root) {
    BranchResult r;
    std::error_code ec;
    fs::path gitDir = fs::path(root) / ".minigit";
    if (!fs::is_directory(gitDir, ec)) {
        r.status = BranchResult::NOT_A_REPO;
        r.errorMessage = "not a minigit repository (run 'minigit init')";
        return r;
    }

    r.branchName = name;
    // The name becomes a path below refs/heads: "../../HEAD" must never reach other repository files.
    if (!isValidBranchName(name)) {
        r.status = BranchResult::INVALID_NAME;
        r.errorMessage = "'" + name + "' is not a valid branch name";
        return r;
    }

    HeadState head = readHeadState(root);
    if (!head.valid) {
        r.status = BranchResult::ERROR;
        r.errorMessage = head.error;
        return r;
    }
    if (name == head.branch) {
        r.status = BranchResult::CANNOT_DELETE_ACTIVE;
        r.errorMessage = "cannot delete the currently active branch '" + name + "'";
        return r;
    }

    fs::path headsDir = gitDir / "refs" / "heads";
    fs::path branchFile = headsDir / name;
    if (!fs::is_regular_file(branchFile, ec)) {
        r.status = BranchResult::NOT_FOUND;
        r.errorMessage = "branch '" + name + "' not found";
        return r;
    }

    std::string tip = getBranchCommitId(name, root);
    if (!fs::remove(branchFile, ec)) {
        r.status = BranchResult::ERROR;
        r.errorMessage = "could not delete branch reference '" + branchFile.string() + "'";
        return r;
    }
    pruneEmptyBranchDirs(headsDir, branchFile.parent_path());

    r.status = BranchResult::DELETE_SUCCESS;
    r.commitId = tip;
    r.shortId = shortOf(tip);
    return r;
}

CheckoutResult checkoutBranch(const std::string& name, bool force, const std::string& root) {
    CheckoutResult r;
    std::error_code ec;
    fs::path gitDir = fs::path(root) / ".minigit";
    if (!fs::is_directory(gitDir, ec)) {
        r.status = CheckoutResult::NOT_A_REPO;
        r.errorMessage = "not a minigit repository (run 'minigit init')";
        return r;
    }

    r.branchName = name;
    if (!isValidBranchName(name)) {
        r.status = CheckoutResult::INVALID_NAME;
        r.errorMessage = "'" + name + "' is not a valid branch name";
        return r;
    }

    HeadState head = readHeadState(root);
    if (!head.valid) {
        r.status = CheckoutResult::ERROR;
        r.errorMessage = head.error;
        return r;
    }

    fs::path branchFile = gitDir / "refs" / "heads" / name;
    if (!fs::is_regular_file(branchFile, ec)) {
        r.status = CheckoutResult::NOT_FOUND;
        r.errorMessage = "branch '" + name + "' not found";
        return r;
    }

    if (name == head.branch) {
        r.status = CheckoutResult::ALREADY_ON_BRANCH;
        return r;
    }

    std::string targetCommitId = getBranchCommitId(name, root);
    const std::string currentCommitId = head.commitId;

    // Work out both file sets before touching anything. The target's history must be
    // complete (every commit and snapshot present), otherwise we would build a half-restored tree.
    bool targetComplete = true;
    std::string targetProblem;
    auto targetTracked = getTrackedFilesAtCommit(root, targetCommitId, &targetComplete, &targetProblem);
    if (!targetComplete) {
        r.status = CheckoutResult::ERROR;
        r.errorMessage = "cannot switch to '" + name + "': " + targetProblem;
        return r;
    }
    auto curTracked = getTrackedFilesAtCommit(root, currentCommitId);

    // Safety checks against data loss
    if (!force) {
        std::string err;
        if (checkUncommittedChanges(root, currentCommitId, err)) {
            r.status = CheckoutResult::UNCOMMITTED_CHANGES;
            r.errorMessage = err + " (use --force to discard)";
            return r;
        }

        // Check for untracked file collisions
        for (const auto& kv : targetTracked) {
            if (curTracked.find(kv.first) == curTracked.end()) {
                fs::path diskPath = fs::path(root) / kv.first;
                if (fs::exists(diskPath, ec)) {
                    r.status = CheckoutResult::UNCOMMITTED_CHANGES;
                    r.errorMessage = "untracked file '" + kv.first + "' would be overwritten by checkout";
                    return r;
                }
            }
        }
    }

    // Restore working-tree files to match target branch (an empty target = branch without commits).
    // Remove files that exist in current commit but not in target commit
    for (const auto& kv : curTracked) {
        if (targetTracked.find(kv.first) == targetTracked.end()) {
            removeFileAndEmptyDirs(root, kv.first);
        }
    }

    // Restore target commit snapshot files into working tree
    for (const auto& kv : targetTracked) {
        fs::path dest = fs::path(root) / kv.first;
        fs::create_directories(dest.parent_path(), ec);
        fs::copy_file(kv.second, dest, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            // HEAD is only moved after every file was restored, so it still names the old branch.
            r.status = CheckoutResult::ERROR;
            r.errorMessage = "could not restore file '" + kv.first +
                             "'; the working tree may be partly updated and HEAD was not changed (retry with --force)";
            return r;
        }
    }

    // Clear staging area
    StagingArea sa(root);
    sa.clear();

    // Update HEAD to target branch
    if (!setHEADBranch(root, name)) {
        r.status = CheckoutResult::ERROR;
        r.errorMessage = "could not update HEAD reference";
        return r;
    }

    r.status = CheckoutResult::SUCCESS;
    r.commitId = targetCommitId;
    r.shortId = shortOf(targetCommitId);
    return r;
}

}  // namespace minigit
