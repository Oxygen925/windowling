#include "core/event_bus.h"

#include <algorithm>

namespace dp::core {

void EventBus::Remove(uint64_t id) {
    for (auto& [type, list] : handlers_)
        std::erase_if(list, [id](const Entry& e) { return e.id == id; });
}

bool EventBus::IsAlive(std::type_index type, uint64_t id) const {
    auto it = handlers_.find(type);
    if (it == handlers_.end()) return false;
    return std::any_of(it->second.begin(), it->second.end(), [id](const Entry& e) { return e.id == id; });
}

}  // namespace dp::core
