#include "CommitCommand.h"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "CommitHistory.h"
#include "PathUtils.h"
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
            w[i] = (msg[chunk + i * 4] << 24) | (msg[chunk + i * 4 + 1] << 16) |
                   (msg[chunk + i * 4 + 2] << 8) | (msg[chunk + i * 4 + 3]);
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

static std::string trimString(const std::string& str) {
    std::size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    std::size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

// Reads latest commit ID from HEAD / branch reference
static std::string getHEADCommitId(const std::string& root) {
    fs::path headFile = fs::path(root) / ".minigit" / "HEAD";
    std::ifstream in(headFile.string());
    if (!in) return "";
    std::string line;
    std::getline(in, line);
    line = trimString(line);

    if (line.rfind("ref:", 0) == 0) {
        std::string refRelPath = trimString(line.substr(4));
        fs::path refPath = fs::path(root) / ".minigit" / refRelPath;
        std::ifstream refIn(refPath.string());
        if (!refIn) return "";
        std::string commitId;
        std::getline(refIn, commitId);
        return trimString(commitId);
    }
    return line;
}

// Updates HEAD / branch reference with the given commit ID
static void setHEADCommitId(const std::string& root, const std::string& commitId) {
    fs::path headFile = fs::path(root) / ".minigit" / "HEAD";
    std::ifstream in(headFile.string());
    std::string line;
    if (in) {
        std::getline(in, line);
        line = trimString(line);
    }

    if (line.empty() || line.rfind("ref:", 0) == 0) {
        std::string refRelPath = line.empty() ? "refs/heads/main" : trimString(line.substr(4));
        fs::path refPath = fs::path(root) / ".minigit" / refRelPath;
        std::error_code ec;
        fs::create_directories(refPath.parent_path(), ec);
        std::ofstream out(refPath.string(), std::ios::trunc);
        if (!commitId.empty()) {
            out << commitId << "\n";
        }
    } else {
        std::ofstream out(headFile.string(), std::ios::trunc);
        if (!commitId.empty()) {
            out << commitId << "\n";
        }
    }
}

static std::string currentTimestampString() {
    std::time_t now = std::time(nullptr);
    return std::to_string(static_cast<long long>(now));
}

CommitResult commitRepo(const std::string& message, const std::string& root) {
    CommitResult r;
    std::error_code ec;

    fs::path gitDir = fs::path(root) / ".minigit";
    if (!fs::is_directory(gitDir, ec)) {
        r.status = CommitResult::NOT_A_REPO;
        r.errorMessage = "not a minigit repository (or any of the parent directories)";
        return r;
    }

    std::string cleanMsg = trimString(message);
    if (cleanMsg.empty()) {
        r.status = CommitResult::INVALID_ARGUMENTS;
        r.errorMessage = "commit message cannot be empty";
        return r;
    }

    StagingArea sa(root);
    std::vector<std::string> stagedFiles = sa.getStagedFiles();
    if (stagedFiles.empty()) {
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

    std::string parentId = getHEADCommitId(root);
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
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        canonicalStream << "file " << f << " " << content.size() << "\n" << content << "\n";
        fileContents.push_back({f, content});
    }

    std::string commitId = computeSHA1(canonicalStream.str());

    // Create commit object directory: .minigit/objects/<commitId>/
    fs::path objDir = gitDir / "objects" / commitId;
    fs::create_directories(objDir, ec);

    // Save commit metadata file (.minigit/objects/<commitId>/metadata)
    fs::path metadataFile = objDir / "metadata";
    std::ofstream metaOut(metadataFile.string(), std::ios::trunc);
    metaOut << "commit " << commitId << "\n";
    metaOut << "parent " << (parentId.empty() ? "none" : parentId) << "\n";
    metaOut << "timestamp " << timestamp << "\n";
    metaOut << "author Krish <krish@minigit>\n";
    metaOut << "message " << cleanMsg << "\n";
    metaOut << "files " << stagedFiles.size() << "\n";
    for (const auto& f : stagedFiles) {
        metaOut << f << "\n";
    }
    metaOut.close();

    // Persist staged file content snapshots (.minigit/objects/<commitId>/snapshot/<path>)
    fs::path snapshotDir = objDir / "snapshot";
    for (const auto& item : fileContents) {
        fs::path targetPath = snapshotDir / item.first;
        fs::create_directories(targetPath.parent_path(), ec);
        std::ofstream snapOut(targetPath.string(), std::ios::binary | std::ios::trunc);
        snapOut.write(item.second.data(), item.second.size());
    }

    // Save state into CommitHistory using deque
    CommitHistory history(root);
    HistoryEntry beforeState;
    beforeState.commitId = parentId;
    beforeState.stagedFiles = stagedFiles;
    beforeState.message = cleanMsg;
    beforeState.timestamp = timestamp;
    history.recordOperation(beforeState);

    // Update branch reference / HEAD
    setHEADCommitId(root, commitId);

    // Clear staging index after commit
    sa.clear();

    r.status = CommitResult::SUCCESS;
    r.commitId = commitId;
    r.parentId = parentId;
    r.message = cleanMsg;
    r.filesCommitted = static_cast<int>(stagedFiles.size());
    return r;
}

CommitResult undoCommit(const std::string& root) {
    CommitResult r;
    std::error_code ec;

    fs::path gitDir = fs::path(root) / ".minigit";
    if (!fs::is_directory(gitDir, ec)) {
        r.status = CommitResult::NOT_A_REPO;
        r.errorMessage = "not a minigit repository (or any of the parent directories)";
        return r;
    }

    CommitHistory history(root);
    if (!history.canUndo()) {
        r.status = CommitResult::NOTHING_TO_UNDO;
        r.errorMessage = "nothing to undo";
        return r;
    }

    StagingArea sa(root);
    HistoryEntry currentState;
    currentState.commitId = getHEADCommitId(root);
    currentState.stagedFiles = sa.getStagedFiles();

    HistoryEntry restoredState;
    if (!history.undo(restoredState, currentState)) {
        r.status = CommitResult::NOTHING_TO_UNDO;
        r.errorMessage = "nothing to undo";
        return r;
    }

    setHEADCommitId(root, restoredState.commitId);
    sa.setStagedFiles(restoredState.stagedFiles);

    r.status = CommitResult::SUCCESS;
    r.commitId = restoredState.commitId;
    r.message = "Undo completed";
    return r;
}

CommitResult redoCommit(const std::string& root) {
    CommitResult r;
    std::error_code ec;

    fs::path gitDir = fs::path(root) / ".minigit";
    if (!fs::is_directory(gitDir, ec)) {
        r.status = CommitResult::NOT_A_REPO;
        r.errorMessage = "not a minigit repository (or any of the parent directories)";
        return r;
    }

    CommitHistory history(root);
    if (!history.canRedo()) {
        r.status = CommitResult::NOTHING_TO_REDO;
        r.errorMessage = "nothing to redo";
        return r;
    }

    StagingArea sa(root);
    HistoryEntry currentState;
    currentState.commitId = getHEADCommitId(root);
    currentState.stagedFiles = sa.getStagedFiles();

    HistoryEntry restoredState;
    if (!history.redo(restoredState, currentState)) {
        r.status = CommitResult::NOTHING_TO_REDO;
        r.errorMessage = "nothing to redo";
        return r;
    }

    setHEADCommitId(root, restoredState.commitId);
    sa.setStagedFiles(restoredState.stagedFiles);

    r.status = CommitResult::SUCCESS;
    r.commitId = restoredState.commitId;
    r.message = "Redo completed";
    return r;
}

}  // namespace minigit
