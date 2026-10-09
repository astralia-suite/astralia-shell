#include <string>

#include "service/telemetry_service.h"

#include "check.h"

void check_cpu_temp() {
    using namespace astralia;
    using test::check;
    check(cpu_temp_detail_is_cpu_hwmon_name("coretemp"), "cpu_temp_detail_is_cpu_hwmon_name(\"coretemp\")");
    check(cpu_temp_detail_is_cpu_hwmon_name("k10temp"), "cpu_temp_detail_is_cpu_hwmon_name(\"k10temp\")");
    check(cpu_temp_detail_is_cpu_hwmon_name("zenpower"), "cpu_temp_detail_is_cpu_hwmon_name(\"zenpower\")");
    check(!cpu_temp_detail_is_cpu_hwmon_name("amdgpu"), "!cpu_temp_detail_is_cpu_hwmon_name(\"amdgpu\")");
    check(!cpu_temp_detail_is_cpu_hwmon_name(""), "!cpu_temp_detail_is_cpu_hwmon_name(\"\")");

    check(cpu_temp_detail_is_cpu_thermal_zone_type("cpu-thermal"), "cpu_temp_detail_is_cpu_thermal_zone_type(\"cpu-thermal\")");
    check(cpu_temp_detail_is_cpu_thermal_zone_type("cpu"), "cpu_temp_detail_is_cpu_thermal_zone_type(\"cpu\")");
    check(!cpu_temp_detail_is_cpu_thermal_zone_type("gpu-thermal"), "!cpu_temp_detail_is_cpu_thermal_zone_type(\"gpu-thermal\")");
    check(!cpu_temp_detail_is_cpu_thermal_zone_type(""), "!cpu_temp_detail_is_cpu_thermal_zone_type(\"\")");

    CpuTempState state;
    check(!cpu_temp_available(state), "!cpu_temp_available(state)");
}

void check_gpu_temp() {
    using namespace astralia;
    using test::check;
    check(gpu_temp_detail_is_gpu_hwmon_name("amdgpu"), "gpu_temp_detail_is_gpu_hwmon_name(\"amdgpu\")");
    check(gpu_temp_detail_is_gpu_hwmon_name("i915"), "gpu_temp_detail_is_gpu_hwmon_name(\"i915\")");
    check(gpu_temp_detail_is_gpu_hwmon_name("xe"), "gpu_temp_detail_is_gpu_hwmon_name(\"xe\")");
    check(!gpu_temp_detail_is_gpu_hwmon_name("coretemp"), "!gpu_temp_detail_is_gpu_hwmon_name(\"coretemp\")");
    check(!gpu_temp_detail_is_gpu_hwmon_name(""), "!gpu_temp_detail_is_gpu_hwmon_name(\"\")");

    auto ok = gpu_temp_detail_parse_nvidia_smi_output("62\n");
    check(ok.has_value(), "ok.has_value()");
    check(*ok == 62.0f, "*ok == 62.0f");

    auto empty = gpu_temp_detail_parse_nvidia_smi_output("");
    check(!empty.has_value(), "!empty.has_value()");

    auto garbage = gpu_temp_detail_parse_nvidia_smi_output("not-a-number\n");
    check(!garbage.has_value(), "!garbage.has_value()");

    GpuTempState state;
    check(!gpu_temp_available(state), "!gpu_temp_available(state)");
    check(state.clock_ghz < 0.0f, "state.clock_ghz < 0.0f");
}

void check_system_stats() {
    using namespace astralia;
    using test::check;
    {
        std::string text = "cpu  100 0 100 800 0 0 0 0 0 0\n"
                           "cpu0 100 0 100 800 0 0 0 0 0 0\n";
        auto j = system_stats_detail_parse_proc_stat(text);
        check(j.has_value(), "j.has_value()");
        check(j->total == 1000, "j->total == 1000");
        check(j->idle == 800, "j->idle == 800");
    }
    check(!system_stats_detail_parse_proc_stat("").has_value(), "!system_stats_detail_parse_proc_stat(\"\").has_value()");
    check(!system_stats_detail_parse_proc_stat("notcpu 1 2 3\n").has_value(), "!system_stats_detail_parse_proc_stat(\"notcpu 1 2 3\\n\").has_value()");

    {
        CpuJiffies prev{800, 1000};
        CpuJiffies cur{850, 1100};
        float usage = system_stats_detail_cpu_usage(prev, cur);
        check(usage > 0.49f && usage < 0.51f, "usage > 0.49f && usage < 0.51f");
    }
    check(system_stats_detail_cpu_usage({800, 1000}, {800, 1000}) < 0.0f, "system_stats_detail_cpu_usage({800, 1000}, {800, 1000}) < 0.0f");

    {
        std::string text = "MemTotal:       16384000 kB\n"
                           "MemFree:         2000000 kB\n"
                           "MemAvailable:    8192000 kB\n";
        auto m = system_stats_detail_parse_proc_meminfo(text);
        check(m.has_value(), "m.has_value()");
        check(m->total_kb == 16384000, "m->total_kb == 16384000");
        check(m->available_kb == 8192000, "m->available_kb == 8192000");
        float usage = system_stats_detail_mem_usage(*m);
        check(usage > 0.49f && usage < 0.51f, "usage > 0.49f && usage < 0.51f");
    }
    check(!system_stats_detail_parse_proc_meminfo("garbage\n").has_value(), "!system_stats_detail_parse_proc_meminfo(\"garbage\\n\").has_value()");

    {
        std::string text = "processor : 0\ncpu MHz : 2400.000\n"
                           "processor : 1\ncpu MHz : 2600.000\n";
        auto freq = system_stats_detail_parse_cpu_freq_avg_mhz(text);
        check(freq.has_value(), "freq.has_value()");
        check(*freq > 2499.0f && *freq < 2501.0f, "*freq > 2499.0f && *freq < 2501.0f");
    }
    check(!system_stats_detail_parse_cpu_freq_avg_mhz("no such line\n").has_value(), "!system_stats_detail_parse_cpu_freq_avg_mhz(\"no such line\\n\").has_value()");

    {
        std::string text =
            "Inter-|   Receive                                                "
            "|  Transmit\n face |bytes packets errs drop fifo frame "
            "compressed multicast|bytes packets errs drop fifo colls "
            "carrier compressed\n"
            "    lo: 100 1 0 0 0 0 0 0 100 1 0 0 0 0 0 0\n"
            "  eth0: 1000 2 0 0 0 0 0 0 2000 3 0 0 0 0 0 0\n";
        auto net = system_stats_detail_parse_proc_net_dev(text);
        check(net.has_value(), "net.has_value()");
        check(net->rx_bytes == 1000, "net->rx_bytes == 1000");
        check(net->tx_bytes == 2000, "net->tx_bytes == 2000");
    }
    check(!system_stats_detail_parse_proc_net_dev("").has_value(), "!system_stats_detail_parse_proc_net_dev(\"\").has_value()");

    check(system_stats_detail_format_speed(512) == "0.5KiB", "system_stats_detail_format_speed(512) == \"0.5KiB\"");
    check(system_stats_detail_format_speed(15.0 * 1024) == "15KiB", "system_stats_detail_format_speed(15.0 * 1024) == \"15KiB\"");
    check(system_stats_detail_format_speed(2.5 * 1024 * 1024) == "2.5MiB", "system_stats_detail_format_speed(2.5 * 1024 * 1024) == \"2.5MiB\"");

    auto disk = system_stats_detail_disk_usage("/");
    check(disk.has_value(), "disk.has_value()");
    check(disk->total_bytes > 0, "disk->total_bytes > 0");
    check(disk->used_bytes <= disk->total_bytes, "disk->used_bytes <= disk->total_bytes");
    check(!system_stats_detail_disk_usage("/does/not/exist").has_value(), "!system_stats_detail_disk_usage(\"/does/not/exist\").has_value()");

    SystemStatsState state;
    check(!state.have_prev, "!state.have_prev");
    check(!state.have_prev_net, "!state.have_prev_net");
}
