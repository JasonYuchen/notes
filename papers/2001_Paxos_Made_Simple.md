# [2001] Paxos Made Simple

## 共识问题 The Problem

假定一组进程processes可以提出值propose values，**共识算法的目的就是确保这组提交值里只有一个值被选择**（即所有进程对被选择的值达成共识consensus），其安全性要求包括：

- 只有被提出的值才有可能被选择，由 **Proposers** 负责
- 只有单个值被选择，由 **Acceptors** 负责
- 任意一个进程只有在某个值被选择后，才能感知到该值（未被选中的值不应该被其他进程感知），由 **Learners** 负责

在本文中不会对**活性liveness**做出要求，但是目标依然是当一个值被选中后，所有进程最终可以感知到

实际实现中，单个进程可以充当一个或多个角色（**proposer、acceptor、learner统称为agent**），并且假定所有消息传递通过异步通信完成，且没有恶意进程，即**非拜占庭模型non-Byzantine model**：

- agents以任意的速度完成工作，可能随时宕机、重启
- messages以任意延迟抵达通信对端，可能重复、丢失、乱序，但数据本身可靠不会被篡改

## 选择一个值 Choosing a Value

显然最简单的选择值的方式就是只有**单个acceptor**作为single source of truth，所有proposers都通过发送proposals给这个acceptor，随后由该acceptor唯一的确认一个值（例如选择第一个收到的proposal），但是这种模型在分布式领域中难堪大用，因为单个acceptor导致了**单点故障SPOF**，一旦acceptor宕机就会导致整个系统无法工作

实际可行的方式是采用一组acceptors，每个proposer发送proposal给每个acceptor，并且**由这组acceptors中的多数majority来选择一个值**（**任意majority至少重叠一个acceptor，而每个acceptor不能同时选择多个值**，从而保证了只要达成accetpors中的majority就可以确保唯一）

在没有消息的丢失和进程宕机的情况下，为了acceptors只选择一个被提出的值，要求：

- **P1**: acceptor一定要选择第一个收到的proposal

P1要求也引入了另一个问题，当多个proposer接近同时提出了多个值，而每个acceptor都选择第一个收到的值，就可能**导致没有一个提案获得了majority**，从而放宽限制允许每个acceptor选择多个提案，但是对不同的提案都需要赋予一个自然数来追踪，从而每个提案包括了**值value**和**提案号proposal number**，并且为了避免冲突**每个提案号应该唯一**（如何对每个提案赋予唯一的提案号由具体实现来确定）

此时acceptor可以接受多个提案，并且只要当某个提案被majority接受时，其对应的值就默认被选中，此时就可能出现多个提案被选中，但是需要确保**被选中的多个提案拥有相同的值**，可以通过保证P2来实现：

- **P2**: 如果某个提案被选中，其对应的值是`v`，则任意提案号更大的被选中的后续提案也包含`v`

由于**提案号是全序的totally ordered**，P2确保了单个值被选中（提案号较小的提案及`v`被majority选中，后续提案号较大的提案必须包含该majority选中的`v`则后续提案号较大的提案也被majority选中），P2可以通过简化的P2A来满足：

- **P2A**：如果某个提案被选中，其对应的值是`v`，则每个被任意acceptor接受的更大提案号的提案都含有`v`

此时依然**需要P1来确保总是有提案被选择**，但是异步通信可以有任意延迟、乱序、丢失，某个不该被接受的提案（不包含`v`）可能被此前没有收到过任何提案的acceptor接受，违背了P2A，则P2A可以被修改为P2B：

- **P2B**：如果某个提案被选中，其对应的值是`v`，则后续每个proposer提出的更大提案号的提案必须含有`v`

假定一个提案带有提案号`m`及值`v`，新提案带有提案号`n`（`n > m`）必须也包含`v`，则需要证明任意提案号在`[m, n-1]`的提案包含了`v`，而提案号`m`的提案需要被选中则一定要存在一个acceptor的majority集合`C`并且其中每个acceptor都接受`m`，整合如下：

- `C`中的每个acceptor都接受了一个提案号在`[m, n-1]`中的提案，并且每个被任意acceptor接受的`[m, n-1]`中的提案都一定含有`v`

由于任意一个包含majority acceptors的集合`S`都一定和`C`（也是一个majority）存在至少一个重合的acceptor，从而可以通过确保P2C来满足P2B，来保证一个提案号`n`的提案包含值`v`：

- **P2C**：对于任意的`v`和`n`，如果一个提案对应的值是`v`且提案号是`n`，则有一个acceptor的majority集合`S`满足以下任意一条：
  - `S`中的acceptors均没有接受过提案号小于`n`的提案
  - `S`中所有acceptors已经接受的提案号小于`n`的提案中，提案号最大的提案含有`v`

为了满足P2C，**proposer在提出提案号`n`的提案时，必须感知已经被majority接受的（或将要被接受的）提案号小于`n`的提案中拥有最大提案号的提案**，显然感知已经被接受的提案较为直接，但是预测将要被接受的提案是非常困难的，通过**限制这种提案，即不允许提案号在已经被接受的最大提案号到`n`之间的提案继续被接受**来简化算法，proposer侧算法如下：

1. **准备 prepare**，proposer选择一个新的提案号`n`不带有值`v`并发送请求给某个acceptor集合中的每个成员，并要求收到的acceptor保证：
   - **不再响应提案号小于`n`的提案**
   - **返回该acceptor已经接受的最大提案号提案，或没有接受任何提案返回空**
2. **接受 accept**，若该proposer收到了majority的响应，则其可以发出该提案号为`n`的提案并且带有值`v`（`v`是所有响应中已被接收的提案中提案号最大的提案对应的值），或者任意值（如果所有响应都显示目前没有接受的提案），此次accept请求的目的地acceptors可以不与prepare请求的acceptors相同

acceptor会收到两类请求，即prepare请求和accept请求，acceptor可以随意忽略任意收到的prepare请求而不损害安全性，也总是可以响应prepare请求，而对于accept请求，acceptor只有在**没有保证不响应的时候**可以响应该请求并且接受对应的提案，即P1A：

- **P1A**：acceptor只有在尚未响应一个prepare请求（携带的提案号大于`n`）时可以接受一个提案号为`n`的提案

以上即为acceptor和proposer的完整算法，但还可以有一个小优化如下：假定acceptor收到一个prepare请求带有`n`的提案，该acceptor已经在此前响应了另一个prepare请求带有大于`n`的提案从而不能响应该请求，并且**额外要求该acceptor也忽略携带有它已经接受的提案的prepare请求**，从而**该acceptor只需要记住（持久化以确保能够在宕机重启后依然保持）接受过的最大提案号的提案**即可，而**proposer可以任意丢失提案，只需要每次发起提案的提案号都不同**

完整的算法归纳如下：

- **Phase 1**
  - proposer选择一个提案号`n`，并发送prepare请求给majority of acceptors
  - acceptor收到prepare请求时，如果自身已经响应的prepare请求对应的提案号均小于等于`n`，则可以响应该prepare请求并带上已经接受的最大提案号提案或空（没有接受过任何提案），并保证不再响应其他小于`n`的prepare请求
- **Phase 2**：
  - proposer收到了提案号为`n`的prepare请求的majority响应，则可以发送accept请求给acceptors，携带有提案号`n`值`v`的提案，`v`是所有prepare响应中提案号最大的提案带有的值`v`，或是任意值（如果所有响应表明都未接受过任何提案）
  - acceptor收到提案号`n`的accept请求时，只要没有响应过提案号大于`n`的prepare请求，就接受该accept请求

proposer可以一次**发出任意数量的提案**，只要服从上述算法且提案号唯一，并且proposer也可以**中途随意丢弃提案**而不影响正确性（例如当发现已经有其他proposer在使用更大的提案号时主动丢弃较小提案号的提案），从实现的角度来说，也可以优化acceptor的行为，使其在**主动拒绝提案号较小的prepare请求而不是忽略**，使得相应的proposer能感知到当前最大的提案号

## 感知被选择的值 Learning a Chosen Value

最简单的方式就是每当acceptor接受一个提案时，都广播给每一个learner，但是代价就是网络中需要传输acceptor与learner的乘积数量的消息，另一种方式是将learner区分为**normal learner**和**distinguished learner**，每次acceptor接受提案时就通知一组distinguished learners，随后由distinguished learner再广播给normal learners

异步网络中可以出现任意的消息延迟、乱序、丢失，因此有可能出现learner一直没有收到acceptor的接受提案通知，此时**learner也可以选择周期性主动从acceptor上拉取消息**来感知已经被接受的提案信息

## 避免活锁 Progress

显然上述算法没有保证系统总是有进展，例如p，q两个proposers始终在增加提案号并发出提案，p对`n1`完成了Phase 1，随后q对`n2 > n1`完成了Phase 1，而p随后的accept请求就会被忽视，因为所有acceptor都保证不再响应`n1 < n2`的请求，从而p就重新开始Phase 1并采用`n3 > n2`，显然q也只能继续`n4 > n3`的Phase 1，**周而复始陷入"活锁"**

为了避免这种清空，需要选择一个**distinguished proposer作为唯一一个可以提出提案的proposer**，被选出的distinguished proposer能够与majority acceptors通信，并且当其发出一个提案带有当前最大提案号时，就能成功完成两个阶段，当其发现有其他更大提案号时只需要重试更大的提案号即可

## 实现 The Implementation

实际实现中，每个进程作为proposer、acceptor、learner的角色，并且需要选举出一个**leader节点作为distinguished proposer以及distinguished learner**，稳定存储用来保存acceptor需要记录的信息，即其需要在响应请求前将响应内容response持久化保存，以及保存proposer需要记录的信息，即其目前使用到的最大提案号，每次发起提案时确保使用更大的提案号

所有proposer需要挑选独立的提案号来发起提案，可以通过每个proposer从互不重叠的提案号池中获取提案号，例如5个proposer编号为A-E，则A的提案号序列可以是`1->6->11...`，B的提案号相应就是`2->7->12...`

## 实现状态机 Implementing a State Machine

分布式系统的简单实现就是一组clients向中心服务器发送一系列命令commands，而中心服务器被抽象为一个确定性状态机deterministic state machine，从而一系列输入命令可以获得一系列输出结果

由于单个中心服务器会引入SPOF，因此可以采用一组中心服务器，每个中心服务器都维护一个复制状态机replicated state machine，所有clients的请求通过Paxos算法在每个服务器的状态机上执行，从而当任意一个服务器故障时，其余状态机的状态依然保持正确并可以继续响应clients的请求

为了保证所有服务器上的状态机都执行相同顺序的commands，即**对commands序列达成共识**，可以**实现一个Paxos共识序列instances of the Paxos consensus algorithm**，则第`i-th`Paxos实例所选择的值就是第`i-th`状态机需要执行的命令，服务器充当Paxos算法的所有角色proposer、acceptor、learner

正常情况下只有一个服务器充当leader，则所有clients都将commands发送给leader，由leader来决定执行的commands的顺序，而当出现故障例如leader宕机时，就会选举出新的leader（在此前所有Paxos实例中是learner，宕机时实际已经有140条commands出现），对新leader而言应该对绝大多数已经被选择的commands有感知，假定一个流程如下：

1. leader感知到了已经被选择的第1-134, 138-139条commands
2. leader从而**对135-137以及140-inf发起Paxos Phase I，在Paxos Phase I的响应中它获知了应该被选择的第135，140条commands**（majority acceptor返回了135和140个Paxos实例中的最大提案号提案，而对136-137，141-inf返回空）
3. leader随后对135和140执行Paxos Phase II以选择这两条commands
4. 对于位置的136和137，leader可以选择等待随后的clients请求来填充，也可**以直接采用两条`NoOP`指令来填充以允许立即执行后续的命令**，`NoOP`不应该改变状态机的状态
5. 选择了`NoOP`后，leader就可以对已经选择的1-140+的commands应用到状态机上，而不用等待136和137

核心在于虽然对全局的commands顺序达成了共识，但是实际上**commands顺序与clients的请求顺序并没有严格对应，但在异步网络的假设下本身消息就可以任意延迟、丢弃、乱序**

这种算法的核心在于**每个位置的commands都是单独的Paxos实例，即只要对每个位置的commands达成共识，而达成共识的顺序并不重要**，从而可以**乱序确认commands序列**，即如上述流程的先确认`[1, 134], [138, 139]`，再确认`135, 140`（也可以是`140, 135`）

完成以上流程后，leader就可以自由对第141条及以后执行Paxos Phase II来选择（在上述步骤2中已经完成了Paxos Phase I）commands，并且可以乱序并发发起，即不必等待141的完成就可以发起142的选择，这就有可能在序列中出现gap

## 成员变更 Membership Change

最简单的成员变更方式是**通过状态机自身进行成员变更，即当前参与Paxos协议的服务器组也作为状态的一部分**，并可以通过达成共识的commands进行执行，从而每个服务器对当前参与协议的服务器达成共识

例如leader可以通过执行第i条commands（协议成员指令），根据产生的结果来决定后续a条commands由哪些服务器来完成Paxos协议，以这种方式可以完成任意的集群成员变化

## Example

[origin post](https://blog.openacid.com/algo/paxos/)

- `last_rnd`：acceptor持久化保存的当前接受的最大提案号
- `v`：最后写入的值
- `v_rnd`：在哪个提案中相应的`v`被写入，即acceptor接受`v`对应的提案号

### 没有出现异常的基本流程

**Phase I阶段acceptor收到proposer的prepare请求时**：

- 若请求中`rnd`不大于自身`last_rnd`时，拒绝请求
- 将请求中`rnd`持久化保存并更新`last_rnd`，此后只可能接受带有此`rnd`的accept请求
- 返回，并带上此前的`last_rnd`以及此前接受的`v`

**Phase I阶段proposer收到acceptor的prepare响应时**：

- 若返回的任一响应出现`last_rnd > rnd`，则放弃并选用更大的`rnd`
- 从所有响应中选择`v_rnd`最大的`v`，因为此`v`有可能已经被选择，不能改变达成共识的值，必须选择此`v`
- 若所有响应的`v`均为空值，则可以选择想要达成共识的`v`
- 若响应的数量不足majority，放弃并随后重试

```text
Classic Paxos Phase I

         Proposer                         Acceptors 1, 2, 3
  P               X: rnd = 1               
  H         X ------------------------> [  -  ][  -  ][  -  ]
  A             1,2: last_rnd = 0,
  S                         v = null,
  E                     v_rnd = 0
  I         X <------------------------ [ 1,  ][ 1,  ][  -  ]
```

**Phase II阶段proposer发出accept请求**：

- 带上prepare请求所使用的`rnd`，并带上所选择的`v`（可能是自己选择的，或是从响应中认识到的）

**Phase II阶段acceptor收到proposer的accept请求时**：

- 拒绝`rnd < last_rnd`的请求，使用`last_rnd <= rnd`来确保其他proposer此阶段没有写入其他值
- 将`v`持久化存储，并标记为已接受，此后依然有可能被更大的`rnd`覆盖
- 收到拒绝accept请求的响应时proposer放弃并选用更大的`rnd`重新开始Phase I

```text
Classic Paxos Phase II

         Proposer                         Acceptors 1, 2, 3
  P               X: v = 'x', rnd = 1               
  H         X ------------------------> [ 1,  ][ 1,  ][  -  ]
  A
  S
  E             1,2: accepted
  II        X <------------------------ [ 1,x1][ 1,x1][  -  ]     // x1: v='x', v_rnd = 1
```

### 并发写冲突

假定由于网络原因，X与acceptor 3，以及Y与acceptor 1之间无法通信

```text
         Proposer                         Acceptors 1, 2, 3                         Proposer
                  X: rnd = 1               
            X ------------------------> [  -  ][  -  ][  -  ]
                1,2: rnd = 1 > 0, OK
            X <------------------------ [ 1,  ][ 1,  ][  -  ]
```

X在尚未完成两阶段Paxos时，Y就介入采用更大的提案号更新了acceptor 2的状态

```text
                                                                  Y: rnd = 2 
                                        [ 1,  ][ 1,  ][  -  ] <------------------------ Y
                                                                  2: rnd = 2 > 1, OK
                                                                  3: rnd = 2 > 0, OK
                                        [ 1,  ][ 2,  ][ 2,  ] ------------------------> Y
```

随后X认为自己还能完成，因此继续按流程发出accept请求，会被acceptor 1接受，但会被acceptor 2拒绝

```text
                  X: v = 'x', rnd = 1           
            X ------------------------> [ 1,  ][ 2,  ][ 2,  ]
                  1: accepted
                  2: rnd = 1 < 2, REJ
            X <------------------------ [ 1,x1][ 2,  ][  -  ]
```

随后Y认为自己还能完成，因此继续按流程发出accept请求，成功被majority接受，完成写入`v='y', v_rnd=2`

```text
                                                                  Y: v = 'y', rnd = 2
                                        [ 1,x1][ 2,  ][  -  ] <------------------------ Y
                                                                2,3: accepted
                                        [ 1,x1][ 2,y2][ 2,y2] ------------------------> Y
```

被拒绝后的X则更新提案号，采用更大的`rnd = 3`发起prepare，同样成功被acceptor 1和2接受，但此时X发现acceptor 2返回了较大的`rnd = 2`以及`v = 'y'`

```text
                  X: rnd = 3
            X ------------------------> [ 1,x1][ 2,y2][ 2,y2]
                  1: last_rnd = 1
                            v = x
                        v_rnd = 1
                  2: last_rnd = 2
                            v = y
                        v_rnd = 2
            X <------------------------ [ 3,x1][ 3,y2][ 2,y2]
```

根据约定，此时acceptor 2上的值有可能已经被majority接受（acceptor 1的`v = 'x'`不可能被接受，因为根据约定如果其他proposer发现这个值有可能被接受时就不会再覆盖而是主动继续使用`v = 'x'`），因此不能覆盖，**此时的accept请求必须携带此`v = 'y'`，类似于解决冲突conflicts resolution从而使集群达成一致**

最终X与acceptor 3的网络恢复，此时的accept请求被全部接受，所有acceptor达成共识，对于acceptor 3而言，**收到`rnd >= last_rnd`的accept请求时可以肯定该proposer此前的prepare请求已经收到majority的响应，因此可以直接采用accept携带的值**

即使网络依然中断一段时间，但是不影响所有acceptor已对`v = 'y'`达成一致，**此后该值不可能再改变，因为任意majority的响应都一定带有该值**，所以后续即使更新提案号也不可能改变达成共识的`v = 'y'`

```text
                  X: v = 'y', rnd = 3           
            X ------------------------> [ 3,x1][ 3,y2][ 2,y2]
                1,2: accepted
                  3: network recovered
                     accepted
            X <------------------------ [ 3,y3][ 3,y3][ 3,y3]
```

### 额外补充

#### Phxpaxos与Paxos的差异

[original post](https://zhuanlan.zhihu.com/p/111574502)

[Phxpaxos](https://github.com/Tencent/phxpaxos)是腾讯微信事业群开源的Multi Paxos实现，其中有一个实现细节与Classic Paxos明显不同，在Classic Paxos原文对Phase I的描述中有：

> (b) If an acceptor receives a prepare request with number n **greater than** that of  any prepare request to which it has already responded, then it responds to the request with a promise not to accept any more proposals numbered less than n and with the highest-numbered proposal (if any) that it has accepted.

即acceptor只能响应携带有提案号大于自身已经记录的最大提案号的prepare请求，而在Phxpaxos的实现中却采用了`>=`：

```cpp
int Acceptor::OnPrepare(const PaxosMsg &oPaxosMsg)
{ 
  // skip
  BallotNumber oBallot(oPaxosMsg.proposalid(), oPaxosMsg.nodeid());
  if (oBallot >= m_oAcceptorState.GetPromiseBallot())
  {
      // skip
  }
  // skip
}
```

这样的问题在于有可能出现对**同一个提案号接受了不同的值**，考虑以下场景：

1. proposer A以`n`给所有acceptors发送prepare请求，并且收到了**除了自己以外的节点回复OK**（每个节点均作为proposer、acceptor、learner，自己节点没有OK可以假定磁盘繁忙没有完成持久化因此没有回复）
2. proposer A以`n, v1`给所有accetpors发送accept请求，并且所有acceptors除了自身都确认`v1`
3. proposer A此时宕机，并且依然没有完成`n`的持久化，即**proposer A宕机后丢失了关于`n`的信息**
4. proposer A重启后，**继续选择`n`并发送prepare请求**，由于采用了`>=`因此依然得以收到所有acceptors的确认
5. proposer A以`n, v2`发送accept请求被接收，**Phase II明确要求可以接受`>=`的accept请求**（必须有`=`否则无法接受Phase I中相等提案号的proposer提出的accept请求就无法正常选择一个值）

很明显上述流程中，proposer A成功的改变了达成共识的`n, v1`，显然违背了安全性，其原因就在于**提案号出现了不唯一的情况**

PhxPaxos中通过**首先持久化prepare请求，随后再广播给其他acceptors，重启后会读取持久化状态避免重用提案号**来避免上述场景的发生：

```cpp
int Base::BroadcastMessage(const PaxosMsg &oPaxosMsg, const int iRunType, const int iSendType)
{
    // skip
    if (iRunType == BroadcastMessage_Type_RunSelf_First)
    {   // OnReceivePaxosMsg locally first
        if (m_poInstance->OnReceivePaxosMsg(oPaxosMsg) != 0)
        {
            // skip
        }
    }
    // skip
    // broadcast to others afterwards
    ret = m_poMsgTransport->BroadcastMessage(m_poConfig->GetMyGroupIdx(), sBuffer, iSendType);
}

int Acceptor::OnPrepare(const PaxosMsg &oPaxosMsg)
{
  // skip
  m_oAcceptorState.SetPromiseBallot(oBallot);
  // persist the state
  int ret = m_oAcceptorState.Persist(GetInstanceID(), GetLastChecksum());
  if (ret != 0)
  {
    // skip
  }
  // skip
}
```

在现代网络环境中，通常存储层延迟会大于网络通信延迟，采用**先持久化再广播的方式无疑会增加系统延迟**，而这样做的原因一是为了避免上述违背协议的场景，二是为了解决采用`>`的另一个问题：

**采用`>`意味着每次因丢包、超时等任何原因导致的重试都必须增加提案号**，这就导致少数机器出现网络隔离时会不断的增加提案号并重试，当**网络恢复时这些机器携带极大的提案号加入集群会冲击原有集群的leader**，导致需要重新执行Multi Paxos选举新的leader，[类似Raft中的PreVote希望解决的问题](https://github.com/JasonYuchen/notes/blob/master/raft/09.Leader_Election_Evaluation.md#%E9%98%B2%E6%AD%A2%E9%87%8D%E6%96%B0%E5%8A%A0%E5%85%A5%E9%9B%86%E7%BE%A4%E7%9A%84%E8%8A%82%E7%82%B9%E7%A0%B4%E5%9D%8F%E9%9B%86%E7%BE%A4-preventing-disruptions-when-a-server-rejoins-the-cluster)

由于采用了Multi Paxos的模型，系统稳定情况下**Phase I并不会每个Paxos实例都被执行**，因此这里引入的延迟对性能影响非常有限，而在Phase II中PhxPaxos就是在广播accept请求后再执行本地持久化任务

## Paxos的优化

### Multi-Paxos

Classic Paxos依赖Phase I和Phase II来完成一个值的写入，即**2轮RPC写入一个值**，而在集群状态稳定时（即leader/distinguished proposer不发生改变时）可以优化为近似1轮RPC写入一个值

例如leader可以通过**一轮RPC一次性为连续十个值执行Phase I**，一次性为十个Paxos实例选择`rnd`，从而随后对每个值只需要**一轮RPC执行Phase II**就可以完成写入，对这十个值的总计开销为十一轮RPC（Classic Paxos需要二十轮RPC）

这种设计实质上与Raft中的leader term概念非常接近，[Raft的详细笔记见此](https://github.com/JasonYuchen/notes/tree/master/raft)

### Fast-Paxos

Classic Paxos中依赖**简单多数majority即N/2+1**，Fast Paxos通过**增加quorum的要求来达到一轮RPC就能实现共识**，而若一轮RPC无法实现说明集群状态不一致就需要**退化到Classic Paxos**

- proposer直接开始Phase II发送accept请求，并且带有`rnd = 0`保证小于任何一个Classic Paxos的`rnd`
- acceptor只有在自身`v`为空时才会接受accept请求
- 必须有N*3/4+1个acceptor接受才被人为确认成功
- 若发生冲突则回退到Classic Paxos并重新采用`rnd > 0`开始prepare请求

**Fast Paxos必须采用更大的quorum值来保证一致性**，例如下例中，如果采用简单多数N/2+1=3来确定，则Y无法与1,2通信就意味着此时`x`和`y`都有可能已经达成了简单多数，Y无法确定：

```text
         Proposer                             Acceptors 1, 2, 3, 4, 5                             Proposer
  P                X: v = 'x', rnd = 0               
  H         X ------------------------> [  -  ][  -  ][  -  ][  -  ][  -  ]
  A              1-3: accepted
  S         X <------------------------ [ 0,x0][ 0,x0][ 0,x0][  -  ][  -  ]
  E                                                                             Y: v='y', rnd = 0
  II                                    [  ?  ][  ?  ][ 0,x0][  -  ][  -  ] <------------------------ Y
                                                                            3: rejected, 4,5: accepted
                                        [  ?  ][  ?  ][ 0,x0][ 0,y0][ 0,y0] ------------------------> Y
```

Fast Paxos要求quorum必须满足大于等于N*3/4+1，此例中需要4个节点，从而当**Y被拒绝时就会回退到Classic Paxos**，（acceptor 1&2标记位`?`仅代表Y对其状态未知，而实际状态保持`0,x0`）：

```text
  P                X: v = 'x', rnd = 0               
  H         X ------------------------> [  -  ][  -  ][  -  ][  -  ][  -  ]
  A              1-4: accepted
  S         X <------------------------ [ 0,x0][ 0,x0][ 0,x0][ 0,x0][  -  ]
  E                                                                             Y: v='y', rnd = 0
  II                                    [  ?  ][  ?  ][ 0,x0][ 0,x0][  -  ] <------------------------ Y
                                                                              3,4: rejected, v = 'x'
                                                                                5: accepted
                                        [  ?  ][  ?  ][ 0,x0][ 0,x0][ 0,y0] ------------------------> Y

```

被拒绝后Y发现自己只有1个acceptor 5，从而重新开始Classic Paxos并且必须选择上一轮Fast Paxos Phase II感知到的`v = 'x'`，完成两阶段并确认值为`v = 'x'`，**回退到Classic Paxos后quorum就是简单多数N/2+1**：

```text
                                                                                  Y: rnd = 2
                                        [  ?  ][  ?  ][ 0,x0][ 0,x0][ 0,y0] <------------------------ Y
                                                                              3,4: last_rnd = 0
                                                                                          v = x
                                                                                      v_rnd = 0
                                                                                5: last_rnd = 0
                                                                                          v = y
                                                                                      v_rnd = 0
                                        [  ?  ][  ?  ][ 2,x0][ 2,x0][ 2,y0] ------------------------> Y
                                                                                Y: v = 'x', rnd = 2
                                        [  ?  ][  ?  ][ 2,x0][ 2,x0][ 2,y0] <------------------------ Y
                                                                              3-5: accepted v = 'x'
                                        [  ?  ][  ?  ][ 2,x2][ 2,x2][ 2,x2] ------------------------> Y

```

以下是一个X和Y均没有达到quorum，从而**均回退到Classic Paxos的示例**，随后X和Y通过Classic Paxos的流程，为了简化过程，将RPC的请求和响应写在一行中，并只保留一些关键信息:

```text
Fast:
                   X: v = 'x', rnd = 0                                          Y: v='y', rnd = 0
            X ------------------------> [  -  ][  -  ][  -  ][  -  ][  -  ] <------------------------ Y
                       Failed                                                        Failed
            X <------------------------ [ 0,x0][ 0,x0][ 0,x0][ 0,y0][ 0,y0] ------------------------> Y
Classic:
                   X: rnd = 1
                 1-3: OK, 4-5: lost
    phase I X <-----------------------> [ 1,x0][ 1,x0][ 1,x0][ 0,y0][ 0,y0]
                                                                                Y: rnd = 2
                                                                              1-2: lost, 3-5: OK
                                        [ 1,x0][ 1,x0][ 2,x0][ 2,y0][ 2,y0] <-----------------------> Y phase I
                                                                                Y: v = 'y', rnd = 2
                                                                              1-5: accepted
                                        [ 2,y0][ 2,y0][ 2,y0][ 2,y0][ 2,y0] <-----------------------> Y phase II
                       Failed
   phase II X <-----------------------> [ 2,y0][ 2,y0][ 2,y0][ 2,y0][ 2,y0]
```

Fast Paxos需要扩大quorum的值，从而Fast Paxos比Classic Paxos、Multi Paxos更容易受到慢节点的影响，并且在失败时也需要回退到Classic Paxos进行重试，因此**并不见得Fast Paxos在任何场合下都更加高效**
