//! Dependency-free JSON reader for capture sidecars (control path only).
use std::collections::BTreeMap;

#[derive(Debug)]
pub(crate) enum Value {
    Number(f64),
    String(String),
    Array(Vec<Self>),
    Object(BTreeMap<String, Self>),
    Other,
}
impl Value {
    pub(crate) fn get(&self, key: &str) -> Result<&Self, String> {
        if let Self::Object(map) = self {
            map.get(key).ok_or_else(|| format!("missing {key}"))
        } else {
            Err("expected object".into())
        }
    }
    pub(crate) fn number(&self) -> Result<f64, String> {
        if let Self::Number(n) = self {
            Ok(*n)
        } else {
            Err("expected number".into())
        }
    }
    pub(crate) fn string(&self) -> Result<&str, String> {
        if let Self::String(s) = self {
            Ok(s)
        } else {
            Err("expected string".into())
        }
    }
    pub(crate) fn array(&self) -> Result<&[Self], String> {
        if let Self::Array(a) = self {
            Ok(a)
        } else {
            Err("expected array".into())
        }
    }
}
struct Parser<'a> {
    text: &'a str,
    pos: usize,
}
impl Parser<'_> {
    fn ws(&mut self) {
        while self.peek().is_some_and(|c| c.is_ascii_whitespace()) {
            self.pos += 1;
        }
    }
    fn peek(&self) -> Option<u8> {
        self.text.as_bytes().get(self.pos).copied()
    }
    fn take(&mut self, c: u8) -> Result<(), String> {
        self.ws();
        if self.peek() != Some(c) {
            return Err(format!("expected {} at {}", char::from(c), self.pos));
        }
        self.pos += 1;
        Ok(())
    }
    fn string(&mut self) -> Result<String, String> {
        self.take(b'"')?;
        let mut result = String::new();
        loop {
            let c = self.text[self.pos..]
                .chars()
                .next()
                .ok_or("unterminated string")?;
            self.pos += c.len_utf8();
            match c {
                '"' => return Ok(result),
                '\\' => {
                    let e = self.peek().ok_or("unterminated escape")?;
                    self.pos += 1;
                    result.push(match e {
                        b'"' => '"',
                        b'\\' => '\\',
                        b'/' => '/',
                        b'n' => '\n',
                        b'r' => '\r',
                        b't' => '\t',
                        b'b' => '\u{8}',
                        b'f' => '\u{c}',
                        b'u' => {
                            let end = self.pos + 4;
                            let hex = self
                                .text
                                .get(self.pos..end)
                                .ok_or("invalid unicode escape")?;
                            self.pos = end;
                            let first =
                                u16::from_str_radix(hex, 16).map_err(|_| "invalid unicode")?;
                            let code = if (0xd800..=0xdbff).contains(&first) {
                                self.take(b'\\')?;
                                self.take(b'u')?;
                                let end = self.pos + 4;
                                let hex =
                                    self.text.get(self.pos..end).ok_or("invalid surrogate")?;
                                self.pos = end;
                                let low = u16::from_str_radix(hex, 16)
                                    .map_err(|_| "invalid surrogate")?;
                                if !(0xdc00..=0xdfff).contains(&low) {
                                    return Err("invalid surrogate".into());
                                }
                                0x10000 + ((u32::from(first) - 0xd800) << 10) + u32::from(low)
                                    - 0xdc00
                            } else {
                                u32::from(first)
                            };
                            char::from_u32(code).ok_or("invalid unicode scalar")?
                        }
                        _ => return Err("invalid escape".into()),
                    });
                }
                c if c < ' ' => return Err("control character in string".into()),
                _ => result.push(c),
            }
        }
    }
    fn value(&mut self, depth: usize) -> Result<Value, String> {
        if depth > 64 {
            return Err("JSON nesting exceeds 64".into());
        }
        self.ws();
        match self.peek() {
            Some(b'"') => Ok(Value::String(self.string()?)),
            Some(b'[') => {
                self.pos += 1;
                self.ws();
                let mut a = Vec::new();
                if self.peek() != Some(b']') {
                    loop {
                        a.push(self.value(depth + 1)?);
                        self.ws();
                        if self.peek() == Some(b']') {
                            break;
                        }
                        self.take(b',')?;
                    }
                }
                self.take(b']')?;
                Ok(Value::Array(a))
            }
            Some(b'{') => {
                self.pos += 1;
                self.ws();
                let mut m = BTreeMap::new();
                if self.peek() != Some(b'}') {
                    loop {
                        let k = self.string()?;
                        self.take(b':')?;
                        if m.insert(k, self.value(depth + 1)?).is_some() {
                            return Err("duplicate JSON key".into());
                        }
                        self.ws();
                        if self.peek() == Some(b'}') {
                            break;
                        }
                        self.take(b',')?;
                    }
                }
                self.take(b'}')?;
                Ok(Value::Object(m))
            }
            Some(b't' | b'f' | b'n') => {
                for literal in ["true", "false", "null"] {
                    if self.text[self.pos..].starts_with(literal) {
                        self.pos += literal.len();
                        return Ok(Value::Other);
                    }
                }
                Err("invalid literal".into())
            }
            Some(b'-' | b'0'..=b'9') => {
                let start = self.pos;
                if self.peek() == Some(b'-') {
                    self.pos += 1;
                }
                match self.peek() {
                    Some(b'0') => {
                        self.pos += 1;
                    }
                    Some(b'1'..=b'9') => {
                        while self.peek().is_some_and(|c| c.is_ascii_digit()) {
                            self.pos += 1;
                        }
                    }
                    _ => return Err("invalid number integer".into()),
                }
                if self.peek() == Some(b'.') {
                    self.pos += 1;
                    let begin = self.pos;
                    while self.peek().is_some_and(|c| c.is_ascii_digit()) {
                        self.pos += 1;
                    }
                    if begin == self.pos {
                        return Err("missing fractional digits".into());
                    }
                }
                if matches!(self.peek(), Some(b'e' | b'E')) {
                    self.pos += 1;
                    if matches!(self.peek(), Some(b'+' | b'-')) {
                        self.pos += 1;
                    }
                    let begin = self.pos;
                    while self.peek().is_some_and(|c| c.is_ascii_digit()) {
                        self.pos += 1;
                    }
                    if begin == self.pos {
                        return Err("missing exponent digits".into());
                    }
                }
                let n: f64 = self.text[start..self.pos]
                    .parse()
                    .map_err(|_| "invalid number")?;
                if !n.is_finite() {
                    return Err("nonfinite JSON number".into());
                }
                Ok(Value::Number(n))
            }
            _ => Err(format!("invalid JSON at {}", self.pos)),
        }
    }
}
pub(crate) fn parse(text: &str) -> Result<Value, String> {
    let mut p = Parser { text, pos: 0 };
    let v = p.value(0)?;
    p.ws();
    if p.pos != text.len() {
        return Err("trailing JSON".into());
    }
    Ok(v)
}
#[cfg(test)]
mod tests {
    #[test]
    fn escaped_metadata_and_invalid_input() {
        assert_eq!(
            super::parse(r#"{"s":"\uD83D\uDE00\n"}"#)
                .unwrap()
                .get("s")
                .unwrap()
                .string()
                .unwrap(),
            "😀\n"
        );
        for text in [
            "{",
            "[1,]",
            "null trailing",
            "1e999",
            "01",
            "1.",
            "1e",
            "--1",
            "{\"a\":1,\"a\":2}",
        ] {
            assert!(super::parse(text).is_err());
        }
    }
}
