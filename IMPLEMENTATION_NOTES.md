# Implementation Notes

## Clean-Room Implementation Policy

This project implements Ruby Dir.glob/File.fnmatch functionality through **clean-room implementation** to ensure license compliance.

### What We Reference

✅ **ALLOWED - Public Specifications:**
- Ruby official documentation: https://docs.ruby-lang.org/
- POSIX glob/fnmatch standards (public domain)
- Test case behavior (observable specification)
- Algorithm ideas from permissively licensed implementations:
  - musl libc (MIT License)
  - BSD libc (BSD License)
  - Git wildmatch (GPL - ideas only, not code)

### What We DO NOT Reference

❌ **PROHIBITED - Implementation Code:**
- Ruby (MRI) source code (GPL/Ruby License)
- Any copyleft implementation details
- Internal data structures from Ruby
- Line-by-line logic from GPL code

### Development Guidelines

1. **Specification-First Approach**
   - Read Ruby documentation for behavior requirements
   - Design our own algorithm independently
   - Verify against test cases

2. **Independent Algorithm Design**
   - Use standard algorithms (DFS, sorting, pattern matching)
   - Implement our own data structures
   - Document algorithm choices

3. **Comment Style**
   - Reference "Ruby Dir.glob specification" or "Ruby behavior"
   - NEVER reference "MRI", "dir.c", "Ruby internal", or line numbers
   - Describe WHAT, not HOW Ruby implements it

4. **Testing Approach**
   - Use Ruby as a black-box oracle
   - Generate test cases with Ruby scripts
   - Compare outputs, not implementations

### Algorithm Choices (Documented for Transparency)

Our implementation uses:

1. **Pattern Parsing**: Single-pass streaming parser
2. **Directory Traversal**: Depth-first search (DFS) for `**` patterns
3. **Sorting**: qsort() at directory level for consistent results
4. **Path Building**: Incremental buffer construction
5. **Brace Expansion**: Recursive descent with callback

These are standard algorithms, not derived from any specific implementation.

### If You Need to Add Features

1. Read Ruby documentation for the feature specification
2. Design your own algorithm (sketch on paper if needed)
3. Implement without looking at Ruby source
4. Test against Ruby's observable behavior
5. Document your algorithm choice

### Legal Safety Checklist

Before committing code, verify:

- [ ] No MRI source code was referenced
- [ ] Comments don't mention "MRI", "dir.c", or Ruby internals
- [ ] Algorithm is independently designed
- [ ] Only public specifications were used
- [ ] Test cases are independently generated (or from public sources)

## References (Legally Safe)

- Ruby Documentation: https://docs.ruby-lang.org/
- POSIX glob: https://pubs.opengroup.org/onlinepubs/9699919799/functions/glob.html
- musl glob (MIT): https://git.musl-libc.org/cgit/musl/tree/src/regex/glob.c
- BSD glob (BSD): https://github.com/freebsd/freebsd-src/blob/main/lib/libc/gen/glob.c

---

Last Updated: 2026-01-26
