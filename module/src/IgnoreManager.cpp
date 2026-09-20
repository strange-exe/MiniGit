#include "IgnoreManager.h"

#include <filesystem>
#include <fstream>

#include "PathUtils.h"

namespace fs = std::filesystem;

namespace minigit {

static std::string trim(const std::string& s) {
    std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

IgnoreManager::IgnoreManager(const std::string& rootDir) : root_(rootDir) { load(); }

std::string IgnoreManager::ignoreFilePath() const {
    return (fs::path(root_) / ".minigitignore").string();
}

bool IgnoreManager::load() {
    lines_.clear();
    std::ifstream in(ignoreFilePath());
    if (in) {
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();  // CRLF files
            lines_.push_back(line);
        }
    }
    rebuildTries();
    return true;
}

bool IgnoreManager::saveFile() const {
    std::ofstream out(ignoreFilePath(), std::ios::trunc);
    if (!out) return false;
    for (const auto& l : lines_) out << l << '\n';
    return static_cast<bool>(out);
}

IgnoreManager::ParsedRule IgnoreManager::parseRule(const std::string& raw) const {
    ParsedRule r;
    std::string t = trim(raw);
    if (t.empty() || t[0] == '#') return r;

    // Extension rule: "*.ext" (no other wildcard / slash)
    if (t.size() > 2 && t[0] == '*' && t[1] == '.' && t.find_first_of("*/\\", 2) == std::string::npos) {
        r.kind = ParsedRule::EXT_RULE;
        r.pattern = t;
        r.key = reverseString(t.substr(1));  // ".exe" -> "exe."
        return r;
    }

    std::string n = normalizePath(t, root_);
    if (n.empty() || n == "." || isOutsideRepo(n)) return r;
    if (n.back() != '/') {
        std::error_code ec;
        if (fs::is_directory(fs::path(root_) / n, ec)) n += '/';  // existing dir => dir rule
    }
    r.kind = (n.back() == '/') ? ParsedRule::DIR_RULE : ParsedRule::FILE_RULE;
    r.pattern = n;
    r.key = n;
    return r;
}

void IgnoreManager::rebuildTries() {
    pathTrie_.clear();
    extTrie_.clear();
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        ParsedRule r = parseRule(lines_[i]);
        int lineNo = static_cast<int>(i) + 1;
        switch (r.kind) {
            case ParsedRule::EXT_RULE: extTrie_.insert(r.key, r.pattern, lineNo, true); break;
            case ParsedRule::DIR_RULE: pathTrie_.insert(r.key, r.pattern, lineNo, true); break;
            case ParsedRule::FILE_RULE: pathTrie_.insert(r.key, r.pattern, lineNo, false); break;
            default: break;
        }
    }
}

bool IgnoreManager::lookup(const ParsedRule& r, Trie::Match& m) const {
    if (r.kind == ParsedRule::EXT_RULE) return extTrie_.find(r.key, m);
    if (r.kind == ParsedRule::NONE) return false;
    return pathTrie_.find(r.key, m);
}

IgnoreResult IgnoreManager::addPattern(const std::string& raw) {
    IgnoreResult res;
    ParsedRule rule = parseRule(raw);
    if (rule.kind == ParsedRule::NONE) {
        res.message = "invalid pattern '" + raw + "'";
        return res;
    }
    Trie::Match m;
    if (lookup(rule, m)) {
        res.status = IgnoreResult::ALREADY_PRESENT;
        res.lineNumber = m.line;
        res.matchedPattern = m.pattern;
        return res;
    }
    lines_.push_back(rule.pattern);
    if (!saveFile()) {
        lines_.pop_back();
        res.message = "could not write .minigitignore";
        return res;
    }
    int lineNo = static_cast<int>(lines_.size());
    if (rule.kind == ParsedRule::EXT_RULE) extTrie_.insert(rule.key, rule.pattern, lineNo, true);
    else pathTrie_.insert(rule.key, rule.pattern, lineNo, rule.kind == ParsedRule::DIR_RULE);
    res.status = IgnoreResult::ADDED;
    res.lineNumber = lineNo;
    res.matchedPattern = rule.pattern;
    return res;
}

IgnoreResult IgnoreManager::removePattern(const std::string& raw) {
    IgnoreResult res;
    ParsedRule rule = parseRule(raw);
    if (rule.kind == ParsedRule::NONE) {
        res.message = "invalid pattern '" + raw + "'";
        return res;
    }
    Trie::Match m;
    bool found = lookup(rule, m);
    if (!found && rule.kind == ParsedRule::FILE_RULE) {  // dir that no longer exists on disk
        ParsedRule alt = rule;
        alt.kind = ParsedRule::DIR_RULE;
        alt.pattern += '/';
        alt.key += '/';
        found = lookup(alt, m);
    }
    if (!found) {
        res.status = IgnoreResult::NOT_PRESENT;
        IgnoreResult v = verify(raw);
        if (v.status == IgnoreResult::IGNORED) {  // hint: ignored through another rule
            res.matchedPattern = v.matchedPattern;
            res.lineNumber = v.lineNumber;
            res.message = "ignored by '" + v.matchedPattern + "' (line " + std::to_string(v.lineNumber) +
                          "), remove that rule instead";
        }
        return res;
    }
    lines_.erase(lines_.begin() + (m.line - 1));  // later lines shift up
    if (!saveFile()) {
        res.message = "could not write .minigitignore";
        load();
        return res;
    }
    rebuildTries();  // keeps every stored line number correct
    res.status = IgnoreResult::REMOVED;
    res.lineNumber = m.line;
    res.matchedPattern = m.pattern;
    return res;
}

IgnoreResult IgnoreManager::verify(const std::string& path) const {
    IgnoreResult res;
    std::string n = normalizePath(path, root_);
    if (n.empty() || isOutsideRepo(n)) {
        res.message = "invalid path '" + path + "'";
        return res;
    }
    if (n.back() != '/') {
        std::error_code ec;
        if (fs::is_directory(fs::path(root_) / n, ec)) n += '/';
    }

    // Built-in rule: the repository folder itself is never tracked.
    if (n == ".minigit" || n.rfind(".minigit/", 0) == 0) {
        res.status = IgnoreResult::IGNORED;
        res.matchedPattern = ".minigit/";
        res.message = "built-in rule";
        return res;
    }

    Trie::Match a, b;
    bool ha = pathTrie_.findMatch(n, a);
    bool hb = extTrie_.findMatch(reverseString(n), b);
    if (!ha && !hb) {
        res.status = IgnoreResult::NOT_IGNORED;
        return res;
    }
    const Trie::Match& best = (ha && (!hb || a.line <= b.line)) ? a : b;
    res.status = IgnoreResult::IGNORED;
    res.matchedPattern = best.pattern;
    res.lineNumber = best.line;
    return res;
}

bool IgnoreManager::isIgnored(const std::string& path) const {
    return verify(path).status == IgnoreResult::IGNORED;
}

IgnoreResult ignoreAdd(const std::string& path, const std::string& root) {
    IgnoreManager im(root);
    return im.addPattern(path);
}
IgnoreResult ignoreRemove(const std::string& path, const std::string& root) {
    IgnoreManager im(root);
    return im.removePattern(path);
}
IgnoreResult ignoreVerify(const std::string& path, const std::string& root) {
    IgnoreManager im(root);
    return im.verify(path);
}

}  // namespace minigit
