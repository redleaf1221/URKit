#pragma once

#include "mod_api.h"

#include <cstddef>
#include <string>

namespace URK::ModApiInternal {

int CopyName(const std::string &value, char *output, size_t size);


} // namespace URK::ModApiInternal

