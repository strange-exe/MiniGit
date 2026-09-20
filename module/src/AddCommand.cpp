#include "AddCommand.h"

#include <filesystem>

#include "PathUtils.h"

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

// Recursively stage every non-ignored file under `startRel` ("" = repo root).
static void stageTree(const std::string& root, const std::string& startRel, IgnoreManager& im,
                      StagingArea& sa, AddResult& r) {
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
            } else if (im.isIgnored(rel + "/")) {
                it.disable_recursion_pending();  // skip whole ignored directory
                ++r.skippedCount;
            }
            continue;
        }
        if (!it->is_regular_file(e2)) continue;
        if (im.isIgnored(rel)) {
            ++r.skippedCount;
            continue;
        }
        if (sa.stage(rel)) ++r.stagedCount;
        else ++r.alreadyStagedCount;
    }
}

static AddResult summarize(AddResult r) {
    r.status = (r.stagedCount == 0 && r.alreadyStagedCount > 0) ? AddResult::ALREADY_STAGED : AddResult::STAGED;
    r.message = "staged " + std::to_string(r.stagedCount) + " file(s), " + std::to_string(r.alreadyStagedCount) +
                " already staged, skipped " + std::to_string(r.skippedCount) + " ignored";
    return r;
}

AddResult addAll(const std::string& root) {
    if (!isRepo(root)) return fail("not a minigit repository (run 'minigit init')");
    IgnoreManager im(root);
    StagingArea sa(root);
    AddResult r;
    stageTree(root, "", im, sa, r);
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
    std::error_code ec;
    fs::path full = fs::path(root) / rel;
    bool isDir = fs::is_directory(full, ec);
    if (!isDir && !fs::is_regular_file(full, ec)) {
        AddResult r;
        r.status = AddResult::NOT_FOUND;
        r.message = "'" + rel + "' does not exist";
        return r;
    }

    IgnoreResult ig = im.verify(rel);
    if (ig.status == IgnoreResult::IGNORED) {
        AddResult r;
        r.status = AddResult::IGNORED;
        r.message = ig.matchedPattern;
        r.line = ig.lineNumber;
        return r;
    }

    AddResult r;
    if (isDir) {
        stageTree(root, rel, im, sa, r);
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
    if (!sa.unstage(rel)) {
        r.status = AddResult::NOT_STAGED;
        return r;
    }
    if (!sa.save()) return fail("could not write .minigit/index");
    r.status = AddResult::UNSTAGED;  // file on disk is never touched
    r.stagedCount = 1;
    return r;
}

}  // namespace minigit
