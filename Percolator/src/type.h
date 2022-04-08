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
    Put,
    Delete,
};

struct Default {
    OpType _type;
    int _b, _c;

    Default(): _b(0), _c(0), _type(Put) {}
    Default(int b, int c, OpType type): _b(b), _c(c), _type(type) {}
};

struct Transaction {
    int _start_timestamp;
    int _commit_timestamp;
    TxnState _state;
    unordered_map<int, Default> _write_list;
    int _primary;

    Transaction(): _start_timestamp(0),
                   _commit_timestamp(0),
                   _state(InProgress),
                   _primary(0) {}
};

struct Key {
    int _key;
    int _timestamp;
    Key(int key, int timestamp): _key(key), _timestamp(timestamp) {}

    bool operator<(const Key &rhs) const {
        if (this->_key == rhs._key) {
            return this->_timestamp > rhs._timestamp;
        }
        return this->_key < rhs._key;
    }
};

struct Lock {
    bool _valid;
    int _key;
    int _timestamp;

    Lock(bool valid, int key, int timestamp): _valid(valid), _key(key), _timestamp(timestamp) {}
    Lock(): _valid(false), _key(0), _timestamp(0) {}
};

struct Write {
    int _timestamp;

    Write(): _timestamp(0) {}
    Write(int timestamp): _timestamp(timestamp) {}
};

#endif