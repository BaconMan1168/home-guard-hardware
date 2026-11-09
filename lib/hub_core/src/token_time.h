#pragma once

#include <cstdint>
#include <optional>
#include <string>

std::optional<int64_t> tokenIssuedAtSec(const std::string& jwt);
