#pragma once
// Loads .minigitignore into two Tries and answers ignore questions.
//   pathTrie_ : forward Trie  -> file rules ("secret.txt") and dir rules ("build/")
//   extTrie_  : reversed Trie -> extension rules ("*.exe" stored as "exe.")
#include <string>
#include <vector>

#include "Trie.h"

namespace minigit {

struct IgnoreResult {
    enum Status { ADDED, ALREADY_PRESENT, REMOVED, NOT_PRESENT, IGNORED, NOT_IGNORED, ERROR };
    Status status = ERROR;
    int lineNumber = -1;         // 1-based line in .minigitignore, -1 if n/a
    std::string matchedPattern;  // rule that matched / was added / was removed
    std::string message;         // extra detail (hints, error reason)
};

class IgnoreManager {
public:
    explicit IgnoreManager(const std::string& rootDir = ".");

    bool load();  // (re)read .minigitignore; called by the constructor

    IgnoreResult addPattern(const std::string& pattern);     // ignore <path>
    IgnoreResult removePattern(const std::string& pattern);  // ignore -r <path>
    IgnoreResult verify(const std::string& path) const;      // ignore -v <path>
    bool isIgnored(const std::string& path) const;           // used by add

    const std::vector<std::string>& lines() const { return lines_; }

private:
    struct ParsedRule {
        enum Kind { NONE, FILE_RULE, DIR_RULE, EXT_RULE } kind = NONE;
        std::string pattern;  // text written to .minigitignore
        std::string key;      // key stored in the Trie
    };

    ParsedRule parseRule(const std::string& raw) const;
    bool lookup(const ParsedRule& rule, Trie::Match& m) const;
    void rebuildTries();
    bool saveFile() const;
    std::string ignoreFilePath() const;

    std::string root_;
    std::vector<std::string> lines_;  // raw file lines (blank/comments included)
    Trie pathTrie_;
    Trie extTrie_;
};

// Free functions matching the team interface (called by Abhinesh's CLI).
IgnoreResult ignoreAdd(const std::string& path, const std::string& root = ".");
IgnoreResult ignoreRemove(const std::string& path, const std::string& root = ".");
IgnoreResult ignoreVerify(const std::string& path, const std::string& root = ".");

}  // namespace minigit
