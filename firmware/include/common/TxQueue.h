#ifndef TX_QUEUE_H
#define TX_QUEUE_H

#include <cstdint>
#include <cstddef>
#include "common/MeshPacket.h"

/**
 * @file TxQueue.h
 * @brief Fixed-size priority TX queue for LoRa transmissions
 *
 * Provides a priority-ordered queue with no dynamic allocation.
 * Higher-priority packets (lower enum value) are transmitted first.
 * Supports retry scheduling, relay cancellation, and timer extension.
 */

enum TxPriority : uint8_t
{
    TX_PRIORITY_ACK = 0,       // Highest - time-critical ACK responses
    TX_PRIORITY_USER_TEXT = 1,  // User messages from BLE
    TX_PRIORITY_RELAY = 2,     // Mesh rebroadcasts
    TX_PRIORITY_BROADCAST = 3, // NodeInfo, Telemetry (background)
};

struct TxQueueEntry
{
    uint8_t packet[MeshPacket::MAX_PACKET_SIZE]; // 253 bytes
    size_t length;
    TxPriority priority;
    uint8_t retryCount;
    uint8_t maxRetries;
    uint32_t earliestTxTime; // Don't TX before this (millis)
    uint32_t fromNode;       // For cancellation/matching
    uint32_t packetId;       // For cancellation/matching
    bool isRelay;
    bool active;
};

/**
 * @brief Fixed-size priority TX queue (16 entries, no dynamic allocation)
 *
 * Entries are stored in a flat array. peekReady() scans for the
 * highest-priority entry whose earliestTxTime has passed.
 */
class TxQueue
{
public:
    static constexpr size_t MAX_ENTRIES = 16;

    TxQueue();

    /**
     * @brief Add a packet to the queue
     * @return true if added, false if queue is full
     */
    bool push(const uint8_t *data, size_t len, TxPriority priority,
              uint32_t fromNode, uint32_t packetId,
              bool isRelay, uint8_t maxRetries, uint32_t delayMs);

    /**
     * @brief Find the highest-priority entry ready to transmit
     * @param now Current millis() value
     * @return Pointer to entry, or nullptr if nothing ready
     */
    TxQueueEntry *peekReady(uint32_t now);

    /**
     * @brief Remove an entry from the queue (mark inactive)
     */
    void pop(TxQueueEntry *entry);

    /**
     * @brief Requeue an entry for retry with additional delay
     * @return true if requeued, false if max retries exceeded
     */
    bool requeueForRetry(TxQueueEntry *entry, uint32_t delayMs);

    /**
     * @brief Cancel all queued entries matching fromNode + packetId
     * @return Number of entries cancelled
     */
    int cancel(uint32_t fromNode, uint32_t packetId);

    /**
     * @brief Cancel only relay entries matching fromNode + packetId
     * @return Number of relay entries cancelled
     */
    int cancelRelays(uint32_t fromNode, uint32_t packetId);

    /**
     * @brief Extend earliestTxTime of all pending entries by ms
     *
     * Called when we hear another packet on-air to avoid collisions.
     */
    void extendTimers(uint32_t ms);

    /**
     * @brief Get number of active entries in queue
     */
    size_t count() const;

private:
    TxQueueEntry entries_[MAX_ENTRIES];
};

#endif // TX_QUEUE_H
