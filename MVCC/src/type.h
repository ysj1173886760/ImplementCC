#ifndef TYPE_H
#define TYPE_H

struct Key {
    int _a;
    int _xmin;
    Key(int a, int xmin): _a(a), _xmin(xmin) {}

    bool operator<(const Key &rhs) const {
        if (this->_a == rhs._a) {
            return this->_xmin > rhs._xmin;
        }
        return this->_a < rhs._a;
    }
};

struct Value {
    int _b, _c;
    int _xmax;

    Value(): _b(0), _c(0), _xmax(0) {}
    Value(int b, int c, int xmax): _b(b), _c(c), _xmax(xmax) {}
};


enum TxnState {
    InProgress = 0,
    Commited,
    Abort
};

enum OpType {
    Insert = 0,
    Delete
};

struct RollbackItem {
    OpType _type;
    int _primary_key;
    int _version;
    RollbackItem(OpType type, int primary_key, int version): 
        _type(type), 
        _primary_key(primary_key), 
        _version(version) {}
};

#endif