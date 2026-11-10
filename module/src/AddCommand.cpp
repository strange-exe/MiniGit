#include "AddCommand.h"

#include <filesystem>
#include <set>

#include "PathUtils.h"
#include "RepoUtils.h"

namespace fs = std::filesystem;

namespace minigit {

static bool isRepo(const std::string& root) {
    std::error_code ec;
    return fs::is_directory(fs::path(root) / ".minigit", ec);
}

static AddResult fail(const std::string& msg) {
    AddResult r;
    r.status = AddResult::ERROR;
    r.message = msg;
    return r;
}

// Every path the active branch's latest commit tracks (see getTrackedFilesAtCommit).
static std::set<std::string> trackedAtHead(const std::string& root) {
    std::set<std::string> paths;
    for (const auto& kv : getTrackedFilesAtCommit(root, getHEADCommitId(root))) paths.insert(kv.first);
    return paths;
}

// True if `tracked` holds a path inside the directory `dirRel` (given without trailing '/').
static bool trackedUnder(const std::set<std::string>& tracked, const std::string& dirRel) {
    std::string prefix = dirRel.empty() ? std::string() : dirRel + "/";
    auto it = tracked.lower_bound(prefix);
    return it != tracked.end() && it->compare(0, prefix.size(), prefix) == 0;
}

// Recursively stage every non-ignored file under `startRel` ("" = repo root).
// Tracked files are staged even when an ignore rule matches them.
static void stageTree(const std::string& root, const std::string& startRel, IgnoreManager& im,
                      const std::set<std::string>& tracked, StagingArea& sa, AddResult& r) {
    fs::path rootPath(root);
    fs::path start = startRel.empty() ? rootPath : rootPath / startRel;
    std::error_code ec;
    fs::recursive_directory_iterator it(start, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        std::string rel = it->path().lexically_relative(rootPath).generic_string();
        if (it->is_directory(e2)) {
            if (rel == ".minigit") {
                it.disable_recursion_pending();
            } else if (im.isIgnored(rel + "/") && !trackedUnder(tracked, rel)) {
                it.disable_recursion_pending();  // skip whole ignored directory
                ++r.skippedCount;
            }
            continue;
        }
        if (!it->is_regular_file(e2)) continue;
        if (im.isIgnored(rel) && tracked.count(rel) == 0) {
            ++r.skippedCount;
            continue;
        }
        if (sa.stage(rel)) ++r.stagedCount;
        else ++r.alreadyStagedCount;
    }
}

// Stages a deletion for every tracked file under `dirRel` ("" = everything) that is no longer on disk.
static void stageDeletionsUnder(const std::string& root, const std::string& dirRel,
                                const std::set<std::string>& tracked, StagingArea& sa, AddResult& r) {
    std::string prefix = dirRel.empty() ? std::string() : dirRel + "/";
    for (const std::string& rel : tracked) {
        if (rel.compare(0, prefix.size(), prefix) != 0) continue;
        std::error_code ec;
        if (fs::exists(fs::path(root) / rel, ec)) continue;
        if (sa.stageDeletion(rel)) ++r.deletionsStaged;
        else ++r.alreadyStagedCount;
    }
}

static AddResult summarize(AddResult r) {
    bool nothingNew = r.stagedCount == 0 && r.deletionsStaged == 0;
    r.status = (nothingNew && r.alreadyStagedCount > 0) ? AddResult::ALREADY_STAGED : AddResult::STAGED;
    r.message = "staged " + std::to_string(r.stagedCount) + " file(s), " + std::to_string(r.alreadyStagedCount) +
                " already staged, skipped " + std::to_string(r.skippedCount) + " ignored";
    if (r.deletionsStaged > 0) r.message += ", staged " + std::to_string(r.deletionsStaged) + " deletion(s)";
    return r;
}

AddResult addAll(const std::string& root) {
    if (!isRepo(root)) return fail("not a minigit repository (run 'minigit init')");
    IgnoreManager im(root);
    StagingArea sa(root);
    std::set<std::string> tracked = trackedAtHead(root);
    AddResult r;
    stageTree(root, "", im, tracked, sa, r);
    stageDeletionsUnder(root, "", tracked, sa, r);
    if (!sa.save()) return fail("could not write .minigit/index");
    return summarize(r);
}

AddResult addFile(const std::string& path, const std::string& root) {
    if (!isRepo(root)) return fail("not a minigit repository (run 'minigit init')");
    std::string rel = normalizePath(path, root);
    if (rel.empty() || isOutsideRepo(rel)) return fail("path '" + path + "' is outside the repository");
    if (rel == ".") return addAll(root);

    IgnoreManager im(root);
    StagingArea sa(root);
    std::set<std::string> tracked = trackedAtHead(root);
    std::error_code ec;
    fs::path full = fs::path(root) / rel;
    bool isDir = fs::is_directory(full, ec);
    std::string bare = rel.back() == '/' ? rel.substr(0, rel.size() - 1) : rel;
    if (!isDir && !fs::is_regular_file(full, ec)) {
        // Gone from disk. If the last commit tracks it (or files below it), stage the deletion.
        AddResult r;
        if (tracked.count(bare)) {
            if (sa.stageDeletion(bare)) {
                if (!sa.save()) return fail("could not write .minigit/index");
                r.status = AddResult::DELETION_STAGED;
                r.deletionsStaged = 1;
            } else {
                r.status = AddResult::ALREADY_STAGED;
                r.message = "deletion of '" + bare + "' is already staged";
            }
            return r;
        }
        if (trackedUnder(tracked, bare)) {
            stageDeletionsUnder(root, bare, tracked, sa, r);
            if (!sa.save()) return fail("could not write .minigit/index");
            r.status = r.deletionsStaged > 0 ? AddResult::DELETION_STAGED : AddResult::ALREADY_STAGED;
            return r;
        }
        r.status = AddResult::NOT_FOUND;
        r.message = "'" + rel + "' does not exist";
        return r;
    }

    IgnoreResult ig = im.verify(rel);
    bool trackedHere = isDir ? trackedUnder(tracked, bare) : tracked.count(rel) > 0;
    if (ig.status == IgnoreResult::IGNORED && !trackedHere) {
        AddResult r;
        r.status = AddResult::IGNORED;
        r.message = ig.matchedPattern;
        r.line = ig.lineNumber;
        return r;
    }

    AddResult r;
    if (isDir) {
        stageTree(root, bare, im, tracked, sa, r);
        stageDeletionsUnder(root, bare, tracked, sa, r);
        if (!sa.save()) return fail("could not write .minigit/index");
        return summarize(r);
    }
    if (!sa.stage(rel)) {
        r.status = AddResult::ALREADY_STAGED;
        return r;
    }
    if (!sa.save()) return fail("could not write .minigit/index");
    r.status = AddResult::STAGED;
    r.stagedCount = 1;
    return r;
}

AddResult addVerify(const std::string& path, const std::string& root) {
    if (!isRepo(root)) return fail("not a minigit repository (run 'minigit init')");
    std::string rel = normalizePath(path, root);
    if (rel.empty() || isOutsideRepo(rel)) return fail("path '" + path + "' is outside the repository");

    StagingArea sa(root);
    AddResult r;
    if (sa.isStaged(rel)) {
        r.status = AddResult::IS_STAGED;
        return r;
    }
    if (sa.isStagedForDeletion(rel)) {
        r.status = AddResult::IS_STAGED;
        r.message = "deletion";
        return r;
    }
    IgnoreManager im(root);
    IgnoreResult ig = im.verify(rel);
    if (ig.status == IgnoreResult::IGNORED) {
        r.status = AddResult::IGNORED;
        r.message = ig.matchedPattern;
        r.line = ig.lineNumber;
        return r;
    }
    std::error_code ec;
    if (!fs::exists(fs::path(root) / rel, ec)) {
        r.status = AddResult::NOT_FOUND;
        return r;
    }
    r.status = AddResult::NOT_STAGED;
    return r;
}

AddResult removeFile(const std::string& path, const std::string& root) {
    if (!isRepo(root)) return fail("not a minigit repository (run 'minigit init')");
    StagingArea sa(root);
    AddResult r;
    if (path == ".") {  // unstage everything
        r.stagedCount = static_cast<int>(sa.size());
        r.status = r.stagedCount > 0 ? AddResult::UNSTAGED : AddResult::NOT_STAGED;
        if (r.stagedCount > 0) sa.clear();
        return r;
    }
    std::string rel = normalizePath(path, root);
    if (rel.empty() || isOutsideRepo(rel)) return fail("path '" + path + "' is outside the repository");
    int unstaged = sa.unstage(rel) ? 1 : 0;
    if (unstaged == 0) {  // a directory: unstage every staged file (and staged deletion) below it
        std::string prefix = rel.back() == '/' ? rel : rel + "/";
        for (const std::string& f : sa.getStagedFiles())
            if (f.compare(0, prefix.size(), prefix) == 0 && sa.unstage(f)) ++unstaged;
        for (const std::string& f : sa.getStagedDeletions())
            if (f.compare(0, prefix.size(), prefix) == 0 && sa.unstage(f)) ++unstaged;
    }
    if (unstaged == 0) {
        r.status = AddResult::NOT_STAGED;
        return r;
    }
    if (!sa.save()) return fail("could not write .minigit/index");
    r.status = AddResult::UNSTAGED;  // file on disk is never touched
    r.stagedCount = unstaged;
    return r;
}

}  // namespace minigit
