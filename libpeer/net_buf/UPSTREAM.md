Source: https://github.com/zephyrproject-rtos/zephyr

Revision: `8445f9958c756bc91931882c07e174e9650e55c9` (main, retrieved 2026-09-16).

Upstream files:

- `lib/net_buf/buf.c` → `buf.c`
- `lib/net_buf/buf_simple.c` → `buf_simple.c`
- `include/zephyr/net_buf.h` → `include/zephyr/net_buf.h`

Keep these implementations unchanged. Only include directives may be adapted.
FreeRTOS compatibility belongs in the port layer.

Variable and heap pool payloads use system `kmalloc/kmemalign/kfree`.
The variable-pool size argument does not reserve or limit a private heap in
this port. Payload allocation is immediate (NULL on failure); only descriptor
allocation supports waiting. Fixed pools retain static payload storage.

The adapter uses `xQueueCreateStatic()` and pointer-sized queue entries for each
pool, with capacity equal to its descriptor count. Call `libpeer_net_buf_init()`
once in task context before using any pool. Queue operations do not initialize
or scan pools. Blocking allocation is task-only.

For caller-owned memory, declare `NET_BUF_POOL_ARENA_DEFINE(pool)` and call
`net_buf_pool_init()` with the arena. Descriptors, payloads, and queue storage
are carved from that arena; this path does not allocate from the system heap.

Check against the pinned upstream revision:

```sh
bash tests/verify_upstream.sh
bash tests/run_host.sh
```

The host check uses a pthread-based FreeRTOS API test double. It exercises pool
exhaustion, timeout/wakeup, descriptor reuse, external data release, and
variable/aligned/heap pools. It does not replace a test on FreeRTOS hardware.

External data is borrowed by `net_buf_alloc_with_data()`. Store its release
callback/context in pool user data and invoke it from the pool destroy callback
before calling `net_buf_destroy()`. Share an existing buffer with `net_buf_ref()`;
`net_buf_clone()` may copy the payload.
