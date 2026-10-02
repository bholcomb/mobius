#include "frontend/diagnostics.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static thread_local DiagnosticScope* t_scope = nullptr;

DiagnosticScope::DiagnosticScope() : previous(t_scope) { t_scope = this; }
DiagnosticScope::~DiagnosticScope() { t_scope = previous; }

void report_diagnostic(const char* file, int line, const std::string& message) {
    if (t_scope) {
        t_scope->diagnostics.push_back({line, message});
        return;
    }
    if (file && line > 0) fprintf(stderr, "[%s:%d] %s\n", file, line, message.c_str());
    else if (file) fprintf(stderr, "[%s] %s\n", file, message.c_str());
    else fprintf(stderr, "%s\n", message.c_str());
}

void compile_diag(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (!t_scope) {
        fputs(buf, stderr);
        return;
    }
    // "Compile error [file:12]: text\n" -> line 12, "text".
    std::string text(buf);
    while (!text.empty() && text.back() == '\n') text.pop_back();
    int line = 0;
    const char* prefix = "Compile error";
    if (text.compare(0, strlen(prefix), prefix) == 0) {
        size_t colon_space = text.find("]: ");
        size_t open = text.find('[');
        if (open != std::string::npos && colon_space != std::string::npos && open < colon_space) {
            std::string where = text.substr(open + 1, colon_space - open - 1);
            size_t last_colon = where.rfind(':');
            if (last_colon != std::string::npos) line = atoi(where.c_str() + last_colon + 1);
            text = text.substr(colon_space + 3);
        } else if (text.compare(0, strlen(prefix) + 2, std::string(prefix) + ": ") == 0) {
            text = text.substr(strlen(prefix) + 2);
        }
        text = "Compile error: " + text;
    }
    t_scope->diagnostics.push_back({line, text});
}
