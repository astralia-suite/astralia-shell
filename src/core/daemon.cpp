#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include "core/daemon.h"
#include "core/log.h"

namespace astralia {

namespace {

void redirect(int target, const char *path, int flags) {
    int fd = open(path, flags | O_CLOEXEC, 0600);
    if (fd < 0) {
        fd = open("/dev/null", O_RDWR | O_CLOEXEC);
    }
    if (fd >= 0) {
        dup2(fd, target);
        close(fd);
    }
}

} // namespace

void daemonize() {
    std::fflush(nullptr);
    pid_t pid = fork();
    if (pid < 0) {
        log::error("fork: {}", std::strerror(errno));
        std::exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        _exit(EXIT_SUCCESS);
    }
    setsid();
    if (chdir("/") != 0) {
        log::error("chdir /: {}", std::strerror(errno));
    }
    redirect(STDIN_FILENO, "/dev/null", O_RDONLY);
    redirect(STDOUT_FILENO, "/dev/null", O_WRONLY);
    redirect(STDERR_FILENO, "/dev/null", O_WRONLY);
}

} // namespace astralia
