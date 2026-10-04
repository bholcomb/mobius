#ifndef MOBIUS_MODULES_REGEX_ENGINE_H
#define MOBIUS_MODULES_REGEX_ENGINE_H

// Mobius's regular expression engine: POSIX extended regular expressions
// (ERE), the same on every platform.
//
// Syntax: . [] [^] (with [:class:]) ^ $ | () * + ? {m} {m,} {m,n}, and the
// escapes \w \W \s \S \b \B \< \>. Any other escaped character is
// literal. Back-references are not supported. Matching is byte by byte
// (character classes are ASCII), and finds the leftmost-longest match, as
// POSIX requires.
//
// Matching never backtracks: it simulates all paths through the pattern at
// once (a Pike VM), so time is proportional to the input length times the
// pattern size, and memory and stack use don't grow with the input.
//
// Submatches: of the ways the leftmost-longest match can be divided among
// the groups, the one chosen is the one a backtracking matcher would find
// first (quantifiers taking as many repetitions as they can, alternatives
// tried left to right).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mobius_regex {

class Regex {
public:
    // Compile `pattern`. False on a bad pattern, with *error describing it.
    bool compile(const std::string& pattern, bool ignore_case, std::string* error);

    // The number of parenthesized groups.
    size_t group_count() const { return groups_; }

    // Find the leftmost-longest match in subject[0..len) starting at or
    // after `start` (^ and \` still mean the start of the subject). On a
    // match, caps holds 2 * (group_count() + 1) offsets: the match's start
    // and end, then each group's (-1 for a group that took no part).
    bool search(const char* subject, size_t len, size_t start, std::vector<ptrdiff_t>* caps) const;

private:
    enum Op : uint8_t { BYTE, SET, ANY, SPLIT, JMP, SAVE, ASSERT, MATCH };
    struct Inst {
        Op op;
        uint8_t byte = 0;    // BYTE
        int arg = 0;         // SET: set index; SPLIT/JMP: target; SAVE: slot; ASSERT: kind
        int arg2 = 0;        // SPLIT: the second (less preferred) target
    };
    struct ByteSet {
        uint64_t bits[4] = {0, 0, 0, 0};
        bool has(uint8_t c) const { return (bits[c >> 6] >> (c & 63)) & 1; }
        void add(uint8_t c) { bits[c >> 6] |= (uint64_t)1 << (c & 63); }
    };
    struct Node;
    class Parser;

    // The threads alive at one input position (see search).
    struct ThreadList {
        std::vector<int> pcs;
        std::vector<ptrdiff_t> caps;   // pcs.size() * ncap
        std::vector<uint32_t> mark;    // per instruction: == gen if already added
        uint32_t gen = 1;
        size_t count = 0;
        void init(size_t prog_size, size_t ncap);
        void clear();
    };
    struct StackEntry { int pc; int slot; ptrdiff_t value; };   // slot >= 0: restore caps[slot]

    void emit_node(const Node& node);
    int emit(Inst inst);
    void compute_first_bytes();

    // One pass of the matcher. With end < 0: find the leftmost-longest
    // match at or after start, tracking only where each attempt began
    // (captures false) or every group (true). With end >= 0: the match is
    // known to be [start, end); find its groups.
    bool run(const char* s, size_t len, size_t start, ptrdiff_t end, bool captures,
             std::vector<ptrdiff_t>* out) const;

    std::vector<Inst> prog_;
    std::vector<ByteSet> sets_;
    size_t groups_ = 0;
    bool too_big_ = false;

    // The bytes a match can start with, when it can't be empty: a search
    // skips ahead to the next one instead of trying every position.
    bool can_skip_ = false;
    ByteSet first_bytes_;
    int single_first_byte_ = -1;   // when there is only one

    // For the first pass (no captures): from each instruction a thread can
    // continue at, the instructions it reaches without consuming input, in
    // priority order, when that doesn't depend on the position (no
    // assertion on the way). Precomputed for patterns up to a size limit.
    std::vector<std::vector<int>> closures_;
    std::vector<bool> closure_known_;
    void compute_closures();

    // Matching scratch, kept between searches (a Regex is used by one
    // thread at a time).
    mutable ThreadList lists_[2];
    mutable std::vector<ptrdiff_t> work_;
    mutable std::vector<StackEntry> stack_;
};

} // namespace mobius_regex

#endif // MOBIUS_MODULES_REGEX_ENGINE_H
