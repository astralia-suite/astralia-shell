#include <cstdlib>

#include "core/path_home.h"

#include "check.h"

void check_path_home() {
    using astralia::path_collapse_home;
    using astralia::path_expand_home;
    using test::check;
    setenv("HOME", "/home/tester", 1);

    check(path_collapse_home("/home/tester") == "~", "home collapses to tilde");
    check(path_collapse_home("/home/tester/Pictures") == "~/Pictures", "child collapses");
    check(path_collapse_home("/etc/passwd") == "/etc/passwd", "outside home is kept");
    check(path_collapse_home("").empty(), "empty stays empty");
    check(path_collapse_home("/home/testerwork") == "/home/testerwork", "sibling prefix is kept");

    check(path_expand_home("~") == "/home/tester", "tilde expands");
    check(path_expand_home("~/Videos") == "/home/tester/Videos", "tilde child expands");
    check(path_expand_home("/usr/share") == "/usr/share", "absolute is kept");
    check(path_expand_home("~foo") == "~foo", "tilde user is kept");
    check(path_expand_home("").empty(), "empty expands to empty");

    check(path_expand_home(path_collapse_home("/home/tester/a/b")) == "/home/tester/a/b", "expand inverts collapse");
    check(path_collapse_home(path_expand_home("~/a/b")) == "~/a/b", "collapse inverts expand");
}
