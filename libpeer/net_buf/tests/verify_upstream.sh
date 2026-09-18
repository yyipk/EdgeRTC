#!/usr/bin/env bash
set -euo pipefail

net_buf_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
revision=8445f9958c756bc91931882c07e174e9650e55c9
base_url="https://raw.githubusercontent.com/zephyrproject-rtos/zephyr/$revision"

# Ignore only include directives; every other line must match upstream.
verify() {
    local local_path=$1 upstream_path=$2 upstream
    upstream=$(curl --fail --silent --show-error --location "$base_url/$upstream_path")
    diff -u --label "$upstream_path@$revision" --label "$local_path" \
        <(printf '%s\n' "$upstream" | sed '/^[[:space:]]*#[[:space:]]*include[[:space:]]/d') \
        <(sed '/^[[:space:]]*#[[:space:]]*include[[:space:]]/d' "$net_buf_dir/$local_path")
}

verify buf.c lib/net_buf/buf.c
verify buf_simple.c lib/net_buf/buf_simple.c
verify include/zephyr/net_buf.h include/zephyr/net_buf.h
printf 'Zephyr %s: net_buf implementation matches upstream.\n' "$revision"
