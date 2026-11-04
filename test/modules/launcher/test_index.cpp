#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "config/launcher_config.h"

#include "modules/launcher/files.h"
#include "modules/launcher/index.h"
#include "modules/launcher/visits.h"

#include "check.h"

using test::check;

namespace {

namespace fs = std::filesystem;

void touch(const fs::path &path) { std::ofstream(path).put('x'); }

bool has(const std::vector<astralia::FileEntry> &found, const std::string &name, bool is_dir) {
    for (const astralia::FileEntry &entry : found) {
        if (entry.name == name && entry.is_dir == is_dir) {
            return true;
        }
    }
    return false;
}

} // namespace

void check_launcher_index() {
    fs::path root = fs::temp_directory_path() / ("astralia_test_index_" + std::to_string(getpid()));
    fs::create_directories(root / "Projects" / ".hidden_dir");
    touch(root / "Projects" / "report.pdf");
    touch(root / "Projects" / ".hidden_dir" / "secret_report.txt");
    touch(root / "notes.txt");
    touch(root / "my-notes.txt");
    fs::create_symlink(root / "notes.txt", root / "link_notes.txt");
    fs::create_directory_symlink(root / "Projects", root / "link_projects");

    std::atomic<bool> cancel = false;
    auto index = astralia::launcher::file_index_build(root.string() + "/", cancel);
    check(index->root == root.string(), "the index root drops a trailing slash");
    check(index->nodes.size() == 6, "files and folders are indexed, symlinks and the root are not");

    astralia::VisitStore visits;
    auto found = astralia::launcher::file_index_search(*index, "REPORT", visits, 20);
    check(found.size() == 2 && has(found, "report.pdf", false) &&
              has(found, "secret_report.txt", false),
          "matching ignores case and keeps hidden entries");
    for (const astralia::FileEntry &entry : found) {
        check(entry.score == astralia::launcher::score_path(entry.name, "REPORT"),
              "the index scores like score_path");
        check(entry.path == (root / "Projects" / (entry.name == "report.pdf" ? "" : ".hidden_dir") /
                             entry.name)
                                .lexically_normal()
                                .string(),
              "a file path is rebuilt from the tree");
    }

    auto dirs = astralia::launcher::file_index_search(*index, "proj", visits, 20);
    check(dirs.size() == 1 && dirs[0].is_dir && dirs[0].path == (root / "Projects").string() + "/",
          "a folder path ends with a slash, like fd");
    check(astralia::launcher::file_index_search(*index, "rep*pdf", visits, 20).size() == 1,
          "ordered parts match");
    check(astralia::launcher::file_index_search(*index, "pdf*rep", visits, 20).empty(),
          "parts out of order do not match");
    check(astralia::launcher::file_index_search(*index, "Projects/report", visits, 20).empty(),
          "a query with a slash matches no basename");
    check(astralia::launcher::file_index_search(*index, "", visits, 20).empty(),
          "an empty query finds nothing");

    auto notes = astralia::launcher::file_index_search(*index, "notes", visits, 1);
    check(notes.size() == 1 && notes[0].name == "notes.txt", "the best score survives the cut");
    visits.counts[astralia::launcher::visit_store_file_key((root / "my-notes.txt").string())] = 3;
    notes = astralia::launcher::file_index_search(*index, "notes", visits, 1);
    check(notes.size() == 1 && notes[0].name == "my-notes.txt",
          "a visited file outranks a better score");

    cancel = true;
    auto stopped = astralia::launcher::file_index_build(root.string(), cancel);
    check(stopped->nodes.empty(), "a cancelled build stops");

    fs::remove_all(root);
}
