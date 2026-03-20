#ifndef MOBIUS_METAMETHODS_H
#define MOBIUS_METAMETHODS_H

class StringInternPool;
struct MobiusString;

class Metamethods {
public:
    explicit Metamethods(StringInternPool* pool);

    Metamethods(const Metamethods&) = delete;
    Metamethods& operator=(const Metamethods&) = delete;

    // Arithmetic
    MobiusString* add() const { return add_; }
    MobiusString* sub() const { return sub_; }
    MobiusString* mul() const { return mul_; }
    MobiusString* div() const { return div_; }
    MobiusString* mod() const { return mod_; }
    MobiusString* unm() const { return unm_; }

    // Bitwise
    MobiusString* band() const { return band_; }
    MobiusString* bor() const { return bor_; }
    MobiusString* bxor() const { return bxor_; }
    MobiusString* bnot() const { return bnot_; }
    MobiusString* shl() const { return shl_; }
    MobiusString* shr() const { return shr_; }

    // Comparison
    MobiusString* eq() const { return eq_; }
    MobiusString* lt() const { return lt_; }
    MobiusString* le() const { return le_; }

    // Access
    MobiusString* index() const { return index_; }
    MobiusString* newindex() const { return newindex_; }

    // Invocation / conversion
    MobiusString* call() const { return call_; }
    MobiusString* tostring() const { return tostring_; }

private:
    MobiusString* add_;
    MobiusString* sub_;
    MobiusString* mul_;
    MobiusString* div_;
    MobiusString* mod_;
    MobiusString* unm_;

    MobiusString* band_;
    MobiusString* bor_;
    MobiusString* bxor_;
    MobiusString* bnot_;
    MobiusString* shl_;
    MobiusString* shr_;

    MobiusString* eq_;
    MobiusString* lt_;
    MobiusString* le_;

    MobiusString* index_;
    MobiusString* newindex_;

    MobiusString* call_;
    MobiusString* tostring_;
};

#endif // MOBIUS_METAMETHODS_H
