# Security policy

This is a hobby project provided without warranty (see [`LICENSE`](LICENSE)). It
is a desk sign: it reads a public transit API over HTTPS and draws the result on
a screen. It controls nothing, and it is not safety equipment — **do not rely on
it to catch a train you cannot afford to miss.**

## Reporting a vulnerability

Please **do not open a public issue** for a security problem. Use GitHub's
private vulnerability reporting instead, which is enabled on this repository:

**[Report a vulnerability](https://github.com/cheeseprince/caltrain-notifier/security/advisories/new)**
(the same form is linked from the repo's **Security** tab → **Report a
vulnerability**). See GitHub's own docs on
[privately reporting a security vulnerability](https://docs.github.com/en/code-security/security-advisories/guidance-on-reporting-and-writing/privately-reporting-a-security-vulnerability)
if you want to know what that form does with your report before using it.

That routes it privately to the maintainer. Include what the issue is, how to
reproduce it, and the impact you see. There is no formal SLA on a hobby project,
but reports are appreciated and will be looked at.

## Scope

The interesting surfaces, roughly in order of how much they are worth looking at:

| Surface | Why it matters |
| :--- | :--- |
| **The 511 API token** | A per-user credential with a 60 requests/hour rate limit. Anything that leaks it out of NVS — into a log line, a rendered screen, a portal response, or a compiled binary — is in scope. |
| **The setup portal** | A SoftAP captive portal that accepts the WiFi password and the token over plain HTTP on `192.168.4.1`. It is CSRF-guarded and only runs when the device is unconfigured or `BOOT` is held at power-on, but it is the one place secrets are typed in. |
| **JSON parsing of a remote feed** | `siri_parse.cpp` consumes whatever `api.511.org` returns. Malformed or hostile input reaching a crash or an overflow is in scope; the host tests under `test/` are the place to add a reproducing case. |
| **TLS** | Certificates are validated against the Mozilla root bundle. A regression to `setInsecure()`, or any path that skips validation, is a real finding. |
| **The OTA release channel** | The one path that can put arbitrary code on the device unattended. Its trust model is deliberately different from the row above — see the next section before flagging `setInsecure()` there as a regression. |

## Over-the-air update trust model

The sign updates itself daily by fetching a small manifest and, when it
names a newer release for this board revision, a firmware image — both over
plain WiFi, with no human present to click "allow". That combination is
exactly the shape of a silent remote-code-execution path if the channel
cannot prove who published what it just downloaded, so the design puts every
bit of trust in one place and states it plainly here rather than leaving it
implicit in the code.

- **TLS uses `setInsecure()` — deliberately, not by omission.** The OTA
  fetches skip certificate validation entirely (`src/ota_task.cpp`,
  `fetchCapped()`). That is safe here only because certificate validation is
  not what makes this channel trustworthy: authenticity comes from the
  signature below, checked independently of the transport. TLS is doing one
  job — keeping the transfer private — not the job of proving who sent it.
- **`manifest.txt` must carry a detached ECDSA-P256/SHA-256 signature**,
  checked against the public key compiled into the running firmware
  (`src/ota_pubkey.h`, verified in `src/ota_verify.cpp` via mbedtls). This is
  checked **before** a single field of the manifest is read for any decision
  — see the ordering comment in `runAttempt()` in `src/ota_task.cpp`. An
  absent, malformed, or invalid signature is refused outright.
  **There is no transition mode and no unsigned fallback.** A sibling
  project (`obd-gauge-cluster`) ships an opt-in mode where an empty key
  disables checking, safe there only because a human triggers each update.
  This device updates unattended, so the same setting here would turn an
  empty or misconfigured key into a silent way to run arbitrary code.
- **The firmware image itself is checked against a SHA-256 in the signed
  manifest** before the new slot is activated (`src/ota_task.cpp`, step 6).
  The signature proves the manifest is ours; the hash proves the bytes that
  landed in flash are the ones the manifest actually named. Neither
  substitutes for the other — a valid signature over a manifest naming the
  wrong hash, or a hash match against an unsigned manifest, are both refused.
- **Signing and publishing both refuse to run without a key**, rather than
  degrade to unsigned — see `.github/workflows/release.yml` and
  `tools/publish_ota.sh`. A release workflow that "succeeds" by silently
  skipping the signature would look identical to a healthy one until a
  device stops updating with nothing on screen to explain why.
- **Replacing the public key strands every already-deployed unit.** A device
  trusts only the key baked into the build it is currently running
  (`src/ota_pubkey.h`), so a key rotation is invisible to OTA itself — the
  next release, signed with the new private key, simply fails every
  in-field device's signature check and is refused. Recovering those units
  requires a USB re-flash apiece. This is also why the private key's
  off-machine backup matters more than most: losing it does not just block
  new releases, it makes every future release un-installable by anything
  already in the field until it is physically re-flashed.

## Credential handling, by design

- **The 511 token is never compiled in.** It is entered once through the setup
  portal and stored in NVS. There is no token in this repository, in any commit
  in its history, or in any binary this project produces.
- **The WiFi password is likewise NVS-only** and is never rendered or logged.
- The AP password is generated per device from `esp_random()` and persisted, so
  it neither ships in the source nor rotates on every boot (which would strand a
  phone that had saved the network).

## Known limitations, stated rather than implied

- **The setup portal is HTTP, not HTTPS.** A self-signed certificate on
  `192.168.4.1` trains users to click through warnings, which is worse. The
  window is short and requires RF proximity, but a passive listener within range
  during setup can see the WiFi password and the token.
- **NVS is not encrypted** in this build. Physical access to the flash yields
  both stored credentials.
