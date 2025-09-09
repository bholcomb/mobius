# Reference Counting Implementation Roadmap

## Phase 1: String Reference Counting (Biggest Win)
**Estimated Performance Gain: 10-50x for string operations**

### Changes Required:
1. Add `RefCountedString` structure
2. Modify `make_string_value()` and `copy_value()`
3. Update string concatenation functions
4. Add string lifecycle management

**Risk Level: Medium** - Well-contained change
**Benefit: High** - Massive performance improvement

## Phase 2: AST Reference Counting (Fixes load() issue)
**Estimated Benefit: Eliminates load() function memory bugs**

### Changes Required:
1. Add ref counting to Stmt and Expr structures  
2. Modify parser to create ref-counted AST nodes
3. Update function creation to retain AST references
4. Enable proper cleanup in load() function

**Risk Level: High** - Touches core parser/evaluator
**Benefit: High** - Production-ready load() function

## Phase 3: Optimization & Cleanup
### Potential additions:
- Cycle detection for circular references
- String interning for frequently used strings
- Copy-on-write for mutable strings

## Alternative: Stay with Current Approach
**Pros:**
- Zero implementation risk
- Current system is stable and working
- Performance is "good enough" for many use cases

**Cons:**  
- Missing 10-50x performance gains
- load() function still has memory issues with recursion
- Higher memory usage

## Recommendation
**Implement Phase 1 (String Reference Counting)**
- Biggest performance win with contained risk
- Can be implemented incrementally
- Builds on existing table ref counting experience
- Provides dramatic user-visible performance improvement

**Hold on Phase 2** until Phase 1 proves successful
- AST ref counting is complex and risky
- Current workaround (disabled cleanup) is acceptable short-term
- Focus on user-facing performance first
