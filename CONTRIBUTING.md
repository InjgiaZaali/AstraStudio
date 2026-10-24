# Contributing

Astra Studio is a focused educational language-toolchain project. All changes should include a test for observable behavior, descriptive error messages, and a clear README update when syntax changes.

## Development workflow

1. Build with CMake in Debug mode.
2. Run `ctest --test-dir build --output-on-failure`.
3. For compiler/VM changes, add success and failure cases to `tests/test_engine.cpp`.
4. For web UI changes, verify editor, Analyze, Run, save/open and error navigation in a browser.
5. Check the GitHub Actions matrix on your PR.

Avoid adding external dependencies without explaining the cross-platform build impact.
