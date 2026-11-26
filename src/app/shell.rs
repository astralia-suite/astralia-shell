use std::rc::Rc;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ShellVerb {
    Launcher,
    LauncherGlobal,
    Logout,
    Overview,
    Settings,
    Wallpaper,
    Rain,
    Bamboo,
    Visualizer,
    Lock,
}

impl ShellVerb {
    pub const ALL: [ShellVerb; 10] = [
        ShellVerb::Launcher,
        ShellVerb::LauncherGlobal,
        ShellVerb::Logout,
        ShellVerb::Overview,
        ShellVerb::Settings,
        ShellVerb::Wallpaper,
        ShellVerb::Rain,
        ShellVerb::Bamboo,
        ShellVerb::Visualizer,
        ShellVerb::Lock,
    ];

    pub fn name(self) -> &'static str {
        match self {
            ShellVerb::Launcher => "launcher",
            ShellVerb::LauncherGlobal => "launcher global",
            ShellVerb::Logout => "logout",
            ShellVerb::Overview => "overview",
            ShellVerb::Settings => "settings",
            ShellVerb::Wallpaper => "wallpaper",
            ShellVerb::Rain => "rain",
            ShellVerb::Bamboo => "bamboo",
            ShellVerb::Visualizer => "visualizer",
            ShellVerb::Lock => "lock",
        }
    }

    pub fn description(self) -> &'static str {
        match self {
            ShellVerb::Launcher => "toggle the launcher, searching from $HOME",
            ShellVerb::LauncherGlobal => "toggle the launcher, searching from /",
            ShellVerb::Logout => "toggle the logout overlay",
            ShellVerb::Overview => "toggle the overview",
            ShellVerb::Settings => "toggle the settings overlay",
            ShellVerb::Wallpaper => "toggle the wallpaper selector",
            ShellVerb::Rain => "toggle the rain overlay",
            ShellVerb::Bamboo => "toggle the bamboo overlay",
            ShellVerb::Visualizer => "toggle the audio visualizer overlay",
            ShellVerb::Lock => "lock the session",
        }
    }
}

type Binding<C> = Rc<dyn Fn(&mut C)>;
type AfterHook<C> = Rc<dyn Fn(&mut C, ShellVerb)>;
type IsOpen<C> = Box<dyn Fn(&C) -> bool>;

/// The verb table of a running shell, over the loop data `C` the modules live in.
pub struct Shell<C> {
    bindings: [Option<Binding<C>>; ShellVerb::ALL.len()],
    tracked: Vec<(String, IsOpen<C>)>,
    after: Option<AfterHook<C>>,
}

/// Loop data that may carry a shell; IPC verbs reach the modules through it.
pub trait Host: Sized {
    fn shell(&self) -> Option<&Shell<Self>> {
        None
    }
}

impl<C> Shell<C> {
    pub fn new() -> Shell<C> {
        Shell { bindings: Default::default(), tracked: Vec::new(), after: None }
    }

    pub fn bind(&mut self, verb: ShellVerb, run: impl Fn(&mut C) + 'static) {
        self.bindings[verb as usize] = Some(Rc::new(run));
    }

    pub fn bound(&self, verb: ShellVerb) -> bool {
        self.bindings[verb as usize].is_some()
    }

    pub fn bound_verbs(&self) -> Vec<ShellVerb> {
        ShellVerb::ALL.into_iter().filter(|&v| self.bound(v)).collect()
    }

    /// Runs after every verb, with the verb that ran.
    pub fn after_verb(&mut self, hook: impl Fn(&mut C, ShellVerb) + 'static) {
        self.after = Some(Rc::new(hook));
    }

    pub fn track(&mut self, name: &str, is_open: impl Fn(&C) -> bool + 'static) {
        self.tracked.push((name.to_string(), Box::new(is_open)));
    }

    #[cfg(test)]
    pub fn any_open(&self, ctx: &C) -> bool {
        self.tracked.iter().any(|(_, is_open)| is_open(ctx))
    }

    pub fn open_modules(&self, ctx: &C) -> Vec<String> {
        self.tracked.iter().filter(|(_, is_open)| is_open(ctx)).map(|(name, _)| name.clone()).collect()
    }

    /// The `status` answer: the backend and the tracked modules open now.
    pub fn status(&self, ctx: &C) -> String {
        let open = self.open_modules(ctx);
        status(&open.iter().map(String::as_str).collect::<Vec<_>>())
    }
}

impl<C: Host> Shell<C> {
    /// Runs the binding of `verb` on `ctx`, then the after hook; false when the verb is unbound.
    pub fn run(ctx: &mut C, verb: ShellVerb) -> bool {
        let Some(shell) = ctx.shell() else { return false };
        let Some(run) = shell.bindings[verb as usize].clone() else { return false };
        let after = shell.after.clone();
        run(ctx);
        if let Some(after) = after {
            after(ctx, verb);
        }
        true
    }
}

/// The `astralia modules` tables.
pub fn report() -> String {
    let mut text = String::from("| Module |\n| --- |\n");
    for name in [
        "bar", "launcher", "logout", "overview", "settings", "notification", "osd", "polkit", "wallpaper", "bamboo", "lock", "idle", "rain",
        "visualizer",
    ] {
        text += &format!("| `{name}` |\n");
    }
    text += "\n| Verb | Description |\n| --- | --- |\n";
    for verb in ShellVerb::ALL {
        text += &format!("| `{}` | {} |\n", verb.name(), verb.description());
    }
    text
}

fn list_or_none(names: &[&str]) -> String {
    if names.is_empty() { "none".into() } else { names.join(" ") }
}

pub fn status(open: &[&str]) -> String {
    format!("backend: wayland\nopen: {}\n", list_or_none(open))
}

pub struct Builtin {
    pub verb: &'static str,
    pub description: &'static str,
}

pub const BUILTINS: [Builtin; 3] = [
    Builtin { verb: "help", description: "list the available verbs" },
    Builtin { verb: "kill", description: "gracefully quit astralia" },
    Builtin { verb: "status", description: "show the backend and the open modules" },
];

pub fn help(entries: &[(&str, &str)]) -> String {
    let mut sorted = entries.to_vec();
    sorted.sort_by_key(|&(verb, _)| verb);
    let width = sorted.iter().map(|(verb, _)| verb.len()).max().unwrap_or(0);
    let mut text = String::from("astralia <verb>:\n");
    for (verb, description) in sorted {
        text += &format!("  {verb:<width$}  {description}\n");
    }
    text
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashSet;

    #[test]
    fn verb_names_unique() {
        assert_eq!(ShellVerb::ALL.len(), 10);
        let names: HashSet<_> = ShellVerb::ALL.iter().map(|v| v.name()).collect();
        assert_eq!(names.len(), 10);
        assert_eq!(ShellVerb::LauncherGlobal.name(), "launcher global");
    }

    #[test]
    fn status_text() {
        assert_eq!(status(&["overview", "launcher"]), "backend: wayland\nopen: overview launcher\n");
        assert_eq!(status(&[]), "backend: wayland\nopen: none\n");
    }

    #[test]
    fn report_rows() {
        let text = report();
        assert!(text.contains("| `rain` |"));
        assert!(text.contains("| `overview` | toggle the overview |"));
    }

    struct Fake {
        shell: Shell<Fake>,
        calls: i32,
        after: i32,
        launcher_open: bool,
        logout_open: bool,
    }

    impl Host for Fake {
        fn shell(&self) -> Option<&Shell<Fake>> {
            Some(&self.shell)
        }
    }

    fn fake() -> Fake {
        Fake { shell: Shell::new(), calls: 0, after: 0, launcher_open: false, logout_open: false }
    }

    #[test]
    fn shell_bindings() {
        let mut f = fake();
        f.shell.after_verb(|f: &mut Fake, _| f.after += 1);
        f.shell.bind(ShellVerb::Overview, |f: &mut Fake| f.calls += 1);
        assert!(f.shell.bound(ShellVerb::Overview), "a verb binds");
        assert!(Shell::run(&mut f, ShellVerb::Overview) && f.calls == 1 && f.after == 1, "running calls the module and the hook");
        assert!(!Shell::run(&mut f, ShellVerb::Logout) && f.after == 1, "an unbound verb does nothing");

        f.shell.bind(ShellVerb::Logout, |f: &mut Fake| f.calls += 10);
        assert_eq!(f.shell.bound_verbs(), vec![ShellVerb::Logout, ShellVerb::Overview], "bound verbs come in verb order");
        assert!(Shell::run(&mut f, ShellVerb::Logout) && f.calls == 11, "a verb runs its binding");

        f.shell.track("overview", |f: &Fake| f.logout_open);
        f.shell.track("launcher", |_: &Fake| true);
        assert!(f.shell.any_open(&f) && f.shell.open_modules(&f) == ["launcher"], "open modules are listed");
        f.logout_open = true;
        assert_eq!(f.shell.open_modules(&f).len(), 2, "a module that opens is listed too");
    }

    #[test]
    fn verbs_close_other_overlays() {
        let mut f = fake();
        f.shell.bind(ShellVerb::Launcher, |f: &mut Fake| f.launcher_open = !f.launcher_open);
        f.shell.bind(ShellVerb::Logout, |f: &mut Fake| f.logout_open = !f.logout_open);
        f.shell.after_verb(|f: &mut Fake, verb| {
            if verb != ShellVerb::Launcher {
                f.launcher_open = false;
            }
            if verb != ShellVerb::Logout {
                f.logout_open = false;
            }
        });
        Shell::run(&mut f, ShellVerb::Launcher);
        assert!(f.launcher_open && !f.logout_open);
        Shell::run(&mut f, ShellVerb::Logout);
        assert!(f.logout_open && !f.launcher_open, "opening the logout closes the launcher");
        Shell::run(&mut f, ShellVerb::Logout);
        assert!(!f.logout_open && !f.launcher_open, "a second verb closes its own overlay");
    }

    #[test]
    fn status_lists_open_overlays() {
        let mut f = fake();
        f.shell.track("settings", |f: &Fake| f.launcher_open);
        f.shell.track("logout", |f: &Fake| f.logout_open);
        assert!(f.shell.status(&f).ends_with("open: none\n"), "nothing open, nothing listed");
        f.launcher_open = true;
        assert!(f.shell.status(&f).ends_with("open: settings\n"), "an open overlay is listed");
        f.logout_open = true;
        assert_eq!(f.shell.status(&f).lines().last(), Some("open: settings logout"), "in the order they were tracked");
    }

    #[test]
    fn help_sorted_and_padded() {
        assert_eq!(
            help(&[("status", "s"), ("kill", "k"), ("help", "h")]),
            "astralia <verb>:\n  help    h\n  kill    k\n  status  s\n"
        );
    }
}
