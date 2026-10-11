# Hostname and UniFi discovery

After installing the updated firmware, open the panel's web settings by IP.
In **Wi-Fi & Network**, set **Hostname** (for example `bedroom-panel`), then
choose **Save & reboot**. Use a unique name for each panel: 1–32 lowercase
letters, digits or hyphens, without a leading/trailing hyphen or domain suffix.
The default is `panel-` plus the last six hexadecimal digits of the station MAC.
Existing installations receive that default without losing other settings.
Changing this hostname does not change the MQTT panel ID or topic mappings.

## What the panel advertises

- DHCP hostname option 12 is set before the first Wi-Fi connection, so the
  DHCP server receives the chosen name during discovery and lease requests.
- The mDNS responder answers `bedroom-panel.local` and advertises the web
  settings service as `_http._tcp` on port 80.
- The DHCP client requests domain-name option 15, validates it from DHCP ACKs,
  and refreshes it when supplied during renewal. The suffix is runtime lease
  information, not a saved configuration override; disconnecting clears it.
- The **Network names** card displays the supplied suffix and resulting LAN
  name. For example, hostname `bedroom-panel` plus DHCP domain `snoognet.core`
  gives `bedroom-panel.snoognet.core`. With no suffix supplied, it shows
  **Not supplied** and retains the bare hostname. `.local` remains the separate
  multicast DNS name; it is never sent as part of the DHCP hostname.

UniFi learns the DHCP hostname when the updated panel obtains a lease. A
manually assigned client alias or cached name can still affect what the
controller displays. The firmware answers standard mDNS discovery requests;
it does not impersonate a UniFi-managed device or implement adoption services.

## DNS requirements and verification

The router/DNS server controls unicast forward and reverse DNS records. The
panel cannot create records on an external DNS server merely by advertising a
DHCP hostname. If the full LAN name does not resolve, check the gateway's DNS
configuration or configure a UniFi Local DNS Record. Clients must use a DNS
server that knows that LAN zone. This feature does not implement DHCP option
119 search lists or change how outbound single-label broker names are resolved;
use the broker's full DNS name in its URI.

1. Flash the firmware, set a hostname, and save/reboot.
2. Check the hostname in UniFi's client details after DHCP lease acquisition.
3. Check **Network names** for the expected DHCP suffix.
4. From a client on the same LAN, open `http://bedroom-panel.local/`.
5. Resolve `bedroom-panel.snoognet.core` using your LAN DNS server and open it.
   mDNS across VLANs requires appropriate multicast forwarding; LAN DNS does not.

For packet-level verification, capture DHCP on the gateway. DISCOVER/REQUEST
should carry option 12 with the bare hostname and request option 15 in option
55. The ACK must supply option 15 for the panel to learn the domain. Also test
reboot, reconnect and lease renewal. These steps require real panel/network
hardware; host tests cannot verify UniFi's live client display.

## Development checks

```sh
python tools/test_color_settings.py
python tools/test_network_identity.py
python tools/test_component_cmake.py
pio run -e waveshare-7c-box
```

The host checks cover hostname persistence/migration/reset, form validation,
DHCP request-list insertion and bounds, chained packet buffers, domain
validation and renewal, and preservation of ESP-IDF's existing DHCP hooks.
The network test uses address/undefined-behavior sanitizers. In a sandbox that
blocks LeakSanitizer's process inspection, use `ASAN_OPTIONS=detect_leaks=0`.

References: [ESP-IDF ESP-NETIF](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/network/esp_netif_programming.html),
[Espressif mDNS](https://docs.espressif.com/projects/esp-protocols/mdns/docs/latest/en/index.html),
[UniFi local DNS](https://help.ui.com/hc/en-us/articles/15179064940439-UniFi-DNS-Records-and-Local-Hostnames).
