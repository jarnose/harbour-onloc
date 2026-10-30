# The background service (`onloc-daemon`)

`onlocd` keeps reporting the phone's position and carries out the server's
ring / lock / flash commands while the application is closed. It is a systemd
user service in its own package, `onloc-daemon`, built from this tree with

    sfdk -c target=SailfishOS-5.1.0.11-aarch64 -c specfile=daemon/onloc-daemon.spec build

(`-DBUILD_APP=OFF -DBUILD_DAEMON=ON`). The spec is under `daemon/`, not `rpm/`,
because sfdk refuses to build when `rpm/` holds two spec files - the same
arrangement as harbour-suuntosync's notification daemon.

## Why a separate package

Inside Sailjail an application cannot outlive itself, cannot drive PulseAudio
or mce, and cannot read files outside its own directories. The daemon needs all
three, so it lives outside the sandbox and the application stays inside it.
There is no RPM dependency either way: removing the application from the app
grid must not fail, and the daemon works on its own once configured.

## Installing it from the application

The application carries the daemon's RPM and offers to install it: *Settings
-> Background service -> Install the service*. The design is
harbour-suuntosync's (`docs/embedded-daemon-install.md` there), which in turn
is how harbour-situations2application does it.

- `DAEMON_RPM` in `CMakeLists.txt` (or the conventional path
  `daemon/embed/onloc-daemon.rpm`, gitignored) embeds the package as a Qt
  resource at `:/rpms/onloc-daemon.rpm` and defines `ONLOC_EMBEDDED_DAEMON_RPM`.
  The RPM's lead magic is checked at configure time. Without it the
  application builds as before and the Settings section is absent, not inert.
- `AppController::installDaemon()` writes the resource to
  `~/Downloads/onloc-daemon.rpm` (fixed name, so a second install replaces the
  first) and calls `QDesktopServices::openUrl()`. The application installs
  nothing itself: the system's installer, outside the sandbox, asks the user.
  Hence the `Downloads` permission. The file is left behind, because `openUrl`
  is asynchronous and deleting it would race the installer.
- Sideloading has to be allowed (Settings | Untrusted software); the page says
  so before the attempt, since the refusal does not explain itself.
- **Knowing whether it is installed:** from inside the sandbox `/usr/bin`
  shows only part of the real directory, so the binary cannot be looked for.
  The daemon writes `daemon-status.json` as soon as it starts, configured or
  not, and that file's existence is the signal. The same file carries the
  daemon's version; when it differs from the application's, Settings offers
  *Update the service*. Both binaries get their version from the one
  `project(... VERSION ...)` line in `CMakeLists.txt`.
- **Build order:** the daemon first, its RPM staged at
  `daemon/embed/onloc-daemon.rpm`, then the application. Built the other way
  round the application would come out silently without the installer. CI does
  it in that order, and unpacks the finished application RPM to look for the
  lead magic in its binary, so the check is on the artifact. The embedded
  package is architecture-specific and the CI matrix gives one per
  architecture, so the aarch64 application carries the aarch64 daemon.

## How the two talk: a file, and a file back

- **App -> daemon:** `~/.config/io.github.jarnose/onloc/daemon.json`, mode 0600.
  Server, device id, interval, an `enabled` flag for position reporting, and
  the **refresh token**. This is the one place the token is on disk in plain
  text: the vault that holds it for the application is owner-only to the
  application, and there is no other way to hand a secret to a different
  executable. It is written only when the user turns *Run in the background*
  on, and deleted when they turn it off or sign out. It is a revocable token,
  never the password. The daemon watches the file, so changes apply at once.
- **Daemon -> app:** `~/.local/share/io.github.jarnose/onloc/daemon-status.json`,
  rewritten on change and every 30 s. The application reads its age to decide
  whether the daemon is alive; while it is, the application's own tracker stays
  off so nothing is uploaded twice.

The daemon's log is `~/.cache/io.github.jarnose/onloc/daemon.log` (256 KB, one
rotation), because the journal keeps only seconds of history on this device.

## Reporting

Fixes are taken at the configured interval and **queued before they are
sent** (memory plus `upload-queue.jsonl`, capped at 5000), then uploaded
oldest first. A fix taken with no network is sent when it returns, with its own
timestamp, so the server's history has the true times. A 4xx the server will
never accept is dropped so it cannot block the rest; a network error or 5xx
keeps everything and retries.

Authentication failures are told apart from network failures on purpose. A
refresh the server *rejects* (400/401/403) means signed out and the daemon
stops until the application writes a new token. A dead network is status 0 and
only retried - an earlier version reported it as 401 and stopped for good.

## Commands

The server pushes `ring-command`, `lock-command` {message} and `flash-command`
to every socket registered for the device. The daemon connects with
Socket.IO v4 over **Engine.IO long polling** (`src/daemon/socketioclient.cpp`):
the SDK target has no QtWebSockets headers, polling is plain HTTP, and the
server speaks it by default. It asks for a fresh access token on every
(re)connect, because the server checks the token only then, and reconnects
with a delay that doubles up to a minute. On connecting it tells the server
the device can ring, lock and flash.

| command | what the phone does |
|---|---|
| ring | `paplay` of the ringtone for 30 s at 100 % volume and unmuted, whatever the profile; the previous volume and mute are restored. The display is lit. Power button or unlocking stops it early. |
| lock | `org.nemomobile.lipstick.devicelock.setState(1)` on the system bus (the device lock's Locked state, so the code is needed at once - the daemon refuses any other value, and refuses all when the device lock is not in use), then mce: lock the touch screen and turn the display off. The message, if any, is a notification. |
| flash | blinks the display for about five seconds (there is no torch LED a user session may drive), then leaves it as found. |

**Not verified on a real phone yet:** the three commands' effects. What *was*
verified, against a local OnLoc server (the released 1.3.0 image), with the
daemon built for the i486 target and run in the SDK build engine and a fake
position: the polling session and namespace connect, `register-device`,
capability update (PATCH), all three events arriving, uploads, and the queue
draining after the server was stopped and started again.

## Not done

- Ringing stops after 30 s, on a power-button press or when the phone is unlocked (mce's `power_button_trigger` and `tklock_mode_ind`). A volume key does not stop it: mce does not report those.
- No upload batching: the released API takes one location per request.
