#include "CommitHistory.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "PathUtils.h"

namespace fs = std::filesystem;

namespace minigit {

CommitHistory::CommitHistory(const std::string& rootDir, std::size_t limit)
    : root_(rootDir), limit_(limit) {
    load();
}

std::string CommitHistory::historyFilePath() const {
    return (fs::path(root_) / ".minigit" / "history").string();
}

// Reads one line, dropping a trailing '\r' (CRLF files). False at end of file.
static bool readLine(std::istream& in, std::string& line) {
    if (!std::getline(in, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return true;
}

// Reads `count` entries written by save(). Strictly line-based: the message line
// may be empty (undo/redo snapshots have none), so it must never be skipped as
// whitespace. Returns false if the file ends early.
static bool readEntries(std::istream& in, std::size_t count, std::deque<HistoryEntry>& out) {
    for (std::size_t i = 0; i < count; ++i) {
        std::string header;
        if (!readLine(in, header)) return false;
        std::istringstream hs(header);
        HistoryEntry entry;
        std::size_t stagedCount = 0;
        if (!(hs >> entry.commitId >> entry.parentCommitId >> entry.timestamp >> stagedCount)) return false;
        if (entry.commitId == "-") entry.commitId = "";
        if (entry.parentCommitId == "-") entry.parentCommitId = "";
        if (entry.timestamp == "-") entry.timestamp = "";

        if (!readLine(in, entry.message)) return false;
        for (std::size_t s = 0; s < stagedCount; ++s) {
            std::string file;
            if (!readLine(in, file)) return false;
            if (!file.empty()) entry.stagedFiles.push_back(file);
        }
        out.push_back(entry);
    }
    return true;
}

// Loads persistent operation history from disk (.minigit/history)
bool CommitHistory::load() {
    undoDeque_.clear();
    redoDeque_.clear();

    std::ifstream in(historyFilePath());
    if (!in) return true;  // No history file yet

    std::string line;
    while (readLine(in, line)) {
        std::istringstream ls(line);
        std::string section;
        std::size_t count = 0;
        if (!(ls >> section >> count)) continue;
        bool ok = true;
        if (section == "UNDO_COUNT") ok = readEntries(in, count, undoDeque_);
        else if (section == "REDO_COUNT") ok = readEntries(in, count, redoDeque_);
        if (!ok) {  // truncated / hand-edited file: start with a clean history
            undoDeque_.clear();
            redoDeque_.clear();
            return false;
        }
    }
    return true;
}

// Saves operation history stacks (undoDeque and redoDeque) to disk
bool CommitHistory::save() const {
    std::error_code ec;
    fs::path gitDir = fs::path(root_) / ".minigit";
    if (!fs::is_directory(gitDir, ec)) return false;

    std::ofstream out(historyFilePath(), std::ios::trunc);
    if (!out) return false;

    // Save undoDeque_
    out << "UNDO_COUNT " << undoDeque_.size() << "\n";
    for (const auto& entry : undoDeque_) {
        std::string cid = entry.commitId.empty() ? "-" : entry.commitId;
        std::string pid = entry.parentCommitId.empty() ? "-" : entry.parentCommitId;
        std::string ts = entry.timestamp.empty() ? "-" : entry.timestamp;
        out << cid << " " << pid << " " << ts << " " << entry.stagedFiles.size() << "\n";
        out << entry.message << "\n";
        for (const auto& f : entry.stagedFiles) {
            out << f << "\n";
        }
    }

    // Save redoDeque_
    out << "REDO_COUNT " << redoDeque_.size() << "\n";
    for (const auto& entry : redoDeque_) {
        std::string cid = entry.commitId.empty() ? "-" : entry.commitId;
        std::string pid = entry.parentCommitId.empty() ? "-" : entry.parentCommitId;
        std::string ts = entry.timestamp.empty() ? "-" : entry.timestamp;
        out << cid << " " << pid << " " << ts << " " << entry.stagedFiles.size() << "\n";
        out << entry.message << "\n";
        for (const auto& f : entry.stagedFiles) {
            out << f << "\n";
        }
    }
    return true;
}

// Records a state snapshot before a new commit.
// DEQUE EXPLANATION FOR PBL / VIVA:
// - std::deque is used as a double-ended queue for history management.
// - push_back() adds new state entries to the tail of undoDeque_.
// - pop_front() drops the oldest state from the head when the history limit is exceeded.
// - redoDeque_.clear() invalidates forward redo states when a new branch of work begins.
void CommitHistory::recordOperation(const HistoryEntry& entry) {
    undoDeque_.push_back(entry);
    redoDeque_.clear();

    // History Limit handling using deque's pop_front()
    while (undoDeque_.size() > limit_) {
        undoDeque_.pop_front();
    }
    save();
}

// Undo operation:
// Pushes currentState to redoDeque_ (using push_back).
// Pops recent state from undoDeque_ (using pop_back) into restoredEntry.
bool CommitHistory::undo(HistoryEntry& restoredEntry, const HistoryEntry& currentState) {
    if (undoDeque_.empty()) return false;

    redoDeque_.push_back(currentState);
    restoredEntry = undoDeque_.back();
    undoDeque_.pop_back();
    save();
    return true;
}

// Redo operation:
// Pushes currentState to undoDeque_ (using push_back).
// Pops state from redoDeque_ (using pop_back) into restoredEntry.
bool CommitHistory::redo(HistoryEntry& restoredEntry, const HistoryEntry& currentState) {
    if (redoDeque_.empty()) return false;

    undoDeque_.push_back(currentState);
    restoredEntry = redoDeque_.back();
    redoDeque_.pop_back();
    save();
    return true;
}

}  // namespace minigit
