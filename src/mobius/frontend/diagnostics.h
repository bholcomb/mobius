#ifndef MOBIUS_FRONTEND_DIAGNOSTICS_H
#define MOBIUS_FRONTEND_DIAGNOSTICS_H

// Parse and compile errors. While a DiagnosticScope is active on the
// thread (MobiusState compiling a chunk), they are collected so the state
// can report them through its error handler with the real message;
// otherwise they are written to stderr.

#include <string>
#include <vector>

struct Diagnostic {
    int line = 0;           // 0 when unknown
    std::string message;    // without the file and line
};

struct DiagnosticScope {
    DiagnosticScope();
    ~DiagnosticScope();
    DiagnosticScope(const DiagnosticScope&) = delete;
    DiagnosticScope& operator=(const DiagnosticScope&) = delete;

    std::vector<Diagnostic> diagnostics;
    DiagnosticScope* previous;
};

// Report a diagnostic. `file` may be null.
void report_diagnostic(const char* file, int line, const std::string& message);

// printf-style compiler diagnostic: "Compile error [file:line]: ..." text
// as the compiler always wrote it. The "Compile error [...]: " prefix is
// split off into the file and line when collected.
void compile_diag(const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

#endif // MOBIUS_FRONTEND_DIAGNOSTICS_H
