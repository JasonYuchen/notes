# What Goes Around Comes Around

***He who does not understand history is condemned to repeat it.***

*Lessons learned in each era:*

## IMS Era (Hierarchical)

- Physical and logical **data independence** are highly desirable
- Tree structured data models are very **restrictive**
- It is a challenge to provide sophisticated **logical reorganizations** of tree
structured data
- A **record-at-a-time** user interface forces the programmer to do manual query
optimization, and this is often hard

## CODASYL Era (Network)

- Networks are more **flexible** than hierarchies but more **complex**
- Loading and recovering networks is more complex than hierarchies

## Relational Era (Relational)

- Set-a-time languages are good, regardless of the data model, since they offer **much improved physical data independence**
- Logical data independence is easier with a **simple data model** than with a complex one
- Technical debates are usually settled by **the elephants of the marketplace**, and often for reasons that have **little to do with the technology**
- **Query optimizers can beat all** but the best record-at-a-time DBMS application programmers

## The Entity-Relationship Era (Entity-Relationship)

- **Functional dependencies are too difficult** for mere mortals to understand. Another reason for KISS (Keep it simple stupid)

## R++ Era (Extended Relational)

- Unless there is a big performance or functionality advantage, **new constructs will go nowhere**

## The Semantic Data Model Era (Semantic)

> Most semantic data models were **very complex**, and were generally paper proposals

## OO Era (Object-oriented)

- Packages will not sell to users unless they are in **"major pain"**
- Persistent languages will go nowhere without the support of the programming language **community**

## The Object-Relational Era (Object-relational)

- The major benefits of OR is two-fold: **putting code in the data base** (and thereby bluring the distinction between code and data) and **user-defined access methods**
- Widespread adoption of new technology requires either **standards** and/or an **elephant** pushing hard

## Semi Structured Data

### Schema Last

大部分数据可以分为这四类：

1. **rigidly structured data**
   该类数据必须符合一个schema，包含了业务逻辑所要涉及的所有数据，通常任意一个字段的缺失、或是格式不匹配都是不可接受的
2. **rigidly structured data with some text fields**
   以结构化的数据为主，这部分数据同样必须有一个schema，额外包含一些自由文本，通常可以由关系型DBMS加上额外的文本类型字段来支持
3. **semi-structured data**
   半结构化的数据，可能包含非强制的字段，数据/字段可以自由的变化而没有一个统一的schema来约束，**schema last在这种情况下是一个好的选择**
4. **text**
   纯文本，没有特别的结构，通常时Information Retrieval系统的关注重点，数据挖掘等，"schema not at all"

### XML Data Model
