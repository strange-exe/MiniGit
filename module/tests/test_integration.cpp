// End-to-end tests: realistic workflows (scenarios A-F of the audit plan) that combine
// init, staging, commit, log, undo/redo, rollback, branches and the commit DAG, plus
// invalid-operation and repository-integrity checks.
#include <algorithm>
#include <map>
#include <set>

#include "AddCommand.h"
#include "BranchCommand.h"
#include "CommitCommand.h"
#include "CommitGraph.h"
#include "CommitLog.h"
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

static std::string commitFiles(const std::string& root, const FileList& files, const std::string& msg) {
    for (const auto& f : files) {
        writeFile(fs::path(root) / f.first, f.second);
        CHECK(addFile(f.first, root).status == AddResult::STAGED);
    }
    CommitResult r = commitRepo(msg, root);
    CHECK(r.status == CommitResult::SUCCESS);
    return r.commitId;
}

// Commit "L": f.txt holds the letter, x_L.txt is a file that exists from commit L onwards.
static std::string commitLetter(const std::string& root, const std::string& L) {
    return commitFiles(root, {{"f.txt", L}, {"x_" + L + ".txt", "file " + L}}, L);
}

static std::string treeOf(const std::string& root) {
    std::string out = "f=" + (fs::exists(fs::path(root) / "f.txt") ? readFile(fs::path(root) / "f.txt") : "-") + " files=";
    std::set<std::string> names;
    for (const auto& e : fs::directory_iterator(root)) {
        std::string n = e.path().filename().string();
        if (n.rfind("x_", 0) == 0) names.insert(n);
    }
    for (const auto& n : names) out += n + ",";
    return out;
}

// Content of every file under .minigit: used to prove that read-only commands change nothing.
static std::map<std::string, std::string> repoDigest(const std::string& root) {
    std::map<std::string, std::string> d;
    for (const auto& e : fs::recursive_directory_iterator(fs::path(root) / ".minigit")) {
        if (e.is_regular_file()) d[e.path().generic_string()] = readFile(e.path());
    }
    return d;
}

static std::vector<std::string> messagesOf(const std::string& root) {
    std::vector<std::string> out;
    LogResult lr = showLog(root, false);
    CHECK(lr.status == LogResult::OK);
    for (const auto& e : lr.entries) out.push_back(e.message);
    return out;
}

static std::string head(const std::string& root) { return getHEADCommitId(root); }

int main() {
    std::cout << "Running test_integration...\n";

    // Initialization: structure, re-init safety, damaged targets.
    {
        std::string R = freshDir("temp_int_init");
        InitResult r = initRepo(R);
        CHECK(r.status == InitResult::INITIALIZED);
        fs::path g = fs::path(R) / ".minigit";
        CHECK(fs::is_directory(g / "objects") && fs::is_directory(g / "refs" / "heads"));
        CHECK(readFile(g / "HEAD") == "ref: refs/heads/main\n");
        CHECK(fs::exists(g / "index") && fs::exists(fs::path(R) / ".minigitignore"));
        CHECK(showLog(R, false).status == LogResult::EMPTY);
        CHECK(listBranches(R).branches.size() == 1 && listBranches(R).branches[0].name == "main");

        commitFiles(R, {{"a.txt", "1"}}, "A");
        writeFile(g / "HEAD", "ref: refs/heads/other\n");
        writeFile(fs::path(R) / ".minigitignore", "*.log\n");
        auto before = repoDigest(R);
        CHECK(initRepo(R).status == InitResult::REINITIALIZED);
        CHECK(repoDigest(R) == before);  // re-init changes nothing
        CHECK(readFile(fs::path(R) / ".minigitignore") == "*.log\n");

        std::string F = freshDir("temp_int_init_file");
        writeFile(fs::path(F) / ".minigit", "i am a file");
        CHECK(initRepo(F).status == InitResult::ERROR);
        CHECK(readFile(fs::path(F) / ".minigit") == "i am a file");
        std::string P = freshDir("temp_int_init_partial");
        fs::create_directories(fs::path(P) / ".minigit");  // only the folder exists
        CHECK(initRepo(P).status == InitResult::REINITIALIZED);
        CHECK(fs::is_directory(fs::path(P) / ".minigit" / "refs" / "heads") && fs::exists(fs::path(P) / ".minigit" / "HEAD"));
        removeDir(R);
        removeDir(F);
        removeDir(P);
        PASS("Init: structure, idempotent re-init, regular file or half-built .minigit");
    }

    // Commands outside a repository fail cleanly and create nothing.
    {
        std::string R = freshDir("temp_int_norepo");
        CHECK(addFile("a.txt", R).status == AddResult::ERROR);
        CHECK(addAll(R).status == AddResult::ERROR);
        CHECK(removeFile("a.txt", R).status == AddResult::ERROR);
        CHECK(commitRepo("m", R).status == CommitResult::NOT_A_REPO);
        CHECK(undoCommit(R).status == CommitResult::NOT_A_REPO);
        CHECK(redoCommit(R).status == CommitResult::NOT_A_REPO);
        CHECK(showLog(R, false).status == LogResult::NOT_A_REPO);
        CHECK(showLogGraph(R, true).status == LogResult::NOT_A_REPO);
        CHECK(listBranches(R).status == BranchResult::NOT_A_REPO);
        CHECK(createBranch("x", R).status == BranchResult::NOT_A_REPO);
        CHECK(deleteBranch("x", R).status == BranchResult::NOT_A_REPO);
        CHECK(checkoutBranch("x", false, R).status == CheckoutResult::NOT_A_REPO);
        CHECK(rollbackRepo("abcd", false, R).status == RollbackResult::NOT_A_REPO);
        CHECK(fs::is_empty(R));
        removeDir(R);
        PASS("Every command refuses to run outside a repository and leaves the folder empty");
    }

    // Scenario A: basic workflow, special characters in messages, "restart" by re-reading everything from disk.
    {
        std::string R = freshDir("temp_int_a");
        CHECK(initRepo(R).status == InitResult::INITIALIZED);
        writeFile(fs::path(R) / "a.txt", "one");
        writeFile(fs::path(R) / "src" / "deep" / "b.txt", "bee");
        CHECK(addFile("a.txt", R).status == AddResult::STAGED);
        AddResult dir = addFile("src", R);
        CHECK(dir.status == AddResult::STAGED && dir.stagedCount == 1);
        const std::string msg1 = "first: \"quoted\" 'single' $HOME 100% ^ & ; | < > !";
        CommitResult c1 = commitRepo(msg1, R);
        CHECK(c1.status == CommitResult::SUCCESS && c1.filesCommitted == 2 && c1.parentId.empty() && c1.branch == "main");
        CHECK(c1.commitId.size() == 40);

        writeFile(fs::path(R) / "a.txt", "two");
        CHECK(addFile("a.txt", R).status == AddResult::STAGED);
        CommitResult c2 = commitRepo("second", R);
        CHECK(c2.status == CommitResult::SUCCESS && c2.parentId == c1.commitId && c2.commitId != c1.commitId);
        CHECK(commitRepo("third", R).status == CommitResult::NOTHING_TO_COMMIT);  // empty staging area

        // Restart: nothing is kept in memory, every module re-reads the repository.
        CommitLog log(R);
        CHECK(log.load() && log.size() == 2 && log.head()->id == c1.commitId && log.tail()->id == c2.commitId);
        CHECK(log.tail()->parentId == c1.commitId && log.head()->parentId.empty());
        CHECK(log.find(c1.commitId)->message == msg1 && log.find(c2.commitId)->files.size() == 1);
        CommitGraph graph(R);
        CHECK(graph.build() && graph.size() == 2 && !graph.hasCycle());
        CHECK(graph.getNode(c2.commitId)->parents == std::vector<std::string>({c1.commitId}));
        CHECK(graph.getNode(c1.commitId)->children == std::vector<std::string>({c2.commitId}));
        CHECK(head(R) == c2.commitId && getCurrentBranch(R) == "main");
        auto tracked = getTrackedFilesAtCommit(R, c2.commitId);
        CHECK(tracked.size() == 2 && readFile(tracked["a.txt"]) == "two" && readFile(tracked["src/deep/b.txt"]) == "bee");
        CHECK(readFile(fs::path(R) / ".minigit" / "objects" / c1.commitId / "snapshot" / "a.txt") == "one");
        CHECK(messagesOf(R) == std::vector<std::string>({"second", msg1}));

        // Log commands are read-only and reverse order really is oldest-first.
        auto digest = repoDigest(R);
        LogResult rev = showLog(R, true);
        CHECK(rev.status == LogResult::OK && rev.entries.size() == 2 && rev.entries[0].message == msg1);
        CHECK(rev.entries[1].parentId == c1.commitId && rev.entries[0].parentId.empty());
        CHECK(showLogGraph(R, true).status == LogResult::OK);
        CHECK(repoDigest(R) == digest);
        CHECK(!formatTimestamp(rev.entries[0].timestamp).empty() && formatTimestamp("abc") == "abc");

        std::string ids = c1.commitId + c2.commitId;
        CHECK(ids.find_first_not_of("0123456789abcdef") == std::string::npos);
        removeDir(R);
        PASS("Scenario A: stage, commit, modify, commit, log, restart, verify data");
    }

    // Scenario B: branch divergence and the shape of the DAG (two lines of work off one shared ancestor).
    {
        std::string R = freshDir("temp_int_b");
        CHECK(initRepo(R).status == InitResult::INITIALIZED);
        std::string A = commitLetter(R, "A");
        std::string B = commitLetter(R, "B");
        CHECK(createBranch("feature", R).status == BranchResult::CREATE_SUCCESS);
        CHECK(getBranchCommitId("feature", R) == B);
        CHECK(checkoutBranch("feature", false, R).status == CheckoutResult::SUCCESS);
        std::string C = commitLetter(R, "C");
        std::string D = commitLetter(R, "D");
        CHECK(getBranchCommitId("main", R) == B);  // a commit on feature never advances main
        CHECK(checkoutBranch("main", false, R).status == CheckoutResult::SUCCESS);
        CHECK(treeOf(R) == "f=B files=x_A.txt,x_B.txt,");
        std::string E = commitLetter(R, "E");
        std::string F = commitLetter(R, "F");
        CHECK(getBranchCommitId("feature", R) == D && getBranchCommitId("main", R) == F);

        // Graph:   A - B - C - D   (feature)
        //                 \ E - F  (main)
        CommitGraph g(R);
        CHECK(g.build() && g.size() == 6 && !g.hasCycle());
        auto parentsOf = [&](const std::string& id) { return g.getNode(id)->parents; };
        CHECK(parentsOf(A).empty());
        CHECK(parentsOf(B) == std::vector<std::string>({A}));
        CHECK(parentsOf(C) == std::vector<std::string>({B}) && parentsOf(E) == std::vector<std::string>({B}));
        CHECK(parentsOf(D) == std::vector<std::string>({C}) && parentsOf(F) == std::vector<std::string>({E}));
        std::set<std::string> kids(g.getNode(B)->children.begin(), g.getNode(B)->children.end());
        CHECK(kids == std::set<std::string>({C, E}) && g.getNode(B)->children.size() == 2);
        CHECK(g.getNode(A)->children == std::vector<std::string>({B}));
        CHECK(g.getNode(D)->children.empty() && g.getNode(F)->children.empty());
        CHECK(g.getNode(F)->isHead && !g.getNode(D)->isHead && g.getActiveBranch() == "main");
        CHECK(g.getNode(D)->branchRefs == std::vector<std::string>({"feature"}));

        std::vector<std::string> reach = g.getReachableFrom(D);
        CHECK(reach.size() == 4 && std::set<std::string>(reach.begin(), reach.end()).size() == 4);  // no duplicates
        CHECK(std::set<std::string>(reach.begin(), reach.end()) == std::set<std::string>({A, B, C, D}));
        std::vector<std::string> all = g.getAllReachable();
        CHECK(all.size() == 6 && std::set<std::string>(all.begin(), all.end()).size() == 6);
        std::vector<std::string> topo = g.getTopologicalOrder(false);
        CHECK(topo.size() == 6);
        std::map<std::string, std::size_t> pos;
        for (std::size_t i = 0; i < topo.size(); ++i) pos[topo[i]] = i;
        for (const auto& id : topo) {
            for (const auto& p : parentsOf(id)) CHECK(pos[id] < pos[p]);  // children always before parents
        }
        std::vector<std::string> oldestFirst = g.getTopologicalOrder(true);
        CHECK(oldestFirst.front() == A && oldestFirst.size() == 6);
        CHECK(g.resolvePrefix(D.substr(0, 8)) == D && g.resolvePrefix("").empty());

        std::string art = g.renderGraph();
        CHECK(contains(art, "HEAD -> main") && contains(art, "(feature)") && contains(art, "|/"));
        for (const auto& id : {A, B, C, D, E, F}) {
            std::size_t first = art.find("commit " + id.substr(0, 7));
            CHECK(first != std::string::npos && art.find("commit " + id.substr(0, 7), first + 1) == std::string::npos);
        }

        // Two branches may reference the same commit.
        CHECK(createBranch("alias", R).status == BranchResult::CREATE_SUCCESS);
        CommitGraph g2(R);
        CHECK(g2.build() && g2.getNode(F)->branchRefs.size() == 2);
        CHECK(deleteBranch("alias", R).status == BranchResult::DELETE_SUCCESS);

        // Exact file restoration when switching, repeatedly, in both directions.
        for (int i = 0; i < 3; ++i) {
            CHECK(checkoutBranch("feature", false, R).status == CheckoutResult::SUCCESS);
            CHECK(treeOf(R) == "f=D files=x_A.txt,x_B.txt,x_C.txt,x_D.txt,");
            CHECK(readFile(fs::path(R) / "x_C.txt") == "file C");
            CHECK(checkoutBranch("main", false, R).status == CheckoutResult::SUCCESS);
            CHECK(treeOf(R) == "f=F files=x_A.txt,x_B.txt,x_E.txt,x_F.txt,");
        }
        // Both histories read independently.
        CHECK(messagesOf(R) == std::vector<std::string>({"F", "E", "B", "A"}));
        CHECK(checkoutBranch("feature", false, R).status == CheckoutResult::SUCCESS);
        CHECK(messagesOf(R) == std::vector<std::string>({"D", "C", "B", "A"}));
        removeDir(R);
        PASS("Scenario B: divergent branches, DAG structure, shared ancestor, exact restoration");
    }

    // DAG robustness: a tampered repository (cycle, missing parent) can never hang or crash the traversals.
    {
        std::string R = freshDir("temp_int_dag_bad");
        CHECK(initRepo(R).status == InitResult::INITIALIZED);
        std::string A = commitLetter(R, "A");
        std::string B = commitLetter(R, "B");
        fs::path metaA = fs::path(R) / ".minigit" / "objects" / A / "metadata";
        std::string text = readFile(metaA);
        std::string missing(40, 'a');
        std::string withMissing = text;
        withMissing.replace(withMissing.find("parent none"), 11, "parent " + missing);
        std::ofstream(metaA.string(), std::ios::trunc) << withMissing;
        CommitGraph g(R);
        CHECK(g.build() && g.size() == 2 && !g.hasCycle());  // missing parent: tolerated, not followed
        CHECK(g.getReachableFrom(B).size() == 2 && g.getTopologicalOrder(false).size() == 2);
        CHECK(!g.renderGraph().empty());
        CHECK(showLog(R, false).status == LogResult::OK && showLog(R, false).entries.size() == 2);

        std::string cyclic = text;
        cyclic.replace(cyclic.find("parent none"), 11, "parent " + B);  // A's parent is B: A <-> B
        std::ofstream(metaA.string(), std::ios::trunc) << cyclic;
        CommitGraph g2(R);
        CHECK(g2.build() && g2.hasCycle());
        CHECK(g2.getReachableFrom(B).size() == 2 && g2.getTopologicalOrder(false).size() == 2);
        CHECK(!g2.renderGraph().empty());
        LogResult lr = showLog(R, false);
        CHECK(lr.status == LogResult::OK && lr.entries.size() == 2 && contains(lr.warning, "loops"));
        std::string err;
        bool complete = true;
        getTrackedFilesAtCommit(R, B, &complete, &err);
        CHECK(!complete && contains(err, "cycle"));
        CHECK(rollbackRepo(A, true, R).status == RollbackResult::ERROR);  // refuses to act on a looping history
        removeDir(R);
        PASS("DAG: cycles and dangling parents are detected or tolerated, traversal always terminates");
    }

    // Scenario C: rollback with branching.
    {
        std::string R = freshDir("temp_int_c");
        CHECK(initRepo(R).status == InitResult::INITIALIZED);
        std::string A = commitLetter(R, "A");
        std::string B = commitLetter(R, "B");
        std::string C = commitLetter(R, "C");
        std::string D = commitLetter(R, "D");
        CHECK(createBranch("feature", R).status == BranchResult::CREATE_SUCCESS);
        RollbackResult rb = rollbackRepo(B.substr(0, 7), false, R);
        CHECK(rb.status == RollbackResult::SUCCESS && rb.commitId == B && rb.branch == "main");
        CHECK(rb.filesRemoved == 2 && rb.filesRestored == 3);
        CHECK(treeOf(R) == "f=B files=x_A.txt,x_B.txt,");
        CHECK(head(R) == B && getBranchCommitId("feature", R) == D);

        std::string E = commitLetter(R, "E");
        CommitMetadata m;
        CHECK(readCommitMeta(R, E, m) && m.parentId == B);
        CHECK(fs::exists(fs::path(R) / ".minigit" / "objects" / C / "metadata"));  // C and D are still stored
        CHECK(fs::exists(fs::path(R) / ".minigit" / "objects" / D / "snapshot" / "f.txt"));
        CHECK(messagesOf(R) == std::vector<std::string>({"E", "B", "A"}));
        CHECK(checkoutBranch("feature", false, R).status == CheckoutResult::SUCCESS);
        CHECK(treeOf(R) == "f=D files=x_A.txt,x_B.txt,x_C.txt,x_D.txt,");
        CHECK(messagesOf(R) == std::vector<std::string>({"D", "C", "B", "A"}));
        CHECK(checkoutBranch("main", false, R).status == CheckoutResult::SUCCESS);
        CHECK(treeOf(R) == "f=E files=x_A.txt,x_B.txt,x_E.txt,");

        // Both lines of history are in the DAG: E and C hang off B.
        CommitGraph g(R);
        CHECK(g.build() && g.size() == 5);
        std::set<std::string> kids(g.getNode(B)->children.begin(), g.getNode(B)->children.end());
        CHECK(kids == std::set<std::string>({C, E}));

        // Rollback on the other branch moves only that branch.
        CHECK(checkoutBranch("feature", false, R).status == CheckoutResult::SUCCESS);
        CHECK(rollbackRepo(A, false, R).status == RollbackResult::SUCCESS);
        CHECK(getBranchCommitId("feature", R) == A && getBranchCommitId("main", R) == E);
        CHECK(treeOf(R) == "f=A files=x_A.txt,");
        // A commit on top of the rolled-back root, then back and forth across it.
        std::string G = commitLetter(R, "G");
        CHECK(readCommitMeta(R, G, m) && m.parentId == A);
        CHECK(rollbackRepo(A, false, R).status == RollbackResult::SUCCESS);  // back to the root
        CHECK(rollbackRepo(G, false, R).status == RollbackResult::SUCCESS);  // forward again to an unreferenced commit
        CHECK(head(R) == G && treeOf(R) == "f=G files=x_A.txt,x_G.txt,");
        removeDir(R);
        PASS("Scenario C: rollback keeps old commits, other branches and the DAG intact");
    }

    // Rollback safety: staged and uncommitted changes, deleted files, failed rollback leaves nothing half-done.
    {
        std::string R = freshDir("temp_int_rb_safety");
        CHECK(initRepo(R).status == InitResult::INITIALIZED);
        std::string A = commitLetter(R, "A");
        std::string B = commitLetter(R, "B");
        auto digest = repoDigest(R);

        writeFile(fs::path(R) / "f.txt", "dirty");
        RollbackResult r = rollbackRepo(A, false, R);
        CHECK(r.status == RollbackResult::UNCOMMITTED_CHANGES && contains(r.errorMessage, "f.txt"));
        CHECK(readFile(fs::path(R) / "f.txt") == "dirty" && head(R) == B && repoDigest(R) == digest);
        writeFile(fs::path(R) / "f.txt", "B");

        fs::remove(fs::path(R) / "x_A.txt");
        r = rollbackRepo(A, false, R);
        CHECK(r.status == RollbackResult::UNCOMMITTED_CHANGES && contains(r.errorMessage, "deleted"));
        CHECK(head(R) == B);
        r = rollbackRepo(A, true, R);
        CHECK(r.status == RollbackResult::SUCCESS && fs::exists(fs::path(R) / "x_A.txt"));

        writeFile(fs::path(R) / "new.txt", "staged");
        CHECK(addFile("new.txt", R).status == AddResult::STAGED);
        CHECK(rollbackRepo(B, false, R).status == RollbackResult::UNCOMMITTED_CHANGES);
        CHECK(head(R) == A);
        CHECK(rollbackRepo(B, true, R).status == RollbackResult::SUCCESS);
        CHECK(StagingArea(R).size() == 0 && fs::exists(fs::path(R) / "new.txt"));  // untracked file is kept

        // A missing snapshot file is detected before anything is changed.
        std::string C = commitLetter(R, "C");
        fs::remove(fs::path(R) / ".minigit" / "objects" / A / "snapshot" / "x_A.txt");
        digest = repoDigest(R);
        std::string treeBefore = treeOf(R);
        r = rollbackRepo(A, true, R);
        CHECK(r.status == RollbackResult::ERROR && contains(r.errorMessage, "snapshot"));
        CHECK(head(R) == C && treeOf(R) == treeBefore && repoDigest(R) == digest);
        CHECK(checkoutBranch("main", true, R).status == CheckoutResult::ALREADY_ON_BRANCH);
        removeDir(R);
        PASS("Rollback: refuses to lose work, --force overrides, a broken target changes nothing");
    }

    // Scenario D: undo/redo interacting with branches and rollback.
    {
        std::string R = freshDir("temp_int_d");
        CHECK(initRepo(R).status == InitResult::INITIALIZED);
        std::string A = commitLetter(R, "A");
        std::string B = commitLetter(R, "B");
        std::string C = commitLetter(R, "C");

        // Undo and redo move HEAD but leave the working tree and the commit objects alone.
        CommitResult u = undoCommit(R);
        CHECK(u.status == CommitResult::SUCCESS && head(R) == B);
        CHECK(fs::exists(fs::path(R) / ".minigit" / "objects" / C / "metadata"));
        CHECK(StagingArea(R).getStagedFiles() == std::vector<std::string>({"f.txt", "x_C.txt"}));
        CHECK(readFile(fs::path(R) / "f.txt") == "C");
        CHECK(checkoutBranch("main", false, R).status == CheckoutResult::ALREADY_ON_BRANCH);
        CHECK(redoCommit(R).status == CommitResult::SUCCESS && head(R) == C);
        CHECK(StagingArea(R).size() == 0);

        // Undo twice, redo once, then a new commit throws the redo history away.
        CHECK(undoCommit(R).status == CommitResult::SUCCESS && undoCommit(R).status == CommitResult::SUCCESS);
        CHECK(head(R) == A);
        CHECK(redoCommit(R).status == CommitResult::SUCCESS && head(R) == B);
        fs::remove(fs::path(R) / "x_C.txt");  // undo left C's file behind in the working tree
        CHECK(rollbackRepo(B, true, R).status == RollbackResult::SUCCESS);  // clean tree for the next steps
        std::string D = commitLetter(R, "D");
        CHECK(redoCommit(R).status == CommitResult::NOTHING_TO_REDO);
        CHECK(messagesOf(R) == std::vector<std::string>({"D", "B", "A"}));

        // Branch, commit elsewhere, come back, change things, roll back: everything stays consistent.
        CHECK(createBranch("dev", R).status == BranchResult::CREATE_SUCCESS);
        CHECK(checkoutBranch("dev", false, R).status == CheckoutResult::SUCCESS);
        std::string E = commitLetter(R, "E");
        CHECK(undoCommit(R).status == CommitResult::SUCCESS && getBranchCommitId("dev", R) == D);
        CHECK(redoCommit(R).status == CommitResult::SUCCESS && getBranchCommitId("dev", R) == E);
        CHECK(checkoutBranch("main", false, R).status == CheckoutResult::SUCCESS);
        CHECK(undoCommit(R).status == CommitResult::NOTHING_TO_UNDO);  // newest entry belongs to dev
        CHECK(head(R) == D && getBranchCommitId("dev", R) == E);
        std::string G = commitLetter(R, "G");
        CHECK(rollbackRepo(B, false, R).status == RollbackResult::SUCCESS);
        CHECK(getBranchCommitId("main", R) == B && getBranchCommitId("dev", R) == E);
        CHECK(undoCommit(R).status == CommitResult::NOTHING_TO_UNDO);  // rollback moved the branch, so undo is refused
        CHECK(treeOf(R) == "f=B files=x_A.txt,x_B.txt,");

        CommitGraph g(R);
        CHECK(g.build() && g.size() == 6 && !g.hasCycle());  // A B C D E G all still stored
        CHECK(g.getNode(D)->children.size() == 2);            // E (dev) and G (main) both hang off D
        CHECK(g.getNode(C)->children.empty() && g.getNode(B)->children.size() == 2);  // C and D both hang off B
        (void)A;
        (void)G;
        removeDir(R);
        PASS("Scenario D: undo/redo, branch switching and rollback keep refs, files and DAG consistent");
    }

    // Scenario E: persistence. Every call below uses brand-new objects, like a fresh process.
    {
        std::string R = freshDir("temp_int_e");
        CHECK(initRepo(R).status == InitResult::INITIALIZED);
        commitLetter(R, "A");
        commitLetter(R, "B");
        std::string C = commitLetter(R, "C");
        CHECK(createBranch("alpha", R).status == BranchResult::CREATE_SUCCESS);
        CHECK(createBranch("beta", R).status == BranchResult::CREATE_SUCCESS);
        CHECK(checkoutBranch("alpha", false, R).status == CheckoutResult::SUCCESS);
        std::string D = commitLetter(R, "D");
        CHECK(rollbackRepo(C, false, R).status == RollbackResult::SUCCESS);  // alpha back to C

        int code = 0;
        std::string out = runCli({"minigit", "branch"}, R, code);
        CHECK(code == 0 && contains(out, "* alpha (" + C.substr(0, 7) + ")") && contains(out, "  beta (" + C.substr(0, 7) + ")"));
        CHECK(contains(out, "  main (" + C.substr(0, 7) + ")"));
        CHECK(getCurrentBranch(R) == "alpha" && head(R) == C);
        CHECK(getBranchCommitId("main", R) == C && getBranchCommitId("beta", R) == C);
        CHECK(treeOf(R) == "f=C files=x_A.txt,x_B.txt,x_C.txt,");
        CHECK(messagesOf(R) == std::vector<std::string>({"C", "B", "A"}));
        CHECK(fs::exists(fs::path(R) / ".minigit" / "objects" / D / "metadata"));  // stored, but no branch reaches it
        out = runCli({"minigit", "log", "--reverse"}, R, code);
        CHECK(code == 0 && out.find("    A") < out.find("    C"));
        out = runCli({"minigit", "log", "--all", "--graph"}, R, code);
        CHECK(code == 0 && contains(out, "HEAD -> alpha") && contains(out, "beta") && contains(out, "main"));
        CHECK(!contains(out, "commit " + D.substr(0, 7)));  // the graph shows what the branches reach
        CHECK(readFile(fs::path(R) / ".minigit" / "HEAD") == "ref: refs/heads/alpha\n");
        removeDir(R);
        PASS("Scenario E: HEAD, branch refs, history, files and graph survive a restart");
    }

    // Scenario F: invalid operations never corrupt the repository.
    {
        std::string R = freshDir("temp_int_f");
        CHECK(initRepo(R).status == InitResult::INITIALIZED);
        CHECK(commitRepo("empty", R).status == CommitResult::NOTHING_TO_COMMIT);
        CHECK(rollbackRepo("abcd", false, R).status == RollbackResult::NO_COMMITS);
        CHECK(undoCommit(R).status == CommitResult::NOTHING_TO_UNDO && redoCommit(R).status == CommitResult::NOTHING_TO_REDO);
        CHECK(createBranch("x", R).status == BranchResult::NO_COMMITS);
        CHECK(checkoutBranch("x", false, R).status == CheckoutResult::NOT_FOUND);
        CHECK(addFile("missing.txt", R).status == AddResult::NOT_FOUND);
        CHECK(addFile("../outside.txt", R).status == AddResult::ERROR);
        CHECK(addFile(".minigit/HEAD", R).status == AddResult::IGNORED);  // internal files are never tracked
        CHECK(commitRepo("", R).status == CommitResult::INVALID_ARGUMENTS);
        CHECK(commitRepo("  \n ", R).status == CommitResult::INVALID_ARGUMENTS);

        commitLetter(R, "A");
        commitLetter(R, "B");
        auto digest = repoDigest(R);
        CHECK(rollbackRepo("nonexistent", false, R).status == RollbackResult::INVALID_COMMIT);
        CHECK(rollbackRepo("", false, R).status == RollbackResult::INVALID_COMMIT);
        CHECK(createBranch("main", R).status == BranchResult::ALREADY_EXISTS);
        CHECK(createBranch("bad name", R).status == BranchResult::INVALID_NAME);
        CHECK(createBranch("feat", R).status == BranchResult::CREATE_SUCCESS);
        CHECK(createBranch("feat", R).status == BranchResult::ALREADY_EXISTS);
        CHECK(deleteBranch("main", R).status == BranchResult::CANNOT_DELETE_ACTIVE);
        CHECK(deleteBranch("ghost", R).status == BranchResult::NOT_FOUND);
        CHECK(checkoutBranch("ghost", false, R).status == CheckoutResult::NOT_FOUND);
        CHECK(checkoutBranch("../ghost", false, R).status == CheckoutResult::INVALID_NAME);
        CHECK(checkoutBranch("main", false, R).status == CheckoutResult::ALREADY_ON_BRANCH);
        // Only `feat` was created above, so the repository differs by exactly that one ref file.
        CHECK(repoDigest(R).size() == digest.size() + 1);

        // Unsafe checkout: uncommitted modification, staged change, colliding untracked file.
        CHECK(checkoutBranch("feat", false, R).status == CheckoutResult::SUCCESS);
        std::string C = commitLetter(R, "C");  // feat is now ahead: C only exists there
        CHECK(checkoutBranch("main", false, R).status == CheckoutResult::SUCCESS);
        writeFile(fs::path(R) / "f.txt", "local edit");
        CheckoutResult cr = checkoutBranch("feat", false, R);
        CHECK(cr.status == CheckoutResult::UNCOMMITTED_CHANGES && getCurrentBranch(R) == "main");
        CHECK(readFile(fs::path(R) / "f.txt") == "local edit");
        writeFile(fs::path(R) / "f.txt", "B");
        writeFile(fs::path(R) / "staged.txt", "s");
        CHECK(addFile("staged.txt", R).status == AddResult::STAGED);
        CHECK(checkoutBranch("feat", false, R).status == CheckoutResult::UNCOMMITTED_CHANGES);
        CHECK(removeFile("staged.txt", R).status == AddResult::UNSTAGED);
        writeFile(fs::path(R) / "x_C.txt", "my own untracked data");  // would be overwritten by feat's x_C.txt
        cr = checkoutBranch("feat", false, R);
        CHECK(cr.status == CheckoutResult::UNCOMMITTED_CHANGES && contains(cr.errorMessage, "untracked"));
        CHECK(readFile(fs::path(R) / "x_C.txt") == "my own untracked data" && getCurrentBranch(R) == "main");
        CHECK(checkoutBranch("feat", true, R).status == CheckoutResult::SUCCESS);  // --force overrides
        CHECK(readFile(fs::path(R) / "x_C.txt") == "file C" && head(R) == C);
        CHECK(deleteBranch("main", R).status == BranchResult::DELETE_SUCCESS);
        CHECK(listBranches(R).branches.size() == 1);
        removeDir(R);
        PASS("Scenario F: invalid operations are rejected without touching the repository");
    }

    // Ignore rules: persistence, files, directories, extensions, nesting, CRLF rule files, internals never staged.
    {
        std::string R = freshDir("temp_int_ignore");
        CHECK(initRepo(R).status == InitResult::INITIALIZED);
        CHECK(ignoreAdd("*.log", R).status == IgnoreResult::ADDED);
        CHECK(ignoreAdd("build/", R).status == IgnoreResult::ADDED);
        CHECK(ignoreAdd("secret.env", R).status == IgnoreResult::ADDED);
        CHECK(ignoreAdd("secret.env", R).status == IgnoreResult::ALREADY_PRESENT);
        // New IgnoreManager objects read the rules back from .minigitignore.
        CHECK(ignoreVerify("a.log", R).status == IgnoreResult::IGNORED);
        CHECK(ignoreVerify("deep/er/b.log", R).status == IgnoreResult::IGNORED);
        CHECK(ignoreVerify("deep/er/b.log", R).lineNumber == 1);
        CHECK(ignoreVerify("build/out/x.o", R).matchedPattern == "build/");
        CHECK(ignoreVerify("src/build/x.o", R).status == IgnoreResult::NOT_IGNORED);  // dir rules are anchored at the root
        CHECK(ignoreVerify("secret.env", R).lineNumber == 3);
        CHECK(ignoreVerify("secret.env.bak", R).status == IgnoreResult::NOT_IGNORED);
        CHECK(ignoreVerify("..", R).status == IgnoreResult::ERROR && ignoreVerify("../x", R).status == IgnoreResult::ERROR);

        writeFile(fs::path(R) / "keep.txt", "k");
        writeFile(fs::path(R) / "a.log", "l");
        writeFile(fs::path(R) / "build" / "o.o", "o");
        writeFile(fs::path(R) / "src" / "ok.cpp", "c");
        writeFile(fs::path(R) / "src" / "x.log", "l");
        AddResult all = addAll(R);
        CHECK(all.status == AddResult::STAGED && all.skippedCount == 3);  // a.log, build/, src/x.log
        StagingArea sa(R);
        CHECK(sa.isStaged("keep.txt") && sa.isStaged("src/ok.cpp") && !sa.isStaged("a.log") && !sa.isStaged("build/o.o"));
        for (const auto& f : sa.getStagedFiles()) CHECK(f != ".minigit" && f.rfind(".minigit/", 0) != 0);  // internals never tracked

        CHECK(ignoreRemove("*.log", R).status == IgnoreResult::REMOVED);
        CHECK(ignoreVerify("a.log", R).status == IgnoreResult::NOT_IGNORED);
        CHECK(ignoreVerify("secret.env", R).lineNumber == 2);  // later rules move up one line
        CHECK(ignoreRemove("*.log", R).status == IgnoreResult::NOT_PRESENT);
        writeFile(fs::path(R) / ".minigitignore", "*.zip\r\n# comment\r\n\r\nfoo.txt\r\n");  // CRLF file with comments
        CHECK(ignoreVerify("x.zip", R).lineNumber == 1 && ignoreVerify("foo.txt", R).lineNumber == 4);
        CHECK(ignoreVerify("# comment", R).status == IgnoreResult::NOT_IGNORED);
        removeDir(R);
        PASS("Ignore: files, directories, extensions, persistence, CRLF files, internals never staged");
    }

    std::cout << "All integration tests passed (" << tc::checkCount() << " checks).\n";
    return 0;
}
