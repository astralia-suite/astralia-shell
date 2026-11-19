#[derive(Debug, PartialEq, Eq)]
pub enum Invocation {
    Daemon,
    Debug,
    Client(String),
}

pub fn parse(args: &[String]) -> Invocation {
    match args {
        [] => Invocation::Daemon,
        [arg] if arg == "debug" => Invocation::Debug,
        _ => Invocation::Client(args.join(" ")),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_modes() {
        assert_eq!(parse(&[]), Invocation::Daemon);
        assert_eq!(parse(&["debug".into()]), Invocation::Debug);
        assert_eq!(parse(&["launcher".into(), "global".into()]), Invocation::Client("launcher global".into()));
        assert_eq!(parse(&["debug".into(), "x".into()]), Invocation::Client("debug x".into()));
    }
}
