#include <iostream>
using namespace std;
#include "mvcc.h"

Transaction TransactionManager::beginTxn() {
    lock_guard<mutex> guard(_mu);
    _cur_tid++;
    _active_txns.insert(_cur_tid);
    Transaction t(_cur_tid);
    t._active_txns = _active_txns;
    return t;
}

string TransactionManager::insert(Transaction &txn, int primary_key, const Value &value) {
    lock_guard<mutex> guard(_mu);
    // get newest record
    auto it = _db.lower_bound(Key(primary_key, _cur_tid));
    
    // first time
    if (it == _db.end() || it->first._a != primary_key) {
        _db.insert({Key(primary_key, txn._tid), Value(value._b, value._c, 0)});
        txn._rollback_list.push_back(RollbackItem(Insert, primary_key, txn._tid));
        return "succeed\n";
    }

    // created by a outstanding txn, this is write-write conflict, we can wait till this txn is done.
    // for the simplicity, i will abort here
    if (it->first._xmin >= *txn._active_txns.rbegin() || txn._active_txns.count(it->first._xmin)) {
        if (it->first._xmin == txn._tid) {
            return "record already existed\n";
        }
        _txn_state[txn._tid] = Abort;
        return "Abort\n";
    }

    // otherwise, we need to detect whether the record is existed
    if (it->second._xmax != 0) {
        if (it->second._xmax >= *txn._active_txns.rbegin() || txn._active_txns.count(it->second._xmax)) {
            // update by outstanding txn, then it's a write-write conflict
            _txn_state[txn._tid] = Abort;
            return "Abort\n";
        }

        // otherwise, we can see this change, which means the record is deleted
        _db.insert({Key(primary_key, txn._tid), Value(value._b, value._c, 0)});
        txn._rollback_list.push_back(RollbackItem(Insert, primary_key, txn._tid));
        return "succeed\n";
    }

    // if xmax is not valid, which means the record is valid. then we are inserting a duplicated record
    return "record already existed\n";
}

string TransactionManager::update(Transaction &txn, int primary_key, const Value &value) {
    lock_guard<mutex> guard(_mu);

    // get newest record
    auto it = _db.lower_bound(Key(primary_key, _cur_tid));

    if (it == _db.end() || it->first._a != primary_key) {
        // we are updating an non-existing record, we should fail here
        return "failed to find tuple\n";
    }

    if (it->first._xmin >= *txn._active_txns.rbegin() || txn._active_txns.count(it->first._xmin)) {
        if (it->first._xmin == txn._tid) {
            // if we deleted this record
            if (it->second._xmax == txn._tid) {
                return "failed to find tuple\n";
            }
            // otherise we can update this record safely
            it->second._xmax = txn._tid;
            txn._rollback_list.push_back(RollbackItem(Delete, primary_key, it->first._xmin));
            _db.insert({Key(primary_key, txn._tid), Value(value._b, value._c, 0)});
            txn._rollback_list.push_back(RollbackItem(Insert, primary_key, txn._tid));
            return "succeed\n";
        }
        // write-write conflict
        _txn_state[txn._tid] = Abort;
        return "Abort\n";
    }

    if (it->second._xmax != 0) {
        if (it->second._xmax >= *txn._active_txns.rbegin() || txn._active_txns.count(it->second._xmax)) {
            if (it->second._xmax == txn._tid) {
                return "failed to find tuple\n";
            }
            // write-write conflict
            _txn_state[txn._tid] = Abort;
            return "Abort\n";
        }

        return "failed to find tuple\n";
    }

    it->second._xmax = txn._tid;
    txn._rollback_list.push_back(RollbackItem(Delete, primary_key, it->first._xmin));
    _db.insert({Key(primary_key, txn._tid), Value(value._b, value._c, 0)});
    txn._rollback_list.push_back(RollbackItem(Insert, primary_key, txn._tid));
    return "succeed\n";
}

string TransactionManager::remove(Transaction &txn, int primary_key) {
    lock_guard<mutex> guard(_mu);

    // get newest record
    auto it = _db.lower_bound(Key(primary_key, _cur_tid));

    if (it == _db.end() || it->first._a != primary_key) {
        // we are deleting an non-existing record, we should fail here
        return "failed to find tuple\n";
    }

    if (it->first._xmin >= *txn._active_txns.rbegin() || txn._active_txns.count(it->first._xmin)) {
        if (it->first._xmin == txn._tid) {
            if (it->second._xmax == txn._tid) {
                return "failed to find tuple\n";
            } else {
                it->second._xmax = txn._tid;
                txn._rollback_list.push_back(RollbackItem(Delete, primary_key, it->first._xmin));
                return "succeed\n";
            }
        } else {
            // write-write conflict
            _txn_state[txn._tid] = Abort;
            return "Abort\n";
        }
    }

    if (it->second._xmax != 0) {
        if (it->second._xmax >= *txn._active_txns.rbegin() || txn._active_txns.count(it->second._xmax)) {
            if (it->second._xmax == txn._tid) {
                return "failed to find tuple\n";
            }
            // write-write conflict
            _txn_state[txn._tid] = Abort;
            return "Abort\n";
        }

        return "failed to find tuple\n";
    }

    // otherwise, the tuple is valid and not being updated by outstanding txn
    // then we delete it
    it->second._xmax = txn._tid;
    txn._rollback_list.push_back(RollbackItem(Delete, primary_key, it->first._xmin));
    return "succeed\n";
}

bool TransactionManager::visible(Transaction &txn, const pair<Key, Value> &record) {
    if (record.first._xmin >= *txn._active_txns.rbegin() || txn._active_txns.count(record.first._xmin)) {
        // not created by ourself
        if (record.first._xmin != txn._tid) {
            return false;
        } else {
            return record.second._xmax != txn._tid;
        }
    }

    if (record.second._xmax >= *txn._active_txns.rbegin() || txn._active_txns.count(record.second._xmax)) {
        if (record.second._xmax == txn._tid) {
            return false;
        } else {
            return true;
        }
    }

    return record.second._xmax == 0;
}

string convert(int primary_key, const Value &record) {
    return to_string(primary_key) + ", " + to_string(record._b) + ", " + to_string(record._c) + "\n";
}

string TransactionManager::select(Transaction &txn, int primary_key, bool scan_all) {
    lock_guard<mutex> guard(_mu);

    string result;
    if (!scan_all) {
        auto it = _db.lower_bound(Key(primary_key, _cur_tid));
        if (it == _db.end() || it->first._a != primary_key) {
            return "failed to find record\n";
        }

        while (it != _db.end() && it->first._a == primary_key) {
            if (visible(txn, *it)) {
                result += convert(primary_key, it->second);
                return result;
            }
        }
    }

    int last = -1;
    for (const auto &[key, value] : _db) {
        if (key._a == last) {
            continue;
        }
        if (visible(txn, {key, value})) {
            result += convert(key._a, value);
            last = key._a;
        }
    }
    if (result == "") {
        return "failed to find record\n";
    }
    return result;
}

string TransactionManager::commitTxn(Transaction &txn) {

}

string TransactionManager::abortTxn(Transaction &txn) {

}