#include "yk/runtime/Services.hpp"
#include "yk/runtime/GameContext.hpp"

namespace yk {
void Services::start(Service &service) {
    service.onStart(*owner_);
}
void Services::tick(UpdatePhase phase, float seconds) {
    // Snapshot: a service started during this pass joins the next tick.
    std::vector<Service *> snapshot;
    snapshot.reserve(ordered_.size());
    for (const auto &service : ordered_)
        if (service->phase() == phase)
            snapshot.push_back(service.get());
    for (Service *service : snapshot)
        service->onFixedUpdate(*owner_, seconds);
}
void Services::shutdown() {
    // A service may ask for another one while shutting down; iterate a snapshot.
    std::vector<Service *> snapshot;
    for (const auto &service : ordered_)
        snapshot.push_back(service.get());
    for (auto it = snapshot.rbegin(); it != snapshot.rend(); ++it)
        (*it)->onShutdown(*owner_);
    byType_.clear();
    while (!ordered_.empty()) // Reverse creation order, so later services go first.
        ordered_.pop_back();
}
std::vector<Service *> Services::all() const {
    std::vector<Service *> result;
    result.reserve(ordered_.size());
    for (const auto &service : ordered_)
        result.push_back(service.get());
    return result;
}
Service *Services::byKey(const std::string &saveKey) const {
    if (saveKey.empty())
        return nullptr;
    for (const auto &service : ordered_)
        if (service->saveKey() == saveKey)
            return service.get();
    return nullptr;
}
} // namespace yk
