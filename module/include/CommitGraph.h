#pragma once
// Commit graph (DAG) with parent links (Snehil's Phase 3 module).
//
// Every commit stores its parents (one normally, two for a merge commit).
// The graph is rebuilt from disk by walking parent links from every branch tip
// (and from HEAD when it is detached) with a queue and a visited set, so a
// corrupt parent cycle cannot loop forever.
//
// Used by:  merge (nearestCommonAncestor), switch (is a detached commit safe to leave?),
//           log --graph (topologicalOrder + branchTips).
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace minigit {

struct GraphNode {
    std::string id;
    std::vector<std::string> parents;   // parents[0] = first parent
    std::vector<std::string> children;  // filled in by the graph
    std::string message;
    std::string timestamp;  // unix seconds, as a string
    std::vector<std::string> files;
};

class CommitGraph {
public:
    explicit CommitGraph(const std::string& rootDir = ".");

    // Reads the graph from disk. includeHead=false walks from branch tips only
    // (used to ask "does any branch still reach this commit?").
    // Returns false when root is not a repository OR a reachable branch/commit
    // reference is missing or malformed; it never reports a partial graph as OK.
    bool load(bool includeHead = true);

    // In-memory insertion, used by tests and by load(). Parents that are not in
    // the graph yet are fine; call finalize() once all commits are added.
    void addCommit(const GraphNode& node);
    void finalize();  // drops dangling parents, preserves cycles for detection, fills children

    bool contains(const std::string& id) const { return nodes_.count(id) > 0; }
    const GraphNode* find(const std::string& id) const;
    std::size_t size() const { return nodes_.size(); }

    // True if `ancestor` can be reached from `descendant` by following parents.
    // A commit counts as its own ancestor.
    bool isAncestor(const std::string& ancestor, const std::string& descendant) const;

    // Best common ancestor of two commits. Breadth-first searches find each
    // ancestor set; ancestors that are themselves ancestors of another common
    // ancestor are discarded. If multiple incomparable merge bases remain,
    // the newer timestamp wins, then the smaller id for deterministic output.
    // Returns "" when histories are unrelated.
    // If one commit is an ancestor of the other, that commit is returned.
    std::string nearestCommonAncestor(const std::string& a, const std::string& b) const;

    // All commits, newest first, every commit before its parents. For an
    // in-memory graph containing a cycle, returns only the acyclic portion;
    // compare the result size with size() to detect that invalid graph.
    std::vector<std::string> topologicalOrder() const;

    // Branch name -> tip commit id (the Map from the proposal).
    const std::map<std::string, std::string>& branchTips() const { return branchTips_; }
    const std::string& headBranch() const { return headBranch_; }  // "" when detached
    const std::string& headId() const { return headId_; }          // "" when no commits

private:
    std::string root_;
    std::map<std::string, GraphNode> nodes_;
    std::map<std::string, std::string> branchTips_;
    std::string headBranch_;
    std::string headId_;
};

}  // namespace minigit
