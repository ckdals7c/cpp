#pragma once

#include "Define/Type.h"

constexpr static INT32 IOCP_DATA_BUFFER_SIZE = 1024;

constexpr static INT32 ACCEPT_COUNT          = 128;
constexpr static INT32 SESSION_COUNT         = 10000;
constexpr static INT32 SESSION_SENDER_COUNT  = 2;

constexpr static INT32 COMMON_POOL_CACHE_DIV = 4;
constexpr static INT32 COMMON_POOL_STORE_MUL = 2;