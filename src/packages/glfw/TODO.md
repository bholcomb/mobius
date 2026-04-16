# GLFW Package TODO

This file tracks GLFW APIs that still need a Mobius package surface written.
It is focused on missing package-facing functions, not the functions that are
already wrapped in `glfw_plugin.cpp`.

Already broadly covered:

- init / terminate / error retrieval / version info
- default window hints / integer window hints
- timer APIs
- clipboard text
- monitor enumeration and basic monitor queries
- window creation and core window lifecycle
- window position / size / framebuffer / frame / content scale getters
- window attributes and input modes
- current context / swap buffers / swap interval
- event polling / waiting
- a large first-pass callback set
- basic Vulkan support queries (`glfwVulkanSupported`,
  `glfwGetRequiredInstanceExtensions`)

## High Priority Missing APIs

These are the most useful remaining GLFW APIs for real applications.

### Init / Global State

- [ ] `glfwInitHint`
- [ ] `glfwSetErrorCallback`

### Window Hints / Creation

- [ ] `glfwWindowHintString`

### Window API

- [ ] `glfwSetWindowIcon`
- [ ] `glfwSetWindowUserPointer`
- [ ] `glfwGetWindowUserPointer`

### Missing Window Callbacks

- [ ] `glfwSetWindowPosCallback`
- [ ] `glfwSetWindowRefreshCallback`
- [ ] `glfwSetWindowContentScaleCallback`
- [ ] `glfwSetDropCallback`
- [ ] `glfwSetCharModsCallback`

### Cursor API

- [ ] `glfwCreateCursor`
- [ ] `glfwCreateStandardCursor`
- [ ] `glfwDestroyCursor`
- [ ] `glfwSetCursor`

### Monitor API

- [ ] `glfwSetMonitorUserPointer`
- [ ] `glfwGetMonitorUserPointer`
- [ ] `glfwSetMonitorCallback`
- [ ] `glfwSetGamma`
- [ ] `glfwGetGammaRamp`
- [ ] `glfwSetGammaRamp`

### Vulkan Integration

- [ ] `glfwGetInstanceProcAddress`
- [ ] `glfwGetPhysicalDevicePresentationSupport`
- [ ] `glfwCreateWindowSurface`

## Medium Priority Missing APIs

These are important for fuller GLFW parity, but not blockers for the common
window/input path.

### OpenGL / Loader Utilities

- [ ] `glfwGetProcAddress`

### Joystick / Gamepad

- [ ] `glfwJoystickPresent`
- [ ] `glfwGetJoystickAxes`
- [ ] `glfwGetJoystickButtons`
- [ ] `glfwGetJoystickHats`
- [ ] `glfwGetJoystickName`
- [ ] `glfwGetJoystickGUID`
- [ ] `glfwSetJoystickUserPointer`
- [ ] `glfwGetJoystickUserPointer`
- [ ] `glfwJoystickIsGamepad`
- [ ] `glfwSetJoystickCallback`
- [ ] `glfwUpdateGamepadMappings`
- [ ] `glfwGetGamepadName`
- [ ] `glfwGetGamepadState`

## Lower Priority / Compatibility APIs

These are lower priority either because they are niche, deprecated, or less
important than the remaining core surface above.

- [ ] evaluate whether to expose deprecated `glfwSetCharModsCallback`
      as a compatibility-only API or skip it intentionally

## Notes

- When adding cursor, joystick, or monitor-pointer APIs, decide whether they
  should use userdata wrappers instead of raw integer / opaque-handle shapes.
- When adding the missing callbacks, reuse the existing rooted callback
  machinery and callback dispatch flow already used by the current window event
  APIs.
- When adding Vulkan surface creation, decide what the Mobius representation
  should be for returned Vulkan handles:
  raw integer handle, opaque userdata, or a vulkease-integrated shape.
