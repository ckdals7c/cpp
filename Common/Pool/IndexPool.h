#pragma once

#include <list>
#include <mutex>
#include <vector>
#include <memory>

#include "Define/Type.h"
#include "Define/Error.h"
#include "Logger/Logger.h"

struct SIndexPoolSlot
{
	INT32             Index = 0;
	std::atomic<bool> Ready = true;
};

template <typename T>
class CIndexPoolData
{
public:
	UINT32 GetGen() const {
		return m_Gen.load(std::memory_order_relaxed);
	}

	UINT32 GetIdx() const {
		return m_Idx;
	}

	T* GetData() {
		return &m_Data;
	}

	bool IsActive() const {
		return m_IsActive.load(std::memory_order_acquire);
	}

	void SetIdx(const UINT32 _idx) {
		m_Idx = _idx;
	}

	void IncGen() {
		m_Gen.fetch_add(1, std::memory_order_relaxed);
	}

	bool Activate()
	{
		bool isActive = false;
		return m_IsActive.compare_exchange_strong(isActive, true, std::memory_order_release);
	}

	bool Deactivate() 
	{
		bool isActive = true;
		return m_IsActive.compare_exchange_strong(isActive, false, std::memory_order_release);
	}

private:
	T                   m_Data;
	INT32               m_Idx     = 0;
	std::atomic<UINT32> m_Gen     = 0;
	std::atomic<bool>   m_IsActive = false;	
};

template <typename T>
class CIndexPool
{
	constexpr static INT32 INVALID_IDX = -1;

	constexpr static IndexPoolKey IDX_BITS = 32;
	constexpr static IndexPoolKey GEN_BITS = 32;

	constexpr static IndexPoolKey IDX_MASK = (1ull << IDX_BITS) - 1;
	constexpr static IndexPoolKey GEN_MASK = (1ull << GEN_BITS) - 1;

public:
	CIndexPool(const INT32 _count) : m_Top(_count - 1), m_Idxs(_count), m_Data(_count)
	{
		if (0 >= _count) {
			return;
		}

		for (INT32 i = 0; i < _count; ++i)
		{
			CIndexPoolData<T>& data = m_Data[i];
			data.SetIdx(i);
			
			m_Idxs[i].Index = i;
		}		
	}

	CIndexPool(CIndexPool&&) = delete;
	CIndexPool(const CIndexPool&) = delete;

	CIndexPool& operator=(CIndexPool&&) = delete;
	CIndexPool& operator=(const CIndexPool&) = delete;

public:	
	std::pair<IndexPoolKey, T*> Acquire()
	{	
		while (true)
		{
			INT64 top = m_Top.load(std::memory_order_acquire);
			if (0 > top) {
				return std::pair<IndexPoolKey, T*>(0, nullptr);
			}

			if (true == m_Top.compare_exchange_weak(top, top - 1, std::memory_order_acquire))
			{
				SIndexPoolSlot& slot = m_Idxs[top];
				while (false == slot.Ready.load(std::memory_order_acquire)) {
					_mm_pause();
				}

				slot.Ready.store(false, std::memory_order_relaxed);
				INT32 idx = slot.Index;				

				CIndexPoolData<T>& data = m_Data[idx];
				if (false == data.Activate())
				{
					// Release 시점에 Deactivate 후 Top을 변경하기에 실질적으로 해당 부분으로 진입하는 경우 로직 에러
					Logger.PushErr(ErrorType::InvalidLogic, "index pool data is active");
					return std::pair<IndexPoolKey, T*>(0, nullptr);
				}

				IndexPoolKey key = MakeKey(&data);
				return std::pair<IndexPoolKey, T*>(key, data.GetData());
			}
		}
	}

	bool Release(const IndexPoolKey& _key)
	{
		const UINT32 gen = GetGenByKey(_key);
		const UINT32 idx = GetIdxByKey(_key);

		if (idx >= m_Data.size()) {
			return false;
		}

		CIndexPoolData<T>& data = m_Data[idx];
		if (gen != data.GetGen()) {
			return false;
		}

		if (true == data.Deactivate())
		{
			data.IncGen();
			while (true)
			{
				INT64 top = m_Top.load(std::memory_order_relaxed);
				if (false == m_Top.compare_exchange_weak(top, top + 1, std::memory_order_release, std::memory_order_relaxed))
					continue;

				SIndexPoolSlot& slot = m_Idxs[top + 1];
				slot.Index = idx;
				slot.Ready.store(true, std::memory_order_release);
				break;
			}
			return true;
		}
		else
		{
			return false;
		}
	}

	T* Find(const IndexPoolKey& _key)
	{
		const UINT32 gen = GetGenByKey(_key);
		const UINT32 idx = GetIdxByKey(_key);
		
		if (idx >= m_Data.size()) {
			return nullptr;
		}

		CIndexPoolData<T>& data = m_Data[idx];
		if (gen != data.GetGen()) {
			return nullptr;
		}

		if (false == data.IsActive()) {
			return nullptr;
		}
		return data.GetData();
	}

private:
	IndexPoolKey MakeKey(const CIndexPoolData<T>* _data) const 
	{
		const IndexPoolKey gen = _data->GetGen();
		const IndexPoolKey idx = _data->GetIdx();
		return (gen << IDX_BITS) | IndexPoolKey(idx);
	}

	UINT32 GetIdxByKey(const IndexPoolKey& _key) const {
		return UINT32(_key & IDX_MASK);
	}

	UINT32 GetGenByKey(const IndexPoolKey& _key) const {
		return UINT32(_key >> IDX_BITS) & GEN_MASK;
	}

public:	
	std::vector<CIndexPoolData<T>> m_Data;
	std::vector<SIndexPoolSlot>    m_Idxs;
	std::atomic<INT64>             m_Top;
};