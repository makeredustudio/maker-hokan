# 貢獻指南 / Contributing

多謝你幫 Maker 補完計劃變得更完整！

## 點樣加入新專案

1. Fork 本 repo（或喺 `makeredustudio` 開 branch）
2. 喺 `projects/` 底下建立一個資料夾，建議用小寫 kebab-case：
   - 例：`projects/my-cool-gadget/`
3. 資料夾最少要有：
   - `README.md`（中文為主，可加短英文）— 說明做咩、需要咩零件、點樣開始
   - 原始碼／圖紙／BOM／相片等你自己創作嘅內容
4. **唔好**上載：
   - 完整商業圖書 PDF、掃描頁
   - 私人金鑰、Wi‑Fi 密碼、API key
   - 「私人」Drive 資料夾入面嘅內容
5. 開 Pull Request，簡述你嘅專案同測試過嘅硬件

## 資料夾結構建議

```
projects/<project-slug>/
  README.md
  src/           # 或 .ino / .js 直接放根目錄
  docs/          # 說明、簡報
  hardware/      # BOM、原理圖、3D
  media/         # 相片、截圖
```

## 授權

提交即表示你同意程式碼以 MIT、文件以 CC BY 4.0 授權（見 `LICENSE`），除非你喺 README 另有註明。

## 問題

電郵：larryyee@makeredu.studio  
網站：[https://makeredu.studio](https://makeredu.studio)
