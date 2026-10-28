#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <sys/inotify.h>
#include <unistd.h>

#include "core/log.h"
#include "core/spawn.h"

#include "service/brightness_service.h"

namespace astralia {

namespace {

constexpr const char *backlight_dir = "/sys/class/backlight";
constexpr int min_percent = 1;

int read_int(const std::filesystem::path &path) {
    std::ifstream file(path);
    int value = 0;
    file >> value;
    return value;
}

std::string trimmed(std::string_view text) {
    size_t a = text.find_first_not_of(" \t\r");
    size_t b = text.find_last_not_of(" \t\r");
    return a == std::string_view::npos ? std::string() : std::string(text.substr(a, b - a + 1));
}

std::string value_of(const std::string &line, std::string_view key) {
    return line.starts_with(key) ? trimmed(std::string_view(line).substr(key.size())) : "";
}

} // namespace

std::vector<DdcBus> parse_ddc_detect(std::string_view text) {
    std::vector<DdcBus> found;
    std::istringstream in{std::string(text)};
    std::string line;
    bool display = false;
    DdcBus current;
    auto flush = [&] {
        if (display && !current.output.empty() && !current.bus.empty()) {
            found.push_back(current);
        }
        current = {};
        display = false;
    };
    while (std::getline(in, line)) {
        std::string t = trimmed(line);
        if (t.empty()) {
            flush();
        } else if (line.starts_with("Display ")) {
            flush();
            display = true;
        } else if (std::string bus = value_of(t, "I2C bus:"); !bus.empty()) {
            size_t at = bus.rfind('-');
            current.bus = at == std::string::npos ? "" : bus.substr(at + 1);
        } else if (std::string conn = value_of(t, "DRM connector:"); !conn.empty()) {
            size_t at = conn.find('-');
            current.output = at == std::string::npos ? "" : conn.substr(at + 1);
        }
    }
    flush();
    return found;
}

std::optional<std::pair<int, int>> parse_ddc_brightness(std::string_view text) {
    std::istringstream in{std::string(text)};
    std::string vcp, code, type;
    int current = 0;
    int max = 0;
    if (!(in >> vcp >> code >> type >> current >> max) || vcp != "VCP" || code != "10" ||
        type != "C" || max <= 0) {
        return std::nullopt;
    }
    return std::make_pair(current, max);
}

BrightnessService::BrightnessService(Reactor &loop) : loop_(loop), detect_(loop), worker_(loop) {
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator(backlight_dir, error)) {
        device_ = entry.path().filename();
        max_ = read_int(entry.path() / "max_brightness");
        break;
    }
    detect_.start({"ddcutil", "detect", "--brief"}, [this](std::string text) { detected(text); });
    if (!available()) {
        log::info("brightness: no backlight device");
        return;
    }
    std::filesystem::path file = std::filesystem::path(backlight_dir) / device_ / "brightness";
    watch_ = UniqueFd(inotify_init1(IN_NONBLOCK | IN_CLOEXEC));
    if (watch_.get() < 0 || inotify_add_watch(watch_.get(), file.c_str(), IN_MODIFY) < 0) {
        log::error("brightness: cannot watch {}", file.string());
        watch_ = UniqueFd();
        return;
    }
    loop_.on_fd(watch_.get(), [this] {
        std::array<char, 256> buffer{};
        bool modified = false;
        while (read(watch_.get(), buffer.data(), buffer.size()) > 0) {
            modified = true;
        }
        if (modified) {
            changed.emit();
        }
    });
}

BrightnessService::~BrightnessService() {
    if (watch_.get() >= 0) {
        loop_.remove_fd(watch_.get());
    }
}

int BrightnessService::percent() const {
    if (!available()) {
        return 0;
    }
    int current = read_int(std::filesystem::path(backlight_dir) / device_ / "brightness");
    return static_cast<int>(std::lround(current * 100.0 / max_));
}

void BrightnessService::set(int percent) const {
    if (available()) {
        spawn_detached(std::format("brightnessctl -q -d {} set {}%", device_,
                                   std::clamp(percent, min_percent, 100)));
    }
}

BrightnessService::Display *BrightnessService::find(const std::string &output) {
    for (Display &display : displays_) {
        if (display.ddc.output == output && display.max > 0) {
            return &display;
        }
    }
    return nullptr;
}

const BrightnessService::Display *BrightnessService::find(const std::string &output) const {
    return const_cast<BrightnessService *>(this)->find(output);
}

bool BrightnessService::available(const std::string &output) const {
    return find(output) != nullptr || available();
}

int BrightnessService::percent(const std::string &output) const {
    if (const Display *display = find(output)) {
        return static_cast<int>(std::lround(display->value * 100.0 / display->max));
    }
    return percent();
}

void BrightnessService::set(const std::string &output, int percent) {
    Display *display = find(output);
    if (display == nullptr) {
        set(percent);
        return;
    }
    percent = std::clamp(percent, min_percent, 100);
    display->value = static_cast<int>(std::lround(percent * display->max / 100.0));
    queue({output, true, std::max(1, display->value)});
}

void BrightnessService::refresh(const std::string &output) {
    if (find(output) != nullptr) {
        queue({output, false, 0});
    }
}

void BrightnessService::queue(Job job) {
    std::erase_if(jobs_, [&](const Job &queued) {
        return queued.output == job.output && queued.write == job.write;
    });
    jobs_.push_back(std::move(job));
    next();
}

void BrightnessService::next() {
    if (worker_.running() || jobs_.empty()) {
        return;
    }
    Job job = jobs_.front();
    jobs_.pop_front();
    const Display *display = find(job.output);
    if (display == nullptr) {
        next();
        return;
    }
    std::vector<std::string> argv{"ddcutil", "-b", display->ddc.bus};
    if (job.write) {
        argv.insert(argv.end(), {"setvcp", "10", std::to_string(job.raw)});
    } else {
        argv.insert(argv.end(), {"getvcp", "10", "--brief"});
    }
    worker_.start(argv, [this, job](std::string text) {
        if (!job.write) {
            Display *display = find(job.output);
            if (std::optional<std::pair<int, int>> value = parse_ddc_brightness(text);
                display != nullptr && value) {
                bool moved = display->value != value->first || display->max != value->second;
                display->value = value->first;
                display->max = value->second;
                if (moved) {
                    changed.emit();
                }
            }
        }
        next();
    });
}

void BrightnessService::detected(const std::string &text) {
    for (const DdcBus &ddc : parse_ddc_detect(text)) {
        displays_.push_back({ddc, 0, 0});
        log::info("brightness: ddc display {} on i2c-{}", ddc.output, ddc.bus);
    }
    for (Display &display : displays_) {
        std::string output = display.ddc.output;
        display.max = 100;
        queue({output, false, 0});
    }
}

} // namespace astralia
