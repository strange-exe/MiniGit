#pragma once
// Shared repository utilities for DAG branching, rollback, and commit resolution.
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "FileUtils.h"
#include "PathUtils.h"
#include "StagingArea.h"

namespace minigit {

inline std::string trim(const std::string& s) {
    std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

struct CommitMetadata {
    std::string commitId;
    std::string parentId;
    std::vector<std::string> parents;
    std::string timestamp;
    std::string author;
    std::string message;
    std::vector<std::string> files;    // files whose snapshot is stored in this commit
    std::vector<std::string> deleted;  // tracked files this commit removes
};

// Reads .minigit/objects/<id>/metadata. Returns false if the file is missing,
// incomplete (a header line or part of the file list is absent) or lists an
// unsafe path, so a truncated or tampered commit is never treated as valid.
//
// Format (written by commitRepo):
//   commit <id> / parent <id|none> / timestamp <unix> / author <name> /
//   message <one line> / files <N> + N path lines / [deleted <M> + M path lines]
// The file lists are read by position, so a path that happens to start with a
// keyword ("message 1.txt") can never be mistaken for a header line.
inline bool readCommitMeta(const std::string& root, const std::string& id, CommitMetadata& out) {
    namespace fs = std::filesystem;
    fs::path metaPath = fs::path(root) / ".minigit" / "objects" / id / "metadata";
    std::ifstream in(metaPath.string());
    if (!in) return false;

    out.commitId = id;
    out.parentId.clear();
    out.parents.clear();
    out.timestamp.clear();
    out.author.clear();
    out.message.clear();
    out.files.clear();
    out.deleted.clear();

    enum Section { HEADER, FILES, DELETED } section = HEADER;
    long remaining = 0;
    bool sawParent = false, sawTimestamp = false, sawMessage = false, sawFiles = false;

    auto parseCount = [](const std::string& s, long& n) {
        std::string t = trim(s);
        if (t.empty() || t.size() > 9 || !std::all_of(t.begin(), t.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
            return false;
        n = std::atol(t.c_str());
        return true;
    };

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (section != HEADER) {
            if (!isSafeRepoPath(line)) return false;
            (section == FILES ? out.files : out.deleted).push_back(line);
            if (--remaining == 0) section = HEADER;
            continue;
        }

        if (line.rfind("parent ", 0) == 0) {
            sawParent = true;
            std::string p = line.substr(7);
            if (p != "none" && !p.empty()) {
                out.parentId = p;
                out.parents.push_back(p);
            }
        } else if (line.rfind("parents ", 0) == 0) {
            sawParent = true;
            std::istringstream iss(line.substr(8));
            std::string p;
            while (iss >> p) {
                if (p != "none" && !p.empty()) out.parents.push_back(p);
            }
            if (out.parentId.empty() && !out.parents.empty()) out.parentId = out.parents[0];
        } else if (line.rfind("timestamp ", 0) == 0) {
            sawTimestamp = true;
            out.timestamp = line.substr(10);
        } else if (line.rfind("author ", 0) == 0) {
            out.author = line.substr(7);
        } else if (line.rfind("message ", 0) == 0) {
            sawMessage = true;
            out.message = line.substr(8);
        } else if (line.rfind("files ", 0) == 0) {
            if (sawFiles || !parseCount(line.substr(6), remaining)) return false;
            sawFiles = true;
            if (remaining > 0) section = FILES;
        } else if (line.rfind("deleted ", 0) == 0) {
            if (!parseCount(line.substr(8), remaining)) return false;
            if (remaining > 0) section = DELETED;
        } else if (line.rfind("commit ", 0) == 0) {
            out.commitId = line.substr(7);
        }
    }
    return sawParent && sawTimestamp && sawMessage && sawFiles && section == HEADER;
}

// "refs/heads/<name>" with no ".." and nothing before or after: the only kind of
// reference HEAD may point to. Anything else is treated as damage, never followed.
inline bool isSafeHeadRef(const std::string& ref) {
    const std::string prefix = "refs/heads/";
    if (ref.size() <= prefix.size() || ref.compare(0, prefix.size(), prefix) != 0) return false;
    if (ref.find("..") != std::string::npos || ref.find('\\') != std::string::npos) return false;
    return ref.back() != '/';
}

struct HeadState {
    bool valid = false;
    bool detached = false;  // HEAD holds a commit id ('checkout <commit-id>'), not a branch
    std::string branch;     // active branch name, "" when detached
    std::string commitId;   // tip of the active branch (or the detached commit), "" if no commits yet
    std::string error;      // why `valid` is false
};

// A full 40-character lowercase hex commit id.
inline bool isFullCommitId(const std::string& s) {
    return s.size() == 40 && std::all_of(s.begin(), s.end(), [](unsigned char c) {
               return std::isdigit(c) || (c >= 'a' && c <= 'f');
           });
}

// The message every branch-moving command gives while HEAD is detached.
inline std::string detachedHeadError(const HeadState& h) {
    return "HEAD is detached at " + h.commitId.substr(0, 7) +
           " (an old commit is checked out); run 'minigit checkout <branch>' to go back, or "
           "'minigit branch <name>' and 'minigit checkout <name>' to continue from here";
}

// Reads .minigit/HEAD and the branch tip it points to. MiniGit writes either
// "ref: refs/heads/<branch>" or, after 'checkout <commit-id>', the full id of an existing
// commit (detached HEAD). Anything else means the repository is damaged and commands
// must stop instead of guessing.
inline HeadState readHeadState(const std::string& root) {
    namespace fs = std::filesystem;
    HeadState h;
    std::ifstream in((fs::path(root) / ".minigit" / "HEAD").string());
    if (!in) {
        h.error = ".minigit/HEAD is missing (run 'minigit init' to repair it)";
        return h;
    }
    std::string line;
    std::getline(in, line);
    line = trim(line);
    if (isFullCommitId(line)) {
        std::error_code ec;
        if (!fs::is_regular_file(fs::path(root) / ".minigit" / "objects" / line / "metadata", ec)) {
            h.error = ".minigit/HEAD is detached at commit '" + line + "', which is missing (repository is damaged)";
            return h;
        }
        h.detached = true;
        h.commitId = line;
        h.valid = true;
        return h;
    }
    if (line.rfind("ref:", 0) != 0 || !isSafeHeadRef(trim(line.substr(4)))) {
        h.error = ".minigit/HEAD is damaged (expected 'ref: refs/heads/<branch>' or a commit id)";
        return h;
    }
    std::string ref = trim(line.substr(4));
    h.branch = ref.substr(std::string("refs/heads/").size());
    std::ifstream refIn((fs::path(root) / ".minigit" / ref).string());
    if (refIn) {
        std::string cid;
        std::getline(refIn, cid);
        h.commitId = trim(cid);
    }
    h.valid = true;
    return h;
}

// Reads active branch name from HEAD (e.g. "main" or "feature")
inline std::string getHEADBranch(const std::string& root) {
    namespace fs = std::filesystem;
    fs::path headFile = fs::path(root) / ".minigit" / "HEAD";
    std::ifstream in(headFile.string());
    if (!in) return "";
    std::string line;
    std::getline(in, line);
    line = trim(line);
    if (line.rfind("ref: refs/heads/", 0) == 0) {
        return trim(line.substr(16));
    }
    if (line.rfind("ref:", 0) == 0) {
        std::string ref = trim(line.substr(4));
        std::size_t slash = ref.rfind('/');
        return (slash != std::string::npos) ? ref.substr(slash + 1) : ref;
    }
    return ""; // detached HEAD or raw commit ID
}

// Sets active branch in HEAD
inline bool setHEADBranch(const std::string& root, const std::string& branchName) {
    namespace fs = std::filesystem;
    return writeFileAtomic((fs::path(root) / ".minigit" / "HEAD").string(), "ref: refs/heads/" + branchName + "\n");
}

// Reads latest commit ID from HEAD / branch reference
inline std::string getHEADCommitId(const std::string& root) {
    namespace fs = std::filesystem;
    fs::path headFile = fs::path(root) / ".minigit" / "HEAD";
    std::ifstream in(headFile.string());
    if (!in) return "";
    std::string line;
    std::getline(in, line);
    line = trim(line);

    if (line.rfind("ref:", 0) == 0) {
        std::string refRelPath = trim(line.substr(4));
        if (!isSafeHeadRef(refRelPath)) return "";
        fs::path refPath = fs::path(root) / ".minigit" / refRelPath;
        std::ifstream refIn(refPath.string());
        if (!refIn) return "";
        std::string commitId;
        std::getline(refIn, commitId);
        return trim(commitId);
    }
    return line;
}

// Moves the branch HEAD points to onto `commitId` ("" = branch with no commits).
// The reference file is replaced atomically. Returns false, changing nothing, if
// HEAD is missing or damaged.
inline bool setHEADCommitId(const std::string& root, const std::string& commitId) {
    namespace fs = std::filesystem;
    HeadState h = readHeadState(root);
    if (!h.valid || h.detached) return false;  // no branch to move
    fs::path refPath = fs::path(root) / ".minigit" / "refs" / "heads" / h.branch;
    std::error_code ec;
    fs::create_directories(refPath.parent_path(), ec);
    return writeFileAtomic(refPath.string(), commitId.empty() ? std::string() : commitId + "\n");
}

// Resolves a full 40-char commit ID or a unique prefix (at least 4 hex digits).
// Returns "" if nothing matches; `*ambiguous` tells "several match" from "none".
inline std::string resolveCommitId(const std::string& root, const std::string& idOrPrefix, bool* ambiguous = nullptr) {
    namespace fs = std::filesystem;
    if (ambiguous) *ambiguous = false;
    std::error_code ec;
    fs::path objDir = fs::path(root) / ".minigit" / "objects";
    if (!fs::is_directory(objDir, ec) || idOrPrefix.size() < 4 || idOrPrefix.size() > 40) return "";

    std::string key = idOrPrefix;
    for (char& c : key) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return "";  // also blocks "../" tricks
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (key.size() == 40 && fs::exists(objDir / key / "metadata", ec)) return key;

    std::vector<std::string> matches;
    for (const auto& entry : fs::directory_iterator(objDir, ec)) {
        if (entry.is_directory(ec)) {
            std::string name = entry.path().filename().string();
            if (name.rfind(key, 0) == 0 && fs::exists(entry.path() / "metadata", ec)) matches.push_back(name);
        }
    }

    if (matches.size() == 1) return matches[0];
    if (matches.size() > 1 && ambiguous) *ambiguous = true;
    return "";  // not found or ambiguous
}

// Reconstructs all tracked files at a given commit by walking backwards through
// parent history. A commit overrides its ancestors for the files it stores, and a
// path in its "deleted" list stops being tracked. Returns map: relPath -> snapshotFilePath.
//
// If the chain is broken (a commit or snapshot file is missing, or the parents form a
// cycle) `*complete` becomes false and `*problem` says why: callers that are about to
// change the working tree must stop, because the returned map is then only partial.
inline std::map<std::string, std::string> getTrackedFilesAtCommit(
    const std::string& root, const std::string& commitId, bool* complete = nullptr, std::string* problem = nullptr) {
    namespace fs = std::filesystem;
    std::map<std::string, std::string> tracked;
    if (complete) *complete = true;
    auto broken = [&](const std::string& why) {
        if (complete) *complete = false;
        if (problem && problem->empty()) *problem = why;
    };
    if (commitId.empty()) return tracked;

    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> decided;  // paths already settled by a newer commit
    std::string cur = commitId;

    while (!cur.empty() && cur != "none") {
        if (!visited.insert(cur).second) {
            broken("commit history contains a cycle at '" + cur + "'");
            break;
        }
        CommitMetadata meta;
        if (!readCommitMeta(root, cur, meta)) {
            broken("commit '" + cur + "' is missing or damaged");
            break;
        }

        for (const auto& f : meta.deleted) decided.insert(f);

        fs::path snapDir = fs::path(root) / ".minigit" / "objects" / cur / "snapshot";
        for (const auto& f : meta.files) {
            if (!decided.insert(f).second) continue;
            fs::path snapFile = snapDir / f;
            std::error_code ec;
            if (fs::exists(snapFile, ec)) {
                tracked[f] = snapFile.string();
            } else {
                broken("snapshot of '" + f + "' is missing in commit '" + cur + "'");
            }
        }
        cur = meta.parentId;
    }
    return tracked;
}

// Checks if working tree or staging index has uncommitted changes that would be lost
inline bool checkUncommittedChanges(
    const std::string& root,
    const std::string& currentCommitId,
    std::string& outError) {
    namespace fs = std::filesystem;
    std::error_code ec;

    // 1. Check Staging Area (staged files and staged deletions)
    StagingArea sa(root);
    if (sa.size() > 0) {
        outError = "you have staged changes in the index";
        return true;
    }

    if (currentCommitId.empty()) return false;

    // 2. Check tracked files against disk
    bool complete = true;
    std::string problem;
    auto tracked = getTrackedFilesAtCommit(root, currentCommitId, &complete, &problem);
    if (!complete) {
        outError = "repository history is damaged: " + problem;
        return true;
    }
    for (const auto& kv : tracked) {
        const std::string& relPath = kv.first;
        const std::string& snapPath = kv.second;
        fs::path diskPath = fs::path(root) / relPath;

        if (!fs::exists(diskPath, ec)) {
            outError = "tracked file '" + relPath + "' was deleted in working tree";
            return true;
        }

        auto diskSize = fs::file_size(diskPath, ec);
        auto snapSize = fs::file_size(snapPath, ec);
        if (diskSize != snapSize) {
            outError = "working tree modifications in '" + relPath + "'";
            return true;
        }

        std::ifstream fDisk(diskPath.string(), std::ios::binary);
        std::ifstream fSnap(snapPath, std::ios::binary);
        if (!fDisk || !fSnap) {
            outError = "cannot read '" + relPath + "'";
            return true;
        }
        std::string sDisk((std::istreambuf_iterator<char>(fDisk)), std::istreambuf_iterator<char>());
        std::string sSnap((std::istreambuf_iterator<char>(fSnap)), std::istreambuf_iterator<char>());
        if (sDisk != sSnap) {
            outError = "working tree modifications in '" + relPath + "'";
            return true;
        }
    }

    return false;
}

// Removes a tracked file and recursively cleans up any empty parent directories
// (never the repository root itself).
inline void removeFileAndEmptyDirs(const std::string& root, const std::string& relPath) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!isSafeRepoPath(relPath)) return;  // never delete outside the working tree
    fs::path fullPath = fs::path(root) / relPath;
    fs::remove(fullPath, ec);

    fs::path parent = fullPath.parent_path();
    while (!parent.empty() && fs::is_directory(parent, ec)) {
        std::error_code same;
        if (fs::equivalent(parent, fs::path(root), same)) break;
        if (!fs::is_empty(parent, ec)) break;
        fs::remove(parent, ec);
        parent = parent.parent_path();
    }
}

}  // namespace minigit
