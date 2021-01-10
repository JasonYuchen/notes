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

## rafter - segmented log files

```text
                     -------------- segment manager --------------
                    /                      |                      \             
                   /                       |                       \
              segment ptr              segment ptr              segment ptr
             +-----------+                 |                        | 
             |   meta    |                 |                        |
             +-----------+                 |                        |
             |inmem index|                 |                        |
             +-----------+                 |                        |
memory            |                        |                        |
------------------|------------------------|------------------------|-------------
disk(s)           |                        |                        |
               00001.log               00056.log           00129.log_inprogress
             +-----------+           +-----------+            +-----------+
             |           |           |           |            |           |
             +-----------+           +-----------+            +-----------+
                  
* segment ptr holds an in-memory index for each segment log file: 
    vector<pair<uint64_t/*offset*/, uint64_t/*term*/>>
  which is constructed when loading the segment log file.

different clusters can speficify different directories to store raft data.

rafter @ disk0
  |--<clusterID:020d_nodeID:020d>
  |    |--config
  |    |--<startIndex:020d>.log
  |    |--<startIndex:020d>.log
  |    |--<startIndex:020d>.log_inprogress
  |    |--...
  |    |--<includeIndex:020d>.snapdata
  |    |    |--snapshot
  |    |    |--<fileID>.external          <-- hard link to user provided file
  |    |    |--<fileID>.external
  |    |--<uuid>.snapdata                 <-- when receiving snapshot from leader or generating new snapshot, only rename to finalDir when completed
  |         |--snapshot
  |         |--<fileID>.external
  |         |--<fileID>.external
  |--<clusterID:020d_nodeID:020d>

rafter @ disk1
  |--<clusterID:020d_nodeID:020d>
```

**dragonboat:**

- 对于`snapshot`，直接存储在rocksdb中，并且包含一个`filepath`指向`snapshot`具体的`snapshot data`数据文件（`session, statemachine`写入的数据等）
- 对于user statemachine在`file collection`中提供的文件，直接hard link到`snapshot data`目录下，发送时从这里发送见`rsm/files.go::Files::PrepareFiles()`

**rafter:**

- 对于`snapshot`，在`.snapdata`目录下存储`snapshot`文件（即drganboat中的`snapshot`和`snapshot data`二合一）
- 对于user statemachine在`file collection`中提供的文件，直接hard link到`snapshot data`目录下，
- 发送`snapshot`时，将`.snapdata`下的每一文件单独分割成数个`snapshotchunk`发送，在remote再重建为`snapshot, <fileID>.external`文件
- 接收或生成`snapshot`时，所有文件都暂存在`<uuid>.snapdata`中，待全部完成后，最后一步是`rename`为`<includeIndex:020d>.snapdata`

```text
             segment
    +-----------------------+
    | 64bit latest term     |
    +-----------------------+
    | 64bit latest vote     |
    +-----------------------+
    | 64bit latest commit   |  <-- fdatasync is enough
    +-----------------------+
    | 64bit start index     |  <-- the same as the end index of last completed segment
    +-----------------------+
    | 64bit end index       |  <-- initially 0, filled when rolling, exclusive
    +-----------------------+
    |  8bit checksum type   |
    +-----------------------+
    | 24bit reserved        |
    +-----------------------+
    | 32bit header chksum   |
    +-----------------------+
    | 64bit log term        |  <-- log entry
    | 64bit log index       |
    |  8bit log type        |
    |  8bit checksum type   |
    | 16bit reserved        |
    | 32bit data length     |  // TODO: check the checksum implementation in braft
    | 32bit data chksum     |  <-- the checksum of data
    | 32bit header chksum   |  <-- the checksum of previous fields
    | bytes data            |
    +-----------------------+
    |        ......         |
    +-----------------------+  <-- exceeds segment max size, rolling

            snapshot
    +-----------------------+
    | 64bit include term    |
    +-----------------------+
    | 64bit include index   |
    +-----------------------+
    |  8bit header chk type | <-- checksum type for header (except 4 bytes fields)
    +-----------------------+
    |  8bit data chk type   | <-- checksum type for data
    +-----------------------+
    |  8bit compression type| <-- compression type for payload
    +-----------------------+
    |  8bit sm type         | <-- state machine type
    +-----------------------+
    | 32bit option          | <-- is_witness: 1<<0, is_dummy: 1<<1, is_imported: 1<<2 
    +-----------------------+
    | 64bit membership len  |
    +-----------------------+
    | 64bit snapshotfile len|
    +-----------------------+
    | 64bit session len     |
    +-----------------------+
    | 64bit payload len     |
    +-----------------------+
    | 32bit data checksum   |
    +-----------------------+
    | 32bit header checksum |
    +-----------------------+
    |512bit reserved        | <-- to form a header of 128 Bytes (1024 bits)
    +-----------------------+
    | bytes membership      |
    +-----------------------+
    | bytes snapshotfile    |
    +-----------------------+
    | bytes session         |
    +-----------------------+
    | bytes payload         |
    +-----------------------+
```
