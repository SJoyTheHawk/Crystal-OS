# Third-party Clock — worked example

A design sketch, not working code: the Lua runtime and host ABI it targets are
proposed, not built. It exists to make the developer experience concrete enough
to argue with before any of it is implemented.

## What ships vs. what lands on the device

Authored folder:

```
clock3p/
  app.yaml
  main.lua
  assets/icon.bin
```

`crystal pack ./clock3p` produces `com.example.clock3p-1.0.0.capp` — one signed
file, for a catalog download or an email attachment. Installing it unpacks to:

```
/apps/com.example.clock3p/
  app.json          app.yaml, converted at pack time (no YAML parser on device)
  main.lua
  assets/icon.bin
  data/             created empty; state.json appears on first write
```

`.capp` is transport only. On the device there is just a folder, and deleting the
folder is a complete uninstall — program, assets, and state go together.

In developer mode you skip the archive and push the folder straight over USB:

```
crystal install --usb ./clock3p     # unsigned; developer mode only
crystal logs -f                     # streams this app's Lua errors
crystal run com.example.clock3p     # install + launch, one step
```

## What the OS owns, and why

The countdown is not in this app. `svc.timer.arm()` hands an absolute end instant
to the `crystal_service` timer, which raises the expiry toast and chime wherever
the user happens to be. Switch away from a running timer and the app is destroyed
outright — the timer is not, because it never belonged to the app.

The stopwatch stores an absolute start instant and derives elapsed time on read.
Same reason, plus one more: an SNTP correction that moves the wall clock leaves a
stored *elapsed counter* wrong forever, while a stored *instant* stays right.

Both are the rules from `DESIGN.md` §9.5, reachable from a script through the
ABI rather than reimplemented in it.

## Lifecycle notes for app authors

- `onCreate` runs on **every** launch, on a fresh widget tree. One resident app
  (`max_running_num = 1`) means there is no "restored" path to branch on.
- `onPause` is the only guaranteed write point, and it is called before
  `onDestroy` on every path including return-to-launcher.
- Widget handles do not survive `onDestroy`. Rebuild in `onCreate`; never cache
  one in a module-level table expecting it to still be valid.
- `onStop` is **not called in v1**. It is declared so adding it later is additive,
  and is reserved for when an OS overlay can fully cover a card. Do not rely on it:
  stop your timers in `onPause`. When it does arrive, `onStop` is the right place,
  because the panel is bandwidth-bound and redrawing behind an opaque overlay costs
  an animation its frames.
- A callback that runs long is killed. The host checks a deadline every ~10k VM
  instructions; overrun raises a Lua error, the app is torn down, and the user
  gets a toast. The OS stays up.

## Permissions

`app.yaml` declares them and the user sees them at install time. Absent means
denied, not unrestricted: this manifest has no `network_hosts`, so `http.fetch`
is not reachable from this app at all. A weather app would add:

```yaml
permissions:
  network_hosts: ["api.open-meteo.com"]
```
