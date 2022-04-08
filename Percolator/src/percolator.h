#include <mutex>
#include <set>
#include <map>
#include <unordered_map>
#include <vector>
#include <string>
#include "type.h"

using namespace std;

// storage should be decoupled from txn manager, however this requires serialize/deserialize the byte code stream
// to achieve the effect of generalized value
// since i'm just create the demo here, i will use the simple implementation here

class Node {
public:
    map<Key, Default> _default_cf;
    map<Key, Lock> _lock_cf;
    map<Key, Write> _write_cf;
    // for simplicity, i will use a big latch here
    mutex _latch;
    // unordered_map<int, mutex> _latch;

    Node() {}
    Node(const Node &) = delete;
    Node(Node &&) = delete;
};

struct Transaction {
public:
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

class TransactionManager {
public:
    mutex _mu;
    int _cur_tid;
    int _shard_num;
    vector<Node> _servers;

    inline int hash(int primary_key) {
        return primary_key % _shard_num;
    }

    bool get(Transaction &txn, int primary_key, Value &value);
    void set(Transaction &txn, int primary_key, const Default &value);
    
public:
    TransactionManager(int shard_num): _shard_num(shard_num), _servers(vector<Node>(shard_num)) {
        _cur_tid = 0;
    }

    Transaction beginTxn();
    string insert(Transaction &txn, int primary_key, const Value &value);
    string update(Transaction &txn, int primary_key, const Value &value);
    string remove(Transaction &txn, int primary_key);
    string select(Transaction &txn, int primary_key, bool scan_all);
    bool Prewrite(Transaction &txn, int primary_key, const Default &write_intent, const Key &primary);
    bool commitTxn(Transaction &txn);
    void abortTxn(Transaction &txn);
};
