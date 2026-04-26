#pragma once
#include "Command.h"
#include "world/CelestialBody.h"
#include <algorithm>
#include <string>

class MoveEntityCommand : public Command {
public:
    MoveEntityCommand(std::vector<WorldEntity>& ents, std::string id,
                      float newLat, float newLon, float oldLat, float oldLon)
        : m_Ents(ents), m_Id(std::move(id)),
          m_NewLat(newLat), m_NewLon(newLon), m_OldLat(oldLat), m_OldLon(oldLon) {}

    void execute() override { apply(m_NewLat, m_NewLon); }
    void undo()    override { apply(m_OldLat, m_OldLon); }

private:
    void apply(float lat, float lon) {
        auto it = std::find_if(m_Ents.begin(), m_Ents.end(),
            [&](const WorldEntity& e) { return e.id == m_Id; });
        if (it != m_Ents.end()) { it->lat_deg = lat; it->lon_deg = lon; }
    }

    std::vector<WorldEntity>& m_Ents;
    std::string               m_Id;
    float m_NewLat, m_NewLon;
    float m_OldLat, m_OldLon;
};
