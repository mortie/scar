use regex::{self, Regex};

pub fn glob_to_regex_string(glob: &str) -> String {
    let mut rx: String = "^(?:./|/)?".into();

    let mut chars = glob.chars().peekable();

    if chars.peek().map(|x| *x) == Some('.') {
        let _ = chars.next();
        if chars.peek().map(|x| *x) == Some('/') {
            let _ = chars.next();
        } else {
            rx.push('.');
        }
    } else if chars.peek().map(|x| *x) == Some('/') {
        chars.next();
    }

    let mut prev = '\0';
    while let Some(ch) = chars.next() {
        if ch == '['
            || ch == ']'
            || ch == '\\'
            || ch == '|'
            || ch == '^'
            || ch == '$'
            || ch == '('
            || ch == ')'
            || ch == '.'
            || ch == '?'
        {
            rx.push('\\');
            rx.push(ch);
        } else if ch == '*' {
            if chars.peek().map(|x| *x) == Some('*') {
                let _ = chars.next();
                rx += ".*";
            } else {
                rx += "[^/]+";
            }
        } else if ch == '?' {
            rx.push('?');
        } else if ch == '/' && prev == '/' {
            // Drop this one
        } else {
            rx.push(ch);
        }

        prev = ch;
    }

    rx += "/?$";

    rx.push('$');
    rx
}

pub fn glob_to_regex(glob: &str) -> Result<Regex, regex::Error> {
    let rx = glob_to_regex_string(glob);
    Regex::new(rx.as_str())
}
