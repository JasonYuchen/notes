# Replication Flow Control

参考自dragonboat，从实现来看源自etcd

Raft的一个远端节点作为follower可能处于多种状态，例如无响应（宕机、网络分区等）、缓慢、接收snapshot、正常复制等状态，不同状态下对于leader发送的需要复制的日志来说处理能力不同，因此leader通过`class Remote`来追踪每个节点的状态，并**根据状态来调整replication的策略**，从而最大化利用网络带宽

- `Retry`：leader依然会向处于`Retry`的远端节点尝试性的发送少量日志仅需replication，在etcd等其他Raft实现中，这也被称为**探测probe**，这种状态通常可能是是由于log不匹配等原因拒绝了leader最新的replication，此时[leader需要回退index逐步寻找到日志的匹配点](https://github.com/JasonYuchen/notes/blob/master/raft/03.Basic_Raft_Algorithm.md#5-%E6%97%A5%E5%BF%97%E5%A4%8D%E5%88%B6-log-replication)，或是远端节点失联导致连接中断
- `Wait`：leader不会向处于`Wait`的远端节点发送任何信息，通常当leader发送了snapshot后就会标记远端节点为`Wait`状态停止发送后续log，此时远端节点可能正处于利用快照重建状态机的过程中，leader会等待远端节点主动发送响应（etcd实现中并没有`Wait`状态）
- `Replicate`：leader会乐观的向处于`Replicate`的节点发送大量日志进行replication，此时集群通常处于网络正常、节点正常响应、没有进行快照的良好状态
- `Snapshot`：leader不会向处于`Snapshot`的远端节点发送任何信息，通常因为远端节点日志远落后于leader导致必须发送snapshot才能跟随replication，当处于`Snapshot`时leader会在发送完snapshot后将状态修改为`Wait`

```cpp
class Remote {
 public:
  enum State : uint8_t {
    // Retry means leader could probe the remote after:
    // 1. The remote has been Wait and now leader receives a heartbeat response
    //    which means leader could probe the remote then, set remote as Retry.
    // 2. The remote has been Wait or Replicate and now leader receives a
    //    rejection of the replication, try to decrease the next index for the
    //    remote and set the remote as Retry.
    // 3. The remote has been Wait and now leader receives an acceptance of the
    //    replication, try to increase the next & match index for the remote
    //    and set the remote as Retry.
    // 4. The leader receives an acceptance of replication, if the remote is in
    //    Snapshot state and the match >= snapshotIndex, set the remote as Retry
    //    and do probing.
    // 5. The remote is unreachable, if it is Replicate then set it as Retry.
    // Retry is an active state and the leader can send messages.
    Retry,
    // Wait means leader should stop replicating after:
    // 1. The remote has been Snapshot and leader receives the result, set the
    //    remote as Wait whether the result is successful or failed.
    //    If successful, just wait for the replication response which will be
    //    sent once the remote is restored (the remote will responds a
    //    replication response once restored the snapshot).
    //    If failed, just wait for another heartbeat interval before next probe
    //    (the ReportSnapshotStatus will be called when transport module failed
    //    to send the snapshot which eventually calls becomeWait).
    // 2. Leader usually optimistically updates the next for remotes, but if a
    //    remote has been in Retry, set it as Wait to reduce redundant probes.
    // Wait is an inactive state and the leader won't send messages.
    Wait,
    // Replicate means the remote is eager for the replication after:
    // 1. The remote has been Retry and leader receives an acceptance of the
    //    replication, set the remote as Replicate.
    // Replicate is an active state and the leader can send messages.
    Replicate,
    // Snapshot means the remote is far behind the leader and needs a snapshot:
    // 1. The remote is set as Snapshot if the required logs are compacted.
    // Snapshot is an inactive state and the leader won't send messages.
    Snapshot,
    NumOfState
  };
  uint64_t Match = 0;
  uint64_t Next = 0;
  uint64_t SnapshotIndex = 0;
  bool Active = false;
  enum State State = State::Retry;

  bool isActive() const;
  void setActive();
  void setNotActive();
  bool isPaused() const;

  // The remote state is unknown, retry to find its index.
  // See probe state in etcd:
  // https://github.com/etcd-io/etcd/blob/master/raft/tracker/progress.go#L43
  void becomeRetry();

  // The remote state is waiting due to inflight snapshot.
  // The etcd does not have this state (retry + wait here = probe in etcd).
  void becomeWait();

  // The remote state is clear, keep replicating logs.
  // See replicate state in etcd:
  // https://github.com/etcd-io/etcd/blob/master/raft/tracker/progress.go#L43
  void becomeReplicate();

  // The snapshot is inflight, stop replicating logs.
  // See snapshot state in etcd:
  // https://github.com/etcd-io/etcd/blob/master/raft/tracker/progress.go#L43
  void becomeSnapshot(uint64_t index);

  void retryToWait();
  void waitToRetry();
  void replicateToRetry();
  void clearPendingSnapshot();

  // Will be called when receives ReplicateResp, try to update the log track of
  // the remote node, return true if succeed.
  bool tryUpdate(uint64_t index);

  // Will be called when sends Replicate message, optimistically update the
  // "next" for a remote in Replicate state.
  void optimisticUpdate(uint64_t lastIndex);

  // Will be called when receives ReplicationResp with rejection, try to
  // decrease the log track of the remote node, return true if succeed.
  bool tryDecrease(uint64_t rejectedIndex, uint64_t remoteLastIndex);

  void respondedTo();
};
```
