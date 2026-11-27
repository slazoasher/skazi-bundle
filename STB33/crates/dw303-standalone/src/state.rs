//! CLAP v2-compatible state and durable atomic replacement, outside audio.
use crate::Result;
use dw303_engine::{link::Memory, params};
use ravet_foundation::ParameterState;
use std::{
    io::Write,
    path::{Path, PathBuf},
};
const PRODUCT: [u8; 8] = *b"DJWDW303";
const PARAM_BYTES: usize = 16 + params::COUNT * 8;
pub(crate) const STATE_BYTES: usize = PARAM_BYTES + 4 + Memory::ENCODED_SIZE;
pub(crate) fn encode(values: [f32; params::COUNT], memory: &Memory) -> Result<Vec<u8>> {
    let mut bytes = vec![0; STATE_BYTES];
    params::schema()?
        .state(values)?
        .encode(PRODUCT, 2, &mut bytes[..PARAM_BYTES])?;
    bytes[PARAM_BYTES..PARAM_BYTES + 4]
        .copy_from_slice(&(Memory::ENCODED_SIZE as u32).to_le_bytes());
    memory
        .encode(&mut bytes[PARAM_BYTES + 4..])
        .map_err(|_| "Invalid memory")?;
    Ok(bytes)
}
pub(crate) fn decode(bytes: &[u8]) -> Result<([f32; params::COUNT], Memory)> {
    let schema = params::schema()?;
    if bytes.len() == PARAM_BYTES {
        return Ok((
            *ParameterState::decode(schema, PRODUCT, 1, bytes)?.values(),
            dw303_engine::factory_memory(),
        ));
    }
    if bytes.len() != STATE_BYTES
        || bytes[PARAM_BYTES..PARAM_BYTES + 4] != (Memory::ENCODED_SIZE as u32).to_le_bytes()
    {
        return Err("Invalid DW-303 state length".into());
    }
    Ok((
        *ParameterState::decode(schema, PRODUCT, 2, &bytes[..PARAM_BYTES])?.values(),
        Memory::decode(&bytes[PARAM_BYTES + 4..]).map_err(|_| "Invalid DW-303 memory")?,
    ))
}
/// Per-user state file: Application Support on macOS, `%APPDATA%` on Windows
/// and `$XDG_CONFIG_HOME` (or `~/.config`) elsewhere.
pub(crate) fn path() -> Result<PathBuf> {
    let home = || -> Result<PathBuf> {
        Ok(PathBuf::from(
            std::env::var_os("HOME").ok_or("HOME unavailable")?,
        ))
    };
    let directory = if cfg!(target_os = "macos") {
        home()?.join("Library/Application Support/DW-303")
    } else if cfg!(windows) {
        PathBuf::from(std::env::var_os("APPDATA").ok_or("APPDATA unavailable")?).join("DW-303")
    } else {
        match std::env::var_os("XDG_CONFIG_HOME").map(PathBuf::from) {
            Some(config) if config.is_absolute() => config,
            _ => home()?.join(".config"),
        }
        .join("dw-303")
    };
    Ok(directory.join("state.bin"))
}
pub(crate) fn read(path: &Path) -> Result<([f32; params::COUNT], Memory)> {
    use std::io::Read;
    let mut bytes = Vec::new();
    std::fs::File::open(path)?
        .take((STATE_BYTES + 1) as u64)
        .read_to_end(&mut bytes)?;
    decode(&bytes)
}
pub(crate) fn write(path: &Path, values: [f32; params::COUNT], memory: &Memory) -> Result<()> {
    let bytes = encode(values, memory)?;
    let parent = path.parent().ok_or("Missing state directory")?;
    std::fs::create_dir_all(parent)?;
    let temporary = parent.join(format!(".state-{}.tmp", std::process::id()));
    let result = (|| -> Result<()> {
        let mut file = std::fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&temporary)?;
        file.write_all(&bytes)?;
        file.sync_all()?;
        drop(file);
        std::fs::rename(&temporary, path)?;
        // Directories cannot be opened as files on Windows; NTFS journals the rename.
        #[cfg(unix)]
        std::fs::File::open(parent)?.sync_all()?;
        Ok(())
    })();
    if result.is_err() {
        let _ = std::fs::remove_file(&temporary);
    }
    result
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn codec_and_atomic_replacement() {
        let values = *params::schema().unwrap().defaults().values();
        let mut memory = dw303_engine::factory_memory();
        let bytes = encode(values, &memory).unwrap();
        assert_eq!(decode(&bytes).unwrap(), (values, memory.clone()));
        for length in [0, PARAM_BYTES - 1, STATE_BYTES - 1] {
            assert!(decode(&bytes[..length]).is_err())
        }
        let mut bad = bytes.clone();
        bad[PARAM_BYTES] = 0;
        assert!(decode(&bad).is_err());
        let mut v1 = vec![0; PARAM_BYTES];
        params::schema()
            .unwrap()
            .state(values)
            .unwrap()
            .encode(PRODUCT, 1, &mut v1)
            .unwrap();
        assert_eq!(decode(&v1).unwrap().1, dw303_engine::factory_memory());
        let dir = std::env::temp_dir().join(format!("dw303-state-test-{}", std::process::id()));
        let path = dir.join("state.bin");
        write(&path, values, &memory).unwrap();
        memory.bank.patterns[0].steps[0].accent = true;
        write(&path, values, &memory).unwrap();
        assert_eq!(read(&path).unwrap(), (values, memory));
        std::fs::remove_dir_all(dir).unwrap();
    }
}
