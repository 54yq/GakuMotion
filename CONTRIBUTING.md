# Contributing to GakuMotion

Thank you for contributing to GakuMotion! Please review the guidelines below before submitting changes.

## Branch Workflow

- **`main`**: Stable release branch. Direct commits and feature PRs should not target `main`.
- **`dev`**: Active integration and development baseline. All new features, refactors, and non-critical bug fixes should branch off `dev` and submit PRs targeting `dev`.
- Feature branches should follow standard naming conventions (e.g., `feat/<name>`, `fix/<name>`).

## Contribution Rules

1. **Clean Commits**: Keep commits atomic, well-described, and focused on a single change.
2. **No Build Artifacts or Caches**: Never commit binaries, build outputs (`build/`, `bin/`, `obj/`, `.dll`, `.exe`), temporary logs, or compiler/interpreter caches (`__pycache__/`, `*.pyc`).
3. **No Sensitive Assets**: Do not commit unauthorized game assets, proprietary shaders/models, private research datasets, tokens, credentials, or machine-specific absolute paths.
4. **Validation**: Verify that core unit tests, stage-load checks, and any relevant CLI or orchestrator tests pass cleanly before creating a PR.
