# MeowOS Documentation

## User

- [Installing packages](downloads.md)
- [Updating](repositories.md)
- [Verifying & repairing](security.md)
- [Doctor diagnostics](doctor.md) — including `doctor --security`

## Repository operators

- [Repository layout & identity](repositories.md)
- [Signing & trust](security.md)
- [Hosting a repository (meow-server)](repository-server.md)
- [Repository selection, health & mirrors](repository-selection.md)
- [Reproducible metadata/builds](reproducible.md)
- [Package cache](cache.md)

## Developers

- [DEVELOPING.md](../DEVELOPING.md) — Build, test, commands, internals
- [Architecture](architecture.md)
- [Bootstrap chain](bootstrap.md) — incl. `base` meta-package + ISO (`scripts/mkiso.sh`)
- [Packaging guide](packaging.md)
- [Package format & reproducible builds](reproducible.md)
- [Download transport](downloads.md) — progress UI, idempotent resolve
- [Restricted hook runner](hooks.md)
- [Cache & verification](cache.md)
- [Package history & install reasons](package-history.md)
- [Optional dependencies](optional-dependencies.md)
- [Package groups](package-groups.md) — groups vs meta-packages (`base` is a package)
- [Resolver backends: Legacy vs SAT](resolver-comparison.md)
- [SAT-as-default transition criteria](sat-default-criteria.md)
- [Benchmark methodology](benchmark-methodology.md)
- [Testing](testing.md)
