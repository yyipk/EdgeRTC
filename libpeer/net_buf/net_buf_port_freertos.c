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
extern const struct net_buf_lifo_storage __start_libpeer_net_buf_lifos[];
extern const struct net_buf_lifo_storage __stop_libpeer_net_buf_lifos[];

struct runtime_pool {
	struct runtime_pool *next;
	struct net_buf_pool *pool;
	uint8_t id;
};

struct pool_arena {
	struct net_buf_pool_fixed fixed;
	struct net_buf_data_alloc alloc;
	StaticQueue_t control;
	struct runtime_pool registration;
};

static struct runtime_pool *runtime_pools;

struct net_buf_pool *libpeer_net_buf_pool_lookup(int id)
{
	struct net_buf_pool *pool = NULL;
	ptrdiff_t static_count = __stop_libpeer_net_buf_lifos - __start_libpeer_net_buf_lifos;
	unsigned int key;

	if (id >= 0 && id < static_count) {
		pool = __start_libpeer_net_buf_lifos[id].pool;
		configASSERT(pool);
		return pool;
	}
	key = libpeer_net_buf_irq_lock();
	for (struct runtime_pool *entry = runtime_pools; entry; entry = entry->next) {
		if (entry->id == id) {
			pool = entry->pool;
			break;
		}
	}
	libpeer_net_buf_irq_unlock(key);
	configASSERT(pool);
	return pool;
}

int libpeer_net_buf_pool_index(struct net_buf_pool *pool)
{
	for (const struct net_buf_lifo_storage *entry = __start_libpeer_net_buf_lifos;
	     entry < __stop_libpeer_net_buf_lifos; entry++) {
		if (entry->pool == pool) {
			return entry - __start_libpeer_net_buf_lifos;
		}
	}
	unsigned int key = libpeer_net_buf_irq_lock();
	for (struct runtime_pool *entry = runtime_pools; entry; entry = entry->next) {
		if (entry->pool == pool) {
			int id = entry->id;

			libpeer_net_buf_irq_unlock(key);
			return id;
		}
	}
	libpeer_net_buf_irq_unlock(key);
	configASSERT(0);
	return -1;
}

static void libpeer_net_buf_lifo_init(struct k_lifo *lifo, uint8_t *storage,
				      UBaseType_t count, StaticQueue_t *control);

void libpeer_net_buf_init(void)
{
	const struct net_buf_lifo_storage *entry;

	configASSERT(!xPortIsInsideInterrupt());
	taskENTER_CRITICAL();
	for (entry = __start_libpeer_net_buf_lifos;
	     entry < __stop_libpeer_net_buf_lifos; entry++) {
		if (entry->count && entry->lifo && !entry->lifo->handle) {
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

size_t net_buf_pool_arena_size(uint16_t count, uint16_t data_size, uint8_t user_data_size)
{
	size_t struct_size = ROUND_UP(sizeof(struct net_buf) + user_data_size,
				    __alignof__(struct net_buf));
	size_t offset;

	if (!count || !data_size) {
		return 0;
	}
	offset = align_up(sizeof(struct pool_arena) + (size_t)count * sizeof(void *),
			  __alignof__(struct net_buf));
	if (count > (SIZE_MAX - offset) / struct_size) {
		return 0;
	}
	offset = align_up(offset + struct_size * count, __alignof__(struct net_buf));
	if (count > (SIZE_MAX - offset) / data_size) {
		return 0;
	}
	return offset + (size_t)count * data_size;
}

int net_buf_pool_init(struct net_buf_pool *pool, void *arena, size_t arena_size,
		      uint16_t count, uint16_t data_size,
		      uint8_t user_data_size,
		      void (*destroy)(struct net_buf *buf))
{
	struct pool_arena *storage = arena;
	uint8_t *base = arena;
	uint8_t *queue_storage;
	uint8_t *buf_storage;
	uint8_t *data_storage;
	size_t offset;
	size_t struct_size;
	size_t required = net_buf_pool_arena_size(count, data_size, user_data_size);
	int id = -1;
	unsigned int key;

	if (!pool || !arena || !required || (uintptr_t)arena % __alignof__(struct pool_arena)) {
		return -EINVAL;
	}
	if (required > arena_size) {
		return -ENOMEM;
	}
	if (pool->free.handle) {
		return -EBUSY;
	}

	struct_size = ROUND_UP(sizeof(struct net_buf) + user_data_size,
				      __alignof__(struct net_buf));
	offset = sizeof(*storage);
	queue_storage = base + offset;
	offset += (size_t)count * sizeof(void *);

	offset = align_up(offset, __alignof__(struct net_buf));
	buf_storage = base + offset;
	offset += struct_size * count;

	offset = align_up(offset, __alignof__(struct net_buf));
	data_storage = base + offset;

	key = libpeer_net_buf_irq_lock();
	for (const struct net_buf_lifo_storage *entry = __start_libpeer_net_buf_lifos;
	     entry < __stop_libpeer_net_buf_lifos; entry++) {
		if (entry->pool == pool) {
			id = entry - __start_libpeer_net_buf_lifos;
		}
	}
	if (id < 0) {
		for (id = __stop_libpeer_net_buf_lifos - __start_libpeer_net_buf_lifos;
		     id <= UINT8_MAX; id++) {
			struct runtime_pool *entry;

			for (entry = runtime_pools; entry && entry->id != id; entry = entry->next) {}
			if (!entry) break;
		}
		if (id > UINT8_MAX) {
			libpeer_net_buf_irq_unlock(key);
			return -ENOSPC;
		}
		storage->registration = (struct runtime_pool){ runtime_pools, pool, id };
		runtime_pools = &storage->registration;
	}
	storage->fixed.data_pool = data_storage;
	storage->alloc = (struct net_buf_data_alloc){
		.cb = &net_buf_fixed_cb,
		.alloc_data = &storage->fixed,
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
		.pool_size = MIN((size_t)data_size * count, UINT16_MAX),
		.max_used = 0,
		.name = NULL,
#endif
		.destroy = destroy,
		.alloc = &storage->alloc,
		.__bufs = (struct net_buf *)buf_storage,
	};
	memcpy(pool, &initialized, sizeof(initialized));
	libpeer_net_buf_lifo_init(&pool->free, queue_storage, count, &storage->control);
	libpeer_net_buf_irq_unlock(key);
	return 0;
}

int net_buf_pool_deinit(struct net_buf_pool *pool)
{
	struct runtime_pool **link;
	unsigned int key;

	if (!pool) {
		return -EINVAL;
	}
	key = libpeer_net_buf_irq_lock();
	if (!pool->free.handle) {
		libpeer_net_buf_irq_unlock(key);
		return 0;
	}
	if (pool->uninit_count + uxQueueMessagesWaiting(pool->free.handle) != pool->buf_count) {
		libpeer_net_buf_irq_unlock(key);
		return -EBUSY;
	}
	for (link = &runtime_pools; *link; link = &(*link)->next) {
		if ((*link)->pool == pool) {
			*link = (*link)->next;
			break;
		}
	}
	vQueueDelete(pool->free.handle);
	memset(pool, 0, sizeof(*pool));
	libpeer_net_buf_irq_unlock(key);
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
