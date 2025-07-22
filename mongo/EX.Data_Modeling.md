# MongoDB Data Modeling

## Embedding vs. Referencing

## Collections / Indexes

### Views / On-Demand Materialized Views

### Capped Collections

### Clustered Collections/Indexes

- 聚集索引相当于和实际数据存储在一起，**除此之外其他索引都和实际数据分离存储**，因此更新数据时都需要更新两个位置
- 每个聚集集合只能有一个聚集索引，且在创建时即指定为`{_id: 1}`，聚集索引的顺序就直接确定了**聚集集合底层存储的顺序**，聚集索引性能更好，占用空间更小
- 聚集索引只需要`_id`是时间类型就可以**直接支持TTL功能**，而不需要再建立TTL索引，且性能更好
- 聚集集合不能是capped集合
- 聚集集合和非聚集集合不能互相转换，在创建时就必须确定（可以通过aggregation pipeline同现有集合生成新的另一种集合）
- 聚集索引不能被隐藏

### Timeseries Collections

- 内部将数据按照时间来存储（聚集索引），从而获得高效的基于时间的访问，同时推荐在`metaField`上构建额外的二级索引来支持多样化的数据访问
- 最好提供`metaField`从而能够跟好的组织管理数据（基于`metaField`分组，基于`timeField`排序）
- `metaField`应该几乎不会改变，且可以是任意数据类型（字符串、对象、等）

```js
db.createCollection("weather", {
  timeseries: {
    timeField: "timestamp",  // required
    metaField: "metadata",   // optional
    granularity: "hours",    // optional
  },
})

db.weather.createIndex( { "metadata.sensorId": 1, "timestamp": 1 } )
```

### Compound Indexes

- **Equility - Sort - Range, ESR规则**
- index的底层结构是B树
- array等多值字段只能在index中出现一次，所有数组的值都会被展开成一个index entry
- wildcard只能在index中出现一次，所有wildcard匹配的字段都会被展开成一个index entry
- `{"A": 1, "B": -1}`和`{"A": -1, "B": 1}`在排序上是等效的
- covered query意味着仅索引中的所有数据就足够满足查询条件，从而不必去读区真正的document，性能更优
- compound indexes的所有前缀都是implicit indexes，例如`{"A": 1, "B": -1}`包含了`{"A": 1}`

### Wildcard Indexes

- 相当于**wildcard匹配的字段均动态生成index entry**，因此即使事先不知道会有这个字段，但在搜索时也可以被index命中
- 不支持unique和TTL选项

```js
product_attributes: {
    type: "white",
    calories: 100,
    weight: "24g",
    crust: "soft",
}

db.products.createIndex({ "product_attributes.$**" : 1 })
// product_attributes.type
// product_attributes.calories
// product_attributes.weight
// product_attributes.crust

db.products.find({
  "product_attributes.crust": false,
}).explain().queryPlanner.winningPlan
```

### Partial Indexes

只对部分数据构建索引（通过一个filter表达式来指定）

```js
db.zips.createIndex(
  { state: 1 },
  { partialFilterExpression: { pop: { $gte: 10000 } } }
)
```

### Sparse Indexes

- 对应字段存在数据才会被索引，且`null`值会被包含在内
- 空间利用率更高，性能更高，但对于查询索引字段不在的文档可能出现意外行为

partial index相当于是sparse index的超集，**优先使用partial index**

### Atlas Search Indexes

- 底层基于apache lucene，通过mongodb CDC同步数据给atlas search (mongot)
- search/searchMeta查询必须是aggregation pipeline的第一级

## Schema Design Patterns

### Approximation

### Archive(*)

### Attribute

### Bucket

### Computed

### Document Versioning

### Externded Reference

### Outlier

### Preallocated

### Polymorphic, Inheritence(*)

### Schema Versioning

### Single Collection(*)

### Subset

### Tree

### Anti-Patterns

#### Unbounded Arrays

- extended reference
- subset

#### Bloated Documents

#### Massive Number of Collections

#### Unnecessary Indexes

#### Data Normalization

#### Case-Sensitivity

### Schema Validation

### Schema Migration

### Schema Lifecycle Management

## Sharding

### Hashed Sharding

### Ranged Sharding

### Zone
