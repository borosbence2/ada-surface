#include "ada/doc/history.hpp"

#include <stdexcept>
#include <utility>

namespace ada {

namespace {

const std::string kNoLabel;

} // namespace

History::History(std::size_t capacity)
    : capacity_(capacity == 0 ? 1 : capacity) {}

void History::record(const Scene& before, std::string label) {
    undo_.push_back({scene_to_json(before), std::move(label)});
    if (undo_.size() > capacity_) {
        undo_.pop_front();
    }
    redo_.clear();
}

Scene History::undo(const Scene& current) {
    if (undo_.empty()) {
        throw std::logic_error("nothing to undo");
    }
    Entry previous = std::move(undo_.back());
    undo_.pop_back();
    redo_.push_back({scene_to_json(current), previous.label});
    return scene_from_json(previous.scene);
}

Scene History::redo(const Scene& current) {
    if (redo_.empty()) {
        throw std::logic_error("nothing to redo");
    }
    Entry next = std::move(redo_.back());
    redo_.pop_back();
    undo_.push_back({scene_to_json(current), next.label});
    return scene_from_json(next.scene);
}

const std::string& History::undo_label() const noexcept {
    return undo_.empty() ? kNoLabel : undo_.back().label;
}

const std::string& History::redo_label() const noexcept {
    return redo_.empty() ? kNoLabel : redo_.back().label;
}

} // namespace ada
