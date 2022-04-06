#include <mutex>
#include <set>
#include <map>
#include <unordered_map>
#include <vector>
#include <string>
#include "type.h"

using namespace std;

struct Transaction {
    set<int> _active_txns;
    int _tid;
    vector<RollbackItem> _rollback_list;
    Transaction(int tid): _tid(tid) {
        _active_txns.clear();
        _rollback_list.clear();
    }
};

class TransactionManager {
public:
    mutex _mu;
    int _cur_tid;
    set<int> _active_txns;
    unordered_map<int, TxnState> _txn_state;
    map<Key, Value> _db;
public:
    TransactionManager() {
        _cur_tid = 0;
    }

    Transaction beginTxn();
    string insert(Transaction &txn, int primary_key, const Value &value);
    string update(Transaction &txn, int primary_key, const Value &value);
    string remove(Transaction &txn, int primary_key);
    string select(Transaction &txn, int primary_key, bool scan_all);
    string commitTxn(Transaction &txn);
    string abortTxn(Transaction &txn);
    TxnState getTxnState(int tid) {
        return _txn_state[tid];
    }

    bool visible(Transaction &txn, const pair<Key, Value> &record);

};
