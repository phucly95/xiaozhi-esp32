#pragma once

#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"

/**
 * Thread-safe ring buffer for audio streaming
 * Uses ESP-IDF's built-in xRingbuffer (battle-tested, no-split mode)
 */

class RingBuffer {
public:
    RingBuffer(size_t size, bool use_psram = true);
    ~RingBuffer();
    
    // Write data (producer)
    size_t Write(const uint8_t* data, size_t len, TickType_t timeout = portMAX_DELAY);
    
    // Read data (consumer)
    size_t Read(uint8_t* data, size_t len, TickType_t timeout = portMAX_DELAY);
    
    // Query
    size_t Available() const;
    size_t FreeSpace() const;
    void Clear();
    
private:
    RingbufHandle_t handle_;
    size_t size_;
};
