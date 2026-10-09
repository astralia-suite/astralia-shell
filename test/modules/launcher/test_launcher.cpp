#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "check.h"

#include "config/launcher_config.h"

#include "modules/launcher/apps_provider.h"
#include "modules/launcher/desktop_entry.h"
#include "modules/launcher/files_provider.h"
#include "modules/launcher/launch_action.h"
#include "modules/launcher/search.h"
#include "modules/launcher/submenu.h"
#include "modules/launcher/visit_store.h"

using test::check;

void check_launcher_text() {
    check(astralia::elide("abcdef", 4) == "abc…", "elide keeps max - 1 chars");
    check(astralia::elide("héllo", 5) == "héllo", "elide counts UTF-8 chars");
    check(astralia::elide_middle("abcdefghij", 5) == "ab…ij", "elide_middle keeps both ends");
    check(astralia::collapse_home("/home/u/docs", "/home/u") == "~/docs", "home collapses");
    check(astralia::collapse_home("/home/user2", "/home/u") == "/home/user2", "prefix without slash is kept");
    check(astralia::basename_of("/a/b/") == "b" && astralia::basename_of("/") == "/", "basename_of");
    check(astralia::basename_of("/home/user/file.txt") == "file.txt", "basename_of a file");
    check(astralia::parent_of("/a/b") == "/a" && astralia::parent_of("/a") == "/", "parent_of");
}

void check_launcher_modes() {
    using astralia::detect_mode_and_query;
    using astralia::LauncherMode;
    check(detect_mode_and_query("  fire").mode == LauncherMode::drun, "plain text is drun");
    check(detect_mode_and_query("  fire").query == "fire", "leading space is trimmed");
    check(detect_mode_and_query("").mode == LauncherMode::drun && detect_mode_and_query("").query.empty(), "empty is drun");
    check(detect_mode_and_query("> ls -l").mode == LauncherMode::run && detect_mode_and_query("> ls -l").query == "ls -l", "> is run mode");
    check(detect_mode_and_query("gg cats").mode == LauncherMode::google, "gg is google");
    check(detect_mode_and_query("gg cat pictures").query == "cat pictures", "gg query");
    check(detect_mode_and_query("ggx").mode == LauncherMode::drun, "word prefix needs a space");
    check(detect_mode_and_query("ggwp").mode == LauncherMode::drun, "gg inside a word is not a prefix");
    check(detect_mode_and_query("yt").mode == LauncherMode::youtube, "bare prefix switches mode");
    check(detect_mode_and_query("  yt lofi beats").query == "lofi beats", "yt query after leading space");
    check(detect_mode_and_query("ddg x").mode == LauncherMode::duckduckgo, "ddg is duckduckgo");
    check(detect_mode_and_query("url example.com").query == "example.com", "url query");
}

void check_launcher_scoring() {
    check(astralia::score_app("Firefox", "fire") > astralia::score_app("Wildfire", "fire"), "prefix match scores higher");
    check(astralia::score_app("Firefox", "fire") > astralia::score_app("Bonfire", "fire"), "earlier match scores higher");
    check(astralia::score_app("Firefox", "zz") < 0.0f, "no match is negative");
    check(astralia::score_app("Firefox", "") < 0.0f && astralia::score_app("", "fire") < 0.0f, "empty sides never match");
    check(astralia::to_glob_pattern(" notes ") == "**/*notes*", "glob wraps a bare word");
    check(astralia::to_glob_pattern("a/b") == "**/a/b", "glob anchors a path");
    check(astralia::to_glob_pattern("/abs/path") == "/abs/path", "glob keeps an absolute path");
    check(astralia::to_glob_pattern("**/already") == "**/already", "glob keeps an anchored pattern");
    check(astralia::to_glob_pattern("").empty(), "empty glob");
    check(astralia::split_query_parts("Foo * bar") == std::vector<std::string>{"foo", "bar"}, "query splits on *");
    check(astralia::split_query_parts("hello") == std::vector<std::string>{"hello"}, "single part");
    check(astralia::split_query_parts("").empty(), "no parts");
    check(astralia::score_path("report.pdf", "rep*pdf") > 0.0f, "ordered parts match");
    check(astralia::score_path("report.pdf", "pdf*rep") < 0.0f, "out of order parts fail");
    check(astralia::score_path("notes.txt", "notes") > astralia::score_path("my-notes.txt", "notes"), "earlier path match scores higher");
    check(astralia::score_path("foobarxxxxxxxxxxxxxxxxxxxx", "foo*bar") < astralia::score_path("fooxxxxxxxxxxxxxxxxxxxxbar", "foo*bar"), "tight parts score higher");
    std::vector<astralia::FileEntry> parsed = astralia::fd_search_parse_output("/a/b\n\n  /c/d  \n", true);
    check(parsed.size() == 2 && parsed[1].name == "d" && parsed[1].is_dir, "fd output parses");

    astralia::DesktopEntry firefox{"firefox.desktop", "Firefox", "firefox", "", false, false, false};
    astralia::DesktopEntry bonfire{"bonfire.desktop", "Bonfire", "bonfire", "", false, false, false};
    astralia::DesktopEntry calc{"calc.desktop", "Calculator", "calc", "", false, false, false};
    std::vector<astralia::DesktopEntry> entries{firefox, bonfire, calc};
    std::vector<astralia::ScoredApp> found = astralia::search_apps(entries, "fire");
    check(found.size() == 2 && found[0].entry->name == "Firefox", "search_apps drops non-matches and ranks");
    check(astralia::search_apps(entries, "").empty(), "empty query finds nothing");
}

void check_launcher_listing() {
    if (std::system("command -v fd >/dev/null 2>&1") != 0) {
        return;
    }
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("astralia_test_fd_" + std::to_string(getpid()));
    std::filesystem::create_directories(root / "subdir");
    std::ofstream(root / "hello.txt").put('x');
    std::vector<std::string> file_argv = astralia::fd_search_argv("**/*hello*", root.string(), false, 10);
    check(!file_argv.empty() && file_argv.front() == "fd", "fd search argv starts with fd");
    std::vector<astralia::FileEntry> files = astralia::list_directory(root.string(), false);
    std::vector<astralia::FileEntry> dirs = astralia::list_directory(root.string(), true);
    check(files.size() == 1 && files[0].name == "hello.txt" && !files[0].is_dir, "list_directory lists files");
    check(dirs.size() == 1 && dirs[0].name == "subdir" && dirs[0].is_dir, "list_directory lists directories");
    std::filesystem::remove_all(root);
}

void check_desktop_entry() {
    std::istringstream in("[Desktop Entry]\nType=Application\nName=Foo\nName=Ignored\n"
                          "Exec=foo %U\nIcon=foo\nTerminal=true\n[Desktop Action x]\nName=X\n");
    auto entry = astralia::parse_desktop_entry(in, "foo.desktop");
    check(entry && entry->name == "Foo" && entry->terminal && entry->icon == "foo" && entry->exec == "foo %U", "desktop entry parses the main section");
    std::istringstream hidden("[Desktop Entry]\nType=Application\nName=H\nExec=h\nNoDisplay=true\n");
    auto hidden_entry = astralia::parse_desktop_entry(hidden, "h.desktop");
    check(hidden_entry && hidden_entry->no_display, "NoDisplay is read");
    std::istringstream link("[Desktop Entry]\nType=Link\nName=L\nExec=l\n");
    check(!astralia::parse_desktop_entry(link, "l.desktop"), "non-applications are skipped");
    check(astralia::strip_exec_field_codes("foo %U --x %% %f") == "foo --x %", "field codes are stripped");
    check(astralia::strip_exec_field_codes("cmd %i %c %k end") == "cmd   end", "unused codes leave their spaces");
    check(astralia::desktop_entry_dirs(nullptr, "/x:/y", "/home/u") ==
              std::vector<std::string>{"/home/u/.local/share/applications", "/x/applications", "/y/applications"},
          "desktop dirs follow XDG");
}

void check_launch_urls() {
    check(astralia::normalize_url("example.com") == "http://example.com", "bare host");
    check(astralia::normalize_url("https://a.b/c") == "https://a.b/c", "scheme kept");
    check(astralia::normalize_url("//example.com") == "https://example.com", "scheme-relative");
    check(astralia::normalize_url("localhost:8080") == "http://localhost:8080", "localhost");
    check(astralia::normalize_url("192.168.1.1:9000/path") == "http://192.168.1.1:9000/path", "ipv4 with port");
    check(astralia::normalize_url("host:1234") == "http://host:1234", "host with port");
    check(astralia::normalize_url("two words") == "" && astralia::normalize_url("").empty(), "spaces and empty are not URLs");
    check(astralia::make_search_url(" a b ", "q=") == "q=a%20b", "search URL encodes");
    check(astralia::make_search_url("", "q=").empty(), "empty search URL");
    check(astralia::shell_quote("it's") == "'it'\\''s'" && astralia::shell_quote("").size() == 2, "shell quoting");
    check(astralia::shell_quote("Bob's Files") == "'Bob'\\''s Files'", "shell quoting with spaces");
    astralia::DesktopEntry htop{"htop.desktop", "htop", "htop %f", "", true, false, false};
    check(astralia::app_command(htop) == std::string(astralia::launcher_config::terminal) + " htop", "terminal apps use the configured terminal wrapper");
    astralia::DesktopEntry plain{"x.desktop", "x", "x --flag", "", false, false, false};
    check(astralia::app_command(plain) == "x --flag", "plain apps run as they are");
    check(!astralia::launch_non_drun(astralia::LauncherMode::run, "") && !astralia::launch_non_drun(astralia::LauncherMode::google, "") && !astralia::launch_non_drun(astralia::LauncherMode::drun, "x"), "empty queries and drun launch nothing");
}

void check_drun_results() {
    astralia::DesktopEntry a{"a.desktop", "Alpha", "a", "", false, false, false};
    astralia::DesktopEntry b{"b.desktop", "Beta", "b", "", false, false, false};
    std::vector<astralia::ScoredApp> apps{{&a, 900.0f}, {&b, 100.0f}};
    std::vector<astralia::FileEntry> files{{"f", "/f", false, 999.0f}, {"d", "/d", true, 1.0f}};
    astralia::VisitStore visits;
    visits.counts[astralia::visit_store_app_key("b.desktop")] = 3;
    auto results = astralia::combined_drun_results(apps, files, visits, 3);
    check(results.size() == 3, "results are capped");
    check(results[0].app == &b && results[1].app == &a, "visits outrank score among apps");
    check(results[2].kind == astralia::DrunResult::Kind::dir, "dirs come before files");
    check(astralia::combined_drun_results(apps, files, visits, 10).size() == 4, "all results fit under the cap");
}

void check_visit_store() {
    check(astralia::visit_store_path("", "/home/u") == "/home/u/.local/state/astralia-shell/launcher_visits", "visit store path from home");
    check(astralia::visit_store_path("/s", "/home/u") == "/s/astralia-shell/launcher_visits", "visit store path from state home");
    std::filesystem::path dir = std::filesystem::temp_directory_path() / ("astralia_test_visits_" + std::to_string(getpid()));
    std::string path = (dir / "nested" / "visits").string();
    astralia::VisitStore store = astralia::visit_store_load(path);
    check(astralia::visit_store_get(store, "app:x") == 0, "unknown key has no visits");
    astralia::visit_store_record(store, astralia::visit_store_app_key("firefox.desktop"));
    astralia::visit_store_record(store, astralia::visit_store_app_key("firefox.desktop"));
    astralia::visit_store_record(store, astralia::visit_store_file_key("/home/u/notes.txt"));
    astralia::visit_store_record(store, "");
    astralia::VisitStore reloaded = astralia::visit_store_load(path);
    check(astralia::visit_store_get(reloaded, "app:firefox.desktop") == 2, "app visits persist");
    check(astralia::visit_store_get(reloaded, "file:/home/u/notes.txt") == 1, "file visits persist");
    check(reloaded.counts.size() == 2, "empty keys are ignored");
    std::filesystem::remove_all(dir);
}

void check_submenu() {
    astralia::DirLister lister = [](const std::string &path, bool dirs) {
        return std::vector<astralia::FileEntry>{{dirs ? "sub" : "file", path + "/x", dirs, 0.0f}};
    };
    astralia::SubmenuState s;
    astralia::submenu_open_directory(s, "/a/b", lister);
    check(s.screen == astralia::SubmenuScreen::browse && s.items.size() == 4, "browse lists actions, dirs and files");
    check(s.items[0].icon == astralia::SubmenuIcon::arrow_right && s.items[1].icon == astralia::SubmenuIcon::arrow_left, "browse actions carry icons");
    check(s.items[2].icon == astralia::SubmenuIcon::none, "listed entries have no icon");
    astralia::submenu_handle_entry(s, s.items[1], lister);
    check(s.current_path == "/a", "previous directory goes up");
    astralia::submenu_open_directory(s, "/", lister);
    astralia::submenu_handle_entry(s, s.items[1], lister);
    check(s.current_path == "/", "previous directory stops at the root");
    astralia::submenu_open_directory(s, "/a/b", lister);
    astralia::submenu_handle_entry(s, s.items[2], lister);
    check(s.screen == astralia::SubmenuScreen::browse && s.current_path == "/a/b/x", "a directory opens");
    astralia::submenu_open_directory(s, "/a/b", lister);
    astralia::submenu_handle_entry(s, s.items[3], lister);
    check(s.screen == astralia::SubmenuScreen::file_actions && s.came_from_browse && s.items.size() == 2, "a file opens its actions");
    astralia::submenu_handle_entry(s, s.items[1], lister);
    check(s.screen == astralia::SubmenuScreen::dir_actions && s.items.size() == 3, "containing directory opens directory actions");
    astralia::submenu_go_back(s, lister);
    check(s.screen == astralia::SubmenuScreen::browse, "back from directory actions returns to browse");
    astralia::submenu_open_file_actions(s, "/a/x");
    astralia::submenu_go_back(s, lister);
    check(s.screen == astralia::SubmenuScreen::browse && s.current_path == "/a", "back from file actions returns to its directory");
    astralia::submenu_go_back(s, lister);
    check(s.screen == astralia::SubmenuScreen::search && s.items.empty(), "back from browse closes");
    check(!astralia::submenu_go_back(s, lister), "nothing to go back from search");
    astralia::submenu_open_file_actions(s, "/other/file.txt");
    check(!s.came_from_browse, "a file opened from search does not come from browse");
    astralia::submenu_go_back(s, lister);
    check(s.screen == astralia::SubmenuScreen::search, "back from search-opened file actions closes");
}
