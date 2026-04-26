#include "Shader.h"
#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iostream>

Shader::Shader(const std::string& vertPath, const std::string& fragPath) {
    GLuint vert = compileStage(GL_VERTEX_SHADER,   readFile(vertPath));
    GLuint frag = compileStage(GL_FRAGMENT_SHADER, readFile(fragPath));

    m_Program = glCreateProgram();
    glAttachShader(m_Program, vert);
    glAttachShader(m_Program, frag);
    glLinkProgram(m_Program);

    GLint ok = 0;
    glGetProgramiv(m_Program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(m_Program, 512, nullptr, log);
        glDeleteShader(vert);
        glDeleteShader(frag);
        throw std::runtime_error(std::string("Shader link error: ") + log);
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
}

Shader::~Shader() {
    if (m_Program) glDeleteProgram(m_Program);
}

void Shader::bind()   const { glUseProgram(m_Program); }
void Shader::unbind() const { glUseProgram(0); }

void Shader::setMat4(const std::string& name, const glm::mat4& v) const {
    glUniformMatrix4fv(location(name), 1, GL_FALSE, glm::value_ptr(v));
}
void Shader::setInt(const std::string& name, int v)  const { glUniform1i(location(name), v); }
void Shader::setBool(const std::string& name, bool v) const { glUniform1i(location(name), v ? 1 : 0); }
void Shader::setVec3(const std::string& name, const glm::vec3& v) const {
    glUniform3fv(location(name), 1, glm::value_ptr(v));
}

GLint Shader::location(const std::string& name) const {
    return glGetUniformLocation(m_Program, name.c_str());
}

std::string Shader::readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open shader: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

GLuint Shader::compileStage(GLenum type, const std::string& src) {
    GLuint id = glCreateShader(type);
    const char* cstr = src.c_str();
    glShaderSource(id, 1, &cstr, nullptr);
    glCompileShader(id);

    GLint ok = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(id, 512, nullptr, log);
        glDeleteShader(id);
        throw std::runtime_error(std::string("Shader compile error: ") + log);
    }
    return id;
}
