//! Allocation-free TB-303 pattern storage, sample clock and monophonic playback.
#![no_std]

mod clock;
pub mod edit;
mod model;
mod playback;
pub use clock::*;
pub use model::*;
pub use playback::*;

/// Preparation error shared with the circuit stages.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum StageError {
    /// Rate must be finite and in the supported 1 kHz–1.536 MHz range.
    InvalidSampleRate,
}
