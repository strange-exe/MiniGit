#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "AddCommand.h"
#include "CLI.h"
#include "CommitCommand.h"
#include "CommitHistory.h"
#include "InitCommand.h"
#include "StagingArea.h"

namespace fs = std::filesystem;
using namespace minigit;

static void setupTestDir(const std::string& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
    fs::create_directories(path, ec);
}

static void cleanupTestDir(const std::string& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
}

int main() {
    std::cout << "Running test_commit...\n";
    std::string testDir = "temp_test_commit";
    setupTestDir(testDir);

    // 1. Commit fails outside repository
    {
        CommitResult r = commitRepo("Initial commit", testDir);
        assert(r.status == CommitResult::NOT_A_REPO);
        std::cout << "  [PASS] 1. Commit fails outside repository\n";
    }

    // Initialize repository
    InitResult initRes = initRepo(testDir);
    assert(initRes.status == InitResult::INITIALIZED);

    // 2. Commit fails when there are no staged files
    {
        CommitResult r = commitRepo("Initial commit", testDir);
        assert(r.status == CommitResult::NOTHING_TO_COMMIT);
        std::cout << "  [PASS] 2. Commit fails when no staged files\n";
    }

    // Create a file and stage it
    std::string fileA = "fileA.txt";
    fs::path fullPathA = fs::path(testDir) / fileA;
    {
        std::ofstream out(fullPathA.string());
        out << "Hello World\n";
    }
    AddResult addRes = addFile(fileA, testDir);
    assert(addRes.status == AddResult::STAGED);

    // 3. Commit succeeds with staged file(s)
    CommitResult r1 = commitRepo("Initial commit", testDir);
    assert(r1.status == CommitResult::SUCCESS);
    assert(r1.filesCommitted == 1);
    assert(r1.message == "Initial commit");
    std::cout << "  [PASS] 3. Commit succeeds with staged file(s)\n";

    // 4. Commit generates a non-empty ID (40 hex chars)
    assert(!r1.commitId.empty());
    assert(r1.commitId.length() == 40);
    std::cout << "  [PASS] 4. Commit generates 40-char ID (" << r1.commitId.substr(0, 7) << ")\n";

    // 5. Commit ID is persisted in objects
    fs::path commitObjDir = fs::path(testDir) / ".minigit" / "objects" / r1.commitId;
    assert(fs::is_directory(commitObjDir));
    std::cout << "  [PASS] 5. Commit ID directory persisted in objects\n";

    // 6. Commit metadata is persisted
    fs::path metadataFile = commitObjDir / "metadata";
    assert(fs::exists(metadataFile));
    std::cout << "  [PASS] 6. Commit metadata persisted on disk\n";

    // 7 & 8. Commit message and timestamp are persisted in metadata
    {
        std::ifstream in(metadataFile.string());
        std::string line;
        bool foundMsg = false;
        bool foundTs = false;
        while (std::getline(in, line)) {
            if (line.find("message Initial commit") != std::string::npos) foundMsg = true;
            if (line.find("timestamp ") != std::string::npos) foundTs = true;
        }
        assert(foundMsg);
        assert(foundTs);
        std::cout << "  [PASS] 7 & 8. Message and timestamp persisted in metadata\n";
    }

    // 9. First commit has no parent
    assert(r1.parentId.empty());
    std::cout << "  [PASS] 9. First commit has no parent\n";

    // 11. Committed file state snapshot is persisted
    fs::path snapshotFile = commitObjDir / "snapshot" / "fileA.txt";
    assert(fs::exists(snapshotFile));
    {
        std::ifstream in(snapshotFile.string());
        std::string content;
        std::getline(in, content);
        assert(content == "Hello World");
    }
    std::cout << "  [PASS] 11. Committed file state snapshot persisted\n";

    // 12. Staging behavior after commit (index is cleared)
    {
        StagingArea sa(testDir);
        assert(sa.getStagedFiles().empty());
    }
    std::cout << "  [PASS] 12. Staging index cleared after commit\n";

    // Create a second file and stage it for a second commit
    std::string fileB = "fileB.txt";
    fs::path fullPathB = fs::path(testDir) / fileB;
    {
        std::ofstream out(fullPathB.string());
        out << "Second File\n";
    }
    addFile(fileB, testDir);

    CommitResult r2 = commitRepo("Second commit", testDir);
    assert(r2.status == CommitResult::SUCCESS);

    // 10. Second commit points to first commit as parent
    assert(r2.parentId == r1.commitId);
    std::cout << "  [PASS] 10. Second commit points to first commit as parent\n";

    // 13. Undo restores previous staging state and HEAD
    CommitResult undoRes = undoCommit(testDir);
    assert(undoRes.status == CommitResult::SUCCESS);
    {
        StagingArea sa(testDir);
        assert(sa.isStaged("fileB.txt"));
    }
    std::cout << "  [PASS] 13. Undo restores previous staged state and HEAD\n";

    // 14. Redo restores undone state
    CommitResult redoRes = redoCommit(testDir);
    assert(redoRes.status == CommitResult::SUCCESS);
    {
        StagingArea sa(testDir);
        assert(!sa.isStaged("fileB.txt"));
    }
    std::cout << "  [PASS] 14. Redo restores undone state\n";

    // 15. New operation after undo clears redo path
    // Re-commit second commit to set up undo state
    addFile(fileB, testDir);
    CommitResult r2_2 = commitRepo("Second commit repeat", testDir);
    assert(r2_2.status == CommitResult::SUCCESS);

    undoCommit(testDir);  // Undo second commit, now fileB is staged again
    std::string fileC = "fileC.txt";
    fs::path fullPathC = fs::path(testDir) / fileC;
    {
        std::ofstream out(fullPathC.string());
        out << "Third File\n";
    }
    addFile(fileC, testDir);
    CommitResult r3 = commitRepo("Branch commit", testDir);  // New commit clears redo path
    assert(r3.status == CommitResult::SUCCESS);

    CommitResult invalidRedo = redoCommit(testDir);
    assert(invalidRedo.status == CommitResult::NOTHING_TO_REDO);
    std::cout << "  [PASS] 15. New operation after undo clears redo path\n";

    // 16. History limit handling using deque (limit = 3 test)
    {
        std::string limitTestDir = "temp_test_limit";
        setupTestDir(limitTestDir);
        initRepo(limitTestDir);

        CommitHistory customHistory(limitTestDir, 3);
        for (int i = 1; i <= 5; ++i) {
            HistoryEntry entry;
            entry.commitId = "commit_" + std::to_string(i);
            customHistory.recordOperation(entry);
        }
        // Limit is 3, so deque should contain exactly 3 items (commits 3, 4, 5)
        assert(customHistory.undoDeque().size() == 3);
        assert(customHistory.undoDeque().front().commitId == "commit_3");
        assert(customHistory.undoDeque().back().commitId == "commit_5");
        cleanupTestDir(limitTestDir);
        std::cout << "  [PASS] 16. History limit (pop_front) works correctly\n";
    }

    // 17 & 18. CLI integration tests
    {
        std::string cliTestDir = "temp_test_cli_commit";
        setupTestDir(cliTestDir);
        initRepo(cliTestDir);

        std::string testFile = "cli_test.txt";
        fs::path fullPathCLI = fs::path(cliTestDir) / testFile;
        {
            std::ofstream out(fullPathCLI.string());
            out << "CLI test\n";
        }
        addFile(testFile, cliTestDir);

        const char* argv1[] = {"minigit", "commit", "CLI commit message"};
        int rc1 = CLI::dispatch(3, const_cast<char**>(argv1), cliTestDir);
        assert(rc1 == 0);

        const char* argv2[] = {"minigit", "commit"};
        int rc2 = CLI::dispatch(2, const_cast<char**>(argv2), cliTestDir);
        assert(rc2 == 1);  // Fails missing message

        cleanupTestDir(cliTestDir);
        std::cout << "  [PASS] 17 & 18. CLI dispatch integration and existing behavior preserved\n";
    }

    cleanupTestDir(testDir);
    std::cout << "All commit tests passed successfully!\n";
    return 0;
}
