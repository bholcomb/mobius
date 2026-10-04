#ifndef MOBIUS_MODULES_OS_PLATFORM_POSIX_H
#define MOBIUS_MODULES_OS_PLATFORM_POSIX_H

// What differs between Linux and macOS under os_platform_posix.cpp,
// implemented by os_platform_linux.cpp and os_platform_macos.cpp.

// The process environment (a shared library can't link `environ` on macOS).
char** posix_environ();

#endif // MOBIUS_MODULES_OS_PLATFORM_POSIX_H
