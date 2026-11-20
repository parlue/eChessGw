#pragma once
#include <mutex>
// Reader replies and background search lines share stdout. Hold one guard
// across each complete message, including its PV, never across search joins.
inline std::mutex uciOutputMutex;
struct UciOutputGuard { std::lock_guard<std::mutex> lock{uciOutputMutex}; };
