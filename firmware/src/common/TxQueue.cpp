#include "common/TxQueue.h"
#include "common/Logging.h"
#include <Arduino.h>
#include <cstring>

static const char *TAG = "TxQ";

TxQueue::TxQueue()
{
    for (size_t i = 0; i < MAX_ENTRIES; i++)
    {
        entries_[i].active = false;
    }
}

bool TxQueue::push(const uint8_t *data, size_t len, TxPriority priority,
                   uint32_t fromNode, uint32_t packetId,
                   bool isRelay, uint8_t maxRetries, uint32_t delayMs)
{
    // Find a free slot
    for (size_t i = 0; i < MAX_ENTRIES; i++)
    {
        if (!entries_[i].active)
        {
            memcpy(entries_[i].packet, data, len);
            entries_[i].length = len;
            entries_[i].priority = priority;
            entries_[i].retryCount = 0;
            entries_[i].maxRetries = maxRetries;
            entries_[i].earliestTxTime = millis() + delayMs;
            entries_[i].fromNode = fromNode;
            entries_[i].packetId = packetId;
            entries_[i].isRelay = isRelay;
            entries_[i].active = true;

            LOG_D(TAG, "Queued pkt id=%lu pri=%u delay=%lums (%lu active)",
                  (unsigned long)packetId, priority,
                  (unsigned long)delayMs, (unsigned long)count());
            return true;
        }
    }

    LOG_W(TAG, "Queue full! Dropping pkt id=%lu pri=%u",
          (unsigned long)packetId, priority);
    return false;
}

TxQueueEntry *TxQueue::peekReady(uint32_t now)
{
    TxQueueEntry *best = nullptr;

    for (size_t i = 0; i < MAX_ENTRIES; i++)
    {
        if (!entries_[i].active)
            continue;

        // Check if entry is ready (time-wise)
        // Handle millis() wraparound: (now - earliest) will wrap correctly
        // for entries whose time has passed
        if ((int32_t)(now - entries_[i].earliestTxTime) < 0)
            continue;

        // Pick highest priority (lowest enum value), then earliest time
        if (best == nullptr ||
            entries_[i].priority < best->priority ||
            (entries_[i].priority == best->priority &&
             (int32_t)(entries_[i].earliestTxTime - best->earliestTxTime) < 0))
        {
            best = &entries_[i];
        }
    }

    return best;
}

void TxQueue::pop(TxQueueEntry *entry)
{
    if (entry)
    {
        entry->active = false;
        LOG_D(TAG, "Popped pkt id=%lu (%lu remaining)",
              (unsigned long)entry->packetId, (unsigned long)(count()));
    }
}

bool TxQueue::requeueForRetry(TxQueueEntry *entry, uint32_t delayMs)
{
    if (!entry || !entry->active)
        return false;

    if (entry->retryCount >= entry->maxRetries)
    {
        LOG_D(TAG, "Max retries reached for pkt id=%lu (%u/%u)",
              (unsigned long)entry->packetId,
              entry->retryCount, entry->maxRetries);
        return false;
    }

    entry->retryCount++;
    entry->earliestTxTime = millis() + delayMs;

    LOG_I(TAG, "Retry %u/%u for pkt id=%lu (delay=%lums)",
          entry->retryCount, entry->maxRetries,
          (unsigned long)entry->packetId, (unsigned long)delayMs);
    return true;
}

int TxQueue::cancel(uint32_t fromNode, uint32_t packetId)
{
    int cancelled = 0;
    for (size_t i = 0; i < MAX_ENTRIES; i++)
    {
        if (entries_[i].active &&
            entries_[i].fromNode == fromNode &&
            entries_[i].packetId == packetId)
        {
            entries_[i].active = false;
            cancelled++;
        }
    }

    if (cancelled > 0)
    {
        LOG_D(TAG, "Cancelled %d entries for from=%08lx id=%lu",
              cancelled, (unsigned long)fromNode, (unsigned long)packetId);
    }
    return cancelled;
}

int TxQueue::cancelRelays(uint32_t fromNode, uint32_t packetId)
{
    int cancelled = 0;
    for (size_t i = 0; i < MAX_ENTRIES; i++)
    {
        if (entries_[i].active &&
            entries_[i].isRelay &&
            entries_[i].fromNode == fromNode &&
            entries_[i].packetId == packetId)
        {
            entries_[i].active = false;
            cancelled++;
        }
    }

    if (cancelled > 0)
    {
        LOG_I(TAG, "Cancelled %d relays for from=%08lx id=%lu",
              cancelled, (unsigned long)fromNode, (unsigned long)packetId);
    }
    return cancelled;
}

void TxQueue::extendTimers(uint32_t ms)
{
    for (size_t i = 0; i < MAX_ENTRIES; i++)
    {
        if (entries_[i].active)
        {
            entries_[i].earliestTxTime += ms;
        }
    }
}

size_t TxQueue::count() const
{
    size_t n = 0;
    for (size_t i = 0; i < MAX_ENTRIES; i++)
    {
        if (entries_[i].active)
            n++;
    }
    return n;
}
