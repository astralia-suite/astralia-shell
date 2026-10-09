#include <cstdlib>

#include "app/backend.h"
#include "app/services.h"
#include "app/shell.h"

#include "core/animation.h"
#include "core/deferred_call.h"
#include "core/event_loop.h"
#include "core/ipc.h"
#include "core/log.h"
#include "core/runtime_paths.h"
#include "core/x_connection.h"

#include "modules/bar/x11/bar_set.h"
#include "modules/launcher/x11/launcher.h"
#include "modules/logout/x11/logout.h"
#include "modules/notification/x11/notification.h"
#include "modules/osd/x11/osd.h"
#include "modules/overview/x11/overview.h"
#include "modules/polkit/x11/polkit.h"
#include "modules/settings/x11/settings.h"
#include "modules/wallpaper/x11/wallpaper.h"

namespace astralia {

namespace {

class X11Backend final : public Backend {
  public:
    const char *name() const override { return "x11"; }
    int malloc_arenas() const override { return 1; }

    int run() override {
        animation_set_instant(true);
        auto x = XConnection::connect();
        if (!x) {
            log::error("{}", x.error());
            return EXIT_FAILURE;
        }
        auto loop = EventLoop::create(*x);
        if (!loop) {
            log::error("{}", loop.error());
            return EXIT_FAILURE;
        }
        DeferredCall::attach(*loop);
        auto ipc = IpcServer::create(*loop, runtime_path(".sock"));
        if (!ipc) {
            log::error("{}", ipc.error());
            return EXIT_FAILURE;
        }
        Shell shell("x11", x11_capabilities());
        Services services(*x, *loop);
        Wallpaper wallpaper(*x, services);
        BarSet bars(*x, *loop, shell, services);
        Launcher launcher(*x, *loop, shell);
        Logout logout(*x, *loop, shell);
        Overview overview(*x, *loop, shell, services);
        Polkit polkit(*x, *loop, services);
        Notifications notifications(*x, *loop, services);
        Osd osd(*x, *loop, services);
        Settings settings(*x, *loop, shell, services);
        shell.attach(**ipc);
        return loop->run();
    }
};

} // namespace

} // namespace astralia

extern "C" __attribute__((visibility("default"))) astralia::Backend *astralia_backend_create() { return new astralia::X11Backend(); }
