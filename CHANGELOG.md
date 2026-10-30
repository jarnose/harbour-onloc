# Changelog

## Unreleased

- The application carries the background service's package and installs it from
  *Settings -> Background service*: the package is written to Downloads and the
  system's installer asks for confirmation. Settings also offers an update when the
  installed service's version differs from the application's. Needs the `Downloads`
  permission. Both binaries now take their version from one place, `CMakeLists.txt`.

## 1.0.0

First release. Two packages: the application, `harbour-onloc`, and the optional background
service, `onloc-daemon`.

Application

- Signs in to your own OnLoc server (access and refresh tokens; the refresh token is kept in the
  Sailfish Secrets vault, the password is not stored). Works with the released 1.3.0 API and the
  development branch.
- Device list with last position, battery and connection state, including devices shared with you.
- A device's last 24 hours as a route on a base map (OpenFreeMap vector tiles, decoded and painted
  in the application; on by default, switchable and re-pointable in Settings), a history list and
  a hand-off to the maps application.
- Sends ring, lock (with a message) and flash to a device.
- This phone as a device: create or choose it, report its position on an interval or on demand.
- Finnish translation.

Background service

- Keeps reporting the phone's position with the application closed, queueing fixes while there is no
  network and sending them with their real timestamps.
- Ring (30 s at full volume whatever the profile; a power-button double press or unlocking stops
  it), lock (the device lock's Locked state, so the code is needed at once) and flash (blinks the
  display), received over Socket.IO on long polling.
- The application hands it the sign-in through a file only the user can read, written when
  *Run in the background* is switched on and deleted when it is switched off or on sign-out.

Checked on a Jolla phone (Sailfish OS 5.2) against a real server: sign-in, upload, map,
background reporting with the application closed, ring, lock and flash. Queueing and reconnecting
also against a local OnLoc 1.3.0 container.

Known limits: the map is static (no panning or zooming); no UnifiedPush; ringing cannot be stopped
from the server and the volume keys do not stop it; the battery cost of background reporting has
not been measured, so prefer an interval of five minutes or more.
