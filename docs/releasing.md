# Release checklist

1. Select the exact source commit and release version. Run both GitHub Actions workflows against that revision and inspect the firmware and UI artifacts.
2. Test first-boot provisioning, both supported board profiles where available, touch navigation, MQTT controls/state synchronization, weather, and any advertised sensor features on hardware. Test OTA success, interrupted upload, settings persistence and rollback as described in [firmware updates](firmware-updates.md).
3. Refresh the [screenshots](screenshots/README.md) when the UI changes. Use sample data and keep credentials and personal information out of captures and logs.
4. Include `LICENSE`, `THIRD_PARTY_NOTICES.md` and the Outfit OFL notice. Review the exact resolved dependency licenses/notices and artwork provenance before distribution. Retain third-party notices rather than replacing them with the project license.
5. Publish the corresponding source for the exact firmware build, including build scripts, canonical PNG inputs, generated font source and dependency/version information. Keep the resolved component lockfile and tool versions with the release build so it can be reproduced. Do not ship credentials or local NVS/settings dumps.
6. Label an OTA attachment as an **application image for ESP32-S3**, and state the supported boards and initial USB-flash requirement. Do not present the OTA image as a full-flash image. Include migration notes and the matching Home Assistant bridge package.
7. Tag the verified commit and publish release notes describing changes, installation, known limitations and the corresponding source location.

Documentation and host-rendered screenshots alone do not certify hardware behavior or complete a release. Repository visibility, tags and published releases are separate actions.
