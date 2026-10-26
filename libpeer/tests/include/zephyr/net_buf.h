#ifndef SCTP_TEST_NET_BUF_H
#define SCTP_TEST_NET_BUF_H
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
struct net_buf_pool {
  size_t data_size;
  size_t buf_count;
  size_t used;
  struct net_buf* bufs;
};
struct net_buf {
  uint8_t* data;
  uint16_t len;
  uint16_t size;
  uint8_t ref;
  uint8_t tracked;
  struct net_buf* frags;
  struct net_buf_pool* pool;
};
#define K_NO_WAIT 0
#define NET_BUF_POOL_HEAP_DEFINE(name, count, user, destroy) \
  struct net_buf_pool name = { 0 }
#define NET_BUF_POOL_FIXED_DEFINE(name, count, bytes, user, destroy) \
  struct net_buf_pool name = { .data_size = (bytes) }
static size_t fixture_live, fixture_allocations;
static int fixture_fail_alloc;
static int fixture_fail_pool_init;
static inline void libpeer_net_buf_init(void) {}
static inline size_t net_buf_pool_arena_size(uint16_t count, uint16_t data_size,
                                             uint8_t user_data_size) {
  (void)user_data_size;
  return count * (sizeof(struct net_buf) + data_size);
}
static inline int net_buf_pool_init(struct net_buf_pool* pool, void* arena,
                                    size_t arena_size, uint16_t count,
                                    uint16_t data_size, uint8_t user_data_size,
                                    void (*destroy)(struct net_buf*)) {
  (void)destroy;
  if (fixture_fail_pool_init && --fixture_fail_pool_init == 0) return -ENOMEM;
  assert(arena_size >= net_buf_pool_arena_size(count, data_size, user_data_size));
  *pool = (struct net_buf_pool){ .data_size = data_size, .buf_count = count, .bufs = arena };
  uint8_t* data = (uint8_t*)(pool->bufs + count);
  for (size_t i = 0; i < count; i++) {
    pool->bufs[i] = (struct net_buf){ .data = data + i * data_size,
                                    .size = data_size, .pool = pool };
  }
  return 0;
}
static inline int net_buf_pool_deinit(struct net_buf_pool* pool) {
  if (pool->used) return -EBUSY;
  memset(pool, 0, sizeof(*pool));
  return 0;
}
static inline struct net_buf* net_buf_alloc(struct net_buf_pool* pool, int wait);
static inline struct net_buf* net_buf_alloc_len(struct net_buf_pool* pool, size_t size, int wait) {
  (void)pool;
  (void)wait;
  assert(size <= UINT16_MAX);
  if (fixture_fail_alloc) { fixture_fail_alloc = 0; return NULL; }
  if (pool->bufs) {
    if (size > pool->data_size) return NULL;
    struct net_buf* b = net_buf_alloc(pool, wait);
    if (b) {
      b->tracked = 1;
      fixture_live++;
      fixture_allocations++;
    }
    return b;
  }
  struct net_buf* b = calloc(1, sizeof(*b));
  assert(b);
  b->data = malloc(size ? size : 1);
  assert(b->data);
  memset(b->data, 0xa5, size ? size : 1);
  b->size = size;
  b->ref = 1;
  b->tracked = 1;
  fixture_live++;
  fixture_allocations++;
  return b;
}
static inline struct net_buf* net_buf_alloc(struct net_buf_pool* pool, int wait) {
  (void)wait;
  if (pool->bufs) {
    for (size_t i = 0; i < pool->buf_count; i++) {
      struct net_buf* b = &pool->bufs[i];
      if (!b->ref) {
        b->ref = 1;
        b->len = 0;
        b->size = pool->data_size;
        b->tracked = 0;
        b->frags = NULL;
        pool->used++;
        return b;
      }
    }
    return NULL;
  }
  struct net_buf* b = calloc(1, sizeof(*b));
  assert(b);
  b->data = malloc(pool->data_size ? pool->data_size : 1);
  assert(b->data);
  b->size = pool->data_size;
  b->ref = 1;
  return b;
}
static inline void* net_buf_add(struct net_buf* b, size_t size) {
  assert(b && b->ref && b->len <= b->size && size <= (size_t)(b->size - b->len));
  void* result = b->data + b->len;
  b->len += size;
  return result;
}
static inline void* net_buf_add_mem(struct net_buf* b, const void* data, size_t size) {
  void* result = net_buf_add(b, size);
  if (size) memcpy(result, data, size);
  return result;
}
static inline struct net_buf* net_buf_ref(struct net_buf* b) {
  assert(b && b->ref && b->ref < 255);
  b->ref++;
  return b;
}
static inline void net_buf_unref(struct net_buf* b) {
  assert(b && b->ref);
  if (--b->ref) return;
  struct net_buf* next = b->frags;
  uint8_t tracked = b->tracked;
  if (b->pool) {
    b->pool->used--;
    if (tracked) fixture_live--;
    if (next) net_buf_unref(next);
    return;
  }
  free(b->data);
  free(b);
  if (tracked) fixture_live--;
  if (next) net_buf_unref(next);
}
#endif
