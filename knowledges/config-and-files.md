# Config and files

Reading, writing and watching files the shell owns. Sources: `src/core/config_file.cpp`, `src/core/path_home.cpp`, `src/service/settings_service.cpp`.

- Compare a config file the shell writes after parsing, before emitting. The shell's own atomic write trips its `inotify` watch.
- Watch a config file's directory with `inotify`, filtered by name. Editors save by rename, which drops a watch on the file.
- A file writer must create its own target directory. A fresh install otherwise fails the first save silently.
- Renaming a persisted config value needs a load-time mapping. Users otherwise silently lose the setting.
- `~` is a display convention, never a real path. `std::filesystem` never expands it, so expand before use.
- Config path defaults must already be absolute. A default returned unexpanded is scanned directly and finds nothing.
- Give every bundled asset an installed-path-plus-source-tree fallback. A bare relative path resolves against the daemon's working directory.
