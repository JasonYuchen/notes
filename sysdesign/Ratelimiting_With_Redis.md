# Introduction to rate limiting with Redis

[original post part I](https://www.dr-josiah.com/2014/11/introduction-to-rate-limiting-with.html)

[original post part II](https://www.dr-josiah.com/2014/11/introduction-to-rate-limiting-with_26.html)

## Why rate limit?

通常用于避免系统负载过大、恶意用户浪费系统资源、限制不同付费级别用户的权限等，假定现在的目标是限制用户访问某个接口的频率不能超过`240/h`，在限制时通常可以考虑用户`uid`或是请求的源`ip`地址，或组合两者，例如：

```py3
def get_identifiers():
    ret = ['ip:' + request.remote_addr]
    if g.user.is_authenticated():
        ret.append('user:' + g.user.get_id())
    return ret
```

## Just use a counter

最简单的做法就是直接采用一个计数器来限制单位时间内的请求数，**将时间分成不同的桶buckets，每个请求根据时间戳放入桶内**，当超过桶的容量时就触发了限流

```py3
def over_limit(conn, duration=3600, limit=240):
    bucket = ':%i:%i'%(duration, time.time() // duration)
    for id in get_identifiers():
        key = id + bucket
 
        count = conn.incr(key)
        conn.expire(key, duration)
        if count > limit:
            return True
 
    return False
```

按照`duration=3600`的设计，相当于每个小时就会重置请求数，则client就可以在每个小时一开始就立即耗尽`240/h`的限额，虽然依然满足了`240/h`的限制，但是在重置请求数量后短暂爆发的过程中请求速率则远超了`240/h`的限制，此时就需要考虑重置请求数量的粒度，即**桶的大小**

## Multiple bucket sizes

通过采用多个桶，例如`10/s`、`120/m`、`240/h`的限制，来使得每个窗口内都尽可能平滑，且满足原先的`240/h`的要求

```py3
def over_limit_multi(conn, limits=[(1, 10), (60, 120), (3600, 240)]):
    for duration, limit in limits:
        if over_limit(conn, duration, limit):
            return True
    return False
```

这样的做法简单有效，但是代价在于此时一个请求可能需要同时访问3个桶，且每个桶的访问都涉及到一次`incr`和`expire`命令，导致了6次的Redis访问（或考虑`uid`和`ip`分别限制则是12次），**代价过于高昂**，常见优化手段就是采用**Redis流水线pipelining**，合并`incr`和`expire`从而减少到3次（6次）Redis访问，如下：

```py3
def over_limit(conn, duration=3600, limit=240):
    # Replaces the earlier over_limit() function and reduces round trips with
    # pipelining.
    pipe = conn.pipeline(transaction=True)
    bucket = ':%i:%i'%(duration, time.time() // duration)
    for id in get_identifiers():
        key = id + bucket
 
        pipe.incr(key)
        pipe.expire(key, duration)
        if pipe.execute()[0] > limit:
            return True
 
    return False
```

## Counting correctly

将整个操作都以**Lua脚本的方式直接交由Redis来执行**可以大幅减少反复访问Redis带来的延迟：

```py3
def over_limit_multi_lua(conn, limits=[(1, 10), (60, 120), (3600, 240)]):
    if not hasattr(conn, 'over_limit_lua'):
        conn.over_limit_lua = conn.register_script(over_limit_multi_lua_)
 
    return conn.over_limit_lua(
        keys=get_identifiers(), args=[json.dumps(limits), time.time()])
 
over_limit_multi_lua_ = '''
local limits = cjson.decode(ARGV[1])
local now = tonumber(ARGV[2])
for i, limit in ipairs(limits) do
    local duration = limit[1]
 
    local bucket = ':' .. duration .. ':' .. math.floor(now / duration)
    for j, id in ipairs(KEYS) do
        local key = id .. bucket
 
        local count = redis.call('INCR', key)
        redis.call('EXPIRE', key, duration)
        if tonumber(count) > limit[2] then
            return 1
        end
    end
end
return 0
'''
```

## Problems and solutions

- **在Lua脚本中直接生成keys？**
  通常应该**确保Lua脚本中所涉及到的变量都是由运行前传递进去**，而不是在服务器端计算出来（Redis也是如此推荐的），这是因为假如涉及到Redis集群模式，不同key由不同的集群节点拥有，则**key在运行前就确定可以保证由key所在的Redis节点运行脚本**

  Redis集群对key的分区，也导致了假如同时**操作多个keys的情况下，这些操作不能保证整体的原子性**
- **没有成功（被限流）的请求也增加了记录的请求数？**
  从前述算法来看，无论某一个请求是否被限流，都会导致`incr(1)`从而推高了所记录的请求数，正确的做法是当确实被限流从而不被执行时，应**补偿过增的数据**
- **stampeding elephants问题？**
  假如只有原始的`240/h`的粒度，导致每个用户都在每小时重置请求数时爆发请求，从而依然有服务过载的风险，前述更细粒度的方案（引入`10/s`和`120/m`）能够部分解决这个问题但代价就是多次访问Redis的延迟，且一同使用的桶越多，额外Redis访问越多，真正的解法是引入**滑动窗口计数sliding windows**
- **带权重的请求？**
  只需要把递增的数值`incr(1)`改为相应请求的权重即可`incr(req.weight())`

## Sliding Windows

采用滑动窗口记录请求意味着需要记录请求的历史，例如`240/h`则需要记录一小时内请求的分布和总数，可以通过记录**窗口大小duration**、**精度precision**和**每个子桶最早请求的时间戳ts**来实现，采用一组HASH数据结构作为值，记录了整个窗口内最早的请求时间戳、按精度划分出子桶的请求数和窗口总请求数，key依然是前述生成的用户identifier：

```text
<duration>:<precision>:o    --> <timestamp of oldest entry>
<duration>:<precision>:     --> <count of successful requests in this window>
<duration>:<precision>:<ts> --> <count of successful requests in this sub-bucket>
```

具体实现如下，每个限流目标都通过`[duration, limit, precision]`来描述，其中当不提供`precision`时就退化为前述普通的限流器，该脚本**仅用于更新过期子桶的数据和判断是否需要限流**，只有真正被限流的情况才会进入**第二段脚本增加请求数**

```lua
-- section 1:
-- Argument decoding, and starting the for loop that iterates over all rate limits
-- ARGV[1]: [[duration 1, limit 1], [duration 2, limit 2, precision 2], ...]
local limits = cjson.decode(ARGV[1])
local now = tonumber(ARGV[2])
local weight = tonumber(ARGV[3] or '1')
local longest_duration = limits[1][1] or 0
local saved_keys = {}
-- handle cleanup and limit checks
for i, limit in ipairs(limits) do

    -- section 2:
    -- Prepare our local variables, prepare and save our hash keys,
    -- then start iterating over the provided user identifiers
    -- (should only passs 1 identifier for Redis cluster mode)
    local duration = limit[1]
    longest_duration = math.max(longest_duration, duration)
    local precision = limit[3] or duration
    precision = math.min(precision, duration)
    local blocks = math.ceil(duration / precision)
    local saved = {}
    table.insert(saved_keys, saved)
    saved.block_id = math.floor(now / precision)
    saved.trim_before = saved.block_id - blocks + 1
    saved.count_key = duration .. ':' .. precision .. ':'
    saved.ts_key = saved.count_key .. 'o'
    for j, key in ipairs(KEYS) do

        -- section 3:
        -- Make sure that we aren’t writing data in the past
        local old_ts = redis.call('HGET', key, saved.ts_key)
        old_ts = old_ts and tonumber(old_ts) or saved.trim_before
        if old_ts > now then
            -- don't write in the past
            return 1
        end

        -- section 4:
        -- Find those sub-buckets that need to be cleaned up
        -- discover what needs to be cleaned up
        local decr = 0
        local dele = {}
        local trim = math.min(saved.trim_before, old_ts + blocks)
        for old_block = old_ts, trim - 1 do
            local bkey = saved.count_key .. old_block
            local bcount = redis.call('HGET', key, bkey)
            if bcount then
                decr = decr + tonumber(bcount)
                table.insert(dele, bkey)
            end
        end

        -- section 5:
        -- Handle sub-bucket cleanup and window count updating
        -- handle cleanup
        local cur
        if #dele > 0 then
            redis.call('HDEL', key, unpack(dele))
            cur = redis.call('HINCRBY', key, saved.count_key, -decr)
        else
            cur = redis.call('HGET', key, saved.count_key)
        end

        -- section 6:
        -- Finally check the limit, returning 1 if the limit would have been exceeded
        -- check our limits
        if tonumber(cur or '0') + weight > limit[2] then
            return 1
        end
    end
end
```

```lua
-- section 7:
-- Start iterating over the limits and grab our saved hash key
-- there is enough resources, update the counts
for i, limit in ipairs(limits) do
    local saved = saved_keys[i]

    -- section 8:
    -- Set the oldest data timestamp,
    -- and update both the window and buckets counts for all identifiers passed
    for j, key in ipairs(KEYS) do
        -- update the current timestamp, count, and bucket count
        redis.call('HSET', key, saved.ts_key, saved.trim_before)
        redis.call('HINCRBY', key, saved.count_key, weight)
        redis.call('HINCRBY', key, saved.count_key .. saved.block_id, weight)
    end
end

-- section 9:
-- To ensure that our data is automatically cleaned up if requests stop coming in,
-- set an EXPIRE time on the keys where our hash(es) are stored
-- We calculated the longest-duration limit so we can EXPIRE
-- the whole HASH for quick and easy idle-time cleanup :)
if longest_duration > 0 then
    for _, key in ipairs(KEYS) do
        redis.call('EXPIRE', key, longest_duration)
    end
end

-- section 10:
-- Return 0, signifying that the user is not over the limit
return 0
```

这里采用的时间戳有时候会因为**不同节点之间的时钟漂移**（这在分布式系统中并不少见）而不够准确，可以考虑采用固定的一个Redis节点运行`TIME`来获得一致的时钟值，即该节点作为授时节点，这样做的代价就是额外的一次Redis请求延迟，并且在出现故障转移时时钟依然有不可靠的风险

结合上述脚本，我们就可以得到实际client使用的接口如下（[完整代码](https://gist.github.com/josiahcarlson/80584b49da41549a7d5c)）：

```py3
def over_limit_sliding_window(conn, weight=1, limits=[(1, 10), (60, 120), (3600, 240, 60)], redis_time=False):
    if not hasattr(conn, 'over_limit_sliding_window_lua'):
        conn.over_limit_sliding_window_lua = conn.register_script(over_limit_sliding_window_lua_)
 
    now = conn.time()[0] if redis_time else time.time()
    return conn.over_limit_sliding_window_lua(
        keys=get_identifiers(), args=[json.dumps(limits), now, weight])
```
