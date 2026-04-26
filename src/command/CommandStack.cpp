#include "CommandStack.h"

void CommandStack::execute(std::unique_ptr<Command> cmd) {
    // Discard any redo tail
    m_History.erase(m_History.begin() + m_Cursor, m_History.end());

    cmd->execute();
    m_History.push_back(std::move(cmd));
    ++m_Cursor;
}

void CommandStack::undo() {
    if (!canUndo()) return;
    --m_Cursor;
    m_History[m_Cursor]->undo();
}

void CommandStack::redo() {
    if (!canRedo()) return;
    m_History[m_Cursor]->execute();
    ++m_Cursor;
}
