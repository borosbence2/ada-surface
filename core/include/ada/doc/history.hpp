#pragma once

#include "ada/doc/scene.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <deque>
#include <string>

namespace ada {

/// Undo and redo over whole-scene snapshots. A scene is a few kilobytes of
/// JSON, so snapshots are cheaper to get right than inverse operations, and
/// they cover every kind of edit, including ones made to the file by hand.
class History {
public:
    static constexpr std::size_t kDefaultCapacity = 100;

    explicit History(std::size_t capacity = kDefaultCapacity);

    /// Records the state from before an edit, with what the edit did, which
    /// undo and redo then report. Clears the redo stack; the oldest snapshot
    /// is dropped past capacity.
    void record(const Scene& before, std::string label = {});

    bool can_undo() const noexcept { return !undo_.empty(); }

    bool can_redo() const noexcept { return !redo_.empty(); }

    /// The state to go back to; `current` becomes redoable. Throws
    /// `std::logic_error` when there is nothing to undo.
    Scene undo(const Scene& current);

    /// The state to go forward to; `current` becomes undoable again. Throws
    /// `std::logic_error` when there is nothing to redo.
    Scene redo(const Scene& current);

    /// What `undo` would take back, and what `redo` would make again; empty
    /// when there is nothing (or the edit had no label).
    const std::string& undo_label() const noexcept;
    const std::string& redo_label() const noexcept;

private:
    struct Entry {
        nlohmann::json scene;
        std::string label;
    };

    std::size_t capacity_;
    std::deque<Entry> undo_;
    std::deque<Entry> redo_;
};

} // namespace ada
