# Broad-optimal candidate search

Date: 2026-11-21  
Branch: `flow-integration`

## Question

Is there a candidate strategy that is optimal or near-optimal on most maps and
only fails on adversarial extremes?

## Strategies compared

On the 11 synthetic traps:

```text
square Top-8 / Top-32
ring-kernel Top-8 / Top-32
hybrid: square Top-K + ring-kernel Top-K
all followed by planner 3×3 polish
```

## Synthetic result

```text
STRATEGY square8    mean=1.003 worst=1.000 below95=0
STRATEGY kernel8    mean=1.003 worst=1.000 below95=0
STRATEGY hybrid8    mean=1.003 worst=1.000 below95=0
STRATEGY square32   mean=1.003 worst=1.000 below95=0
STRATEGY kernel32   mean=1.003 worst=1.000 below95=0
STRATEGY hybrid32   mean=1.003 worst=1.000 below95=0
```

All strategies are indistinguishable on this set. The planner polish step is
doing most of the recovery work.

## Exact block oracle

Small high-value map:

```text
grid 16×16 chunks
radius 64
inner 16
centers 65,536
```

```text
EXACT-ORACLE oracle=(135,135,9486) planner=(135,135,9486)
```

Exact block-level enumeration matches the planner.

## Real-seed comparison

Point verification is weighted and independent of planner display fields.

| seed | strategy | blocks | weight | notes |
|---|---|---:|---:|---|
| -8180004378910677489 | square / hybrid | 14739 | 14595.893165 | reference |
| 0 | square / hybrid | 14387 | 14324.515968 | reference |
| 12345 | square baseline | 14742 | 14186.950942 | baseline finds it |
| 12345 | hybrid | 14742 | 14186.950942 | no extra gain |

Hybrid result for seed=12345:

```text
cx=-23958200 cz=-29432179
blocks=14742
weight=14186.950942
```

The square baseline finds the same point, so the hybrid ring kernel does not
improve quality on this seed.

## Extreme cases

`shell_vs_singles`:

```text
outer shell target square rank: absent from Top-32
ring-kernel target rank: 1538
kernel top score: 39922 (decoy edge)
shell target score: 3788
```

This is not a valid “true optimum missed” case: the decoy edge is actually
better than the shell center. The ring kernel correctly ranks the decoy.

## Conclusion

The simplest strategy that is broad-optimal on all current tests is:

```text
full-map square prescan:
  grid=15
  minDensity=0.18
  maxSeeds=4000
+ planner refine all seeds
+ 3×3 block polish on the top 8
```

Ring-weighted ranking is useful as a diagnostic and for thin/outer-shell
structures, but it has not improved quality on the 3-seed benchmark.

Extreme non-optimality is still possible on adversarial maps. The current
`outer_shell` traps do not prove a real quality loss because their true optimum
lies on a decoy edge, not at the shell center.

## Recommended next experiment

Construct a **true-optimum outer-shell map** where:

```text
shell center is the unique global optimum
all square-window-visible candidates have lower exact ring coverage
```

If such a map exists, compare square vs ring-kernel vs hybrid recall.
