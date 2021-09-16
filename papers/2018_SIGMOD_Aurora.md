# [SIGMOD 2018] Amazon Aurora: On Avoiding Distributed Consensus for I/Os, Commits, and Membership Changes

[mit6.824: Aurora](https://github.com/JasonYuchen/notes/blob/master/mit6.824/10.Aurora.md)

## 简介 Introduction

## 高效写入 Making Writes Efficient

### Aurora系统架构 Aurora System Architecture

### 写入 Writes in Aurora

### 存储的一致性和提交 Storage Consistency Points and Commits

### 故障恢复 Crash Recovery in Aurora

## 高效读取 Making Reads Efficient

### 避免quorum读取 Avoiding quorum reads

### 读取的扩容 Scaling Reads Using Read Replicas

### 结构化一致性 Structural Consistency in Aurora Replicas

### 快照隔离 Snapshot Isolation and Read View Anchors in Aurora Replicas

## 宕机和成员 Failures and Quorum Membership

### 采用quorum集合来改变成员 Using Quorum Sets to Change Membership

### 采用quorum集合来减少成本 Using Quorum Sets to Reduce Costs
