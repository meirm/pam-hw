# pam-hw (pam_hw) — Design

**Date:** 2026-11-23
**Status:** Draft, pending human approval
**Supersedes:** pam_bluetooth (hardcoded-MAC module, `pam_bluetooth.c` at repo root)

## Intent & Why

Let physical devices you carry — a Bluetooth phone, a USB storage stick, an
ESP32 token — identify you at **local** login and screen unlock. The device
presence can be *sufficient* (log in without typing a password) or
*mandatory* (refuse login without the device). A configuration tree under
`/etc/pamhw/` maps devices to usernames and sets the rules.

This replaces the existing `pam_bluetooth` module, which hardcodes one MAC
address in source, ignores the username, and shells out through `system()`.

## Goals

1. Per-user device→user mapping in `/etc/pamhw/identities`, covering
   Bluetooth addresses and USB devices.
2. Three verification tiers per device:
   - **presence** — device is seen (phone connected via BT, stick plugged in)
   - **secret** — USB stick additionally carries a secret file whose salted
     SHA-256 hash is stored server-side; a cloned stick without the file fails
   - **sign** — ESP32-style token answers a random challenge by signing it
     with a private key (ECDSA P-256); module verifies with the stored public
     key. Private key never leaves the device.
3. `mode = sufficient | mandatory` — global default, overridable per device
   line. Sufficient ⇒ device absent falls through (`PAM_IGNORE`) to the next
   PAM line; mandatory ⇒ device absent refuses authentication.
4. **Local-only enforcement:** the module must never authenticate a remote
   session (SSH etc.), even if an admin mistakenly adds it to that service's
   PAM stack.
5. Safe inside a root PAM module: no `system()`, strict input validation,
   bounded timeouts on all device I/O, no hangs, no crashes.
6. Testable without physical hardware (fakes for sysfs and devices).

## Non-goals / Anti-goals

- **No remote login support.** Out of scope by design (see local-only rule).
- Not theft-proof: a *stolen* stick that still carries its secret file
  authenticates. This is a convenience factor, not real 2FA; the README must
  say so.
- No GDM user auto-selection (PAM authenticates an already-known username).
- No GUI/lockscreen integration beyond the PAM API.
- No side effects on devices: drop the old `bluetoothctl connect` attempt.
- No daemon of our own.

## Constraints

- Language: C, PAM API, built with gcc `-fPIC -shared`. Fedora-first (existing
  README), modern Linux kernel `/sys` layout.
- New dependencies kept minimal: **OpenSSL libcrypto** (SHA-256, ECDSA
  verify). Bluetooth presence in phase 1 shells out to `bluetoothctl` via
  `fork`/`exec` (never a shell), MAC strictly format-validated.
- `/etc/pamhw/` root-owned, mode `0700`; `identities` `0644` is fine
  (no secrets in it); `secrets/` files `0600`.
- Every device I/O bounded by `timeout_ms` (default 3000).
- Failure semantics (see matrix): internal errors fail open (`PAM_IGNORE`)
  in sufficient mode, closed (`PAM_AUTH_ERR`) in mandatory mode. A device
  that is present but **fails verification** is treated as a spoof attempt:
  `PAM_AUTH_ERR` regardless of mode.

## Data files

### `/etc/pamhw/identities`

One device per line; `#` comments and blank lines allowed.

```
# type  identifier                user    [attr=value ...]
bt      AA:BB:CC:DD:EE:FF         alice
usb     0a12:0001:0123456789      alice   verify=secret
usb     1058:25a3                 carol
bt      DC:54:75:C2:AB:12         bob     verify=sign pubkey=/etc/pamhw/keys/bob.pub mode=mandatory
```

- `usb` identifier: `vid:pid:serial` (hex, lowercase, colon separators).
  Serial-less devices may omit `:serial` (matches on VID:PID only; README
  warns this is weak — identical models share it).
- Per-line attrs: `verify=presence|secret|sign` (default: global `verify`),
  `mode=sufficient|mandatory` (default: global `mode`),
  `pubkey=path` (required when `verify=sign`).
- Malformed line ⇒ syslog warning, line skipped, module keeps working.

### `/etc/pamhw/pamhw.conf`

`key = value` lines, `#` comments.

```
mode = sufficient                    # sufficient | mandatory
verify = presence                    # default tier
timeout_ms = 3000
debug = false
services =                           # optional whitelist; empty = all local services
secret_path = .pamhw.sec   # file on stick, relative to mount root
mount_dir = /run/pamhw/mnt    # private read-only mount point
sys_root = /sys                      # overridable for tests
```

### Secrets & keys

- `/etc/pamhw/secrets/<username>`: lines
  `<usb-identifier> <salt-hex> <sha256-hex>`, mode `0600`. The plain secret
  never lives in `/etc`; users generate it on the stick (e.g.
  `head -c 32 /dev/urandom > .pamhw.sec`).
- Public keys: PEM files referenced by `pubkey=` (we ship them under
  `/etc/pamhw/keys/` by convention).

## Local-only enforcement

In `pam_sm_authenticate`, before any device work:

1. `PAM_RHOST` set, non-empty, and not `localhost`/`127.0.0.0/8`/`::1`
   ⇒ remote ⇒ `PAM_IGNORE` + syslog note.
2. `PAM_TTY` equals `ssh`, starts with `ssh`, or is `notty` ⇒ remote ⇒
   `PAM_IGNORE`.
3. If `services=` whitelist is non-empty and `PAM_SERVICE` is not listed
   ⇒ `PAM_IGNORE`.

`PAM_IGNORE` regardless of mode — a mis-wired remote stack must fall
through, not lock anyone out. Mandatory-mode users should set a
`services=` whitelist (README example:
`gdm-password,login,sudo,gnome-screensaver,polkit-1`): there are no
magic exclusions for cron/other non-interactive services, and a
mandatory user's scheduled jobs will be denied unless whitelisted or
given a device. This warning is documented in the README.

## Identity enrollment (CRUD via pamhw-probe)

Users enroll their own devices; the identities file stays root-owned
(0700 dir, 0644 file), so **writes go through `sudo pamhw-probe`** — no
setuid binary. The helper enforces limits from the *invoking* uid
(`SUDO_UID`), not the effective one:

- `pamhw-probe list [--mine]` — connected devices (`vid:pid:serial`,
  human-friendly comment), or the invoking user's current lines.
- `sudo pamhw-probe add <device> [--user me] [--verify=presence]` — adds
  a line. `--user` must equal the invoking user's name; any other value
  is refused (a user can only create **their own** identity lines).
- `sudo pamhw-probe remove <device|all>` — removes lines; limited to the
  invoking user's own lines.
- **Hijack guard:** a device already mapped to a *different* user cannot
  be claimed or edited by anyone but root — whoever owns a line can
  authenticate with that device, so takeover is refused.
- Tier limits for non-root users: `presence` lines (R1) and `secret`
  lines (R3, only via the `hash` flow with the stick actually inserted).
  `sign` lines are root-managed (pubkey provisioning is root's job).
- Root (real uid 0) may manage all lines and all tiers.
- Rewrites are atomic (temp file + `rename`), and every add/remove is
  logged to syslog (who, which device, tier).

README enrollment flow: plug in stick → `pamhw-probe list` →
`sudo pamhw-probe add` → done; removal symmetric.

## PAM stack wiring (decided: documentation only)

`make install` installs the `.so` and the `pamhw-probe` helper;
it **never edits `/etc/pam.d/*`** (lockout risk). The README documents
wiring per mode:

- sufficient mode ⇒ `auth sufficient pam_hw.so`
- mandatory mode ⇒ `auth required pam_hw.so`
  (a `sufficient` stack line silently defeats mandatory mode — documented
  trap)

## Multi-device aggregation (decided: best result wins)

A user may have several device lines. Each line is evaluated
independently; the user's final result is the **best** of them (KISS,
human decision 2026-11-23):

1. Any device **passes** its tier ⇒ `PAM_SUCCESS`. A passing sufficient
   device succeeds even if another device is absent or failed — a
   verification failure never poisons an honest device.
2. Else, any line that yields a deny (mandatory device absent, or a
   present device failing verification) ⇒ `PAM_AUTH_ERR`.
3. Else (only sufficient lines, none present) ⇒ `PAM_IGNORE`.

## Behavior matrix (per device line)

| Situation                          | sufficient      | mandatory       |
|------------------------------------|-----------------|-----------------|
| No device of this user present     | `PAM_IGNORE`    | `PAM_AUTH_ERR`  |
| Device present but not enrolled for this user (unclaimed, or claimed
  by someone else) — treated exactly like absent | `PAM_IGNORE` | `PAM_AUTH_ERR` |
| Device present, tier passes        | `PAM_SUCCESS`   | `PAM_SUCCESS`   |
| Device present, verification fails | `PAM_AUTH_ERR`  | `PAM_AUTH_ERR`  |
| Internal error / timeout           | `PAM_IGNORE`    | `PAM_AUTH_ERR`  |
| Remote session                     | `PAM_IGNORE`    | `PAM_IGNORE`    |

## Approaches considered

- **A. Shell out for everything** (`bluetoothctl`, `/bin/mount`, `udisksctl`):
  fastest to build; rejected — shell-injection surface inside a root module,
  fragile text parsing, side effects.
- **B. Direct APIs, minimal deps** (chosen): scan `/sys` for USB; `fork`/`exec`
  `bluetoothctl` for BT presence; `mount(2)` read-only for the secret read;
  RFCOMM socket for the ESP32; OpenSSL for hashing/verification. Testable via
  `sys_root` fakes; no shell anywhere.
- **C. BlueZ + udisks2 over D-Bus** (`libdbus`): cleanest semantics; rejected
  for now — two heavy new dependencies and much more code. The device-detection
  code sits behind a small interface so this can be swapped in later.

## Architecture & releases

Each release ships working, testable software on its own and is useful
without the next one. Releases R2–R4 get their own implementation plans
after R1 ships.

**R0 — Documentation first** (docs-as-spec)
- README.md rewritten as the target documentation and how-to for pam-hw
  (install, enrollment, configuration, behavior, security notes), with an
  explicit implementation-status marker: it describes the finished
  product; each shipped capability lands with its release. No code in
  R0 — the code follows the README, never the reverse.

**R1 — "A USB stick unlocks my PC"** (USB presence, sufficient-first)

R1 leads with the sufficient experience: stick in ⇒ no password asked;
stick out ⇒ password as usual. The mandatory mapping rides the same code
path (one config value) and is documented, but R1's tests and docs target
sufficient.
- Helper CLI `pamhw-probe list`: prints `vid:pid:serial` of every
  connected USB device so the user can pick one and paste it into
  `identities`.
- Config + identities parsers (pure functions, unit-tested).
- Local-only guard; user lookup via `pam_get_user`.
- USB presence: `/sys/bus/usb/devices` scan, skip root hubs (`usbN`), read
  `idVendor`/`idProduct`/`serial`, match identifiers; `sys_root` override
  for tests.
- Behavior matrix implemented as specified (sufficient ⇒ ignore when
  absent, mandatory ⇒ deny; verification-fail semantics exist but only
  presence tier is reachable in R1).
- Helper CLI `pamhw-probe` (see Identity enrollment below): `list`,
  `add`, `remove` — device discovery and self-service enrollment with
  per-user CRUD limits.
- `debug=true` ⇒ verbose `pam_syslog` tracing.
- Makefile (`gcc -fPIC -shared`), `make install`, README rewrite, project
  renamed **pam-hw** (module `pam_hw.so`; repo dir name `pam-bluetooth` may stay or be renamed — human's call).
- Tests must cover the identities lookup both ways: **with** the hardware
  identifier listed for the user (match ⇒ success) and **without** it —
  unclaimed device present, and another user's enrolled device present —
  each asserting the fall-through (`sufficient` ⇒ `PAM_IGNORE`) and
  refuse (`mandatory` ⇒ `PAM_AUTH_ERR`) paths.

**R2 — Bluetooth presence**
- `bt` entries in `identities`; presence via `fork`/`exec` `bluetoothctl
  info <mac>` (no shell, strict MAC regex before exec, parse
  `Connected: yes`, bounded by `timeout_ms`; missing binary treated as
  "device absent").
- Known nuance: phones drop connections to save power while staying in
  range, so R2 adds `bt_presence = connected|visible` in the conf file
  (default `visible`: quick scan confirms the device is around;
  `connected` as fast-path). Exact scan strategy decided in the R2 plan.

**R3 — Secret tier (USB proof-of-possession)**
- SELinux: we do not promise enforcing-mode compatibility with no policy
  module; the R3 plan starts with a small feasibility spike (mount(2)
  from a confined PAM context) and ships a local policy snippet if
  needed.
- From a matched USB device, enumerate child block devices & partitions
  (`uevent` `DEVNAME`), read-only mount each to `mount_dir`
  (`ro,noexec,nodev`, mode `0700`), read `secret_path`, salted SHA-256
  compare against `secrets/<user>`, unmount on every path.
- Multiple partitions: first readable match wins; unreadable FS ⇒
  verification failure.
- `pamhw-probe` grows a hash helper: prints ready-made
  `secrets/<user>` lines from a mounted stick — removes manual config
  errors.

**R4 — Sign tier (ESP32 challenge-response)**
- RFCOMM connect to the token's SPP channel, line protocol:
  module → `CHAL <b64-32-random-bytes>` (from `getrandom(2)`),
  token → `SIG <b64-ECDSA-P256-signature>`; bounded by `timeout_ms`.
- Verify with OpenSSL against `pubkey=` PEM.
- Repo ships a reference ESP32 Arduino sketch (mbedtls ECDSA) + protocol
doc.

## What's left to the builder

- Exact parser grammar code, struct layouts, Makefile details.
- Unit-test harness: keep to plain asserts (no new deps) or minunit.
- ESP32 sketch contents and its mbedtls wiring.
- `/sys` traversal corner cases (below) encountered in testing.

## Known hardware realities (seen on the author's machine)

- Root hubs expose serials — must be excluded.
- Some devices report garbage serials (e.g. all zeros) — record VID:PID
  alongside; treat empty/garbage serial as "no serial".
- Identical stick models share VID:PID — documented weak-identifier warning.
