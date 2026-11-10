// Regression tests: one section per defect found during the functionality audit
// (see the error register in DOCUMENTATION.md, section 8). E001-E012 and E015 describe
// behaviour that was reproduced on the original binary before the fix; E013 and E014 cover
// the new safeguards (atomic writes, backward-compatible history format).
#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <utility>

#include "AddCommand.h"
#include "BranchCommand.h"
#include "CommitCommand.h"
#include "CommitHistory.h"
#include "FileUtils.h"
#include "IgnoreManager.h"
#include "InitCommand.h"
#include "LogCommand.h"
#include "RepoUtils.h"
#include "RollbackCommand.h"
#include "StagingArea.h"
#include "test_common.h"

using namespace minigit;
using namespace tc;

typedef std::vector<std::pair<std::string, std::string>> FileList;

// Writes the files, stages each one, commits and returns the new commit id.
static std::string commitFiles(const std::string& root, const FileList& files, const std::string& msg) {
    for (const auto& f : files) {
        writeFile(fs::path(root) / f.first, f.second);
        CHECK(addFile(f.first, root).status == AddResult::STAGED);
    }
    CommitResult r = commitRepo(msg, root);
    CHECK(r.status == CommitResult::SUCCESS);
    return r.commitId;
}

static std::size_t countObjects(const std::string& root) {
    std::size_t n = 0;
    for (const auto& e : fs::directory_iterator(fs::path(root) / ".minigit" / "objects")) {
        (void)e;
        ++n;
    }
    return n;
}

static std::string head(const std::string& root) { return getHEADCommitId(root); }

int main() {
    std::cout << "Running test_regression...\n";

    // E001 (Critical): `branch -d ../../HEAD` deleted .minigit/HEAD (path traversal in the branch name).
    {
        std::string R = freshDir("temp_reg_traversal");
        CHECK(initRepo(R).status == InitResult::INITIALIZED);
        commitFiles(R, {{"a.txt", "1"}}, "A");
        CHECK(createBranch("feature", R).status == BranchResult::CREATE_SUCCESS);
        BranchResult d = deleteBranch("../../HEAD", R);
        CHECK(d.status == BranchResult::INVALID_NAME);
        CHECK(fs::exists(fs::path(R) / ".minigit" / "HEAD"));
        CHECK(deleteBranch("../../index", R).status == BranchResult::INVALID_NAME);
        CHECK(fs::exists(fs::path(R) / ".minigit" / "index"));
        CHECK(deleteBranch("a/../../../HEAD", R).status == BranchResult::INVALID_NAME);
        int code = 0;
        std::string out = runCli({"minigit", "branch", "-d", "../../HEAD"}, R, code);
        CHECK(code == 1 && contains(out, "not a valid branch name"));
        CHECK(fs::exists(fs::path(R) / ".minigit" / "HEAD"));
        removeDir(R);
        PASS("E001 branch -d cannot escape refs/heads");
    }

    // E002 (Critical): undo/redo ignored the branch and rewrote the active branch to another line of history.
    {
        std::string R = freshDir("temp_reg_undo_branch");
        CHECK(initRepo(R).status == InitResult::INITIALIZED);
        std::string A = commitFiles(R, {{"f.txt", "A"}}, "A");
        commitFiles(R, {{"f.txt", "B"}}, "B");
        std::string C = commitFiles(R, {{"f.txt", "C"}}, "C");
        CHECK(createBranch("feature", R).status == BranchResult::CREATE_SUCCESS);
        CHECK(checkoutBranch("feature", false, R).status == CheckoutResult::SUCCESS);
        std::string D = commitFiles(R, {{"f.txt", "D"}}, "D");
        CHECK(checkoutBranch("main", false, R).status == CheckoutResult::SUCCESS);

        CommitResult u = undoCommit(R);  // newest recorded commit belongs to 'feature'
        CHECK(u.status == CommitResult::NOTHING_TO_UNDO);
        CHECK(contains(u.errorMessage, "feature"));
        CHECK(getBranchCommitId("main", R) == C);
        CHECK(getBranchCommitId("feature", R) == D);
        CHECK(readFile(fs::path(R) / "f.txt") == "C");

        CHECK(checkoutBranch("feature", false, R).status == CheckoutResult::SUCCESS);
        u = undoCommit(R);
        CHECK(u.status == CommitResult::SUCCESS);
        CHECK(u.branch == "feature");
        CHECK(getBranchCommitId("feature", R) == C);
        CHECK(getBranchCommitId("main", R) == C);
        CommitResult re = redoCommit(R);
        CHECK(re.status == CommitResult::SUCCESS && re.branch == "feature");
        CHECK(getBranchCommitId("feature", R) == D);
        CHECK(redoCommit(R).status == CommitResult::NOTHING_TO_REDO);

        // A rollback moves the branch behind undo's back: the stale entry must be refused.
        CHECK(checkoutBranch("main", false, R).status == CheckoutResult::SUCCESS);
        CHECK(rollbackRepo(A, false, R).status == RollbackResult::SUCCESS);
        u = undoCommit(R);
        CHECK(u.status == CommitResult::NOTHING_TO_UNDO);
        CHECK(getBranchCommitId("main", R) == A);
        // ...but a new commit on the rolled-back branch is undoable again.
        std::string E = commitFiles(R, {{"f.txt", "E"}}, "E");
        CHECK(getBranchCommitId("main", R) == E);
        CHECK(undoCommit(R).status == CommitResult::SUCCESS);
        CHECK(getBranchCommitId("main", R) == A);
        CHECK(getBranchCommitId("feature", R) == D);
        removeDir(R);
        PASS("E002 undo/redo only apply on their own branch while it is still at the recorded commit");
    }

    // E003 (High): a deleted tracked file could never be committed as deleted.
    {
        std::string R = freshDir("temp_reg_delete");
        CHECK(initRepo(R).status == InitResult::INITIALIZED);
        std::string A = commitFiles(R, {{"a.txt", "1"}, {"b.txt", "2"}, {"d/c.txt", "3"}}, "A");
        CHECK(createBranch("old", R).status == BranchResult::CREATE_SUCCESS);

        fs::remove(fs::path(R) / "a.txt");
        std::string err;
        CHECK(checkUncommittedChanges(R, A, err) && contains(err, "deleted in working tree"));
        AddResult ar = addFile("a.txt", R);
        CHECK(ar.status == AddResult::DELETION_STAGED && ar.deletionsStaged == 1);
        CHECK(addFile("a.txt", R).status == AddResult::ALREADY_STAGED);
        CHECK(addVerify("a.txt", R).status == AddResult::IS_STAGED);
        {
            StagingArea sa(R);
            CHECK(sa.getStagedFiles().empty());
            CHECK(sa.getStagedDeletions() == std::vector<std::string>({"a.txt"}));
            CHECK(sa.size() == 1);
        }
        CHECK(checkUncommittedChanges(R, A, err) && contains(err, "staged changes"));

        CommitResult c = commitRepo("B delete a", R);
        CHECK(c.status == CommitResult::SUCCESS && c.filesCommitted == 1);
        std::string B = c.commitId;
        CommitMetadata m;
        CHECK(readCommitMeta(R, B, m));
        CHECK(m.files.empty());
        CHECK(m.deleted == std::vector<std::string>({"a.txt"}));
        auto tracked = getTrackedFilesAtCommit(R, B);
        CHECK(tracked.count("a.txt") == 0 && tracked.count("b.txt") == 1 && tracked.count("d/c.txt") == 1);
        CHECK(!checkUncommittedChanges(R, B, err));  // clean again: no phantom "deleted" file
        LogResult lr = showLog(R, false);
        CHECK(lr.status == LogResult::OK && lr.entries[0].deletedCount == 1 && lr.entries[0].fileCount == 0);

        // Branches see the deletion correctly in both directions, without --force.
        CHECK(checkoutBranch("old", false, R).status == CheckoutResult::SUCCESS);
        CHECK(readFile(fs::path(R) / "a.txt") == "1");
        CHECK(checkoutBranch("main", false, R).status == CheckoutResult::SUCCESS);
        CHECK(!fs::exists(fs::path(R) / "a.txt"));
        // Rollback back across the deletion restores the file; rolling forward removes it again.
        CHECK(rollbackRepo(A, false, R).status == RollbackResult::SUCCESS);
        CHECK(readFile(fs::path(R) / "a.txt") == "1");
        CHECK(rollbackRepo(B, false, R).status == RollbackResult::SUCCESS);
        CHECK(!fs::exists(fs::path(R) / "a.txt"));

        // `add .` stages deletions of tracked files that are gone; undo/redo keep them.
        fs::remove_all(fs::path(R) / "d");
        AddResult all = addAll(R);
        CHECK(all.status == AddResult::STAGED && all.deletionsStaged == 1);
        CHECK(contains(all.message, "1 deletion"));
        CommitResult c2 = commitRepo("C delete d/c.txt", R);
        CHECK(c2.status == CommitResult::SUCCESS);
        CHECK(getTrackedFilesAtCommit(R, c2.commitId).count("d/c.txt") == 0);
        CHECK(undoCommit(R).status == CommitResult::SUCCESS);
        {
            StagingArea sa(R);
            CHECK(sa.getStagedDeletions() == std::vector<std::string>({"d/c.txt"}));
        }
        CHECK(head(R) == B);
        CHECK(redoCommit(R).status == CommitResult::SUCCESS);
        CHECK(head(R) == c2.commitId);
        {
            StagingArea sa(R);
            CHECK(sa.size() == 0);
        }

        // A deleted directory is staged with `add <dir>`; unstaging also covers deletions.
        std::string R2 = freshDir("temp_reg_delete_dir");
        CHECK(initRepo(R2).status == InitResult::INITIALIZED);
        commitFiles(R2, {{"keep.txt", "k"}, {"src/x.txt", "x"}, {"src/y.txt", "y"}}, "A");
        fs::remove_all(fs::path(R2) / "src");
        AddResult dd = addFile("src", R2);
        CHECK(dd.status == AddResult::DELETION_STAGED && dd.deletionsStaged == 2);
        CHECK(removeFile("src", R2).status == AddResult::UNSTAGED);
        {
            StagingArea sa(R2);
            CHECK(sa.size() == 0);
        }
        CHECK(addFile("nothing_here.txt", R2).status == AddResult::NOT_FOUND);

        // A staged deletion for a file that is not tracked is refused, nothing is written.
        writeFile(fs::path(R2) / ".minigit" / "index", "deleted:never_tracked.txt\n");
        std::size_t before = countObjects(R2);
        CommitResult bad = commitRepo("bad", R2);
        CHECK(bad.status == CommitResult::INVALID_ARGUMENTS && countObjects(R2) == before);
        removeDir(R);
        removeDir(R2);
        PASS("E003 deletions are staged, committed, restored by checkout/rollback and survive undo/redo");
    }

    // E004 (High): a commit could be created on a damaged HEAD or on a branch tip that does not exist.
    {
        std::string R = freshDir("temp_reg_damaged_head");
        CHECK(initRepo(R).status == InitResult::INITIALIZED);
        std::string A = commitFiles(R, {{"a.txt", "1"}}, "A");
        fs::path headFile = fs::path(R) / ".minigit" / "HEAD";

        fs::rename(headFile, fs::path(R) / ".minigit" / "HEAD.bak");  // HEAD missing
        writeFile(fs::path(R) / "b.txt", "2");
        CHECK(addFile("b.txt", R).status == AddResult::STAGED);
        CommitResult c = commitRepo("B", R);
        CHECK(c.status == CommitResult::ERROR && contains(c.errorMessage, "HEAD"));
        CHECK(!fs::exists(headFile));  // not silently re-created
        CHECK(showLog(R, false).status == LogResult::ERROR);
        CHECK(listBranches(R).status == BranchResult::ERROR);
        CHECK(createBranch("x", R).status == BranchResult::ERROR);
        CHECK(checkoutBranch("x", false, R).status == CheckoutResult::ERROR);
        CHECK(rollbackRepo(A, false, R).status == RollbackResult::ERROR);
        CHECK(undoCommit(R).status == CommitResult::ERROR);
        CHECK(initRepo(R).status == InitResult::REINITIALIZED);  // init repairs a missing HEAD
        CHECK(fs::exists(headFile));
        CHECK(commitRepo("B", R).status == CommitResult::SUCCESS);

        writeFile(headFile, "garbage\n");  // HEAD that is not a branch reference
        writeFile(fs::path(R) / "c.txt", "3");
        CHECK(addFile("c.txt", R).status == AddResult::STAGED);
        c = commitRepo("C", R);
        CHECK(c.status == CommitResult::ERROR && contains(c.errorMessage, "damaged"));
        writeFile(headFile, "ref: refs/heads/../../x\n");  // HEAD that tries to leave refs/heads
        CHECK(commitRepo("C", R).status == CommitResult::ERROR);
        writeFile(headFile, "ref: refs/heads/main\n");

        // The branch points at a commit that does not exist.
        std::string tipBefore = head(R);
        writeFile(fs::path(R) / ".minigit" / "refs" / "heads" / "main", "deadbeefdeadbeefdeadbeefdeadbeefdeadbeef\n");
        std::size_t objects = countObjects(R);
        c = commitRepo("C", R);
        CHECK(c.status == CommitResult::ERROR && contains(c.errorMessage, "missing or damaged"));
        CHECK(countObjects(R) == objects);
        CHECK(showLog(R, false).status == LogResult::ERROR);
        CHECK(createBranch("x", R).status == BranchResult::ERROR);
        writeFile(fs::path(R) / ".minigit" / "refs" / "heads" / "main", tipBefore + "\n");
        CHECK(commitRepo("C", R).status == CommitResult::SUCCESS);
        removeDir(R);
        PASS("E004 commit, log, branch, rollback refuse a missing/damaged HEAD or branch tip");
    }

    // E005 (Medium): truncated or tampered commit metadata was accepted as a valid, empty commit.
    {
        std::string R = freshDir("temp_reg_corrupt_meta");
        CHECK(initRepo(R).status == InitResult::INITIALIZED);
        std::string A = commitFiles(R, {{"f.txt", "A"}}, "A");
        std::string B = commitFiles(R, {{"f.txt", "B"}}, "B");
        commitFiles(R, {{"f.txt", "C"}}, "C");
        fs::path objects = fs::path(R) / ".minigit" / "objects";

        fs::path metaB = objects / B / "metadata";
        std::string good = readFile(metaB);
        std::ofstream(metaB.string(), std::ios::trunc) << good.substr(0, good.find("files "));  // cut before the file list
        CommitMetadata m;
        CHECK(!readCommitMeta(R, B, m));
        // B sits in the middle: log shows C and warns, rollback stops before changing anything.
        LogResult lr = showLog(R, false);
        CHECK(lr.status == LogResult::OK && lr.entries.size() == 1 && contains(lr.warning, "incomplete"));
        CHECK(rollbackRepo(A, false, R).status == RollbackResult::UNCOMMITTED_CHANGES);  // damaged history blocks it
        CHECK(readFile(fs::path(R) / "f.txt") == "C");
        // ...with --force the intact target A is restored: the way out of a damaged history.
        RollbackResult rr = rollbackRepo(A, true, R);
        CHECK(rr.status == RollbackResult::SUCCESS && readFile(fs::path(R) / "f.txt") == "A");
        CHECK(head(R) == A);
        lr = showLog(R, false);
        CHECK(lr.status == LogResult::OK && lr.entries.size() == 1 && lr.warning.empty());

        // Branch tip damaged: nothing can be shown, and the error names the commit.
        std::string R3 = freshDir("temp_reg_corrupt_tip");
        CHECK(initRepo(R3).status == InitResult::INITIALIZED);
        std::string a3 = commitFiles(R3, {{"f.txt", "A"}}, "A");
        std::string b3 = commitFiles(R3, {{"f.txt", "B"}}, "B");
        std::ofstream((fs::path(R3) / ".minigit" / "objects" / b3 / "metadata").string(), std::ios::trunc).close();
        lr = showLog(R3, false);
        CHECK(lr.status == LogResult::ERROR && contains(lr.errorMessage, "missing or damaged"));
        CHECK(showLogGraph(R3, true).status == LogResult::OK);  // the graph skips commits it cannot read
        CHECK(rollbackRepo(b3, true, R3).status == RollbackResult::INVALID_COMMIT);
        CHECK(rollbackRepo(a3, true, R3).status == RollbackResult::SUCCESS);  // repair by rolling back to an intact commit
        CHECK(showLog(R3, false).status == LogResult::OK);

        // A parent commit missing from disk: history is cut short with a warning, never silently.
        std::string R2 = freshDir("temp_reg_missing_parent");
        CHECK(initRepo(R2).status == InitResult::INITIALIZED);
        std::string a = commitFiles(R2, {{"f.txt", "A"}, {"only_in_a.txt", "a"}}, "A");
        std::string b = commitFiles(R2, {{"f.txt", "B"}}, "B");
        CHECK(createBranch("other", R2).status == BranchResult::CREATE_SUCCESS);
        fs::rename(fs::path(R2) / ".minigit" / "objects" / a, fs::path(R2) / ".minigit" / "a_moved_away");
        lr = showLog(R2, false);
        CHECK(lr.status == LogResult::OK && lr.entries.size() == 1 && contains(lr.warning, "missing or damaged"));
        // checkout/rollback would build their file lists from this broken chain: stopped before touching anything.
        CHECK(checkoutBranch("other", true, R2).status == CheckoutResult::ERROR);
        CHECK(getCurrentBranch(R2) == "main");
        CHECK(rollbackRepo(b, true, R2).status == RollbackResult::ERROR);
        CHECK(readFile(fs::path(R2) / "f.txt") == "B");
        CHECK(head(R2) == b);
        removeDir(R);
        removeDir(R2);
        removeDir(R3);
        PASS("E005 damaged/incomplete commit history is reported and never half-applied");
    }

    // E006 (Medium): a file whose name starts with a metadata keyword corrupted the commit parser.
    {
        std::string R = freshDir("temp_reg_keyword_names");
        CHECK(initRepo(R).status == InitResult::INITIALIZED);
        FileList files = {{"message 1.txt", "m"}, {"files 3.txt", "f"}, {"parent z.txt", "p"},
                          {"deleted 9.txt", "d"}, {"timestamp 5.txt", "t"}, {"plain.txt", "x"}};
        std::string id = commitFiles(R, files, "keywords");
        CommitMetadata m;
        CHECK(readCommitMeta(R, id, m));
        CHECK(m.files.size() == 6 && m.deleted.empty() && m.message == "keywords");
        CHECK(m.timestamp.find(' ') == std::string::npos && m.parentId.empty());
        auto tracked = getTrackedFilesAtCommit(R, id);
        CHECK(tracked.size() == 6 && tracked.count("message 1.txt") == 1 && tracked.count("files 3.txt") == 1);
        std::string err;
        CHECK(!checkUncommittedChanges(R, id, err));
        LogResult lr = showLog(R, false);
        CHECK(lr.status == LogResult::OK && lr.entries[0].fileCount == 6);
        removeDir(R);
        PASS("E006 file lists are read by position, keyword-like file names are safe");
    }

    // E007 (Medium): paths from the index or from commit metadata could point outside the repository.
    {
        std::string R = freshDir("temp_reg_unsafe_paths");
        CHECK(initRepo(R).status == InitResult::INITIALIZED);
        writeFile(fs::path(R) / ".minigit" / "index",
                  "../outside.txt\nok.txt\ndeleted:../victim.txt\n/abs.txt\n.minigit/HEAD\n");
        StagingArea sa(R);
        CHECK(sa.getStagedFiles() == std::vector<std::string>({"ok.txt"}));
        CHECK(sa.getStagedDeletions().empty());

        fs::path fake = fs::path(R) / ".minigit" / "objects" / "abcdefabcdefabcdefabcdefabcdefabcdefabcd";
        writeFile(fake / "metadata",
                  "commit abcdef\nparent none\ntimestamp 1\nauthor x\nmessage evil\nfiles 1\n../evil.txt\n");
        CommitMetadata m;
        CHECK(!readCommitMeta(R, "abcdefabcdefabcdefabcdefabcdefabcdefabcd", m));

        fs::path outside = "temp_reg_outside.txt";
        writeFile(outside, "must survive");
        removeFileAndEmptyDirs(R, "../temp_reg_outside.txt");
        CHECK(fs::exists(outside));
        fs::remove(outside);
        removeDir(R);
        PASS("E007 unsafe paths are dropped from the index and rejected in commit metadata");
    }

    // E008 (Medium): nested branch names were created but not listed; deleting left folders behind.
    {
        std::string R = freshDir("temp_reg_nested_branch");
        CHECK(initRepo(R).status == InitResult::INITIALIZED);
        std::string A = commitFiles(R, {{"a.txt", "1"}}, "A");
        CHECK(createBranch("feat/login", R).status == BranchResult::CREATE_SUCCESS);
        BranchResult lb = listBranches(R);
        std::vector<std::string> names;
        for (const auto& b : lb.branches) names.push_back(b.name);
        CHECK(names == std::vector<std::string>({"feat/login", "main"}));
        CHECK(getBranchCommitId("feat/login", R) == A);
        CHECK(checkoutBranch("feat/login", false, R).status == CheckoutResult::SUCCESS);
        CHECK(getCurrentBranch(R) == "feat/login");
        CHECK(listBranches(R).branches[0].isActive);
        CHECK(checkoutBranch("main", false, R).status == CheckoutResult::SUCCESS);

        BranchResult conflict = createBranch("feat", R);  // folder 'feat/' already holds a branch
        CHECK(conflict.status == BranchResult::ALREADY_EXISTS && contains(conflict.errorMessage, "feat/"));
        CHECK(createBranch("feat/login/sub", R).status == BranchResult::ALREADY_EXISTS);  // 'feat/login' is a file

        BranchResult del = deleteBranch("feat/login", R);
        CHECK(del.status == BranchResult::DELETE_SUCCESS && del.commitId == A);
        CHECK(!fs::exists(fs::path(R) / ".minigit" / "refs" / "heads" / "feat"));  // empty folder removed
        CHECK(createBranch("feat", R).status == BranchResult::CREATE_SUCCESS);     // name is free again

        // Leftover hidden temp files from an interrupted write are not branches.
        writeFile(fs::path(R) / ".minigit" / "refs" / "heads" / ".main.tmp", "garbage");
        CHECK(listBranches(R).branches.size() == 2);
        removeDir(R);
        PASS("E008 nested branch names are listed, conflict-checked and cleaned up on delete");
    }

    // E009 (Medium): reserved Windows device names and invalid characters were accepted as branch names.
    {
        for (const char* bad : {"CON", "con.txt", "x/aux", "NUL", "com1", "LPT9", "a<b", "q|r", "a\"b", "a.lock",
                                "feat/.hid", "a@{b", "feat//x", "feat/", "-x", ".x", "a..b", "a b", "HEAD"}) {
            CHECK(!isValidBranchName(bad));
        }
        for (const char* ok : {"feature-1", "feat/login", "release_2.0", "main", "fix/a/b", "com10", "console"}) {
            CHECK(isValidBranchName(ok));
        }
        std::string R = freshDir("temp_reg_branch_names");
        CHECK(initRepo(R).status == InitResult::INITIALIZED);
        commitFiles(R, {{"a.txt", "1"}}, "A");
        CHECK(createBranch("CON", R).status == BranchResult::INVALID_NAME);
        CHECK(createBranch("a<b", R).status == BranchResult::INVALID_NAME);
        removeDir(R);
        PASS("E009 branch names that Windows cannot store are rejected up front");
    }

    // E010 (Medium): ignore rules that can never match were accepted and silently did nothing.
    {
        std::string R = freshDir("temp_reg_ignore_rules");
        CHECK(initRepo(R).status == InitResult::INITIALIZED);
        for (const char* bad : {"a/*.txt", "!keep.txt", "*", "file?.txt", "[ab].txt", "**/x"}) {
            IgnoreResult r = ignoreAdd(bad, R);
            CHECK(r.status == IgnoreResult::ERROR);
            CHECK(contains(r.message, "unsupported"));
        }
        CHECK(ignoreAdd("*.log", R).status == IgnoreResult::ADDED);
        CHECK(ignoreAdd("build/", R).status == IgnoreResult::ADDED);
        CHECK(ignoreAdd("secret.env", R).status == IgnoreResult::ADDED);
        CHECK(IgnoreManager(R).lines().size() == 3);  // rejected patterns were not written
        int code = 0;
        std::string out = runCli({"minigit", "ignore", "src/*.cpp"}, R, code);
        CHECK(code == 1 && contains(out, "unsupported"));
        removeDir(R);
        PASS("E010 unsupported ignore patterns are refused with an explanation");
    }

    // E011 (Medium): an ignore rule hid a tracked file, so its changes could never be staged or committed.
    {
        std::string R = freshDir("temp_reg_ignore_tracked");
        CHECK(initRepo(R).status == InitResult::INITIALIZED);
        commitFiles(R, {{"app.log", "v1"}, {"logs/a.log", "x"}, {"keep.txt", "k"}}, "A");
        CHECK(ignoreAdd("*.log", R).status == IgnoreResult::ADDED);
        writeFile(fs::path(R) / "app.log", "v2");
        writeFile(fs::path(R) / "new.log", "untracked");
        CHECK(addFile("app.log", R).status == AddResult::STAGED);     // tracked: the ignore rule does not apply
        CHECK(addFile("new.log", R).status == AddResult::IGNORED);    // untracked: still ignored
        CommitResult c = commitRepo("update app.log", R);
        CHECK(c.status == CommitResult::SUCCESS && c.filesCommitted == 1);
        std::string err;
        CHECK(!checkUncommittedChanges(R, c.commitId, err));
        writeFile(fs::path(R) / "logs" / "a.log", "changed");
        AddResult all = addAll(R);
        CHECK(all.skippedCount == 1);  // only the untracked new.log is skipped
        StagingArea sa(R);
        CHECK(sa.isStaged("logs/a.log") && sa.isStaged("app.log") && !sa.isStaged("new.log"));
        removeDir(R);
        PASS("E011 tracked files stay stageable when an ignore rule matches them");
    }

    // E012 (Low): short, non-hex and ambiguous commit ids gave a misleading "does not exist".
    {
        std::string R = freshDir("temp_reg_resolve");
        CHECK(initRepo(R).status == InitResult::INITIALIZED);
        std::string A = commitFiles(R, {{"f.txt", "A"}}, "A");
        std::string B = commitFiles(R, {{"f.txt", "B"}}, "B");
        CHECK(resolveCommitId(R, A.substr(0, 3)).empty());   // too short to be safe
        CHECK(resolveCommitId(R, "../..").empty());
        CHECK(resolveCommitId(R, "zzzzzz").empty());
        CHECK(resolveCommitId(R, A.substr(0, 7)) == A);
        std::string upper = A.substr(0, 8);
        std::transform(upper.begin(), upper.end(), upper.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        CHECK(resolveCommitId(R, upper) == A);

        for (const char* suffix : {"1", "2"}) {
            fs::path fake = fs::path(R) / ".minigit" / "objects" / ("abcd" + std::string(35, '0') + suffix);
            writeFile(fake / "metadata", "x");
        }
        bool ambiguous = false;
        CHECK(resolveCommitId(R, "abcd", &ambiguous).empty() && ambiguous);
        RollbackResult rr = rollbackRepo("abcd", false, R);
        CHECK(rr.status == RollbackResult::INVALID_COMMIT && contains(rr.errorMessage, "ambiguous"));
        std::string unused = "0000";  // a 4-digit prefix that matches neither real commit
        for (const char* p : {"0000", "1111", "2222", "3333"}) {
            if (A.rfind(p, 0) != 0 && B.rfind(p, 0) != 0) {
                unused = p;
                break;
            }
        }
        rr = rollbackRepo(unused, false, R);
        CHECK(rr.status == RollbackResult::INVALID_COMMIT && contains(rr.errorMessage, "does not exist"));
        removeDir(R);
        PASS("E012 commit id prefixes are validated and ambiguity is reported");
    }

    // E013 (Medium): repository files were truncated and rewritten in place (a failed write destroyed them).
    {
        std::string dir = freshDir("temp_reg_atomic");
        fs::path f = fs::path(dir) / "ref";
        CHECK(writeFileAtomic(f.string(), "one\n"));
        CHECK(readFile(f) == "one\n");
        CHECK(writeFileAtomic(f.string(), "two\n"));
        CHECK(readFile(f) == "two\n");
        CHECK(!fs::exists(fs::path(dir) / ".ref.tmp"));
        writeFile(fs::path(dir) / ".ref.tmp", "junk from an interrupted write");
        CHECK(writeFileAtomic(f.string(), "three"));
        CHECK(readFile(f) == "three" && !fs::exists(fs::path(dir) / ".ref.tmp"));
        CHECK(!writeFileAtomic((fs::path(dir) / "no_such_dir" / "x").string(), "z"));
        CHECK(!fs::exists(fs::path(dir) / "no_such_dir"));
        fs::create_directories(fs::path(dir) / "d1");
        CHECK(!writeFileAtomic((fs::path(dir) / "d1").string(), "x"));  // cannot replace a directory
        CHECK(fs::is_directory(fs::path(dir) / "d1") && !fs::exists(fs::path(dir) / ".d1.tmp"));
        removeDir(dir);

        // The real writers leave no temp files behind and keep the repository readable.
        std::string R = freshDir("temp_reg_atomic_repo");
        CHECK(initRepo(R).status == InitResult::INITIALIZED);
        commitFiles(R, {{"a.txt", "1"}}, "A");
        CHECK(createBranch("b", R).status == BranchResult::CREATE_SUCCESS);
        CHECK(ignoreAdd("*.tmp", R).status == IgnoreResult::ADDED);
        for (const auto& e : fs::recursive_directory_iterator(fs::path(R))) {
            std::string name = e.path().filename().string();
            CHECK(!(name.size() > 4 && name.compare(name.size() - 4, 4, ".tmp") == 0 && name[0] == '.'));
        }
        removeDir(R);
        PASS("E013 repository files are replaced atomically");
    }

    // E014: undo history written by older versions (no branch fields) still loads and works; new fields round-trip.
    {
        std::string R = freshDir("temp_reg_history_compat");
        CHECK(initRepo(R).status == InitResult::INITIALIZED);
        std::string A = commitFiles(R, {{"f.txt", "A"}}, "A");
        writeFile(fs::path(R) / ".minigit" / "history", "UNDO_COUNT 1\n- - 1700000000 1\nA\nf.txt\nREDO_COUNT 0\n");
        {
            CommitHistory h(R);
            CHECK(h.undoDeque().size() == 1 && h.undoDeque().back().branch.empty());
            CHECK(h.undoDeque().back().stagedFiles == std::vector<std::string>({"f.txt"}));
        }
        CHECK(undoCommit(R).status == CommitResult::SUCCESS);  // legacy entry checked by parentage
        CHECK(head(R).empty());
        {
            CommitHistory h(R);
            CHECK(h.canRedo() && h.redoDeque().back().branch == "main");
            CHECK(h.redoDeque().back().commitId == A);
            CHECK(h.redoDeque().back().expectedHead.empty());  // the branch has no commit after this undo
        }
        CHECK(redoCommit(R).status == CommitResult::SUCCESS);
        CHECK(head(R) == A);

        // A legacy entry that does not describe HEAD is refused (it would jump to another history).
        std::string B = commitFiles(R, {{"f.txt", "B"}}, "B");
        writeFile(fs::path(R) / ".minigit" / "history",
                  "UNDO_COUNT 1\n" + std::string(40, 'a') + " - 1700000000 1\nX\nf.txt\nREDO_COUNT 0\n");
        CommitResult u = undoCommit(R);
        CHECK(u.status == CommitResult::NOTHING_TO_UNDO && head(R) == B);
        removeDir(R);
        PASS("E014 old history files load; legacy entries are validated by parentage");
    }

    // E015 (Low): command-line behaviour: ignore outside a repo, help for --force, message wording.
    {
        std::string R = freshDir("temp_reg_cli");
        int code = 0;
        std::string out = runCli({"minigit", "ignore", "x.txt"}, R, code);
        CHECK(code == 1 && contains(out, "not a minigit repository"));
        CHECK(!fs::exists(fs::path(R) / ".minigitignore"));  // no stray rule file outside a repository

        out = runCli({"minigit", "init"}, R, code);
        CHECK(code == 0 && !out.empty());
        out = runCli({"minigit", "help", "checkout"}, R, code);
        CHECK(code == 0 && contains(out, "--force"));
        out = runCli({"minigit", "help"}, R, code);
        CHECK(contains(out, "checkout <branch-name> --force"));

        writeFile(fs::path(R) / "a.txt", "1");
        runCli({"minigit", "add", "a.txt"}, R, code);
        out = runCli({"minigit", "remove", "."}, R, code);
        CHECK(code == 0 && contains(out, "Unstaged 1 file") && !contains(out, "'.'"));
        runCli({"minigit", "add", "a.txt"}, R, code);
        out = runCli({"minigit", "commit", "-m", "A"}, R, code);
        CHECK(code == 0 && contains(out, "[main "));

        fs::remove(fs::path(R) / "a.txt");
        out = runCli({"minigit", "add", "a.txt"}, R, code);
        CHECK(code == 0 && contains(out, "Staged deletion of 'a.txt'"));
        out = runCli({"minigit", "add", "-v", "a.txt"}, R, code);
        CHECK(contains(out, "is staged for deletion"));
        out = runCli({"minigit", "commit", "-m", "remove a"}, R, code);
        CHECK(code == 0 && contains(out, "1 file(s) committed"));
        out = runCli({"minigit", "log"}, R, code);
        CHECK(contains(out, "(0 file(s), 1 deleted)"));

        out = runCli({"minigit", "branch", "temp"}, R, code);
        CHECK(code == 0);
        out = runCli({"minigit", "branch", "-d", "temp"}, R, code);
        CHECK(code == 0 && contains(out, "Deleted branch 'temp' (was "));
        out = runCli({"minigit", "branch", "temp/"}, R, code);
        CHECK(code == 1 && contains(out, "not a valid branch name"));

        out = runCli({"minigit", "rollback", "zz"}, R, code);
        CHECK(code == 1 && contains(out, "does not exist"));

        out = runCli({"minigit", "undo"}, R, code);
        CHECK(code == 0 && contains(out, "[main "));
        removeDir(R);
        PASS("E015 CLI: ignore needs a repo, help lists --force, clear messages for deletions/branches");
    }

    std::cout << "All regression tests passed (" << tc::checkCount() << " checks).\n";
    return 0;
}
