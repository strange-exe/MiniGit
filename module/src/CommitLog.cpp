#include "CommitLog.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace minigit {

static std::string trim(const std::string& s) {
    std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

CommitLog::CommitLog(const std::string& rootDir) : root_(rootDir) {}

CommitLog::~CommitLog() { clear(); }

void CommitLog::clear() {
    CommitNode* n = head_;
    while (n) {
        CommitNode* next = n->next;
        delete n;
        n = next;
    }
    head_ = tail_ = cursor_ = nullptr;
    size_ = 0;
}

// Mirrors CommitCommand's getHEADCommitId(): HEAD holds either "ref: <path>"
// (read the commit id from that ref file) or a raw commit id directly.
static std::string readHeadCommitId(const std::string& root) {
    fs::path headFile = fs::path(root) / ".minigit" / "HEAD";
    std::ifstream in(headFile.string());
    if (!in) return "";
    std::string line;
    std::getline(in, line);
    line = trim(line);
    if (line.rfind("ref:", 0) == 0) {
        fs::path refPath = fs::path(root) / ".minigit" / trim(line.substr(4));
        std::ifstream refIn(refPath.string());
        if (!refIn) return "";
        std::string id;
        std::getline(refIn, id);
        return trim(id);
    }
    return line;
}

// Parses .minigit/objects/<id>/metadata. Returns false if the file is missing
// or malformed (a corrupt or hand-edited repo should not crash `log`).
static bool readCommitMetadata(const std::string& root, const std::string& id, CommitNode& out) {
    fs::path metaPath = fs::path(root) / ".minigit" / "objects" / id / "metadata";
    std::ifstream in(metaPath.string());
    if (!in) return false;

    out.id = id;
    out.parentId.clear();
    out.message.clear();
    out.timestamp.clear();
    out.files.clear();

    std::string line;
    int filesToRead = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("parent ", 0) == 0) {
            std::string p = line.substr(7);
            out.parentId = (p == "none") ? "" : p;
        } else if (line.rfind("timestamp ", 0) == 0) {
            out.timestamp = line.substr(10);
        } else if (line.rfind("message ", 0) == 0) {
            out.message = line.substr(8);
        } else if (line.rfind("files ", 0) == 0) {
            filesToRead = std::atoi(line.substr(6).c_str());
        } else if (line.rfind("commit ", 0) == 0) {
            // already known from the folder name; nothing to do
        } else if (filesToRead > 0 && static_cast<int>(out.files.size()) < filesToRead) {
            out.files.push_back(line);
        }
    }
    return true;
}

bool CommitLog::load() {
    clear();
    std::error_code ec;
    if (!fs::is_directory(fs::path(root_) / ".minigit", ec)) return false;

    std::string headId = readHeadCommitId(root_);
    if (headId.empty()) return true;  // repo exists, nothing committed yet

    std::vector<CommitNode> newestToOldest;
    std::unordered_set<std::string> visited;  // guards against a corrupt parent cycle
    std::string cur = headId;
    while (!cur.empty() && visited.find(cur) == visited.end()) {
        visited.insert(cur);
        CommitNode node;
        if (!readCommitMetadata(root_, cur, node)) break;  // stop at first unreadable commit
        std::string parent = node.parentId;
        newestToOldest.push_back(std::move(node));
        cur = parent;
    }

    for (auto it = newestToOldest.rbegin(); it != newestToOldest.rend(); ++it) {
        insert(*it);
    }
    resetToTail();
    return true;
}

bool CommitLog::insert(const CommitNode& node) {
    CommitNode* n = new CommitNode(node);
    n->prev = tail_;
    n->next = nullptr;
    if (tail_) tail_->next = n;
    tail_ = n;
    if (!head_) head_ = n;
    cursor_ = n;
    ++size_;
    return true;
}

CommitNode* CommitLog::find(const std::string& id) const {
    for (CommitNode* n = head_; n; n = n->next) {
        if (n->id == id) return n;
    }
    return nullptr;
}

CommitNode* CommitLog::next() {
    if (!cursor_ || !cursor_->next) return nullptr;
    cursor_ = cursor_->next;
    return cursor_;
}

CommitNode* CommitLog::prev() {
    if (!cursor_ || !cursor_->prev) return nullptr;
    cursor_ = cursor_->prev;
    return cursor_;
}

void CommitLog::resetToTail() { cursor_ = tail_; }
void CommitLog::resetToHead() { cursor_ = head_; }

}  // namespace minigit
