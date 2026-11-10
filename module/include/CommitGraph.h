#pragma once
// Commit DAG (Directed Acyclic Graph) architecture for MiniGit.
// Represents commits as nodes, parent-child relationships as directed edges,
// and branches as named pointers to commit nodes.
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace minigit {

struct CommitGraphNode {
    std::string id;
    std::vector<std::string> parents;    // Parent commit IDs
    std::vector<std::string> children;   // Child commit IDs (DAG adjacency)
    std::string message;
    std::string timestamp;
    std::string author;
    std::vector<std::string> files;
    std::vector<std::string> deleted;    // Tracked files this commit removed
    std::vector<std::string> branchRefs; // Branch names pointing to this commit
    bool isHead = false;                 // True if active branch / HEAD points here
};

class CommitGraph {
public:
    explicit CommitGraph(const std::string& root = ".");

    // Builds the DAG from disk (.minigit/objects and .minigit/refs/heads)
    bool build();

    // Node & commit queries
    bool hasCommit(const std::string& id) const;
    const CommitGraphNode* getNode(const std::string& id) const;
    std::vector<std::string> getAllCommitIds() const;

    // Branch references
    std::vector<std::pair<std::string, std::string>> getAllBranches() const;
    std::string getActiveBranch() const { return activeBranch_; }
    std::string getActiveCommitId() const { return activeCommitId_; }

    // Traversal algorithms
    // Returns commits in topological order (newest first or oldest first)
    std::vector<std::string> getTopologicalOrder(bool oldestFirst = false) const;

    // Returns all ancestor commits reachable from a starting commit (avoids duplicates)
    std::vector<std::string> getReachableFrom(const std::string& commitId) const;

    // Returns all commits reachable across all branch references (union of all branches)
    std::vector<std::string> getAllReachable() const;

    // Detects whether the commit graph contains any cycles (must be a valid DAG)
    bool hasCycle() const;

    // Resolves a commit ID or prefix (e.g. 7-char short ID) against known nodes
    std::string resolvePrefix(const std::string& prefix) const;

    // Renders the DAG into an ASCII graph representation for `minigit log --all --graph`
    std::string renderGraph() const;

    std::size_t size() const { return nodes_.size(); }
    bool empty() const { return nodes_.empty(); }

private:
    std::string root_;
    std::unordered_map<std::string, CommitGraphNode> nodes_; // Adjacency list & node storage
    std::unordered_map<std::string, std::string> branches_;  // branchName -> commitId
    std::string activeBranch_;
    std::string activeCommitId_;
};

}  // namespace minigit

