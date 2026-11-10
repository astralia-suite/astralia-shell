#include <cstdlib>

#include "app/backend.h"
#include "app/services.h"
#include "app/session.h"
#include "app/shell.h"

#include "core/animation.h"
#include "core/deferred_call.h"
#include "core/event_loop.h"
#include "core/ipc.h"
#include "core/log.h"
#include "core/runtime_paths.h"
#include "core/x_connection.h"

#include "modules/bamboo/x11/bamboo.h"
#include "modules/bar/x11/bar_set.h"
#include "modules/dashboard/x11/dashboard.h"
#include "modules/idle/x11/idle.h"
#include "modules/launcher/x11/launcher.h"
#include "modules/lock/x11/lock.h"
#include "modules/logout/x11/logout.h"
#include "modules/notification/x11/notification.h"
#include "modules/osd/x11/osd.h"
#include "modules/overview/x11/overview.h"
#include "modules/polkit/x11/polkit.h"
#include "modules/rain/x11/rain.h"
#include "modules/settings/x11/settings.h"
#include "modules/visualizer/x11/visualizer.h"
#include "modules/wallpaper/x11/wallpaper.h"
#include "modules/wallpaper_select/x11/wallpaper_select.h"

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
        astralia::wallpaper::Host wallpaper(*x, *loop, services);
        astralia::bar::HostSet bars(*x, *loop, shell, services);
        astralia::launcher::Host launcher(*x, *loop, shell, services);
        astralia::lock::Host lock(*x, *loop, shell, services);
        astralia::logout::Host logout(*x, *loop, shell, services);
        astralia::overview::Host overview(*x, *loop, shell, services);
        astralia::polkit::Host polkit(*x, *loop, shell, services);
        astralia::notification::Host notifications(*x, *loop, services);
        astralia::osd::Host osd(*x, *loop, services);
        astralia::settings::Host settings(*x, *loop, shell, services);
        astralia::wallpaper_select::Host wallpaper_select(*x, *loop, shell, services);
        wallpaper.on_settings = [&shell] { shell.run(ShellVerb::settings); };
        wallpaper.on_wallpaper = [&wallpaper_select](const std::string &monitor, int column) {
            wallpaper_select.open_on(monitor, column);
        };
        astralia::dashboard::Host dashboard(*x, *loop, shell, services);
        astralia::rain::Host rain(*x, *loop, shell, services);
        astralia::bamboo::Host bamboo(*x, *loop, shell, services);
        astralia::idle::Host idle(*x, *loop, services);
        astralia::visualizer::Host visualizer(*x, *loop, shell, services);
        shell.attach(**ipc);
        if (start_locked_pending()) {
            shell.run(ShellVerb::lock);
            if (lock.locked()) {
                start_locked_ack();
            }
        }
        return loop->run();
    }
};

} // namespace

} // namespace astralia

extern "C" __attribute__((visibility("default"))) astralia::Backend *astralia_backend_create() {
    return new astralia::X11Backend();
}
