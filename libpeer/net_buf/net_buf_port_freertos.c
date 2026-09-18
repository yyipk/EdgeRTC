#include "net_buf_port.h"
#include <zephyr/net_buf.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

static size_t align_up(size_t value, size_t alignment)
{
	size_t mask = alignment - 1;

	return (value + mask) & ~mask;
}

static TickType_t timeout_to_ticks(k_timeout_t timeout)
{
	if (timeout == K_FOREVER) {
		return portMAX_DELAY;
	}
	return timeout > 0 ? (TickType_t)(((uint64_t)timeout * configTICK_RATE_HZ +
					 MSEC_PER_SEC - 1) / MSEC_PER_SEC) : 0;
}

/* Sentinel entries keep GNU section boundaries available before any user pools exist. */
static const struct net_buf_lifo_storage lifo_sentinel
	__attribute__((section("libpeer_net_buf_lifos"), used, aligned(sizeof(void *))));
static struct net_buf_pool pool_sentinel
	__attribute__((section("libpeer_net_buf_pool"), used, aligned(sizeof(void *))));
extern const struct net_buf_lifo_storage __start_libpeer_net_buf_lifos[];
extern const struct net_buf_lifo_storage __stop_libpeer_net_buf_lifos[];

static void libpeer_net_buf_lifo_init(struct k_lifo *lifo, uint8_t *storage,
				      UBaseType_t count, StaticQueue_t *control);

void libpeer_net_buf_init(void)
{
	const struct net_buf_lifo_storage *entry;

	configASSERT(!xPortIsInsideInterrupt());
	taskENTER_CRITICAL();
	for (entry = __start_libpeer_net_buf_lifos;
	     entry < __stop_libpeer_net_buf_lifos; entry++) {
		if (entry->lifo && !entry->lifo->handle) {
			libpeer_net_buf_lifo_init(entry->lifo, entry->storage, entry->count,
						  &entry->lifo->control);
		}
	}
	taskEXIT_CRITICAL();
}

static void libpeer_net_buf_lifo_init(struct k_lifo *lifo, uint8_t *storage,
				      UBaseType_t count, StaticQueue_t *control)
{
	lifo->control_storage = control ? control : &lifo->control;
	lifo->handle = xQueueCreateStatic(count, sizeof(void *), storage,
					  lifo->control_storage);
	configASSERT(lifo->handle);
}

int net_buf_pool_init(struct net_buf_pool *pool, void *arena, size_t arena_size,
			      uint16_t count, uint16_t data_size,
			      uint8_t user_data_size,
			      void (*destroy)(struct net_buf *buf))
{
	struct net_buf_pool_fixed *fixed;
	struct net_buf_data_alloc *alloc;
	uint8_t *base = arena;
	uint8_t *queue_storage;
	uint8_t *buf_storage;
	uint8_t *data_storage;
	size_t offset = 0;
	size_t struct_size;
	size_t descriptor_size;
	size_t data_bytes;
	StaticQueue_t *queue_control;

	if (!pool || !arena || !count || !data_size) {
		return -EINVAL;
	}

	struct_size = ROUND_UP(sizeof(struct net_buf) + user_data_size,
				      __alignof__(struct net_buf));
	descriptor_size = struct_size * count;
	data_bytes = (size_t)data_size * count;
	if (struct_size == 0 || descriptor_size / struct_size != count ||
	    data_bytes / data_size != count) {
		return -EOVERFLOW;
	}

	offset = align_up(offset, __alignof__(struct net_buf_pool_fixed));
	if (offset > arena_size || sizeof(*fixed) > arena_size - offset) {
		return -ENOMEM;
	}
	fixed = (struct net_buf_pool_fixed *)(base + offset);
	offset += sizeof(*fixed);

	offset = align_up(offset, __alignof__(struct net_buf_data_alloc));
	if (offset > arena_size || sizeof(*alloc) > arena_size - offset) {
		return -ENOMEM;
	}
	alloc = (struct net_buf_data_alloc *)(base + offset);
	offset += sizeof(*alloc);

	offset = align_up(offset, __alignof__(StaticQueue_t));
	if (offset > arena_size || sizeof(*queue_control) > arena_size - offset) {
		return -ENOMEM;
	}
	queue_control = (StaticQueue_t *)(base + offset);
	offset += sizeof(*queue_control);

	offset = align_up(offset, sizeof(void *));
	if (offset > arena_size || count > (arena_size - offset) / sizeof(void *)) {
		return -ENOMEM;
	}
	queue_storage = base + offset;
	offset += (size_t)count * sizeof(void *);

	offset = align_up(offset, __alignof__(struct net_buf));
	if (offset > arena_size || descriptor_size > arena_size - offset) {
		return -ENOMEM;
	}
	buf_storage = base + offset;
	offset += descriptor_size;

	offset = align_up(offset, __alignof__(struct net_buf));
	if (offset > arena_size || data_bytes > arena_size - offset) {
		return -ENOMEM;
	}
	data_storage = base + offset;

	fixed->data_pool = data_storage;
	*alloc = (struct net_buf_data_alloc){
		.cb = &net_buf_fixed_cb,
		.alloc_data = fixed,
		.max_alloc_size = data_size,
	};
	struct net_buf_pool initialized = {
		.free = { 0 },
		.lock = { 0 },
		.buf_count = count,
		.uninit_count = count,
		.user_data_size = user_data_size,
#if defined(CONFIG_NET_BUF_POOL_USAGE)
		.avail_count = ATOMIC_INIT(count),
		.pool_size = data_bytes > UINT16_MAX ? UINT16_MAX : data_bytes,
		.max_used = 0,
		.name = NULL,
#endif
		.destroy = destroy,
		.alloc = alloc,
		.__bufs = (struct net_buf *)buf_storage,
	};
	memcpy(pool, &initialized, sizeof(initialized));
	libpeer_net_buf_lifo_init(&pool->free, queue_storage, count, queue_control);
	return 0;
}

void libpeer_net_buf_lifo_put(struct k_lifo *lifo, void *data)
{
	BaseType_t result;

	configASSERT(lifo->handle);
	if (xPortIsInsideInterrupt()) {
		BaseType_t woken = pdFALSE;

		result = xQueueSendToFrontFromISR(lifo->handle, &data, &woken);
		portYIELD_FROM_ISR(woken);
	} else {
		result = xQueueSendToFront(lifo->handle, &data, 0);
	}
	/* A pool has one queue slot for every descriptor; full means double return. */
	configASSERT(result == pdPASS);
	(void)result;
}

void *libpeer_net_buf_lifo_get(struct k_lifo *lifo, k_timeout_t timeout)
{
	void *data = NULL;

	configASSERT(lifo->handle);
	if (xPortIsInsideInterrupt()) {
		BaseType_t woken = pdFALSE;

		configASSERT(timeout == K_NO_WAIT);
		if (timeout != K_NO_WAIT) {
			return NULL;
		}
		xQueueReceiveFromISR(lifo->handle, &data, &woken);
		portYIELD_FROM_ISR(woken);
	} else {
		xQueueReceive(lifo->handle, &data, timeout_to_ticks(timeout));
	}
	return data;
}

k_timepoint_t libpeer_net_buf_timepoint_calc(k_timeout_t timeout)
{
	k_timepoint_t end = {
		.start = xPortIsInsideInterrupt() ? xTaskGetTickCountFromISR() : xTaskGetTickCount(),
		.ticks = timeout_to_ticks(timeout),
	};

	return end;
}

k_timeout_t libpeer_net_buf_timepoint_timeout(k_timepoint_t end)
{
	TickType_t now = xPortIsInsideInterrupt() ? xTaskGetTickCountFromISR() : xTaskGetTickCount();
	TickType_t elapsed = now - end.start;

	if (end.ticks == portMAX_DELAY) {
		return K_FOREVER;
	}
	if (elapsed >= end.ticks) {
		return K_NO_WAIT;
	}
	return (k_timeout_t)(((uint64_t)(end.ticks - elapsed) * MSEC_PER_SEC +
			      configTICK_RATE_HZ - 1) / configTICK_RATE_HZ);
}

uint32_t libpeer_net_buf_uptime_get_32(void)
{
	TickType_t ticks = xPortIsInsideInterrupt() ? xTaskGetTickCountFromISR() :
						       xTaskGetTickCount();

	return (uint32_t)(((uint64_t)ticks * MSEC_PER_SEC) / configTICK_RATE_HZ);
}

unsigned int libpeer_net_buf_irq_lock(void)
{
	if (xPortIsInsideInterrupt()) {
		return taskENTER_CRITICAL_FROM_ISR();
	}
	taskENTER_CRITICAL();
	return 0;
}

void libpeer_net_buf_irq_unlock(unsigned int key)
{
	if (xPortIsInsideInterrupt()) {
		taskEXIT_CRITICAL_FROM_ISR(key);
	} else {
		taskEXIT_CRITICAL();
	}
}
