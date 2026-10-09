#include <string>
#include <vector>

#include "core/config_file.h"

#include "check.h"

void check_config_file() {
    using astralia::config_file_path;
    using test::check;
    check(config_file_path("/cfg", "/home/u", "astralia/wallpaper.conf") ==
              "/cfg/astralia/wallpaper.conf",
          "XDG_CONFIG_HOME wins");
    check(config_file_path("", "/home/u", "astralia/wallpaper.conf") ==
              "/home/u/.config/astralia/wallpaper.conf",
          "empty XDG_CONFIG_HOME falls back to ~/.config");
    check(astralia::expand_home("~/a.png", "/home/u") == "/home/u/a.png", "~/ expands");
    check(astralia::expand_home("/a/~b.png", "/home/u") == "/a/~b.png", "inner ~ is kept");

    astralia::ConfigLines lines = astralia::parse_config_lines("# c\n a = 1 \nbad\n=x\nb=\n");
    check(lines.entries.size() == 1 && lines.entries[0].key == "a" && lines.entries[0].value == "1" &&
              lines.entries[0].line == 2,
          "entries are trimmed and numbered");
    check(lines.invalid == std::vector<std::size_t>{3, 4, 5}, "bad lines are reported");

    std::string text = "# keep\na = 1\nother = 2\n";
    check(astralia::with_entry(text, "a", "9") == "# keep\na = 9\nother = 2\n",
          "an existing key is replaced in place");
    check(astralia::with_entry(text, "c", "3") == "# keep\na = 1\nother = 2\nc = 3\n",
          "a new key is appended");
    check(astralia::with_entry("", "a", "1") == "a = 1\n", "an empty file gains the key");
    check(astralia::without_entry(text, "a") == "# keep\nother = 2\n", "a key is removed");
    check(astralia::without_entry(text, "zzz") == text, "a missing key changes nothing");
}
