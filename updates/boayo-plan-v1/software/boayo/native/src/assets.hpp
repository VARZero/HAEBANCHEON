#pragma once
#include "ui.hpp"
#include <optional>
namespace boayo {
std::optional<Surface> asset_surface(const J &key);
J asset_status();
} // namespace boayo
