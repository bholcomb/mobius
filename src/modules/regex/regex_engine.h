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

    void emit_node(const Node& node);
    int emit(Inst inst);

    std::vector<Inst> prog_;
    std::vector<ByteSet> sets_;
    size_t groups_ = 0;
    bool too_big_ = false;
};

} // namespace mobius_regex

#endif // MOBIUS_MODULES_REGEX_ENGINE_H
