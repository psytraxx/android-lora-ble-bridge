//! ESP32-S3 LoRa-BLE Bridge Library
//!
//! This library provides modules for BLE communication, LoRa radio operations,
//! and protocol handling for message exchange between ESP32 devices.

#![no_std]

extern crate alloc;

pub mod ble;
pub mod lora;
/// Protocol definitions for LoRa messages between ESP32 devices.
pub mod protocol;
