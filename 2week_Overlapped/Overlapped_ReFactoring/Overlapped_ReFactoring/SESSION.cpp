#include "stdafx.h"
#include "SESSION.h"

std::unordered_map<int, std::unique_ptr<SESSION>> clients;
