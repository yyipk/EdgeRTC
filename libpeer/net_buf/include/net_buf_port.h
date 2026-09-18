#ifndef LIBPEER_NET_BUF_PORT_H_
#define LIBPEER_NET_BUF_PORT_H_

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

struct net_buf;
struct net_buf_pool;

#ifndef CONFIG_HEAP_MEM_POOL_SIZE
#define CONFIG_HEAP_MEM_POOL_SIZE 1
#endif

#ifndef CONFIG_NET_BUF_WARN_ALLOC_INTERVAL
#define CONFIG_NET_BUF_WARN_ALLOC_INTERVAL 0
#endif

#define MSEC_PER_SEC 1000U
#define K_NO_WAIT 0
#define K_FOREVER (-1)
#define K_SECONDS(seconds) ((k_timeout_t)((seconds) * MSEC_PER_SEC))

typedef int32_t k_timeout_t;
typedef int32_t s32_t;
typedef uint8_t u8_t;
typedef uint16_t u16_t;
typedef uint32_t u32_t;
typedef uint64_t u64_t;

typedef long atomic_t;
typedef long atomic_val_t;
typedef void *atomic_ptr_t;

#define ATOMIC_INIT(value) (value)
#define atomic_get(target) __atomic_load_n((target), __ATOMIC_ACQUIRE)
#define atomic_inc(target) __atomic_fetch_add((target), 1, __ATOMIC_ACQ_REL)
#define atomic_dec(target) __atomic_fetch_sub((target), 1, __ATOMIC_ACQ_REL)
#define atomic_set(target, value) __atomic_exchange_n((target), (value), __ATOMIC_ACQ_REL)
#define atomic_ptr_clear(target) __atomic_exchange_n((target), NULL, __ATOMIC_ACQ_REL)

#define ARG_UNUSED(value) ((void)(value))
#define BIT(bit) (1UL << (bit))
#define BUILD_ASSERT(condition, ...) _Static_assert(condition, #condition)
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define max(a, b) MAX(a, b)
#define min(a, b) MIN(a, b)
#define ROUND_UP(value, align) (((value) + (align) - 1) / (align) * (align))
#define __maybe_unused __attribute__((unused))
#define __must_check __attribute__((warn_unused_result))
#define K_HEAP_MEM_POOL_SIZE CONFIG_HEAP_MEM_POOL_SIZE
#define K_TIMEOUT_EQ(a, b) ((a) == (b))
#define K_MSEC(ms) ((k_timeout_t)(ms))
#ifndef CONFIG_NET_BUF_ALIGNMENT
#define CONFIG_NET_BUF_ALIGNMENT 0
#endif
#define NET_BUF_CAT_(a, b) a##b
#define NET_BUF_CAT(a, b) NET_BUF_CAT_(a, b)
#define NET_BUF_SECOND_(a, b, ...) b
#define NET_BUF_ENABLED_1 ~, 1
#define NET_BUF_ENABLED_(value) NET_BUF_SECOND_(value, 0)
#ifndef IS_ENABLED
#define IS_ENABLED(value) NET_BUF_ENABLED_(NET_BUF_CAT(NET_BUF_ENABLED_, value))
#endif
#define NET_BUF_UNWRAP(...) __VA_ARGS__
#define NET_BUF_IF_1(code) NET_BUF_UNWRAP code
#define NET_BUF_IF_0(code)
#define IF_ENABLED(flag, code) NET_BUF_CAT(NET_BUF_IF_, IS_ENABLED(flag))(code)
#define LOG_MODULE_REGISTER(...)
#define LOG_LEVEL_WRN 2
#define LOG_DBG(...) do { printf(__VA_ARGS__); printf("\n"); } while (0)
#define LOG_ERR(...) LOG_DBG(__VA_ARGS__)
#define LOG_WRN(...) LOG_DBG(__VA_ARGS__)
#define LOG_INF(...) LOG_DBG(__VA_ARGS__)
#define k_current_get() xTaskGetCurrentTaskHandle()
#define STRINGIFY_(value) #value
#define STRINGIFY(value) STRINGIFY_(value)

#ifndef __aligned
#define __aligned(value) __attribute__((aligned(value)))
#endif
#define __noinit
#ifndef __packed
#define __packed __attribute__((packed))
#endif
#ifndef __used
#define __used __attribute__((used))
#endif
#define __in_section(a, b, c) __attribute__((section("libpeer_net_buf_pool"), used))

#define __ASSERT(condition, ...) configASSERT(condition)
#define __ASSERT_NO_MSG(condition) configASSERT(condition)

#define UNALIGNED_GET(pointer)                                             \
	__extension__({                                                     \
		struct __packed { __typeof__(*(pointer)) value; } *unaligned; \
		unaligned = (__typeof__(unaligned))(pointer);                  \
		unaligned->value;                                             \
	})

struct _snode {
	struct _snode *next;
};

typedef struct _snode sys_snode_t;

typedef struct {
	sys_snode_t *head;
	sys_snode_t *tail;
} sys_slist_t;

static inline void sys_slist_append_list(sys_slist_t *list, void *head, void *tail)
{
	if (list->tail) {
		list->tail->next = head;
	} else {
		list->head = head;
	}
	list->tail = tail;
	list->tail->next = NULL;
}

static inline sys_snode_t *sys_slist_get(sys_slist_t *list)
{
	sys_snode_t *node = list->head;

	if (!node) {
		return NULL;
	}

	list->head = node->next;
	if (!list->head) {
		list->tail = NULL;
	}
	node->next = NULL;
	return node;
}

struct k_lifo {
	QueueHandle_t handle;
	StaticQueue_t control;
	StaticQueue_t *control_storage;
};

struct net_buf_lifo_storage {
	struct k_lifo *lifo;
	uint8_t *storage;
	UBaseType_t count;
};

/* Each native pool declaration supplies exactly buf_count pointer slots. */
#define STRUCT_SECTION_ITERABLE(type, name) \
	struct type name; \
	static void *name##_queue_storage[sizeof(_net_buf_##name) / sizeof(_net_buf_##name[0])]; \
	static const struct net_buf_lifo_storage name##_queue_binding \
		__attribute__((section("libpeer_net_buf_lifos"), used, aligned(sizeof(void *)))) = { \
		&name.free, (uint8_t *)name##_queue_storage, \
		sizeof(name##_queue_storage) / sizeof(void *) }; \
	static struct type name __attribute__((section("libpeer_net_buf_pool"), used, aligned(sizeof(void *))))
#define STRUCT_SECTION_START_EXTERN(type) extern struct type __start_libpeer_net_buf_pool[]
#define TYPE_SECTION_START(type) __start_libpeer_net_buf_pool
#define STRUCT_SECTION_GET(type, index, result) (*(result) = &TYPE_SECTION_START(type)[index])
#define Z_LIFO_INITIALIZER(object) { 0 }
#define NET_BUF_POOL_ARENA_DEFINE(name) \
	static struct net_buf_pool name \
	__attribute__((section("libpeer_net_buf_pool"), used, aligned(sizeof(void *))))

void libpeer_net_buf_init(void);
int net_buf_pool_init(struct net_buf_pool *pool, void *arena, size_t arena_size,
			      uint16_t count, uint16_t data_size,
			      uint8_t user_data_size,
			      void (*destroy)(struct net_buf *buf));
void libpeer_net_buf_lifo_put(struct k_lifo *lifo, void *data);
void *libpeer_net_buf_lifo_get(struct k_lifo *lifo, k_timeout_t timeout);
#define k_lifo_put(lifo, data) libpeer_net_buf_lifo_put(lifo, data)
#define k_lifo_get(lifo, timeout) libpeer_net_buf_lifo_get(lifo, timeout)

struct k_spinlock { unsigned char unused; };
typedef unsigned int k_spinlock_key_t;
#define k_spin_lock(lock) ((void)(lock), libpeer_net_buf_irq_lock())
#define k_spin_unlock(lock, key) ((void)(lock), libpeer_net_buf_irq_unlock(key))

typedef struct {
	TickType_t start;
	TickType_t ticks;
} k_timepoint_t;
k_timepoint_t libpeer_net_buf_timepoint_calc(k_timeout_t timeout);
k_timeout_t libpeer_net_buf_timepoint_timeout(k_timepoint_t end);
#define sys_timepoint_calc(timeout) libpeer_net_buf_timepoint_calc(timeout)
#define sys_timepoint_timeout(end) libpeer_net_buf_timepoint_timeout(end)

static inline void sys_slist_append(sys_slist_t *list, sys_snode_t *node)
{
	sys_slist_append_list(list, node, node);
}

struct k_heap {
	uint8_t unused;
};
#define K_HEAP_DEFINE(name, size) static struct k_heap name
#include "mm.h"
#define k_heap_alloc(heap, size, timeout) ((void)(heap), (void)(timeout), k_malloc(size))
#define k_heap_free(heap, data) ((void)(heap), k_free(data))
#define k_heap_aligned_alloc(heap, align, size, timeout) ((void)(heap), (void)(timeout), kmemalign(size, align))
#define k_malloc(size) kmalloc(size, 0)
#define k_free(data) kfree(data)
#define k_uptime_get_32() libpeer_net_buf_uptime_get_32()
#define irq_lock() libpeer_net_buf_irq_lock()
#define irq_unlock(key) libpeer_net_buf_irq_unlock(key)

uint32_t libpeer_net_buf_uptime_get_32(void);
unsigned int libpeer_net_buf_irq_lock(void);
void libpeer_net_buf_irq_unlock(unsigned int key);

static inline uint16_t sys_bswap16(uint16_t value)
{
	return __builtin_bswap16(value);
}

static inline uint32_t sys_bswap24(uint32_t value)
{
	return ((value & 0x0000ffU) << 16) | (value & 0x00ff00U) |
	       ((value & 0xff0000U) >> 16);
}

static inline uint64_t sys_bswap48(uint64_t value)
{
	return ((__builtin_bswap64(value) >> 16) & UINT64_C(0x0000ffffffffffff));
}

static inline uint64_t sys_bswap40(uint64_t value)
{
	return __builtin_bswap64(value) >> 24;
}

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define sys_le16_to_cpu(value) (value)
#define sys_le24_to_cpu(value) (value)
#define sys_le32_to_cpu(value) (value)
#define sys_le40_to_cpu(value) (value)
#define sys_le48_to_cpu(value) (value)
#define sys_le64_to_cpu(value) (value)
#define sys_be16_to_cpu(value) sys_bswap16(value)
#define sys_be24_to_cpu(value) sys_bswap24(value)
#define sys_be32_to_cpu(value) __builtin_bswap32(value)
#define sys_be40_to_cpu(value) sys_bswap40(value)
#define sys_be48_to_cpu(value) sys_bswap48(value)
#define sys_be64_to_cpu(value) __builtin_bswap64(value)
#else
#define sys_le16_to_cpu(value) sys_bswap16(value)
#define sys_le24_to_cpu(value) sys_bswap24(value)
#define sys_le32_to_cpu(value) __builtin_bswap32(value)
#define sys_le40_to_cpu(value) sys_bswap40(value)
#define sys_le48_to_cpu(value) sys_bswap48(value)
#define sys_le64_to_cpu(value) __builtin_bswap64(value)
#define sys_be16_to_cpu(value) (value)
#define sys_be24_to_cpu(value) (value)
#define sys_be32_to_cpu(value) (value)
#define sys_be40_to_cpu(value) (value)
#define sys_be48_to_cpu(value) (value)
#define sys_be64_to_cpu(value) (value)
#endif

static inline void sys_put_le16(uint16_t value, uint8_t dst[2])
{
	dst[0] = value;
	dst[1] = value >> 8;
}

static inline void sys_put_be16(uint16_t value, uint8_t dst[2])
{
	dst[0] = value >> 8;
	dst[1] = value;
}

static inline void sys_put_le24(uint32_t value, uint8_t dst[3])
{
	dst[0] = value;
	dst[1] = value >> 8;
	dst[2] = value >> 16;
}

static inline void sys_put_be24(uint32_t value, uint8_t dst[3])
{
	dst[0] = value >> 16;
	dst[1] = value >> 8;
	dst[2] = value;
}

static inline void sys_put_le32(uint32_t value, uint8_t dst[4])
{
	for (size_t i = 0; i < 4; i++) {
		dst[i] = value >> (i * 8);
	}
}

static inline void sys_put_be32(uint32_t value, uint8_t dst[4])
{
	for (size_t i = 0; i < 4; i++) {
		dst[3 - i] = value >> (i * 8);
	}
}

static inline void sys_put_le48(uint64_t value, uint8_t dst[6])
{
	for (size_t i = 0; i < 6; i++) {
		dst[i] = value >> (i * 8);
	}
}

static inline void sys_put_le40(uint64_t value, uint8_t dst[5])
{
	for (size_t i = 0; i < 5; i++) {
		dst[i] = value >> (i * 8);
	}
}

static inline void sys_put_be40(uint64_t value, uint8_t dst[5])
{
	for (size_t i = 0; i < 5; i++) {
		dst[4 - i] = value >> (i * 8);
	}
}

static inline void sys_put_be48(uint64_t value, uint8_t dst[6])
{
	for (size_t i = 0; i < 6; i++) {
		dst[5 - i] = value >> (i * 8);
	}
}

static inline void sys_put_le64(uint64_t value, uint8_t dst[8])
{
	for (size_t i = 0; i < 8; i++) {
		dst[i] = value >> (i * 8);
	}
}

static inline void sys_put_be64(uint64_t value, uint8_t dst[8])
{
	for (size_t i = 0; i < 8; i++) {
		dst[7 - i] = value >> (i * 8);
	}
}

#ifdef __cplusplus
}
#endif

#endif /* LIBPEER_NET_BUF_PORT_H_ */
