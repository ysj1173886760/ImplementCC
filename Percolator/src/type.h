#ifndef TYPE_H
#define TYPE_H

#include <set>
#include <vector>
#include <unordered_map>

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

struct Value {
    int _b, _c;

    Value(): _b(0), _c(0) {}
    Value(int b, int c): _b(b), _c(c) {}
};

struct Default {
    OpType _type;
    Value _value;

    Default(): _value(), _type(Put) {}
    Default(int b, int c, OpType type): _value(b, c), _type(type) {}
    Default(Value value, OpType type): _value(value), _type(type) {}
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
    int _key;
    int _timestamp;

    Lock(int key, int timestamp): _key(key), _timestamp(timestamp) {}
    Lock(): _key(0), _timestamp(0) {}
};

struct Write {
    int _timestamp;

    Write(): _timestamp(0) {}
    Write(int timestamp): _timestamp(timestamp) {}
};

#endif