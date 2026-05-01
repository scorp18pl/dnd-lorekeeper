#pragma once
#include <string>
#include <glm/glm.hpp>

struct PoliticalEntity {
    std::string id;
    std::string name;
    glm::vec4   color    { 0.8f, 0.3f, 0.3f, 0.7f };
    std::string type;       // free text: "kingdom", "empire", etc.
    std::string liege_id;   // empty = sovereign
};
