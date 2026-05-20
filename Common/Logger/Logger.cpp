#include <mutex>
#include <iostream>

#include "Logger.h"

struct CLogger::IMPL
{
	std::mutex Mutex;
};

CLogger::CLogger() : m_Impl(std::make_unique<CLogger::IMPL>())
{
}
CLogger::~CLogger() = default;

ErrorType CLogger::PushErr(const ErrorType& _err, const std::string_view _str)
{
	Print(_str);
	return _err;
}

void CLogger::Print(const std::string_view msg)
{
	std::lock_guard<std::mutex> lock(m_Impl->Mutex);
	std::cout << msg << std::endl;
}

