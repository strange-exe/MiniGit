#pragma once
// Trie (prefix tree) used for .minigitignore rules.
// Pure data structure: no file I/O, no knowledge of Git.
#include <cstddef>
#include <map>
#include <memory>
#include <string>

namespace minigit {

class Trie {
public:
    struct Match {
        std::string pattern;  // the rule as written, e.g. "build/" or "*.exe"
        int line = -1;        // 1-based line in .minigitignore
    };

    Trie();

    // Insert a key. prefixRule=true means "anything that starts with this key
    // matches" (directory rules, reversed-extension rules).
    // Returns false if the key is empty or already present.
    bool insert(const std::string& key, const std::string& pattern, int line, bool prefixRule);

    // Exact key lookup.
    bool search(const std::string& key) const;
    bool find(const std::string& key, Match& out) const;

    // Removes a key and prunes nodes that are no longer used.
    bool remove(const std::string& key);

    // Walks `text` once. Matches if a prefix rule is passed on the way,
    // or if an exact rule equals the whole text. O(len(text)).
    bool findMatch(const std::string& text, Match& out) const;

    void clear();
    std::size_t size() const { return count_; }
    bool empty() const { return count_ == 0; }

private:
    struct Node {
        std::map<char, std::unique_ptr<Node>> children;
        bool terminal = false;
        bool prefixRule = false;
        int line = -1;
        std::string pattern;
    };

    bool removeRec(Node* node, const std::string& key, std::size_t depth, bool& removed);

    std::unique_ptr<Node> root_;
    std::size_t count_ = 0;
};

}  // namespace minigit
