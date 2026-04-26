#pragma once
#include "Command.h"
#include "world/CelestialBody.h"
#include <algorithm>

class DeleteEntityCommand : public Command {
public:
    DeleteEntityCommand(std::vector<WorldEntity>& entities, const std::string& id)
        : m_Entities(entities), m_Id(id) {}

    void execute() override {
        auto it = std::find_if(m_Entities.begin(), m_Entities.end(),
            [&](const WorldEntity& e) { return e.id == m_Id; });
        if (it != m_Entities.end()) {
            m_Saved = *it;
            m_Entities.erase(it);
        }
    }

    void undo() override {
        m_Entities.push_back(m_Saved);
    }

private:
    std::vector<WorldEntity>& m_Entities;
    std::string               m_Id;
    WorldEntity               m_Saved;
};
