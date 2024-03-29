# Project #1 - Foreign Data Wrapper

[instruction](https://15721.courses.cs.cmu.edu/spring2023/project1.html)

![status](images/project.png)

## 踩坑记录

1. *可以采用Clion remote makefile的模式*
2. 正常安装依赖、编译完成后无法启动，报错`2023-02-23 23:01:44.206 CST [18500] FATAL:  could not map anonymous shared memory: Cannot allocate memory`
   - 因为采用云主机 4C4G 的配置，需要修改`./cmudb/env/pgtune.auto.conf`内与内存相关的配置，由于注释配置参考16G内存，将内存相关的选项均`/4`以避免postgre无法启动
3. 一些类型
   - `PG_TYPE`, `catalog/pg_type_d.h` (generated by `pg_type.dat`)
   - `PG_OPERATOR`, `catalog/pg_operator_d.h` (generated by `pg_operator.dat`)
4. 采用`nodeToString`可以观察`WHERE`，例如`WHERE identifier = 1`可以观察到（省略一些字段）：
  
   ```text
   {RESTRICTINFO :clause
     {OPEXPR :opno 96                                      <= PG_OPERATOR, '='
             ...
             :args (
               {VAR   :varno 1                             <= 'identifier'
                      :varattno 1                          <= the 1st attribute of the base relation
                      :vartype 23                          <= PG_TYPE, int4
                      ...}
               {CONST :consttype 23                        <= PG_TYPE, int4
                      :constlen 4
                      :constbyval true
                      :constisnull false
                      :constvalue 4 [ 1 0 0 0 0 0 0 0 ]})  <= literal value `1`
                      ...}
             :is_pushed_down true
             :clause_relids (b 1)
             ...
             :left_em  {EQUIVALENCEMEMBER :em_expr {VAR :varno 1 :varattno 1 :vartype 23 ...}
                                          :em_relids (b 1)}
             :right_em {EQUIVALENCEMEMBER :em_expr {CONST :consttype 23 :constvalue 4 [ 1 0 0 0 0 0 0 0 ]}
                                          :em_relids (b)}
             ...}
   ```

5. `WHERE`内各个条件是逻辑AND连接的，而若出现OR则实际上是`BOOLEXPR OR`算子连接了两个子表达式，本project中只需要考虑最简单的条件（在GradeScope提交一次就可以看到20条测试query）
6. 使用`extract_actual_clauses(scan_clauses, false)`在`GetForeignPlan()`中处理`scan_clauses`，一开始直接`NIL`会导致后面有聚合操作的查询失败，例如`SELECT COUNT(*) FROM`，因为`COUNT(*)`是更上层执行的，这里的一个额外优化点之一就是**Aggregation Pushdown**
7. 对于不需要输出的字段（即**Projection Pushdown**），必须在设置tuple时设置为空（否则会出现任意Segmentation Fault，猜测是因为`tts_isnull`为任意`true`或者`false`，则可能导致进一步去使用持有任意值`tts_values`），例如：

   ```cpp
   for (int attr = 0; attr < slot->tts_tupleDescriptor->natts; attr++) {
      slot->tts_isnull[attr] = true;
   }
   // projection pushdown, only emit required attributes
   for (int attr : target_attrs) {
      slot->tts_isnull[attr] = false;
      // set values...
   }
   ```

8. 没有拿到全部的满分，有一些查询性能还可以优化，初步可以考虑：

   - **Predicate reorder**: 基于selectivity来进行predicates求值，对于更难满足的predicate优先求值，例如`identifier = ?`，从而可以fail-fast
   - **Prefetch**:（采用了整块block读取的方式，一块block实际上占用的内存非常小）可以尝试调整缓冲的大小，读取更多块
   - **File read parallelism**: 可以采用异步/并发的方式来读取文件内容
   - **Memory pool**: 采用`MemoryContext`，在处理str数据时，inplace的`palloc`可能并不是最优的，可以考虑预先分配一整块内存用于str数据

## 基本流程

1. `GetForeignRelSize()`

   Get filename and tablename from query, construct `FdwPlanState` with parsed metadata of the file.
   Use `baserel->baserestrictinfo` to build filters (for predicate pushdown, only need to support `column OP constant`
   or `constant OP column` predicate) and collect interested attributes (for projection pushdown)

    - `FdwPlanState` with the ability to read/parse the metadata of `.db721`
    - collect interested attributes from `baserel->baserestrictinfo` and `baserel->reltarget->exprs`
    - collect predicates from `baserel->baserestrictinfo`
    - (Optional) more predicates
2. `GetForeignPaths()`

   Estimate cost based on `FdwPlanState` and generate access path.

    - (Optional) cost estimator
3. `GetForeignPlan()`

   Strip the qpquals list as required by 15.721
4. `BeginForeignScan()`

   Construct `FdwExecState` with given context
5. `IterateForeignScan`

   Iteratively return the qualified tuples, must remove the responsibility of checking these predicates from the core
   executors.
    - `FdwExecState` with the ability to read/parse the rawdata of `.db721`
    - predicate pushdown, evaluate the rows
    - projection pushdown, only emit required attributes
    - conversion from `.db721` layout to postgresql tuple
    - (Optional) make use of `MemoryContext` to accelerate `text` data handling
    - (Optional) make buffer (currently the same as a data block) tunable to load more data and reduce # of IO
6. `EndForeignScan()`

   Free `FdwExecState` related resources.

## data-farms

- schema

   | `farm_name` | `min_age_weeks` | `max_age_weeks` |
   |:------------|:----------------|:----------------|
   | str         | float           | float           |

- example

```JSON
{
  "Table": "Farm",
  "Columns": {
    "farm_name": {
      "type": "str",
      "block_stats": {
        "0": {
          "num": 6,
          "min": "Breakfast Lunch Dinner",
          "max": "Incubator",
          "min_len": 9,
          "max_len": 22
        }
      },
      "num_blocks": 1,
      "start_offset": 0
    },
    "min_age_weeks": {
      "type": "float",
      "block_stats": {
        "0": {
          "num": 6,
          "min": 0,
          "max": 52
        }
      },
      "num_blocks": 1,
      "start_offset": 192
    },
    "max_age_weeks": {
      "type": "float",
      "block_stats": {
        "0": {
          "num": 6,
          "min": 2,
          "max": 156
        }
      },
      "num_blocks": 1,
      "start_offset": 216
    }
  },
  "Max Values Per Block": 50000
}
```

## data-chickens

- schema

   | `identifier`             | `farm_name`     | `weight_model` | `sex` | `age_weeks` | `weight_g` | `notes` |
   |:-------------------------|:----------------|:---------------|:------|:------------|:-----------|:--------|
   | int                      | str             | str            | str   | float       | float      | str     |

  - `identifier`: monotonically increasing from 1
  - `farm_name`: foreign key (see data-farms)
  - `weight_model`: only few possible values, `{"GOMPERTZ", "MMF", "WEIBULL"}`, consider bitmap / dictionary compression
  - `sex`: only few possible values, `{"FEMAIL", "MALE"}`, consider bitmap / dictionary compression
  - `age_weeks`: `[0, 52*12]`
  - `weight_g`
  - `notes`: (NOT SURE) `{"", "WOODY"}`

- example

```JSON
{
  "Table": "Chicken",
  "Columns": {
    "identifier": {
      "type": "int",
      "block_stats": {
        "0": {
          "num": 50000,
          "min": 1,
          "max": 50000
        },
        ...
        "11": {
          "num": 50000,
          "min": 550001,
          "max": 600000
        }
      },
      "num_blocks": 12,
      "start_offset": 0
    },
    "farm_name": {
      "type": "str",
      "block_stats": {
        "0": {
          "num": 50000,
          "min": "Cheep Birds",
          "max": "Cheep Birds",
          "min_len": 11,
          "max_len": 11
        },
        ...
        "11": {
          "num": 50000,
          "min": "Incubator",
          "max": "Incubator",
          "min_len": 9,
          "max_len": 9
        }
      },
      "num_blocks": 12,
      "start_offset": 2400000
    },
    "weight_model": {
      "type": "str",
      "block_stats": {
        "0": {
          "num": 50000,
          "min": "GOMPERTZ",
          "max": "WEIBULL",
          "min_len": 3,
          "max_len": 8
        },
        ...
        "11": {
          "num": 50000,
          "min": "GOMPERTZ",
          "max": "WEIBULL",
          "min_len": 3,
          "max_len": 8
        }
      },
      "num_blocks": 12,
      "start_offset": 21600000
    },
    "sex": {
      "type": "str",
      "block_stats": {
        "0": {
          "num": 50000,
          "min": "FEMALE",
          "max": "MALE",
          "min_len": 4,
          "max_len": 6
        },
        ...
        "11": {
          "num": 50000,
          "min": "FEMALE",
          "max": "MALE",
          "min_len": 4,
          "max_len": 6
        }
      },
      "num_blocks": 12,
      "start_offset": 40800000
    },
    "age_weeks": {
      "type": "float",
      "block_stats": {
        "0": {
          "num": 50000,
          "min": 0,
          "max": 6
        },
        ...
        "11": {
          "num": 50000,
          "min": 0,
          "max": 2
        }
      },
      "num_blocks": 12,
      "start_offset": 60000000
    },
    "weight_g": {
      "type": "float",
      "block_stats": {
        "0": {
          "num": 50000,
          "min": 357.7573225215317,
          "max": 3131.53081842399
        },
        ...
        "11": {
          "num": 50000,
          "min": 32.47,
          "max": 746.34
        }
      },
      "num_blocks": 12,
      "start_offset": 62400000
    },
    "notes": {
      "type": "str",
      "block_stats": {
        "0": {
          "num": 50000,
          "min": "WOODY",
          "max": "WOODY",
          "min_len": 5,
          "max_len": 5
        },
        ...
        "11": {
          "num": 50000,
          "min": "",
          "max": "",
          "min_len": 0,
          "max_len": 0
        }
      },
        "num_blocks": 12,
        "start_offset": 64800000
    }
  },
    "Max Values Per Block": 50000
}
```
