#pragma once
#include <memory>
#include <vector>
#include "Command.h"

class CommandStack {
public:
    // Takes ownership, calls execute(), clears redo history.
    void execute(std::unique_ptr<Command> cmd);

    void undo();
    void redo();
    void clear() { m_History.clear(); m_Cursor = 0; }

    bool canUndo() const { return m_Cursor > 0; }
    bool canRedo() const { return m_Cursor < static_cast<int>(m_History.size()); }

private:
    std::vector<std::unique_ptr<Command>> m_History;
    int m_Cursor = 0;
};
