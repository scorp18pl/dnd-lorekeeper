#include "Window.h"
#include <stdexcept>

Window::Window(int width, int height, const std::string& title)
    : m_Width(width), m_Height(height)
{
    if (!glfwInit())
        throw std::runtime_error("glfwInit failed");

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    m_Handle = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_Handle) {
        glfwTerminate();
        throw std::runtime_error("glfwCreateWindow failed");
    }

    glfwMakeContextCurrent(m_Handle);
    glfwSwapInterval(1); // vsync

    if (!gladLoadGL(glfwGetProcAddress))
        throw std::runtime_error("gladLoadGL failed");

    glfwSetWindowUserPointer(m_Handle, this);
    glfwSetFramebufferSizeCallback(m_Handle, cbFramebuffer);
    glfwSetScrollCallback        (m_Handle, cbScroll);
    glfwSetCursorPosCallback     (m_Handle, cbCursorPos);
}

Window::~Window() {
    glfwDestroyWindow(m_Handle);
    glfwTerminate();
}

bool Window::shouldClose() const { return glfwWindowShouldClose(m_Handle); }

void Window::pollEvents() {
    m_CursorDelta = glm::vec2(0.0f);
    m_ScrollDelta = 0.0f;
    glfwPollEvents();
}

void Window::swapBuffers() const { glfwSwapBuffers(m_Handle); }

bool Window::mouseButton(int btn) const {
    return glfwGetMouseButton(m_Handle, btn) == GLFW_PRESS;
}

// ── Static callbacks ──────────────────────────────────────────────────────────

void Window::cbFramebuffer(GLFWwindow* w, int width, int height) {
    auto* self   = static_cast<Window*>(glfwGetWindowUserPointer(w));
    self->m_Width  = width;
    self->m_Height = height;
    glViewport(0, 0, width, height);
}

void Window::cbScroll(GLFWwindow* w, double /*dx*/, double dy) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
    self->m_ScrollDelta += static_cast<float>(dy);
}

void Window::cbCursorPos(GLFWwindow* w, double x, double y) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
    glm::vec2 pos(static_cast<float>(x), static_cast<float>(y));
    if (self->m_FirstCursor) {
        self->m_LastCursor  = pos;
        self->m_FirstCursor = false;
    }
    self->m_CursorDelta = pos - self->m_LastCursor;
    self->m_LastCursor  = pos;
}
