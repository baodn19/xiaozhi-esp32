# Plan: HTTPS for the LotusAI recipe request transport

## Context
The study protocol requires the recipe request path (device -> LotusAI server) to stop using plain LAN HTTP before enrollment. Recipe fields (ingredients, age, conditions, allergies) currently travel unencrypted. Goal: the device calls the recipe server over HTTPS with certificate verification, and no build used in the study can fall back to `http://`.

## Current state (verified in repo)
- `main/Kconfig.projbuild:1012` `LOTUSAI_BASE_URL` defaults to `https://lotusfoodasmedicine.com`; help text recommends `http://192.168.x.x:8000` for local dev.
- `sdkconfig:813` overrides it: `CONFIG_LOTUSAI_BASE_URL="http://10.253.106.97:8006"` (the audited build used a LAN `http://` address; `192.168.50.51` appears only in `monitor.log`).
- `main/boards/common/lotusai_controller.h:42` `HttpPost` uses `board.GetNetwork()->CreateHttp(0)`; `:228` builds `CONFIG_LOTUSAI_BASE_URL + "/api/xiaozhi/recommend"` (the select call uses the same base).
- TLS support exists: `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y` (full public CA bundle); `managed_components/78__esp-ml307/src/esp/esp_ssl.cc:35` attaches `esp_crt_bundle_attach`. `CONFIG_ESP_TLS_INSECURE` is off.
- The server (`food_as_medicine`, FastAPI/uvicorn) has no TLS config; the compose file only maps a port.

## Deployment fact and certificate choice
The study server is `uvicorn` on the researcher's laptop, on the same Wi-Fi as the robot and the participant phone. So the server is a LAN host, not a public one. `localhost` is not reachable from the robot, so the device must use the laptop's LAN address (currently `10.253.106.97`, which can change with DHCP). A public CA will not issue a certificate for a bare private IP. Two workable routes:

- **A (recommended): a name you own that points at the laptop's LAN IP, with a public certificate via DNS-01.**
  - Create a DNS record, e.g. `study.lotusfoodasmedicine.com -> <laptop LAN IP>` (README lists `lotusfoodasmedicine.com` as the project domain; confirm you control its DNS). A private-IP A record is fine; it only resolves usefully on this Wi-Fi.
  - Get a Let's Encrypt certificate with the DNS-01 challenge (certbot DNS plugin or Caddy with a DNS provider module). DNS-01 needs no inbound access to the laptop, so nothing is exposed to the internet.
  - Robot and phones trust it with no setup: the existing CA bundle covers the robot (no firmware cert code), and phones open the QR link with no warning.
  - Cost: re-point the DNS record if the laptop IP changes (use a DHCP reservation or a hotspot with a fixed address), and renew every ~60 days.
  - Check that the Wi-Fi's DNS resolver does not block private-IP answers (DNS rebinding protection); if it does, use route B.
- **B (no domain): local CA (e.g. `mkcert`) with an IP/`.local` name.**
  - Embed and pin the CA in firmware (step 4b); install the CA on each participant phone, or accept a browser warning on the QR link. A participant-phone CA install is a poor fit for the study, so use only if A is impossible.
  - Never disable certificate verification.

Steps below assume route A; 4b applies only to route B.

## Study network (ASUS GT-AX11000 Pro)
The lab router carries the laptop, robot, and participant phone; upstream is Ethernet or the iPhone hotspot. This settles the address problem and adds a few things to configure and test.

- **Fixed addresses:** DHCP reservations for the laptop and robot make the DNS record stable. Create the record (`study.<domain> -> laptop reserved IP`) after choosing the reservation, and use the router's LAN subnet (ASUS default is `192.168.50.0/24`, which may be why the audit showed `192.168.50.51`), not the old `10.253.106.97` in `sdkconfig`.
- **Same-LAN reachability:** put all three devices on the main SSID (not Guest), and make sure client/AP isolation is off, or the phone and robot cannot reach the laptop.
- **DNS:** phones and robot must resolve the study name through the router. In the router's WAN settings, turn off "DNS Rebind protection" if it is on (it can block a public name that answers with a private IP). If it still fails, set a router-side static name (dnsmasq `address=/study.<domain>/<ip>` where available) or fall back to route B.
- **Upstream:** the robot needs internet for XiaoZhi and SNTP (certificate validation needs a correct clock). With the iPhone hotspot as upstream, participants' phones join the router's Wi-Fi, not the hotspot, so the QR link is reached over the LAN. Do not rely on cellular data on the participant phone for the QR link (it can't reach a private IP).
- **Certificate timing:** issue/renew the DNS-01 certificate on the laptop with internet before the session (Let's Encrypt certs last 90 days), and keep the cert and key on the laptop only, not in the repo. Caddy needs internet only for renewal, not to serve.
- **Firewall:** on the laptop allow inbound only on the TLS port from the LAN; on the router do not port-forward the study server and do not enable DDNS or remote access to it.

## Steps

### 1. Server: terminate TLS (deployment, no app-code change)
- Run the laptop server behind a reverse proxy (Caddy is simplest: it can obtain and renew the DNS-01 certificate itself), listening on 443 (or 8443) on the LAN interface and proxying to uvicorn on 127.0.0.1; or run `uvicorn --ssl-keyfile ... --ssl-certfile ... --host 0.0.0.0`.
- Do not expose the plain-HTTP app port on the LAN (bind uvicorn to 127.0.0.1; allow the laptop firewall only on the TLS port).
- Files: `food_as_medicine/docker-compose.yml` (proxy service / port mapping), proxy config (new, e.g. `food_as_medicine/deploy/`).
- Set `PUBLIC_BASE_URL=https://<host>` in `.env` (see `.env.example`) so the QR/PDF URLs returned by `routers/xiaozhi.py:_base_url` are https too.
- If behind a proxy, run uvicorn with `--proxy-headers` so `request.base_url` reports https when `PUBLIC_BASE_URL` is unset.

### 2. Firmware config: point at the HTTPS URL
- Set `CONFIG_LOTUSAI_BASE_URL="https://<host>[:port]"` in `sdkconfig` and add the same value to `sdkconfig.defaults` so a clean build does not regress to http.
- Update `main/Kconfig.projbuild:1012` help text: remove the `http://192.168.x.x` suggestion (or mark it dev-only, never for study builds).

### 3. Firmware guard: refuse non-HTTPS
- In `lotusai_controller.h` (near `:23` and `HttpPost` at `:42`) add a compile-time or startup check that `CONFIG_LOTUSAI_BASE_URL` starts with `https://`; use `static_assert` on the macro prefix or a runtime check in `HttpPost` that logs an error and returns empty for `http://` URLs. Add a Kconfig bool (e.g. `LOTUSAI_ALLOW_INSECURE_HTTP`, default n) so dev builds can opt in explicitly.

### 4. Certificate validation on the device
- **4a (option A):** nothing to change; confirm `CreateHttp(0)` goes through the esp-ml307 / esp-tls path using `esp_crt_bundle_attach`. Verify by test (below) that an invalid cert is rejected.
- **4b (option B):** embed the CA PEM (`EMBED_TXTFILES` in `main/CMakeLists.txt`) and pass it to the HTTP/TLS client in `HttpPost` (the network layer's CA setter, or `esp_tls_cfg_t.cacert_buf`). If the esp-ml307 `CreateHttp` interface has no CA hook, add one in the board network wrapper rather than turning off verification. Check clock: cert validation needs correct time (SNTP already done before network use; confirm).

### 5. Select call and QR URL
- Confirm `lotusai.select` and any other call using `CONFIG_LOTUSAI_BASE_URL` get https automatically (same base). Confirm the QR URL returned by the server is `https://`, and that participant phones trust the cert (option A is simplest; option B needs the CA on phones, or the protocol must note a browser warning, so prefer A for the QR link).

### 6. Logging
- Do not log request bodies (the commented-out `recommend body` log at `lotusai_controller.h:212` stays disabled).

## Verification
1. Build firmware (`idf.py build`), flash, run a recipe request; confirm `monitor.log`/serial shows success and the server access log shows TLS requests.
2. Packet capture on the LAN (Wireshark filter `ip.addr==<server>`): no cleartext HTTP; only TLS records on the chosen port. Confirm `http://<server>:8006` no longer answers from the LAN.
3. Negative tests: temporarily serve an expired/wrong-host cert (or wrong CA for option B) and confirm the device shows "Could not reach LotusAI" rather than connecting.
4. Build with an `http://` base URL and confirm the guard in step 3 rejects it.
5. On the study router, with each upstream (Ethernet and iPhone hotspot): robot resolves and reaches `https://study.<domain>`, an iPhone on the router's Wi-Fi opens the QR link with no warning, and the robot still reaches XiaoZhi. Repeat after rebooting the router and laptop to confirm the reservations hold.
6. Check the QR URL scheme returned by `POST /api/xiaozhi/select` and that a phone opens it without a warning.

## Follow-up (outside this plan)
- Share-link access control (`routers/pdf_share.py`, `routers/xiaozhi.py`) is a separate change.
- After tests pass, update the protocol's recipe-processing paragraph and the Data Collection Form text that still describes plain HTTP / login-free links as current behavior.
