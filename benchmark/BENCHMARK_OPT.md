# Benchmark Results (Stack Arena Optimization)

## Summary
The Stack Arena optimization has successfully eliminated the `malloc`/`free` overhead.
`rbcglob` is now significantly faster than `libc` `fnmatch` even for short patterns, while maintaining constant-time performance for pathological cases where legacy recursions explode.

## Detailed Results (100,000 iterations)

### 1. Simple Case: `*.c`
| Implementation | Time (ms) | Notes |
|----------------|-----------|-------|
| libc fnmatch   | 5.54 | Baseline |
| Legacy (recursive) | 1.50 | Very fast but unsafe |
| **Optimized rbcglob** | **2.85** | **~2x faster than libc** |

### 2. Pathological Case: `a*a*...*b`
| Implementation | Time (ms) | Notes |
|----------------|-----------|-------|
| libc fnmatch   | 4.44 | Safe |
| Legacy (recursive) | 3300.90 | **Exploded (3.3 seconds)** |
| **Optimized rbcglob** | **4.10** | **Safe & Efficient** |

## Conclusion
The combination of the **Compiler/vm approach** (for safety) and **Stack Arena** (for speed) provides the best balance.
It is safer than the legacy implementation and faster than the system libc.
