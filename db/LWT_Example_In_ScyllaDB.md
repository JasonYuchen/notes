# Getting the Most out of Lightweight Transactions in ScyllaDB

[original post](https://www.scylladb.com/2020/07/15/getting-the-most-out-of-lightweight-transactions-in-scylla/)

[repo: lightest](https://github.com/kostja/lightest)

## What is a Lightweight Transaction?

ScyllaDB中将带有条件判断的SQL称为**轻量化事务Lightweight Transaction LWT**，只作用于一条数据，并仅在条件成立时才会执行，也可以称为*conditional statement*，可以执行原子的条件判断并执行

显然这样的“事务”与传统数据库领域的ACID相去甚远，但基于LWT，同样可以根据应用层的需求实现类似更高级的不仅限于单行数据的事务流程

## LWT Internals

`TODO`

## A Practical Example: An LWT Banking App

一个银行系统的典型场景就是转账，因此首先建立账户表如下，并且根据银行识别号和用户账号作为组合键进行分区，从而尽可能将所有账户能够均匀分区，并且单个分区不会过大：

由于LWT的实现，当转账涉及的行都在相同分区时就会落在同一个ScyllaDB节点，性能会更高，并且过大的分区也对LWT的性能不利（`TODO: why`）

```SQL
CREATE TABLE lightest.accounts (
    bic TEXT,               -- bank identifier code
    ban TEXT,               -- bank account number within the bank
    balance DECIMAL,        -- account balance
    pending_transfer UUID,  -- will be used later
    pending_amount DECIMAL, -- will be used later
    PRIMARY KEY((bic, ban)) -- composite key
)
```

向账户表插入一条数据的方式只需要使用`INSERT`即可，关键在于必须确保不会覆盖原有的数据，即**只有不存在时才创建，此时就必须引入LWT**，在语句末尾加上`IF`即自动成为了LWT

```SQL
INSERT INTO accounts
    (bic, ban, balance, pending_amount)
VALUES
    (?, ?, ?, 0)
IF NOT EXISTS
```

`TODO: INSERT本身为什么不能保证插入的原子性？`

### Dealing with Failure

前述LWT是条件语句，因此只有在条件为真时才会真的执行，并且通过特殊的列`[applied]`告知用户是否成功执行，例如插入账户失败时：

```sh
cqlsh:lightest> INSERT INTO accounts (bic, ban, balance, pending_transfer) VALUES (...) IF NOT EXISTS;

 [applied] | bic | ban | balance | pending_amount | pending_transfer
-----------+-----+-----+---------+----------------+------------------
     False | ... | ... |       0 |           null |             null
```

对于分布式数据库来说，一个请求的失败（数据库节点宕机、网络中断等）并不意味着所发起的请求失败，当分布式数据库的容灾机制依然有效时（例如ScyllaDB的majority存活），一条返回超时的请求依然有可能被提交，在已经宕机节点上发起的事务依然有可能成功提交

取决于不同数据库的策略，核心是**query failure != transaction failure**

当遇到请求失败时可以尝试重试（此时请求有**可能已经被应用**了），重试会引入的问题就是**重复应用double apply**，例如所选择的`IF`条件在应用后并不会变为假，那么重试也会成功，一种措施就是在请求之后通过`SERIAL SELECT`来查询值是否真的写入，是否需要重试

`TODO: 性能分析以及LWT的执行细节`

### Adding transfer history

一次转账所需要满足的条件：

- 无法透支账户
- 源账户和目的账户都必须被相应操作，不能只修改了其中一个账户
- 转账操作必须幂等，即一次操作成功后重试应该是no-op

采用传统支持ACID的数据库可以轻易的表达为（略过余额校验的语句）：

```SQL
BEGIN
  UPDATE accounts
    WHERE bic=$bic AND ban=$src_ban
    SET balance = $balance - $amount
  UPDATE accounts
    WHERE bic=$bic AND ban=$dst_ban
    SET balance = balance + $amount
COMMIT (*)
```

显然在ScyllaDB中无法通过简单的语句直接实现这种涉及多行数据的事务，而必须通过精巧的构造执行流程来满足转账的要求，原文提出的做法是**事件溯源event sourcing**：

- 一次转账包含多个操作，每个操作都以类似日志的形式记录
- 每个操作必须是幂等的，从而允许重试而不出现double apply
- 每个操作都通过LWT来实现

1. 首先创建转账表，用于记录每一笔（由`UUID`来唯一标识）转账的执行状态，即转账历史，从而转账的**执行状态修改就被收敛到一条LWT就可以修改的单行数据中**，实现了类似锁的语义

    ```SQL
    CREATE TABLE lightest.transfers (
        transfer_id UUID, -- transfer UUID
        src_bic TEXT,     -- source bank identification code
        src_ban TEXT,     -- source bank account number
        dst_bic TEXT,     -- destination bank identification code
        dst_ban TEXT,     -- destination bank account number
        amount DECIMAL,   -- transfer amount
        state TEXT,       -- 'new', 'locked', 'complete'
        client_id UUID,   -- the client performing the transfer
        PRIMARY KEY (transfer_id)
    )
    ```

2. 当需要发起转账时，只需要向`transfers`中新建一条记录即可

    ```SQL
    INSERT INTO transfers
        (transfer_id, src_bic, src_ban, dst_bic, dst_ban, amount, state)
    VALUES
        (?, ?, ?, ?, ?, ?, 'new')
    IF NOT EXISTS
    ```

3. 更新该转账记录，将发起转账的客户端注册到该转帐记录中
   - `amount != NULL`：确保转账不是意外创建的，是存在数额的有效转账记录
   - `client_id = NULL`：确保当前没有一个客户端正在持有该转账，确保所有权唯一
   - `USING TTL 30`：该客户端必须在30秒内持有并完成该转账记录，用于**预防客户端宕机或失联导致的转账卡死**，允许其他客户端替代执行

    ```SQL
    UPDATE transfers USING TTL 30
    SET client_id = ?
    WHERE transfer_id = ?
    IF amount != NULL AND client_id = NULL
    ```

    ***当失败重试时***：由于`client_id = NULL`确保了继任的客户端也只有一个能够成功继续执行转账

4. 准备转账，更新出账入账的账户（注意需要**以全局特定的顺序，例如账号字典序，来更新，避免出现死锁**）
   - `balance != NULL`：确保有效账号（对于扣款账户是否应该是`balance > pending_amount`确保不透支？）
   - `pending_transfer = NULL`：确保当前账号没有进行中的转账，即**每个账号的转账不存在并发**，同时也避免了重复注册同一个转账

    ```SQL
    UPDATE accounts
    SET pending_transfer = ?, pending_amount = ?
    WHERE bic = ? AND ban = ?
    IF balance != NULL AND
       pending_amount != NULL AND
       pending_transfer = NULL
    ```

    - ScyllaDB的LWT会**返回相应的字段在成功更新后的数据**，即实现类似CAS的操作

    ```sh
    cqlsh:lightest> UPDATE accounts
      SET pending_transfer = b22cfef0-9078-11ea-bda5-b306a8f6411c,
          pending_amount = -24.12
      WHERE bic = 'DCCDIN51' AND ban = '30000000000000'
      IF balance != NULL AND
         pending_amount != NULL AND
         pending_transfer = NULL;

    [applied] | balance | pending_amount | pending_transfer
    ----------+---------+----------------+------------------
        True |   42716 |              0 |             null
    ```

    - 多次执行就可以看到**重复的更新并没有应用**，并且返回了当前的最新值

    ```sh
    cqlsh:lightest> UPDATE accounts
      SET pending_transfer = b22cfef0-9078-11ea-bda5-b306a8f6411c,
          pending_amount = -24.12
      WHERE bic = 'DCCDIN51' AND ban = '30000000000000'
      IF balance != NULL AND
        pending_amount != NULL AND
        pending_transfer = NULL;

    [applied] | balance | pending_amount | pending_transfer
    ----------+---------+----------------+-------------------------------------
        False |   42716 |         -24.12 | b22cfef0-9078-11ea-bda5-b306a8f6411c
    ```

    ***当失败重试时***：由于`pending_transfer = NULL`确保了不会重复注册转账，并且返回的`pending_amount`若为0，则说明后续第6步实际上已经执行成功

5. 此时两个账户都已经注册了这次转账，更新转账记录的状态
   - 上一步结束时注册转账（相当于锁住账户）并获得了当前最新余额，可以检查余额是否充足，若充足则继续执行转账，若不充足则直接更新到`state = 'complete'`并终止转账
   - 采用`state = 'locked'`可以确保一次转账只执行一次，避免重复执行

    ```SQL
    UPDATE transfers
    SET state = 'locked'
    WHERE transfer_id = ?
    IF amount != NULL AND client_id = ?
    ```

6. 执行转账，更新出账入账的账户
   - `pending_amount = 0`：确保当宕机失联等需要重新执行转账时，不会重复扣款，相当于fence
   - `pending_transfer = ?`：确保操作对应的是正在执行的转账

    ```SQL
    UPDATE accounts
      SET pending_amount = 0, balance = ?
      WHERE bic = ? AND ban = ?
      IF balance != NULL AND pending_transfer = ?
    ```

    ***当失败重试时***：当前述第4步返回的`pending_amount`为0时，说明已经成功写入过，此时计算出的`balance`就是当前的`balance`，不会重复出账或入账金额

7. 完成转账，更新转账记录的状态
   - `state = 'complete'`：此次转账已结束，不会重复发生

    ```SQL
    UPDATE transfers
    SET state = ‘complete’
    WHERE transfer_id = ?
    IF amount != NULL AND client_id = ?
    ```

8. 解锁出账入账的账户，允许后续其他转账

    ```SQL
    UPDATE accounts
    SET pending_transfer = NULL, pending_amount = 0
    WHERE bic = ? AND ban = ?
    IF balance != NULL AND pending_transfer = ?
    ```

    ***当失败重试时***：解锁释放账户显然是幂等安全的操作

9. 从转账记录表中删除转账，从而一笔转账彻底完成

    ```SQL
    DELETE FROM transfers
    WHERE transfer_id = ?
    IF client_id = ?
    ```

### Error handling

**由于依赖LWT以及每个操作都是幂等的**，因此错误处理非常简单，只需要**从头（步骤3）开始重新执行转账的步骤**即可，已经应用的操作不会重复，尚未应用的操作就由恢复过程继续执行

对于出错的事务，当超过TTL之后，`client_id`就会失效，从而可以通过在后台运行一个**单独的恢复进程来扫描**这些转账即可：

```SQL
CREATE MATERIALIZED VIEW orphaned_transfers AS
    SELECT * FROM transfers WHERE client_id=null
    PRIMARY KEY (id);
```

`TODO: 补充更多细节，当其中一个账户的锁无法拿到等`

[An additional challenge: there is a very improbable race if a transfer takes longer than 30 seconds](https://github.com/scylladb/scylladb/issues/6149)

> ScyllaDB introduced Paxos table pruning to not rely on TTL for correctness.
