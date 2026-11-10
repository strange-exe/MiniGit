// checkout <commit-id>: files restored exactly, unsaved work refused first,
// tracked-only removal, ignored files untouched, HEAD detached, bad ids rejected.
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <iterator>
#include <string>

#include "AddCommand.h"
#include "BranchCommand.h"
#include "CLI.h"
#include "CommitCommand.h"
#include "IgnoreManager.h"
#include "InitCommand.h"
#include "LogCommand.h"
#include "RepoUtils.h"
#include "RestoreCommand.h"
#include "StagingArea.h"

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

static bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

static bool isDetached(const std::string& R) { return readHeadState(R).detached; }

// path -> contents of every file tracked at `id`, through the shared RepoUtils reader.
static std::map<std::string, std::string> treeOf(const std::string& R, const std::string& id, bool& complete) {
    std::map<std::string, std::string> out;
    for (const auto& kv : getTrackedFilesAtCommit(R, id, &complete)) out[kv.first] = readFile(kv.second);
    return out;
}

int main() {
    fs::path rootP = fs::temp_directory_path() / "minigit_test_checkout_repo";
    fs::remove_all(rootP);
    fs::create_directories(rootP);
    const std::string R = rootP.string();

    // 1. outside a repository
    CHECK(checkoutCommit("abcd1234", R).status == RestoreResult::NOT_A_REPO);
    std::cout << "  [PASS] 1. checkout outside a repository\n";

    CHECK(initRepo(R).status == InitResult::INITIALIZED);

    // Treat .minigitignore as a normal committed project file in this regression
    // case. It must survive later commits that stage only other paths.
    writeFile(rootP / ".minigitignore", "*.log\n");

    // c1 changes a.txt, b.txt, sub/d.txt and .minigitignore.
    // c2 changes only a.txt and c.txt; the other c1 files must be inherited.
    writeFile(rootP / "a.txt", "v1");
    writeFile(rootP / "b.txt", "b1");
    writeFile(rootP / "sub" / "d.txt", "d1");
    CHECK(addFile("a.txt", R).status == AddResult::STAGED);
    CHECK(addFile("b.txt", R).status == AddResult::STAGED);
    CHECK(addFile("sub/d.txt", R).status == AddResult::STAGED);
    CHECK(addFile(".minigitignore", R).status == AddResult::STAGED);
    CommitResult c1 = commitRepo("first", R);
    CHECK(c1.status == CommitResult::SUCCESS);

    writeFile(rootP / "a.txt", "v2");
    writeFile(rootP / "c.txt", "c1");
    CHECK(addFile("a.txt", R).status == AddResult::STAGED);
    CHECK(addFile("c.txt", R).status == AddResult::STAGED);
    CommitResult c2 = commitRepo("second", R);
    CHECK(c2.status == CommitResult::SUCCESS);

    bool complete = false;
    std::map<std::string, std::string> secondTree = treeOf(R, c2.commitId, complete);
    CHECK(complete);
    CHECK(secondTree.size() == 5);
    CHECK(secondTree.at("a.txt") == "v2");
    CHECK(secondTree.at("b.txt") == "b1");
    CHECK(secondTree.at("sub/d.txt") == "d1");
    CHECK(secondTree.at(".minigitignore") == "*.log\n");
    CHECK(secondTree.at("c.txt") == "c1");

    // 2. bad arguments
    CHECK(checkoutCommit("", R).status == RestoreResult::INVALID_ARGUMENTS);
    CHECK(checkoutCommit("abc", R).status == RestoreResult::INVALID_ARGUMENTS);
    CHECK(checkoutCommit("ffffffffff", R).status == RestoreResult::COMMIT_NOT_FOUND);
    std::cout << "  [PASS] 2. empty, too-short and unknown ids rejected\n";

    // 3. a branch name is not a commit id, and the message says what to do
    CHECK(createBranch("side", R).status == BranchResult::CREATE_SUCCESS);
    RestoreResult rb = checkoutCommit("side", R);
    CHECK(rb.status == RestoreResult::COMMIT_NOT_FOUND);
    CHECK(rb.errorMessage.find("checkout side") != std::string::npos);
    CHECK(deleteBranch("side", R).status == BranchResult::DELETE_SUCCESS);
    std::cout << "  [PASS] 3. branch name gives a 'use checkout <branch>' hint\n";

    // 4. staged work blocks checkout; nothing changes
    writeFile(rootP / "e.txt", "e1");
    CHECK(addFile("e.txt", R).status == AddResult::STAGED);
    RestoreResult r = checkoutCommit(c1.commitId, R);
    CHECK(r.status == RestoreResult::UNSAVED_CHANGES);
    CHECK(contains(r.blockingFiles, "staged: e.txt"));
    CHECK(readFile(rootP / "a.txt") == "v2");
    CHECK(getHEADCommitId(R) == c2.commitId);
    CHECK(removeFile("e.txt", R).status == AddResult::UNSTAGED);
    fs::remove(rootP / "e.txt");
    std::cout << "  [PASS] 4. staged files block checkout, nothing changed\n";

    // 5. edited and deleted tracked files block checkout
    writeFile(rootP / "a.txt", "dirty");
    r = checkoutCommit(c1.commitId, R);
    CHECK(r.status == RestoreResult::UNSAVED_CHANGES);
    CHECK(contains(r.blockingFiles, "modified: a.txt"));
    CHECK(readFile(rootP / "a.txt") == "dirty");
    writeFile(rootP / "a.txt", "v2");
    fs::remove(rootP / "c.txt");
    r = checkoutCommit(c1.commitId, R);
    CHECK(r.status == RestoreResult::UNSAVED_CHANGES);
    CHECK(contains(r.blockingFiles, "deleted: c.txt"));
    writeFile(rootP / "c.txt", "c1");
    std::cout << "  [PASS] 5. modified and deleted tracked files block checkout\n";

    // 6. A file committed by an ancestor is still tracked even though it is
    // absent from c2's own metadata.files list. This used to be misreported as
    // an untracked overwrite during restore/switch.
    writeFile(rootP / "b.txt", "my own b");
    r = checkoutCommit(c1.commitId, R);
    CHECK(r.status == RestoreResult::UNSAVED_CHANGES);
    CHECK(contains(r.blockingFiles, "modified: b.txt"));
    CHECK(readFile(rootP / "b.txt") == "my own b");
    writeFile(rootP / "b.txt", "b1");
    std::cout << "  [PASS] 6. inherited files are recognised as tracked\n";

    // 7. an ignored file sits in the working folder; checkout must never touch it
    writeFile(rootP / "debug.log", "keep me");

    // 8. checkout the first commit by full id
    r = checkoutCommit(c1.commitId, R);
    CHECK(r.status == RestoreResult::SUCCESS);
    CHECK(r.commitId == c1.commitId);
    CHECK(r.message == "first");
    CHECK(readFile(rootP / "a.txt") == "v1");
    CHECK(readFile(rootP / "b.txt") == "b1");
    CHECK(readFile(rootP / "sub" / "d.txt") == "d1");
    CHECK(readFile(rootP / ".minigitignore") == "*.log\n");  // inherited from c1
    CHECK(!fs::exists(rootP / "c.txt"));                       // introduced by c2: removed
    CHECK(readFile(rootP / "debug.log") == "keep me");         // ignored: untouched
    CHECK(isDetached(R));
    CHECK(getHEADCommitId(R) == c1.commitId);
    CHECK(StagingArea(R).size() == 0);
    CHECK(r.previousCommitId == c2.commitId);
    CHECK(r.previousHead == "ref: refs/heads/main");
    CHECK(r.filesRestored == 4 && r.filesRemoved == 1);
    std::cout << "  [PASS] 7-8. files restored exactly, tracked file removed, ignored file untouched\n";

    // 9. the branch was not moved, and log still works on a detached HEAD
    CHECK(getBranchCommitId("main", R) == c2.commitId);
    LogResult lr = showLog(R);
    CHECK(lr.status == LogResult::OK && lr.entries.size() == 1 && lr.entries[0].commitId == c1.commitId);
    std::cout << "  [PASS] 9. branch ref unchanged; log works while detached\n";

    // 10. go forward again by a unique prefix. The second commit's effective
    // tree includes unchanged files from its first parent.
    r = checkoutCommit(c2.commitId.substr(0, 8), R);
    CHECK(r.status == RestoreResult::SUCCESS);
    CHECK(readFile(rootP / "a.txt") == "v2");
    CHECK(readFile(rootP / "b.txt") == "b1");
    CHECK(readFile(rootP / "sub" / "d.txt") == "d1");
    CHECK(readFile(rootP / ".minigitignore") == "*.log\n");
    CHECK(readFile(rootP / "c.txt") == "c1");
    CHECK(readFile(rootP / "debug.log") == "keep me");
    std::cout << "  [PASS] 10. prefix checkout preserves inherited files\n";

    // 11. while detached, branch-moving commands refuse (a commit there would belong to
    // no branch) and say how to continue; nothing changes.
    writeFile(rootP / "z.txt", "z");
    CHECK(addFile("z.txt", R).status == AddResult::STAGED);
    CommitResult c3 = commitRepo("detached work", R);
    CHECK(c3.status == CommitResult::ERROR && c3.errorMessage.find("detached") != std::string::npos);
    CHECK(rollbackRepo(c1.commitId, false, R).status == RollbackResult::ERROR);
    CHECK(undoCommit(R).status == CommitResult::ERROR);
    CHECK(getHEADCommitId(R) == c2.commitId && isDetached(R));
    CHECK(getBranchCommitId("main", R) == c2.commitId);
    CHECK(listBranches(R).detachedAt == c2.commitId);
    CHECK(removeFile("z.txt", R).status == AddResult::UNSTAGED);
    std::cout << "  [PASS] 11. commit, undo and rollback refuse on a detached HEAD\n";

    // 11b. 'branch <name>' + 'checkout <name>' continues from the detached commit
    CHECK(createBranch("fix", R).status == BranchResult::CREATE_SUCCESS);
    CHECK(getBranchCommitId("fix", R) == c2.commitId);
    CHECK(checkoutBranch("fix", false, R).status == CheckoutResult::SUCCESS);
    CHECK(!isDetached(R) && getCurrentBranch(R) == "fix");
    CHECK(addFile("z.txt", R).status == AddResult::STAGED);
    c3 = commitRepo("work on fix", R);
    CHECK(c3.status == CommitResult::SUCCESS && c3.parentId == c2.commitId);
    CHECK(getBranchCommitId("fix", R) == c3.commitId && getBranchCommitId("main", R) == c2.commitId);
    r = checkoutCommit(c2.commitId, R);  // detach again; z.txt (tracked on fix only) is removed
    CHECK(r.status == RestoreResult::SUCCESS && !fs::exists(rootP / "z.txt"));
    writeFile(rootP / "z.txt", "z");     // an untracked file the next checks must not touch
    std::cout << "  [PASS] 11b. branch from a detached HEAD, commit there, detach again\n";

    // 12. a missing snapshot is reported and nothing is changed
    fs::remove(rootP / ".minigit" / "objects" / c1.commitId / "snapshot" / "b.txt");
    r = checkoutCommit(c1.commitId, R);
    CHECK(r.status == RestoreResult::SNAPSHOT_MISSING);
    CHECK(readFile(rootP / "z.txt") == "z");
    CHECK(getHEADCommitId(R) == c2.commitId);
    // Restore the deliberately damaged object so the later CLI test can read
    // c2's inherited tree as well.
    writeFile(rootP / ".minigit" / "objects" / c1.commitId / "snapshot" / "b.txt", "b1");
    std::cout << "  [PASS] 12. missing ancestor snapshot: refused, repository unchanged\n";

    // 13. CLI integration
    char a0[] = "minigit", a1[] = "checkout", a2[] = "zzzzzz";
    char* argv1[] = {a0, a1, a2};
    CHECK(dispatch(3, argv1, R) == 1);
    char* argv2[] = {a0, a1};
    CHECK(dispatch(2, argv2, R) == 1);  // usage
    std::string c2id = c2.commitId;
    std::vector<char> idBuf(c2id.begin(), c2id.end());
    idBuf.push_back('\0');
    char* argv3[] = {a0, a1, idBuf.data()};
    CHECK(dispatch(3, argv3, R) == 0);
    CHECK(getHEADCommitId(R) == c2.commitId);
    std::cout << "  [PASS] 13. CLI dispatch: success, bad id and usage\n";

    // 14. A regular file where a target path needs a directory must be caught
    // before any tracked file is changed. This guards against partial checkout.
    const std::string conflictId = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    fs::path conflictObject = rootP / ".minigit" / "objects" / conflictId;
    writeFile(conflictObject / "metadata", "commit " + conflictId + "\nparent " + c2.commitId +
              "\ntimestamp 9999999999\nauthor test\nmessage path conflict\nfiles 1\nnested/file.txt\n");
    writeFile(conflictObject / "snapshot" / "nested" / "file.txt", "target\n");
    writeFile(rootP / "nested", "untracked parent file\n");
    const std::string beforeA = readFile(rootP / "a.txt");
    r = checkoutCommit(conflictId, R);
    CHECK(r.status == RestoreResult::UNSAVED_CHANGES);
    CHECK(readFile(rootP / "a.txt") == beforeA);
    CHECK(readFile(rootP / "nested") == "untracked parent file\n");
    CHECK(getHEADCommitId(R) == c2.commitId);
    fs::remove(rootP / "nested");
    std::cout << "  [PASS] 14. path conflict is rejected before partial file writes\n";

    // 15. Truncated metadata must not be accepted as a valid, incomplete tree.
    const std::string truncatedId = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    fs::path truncatedObject = rootP / ".minigit" / "objects" / truncatedId;
    writeFile(truncatedObject / "metadata", "commit " + truncatedId + "\nparent " + c2.commitId +
              "\ntimestamp 9999999999\nauthor test\nmessage truncated\nfiles 2\nonly.txt\n");
    writeFile(truncatedObject / "snapshot" / "only.txt", "one\n");
    CommitMetadata invalidMeta;
    CHECK(!readCommitMeta(R, truncatedId, invalidMeta));
    r = checkoutCommit(truncatedId, R);
    CHECK(r.status == RestoreResult::COMMIT_NOT_FOUND);
    CHECK(readFile(rootP / "a.txt") == beforeA);
    CHECK(getHEADCommitId(R) == c2.commitId);
    std::cout << "  [PASS] 15. truncated metadata is rejected without changing the worktree\n";

    // 16. Unsafe and structurally conflicting snapshot paths are rejected.
    const std::string unsafeId = "cccccccccccccccccccccccccccccccccccccccc";
    fs::path unsafeObject = rootP / ".minigit" / "objects" / unsafeId;
    writeFile(unsafeObject / "metadata", "commit " + unsafeId + "\nparent " + c2.commitId +
              "\ntimestamp 9999999999\nauthor test\nmessage unsafe\nfiles 1\n../outside.txt\n");
    CHECK(!readCommitMeta(R, unsafeId, invalidMeta));

    const std::string conflictBaseId = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
    fs::path conflictBaseObject = rootP / ".minigit" / "objects" / conflictBaseId;
    writeFile(conflictBaseObject / "metadata", "commit " + conflictBaseId + "\nparent " + c2.commitId +
              "\ntimestamp 9999999998\nauthor test\nmessage file path\nfiles 1\nnode\n");
    writeFile(conflictBaseObject / "snapshot" / "node", "file\n");

    const std::string conflictTreeId = "dddddddddddddddddddddddddddddddddddddddd";
    fs::path conflictTreeObject = rootP / ".minigit" / "objects" / conflictTreeId;
    writeFile(conflictTreeObject / "metadata", "commit " + conflictTreeId + "\nparent " + conflictBaseId +
              "\ntimestamp 9999999999\nauthor test\nmessage conflicting tree\nfiles 1\nnode/child.txt\n");
    writeFile(conflictTreeObject / "snapshot" / "node" / "child.txt", "child\n");
    r = checkoutCommit(conflictTreeId, R);
    CHECK(r.status == RestoreResult::SNAPSHOT_MISSING);
    CHECK(r.errorMessage.find("file/folder conflict") != std::string::npos);
    CHECK(readFile(rootP / "a.txt") == beforeA);
    CHECK(getHEADCommitId(R) == c2.commitId);
    std::cout << "  [PASS] 16. unsafe paths and file/directory conflicts are rejected\n";

    // 17. A malformed or unreadable HEAD must not be interpreted as an empty
    // current tree, otherwise checkout could misclassify tracked files.
    const std::string validHead = trim(readFile(rootP / ".minigit" / "HEAD"));
    writeFile(rootP / ".minigit" / "HEAD", "not-a-valid-head\n");
    const std::string beforeMalformedHead = readFile(rootP / "a.txt");
    r = checkoutCommit(c1.commitId, R);
    CHECK(r.status == RestoreResult::ERROR);
    CHECK(readFile(rootP / "a.txt") == beforeMalformedHead);
    writeFile(rootP / ".minigit" / "HEAD", validHead + "\n");
    std::cout << "  [PASS] 17. malformed HEAD is rejected without worktree changes\n";

    // 18. A file deleted by a later commit (its 'deleted' list) is not brought back,
    // and an older checkout still restores it.
    CHECK(checkoutBranch("main", false, R).status == CheckoutResult::SUCCESS);
    fs::remove(rootP / "b.txt");
    CHECK(addFile("b.txt", R).status == AddResult::DELETION_STAGED);
    CommitResult c4 = commitRepo("drop b", R);
    CHECK(c4.status == CommitResult::SUCCESS);
    r = checkoutCommit(c1.commitId, R);
    CHECK(r.status == RestoreResult::SUCCESS && readFile(rootP / "b.txt") == "b1");
    r = checkoutCommit(c4.commitId, R);
    CHECK(r.status == RestoreResult::SUCCESS && !fs::exists(rootP / "b.txt"));
    CHECK(readFile(rootP / "a.txt") == "v2" && readFile(rootP / "c.txt") == "c1");
    CHECK(checkoutBranch("main", false, R).status == CheckoutResult::SUCCESS && !isDetached(R));
    std::cout << "  [PASS] 18. deletions recorded by commit are honoured by checkout\n";

    fs::remove_all(rootP);
    std::cout << "test_checkout: all tests passed\n";
    return 0;
}
