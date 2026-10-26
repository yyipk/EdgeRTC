#include <zephyr/net_buf.h>
#include <errno.h>

pthread_mutex_t net_buf_test_critical;
unsigned int net_buf_test_queue_creates;
static unsigned int released;
static uint8_t external_data[32];

struct external_owner {
    void (*release)(void *data);
};

static void release_external(void *data)
{
    assert(data == external_data);
    ++released;
}

static void destroy(struct net_buf *buf)
{
    if (buf->flags & NET_BUF_EXTERNAL_DATA) {
        struct external_owner *owner = net_buf_user_data(buf);
        owner->release(buf->__buf);
    }
    net_buf_destroy(buf);
}

NET_BUF_POOL_FIXED_DEFINE(fixed, 2, 64, sizeof(struct external_owner), destroy);
NET_BUF_POOL_FIXED_DEFINE(large_fixed, 2, 345600, 0, NULL);
NET_BUF_POOL_VAR_DEFINE(variable, 2, 1024, 0, NULL);
NET_BUF_POOL_VAR_ALIGN_DEFINE(aligned, 2, 1024, 0, NULL, 32);
NET_BUF_POOL_HEAP_DEFINE(dynamic, 2, 0, NULL);
NET_BUF_POOL_ARENA_DEFINE(arena);
static uint8_t arena_memory[4096]
    __attribute__((section(".dma_buffer"), aligned(8)));

static void *wait_for_buffer(void *unused)
{
    (void)unused;
    return net_buf_alloc(&fixed, K_SECONDS(1));
}

static void test_large_fixed_pool(void)
{
    struct net_buf *a = net_buf_alloc_fixed(&large_fixed, K_NO_WAIT);
    struct net_buf *b = net_buf_alloc_fixed(&large_fixed, K_NO_WAIT);
    struct net_buf_simple_state full, pulled;
    uint8_t *data;

    assert(a && b && a != b && !net_buf_alloc_fixed(&large_fixed, K_NO_WAIT));
    assert(a->size == 345600 && b->size == 345600);
    assert(net_buf_max_len(a) == 345600);
    data = net_buf_add(a, 345600);
    assert(data == a->__buf && a->len == 345600 && net_buf_tailroom(a) == 0);
    memset(data, 0x5a, a->len);
    net_buf_simple_save(&a->b, &full);
    net_buf_pull(a, 70000);
    assert(net_buf_max_len(a) == 275600);
    net_buf_simple_save(&a->b, &pulled);
    net_buf_pull(a, 70000);
    net_buf_simple_restore(&a->b, &pulled);
    assert(a->data == data + 70000 && a->len == 275600);
    net_buf_simple_restore(&a->b, &full);
    assert(a->data == data && a->len == 345600 && net_buf_is_valid(a));
    assert(net_buf_append_bytes(b, a->len, a->data, K_NO_WAIT, NULL, NULL) == 345600);
    assert(b->len == 345600 && b->frags == NULL && !memcmp(a->data, b->data, a->len));
    net_buf_unref(a);
    a = net_buf_alloc_fixed(&large_fixed, K_NO_WAIT);
    assert(a && a->data == data && a->size == 345600 && a->len == 0);
    net_buf_unref(a);
    net_buf_unref(b);
}

static void test_runtime_pools(void)
{
    struct net_buf_pool first = {0}, second = {0};
    size_t first_size = net_buf_pool_arena_size(2, 64, 0);
    size_t second_size = net_buf_pool_arena_size(68, 32, 0);
    void *first_arena = malloc(first_size);
    void *second_arena = malloc(second_size);
    struct net_buf *a, *b, *buffers[68];

    assert(first_arena && second_arena);
    assert(!net_buf_pool_arena_size(0, 64, 0));
    assert(!net_buf_pool_arena_size(2, 0, 0));
    assert(net_buf_pool_init(&first, first_arena, first_size - 1, 2, 64, 0, NULL) == -ENOMEM);
    assert(!first.free.handle);
    assert(!net_buf_pool_init(&first, first_arena, first_size, 2, 64, 0, NULL));
    assert(!net_buf_pool_init(&second, second_arena, second_size, 68, 32, 0, NULL));
    a = net_buf_alloc(&first, K_NO_WAIT);
    b = net_buf_alloc(&first, K_NO_WAIT);
    assert(a && b && !net_buf_alloc(&first, K_NO_WAIT));
    assert(net_buf_pool_get(a->pool_id) == &first);
    assert(net_buf_pool_deinit(&first) == -EBUSY);
    for (size_t i = 0; i < 68; ++i) {
        buffers[i] = net_buf_alloc(&second, K_NO_WAIT);
        assert(buffers[i] && buffers[i]->size == 32);
        assert(net_buf_pool_get(buffers[i]->pool_id) == &second);
        assert(buffers[i]->pool_id != a->pool_id);
    }
    assert(!net_buf_alloc(&second, K_NO_WAIT));
    net_buf_unref(a);
    a = net_buf_alloc(&first, K_NO_WAIT);
    assert(a && a->size == 64);
    net_buf_unref(a);
    net_buf_unref(b);
    assert(!net_buf_pool_deinit(&first));
    assert(!net_buf_pool_deinit(&first));
    free(first_arena);
    for (size_t i = 0; i < 68; ++i) net_buf_unref(buffers[i]);
    assert(!net_buf_pool_deinit(&second));
    free(second_arena);

    for (size_t i = 1; i <= 300; ++i) {
        uint16_t count = (uint16_t)(i % 4 + 1);
        size_t size = net_buf_pool_arena_size(count, 16, 0);
        void *memory = malloc(size);
        assert(memory && !net_buf_pool_init(&first, memory, size, count, 16, 0, NULL));
        a = net_buf_alloc(&first, K_NO_WAIT);
        assert(a && a->size == 16);
        net_buf_unref(a);
        assert(!net_buf_pool_deinit(&first));
        free(memory);
    }
}

int main(void)
{
    pthread_mutexattr_t attr;
    pthread_t waiter;
    struct net_buf *a, *b, *result;
    unsigned int creates;
    TickType_t start;

    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&net_buf_test_critical, &attr);
    libpeer_net_buf_init();
    creates = net_buf_test_queue_creates;
    assert(creates == 5);
    test_large_fixed_pool();

    a = net_buf_alloc(&fixed, K_NO_WAIT);
    b = net_buf_alloc_with_data(&fixed, external_data, sizeof(external_data), K_NO_WAIT);
    assert(a && b && a != b && b->data == external_data);
    ((struct external_owner *)net_buf_user_data(b))->release = release_external;
    assert(!net_buf_alloc(&fixed, K_NO_WAIT));
    start = xTaskGetTickCount();
    assert(!net_buf_alloc(&fixed, K_MSEC(20)));
    assert(xTaskGetTickCount() - start >= 19);
    assert(net_buf_ref(b) == b);
    net_buf_unref(b);
    assert(released == 0);
    net_buf_unref(b);
    assert(released == 1);
    b = net_buf_alloc(&fixed, K_NO_WAIT);
    assert(b && b->data != external_data);

    assert(!pthread_create(&waiter, NULL, wait_for_buffer, NULL));
    struct timespec pause = { .tv_nsec = 20000000 };
    nanosleep(&pause, NULL);
    net_buf_unref(a);
    assert(!pthread_join(waiter, (void **)&result));
    assert(result == a);
    net_buf_unref(result);
    net_buf_unref(b);
    for (unsigned int i = 0; i < 100; ++i) {
        a = net_buf_alloc(&fixed, K_NO_WAIT);
        assert(a);
        net_buf_unref(a);
    }
    assert(net_buf_test_queue_creates == creates);

    assert(net_buf_pool_init(&arena, arena_memory, sizeof(arena_memory),
                             2, 64, 0, NULL) == 0);
    assert((uint8_t *)arena.free.control_storage >= arena_memory &&
           (uint8_t *)arena.free.control_storage < arena_memory + sizeof(arena_memory));
    a = net_buf_alloc(&arena, K_NO_WAIT);
    assert(a && a->__buf >= arena_memory &&
           a->__buf + a->size <= arena_memory + sizeof(arena_memory));
    net_buf_add_u8(a, 42);
    net_buf_unref(a);
    assert(!net_buf_pool_deinit(&arena));
    assert(!net_buf_pool_init(&arena, arena_memory, sizeof(arena_memory), 3, 48, 0, NULL));
    a = net_buf_alloc(&arena, K_NO_WAIT);
    assert(a && a->size == 48 && net_buf_pool_get(a->pool_id) == &arena);
    net_buf_unref(a);
    assert(!net_buf_pool_deinit(&arena));
    test_runtime_pools();

    struct net_buf_pool *pools[] = { &variable, &aligned, &dynamic };
    for (unsigned int i = 0; i < 3; ++i) {
        a = net_buf_alloc_len(pools[i], 64, K_NO_WAIT);
        assert(a);
        if (i == 1) {
            assert((uintptr_t)a->data % 32 == 0);
        }
        net_buf_add_u8(a, 42);
        b = net_buf_clone(a, K_NO_WAIT);
        assert(b && b->data == a->data && b->data[0] == 42);
        net_buf_unref(a);
        assert(b->data[0] == 42);
        net_buf_unref(b);
    }
    puts("net_buf: exhaustion, timeout, wakeup, recycling, external release and aligned/variable/heap pools passed");
    return 0;
}
