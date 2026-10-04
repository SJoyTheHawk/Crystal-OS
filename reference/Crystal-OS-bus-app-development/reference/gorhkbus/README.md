# GoR HK Bus source snapshot

`index.html` is the complete client-side source served by
<https://gorhkbus.netlify.app/> on 2026-09-11. The deployment is a single HTML
file containing its CSS and JavaScript; it has no application bundles or source
maps.

- Retrieved size: 523,197 bytes
- Gzip-compressed size: 355,736 bytes
- Embedded WebP payloads: 329,708 decoded bytes (two theme banners)
- SHA-256: `0397e88704d8cf31affa0584b8d5430be04db27d063668474b3afe26627cafed`
- External runtime dependency: Google Fonts
- Data sources: KMB/LWB and Citybus public JSON APIs

The final Netlify HUD script and hosting metadata in `index.html` are part of
the response served by Netlify, not app logic. No repository URL, build files,
commit history, or license declaration was exposed by the deployment. Confirm
redistribution rights before treating this snapshot as third-party licensed
source.

See [`../../docs/HK_BUS_APP_EVALUATION.md`](../../docs/HK_BUS_APP_EVALUATION.md)
for the Crystal OS port and size evaluation.
