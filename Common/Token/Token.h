#pragma once

#include "Define/Type.h"
#include "Pattern/Singleton.h"

class CToken final
{
public:
	static SessionToken Create();
};

