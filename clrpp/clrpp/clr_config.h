#pragma once

#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace clr
{
template <typename T>
using non_owning_ptr = T*;
} // namespace clr
