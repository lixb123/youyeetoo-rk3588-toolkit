# Camera Known Issues And Interpretation

- `BUG-A-010`: 8K recordings without LRV may be absent from the current media-list path because the SDK listing flow depends on LRV visibility.
- Some older `.insv` and `.lrv` records returned HTTP `400` with body `1004` through both the SDK downloader and direct HTTP. This supports a media-record or file-readability problem, not a general extension rejection.
- A confirmed-readable `.lrv` succeeded through both SDK and HTTP with identical size and SHA-256.
- Device discovery may briefly open cameras and compete with an active session. The application supplements active-session entries, so vendor and application enumeration can differ.
- `.lrv` deletion has had SDK limitations. Do not interpret partial delete-all results without checking per-file evidence.
- Video downloads need substantially longer timeouts than status calls. Preserve elapsed time and configured timeout in defect reports.
- Camera slot names such as `cam0` resolve through physical USB topology, not enumeration order. Changing hub or port requires remapping.
- Multiple X5 devices share a model name. Use the SDK serial as the session key; mixed models follow the same rule.
- Extended mode parameters differ by model. X5 and X4 Air have explicit profiles; unverified models must return unsupported instead of inheriting X5 defaults.
- `CameraSDKTest` and `youyeetoo-app.service` should not own the same camera concurrently.
