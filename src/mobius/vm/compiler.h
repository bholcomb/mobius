#ifndef MOBIUS_VM_COMPILER_H
#define MOBIUS_VM_COMPILER_H

#include "vm/bytecode.h"
#include "frontend/ast.h"
#include "internal/string_intern.h"

#include <vector>
#include <cstdint>

// ============================================================================
// Compiler — translates an AST into a Prototype (bytecode + constants)
//
// Usage:
//   Compiler compiler(pool);
//   Prototype* proto = compiler.compile(statements, count, "script.mob");
//   disassemble_prototype(proto);
//   // ... pass proto to VM for execution ...
//   delete proto;
// ============================================================================

class Compiler {
public:
    explicit Compiler(StringInternPool* pool);

    Prototype* compile(Stmt** statements, size_t count, const char* source_name);

private:
    // --- Local variable tracking ---
    struct Local {
        const char* name;  // interned pointer
        int depth;         // scope depth (0 = top-level of function)
        bool is_captured;  // true if an inner function closes over this local
    };

    // --- Loop jump targets for break/continue ---
    struct LoopContext {
        int start_pc;                      // instruction index of loop condition
        bool is_for_loop = false;          // true if this is a for loop (continue needs patching)
        std::vector<int> break_jumps;      // JMP instructions to patch on loop exit
        std::vector<int> continue_jumps;   // JMP instructions to patch to increment (for loops)
        int scope_depth;                   // scope depth at loop entry
    };

    // --- Compiler state for one function scope ---
    struct FunctionState {
        Prototype* proto;
        FunctionState* enclosing;

        std::vector<Local> locals;
        int scope_depth = 0;
        int free_reg = 0;              // next free register
        int max_reg = 0;               // high-water mark

        std::vector<LoopContext> loops; // stack of active loops
    };

    FunctionState* current_;
    StringInternPool* pool_;

    // --- Register management ---
    int allocReg();
    void allocRegs(int count);
    void freeReg();
    void freeRegs(int count);
    void setFreeReg(int reg);
    void reserveRegs(int count);

    // --- Scope management ---
    void beginScope();
    void endScope();

    // --- Local variable management ---
    int addLocal(const char* name);
    int resolveLocal(const char* name);
    int resolveUpvalue(FunctionState* fs, const char* name);
    int addUpvalue(FunctionState* fs, uint8_t index, bool is_local);

    // --- Constant management ---
    int stringConstant(const char* name);
    uint8_t makeRK(int reg_or_const);

    // --- Code emission wrappers ---
    int currentLine_ = 0;
    int emitABC(OpCode op, uint8_t a, uint8_t b, uint8_t c);
    int emitABx(OpCode op, uint8_t a, uint16_t bx);
    int emitAsBx(OpCode op, uint8_t a, int sbx);
    int emitJump();
    void patchJump(int jump_idx);
    void emitReturn(int first_reg, int count);
    int emitLoadK(int reg, int const_idx);

    // --- Expression compilation ---
    // Returns the register holding the result.
    int compileExpr(Expr* expr, int dest = -1);
    int compileLiteral(LiteralExpr* expr, int dest);
    int compileVariable(VariableExpr* expr, int dest);
    int compileBinary(BinaryExpr* expr, int dest);
    bool tryExprAsRK(Expr* e, uint8_t* rk);
    int compileUnary(UnaryExpr* expr, int dest);
    int compileAssignment(AssignmentExpr* expr, int dest);
    int compileCall(CallExpr* expr, int dest);
    int compileGrouping(GroupingExpr* expr, int dest);
    int compileArrayLiteral(ArrayLiteralExpr* expr, int dest);
    int compileArrayIndex(ArrayIndexExpr* expr, int dest);
    int compileTableLiteral(TableLiteralExpr* expr, int dest);
    int compileTableIndex(TableIndexExpr* expr, int dest);
    int compileTableDot(TableDotExpr* expr, int dest);
    int compileEnumAccess(EnumAccessExpr* expr, int dest);
    int compileIncrement(IncrementExpr* expr, int dest);
    int compileLogicalAnd(BinaryExpr* expr, int dest);
    int compileLogicalOr(BinaryExpr* expr, int dest);

    // --- Condition compilation (fused compare+branch) ---
    int compileConditionJump(Expr* condition);

    // --- Statement compilation ---
    void compileStmt(Stmt* stmt);
    void compileExpressionStmt(ExpressionStmt* stmt);
    void compileVarStmt(VarStmt* stmt);
    void compileBlockStmt(BlockStmt* stmt);
    void compileIfStmt(IfStmt* stmt);
    void compileWhileStmt(WhileStmt* stmt);
    void compileForStmt(ForStmt* stmt);
    void compileFunctionStmt(FunctionStmt* stmt);
    void compileReturnStmt(ReturnStmt* stmt);
    void compileSwitchStmt(SwitchStmt* stmt);
    void compileBreakStmt();
    void compileContinueStmt();
    void compileImportStmt(ImportStmt* stmt);
    void compileEnumStmt(EnumStmt* stmt);
    void compilePragmaStmt(PragmaStmt* stmt);
    void compilePrintStmt(PrintStmt* stmt);

    void compileBlock(Stmt** stmts, size_t count);

    // --- Helpers ---
    Prototype* endCompiler();
    FunctionState* initCompiler(FunctionState* enclosing, const char* name);
};

#endif // MOBIUS_VM_COMPILER_H
