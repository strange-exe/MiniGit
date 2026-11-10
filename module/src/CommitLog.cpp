#include "CommitLog.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <vector>

#include "RepoUtils.h"

namespace fs = std::filesystem;

namespace minigit {

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

// Reads one commit through the shared, validating parser (RepoUtils.h). A missing,
// truncated or tampered metadata file makes it return false instead of an empty commit.
static bool readCommitNode(const std::string& root, const std::string& id, CommitNode& out) {
    CommitMetadata meta;
    if (!readCommitMeta(root, id, meta)) return false;
    out.id = id;
    out.parentId = meta.parentId;
    out.message = meta.message;
    out.timestamp = meta.timestamp;
    out.files = meta.files;
    out.deleted = meta.deleted;
    return true;
}

bool CommitLog::load() {
    clear();
    error_.clear();
    warning_.clear();
    std::error_code ec;
    if (!fs::is_directory(fs::path(root_) / ".minigit", ec)) {
        error_ = "not a minigit repository (run 'minigit init')";
        return false;
    }

    HeadState head = readHeadState(root_);
    if (!head.valid) {
        error_ = head.error;
        return false;
    }
    if (head.commitId.empty()) return true;  // repo exists, nothing committed yet

    std::vector<CommitNode> newestToOldest;
    std::unordered_set<std::string> visited;  // guards against a corrupt parent cycle
    std::string cur = head.commitId;
    while (!cur.empty()) {
        if (!visited.insert(cur).second) {
            warning_ = "history is incomplete: the parent chain loops back to commit '" + cur + "'";
            break;
        }
        CommitNode node;
        if (!readCommitNode(root_, cur, node)) {
            std::string what = "commit '" + cur + "' is missing or damaged";
            if (newestToOldest.empty()) {  // the branch tip itself: nothing at all can be shown
                error_ = "branch '" + head.branch + "' points to " + what;
                return false;
            }
            warning_ = "history is incomplete: " + what + "; older commits are not shown";
            break;
        }
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
