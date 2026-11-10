// Commit graph (DAG): parent links, nearest common ancestor, topological order,
// and the drawing produced by `log --graph`.
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "AddCommand.h"
#include "BranchCommand.h"
#include "CLI.h"
#include "CommitCommand.h"
#include "CommitGraph.h"
#include "InitCommand.h"
#include "LogCommand.h"
#include "RepoStore.h"

using namespace minigit;
namespace fs = std::filesystem;

#define CHECK(c)                                                           \
    do {                                                                   \
        if (!(c)) {                                                        \
            std::cerr << "FAIL line " << __LINE__ << ": " #c << std::endl; \
            std::exit(1);                                                  \
        }                                                                  \
    } while (0)

static GraphNode node(const std::string& id, std::vector<std::string> parents, const std::string& ts) {
    GraphNode n;
    n.id = id;
    n.parents = parents;
    n.timestamp = ts;
    n.message = "msg " + id;
    return n;
}

static void writeFile(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p.string(), std::ios::binary) << s;
}

static std::string commitFile(const fs::path& rootP, const std::string& name, const std::string& text,
                              const std::string& msg) {
    writeFile(rootP / name, text);
    CHECK(addFile(name, rootP.string()).status == AddResult::STAGED);
    CommitResult c = commitRepo(msg, rootP.string());
    CHECK(c.status == CommitResult::SUCCESS);
    return c.commitId;
}

static std::size_t indexOf(const std::vector<std::string>& v, const std::string& s) {
    return static_cast<std::size_t>(std::find(v.begin(), v.end(), s) - v.begin());
}

int main() {
    // ---- in-memory graph:  A - B - D(merge B,C) - E          main = E
    //                          \- C -/                           (A is the root)
    //                    and an unrelated root X
    {
        CommitGraph g;
        g.addCommit(node("A", {}, "100"));
        g.addCommit(node("B", {"A"}, "110"));
        g.addCommit(node("C", {"A"}, "120"));
        g.addCommit(node("D", {"B", "C"}, "130"));
        g.addCommit(node("E", {"D"}, "140"));
        g.addCommit(node("X", {}, "50"));
        g.finalize();

        CHECK(g.size() == 6);
        CHECK(g.find("D")->parents.size() == 2);
        CHECK(g.find("A")->children.size() == 2);
        std::cout << "  [PASS] 1. parent and child links, merge commit with two parents\n";

        CHECK(g.isAncestor("A", "E") && g.isAncestor("B", "D") && g.isAncestor("C", "E"));
        CHECK(!g.isAncestor("E", "A") && !g.isAncestor("B", "C") && !g.isAncestor("X", "E"));
        CHECK(g.isAncestor("D", "D"));
        std::cout << "  [PASS] 2. isAncestor follows every parent\n";

        CHECK(g.nearestCommonAncestor("B", "C") == "A");
        CHECK(g.nearestCommonAncestor("E", "B") == "B");  // one is an ancestor of the other
        CHECK(g.nearestCommonAncestor("B", "B") == "B");
        CHECK(g.nearestCommonAncestor("D", "E") == "D");
        CHECK(g.nearestCommonAncestor("E", "X").empty());  // unrelated histories
        CHECK(g.nearestCommonAncestor("E", "nope").empty());
        std::cout << "  [PASS] 3. nearest common ancestor (diverged, nested, same, unrelated)\n";

        std::vector<std::string> order = g.topologicalOrder();
        CHECK(order.size() == 6);
        CHECK(indexOf(order, "E") < indexOf(order, "D"));
        CHECK(indexOf(order, "D") < indexOf(order, "B") && indexOf(order, "D") < indexOf(order, "C"));
        CHECK(indexOf(order, "B") < indexOf(order, "A") && indexOf(order, "C") < indexOf(order, "A"));
        std::cout << "  [PASS] 4. topological order lists children before parents\n";
    }

    // ---- criss-cross: M1 and M2 both merge B and C. Their nearest common ancestors are B and C
    //      (distance 1 each); the answer must be deterministic: newer commit (C) wins.
    {
        CommitGraph g;
        g.addCommit(node("A", {}, "1"));
        g.addCommit(node("B", {"A"}, "2"));
        g.addCommit(node("C", {"A"}, "3"));
        g.addCommit(node("M1", {"B", "C"}, "4"));
        g.addCommit(node("M2", {"C", "B"}, "5"));
        g.finalize();
        CHECK(g.nearestCommonAncestor("M1", "M2") == "C");
        std::cout << "  [PASS] 5. criss-cross merge gives a deterministic ancestor\n";
    }

    // ---- in-memory graph corruption is detectable: dangling parents are
    // removed by finalize(), while a cycle prevents a complete topological order.
    {
        CommitGraph g;
        g.addCommit(node("P", {"Q"}, "1"));
        g.addCommit(node("Q", {"P", "GONE"}, "2"));
        g.addCommit(node("SELF", {"SELF"}, "3"));
        g.finalize();
        CHECK(g.find("Q")->parents.size() == 1);  // GONE dropped
        CHECK(g.find("SELF")->parents.size() == 1);  // self-cycle retained for detection
        CHECK(g.topologicalOrder().size() < g.size());
        std::cout << "  [PASS] 6. in-memory cycle is detected by incomplete topological order\n";
    }

    // ---- a merge-DAG case where minimum summed distance chooses the wrong
    //      (older) common ancestor. The maximal common ancestor must win.
    {
        CommitGraph g;
        g.addCommit(node("A", {}, "1"));
        g.addCommit(node("B", {"A"}, "2"));
        g.addCommit(node("X1", {"B"}, "3"));
        g.addCommit(node("X2", {"X1"}, "4"));
        g.addCommit(node("T1", {"A", "X2"}, "5"));
        g.addCommit(node("T2", {"B"}, "6"));
        g.finalize();
        CHECK(g.nearestCommonAncestor("T1", "T2") == "B");
        std::cout << "  [PASS] 7. maximal common ancestor beats an older shortcut ancestor\n";
    }

    // ---- real repository
    fs::path rootP = fs::temp_directory_path() / "minigit_test_graph_repo";
    fs::remove_all(rootP);
    fs::create_directories(rootP);
    const std::string R = rootP.string();

    CHECK(showGraph(R).status == GraphResult::NOT_A_REPO);
    CHECK(initRepo(R).status == InitResult::INITIALIZED);
    CHECK(showGraph(R).status == GraphResult::EMPTY);

    writeFile(rootP / ".minigit" / "HEAD", "malformed head\n");
    CommitGraph malformedHead(R);
    CHECK(!malformedHead.load());
    CHECK(malformedHead.size() == 0);
    writeFile(rootP / ".minigit" / "HEAD", "ref: refs/heads/main\n");
    std::cout << "  [PASS] malformed HEAD is rejected by graph loading\n";

    std::string c1 = commitFile(rootP, "a.txt", "a1", "first");
    CHECK(createBranch("feature", R).status == BranchResult::CREATED);

    // main: c1 - c2        feature: c1 - f1 - f2
    std::string c2 = commitFile(rootP, "m.txt", "m", "main work");
    CHECK(switchBranch("feature", R).status == BranchResult::SWITCHED);
    std::string f1 = commitFile(rootP, "f1.txt", "f", "feature one");
    std::string f2 = commitFile(rootP, "f2.txt", "f", "feature two");
    CHECK(switchBranch("main", R).status == BranchResult::SWITCHED);

    CommitGraph g(R);
    CHECK(g.load());
    CHECK(g.size() == 4);
    CHECK(g.branchTips().at("main") == c2 && g.branchTips().at("feature") == f2);
    CHECK(g.headBranch() == "main" && g.headId() == c2);
    CHECK(g.find(f1)->parents.size() == 1 && g.find(f1)->parents[0] == c1);
    CHECK(g.nearestCommonAncestor(c2, f2) == c1);
    CHECK(g.nearestCommonAncestor(f1, f2) == f1);
    CHECK(g.isAncestor(c1, f2) && !g.isAncestor(c2, f2));
    std::cout << "  [PASS] 8. graph loaded from disk; common ancestor of two branches is the fork point\n";

    CommitGraph onlyBranches(R);
    CHECK(onlyBranches.load(false) && onlyBranches.size() == 4);
    std::cout << "  [PASS] 9. load(false) walks branch tips only\n";

    // log --graph: two branches forked at c1
    GraphResult gr = showGraph(R);
    CHECK(gr.status == GraphResult::OK);
    std::vector<std::string> shown;
    for (const GraphRow& row : gr.rows)
        if (!row.commitId.empty()) shown.push_back(row.commitId);
    CHECK(shown.size() == 4);
    CHECK(indexOf(shown, f2) < indexOf(shown, f1) && indexOf(shown, f1) < indexOf(shown, c1));
    CHECK(indexOf(shown, c2) < indexOf(shown, c1));
    bool sawConverge = false, sawHeadLabel = false, sawFeatureLabel = false;
    for (const GraphRow& row : gr.rows) {
        if (row.commitId.empty() && row.graph == "|/") sawConverge = true;
        if (row.commitId == c2 && row.decoration == "HEAD -> main") sawHeadLabel = true;
        if (row.commitId == f2 && row.decoration == "feature") sawFeatureLabel = true;
    }
    CHECK(sawConverge && sawHeadLabel && sawFeatureLabel);
    CHECK(gr.rows.back().commitId == c1 && gr.rows.back().graph == "*");
    std::cout << "  [PASS] 10. log --graph: order, branch labels and the join at the fork\n";

    // Merge commit written the way Krish's merge will write it: "parent2" line in the metadata.
    std::string mergeId = "0123456789abcdef0123456789abcdef01234567";
    fs::path obj = rootP / ".minigit" / "objects" / mergeId;
    fs::create_directories(obj / "snapshot");
    std::ofstream(obj / "metadata") << "commit " << mergeId << "\nparent " << c2 << "\nparent2 " << f2
                                    << "\ntimestamp 9999999999\nauthor t\nmessage merge feature\nfiles 0\n";
    CHECK(repo::writeBranch(R, "main", mergeId));

    CommitGraph gm(R);
    CHECK(gm.load());
    CHECK(gm.size() == 5);
    CHECK(gm.find(mergeId)->parents.size() == 2);
    CHECK(gm.find(mergeId)->parents[0] == c2 && gm.find(mergeId)->parents[1] == f2);
    CHECK(gm.isAncestor(f2, mergeId) && gm.isAncestor(c2, mergeId));
    CHECK(gm.nearestCommonAncestor(mergeId, f2) == f2);
    std::cout << "  [PASS] 11. merge commit: both parents read from metadata\n";

    gr = showGraph(R);
    CHECK(gr.status == GraphResult::OK);
    CHECK(gr.rows.size() >= 6);
    CHECK(gr.rows[0].commitId == mergeId && gr.rows[0].graph == "*");
    CHECK(gr.rows[0].decoration == "HEAD -> main");
    CHECK(gr.rows[1].commitId.empty() && gr.rows[1].graph == "|\\");  // the merge opens a second lane
    int commitRows = 0;
    for (const GraphRow& row : gr.rows) commitRows += row.commitId.empty() ? 0 : 1;
    CHECK(commitRows == 5);
    std::cout << "  [PASS] 12. log --graph draws the merge as '*' then '|\\'\n";

    // The plain `log` (first-parent chain through CommitLog) still works with a merge commit.
    LogResult lr = showLog(R);
    CHECK(lr.status == LogResult::OK && lr.entries.size() == 3);  // merge, c2, c1
    std::cout << "  [PASS] 13. plain log follows the first parent through a merge\n";

    // CLI
    char a0[] = "minigit", a1[] = "log", a2[] = "--graph", a3[] = "--bogus";
    char* argv1[] = {a0, a1, a2};
    CHECK(dispatch(3, argv1, R) == 0);
    char* argv2[] = {a0, a1, a3};
    CHECK(dispatch(3, argv2, R) == 1);
    std::cout << "  [PASS] 14. CLI: log --graph\n";

    // A reachable parent cycle must be rejected rather than displayed as a DAG.
    const std::string cycleId = "9999999999999999999999999999999999999999";
    fs::path cycleMeta = rootP / ".minigit" / "objects" / cycleId / "metadata";
    writeFile(cycleMeta, "commit " + cycleId + "\nparent " + cycleId +
                           "\ntimestamp 9999999999\nauthor test\nmessage cycle\nfiles 0\n");
    CHECK(repo::writeBranch(R, "main", cycleId));
    CommitGraph cyclic(R);
    CHECK(!cyclic.load());
    CHECK(cyclic.size() == 0 && cyclic.branchTips().empty());
    CHECK(repo::writeBranch(R, "main", mergeId));
    std::cout << "  [PASS] 15. graph load rejects a reachable parent cycle\n";

    // A reachable missing object must not be hidden behind a partial graph.
    fs::remove(rootP / ".minigit" / "objects" / c1 / "metadata");
    CommitGraph broken(R);
    CHECK(!broken.load());
    CHECK(broken.size() == 0);
    std::cout << "  [PASS] 16. graph load rejects a missing reachable commit\n";

    fs::remove_all(rootP);
    std::cout << "test_graph: all tests passed\n";
    return 0;
}
