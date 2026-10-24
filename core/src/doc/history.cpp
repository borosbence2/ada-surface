#include "ada/doc/history.hpp"

#include <stdexcept>

namespace ada {

History::History(std::size_t capacity)
    : capacity_(capacity == 0 ? 1 : capacity) {}

void History::record(const Scene& before) {
    undo_.push_back(scene_to_json(before));
    if (undo_.size() > capacity_) {
        undo_.pop_front();
    }
    redo_.clear();
}

Scene History::undo(const Scene& current) {
    if (undo_.empty()) {
        throw std::logic_error("nothing to undo");
    }
    redo_.push_back(scene_to_json(current));
    nlohmann::json previous = std::move(undo_.back());
    undo_.pop_back();
    return scene_from_json(previous);
}

Scene History::redo(const Scene& current) {
    if (redo_.empty()) {
        throw std::logic_error("nothing to redo");
    }
    undo_.push_back(scene_to_json(current));
    nlohmann::json next = std::move(redo_.back());
    redo_.pop_back();
    return scene_from_json(next);
}

} // namespace ada
