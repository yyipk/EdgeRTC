#include <zephyr/net_buf.h>

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
    assert(creates == 4);

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
