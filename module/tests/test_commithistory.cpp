// Unit tests for CommitHistory: the two std::deque stacks behind undo and redo,
// the history limit, and the .minigit/history file. Needs only an empty
// .minigit directory, so no command code is involved.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "CommitHistory.h"

using namespace minigit;
namespace fs = std::filesystem;

#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::cerr << "FAIL line " << __LINE__ << ": " #c << std::endl;    \
            std::exit(1);                                                     \
        }                                                                     \
    } while (0)

static const std::string kRoot = "test_commithistory_tmp";

static void freshRoot(bool withGitDir = true) {
    std::error_code ec;
    fs::remove_all(kRoot, ec);
    fs::create_directories(withGitDir ? fs::path(kRoot) / ".minigit" : fs::path(kRoot), ec);
}

static HistoryEntry entry(const std::string& id, const std::string& message = "msg") {
    HistoryEntry e;
    e.commitId = id;
    e.parentCommitId = "p-" + id;
    e.message = message;
    e.timestamp = "100";
    return e;
}

static void writeHistoryFile(const std::string& text) {
    std::ofstream((fs::path(kRoot) / ".minigit" / "history").string(), std::ios::binary) << text;
}

static void testStartsEmpty() {
    freshRoot();
    CommitHistory h(kRoot);
    CHECK(!h.canUndo() && !h.canRedo());
    CHECK(h.undoDeque().empty() && h.redoDeque().empty());
    CHECK(h.historyLimit() == DEFAULT_HISTORY_LIMIT);
    CHECK(DEFAULT_HISTORY_LIMIT == 20);

    HistoryEntry out;
    CHECK(!h.undo(out, entry("now")));  // nothing to undo or redo
    CHECK(!h.redo(out, entry("now")));
}

static void testUndoRedoOrder() {
    freshRoot();
    CommitHistory h(kRoot);
    h.recordOperation(entry("c1"));
    h.recordOperation(entry("c2"));
    h.recordOperation(entry("c3"));
    CHECK(h.undoDeque().size() == 3 && h.canUndo() && !h.canRedo());

    // Undo takes the newest entry (pop_back) and parks the current state for redo.
    HistoryEntry restored;
    CHECK(h.undo(restored, entry("state-after-c3")));
    CHECK(restored.commitId == "c3");
    CHECK(h.undoDeque().size() == 2 && h.undoDeque().back().commitId == "c2");
    CHECK(h.redoDeque().size() == 1 && h.redoDeque().back().commitId == "state-after-c3");

    CHECK(h.undo(restored, entry("state-after-c2")));
    CHECK(restored.commitId == "c2");
    CHECK(h.redoDeque().size() == 2);

    // Redo replays in the opposite order.
    CHECK(h.redo(restored, entry("state-after-undo")));
    CHECK(restored.commitId == "state-after-c2");
    CHECK(h.redoDeque().size() == 1);
    CHECK(h.undoDeque().back().commitId == "state-after-undo");

    CHECK(h.redo(restored, entry("again")));
    CHECK(restored.commitId == "state-after-c3");
    CHECK(!h.canRedo());
}

static void testNewOperationClearsRedo() {
    freshRoot();
    CommitHistory h(kRoot);
    h.recordOperation(entry("c1"));
    h.recordOperation(entry("c2"));
    HistoryEntry restored;
    CHECK(h.undo(restored, entry("s")));
    CHECK(h.canRedo());

    h.recordOperation(entry("c3"));  // new work: the undone future is gone
    CHECK(!h.canRedo());
    CHECK(h.undoDeque().size() == 2 && h.undoDeque().back().commitId == "c3");
}

static void testHistoryLimit() {
    freshRoot();
    // Not named `small`: the Windows headers define `small` as a macro for `char`.
    CommitHistory limited(kRoot, 3);
    CHECK(limited.historyLimit() == 3);
    for (int i = 1; i <= 5; ++i) limited.recordOperation(entry("c" + std::to_string(i)));
    CHECK(limited.undoDeque().size() == 3);
    // The oldest entries leave from the front (pop_front); the newest stay.
    CHECK(limited.undoDeque().front().commitId == "c3");
    CHECK(limited.undoDeque().back().commitId == "c5");

    freshRoot();
    CommitHistory big(kRoot);
    for (int i = 1; i <= 25; ++i) big.recordOperation(entry("c" + std::to_string(i)));
    CHECK(big.undoDeque().size() == 20);
    CHECK(big.undoDeque().front().commitId == "c6");
    CHECK(big.undoDeque().back().commitId == "c25");
}

static void testPersistence() {
    freshRoot();
    {
        CommitHistory h(kRoot);
        HistoryEntry a = entry("c1", "first commit");
        a.stagedFiles = {"a.txt", "dir/b.txt", "deleted:old.txt"};
        a.branch = "feature/x";
        a.expectedHead = "c1";
        h.recordOperation(a);

        HistoryEntry empty;  // every field empty: stored as "-" and read back empty
        h.recordOperation(empty);

        HistoryEntry restored;
        CHECK(h.undo(restored, entry("parked", "")));  // the redo side is saved as well
    }

    CommitHistory loaded(kRoot);
    CHECK(loaded.undoDeque().size() == 1);
    CHECK(loaded.redoDeque().size() == 1);

    const HistoryEntry& a = loaded.undoDeque().front();
    CHECK(a.commitId == "c1" && a.parentCommitId == "p-c1" && a.timestamp == "100");
    CHECK(a.message == "first commit");
    CHECK(a.stagedFiles.size() == 3 && a.stagedFiles[2] == "deleted:old.txt");
    CHECK(a.branch == "feature/x" && a.expectedHead == "c1");

    const HistoryEntry& r = loaded.redoDeque().front();
    CHECK(r.commitId == "parked" && r.message.empty());
    CHECK(r.branch.empty() && r.expectedHead.empty());
}

static void testLegacyFileWithoutGuardFields() {
    freshRoot();
    writeHistoryFile("UNDO_COUNT 1\nc1 - 100 1\nold message\na.txt\nREDO_COUNT 0\n");
    CommitHistory h(kRoot);
    CHECK(h.undoDeque().size() == 1);
    const HistoryEntry& e = h.undoDeque().front();
    CHECK(e.commitId == "c1" && e.parentCommitId.empty() && e.timestamp == "100");
    CHECK(e.message == "old message");
    CHECK(e.stagedFiles.size() == 1 && e.stagedFiles[0] == "a.txt");
    CHECK(e.branch.empty() && e.expectedHead.empty());  // such entries are checked by parentage
}

static void testDamagedFileIsDiscarded() {
    freshRoot();
    writeHistoryFile("UNDO_COUNT 2\nc1 - 100 0\nmsg\n");  // second entry missing
    CommitHistory h(kRoot);
    CHECK(h.undoDeque().empty() && h.redoDeque().empty());
    CHECK(!h.load());  // reported, and nothing half-trusted is kept
    CHECK(h.undoDeque().empty());

    writeHistoryFile("UNDO_COUNT 1\nnot a valid header\nmsg\nREDO_COUNT 0\n");
    CHECK(!h.load());
    CHECK(h.undoDeque().empty());

    writeHistoryFile("");  // an empty file is simply no history
    CHECK(h.load() && h.undoDeque().empty());
}

static void testSaveNeedsRepository() {
    freshRoot(false);  // folder without .minigit
    CommitHistory h(kRoot);
    h.recordOperation(entry("c1"));  // kept in memory, nothing written
    CHECK(h.undoDeque().size() == 1);
    CHECK(!h.save());
    CHECK(!fs::exists(fs::path(kRoot) / ".minigit"));
}

int main() {
    testStartsEmpty();
    testUndoRedoOrder();
    testNewOperationClearsRedo();
    testHistoryLimit();
    testPersistence();
    testLegacyFileWithoutGuardFields();
    testDamagedFileIsDiscarded();
    testSaveNeedsRepository();

    std::error_code ec;
    fs::remove_all(kRoot, ec);
    std::cout << "test_commithistory: all tests passed\n";
    return 0;
}
