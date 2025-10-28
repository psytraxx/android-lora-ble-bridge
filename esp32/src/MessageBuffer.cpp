#include "MessageBuffer.h"

// Place buffer and indices in RTC slow memory so they survive deep sleep
RTC_DATA_ATTR Message MessageBuffer::buffer[MessageBuffer::MAX_MESSAGES];
RTC_DATA_ATTR int MessageBuffer::head = 0;
RTC_DATA_ATTR int MessageBuffer::tail = 0;
RTC_DATA_ATTR int MessageBuffer::count = 0;

// Note: Methods are implemented inline in the header; the static storage
// definitions above ensure the buffer persists across deep sleep.
