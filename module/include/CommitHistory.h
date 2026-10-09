#pragma once
// Commit History & Operation Management using std::deque (Krish's module).
// Implements undo/redo stack management and history limit enforcement using std::deque.
#include <cstddef>
#include <deque>
#include <string>
#include <vector>

namespace minigit {

constexpr std::size_t DEFAULT_HISTORY_LIMIT = 20;

struct HistoryEntry {
    std::string commitId;                  // Current HEAD commit ID at this snapshot (or "" if uncommitted)
    std::string parentCommitId;            // Parent commit ID
    std::vector<std::string> stagedFiles;  // Snapshot of staged files in index
    std::string message;                   // Commit message if applicable
    std::string timestamp;                 // ISO/Unix timestamp string
};

class CommitHistory {
public:
    explicit CommitHistory(const std::string& rootDir = ".", std::size_t limit = DEFAULT_HISTORY_LIMIT);

    // Save current state before a new commit operation.
    // Invalidates redo history. Enforces history limit using deque.pop_front().
    void recordOperation(const HistoryEntry& entry);

    // Undo operation: pops previous state from undo deque (using deque.pop_back())
    // and pushes current state to redo deque (using deque.push_back()).
    bool undo(HistoryEntry& restoredEntry, const HistoryEntry& currentState);

    // Redo operation: pops state from redo deque (using deque.pop_back())
    // and pushes current state to undo deque (using deque.push_back()).
    bool redo(HistoryEntry& restoredEntry, const HistoryEntry& currentState);

    bool canUndo() const { return !undoDeque_.empty(); }
    bool canRedo() const { return !redoDeque_.empty(); }

    const std::deque<HistoryEntry>& undoDeque() const { return undoDeque_; }
    const std::deque<HistoryEntry>& redoDeque() const { return redoDeque_; }

    std::size_t historyLimit() const { return limit_; }

    // Load/Save history stacks to disk (.minigit/history)
    bool load();
    bool save() const;

private:
    std::string historyFilePath() const;

    std::string root_;
    std::size_t limit_;
    std::deque<HistoryEntry> undoDeque_;  // Deque managing undo history
    std::deque<HistoryEntry> redoDeque_;  // Deque managing redo history
};

}  // namespace minigit
