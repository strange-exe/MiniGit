#include "InitCommand.h"

#include <filesystem>
#include <fstream>
#include <utility>

#include "PathUtils.h"

namespace fs = std::filesystem;

namespace minigit {

// Creates `dir` (and parents) unless it already exists as a directory.
static bool ensureDirectory(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    return fs::is_directory(dir, ec);
}

// Writes `content` to `file` only if the file does not exist yet, so
// re-running init never overwrites HEAD, the index or the user's ignore rules.
static bool ensureFile(const fs::path& file, const std::string& content) {
    std::error_code ec;
    if (fs::exists(file, ec)) return true;
    std::ofstream out(file.string());
    out << content;
    out.close();
    return static_cast<bool>(out);
}

InitResult initRepo(const std::string& root) {
    InitResult r;
    std::error_code ec;
    fs::path rootPath = fs::weakly_canonical(fs::path(root), ec);
    fs::path gitDir = rootPath / ".minigit";

    bool alreadyExists = fs::is_directory(gitDir, ec);

    // objects/ holds commit snapshots; refs/heads/ holds the branch file HEAD points to.
    for (const fs::path& dir : {gitDir / "objects", gitDir / "refs" / "heads"}) {
        if (!ensureDirectory(dir)) {
            r.status = InitResult::ERROR;
            r.message = "cannot create directory '" + dir.string() + "'";
            return r;
        }
    }

    const std::pair<fs::path, std::string> files[] = {
        {gitDir / "HEAD", "ref: refs/heads/main\n"},
        {gitDir / "index", ""},
        {rootPath / ".minigitignore", ""},
    };
    for (const auto& f : files) {
        if (!ensureFile(f.first, f.second)) {
            r.status = InitResult::ERROR;
            r.message = "cannot create file '" + f.first.string() + "'";
            return r;
        }
    }

    r.path = gitDir.string();
    if (alreadyExists) {
        r.status = InitResult::REINITIALIZED;
        r.message = "Reinitialized existing MiniGit repository in " + gitDir.string();
    } else {
        r.status = InitResult::INITIALIZED;
        r.message = "Initialized empty MiniGit repository in " + gitDir.string();
    }
    return r;
}

}  // namespace minigit
