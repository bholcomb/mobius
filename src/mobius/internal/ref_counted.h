#ifndef MOBIUS_REF_COUNTED_H
#define MOBIUS_REF_COUNTED_H

class RefCounted {
public:
    RefCounted() : ref_count(1) {}
    virtual ~RefCounted() = default;

    void retain() { ref_count++; }

    void release() {
        if (--ref_count <= 0) {
            delete this;
        }
    }

    int refCount() const { return ref_count; }

private:
    int ref_count;
};

#endif // MOBIUS_REF_COUNTED_H
