// branch / switch: refs/heads/<name>, HEAD handling, files restored on switch,
// unsaved work refused, switch -c, name validation.
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "AddCommand.h"
#include "BranchCommand.h"
#include "CLI.h"
#include "CommitCommand.h"
#include "InitCommand.h"
#include "RepoStore.h"
#include "RestoreCommand.h"

using namespace minigit;
namespace fs = std::filesystem;

#define CHECK(c)                                                           \
    do {                                                                   \
        if (!(c)) {                                                        \
            std::cerr << "FAIL line " << __LINE__ << ": " #c << std::endl; \
            std::exit(1);                                                  \
        }                                                                  \
    } while (0)

static void writeFile(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p.string(), std::ios::binary) << s;
}

static std::string readFile(const fs::path& p) {
    std::ifstream in(p.string(), std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static std::string commitFile(const fs::path& rootP, const std::string& name, const std::string& text,
                              const std::string& msg) {
    const std::string R = rootP.string();
    writeFile(rootP / name, text);
    CHECK(addFile(name, R).status == AddResult::STAGED);
    CommitResult c = commitRepo(msg, R);
    CHECK(c.status == CommitResult::SUCCESS);
    return c.commitId;
}

int main() {
    fs::path rootP = fs::temp_directory_path() / "minigit_test_branch_repo";
    fs::remove_all(rootP);
    fs::create_directories(rootP);
    const std::string R = rootP.string();

    // 1. outside a repository
    CHECK(showBranches(R).status == BranchResult::NOT_A_REPO);
    CHECK(createBranch("x", R).status == BranchResult::NOT_A_REPO);
    CHECK(switchBranch("x", R).status == BranchResult::NOT_A_REPO);
    CHECK(initRepo(R).status == InitResult::INITIALIZED);
    std::cout << "  [PASS] 1. commands fail outside a repository\n";

    // 2. fresh repository: main is listed and current; no branch can be created yet
    BranchResult l = showBranches(R);
    CHECK(l.status == BranchResult::LISTED && l.current == "main");
    CHECK(l.branches.size() == 1 && l.branches[0] == "main");
    CHECK(createBranch("feature", R).status == BranchResult::NO_COMMITS);
    std::cout << "  [PASS] 2. empty repo lists main; branch needs a first commit\n";

    std::string c1 = commitFile(rootP, "a.txt", "a1", "first");

    // 3. create a branch: ref file holds the current commit, HEAD stays on main
    BranchResult cr = createBranch("feature", R);
    CHECK(cr.status == BranchResult::CREATED && cr.commitId == c1);
    CHECK(repo::readBranch(R, "feature") == c1);
    CHECK(repo::currentBranch(R) == "main");
    CHECK(createBranch("feature", R).status == BranchResult::ALREADY_EXISTS);
    std::cout << "  [PASS] 3. branch created at the current commit\n";

    // 4. invalid names
    for (const char* bad : {"", "bad name", "..", "-x", ".hidden", "a/b", "x.lock", "HEAD", "trail."}) {
        CHECK(createBranch(bad, R).status == BranchResult::INVALID_NAME);
        CHECK(switchBranch(bad, R).status == BranchResult::INVALID_NAME);
    }
    CHECK(createBranch("v1.2-rc_3", R).status == BranchResult::CREATED);
    fs::remove(rootP / ".minigit" / "refs" / "heads" / "v1.2-rc_3");
    std::cout << "  [PASS] 4. invalid branch names rejected, normal names accepted\n";

    // 5. listing is sorted and marks the current branch
    l = showBranches(R);
    CHECK(l.branches.size() == 2 && l.branches[0] == "feature" && l.branches[1] == "main");
    std::cout << "  [PASS] 5. branches listed in order\n";

    // 6. switch: HEAD follows the branch; commits advance only that branch
    BranchResult sw = switchBranch("feature", R);
    CHECK(sw.status == BranchResult::SWITCHED && sw.commitId == c1);
    CHECK(repo::currentBranch(R) == "feature" && !repo::isDetached(R));
    std::string c2 = commitFile(rootP, "f.txt", "f1", "feature work");
    CHECK(repo::readBranch(R, "feature") == c2);
    CHECK(repo::readBranch(R, "main") == c1);
    std::cout << "  [PASS] 6. switch moves HEAD; commit advances only the current branch\n";

    // 7. switch back restores main's files and removes the feature-only file
    sw = switchBranch("main", R);
    CHECK(sw.status == BranchResult::SWITCHED);
    CHECK(readFile(rootP / "a.txt") == "a1");
    CHECK(!fs::exists(rootP / "f.txt"));
    CHECK(sw.filesRestored == 1 && sw.filesRemoved == 1);
    sw = switchBranch("feature", R);
    CHECK(sw.status == BranchResult::SWITCHED);
    CHECK(readFile(rootP / "a.txt") == "a1");  // inherited from c1, not lost on switch
    CHECK(readFile(rootP / "f.txt") == "f1");
    CHECK(switchBranch("main", R).status == BranchResult::SWITCHED);

    // A genuinely untracked file that would be overwritten by the feature tip
    // must still block the switch.
    writeFile(rootP / "f.txt", "user-owned");
    sw = switchBranch("feature", R);
    CHECK(sw.status == BranchResult::UNSAVED_CHANGES);
    CHECK(std::find(sw.blockingFiles.begin(), sw.blockingFiles.end(),
                    "untracked: f.txt would be overwritten") != sw.blockingFiles.end());
    CHECK(repo::currentBranch(R) == "main");
    CHECK(readFile(rootP / "f.txt") == "user-owned");
    fs::remove(rootP / "f.txt");
    CHECK(switchBranch("feature", R).status == BranchResult::SWITCHED);
    CHECK(readFile(rootP / "a.txt") == "a1");
    CHECK(readFile(rootP / "f.txt") == "f1");
    CHECK(switchBranch("main", R).status == BranchResult::SWITCHED);
    std::cout << "  [PASS] 7. branch switching keeps inherited files and protects untracked files\n";

    // 8. already on / unknown branch
    CHECK(switchBranch("main", R).status == BranchResult::ALREADY_ON);
    CHECK(switchBranch("nope", R).status == BranchResult::NOT_FOUND);
    std::cout << "  [PASS] 8. already-on and unknown branch\n";

    // 9. unsaved work blocks a switch and leaves everything as it was
    writeFile(rootP / "a.txt", "dirty");
    sw = switchBranch("feature", R);
    CHECK(sw.status == BranchResult::UNSAVED_CHANGES);
    CHECK(std::find(sw.blockingFiles.begin(), sw.blockingFiles.end(), "modified: a.txt") != sw.blockingFiles.end());
    CHECK(repo::currentBranch(R) == "main" && readFile(rootP / "a.txt") == "dirty");
    writeFile(rootP / "a.txt", "a1");
    writeFile(rootP / "s.txt", "staged");
    CHECK(addFile("s.txt", R).status == AddResult::STAGED);
    CHECK(switchBranch("feature", R).status == BranchResult::UNSAVED_CHANGES);
    CHECK(removeFile("s.txt", R).status == AddResult::UNSTAGED);
    fs::remove(rootP / "s.txt");
    std::cout << "  [PASS] 9. modified or staged files block switch\n";

    // 10. switch -c creates at the current commit and keeps the working folder
    sw = switchBranch("hotfix", R, true);
    CHECK(sw.status == BranchResult::SWITCHED && sw.commitId == c1);
    CHECK(repo::readBranch(R, "hotfix") == c1 && repo::currentBranch(R) == "hotfix");
    CHECK(readFile(rootP / "a.txt") == "a1");
    CHECK(switchBranch("hotfix", R, true).status == BranchResult::ALREADY_EXISTS);
    std::cout << "  [PASS] 10. switch -c creates and switches\n";

    // 11. detached HEAD: listing shows it; leaving an unreachable commit warns
    CHECK(switchBranch("main", R).status == BranchResult::SWITCHED);
    CHECK(checkoutCommit(c2, R).status == RestoreResult::SUCCESS);   // c2 is feature's tip
    l = showBranches(R);
    CHECK(l.current.empty() && l.headCommitId == c2);
    sw = switchBranch("main", R);
    CHECK(sw.status == BranchResult::SWITCHED && sw.warning.empty());  // feature still reaches c2
    CHECK(checkoutCommit(c1, R).status == RestoreResult::SUCCESS);
    std::string orphan = commitFile(rootP, "o.txt", "o", "orphan");    // reachable from no branch
    sw = switchBranch("main", R);
    CHECK(sw.status == BranchResult::SWITCHED);
    CHECK(sw.warning.find(orphan.substr(0, 7)) != std::string::npos);
    std::cout << "  [PASS] 11. detached HEAD listed; warning only when a commit would be left behind\n";

    // 12. on an empty repo, switch -c just renames the unborn branch
    fs::path root2 = fs::temp_directory_path() / "minigit_test_branch_repo2";
    fs::remove_all(root2);
    fs::create_directories(root2);
    CHECK(initRepo(root2.string()).status == InitResult::INITIALIZED);
    sw = switchBranch("dev", root2.string(), true);
    CHECK(sw.status == BranchResult::SWITCHED && repo::currentBranch(root2.string()) == "dev");
    writeFile(root2 / "x.txt", "x");
    CHECK(addFile("x.txt", root2.string()).status == AddResult::STAGED);
    CHECK(commitRepo("on dev", root2.string()).status == CommitResult::SUCCESS);
    CHECK(!repo::readBranch(root2.string(), "dev").empty());
    fs::remove_all(root2);
    std::cout << "  [PASS] 12. switch -c on an empty repository\n";

    // 13. CLI integration
    char a0[] = "minigit", b1[] = "branch", s1[] = "switch", n1[] = "dev2", f1[] = "-c";
    char* argvList[] = {a0, b1};
    CHECK(dispatch(2, argvList, R) == 0);
    char* argvCreate[] = {a0, b1, n1};
    CHECK(dispatch(3, argvCreate, R) == 0);
    CHECK(dispatch(3, argvCreate, R) == 1);  // already exists
    char* argvSwitch[] = {a0, s1, n1};
    CHECK(dispatch(3, argvSwitch, R) == 0);
    char n2[] = "dev3";
    char* argvSwitchC[] = {a0, s1, f1, n2};
    CHECK(dispatch(4, argvSwitchC, R) == 0);
    char* argvBad[] = {a0, s1};
    CHECK(dispatch(2, argvBad, R) == 1);  // usage
    std::cout << "  [PASS] 13. CLI dispatch for branch and switch\n";

    // 14. Ignore rules from the currently active worktree protect local ignored
    // content during a switch, even when the target branch has different rules.
    fs::path rootIgnore = fs::temp_directory_path() / "minigit_test_branch_ignore_repo";
    fs::remove_all(rootIgnore);
    fs::create_directories(rootIgnore);
    const std::string RI = rootIgnore.string();
    CHECK(initRepo(RI).status == InitResult::INITIALIZED);
    writeFile(rootIgnore / ".minigitignore", "");
    writeFile(rootIgnore / "secret.txt", "tracked base\n");
    CHECK(addFile(".minigitignore", RI).status == AddResult::STAGED);
    CHECK(addFile("secret.txt", RI).status == AddResult::STAGED);
    CHECK(commitRepo("base with empty ignore rules", RI).status == CommitResult::SUCCESS);
    CHECK(createBranch("clean", RI).status == BranchResult::CREATED);
    writeFile(rootIgnore / ".minigitignore", "secret.txt\n");
    CHECK(addFile(".minigitignore", RI).status == AddResult::STAGED);
    CHECK(commitRepo("ignore secret on main", RI).status == CommitResult::SUCCESS);
    writeFile(rootIgnore / "secret.txt", "local ignored content\n");
    sw = switchBranch("clean", RI);
    CHECK(sw.status == BranchResult::SWITCHED);
    CHECK(readFile(rootIgnore / "secret.txt") == "local ignored content\n");
    CHECK(readFile(rootIgnore / ".minigitignore").empty());
    CHECK(repo::currentBranch(RI) == "clean");
    CHECK(sw.filesRestored == 1 && sw.filesRemoved == 0);
    fs::remove_all(rootIgnore);
    std::cout << "  [PASS] 14. active ignore rules preserve ignored local content across branch switches\n";

    fs::remove_all(rootP);
    std::cout << "test_branch: all tests passed\n";
    return 0;
}
