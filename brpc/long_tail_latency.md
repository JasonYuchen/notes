# 长尾延迟 Long Tail Latency

## Service Level Agreements

[SLA](https://azure.microsoft.com/en-us/blog/azure-documentdb-service-level-agreements/)

## Tail Latency Study

[尾延迟](http://accelazh.github.io/storage/Tail-Latency-Study)

## brpc benchmark

[Brpc benchmark](https://github.com/apache/incubator-brpc/blob/master/docs/cn/benchmark.md)

## 减小延迟 Mitigate the Latency

### 对于延迟中的low, middle部分

1. 增加系统资源 provisioning more resources
2. 细分且并行化处理 cut and parallelize the tasks
3. 缓存 caching

### 对于延迟中的tail部分(Long Tail Latency)

1. **发送备份请求** send more requests than necessary and only collect the fastest returned  (hedging)
    例如首先发送请求给A服务器，假定真实响应会在`5s`后收到，即此次请求的延迟为`5s`，当`1s`后还未响应时立即发送备份请求给B服务器并等待先返回的响应，假定真实响应会在`2s`后收到，即此次请求的延迟为`1+2=3s`，那么对于客户来说延迟从`5s->3s`
2. 任务分区更小 smaller task partitions (smoother latency distribution percentiles)
3. 更细致的任务调度策略 more fine-grained scheduling
