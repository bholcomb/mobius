// Mobius's regular expression engine (see regex_engine.h): a parser for
// POSIX extended syntax, a compiler to a small instruction program, and a
// Pike VM that runs every path through the program in step over the input.

#include "regex_engine.h"

#include <algorithm>
#include <cstring>

namespace mobius_regex {

namespace {

// Limits that keep a hostile pattern from exhausting the stack or memory.
const int MAX_NESTING = 250;          // parentheses within parentheses
const int MAX_REPEAT = 32767;         // the largest count in {m,n} (RE_DUP_MAX)
const size_t MAX_PROGRAM = 100000;    // instructions after expanding {m,n}

enum AssertKind {
    A_BEGIN,        // ^
    A_END,          // $
    A_WORD_BOUNDARY,
    A_NOT_WORD_BOUNDARY,
    A_WORD_START,   // \<
    A_WORD_END,     // \>
};

bool is_upper(int c) { return c >= 'A' && c <= 'Z'; }
bool is_lower(int c) { return c >= 'a' && c <= 'z'; }
bool is_digit(int c) { return c >= '0' && c <= '9'; }
bool is_alpha(int c) { return is_upper(c) || is_lower(c); }
bool is_alnum(int c) { return is_alpha(c) || is_digit(c); }
bool is_word(int c) { return is_alnum(c) || c == '_'; }
bool is_space(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
bool is_cntrl(int c) { return c < 32 || c == 127; }
bool is_print(int c) { return c >= 32 && c < 127; }
bool is_graph(int c) { return c > 32 && c < 127; }
bool is_punct(int c) { return is_graph(c) && !is_alnum(c); }
bool is_xdigit(int c) { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
bool is_blank(int c) { return c == ' ' || c == '\t'; }

int other_case(int c) {
    if (is_upper(c)) return c - 'A' + 'a';
    if (is_lower(c)) return c - 'a' + 'A';
    return c;
}

} // namespace

struct Regex::Node {
    enum Kind { EMPTY, BYTE, SET, ANY, ASSERT, GROUP, CAT, ALT, REPEAT };
    Kind kind = EMPTY;
    uint8_t byte = 0;
    int set = 0;          // SET
    int assert_kind = 0;  // ASSERT
    int group = 0;        // GROUP (1-based)
    int min = 0, max = 0; // REPEAT; max -1: no limit
    std::vector<Node> kids;
};

// Recursive descent over the pattern; recursion depth is bounded by
// MAX_NESTING.
class Regex::Parser {
public:
    Parser(const std::string& p, bool icase, Regex& re) : p_(p), icase_(icase), re_(re) {}

    bool parse(Node* root, std::string* error) {
        if (!parse_alt(root)) { *error = error_; return false; }
        return true;
    }

private:
    bool fail(const char* message) {
        if (error_.empty()) error_ = message;
        return false;
    }

    bool at_end() const { return i_ >= p_.size(); }
    char peek() const { return p_[i_]; }

    // alternatives: concat ('|' concat)*
    bool parse_alt(Node* out) {
        Node first;
        if (!parse_concat(&first)) return false;
        if (at_end() || peek() != '|') { *out = std::move(first); return true; }
        out->kind = Node::ALT;
        out->kids.push_back(std::move(first));
        while (!at_end() && peek() == '|') {
            i_++;
            Node next;
            if (!parse_concat(&next)) return false;
            out->kids.push_back(std::move(next));
        }
        return true;
    }

    // concat: (atom quantifier*)*, up to '|', the end, or the ')' closing
    // an open group
    bool parse_concat(Node* out) {
        out->kind = Node::CAT;
        while (!at_end()) {
            char c = peek();
            if (c == '|') break;
            if (c == ')' && open_ > 0) break;
            if (c == '*' || c == '+' || c == '?' || c == '{')
                return fail("Invalid preceding regular expression");
            Node atom;
            if (!parse_atom(&atom)) return false;
            if (!parse_quantifiers(&atom)) return false;
            out->kids.push_back(std::move(atom));
        }
        if (out->kids.empty()) out->kind = Node::EMPTY;
        else if (out->kids.size() == 1) { Node only = std::move(out->kids[0]); *out = std::move(only); }
        return true;
    }

    bool parse_quantifiers(Node* atom) {
        while (!at_end()) {
            char c = peek();
            int min, max;
            if (c == '*') { min = 0; max = -1; i_++; }
            else if (c == '+') { min = 1; max = -1; i_++; }
            else if (c == '?') { min = 0; max = 1; i_++; }
            else if (c == '{') { if (!parse_interval(&min, &max)) return false; }
            else break;
            if (atom->kind == Node::ASSERT) return fail("Invalid preceding regular expression");
            Node rep;
            rep.kind = Node::REPEAT;
            rep.min = min;
            rep.max = max;
            rep.kids.push_back(std::move(*atom));
            *atom = std::move(rep);
        }
        return true;
    }

    // {m} {m,} {m,n}
    bool parse_interval(int* min, int* max) {
        i_++;   // '{'
        auto number = [&](int* out) {
            if (at_end() || !is_digit(peek())) return false;
            long v = 0;
            while (!at_end() && is_digit(peek())) {
                v = v * 10 + (peek() - '0');
                if (v > MAX_REPEAT + 1) v = MAX_REPEAT + 1;
                i_++;
            }
            *out = (int)v;
            return true;
        };
        bool has_min = number(min);
        if (!at_end() && peek() == ',') {
            i_++;
            if (!number(max)) *max = -1;
        } else {
            *max = *min;
        }
        if (at_end()) return fail("Unmatched \\{");
        if (peek() != '}') {
            // Scan for the brace to report the right error.
            size_t j = i_;
            while (j < p_.size() && p_[j] != '}') j++;
            return fail(j < p_.size() ? "Invalid content of \\{\\}" : "Unmatched \\{");
        }
        i_++;   // '}'
        if (!has_min) return fail("Invalid content of \\{\\}");
        if (*min > MAX_REPEAT || *max > MAX_REPEAT) return fail("Regular expression too big");
        if (*max != -1 && *max < *min) return fail("Invalid content of \\{\\}");
        return true;
    }

    bool parse_atom(Node* out) {
        char c = peek();
        i_++;
        switch (c) {
            case '(': {
                if (open_ >= MAX_NESTING) return fail("Regular expression too big");
                int group = (int)++re_.groups_;
                open_++;
                Node body;
                if (!parse_alt(&body)) return false;
                if (at_end() || peek() != ')') return fail("Unmatched ( or \\(");
                i_++;
                open_--;
                out->kind = Node::GROUP;
                out->group = group;
                out->kids.push_back(std::move(body));
                return true;
            }
            case '.':
                out->kind = Node::ANY;
                return true;
            case '^':
                out->kind = Node::ASSERT;
                out->assert_kind = A_BEGIN;
                return true;
            case '$':
                out->kind = Node::ASSERT;
                out->assert_kind = A_END;
                return true;
            case '[':
                return parse_bracket(out);
            case '\\':
                return parse_escape(out);
            default:   // includes an unmatched ')'
                literal(out, (uint8_t)c);
                return true;
        }
    }

    void literal(Node* out, uint8_t c) {
        if (icase_ && other_case(c) != c) {
            ByteSet set;
            set.add(c);
            set.add((uint8_t)other_case(c));
            set_node(out, set);
        } else {
            out->kind = Node::BYTE;
            out->byte = c;
        }
    }

    void set_node(Node* out, const ByteSet& set) {
        out->kind = Node::SET;
        out->set = (int)re_.sets_.size();
        re_.sets_.push_back(set);
    }

    bool parse_escape(Node* out) {
        if (at_end()) return fail("Trailing backslash");
        char c = peek();
        i_++;
        ByteSet set;
        switch (c) {
            case 'w': case 'W':
                for (int b = 0; b < 256; b++) if (is_word(b) == (c == 'w')) set.add((uint8_t)b);
                set_node(out, set);
                return true;
            case 's': case 'S':
                for (int b = 0; b < 256; b++) if (is_space(b) == (c == 's')) set.add((uint8_t)b);
                set_node(out, set);
                return true;
            case 'b': return assertion(out, A_WORD_BOUNDARY);
            case 'B': return assertion(out, A_NOT_WORD_BOUNDARY);
            case '<': return assertion(out, A_WORD_START);
            case '>': return assertion(out, A_WORD_END);
            default:
                if (c >= '1' && c <= '9') return fail("Back-references are not supported");
                literal(out, (uint8_t)c);
                return true;
        }
    }

    bool assertion(Node* out, int kind) {
        out->kind = Node::ASSERT;
        out->assert_kind = kind;
        return true;
    }

    // [:name:] -> add the class's bytes
    bool add_class(const std::string& name, ByteSet* set) {
        bool (*test)(int) = nullptr;
        if (name == "alpha") test = is_alpha;
        else if (name == "digit") test = is_digit;
        else if (name == "alnum") test = is_alnum;
        else if (name == "upper") test = is_upper;
        else if (name == "lower") test = is_lower;
        else if (name == "space") test = is_space;
        else if (name == "blank") test = is_blank;
        else if (name == "punct") test = is_punct;
        else if (name == "print") test = is_print;
        else if (name == "graph") test = is_graph;
        else if (name == "cntrl") test = is_cntrl;
        else if (name == "xdigit") test = is_xdigit;
        else return fail("Invalid character class name");
        for (int b = 0; b < 256; b++) if (test(b)) set->add((uint8_t)b);
        return true;
    }

    // [:name:] at i_: add the class. False with *is_class false when i_
    // isn't at one (a '[' is then an ordinary member).
    bool bracket_class(ByteSet* set, bool* is_class) {
        *is_class = false;
        if (p_[i_] != '[' || i_ + 1 >= p_.size() || p_[i_ + 1] != ':') return true;
        size_t close = p_.find(":]", i_ + 2);
        if (close == std::string::npos) return fail("Unmatched [ or [:");
        std::string name = p_.substr(i_ + 2, close - i_ - 2);
        i_ = close + 2;
        *is_class = true;
        return add_class(name, set);
    }

    bool parse_bracket(Node* out) {
        ByteSet set;
        bool negate = false;
        if (!at_end() && peek() == '^') { negate = true; i_++; }
        bool first = true;
        while (true) {
            if (at_end()) return fail("Unmatched [ or [:");
            if (peek() == ']' && !first) { i_++; break; }
            first = false;
            bool is_class = false;
            if (!bracket_class(&set, &is_class)) return false;
            if (is_class) continue;
            int lo = (uint8_t)p_[i_++];
            // A range, unless the '-' is last ("[a-]").
            if (i_ + 1 < p_.size() && p_[i_] == '-' && p_[i_ + 1] != ']') {
                int hi = (uint8_t)p_[i_ + 1];
                i_ += 2;
                if (hi < lo) return fail("Invalid range end");
                for (int b = lo; b <= hi; b++) set.add((uint8_t)b);
            } else {
                set.add((uint8_t)lo);
            }
        }
        if (icase_) {
            ByteSet folded = set;
            for (int b = 0; b < 256; b++) if (set.has((uint8_t)b)) folded.add((uint8_t)other_case(b));
            set = folded;
        }
        if (negate) {
            ByteSet inverted;
            for (int b = 0; b < 256; b++) if (!set.has((uint8_t)b)) inverted.add((uint8_t)b);
            set = inverted;
        }
        set_node(out, set);
        return true;
    }

    const std::string& p_;
    bool icase_;
    Regex& re_;
    size_t i_ = 0;
    int open_ = 0;
    std::string error_;
};

// ---------------------------------------------------------------------------
// Compiling: Thompson's construction. A SPLIT prefers its first target, so
// quantifiers prefer more repetitions and alternatives the leftmost.
// ---------------------------------------------------------------------------

int Regex::emit(Inst inst) {
    if (prog_.size() >= MAX_PROGRAM) {
        too_big_ = true;
        return (int)prog_.size() - 1;
    }
    prog_.push_back(inst);
    return (int)prog_.size() - 1;
}

void Regex::emit_node(const Node& n) {
    if (too_big_) return;
    switch (n.kind) {
        case Node::EMPTY:
            break;
        case Node::BYTE: {
            Inst inst{BYTE};
            inst.byte = n.byte;
            emit(inst);
            break;
        }
        case Node::SET: {
            Inst inst{SET};
            inst.arg = n.set;
            emit(inst);
            break;
        }
        case Node::ANY:
            emit(Inst{ANY});
            break;
        case Node::ASSERT: {
            Inst inst{ASSERT};
            inst.arg = n.assert_kind;
            emit(inst);
            break;
        }
        case Node::GROUP: {
            Inst open{SAVE};
            open.arg = 2 * n.group;
            emit(open);
            emit_node(n.kids[0]);
            Inst close{SAVE};
            close.arg = 2 * n.group + 1;
            emit(close);
            break;
        }
        case Node::CAT:
            for (const Node& k : n.kids) emit_node(k);
            break;
        case Node::ALT: {
            std::vector<int> jumps;
            for (size_t k = 0; k + 1 < n.kids.size(); k++) {
                int split = emit(Inst{SPLIT});
                prog_[split].arg = (int)prog_.size();
                emit_node(n.kids[k]);
                jumps.push_back(emit(Inst{JMP}));
                prog_[split].arg2 = (int)prog_.size();
            }
            emit_node(n.kids.back());
            if (too_big_) return;
            for (int j : jumps) prog_[j].arg = (int)prog_.size();
            break;
        }
        case Node::REPEAT: {
            const Node& body = n.kids[0];
            for (int k = 0; k < n.min && !too_big_; k++) emit_node(body);
            if (n.max == -1) {
                // body*: L: SPLIT(L+1, out); body; JMP L; out:
                int loop = emit(Inst{SPLIT});
                prog_[loop].arg = (int)prog_.size();
                emit_node(body);
                Inst back{JMP};
                back.arg = loop;
                emit(back);
                if (!too_big_) prog_[loop].arg2 = (int)prog_.size();
            } else {
                // body? nested (max - min) times; skipping one skips the rest.
                std::vector<int> splits;
                for (int k = n.min; k < n.max && !too_big_; k++) {
                    int split = emit(Inst{SPLIT});
                    prog_[split].arg = (int)prog_.size();
                    splits.push_back(split);
                    emit_node(body);
                }
                if (too_big_) return;
                for (int s : splits) prog_[s].arg2 = (int)prog_.size();
            }
            break;
        }
    }
}

bool Regex::compile(const std::string& pattern, bool ignore_case, std::string* error) {
    prog_.clear();
    sets_.clear();
    groups_ = 0;
    too_big_ = false;
    Node root;
    Parser parser(pattern, ignore_case, *this);
    if (!parser.parse(&root, error)) return false;
    emit_node(root);
    emit(Inst{MATCH});
    if (too_big_) {
        *error = "Regular expression too big";
        prog_.clear();
        return false;
    }
    compute_first_bytes();
    compute_closures();
    return true;
}

// ---------------------------------------------------------------------------
// Matching: a Pike VM. Each step holds the threads alive at one input
// position, in priority order, each with its capture offsets; a thread that
// reaches an instruction another thread already reached at this position is
// dropped (it would do the same from here on). Threads started earlier have
// priority, so the first start that matches is the leftmost; among matches
// from it the longest wins.
// ---------------------------------------------------------------------------

void Regex::ThreadList::init(size_t prog_size, size_t ncap) {
    pcs.resize(prog_size);
    caps.resize(prog_size * ncap);
    if (mark.size() != prog_size) mark.assign(prog_size, 0);
}

void Regex::ThreadList::clear() {
    count = 0;
    if (++gen == 0) {   // wrapped: reset the marks
        std::fill(mark.begin(), mark.end(), 0);
        gen = 1;
    }
}

void Regex::compute_closures() {
    closures_.assign(prog_.size(), {});
    closure_known_.assign(prog_.size(), false);
    if (prog_.size() > 4096) return;   // memory grows with size squared
    std::vector<bool> roots(prog_.size(), false);
    roots[0] = true;
    for (size_t pc = 0; pc + 1 < prog_.size(); pc++) {
        Op op = prog_[pc].op;
        if (op == BYTE || op == SET || op == ANY) roots[pc + 1] = true;
    }
    std::vector<int> seen(prog_.size(), -1);
    std::vector<int> todo;
    for (size_t root = 0; root < prog_.size(); root++) {
        if (!roots[root]) continue;
        bool known = true;
        std::vector<int>& out = closures_[root];
        todo.assign(1, (int)root);
        while (!todo.empty() && known) {
            int pc = todo.back();
            todo.pop_back();
            if (seen[(size_t)pc] == (int)root) continue;
            seen[(size_t)pc] = (int)root;
            const Inst& inst = prog_[(size_t)pc];
            switch (inst.op) {
                case JMP: todo.push_back(inst.arg); break;
                case SPLIT: todo.push_back(inst.arg2); todo.push_back(inst.arg); break;
                case SAVE: todo.push_back(pc + 1); break;
                case ASSERT: known = false; break;
                default: out.push_back(pc); break;
            }
        }
        closure_known_[root] = known;
        if (!known) out.clear();
    }
}

// The bytes a match can begin with: those reachable from the start without
// consuming input (assertions are assumed to hold). A match that can be
// empty, or begin with any byte, disables skipping.
void Regex::compute_first_bytes() {
    can_skip_ = true;
    first_bytes_ = ByteSet();
    std::vector<bool> seen(prog_.size(), false);
    std::vector<int> todo = {0};
    while (!todo.empty() && can_skip_) {
        int pc = todo.back();
        todo.pop_back();
        if (seen[(size_t)pc]) continue;
        seen[(size_t)pc] = true;
        const Inst& inst = prog_[(size_t)pc];
        switch (inst.op) {
            case JMP: todo.push_back(inst.arg); break;
            case SPLIT: todo.push_back(inst.arg); todo.push_back(inst.arg2); break;
            case SAVE: case ASSERT: todo.push_back(pc + 1); break;
            case BYTE: first_bytes_.add(inst.byte); break;
            case SET:
                for (int k = 0; k < 4; k++) first_bytes_.bits[k] |= sets_[(size_t)inst.arg].bits[k];
                break;
            case ANY: case MATCH: can_skip_ = false; break;
        }
    }
    single_first_byte_ = -1;
    if (!can_skip_) return;
    int count = 0;
    for (int b = 0; b < 256; b++) {
        if (first_bytes_.has((uint8_t)b)) { count++; single_first_byte_ = b; }
    }
    if (count != 1) single_first_byte_ = -1;
    if (count == 256) can_skip_ = false;
}

// Finding a match tracks only where each attempt began, which is cheap;
// only a pattern with groups then reruns over the match to find them. The
// rerun finds the same match: an attempt the first pass dropped (another
// that began earlier reached the same place) could never have matched.
bool Regex::search(const char* s, size_t len, size_t start, std::vector<ptrdiff_t>* out) const {
    if (prog_.empty() || start > len) return false;
    if (!run(s, len, start, -1, false, out)) return false;
    if (groups_ == 0) return true;
    ptrdiff_t match_start = (*out)[0], match_end = (*out)[1];
    return run(s, len, (size_t)match_start, match_end, true, out);
}

bool Regex::run(const char* s, size_t len, size_t start, ptrdiff_t end, bool captures,
                std::vector<ptrdiff_t>* out) const {
    const bool known = end >= 0;   // finding the groups of [start, end)
    const size_t ncap = captures ? 2 * (groups_ + 1) : 2;
    lists_[0].init(prog_.size(), ncap);
    lists_[1].init(prog_.size(), ncap);
    ThreadList* clist = &lists_[0];
    ThreadList* nlist = &lists_[1];
    std::vector<ptrdiff_t>& work = work_;
    work.resize(ncap);
    std::vector<StackEntry>& stack = stack_;
    stack.clear();

    auto is_word_at = [&](size_t pos) { return pos < len && is_word((uint8_t)s[pos]); };
    auto holds = [&](int kind, size_t pos) {
        bool before = pos > 0 && is_word((uint8_t)s[pos - 1]);
        bool after = is_word_at(pos);
        switch (kind) {
            case A_BEGIN: return pos == 0;
            case A_END: return pos == len;
            case A_WORD_BOUNDARY: return before != after;
            case A_NOT_WORD_BOUNDARY: return before == after;
            case A_WORD_START: return !before && after;
            case A_WORD_END: return before && !after;
        }
        return false;
    };

    // Add the thread at `pc` (captures in `work`) and everything it reaches
    // without consuming input, in priority order. `work` is restored after.
    auto add_thread = [&](ThreadList* list, int pc0, size_t pos) {
        if (!captures && closure_known_[(size_t)pc0]) {
            for (int pc : closures_[(size_t)pc0]) {
                if (list->mark[(size_t)pc] == list->gen) continue;
                list->mark[(size_t)pc] = list->gen;
                size_t k = list->count++;
                list->pcs[k] = pc;
                list->caps[k * 2] = work[0];
                list->caps[k * 2 + 1] = work[1];
            }
            return;
        }
        stack.push_back({pc0, -1, 0});
        while (!stack.empty()) {
            StackEntry e = stack.back();
            stack.pop_back();
            if (e.slot >= 0) { work[(size_t)e.slot] = e.value; continue; }
            int pc = e.pc;
            if (list->mark[(size_t)pc] == list->gen) continue;
            list->mark[(size_t)pc] = list->gen;
            const Inst& inst = prog_[(size_t)pc];
            switch (inst.op) {
                case JMP:
                    stack.push_back({inst.arg, -1, 0});
                    break;
                case SPLIT:
                    stack.push_back({inst.arg2, -1, 0});
                    stack.push_back({inst.arg, -1, 0});   // explored first
                    break;
                case SAVE:
                    if (captures) {
                        stack.push_back({0, inst.arg, work[(size_t)inst.arg]});
                        work[(size_t)inst.arg] = (ptrdiff_t)pos;
                    }
                    stack.push_back({pc + 1, -1, 0});
                    break;
                case ASSERT:
                    if (holds(inst.arg, pos)) stack.push_back({pc + 1, -1, 0});
                    break;
                default: {   // BYTE, SET, ANY, MATCH: wait here
                    size_t k = list->count++;
                    list->pcs[k] = pc;
                    std::copy(work.begin(), work.end(), list->caps.begin() + (ptrdiff_t)(k * ncap));
                    break;
                }
            }
        }
    };

    bool matched = false;
    ptrdiff_t best_start = -1, best_end = -1;
    std::vector<ptrdiff_t>& best = *out;
    best.assign(ncap, -1);

    clist->clear();
    for (size_t pos = start; ; pos++) {
        if (!matched && clist->count == 0 && can_skip_ && !known) {
            // Nothing in progress: go to where a match could begin.
            if (single_first_byte_ >= 0) {
                const void* hit = pos < len ? memchr(s + pos, single_first_byte_, len - pos) : nullptr;
                if (!hit) return false;
                pos = (size_t)((const char*)hit - s);
            } else {
                while (pos < len && !first_bytes_.has((uint8_t)s[pos])) pos++;
                if (pos >= len) return false;
            }
        }
        if (!matched && (!known || pos == start)) {
            // A new thread starting here, after (below) the older ones.
            std::fill(work.begin(), work.end(), -1);
            work[0] = (ptrdiff_t)pos;
            add_thread(clist, 0, pos);
        }
        if (clist->count == 0 && (matched || pos >= len || known)) break;

        nlist->clear();
        for (size_t t = 0; t < clist->count; t++) {
            const ptrdiff_t* tc = &clist->caps[t * ncap];
            if (matched && tc[0] > best_start) continue;   // can't be leftmost
            const Inst& inst = prog_[(size_t)clist->pcs[t]];
            bool advance = false;
            switch (inst.op) {
                case BYTE: advance = pos < len && (uint8_t)s[pos] == inst.byte; break;
                case SET:  advance = pos < len && sets_[(size_t)inst.arg].has((uint8_t)s[pos]); break;
                case ANY:  advance = pos < len && s[pos] != '\0'; break;
                case MATCH:
                    if (known) {
                        // The first (highest priority) way to the known end.
                        if (!matched && (ptrdiff_t)pos == end) {
                            matched = true;
                            std::copy(tc, tc + ncap, best.begin());
                            best[1] = (ptrdiff_t)pos;
                        }
                        break;
                    }
                    if (!matched || tc[0] < best_start || (tc[0] == best_start && (ptrdiff_t)pos > best_end)) {
                        matched = true;
                        best_start = tc[0];
                        best_end = (ptrdiff_t)pos;
                        std::copy(tc, tc + ncap, best.begin());
                        best[1] = (ptrdiff_t)pos;
                    }
                    break;
                default: break;
            }
            if (advance) {
                std::copy(tc, tc + ncap, work.begin());
                add_thread(nlist, clist->pcs[t] + 1, pos + 1);
            }
        }
        std::swap(clist, nlist);
        if (known && (matched || (ptrdiff_t)pos >= end)) break;
        if (pos >= len) {
            // Threads past the end can only be MATCHes left in clist.
            if (clist->count == 0) break;
        }
        if (pos > len) break;
    }
    return matched;
}

} // namespace mobius_regex
