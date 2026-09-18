/* Host-only FreeRTOS API test double; never included in firmware builds. */
#ifndef NET_BUF_TEST_FREERTOS_H
#define NET_BUF_TEST_FREERTOS_H
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef uint32_t TickType_t;
typedef unsigned int UBaseType_t;
typedef int BaseType_t;
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    unsigned int count, capacity, item_size;
    uint8_t *data;
} StaticQueue_t;
typedef StaticQueue_t StaticSemaphore_t;
typedef StaticQueue_t *QueueHandle_t;
typedef QueueHandle_t SemaphoreHandle_t;
extern pthread_mutex_t net_buf_test_critical;
extern unsigned int net_buf_test_queue_creates;
#define configTICK_RATE_HZ 1000
#define configASSERT(x) assert(x)
#define pdPASS 1
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (ms)
#define taskENTER_CRITICAL() pthread_mutex_lock(&net_buf_test_critical)
#define taskEXIT_CRITICAL() pthread_mutex_unlock(&net_buf_test_critical)
#define taskENTER_CRITICAL_FROM_ISR() (taskENTER_CRITICAL(), 0U)
#define taskEXIT_CRITICAL_FROM_ISR(key) ((void)(key), taskEXIT_CRITICAL())
#define portYIELD_FROM_ISR(woken) ((void)(woken))
#define xPortIsInsideInterrupt() 0
#define xTaskGetCurrentTaskHandle() ((void *)1)
#define pvPortMalloc(size) malloc(size)
#define vPortFree(ptr) free(ptr)

static inline TickType_t xTaskGetTickCount(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (TickType_t)((uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000);
}
#define xTaskGetTickCountFromISR() xTaskGetTickCount()

static inline QueueHandle_t xQueueCreateStatic(UBaseType_t capacity, UBaseType_t size,
                                              uint8_t *storage, StaticQueue_t *queue)
{
    pthread_mutex_init(&queue->lock, NULL);
    pthread_cond_init(&queue->changed, NULL);
    queue->count = 0;
    queue->capacity = capacity;
    queue->item_size = size;
    queue->data = storage;
    ++net_buf_test_queue_creates;
    return queue;
}

static inline BaseType_t xQueueSendToFront(QueueHandle_t queue, const void *data,
                                         TickType_t timeout)
{
    (void)timeout;
    pthread_mutex_lock(&queue->lock);
    if (queue->count == queue->capacity) {
        pthread_mutex_unlock(&queue->lock);
        return 0;
    }
    if (queue->item_size) {
        memmove(queue->data + queue->item_size, queue->data, queue->count * queue->item_size);
        memcpy(queue->data, data, queue->item_size);
    }
    ++queue->count;
    pthread_cond_signal(&queue->changed);
    pthread_mutex_unlock(&queue->lock);
    return pdPASS;
}

static inline BaseType_t xQueueReceive(QueueHandle_t queue, void *data, TickType_t timeout)
{
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout / 1000;
    deadline.tv_nsec += (timeout % 1000) * 1000000L;
    deadline.tv_sec += deadline.tv_nsec / 1000000000L;
    deadline.tv_nsec %= 1000000000L;
    pthread_mutex_lock(&queue->lock);
    while (!queue->count) {
        if (!timeout || (timeout != portMAX_DELAY &&
            pthread_cond_timedwait(&queue->changed, &queue->lock, &deadline))) {
            pthread_mutex_unlock(&queue->lock);
            return 0;
        }
        if (timeout == portMAX_DELAY) {
            pthread_cond_wait(&queue->changed, &queue->lock);
        }
    }
    --queue->count;
    if (queue->item_size) {
        memcpy(data, queue->data, queue->item_size);
        memmove(queue->data, queue->data + queue->item_size, queue->count * queue->item_size);
    }
    pthread_mutex_unlock(&queue->lock);
    return pdPASS;
}
#define xQueueSendToFrontFromISR(q, data, woken) xQueueSendToFront(q, data, 0)
#define xQueueReceiveFromISR(q, data, woken) xQueueReceive(q, data, 0)
#define xSemaphoreCreateBinaryStatic(control) xQueueCreateStatic(1, 0, NULL, control)
#define xSemaphoreGive(sem) xQueueSendToFront(sem, NULL, 0)
#define xSemaphoreTake(sem, timeout) xQueueReceive(sem, NULL, timeout)
#define xSemaphoreGiveFromISR(sem, woken) xSemaphoreGive(sem)
#endif
