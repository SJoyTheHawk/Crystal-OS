# HTTPS Phase 5 4R.1 Host Catalog Evidence

Status: full-source host conversion verified on 2026-10-02. Online delivery and
device resource acceptance remain open.

## Route-stop retry investigation (2026-10-02)

The latest pasted trace contains a successful automatic retry: framework
request 2 (CTB route 10 inbound) timed out before headers on attempt 1 and
returned HTTP 200 with 4,812 bytes on attempt 2. Repeated failures are not a
universal property of the retry loop.

KMB request 6 instead recorded `mbedtls_ssl_setup returned -0x008D` on
attempts 2 and 3. In the installed IDF 6.1 / Mbed TLS 4 headers this is
`PSA_ERROR_INSUFFICIENT_MEMORY` / `MBEDTLS_ERR_SSL_ALLOC_FAILED`; esp-tls
reported its captured code as `141` in the device diagnostic.
Request 5 recorded `-0x7F80` (`MBEDTLS_ERR_SSL_HW_ACCEL_FAILED`) on attempts
1 and 3. The latter identifies a crypto failure, but does not alone establish
which allocation or accelerator failed. Other attempts had real connection
timeouts. All were collapsed into `ESP_ERR_HTTP_CONNECT` by the HTTP client.

The framework already cleans up the client on every attempt and releases a
failed response body before retrying. The fix captures the detailed IDF TLS
error before cleanup, classifies the known TLS allocation error as
`ESP_ERR_NO_MEM` (which is not retried), and logs TLS flags, socket errno, and
largest free PSRAM block. HTTP receive/transmit buffers now use 1,024 bytes
each instead of 4,096/2,048, reducing requested ordinary heap allocations by
4,096 bytes per active framework client. Full response bodies remain in PSRAM.
Framework cumulative heap minima now include every attempt, not just the final
attempt. Current sdkconfig already uses external TLS allocation and disables
hardware AES; startup logs report the settings compiled into the flashed image.

`python3 tools/test_crystal_http_retry.py` passes against the production worker
with mocked IDF transport: timeout then success, allocation rejection without
retry, crypto error then success, exhausted retries, pre-start cancellation,
body cap rejection, cleanup, and cumulative memory minima. The changed C file
also compiles with the existing ESP-IDF 6.1 compile command, with its output
redirected to a temporary directory. No shared build output or device was
touched. Full firmware linking and on-device reliability remain to be checked
by the device owner; these changes do not establish the root cause of every
timeout or allocation failure.

The 2026-10-03 route-browsing trace exercised 16 framework HTTPS requests
after the smoke test: 8 CTB route-stop requests and 8 KMB route-stop requests
are visible. All 16 completed route-stop responses returned HTTP 200 and
the expected 40/37 CTB and 35/22 KMB stop counts. Two KMB first attempts
failed, one with TLS allocation code `141` and one with a connection timeout;
both succeeded on attempt 2. The run reached 17 successes, 2 retries, and 0
failures. No stop-detail request or prepared-catalog request appeared.

The run also showed the new heap fields: the lowest cumulative TLS internal
free value was 8,459 bytes, the lowest largest internal block was 4,096 bytes,
and free PSRAM stayed above 3,548,576 bytes. This supports the retry and
cleanup fix under repeated browsing. It does not test 4R.2 catalog transfer;
the manifest URL was still unset.

The subsequent hosted run confirms the raw GitHub URL is usable: the 2,232-byte
manifest returned HTTP 200 twice, and the KMB transfer allocated 685,021 bytes
(the 685,020-byte artifact plus its body terminator). The user selected a route
while that transfer was reading, which triggered the catalog-owner cancellation;
the transfer ended with `ESP_ERR_INVALID_STATE`, body length zero, and
`validated=0`. This is foreground cancellation evidence, not a successful
catalog synchronization. No Wi-Fi disconnect event appears in the trace. The
route request then encountered a timeout and two TLS handshake failures while
the cancelled transfer was being drained. Repeat the test by staying on the
route list until both providers validate, then separately test route selection
during transfer.

Follow [the device test procedure](crystal-http-phase5-slice4r2-device-test.md)
for the route retry check and the subsequent hosted 4R.2 catalog tests.

## 4R.2 implementation checkpoint

The firmware now has a separate prepared-catalog sync worker. With a configured
HTTPS manifest URL it checks on bootstrap, after network recovery, on explicit
refresh, and once per day while running. It fetches the manifest through
`crystal_http`, compares provider artifact hashes within the running session,
and downloads only changed KMB/CTB artifacts one at a time. It checks the
manifest schema, provider, filename, declared count/size, binary header, and
SHA-256 before writing a provider-specific candidate file. A foreground route
request cancels the background HTTP request; the worker resumes afterward.
Completion ownership is handed to the catalog worker without copying a full
response body. The worker logs TLS heap minima, post-transfer heap, and actual
SPIFFS free space around each candidate save.

`CRYSTAL_BUS_CATALOG_MANIFEST_URL` is an ESP-IDF CMake cache string. It must be
an HTTPS URL ending in `/manifest.json`; provider files must be beside it.
The default is empty, so this checkpoint firmware starts no catalog worker or
online catalog requests. The prepared `.candidate` files are staging only.
Reboot validation, durable active generations, and local lookup from these
files belong to 4R.3 and 4R.4. Candidate hashes are remembered only for the
current boot.

The new transfer path has compiled, but no hosted URL or full-size device trace
exists yet. Its missing/unchanged/changed, cancellation, foreground-priority,
and low-heap exit tests remain open. The prior no-detail-request traces cover
4R.0 firmware, not this new sync worker.

The subsequent device trace confirms this firmware started with the configured
URL empty: `bus_catalog_sync` logged that online sync was disabled. It loaded
a fresh route catalog cache and made eight framework HTTP requests: one smoke
test and seven route-stop mappings. Four mappings succeeded, two KMB requests
failed with `ESP_ERR_HTTP_CONNECT`, and one KMB request was cancelled while
leaving the stop page. CTB route 10 resolved 40 inbound and 37 outbound stops;
KMB route 101 eventually resolved 35 and 22 stops for service types 1 and 2.
No stop-detail or prepared-catalog HTTP request appears in the captured window.
The framework reported TLS minima of 5,587 bytes free internal heap, a 2,560-byte
largest internal block, and 3,548,832 bytes free PSRAM. The largest body was
4,812 bytes. This confirms browsing still avoids metadata fan-out in the new
firmware. It cannot validate a full catalog download or foreground cancellation
of one, because none was configured.

## Converter and format

`tools/bus_catalog/build_stop_catalogs.py` converts saved source JSON or `.json.gz` into
separate `kmb-stops.bsc` and `ctb-stops.bsc` files. The format is version 2,
little-endian, with a fixed header, sorted fixed-size index entries, and one
UTF-8 string pool. IDs retain leading zeroes. The converter rejects malformed
records, overlong IDs, and CTB route references with no matching stop record;
it never truncates a provider catalog.

The checks in `tools/bus_catalog/test_stop_catalogs.py` verify deterministic
output, header/index/pool bounds, provider tags, record counts, SHA-256,
provider-qualified CTB membership, missing-reference rejection, and full-size
regeneration when run with `--full`.

## Provenance

The pinned inputs are compressed under `artifacts/bus_catalog/sources/`. They
were downloaded from the [KMB bulk stop
source](https://data.gov.hk/en-data/dataset/hk-td-tis_21-etakmb/resource/3d6ded6c-ee36-40a0-a6fe-8f40966dff67)
and the [HK Bus Crawling published snapshot](https://github.com/hkbus/hk-bus-crawling).
The latter URL redirects to `https://data.hkbus.app/routeFareList.min.json`.
The immutable source revision for each input is its uncompressed SHA-256 in
`artifacts/bus_catalog/full/manifest.json`. KMB supplied a
`generated_timestamp`; CTB's `Last-Modified` header is recorded separately as
publication metadata and is not treated as upstream data freshness. The full
binary artifacts and manifest are under `artifacts/bus_catalog/full/`; the
root `artifacts/bus_catalog/` files remain small development fixtures.

The published snapshot has 955 CTB-tagged route records. Of these, 952 carry
CTB stop arrays and three shared-route records carry only KMB arrays. Those
three are counted in the manifest. The 2,586 unique CTB stop references all
resolve in `stopList`; no reference was silently dropped.

## Measured Catalogs

| Provider | Source bytes | Records | Artifact bytes | Index bytes | Pool bytes | Longest ID / EN / TC (UTF-8 bytes) |
| --- | ---: | ---: | ---: | ---: | --- | --- |
| KMB fixture | 236 | 2 | 141 | 72 | 53 | 7 / 15 / 9 |
| CTB fixture | 330 | 2 | 127 | 72 | 39 | 6 / 8 / 6 |
| KMB pinned | 1,189,674 | 6,753 | 685,020 | 243,108 | 441,896 | 16 / 50 / 42 |
| CTB pinned | 8,296,208 | 2,586 | 258,706 | 93,096 | 165,594 | 6 / 76 / 59 |

The pinned active catalogs total 943,726 bytes; their indexes total 336,204
bytes if loaded completely into PSRAM. Active catalogs plus one replacement
of the larger KMB artifact require 1,628,746 bytes. Keeping two complete
provider generations plus the 2,232-byte manifest requires 1,889,684 bytes,
leaving 2,304,620 bytes of the 4 MiB partition before route caches, other
files, and SPIFFS overhead. Actual free space and replacement behavior must
be measured on the device; these host numbers alone do not prove a safe save.

`crystal_http` buffers a complete response in memory. The current route catalog
body cap is 512 KiB; the stop-detail cap is 64 KiB. The KMB artifact exceeds
the existing 512 KiB route cap by 160,732 bytes; CTB fits. This is a caller
option rather than a framework-wide limit, but increasing it for a whole-body
catalog transfer requires a measured PSRAM and internal-heap peak. A bounded
streaming/file-sink transport may be necessary before 4R.2.

An instrumented device run must record free internal heap, largest internal
block, free PSRAM, TLS minimums, response body peak, parser/index peak, and
post-release values for the full transfer. No such full-catalog device run
exists yet.

Reproduce the full host check with:

```sh
python3 tools/bus_catalog/test_stop_catalogs.py --full
```

## Device trace: CTB route 10

The pasted device trace from 2026-10-02 shows one HTTPS smoke request and two
CTB route-stop requests (inbound and outbound). The route-stop responses each
returned HTTP 200 and parsed 40 and 37 rows. No stop-detail request appears
between Wi-Fi connection and completion of both browses. This verifies the
4R.0 browsing path for the two CTB directions shown, not all providers or
reconnect scenarios. The device still loaded the preserved v1 partial catalog
with 11 records and 27 pending entries; it made no corresponding detail fetch
in this trace.

Across these three HTTPS requests, logged TLS minima were 6,915 bytes free
internal heap, a 2,048-byte largest internal block, and 3,527,796 bytes free
PSRAM. The largest response body was 4,812 bytes. These are a baseline for
small route-stop responses, not a peak measurement of full catalog transfer,
parsing, or replacement. The bus app also logged a 5,500 ms `onCreate` against
its 80 ms budget; that startup observation needs separate investigation.

A second pasted device trace includes a fresh route-catalog load and browsing
for both providers. KMB and CTB route-catalog responses were 350,950 and
112,270 bytes. CTB route 10 inbound resolved 40 stops. Its first outbound
request failed after three connection timeouts; a later user selection resolved
37 stops. KMB route 101 service type 1 failed once after DNS and TLS setup
errors, then a later selection resolved 35 stops after one handshake retry.
Service type 2 resolved 22 stops. Of nine framework requests, one was the
smoke test, two were route catalogs, and six were route-stop mappings; zero
stop-detail requests appear in the captured window. This extends the 4R.0
browsing evidence to both providers, but no Wi-Fi disconnect/reconnect cycle
is shown.

The second trace reports TLS minima of 4,531 bytes free internal heap, a
2,432-byte largest internal block, and 3,068,084 bytes free PSRAM. These
figures show narrow internal-heap margin during ordinary browsing; they do
not establish the peak cost of a prepared stop-catalog transfer. The observed
connection failures are route-stop transport failures, not metadata fan-out.

## Hosting and refresh dependency

No artifact hosting URL, scheduled generator, owner, or refresh cadence is
configured in this repository. Pinned local full-size artifacts validate the
converter and host budget; online update support is unresolved until a
publication location and scheduled source-pinning job are selected and verified.

An idle hosted run then completed the full transfer on the earlier 4R.2
firmware: KMB received 685,020 bytes and CTB received 258,706 bytes, both with
`validated=1`. The measured SPIFFS partition reported 3,848,081 bytes total;
free space fell from 2,786,602 to 2,095,599 bytes after the KMB candidate and
from 2,095,599 to 1,834,559 bytes after the CTB candidate. The lowest TLS
internal free value was 5,783 bytes and the lowest largest internal block was
1,280 bytes; free PSRAM stayed above 3,608,260 bytes. This establishes the
full-size transfer and candidate-save baseline. It does not cover unchanged
manifests, one-provider changes, Wi-Fi recovery, or the latest retry diagnostic
edit.
