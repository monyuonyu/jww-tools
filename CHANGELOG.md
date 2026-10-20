# Changelog

## 0.1.2 — 2026-10-20

- `--check` ends with one `check total:` line with the count of every kind (same keys every time), to compare many drawings side by side.
- Default output unchanged.

## 0.1.1 — 2026-10-20

- `--check` / `--warn-overlaps`: report duplicate lines, overlapping collinear lines, duplicate
  circles/ellipses/arcs and (as information) collinear lines touching end to end, per layer and
  across layers; `--strict` exits with 4 when something was found.
- `--json`: `check` and `issues`.
- `--merge-collinear`: write runs of collinear overlapping/touching lines as one line (off by default).
- `--tol MM`: tolerance in DXF millimetres.
- Default output unchanged (byte for byte the same as 0.1.0).

## 0.1.0 — 2026-10-20

First release. See `docs/release-notes/v0.1.0.md`.
