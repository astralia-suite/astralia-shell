use std::cell::{Cell, RefCell};
use std::collections::VecDeque;
use std::os::fd::OwnedFd;
use std::path::{Path, PathBuf};
use std::rc::{Rc, Weak};

use async_io::Async;

use crate::base::inotify;
use crate::base::process::{AsyncProcess, spawn_detached};
use crate::base::signal::Signal;
use crate::service::runtime;

const BACKLIGHT_DIR: &str = "/sys/class/backlight";
const MIN_PERCENT: i32 = 1;

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct DdcBus {
    pub output: String,
    pub bus: String,
}

fn value_of<'a>(line: &'a str, key: &str) -> &'a str {
    line.strip_prefix(key).map_or("", str::trim)
}

/// The displays of `ddcutil detect --brief`, connectors without their `cardN-` prefix.
pub fn parse_ddc_detect(text: &str) -> Vec<DdcBus> {
    let mut found = Vec::new();
    let mut display = false;
    let mut current = DdcBus::default();
    let mut flush = |display: &mut bool, current: &mut DdcBus| {
        if *display && !current.output.is_empty() && !current.bus.is_empty() {
            found.push(std::mem::take(current));
        }
        *current = DdcBus::default();
        *display = false;
    };
    for line in text.lines() {
        let t = line.trim_matches([' ', '\t', '\r']);
        let (bus, conn) = (value_of(t, "I2C bus:"), value_of(t, "DRM connector:"));
        if t.is_empty() {
            flush(&mut display, &mut current);
        } else if line.starts_with("Display ") {
            flush(&mut display, &mut current);
            display = true;
        } else if !bus.is_empty() {
            current.bus = bus.rfind('-').map_or(String::new(), |at| bus[at + 1..].to_string());
        } else if !conn.is_empty() {
            current.output = conn.find('-').map_or(String::new(), |at| conn[at + 1..].to_string());
        }
    }
    flush(&mut display, &mut current);
    found
}

/// `VCP 10 C <current> <max>` from `ddcutil getvcp 10 --brief`.
pub fn parse_ddc_brightness(text: &str) -> Option<(i32, i32)> {
    let mut words = text.split_whitespace();
    let (vcp, code, kind) = (words.next()?, words.next()?, words.next()?);
    let current: i32 = words.next()?.parse().ok()?;
    let max: i32 = words.next()?.parse().ok()?;
    (vcp == "VCP" && code == "10" && kind == "C" && max > 0).then_some((current, max))
}

fn read_int(path: &Path) -> i32 {
    std::fs::read_to_string(path).ok().and_then(|t| t.trim().parse().ok()).unwrap_or(0)
}

fn percent_of(value: i32, max: i32) -> i32 {
    (f64::from(value) * 100.0 / f64::from(max)).round() as i32
}

/// The raw DDC value for `percent`; when the percent changes but rounds back to `value` (a maximum below 100),
/// it moves one raw unit toward it, so every step does something. Never below 1.
fn ddc_raw_for(percent: i32, value: i32, max: i32) -> i32 {
    let target = (f64::from(percent) * f64::from(max) / 100.0).round() as i32;
    let current = percent_of(value, max);
    let raw = if target == value && percent != current { value + (percent - current).signum() } else { target };
    raw.clamp(1, max.max(1))
}

struct Display {
    ddc: DdcBus,
    max: i32,
    value: i32,
}

#[derive(Clone)]
struct Job {
    output: String,
    write: bool,
    raw: i32,
}

pub struct BrightnessService {
    this: Weak<BrightnessService>,
    /// The first backlight device, as `/sys/class/backlight/<device>`.
    device: Option<PathBuf>,
    max: i32,
    backlight_percent: Cell<i32>,
    detect: RefCell<AsyncProcess>,
    worker: RefCell<AsyncProcess>,
    displays: RefCell<Vec<Display>>,
    jobs: RefCell<VecDeque<Job>>,
    pub changed: Signal,
}

impl BrightnessService {
    pub fn start() -> Rc<BrightnessService> {
        let device = std::fs::read_dir(BACKLIGHT_DIR).ok().and_then(|mut d| d.next()).and_then(Result::ok).map(|e| e.path());
        let max = device.as_ref().map_or(0, |d| read_int(&d.join("max_brightness")));
        let service = Rc::new_cyclic(|this| BrightnessService {
            this: this.clone(),
            device,
            max,
            backlight_percent: Cell::new(0),
            detect: RefCell::default(),
            worker: RefCell::default(),
            displays: RefCell::default(),
            jobs: RefCell::default(),
            changed: Signal::default(),
        });
        let detect = service.detect.borrow_mut().start(&["ddcutil", "detect", "--brief"], false);
        let weak = Rc::downgrade(&service);
        runtime::spawn(async move {
            if let (Some(text), Some(service)) = (detect.await, weak.upgrade()) {
                service.detected(&text);
            }
        });
        if !service.available() {
            ::log::info!("brightness: no backlight device");
            return service;
        }
        service.backlight_percent.set(service.percent());
        match service.watch_backlight() {
            Ok(fd) => runtime::spawn(watch_loop(fd, Rc::downgrade(&service))),
            Err(e) => ::log::error!("brightness: cannot watch the backlight: {e}"),
        }
        service
    }

    /// `brightness` changes on writes, `actual_brightness` on hardware keys.
    fn watch_backlight(&self) -> std::io::Result<Async<OwnedFd>> {
        let device = self.device.as_ref().ok_or(std::io::ErrorKind::NotFound)?;
        let fd = inotify::open()?;
        inotify::add_watch(&fd, &device.join("brightness"), libc::IN_MODIFY)?;
        let _ = inotify::add_watch(&fd, &device.join("actual_brightness"), libc::IN_MODIFY);
        Ok(fd)
    }

    fn backlight_touched(&self) {
        let next = self.percent();
        if self.backlight_percent.replace(next) != next {
            ::log::info!("brightness: {next}%");
            self.changed.emit(&());
        }
    }

    pub fn available(&self) -> bool {
        self.max > 0
    }

    pub fn percent(&self) -> i32 {
        match &self.device {
            Some(device) if self.available() => percent_of(read_int(&device.join("brightness")), self.max),
            _ => 0,
        }
    }

    pub fn set(&self, percent: i32) {
        if let (Some(device), true) = (&self.device, self.available()) {
            let name = device.file_name().unwrap_or_default().to_string_lossy();
            spawn_detached(&format!("brightnessctl -q -d {name} set {}%", percent.clamp(MIN_PERCENT, 100)));
        }
    }

    fn with_display<T>(&self, output: &str, f: impl FnOnce(&mut Display) -> T) -> Option<T> {
        self.displays.borrow_mut().iter_mut().find(|d| d.ddc.output == output && d.max > 0).map(f)
    }

    /// An external monitor over DDC, else the backlight.
    pub fn available_on(&self, output: &str) -> bool {
        self.with_display(output, |_| ()).is_some() || self.available()
    }

    pub fn percent_on(&self, output: &str) -> i32 {
        self.with_display(output, |d| percent_of(d.value, d.max)).unwrap_or_else(|| self.percent())
    }

    pub fn set_on(&self, output: &str, percent: i32) {
        let percent = percent.clamp(MIN_PERCENT, 100);
        let raw = self.with_display(output, |d| {
            d.value = ddc_raw_for(percent, d.value, d.max);
            d.value
        });
        match raw {
            Some(raw) => self.queue(Job { output: output.into(), write: true, raw }),
            None => self.set(percent),
        }
    }

    pub fn refresh(&self, output: &str) {
        if self.with_display(output, |_| ()).is_some() {
            self.queue(Job { output: output.into(), write: false, raw: 0 });
        }
    }

    /// A newer job replaces a queued one of the same kind for the same output.
    fn queue(&self, job: Job) {
        {
            let mut jobs = self.jobs.borrow_mut();
            jobs.retain(|q| q.output != job.output || q.write != job.write);
            jobs.push_back(job);
        }
        self.next();
    }

    fn next(&self) {
        loop {
            if self.worker.borrow().running() {
                return;
            }
            let Some(job) = self.jobs.borrow_mut().pop_front() else { return };
            let Some(bus) = self.with_display(&job.output, |d| d.ddc.bus.clone()) else { continue };
            let raw = job.raw.to_string();
            let argv: Vec<&str> =
                if job.write { vec!["ddcutil", "-b", &bus, "setvcp", "10", &raw] } else { vec!["ddcutil", "-b", &bus, "getvcp", "10", "--brief"] };
            let run = self.worker.borrow_mut().start(&argv, false);
            let weak = self.this.clone();
            runtime::spawn(async move {
                let Some(text) = run.await else { return };
                if let Some(service) = weak.upgrade() {
                    service.finished(&job, &text);
                }
            });
            return;
        }
    }

    fn finished(&self, job: &Job, text: &str) {
        if !job.write
            && let Some((value, max)) = parse_ddc_brightness(text)
        {
            let moved = self.with_display(&job.output, |d| {
                let moved = d.value != value || d.max != max;
                d.value = value;
                d.max = max;
                moved
            });
            if moved == Some(true) {
                ::log::info!("brightness: {} {}%", job.output, percent_of(value, max));
                self.changed.emit(&());
            }
        }
        self.next();
    }

    fn detected(&self, text: &str) {
        let found = parse_ddc_detect(text);
        for ddc in &found {
            ::log::info!("brightness: ddc display {} on i2c-{}", ddc.output, ddc.bus);
        }
        let outputs: Vec<String> = {
            let mut displays = self.displays.borrow_mut();
            displays.extend(found.into_iter().map(|ddc| Display { ddc, max: 0, value: 0 }));
            for display in displays.iter_mut() {
                display.max = 100;
            }
            displays.iter().map(|d| d.ddc.output.clone()).collect()
        };
        for output in outputs {
            self.queue(Job { output, write: false, raw: 0 });
        }
    }
}

async fn watch_loop(fd: Async<OwnedFd>, service: Weak<BrightnessService>) {
    while fd.readable().await.is_ok() {
        inotify::drain(&fd);
        let Some(service) = service.upgrade() else { return };
        service.backlight_touched();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ddc_steps_move_the_raw_value() {
        assert_eq!(ddc_raw_for(49, 50, 100), 49, "one raw unit per percent at max 100");
        assert_eq!(ddc_raw_for(99, 50, 50), 49, "a step down at max 50 still moves down");
        assert_eq!(ddc_raw_for(98, 49, 50), 49, "98 % of 50 is the current value already");
        assert_eq!(ddc_raw_for(99, 49, 50), 50, "a step up moves up");
        assert_eq!(ddc_raw_for(100, 50, 50), 50, "no change at the top");
        assert_eq!(ddc_raw_for(1, 1, 50), 1, "never below 1");
        assert_eq!(ddc_raw_for(70, 10, 100), 70, "a jump lands where asked");
    }

    #[test]
    fn brightness_parsers() {
        let found = parse_ddc_detect(
            "Display 1\n   I2C bus:  /dev/i2c-5\n   DRM connector:           card1-DP-1\n   Monitor:  DEL:DELL U2720Q:ABC\n\nDisplay 2\n   I2C bus:  /dev/i2c-7\n   DRM connector:           card1-HDMI-A-1\n\nInvalid display\n   I2C bus:  /dev/i2c-9\n   DRM connector:           card1-DP-3\n",
        );
        assert_eq!(found.len(), 2, "only listed displays are DDC buses");
        assert!(found[0].output == "DP-1" && found[0].bus == "5", "the connector drops the card prefix");
        assert!(found[1].output == "HDMI-A-1" && found[1].bus == "7", "connector names keep their dashes");
        assert!(parse_ddc_detect("").is_empty(), "no ddcutil output means no displays");

        let real = parse_ddc_detect(
            "Display 1\n   I2C bus:          /dev/i2c-7\n   DRM connector:    card1-DP-2\n   drm_connector_id: 837\n   Monitor:          SAM:LC34G55T:HNTXA00054\n\nDisplay 2\n   I2C bus:          /dev/i2c-8\n   DRM connector:    card1-DP-3\n   drm_connector_id: 841\n   Monitor:          SAM:Odyssey G91F:HNTY700002\n",
        );
        assert!(
            real.len() == 2 && real[0].output == "DP-2" && real[0].bus == "7" && real[1].output == "DP-3" && real[1].bus == "8",
            "ddcutil's real two-monitor output parses"
        );

        assert_eq!(parse_ddc_brightness("VCP 10 C 35 100\n"), Some((35, 100)), "brightness is current and max");
        assert_eq!(parse_ddc_brightness("VCP 12 C 35 100"), None, "other VCP codes are ignored");
        assert_eq!(parse_ddc_brightness("Display not found"), None, "an error is not a value");
        assert_eq!(parse_ddc_brightness("VCP 10 C 0 0"), None, "a zero max is not usable");
    }
}
