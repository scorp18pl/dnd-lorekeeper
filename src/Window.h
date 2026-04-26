#pragma once
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <string>

class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;

    bool shouldClose()  const;
    void pollEvents();
    void swapBuffers()  const;

    GLFWwindow* handle()  const { return m_Handle; }
    int         width()   const { return m_Width; }
    int         height()  const { return m_Height; }
    float       aspect()  const { return static_cast<float>(m_Width) / static_cast<float>(m_Height); }

    // Input deltas — valid for the current frame, reset each pollEvents().
    glm::vec2 cursorDelta()  const { return m_CursorDelta; }
    float     scrollDelta()  const { return m_ScrollDelta; }
    bool      mouseButton(int btn) const;

private:
    GLFWwindow* m_Handle = nullptr;
    int         m_Width, m_Height;

    glm::vec2 m_LastCursor{};
    glm::vec2 m_CursorDelta{};
    float     m_ScrollDelta   = 0.0f;
    bool      m_FirstCursor   = true;

    static void cbFramebuffer(GLFWwindow*, int w, int h);
    static void cbScroll     (GLFWwindow*, double, double dy);
    static void cbCursorPos  (GLFWwindow*, double x, double y);
};
