# Geolocation provider

> Supply web geolocation fixes from the Electron main process.

Process: [Main](../glossary.md#main-process)

`Session.setGeolocationProvider()` installs a provider for one session. It is
useful when an application has its own location source or wants to use a
network service other than Chromium's default geolocation provider.

```js
const { session } = require('electron')

const ses = session.fromPartition('persist:location')
ses.setGeolocationProvider(async (details, callback) => {
  try {
    const response = await ses.fetch('https://geoip.example.test/json')
    const location = await response.json()
    callback(null, {
      latitude: location.latitude,
      longitude: location.longitude,
      accuracy: 20,
      timestamp: Date.now()
    })
  } catch (error) {
    callback(error)
  }
})
```

## Provider contract

The provider receives `(details, callback)`.

`details` contains the requesting `webContents`, the serialized top-level
`origin`, and `enableHighAccuracy`. The high-accuracy value already reflects
the page request and Chromium's permission constraints.

Call `callback(null, position)` to supply a fix. A position requires numeric
`latitude`, `longitude`, and non-negative `accuracy`. It may additionally
include `altitude`, `altitudeAccuracy`, `heading`, `speed`, and a Unix
millisecond `timestamp`; the timestamp defaults to the current time.

Call `callback(error)` with an `Error` or string to report
`POSITION_UNAVAILABLE` to the page. Only the first callback result is used.

## Browser behaviour

Chromium continues to own permission checks, secure-context requirements,
`timeout`, `maximumAge`, `getCurrentPosition`, `watchPosition`, and DevTools
geolocation overrides. A watch pulls the provider again after each delivered
fix, so a provider should wait for a new source update or rate-limit polling.

Pass `null` to `setGeolocationProvider` to restore Chromium's default provider.
Providers are isolated per session. Network requests are not implicitly bound
to the session, so use `ses.fetch()` when the session proxy configuration must
be honoured.
