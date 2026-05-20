#include "Token.h"
#include "Define/Error.h"
#include "Logger/Logger.h"

#include <format>
#include <Windows.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

SessionToken CToken::Create()
{   
    constexpr static DWORD len   = 32;
    constexpr static char  hex[] = "0123456789abcdef";
    
    BYTE bytes[len] = {};
    NTSTATUS status = BCryptGenRandom(
        nullptr,
        bytes,
        len,
        BCRYPT_USE_SYSTEM_PREFERRED_RNG
    );

    if (false == BCRYPT_SUCCESS(status)) {
		Logger.PushErr(ErrorType::CreateFaield, std::format("failed create token : {}", status));
        return "";
    }

    std::string out;
    out.reserve(size_t(len * 2));
    
    for (size_t i = 0; i < len; ++i)
    {
		out.push_back(hex[bytes[i] >> 4]);   // 좌측 4Bit 추출
		out.push_back(hex[bytes[i] & 0x0F]); // 우측 4Bit 추출
    }
    return out;	
}