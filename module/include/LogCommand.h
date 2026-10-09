#pragma once
// minigit log / minigit log --reverse  (Snehil's command).
// Reads history through CommitLog (the Doubly Linked List) and returns
// plain data; the CLI does the printing, same convention as every other
// module in this project.
#include <string>
#include <vector>

namespace minigit {

struct LogEntry {
    std::string commitId;
    std::string parentId;   // "" for the first commit
    std::string message;
    std::string timestamp;  // unix time, as stored by commitRepo
    int fileCount = 0;
};

struct LogResult {
    enum Status { OK, EMPTY, NOT_A_REPO, ERROR } status = ERROR;
    std::vector<LogEntry> entries;  // see `reverse` below for ordering
    std::string errorMessage;
};

// reverse=false (default): newest commit first, like `git log`.
// reverse=true ("minigit log --reverse"): oldest commit first.
// Internally walks CommitLog's cursor with next()/prev(), so both directions
// exercise the doubly linked list rather than a plain array copy.
LogResult showLog(const std::string& root = ".", bool reverse = false);

// Formats a unix-timestamp string (as stored in commit metadata) into
// "YYYY-MM-DD HH:MM:SS" (local time). Returns the input unchanged if it
// isn't a plain number.
std::string formatTimestamp(const std::string& unixTimestamp);

}  // namespace minigit
