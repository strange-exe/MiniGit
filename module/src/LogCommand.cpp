#include "LogCommand.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <sstream>

#include "CommitGraph.h"
#include "CommitLog.h"
#include "RepoUtils.h"

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
    e.deletedCount = static_cast<int>(n->deleted.size());
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
        res.errorMessage = log.error().empty() ? "could not read commit history" : log.error();
        return res;
    }
    res.warning = log.warning();
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

LogResult showLogGraph(const std::string& root, bool all) {
    (void)all;
    LogResult res;
    std::error_code ec;
    if (!fs::is_directory(fs::path(root) / ".minigit", ec)) {
        res.status = LogResult::NOT_A_REPO;
        res.errorMessage = "not a minigit repository (or any of the parent directories)";
        return res;
    }

    HeadState head = readHeadState(root);
    if (!head.valid) {
        res.status = LogResult::ERROR;
        res.errorMessage = head.error;
        return res;
    }

    CommitGraph graph(root);
    if (!graph.build()) {
        res.status = LogResult::ERROR;
        res.errorMessage = "could not read commit history";
        return res;
    }
    if (graph.empty()) {
        res.status = LogResult::EMPTY;
        res.errorMessage = "no commits yet";
        return res;
    }

    res.status = LogResult::OK;
    res.graphOutput = graph.renderGraph();
    return res;
}

}  // namespace minigit
