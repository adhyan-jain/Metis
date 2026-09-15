# BUDGET-SYM — COMPLETE RESEARCH EXECUTION SPECIFICATION

> This is the authoritative research and implementation specification for
> taking BUDGET-SYM from its current V2 state to a complete, reproducible,
> scientifically defensible, paper-ready research project.
>
> Claude Code must read this document before performing research or
> implementation work.
>
> The repository itself is the source of truth for implementation details.
> This document is the source of truth for research objectives, methodology,
> execution order, and scientific constraints.

---

# 0. PRIMARY OBJECTIVE

BUDGET-SYM is an adaptive, memory-efficient compiler symbol table.

The central research question is:

> Can an adaptive compiler symbol table substantially reduce memory usage
> while maintaining competitive lookup latency by exploiting symbol lifetime,
> workload characteristics, locality, and compact representations?

The project MUST remain centered on:

- compiler symbol tables
- symbol lifetime and scope
- memory efficiency
- adaptive representation selection
- lookup performance
- workload-aware behavior

Do NOT turn the project into:

- a generic database
- a generic string dictionary
- a generic compression library
- a generic learned index
- a generic cache benchmark

Techniques from those areas may be used when they directly improve the
compiler symbol-table problem.

---

# 1. EXECUTION PHILOSOPHY

The objective is NOT to manufacture impressive benchmark numbers.

The objective is to determine whether the research hypothesis is actually true.

Therefore:

1. Do not cherry-pick datasets.
2. Do not remove unfavorable workloads.
3. Do not tune parameters on final test workloads.
4. Do not use future information unavailable to a real compiler runtime.
5. Do not claim latency improvements without measurements.
6. Do not call raw token extraction compiler-semantic extraction.
7. Do not compare baselines using different semantic event streams.
8. Do not use statistically invalid significance tests.
9. Do not hide negative results.
10. Distinguish actual heap measurements from modeled memory.
11. Every major architectural claim requires an experiment.
12. Every important optimization requires an ablation or justification.
13. If ML does not improve over a simpler heuristic, report that honestly.
14. Preserve V1 as a reproducible baseline.
15. Prefer a simpler defensible implementation over unnecessary complexity.
16. Never modify experiments merely to obtain a desired result.
17. Never alter a workload because the system performs poorly on it.
18. Never present preliminary results as final results.

A negative result is acceptable.

An unsupported positive result is not.

---

# 2. CURRENT PROJECT STATE

Current known HEAD:

`18ff32d`

Verify the actual repository state before doing anything.

---

## 2.1 Existing V1

V1 contains:

- INLINE representation
- INTERNED representation
- COMPRESSED representation
- scope-aware symbol management
- adaptive policy
- front-coded compression
- lookup cache
- memory accounting
- offline ML threshold prediction
- synthetic workload generation
- benchmark infrastructure
- ablation infrastructure
- multiseed evaluation
- corpus evaluation
- algorithm comparisons

Important implementation files include:

- `include/common.hpp`
- `include/memory_tracker.hpp`
- `include/conventional_symbol_table.hpp`
- `include/interned_symbol_table.hpp`
- `include/budget_sym.hpp`
- `include/lookup_cache.hpp`
- `include/workload_profiler.hpp`
- `include/predicted_thresholds.hpp`
- `include/hash_functions.hpp`
- `include/robinhood_symbol_table.hpp`
- `include/trie_symbol_table.hpp`
- `include/dataset_generators.hpp`
- `include/bench_metrics.hpp`
- `src/benchmark_main.cpp`
- `src/ablation_main.cpp`
- `src/grid_search_main.cpp`
- `src/multiseed_main.cpp`
- `src/corpus_bench_main.cpp`
- `src/cache_benchmark_main.cpp`
- `src/algorithm_benchmark_main.cpp`
- `src/analyze_main.cpp`
- `src/demo_main.cpp`
- `scripts/train_threshold_predictor.py`
- `scripts/extract_identifiers.py`
- `scripts/multiseed_stats.py`
- `scripts/plot_results.py`
- `tests/smoke_test.cpp`

Do not assume these files are unchanged. Inspect them.

---

# 3. COMPLETED AUDIT FINDINGS

The previous V1 audit found the following.

---

## A1 — V1 storage overhead

V1's append-oriented Entry representation created large physical memory
overhead.

Example:

Zephyr:
- approximately 3.7M inserts
- approximately 268K live names
- approximately 297 MB measured
- approximately 73% of total heap

This established that V1's storage architecture itself was a major issue.

---

## A2 — Symbol semantics mismatch

Previous corpus and benchmark comparisons did not always count equivalent
semantic objects.

Conventional could count unique live symbol entries while BudgetSym could
count declaration occurrences.

This invalidates those comparisons.

All final experiments must define symbol semantics explicitly.

---

## A3 — Corpus extraction was invalid for final compiler claims

The previous corpus extractor primarily performed raw token/identifier
extraction.

It could treat ordinary prose-like tokens as identifiers.

Example:

`the`

appeared thousands of times in a corpus.

This is NOT equivalent to compiler symbol usage.

Therefore old corpus results are preliminary and MUST NOT be presented as
valid compiler-symbol results.

---

## A4 — Timing methodology

Previous timing lacked sufficiently rigorous:

- warmup
- repetition
- variance measurement
- distribution reporting

Single-run microbenchmark numbers are not sufficient evidence.

---

## A5 — Shadowing/cache issue

A stale cache problem around shadowing was identified and fixed.

The fix must remain covered by regression tests.

---

## A6 — Old grid search optimized memory only

The previous grid search optimized modeled memory.

It did not optimize:

- lookup latency
- tail latency
- memory/latency tradeoff

Therefore its selected configuration is not evidence of a latency-aware
adaptive policy.

---

## A7 — Old ML learned the wrong objective

The previous ML pipeline attempted to predict memory-optimal thresholds.

It did NOT learn the actual research objective:

memory efficiency subject to acceptable lookup latency.

The ML pipeline must therefore be redesigned only after the latency-aware
oracle exists.

---

## A8 — Old statistical treatment

Some p-values were calculated over deterministic modeled-memory measurements.

Those must not be treated as ordinary independent noisy observations.

The statistical methodology must be corrected.

---

# 4. V2 CURRENT STATE

V2 exists in:

`include/symtab_v2.hpp`

V2 currently contains:

1. Free-list slot allocation.
2. Per-scope open-addressing index.
3. Backward-shift deletion.
4. Block-based front-coded compression.
5. Direct `(blockIndex, slotInBlock)` addressing.
6. Whole-block reclamation.
7. Length filtering.
8. 32-bit fingerprint filtering.

---

## 4.1 Current V2 actual-heap measurements

Current preliminary results:

| Dataset | V2 / Conventional | V2 / Interned |
|---|---:|---:|
| small | 1.31x | 0.72x |
| medium | 1.23x | 0.71x |
| large | 1.50x | 0.77x |
| high-prefix | 1.00x | 0.56x |
| random | 1.24x | 0.71x |
| nested | 1.50x | 0.86x |
| hot-cold | 1.46x | 0.78x |
| memory-stress | 1.49x | 0.79x |
| FreeRTOS | 7.21x | 3.48x |
| Arduino | 2.84x | 1.42x |
| Zephyr | 2.94x | 1.43x |

These are encouraging but are NOT sufficient to establish the final
research claim.

In particular:

- V2 latency has not yet been rigorously measured.
- corpus extraction remains problematic.
- no latency-constrained Pareto frontier exists yet.
- ablations are incomplete.
- the ML objective is not yet aligned with the final research objective.

Treat these as preliminary evidence.

---

# 5. REQUIRED FINAL ARCHITECTURE

The final system should investigate the following architecture.

Do NOT implement a component merely because it is listed here.

First determine whether it is justified by the measured bottleneck and
research question.

---

## 5.1 Scope/lifetime-aware storage

Symbols have compiler-defined lifetimes.

The implementation should exploit scope boundaries for reclamation.

Investigate:

- per-scope storage
- free-list reuse
- segment/arena-like allocation
- compact metadata
- immediate reclamation where possible
- whole-block reclamation

Requirements:

- no unbounded dead-entry accumulation
- no stale index entries
- no invalid references
- no incorrect symbol visibility after scope exit

---

## 5.2 Block-based front coding

Compressed strings should use bounded, directly addressable blocks.

Requirements:

- explicit block boundaries
- direct slot addressing
- bounded reconstruction
- configurable block size
- configurable anchor interval
- whole-block reclamation

Avoid:

- unbounded linked reconstruction chains
- pointer-heavy structures where avoidable
- hidden dynamic allocations

---

## 5.3 Fingerprint-assisted lookup

The lookup path should conceptually be:

```text
hash
  ↓
candidate
  ↓
length check
  ↓
fingerprint check
  ↓
exact equality / reconstruction

Fingerprint rejection is only an optimization.

It MUST NOT replace exact equality.

Hash collisions and fingerprint collisions must remain correct.

5.4 Hot/cold tiering

Investigate:

HOT
 ├── INLINE
 └── INTERNED

COLD
 └── COMPRESSED

Hot/cold classification must use only information available at runtime.

Potential signals:

access frequency
recency
scope behavior
observed workload characteristics

No future information.

No benchmark labels.

No test-set knowledge.

6. EXECUTION ORDER

The following order is mandatory.

P0 V2 core
       ↓
ECC review
       ↓
ECC fixes
       ↓
Benchmark infrastructure
       ↓
Corpus event extraction
       ↓
Dataset characterization
       ↓
Pareto optimization
       ↓
Parameter study
       ↓
Ablation
       ↓
Statistical validation
       ↓
ML redesign
       ↓
Related work
       ↓
Adversarial review
       ↓
Reproducibility
       ↓
Final results
       ↓
Final claims audit

Do not skip directly to ML.

Do not declare success before Pareto evaluation.

7. P0 — COMPLETE V2 CORE
P0.1 Full architecture audit

Before modifying architecture:

Trace the complete lifecycle:

DECLARE
 ↓
policy decision
 ↓
representation allocation
 ↓
index insertion
 ↓
LOOKUP
 ↓
access tracking
 ↓
promotion
 ↓
scope exit
 ↓
reclamation

Inspect:

ownership
allocation
deallocation
metadata
index layout
compression layout
cache
representation transitions
memory accounting
pointer/reference validity

Create:

docs/v2_architecture.md

P0.2 Fingerprint implementation

Complete and validate fingerprint-assisted lookup.

Test:

ordinary lookup
absent symbols
hash collision
fingerprint collision
shadowing
redeclaration
nested scope
scope exit
promotion

Exact equality must remain the correctness authority.

P0.3 Block compression

Audit and improve block front coding.

Measure:

block size
anchor interval
reconstruction operations
reconstruction depth
memory
lookup latency

Requirements:

bounded reconstruction
direct addressing
no unbounded chain
safe reclamation
P0.4 Hot/cold tiering

Implement only if justified.

Define explicitly:

hot threshold
cold threshold if needed
promotion behavior
demotion behavior if any
scope interaction
memory transition
lookup transition

Create:

docs/hot_cold_design.md

8. ECC REVIEW

After P0 implementation, perform an adversarial code review.

Do NOT modify code during the review itself.

Review:

Memory
ownership
free list
pool reclamation
compressed blocks
index capacity
fragmentation
allocator overhead
metadata
Correctness
same-scope redeclaration
nested scopes
shadowing
scope exit
collisions
fingerprints
cache invalidation
promotion
slot reuse
Performance
pointer chasing
allocation count
string allocation
hashing
reconstruction
index probes
locality
Research validity
hidden assumptions
future information
inconsistent accounting
benchmark artifacts

Create:

results/ecc_review.md

Then fix all CRITICAL and HIGH findings.

Run:

unit tests
differential tests
ASan
UBSan
stress tests
9. RESEARCH-GRADE BENCHMARK SYSTEM

Build one unified benchmark methodology.

Compare:

Conventional
Interned
BudgetSym V1
BudgetSym V2

All receive the EXACT SAME semantic event trace.

9.1 Memory metrics

Measure:

peak actual heap
live/final heap where meaningful
bytes/live symbol
bytes/unique symbol
index memory
metadata memory
string/pool memory
compressed memory
cache memory

Clearly distinguish:

ACTUAL MEASURED MEMORY

from:

MODELED / COMPONENT ACCOUNTING

Never mix the two.

9.2 Latency metrics

Measure:

mean
median
p50
p95
p99

Separately measure:

lookup
insertion
scope exit

Also distinguish:

cold lookup
repeated/hot lookup
9.3 Internal counters

Record:

cache hits
cache misses
reconstruction count
fingerprint rejections
exact string comparisons
index probes
promotions
demotions if implemented
representation distribution
9.4 Timing methodology

Use:

warmup
multiple repetitions
fixed documented repetitions
variance
confidence intervals where appropriate
dead-code-elimination protection
identical event traces
documented CPU
documented compiler
documented optimization flags

Do NOT rely on one timing run.

Create:

results/v2_latency_memory.csv

results/benchmark_methodology.md

10. REAL-WORLD CORPUS PIPELINE

The corpus pipeline must generate compiler-like events.

Desired event model:

DECLARE(symbol, scope)
USE(symbol, scope)
ENTER_SCOPE
EXIT_SCOPE

Prefer an actual C/C++ parser if practical.

If a parser is impractical, implement the strongest defensible approximation
and document limitations explicitly.

10.1 Must NOT treat these as symbols

Exclude:

comments
string literals
character literals
keywords
arbitrary prose
preprocessor noise where inappropriate

Repeated natural-language words must not become declarations.

10.2 Scope

Preserve lexical scope as accurately as practical.

Validate with hand-written C/C++ fixtures.

Example fixtures should include:

global declarations
local declarations
nested blocks
function parameters
shadowing
redeclaration
repeated use
scope exit

Expected event traces must be explicit.

10.3 Corpus characterization

For every corpus report:

files
declarations
uses
unique symbols
redeclarations
shadowing
maximum scope depth
average scope depth
identifier length
prefix similarity
repeat rate
entropy
access skew
churn

Evaluate:

FreeRTOS
Arduino
Zephyr
existing corpora
additional representative embedded/compiler-oriented projects where
practical

Do not remove poor-performing projects.

Create:

results/corpus_characterization.csv

results/corpus_benchmark.csv

results/corpus_methodology.md

11. DATASET TAXONOMY

The final evaluation must contain three categories.

A — Representative

Real embedded/compiler-oriented projects.

B — Redundancy-rich

Workloads containing:

long identifiers
prefix-heavy identifiers
repeated symbols
nested scopes
realistic access locality
C — Adversarial

Workloads containing:

random identifiers
low prefix similarity
high churn
frequent scope creation/destruction
unfavorable access distributions

The purpose is to determine:

When does the adaptive memory-efficient design help?

NOT:

Which workload makes the system look best?

12. LATENCY-CONSTRAINED PARETO OPTIMIZATION

This is the central experiment.

Objective:

MINIMIZE MEMORY

subject to:

LOOKUP_LATENCY <= L × CONVENTIONAL_LOOKUP_LATENCY

Use predetermined:

L = 1.10
L = 1.25
L = 1.50
L = 2.00

These thresholds MUST be selected before final evaluation and MUST NOT be
changed based on results.

12.1 For every configuration measure
memory
p50
p95
p99
insertion
compression ratio
representation distribution
promotions
cache hit rate
reconstruction work
12.2 Generate

results/pareto_results.csv

results/pareto_frontier.csv

Figures:

memory vs lookup latency
memory savings vs latency overhead
frontier by workload
aggregate frontier
workload characteristics vs benefit
12.3 Required questions

Determine:

Memory saving at 1.10x latency.
Memory saving at 1.25x.
Memory saving at 1.50x.
Memory saving at 2.00x.
Which workloads have no useful frontier.
Which workload properties correlate with improvement.

Do not hide failure cases.

13. PARAMETER STUDY

Evaluate reasonable combinations of:

block size
anchor interval
fingerprint configuration
hot threshold
policy parameters

Measure:

memory
p50
p95
p99
reconstruction
cache behavior
insertion
scope exit

Use development/training workloads for tuning.

Use held-out workloads for final evaluation.

Create:

results/parameter_sweep.csv

14. ABLATION STUDY

Compare:

Conventional
Interned
V1
Full V2
V2 without block compression
V2 without fingerprints
V2 without hot/cold
V2 without access-aware promotion
V2 without adaptive representation
V2 without scope-aware reclamation

Only include technically meaningful ablations.

Measure:

peak memory
bytes/live symbol
p50
p95
p99
insertion
scope exit
cache hit rate
reconstruction
promotions

Generate:

results/ablation.csv

The purpose is to answer:

Which mechanisms actually create the improvement?

15. STATISTICAL VALIDATION

Audit all existing statistical scripts.

Do NOT treat deterministic modeled bytes as independent noisy observations.

For repeated timing experiments report:

mean
median
standard deviation
95% CI
effect size where meaningful

Use paired comparisons where appropriate.

For multiseed workloads:

use independent seeds
aggregate correctly
avoid pseudoreplication

Create:

results/statistical_summary.csv

results/statistical_methodology.md

16. ML REDESIGN

ML is intentionally LAST.

Do not redesign ML until the Pareto system exists.

16.1 Create an oracle

For each workload determine:

The best configuration satisfying the predetermined latency constraint.

For:

1.10x
1.25x
1.50x
16.2 Candidate models

Compare:

static baseline
hand-designed heuristic
Ridge/logistic regression
small decision tree
ExtraTrees or other lightweight nonlinear model if justified

Potential features:

mean identifier length
prefix similarity
repeat rate
scope depth
scope variance
entropy
access skew
unique/live ratio
redeclaration/churn
workload size
16.3 Leakage prevention

Do NOT randomly split configurations from the same workload when that
creates workload leakage.

Prefer:

workload-level train/test
leave-one-workload-out validation where practical
16.4 Evaluate system outcomes

Do NOT evaluate ML only by R².

Measure:

memory
latency
p95
constraint violation rate
regret vs oracle
model size
runtime inference cost

If the heuristic beats ML:

Keep the heuristic.

Do not force ML into the paper.

Create:

results/ml_comparison.csv

results/ml_validation.md

17. RELATED WORK

Position the final system against relevant literature involving:

compiler symbol tables
interned string tables
hash-based string tables
tries
front-coded dictionaries
cache-conscious string structures
fingerprints in compressed strings
succinct/cache-conscious tries
minimal perfect hashing
adaptive/learned indexes

Do not claim established techniques as novel.

The defensible novelty should be investigated around the combination of:

compiler symbol semantics
scope/lifetime-aware reclamation
adaptive representation
hot/cold behavior
block-based compression
latency-constrained policy selection

If this combination is not sufficiently novel, say so.

Create:

docs/related_work_positioning.md

18. ADVERSARIAL PAPER REVIEW

Act as a hostile systems/compiler reviewer.

Attempt to reject the work.

Look for:

cherry-picked datasets
unfair baselines
semantic mismatch
future information
benchmark artifacts
allocator artifacts
cache artifacts
warm-cache bias
timing noise
insufficient repetitions
ML leakage
overfitting
compression accounting errors
index overhead
scope mistakes
V1 bug fixes presented as research contribution
generic data-structure improvements presented as novelty
invalid statistical claims
unsupported quantitative claims
missing failure cases
weak compiler relevance

Create:

results/adversarial_review.md

Classify:

CRITICAL
HIGH
MEDIUM
LOW

Fix all CRITICAL and HIGH issues that can reasonably be fixed.

Rerun affected experiments.

19. REPRODUCIBILITY

A researcher should be able to reproduce the major results from a clean
checkout.

Document:

compiler version
C++ standard
optimization flags
Python dependencies
datasets
preprocessing
random seeds
repetitions
hardware
operating environment
benchmark commands
plotting commands
ML commands

Create one entry point where practical:

run_research_experiments.sh

Expected pipeline:

build
 ↓
tests
 ↓
corpus extraction
 ↓
benchmark
 ↓
Pareto
 ↓
parameter sweep
 ↓
ablation
 ↓
statistics
 ↓
ML
 ↓
plots

Do not silently depend on proprietary services or credentials.

20. FINAL RESULTS PACKAGE

Create:

results/FINAL_RESULTS.md

Required sections:

Research question
Hypothesis
Architecture
V1 vs V2
Experimental methodology
Datasets
Baselines
Memory results
Latency results
Pareto frontier
Parameter study
Ablation
Corpus results
ML results
Failure cases
Statistical validation
Limitations
Supported claims
Unsupported claims
Conclusion

For every quantitative claim provide the source CSV/table/figure.

Clearly distinguish:

actual heap
modeled memory
timing
synthetic workload
real-world workload
21. FINAL CLAIMS AUDIT

Compare:

PRD
README
implementation
benchmark methodology
CSVs
plots
final results

Find contradictions.

Examples:

README claims faster but benchmark shows slower.
PRD claims latency-aware policy but implementation is memory-only.
"real-world evaluation" uses invalid token extraction.
modeled memory is described as actual heap.
ML is claimed superior while heuristic performs better.
V2 claims complete reclamation while dead storage remains.

Create:

results/FINAL_CLAIMS_AUDIT.md

Every quantitative claim in README/docs/paper material must be supported.

22. REQUIRED FINAL FIGURES

Generate publication-quality figures for, where supported by results:

Figure 1

System architecture.

Figure 2

Memory vs lookup latency Pareto frontier.

Figure 3

Memory savings across workloads.

Figure 4

Latency overhead across workloads.

Figure 5

Representation distribution.

Figure 6

Ablation results.

Figure 7

Workload characteristics vs memory benefit.

Figure 8

ML/heuristic vs oracle if ML survives evaluation.

Do not generate a figure merely because it looks good.

Every figure must answer a research question.

23. REQUIRED FINAL TABLES

Generate paper-ready tables for:

Table 1

System/baseline characteristics.

Table 2

Memory results.

Table 3

Latency results.

Table 4

Memory savings at fixed latency constraints.

Table 5

Ablation.

Table 6

Real-world corpus characteristics.

Table 7

ML vs heuristic vs oracle, only if applicable.

Table 8

Failure cases and limitations.

24. GIT DISCIPLINE

Before modifications:

git status
git log --oneline -20

Do not rewrite history.

Do not squash existing commits.

Do not add unrelated changes.

Prefer logically separated commits.

Suggested structure:

1. research audit / benchmark infrastructure
2. V2 fingerprint completion
3. V2 hot-cold tiering
4. corpus event extraction
5. latency-memory benchmark
6. Pareto optimization
7. parameter study
8. ablations
9. statistics
10. ML redesign
11. research documentation
12. reproducibility/finalization

Commit after each logically complete phase.

25. TESTING REQUIREMENTS

After every architectural change run relevant:

unit tests
smoke tests
differential tests
redeclaration tests
shadowing tests
nested-scope tests
scope-reclamation tests
collision tests
fingerprint tests
stress tests
ASan
UBSan

Before final completion:

Run the complete test suite.

Run the complete research benchmark suite.

Verify generated CSVs.

Verify plots.

Verify reproducibility.

26. ACCEPTANCE CRITERIA

The project is NOT considered complete merely because code compiles.

The following questions must be answerable:

Memory
Does V2 reduce actual memory?
Compared with which baseline?
By how much?
Is the reduction consistent across workloads?
Latency
What is lookup p50?
What is lookup p95?
What is lookup p99?
What is the latency overhead relative to Conventional?
Adaptation
Does adaptive selection outperform a static strategy?
Under what workload characteristics?
Pareto
Is there a useful memory/latency Pareto frontier?
How much memory can be saved at 1.10x?
At 1.25x?
At 1.50x?
At 2.00x?
Real world
Does the result hold on compiler-like real-world workloads?
What happens on unfavorable workloads?
Components
Which architectural components produce the benefit?
ML
Does ML outperform a strong heuristic?
If not, is the heuristic sufficient?
Scientific validity
Are all comparisons fair?
Are statistics appropriate?
Are claims supported by evidence?
Are limitations explicitly documented?
27. DEFINITION OF SUCCESS

Success does NOT mean:

"Every benchmark looks good."

Success means:

"We have enough rigorous evidence to state exactly where adaptive
memory-efficient symbol tables work, how much memory they save, what
latency they cost, why they work, and where they fail."

A result such as:

35% less memory at 1.18x lookup latency

is valuable if rigorously measured.

A result such as:

70% less memory at 4x latency

must be reported honestly.

A result showing no useful Pareto frontier must also be reported honestly.

Do not optimize the implementation to avoid negative results.

28. EXECUTION PROTOCOL

Whenever asked to continue:

Read this document.
Inspect the actual repository state.
Inspect git status.
Determine the first incomplete phase.
Read all relevant source files.
Implement ONLY that phase.
Run its required tests.
Run its validation experiments.
Record results.
Update progress.
Commit the completed work.
Report:
what changed
files changed
tests
experiments
results
remaining issues
next phase

Never silently skip phases.

Never assume a phase is complete merely because code exists.

A phase is complete only when its implementation AND validation are complete.

29. PROGRESS TRACKER

Update this section after every completed phase.

[x] Repository/current-state audit
[x] P0.1 V2 architecture audit
[x] P0.2 fingerprint lookup -- implementation (nameEquals/fp8 in
    symtab_v2.hpp) plus the full required test scenario list
    (ordinary/absent/collision/fingerprint-collision/shadowing/redeclaration/
    nested-scope/scope-exit/promotion), all passing clean under
    -fsanitize=address,undefined. Corpus-scale stress testing happens as
    part of the later "Benchmark infrastructure"/"Complete research
    benchmark suite" phases (CLAUDE_RESEARCH.md section 25/26), not gated
    here.
[x] P0.3 block compression validation -- implementation, correctness tests,
    AND the required parameter measurement: results/block_compression_sweep.csv
    (48 rows: blockSize x {4,8,16,32,64,128}, anchorInterval x {2,4,8,16,32},
    over high-prefix-similarity and random-long datasets), measuring modeled
    memory, reconstruction count/steps/mean-depth, and cold/hot lookup
    latency (p50/p95/p99/mean). See docs/v2_architecture.md section 10 item 3
    for the findings summary (memory/latency tradeoff confirmed, and shown
    to be prefix-similarity-dependent -- both directions reported honestly).
[x] P0.4 hot/cold tiering -- promotion (pre-existing) AND a new, explicit,
    opt-in demotion policy (PolicyConfigV2::coldIdleEpochs,
    SymTabV2::runMaintenance()/demote()), documented in
    docs/hot_cold_design.md (hot/cold thresholds, promotion/demotion
    behavior, scope interaction, memory/lookup transition costs), with 4
    correctness tests, all passing clean under -fsanitize=address,undefined.
 ECC review
 ECC fixes
 Benchmark infrastructure
 Corpus event extraction
 Corpus validation
 Dataset characterization
 Pareto optimization
 Parameter study
 Ablation
 Statistical validation
 ML oracle
 ML comparison
 Related work
 Adversarial review
 Adversarial fixes
 Reproducibility
 Final figures
 Final tables
 FINAL_RESULTS.md
 FINAL_CLAIMS_AUDIT.md
 Complete test suite
 Complete research benchmark suite
30. FINAL INSTRUCTION TO CLAUDE

This document is the persistent specification.

Do not merely implement features.

The objective is to produce a complete research artifact with:

correct implementation
rigorous experimental methodology
fair baselines
meaningful real-world evaluation
latency measurements
memory measurements
Pareto analysis
ablations
statistical validation
honest ML evaluation
related-work positioning
reproducibility
defensible final claims

If a planned technique does not improve the actual research objective,
remove it or report the negative result.

If an experiment invalidates an earlier assumption, update the project
rather than hiding the contradiction.

If evidence is insufficient for a claim, mark the claim unsupported.

Scientific validity takes priority over attractive results.