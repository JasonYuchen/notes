# Project (2020)

做Project的一些踩坑记录，该项目的源代码不开放

## 1. **C++ Primer**

- Matrix ✅
- Matrix Operation ✅

C++入门项目，略过

## 2. **Buffer Pool Manager**

- LRU Replacement Policy ✅
- Buffer Pool Manager ✅

一个LRU的置换算法+缓存池管理，在脏页判断时犯了一个错误，一个页一旦变为脏页，就一直是脏页直到写入磁盘，因此在设置脏页位`is_dirty_`时必须用`|=`而不能用`=`

```C++
bool BufferPoolManager::UnpinPageImpl(page_id_t page_id, bool is_dirty) {
  std::lock_guard<std::mutex> guard(latch_);
  if (auto p = page_table_.find(page_id); p != page_table_.end()) {
    auto fid = p->second;
    // once true, always true, cannot just assign
    // WRONG: pages_[fid].is_dirty_ = is_dirty;
    pages_[fid].is_dirty_ |= is_dirty;
    // return false if already <= 0
    if (pages_[fid].pin_count_ <= 0) {
      return false;
    }
    if (pages_[fid].pin_count_-- == 1) {
      replacer_->Unpin(fid);
    }
  }
  // just return true if not found
  return true;
}
```

## 3. **B+ Tree Index**

- B+ Tree Pages ✅
- B+ Tree Data Structure ✅
- Index Iterator ✅
- Concurrent Index ✅

实现B+树索引，分为搜索、插入、删除、迭代器、并发支持五步实现，难度显著比前两个project要大，尤其是并发支持需要考虑的非常细致，尤其是：

- `buffer_pool_manager->FetchPage`和`buffer_pool_manager->UnpinPage`必须配对
  类似`new/delete`配对，写的过程中有一处少了一次`UnpinPage`导致资源泄露，导致其他测试都能正常通过，而在**ScaleTest中因为资源泄露**达到`buffer_pool_manager`的上限，随后`FetchPage`都会返回空指针（缓存池已满）随后触发内存错误
- 加锁与解锁也要配对，并且必须先解锁再调用`buffer_pool_manager->UnpinPage`
- 额外的根节点ID即`root_page_id_`也必须被锁保护，通过额外一个`ReaderWriterLatch`保护，由于对此锁也需要配对加锁和解锁操作，每个线程采用线程本地变量TLS来记录加解锁次数，防止死锁
- 全部正确实现依然有可能无法通过gradescope中的`test_memory_safety`，这是由于valgrind运行极其缓慢，非常有可能超时，可以参考的优化是
  - 节点中查找元素可以使用二分搜索
  - 尽可能减少不必要的加锁（大粒度加锁导致并发性能下降）
  - **个人在过程中使用了大量的`assert`，尤其是每次插入元素都会`assert(std::is_sorted)`导致性能非常差**，确认正确实现后，去掉所有`assert`就解决了超时问题
  - 从论坛discord来看，大量使用`LOG`也会导致超时

## 4. **Query Execution**

- System Catalog ✅
- Executors ✅

实现系统元数据表以及流水线式的查询执行器，具体包括：

- 顺序扫描算子 Sequential Scan
- 索引扫描算子 Index Scans
- 插入算子 Insert
- 更新算子 Update
- 删除算子 Delete
- 嵌套循环连接算子 Nested Loop Join
- 索引嵌套循环连接算子 Index Nested Loop Join
- 聚合算子 Aggregation
- 限制算子 Limit

一些注意点如下：

- 在`Catalog`中应抛出的是`std::out_of_range`而不是`Exception(ExceptionType::OUT_OF_RANGE)`
- `INSERT/UPDATE/DELETE`不应该有返回的结果，在对应的`executor`执行后不应该将无效的`Tuple`加入到`result_set`，因此这三个操作的`Next`总是返回`false`
- `UPDATE`如果更新的数据列上有索引，则应该同时更新索引，先删除旧值，再插入新值
- 嵌套循环连接算子注意需要保存当前内外表的处理位置，因为`Next`需要从上次的位置开始继续查找下一个能连接的`Tuple`

    ```C++
    while (true) {
      Tuple tmp_inner_tuple;
      RID tmp_inner_rid;
      if (next_left_) {
        next_left_ = false;
        if (!left_executor_->Next(&tmp_outer_tuple_, &tmp_outer_rid_)) {
          done_ = true;
          return false;
        }
      }
      while (right_executor_->Next(&tmp_inner_tuple, &tmp_inner_rid)) {
        // iterate inner table to find a match
        return true;
      }
      // don't forget to re-init the inner table executor for the next re-iteration
      right_executor_->Init();
      next_left_ = true;
    }
    ```

- 所有会输出结果的执行器都应该根据`OutputSchema`重新组织将输出的`Tuple`，根据每一列的`Expression`进行`Evaluate*`来获取对应的数据：

    ```C++
    std::vector<Value> values;
    for (const auto &column : output_schema->GetColumns()) {
      values.emplace_back(column.GetExpr()->Evaluate(&origin, origin_schema));
      // For join
      values.emplace_back(column.GetExpr()->EvaluateJoin(&left, left_schema, &right, right_schema));
      // For aggregation
      values.emplace_back(column.GetExpr()->EvaluateAggregate(group_bys, aggregates));
    }
    return Tuple(std::move(values), output_schema);
    ```

- 聚合算子的核心是在`Init`时就完成结果的计算，`Next`只是使用`SimpleAggregationHashTable::Iterator`逐个输出：

    ```C++
    void AggregationExecutor::Init() {
      aht_ = std::make_unique<SimpleAggregationHashTable>(plan_->GetAggregates(), plan_->GetAggregateTypes());
      Tuple tmp_tuple;
      RID tmp_rid;
      while (child_->Next(&tmp_tuple, &tmp_rid)) {
        aht_->InsertCombine(MakeKey(&tmp_tuple), MakeVal(&tmp_tuple));
      }
      aht_iterator_ = aht_->Begin();
    }
    ```

## 5. **Concurrency Control**

- Lock Manager
- Deadlock Detection
- Concurrent Query Execution
