# Design Documents

This directory contains internal design documents, implementation plans, and technical specifications for the Mobius scripting language.

## Categories

### Bytecode System
- `bytecode_system_design.md` - Original bytecode VM design
- `bytecode_system_revised.md` - Revised bytecode architecture
- `bytecode_system_summary.md` - Summary of bytecode implementation
- `vm_agnostic_bytecode_design.md` - VM-agnostic bytecode design

### Implementation Plans
- `implementation_roadmap.md` - Overall implementation roadmap
- `implementation_risk_assessment.md` - Risk analysis for implementation
- `enhancement_roadmap.md` - Planned enhancements

### Memory Management
- `refcount_design.md` - Reference counting design overview
- `reference_counting_implementation_plan.md` - Implementation plan for reference counting
- `phase1_string_refcount_design.md` - Phase 1: String reference counting
- `phase2_ast_refcount_design.md` - Phase 2: AST reference counting

### Language Features
- `type_annotations_design.md` - Type annotation system design
- `switch_statement_design.md` - Switch statement implementation
- `userdata_implementation.md` - User data type implementation

### Data Structures
- `table_implementation_plan.md` - Hash table implementation
- `table_refinements.md` - Table feature refinements

### Backend/Compilation
- `riscv_backend_plugins.md` - RISC-V backend plugin system
- `riscv_compilation_examples.md` - RISC-V compilation examples

### Project Management
- `TASK_REVIEW_SUMMARY.md` - Task review and progress summary

## Note

These documents are primarily for developers working on the Mobius language implementation. For user-facing documentation, see the parent `docs/` directory.
