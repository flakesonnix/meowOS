# Agent Working Mode

Current project phase: Release / Feature Development

## Default behavior

- Implement tasks immediately.
- Modify code directly.
- Add or update tests.
- Build the affected targets.
- Run relevant tests.
- Fix regressions introduced by the change.
- Create a single clean commit.

## Do not

- Write design documents.
- Produce implementation plans.
- Produce roadmaps.
- Perform architecture reviews unless explicitly requested.
- Refactor unrelated code.
- Expand scope beyond the requested task.

## Only stop and ask if

- the requested behavior is ambiguous,
- multiple incompatible implementations exist,
- or a decision would permanently affect public APIs or file formats.

## Architecture

Architecture is considered stable. Default assumption: implement, don't redesign.

## Expected output

1. Short implementation summary.
2. Tests executed.
3. Results.
4. Commit hash.
