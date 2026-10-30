# OnLoc for Sailfish OS

An unofficial, native Sailfish OS client for [OnLoc](https://onloc.app/), the
self-hosted device tracker (server:
[onloc-app/onloc-api](https://github.com/onloc-app/onloc-api)). It is not
affiliated with the OnLoc project; it talks to the server's public REST and
Socket.IO API, which was read from that project's source.

It does two jobs: **see and control** your devices, and **be** one of them.

## What it does

- **Sign in** to your own server (JWT access + refresh token). The refresh
  token is kept in the Sailfish Secrets vault; the password is never stored.
- **Devices:** a list with the last known position, battery and connection
  state, including devices shared with you.
- **Device page:** the last 24 hours of positions, drawn as a route on a base
  map (OpenFreeMap vector tiles, decoded and painted in the app - see
  [docs/base-map.md](docs/base-map.md)) with the newest point marked, a history
  list, and *Open in Maps* through a `geo:` link.
- **Commands:** ring, lock (with an optional message) and flash. The server
  queues them when the target device is offline.
- **This phone as a device:** create or pick a device, then report the phone's
  position at a chosen interval, or on demand.
- **Finnish translation** included; English otherwise.

## Background service (`onloc-daemon`)

A second, optional package that makes the phone a proper OnLoc device. The
application carries it: **Settings -> Background service -> Install the
service** writes the package to Downloads and the system's installer asks you
to confirm (allow untrusted software first; these are unsigned builds). See
[docs/daemon.md](docs/daemon.md) for the design.

- keeps reporting the phone's position after the app is closed, queueing
  fixes while there is no network and sending them with their real
  timestamps;
- obeys the server's commands:

  | command | on the phone |
  |---|---|
  | ring | plays the ringtone for 30 s at full volume whatever the profile, then restores the volume. A double press of the power button or unlocking the phone stops it early. |
  | lock | puts the device lock in its Locked state, so the code is needed at once, then locks the screen and turns the display off. An optional message is posted as a notification. |
  | flash | blinks the display for about five seconds. |

It is a separate package because it cannot run inside the Sailjail sandbox:
it has to outlive the app and drive PulseAudio and mce. **Handing it your
sign-in means a refresh token sits in a file only you can read**
(`~/.config/io.github.jarnose/onloc/daemon.json`, mode 0600); it is written
only when *Run in the background* is switched on and deleted when it is
switched off or you sign out. The password is never in it.

## Privacy

- The app talks to your OnLoc server and to the map tile server
  (`tiles.openfreemap.org` by default), nothing else. The tile server learns
  the *area* of every device page you open, never the positions themselves;
  the map can be switched off, or pointed at another source, in Settings.
- Positions are sent only to the server you signed in to.

## Not done

- **The map is static:** the device's track fitted into a frame. It does not
  pan or zoom and draws no labels.
- **Commands arrive over Socket.IO long polling** (the SDK has no QtWebSockets
  headers); UnifiedPush is not implemented.
- **Without the daemon,** reporting works only while the app is running and
  failed uploads are not queued.
- **Ringing cannot be stopped from the server**, and the volume keys do not
  stop it (mce does not report them).
- **Battery cost of background reporting is unmeasured.** Prefer a long
  interval (5 minutes or more) for everyday use.

## Server versions

The released OnLoc API (1.3.0) takes one location per `POST /api/locations`;
the development branch takes `{"locations": [...]}`. The client tries the
first and switches on a 400, so both work.

## Build

Both packages are built from this tree with the Sailfish SDK's `sfdk`. The
daemon goes first, because the application embeds its RPM:

```
# 1. the background service (its spec is in daemon/, not rpm/)
sfdk -c target=SailfishOS-5.1.0.11-aarch64 -c specfile=daemon/onloc-daemon.spec build
mkdir -p daemon/embed && cp RPMS/onloc-daemon-*.aarch64.rpm daemon/embed/onloc-daemon.rpm

# 2. the application (without daemon/embed/onloc-daemon.rpm it builds, but
#    Settings has no install entry)
sfdk -c target=SailfishOS-5.1.0.11-aarch64 build
```

Both builds are in-tree and share one CMake cache, so delete `CMakeCache.txt`
and `CMakeFiles/` between builds for different targets or packages. RPMs land
in `RPMS/`. Qt Creator builds the application with the default options.

The target's Qt is 5.6; that is why some newer Qt APIs are avoided (see the
comments in `src/api/onlocclient.cpp`).

Tests for the map code, which needs no Qt, run with plain g++; `tools/ci.sh`
runs everything CI runs before the RPMs are built (those tests, a QML check
for what only a phone would show as a blank page, and the translation
catalogues - a new `qsTr()` string needs its Finnish text or CI fails):

```
tools/ci.sh
```

GitHub Actions (`.github/workflows/build.yml`) builds both RPMs for aarch64
and armv7hl with the SDK image on every push, the daemon first and embedded in
the application. A tag `v<version>` publishes
them with checksums as a release, after `tools/check_version.sh` has held the
tag to both specs' `Version:`, the daemon's version string and a
`CHANGELOG.md` section.

## Install

Take the RPMs from the [releases](https://github.com/jarnose/harbour-onloc/releases)
(unsigned CI builds; the phone warns that they are untrusted) or build them,
then:

```
scp RPMS/harbour-onloc-*.aarch64.rpm RPMS/onloc-daemon-*.aarch64.rpm defaultuser@<phone>:Downloads/
ssh -t defaultuser@<phone> 'devel-su rpm -Uvh --force Downloads/harbour-onloc-*.rpm Downloads/onloc-daemon-*.rpm'
```

The app's permissions are `Internet`, `Location`, `Secrets` and `Downloads`
(to hand the bundled daemon package to the system's installer). The daemon's
log is `~/.cache/io.github.jarnose/onloc/daemon.log`.

## Layout

| path | what |
|---|---|
| `src/api/` | the OnLoc REST client |
| `src/controller/`, `src/model/` | application logic and list models |
| `src/map/` | base map: Mercator, vector-tile decoder, tile source, `MapCanvas` |
| `src/shared/` | config and location code used by both the app and the daemon |
| `src/daemon/`, `src/onlocd.cpp` | the background service |
| `qml/` | the Silica user interface |
| `docs/` | design notes for the map and the daemon |
| `tests/` | Qt-free tests for the map code |

## Credits

The map code is ported from the author's other Sailfish apps
(harbour-edgesync, itself from harbour-suuntosync). Map data © OpenStreetMap
contributors via OpenFreeMap / OpenMapTiles; the attribution is drawn on the
map. MIT licence, see [LICENSE](LICENSE).
