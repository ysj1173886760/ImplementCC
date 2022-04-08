#include <iostream>
#include <chrono>
#include <thread>
#include "percolator.h"
#include "assert.h"

using namespace std;

bool TransactionManager::get(Transaction &txn, int primary_key, Value &value) {
    {
        auto it = txn._write_list.find(primary_key);
        if (it != txn._write_list.end()) {
            if (it->second._type == Delete) {
                return false;
            }
            value = it->second._value;
            return true;
        }
    }

    int shard = hash(primary_key);
    while (true) {
        // first check lock, range from 0 to start_time_stamp
        _servers[shard]._latch.lock();
        {
            auto it = _servers[shard]._lock_cf.lower_bound(Key(primary_key, txn._start_timestamp));
            if (it != _servers[shard]._lock_cf.end() && it->first._key == primary_key) {
                // TODO: check the wall time and maybe clean the lock
                // there is a lock, we shall wait
                _servers[shard]._latch.unlock();
                
                // sleep to reduce contention
                this_thread::sleep_for(chrono::milliseconds(100));
                continue;
            }
        }

        // find latest write below start timestamp
        auto it = _servers[shard]._write_cf.lower_bound(Key(primary_key, txn._start_timestamp));
        if (it == _servers[shard]._write_cf.end() || it->first._key != primary_key) {
            // we can't find any write
            _servers[shard]._latch.unlock();
            return false;
        }

        int start_ts = it->second._timestamp;
        auto it_default = _servers[shard]._default_cf.find(Key(primary_key, start_ts));

        // we shall find this write record
        assert(it_default != _servers[shard]._default_cf.end());

        // check whether write is put
        if (it_default->second._type == Delete) {
            _servers[shard]._latch.unlock();
            return false;
        }

        value = it_default->second._value;
        _servers[shard]._latch.unlock();
        return true;
    }
}

void TransactionManager::set(Transaction &txn, int primary_key, const Default &value) {
    // overwrite existing value
    txn._write_list[primary_key] = value;
}

Transaction TransactionManager::beginTxn() {
    lock_guard<mutex> guard(_mu);
    _cur_tid++;
    Transaction t;
    t._start_timestamp = _cur_tid;
    return t;
}

string TransactionManager::insert(Transaction &txn, int primary_key, const Value &value) {
    // get newest record
    Value val;
    if (get(txn, primary_key, val)) {
        // record already exists
        return "record already exists\n";
    }

    set(txn, primary_key, Default(value, Put));
    return "succeed\n";
}

string TransactionManager::update(Transaction &txn, int primary_key, const Value &value) {
    // get newest record
    Value val;
    if (!get(txn, primary_key, val)) {
        // record already exists
        return "record not exists\n";
    }

    set(txn, primary_key, Default(value, Put));
    return "succeed\n";
}

string TransactionManager::remove(Transaction &txn, int primary_key) {
    // get newest record
    Value val;
    if (!get(txn, primary_key, val)) {
        // record already exists
        return "record not exists\n";
    }

    set(txn, primary_key, Default(Value(), Delete));
    return "succeed\n";
}

string convert(int primary_key, const Value &record) {
    return to_string(primary_key) + ", " + to_string(record._b) + ", " + to_string(record._c) + "\n";
}

string TransactionManager::select(Transaction &txn, int primary_key, bool scan_all) {
    string result;
    if (!scan_all) {
        Value val;
        if (!get(txn, primary_key, val)) {
            return "failed to find record\n";
        }
        return convert(primary_key, val);
    }

    int last = -1;
    vector<int> meta;
    // gather the meta-data
    // this should be done at global manager(PD)
    for (auto &server : _servers) {
        server._latch.lock();
        for (const auto &[key, value] : server._default_cf) {
            if (key._key == last) {
                continue;
            }
            meta.push_back(key._key);
            last = key._key;
        }
        server._latch.unlock();
    }

    for (const auto &[key, value]: txn._write_list) {
        int shard = hash(key);
        _servers[shard]._latch.lock();
        auto it = _servers[shard]._default_cf.lower_bound(Key(key, INT32_MAX));
        if (it == _servers[shard]._default_cf.end() || it->first._key != key) {
            // not found, then we add it to metadata
            meta.push_back(key);
        }
        _servers[shard]._latch.unlock();
    }

    // do the real look-up
    for (const auto &k : meta) {
        Value val;
        if (get(txn, k, val)) {
            result += convert(k, val);
        }
    }

    if (result == "") {
        return "failed to find record\n";
    }
    return result;
}

bool TransactionManager::Prewrite(Transaction &txn, int primary_key, const Default &write_intent, const Key &primary) {
    int shard = hash(primary_key);
    _servers[shard]._latch.lock();

    // abort write after our start timestamp
    {
        auto it = _servers[shard]._write_cf.lower_bound(Key(primary_key, INT32_MAX));
        if (it == _servers[shard]._write_cf.end() || 
            it->first._key != primary_key ||
            it->first._timestamp < txn._start_timestamp) {
            // we are safe
        } else {
            _servers[shard]._latch.unlock();
            return false;
        }
    }

    // abort if there is lock at any timestamp
    {
        auto it = _servers[shard]._lock_cf.lower_bound(Key(primary_key, INT32_MAX));
        if (it == _servers[shard]._lock_cf.end() ||
            it->first._key != primary_key) {
            // we are safe
        } else {
            _servers[shard]._latch.unlock();
            return false;
        }
    }

    _servers[shard]._default_cf.insert(
        make_pair(Key(primary_key, txn._start_timestamp), write_intent));
    _servers[shard]._lock_cf.insert(
        make_pair(Key(primary_key, txn._start_timestamp), Lock(primary._key, primary._timestamp)));

    _servers[shard]._latch.unlock();
    return true;
}

bool TransactionManager::commitTxn(Transaction &txn) {
    if (txn._write_list.size() == 0) {
        return true;
    }
    vector<pair<int, Default>> write_intents;
    for (const auto &x : txn._write_list) {
        write_intents.push_back(x);
    }

    // commit primary first
    if (!Prewrite(txn, 
        write_intents[0].first, 
        write_intents[0].second, 
        Key(write_intents[0].first, txn._start_timestamp))) {
        return false;
    }
    
    for (int i = 1; i < write_intents.size(); i++) {
        if (!Prewrite(txn,
            write_intents[i].first,
            write_intents[i].second,
            Key(write_intents[0].first, txn._start_timestamp))) {
            return false;
        }
    }

    _mu.lock();
    _cur_tid++;
    txn._commit_timestamp = _cur_tid;
    _mu.unlock();

    // first check the lock on primary
    // because we use primary to synchronize the commit point
    {
        int shard = hash(write_intents[0].first);
        _servers[shard]._latch.lock();
        auto it = _servers[shard]._lock_cf.find(Key(write_intents[0].first, txn._start_timestamp));
        if (it == _servers[shard]._lock_cf.end()) {
            return false;
        }

        // this should wrapped in a transaction
        // erase the lock
        _servers[shard]._lock_cf.erase(Key(write_intents[0].first, txn._start_timestamp));
        // confirm the write
        _servers[shard]._write_cf.insert(make_pair(
            Key(write_intents[0].first, txn._commit_timestamp),
            Write(txn._start_timestamp)));

        _servers[shard]._latch.unlock();
    }

    // from now on, even if the server crashed, the txn still managed to commit
    // in another words, we can return here directly, and do the second phase asynchronizly
    for (int i = 1; i < write_intents.size(); i++) {
        int shard = hash(write_intents[i].first);
        _servers[shard]._latch.lock();

        _servers[shard]._lock_cf.erase(Key(write_intents[i].first, txn._start_timestamp));
        _servers[shard]._write_cf.insert(make_pair(
            Key(write_intents[i].first, txn._commit_timestamp),
            Write(txn._start_timestamp)));

        _servers[shard]._latch.unlock();
    }

    return true;
}

void TransactionManager::abortTxn(Transaction &txn) {
    vector<pair<int, Default>> write_intents;
    for (const auto &x : txn._write_list) {
        write_intents.push_back(x);
    }

    // abort txn will just clean the lock
    for (int i = 0; i < write_intents.size(); i++) {
        int shard = hash(write_intents[i].first);
        _servers[shard]._latch.lock();
        _servers[shard]._lock_cf.erase(Key(write_intents[i].first, txn._start_timestamp));
        _servers[shard]._latch.unlock();
    }
}