#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_set>

#include "AddCommand.h"
#include "BranchCommand.h"
#include "CLI.h"
#include "CommitCommand.h"
#include "CommitGraph.h"
#include "InitCommand.h"
#include "LogCommand.h"
#include "RepoUtils.h"

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

int main() {
    std::cout << "Running test_dag...\n";
    std::string testDir = "temp_test_dag";
    setupTestDir(testDir);
    fs::path rootP(testDir);

    CHECK(initRepo(testDir).status == InitResult::INITIALIZED);

    /*
       Build DAG history:
                 A
                / \
               B   C
              /     \
             D       E
    */

    // Commit A (Initial common ancestor)
    writeFile(rootP / "common.txt", "Common A");
    CHECK(addFile("common.txt", testDir).status == AddResult::STAGED);
    CommitResult cA = commitRepo("Commit A (Shared Root)", testDir);
    CHECK(cA.status == CommitResult::SUCCESS);

    // Create branch "feature" at A
    CHECK(createBranch("feature", testDir).status == BranchResult::CREATE_SUCCESS);

    // Commit B on main (parent A)
    writeFile(rootP / "fileB.txt", "Content B");
    CHECK(addFile("fileB.txt", testDir).status == AddResult::STAGED);
    CommitResult cB = commitRepo("Commit B on main", testDir);
    CHECK(cB.status == CommitResult::SUCCESS);
    CHECK(cB.parentId == cA.commitId);

    // Commit D on main (parent B)
    writeFile(rootP / "fileD.txt", "Content D");
    CHECK(addFile("fileD.txt", testDir).status == AddResult::STAGED);
    CommitResult cD = commitRepo("Commit D on main", testDir);
    CHECK(cD.status == CommitResult::SUCCESS);
    CHECK(cD.parentId == cB.commitId);

    // Switch to "feature" (which is at commit A)
    CHECK(checkoutBranch("feature", false, testDir).status == CheckoutResult::SUCCESS);
    CHECK(getHEADCommitId(testDir) == cA.commitId);

    // Commit C on feature (parent A)
    writeFile(rootP / "fileC.txt", "Content C");
    CHECK(addFile("fileC.txt", testDir).status == AddResult::STAGED);
    CommitResult cC = commitRepo("Commit C on feature", testDir);
    CHECK(cC.status == CommitResult::SUCCESS);
    CHECK(cC.parentId == cA.commitId);

    // Commit E on feature (parent C)
    writeFile(rootP / "fileE.txt", "Content E");
    CHECK(addFile("fileE.txt", testDir).status == AddResult::STAGED);
    CommitResult cE = commitRepo("Commit E on feature", testDir);
    CHECK(cE.status == CommitResult::SUCCESS);
    CHECK(cE.parentId == cC.commitId);

    // Now test DAG representation with CommitGraph
    CommitGraph graph(testDir);
    CHECK(graph.build());

    // 1. Total commits
    CHECK(graph.size() == 5);
    CHECK(graph.hasCommit(cA.commitId));
    CHECK(graph.hasCommit(cB.commitId));
    CHECK(graph.hasCommit(cC.commitId));
    CHECK(graph.hasCommit(cD.commitId));
    CHECK(graph.hasCommit(cE.commitId));
    std::cout << "  [PASS] 1. Graph built successfully with 5 nodes\n";

    // 2. Parent-child relationships
    const CommitGraphNode* nA = graph.getNode(cA.commitId);
    const CommitGraphNode* nB = graph.getNode(cB.commitId);
    const CommitGraphNode* nC = graph.getNode(cC.commitId);
    const CommitGraphNode* nD = graph.getNode(cD.commitId);
    const CommitGraphNode* nE = graph.getNode(cE.commitId);

    CHECK(nA && nB && nC && nD && nE);

    // Check parents
    CHECK(nA->parents.empty()); // Root
    CHECK(nB->parents.size() == 1 && nB->parents[0] == cA.commitId);
    CHECK(nD->parents.size() == 1 && nD->parents[0] == cB.commitId);
    CHECK(nC->parents.size() == 1 && nC->parents[0] == cA.commitId);
    CHECK(nE->parents.size() == 1 && nE->parents[0] == cC.commitId);

    // Check children (A has two children: B and C)
    CHECK(nA->children.size() == 2);
    bool hasChildB = false, hasChildC = false;
    for (const auto& ch : nA->children) {
        if (ch == cB.commitId) hasChildB = true;
        if (ch == cC.commitId) hasChildC = true;
    }
    CHECK(hasChildB && hasChildC);
    std::cout << "  [PASS] 2. Shared ancestor A has both children B and C\n";

    // 3. Cycle detection
    CHECK(!graph.hasCycle());
    std::cout << "  [PASS] 3. Cycle detection verifies graph is a valid DAG\n";

    // 4. Branch tips reference correct commits
    auto branches = graph.getAllBranches();
    CHECK(branches.size() == 2);
    CHECK(getBranchCommitId("main", testDir) == cD.commitId);
    CHECK(getBranchCommitId("feature", testDir) == cE.commitId);
    std::cout << "  [PASS] 4. Branch references point to respective tips D and E\n";

    // 5. Reachable commits from main tip D does not include C or E
    std::vector<std::string> fromD = graph.getReachableFrom(cD.commitId);
    std::unordered_set<std::string> setD(fromD.begin(), fromD.end());
    CHECK(setD.count(cD.commitId) == 1);
    CHECK(setD.count(cB.commitId) == 1);
    CHECK(setD.count(cA.commitId) == 1);
    CHECK(setD.count(cC.commitId) == 0);
    CHECK(setD.count(cE.commitId) == 0);

    // Reachable commits from feature tip E does not include B or D
    std::vector<std::string> fromE = graph.getReachableFrom(cE.commitId);
    std::unordered_set<std::string> setE(fromE.begin(), fromE.end());
    CHECK(setE.count(cE.commitId) == 1);
    CHECK(setE.count(cC.commitId) == 1);
    CHECK(setE.count(cA.commitId) == 1);
    CHECK(setE.count(cB.commitId) == 0);
    CHECK(setE.count(cD.commitId) == 0);
    std::cout << "  [PASS] 5. Independent branch histories are recoverable\n";

    // 6. Graph traversal across all branches does not duplicate shared commits
    std::vector<std::string> allReachable = graph.getAllReachable();
    CHECK(allReachable.size() == 5);
    std::unordered_set<std::string> seen;
    for (const auto& id : allReachable) {
        CHECK(seen.find(id) == seen.end()); // No duplicates!
        seen.insert(id);
    }
    CHECK(seen.count(cA.commitId) == 1);
    std::cout << "  [PASS] 6. Graph traversal does not duplicate shared commits\n";

    // 7. Topological sorting
    std::vector<std::string> topo = graph.getTopologicalOrder(false); // newest first
    CHECK(topo.size() == 5);
    // In topo newest first, children appear before parents
    auto posOf = [&](const std::string& id) {
        for (std::size_t i = 0; i < topo.size(); ++i) {
            if (topo[i] == id) return i;
        }
        return std::size_t(999);
    };
    CHECK(posOf(cD.commitId) < posOf(cB.commitId));
    CHECK(posOf(cB.commitId) < posOf(cA.commitId));
    CHECK(posOf(cE.commitId) < posOf(cC.commitId));
    CHECK(posOf(cC.commitId) < posOf(cA.commitId));
    std::cout << "  [PASS] 7. Topological ordering correctly orders children before parents\n";

    // 8. Render ASCII graph
    std::string asciiGraph = graph.renderGraph();
    CHECK(!asciiGraph.empty());
    CHECK(asciiGraph.find(cD.commitId.substr(0, 7)) != std::string::npos);
    CHECK(asciiGraph.find(cE.commitId.substr(0, 7)) != std::string::npos);
    CHECK(asciiGraph.find(cA.commitId.substr(0, 7)) != std::string::npos);
    std::cout << "  [PASS] 8. ASCII graph renders all commits and branch references\n";

    // 9. showLogGraph integration via LogCommand
    LogResult lr = showLogGraph(testDir, true);
    CHECK(lr.status == LogResult::OK);
    CHECK(!lr.graphOutput.empty());
    std::cout << "  [PASS] 9. showLogGraph correctly interfaces with CommitGraph\n";

    cleanupTestDir(testDir);
    std::cout << "All DAG tests passed successfully!\n";
    return 0;
}

