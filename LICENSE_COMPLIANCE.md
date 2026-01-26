# License Compliance Report

**Project**: rbcglob
**Date**: 2026-01-26
**Status**: Clean-room implementation

## Implementation Methodology

This library implements Ruby Dir.glob/File.fnmatch functionality through **independent algorithm design** based solely on:

1. **Public Specifications**
   - Ruby official documentation: https://docs.ruby-lang.org/
   - Observable behavior from test cases
   - POSIX glob/fnmatch standards (public domain)

2. **Standard Algorithms**
   - Depth-First Search (DFS) for recursive directory traversal
   - qsort() for directory entry sorting
   - Standard pattern matching techniques

3. **Independent Data Structures**
   - All data structures are independently designed
   - No correspondence to Ruby internal structures

## What We Did NOT Reference

❌ **Ruby (MRI) Source Code**
- We do not reference `dir.c`, `glob.c`, or any Ruby internal implementation
- All algorithm choices are made independently

## Algorithm Justification

### Directory Traversal
- **Choice**: Depth-First Search (DFS)
- **Rationale**: Standard algorithm for tree traversal, ensures depth-first ordering
- **Source**: Computer Science textbook knowledge

### Entry Sorting
- **Choice**: qsort() with strcmp()
- **Rationale**: POSIX standard, ensures lexicographic ordering
- **Source**: POSIX specification

### Pattern Normalization
- **Choice**: Single-pass folding of `**/` patterns
- **Rationale**: Prevents duplicate results, standard deduplication technique
- **Source**: Independent design

### Segment-based Parsing
- **Choice**: Streaming parser with segment classification
- **Rationale**: Memory-efficient, allows incremental processing
- **Source**: Standard compiler design techniques

### Dotfile Handling
- **Choice**: Multi-level filtering based on flags
- **Rationale**: Implements Ruby specification requirements
- **Source**: Derived from Ruby documentation behavior

## Code Provenance

All code in this project:
- Is written from scratch
- Uses standard C99 features
- Depends only on POSIX libc functions
- Contains no copied code from Ruby or GPL projects

## References Used (Legal)

1. Ruby Documentation (https://docs.ruby-lang.org/) - Public specification ✅
2. POSIX glob specification - Public standard ✅
3. musl libc glob (MIT License) - Algorithm ideas only ✅
4. BSD libc glob (BSD License) - Algorithm ideas only ✅
5. Standard algorithms textbooks - General knowledge ✅

## Legal Opinion

**Recommendation**: This implementation is legally safe for the following reasons:

1. **API Compatibility**: Implementing a compatible API is legal (see Oracle v. Google)
2. **Specification-based**: Based on public specification, not code
3. **Independent Implementation**: All algorithms and structures are independently designed
4. **No Code Copying**: No verbatim or substantial code copying from GPL sources
5. **MIT License**: Clean, permissive license with no copyleft obligations

## Risk Assessment

- **Copyright Risk**: LOW (clean-room, spec-based implementation)
- **Patent Risk**: NONE (no novel algorithms, standard techniques only)
- **Trade Secret Risk**: NONE (public specifications only)

## Certification

I certify that to the best of my knowledge:

- No Ruby (MRI) source code was referenced during implementation
- All algorithms are independently designed or from public sources
- All code is original work or from compatible licenses
- This implementation is based solely on public specifications

---

**Prepared by**: Development Team
**Date**: January 26, 2026
**Version**: 1.0
