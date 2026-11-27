//! Small bounded control-thread JSON codec; no additional registry dependency.
use std::collections::BTreeMap;
#[derive(Clone, Debug, PartialEq)]
pub(crate) enum Value {
    Null,
    Bool(bool),
    Number(f64),
    String(String),
    Array(Vec<Value>),
    Object(BTreeMap<String, Value>),
}
impl Value {
    pub fn as_object(&self) -> Option<&BTreeMap<String, Value>> {
        if let Self::Object(v) = self {
            Some(v)
        } else {
            None
        }
    }
    pub fn as_array(&self) -> Option<&[Value]> {
        if let Self::Array(v) = self {
            Some(v)
        } else {
            None
        }
    }
    pub fn as_str(&self) -> Option<&str> {
        if let Self::String(v) = self {
            Some(v)
        } else {
            None
        }
    }
    pub fn as_f64(&self) -> Option<f64> {
        if let Self::Number(v) = self {
            Some(*v)
        } else {
            None
        }
    }
    pub fn as_u64(&self) -> Option<u64> {
        self.as_f64()
            .filter(|n| *n >= 0. && *n < 18446744073709551616. && n.fract() == 0.)
            .map(|n| n as u64)
    }
}
impl std::ops::Index<&str> for Value {
    type Output = Value;
    fn index(&self, key: &str) -> &Value {
        self.as_object()
            .and_then(|o| o.get(key))
            .unwrap_or(&Value::Null)
    }
}
impl PartialEq<&str> for Value {
    fn eq(&self, s: &&str) -> bool {
        self.as_str() == Some(*s)
    }
}
impl From<&str> for Value {
    fn from(s: &str) -> Self {
        Self::String(s.into())
    }
}
impl From<String> for Value {
    fn from(s: String) -> Self {
        Self::String(s)
    }
}
impl From<bool> for Value {
    fn from(s: bool) -> Self {
        Self::Bool(s)
    }
}
impl From<f32> for Value {
    fn from(s: f32) -> Self {
        Self::Number(f64::from(s))
    }
}
impl From<u8> for Value {
    fn from(s: u8) -> Self {
        Self::Number(f64::from(s))
    }
}
pub(crate) fn object<const N: usize>(fields: [(&str, Value); N]) -> Value {
    Value::Object(fields.into_iter().map(|(k, v)| (k.into(), v)).collect())
}
impl std::fmt::Display for Value {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Null => write!(f, "null"),
            Self::Bool(b) => write!(f, "{b}"),
            Self::Number(n) => {
                if n.is_finite() {
                    write!(f, "{n}")
                } else {
                    write!(f, "null")
                }
            }
            Self::String(s) => {
                write!(f, "\"")?;
                for c in s.chars() {
                    match c {
                        '"' => write!(f, "\\\"")?,
                        '\\' => write!(f, "\\\\")?,
                        c if c < ' ' => write!(f, "\\u{:04x}", c as u32)?,
                        c => write!(f, "{c}")?,
                    }
                }
                write!(f, "\"")
            }
            Self::Array(a) => {
                write!(f, "[")?;
                for (i, v) in a.iter().enumerate() {
                    if i > 0 {
                        write!(f, ",")?
                    }
                    write!(f, "{v}")?
                }
                write!(f, "]")
            }
            Self::Object(o) => {
                write!(f, "{{")?;
                for (i, (k, v)) in o.iter().enumerate() {
                    if i > 0 {
                        write!(f, ",")?
                    }
                    write!(f, "{}:{v}", Self::String(k.clone()))?
                }
                write!(f, "}}")
            }
        }
    }
}
pub(crate) fn parse(text: &str) -> Result<Value, &'static str> {
    if text.len() > 4096 {
        return Err("Message too large");
    }
    let mut p = Parser { text, pos: 0 };
    let v = p.value(0)?;
    p.space();
    if p.pos != text.len() {
        return Err("Trailing JSON data");
    }
    Ok(v)
}
struct Parser<'a> {
    text: &'a str,
    pos: usize,
}
impl Parser<'_> {
    fn peek(&self) -> Option<u8> {
        self.text.as_bytes().get(self.pos).copied()
    }
    fn space(&mut self) {
        while self
            .peek()
            .is_some_and(|b| matches!(b, b' ' | b'\n' | b'\r' | b'\t'))
        {
            self.pos += 1
        }
    }
    fn take(&mut self, b: u8) -> Result<(), &'static str> {
        self.space();
        if self.peek() == Some(b) {
            self.pos += 1;
            Ok(())
        } else {
            Err("Invalid JSON")
        }
    }
    fn string(&mut self) -> Result<String, &'static str> {
        self.take(b'"')?;
        let mut s = String::new();
        loop {
            let c = self.text[self.pos..]
                .chars()
                .next()
                .ok_or("Unterminated string")?;
            self.pos += c.len_utf8();
            match c {
                '"' => return Ok(s),
                '\\' => {
                    let b = self.peek().ok_or("Invalid escape")?;
                    self.pos += 1;
                    s.push(match b {
                        b'"' => '"',
                        b'\\' => '\\',
                        b'/' => '/',
                        b'b' => '\u{8}',
                        b'f' => '\u{c}',
                        b'n' => '\n',
                        b'r' => '\r',
                        b't' => '\t',
                        b'u' => {
                            let first = self.hex()?;
                            let code = if (0xd800..=0xdbff).contains(&first) {
                                if !self.text[self.pos..].starts_with("\\u") {
                                    return Err("Invalid surrogate");
                                }
                                self.pos += 2;
                                let low = self.hex()?;
                                if !(0xdc00..=0xdfff).contains(&low) {
                                    return Err("Invalid surrogate");
                                }
                                0x10000 + ((first - 0xd800) << 10) + (low - 0xdc00)
                            } else {
                                first
                            };
                            char::from_u32(code).ok_or("Invalid Unicode")?
                        }
                        _ => return Err("Invalid escape"),
                    })
                }
                c if c < ' ' => return Err("Control in string"),
                c => s.push(c),
            }
        }
    }
    fn hex(&mut self) -> Result<u32, &'static str> {
        let bytes = self
            .text
            .as_bytes()
            .get(self.pos..self.pos + 4)
            .ok_or("Invalid Unicode")?;
        if !bytes.iter().all(u8::is_ascii_hexdigit) {
            return Err("Invalid Unicode");
        }
        let v = u32::from_str_radix(&self.text[self.pos..self.pos + 4], 16)
            .map_err(|_| "Invalid Unicode")?;
        self.pos += 4;
        Ok(v)
    }
    fn value(&mut self, depth: usize) -> Result<Value, &'static str> {
        if depth > 8 {
            return Err("JSON nesting too deep");
        }
        self.space();
        match self.peek() {
            Some(b'"') => Ok(Value::String(self.string()?)),
            Some(b'{') => {
                self.pos += 1;
                self.space();
                let mut o = BTreeMap::new();
                if self.peek() == Some(b'}') {
                    self.pos += 1;
                    return Ok(Value::Object(o));
                }
                loop {
                    let k = self.string()?;
                    self.take(b':')?;
                    let v = self.value(depth + 1)?;
                    if o.insert(k, v).is_some() {
                        return Err("Duplicate JSON field");
                    }
                    self.space();
                    if self.peek() == Some(b'}') {
                        self.pos += 1;
                        break;
                    }
                    self.take(b',')?;
                }
                Ok(Value::Object(o))
            }
            Some(b'[') => {
                self.pos += 1;
                self.space();
                let mut a = Vec::new();
                if self.peek() == Some(b']') {
                    self.pos += 1;
                    return Ok(Value::Array(a));
                }
                loop {
                    a.push(self.value(depth + 1)?);
                    self.space();
                    if self.peek() == Some(b']') {
                        self.pos += 1;
                        break;
                    }
                    self.take(b',')?;
                }
                Ok(Value::Array(a))
            }
            Some(b't' | b'f' | b'n') => {
                for (s, v) in [
                    ("true", Value::Bool(true)),
                    ("false", Value::Bool(false)),
                    ("null", Value::Null),
                ] {
                    if self.text[self.pos..].starts_with(s) {
                        self.pos += s.len();
                        return Ok(v);
                    }
                }
                Err("Invalid literal")
            }
            Some(b'-' | b'0'..=b'9') => {
                let start = self.pos;
                if self.peek() == Some(b'-') {
                    self.pos += 1
                }
                match self.peek() {
                    Some(b'0') => self.pos += 1,
                    Some(b'1'..=b'9') => {
                        while self.peek().is_some_and(|b| b.is_ascii_digit()) {
                            self.pos += 1
                        }
                    }
                    _ => return Err("Invalid number"),
                };
                if self.peek() == Some(b'.') {
                    self.pos += 1;
                    let begin = self.pos;
                    while self.peek().is_some_and(|b| b.is_ascii_digit()) {
                        self.pos += 1
                    }
                    if begin == self.pos {
                        return Err("Invalid fraction");
                    }
                }
                if matches!(self.peek(), Some(b'e' | b'E')) {
                    self.pos += 1;
                    if matches!(self.peek(), Some(b'+' | b'-')) {
                        self.pos += 1
                    }
                    let begin = self.pos;
                    while self.peek().is_some_and(|b| b.is_ascii_digit()) {
                        self.pos += 1
                    }
                    if begin == self.pos {
                        return Err("Invalid exponent");
                    }
                }
                let n = self.text[start..self.pos]
                    .parse::<f64>()
                    .map_err(|_| "Invalid number")?;
                if !n.is_finite() {
                    return Err("Nonfinite number");
                }
                Ok(Value::Number(n))
            }
            _ => Err("Invalid JSON"),
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn strict_roundtrip() {
        let v = parse(r#"{"name":"A\uD83C\uDFB9\n","n":120,"a":[true,null]}"#).unwrap();
        assert_eq!(parse(&v.to_string()).unwrap(), v);
        for s in ["01", "1.", "1e", "1e999", r#"{"a":1,"a":2}"#, r#""\uD800""#] {
            assert!(parse(s).is_err(), "{s}")
        }
    }
}
