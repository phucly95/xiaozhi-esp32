#include "ring_buffer.h"
#include <string.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

#define TAG "RingBuffer"

// Static storage for ring buffers (stored as class members)
static StaticRingbuffer_t* rb_struct1 = nullptr;
static StaticRingbuffer_t* rb_struct2 = nullptr;
static uint8_t* rb_storage1 = nullptr;
static uint8_t* rb_storage2 = nullptr;
static int buffer_id = 0;

RingBuffer::RingBuffer(size_t size, bool use_psram) 
    : handle_(nullptr)
    , size_(size)
{
    // Allocate storage from PSRAM
    StaticRingbuffer_t* rb_struct = nullptr;
    uint8_t* rb_storage = nullptr;
    
    // Track which buffer we're creating (for cleanup)
    int current_id = buffer_id++;
    
    if (use_psram) {
        rb_struct = (StaticRingbuffer_t*)heap_caps_malloc(sizeof(StaticRingbuffer_t), MALLOC_CAP_SPIRAM);
        rb_storage = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
        
        if (rb_struct && rb_storage) {
            // Use BYTEBUF mode for streaming audio
            handle_ = xRingbufferCreateStatic(size, RINGBUF_TYPE_BYTEBUF, rb_storage, rb_struct);
            
            // Store for cleanup
            if (current_id == 0) {
                rb_struct1 = rb_struct;
                rb_storage1 = rb_storage;
            } else {
                rb_struct2 = rb_struct;
                rb_storage2 = rb_storage;
            }
            
            if (handle_) {
                ESP_LOGI(TAG, "Created PSRAM ring buffer: %d bytes at %p", (int)size, rb_storage);
            } else {
                ESP_LOGE(TAG, "Failed to create static ring buffer!");
            }
        } else {
            ESP_LOGE(TAG, "Failed to allocate PSRAM for ring buffer!");
            if (rb_struct) heap_caps_free(rb_struct);
            if (rb_storage) heap_caps_free(rb_storage);
        }
    } else {
        // Fall back to internal RAM with smaller size
        handle_ = xRingbufferCreate(size > 8192 ? 8192 : size, RINGBUF_TYPE_BYTEBUF);
        if (handle_) {
            ESP_LOGI(TAG, "Created internal ring buffer: %d bytes", (int)(size > 8192 ? 8192 : size));
        } else {
            ESP_LOGE(TAG, "Failed to create internal ring buffer!");
        }
    }
}

RingBuffer::~RingBuffer() {
    if (handle_) {
        vRingbufferDelete(handle_);
    }
    // Note: For static buffers we need to free memory separately
    // This is handled at application shutdown
}

size_t RingBuffer::Write(const uint8_t* data, size_t len, TickType_t timeout) {
    if (!handle_ || !data || len == 0) {
        static int warn_count = 0;
        if (warn_count++ % 100 == 0) {
            ESP_LOGW(TAG, "Write: handle=%p data=%p len=%d", handle_, data, (int)len);
        }
        return 0;
    }
    
    if (xRingbufferSend(handle_, data, len, timeout) == pdTRUE) {
        return len;
    }
    return 0;
}

size_t RingBuffer::Read(uint8_t* data, size_t len, TickType_t timeout) {
    if (!handle_ || !data || len == 0) {
        return 0;
    }
    
    size_t received_size = 0;
    uint8_t* item = (uint8_t*)xRingbufferReceiveUpTo(handle_, &received_size, timeout, len);
    
    if (item) {
        memcpy(data, item, received_size);
        vRingbufferReturnItem(handle_, item);
        return received_size;
    }
    return 0;
}

size_t RingBuffer::Available() const {
    if (!handle_) return 0;
    
    UBaseType_t items_waiting = 0;
    vRingbufferGetInfo(handle_, nullptr, nullptr, nullptr, nullptr, &items_waiting);
    return items_waiting;
}

size_t RingBuffer::FreeSpace() const {
    if (!handle_) return 0;
    return xRingbufferGetCurFreeSize(handle_);
}

void RingBuffer::Clear() {
    if (!handle_) return;
    
    size_t received_size;
    void* item;
    while ((item = xRingbufferReceive(handle_, &received_size, 0)) != nullptr) {
        vRingbufferReturnItem(handle_, item);
    }
}
