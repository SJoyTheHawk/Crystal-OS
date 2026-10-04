# TLS AES internal-memory failure follow-up

Status: configuration fix implemented; device verification pending.

The repeated CTB stop-request failures include `esp-aes: Failed to allocate
memory`, TLS read error `-0x0084`, and handshake hardware-acceleration error
`-0x7F80` (as defined by the installed ESP-IDF v6.1 TLS headers).
The samples show roughly 8 KiB of internal free memory and smaller largest
blocks while PSRAM remains plentiful.

TLS uses external memory allocation, but the ESP-IDF v6.1 hardware AES path
still stages external buffers through internal DMA memory, using chunks up to
1600 bytes on ESP32-S3, potentially for both input and output, plus DMA
descriptors. Total internal free memory does not guarantee these DMA-capable
allocations will succeed. Set
`CONFIG_MBEDTLS_HARDWARE_AES=n` in the tracked defaults and generated local
configuration so software AES avoids those staging allocations. TLS buffers
remain in PSRAM and certificate verification is unchanged.

Software AES may increase CPU use and transfer time. Other network timeouts
can still occur, so device verification is required.

## Device acceptance

Flash the rebuilt firmware and repeat CTB route 10 stop requests in both
directions, including cancellations and a reboot. Confirm successful stop
responses and no `esp-aes` or TLS allocation errors. Repeat one KMB request and
check UI responsiveness and CPU use. If failures remain, capture the complete
log with heap samples; timeout-only failures need separate investigation.
