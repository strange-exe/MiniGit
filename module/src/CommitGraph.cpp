#include "CommitGraph.h"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <set>

#include "RepoStore.h"

namespace fs = std::filesystem;

namespace minigit {

static long long stamp(const GraphNode& n) { return std::atoll(n.timestamp.c_str()); }

// "a is a better pick than b": newer first, then smaller id.
static bool newerFirst(const GraphNode* a, const GraphNode* b) {
    if (stamp(*a) != stamp(*b)) return stamp(*a) > stamp(*b);
    return a->id < b->id;
}

CommitGraph::CommitGraph(const std::string& rootDir) : root_(rootDir) {}

void CommitGraph::addCommit(const GraphNode& node) {
    GraphNode copy = node;
    copy.children.clear();
    nodes_[copy.id] = copy;
}

void CommitGraph::finalize() {
    for (auto& kv : nodes_) kv.second.children.clear();
    for (auto& kv : nodes_) {
        GraphNode& n = kv.second;
        std::vector<std::string> kept;
        for (const std::string& p : n.parents) {
            if (nodes_.count(p) && std::find(kept.begin(), kept.end(), p) == kept.end())
                kept.push_back(p);
        }
        n.parents = kept;
    }
    for (auto& kv : nodes_)
        for (const std::string& p : kv.second.parents) nodes_[p].children.push_back(kv.first);
}

bool CommitGraph::load(bool includeHead) {
    nodes_.clear();
    branchTips_.clear();
    headBranch_.clear();
    headId_.clear();

    auto fail = [this]() {
        nodes_.clear();
        branchTips_.clear();
        headBranch_.clear();
        headId_.clear();
        return false;
    };

    std::error_code ec;
    if (!fs::is_directory(fs::path(root_) / ".minigit", ec) || ec) return fail();

    const std::string rawHead = repo::readHeadRaw(root_);
    if (rawHead.empty()) return fail();
    headBranch_ = repo::currentBranch(root_);
    headId_ = repo::headCommitId(root_);
    if ((rawHead.rfind("ref:", 0) == 0 && headBranch_.empty()) ||
        (rawHead.rfind("ref:", 0) != 0 && headId_.empty())) return fail();

    std::deque<std::string> queue;  // breadth-first walk over parent links
    for (const std::string& name : repo::listBranchNames(root_)) {
        std::string tip = repo::readBranch(root_, name);
        // A branch reference file must contain a valid object id. Silently
        // skipping a damaged ref would make the displayed DAG incomplete.
        if (tip.empty()) {
            return fail();
        }
        branchTips_[name] = tip;
        queue.push_back(tip);
    }
    if (includeHead && !headId_.empty()) queue.push_back(headId_);

    std::set<std::string> visited;
    while (!queue.empty()) {
        std::string id = queue.front();
        queue.pop_front();
        if (!visited.insert(id).second) continue;  // also stops corrupt parent cycles
        repo::CommitMeta meta;
        if (!repo::readCommitMeta(root_, id, meta)) {
            // Do not present a partial graph as a successful history. The user
            // needs to know that one reachable commit is missing or malformed.
            return fail();
        }
        GraphNode n;
        n.id = id;
        n.parents = meta.parents;
        n.message = meta.message;
        n.timestamp = meta.timestamp;
        n.files = meta.files;
        addCommit(n);
        for (const std::string& p : meta.parents) queue.push_back(p);
    }
    finalize();
    // A visited set prevents an infinite walk, but a cycle is not a valid DAG.
    // Kahn's algorithm must be able to emit every reachable node.
    if (topologicalOrder().size() != nodes_.size()) return fail();
    return true;
}

const GraphNode* CommitGraph::find(const std::string& id) const {
    auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : &it->second;
}

bool CommitGraph::isAncestor(const std::string& ancestor, const std::string& descendant) const {
    if (!contains(ancestor) || !contains(descendant)) return false;
    std::deque<std::string> queue{descendant};
    std::set<std::string> visited;
    while (!queue.empty()) {
        std::string id = queue.front();
        queue.pop_front();
        if (id == ancestor) return true;
        if (!visited.insert(id).second) continue;
        for (const std::string& p : nodes_.at(id).parents) queue.push_back(p);
    }
    return false;
}

// BFS from `start`; distance = number of parent steps. Each commit is visited once.
static std::map<std::string, int> distancesFrom(const std::map<std::string, GraphNode>& nodes,
                                                const std::string& start) {
    std::map<std::string, int> dist;
    std::deque<std::string> queue;
    dist[start] = 0;
    queue.push_back(start);
    while (!queue.empty()) {
        std::string id = queue.front();
        queue.pop_front();
        for (const std::string& p : nodes.at(id).parents) {
            if (dist.count(p)) continue;  // visited set
            dist[p] = dist[id] + 1;
            queue.push_back(p);
        }
    }
    return dist;
}

std::string CommitGraph::nearestCommonAncestor(const std::string& a, const std::string& b) const {
    if (!contains(a) || !contains(b)) return "";
    const std::map<std::string, int> da = distancesFrom(nodes_, a);
    const std::map<std::string, int> db = distancesFrom(nodes_, b);

    // In a DAG, a simple minimum sum of distances can select an older commit
    // when one tip reaches that old commit directly but reaches a newer common
    // ancestor through another parent. A merge base must instead be a maximal
    // common ancestor: no other common ancestor may descend from it.
    std::vector<const GraphNode*> common;
    for (const auto& kv : da) {
        if (db.count(kv.first)) common.push_back(&nodes_.at(kv.first));
    }

    const GraphNode* best = nullptr;
    for (const GraphNode* candidate : common) {
        bool olderThanAnotherCommon = false;
        for (const GraphNode* other : common) {
            if (candidate->id != other->id && isAncestor(candidate->id, other->id)) {
                olderThanAnotherCommon = true;
                break;
            }
        }
        if (!olderThanAnotherCommon && (!best || newerFirst(candidate, best))) best = candidate;
    }
    return best ? best->id : "";
}

std::vector<std::string> CommitGraph::topologicalOrder() const {
    // Kahn's algorithm from the newest end: a commit is ready once every child is output.
    std::map<std::string, std::size_t> waiting;
    std::vector<const GraphNode*> ready;
    for (const auto& kv : nodes_) {
        waiting[kv.first] = kv.second.children.size();
        if (kv.second.children.empty()) ready.push_back(&kv.second);
    }

    std::vector<std::string> order;
    while (!ready.empty()) {
        auto best = std::min_element(ready.begin(), ready.end(), newerFirst);  // newest ready commit
        const GraphNode* n = *best;
        ready.erase(best);
        order.push_back(n->id);
        for (const std::string& p : n->parents)
            if (--waiting[p] == 0) ready.push_back(&nodes_.at(p));
    }
    // If a cycle exists, this contains only the acyclic portion. load() checks
    // the size before accepting a persisted graph; callers inspecting an
    // in-memory graph can detect corruption by comparing size() with this result.
    return order;
}

}  // namespace minigit
