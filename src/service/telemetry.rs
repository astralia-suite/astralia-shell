use std::cell::Cell;
use std::ffi::CString;
use std::path::{Path, PathBuf};
use std::rc::Rc;
use std::time::Instant;

use crate::base::process::AsyncProcess;
use crate::service::runtime;

const VIRTUAL_IFACE_PREFIXES: [&str; 11] = ["lo", "docker", "veth", "br-", "virbr", "vnet", "tun", "tap", "wg", "tailscale", "nordlynx"];

pub(crate) fn cpu_is_hwmon_name(name: &str) -> bool {
    matches!(name, "coretemp" | "k10temp" | "zenpower")
}

pub(crate) fn cpu_is_thermal_zone_type(kind: &str) -> bool {
    kind.starts_with("cpu")
}

pub(crate) fn gpu_is_hwmon_name(name: &str) -> bool {
    matches!(name, "amdgpu" | "i915" | "xe")
}

/// The longest leading number of `text` after leading whitespace, as `strtod` reads it.
fn leading_number<T: std::str::FromStr>(text: &str) -> Option<T> {
    let text = text.trim_start();
    let end = text.bytes().take_while(|b| b.is_ascii_digit() || matches!(b, b'.' | b'-' | b'+' | b'e' | b'E')).count();
    (1..=end).rev().find_map(|i| text[..i].parse().ok())
}

pub(crate) fn parse_nvidia_smi_output(text: &str) -> Option<f32> {
    let first = text.lines().next()?.trim_end_matches(['\r', ' ']);
    if first.is_empty() { None } else { leading_number(first) }
}

fn read_first_line(path: &Path) -> String {
    std::fs::read_to_string(path).ok().and_then(|t| t.lines().next().map(|l| l.trim_end_matches('\r').to_string())).unwrap_or_default()
}

fn read_long(path: &Path) -> Option<i64> {
    std::fs::read_to_string(path).ok().and_then(|t| leading_number(&t))
}

fn find_hwmon(matches: fn(&str) -> bool) -> Option<PathBuf> {
    std::fs::read_dir("/sys/class/hwmon").ok()?.flatten().map(|e| e.path()).find(|p| matches(&read_first_line(&p.join("name")))).map(|p| p.join("temp1_input"))
}

fn find_thermal_zone_sensor() -> Option<PathBuf> {
    std::fs::read_dir("/sys/class/thermal")
        .ok()?
        .flatten()
        .map(|e| e.path())
        .filter(|p| p.file_name().is_some_and(|n| n.to_string_lossy().starts_with("thermal_zone")))
        .find(|p| cpu_is_thermal_zone_type(&read_first_line(&p.join("type"))))
        .map(|p| p.join("temp"))
}

#[derive(Debug)]
pub struct CpuTempState {
    pub sensor_path: Option<PathBuf>,
    pub celsius: f32,
}

impl Default for CpuTempState {
    fn default() -> Self {
        CpuTempState { sensor_path: None, celsius: -1.0 }
    }
}

pub fn cpu_temp_init(state: &mut CpuTempState) {
    state.sensor_path = find_hwmon(cpu_is_hwmon_name).or_else(find_thermal_zone_sensor);
}

pub fn cpu_temp_poll(state: &mut CpuTempState) {
    state.celsius = state.sensor_path.as_deref().and_then(read_long).map_or(-1.0, |m| m as f32 / 1000.0);
}

pub fn cpu_temp_available(state: &CpuTempState) -> bool {
    state.sensor_path.is_some() && state.celsius >= 0.0
}

/// Temperature, usage and clock from the last `nvidia-smi` run.
type NvidiaReading = (f32, f32, f32);

pub struct GpuTempState {
    pub sensor_path: Option<PathBuf>,
    pub usage_sensor_path: Option<PathBuf>,
    pub clock_sensor_path: Option<PathBuf>,
    pub nvidia_smi_present: bool,
    pub nvidia_smi_proc: AsyncProcess,
    /// Written by the `nvidia-smi` task, applied by the next poll.
    nvidia_reading: Rc<Cell<Option<NvidiaReading>>>,
    pub celsius: f32,
    pub usage_percent: f32,
    pub clock_ghz: f32,
}

impl Default for GpuTempState {
    fn default() -> Self {
        GpuTempState {
            sensor_path: None,
            usage_sensor_path: None,
            clock_sensor_path: None,
            nvidia_smi_present: false,
            nvidia_smi_proc: AsyncProcess::default(),
            nvidia_reading: Rc::default(),
            celsius: -1.0,
            usage_percent: -1.0,
            clock_ghz: -1.0,
        }
    }
}

fn nvidia_smi_on_path() -> bool {
    std::env::var("PATH").unwrap_or_default().split(':').filter(|d| !d.is_empty()).any(|dir| {
        // SAFETY: NUL-terminated path for `access`.
        CString::new(format!("{dir}/nvidia-smi")).is_ok_and(|p| unsafe { libc::access(p.as_ptr(), libc::X_OK) } == 0)
    })
}

fn find_gpu_usage_sensor(temp_hwmon: &Path) -> Option<PathBuf> {
    Some(temp_hwmon.parent()?.join("device/gpu_busy_percent")).filter(|p| p.exists())
}

fn find_drm_act_freq(drm_dir: &Path) -> Option<PathBuf> {
    std::fs::read_dir(drm_dir).ok()?.flatten().map(|e| e.path().join("gt_act_freq_mhz")).find(|p| p.exists())
}

fn find_gpu_clock_sensor(temp_hwmon: Option<&Path>) -> Option<PathBuf> {
    if let Some(hwmon_dir) = temp_hwmon.and_then(Path::parent) {
        let freq = hwmon_dir.join("freq1_input");
        if freq.exists() {
            return Some(freq);
        }
        if let Some(act) = find_drm_act_freq(&hwmon_dir.join("device/drm")) {
            return Some(act);
        }
    }
    find_drm_act_freq(Path::new("/sys/class/drm"))
}

fn read_gpu_clock_ghz(path: &Path) -> f32 {
    match read_long(path) {
        Some(v) if path.to_string_lossy().ends_with("_mhz") => v as f32 / 1000.0,
        Some(v) => v as f32 / 1e9,
        None => -1.0,
    }
}

pub fn gpu_temp_init(state: &mut GpuTempState) {
    state.sensor_path = find_hwmon(gpu_is_hwmon_name);
    match &state.sensor_path {
        Some(sensor) => state.usage_sensor_path = find_gpu_usage_sensor(sensor),
        None => state.nvidia_smi_present = nvidia_smi_on_path(),
    }
    if !state.nvidia_smi_present {
        state.clock_sensor_path = find_gpu_clock_sensor(state.sensor_path.as_deref());
    }
}

pub fn gpu_temp_poll(state: &mut GpuTempState) {
    if let Some(clock) = &state.clock_sensor_path {
        state.clock_ghz = read_gpu_clock_ghz(clock);
    }
    if let Some(sensor) = &state.sensor_path {
        state.celsius = read_long(sensor).map_or(-1.0, |m| m as f32 / 1000.0);
        if let Some(usage) = &state.usage_sensor_path {
            state.usage_percent = read_long(usage).map_or(-1.0, |p| p as f32);
        }
        return;
    }
    if let Some((celsius, usage, clock)) = state.nvidia_reading.take() {
        (state.celsius, state.usage_percent, state.clock_ghz) = (celsius, usage, clock);
    }
    if !state.nvidia_smi_present || state.nvidia_smi_proc.running() {
        return;
    }
    let run = state.nvidia_smi_proc.start(&["nvidia-smi", "--query-gpu=temperature.gpu,utilization.gpu,clocks.gr", "--format=csv,noheader,nounits"], false);
    let reading = state.nvidia_reading.clone();
    runtime::spawn(async move {
        let Some(output) = run.await else { return };
        let mut fields = output.split(',');
        let mut next = || parse_nvidia_smi_output(fields.next().unwrap_or(""));
        let (celsius, usage, clock) = (next(), next(), next());
        reading.set(Some((celsius.unwrap_or(-1.0), usage.unwrap_or(-1.0), clock.map_or(-1.0, |mhz| mhz / 1000.0))));
    });
}

pub fn gpu_temp_available(state: &GpuTempState) -> bool {
    state.celsius >= 0.0
}

pub fn gpu_stats_available(state: &GpuTempState) -> bool {
    state.celsius >= 0.0 && state.usage_percent >= 0.0
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct CpuJiffies {
    pub idle: u64,
    pub total: u64,
}

pub(crate) fn parse_proc_stat(text: &str) -> Option<CpuJiffies> {
    let mut words = text.lines().next()?.split_whitespace();
    if words.next()? != "cpu" {
        return None;
    }
    let values: Vec<u64> = words.map_while(|w| w.parse().ok()).collect();
    if values.is_empty() {
        return None;
    }
    // Fields 3 and 4 are idle and iowait.
    Some(CpuJiffies { idle: values.iter().skip(3).take(2).sum(), total: values.iter().sum() })
}

pub(crate) fn cpu_usage(prev: CpuJiffies, cur: CpuJiffies) -> f32 {
    if cur.total <= prev.total {
        return -1.0;
    }
    let total = cur.total - prev.total;
    let idle = cur.idle.wrapping_sub(prev.idle);
    if idle > total {
        return -1.0;
    }
    (total - idle) as f32 / total as f32
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct MemInfo {
    pub total_kb: u64,
    pub available_kb: u64,
}

pub(crate) fn parse_proc_meminfo(text: &str) -> Option<MemInfo> {
    let (mut total, mut available) = (None, None);
    for line in text.lines() {
        let mut words = line.split_whitespace();
        let (key, value) = (words.next(), words.next().and_then(|v| v.parse::<u64>().ok()).unwrap_or(0));
        match key {
            Some("MemTotal:") => total = Some(value),
            Some("MemAvailable:") => available = Some(value),
            _ => {}
        }
    }
    Some(MemInfo { total_kb: total?, available_kb: available? })
}

pub(crate) fn mem_usage(info: &MemInfo) -> f32 {
    if info.total_kb == 0 || info.available_kb > info.total_kb {
        return -1.0;
    }
    (info.total_kb - info.available_kb) as f32 / info.total_kb as f32
}

pub(crate) fn parse_cpu_freq_avg_mhz(cpuinfo: &str) -> Option<f32> {
    let values: Vec<f64> = cpuinfo
        .lines()
        .filter(|l| l.starts_with("cpu MHz"))
        .filter_map(|l| l.split_once(':'))
        .filter_map(|(_, v)| leading_number::<f64>(v))
        .collect();
    if values.is_empty() { None } else { Some((values.iter().sum::<f64>() / values.len() as f64) as f32) }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct DiskUsage {
    pub used_bytes: u64,
    pub total_bytes: u64,
}

pub(crate) fn disk_usage(path: &str) -> Option<DiskUsage> {
    let path = CString::new(path).ok()?;
    // SAFETY: statvfs fills the zeroed struct on success.
    let vfs = unsafe {
        let mut vfs: libc::statvfs = std::mem::zeroed();
        if libc::statvfs(path.as_ptr(), &mut vfs) != 0 {
            return None;
        }
        vfs
    };
    let total = vfs.f_blocks * vfs.f_frsize;
    let free = vfs.f_bfree * vfs.f_frsize;
    if total == 0 || free > total {
        return None;
    }
    Some(DiskUsage { used_bytes: total - free, total_bytes: total })
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct NetBytes {
    pub rx_bytes: u64,
    pub tx_bytes: u64,
}

/// Received and sent bytes over every physical interface.
pub(crate) fn parse_proc_net_dev(text: &str) -> Option<NetBytes> {
    let mut total = NetBytes::default();
    let mut any = false;
    for line in text.lines().skip(2) {
        let Some((iface, rest)) = line.split_once(':') else { continue };
        let iface = iface.trim_start_matches([' ', '\t']);
        if iface.is_empty() || VIRTUAL_IFACE_PREFIXES.iter().any(|p| iface.starts_with(p)) {
            continue;
        }
        let values: Vec<u64> = rest.split_whitespace().map_while(|v| v.parse().ok()).take(16).collect();
        if values.len() < 9 {
            continue;
        }
        total.rx_bytes += values[0];
        total.tx_bytes += values[8];
        any = true;
    }
    any.then_some(total)
}

pub(crate) fn format_speed(bytes_per_sec: f64) -> String {
    const UNITS: [&str; 3] = ["KiB", "MiB", "GiB"];
    let mut v = bytes_per_sec / 1024.0;
    let mut i = 0;
    while v >= 1024.0 && i < 2 {
        v /= 1024.0;
        i += 1;
    }
    let v = v.max(0.0);
    if v < 10.0 { format!("{v:.1}{}", UNITS[i]) } else { format!("{v:.0}{}", UNITS[i]) }
}

#[derive(Debug)]
pub struct SystemStatsState {
    pub prev_jiffies: CpuJiffies,
    pub have_prev: bool,
    pub cpu_usage: f32,
    pub mem_usage: f32,
    pub mem_used_gb: f32,
    pub mem_total_gb: f32,
    pub cpu_freq_ghz: f32,
    pub disk_used_gb: f32,
    pub disk_total_gb: f32,
    pub disk_pct: f32,
    pub prev_net: NetBytes,
    pub prev_net_time: Option<Instant>,
    pub have_prev_net: bool,
    pub net_rx_bps: f64,
    pub net_tx_bps: f64,
}

impl Default for SystemStatsState {
    fn default() -> Self {
        SystemStatsState {
            prev_jiffies: CpuJiffies::default(),
            have_prev: false,
            cpu_usage: -1.0,
            mem_usage: -1.0,
            mem_used_gb: -1.0,
            mem_total_gb: -1.0,
            cpu_freq_ghz: -1.0,
            disk_used_gb: -1.0,
            disk_total_gb: -1.0,
            disk_pct: -1.0,
            prev_net: NetBytes::default(),
            prev_net_time: None,
            have_prev_net: false,
            net_rx_bps: -1.0,
            net_tx_bps: -1.0,
        }
    }
}

fn read_proc(path: &str) -> String {
    std::fs::read_to_string(path).unwrap_or_default()
}

pub fn system_stats_poll(state: &mut SystemStatsState) {
    match parse_proc_stat(&read_proc("/proc/stat")) {
        Some(stat) => {
            if state.have_prev {
                state.cpu_usage = cpu_usage(state.prev_jiffies, stat);
            }
            state.prev_jiffies = stat;
            state.have_prev = true;
        }
        None => state.cpu_usage = -1.0,
    }

    match parse_proc_meminfo(&read_proc("/proc/meminfo")) {
        Some(mem) => {
            state.mem_usage = mem_usage(&mem);
            state.mem_used_gb = mem.total_kb.saturating_sub(mem.available_kb) as f32 / 1048576.0;
            state.mem_total_gb = mem.total_kb as f32 / 1048576.0;
        }
        None => (state.mem_usage, state.mem_used_gb, state.mem_total_gb) = (-1.0, -1.0, -1.0),
    }

    state.cpu_freq_ghz = parse_cpu_freq_avg_mhz(&read_proc("/proc/cpuinfo")).map_or(-1.0, |mhz| mhz / 1000.0);

    match disk_usage("/") {
        Some(disk) => {
            state.disk_used_gb = disk.used_bytes as f32 / 1073741824.0;
            state.disk_total_gb = disk.total_bytes as f32 / 1073741824.0;
            state.disk_pct = disk.used_bytes as f32 * 100.0 / disk.total_bytes as f32;
        }
        None => (state.disk_used_gb, state.disk_total_gb, state.disk_pct) = (-1.0, -1.0, -1.0),
    }

    let now = Instant::now();
    match parse_proc_net_dev(&read_proc("/proc/net/dev")) {
        Some(net) => {
            if let Some(prev_time) = state.prev_net_time.filter(|_| state.have_prev_net) {
                let dt = now.saturating_duration_since(prev_time).as_secs_f64();
                if dt > 0.0 {
                    let delta = |cur: u64, prev: u64| (cur.wrapping_sub(prev) as i64).max(0) as f64 / dt;
                    state.net_rx_bps = delta(net.rx_bytes, state.prev_net.rx_bytes);
                    state.net_tx_bps = delta(net.tx_bytes, state.prev_net.tx_bytes);
                }
            }
            state.prev_net = net;
            state.prev_net_time = Some(now);
            state.have_prev_net = true;
        }
        None => (state.net_rx_bps, state.net_tx_bps) = (-1.0, -1.0),
    }
}

/// The sensors and counters the resource panel polls.
#[derive(Default)]
pub struct Telemetry {
    pub cpu: CpuTempState,
    pub gpu: GpuTempState,
    pub stats: SystemStatsState,
}

impl Telemetry {
    pub fn poll(&mut self) {
        cpu_temp_poll(&mut self.cpu);
        gpu_temp_poll(&mut self.gpu);
        system_stats_poll(&mut self.stats);
    }
}

/// Finds the CPU and GPU sensors once; readers poll them.
pub fn start() -> Telemetry {
    let mut cpu = CpuTempState::default();
    let mut gpu = GpuTempState::default();
    cpu_temp_init(&mut cpu);
    gpu_temp_init(&mut gpu);
    let describe = |p: &Option<PathBuf>, fallback: &str| p.as_ref().map_or(fallback.to_string(), |p| p.display().to_string());
    let gpu_sensor = describe(&gpu.sensor_path, if gpu.nvidia_smi_present { "nvidia-smi" } else { "none" });
    ::log::info!("telemetry: cpu sensor {}, gpu sensor {gpu_sensor}", describe(&cpu.sensor_path, "none"));
    Telemetry { cpu, gpu, stats: SystemStatsState::default() }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cpu_temp() {
        assert!(cpu_is_hwmon_name("coretemp") && cpu_is_hwmon_name("k10temp") && cpu_is_hwmon_name("zenpower"));
        assert!(!cpu_is_hwmon_name("amdgpu") && !cpu_is_hwmon_name(""));
        assert!(cpu_is_thermal_zone_type("cpu-thermal") && cpu_is_thermal_zone_type("cpu"));
        assert!(!cpu_is_thermal_zone_type("gpu-thermal") && !cpu_is_thermal_zone_type(""));
        assert!(!cpu_temp_available(&CpuTempState::default()));
    }

    #[test]
    fn gpu_temp() {
        assert!(gpu_is_hwmon_name("amdgpu") && gpu_is_hwmon_name("i915") && gpu_is_hwmon_name("xe"));
        assert!(!gpu_is_hwmon_name("coretemp") && !gpu_is_hwmon_name(""));
        assert_eq!(parse_nvidia_smi_output("62\n"), Some(62.0));
        assert_eq!(parse_nvidia_smi_output(" 45 "), Some(45.0), "csv fields carry a leading space");
        assert_eq!(parse_nvidia_smi_output(""), None);
        assert_eq!(parse_nvidia_smi_output("not-a-number\n"), None);
        let state = GpuTempState::default();
        assert!(!gpu_temp_available(&state));
        assert!(state.clock_ghz < 0.0);
    }

    #[test]
    fn system_stats() {
        let j = parse_proc_stat("cpu  100 0 100 800 0 0 0 0 0 0\ncpu0 100 0 100 800 0 0 0 0 0 0\n").unwrap();
        assert!(j.total == 1000 && j.idle == 800);
        assert!(parse_proc_stat("").is_none() && parse_proc_stat("notcpu 1 2 3\n").is_none());
        let usage = cpu_usage(CpuJiffies { idle: 800, total: 1000 }, CpuJiffies { idle: 850, total: 1100 });
        assert!(usage > 0.49 && usage < 0.51);
        assert!(cpu_usage(CpuJiffies { idle: 800, total: 1000 }, CpuJiffies { idle: 800, total: 1000 }) < 0.0);

        let m = parse_proc_meminfo("MemTotal:       16384000 kB\nMemFree:         2000000 kB\nMemAvailable:    8192000 kB\n").unwrap();
        assert!(m.total_kb == 16384000 && m.available_kb == 8192000);
        let usage = mem_usage(&m);
        assert!(usage > 0.49 && usage < 0.51);
        assert!(parse_proc_meminfo("garbage\n").is_none());

        let freq = parse_cpu_freq_avg_mhz("processor : 0\ncpu MHz : 2400.000\nprocessor : 1\ncpu MHz : 2600.000\n").unwrap();
        assert!(freq > 2499.0 && freq < 2501.0);
        assert!(parse_cpu_freq_avg_mhz("no such line\n").is_none());

        let net = parse_proc_net_dev(
            "Inter-|   Receive                                                |  Transmit\n face |bytes packets errs drop fifo frame compressed multicast|bytes packets errs drop fifo colls carrier compressed\n    lo: 100 1 0 0 0 0 0 0 100 1 0 0 0 0 0 0\n  eth0: 1000 2 0 0 0 0 0 0 2000 3 0 0 0 0 0 0\n",
        )
        .unwrap();
        assert!(net.rx_bytes == 1000 && net.tx_bytes == 2000);
        assert!(parse_proc_net_dev("").is_none());

        assert_eq!(format_speed(512.0), "0.5KiB");
        assert_eq!(format_speed(15.0 * 1024.0), "15KiB");
        assert_eq!(format_speed(2.5 * 1024.0 * 1024.0), "2.5MiB");

        let disk = disk_usage("/").unwrap();
        assert!(disk.total_bytes > 0 && disk.used_bytes <= disk.total_bytes);
        assert!(disk_usage("/does/not/exist").is_none());

        let mut state = SystemStatsState::default();
        assert!(!state.have_prev && !state.have_prev_net);
        system_stats_poll(&mut state);
        assert!(state.have_prev && state.mem_total_gb > 0.0 && state.disk_pct >= 0.0);
    }
}
