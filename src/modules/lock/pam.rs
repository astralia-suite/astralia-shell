//! Password check through PAM, using the shipped `astralia-shell` service file when it is readable.

use std::ffi::{CStr, CString, c_char, c_int, c_void};
use std::ptr;

const PAM_SUCCESS: c_int = 0;
const PAM_BUF_ERR: c_int = 5;
const PAM_AUTHINFO_UNAVAIL: c_int = 9;
const PAM_CONV_ERR: c_int = 19;
const PAM_PROMPT_ECHO_OFF: c_int = 1;
const PAM_PROMPT_ECHO_ON: c_int = 2;

const SERVICE: &str = "astralia-shell";
const INSTALLED_DIR: &str = "/usr/share/astralia-shell/pam";

#[repr(C)]
struct PamMessage {
    msg_style: c_int,
    msg: *const c_char,
}

#[repr(C)]
struct PamResponse {
    resp: *mut c_char,
    resp_retcode: c_int,
}

type ConvFn = unsafe extern "C" fn(c_int, *mut *const PamMessage, *mut *mut PamResponse, *mut c_void) -> c_int;

#[repr(C)]
struct PamConv {
    conv: ConvFn,
    appdata_ptr: *mut c_void,
}

#[repr(C)]
struct PamHandle {
    _opaque: [u8; 0],
}

#[link(name = "pam")]
unsafe extern "C" {
    fn pam_start(service: *const c_char, user: *const c_char, conv: *const PamConv, pamh: *mut *mut PamHandle) -> c_int;
    fn pam_start_confdir(service: *const c_char, user: *const c_char, conv: *const PamConv, confdir: *const c_char, pamh: *mut *mut PamHandle) -> c_int;
    fn pam_authenticate(pamh: *mut PamHandle, flags: c_int) -> c_int;
    fn pam_acct_mgmt(pamh: *mut PamHandle, flags: c_int) -> c_int;
    fn pam_end(pamh: *mut PamHandle, status: c_int) -> c_int;
    fn pam_strerror(pamh: *mut PamHandle, errnum: c_int) -> *const c_char;
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Result {
    pub success: bool,
    pub message: String,
}

/// The directory holding the `astralia-shell` PAM service file: the installed copy when the file is there, else the source tree.
pub fn service_dir() -> String {
    service_dir_from(INSTALLED_DIR)
}

fn service_dir_from(installed: &str) -> String {
    if std::path::Path::new(installed).join(SERVICE).is_file() {
        return installed.to_string();
    }
    format!("{}/assets/pam", env!("CARGO_MANIFEST_DIR"))
}

/// A NUL-terminated copy of the password that zeroes itself on drop.
struct SecretCStr(Vec<u8>);

impl SecretCStr {
    fn new(password: &str) -> SecretCStr {
        let mut bytes = Vec::with_capacity(password.len() + 1);
        bytes.extend(password.bytes().filter(|&b| b != 0));
        bytes.push(0);
        SecretCStr(bytes)
    }
}

impl Drop for SecretCStr {
    fn drop(&mut self) {
        // SAFETY: the pointer covers the vector's own initialised bytes.
        unsafe { libc::explicit_bzero(self.0.as_mut_ptr().cast(), self.0.len()) };
    }
}

/// A `malloc` copy PAM frees itself; null input gives an empty string.
unsafe fn dup_cstr(s: *const c_char) -> *mut c_char {
    let s = if s.is_null() { c"".as_ptr() } else { s };
    // SAFETY: `s` is a valid NUL-terminated string.
    unsafe { libc::strdup(s) }
}

unsafe extern "C" fn conversation(num_msg: c_int, msg: *mut *const PamMessage, resp: *mut *mut PamResponse, appdata: *mut c_void) -> c_int {
    if num_msg <= 0 || msg.is_null() || resp.is_null() || appdata.is_null() {
        return PAM_CONV_ERR;
    }
    let password = appdata as *const c_char;
    let count = num_msg as usize;
    // SAFETY: PAM owns the replies array and frees it with `free`.
    let replies = unsafe { libc::calloc(count, std::mem::size_of::<PamResponse>()) } as *mut PamResponse;
    if replies.is_null() {
        return PAM_BUF_ERR;
    }
    for i in 0..count {
        // SAFETY: PAM passes `num_msg` message pointers.
        let m = unsafe { *msg.add(i) };
        if m.is_null() {
            for j in 0..i {
                // SAFETY: the earlier replies were filled by this loop.
                unsafe { free_reply(&mut *replies.add(j)) };
            }
            // SAFETY: allocated above with `calloc`.
            unsafe { libc::free(replies.cast()) };
            return PAM_CONV_ERR;
        }
        // SAFETY: `m` is non-null and `i` is inside the allocation.
        unsafe {
            (*replies.add(i)).resp = match (*m).msg_style {
                PAM_PROMPT_ECHO_OFF => dup_cstr(password),
                PAM_PROMPT_ECHO_ON => dup_cstr(ptr::null()),
                _ => ptr::null_mut(),
            };
        }
    }
    // SAFETY: `resp` is non-null.
    unsafe { *resp = replies };
    PAM_SUCCESS
}

/// Zeroes and frees one reply string.
unsafe fn free_reply(r: &mut PamResponse) {
    if !r.resp.is_null() {
        // SAFETY: `resp` is a NUL-terminated `malloc` string.
        unsafe {
            libc::explicit_bzero(r.resp.cast(), libc::strlen(r.resp));
            libc::free(r.resp.cast());
        }
        r.resp = ptr::null_mut();
    }
}

fn pam_config_present(pam_dir: &str) -> bool {
    if pam_dir.is_empty() {
        return false;
    }
    let Ok(path) = CString::new(format!("{pam_dir}/{SERVICE}")) else { return false };
    // SAFETY: `path` is NUL-terminated.
    unsafe { libc::access(path.as_ptr(), libc::R_OK) == 0 }
}

/// Checks `password` for `user`. Blocks for as long as PAM does; run it off the loop thread.
pub fn authenticate(user: &str, password: &str, pam_dir: &str) -> Result {
    if user.is_empty() || user == "unknown" {
        return Result { success: false, message: "could not determine current user".into() };
    }
    let (Ok(user_c), Ok(service_c)) = (CString::new(user), CString::new(SERVICE)) else {
        return Result { success: false, message: "could not determine current user".into() };
    };
    let secret = SecretCStr::new(password);
    let conv = PamConv { conv: conversation, appdata_ptr: secret.0.as_ptr() as *mut c_void };

    let mut pamh: *mut PamHandle = ptr::null_mut();
    let rc = match CString::new(pam_dir) {
        Ok(dir_c) if pam_config_present(pam_dir) => {
            // SAFETY: every pointer is valid for the call; `conv` and `secret` outlive `pamh`.
            unsafe { pam_start_confdir(service_c.as_ptr(), user_c.as_ptr(), &conv, dir_c.as_ptr(), &mut pamh) }
        }
        // SAFETY: as above.
        _ => unsafe { pam_start(c"login".as_ptr(), user_c.as_ptr(), &conv, &mut pamh) },
    };
    if rc != PAM_SUCCESS || pamh.is_null() {
        log::error!("lock: pam_start failed rc={rc}");
        return Result { success: false, message: "authentication unavailable".into() };
    }

    // SAFETY: `pamh` came from a successful `pam_start`.
    let mut rc = unsafe { pam_authenticate(pamh, 0) };
    if rc == PAM_SUCCESS {
        // SAFETY: as above.
        let acct = unsafe { pam_acct_mgmt(pamh, 0) };
        if acct != PAM_SUCCESS && acct != PAM_AUTHINFO_UNAVAIL {
            rc = acct;
        }
    }

    let mut message = String::new();
    if rc != PAM_SUCCESS {
        // SAFETY: as above; the returned string is static or owned by PAM.
        let err = unsafe { pam_strerror(pamh, rc) };
        message = if err.is_null() { "authentication failed".into() } else { unsafe { CStr::from_ptr(err) }.to_string_lossy().into_owned() };
    }
    // SAFETY: as above; the handle is not used again.
    unsafe { pam_end(pamh, rc) };
    drop(secret);
    Result { success: rc == PAM_SUCCESS, message }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pam_conversation_replies() {
        let secret = SecretCStr::new("pw");
        let msgs = [
            PamMessage { msg_style: PAM_PROMPT_ECHO_OFF, msg: c"Password: ".as_ptr() },
            PamMessage { msg_style: PAM_PROMPT_ECHO_ON, msg: c"Login: ".as_ptr() },
            PamMessage { msg_style: 4, msg: c"info".as_ptr() },
        ];
        let mut ptrs: Vec<*const PamMessage> = msgs.iter().map(|m| m as *const _).collect();
        let mut resp: *mut PamResponse = ptr::null_mut();
        let rc = unsafe { conversation(3, ptrs.as_mut_ptr(), &mut resp, secret.0.as_ptr() as *mut c_void) };
        assert_eq!(rc, PAM_SUCCESS);
        unsafe {
            assert_eq!(CStr::from_ptr((*resp).resp).to_bytes(), b"pw");
            assert_eq!(CStr::from_ptr((*resp.add(1)).resp).to_bytes(), b"");
            assert!((*resp.add(2)).resp.is_null());
            for i in 0..3 {
                free_reply(&mut *resp.add(i));
            }
            libc::free(resp.cast());
        }

        let mut ptrs = [msgs.as_ptr(), ptr::null()];
        let mut resp: *mut PamResponse = ptr::null_mut();
        let rc = unsafe { conversation(2, ptrs.as_mut_ptr(), &mut resp, secret.0.as_ptr() as *mut c_void) };
        assert_eq!(rc, PAM_CONV_ERR, "a null message is a conversation error");
        assert!(resp.is_null());
        let rc = unsafe { conversation(0, ptrs.as_mut_ptr(), &mut resp, secret.0.as_ptr() as *mut c_void) };
        assert_eq!(rc, PAM_CONV_ERR);
    }

    #[test]
    fn pam_unknown_user() {
        for user in ["", "unknown"] {
            let r = authenticate(user, "pw", "/nonexistent");
            assert!(!r.success);
            assert_eq!(r.message, "could not determine current user");
        }
    }

    #[test]
    fn pam_service_dir() {
        assert_eq!(service_dir_from("/nonexistent/astralia-shell/pam"), format!("{}/assets/pam", env!("CARGO_MANIFEST_DIR")));
        assert_eq!(service_dir_from("/"), format!("{}/assets/pam", env!("CARGO_MANIFEST_DIR")), "a directory without the service file is skipped");
        let dir = tempfile::tempdir().unwrap();
        std::fs::write(dir.path().join(SERVICE), "").unwrap();
        let installed = dir.path().to_str().unwrap();
        assert_eq!(service_dir_from(installed), installed, "the installed service file wins");
        assert_eq!(service_dir(), service_dir_from(INSTALLED_DIR));
        assert!(pam_config_present(&service_dir_from("/nonexistent")), "the source tree ships the service file");
        assert!(!pam_config_present(""));
    }
}
