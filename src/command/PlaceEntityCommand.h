#pragma once
#include "Command.h"
#include "world/CelestialBody.h"
#include <algorithm>

class PlaceEntityCommand : public Command {
public:
    PlaceEntityCommand(std::vector<WorldEntity>& entities, WorldEntity entity)
        : m_Entities(entities), m_Entity(std::move(entity)) {}

    void execute() override {
        m_Entities.push_back(m_Entity);
    }

    void undo() override {
        auto it = std::find_if(m_Entities.begin(), m_Entities.end(),
            [&](const WorldEntity& e) { return e.id == m_Entity.id; });
        if (it != m_Entities.end())
            m_Entities.erase(it);
    }

private:
    std::vector<WorldEntity>& m_Entities;
    WorldEntity               m_Entity;
};
