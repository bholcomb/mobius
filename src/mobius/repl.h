#ifndef MOBIUS_REPL_H
#define MOBIUS_REPL_H

class MobiusState;

class Repl {
public:
    explicit Repl(MobiusState* state);
    void run();

private:
    void loop();
    bool processLine(const char* line);
    bool handleCommand(const char* line);

    static void printWelcome();
    void printPrompt() const;
    static void commandHelp();
    void commandEnv() const;
    static void commandClear();
    static void commandQuit();

    static char* readLine();
    static char* trimWhitespace(char* str);
    static bool isEmptyLine(const char* line);

    MobiusState* state_;
    bool running_;
    int command_count_;
};

#endif // MOBIUS_REPL_H
