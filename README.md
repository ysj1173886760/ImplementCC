Implement a toy MVCC and percolator to enhance your understanding of these knowledges.

Provide a basic interactive interface, and convert transactional sql statements into kv queries.

Table format and basic interface

```sql
table A(int, primary), B(int), C(int)
select or select where A=a(int)
insert a(int),b(int),c(int)
update a(int),b(int),c(int) where A=a(int)
delete where A=a(int)
```

## MVCC test

I've tested the common anomaly, such as unrepeatable-read, phatom-read. And it all looks good to me.

This implementation of MVCC is very similiar to that of Postgres, it can detect lost update automatically.

I havn't write strong test to test this implementation, so there may be some bugs left.

## Lessons

After i've implemented MVCC, i found it's not hard to understand. But MVCC really contains a lot of details, such as how to abort, how to keep track of active txns.

I think the visibility rule is not hard. And we don't need to care about the whether a txn's timestamp is bigger or smaller. For every record, we just need to consider the `xmax` and `xmin`, and there is only 3 situations we need to reason about, `Abort`, `Committed`, and `Active`, which creates 9 different situations.

While i was trying to implement lost-update detection, i found that i need to add some thing on visibility rules. Because normally, while doing select, we just apply visibility rule on every record and collect them. But while updating some records, we need to find the newest record(whether it's manipulating by outstanding txn or not) to detect concurrent writing.

## Percolator 

I've tested percolator just like what i do to mvcc. It may still contains some bugs, and i've not test the machine failure since i didn't mimic the RPC request.

Percolator requires single-row transaction, which requires WAL to implement it. But i think we just need a small latch while updating the row. i.e. avoid race conditions. 

Consider the situation while commiting, if we didn't guarantee the atomicity of writing to a single row. In pre-write phase, if we managed to confirm the write but failed to clean the lock, other transactions will clean it because it will try to find the corresponding record in CF_write. And if we failed to confirm the write, then the result should be determined by the primary. So i think while porting this toy implementation to some kv server, the only thing we need to do is acquire the latch on this row.

I havn't implement cleaning up the locks, but i think that would be easy to do. A easist implementation would be wait for some time, and if lock still exists, then we will iterate the CF_write in primary to confirm whether txn has commited. And if is not, then we can clean the lock.