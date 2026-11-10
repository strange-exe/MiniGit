#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "AddCommand.h"
#include "BranchCommand.h"
#include "CLI.h"
#include "CommitCommand.h"
#include "InitCommand.h"
#include "RepoUtils.h"
#include "RollbackCommand.h"
#include "StagingArea.h"

namespace fs = std::filesystem;
using namespace minigit;

#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::cerr << "FAIL line " << __LINE__ << ": " #c << std::endl;    \
            std::exit(1);                                                     \
        }                                                                     \
    } while (0)

static void setupTestDir(const std::string& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
    fs::create_directories(path, ec);
}

static void cleanupTestDir(const std::string& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
}

static void writeFile(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p.string(), std::ios::binary) << s;
}

static std::string readFile(const fs::path& p) {
    std::ifstream in(p.string(), std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int main() {
    std::cout << "Running test_rollback...\n";
    std::string testDir = "temp_test_rollback";
    setupTestDir(testDir);
    fs::path rootP(testDir);

    // 1. Rollback outside repo
    {
        RollbackResult r = rollbackRepo("abc", false, testDir);
        CHECK(r.status == RollbackResult::NOT_A_REPO);
        std::cout << "  [PASS] Rollback fails outside repository\n";
    }

    CHECK(initRepo(testDir).status == InitResult::INITIALIZED);

    // 2. Rollback in an empty repository (Scenario 5)
    {
        RollbackResult r = rollbackRepo("abc", false, testDir);
        CHECK(r.status == RollbackResult::NO_COMMITS);
        std::cout << "  [PASS] 5. Rollback fails gracefully in empty repository\n";
    }

    // Set up linear history: C1 -> C2 -> C3
    // C1 introduces fileA.txt = "Version 1"
    writeFile(rootP / "fileA.txt", "Version 1");
    CHECK(addFile("fileA.txt", testDir).status == AddResult::STAGED);
    CommitResult c1 = commitRepo("Commit 1", testDir);
    CHECK(c1.status == CommitResult::SUCCESS);

    // C2 modifies fileA.txt = "Version 2" and introduces fileB.txt = "File B"
    writeFile(rootP / "fileA.txt", "Version 2");
    writeFile(rootP / "fileB.txt", "File B");
    CHECK(addFile("fileA.txt", testDir).status == AddResult::STAGED);
    CHECK(addFile("fileB.txt", testDir).status == AddResult::STAGED);
    CommitResult c2 = commitRepo("Commit 2", testDir);
    CHECK(c2.status == CommitResult::SUCCESS);
    CHECK(c2.parentId == c1.commitId);

    // C3 introduces fileC.txt = "File C"
    writeFile(rootP / "fileC.txt", "File C");
    CHECK(addFile("fileC.txt", testDir).status == AddResult::STAGED);
    CommitResult c3 = commitRepo("Commit 3", testDir);
    CHECK(c3.status == CommitResult::SUCCESS);
    CHECK(c3.parentId == c2.commitId);

    // 3. Rollback with invalid commit ID (Scenario 4)
    {
        RollbackResult r = rollbackRepo("nonexistent123", false, testDir);
        CHECK(r.status == RollbackResult::INVALID_COMMIT);
        std::cout << "  [PASS] 4. Rollback rejects invalid commit ID\n";
    }

    // 4. Test rollback when staged changes exist (Scenario 11)
    {
        writeFile(rootP / "uncommitted_staged.txt", "staged data");
        CHECK(addFile("uncommitted_staged.txt", testDir).status == AddResult::STAGED);

        // Without force -> must reject
        RollbackResult r = rollbackRepo(c2.commitId, false, testDir);
        CHECK(r.status == RollbackResult::UNCOMMITTED_CHANGES);

        // Unstage to clean up for next tests
        StagingArea sa(testDir);
        sa.clear();
        std::error_code ec;
        fs::remove(rootP / "uncommitted_staged.txt", ec);
        std::cout << "  [PASS] 11. Rollback protects staged changes from loss\n";
    }

    // 5. Test rollback when uncommitted working tree modifications exist (Scenario 12)
    {
        writeFile(rootP / "fileA.txt", "Dirty working modification");

        // Without force -> must reject
        RollbackResult r = rollbackRepo(c2.commitId, false, testDir);
        CHECK(r.status == RollbackResult::UNCOMMITTED_CHANGES);

        // Restore fileA.txt to clean state
        writeFile(rootP / "fileA.txt", "Version 2");
        std::cout << "  [PASS] 12. Rollback protects uncommitted modifications from loss\n";
    }

    // 6. Rollback from latest commit to its immediate parent: C3 -> C2 (Scenario 1)
    {
        RollbackResult r = rollbackRepo(c2.commitId, false, testDir);
        CHECK(r.status == RollbackResult::SUCCESS);
        CHECK(r.commitId == c2.commitId);
        CHECK(getHEADCommitId(testDir) == c2.commitId);

        // Scenario 6: Verify exact restoration of file contents
        CHECK(readFile(rootP / "fileA.txt") == "Version 2");
        CHECK(readFile(rootP / "fileB.txt") == "File B");

        // Scenario 7: fileC.txt introduced in C3 must be removed from working tree
        CHECK(!fs::exists(rootP / "fileC.txt"));

        // Scenario 8: Subsequent commit C3 remains stored in objects
        fs::path c3ObjDir = rootP / ".minigit" / "objects" / c3.commitId;
        CHECK(fs::is_directory(c3ObjDir));
        CHECK(fs::exists(c3ObjDir / "metadata"));

        std::cout << "  [PASS] 1 & 6 & 7 & 8. Rollback to immediate parent restores exact files and preserves objects\n";
    }

    // 7. Rollback across multiple commits to initial commit C1 (Scenarios 2 & 3)
    {
        // Target C1 by short 7-char ID
        std::string shortC1 = c1.commitId.substr(0, 7);
        RollbackResult r = rollbackRepo(shortC1, false, testDir);
        CHECK(r.status == RollbackResult::SUCCESS);
        CHECK(r.commitId == c1.commitId);
        CHECK(getHEADCommitId(testDir) == c1.commitId);

        // Exactly match state at C1
        CHECK(readFile(rootP / "fileA.txt") == "Version 1");
        CHECK(!fs::exists(rootP / "fileB.txt"));
        CHECK(!fs::exists(rootP / "fileC.txt"));

        // C2 and C3 still exist in objects
        CHECK(fs::is_directory(rootP / ".minigit" / "objects" / c2.commitId));
        CHECK(fs::is_directory(rootP / ".minigit" / "objects" / c3.commitId));

        std::cout << "  [PASS] 2 & 3. Rollback across multiple commits to initial commit (prefix resolution)\n";
    }

    // 8. Create a new commit after rollback and verify its parent (Scenario 9)
    CommitResult c4;
    {
        writeFile(rootP / "fileD.txt", "File D from rollback branch");
        CHECK(addFile("fileD.txt", testDir).status == AddResult::STAGED);
        c4 = commitRepo("Commit 4 after rollback", testDir);
        CHECK(c4.status == CommitResult::SUCCESS);

        // Parent must be C1!
        CHECK(c4.parentId == c1.commitId);
        CHECK(getHEADCommitId(testDir) == c4.commitId);

        std::cout << "  [PASS] 9. New commit after rollback points to target commit as parent\n";
    }

    // 9. Verify other branches remain unaffected (Scenario 10)
    {
        // Create branch "feature" at current C4
        CHECK(createBranch("feature", testDir).status == BranchResult::CREATE_SUCCESS);
        CHECK(getBranchCommitId("feature", testDir) == c4.commitId);

        // Roll back "main" to C1
        RollbackResult r = rollbackRepo(c1.commitId, false, testDir);
        CHECK(r.status == RollbackResult::SUCCESS);
        CHECK(getHEADCommitId(testDir) == c1.commitId);

        // "feature" branch must remain pointing to C4!
        CHECK(getBranchCommitId("feature", testDir) == c4.commitId);

        std::cout << "  [PASS] 10. Other branches remain completely unaffected by rollback\n";
    }

    // 10. Force override test with dirty working directory
    {
        writeFile(rootP / "fileA.txt", "Dirty edit");
        RollbackResult r = rollbackRepo(c4.commitId, true, testDir); // with force=true
        CHECK(r.status == RollbackResult::SUCCESS);
        CHECK(getHEADCommitId(testDir) == c4.commitId);
        CHECK(readFile(rootP / "fileD.txt") == "File D from rollback branch");
        std::cout << "  [PASS] Force override successfully overrides dirty working tree\n";
    }

    cleanupTestDir(testDir);
    std::cout << "All rollback tests passed successfully!\n";
    return 0;
}

