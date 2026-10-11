# Browser firmware updates

## Install OTA support once over USB

Build this version and use the normal full USB upload:

```sh
pio run -e waveshare-7c-box -t upload
```

This installs the application, partition table and rollback-enabled bootloader.
Flashing only the application does not enable rollback in an older bootloader.
Do not erase the flash: ordinary uploads with the existing partition layout
preserve NVS settings. The existing layout has two 7 MiB application slots and
does not need resizing. Native ESP-IDF users must also enable
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` in their active sdkconfig, rebuild the
bootloader, and run `idf.py flash`; defaults do not override an older sdkconfig.
The PlatformIO pre-build script enforces that setting automatically.

## Update through a browser

1. Open the panel's settings using its IP address or hostname.
2. Set an **Administrator password** (8–64 printable ASCII characters), then
   **Save & reboot**. Sign in with username **admin** and that password.
   Settings and firmware updates are protected once a password is configured.
3. Choose **Firmware Update**. The page shows the installed version/build.
4. Choose the application **firmware.bin**, then **Upload & restart**.
5. Leave the panel powered. The page reports upload progress, verification,
   restart, and the outcome of startup checks.

The local application binary is `.pio/build/waveshare-7c-box/firmware.bin`.
Alternatively, download the `esp-home-panel-ota-<commit>` artifact from a
successful **Build panel firmware and test OTA** GitHub Actions run and extract
its `firmware.bin`. No GitHub credentials are stored on the panel. Use the
physical panel build, not the Wokwi simulation build.

Do not upload a ZIP, ELF, bootloader, partition table or merged/full-flash binary.
Uploads only change the inactive application slot and OTA boot selection;
Wi-Fi, MQTT, hostname, display preferences and administrator password stay in NVS.
Future releases that change the partition table or bootloader still need USB.

## Failure handling

The panel checks authorization, a per-boot anti-CSRF token, content type, size,
ESP32-S3 image header, application descriptor and project name before preparing
the inactive slot. ESP-IDF verifies the complete image before it is selected
for boot. Broken connections, stalled uploads and rejected images do not select
the partially written slot. Reload the page after a restart if its token expires.
An upload is refused while first-boot checks are pending.

With the rollback-enabled bootloader installed, new firmware has one initial
boot attempt. This application confirms the image only after board/display/UI
initialization succeeds, web settings start, and the main loop is running at
least 30 seconds after boot. A 90-second startup deadline restarts an unconfirmed
image to trigger rollback; crashes/reboots before confirmation also trigger it.
Internet and MQTT availability are not required, and Wi-Fi setup-AP fallback
counts as a working local configuration service.

These are startup checks, not proof that every screen/control works. A confirmed
image will not automatically roll back for later logical defects. Recovery also
requires a previously valid image and compatible saved settings. Do not upload
older firmware without OTA/boot-confirmation support if you expect to retain
browser updates and rollback behavior.

If the browser loses its connection just as an update completes, reconnect and
check the installed build before retrying. Keep USB available for recovery.

## Administrator access

The existing server uses HTTP. Its Basic authentication and firmware transfer
are not encrypted; use a trusted local network and a unique panel password.
Initial provisioning remains open until a password is set; OTA is disabled
without one. Passwords are stored in NVS alongside the existing network secrets,
without application-level encryption. Do not forward this service to the Internet.
The UI never displays the saved password. Blank password fields preserve it.
An authenticated **Reset configuration** clears all settings, including the
password, and returns the panel to provisioning mode. A forgotten password
requires local USB/NVS recovery, which can also erase Wi-Fi and other preferences.

## Validation

Run `python tools/test_ota.py` for host-side authorization, token, image/header,
size, fragmented upload, timeout, flash-failure, boot-selection and startup-check
tests. It uses address/undefined-behavior sanitizers; restricted sandboxes may
require `ASAN_OPTIONS=detect_leaks=0`. Also run the settings, network and CMake
regression tests. The Actions workflow builds the actual firmware.

Before relying on remote-only updates, test a successful upload on a real panel,
an interrupted transfer, persistence after reboot, and rollback from a deliberately
unconfirmed test build. Physical power-loss and bootloader recovery behavior cannot
be established by host mocks alone.
