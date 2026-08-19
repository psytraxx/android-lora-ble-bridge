use crate::protocol::Message;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum StorageError {
    Full,
    Empty,
    SerializationError,
    StorageError,
}

/// Port trait for persistent message storage (survives deep sleep)
pub trait Storage {
    /// Add a message to storage. If full, implementation should decide policy (drop oldest vs reject).
    /// Returns Ok(()) if added, Err(StorageError) if failed.
    fn add(&mut self, message: &Message) -> Result<(), StorageError>;

    /// Peek at the oldest message without removing it.
    fn peek(&mut self) -> Result<Option<Message>, StorageError>;

    /// Remove the oldest message.
    fn pop(&mut self) -> Result<(), StorageError>;

    /// Check if storage is empty.
    fn is_empty(&self) -> bool;

    /// Check if storage is full.
    fn is_full(&self) -> bool;

    /// Get current number of messages.
    fn count(&self) -> usize;

    /// Clear all messages
    fn clear(&mut self);
}
