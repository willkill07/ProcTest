# proc_test

`proc_test` is a small, header-only unit testing framework designed to unit
test problematic code which may exhibit failures during runtime, namely:
- segmentation faults
- dereferencing null
- infinite loops

Its primary use may be creating unit-test autograders for a course in data structures.

This is a nonfunctional prototype currently.