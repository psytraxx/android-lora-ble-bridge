//! Independent Embassy tasks for hardware operations.
//!
//! This module provides autonomous tasks that communicate via channels instead of traits.
//! This reduces MessageRouter complexity by removing generic parameters.

pub mod battery_task;
pub mod ble_task;
pub mod led_task;
pub mod lora_task;
pub mod watchdog_task;

pub use battery_task::battery_task;
pub use led_task::{LedCommand, LedPattern, led_task};
pub use watchdog_task::watchdog_task;

#[cfg(target_arch = "xtensa")]
pub use lora_task::{RadioMetadata, RssiDbm, SnrDb};
