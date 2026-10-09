#pragma once
// Doubly linked list over commit history (Snehil's module).
// insert() / find() / next() / prev() are the DSA-facing operations; load()
// is the Git-facing operation that rebuilds the list from disk.
//
// List order: head() is the oldest commit, tail() is the newest (current HEAD).
// A cursor supports independent forward/backward traversal for `minigit log`.
#include <cstddef>
#include <string>

#include "CommitNode.h"

namespace minigit {

class CommitLog {
public:
    explicit CommitLog(const std::string& rootDir = ".");
    ~CommitLog();

    CommitLog(const CommitLog&) = delete;
    CommitLog& operator=(const CommitLog&) = delete;

    // Reads every commit reachable from HEAD by following "parent" links
    // (Krish's metadata format) and rebuilds the list, oldest -> newest.
    // Returns false only on a hard failure (not a repo); an empty repo with
    // no commits yet returns true with size() == 0.
    bool load();

    // Appends a new commit as the newest node. Used directly by unit tests
    // that exercise the data structure without touching disk; showLog()
    // normally relies on load() instead.
    bool insert(const CommitNode& node);

    // Exact-id lookup, walking from the oldest commit. O(n).
    CommitNode* find(const std::string& id) const;

    CommitNode* head() const { return head_; }
    CommitNode* tail() const { return tail_; }

    // Traversal cursor. resetToTail()/resetToHead() position it before use.
    CommitNode* current() const { return cursor_; }
    CommitNode* next();  // move toward newer commits; nullptr (unmoved) at the tail
    CommitNode* prev();  // move toward older commits; nullptr (unmoved) at the head
    void resetToTail();
    void resetToHead();

    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

private:
    void clear();

    std::string root_;
    CommitNode* head_ = nullptr;
    CommitNode* tail_ = nullptr;
    CommitNode* cursor_ = nullptr;
    std::size_t size_ = 0;
};

}  // namespace minigit
