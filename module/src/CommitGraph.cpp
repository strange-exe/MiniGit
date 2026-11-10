#include "CommitGraph.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <queue>
#include <sstream>

#include "BranchCommand.h"
#include "LogCommand.h"
#include "RepoUtils.h"

namespace fs = std::filesystem;

namespace minigit {

CommitGraph::CommitGraph(const std::string& root) : root_(root) {}

bool CommitGraph::build() {
    nodes_.clear();
    branches_.clear();
    activeBranch_.clear();
    activeCommitId_.clear();

    std::error_code ec;
    fs::path gitDir = fs::path(root_) / ".minigit";
    if (!fs::is_directory(gitDir, ec)) return false;

    // 1. Read all commit objects from .minigit/objects/
    fs::path objDir = gitDir / "objects";
    if (fs::is_directory(objDir, ec)) {
        for (const auto& entry : fs::directory_iterator(objDir, ec)) {
            if (entry.is_directory(ec)) {
                std::string cid = entry.path().filename().string();
                CommitMetadata meta;
                if (readCommitMeta(root_, cid, meta)) {
                    CommitGraphNode node;
                    node.id = cid;
                    node.parents = meta.parents;
                    node.timestamp = meta.timestamp;
                    node.author = meta.author;
                    node.message = meta.message;
                    node.files = meta.files;
                    node.deleted = meta.deleted;
                    nodes_[cid] = node;
                }
            }
        }
    }

    // 2. Read active branch and HEAD commit ID
    activeBranch_ = getCurrentBranch(root_);
    activeCommitId_ = getHEADCommitId(root_);

    // 3. Read branch pointers from .minigit/refs/heads/
    fs::path headsDir = gitDir / "refs" / "heads";
    if (fs::is_directory(headsDir, ec)) {
        for (const auto& entry : fs::directory_iterator(headsDir, ec)) {
            if (entry.is_regular_file(ec)) {
                std::string bName = entry.path().filename().string();
                std::ifstream in(entry.path().string());
                std::string cid;
                std::getline(in, cid);
                cid = trim(cid);
                if (!cid.empty()) {
                    branches_[bName] = cid;
                    if (nodes_.count(cid)) {
                        nodes_[cid].branchRefs.push_back(bName);
                    }
                }
            }
        }
    }

    // Mark active HEAD node
    if (!activeCommitId_.empty() && nodes_.count(activeCommitId_)) {
        nodes_[activeCommitId_].isHead = true;
    }

    // 4. Build children (directed edges from parent to children)
    for (auto& kv : nodes_) {
        const std::string& childId = kv.first;
        for (const auto& parentId : kv.second.parents) {
            if (nodes_.count(parentId)) {
                nodes_[parentId].children.push_back(childId);
            }
        }
    }

    return true;
}

bool CommitGraph::hasCommit(const std::string& id) const {
    return nodes_.find(id) != nodes_.end();
}

const CommitGraphNode* CommitGraph::getNode(const std::string& id) const {
    auto it = nodes_.find(id);
    return it != nodes_.end() ? &it->second : nullptr;
}

std::vector<std::string> CommitGraph::getAllCommitIds() const {
    std::vector<std::string> ids;
    ids.reserve(nodes_.size());
    for (const auto& kv : nodes_) {
        ids.push_back(kv.first);
    }
    return ids;
}

std::vector<std::pair<std::string, std::string>> CommitGraph::getAllBranches() const {
    std::vector<std::pair<std::string, std::string>> res;
    res.reserve(branches_.size());
    for (const auto& kv : branches_) {
        res.push_back(kv);
    }
    std::sort(res.begin(), res.end());
    return res;
}

bool CommitGraph::hasCycle() const {
    // 0: unvisited, 1: visiting, 2: visited
    std::unordered_map<std::string, int> state;
    for (const auto& kv : nodes_) state[kv.first] = 0;

    std::function<bool(const std::string&)> dfs = [&](const std::string& u) {
        state[u] = 1;
        auto it = nodes_.find(u);
        if (it != nodes_.end()) {
            for (const auto& p : it->second.parents) {
                if (nodes_.count(p)) {
                    if (state[p] == 1) return true; // Cycle detected
                    if (state[p] == 0 && dfs(p)) return true;
                }
            }
        }
        state[u] = 2;
        return false;
    };

    for (const auto& kv : nodes_) {
        if (state[kv.first] == 0) {
            if (dfs(kv.first)) return true;
        }
    }
    return false;
}

std::vector<std::string> CommitGraph::getTopologicalOrder(bool oldestFirst) const {
    if (nodes_.empty()) return {};

    // In newest-first order, every child commit appears before its parents.
    // We compute the in-degree as the number of children in this subgraph.
    std::unordered_map<std::string, int> inDegree;
    for (const auto& kv : nodes_) {
        inDegree[kv.first] = static_cast<int>(kv.second.children.size());
    }

    // Comparator to sort by timestamp descending (newest first)
    auto cmp = [this](const std::string& a, const std::string& b) {
        long long ta = 0, tb = 0;
        auto ita = nodes_.find(a);
        auto itb = nodes_.find(b);
        if (ita != nodes_.end() && !ita->second.timestamp.empty()) {
            ta = std::atoll(ita->second.timestamp.c_str());
        }
        if (itb != nodes_.end() && !itb->second.timestamp.empty()) {
            tb = std::atoll(itb->second.timestamp.c_str());
        }
        if (ta != tb) return ta < tb; // Max-heap: highest timestamp on top
        return a < b;
    };

    std::priority_queue<std::string, std::vector<std::string>, decltype(cmp)> pq(cmp);

    for (const auto& kv : nodes_) {
        if (inDegree[kv.first] == 0) {
            pq.push(kv.first);
        }
    }

    std::vector<std::string> order;
    order.reserve(nodes_.size());

    while (!pq.empty()) {
        std::string u = pq.top();
        pq.pop();
        order.push_back(u);

        auto it = nodes_.find(u);
        if (it != nodes_.end()) {
            for (const auto& p : it->second.parents) {
                if (nodes_.count(p)) {
                    if (--inDegree[p] == 0) {
                        pq.push(p);
                    }
                }
            }
        }
    }

    // Safety fallback: append any unvisited nodes (in case of malformed edges)
    if (order.size() < nodes_.size()) {
        std::unordered_set<std::string> inOrder(order.begin(), order.end());
        for (const auto& kv : nodes_) {
            if (inOrder.find(kv.first) == inOrder.end()) {
                order.push_back(kv.first);
            }
        }
    }

    if (oldestFirst) {
        std::reverse(order.begin(), order.end());
    }

    return order;
}

std::vector<std::string> CommitGraph::getReachableFrom(const std::string& commitId) const {
    if (!hasCommit(commitId)) return {};

    std::vector<std::string> result;
    std::unordered_set<std::string> visited;
    std::queue<std::string> q;

    q.push(commitId);
    visited.insert(commitId);

    while (!q.empty()) {
        std::string cur = q.front();
        q.pop();
        result.push_back(cur);

        auto it = nodes_.find(cur);
        if (it != nodes_.end()) {
            for (const auto& p : it->second.parents) {
                if (nodes_.count(p) && visited.find(p) == visited.end()) {
                    visited.insert(p);
                    q.push(p);
                }
            }
        }
    }
    return result;
}

std::vector<std::string> CommitGraph::getAllReachable() const {
    std::unordered_set<std::string> visited;
    std::queue<std::string> q;

    // Start traversal from all branch tips and HEAD
    for (const auto& kv : branches_) {
        if (nodes_.count(kv.second) && visited.find(kv.second) == visited.end()) {
            visited.insert(kv.second);
            q.push(kv.second);
        }
    }
    if (!activeCommitId_.empty() && nodes_.count(activeCommitId_) && visited.find(activeCommitId_) == visited.end()) {
        visited.insert(activeCommitId_);
        q.push(activeCommitId_);
    }

    while (!q.empty()) {
        std::string cur = q.front();
        q.pop();

        auto it = nodes_.find(cur);
        if (it != nodes_.end()) {
            for (const auto& p : it->second.parents) {
                if (nodes_.count(p) && visited.find(p) == visited.end()) {
                    visited.insert(p);
                    q.push(p);
                }
            }
        }
    }

    // Filter topological order to only include reachable nodes
    std::vector<std::string> allTopo = getTopologicalOrder(false);
    std::vector<std::string> res;
    for (const auto& id : allTopo) {
        if (visited.find(id) != visited.end()) {
            res.push_back(id);
        }
    }
    return res;
}

std::string CommitGraph::resolvePrefix(const std::string& prefix) const {
    if (prefix.empty()) return "";
    if (prefix.size() == 40 && nodes_.count(prefix)) return prefix;

    std::vector<std::string> matches;
    for (const auto& kv : nodes_) {
        if (kv.first.rfind(prefix, 0) == 0) {
            matches.push_back(kv.first);
        }
    }
    return (matches.size() == 1) ? matches[0] : "";
}

std::string CommitGraph::renderGraph() const {
    if (nodes_.empty()) {
        return "No commits yet\n";
    }

    std::vector<std::string> commits = getAllReachable();
    if (commits.empty()) {
        commits = getTopologicalOrder(false);
    }
    if (commits.empty()) {
        return "No commits yet\n";
    }

    std::ostringstream out;
    std::vector<std::string> activeColumns;

    for (const auto& cid : commits) {
        const CommitGraphNode* node = getNode(cid);
        if (!node) continue;

        // 1. Find or allocate column for this commit
        int colIdx = -1;
        for (int i = 0; i < static_cast<int>(activeColumns.size()); ++i) {
            if (activeColumns[i] == cid) {
                colIdx = i;
                break;
            }
        }
        if (colIdx == -1) {
            for (int i = 0; i < static_cast<int>(activeColumns.size()); ++i) {
                if (activeColumns[i].empty()) {
                    colIdx = i;
                    activeColumns[i] = cid;
                    break;
                }
            }
        }
        if (colIdx == -1) {
            colIdx = static_cast<int>(activeColumns.size());
            activeColumns.push_back(cid);
        }

        // 2. Build branch reference decoration
        std::string dec;
        std::vector<std::string> refs;
        if (node->isHead) {
            refs.push_back("HEAD -> " + activeBranch_);
        }
        for (const auto& b : node->branchRefs) {
            if (node->isHead && b == activeBranch_) continue;
            refs.push_back(b);
        }
        if (!refs.empty()) {
            dec = " (";
            for (std::size_t i = 0; i < refs.size(); ++i) {
                if (i > 0) dec += ", ";
                dec += refs[i];
            }
            dec += ")";
        }

        // 3. Print the commit node row with graph columns
        for (int i = 0; i < static_cast<int>(activeColumns.size()); ++i) {
            if (i == colIdx) {
                out << "* ";
            } else if (!activeColumns[i].empty()) {
                out << "| ";
            } else {
                out << "  ";
            }
        }
        std::string shortId = cid.size() >= 7 ? cid.substr(0, 7) : cid;
        out << "commit " << shortId << dec << "\n";

        // 4. Print metadata (author, date, message) with vertical column bars
        std::string barPrefix;
        for (int i = 0; i < static_cast<int>(activeColumns.size()); ++i) {
            if (!activeColumns[i].empty()) {
                barPrefix += "| ";
            } else {
                barPrefix += "  ";
            }
        }

        if (!node->author.empty()) {
            out << barPrefix << "Author: " << node->author << "\n";
        }
        if (!node->timestamp.empty()) {
            out << barPrefix << "Date:   " << formatTimestamp(node->timestamp) << "\n";
        }
        out << barPrefix << "\n";
        out << barPrefix << "    " << node->message << "\n";
        out << barPrefix << "    (" << node->files.size() << " file(s)";
        if (!node->deleted.empty()) out << ", " << node->deleted.size() << " deleted";
        out << ")\n";
        out << barPrefix << "\n";

        // 5. Connect node to its parents in the column tracking
        if (node->parents.empty()) {
            activeColumns[colIdx] = "";
        } else {
            std::string primaryParent = node->parents[0];
            int existingCol = -1;
            for (int i = 0; i < static_cast<int>(activeColumns.size()); ++i) {
                if (i != colIdx && activeColumns[i] == primaryParent) {
                    existingCol = i;
                    break;
                }
            }

            if (existingCol != -1) {
                // Convergence: branch merges back into shared ancestor
                activeColumns[colIdx] = "";
                // Render convergence line
                int minCol = std::min(colIdx, existingCol);
                int maxCol = std::max(colIdx, existingCol);
                for (int i = 0; i < static_cast<int>(activeColumns.size()); ++i) {
                    if (i == minCol) {
                        out << "|/";
                    } else if (i > minCol && i < maxCol) {
                        out << "/ ";
                    } else if (!activeColumns[i].empty()) {
                        out << "| ";
                    } else {
                        out << "  ";
                    }
                }
                out << "\n";
            } else {
                activeColumns[colIdx] = primaryParent;
            }
        }

        // Trim trailing empty columns
        while (!activeColumns.empty() && activeColumns.back().empty()) {
            activeColumns.pop_back();
        }
    }

    return out.str();
}

}  // namespace minigit

