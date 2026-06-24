# configs/

Static AOSP-derived assets that the deb's `:cvd` doesn't ship but
cvd_host_package needs.  Checked in verbatim from a recent
`aosp_cf_riscv64_phone` cvd_host_package.

| File | AOSP source |
|---|---|
| `cvd_avb_testkey_rsa*.pem` | `external/avb/test/data/` |
| `cvd_rsa*.avbpubkey` | derived via `avbtool extract_public_key` |
| `cvd_config/cvd_config_*.json` | `device/google/cuttlefish/shared/config/` |

To refresh: diff against a fresh `aosp_cf_riscv64_phone`
cvd_host_package's `etc/` files and update changed entries.  All files
are Apache-2.0 / AOSP test data.
