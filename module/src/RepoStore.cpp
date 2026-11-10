#include "RepoStore.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <sstream>

#include "PathUtils.h"

namespace fs = std::filesystem;

namespace minigit {
namespace repo {

static std::string trim(const std::string& s) {
    std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static fs::path gitDir(const std::string& root) { return fs::path(root) / ".minigit"; }

static bool isValidObjectId(const std::string& id) {
    if (id.size() != 40) return false;
    for (char c : id) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

static bool isSafeSnapshotPath(const std::string& raw, std::string& normalized) {
    if (raw.empty() || raw.find('\0') != std::string::npos ||
        raw.find('\n') != std::string::npos || raw.find('\r') != std::string::npos) {
        return false;
    }
    normalized = normalizePath(raw);
    if (normalized.empty() || normalized == "." || normalized.back() == '/' || isOutsideRepo(normalized))
        return false;

    // Commit metadata is written with normalized, repository-relative paths.
    // Reject aliases such as a/../b rather than silently mapping them to b.
    if (normalized != raw) return false;
    for (const auto& part : fs::path(normalized)) {
        const std::string component = part.generic_string();
        if (component.empty() || component == "." || component == "..") return false;
    }
    return true;
}

static bool isWithinDirectory(const fs::path& base, const fs::path& candidate) {
    const fs::path relative = candidate.lexically_relative(base);
    if (relative.empty()) return candidate == base;
    return !isOutsideRepo(relative.generic_string());
}

static bool safeGitDirectory(const std::string& root, fs::path& canonicalGit) {
    std::error_code ec;
    const fs::path gd = gitDir(root);
    fs::file_status st = fs::symlink_status(gd, ec);
    if (ec || fs::is_symlink(st) || !fs::is_directory(st)) return false;
    const fs::path canonicalRoot = fs::weakly_canonical(fs::path(root), ec);
    if (ec) return false;
    canonicalGit = fs::weakly_canonical(gd, ec);
    return !ec && canonicalGit != canonicalRoot && isWithinDirectory(canonicalRoot, canonicalGit);
}

static std::string firstLineOf(const fs::path& p) {
    std::ifstream in(p.string());
    if (!in) return "";
    std::string line;
    std::getline(in, line);
    return trim(line);
}

// ---- HEAD and branches --------------------------------------------------------

std::string readHeadRaw(const std::string& root) {
    const fs::path gd = gitDir(root);
    const fs::path head = gd / "HEAD";
    std::error_code ec;
    fs::file_status st = fs::symlink_status(head, ec);
    if (ec || fs::is_symlink(st) || !fs::is_regular_file(st)) return "";
    fs::path canonicalGit;
    if (!safeGitDirectory(root, canonicalGit)) return "";
    fs::path canonicalHead = fs::weakly_canonical(head, ec);
    if (ec || !isWithinDirectory(canonicalGit, canonicalHead) || canonicalHead == canonicalGit) return "";
    return firstLineOf(canonicalHead);
}

bool writeHeadRaw(const std::string& root, const std::string& raw) {
    if (raw.rfind("ref:", 0) == 0) {
        const std::string ref = trim(raw.substr(4));
        const std::string prefix = "refs/heads/";
        std::string err;
        if (ref.rfind(prefix, 0) != 0 || !isValidBranchName(ref.substr(prefix.size()), err)) return false;
    } else if (!isValidObjectId(raw)) {
        return false;
    }

    const fs::path gd = gitDir(root);
    const fs::path headPath = gd / "HEAD";
    std::error_code ec;
    fs::file_status headStatus = fs::symlink_status(headPath, ec);
    if (ec || fs::is_symlink(headStatus) || !fs::is_regular_file(headStatus)) return false;
    fs::path canonicalGit;
    if (!safeGitDirectory(root, canonicalGit)) return false;
    fs::path canonicalHead = fs::weakly_canonical(headPath, ec);
    if (ec || !isWithinDirectory(canonicalGit, canonicalHead) || canonicalHead == canonicalGit) return false;
    std::ofstream out(canonicalHead.string(), std::ios::trunc);
    if (!out) return false;
    out << raw << "\n";
    out.close();
    return static_cast<bool>(out);
}

bool isDetached(const std::string& root) { return readHeadRaw(root).rfind("ref:", 0) != 0; }

std::string currentBranch(const std::string& root) {
    std::string head = readHeadRaw(root);
    if (head.rfind("ref:", 0) != 0) return "";
    std::string ref = trim(head.substr(4));
    const std::string prefix = "refs/heads/";
    if (ref.rfind(prefix, 0) != 0) return "";
    std::string name = ref.substr(prefix.size());
    std::string err;
    return isValidBranchName(name, err) ? name : "";
}

std::string headCommitId(const std::string& root) {
    std::string head = readHeadRaw(root);
    if (head.rfind("ref:", 0) == 0) {
        const std::string ref = trim(head.substr(4));
        const std::string prefix = "refs/heads/";
        if (ref.rfind(prefix, 0) != 0) return "";
        std::string err;
        const std::string name = ref.substr(prefix.size());
        if (!isValidBranchName(name, err)) return "";
        return readBranch(root, name);
    }
    return isValidObjectId(head) ? head : "";
}

bool isValidBranchName(const std::string& name, std::string& err) {
    if (name.empty()) {
        err = "branch name cannot be empty";
        return false;
    }
    if (name.size() > 100) {
        err = "branch name is too long (max 100 characters)";
        return false;
    }
    if (name[0] == '-' || name[0] == '.') {
        err = "branch name cannot start with '" + std::string(1, name[0]) + "'";
        return false;
    }
    for (char c : name) {
        bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.';
        if (!ok) {
            err = "branch name may only use letters, digits, '-', '_' and '.'";
            return false;
        }
    }
    if (name.find("..") != std::string::npos || name.find("@{") != std::string::npos ||
        (name.size() >= 5 && name.compare(name.size() - 5, 5, ".lock") == 0) || name.back() == '.') {
        err = "'" + name + "' is not a valid branch name";
        return false;
    }
    if (name == "HEAD") {
        err = "'HEAD' cannot be used as a branch name";
        return false;
    }
    return true;
}

static fs::path branchPath(const std::string& root, const std::string& name) {
    return gitDir(root) / "refs" / "heads" / name;
}

bool branchFileExists(const std::string& root, const std::string& name) {
    std::string err;
    if (!isValidBranchName(name, err)) return false;
    const fs::path gd = gitDir(root);
    const fs::path refsDir = gd / "refs" / "heads";
    const fs::path file = branchPath(root, name);
    std::error_code ec;
    fs::file_status st = fs::symlink_status(file, ec);
    if (ec || fs::is_symlink(st) || !fs::is_regular_file(st)) return false;
    fs::path canonicalGit;
    if (!safeGitDirectory(root, canonicalGit)) return false;
    fs::path canonicalRefs = fs::weakly_canonical(refsDir, ec);
    if (ec || !isWithinDirectory(canonicalGit, canonicalRefs)) return false;
    fs::path canonicalFile = fs::weakly_canonical(file, ec);
    return !ec && isWithinDirectory(canonicalRefs, canonicalFile) && canonicalFile != canonicalRefs;
}

std::string readBranch(const std::string& root, const std::string& name) {
    if (!branchFileExists(root, name)) return "";
    std::string id = firstLineOf(branchPath(root, name));
    return isValidObjectId(id) ? id : "";
}

bool writeBranch(const std::string& root, const std::string& name, const std::string& commitId) {
    std::string err;
    if (!isValidBranchName(name, err) || (!commitId.empty() && !isValidObjectId(commitId))) return false;
    std::error_code ec;
    const fs::path gd = gitDir(root);
    const fs::path refsDir = gd / "refs" / "heads";
    const fs::path file = branchPath(root, name);
    fs::create_directories(refsDir, ec);
    if (ec) return false;
    fs::path canonicalGit;
    if (!safeGitDirectory(root, canonicalGit)) return false;
    fs::path canonicalRefs = fs::weakly_canonical(refsDir, ec);
    if (ec || !isWithinDirectory(canonicalGit, canonicalRefs)) return false;

    fs::file_status oldStatus = fs::symlink_status(file, ec);
    if (!ec && (fs::is_symlink(oldStatus) ||
                (fs::exists(oldStatus) && !fs::is_regular_file(oldStatus)))) return false;
    if (ec == std::errc::no_such_file_or_directory) ec.clear();
    if (ec) return false;

    std::ofstream out(file.string(), std::ios::trunc);
    if (!out) return false;
    out << commitId << "\n";
    out.close();
    return static_cast<bool>(out);
}

std::vector<std::string> listBranchNames(const std::string& root) {
    std::vector<std::string> names;
    std::error_code ec;
    fs::path dir = gitDir(root) / "refs" / "heads";
    fs::path canonicalGit;
    if (!safeGitDirectory(root, canonicalGit)) return names;
    fs::path canonicalRefs = fs::weakly_canonical(dir, ec);
    if (ec || !isWithinDirectory(canonicalGit, canonicalRefs) || !fs::is_directory(dir, ec)) return names;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code ec2;
        fs::file_status st = fs::symlink_status(it->path(), ec2);
        if (ec2 || fs::is_symlink(st) || !fs::is_regular_file(st)) continue;
        std::string name = it->path().filename().string();
        std::string err;
        if (isValidBranchName(name, err)) names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

// ---- Commit objects -------------------------------------------------------------

bool readCommitMeta(const std::string& root, const std::string& id, CommitMeta& out) {
    out = CommitMeta();
    auto fail = [&]() {
        out = CommitMeta();
        return false;
    };
    if (!isValidObjectId(id)) return fail();

    const fs::path objectsDir = gitDir(root) / "objects";
    const fs::path objectDir = objectsDir / id;
    const fs::path metaPath = objectDir / "metadata";
    std::error_code ec;
    if (!fs::is_regular_file(metaPath, ec) || ec) return fail();

    // Do not follow an object-directory or metadata symlink outside objects/<id>.
    fs::path canonicalGit;
    if (!safeGitDirectory(root, canonicalGit)) return fail();
    fs::path canonicalObject = fs::weakly_canonical(objectDir, ec);
    if (ec) return fail();
    fs::path canonicalObjects = fs::weakly_canonical(objectsDir, ec);
    if (ec || !isWithinDirectory(canonicalGit, canonicalObjects) ||
        !isWithinDirectory(canonicalObjects, canonicalObject)) return fail();
    fs::path canonicalMeta = fs::weakly_canonical(metaPath, ec);
    if (ec || !isWithinDirectory(canonicalObject, canonicalMeta) || canonicalMeta == canonicalObject) return fail();

    std::ifstream in(canonicalMeta.string());
    if (!in) return fail();

    std::string firstParent;
    std::string secondParent;
    std::string line;
    std::size_t filesExpected = 0;
    std::size_t filesRead = 0;
    bool sawCommit = false;
    bool sawParent = false;
    bool sawParent2 = false;
    bool sawTimestamp = false;
    bool sawAuthor = false;
    bool sawMessage = false;
    bool sawFiles = false;
    bool readingFiles = false;
    std::set<std::string> uniqueFiles;

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (readingFiles) {
            std::string normalized;
            if (!isSafeSnapshotPath(line, normalized) || !uniqueFiles.insert(normalized).second) return fail();
            out.files.push_back(normalized);
            ++filesRead;
            if (filesRead == filesExpected) readingFiles = false;
            continue;
        }

        if (line.rfind("commit ", 0) == 0) {
            if (sawCommit || trim(line.substr(7)) != id) return fail();
            sawCommit = true;
        } else if (line.rfind("parent2 ", 0) == 0) {
            if (sawParent2) return fail();
            secondParent = trim(line.substr(8));
            if (secondParent == "none") secondParent.clear();
            if (!secondParent.empty() && !isValidObjectId(secondParent)) return fail();
            sawParent2 = true;
        } else if (line.rfind("parent ", 0) == 0) {
            if (sawParent) return fail();
            firstParent = trim(line.substr(7));
            if (firstParent == "none") firstParent.clear();
            if (!firstParent.empty() && !isValidObjectId(firstParent)) return fail();
            sawParent = true;
        } else if (line.rfind("timestamp ", 0) == 0) {
            const std::string value = trim(line.substr(10));
            if (sawTimestamp || value.empty() ||
                !std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c); })) return fail();
            out.timestamp = value;
            sawTimestamp = true;
        } else if (line.rfind("author ", 0) == 0) {
            if (sawAuthor || trim(line.substr(7)).empty()) return fail();
            out.author = line.substr(7);
            sawAuthor = true;
        } else if (line.rfind("message ", 0) == 0) {
            if (sawMessage || trim(line.substr(8)).empty()) return fail();
            out.message = line.substr(8);
            sawMessage = true;
        } else if (line.rfind("files ", 0) == 0) {
            if (sawFiles) return fail();
            const std::string countText = trim(line.substr(6));
            if (countText.empty() ||
                !std::all_of(countText.begin(), countText.end(), [](unsigned char c) { return std::isdigit(c); })) return fail();
            std::istringstream countStream(countText);
            unsigned long long count = 0;
            char trailing = '\0';
            if (!(countStream >> count) || (countStream >> trailing) ||
                count > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) return fail();
            filesExpected = static_cast<std::size_t>(count);
            if (filesExpected > 1000000) return fail();  // reject unreasonable/corrupt metadata
            sawFiles = true;
            readingFiles = filesExpected != 0;
        } else if (sawFiles) {
            // After the declared path list, allow future key/value extensions
            // (for example `tree <id>`), but not stray undeclared path lines.
            const std::size_t separator = line.find(' ');
            if (separator == std::string::npos || separator == 0 || line.find('/') != std::string::npos ||
                line.find('\\') != std::string::npos) return fail();
            for (std::size_t i = 0; i < separator; ++i) {
                const unsigned char c = static_cast<unsigned char>(line[i]);
                if (!std::isalnum(c) && c != '_' && c != '-') return fail();
            }
        }
        // Unknown metadata keys (for example a future tree-object field) are
        // intentionally ignored for forward compatibility.
    }

    if (in.bad() || readingFiles || filesRead != filesExpected || !sawCommit || !sawParent ||
        !sawTimestamp || !sawAuthor || !sawMessage || !sawFiles) {
        out = CommitMeta();
        return fail();
    }

    if (!secondParent.empty() && (firstParent.empty() || secondParent == firstParent)) return fail();
    if (!firstParent.empty()) out.parents.push_back(firstParent);
    if (!secondParent.empty()) out.parents.push_back(secondParent);
    out.id = id;
    return true;
}

bool readSnapshotFile(const std::string& root, const std::string& id, const std::string& relPath,
                      std::string& content) {
    content.clear();
    if (!isValidObjectId(id)) return false;
    std::string norm;
    if (!isSafeSnapshotPath(relPath, norm)) return false;

    const fs::path objectsDir = gitDir(root) / "objects";
    const fs::path snapshotRoot = objectsDir / id / "snapshot";
    const fs::path snapshotPath = snapshotRoot / fs::path(norm);
    std::error_code ec;
    if (!fs::is_regular_file(snapshotPath, ec) || ec) return false;

    fs::path canonicalGit;
    if (!safeGitDirectory(root, canonicalGit)) return false;
    fs::path canonicalObjects = fs::weakly_canonical(objectsDir, ec);
    if (ec || !isWithinDirectory(canonicalGit, canonicalObjects)) return false;
    fs::path canonicalSnapshot = fs::weakly_canonical(snapshotRoot, ec);
    if (ec || !isWithinDirectory(canonicalObjects, canonicalSnapshot)) return false;
    fs::path canonicalFile = fs::weakly_canonical(snapshotPath, ec);
    if (ec || !isWithinDirectory(canonicalSnapshot, canonicalFile) || canonicalFile == canonicalSnapshot) return false;

    std::ifstream in(canonicalFile.string(), std::ios::binary);
    if (!in) return false;
    content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

std::vector<std::string> listCommitIds(const std::string& root) {
    std::vector<std::string> ids;
    std::error_code ec;
    fs::path dir = gitDir(root) / "objects";
    fs::path canonicalGit;
    if (!safeGitDirectory(root, canonicalGit)) return ids;
    fs::path canonicalObjects = fs::weakly_canonical(dir, ec);
    if (ec || !isWithinDirectory(canonicalGit, canonicalObjects) || !fs::is_directory(dir, ec)) return ids;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code ec2;
        std::string id = it->path().filename().string();
        if (isValidObjectId(id) && fs::is_regular_file(it->path() / "metadata", ec2) && !ec2)
            ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

static bool validateCommitDag(const std::string& root, const std::string& startId,
                              std::string* errorMessage) {
    struct Frame {
        std::string id;
        bool expanded;
    };
    std::vector<Frame> stack;
    std::map<std::string, int> color;  // 0 = unseen, 1 = active, 2 = complete
    stack.push_back({startId, false});

    while (!stack.empty()) {
        Frame frame = stack.back();
        stack.pop_back();
        if (frame.expanded) {
            color[frame.id] = 2;
            continue;
        }

        const int state = color[frame.id];
        if (state == 1) {
            if (errorMessage) *errorMessage = "commit graph contains a parent cycle at " + shortId(frame.id);
            return false;
        }
        if (state == 2) continue;

        CommitMeta meta;
        if (!readCommitMeta(root, frame.id, meta)) {
            if (errorMessage) *errorMessage = "missing or invalid parent commit " + shortId(frame.id);
            return false;
        }
        color[frame.id] = 1;
        stack.push_back({frame.id, true});
        for (auto it = meta.parents.rbegin(); it != meta.parents.rend(); ++it)
            stack.push_back({*it, false});
    }
    return true;
}

bool effectiveTree(const std::string& root, const std::string& commitId,
                   std::map<std::string, std::string>& tree,
                   std::string* errorMessage) {
    tree.clear();
    if (errorMessage) errorMessage->clear();

    auto fail = [&](const std::string& message) {
        tree.clear();
        if (errorMessage) *errorMessage = message;
        return false;
    };

    if (!isValidObjectId(commitId)) return fail("invalid commit id");

    std::string dagError;
    if (!validateCommitDag(root, commitId, &dagError)) return fail(dagError);

    // Walk only the first-parent chain. For the delta-snapshot format used by
    // commitRepo, a merge commit must store every merged result path that differs
    // from its first parent. The graph still retains both parent links.
    std::vector<CommitMeta> chain;
    std::set<std::string> visited;
    std::string currentId = commitId;
    while (!currentId.empty()) {
        if (!visited.insert(currentId).second)
            return fail("commit history contains a first-parent cycle at " + shortId(currentId));

        CommitMeta meta;
        if (!readCommitMeta(root, currentId, meta))
            return fail("cannot read or validate metadata for commit " + shortId(currentId));

        chain.push_back(meta);
        currentId = meta.parents.empty() ? "" : meta.parents.front();
    }

    // Apply old snapshots first so newer commits replace only the paths they
    // changed. Files not listed by a later commit remain inherited.
    for (auto commit = chain.rbegin(); commit != chain.rend(); ++commit) {
        for (const std::string& path : commit->files) {
            std::string content;
            if (!readSnapshotFile(root, commit->id, path, content))
                return fail("snapshot of '" + path + "' is missing or unsafe in commit " + shortId(commit->id));
            tree[path] = content;
        }
    }

    // A file cannot also be a parent directory of another tracked file.
    // Without this check a restore could write one path and then fail midway
    // while trying to create a directory below it.
    for (const auto& item : tree) {
        const std::string& path = item.first;
        std::size_t slash = path.find('/');
        while (slash != std::string::npos) {
            const std::string ancestor = path.substr(0, slash);
            if (tree.count(ancestor))
                return fail("commit tree contains a file/directory path conflict at '" + ancestor + "'");
            slash = path.find('/', slash + 1);
        }
    }

    return true;
}

IdLookup resolveCommitId(const std::string& root, const std::string& text, std::string& fullId) {
    fullId.clear();
    std::string t = text;
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (t.size() < 4) return IdLookup::TOO_SHORT;
    if (t.size() > 40 || !std::all_of(t.begin(), t.end(), [](unsigned char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        })) return IdLookup::NOT_FOUND;

    std::vector<std::string> matches;
    for (const std::string& id : listCommitIds(root)) {
        if (id == t) {
            fullId = id;
            return IdLookup::FOUND;
        }
        if (id.compare(0, t.size(), t) == 0) matches.push_back(id);
    }
    if (matches.empty()) return IdLookup::NOT_FOUND;
    if (matches.size() > 1) return IdLookup::AMBIGUOUS;
    fullId = matches[0];
    return IdLookup::FOUND;
}

std::string shortId(const std::string& id) { return id.size() >= 7 ? id.substr(0, 7) : id; }

}  // namespace repo
}  // namespace minigit
