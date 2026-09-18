#!/usr/bin/env bash
set -euo pipefail
net_buf_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
test_binary=$(mktemp /tmp/net-buf-test.XXXXXX)
trap 'rm -f -- "$test_binary"' EXIT
cc -std=gnu11 -pthread -g -Wall -Wextra -Werror -Wno-unused-parameter \
    -fsanitize=undefined -DCONFIG_NET_BUF_POOL_USAGE=1 -DCONFIG_NET_BUF_HARDENING=1 \
    -I"$net_buf_dir/tests/host" -I"$net_buf_dir/include" \
    "$net_buf_dir/buf.c" "$net_buf_dir/buf_simple.c" "$net_buf_dir/net_buf_port_freertos.c" \
    "$net_buf_dir/tests/test_net_buf.c" -o "$test_binary"
"$test_binary"
