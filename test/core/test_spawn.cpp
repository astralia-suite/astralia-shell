#include <cerrno>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "check.h"

#include "core/spawn.h"

using test::check;

void check_spawn_helpers() {
    std::string marker = "/tmp/astralia_shell_test_spawn_" + std::to_string(getpid());
    astralia::spawn_detached("touch " + marker);
    bool created = false;
    for (int i = 0; i < 50 && !created; ++i) {
        if (access(marker.c_str(), F_OK) == 0) {
            created = true;
        } else {
            usleep(20000);
        }
    }
    check(created, "spawn_detached runs the command");
    unlink(marker.c_str());
    for (int i = 0; i < 5; ++i) {
        astralia::spawn_detached("true");
    }
    usleep(50000);
    errno = 0;
    check(waitpid(-1, nullptr, WNOHANG) == -1 && errno == ECHILD, "spawn_detached leaves no children to reap");
}
