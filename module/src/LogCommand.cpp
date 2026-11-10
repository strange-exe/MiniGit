#include "LogCommand.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <map>
#include <sstream>

#include "CommitGraph.h"
#include "CommitLog.h"
#include "RepoStore.h"

namespace fs = std::filesystem;

namespace minigit {

std::string formatTimestamp(const std::string& unixTimestamp) {
    if (unixTimestamp.empty() ||
        !std::all_of(unixTimestamp.begin(), unixTimestamp.end(), [](unsigned char c) { return std::isdigit(c); })) {
        return unixTimestamp;
    }
    std::time_t t = static_cast<std::time_t>(std::atoll(unixTimestamp.c_str()));
    std::tm tmBuf{};
#if defined(_WIN32)
    // localtime_s is hidden by MinGW under -std=c++14; the Windows CRT keeps
    // localtime()'s buffer per thread, so copying it out is safe.
    std::tm* local = std::localtime(&t);
    if (!local) return unixTimestamp;
    tmBuf = *local;
#else
    localtime_r(&t, &tmBuf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmBuf);
    return std::string(buf);
}

static LogEntry toEntry(const CommitNode* n) {
    LogEntry e;
    e.commitId = n->id;
    e.parentId = n->parentId;
    e.message = n->message;
    e.timestamp = n->timestamp;
    e.fileCount = static_cast<int>(n->files.size());
    return e;
}

LogResult showLog(const std::string& root, bool reverse) {
    LogResult res;
    std::error_code ec;
    if (!fs::is_directory(fs::path(root) / ".minigit", ec)) {
        res.status = LogResult::NOT_A_REPO;
        res.errorMessage = "not a minigit repository (or any of the parent directories)";
        return res;
    }

    CommitLog log(root);
    if (!log.load()) {
        res.status = LogResult::ERROR;
        res.errorMessage = "could not read commit history";
        return res;
    }
    if (log.empty()) {
        res.status = LogResult::EMPTY;
        res.errorMessage = "no commits yet";
        return res;
    }

    if (reverse) {
        // oldest -> newest: walk the cursor forward with next()
        log.resetToHead();
        while (true) {
            res.entries.push_back(toEntry(log.current()));
            if (!log.next()) break;
        }
    } else {
        // newest -> oldest: walk the cursor backward with prev()
        log.resetToTail();
        while (true) {
            res.entries.push_back(toEntry(log.current()));
            if (!log.prev()) break;
        }
    }

    res.status = LogResult::OK;
    return res;
}

// ---------------------------------------------------------------------------
// log --graph. One "lane" per line of history still waiting for a commit; each lane
// holds the id of the commit it expects next. Commits are taken in topological order
// (children before parents), so a lane always meets its commit.
// ---------------------------------------------------------------------------
static std::string rtrim(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

// "* " at the commit's lane, "| " at every other lane.
static std::string commitCells(std::size_t lanes, std::size_t col) {
    std::string out;
    for (std::size_t i = 0; i < lanes; ++i) out += (i == col ? "* " : "| ");
    return rtrim(out);
}

// Row drawn after lane `removed` joined the lane on its left, e.g. "|/" or "| |/".
static std::string convergeCells(std::size_t lanesAfter, std::size_t removed) {
    std::string out;
    for (std::size_t i = 0; i < lanesAfter; ++i) out += (removed >= 1 && i == removed - 1) ? "|/" : "| ";
    return rtrim(out);
}

// Row drawn after a merge commit opened new lanes to its right: "|\".
static std::string branchOffCells(std::size_t lanesAfter, std::size_t col, std::size_t opened) {
    std::string out;
    for (std::size_t i = 0; i < lanesAfter; ++i) {
        if (i == col) out += "|\\";
        else if (i > col && i <= col + opened) out += "  ";
        else out += "| ";
    }
    return rtrim(out);
}

GraphResult showGraph(const std::string& root) {
    GraphResult res;
    std::error_code ec;
    if (!fs::is_directory(fs::path(root) / ".minigit", ec)) {
        res.status = GraphResult::NOT_A_REPO;
        res.errorMessage = "not a minigit repository (or any of the parent directories)";
        return res;
    }

    CommitGraph graph(root);
    if (!graph.load()) {
        res.status = GraphResult::ERROR;
        res.errorMessage = "could not read commit history";
        return res;
    }
    if (graph.size() == 0) {
        res.status = GraphResult::EMPTY;
        res.errorMessage = "no commits yet";
        return res;
    }

    // Labels for branch tips: "HEAD -> main, feature"; "HEAD" alone when detached.
    std::map<std::string, std::vector<std::string>> labels;
    for (const auto& kv : graph.branchTips()) {
        if (kv.first == graph.headBranch()) labels[kv.second].insert(labels[kv.second].begin(), "HEAD -> " + kv.first);
        else labels[kv.second].push_back(kv.first);
    }
    if (graph.headBranch().empty() && !graph.headId().empty())
        labels[graph.headId()].insert(labels[graph.headId()].begin(), "HEAD");

    std::vector<std::string> lanes;
    auto addRow = [&](const std::string& g, const GraphNode* n) {
        GraphRow row;
        row.graph = g;
        if (n) {
            row.commitId = n->id;
            row.message = n->message;
            auto it = labels.find(n->id);
            if (it != labels.end())
                for (std::size_t i = 0; i < it->second.size(); ++i) row.decoration += (i ? ", " : "") + it->second[i];
        }
        res.rows.push_back(row);
    };

    for (const std::string& id : graph.topologicalOrder()) {
        const GraphNode* n = graph.find(id);

        std::vector<std::size_t> hits;  // lanes waiting for this commit
        for (std::size_t i = 0; i < lanes.size(); ++i)
            if (lanes[i] == id) hits.push_back(i);
        if (hits.empty()) {  // a branch tip: start a new lane on the right
            lanes.push_back(id);
            hits.push_back(lanes.size() - 1);
        }
        std::size_t col = hits[0];

        // Lanes that were heading to the same commit join before it is drawn.
        for (std::size_t k = hits.size(); k-- > 1;) {
            lanes.erase(lanes.begin() + static_cast<std::ptrdiff_t>(hits[k]));
            addRow(convergeCells(lanes.size(), hits[k]), nullptr);
        }

        addRow(commitCells(lanes.size(), col), n);

        if (n->parents.empty()) {
            lanes.erase(lanes.begin() + static_cast<std::ptrdiff_t>(col));  // root commit: lane ends
        } else {
            lanes[col] = n->parents[0];
            std::size_t extra = n->parents.size() - 1;  // merge commit: one new lane per extra parent
            for (std::size_t k = 0; k < extra; ++k)
                lanes.insert(lanes.begin() + static_cast<std::ptrdiff_t>(col + 1 + k), n->parents[k + 1]);
            if (extra > 0) addRow(branchOffCells(lanes.size(), col, extra), nullptr);
        }
    }

    res.status = GraphResult::OK;
    return res;
}

}  // namespace minigit
