#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "core/async_process.h"
#include "core/reactor.h"

namespace astralia {

namespace detail {
bool cpu_is_hwmon_name(const std::string &name);
} // namespace detail

namespace detail {
bool cpu_is_thermal_zone_type(const std::string &type);
} // namespace detail

struct CpuTempState {
    std::string sensor_path;
    float celsius = -1.0f;
};

void cpu_temp_init(CpuTempState &state);

void cpu_temp_poll(CpuTempState &state);

bool cpu_temp_available(const CpuTempState &state);

namespace detail {
bool gpu_is_hwmon_name(const std::string &name);
} // namespace detail

namespace detail {
std::optional<float> parse_nvidia_smi_output(const std::string &text);
} // namespace detail

struct GpuTempState {
    std::string sensor_path;
    std::string usage_sensor_path;
    std::string clock_sensor_path;
    bool nvidia_smi_present = false;
    std::unique_ptr<AsyncProcess> nvidia_smi_proc;
    float celsius = -1.0f;
    float usage_percent = -1.0f;
    float clock_ghz = -1.0f;
};

void gpu_temp_init(GpuTempState &state, Reactor &reactor);

void gpu_temp_poll(GpuTempState &state);

bool gpu_temp_available(const GpuTempState &state);

bool gpu_stats_available(const GpuTempState &state);

struct CpuJiffies {
    uint64_t idle = 0;
    uint64_t total = 0;
};

namespace detail {
std::optional<CpuJiffies> parse_proc_stat(const std::string &text);
} // namespace detail

namespace detail {
float cpu_usage(const CpuJiffies &prev, const CpuJiffies &cur);
} // namespace detail

struct MemInfo {
    uint64_t total_kb = 0;
    uint64_t available_kb = 0;
};

namespace detail {
std::optional<MemInfo> parse_proc_meminfo(const std::string &text);
} // namespace detail

namespace detail {
float mem_usage(const MemInfo &info);
} // namespace detail

namespace detail {
std::optional<float> parse_cpu_freq_avg_mhz(const std::string &cpuinfo_text);
} // namespace detail

struct DiskUsage {
    uint64_t used_bytes = 0;
    uint64_t total_bytes = 0;
};

namespace detail {
std::optional<DiskUsage> disk_usage(const char *path);
} // namespace detail

struct NetBytes {
    uint64_t rx_bytes = 0;
    uint64_t tx_bytes = 0;
};

namespace detail {
std::optional<NetBytes> parse_proc_net_dev(const std::string &text);
} // namespace detail

namespace detail {
std::string format_speed(double bytes_per_sec);
} // namespace detail

struct SystemStatsState {
    CpuJiffies prev_jiffies;
    bool have_prev = false;
    float cpu_usage = -1.0f;
    float mem_usage = -1.0f;
    float mem_used_gb = -1.0f;
    float mem_total_gb = -1.0f;
    float cpu_freq_ghz = -1.0f;
    float disk_used_gb = -1.0f;
    float disk_total_gb = -1.0f;
    float disk_pct = -1.0f;
    NetBytes prev_net;
    std::chrono::steady_clock::time_point prev_net_time;
    bool have_prev_net = false;
    double net_rx_bps = -1.0;
    double net_tx_bps = -1.0;
};

void system_stats_poll(SystemStatsState &state);

} // namespace astralia
