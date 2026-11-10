#include "CommitCommand.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "CommitHistory.h"
#include "FileUtils.h"
#include "PathUtils.h"
#include "RepoUtils.h"
#include "StagingArea.h"

namespace fs = std::filesystem;

namespace minigit {

// Self-contained SHA-1 implementation for deterministic commit ID generation
static std::string computeSHA1(const std::string& input) {
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;

    uint64_t bitLength = static_cast<uint64_t>(input.size()) * 8;
    std::vector<uint8_t> msg(input.begin(), input.end());
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) {
        msg.push_back(0x00);
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<uint8_t>((bitLength >> (i * 8)) & 0xFF));
    }

    for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(msg[chunk + i * 4]) << 24) | (static_cast<uint32_t>(msg[chunk + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 2]) << 8) | (static_cast<uint32_t>(msg[chunk + i * 4 + 3]));
        }
        for (int i = 16; i < 80; ++i) {
            uint32_t val = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
            w[i] = (val << 1) | (val >> 31);
        }

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f = 0, k = 0;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
            e = d;
            d = c;
            c = (b << 30) | (b >> 2);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    std::ostringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(8) << h0 << std::setw(8) << h1
       << std::setw(8) << h2 << std::setw(8) << h3
       << std::setw(8) << h4;
    return ss.str();
}

// Commit metadata and .minigit/history are line-based, so a message must be one
// line: each run of CR/LF becomes a single space ("line1\nline2" -> "line1 line2").
static std::string singleLine(const std::string& msg) {
    std::string out;
    bool inBreak = false;
    for (char c : msg) {
        if (c == '\n' || c == '\r') {
            if (!inBreak) out += ' ';
            inBreak = true;
        } else {
            out += c;
            inBreak = false;
        }
    }
    return out;
}

// Name of the person committing: the OS login name, not a hardcoded team member.
static std::string authorName() {
    for (const char* var : {"MINIGIT_AUTHOR", "USERNAME", "USER"}) {
        const char* v = std::getenv(var);
        if (v && *v) return v;
    }
    return "unknown";
}

static std::string currentTimestampString() {
    std::time_t now = std::time(nullptr);
    return std::to_string(static_cast<long long>(now));
}

static CommitResult failure(CommitResult::Status status, const std::string& message) {
    CommitResult r;
    r.status = status;
    r.errorMessage = message;
    return r;
}

static CommitResult notARepo() {
    return failure(CommitResult::NOT_A_REPO, "not a minigit repository (run 'minigit init')");
}

// HEAD must be a branch reference and that branch must point at a readable commit,
// otherwise a new commit would get a parent that does not exist. Fills `head`.
static bool checkHeadUsable(const std::string& root, HeadState& head, std::string& error) {
    head = readHeadState(root);
    if (!head.valid) {
        error = head.error;
        return false;
    }
    if (head.detached) {  // a commit here would belong to no branch and could be lost
        error = detachedHeadError(head);
        return false;
    }
    if (!head.commitId.empty()) {
        CommitMetadata tip;
        if (!readCommitMeta(root, head.commitId, tip)) {
            error = "branch '" + head.branch + "' points to commit '" + head.commitId +
                    "' which is missing or damaged (repository is damaged)";
            return false;
        }
    }
    return true;
}

// Decides whether the newest undo (or redo) entry still describes the repository: it
// may only be applied on the branch it was recorded on, while that branch still sits
// at the commit the entry expects. Otherwise undo/redo would rewrite a branch to a
// commit from some other line of history. Entries from older versions carry no branch
// and are checked by parentage instead.
static bool entryApplies(const std::string& root, const HeadState& head, const HistoryEntry& entry,
                         bool isUndo, std::string& why) {
    if (!entry.branch.empty()) {
        if (entry.branch != head.branch) {
            why = "the last recorded commit operation belongs to branch '" + entry.branch +
                  "', but the active branch is '" + head.branch + "'";
            return false;
        }
        if (entry.expectedHead != head.commitId) {
            why = "branch '" + head.branch + "' has moved since that operation (checkout or rollback)";
            return false;
        }
        return true;
    }
    CommitMetadata meta;
    if (isUndo) {  // restoring entry.commitId must mean "step back to HEAD's parent"
        bool ok = !head.commitId.empty() && readCommitMeta(root, head.commitId, meta) && meta.parentId == entry.commitId;
        if (!ok) why = "the recorded history does not match the current HEAD";
        return ok;
    }
    // redo: the commit being restored must sit directly on top of HEAD
    bool ok = !entry.commitId.empty() && readCommitMeta(root, entry.commitId, meta) && meta.parentId == head.commitId;
    if (!ok) why = "the recorded history does not match the current HEAD";
    return ok;
}

CommitResult commitRepo(const std::string& message, const std::string& root) {
    CommitResult r;
    std::error_code ec;

    fs::path gitDir = fs::path(root) / ".minigit";
    if (!fs::is_directory(gitDir, ec)) return notARepo();

    std::string cleanMsg = trim(singleLine(message));
    if (cleanMsg.empty()) {
        r.status = CommitResult::INVALID_ARGUMENTS;
        r.errorMessage = "commit message cannot be empty";
        return r;
    }

    HeadState head;
    std::string headError;
    if (!checkHeadUsable(root, head, headError)) return failure(CommitResult::ERROR, headError);
    const std::string parentId = head.commitId;

    StagingArea sa(root);
    std::vector<std::string> stagedFiles = sa.getStagedFiles();
    std::vector<std::string> stagedDeletions = sa.getStagedDeletions();
    if (stagedFiles.empty() && stagedDeletions.empty()) {
        r.status = CommitResult::NOTHING_TO_COMMIT;
        r.errorMessage = "nothing to commit";
        return r;
    }

    // Verify staged files exist on disk before committing
    for (const auto& f : stagedFiles) {
        fs::path p = fs::path(root) / f;
        if (!fs::exists(p, ec) || fs::is_directory(p, ec)) {
            r.status = CommitResult::FILE_NOT_FOUND;
            r.errorMessage = "'" + f + "' does not exist";
            return r;
        }
    }

    // A staged deletion only makes sense for a file the parent commit tracks.
    if (!stagedDeletions.empty()) {
        bool complete = true;
        std::string problem;
        auto tracked = getTrackedFilesAtCommit(root, parentId, &complete, &problem);
        if (!complete) return failure(CommitResult::ERROR, "repository history is damaged: " + problem);
        for (const auto& d : stagedDeletions) {
            if (tracked.find(d) == tracked.end()) {
                return failure(CommitResult::INVALID_ARGUMENTS, "'" + d + "' is staged for deletion but is not tracked");
            }
        }
    }

    std::string timestamp = currentTimestampString();

    // Build canonical content representation for SHA-1 commit ID generation
    std::ostringstream canonicalStream;
    canonicalStream << "parent " << parentId << "\n"
                    << "timestamp " << timestamp << "\n"
                    << "message " << cleanMsg << "\n";

    std::vector<std::pair<std::string, std::string>> fileContents;
    for (const auto& f : stagedFiles) {
        fs::path p = fs::path(root) / f;
        std::ifstream in(p.string(), std::ios::binary);
        if (!in) return failure(CommitResult::ERROR, "cannot read '" + f + "'");
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        canonicalStream << "file " << f << " " << content.size() << "\n" << content << "\n";
        fileContents.push_back({f, content});
    }
    for (const auto& d : stagedDeletions) canonicalStream << "delete " << d << "\n";

    std::string commitId = computeSHA1(canonicalStream.str());

    // Create commit object directory: .minigit/objects/<commitId>/
    // An identical commit (same parent, time, message and content) already on disk is
    // reused as is; a half-written one of our own is removed again on failure.
    fs::path objDir = gitDir / "objects" / commitId;
    bool alreadyStored = fs::exists(objDir / "metadata", ec);
    auto writeFailed = [&](const std::string& what) {
        if (!alreadyStored) fs::remove_all(objDir, ec);  // never leave a half-written commit behind
        return failure(CommitResult::ERROR, "could not write " + what);
    };

    if (!alreadyStored) {
        fs::create_directories(objDir, ec);
        if (!fs::is_directory(objDir, ec)) return writeFailed("commit directory '" + objDir.string() + "'");

        // Persist staged file content snapshots (.minigit/objects/<commitId>/snapshot/<path>)
        fs::path snapshotDir = objDir / "snapshot";
        for (const auto& item : fileContents) {
            fs::path targetPath = snapshotDir / item.first;
            fs::create_directories(targetPath.parent_path(), ec);
            std::ofstream snapOut(targetPath.string(), std::ios::binary | std::ios::trunc);
            snapOut.write(item.second.data(), static_cast<std::streamsize>(item.second.size()));
            snapOut.close();
            if (!snapOut) return writeFailed("snapshot of '" + item.first + "'");
        }

        // The metadata file is written last and atomically: its presence marks the commit
        // as complete, so an interrupted commit is never mistaken for a valid one.
        std::ostringstream meta;
        meta << "commit " << commitId << "\n"
             << "parent " << (parentId.empty() ? "none" : parentId) << "\n"
             << "timestamp " << timestamp << "\n"
             << "author " << authorName() << "\n"
             << "message " << cleanMsg << "\n"
             << "files " << stagedFiles.size() << "\n";
        for (const auto& f : stagedFiles) meta << f << "\n";
        if (!stagedDeletions.empty()) {
            meta << "deleted " << stagedDeletions.size() << "\n";
            for (const auto& d : stagedDeletions) meta << d << "\n";
        }
        if (!writeFileAtomic((objDir / "metadata").string(), meta.str())) return writeFailed("commit metadata");
    }

    // Move the branch to the new commit. Only after that succeeded is the operation
    // recorded for undo and the index cleared, so a failure leaves everything as it was.
    if (!setHEADCommitId(root, commitId)) {
        return failure(CommitResult::ERROR, "could not update branch '" + head.branch + "'");
    }

    CommitHistory history(root);
    HistoryEntry beforeState;
    beforeState.commitId = parentId;
    beforeState.stagedFiles = sa.getStagedEntries();
    beforeState.message = cleanMsg;
    beforeState.timestamp = timestamp;
    beforeState.branch = head.branch;
    beforeState.expectedHead = commitId;
    history.recordOperation(beforeState);

    sa.clear();

    r.status = CommitResult::SUCCESS;
    r.commitId = commitId;
    r.parentId = parentId;
    r.message = cleanMsg;
    r.filesCommitted = static_cast<int>(stagedFiles.size() + stagedDeletions.size());
    r.branch = head.branch;
    return r;
}

CommitResult undoCommit(const std::string& root) {
    std::error_code ec;
    if (!fs::is_directory(fs::path(root) / ".minigit", ec)) return notARepo();

    HeadState head;
    std::string headError;
    if (!checkHeadUsable(root, head, headError)) return failure(CommitResult::ERROR, headError);

    CommitHistory history(root);
    if (!history.canUndo()) return failure(CommitResult::NOTHING_TO_UNDO, "nothing to undo");

    const HistoryEntry top = history.undoDeque().back();
    std::string why;
    if (!entryApplies(root, head, top, true, why)) {
        return failure(CommitResult::NOTHING_TO_UNDO, "nothing to undo here: " + why);
    }
    if (!top.commitId.empty()) {
        CommitMetadata target;
        if (!readCommitMeta(root, top.commitId, target)) {
            return failure(CommitResult::ERROR, "commit '" + top.commitId + "' is missing or damaged");
        }
    }

    // Move the branch first; the history is only touched once that worked.
    if (!setHEADCommitId(root, top.commitId)) {
        return failure(CommitResult::ERROR, "could not update branch '" + head.branch + "'");
    }

    StagingArea sa(root);
    HistoryEntry currentState;
    currentState.commitId = head.commitId;
    currentState.stagedFiles = sa.getStagedEntries();
    currentState.branch = head.branch;
    currentState.expectedHead = top.commitId;  // where the branch sits once this undo is done

    HistoryEntry restoredState;
    history.undo(restoredState, currentState);
    sa.setStagedFiles(restoredState.stagedFiles);

    CommitResult r;
    r.status = CommitResult::SUCCESS;
    r.commitId = restoredState.commitId;
    r.message = "Undo completed";
    r.branch = head.branch;
    return r;
}

CommitResult redoCommit(const std::string& root) {
    std::error_code ec;
    if (!fs::is_directory(fs::path(root) / ".minigit", ec)) return notARepo();

    HeadState head;
    std::string headError;
    if (!checkHeadUsable(root, head, headError)) return failure(CommitResult::ERROR, headError);

    CommitHistory history(root);
    if (!history.canRedo()) return failure(CommitResult::NOTHING_TO_REDO, "nothing to redo");

    const HistoryEntry top = history.redoDeque().back();
    std::string why;
    if (!entryApplies(root, head, top, false, why)) {
        return failure(CommitResult::NOTHING_TO_REDO, "nothing to redo here: " + why);
    }
    if (!top.commitId.empty()) {
        CommitMetadata target;
        if (!readCommitMeta(root, top.commitId, target)) {
            return failure(CommitResult::ERROR, "commit '" + top.commitId + "' is missing or damaged");
        }
    }

    if (!setHEADCommitId(root, top.commitId)) {
        return failure(CommitResult::ERROR, "could not update branch '" + head.branch + "'");
    }

    StagingArea sa(root);
    HistoryEntry currentState;
    currentState.commitId = head.commitId;
    currentState.stagedFiles = sa.getStagedEntries();
    currentState.branch = head.branch;
    currentState.expectedHead = top.commitId;

    HistoryEntry restoredState;
    history.redo(restoredState, currentState);
    sa.setStagedFiles(restoredState.stagedFiles);

    CommitResult r;
    r.status = CommitResult::SUCCESS;
    r.commitId = restoredState.commitId;
    r.message = "Redo completed";
    r.branch = head.branch;
    return r;
}

}  // namespace minigit
