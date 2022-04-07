#ifndef TYPE_H
#define TYPE_H

#include <set>
#include <vector>

using namespace std;

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
    int _cid;
    RollbackItem(OpType type, int primary_key, int version, int cid): 
        _type(type), 
        _primary_key(primary_key), 
        _version(version),
        _cid(cid) {}
};

struct Transaction {
    set<int> _active_txns;
    int _tid;
    int _cid;
    vector<RollbackItem> _rollback_list;
    Transaction(int tid): _tid(tid) {
        _active_txns.clear();
        _rollback_list.clear();
        _cid = 0;
    }
};

struct Key {
    int _a;
    int _xmin;
    int _cid;
    Key(int a, int xmin, int cid): _a(a), _xmin(xmin), _cid(cid) {}

    bool operator<(const Key &rhs) const {
        if (this->_a == rhs._a) {
            if (this->_xmin == rhs._xmin) {
                return this->_cid > rhs._cid;
            }
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
#endif