#include "InitCommand.h"

#include <filesystem>
#include <fstream>

#include "PathUtils.h"

namespace fs = std::filesystem;

namespace minigit {

InitResult initRepo(const std::string& root) {
    InitResult r;
    std::error_code ec;
    fs::path rootPath = fs::weakly_canonical(fs::path(root), ec);
    fs::path gitDir = rootPath / ".minigit";
    fs::path objectsDir = gitDir / "objects";
    fs::path headFile = gitDir / "HEAD";
    fs::path indexFile = gitDir / "index";

    bool alreadyExists = fs::is_directory(gitDir, ec);

    if (!fs::create_directories(objectsDir, ec) && !fs::is_directory(objectsDir, ec)) {
        r.status = InitResult::ERROR;
        r.message = "cannot create directory '" + objectsDir.string() + "'";
        return r;
    }

    // Initialize HEAD if it does not exist
    if (!fs::exists(headFile, ec)) {
        std::ofstream head(headFile.string());
        if (!head) {
            r.status = InitResult::ERROR;
            r.message = "cannot create file '" + headFile.string() + "'";
            return r;
        }
        head << "ref: refs/heads/main\n";
    }

    // Initialize index if it does not exist
    if (!fs::exists(indexFile, ec)) {
        std::ofstream index(indexFile.string());
        if (!index) {
            r.status = InitResult::ERROR;
            r.message = "cannot create file '" + indexFile.string() + "'";
            return r;
        }
    }

    // Initialize .minigitignore at repo root if it does not exist
    fs::path ignoreFile = rootPath / ".minigitignore";
    if (!fs::exists(ignoreFile, ec)) {
        std::ofstream ig(ignoreFile.string());
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
