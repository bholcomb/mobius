#ifndef MOBIUS_VM_COMPILER_H
#define MOBIUS_VM_COMPILER_H

#include "vm/bytecode.h"
#include "frontend/ast.h"
#include "internal/string_intern.h"

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <cstdint>

class MobiusState;
struct GlobalEnvironment;

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
    explicit Compiler(StringInternPool* pool, MobiusState* state = nullptr,
                      GlobalEnvironment* globals = nullptr);

    Prototype* compile(Stmt** statements, size_t count, const char* source_name);

private:
    // --- Local variable tracking ---
    struct Local {
        const char* name;  // interned pointer
        int depth;         // scope depth (0 = top-level of function)
        bool is_captured;  // true if an inner function closes over this local
        ValueType inferred_type;  // VAL_UNKNOWN = type not yet determined
        bool maybe_shared; // true if the binding may hold a SharedCell at runtime
    };

    // --- Loop jump targets for break/continue ---
    struct LoopContext {
        int start_pc;                      // instruction index of loop condition
        bool is_for_loop = false;          // true if this is a for loop (continue needs patching)
        std::vector<int> break_jumps;      // JMP instructions to patch on loop exit
        std::vector<int> continue_jumps;   // JMP instructions to patch to increment (for loops)
        int scope_depth;                   // scope depth at loop entry
        int open_trys_at_entry = 0;        // open try blocks when the loop began;
                                           // break/continue must TRY_END down to this
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

        // Try blocks whose handlers are live at the current statement. Any
        // control transfer that exits a try region (return, break, continue)
        // must emit one OP_TRY_END per crossed region, or the VM keeps a
        // stale handler that hijacks a later unrelated throw.
        int open_trys = 0;
    };

    FunctionState* current_;
    StringInternPool* pool_;
    MobiusState* state_;
    GlobalEnvironment* globals_;
    std::unordered_set<std::string> enum_names_;
    bool had_error_ = false;
    bool unreachable_ = false;  // DCE: set after return/break/continue/throw

    // Loop-invariant global hoisting: small flat array mapping global name ->
    // register holding the pre-loaded value. Stacked per loop depth.
    struct HoistedEntry {
        std::string name;
        int reg;
    };
    struct HoistedGlobals {
        std::vector<HoistedEntry> entries;
    };
    std::vector<HoistedGlobals> hoisted_globals_stack_;

    struct DirectCallTarget {
        bool valid = false;
        bool is_self = false;
        uint16_t proto_index = 0;
        Prototype* proto = nullptr;
    };

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
    int addLocal(const char* name, bool maybe_shared = false);
    int resolveLocal(const char* name);
    int resolveUpvalue(FunctionState* fs, const char* name);
    int addUpvalue(FunctionState* fs, uint8_t index, bool is_local,
                   ValueType type = VAL_UNKNOWN, bool maybe_shared = false);

    // --- Constant management ---
    int stringConstant(const char* name);
    uint8_t makeRK(int reg_or_const);

    // --- Code emission wrappers ---
    int currentLine_ = 0;
    int emitABC(OpCode op, uint8_t a, uint8_t b, uint8_t c);
    int emitABC_D64(OpCode op, uint8_t a, uint8_t b, uint8_t c, uint64_t data);
    int emitABx(OpCode op, uint8_t a, uint16_t bx);
    int emitAsBx(OpCode op, uint8_t a, int sbx);
    int emitJump();
    void patchJump(int jump_idx);
    void emitReturn(int first_reg, int count);
    int emitLoadK(int reg, int const_idx);
    void emitGetGlobal(int reg, const char* name);
    void emitSetGlobal(int reg, const char* name);
    bool emitReadonlyGlobalConstant(int reg, const char* name);

    // --- Expression compilation ---
    // Returns the register holding the result.
    int compileExpr(Expr* expr, int dest = -1);
    int compileLiteral(LiteralExpr* expr, int dest);
    int compileVariable(VariableExpr* expr, int dest);
    int compileUnwrappedExpr(Expr* expr, int dest = -1);
    int compileBinary(BinaryExpr* expr, int dest);
    bool tryExprAsRK(Expr* e, uint8_t* rk);
    int compileUnary(UnaryExpr* expr, int dest);
    int compileAssignment(AssignmentExpr* expr, int dest);
    int compileCall(CallExpr* expr, int dest);
    void compileTailCall(CallExpr* expr);
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
    int compileTernary(TernaryExpr* expr, int dest);
    int compileFunctionExpr(FunctionExpr* expr, int dest);

    // --- Condition compilation (fused compare+branch) ---
    int compileConditionJump(Expr* condition);
    void compileConditionJumps(Expr* condition, std::vector<int>& false_jumps);

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
    void compileStructStmt(StructStmt* stmt);
    void compilePragmaStmt(PragmaStmt* stmt);
    void compilePrintStmt(PrintStmt* stmt);
    void compileForInStmt(ForInStmt* stmt);
    void compileTryCatchStmt(TryCatchStmt* stmt);
    void compileThrowStmt(ThrowStmt* stmt);
    void compileStructMembersArray(StructMemberDef* members, size_t count, int array_reg);
    void compileStructTypeRefValue(const StructTypeRef& type, int dest);

    int compileSpawn(SpawnExpr* expr, int dest);
    int compileAwait(AwaitExpr* expr, int dest);
    int compileShared(SharedExpr* expr, int dest);
    int compileAtomic(AtomicExpr* expr, int dest);

    void compileBlock(Stmt** stmts, size_t count);

    // --- Loop-invariant global hoisting ---
    void collectGlobalCallNames(Stmt* stmt, std::unordered_set<std::string>& names);
    void collectGlobalCallNamesFromExpr(Expr* expr, std::unordered_set<std::string>& names);
    int hoistLoopGlobals(Stmt* body);

    // --- Type inference ---
    ValueType inferExprType(Expr* expr);
    ValueType localType(int reg);
    ValueType globalType(const char* name);
    ValueType nativeReturnType(const char* name);
    bool localMayBeShared(int reg);
    bool globalMayBeShared(const char* name);
    bool callMayBeShared(CallExpr* expr);
    bool exprMayBeShared(Expr* expr);
    bool callArgsArePlain(CallExpr* expr, bool is_method);
    DirectCallTarget resolveDirectCallTarget(const char* name);

    // Global type tracking: maps global name → value type for user-defined
    // globals, and caches native function return types from the stdlib registry.
    std::unordered_map<std::string, ValueType> global_types_;
    std::unordered_map<std::string, bool> global_maybe_shared_;
    std::unordered_map<std::string, ValueType> native_return_types_;
    std::unordered_set<std::string> readonly_function_globals_;
    // Prototypes of readonly global functions, by name, for resolving direct
    // calls to a global function from inside another function. Only functions
    // already compiled (defined earlier) are present; a forward reference falls
    // back to a plain call.
    std::unordered_map<std::string, Prototype*> readonly_function_protos_;

    // --- Helpers ---
    Prototype* endCompiler();
    FunctionState* initCompiler(FunctionState* enclosing, const char* name);
    void peepholeOptimize(Prototype* proto);
    void computeAllScalarRegisters(Prototype* proto);
};

#endif // MOBIUS_VM_COMPILER_H
