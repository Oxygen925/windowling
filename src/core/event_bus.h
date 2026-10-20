#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace dp::core {

// 단일 스레드(UI 스레드) 전용 타입별 발행/구독. 구독 해제는 토큰 소멸 시 자동.
class EventBus {
public:
    class Subscription {
    public:
        Subscription() = default;
        Subscription(Subscription&&) noexcept = default;
        Subscription& operator=(Subscription&&) noexcept = default;
        ~Subscription() { Reset(); }
        void Reset() {
            if (auto alive = alive_.lock()) (*alive)(id_);
            alive_.reset();
        }

    private:
        friend class EventBus;
        Subscription(std::weak_ptr<std::function<void(uint64_t)>> alive, uint64_t id) : alive_(std::move(alive)), id_(id) {}
        std::weak_ptr<std::function<void(uint64_t)>> alive_;
        uint64_t id_ = 0;
    };

    EventBus() : unsubscribe_(std::make_shared<std::function<void(uint64_t)>>([this](uint64_t id) { Remove(id); })) {}
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    template <class E>
    [[nodiscard]] Subscription Subscribe(std::function<void(const E&)> handler) {
        const uint64_t id = ++nextId_;
        handlers_[typeid(E)].push_back({id, [h = std::move(handler)](const void* e) { h(*static_cast<const E*>(e)); }});
        return Subscription(unsubscribe_, id);
    }

    // 발행 중 구독/해제가 일어나도 안전하도록 핸들러 목록 사본으로 호출한다
    template <class E>
    void Publish(const E& event) {
        auto it = handlers_.find(typeid(E));
        if (it == handlers_.end()) return;
        const auto snapshot = it->second;
        for (const auto& entry : snapshot)
            if (IsAlive(typeid(E), entry.id)) entry.fn(&event);
    }

    template <class E>
    size_t SubscriberCount() const {
        auto it = handlers_.find(typeid(E));
        return it == handlers_.end() ? 0 : it->second.size();
    }

private:
    struct Entry {
        uint64_t id;
        std::function<void(const void*)> fn;
    };
    void Remove(uint64_t id);
    bool IsAlive(std::type_index type, uint64_t id) const;

    std::unordered_map<std::type_index, std::vector<Entry>> handlers_;
    std::shared_ptr<std::function<void(uint64_t)>> unsubscribe_;
    uint64_t nextId_ = 0;
};

}  // namespace dp::core
