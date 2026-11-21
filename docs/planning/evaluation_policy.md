# Ring optimization evaluation policy

Date: 2026-11-21  
Branch: `flow-integration`

## 1. Time is a reference, not the goal

```text
210s = 社区参考耗时
```

It is not a pass/fail target. A lower time can simply mean:

```text
候选漏了
精修少了
扫描范围小了
GPU 截断更激进
```

So a speed win is only valid after the quality gate passes.

## 2. Quality first

A candidate kernel must first prove:

```text
reference recall on 3–5 seeds
top-K quality non-inferior to the P0 pool
known reference point still found
no silent fallback that changes semantics
```

Only then can time be optimized.

## 3. Hard experiment limits

```text
single full-map experiment: <= 20 min wall clock
no unbounded brute force
no repeated full-map scans just to tune parameters
offline pool evaluation first
```

If an experiment exceeds 20 minutes, stop it and record why.

## 4. Two-stage gate

### Gate Q: quality

```text
Q1. 3-seed reference recall@K == 1 (or explain)
Q2. best weight >= previous best for that seed
Q3. candidate generation is not a full 1M+ exhaustive scan
```

### Gate T: time

Only after Q passes:

```text
T1. <= 20 min hard cap
T2. faster than the square baseline without quality loss
T3. report generation time and exact-evaluation time separately
```

## 5. Metrics to report

```text
candidate generation time
exact evaluation count / time
reference rank
best weight / blocks
recall@1/10/100/1000
memory peak
fallback / truncation flags
```

Never report only one total time number.

## 6. Progress definition

```text
quality parity with better recall   = progress
same quality with lower work        = progress
faster but lower recall             = regression, not progress
slower but higher quality           = allowed research result, not product default
```

## 7. Current application

P1 ring-weighted kernel:

```text
quality: reference found, rank 1
time: no improvement
verdict: prototype valid, speed gate not attempted as primary
```

Next P2:

```text
first prove 3-seed quality/recall
then, and only then, optimize time
```
