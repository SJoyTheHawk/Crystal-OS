# Slice 4 Stop Catalog Sources and Fixtures

**Status (2026-10-02):** Revised for [4R](crystal-http-phase5-slice4-stop-picker-plan.md).
The existing individual-detail fixtures remain parser evidence; they do not
specify the new device download workflow. Full catalog fixtures and measured
limits are required in 4R.1 and are not yet implemented.

## Sources

| Input | Use |
| --- | --- |
| `https://data.etabus.gov.hk/v1/transport/kmb/stop` | Official bulk KMB directory; host converter input |
| `https://hkbus.github.io/hk-bus-crawling/routeFareList.min.json` | Prepared multi-operator snapshot; host extracts CTB references and names |
| `https://data.etabus.gov.hk/v1/transport/kmb/stop/{stop_id}` | Historical detail-parser fixtures only |
| `https://rt.data.gov.hk/v2/transport/citybus/stop/{stop_id}` | Historical detail-parser fixtures / upstream crawler; not device acquisition |

**Correction:** the previous claim that neither operator exposed a bulk stop
resource was wrong for KMB. Its [official documentation](https://data.gov.hk/en-data/dataset/hk-td-tis_21-etakmb/resource/3d6ded6c-ee36-40a0-a6fe-8f40966dff67)
explicitly describes all-stop download. HK Bus Crawling's
[CTB adapter](https://github.com/hkbus/hk-bus-crawling/blob/master/crawling/ctb.py)
assembles individual CTB records off-device. This does not establish that a
CTB official bulk endpoint exists or cannot exist.

Record snapshot revision, retrieval time, source timestamp when supplied,
SHA-256, operator membership rule, counts and attribution. Existing fixtures
without capture provenance must be labelled synthetic/unverified, not treated
as proof of current live API behaviour. Do not assume the unrelated GeoJSON
`features` shape is the KMB endpoint's bulk contract.

## Retained parser cases

- `kmb_stop_detail_valid.json`
- `kmb_stop_detail_missing_tc_invalid_coords.json`
- `ctb_stop_detail_valid.json`
- `ctb_stop_detail_missing_en.json`
- `stop_detail_empty.json`
- `stop_detail_duplicate_ids.json`
- `stop_detail_provider_qualified_ids.json`

These cover copied fields, missing languages, coordinates and qualified keys.
Duplicate behavior in a one-record parser does not authorize duplicate keys in
a published catalog; the full catalog validator must reject ambiguous input.

## Required revision fixtures

Add small synthetic cases plus a reproducible full-size source run:

- KMB bulk envelope and CTB snapshot membership, including shared route numbers;
- CTB reference missing from stopList, leading zeroes and unexpected schema;
- equal textual IDs in separate providers; duplicate keys within one provider;
- missing EN/TC, long UTF-8 names, optional/non-finite coordinates;
- deterministic compact round-trip and manifest hashes/counts;
- invalid offsets, overflow, length, checksum, provider and schema;
- interrupted A/B generation writes and insufficient filesystem space;
- stale/unknown source age and independent provider failures;
- unknown route-stop ID returns local miss without network work.

The code guide defines the acceptance gates. Do not use a successful single
CTB lookup to sign off both full provider catalogs.
