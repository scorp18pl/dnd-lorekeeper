#pragma once
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <string>

class Shader {
public:
    Shader(const std::string& vertPath, const std::string& fragPath);
    ~Shader();

    Shader(const Shader&)            = delete;
    Shader& operator=(const Shader&) = delete;

    void bind()   const;
    void unbind() const;

    void setMat4   (const std::string& name, const glm::mat4& v)    const;
    void setInt    (const std::string& name, int v)                 const;
    void setBool   (const std::string& name, bool v)                const;
    void setFloat  (const std::string& name, float v)               const;
    void setVec2   (const std::string& name, const glm::vec2& v)    const;
    void setVec3   (const std::string& name, const glm::vec3& v)    const;
    void setFloat1v(const std::string& name, int count, const float* v) const;
    void setInt1v  (const std::string& name, int count, const int*   v) const;

private:
    GLuint m_Program = 0;

    static std::string   readFile(const std::string& path);
    static GLuint        compileStage(GLenum type, const std::string& src);
    GLint location(const std::string& name) const;
};
