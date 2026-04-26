#pragma once
#include "Command.h"
#include "world/CelestialBody.h"
#include <vector>

class DeleteBodyCommand : public Command {
public:
    DeleteBodyCommand(std::vector<CelestialBody>& bodies, int idx)
        : m_Bodies(bodies), m_Idx(idx), m_Body(bodies[idx]) {}

    void execute() override { m_Bodies.erase(m_Bodies.begin() + m_Idx); }
    void undo()    override { m_Bodies.insert(m_Bodies.begin() + m_Idx, m_Body); }

private:
    std::vector<CelestialBody>& m_Bodies;
    int           m_Idx;
    CelestialBody m_Body;
};
