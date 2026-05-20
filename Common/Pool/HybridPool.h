#pragma once

#include <list>
#include <mutex>
#include <vector>
#include <memory>

#include "Define/Error.h"

template <typename T>
class CHybridPool : public std::enable_shared_from_this<CHybridPool<T>>
{
	struct SLocal
	{
	public:
		SLocal(std::weak_ptr<CHybridPool<T>> _pool) : Owner(std::move(_pool)) {}
		~SLocal()
		{
			std::shared_ptr<CHybridPool<T>> owner = Owner.lock();
			if (nullptr == owner) {
				return;
			}

			if (true == owner->IsDestroyed()) {
				return;
			}

			std::vector<void*> copy;
			copy.swap(Cache);

			std::lock_guard<std::mutex> lock(owner->m_Mutex);
			owner->m_Data.insert(owner->m_Data.end(), copy.begin(), copy.end());
		}

	public:
		std::weak_ptr<CHybridPool<T>> Owner;
		std::vector<void*>            Cache{};
	};
	using LocalVec = std::vector<SLocal>;

public:
	CHybridPool() = default;
	~CHybridPool()
	{
		m_IsDead.store(true, std::memory_order_release);

		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Data.clear();
		m_Buffer.clear();
	}

	CHybridPool(CHybridPool&&) = delete;
	CHybridPool(const CHybridPool&) = delete;

	CHybridPool& operator=(CHybridPool&&) = delete;
	CHybridPool& operator=(const CHybridPool&) = delete;

public:
	ErrorType Init(const size_t _cacheCnt, const size_t _allocCnt, const size_t _storeCnt)
	{
		if (0 >= _allocCnt ||
			0 >= _storeCnt) {
			return ErrorType::InvalidParameter;
		}

		if (false == (_allocCnt <= _storeCnt && _allocCnt >= _cacheCnt)) {
			return ErrorType::InvalidParameter;
		}

		if (true == m_IsReady.exchange(true, std::memory_order_acq_rel)) {
			return ErrorType::InitFailed;
		}

		std::lock_guard<std::mutex> lock(m_Mutex);

		m_Data.clear();
		m_Data.reserve(_allocCnt);

		m_Buffer.clear();		
		m_Buffer.reserve(_storeCnt);

		for (size_t i = 0; i < _allocCnt; ++i)
		{
			m_Buffer.emplace_back();
			{
				void* ptr = &m_Buffer.back();
				m_Data.emplace_back(ptr);
			}
		}

		m_CacheCnt = _cacheCnt;
		m_StoreCnt = _storeCnt;
		return ErrorType::Empty;
	}

	template<typename... Args>
	std::shared_ptr<T> Acquire(Args&&... _args)
	{
		if (true == IsDestroyed()) {
			return nullptr;
		}
		SLocal& local = GetLocal();

		std::shared_ptr<T> obj = Export(local.Cache, std::forward<Args>(_args)...);
		if (nullptr != obj) {
			return obj;
		}

		std::lock_guard<std::mutex> lock(m_Mutex);

		obj = Export(m_Data, std::forward<Args>(_args)...);
		if (nullptr == obj) {
			if (true == Grow()) {
				obj = Export(m_Data, std::forward<Args>(_args)...);
			}
		}
		
		if (nullptr != obj) {
			return obj;
		}	
		return nullptr;
	}

private:
	bool Grow()
	{
		if (m_Buffer.size() >= m_StoreCnt) {
			return false;
		}

		size_t cur = m_Buffer.size();
		size_t add = cur / 2;

		if ((cur + add) > m_StoreCnt) {
			add = m_StoreCnt - cur;
		}

		if (add == 0) {
			return false;
		}

		for (size_t i = 0; i < add; ++i)
		{
			m_Buffer.emplace_back();
			m_Data.emplace_back(&m_Buffer.back());
		}
		return true;
	}

	void Release(T* _obj)
	{
		if (nullptr == _obj) {
			return;
		}

		if (IsDestroyed()) {
			return;
		}

		SLocal& local = GetLocal();
		if (local.Cache.size() < m_CacheCnt) {
			local.Cache.emplace_back(std::bit_cast<void*>(_obj));
		}
		else
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Data.emplace_back(std::bit_cast<void*>(_obj));
		}
	}

	bool IsDestroyed() {
		return m_IsDead.load(std::memory_order_acquire);
	}

	SLocal& GetLocal()
	{
		thread_local LocalVec locals{};
		for (auto itr = locals.begin(); itr != locals.end();)
		{
			std::shared_ptr<CHybridPool<T>> owner = itr->Owner.lock();
			if (nullptr == owner) {
				itr = locals.erase(itr);
			}
			else if (true == owner->IsDestroyed()) {
				itr = locals.erase(itr);
			}
			else {
				++itr;
			}
		}

		for (auto& local : locals)
		{
			std::shared_ptr<CHybridPool<T>> owner = local.Owner.lock();
			if (this == owner.get()) {
				return local;
			}
		}

		locals.emplace_back(SLocal(this->shared_from_this()));
		return locals.back();
	}

	template<typename... Args>
	std::shared_ptr<T> Export(std::vector<void*>& _container, Args&&... _args)
	{if (false == _container.empty())
		{
			void* memory = _container.back();
			_container.pop_back();

			try {				
				T* obj = std::bit_cast<T*>(memory);
				std::construct_at(obj, std::forward<Args>(_args)...);

				std::shared_ptr<CHybridPool<T>> pool = this->shared_from_this();
				return std::shared_ptr<T>(obj, [pool](T* ptr)
					{
						std::destroy_at(ptr);
						pool->Release(ptr);
					}
				);
			}
			catch (...) {
				_container.push_back(memory);
				throw;
			}
		}
		return nullptr;
	}

private:
	std::mutex         m_Mutex;
	std::vector<void*> m_Data{};
	std::atomic<bool>  m_IsDead   = false;
	std::atomic<bool>  m_IsReady  = false;
	size_t             m_CacheCnt = 0;
	size_t             m_StoreCnt = 0;

	using DataType = std::aligned_storage_t<sizeof(T), alignof(T)>;
	std::vector<DataType> m_Buffer{};
};

template <typename T>
using HybridPoolPtr = std::shared_ptr<CHybridPool<T>>;

#define MakeHybridPool(T) std::shared_ptr<CHybridPool<T>>(new CHybridPool<T>())