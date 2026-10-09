# Sync notes — Maker 補完計劃 → GitHub mirror

- **Synced at**: 2026-10-09 (Asia/Hong_Kong, HKT)
- **Source**: Google Drive shared — Maker 補完計劃 `17-Hh51skvXsJTFWVWdSREmgHveXg7S6A` → **公開** `1Vrk05EocTjxtZ6tcf-KNzUMTRz5U6b0L`
- **Not synced**: 「私人」folder (untouched)
- **Local path**: `/workspace/github/maker-hokan`
- **Suggested GitHub repo name**: `maker-hokan` (alt: `Maker-Complement-Plan`)
- **Suggested owner**: `makeredustudio`
- **Status**: Local git initialized; **no remote / no push** (auth not ready)

## Downloaded (summary)

| Drive folder | Local path | Notes |
|--------------|------------|-------|
| ESP32 AI 語音助手 | `projects/esp32-ai-voice-assistant/` | Slides, scripts, full PCBA tree |
| ESP32 LCD CYD | `projects/esp32-lcd-cyd/` | Examples, self-learning PPTX, other, Larry + 14 FB ideas, libraries.zip |
| Micro bit 爆旋陀螺… | `projects/microbit-beyblade-arena/` | 2× JS |
| Muse AI × ESP32 | `projects/muse-ai-esp32/` | 1× PPTX |
| Watchy by SQFMI | `projects/watchy-sqfmi/` | PPTX, PDF, links docx |
| Make: Tinkering | `projects/make-tinkering/` | README only (book photos skipped) |
| Intro doc | content folded into root `README.md` | Doc id `1VZUQnYPOHC50uI8tmb2RYcznOVXvglNkAs9w8Ge235o` |

## Skipped

See [`SKIPPED.md`](SKIPPED.md).

## After GitHub auth

```bash
cd /workspace/github/maker-hokan
# optional: git lfs track "*.zip" "projects/esp32-lcd-cyd/libraries.zip"
gh repo create makeredustudio/maker-hokan --public --source=. --remote=origin
git push -u origin main
```

Then update company website Maker 補完計劃 page to link:  
`https://github.com/makeredustudio/maker-hokan`
