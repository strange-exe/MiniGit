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
    std::cout << "Running test_branch...\n";
    std::string testDir = "temp_test_branch";
    setupTestDir(testDir);
    fs::path rootP(testDir);

    // 1. Create the initial main branch (Scenario 1)
    CHECK(initRepo(testDir).status == InitResult::INITIALIZED);
    CHECK(getCurrentBranch(testDir) == "main");
    std::cout << "  [PASS] 1. Initial main branch initialized\n";

    // 2. List branches in empty repository (Scenario 2)
    {
        BranchResult lr = listBranches(testDir);
        CHECK(lr.status == BranchResult::LIST_SUCCESS);
        CHECK(lr.branches.size() == 1);
        CHECK(lr.branches[0].name == "main");
        CHECK(lr.branches[0].isActive == true);
        std::cout << "  [PASS] 2. Branch listing identifies active main branch in fresh repo\n";
    }

    // Branch creation should fail when no commits exist
    {
        BranchResult br = createBranch("feature", testDir);
        CHECK(br.status == BranchResult::NO_COMMITS);
    }

    // 14. Test invalid branch names (Scenario 14)
    {
        for (const char* bad : {"-bad", "/slash", "bad/name/", "bad name", "bad..name", "bad?name", "bad*name", "bad~1", "HEAD"}) {
            BranchResult br = createBranch(bad, testDir);
            CHECK(br.status == BranchResult::INVALID_NAME);
        }
        std::cout << "  [PASS] 14. Invalid branch names rejected\n";
    }

    // Initial commit on main: fileA.txt = "Shared A"
    writeFile(rootP / "fileA.txt", "Shared A");
    CHECK(addFile("fileA.txt", testDir).status == AddResult::STAGED);
    CommitResult c1 = commitRepo("Initial commit on main", testDir);
    CHECK(c1.status == CommitResult::SUCCESS);
    CHECK(c1.branch == "main");

    // 3. Create a feature branch (Scenario 3)
    {
        BranchResult br = createBranch("feature", testDir);
        CHECK(br.status == BranchResult::CREATE_SUCCESS);
        CHECK(br.branchName == "feature");
        CHECK(br.commitId == c1.commitId);
        std::cout << "  [PASS] 3. Feature branch created\n";
    }

    // 4. Verify that both branches initially reference the same commit (Scenario 4)
    {
        std::string mainCid = getBranchCommitId("main", testDir);
        std::string featureCid = getBranchCommitId("feature", testDir);
        CHECK(mainCid == c1.commitId);
        CHECK(featureCid == c1.commitId);
        CHECK(getCurrentBranch(testDir) == "main"); // creating branch did not switch HEAD
        std::cout << "  [PASS] 4. Both branches reference the same initial commit\n";
    }

    // 13. Test duplicate branch names (Scenario 13)
    {
        BranchResult dup = createBranch("feature", testDir);
        CHECK(dup.status == BranchResult::ALREADY_EXISTS);
        std::cout << "  [PASS] 13. Duplicate branch names rejected\n";
    }

    // Switch to feature branch
    {
        CheckoutResult co = checkoutBranch("feature", false, testDir);
        CHECK(co.status == CheckoutResult::SUCCESS);
        CHECK(getCurrentBranch(testDir) == "feature");
    }

    // 5. Create a commit on the feature branch (Scenario 5)
    CommitResult c2;
    {
        writeFile(rootP / "feature.txt", "Feature Work");
        CHECK(addFile("feature.txt", testDir).status == AddResult::STAGED);
        c2 = commitRepo("Feature commit", testDir);
        CHECK(c2.status == CommitResult::SUCCESS);
        CHECK(c2.parentId == c1.commitId);
        CHECK(c2.branch == "feature");
        CHECK(getBranchCommitId("feature", testDir) == c2.commitId);
        std::cout << "  [PASS] 5. Commit created on feature branch with initial commit as parent\n";
    }

    // 6. Verify that main remains unchanged (Scenario 6)
    {
        CHECK(getBranchCommitId("main", testDir) == c1.commitId);
        std::cout << "  [PASS] 6. Main branch pointer remains unchanged at initial commit\n";
    }

    // 7. Switch back to main (Scenario 7)
    {
        CheckoutResult co = checkoutBranch("main", false, testDir);
        CHECK(co.status == CheckoutResult::SUCCESS);
        CHECK(getCurrentBranch(testDir) == "main");
        CHECK(getHEADCommitId(testDir) == c1.commitId);
        // feature.txt must NOT exist on main!
        CHECK(!fs::exists(rootP / "feature.txt"));
        CHECK(readFile(rootP / "fileA.txt") == "Shared A");
        std::cout << "  [PASS] 7. Switched back to main; feature files removed from working tree\n";
    }

    // 8. Create a different commit on main (Scenario 8)
    CommitResult c3;
    {
        writeFile(rootP / "main_only.txt", "Main Work");
        CHECK(addFile("main_only.txt", testDir).status == AddResult::STAGED);
        c3 = commitRepo("Main commit", testDir);
        CHECK(c3.status == CommitResult::SUCCESS);
        CHECK(c3.parentId == c1.commitId);
        CHECK(c3.branch == "main");
        CHECK(getBranchCommitId("main", testDir) == c3.commitId);
        std::cout << "  [PASS] 8. Independent commit created on main branch\n";
    }

    // 9. Verify that the histories diverge correctly (Scenario 9)
    {
        // Both c2 and c3 have c1 as parent, but different IDs
        CHECK(c2.parentId == c1.commitId);
        CHECK(c3.parentId == c1.commitId);
        CHECK(c2.commitId != c3.commitId);
        CHECK(getBranchCommitId("main", testDir) == c3.commitId);
        CHECK(getBranchCommitId("feature", testDir) == c2.commitId);
        std::cout << "  [PASS] 9. Histories diverge with shared ancestor c1\n";
    }

    // 10 & 11. Switch repeatedly between both branches and verify snapshots (Scenarios 10 & 11)
    {
        for (int i = 0; i < 3; ++i) {
            // Switch to feature
            CHECK(checkoutBranch("feature", false, testDir).status == CheckoutResult::SUCCESS);
            CHECK(getCurrentBranch(testDir) == "feature");
            CHECK(fs::exists(rootP / "feature.txt"));
            CHECK(readFile(rootP / "feature.txt") == "Feature Work");
            CHECK(!fs::exists(rootP / "main_only.txt"));
            CHECK(readFile(rootP / "fileA.txt") == "Shared A");

            // Switch to main
            CHECK(checkoutBranch("main", false, testDir).status == CheckoutResult::SUCCESS);
            CHECK(getCurrentBranch(testDir) == "main");
            CHECK(!fs::exists(rootP / "feature.txt"));
            CHECK(fs::exists(rootP / "main_only.txt"));
            CHECK(readFile(rootP / "main_only.txt") == "Main Work");
            CHECK(readFile(rootP / "fileA.txt") == "Shared A");
        }
        std::cout << "  [PASS] 10 & 11. Repeated switching correctly restores each branch's exact snapshot\n";
    }

    // 12. Test nonexistent branches (Scenario 12)
    {
        CheckoutResult co = checkoutBranch("nonexistent_branch", false, testDir);
        CHECK(co.status == CheckoutResult::NOT_FOUND);
        std::cout << "  [PASS] 12. Switching to nonexistent branch safely rejected\n";
    }

    // 15. Test branch deletion and active-branch protection (Scenario 15)
    {
        // Currently on main: deleting main must fail
        BranchResult delActive = deleteBranch("main", testDir);
        CHECK(delActive.status == BranchResult::CANNOT_DELETE_ACTIVE);

        // Deleting nonexistent branch must fail
        BranchResult delMissing = deleteBranch("phantom", testDir);
        CHECK(delMissing.status == BranchResult::NOT_FOUND);

        // Create temporary branch "temp"
        CHECK(createBranch("temp", testDir).status == BranchResult::CREATE_SUCCESS);
        CHECK(getBranchCommitId("temp", testDir) == c3.commitId);

        // Delete "temp"
        BranchResult delSuccess = deleteBranch("temp", testDir);
        CHECK(delSuccess.status == BranchResult::DELETE_SUCCESS);
        CHECK(getBranchCommitId("temp", testDir).empty());

        std::cout << "  [PASS] 15. Active branch deletion blocked; non-active branch deletion succeeds\n";
    }

    // Prevent loss of uncommitted changes during checkout
    {
        writeFile(rootP / "main_only.txt", "Dirty edit on main");
        CheckoutResult co = checkoutBranch("feature", false, testDir);
        CHECK(co.status == CheckoutResult::UNCOMMITTED_CHANGES);

        // Revert dirty edit
        writeFile(rootP / "main_only.txt", "Main Work");
        std::cout << "  [PASS] Checkout protects uncommitted changes from being overwritten\n";
    }

    cleanupTestDir(testDir);
    std::cout << "All branch tests passed successfully!\n";
    return 0;
}

