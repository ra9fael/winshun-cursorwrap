#pragma once

#include "winshun.h"

#include <memory>

namespace cw {

std::unique_ptr<winshun::Plugin> makePlugin();

} // namespace cw
