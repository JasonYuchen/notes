# Log Storage

dragonboat的log分为两个部分提供给Raft模块：

1. 一部分较近的log缓存在内存中通过InMemory提供快速访问
2. 已持久化的log保存在磁盘中LogReader提供较慢的访问

一种可能的日志分布示例如下：

```text
     compacted     not in memory             in memory
 [ .......... 3 ] [ 4  5  6  7 ] [ 8  9  10  11  12  13  14  15  16 ]

 InMemory   [+++]                [++++++++++++++++++++++++++++++++++]
              |                    |      |       |       |       |
           snapshot            marker  applied  commit  savedTo  last

 LogReader  [+++] [+++++++++++++++++++++++++++++++++++++++++]
              |     |                                     |
           marker first                                  last
```

## InMemory

通常情况下，当Raft对一条entry达成共识后，即过半节点已经持久化，可以安全删除这条entry。考虑到下述两点，InMemory中缓存了更多的log：

1. 需要将达成共识的log应用到RSM上，因此commit后的log应该先apply再删除
1. 部分落后的节点可能会需要leader发送较旧的log，因此即使已经apply了，InMemory依然会额外保存了一部分log，具体数量可以通过`soft.go:InMemEntrySliceSize`调整

每次处理完一批日志（持久化、应用到状态机），通过`inMemory::commitUpdate`会更新`appliedToIndex/appliedToTerm/savedTo`，通过`inMemory::merge`使得新的日志在InMemory中可见并更新`markerIndex/entries`

```go
// inMemory is a two stage in memory log storage struct to keep log entries
// that will be used by the raft protocol in immediate future.
type inMemory struct {
  shrunk         bool         // 是否需要释放多余占用的内存
  snapshot       *pb.Snapshot // 缓存的最近一次snapshot
  entries        []pb.Entry   // 缓存的日志
  markerIndex    uint64       // 缓存的第一条日志index
  appliedToIndex uint64       // 最后一条apply日志的index
  appliedToTerm  uint64       // 最后一条apply日志的term
  savedTo        uint64       // 已经持久化的最近一条日志index
  rl             *server.InMemRateLimiter
}
```

## LogReader

LogReader给Raft提供了全部持久化日志和快照，具体实现是通过ShardedDB结构对rocksdb进行读取，在LogReader中并没有保存任何实际日志数据，仅有一些元信息

```go
// LogReader is the struct used to manage logs that have already been persisted
// into LogDB. This implementation is influenced by CockroachDB's
// replicaRaftStorage.
type LogReader struct {
  sync.Mutex
  clusterID   uint64
  nodeID      uint64
  logdb       raftio.ILogDB  // 访问持久化日志的接口，默认实现是基于rocksdb的ShardedDB
  state       pb.State       // term, vote, commit
  snapshot    pb.Snapshot
  markerIndex uint64         // compaction后snapshot的index，注意与InMemory的markerIndex区分
  markerTerm  uint64         // compaction后snapshot的term
  length      uint64         // 目前可读的日志条数
}
```

`TODO`

## ShardedDB

`TODO`
