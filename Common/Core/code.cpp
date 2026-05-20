#pragma once

#include <atomic>
#include <cstdint>
#include <vector>
#include <cassert>

constexpr uint32_t INVALID_INDEX = 0xFFFFFFFF;

/* =========================
   ABA-safe tagged pointer
   ========================= */
struct TaggedIndex
{
    uint32_t idx;
    uint32_t tag;
};

static inline uint64_t Encode(const TaggedIndex& v)
{
    return (uint64_t(v.tag) << 32) | v.idx;
}

static inline TaggedIndex Decode(uint64_t v)
{
    return TaggedIndex{
        uint32_t(v & 0xFFFFFFFF),
        uint32_t(v >> 32)
    };
}

/* =========================
   Free list node
   ========================= */
struct FreeNode
{
    uint32_t next = INVALID_INDEX;
};

/* =========================
   Lock-free FreeList
   ========================= */
class LockFreeFreeList
{
public:
    explicit LockFreeFreeList(uint32_t capacity)
        : m_Nodes(capacity)
    {
        assert(capacity > 0);

        // 링크드 리스트 초기화
        for (uint32_t i = 0; i < capacity - 1; ++i)
            m_Nodes[i].next = i + 1;

        m_Nodes[capacity - 1].next = INVALID_INDEX;

        TaggedIndex init{ 0, 0 };
        m_Head.store(Encode(init), std::memory_order_release);
    }

    LockFreeFreeList(const LockFreeFreeList&) = delete;
    LockFreeFreeList& operator=(const LockFreeFreeList&) = delete;

    /* =========================
       Acquire (Pop)
       ========================= */
    bool Acquire(uint32_t& outIdx)
    {
        uint64_t oldHead = m_Head.load(std::memory_order_acquire);

        while (true)
        {
            TaggedIndex head = Decode(oldHead);

            if (head.idx == INVALID_INDEX)
                return false; // empty

            FreeNode& node = m_Nodes[head.idx];

            TaggedIndex next{
                node.next,
                head.tag + 1
            };

            uint64_t newHead = Encode(next);

            if (m_Head.compare_exchange_weak(
                oldHead,
                newHead,
                std::memory_order_acq_rel,
                std::memory_order_relaxed))
            {
                outIdx = head.idx;
                return true;
            }
        }
    }

    /* =========================
       Release (Push)
       ========================= */
    void Release(uint32_t idx)
    {
        assert(idx < m_Nodes.size());

        uint64_t oldHead = m_Head.load(std::memory_order_acquire);

        while (true)
        {
            TaggedIndex head = Decode(oldHead);

            m_Nodes[idx].next = head.idx;

            TaggedIndex next{
                idx,
                head.tag + 1
            };

            uint64_t newHead = Encode(next);

            if (m_Head.compare_exchange_weak(
                oldHead,
                newHead,
                std::memory_order_release,
                std::memory_order_relaxed))
            {
                return;
            }
        }
    }

    uint32_t Capacity() const { return (uint32_t)m_Nodes.size(); }

private:
    std::vector<FreeNode>   m_Nodes;
    std::atomic<uint64_t>   m_Head;
};
