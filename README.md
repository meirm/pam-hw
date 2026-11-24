# pam-hw

Use a physical device you carry — a USB storage stick, a Bluetooth phone,
an ESP32 token — to identify you at **local login and screen unlock**.
Your stick is in ⇒ no password asked. It's out ⇒ password as usual.
Or, configured *mandatory*: no device, no login.

```
$ sudo pamhw-probe list
usb  0a12:0001:0123456789   # SanDisk Ultra
$ sudo pamhw-probe add 0a12:0001:0123456789
$ sudo pamhw-probe list --mine
usb  0a12:0001:0123456789  me  verify=presence
```

> **Status: R1 — USB presence shipped.** USB sticks work today (presence
> tier, sufficient/mandatory). Bluetooth arrives in R2, secret tier in
> R3, ESP32 signing in R4 — see Roadmap. This README is the specification;
> capabilities arrive with the releases they name; not-yet-shipped features
> are marked ⚠.

> **Honesty first:** pam-hw is a *convenience factor*, not real 2FA.
> USB serials and Bluetooth addresses can be spoofed; a **stolen** stick
> that still carries its secret file authenticates (that is what the R3
> secret tier mitigates — but a stolen *enrolled* stick with its file is
> game over). Never treat it as protection against someone with physical
> access to your enrolled hardware.

## How it works

- `/etc/pamhw/identities` maps devices to usernames:

  ```
  # type  identifier                user   [verify=…] [mode=…] [pubkey=path]
  usb     0a12:0001:0123456789     me
  bt      AA:BB:CC:DD:EE:FF        me
  usb     1058:25a3                carol              # no serial — weak, see below
  ```

- `/etc/pamhw/pamhw.conf` sets the rules (mode, timeouts, debug…).
- At authentication the PAM module checks only **your** devices, only
  **locally** (it refuses to mean anything on SSH or other remote
  sessions), and reports the result to the PAM stack.
- A device present but **not enrolled for you** — unclaimed, or claimed
  by someone else — counts exactly like *absent*.

## Requirements

- Linux with a modern `/sys` layout (developed on Fedora).
- A PAM stack you are allowed to edit (root).
- ⚠ OpenSSL libcrypto from R3 on (hashing, signature verification).

## Install

Installation is two commands — and `make install` **never edits
`/etc/pam.d/*`** (a botched PAM stack can lock you out; you wire that
yourself, see below).

```sh
make
sudo make install          # installs /lib64/security/pam_hw.so and pamhw-probe
```

## Set up your stick (how-to)

1. **Plug it in and find it:**

   ```sh
   sudo pamhw-probe list
   usb  0a12:0001:0123456789   # SanDisk Ultra
   ```

   This is the exact same detection the module does — if your stick is
   not listed here, it will never match. Sticks that report no serial
   fall back to `vid:pid` only (the listing shows them without a serial
   part); two identical stick models then share an identity (⚠ weak).
   Identifiers are case-insensitive on input and stored lowercase.

2. **Enroll it for your user** (you can only enroll *yourself*; devices
   already claimed by another user are refused — hijack guard):

   ```sh
   sudo pamhw-probe add 0a12:0001:0123456789    # silent on success
   sudo pamhw-probe list --mine                 # your lines, plugged in or not
   usb  0a12:0001:0123456789  me  verify=presence
   ```

   **Admins:** managing *other users'* lines (`--user bob`, removing
   someone else's device) needs a real root session. `sudo` and even
   `sudo -i` carry `SUDO_UID`, so pamhw-probe treats them as the invoking
   user and refuses — this is the same guard that protects your lines.
   Use `su -`, or `sudo env -u SUDO_UID pamhw-probe … --user bob`.

3. **Wire the PAM stack** for the services you want (e.g.
   `/etc/pam.d/gdm-password` for the lock screen, `/etc/pam.d/sudo`,
   `/etc/pam.d/login`):

   ```
   auth  sufficient  pam_hw.so     # sufficient mode (default)
   ```

   Put it at the **top**. With the default `mode = sufficient`, a match
   logs you in; an absent stick falls through to the password prompt.
   For mandatory mode use `auth required pam_hw.so` — a `sufficient`
   stack line **silently defeats** mandatory mode (see the trap box
   below).

4. **Test — with a safety net.** Keep a root shell open, then from
   another terminal:

   ```sh
   su me            # with stick: should succeed without password
   su me            # stick unplugged: should ask for password
   ```

5. **Remove enrollment** (your own lines only):

   ```sh
   sudo pamhw-probe remove 0a12:0001:0123456789     # or: remove all
   ```

> **The PAM trap:** `mode = mandatory` only *denies* if the stack line
> is `required`/`requisite`. Under `sufficient`, a `PAM_AUTH_ERR` from
> the module is just… ignored, and the password prompt saves the day —
> mandatory becomes a no-op. Prefer `requisite` for fail-fast.

## Configuration reference

### `/etc/pamhw/pamhw.conf` — `key = value`, `#` comments (a `#` at line start or after whitespace starts a comment)

| Key           | Default              | Meaning |
|---------------|----------------------|---------|
| `mode`        | `sufficient`         | `sufficient` ⇒ absent device falls through to password; `mandatory` ⇒ absent device refuses login. Unknown values are logged and ignored |
| `verify`      | `presence`           | Default tier; per-line `verify=` overrides. ⚠ `secret` R3, `sign` R4 — in R1 a device whose tier is `secret`/`sign` is present-but-unverifiable and denies. Unknown values are logged and ignored |
| `timeout_ms`  | `3000`               | Bound on all device I/O |
| `debug`       | `false`              | Verbose `pam_syslog` tracing (`true`/`false`, `yes`/`no`, `1`/`0`) |
| `services`    | *(empty = all local)*| Whitelist of PAM services allowed to use the module; anything else is ignored. Recommended for mandatory mode: `gdm-password,login,sudo,gnome-screensaver,polkit-1` |
| `secret_path` | `.pamhw.sec`         | ⚠ R3 — secret file on the stick, relative to mount root |
| `mount_dir`   | `/run/pamhw/mnt`     | ⚠ R3 — private read-only mount point |
| `sys_root`    | `/sys`               | Overridable for tests |

### `/etc/pamhw/identities` line grammar

```
# type  identifier                 user   [attr=value ...]
usb     vid:pid[:serial]           user   [verify=presence|secret] [mode=sufficient|mandatory]
bt      AA:BB:CC:DD:EE:FF          user   [verify=…] [mode=…] [pubkey=/etc/pamhw/keys/X.pub]
```

- Identifier matching is exact after normalization (USB lowercase, BT
  uppercase); `vid:pid` without serial matches a serial-less stick.
  `vid` and `pid` are 4 hex digits; whitespace or control characters
  anywhere make the line malformed.
- Per-line `mode`/`verify` override the global defaults. In R1 only
  `presence` can be verified: a line whose tier is `secret`/`sign`
  denies (`PAM_AUTH_ERR`) when its device is present.
- Malformed lines (unknown type, attribute or attribute value, bad
  identifier or username) are skipped with a syslog warning — one typo
  never takes the module down.
- `#` at line start or after whitespace starts a comment.
- ⚠ R3 secrets live in `/etc/pamhw/secrets/<user>` (salted SHA-256, 0600)
  — the plain secret only ever lives on the stick:
  `sudo pamhw-probe hash /media/me/STICK --user me` prints the line to
  paste.

## Behavior

| Situation | sufficient | mandatory |
|---|---|---|
| No enrolled device of this user present | `PAM_IGNORE` → password | `PAM_AUTH_ERR` |
| Device present but **not enrolled for this user** | treated as absent | treated as absent |
| Enrolled device present, tier passes | `PAM_SUCCESS` | `PAM_SUCCESS` |
| Enrolled device present, tier **fails** (spoof/bad secret) | `PAM_AUTH_ERR` | `PAM_AUTH_ERR` |
| Internal error / timeout | `PAM_IGNORE` | `PAM_AUTH_ERR` |
| Remote session (SSH etc.) | always `PAM_IGNORE` | always `PAM_IGNORE` |

**Several devices, best result wins:** if any of your devices passes, you
are in — a phone being away or failing never cancels a stick that passes
(KISS). With no passing device, any mandatory-absent or failed line
denies; otherwise it falls through.

**Local only:** the module checks `PAM_RHOST`/`PAM_TTY`/`PAM_SERVICE` and
steps aside on anything that smells remote (`rhost` set, `ssh`/`notty` TTYs), regardless of mode. Even if someone wires it into
`/etc/pam.d/sshd` by mistake, it will never authenticate remotely.
Mandatory-mode users: scheduled jobs (`cron`, …) are local too and get
**denied** without a device — whitelist the services you actually want
(`services=`) or give the job a device.

BT (`bt`) lines are parsed but inert until R2 ships — a `bt` line never passes or denies in R1 (a `mode=mandatory bt` line therefore does not deny yet).

## Roadmap

| Release | Delivers | Status |
|---|---|---|
| R0 | This README — the spec | **shipped** |
| R1 | USB presence, sufficient/mandatory, `pamhw-probe list/add/remove`, fake-sysfs test suite (incl. not-enrolled cases) | **shipped** |
| R2 | Bluetooth presence (`bt` lines; `bt_presence = connected\|visible` — phones drop connections to save power) | planned |
| R3 | Secret tier: `.pamhw.sec` on the stick, salted-hash check, ro-mount machinery, `pamhw-probe hash` | planned |
| R4 | ESP32 sign tier: challenge-response over Bluetooth SPP, ECDSA P-256, reference sketch in-tree | planned |

## Troubleshooting

- **Module never matches:** run `sudo pamhw-probe list` — what it shows
  is what the module sees. No stick ⇒ no match. Different serial than
  enrolled ⇒ re-enroll.
- **Nothing happens at all:** check the stack line is in the right
  `/etc/pam.d/<service>`, and that the service isn't excluded by
  `services=`.
- **Why is SSH unaffected?** By design — see *Local only* above.
- **Suspected spoof attempt:** a device that is present but fails its
  tier is logged (`PAM_AUTH_ERR`, syslog) — check your journal.
- Turn `debug = true` on and watch the journal (`journalctl -t pam_hw`).

## Uninstall

```sh
sudo make uninstall          # removes pam_hw.so and pamhw-probe
# remove the auth line(s) from your /etc/pam.d/* files
# (keep /etc/pamhw/ if you want your enrollment for later)
```

## License

See [LICENSE.txt](LICENSE.txt).
