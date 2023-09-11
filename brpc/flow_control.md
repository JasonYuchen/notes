# 流量控制 Flow Control

## 概念

- **并发度 `concurrency`**：同时处理的请求数量
- **最大并发度 `max_concurrency`**：设置的最大并发度，从而超出后新的请求会被拒绝
- **最佳最大并发度 `best_max_concurrency`**：实际上最佳的最大并发度（若`max_concurrency > best_max_concurrency`并不会真的能够处理那么多请求）
- **无负载延迟 `noload_latency`**：不需要排队的情况下，仅处理请求需要的时间（受业务逻辑、内存碎片等影响）
- **最小延迟 `min_latency`**：实际测定的延迟中的最小估计值，当`concurrency <= best_max_concurrency`时`min_latency ~ noload_latency`
- **峰值每秒处理量 `peak_qps`**：实际处理量的上限（响应的qps而不是接收的qps），**取决于`best_max_concurrency / noload_latency`，因此是固有属性**，和阻塞情况无关（实际可能由于业务逻辑、内存碎片等出现变化）
- **最大每秒处理量 `max_qps`**：实际测定的处理量中的最大值，通常`max_qps < peak_qps`

## Little's Law

```math
concurrency=latency \times qps
```

当服务处于稳定状态时，满足上述关系，具体来说：

- 当服务**没有过载overload时**，随着流量`qps`上升，延迟`latency`保持稳定接近`noload_latency`，**从而`qps`和`concurrency`线性关系上升**
- 当流量**超过服务的`peak_qps`时**，此时`qps`保持稳定在`peak_qps`（服务固有属性），**从而`concurrency`和`latency`线性关系上升**

显然自适应限流的目标就是**找到`noload_latency`和`peak_qps`并设置`max_concurrency = noload_latency * peak_qps`**

## 自适应限流

通过对请求进行持续采样，根据平均`latency`和服务当前的`qps`计算出下一个采样窗口内的`max_concurrency`，参考公式为：

```math
max\_concurrency = max\_qps \times ((2+\alpha) \times min\_latency - avg\_latency)
```

- $\alpha$：可接受的延时上升幅度，例如0.3
- $avg\_latency$：当前采样窗口的平均延迟
- $min\_latency$：最近一段时间测量到的延迟较小值的指数移动平均值（[原因见此](#4-平滑处理)），是无负载延迟的估计值
- $max\_qps$：最近一段时间可见的最大流量

当服务**负载较低时**，`min_latency ~ noload_latency`，此时计算得出`concurrency < concurrency < best_max_concurrency`，从而流量**存在上涨探索空间**

当服务**负载较高时**，此时`qps ~ max_qps`，同时平均延迟开始明显超过最小延迟，`max_concurrency ~ concurrency`，此时需要**定期衰减避免远离`best_max_concurrency`确保服务不会过载**

## 估算无负载延迟

### 1. `noload_latency`随时间变化

由于`noload_latency`往往随时间变化会改变，因此自适应限流也必须能够正确探测其变化

- **当`noload_latency`下降时**，对应的平均延迟就会下降，容易探测到
- **当`noload_latency`上升时**，较难区分是`noload_latency`上升还是服务出现过载，通常可以有以下方案来估计无负载延迟
  1. 采用**最近一段时间的延迟预测**，例如`min_latency`
     问题在于如果服务持续高负载，则最近一段时间的延迟都会持续高出`noload_latency`，导致预测的无负载延迟不断上升
  2. 采样请求的**排队等待时间，采用`avg(latency - queue_time)`**
     问题在于如果下游服务出现性能瓶颈，则本服务内排队等待可能都是由于下游服务引起的，等待时间不能反映整体的负载情况
  3. 每隔一段时间**衰减`max_concurrency`，衰减后的一小个时间窗口内的请求延迟**就作为新的`noload_latency`
     经过大量实验发现这种方式较为可靠，实际上就会不断探测`best_max_concurrency`，并**波动保持（非常像控制理论里的反馈控制系统）在附近**，原理如下：

     - **若延迟极为稳定一直都满足`latency = avg_latency = min_latency`**，从而限流公式可以简化为$max\_concurrency = max\_qps \times latency \times (1 + \alpha)$，此时根据Little's Law，$qps \leqslant max\_qps \times (1+\alpha)$，**即 $max\_qps \times \alpha$ 就是预留的上涨探索空间**（若 $\alpha = 0$ 就是锁定了流量没有上涨空间，无法探索到`peak_qps`）

     - **当`qps`已经达到了`peak_qps`时**，此时由于 $\alpha$ 的存在会导致流量进一步增加此时延时就会上升，已经出现阻塞，那么测定的 $min\_latency$ 就会大于 $noload\_latency$ 并不断上升导致最终发散，此时进行**定期衰减 $max\_concurrency$ 就可以阻止发散的过程，反过来给 $min\_latency$ 提供下降探索空间**

### 2. 减少重测时的流量损失

根据方案3，每隔一段就会衰减`max_concurrency`并采样计算下一个窗口的`noload_latency`

具体过程中，**由于`max_concurrency < concurrency`会拒绝所有请求并等待队列排空**（从而可以测量`noload_latency`），等待时间可以设置为`avg_latency * 2`，此时才开始采样`min_latency`并认为其是`noload_latency`，通常良好运行的服务下等待的请求数量有限，且延迟不会过大，因此衰减并发带来的流量损失较小

### 3. 应对抖动

当延迟出现抖动时，服务当前的`concurrency`就会随之变化，限流公式中当`avg_latency`与`min_latency`接近时（抖动通常不会显著影响这两个统计值）会计算出一个高于当前并发度的`max_concurrency`从而可以应对一定程度的抖动

### 4. 平滑处理

同样是考虑到抖动影响，并且减少采样延迟带来的开销，对于更易受抖动影响的`min_latency`采用[指数移动平均Exponential Moving Average, EMA](https://en.wikipedia.org/wiki/Moving_average#Exponential_moving_average)进行平滑处理，参考代码如下：

```cpp
if (latency > min_latency) {
    min_latency = ema_alpha * latency + (1 - ema_alpha) * min_latency;
}
```

## 估算峰值流量

### 1. 提高`qps`增长速度

服务起始阶段，由于**流量很低，从而并发度非常低，因此根据流控公式近似可得 $\alpha \times max\_qps \times avg\_latency$ 就是可探索的上涨空间，显然当前 $max\_qps$ 非常低**，从而流量需要一个较长的时间才能增长到最佳情况（类似于TCP初始情况），而这会带来早期服务器能力的巨大浪费

通过下述方法进行优化，尽可能快速达到最佳状态：

- 采样时，一旦采集到的**请求数量足够即更新`max_concurrency`，而不等到一个完整的采样窗口**
- 当当前的流量已经大于记录的最大流量时**即`current_qps > max_qps`，立即更新**而不通过EMA平滑处理

### 2. 平滑处理

**最大流量`max_qps`的计算也会进行EMA处理**以避免抖动带来的影响：

```cpp
if (current_qps > max_qps) {
    max_qps = current_qps;
} else {
    max_qps = ema_alpha/10 * current_qps + (1 - ema_alpha/10) * max_qps;
}
```

注意此处选择`ema_alpha/10`时考虑到**通常`max_qps`下降并不意味着`peak_qps`下降**，因此EMA衰减系数可以取小一些（**而`min_latency`下降意味着`noload_latency`确实下降**了）

## 与netflix gradient算法的对比

netflix采用的公式为：

```math
max\_concurrency=\frac{min\_latency}{latency} \times max\_concurrency + queue\_size
```

- $min\_latency$：最近多个采样窗口的最小延迟
- $latency$：当前采样窗口的最小延迟

梯度就是指 $\frac{min\_latency}{latency}$，显然当 $latency > min\_latency$ 时，最大并发度就会逐渐减小，反之就会逐渐上升，**从而让`max_concurrency`围绕`best_max_concurrency`波动**

`TODO: 理解两种算法的对比`
