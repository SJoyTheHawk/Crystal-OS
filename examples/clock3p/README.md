# Third-party Clock

This is an executable Lua package for the Phase 17 runtime. It builds the same
clock, persistent OS countdown timer, and stopwatch screens as the bundled
clock while using only handles and services exposed through `crystal.*`.

`app.yaml` is the package manifest and `main.lua` returns a table of lifecycle
hooks. The runtime creates a fresh widget tree on every launch; handles are
therefore rebuilt in `onCreate`. The stopwatch stores an absolute start time,
and the countdown belongs to the OS timer service, so both keep their intended
behavior when the package app is closed.

The example requests `notify`, `audio`, and `system_timer` permission. It has no
network permission and sets `worker_threads: 0`. Persistent stopwatch and lap
state is written under the package's `data/` directory and limited by
`storage_kb`.

Package it with the repository tool:

```sh
python3 tools/crystal.py pack examples/clock3p --unsigned -o /tmp/clock3p.capp
```

Unsigned packages are for developer-mode workflows. Release packages must be
signed with the configured package trust key.
