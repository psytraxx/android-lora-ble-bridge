//! NVS Storage Adapter - Persistent message storage using flash memory
//!
//! This adapter provides power-cycle-persistent storage using the ESP32's
//! NVS partition via the esp-storage crate. Unlike RTC memory, data stored
//! here survives complete power loss.
//!
//! Uses the partition table to safely access the NVS partition without
//! conflicting with application code or other system partitions.
//!
//! Storage layout within NVS partition (starting at offset 0x1000 to avoid
//! potential conflicts with esp-idf NVS key-value store at offset 0):
//! ```text
//! [Header 64B][Slot0 68B][Slot1 68B]...[Slot9 68B][Padding]
//! ```

use crate::constants::MAX_BUFFERED_MESSAGES;
use crate::ports::{Storage as StorageTrait, StorageError};
use crate::protocol::Message;
use embedded_storage::{ReadStorage, Storage};
use esp_bootloader_esp_idf::partitions::{self, DataPartitionSubType, PartitionType};
use esp_storage::FlashStorage;
use log::{debug, error, info, warn};

// Storage offset within NVS partition (skip first 4KB to avoid conflicts)
const STORAGE_OFFSET: u32 = 0x1000;

// Storage structure sizes
const HEADER_SIZE: usize = 64;
const SLOT_SIZE: usize = 68; // 1 valid + 1 length + 64 data + 2 CRC
const MAGIC: u32 = 0x4E565333; // "NVS3" - version for partition-based format

// Total storage size needed
const TOTAL_STORAGE_SIZE: usize = HEADER_SIZE + (SLOT_SIZE * MAX_BUFFERED_MESSAGES);

/// Cached message slot - stores serialized message data
#[derive(Clone, Copy)]
struct CachedSlot {
    valid: bool,
    len: usize,
    data: [u8; 64],
}

impl Default for CachedSlot {
    fn default() -> Self {
        Self {
            valid: false,
            len: 0,
            data: [0u8; 64],
        }
    }
}

/// NVS Storage Adapter - Flash-based persistent message storage
///
/// Implements a circular buffer in flash memory that survives power cycles.
/// All data is cached in RAM and persisted atomically.
pub struct NvsStorageAdapter<'a> {
    // Cached state in RAM
    head: usize,
    tail: usize,
    count: usize,
    slots: [CachedSlot; MAX_BUFFERED_MESSAGES],
    // Flash storage and partition info
    flash: FlashStorage<'a>,
    nvs_offset: u32, // Absolute offset of NVS partition in flash
    // Track if we need to persist (dirty flag)
    dirty: bool,
}

impl Default for NvsStorageAdapter<'_> {
    fn default() -> Self {
        panic!("Default not supported, use new() with flash peripheral");
    }
}

impl<'a> NvsStorageAdapter<'a> {
    /// Create a new NVS storage adapter
    ///
    /// Reads the partition table to find NVS partition, then loads existing
    /// state or initializes if empty/corrupted.
    pub fn new(flash_peripheral: esp_hal::peripherals::FLASH<'a>) -> Self {
        info!("[NVS] Initializing flash storage adapter...");

        let mut flash = FlashStorage::new(flash_peripheral);
        info!("[NVS] Flash capacity: {} bytes", flash.capacity());

        // Read partition table
        let mut pt_mem = [0u8; partitions::PARTITION_TABLE_MAX_LEN];
        let pt = match partitions::read_partition_table(&mut flash, &mut pt_mem) {
            Ok(pt) => {
                info!(
                    "[NVS] Partition table read successfully ({} partitions)",
                    pt.len()
                );
                pt
            }
            Err(e) => {
                error!("[NVS] FATAL: Failed to read partition table: {:?}", e);
                panic!("Cannot initialize storage without partition table");
            }
        };

        // Find NVS partition
        let nvs_partition = match pt.find_partition(PartitionType::Data(DataPartitionSubType::Nvs))
        {
            Ok(Some(p)) => {
                info!("[NVS] Found NVS partition");
                p
            }
            Ok(None) => {
                error!("[NVS] FATAL: NVS partition not found in partition table");
                error!("[NVS] Available partitions:");
                for i in 0..pt.len() {
                    if let Ok(p) = pt.get_partition(i) {
                        error!("[NVS]   {:?}", p);
                    }
                }
                panic!("NVS partition required for storage");
            }
            Err(e) => {
                error!("[NVS] FATAL: Error searching for NVS partition: {:?}", e);
                panic!("Cannot find NVS partition");
            }
        };

        let nvs_offset = nvs_partition.offset();
        let nvs_size = nvs_partition.len();

        info!(
            "[NVS] NVS partition: offset=0x{:08X}, size={} bytes ({}KB)",
            nvs_offset,
            nvs_size,
            nvs_size / 1024
        );
        info!(
            "[NVS] Storage area: offset=0x{:08X} (within NVS), size={} bytes",
            STORAGE_OFFSET, TOTAL_STORAGE_SIZE
        );

        // Verify partition is large enough
        if (nvs_size as usize) < STORAGE_OFFSET as usize + TOTAL_STORAGE_SIZE {
            error!(
                "[NVS] FATAL: NVS partition too small! Need {} bytes, have {} bytes",
                STORAGE_OFFSET as usize + TOTAL_STORAGE_SIZE,
                nvs_size
            );
            panic!("NVS partition too small");
        }

        let mut adapter = Self {
            head: 0,
            tail: 0,
            count: 0,
            slots: [CachedSlot::default(); MAX_BUFFERED_MESSAGES],
            flash,
            nvs_offset,
            dirty: false,
        };

        adapter.load_or_init();
        adapter
    }

    /// Get absolute flash address for our storage area
    fn storage_base(&self) -> u32 {
        self.nvs_offset + STORAGE_OFFSET
    }

    /// Load state from flash or initialize if invalid
    fn load_or_init(&mut self) {
        let mut header = [0u8; HEADER_SIZE];
        let base = self.storage_base();

        info!("[NVS] Reading header from flash address 0x{:08X}...", base);

        // Read header from flash
        match self.flash.read(base, &mut header) {
            Ok(_) => {
                debug!("[NVS] Header read successfully");
            }
            Err(e) => {
                error!("[NVS] Failed to read header from flash: {:?}", e);
                error!("[NVS] Initializing empty storage...");
                self.init_empty();
                return;
            }
        }

        let magic = u32::from_le_bytes([header[0], header[1], header[2], header[3]]);
        let crc_valid = Self::verify_header_crc(&header);

        debug!(
            "[NVS] Header: magic=0x{:08X} (expected 0x{:08X}), CRC valid={}",
            magic, MAGIC, crc_valid
        );

        if magic != MAGIC {
            info!(
                "[NVS] Magic mismatch (found 0x{:08X}, expected 0x{:08X}) - initializing fresh storage",
                magic, MAGIC
            );
            self.init_empty();
            return;
        }

        if !crc_valid {
            warn!("[NVS] Header CRC mismatch - storage may be corrupted, reinitializing");
            self.init_empty();
            return;
        }

        // Valid header - restore state
        self.head = u32::from_le_bytes([header[4], header[5], header[6], header[7]]) as usize;
        self.tail = u32::from_le_bytes([header[8], header[9], header[10], header[11]]) as usize;
        self.count = u32::from_le_bytes([header[12], header[13], header[14], header[15]]) as usize;

        debug!(
            "[NVS] Restored state: head={}, tail={}, count={}",
            self.head, self.tail, self.count
        );

        // Sanity check restored values
        if self.head >= MAX_BUFFERED_MESSAGES
            || self.tail >= MAX_BUFFERED_MESSAGES
            || self.count > MAX_BUFFERED_MESSAGES
        {
            error!(
                "[NVS] Corrupted state detected! head={}, tail={}, count={}, max={}",
                self.head, self.tail, self.count, MAX_BUFFERED_MESSAGES
            );
            warn!("[NVS] Reinitializing storage...");
            self.init_empty();
            return;
        }

        // Load all valid slots into RAM cache
        if self.count > 0 {
            info!(
                "[NVS] Loading {} buffered messages from flash...",
                self.count
            );
            self.load_slots_from_flash();
        }

        info!(
            "[NVS] Storage restored successfully: {} messages buffered",
            self.count
        );
    }

    /// Initialize empty state
    fn init_empty(&mut self) {
        info!("[NVS] Initializing empty storage (head=0, tail=0, count=0)");
        self.head = 0;
        self.tail = 0;
        self.count = 0;
        self.slots = [CachedSlot::default(); MAX_BUFFERED_MESSAGES];
        self.dirty = true;
        // Persist immediately to establish valid flash state
        self.persist_all();
        info!("[NVS] Empty storage initialized and persisted");
    }

    /// Load all slots from flash into RAM cache
    fn load_slots_from_flash(&mut self) {
        if self.count == 0 {
            return;
        }

        let base = self.storage_base();
        let mut idx = self.tail;
        let mut loaded = 0;
        let mut valid_count = 0;

        while loaded < self.count {
            let mut slot_buf = [0u8; SLOT_SIZE];
            let offset = base + HEADER_SIZE as u32 + (idx * SLOT_SIZE) as u32;

            match self.flash.read(offset, &mut slot_buf) {
                Ok(_) => {
                    let valid_flag = slot_buf[0];
                    let crc_ok = Self::verify_slot_crc(&slot_buf);

                    if valid_flag == 0x01 && crc_ok {
                        let len = slot_buf[1] as usize;
                        if len <= 64 {
                            self.slots[idx].valid = true;
                            self.slots[idx].len = len;
                            self.slots[idx].data[..len].copy_from_slice(&slot_buf[2..2 + len]);
                            valid_count += 1;
                            debug!("[NVS] Slot {} loaded: {} bytes", idx, len);
                        } else {
                            error!(
                                "[NVS] Slot {} has invalid length {} (max 64), marking invalid",
                                idx, len
                            );
                            self.slots[idx].valid = false;
                        }
                    } else {
                        error!(
                            "[NVS] Slot {} corrupted: valid_flag=0x{:02X}, CRC_ok={}",
                            idx, valid_flag, crc_ok
                        );
                        self.slots[idx].valid = false;
                    }
                }
                Err(e) => {
                    error!(
                        "[NVS] Failed to read slot {} from flash offset 0x{:08X}: {:?}",
                        idx, offset, e
                    );
                    self.slots[idx].valid = false;
                }
            }

            idx = (idx + 1) % MAX_BUFFERED_MESSAGES;
            loaded += 1;
        }

        info!(
            "[NVS] Loaded {}/{} slots successfully from flash",
            valid_count, self.count
        );
    }

    /// Persist all cached data to flash
    ///
    /// Uses the embedded_storage Storage trait which handles flash writes.
    /// We write the complete state as a contiguous block.
    fn persist_all(&mut self) {
        if !self.dirty {
            debug!("[NVS] persist_all called but not dirty, skipping");
            return;
        }

        let base = self.storage_base();
        debug!(
            "[NVS] Persisting storage to flash address 0x{:08X}...",
            base
        );

        // Build complete storage image in RAM
        let mut buffer = [0xFFu8; TOTAL_STORAGE_SIZE];

        // Header
        buffer[0..4].copy_from_slice(&MAGIC.to_le_bytes());
        buffer[4..8].copy_from_slice(&(self.head as u32).to_le_bytes());
        buffer[8..12].copy_from_slice(&(self.tail as u32).to_le_bytes());
        buffer[12..16].copy_from_slice(&(self.count as u32).to_le_bytes());
        let header_crc = Self::calculate_crc(&buffer[0..20]);
        buffer[20..22].copy_from_slice(&header_crc.to_le_bytes());

        // Slots
        let mut slots_written = 0;
        if self.count > 0 {
            let mut idx = self.tail;
            let mut written = 0;

            while written < self.count {
                if self.slots[idx].valid {
                    let slot_offset = HEADER_SIZE + idx * SLOT_SIZE;
                    buffer[slot_offset] = 0x01; // Valid flag
                    buffer[slot_offset + 1] = self.slots[idx].len as u8;
                    buffer[slot_offset + 2..slot_offset + 2 + self.slots[idx].len]
                        .copy_from_slice(&self.slots[idx].data[..self.slots[idx].len]);
                    let slot_crc = Self::calculate_crc(&buffer[slot_offset..slot_offset + 66]);
                    buffer[slot_offset + 66..slot_offset + 68]
                        .copy_from_slice(&slot_crc.to_le_bytes());
                    slots_written += 1;
                }

                idx = (idx + 1) % MAX_BUFFERED_MESSAGES;
                written += 1;
            }
        }

        // Write entire buffer to flash
        match self.flash.write(base, &buffer) {
            Ok(_) => {
                self.dirty = false;
                info!(
                    "[NVS] Persisted {} bytes to flash (header + {} slots, {} messages total)",
                    TOTAL_STORAGE_SIZE, slots_written, self.count
                );
            }
            Err(e) => {
                error!(
                    "[NVS] FAILED to write storage to flash at 0x{:08X}: {:?}",
                    base, e
                );
                error!("[NVS] Storage state may be inconsistent!");
            }
        }
    }

    /// Calculate CRC16-CCITT checksum
    fn calculate_crc(data: &[u8]) -> u16 {
        let mut crc: u16 = 0xFFFF;
        for byte in data {
            crc ^= (*byte as u16) << 8;
            for _ in 0..8 {
                if crc & 0x8000 != 0 {
                    crc = (crc << 1) ^ 0x1021;
                } else {
                    crc <<= 1;
                }
            }
        }
        crc
    }

    /// Verify header CRC
    fn verify_header_crc(header: &[u8]) -> bool {
        let stored_crc = u16::from_le_bytes([header[20], header[21]]);
        let calc_crc = Self::calculate_crc(&header[0..20]);
        stored_crc == calc_crc
    }

    /// Verify slot CRC
    fn verify_slot_crc(slot: &[u8]) -> bool {
        let stored_crc = u16::from_le_bytes([slot[66], slot[67]]);
        let calc_crc = Self::calculate_crc(&slot[0..66]);
        stored_crc == calc_crc
    }
}

impl<'a> StorageTrait for NvsStorageAdapter<'a> {
    fn add(&mut self, message: &Message) -> Result<(), StorageError> {
        info!("[NVS] Adding message to storage: {:?}", message);

        // Serialize message
        let mut buf = [0u8; 64];
        let len = match message.serialize(&mut buf) {
            Ok(len) => {
                debug!("[NVS] Message serialized: {} bytes", len);
                len
            }
            Err(e) => {
                error!("[NVS] Failed to serialize message: {}", e);
                return Err(StorageError::SerializationError);
            }
        };

        // If full, drop oldest (FIFO with drop-oldest policy)
        if self.count >= MAX_BUFFERED_MESSAGES {
            warn!(
                "[NVS] Storage full ({}/{}), dropping oldest message at slot {}",
                self.count, MAX_BUFFERED_MESSAGES, self.tail
            );
            self.slots[self.tail].valid = false;
            self.tail = (self.tail + 1) % MAX_BUFFERED_MESSAGES;
            self.count -= 1;
        }

        // Add to RAM cache
        self.slots[self.head].valid = true;
        self.slots[self.head].len = len;
        self.slots[self.head].data[..len].copy_from_slice(&buf[..len]);

        let added_slot = self.head;

        // Update state
        self.head = (self.head + 1) % MAX_BUFFERED_MESSAGES;
        self.count += 1;
        self.dirty = true;

        info!(
            "[NVS] Message added to slot {}, count now {}/{}",
            added_slot, self.count, MAX_BUFFERED_MESSAGES
        );

        // Persist to flash
        self.persist_all();

        Ok(())
    }

    fn peek(&mut self) -> Result<Option<Message>, StorageError> {
        if self.count == 0 {
            debug!("[NVS] peek: storage is empty");
            return Ok(None);
        }

        // Read from RAM cache
        let slot = &self.slots[self.tail];
        if !slot.valid {
            error!(
                "[NVS] peek: slot {} marked invalid in cache (data corruption?)",
                self.tail
            );
            return Err(StorageError::StorageError);
        }

        debug!(
            "[NVS] peek: reading message from slot {} ({} bytes)",
            self.tail, slot.len
        );

        // Deserialize message
        match Message::deserialize(&slot.data[..slot.len]) {
            Ok(msg) => {
                debug!("[NVS] peek: successfully deserialized message: {:?}", msg);
                Ok(Some(msg))
            }
            Err(e) => {
                error!(
                    "[NVS] peek: failed to deserialize message from slot {}: {}",
                    self.tail, e
                );
                Err(StorageError::SerializationError)
            }
        }
    }

    fn pop(&mut self) -> Result<(), StorageError> {
        if self.count == 0 {
            warn!("[NVS] pop: cannot pop from empty storage");
            return Err(StorageError::Empty);
        }

        let popped_slot = self.tail;

        // Invalidate slot in cache
        self.slots[self.tail].valid = false;

        // Advance tail
        self.tail = (self.tail + 1) % MAX_BUFFERED_MESSAGES;
        self.count -= 1;
        self.dirty = true;

        info!(
            "[NVS] Popped message from slot {}, count now {}/{}",
            popped_slot, self.count, MAX_BUFFERED_MESSAGES
        );

        // Persist to flash
        self.persist_all();

        Ok(())
    }

    fn is_empty(&self) -> bool {
        self.count == 0
    }

    fn is_full(&self) -> bool {
        self.count >= MAX_BUFFERED_MESSAGES
    }

    fn count(&self) -> usize {
        self.count
    }

    fn clear(&mut self) {
        info!("[NVS] Clearing storage (was {} messages)", self.count);
        self.head = 0;
        self.tail = 0;
        self.count = 0;
        self.slots = [CachedSlot::default(); MAX_BUFFERED_MESSAGES];
        self.dirty = true;
        self.persist_all();
        info!("[NVS] Storage cleared successfully");
    }
}
