//! The polkit authentication agent. It registers with polkitd over D-Bus, holds each
//! `BeginAuthentication` call open while the user answers, and runs the PAM conversation through
//! `polkit-agent-helper-1`, which sends `g_strescape`d lines and reads one line per answer.

use std::cell::{Cell, RefCell};
use std::collections::HashMap;
use std::fs::File;
use std::io::{self, Write};
use std::os::fd::{AsRawFd, OwnedFd};
use std::os::unix::net::UnixStream;
use std::path::Path;
use std::rc::{Rc, Weak};
use std::time::Duration;

use async_io::Async;
use futures_lite::future;
use futures_lite::io::{AsyncBufReadExt, BufReader};
use zbus::zvariant::{OwnedObjectPath, OwnedValue, Value};

use crate::base::process::spawn_piped;
use crate::base::signal::Signal;
use crate::render::text_field::text_field_wipe;
use crate::service::runtime;
use crate::service::user::login_name_of;

pub const AGENT_OBJECT_PATH: &str = "/org/astralia_shell/PolkitAuthenticationAgent";
const AUTHORITY_NAME: &str = "org.freedesktop.PolicyKit1";
const AUTHORITY_PATH: &str = "/org/freedesktop/PolicyKit1/Authority";
const AUTHORITY_INTERFACE: &str = "org.freedesktop.PolicyKit1.Authority";
const HELPER_SOCKET: &str = "/run/polkit/agent-helper.socket";
const HELPER_PATH: &str = "/usr/lib/polkit-1/polkit-agent-helper-1";
const INCORRECT_TEXT: &str = "Incorrect password, try again";
/// How long dropping the service waits for the unregister call; polkitd also drops an agent whose connection closes.
const UNREGISTER_TIMEOUT: Duration = Duration::from_millis(500);

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct PolkitRequestIdentity {
    pub kind: String,
    pub id: u32,
    pub user_name: String,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct PolkitRequest {
    pub action_id: String,
    pub message: String,
    pub icon_name: String,
    pub cookie: String,
    pub identities: Vec<PolkitRequestIdentity>,
}

/// Who the agent speaks for.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Subject {
    Session(String),
    User(u32),
}

impl Subject {
    fn arg(&self) -> (&'static str, HashMap<&'static str, Value<'_>>) {
        match self {
            Subject::Session(id) => ("unix-session", HashMap::from([("session-id", Value::from(id.as_str()))])),
            Subject::User(uid) => ("unix-user", HashMap::from([("uid", Value::from(*uid))])),
        }
    }
}

/// `g_strcompress`: undoes `g_strescape`. A trailing lone backslash ends the string.
pub fn strcompress(s: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(s.len());
    let mut i = 0;
    while i < s.len() {
        if s[i] != b'\\' {
            out.push(s[i]);
            i += 1;
            continue;
        }
        i += 1;
        let Some(&c) = s.get(i) else { break };
        if (b'0'..=b'7').contains(&c) {
            let mut value = 0u32;
            let end = (i + 3).min(s.len());
            while i < end && (b'0'..=b'7').contains(&s[i]) {
                value = value * 8 + u32::from(s[i] - b'0');
                i += 1;
            }
            out.push(value as u8);
            continue;
        }
        out.push(match c {
            b'b' => 0x08,
            b'f' => 0x0c,
            b'n' => b'\n',
            b'r' => b'\r',
            b't' => b'\t',
            b'v' => 0x0b,
            other => other,
        });
        i += 1;
    }
    out
}

#[derive(Debug, PartialEq, Eq)]
enum HelperEvent {
    /// The prompt and whether the answer is echoed.
    Request(String, bool),
    Error(String),
    Info(String),
    Completed(bool),
}

/// One helper line, its newline already stripped or not. An unknown line ends the session as a failure.
fn helper_event(line: &[u8]) -> HelperEvent {
    let line = line.strip_suffix(b"\n").unwrap_or(line);
    let text = String::from_utf8_lossy(&strcompress(line)).into_owned();
    let rest = |prefix: &str| text.strip_prefix(prefix).map(str::to_string);
    if let Some(s) = rest("PAM_PROMPT_ECHO_OFF ") {
        HelperEvent::Request(s, false)
    } else if let Some(s) = rest("PAM_PROMPT_ECHO_ON ") {
        HelperEvent::Request(s, true)
    } else if let Some(s) = rest("PAM_ERROR_MSG ") {
        HelperEvent::Error(s)
    } else if let Some(s) = rest("PAM_TEXT_INFO ") {
        HelperEvent::Info(s)
    } else if text.starts_with("SUCCESS") {
        HelperEvent::Completed(true)
    } else {
        if !text.starts_with("FAILURE") {
            ::log::warn!("polkit: unknown line from helper");
        }
        HelperEvent::Completed(false)
    }
}

/// The unix-user whose uid is `euid`, else the first unix-user; groups are skipped.
pub fn choose_identity(identities: &[PolkitRequestIdentity], euid: u32) -> Option<&PolkitRequestIdentity> {
    let mut users = identities.iter().filter(|i| i.kind == "unix-user");
    let first = users.clone().next();
    users.find(|i| i.id == euid).or(first)
}

/// The two ends of a helper conversation: lines come from `read`, answers go to `write`.
pub struct HelperIo {
    pub read: File,
    pub write: File,
    pub pid: Option<libc::pid_t>,
}

/// Opens a helper conversation for `user`; the service then writes the cookie line.
pub type HelperConnect = Box<dyn Fn(&str) -> io::Result<HelperIo>>;

/// The socket-activated helper when its socket answers, else the setuid helper.
fn connect_helper(user: &str) -> io::Result<HelperIo> {
    if Path::new(HELPER_SOCKET).exists() {
        match UnixStream::connect(HELPER_SOCKET) {
            Ok(stream) => {
                let write = File::from(OwnedFd::from(stream.try_clone()?));
                let mut io = HelperIo { read: File::from(OwnedFd::from(stream)), write, pid: None };
                io.write.write_all(format!("{user}\n").as_bytes())?;
                return Ok(io);
            }
            Err(e) => ::log::warn!("polkit: cannot connect to {HELPER_SOCKET}, spawning the setuid helper: {e}"),
        }
    }
    let (pid, stdin, stdout) = spawn_piped(&[HELPER_PATH, user])?;
    Ok(HelperIo { read: File::from(OwnedFd::from(stdout)), write: File::from(OwnedFd::from(stdin)), pid: Some(pid) })
}

/// A running helper conversation; dropping it closes the connection and stops the helper.
struct Helper {
    id: u64,
    write: File,
    pid: Option<libc::pid_t>,
}

impl Drop for Helper {
    fn drop(&mut self) {
        // SAFETY: plain syscalls on our own fd and our own unreaped child. On a pipe `shutdown`
        // fails with ENOTSOCK, which is fine: the kill closes the helper's end.
        unsafe { libc::shutdown(self.write.as_raw_fd(), libc::SHUT_RDWR) };
        if let Some(pid) = self.pid {
            // SAFETY: the pid is reaped only below, so it still names our child.
            unsafe { libc::kill(pid, libc::SIGTERM) };
            blocking::unblock(move || {
                // SAFETY: reaps our own child; nothing else waits for it.
                while unsafe { libc::waitpid(pid, std::ptr::null_mut(), 0) } < 0 && io::Error::last_os_error().kind() == io::ErrorKind::Interrupted {}
            })
            .detach();
        }
    }
}

type Reply = async_channel::Sender<Result<(), String>>;

/// The open `BeginAuthentication` call; it is answered exactly once, as cancelled when dropped unanswered.
struct Pending {
    request: PolkitRequest,
    reply: Option<Reply>,
}

impl Pending {
    fn finish(&mut self, result: Result<(), String>) {
        if let Some(reply) = self.reply.take() {
            let _ = reply.try_send(result);
        }
    }
}

impl Drop for Pending {
    fn drop(&mut self) {
        self.finish(Err("Authentication request was destroyed".into()));
    }
}

#[derive(Default)]
struct Conversation {
    response_required: bool,
    response_visible: bool,
    prompt: String,
    info: String,
    info_error: bool,
}

#[derive(Debug, zbus::DBusError)]
#[zbus(prefix = "org.freedesktop.PolicyKit1.Error")]
enum AgentError {
    #[zbus(error)]
    ZBus(zbus::Error),
    Cancelled(String),
}

enum AgentCall {
    Begin(PolkitRequest, Reply),
    Cancel(String),
}

/// The exported `org.freedesktop.PolicyKit1.AuthenticationAgent`; it runs on zbus's thread and
/// forwards to the loop thread.
struct Agent {
    calls: async_channel::Sender<AgentCall>,
}

/// Only the current owner of the polkit name may drive the agent.
async fn from_authority(conn: &zbus::Connection, header: &zbus::message::Header<'_>) -> bool {
    let Some(sender) = header.sender() else { return false };
    let Ok(dbus) = zbus::fdo::DBusProxy::new(conn).await else { return false };
    let Ok(name) = zbus::names::BusName::try_from(AUTHORITY_NAME) else { return false };
    dbus.get_name_owner(name).await.is_ok_and(|owner| owner.as_str() == sender.as_str())
}

fn request_identity(kind: String, details: &HashMap<String, OwnedValue>) -> PolkitRequestIdentity {
    let number = |key: &str| details.get(key).and_then(|v| v.try_clone().ok()).and_then(|v| u32::try_from(v).ok());
    match kind.as_str() {
        "unix-user" => {
            let id = number("uid").unwrap_or(0);
            PolkitRequestIdentity { kind, id, user_name: login_name_of(id).unwrap_or_else(|| id.to_string()) }
        }
        "unix-group" => PolkitRequestIdentity { kind, id: number("gid").unwrap_or(0), user_name: String::new() },
        _ => PolkitRequestIdentity { kind, ..Default::default() },
    }
}

#[zbus::interface(name = "org.freedesktop.PolicyKit1.AuthenticationAgent")]
impl Agent {
    #[allow(clippy::too_many_arguments)]
    async fn begin_authentication(
        &self,
        #[zbus(connection)] conn: &zbus::Connection,
        #[zbus(header)] header: zbus::message::Header<'_>,
        action_id: String,
        message: String,
        icon_name: String,
        _details: HashMap<String, String>,
        cookie: String,
        identities: Vec<(String, HashMap<String, OwnedValue>)>,
    ) -> Result<(), AgentError> {
        if !from_authority(conn, &header).await {
            return Err(AgentError::Cancelled("Caller is not the polkit authority".into()));
        }
        let mut unique: Vec<PolkitRequestIdentity> = Vec::new();
        for (kind, details) in identities {
            let identity = request_identity(kind, &details);
            if !unique.iter().any(|u| u.kind == identity.kind && u.id == identity.id) {
                unique.push(identity);
            }
        }
        let request = PolkitRequest { action_id, message, icon_name, cookie, identities: unique };
        let (reply, answer) = async_channel::bounded(1);
        if self.calls.send(AgentCall::Begin(request, reply)).await.is_err() {
            return Err(AgentError::Cancelled("Polkit listener is not attached".into()));
        }
        answer.recv().await.unwrap_or_else(|_| Err("Authentication request was destroyed".into())).map_err(AgentError::Cancelled)
    }

    async fn cancel_authentication(&self, #[zbus(connection)] conn: &zbus::Connection, #[zbus(header)] header: zbus::message::Header<'_>, cookie: String) {
        if from_authority(conn, &header).await {
            let _ = self.calls.send(AgentCall::Cancel(cookie)).await;
        }
    }
}

/// `XDG_SESSION_ID`, else logind's session for this pid, else the unix user when logind knows no session.
async fn resolve_subject(bus: &zbus::Connection) -> Result<Subject, String> {
    if let Some(id) = std::env::var("XDG_SESSION_ID").ok().filter(|s| !s.is_empty()) {
        return Ok(Subject::Session(id));
    }
    let pid = std::process::id();
    let manager = zbus::Proxy::new(bus, "org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager").await.map_err(|e| e.to_string())?;
    match manager.call::<_, _, OwnedObjectPath>("GetSessionByPID", &(pid,)).await {
        Ok(path) => {
            let session = zbus::Proxy::new(bus, "org.freedesktop.login1", path, "org.freedesktop.login1.Session").await.map_err(|e| e.to_string())?;
            session.get_property::<String>("Id").await.map(Subject::Session).map_err(|e| e.to_string())
        }
        Err(zbus::Error::MethodError(name, _, _)) if name.as_str() == "org.freedesktop.login1.NoSessionForPID" => {
            ::log::info!("polkit: no logind session for pid, registering for the unix user");
            // SAFETY: getuid cannot fail.
            Ok(Subject::User(unsafe { libc::getuid() }))
        }
        Err(e) => Err(e.to_string()),
    }
}

fn locale() -> String {
    ["LC_ALL", "LC_MESSAGES", "LANG"].iter().filter_map(|k| std::env::var(k).ok()).find(|v| !v.is_empty()).unwrap_or_else(|| "C".into())
}

pub struct PolkitService {
    this: Weak<PolkitService>,
    bus: zbus::Connection,
    connect: HelperConnect,
    subject: RefCell<Option<Subject>>,
    pending: RefCell<Option<Pending>>,
    helper: RefCell<Option<Helper>>,
    next_session: Cell<u64>,
    cancelling: Cell<bool>,
    conversation: RefCell<Conversation>,
    pub changed: Signal,
    /// Registration result: whether it worked, and the reason when not.
    pub ready: Signal<(bool, String)>,
}

impl PolkitService {
    fn new(bus: &zbus::Connection, connect: HelperConnect) -> Rc<PolkitService> {
        Rc::new_cyclic(|this| PolkitService {
            this: this.clone(),
            bus: bus.clone(),
            connect,
            subject: RefCell::new(None),
            pending: RefCell::new(None),
            helper: RefCell::new(None),
            next_session: Cell::new(0),
            cancelling: Cell::new(false),
            conversation: RefCell::default(),
            changed: Signal::default(),
            ready: Signal::default(),
        })
    }

    /// Registers on the system bus in the background; the result is logged and sent through `ready`.
    pub fn start(bus: &zbus::Connection) -> Rc<PolkitService> {
        let service = PolkitService::new(bus, Box::new(connect_helper));
        let (weak, bus) = (Rc::downgrade(&service), bus.clone());
        runtime::spawn(async move {
            let subject = resolve_subject(&bus).await;
            let Some(service) = weak.upgrade() else { return };
            match subject {
                Ok(subject) => service.register(subject).await,
                Err(reason) => service.registration_failed(reason),
            }
        });
        service
    }

    #[cfg(test)]
    fn start_with(bus: &zbus::Connection, subject: Subject, connect: HelperConnect) -> Rc<PolkitService> {
        let service = PolkitService::new(bus, connect);
        let weak = Rc::downgrade(&service);
        runtime::spawn(async move {
            if let Some(service) = weak.upgrade() {
                service.register(subject).await;
            }
        });
        service
    }

    /// Exports the agent, then registers it once; a refusal leaves the agent idle.
    async fn register(&self, subject: Subject) {
        let (calls, incoming) = async_channel::unbounded();
        if let Err(e) = self.bus.object_server().at(AGENT_OBJECT_PATH, Agent { calls }).await {
            self.registration_failed(e.to_string());
            return;
        }
        let call = async {
            let authority = zbus::Proxy::new(&self.bus, AUTHORITY_NAME, AUTHORITY_PATH, AUTHORITY_INTERFACE).await?;
            authority.call_method("RegisterAuthenticationAgent", &(subject.arg(), locale(), AGENT_OBJECT_PATH)).await
        };
        if let Err(e) = call.await {
            let _ = self.bus.object_server().remove::<Agent, _>(AGENT_OBJECT_PATH).await;
            let reason = match e {
                zbus::Error::MethodError(_, Some(message), _) => message,
                e => e.to_string(),
            };
            self.registration_failed(reason);
            return;
        }
        ::log::info!("polkit: agent registered at {AGENT_OBJECT_PATH}");
        *self.subject.borrow_mut() = Some(subject);
        let weak = self.this.clone();
        runtime::spawn(async move {
            while let Ok(call) = incoming.recv().await {
                let Some(service) = weak.upgrade() else { return };
                match call {
                    AgentCall::Begin(request, reply) => service.begin(request, reply),
                    AgentCall::Cancel(cookie) => service.cancel_from_authority(&cookie),
                }
            }
        });
        self.ready.emit(&(true, String::new()));
    }

    fn registration_failed(&self, reason: String) {
        ::log::error!("polkit: agent registration failed: {reason}");
        self.ready.emit(&(false, reason));
    }

    pub fn registered(&self) -> bool {
        self.subject.borrow().is_some()
    }

    pub fn pending(&self) -> bool {
        self.pending.borrow().is_some()
    }

    /// The request's message, else its action id; empty when nothing is pending.
    pub fn message(&self) -> String {
        self.pending.borrow().as_ref().map_or_else(String::new, |p| {
            let r = &p.request;
            if r.message.is_empty() { r.action_id.clone() } else { r.message.clone() }
        })
    }

    pub fn request(&self) -> PolkitRequest {
        self.pending.borrow().as_ref().map(|p| p.request.clone()).unwrap_or_default()
    }

    pub fn response_required(&self) -> bool {
        self.conversation.borrow().response_required
    }

    pub fn response_visible(&self) -> bool {
        self.conversation.borrow().response_visible
    }

    pub fn input_prompt(&self) -> String {
        self.conversation.borrow().prompt.clone()
    }

    pub fn info(&self) -> String {
        self.conversation.borrow().info.clone()
    }

    pub fn info_is_error(&self) -> bool {
        self.conversation.borrow().info_error
    }

    fn changed(&self) {
        self.changed.emit(&());
    }

    fn clear_pending(&self, reason: &str, silent: bool) {
        self.helper.borrow_mut().take();
        let pending = self.pending.borrow_mut().take();
        if let Some(mut pending) = pending {
            pending.finish(Err(reason.into()));
        }
        self.cancelling.set(false);
        *self.conversation.borrow_mut() = Conversation::default();
        if !silent {
            self.changed();
        }
    }

    fn begin(&self, request: PolkitRequest, reply: Reply) {
        if self.pending() {
            self.clear_pending("Replaced by a newer authentication request", true);
        }
        let mut pending = Pending { request, reply: Some(reply) };
        if pending.request.identities.is_empty() {
            ::log::error!("polkit: request {} has no identities", pending.request.action_id);
            pending.finish(Err("Authentication request has no identities".into()));
            return;
        }
        *self.pending.borrow_mut() = Some(pending);
        *self.conversation.borrow_mut() = Conversation::default();
        if !self.start_session() {
            ::log::error!("polkit: cannot start a session for {}", self.request().action_id);
            self.clear_pending("Failed to start authentication session", false);
            return;
        }
        self.changed();
    }

    /// Opens a helper conversation for the chosen identity and sends the cookie.
    fn start_session(&self) -> bool {
        self.helper.borrow_mut().take();
        let request = self.request();
        // SAFETY: geteuid cannot fail.
        let euid = unsafe { libc::geteuid() };
        let Some(identity) = choose_identity(&request.identities, euid) else {
            ::log::error!("polkit: {} has no unix-user identity", request.action_id);
            return false;
        };
        let Some(user) = login_name_of(identity.id) else {
            ::log::error!("polkit: no user with uid {}", identity.id);
            return false;
        };
        let mut io = match (self.connect)(&user) {
            Ok(io) => io,
            Err(e) => {
                ::log::error!("polkit: cannot start the helper: {e}");
                return false;
            }
        };
        if let Err(e) = io.write.write_all(format!("{}\n", request.cookie).as_bytes()) {
            ::log::error!("polkit: cannot write to the helper: {e}");
            return false;
        }
        let read = match Async::new(io.read) {
            Ok(read) => read,
            Err(e) => {
                ::log::error!("polkit: {e}");
                return false;
            }
        };
        let id = self.next_session.get() + 1;
        self.next_session.set(id);
        *self.helper.borrow_mut() = Some(Helper { id, write: io.write, pid: io.pid });
        ::log::info!("polkit: authenticating {} as {user}", request.action_id);
        let weak = self.this.clone();
        runtime::spawn(async move {
            let mut reader = BufReader::new(read);
            let mut line = Vec::new();
            loop {
                line.clear();
                let read = reader.read_until(b'\n', &mut line).await.unwrap_or(0);
                let Some(service) = weak.upgrade() else { return };
                if service.helper.borrow().as_ref().is_none_or(|h| h.id != id) {
                    return;
                }
                // EOF without a verdict counts as a failure.
                let event = if read == 0 { HelperEvent::Completed(false) } else { helper_event(&line) };
                let done = matches!(event, HelperEvent::Completed(_));
                service.on_helper(event);
                if done {
                    return;
                }
            }
        });
        true
    }

    fn on_helper(&self, event: HelperEvent) {
        match event {
            HelperEvent::Request(prompt, echo) => {
                let mut c = self.conversation.borrow_mut();
                c.prompt = if prompt.is_empty() { "Password:".into() } else { prompt };
                c.response_visible = echo;
                c.response_required = true;
            }
            HelperEvent::Error(text) => self.set_info(text, true),
            HelperEvent::Info(text) => self.set_info(text, false),
            HelperEvent::Completed(gained) => {
                self.helper.borrow_mut().take();
                return self.on_completed(gained);
            }
        }
        self.changed();
    }

    fn set_info(&self, text: String, error: bool) {
        let mut c = self.conversation.borrow_mut();
        c.info = text;
        c.info_error = error;
    }

    fn on_completed(&self, gained: bool) {
        if !self.pending() {
            return;
        }
        if gained {
            let pending = self.pending.borrow_mut().take();
            if let Some(mut pending) = pending {
                ::log::info!("polkit: {} authorized", pending.request.action_id);
                pending.finish(Ok(()));
            }
            *self.conversation.borrow_mut() = Conversation::default();
            self.changed();
            return;
        }
        if self.cancelling.get() {
            self.clear_pending("Authentication request cancelled", false);
            return;
        }
        *self.conversation.borrow_mut() = Conversation { info: INCORRECT_TEXT.into(), info_error: true, ..Default::default() };
        self.changed();
        if !self.start_session() {
            self.clear_pending("Failed to restart authentication session", false);
        }
    }

    fn cancel_from_authority(&self, cookie: &str) {
        if self.pending.borrow().as_ref().is_none_or(|p| p.request.cookie != cookie) {
            return;
        }
        self.cancelling.set(true);
        self.clear_pending("Authentication request cancelled by polkit", false);
    }

    /// Sends `response` as the answer to the current prompt; the string is wiped either way.
    pub fn respond(&self, response: &mut String) {
        let ready = self.pending() && self.response_required() && !response.is_empty();
        if ready && let Some(helper) = self.helper.borrow_mut().as_mut() {
            let mut line = String::with_capacity(response.len() + 1);
            line.push_str(response);
            if !line.ends_with('\n') {
                line.push('\n');
            }
            if let Err(e) = helper.write.write_all(line.as_bytes()) {
                ::log::error!("polkit: cannot write to the helper: {e}");
            }
            text_field_wipe(&mut line);
            let mut c = self.conversation.borrow_mut();
            c.response_required = false;
            c.prompt.clear();
            c.info = "Authenticating...".into();
            c.info_error = false;
        }
        text_field_wipe(response);
        self.changed();
    }

    pub fn cancel(&self) {
        if !self.pending() {
            return;
        }
        self.cancelling.set(true);
        self.clear_pending("Authentication request cancelled by user", false);
    }
}

impl Drop for PolkitService {
    fn drop(&mut self) {
        self.clear_pending("Authentication agent is shutting down", true);
        let Some(subject) = self.subject.borrow_mut().take() else { return };
        let bus = self.bus.clone();
        let unregister = async move {
            let authority = zbus::Proxy::new(&bus, AUTHORITY_NAME, AUTHORITY_PATH, AUTHORITY_INTERFACE).await?;
            authority.call_method("UnregisterAuthenticationAgent", &(subject.arg(), AGENT_OBJECT_PATH)).await.map(drop)
        };
        let timeout = async {
            async_io::Timer::after(UNREGISTER_TIMEOUT).await;
            Ok(())
        };
        if let Err(e) = future::block_on(future::or(unregister, timeout)) {
            ::log::info!("polkit: cannot unregister the agent: {e}");
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::base::dbus::{TestBus, connect_to, test_bus};
    use calloop::EventLoop;
    use std::io::{BufRead, Read};
    use std::sync::{Arc, Mutex};
    use std::time::Instant;

    #[test]
    fn strcompress_matches_glib() {
        // Each left side is what `g_strescape` writes for the right side.
        let cases: [(&[u8], &[u8]); 7] = [
            (b"plain", b"plain"),
            (b"a\\nb\\tc", b"a\nb\tc"),
            (b"\\\\ \\\"", b"\\ \""),
            (b"\\b\\f\\r\\v", b"\x08\x0c\r\x0b"),
            (b"\\001\\177", b"\x01\x7f"),
            (b"caf\\303\\251", "café".as_bytes()),
            (b"end\\", b"end"),
        ];
        for (escaped, plain) in cases {
            assert_eq!(strcompress(escaped), plain, "{:?}", String::from_utf8_lossy(escaped));
        }
        assert_eq!(strcompress(b"\\1234"), b"\x534", "octal takes at most three digits");
    }

    #[test]
    fn helper_line_dispatch() {
        assert_eq!(helper_event(b"PAM_PROMPT_ECHO_OFF Password: \n"), HelperEvent::Request("Password: ".into(), false));
        assert_eq!(helper_event(b"PAM_PROMPT_ECHO_ON Login:"), HelperEvent::Request("Login:".into(), true));
        assert_eq!(helper_event(b"PAM_ERROR_MSG bad\\nthing"), HelperEvent::Error("bad\nthing".into()));
        assert_eq!(helper_event(b"PAM_TEXT_INFO hi"), HelperEvent::Info("hi".into()));
        assert_eq!(helper_event(b"SUCCESS\n"), HelperEvent::Completed(true));
        assert_eq!(helper_event(b"FAILURE\n"), HelperEvent::Completed(false));
        assert_eq!(helper_event(b"GARBAGE\n"), HelperEvent::Completed(false), "an unknown line fails the session");
    }

    #[test]
    fn agent_choose_identity() {
        let id = |kind: &str, id| PolkitRequestIdentity { kind: kind.into(), id, user_name: String::new() };
        let list = [id("unix-group", 1000), id("unix-user", 0), id("unix-user", 1000)];
        assert_eq!(choose_identity(&list, 1000), Some(&list[2]), "own uid beats the first unix-user, groups are skipped");
        assert_eq!(choose_identity(&list, 4242), Some(&list[1]), "else the first unix-user");
        assert_eq!(choose_identity(&list[..1], 1000), None, "groups alone choose nothing");
    }

    /// Stands in for polkitd: owns the polkit name and records who registered.
    struct FakeAuthority {
        refuse: bool,
        registrations: Arc<Mutex<Vec<String>>>,
    }

    #[zbus::interface(name = "org.freedesktop.PolicyKit1.Authority")]
    impl FakeAuthority {
        fn register_authentication_agent(
            &self,
            #[zbus(header)] header: zbus::message::Header<'_>,
            _subject: (String, HashMap<String, OwnedValue>),
            _locale: String,
            _path: String,
        ) -> zbus::fdo::Result<()> {
            let sender = header.sender().map(|s| s.to_string()).unwrap_or_default();
            self.registrations.lock().unwrap().push(sender);
            if self.refuse { Err(zbus::fdo::Error::Failed("An authentication agent already exists for the given subject".into())) } else { Ok(()) }
        }

        fn unregister_authentication_agent(&self, _subject: (String, HashMap<String, OwnedValue>), _path: String) {}
    }

    struct Fixture {
        _bus: TestBus,
        event_loop: EventLoop<'static, ()>,
        authority: zbus::Connection,
        registrations: Arc<Mutex<Vec<String>>>,
        service: Rc<PolkitService>,
        /// The test ends of every helper the service opened.
        helpers: Rc<RefCell<Vec<UnixStream>>>,
        /// Whether the next helper connect succeeds.
        helper_ok: Rc<Cell<bool>>,
    }

    fn fixture(refuse: bool) -> Option<Fixture> {
        let bus = test_bus()?;
        let event_loop: EventLoop<'static, ()> = EventLoop::try_new().unwrap();
        runtime::attach(&event_loop.handle()).unwrap();
        let registrations = Arc::new(Mutex::new(Vec::new()));
        let fake = FakeAuthority { refuse, registrations: registrations.clone() };
        let authority = future::block_on(async {
            zbus::connection::Builder::address(bus.address()).unwrap().name(AUTHORITY_NAME).unwrap().serve_at(AUTHORITY_PATH, fake).unwrap().build().await.unwrap()
        });
        let conn = future::block_on(connect_to(bus.address()));
        let helpers: Rc<RefCell<Vec<UnixStream>>> = Rc::default();
        let helper_ok = Rc::new(Cell::new(true));
        let (h, ok) = (helpers.clone(), helper_ok.clone());
        let connect: HelperConnect = Box::new(move |_user| {
            if !ok.get() {
                return Err(io::ErrorKind::NotFound.into());
            }
            let (ours, theirs) = UnixStream::pair()?;
            theirs.set_read_timeout(Some(Duration::from_secs(2)))?;
            h.borrow_mut().push(theirs);
            let write = File::from(OwnedFd::from(ours.try_clone()?));
            Ok(HelperIo { read: File::from(OwnedFd::from(ours)), write, pid: None })
        });
        let service = PolkitService::start_with(&conn, Subject::User(1000), connect);
        Some(Fixture { _bus: bus, event_loop, authority, registrations, service, helpers, helper_ok })
    }

    impl Fixture {
        fn pump(&mut self, done: impl Fn(&Fixture) -> bool) -> bool {
            let deadline = Instant::now() + Duration::from_secs(3);
            while !done(self) && Instant::now() < deadline {
                self.event_loop.dispatch(Duration::from_millis(10), &mut ()).unwrap();
            }
            done(self)
        }

        fn agent(&self) -> String {
            self.registrations.lock().unwrap().last().cloned().unwrap()
        }

        /// Calls `method` on the agent from the authority's connection on another thread; joins to the D-Bus error name, `None` for success.
        fn call(&self, method: &'static str, cookie: &str) -> std::thread::JoinHandle<Option<String>> {
            let (conn, agent, cookie) = (self.authority.clone(), self.agent(), cookie.to_string());
            std::thread::spawn(move || {
                future::block_on(async {
                    let proxy = zbus::Proxy::new(&conn, agent, AGENT_OBJECT_PATH, "org.freedesktop.PolicyKit1.AuthenticationAgent").await.unwrap();
                    // SAFETY: geteuid cannot fail.
                    let uid = unsafe { libc::geteuid() };
                    let user: HashMap<&str, Value> = HashMap::from([("uid", Value::from(uid))]);
                    let identities = vec![("unix-user", user.clone()), ("unix-user", user)];
                    let result = if method == "BeginAuthentication" {
                        proxy.call_method(method, &("org.test.action", "Run a test", "", HashMap::<&str, &str>::new(), cookie.as_str(), identities)).await
                    } else {
                        proxy.call_method(method, &(cookie.as_str(),)).await
                    };
                    match result {
                        Ok(_) => None,
                        Err(zbus::Error::MethodError(name, _, _)) => Some(name.to_string()),
                        Err(e) => Some(e.to_string()),
                    }
                })
            })
        }

        /// Begins an authentication and waits until the service holds it with a helper open.
        fn begin(&mut self, cookie: &str) -> std::thread::JoinHandle<Option<String>> {
            let before = self.helpers.borrow().len();
            let call = self.call("BeginAuthentication", cookie);
            let cookie = cookie.to_string();
            assert!(self.pump(|f| f.service.request().cookie == cookie && f.helpers.borrow().len() > before));
            call
        }

        fn helper_line(&self, at: usize) -> String {
            let stream = self.helpers.borrow()[at].try_clone().unwrap();
            let mut line = String::new();
            std::io::BufReader::new(stream).read_line(&mut line).unwrap();
            line
        }

        /// EOF within the read timeout; unread lines before it do not matter.
        fn helper_closed(&self, at: usize) -> bool {
            let mut stream = self.helpers.borrow()[at].try_clone().unwrap();
            stream.read_to_end(&mut Vec::new()).is_ok()
        }

        fn finish(&mut self, call: std::thread::JoinHandle<Option<String>>) -> Option<String> {
            assert!(self.pump(|_| call.is_finished()));
            call.join().unwrap()
        }
    }

    const CANCELLED: &str = "org.freedesktop.PolicyKit1.Error.Cancelled";

    #[test]
    fn agent_cancel_paths() {
        let Some(mut f) = fixture(false) else { return };
        assert!(f.pump(|f| f.service.registered()));
        let identities = |f: &Fixture| f.service.request().identities.len();

        // CancelAuthentication from polkitd.
        let call = f.begin("c1");
        assert_eq!(identities(&f), 1, "identities are deduplicated");
        assert_eq!(f.helper_line(0), "c1\n", "the cookie is the first line");
        f.helpers.borrow_mut()[0].write_all(b"PAM_PROMPT_ECHO_OFF Password: \n").unwrap();
        assert!(f.pump(|f| f.service.response_required()));
        assert_eq!(f.service.input_prompt(), "Password: ");
        let other = f.call("CancelAuthentication", "other");
        assert_eq!(f.finish(other), None);
        assert!(f.service.pending(), "another cookie cancels nothing");
        let cancel = f.call("CancelAuthentication", "c1");
        assert_eq!(f.finish(cancel), None);
        assert_eq!(f.finish(call).as_deref(), Some(CANCELLED));
        assert!(!f.service.pending() && f.helper_closed(0));

        // Escape on the card.
        let call = f.begin("c2");
        f.service.cancel();
        assert_eq!(f.finish(call).as_deref(), Some(CANCELLED));
        assert!(!f.service.pending() && f.helper_closed(1));

        // A second request replaces the first.
        let first = f.begin("c3");
        let second = f.begin("c4");
        assert_eq!(f.finish(first).as_deref(), Some(CANCELLED));
        assert!(f.helper_closed(2));
        assert_eq!(f.service.request().cookie, "c4");

        // The helper exits without a verdict and cannot be restarted.
        f.helper_ok.set(false);
        f.helpers.borrow_mut()[3].shutdown(std::net::Shutdown::Both).unwrap();
        assert_eq!(f.finish(second).as_deref(), Some(CANCELLED));
        assert!(!f.service.pending());

        // A wrong answer restarts the session; the right one succeeds.
        f.helper_ok.set(true);
        let call = f.begin("c5");
        f.helpers.borrow_mut()[4].write_all(b"PAM_PROMPT_ECHO_OFF Password:\n").unwrap();
        assert!(f.pump(|f| f.service.response_required()));
        let mut password = String::from("hunter2");
        f.service.respond(&mut password);
        assert!(password.is_empty());
        assert_eq!(f.service.info(), "Authenticating...");
        let mut lines = std::io::BufReader::new(f.helpers.borrow()[4].try_clone().unwrap());
        let (mut cookie, mut answer) = (String::new(), String::new());
        lines.read_line(&mut cookie).unwrap();
        lines.read_line(&mut answer).unwrap();
        assert_eq!((cookie.as_str(), answer.as_str()), ("c5\n", "hunter2\n"), "the cookie, then the answer");
        f.helpers.borrow_mut()[4].write_all(b"FAILURE\n").unwrap();
        assert!(f.pump(|f| f.helpers.borrow().len() == 6));
        assert_eq!((f.service.info(), f.service.info_is_error()), (INCORRECT_TEXT.to_string(), true));
        assert_eq!(f.helper_line(5), "c5\n");
        f.helpers.borrow_mut()[5].write_all(b"SUCCESS\n").unwrap();
        assert_eq!(f.finish(call), None, "success returns an empty reply");
        assert!(!f.service.pending());
    }

    #[test]
    fn agent_registration_failure() {
        let Some(mut f) = fixture(true) else { return };
        let result: Rc<RefCell<Vec<(bool, String)>>> = Rc::default();
        let r = result.clone();
        f.service.ready.connect(move |v| r.borrow_mut().push(v.clone()));
        assert!(f.pump(|_| !result.borrow().is_empty()));
        for _ in 0..30 {
            f.event_loop.dispatch(Duration::from_millis(10), &mut ()).unwrap();
        }
        assert_eq!(*result.borrow(), [(false, "An authentication agent already exists for the given subject".to_string())]);
        assert_eq!(f.registrations.lock().unwrap().len(), 1, "registration is not retried");
        assert!(!f.service.registered());
    }
}
