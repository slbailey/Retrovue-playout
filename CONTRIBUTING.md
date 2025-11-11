_Metadata: Status=Canonical • Scope=Contribution workflow • Audience=External and internal contributors_

# Contributing to RetroVue Playout Engine

## Purpose

Describe how to propose changes to `retrovue-air` while staying aligned with RetroVue standards.

## Getting started

1. Review the shared standards in `_standards/`:
   - `_standards/documentation-standards.md`
   - `_standards/repository-conventions.md`
   - `_standards/test-methodology.md`
2. Follow the documentation-first workflow: update relevant docs and contracts before changing code.
3. Use the templates in `_standards/` when creating new docs (README, milestones, changelog, etc.).

## Pull request checklist

- [ ] Documentation updated (`docs/`, README, or milestones) to reflect new behavior.
- [ ] Contract tests added or adjusted under `tests/contracts/` with rule IDs registered in `ContractRegistry`.
- [ ] Unit tests (`ctest --test-dir build --output-on-failure`) and integration scripts (`python scripts/test_server.py`) pass locally.
- [ ] Commit messages follow conventional style and include context for docs or code changes.

## Communication

- File issues in GitHub with clear reproduction steps or proposals.
- For cross-repo standards updates, coordinate via the RetroVue documentation working group before diverging from `_standards/`.
- Use discussions or PR comments for design questions; reference relevant docs to keep decisions traceable.

## License

By contributing, you agree that your changes will be licensed under the repository’s MIT License (`LICENSE`).


