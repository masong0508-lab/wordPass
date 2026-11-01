# Building and testing wordPass in this environment

## Build
The toolchain lives in a Docker image. If `docker info` fails, start the daemon:

    rm -f /var/run/docker.pid; (dockerd --iptables=false --ip6tables=false --bridge=none >/tmp/dockerd.log 2>&1 &); sleep 5

Build (from the repo root; always use this image, libnds 2 ROMs do not boot in DeSmuME):

    docker run --rm -v "$PWD":/work -w /work devkitpro/devkitarm:20240511 make 2>&1 | grep -E "error|warning|built"

A clean build must print `built ... wordPass.nds` and no warnings.

## Emulator harness
`tools/emu/wp.py` wraps a headless DeSmuME (py-desmume, already installed) and reads a
fixed-layout `DBG` mirror of game state from the ROM, so it keeps working when `Game` changes.

    python3 tools/emu/smoke.py wordPass.nds /tmp/shots     # full regression, prints SMOKE OK

Write ad-hoc probes with `from wp import Emu` (see the docstring). Screenshots are
256x384 PNGs: top screen above, touch screen below. Read them with the image viewer to
check visuals. Coordinates: CLASSIC button (52,147), DAILY (120,147), OPTIONS (86,170),
cell centres via `e.center(c, r)`.

## Rules
- Keep play at 60 fps: `Emu.fps()` must return 120 for 120 cycles in play. Build a copy of
  the tree with `-DWP_PROFILE` (add to CFLAGS) and run `python3 tools/emu/prof.py <copy>`
  to read the per-stage timings in `dbgT` (board stage: ~73k idle, effects ~6k with fire).
- `debugSync()` in `source/debug.c` must be kept in sync if you add state the tests need.
- Commit after each verified change with a descriptive message.
