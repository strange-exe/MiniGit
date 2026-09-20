#include "Trie.h"

namespace minigit {

Trie::Trie() : root_(new Node()) {}

bool Trie::insert(const std::string& key, const std::string& pattern, int line, bool prefixRule) {
    if (key.empty()) return false;
    Node* cur = root_.get();
    for (char c : key) {
        auto& child = cur->children[c];
        if (!child) child.reset(new Node());
        cur = child.get();
    }
    if (cur->terminal) return false;
    cur->terminal = true;
    cur->prefixRule = prefixRule;
    cur->line = line;
    cur->pattern = pattern;
    ++count_;
    return true;
}

bool Trie::find(const std::string& key, Match& out) const {
    if (key.empty()) return false;
    const Node* cur = root_.get();
    for (char c : key) {
        auto it = cur->children.find(c);
        if (it == cur->children.end()) return false;
        cur = it->second.get();
    }
    if (!cur->terminal) return false;
    out.pattern = cur->pattern;
    out.line = cur->line;
    return true;
}

bool Trie::search(const std::string& key) const {
    Match m;
    return find(key, m);
}

bool Trie::remove(const std::string& key) {
    if (key.empty()) return false;
    bool removed = false;
    removeRec(root_.get(), key, 0, removed);
    if (removed) --count_;
    return removed;
}

// Returns true if `node` is now useless and its parent may delete it.
bool Trie::removeRec(Node* node, const std::string& key, std::size_t depth, bool& removed) {
    if (depth == key.size()) {
        if (!node->terminal) return false;
        node->terminal = false;
        node->prefixRule = false;
        node->line = -1;
        node->pattern.clear();
        removed = true;
        return node->children.empty();
    }
    auto it = node->children.find(key[depth]);
    if (it == node->children.end()) return false;
    bool prune = removeRec(it->second.get(), key, depth + 1, removed);
    if (prune) node->children.erase(it);
    return removed && !node->terminal && node->children.empty();
}

bool Trie::findMatch(const std::string& text, Match& out) const {
    const Node* cur = root_.get();
    for (std::size_t i = 0;; ++i) {
        if (cur->terminal && (cur->prefixRule || i == text.size())) {
            out.pattern = cur->pattern;
            out.line = cur->line;
            return true;
        }
        if (i == text.size()) return false;
        auto it = cur->children.find(text[i]);
        if (it == cur->children.end()) return false;
        cur = it->second.get();
    }
}

void Trie::clear() {
    root_.reset(new Node());
    count_ = 0;
}

}  // namespace minigit
