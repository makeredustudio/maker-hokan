# google_readonly.py：小智伺服器唯讀查 Google 日曆＋Gmail

學造工作室 · Maker 補完計劃（教學示範，按現狀提供，冇任何保證）

呢個插件畀 xiaozhi-esp32-server 用（https://github.com/xinnan-tech/xiaozhi-esp32-server）。
佢會加三個工具（function calling），等你可以問小智：

- 「今日有咩會？」→ `get_today_events`
- 「今個禮拜有咩約？」→ `get_upcoming_events`
- 「有冇未讀重要郵件？」→ `get_unread_email_summary`

**CoreS3／ESP32 唔會有任何 Google 密碼。** 所有嘢都喺你部電腦嘅伺服器入面。
只用 Python 標準庫，唔使額外 `pip install`。

## 1. 攞日曆「iCal 格式的私人網址」（唯讀）

Google 日曆（電腦版）→ 右上角 ⚙ 設定 → 左邊揀你個日曆 → 「整合日曆」→
複製「iCal 格式的私人網址」（Secret address in iCal format）。

- 只有你自己應該知道呢條網址。唔小心外洩：喺同一頁撳「重設」，舊網址即刻失效。
- 公司／學校帳戶可能被管理員隱藏咗呢個選項。

官方說明：https://support.google.com/calendar/answer/37648

## 2. 整 Gmail「應用程式密碼」

1. Google 帳戶要先開「兩步驗證」。
2. 去 https://myaccount.google.com/apppasswords 建立一個（16 位）。
3. 改 Google 密碼之後，應用程式密碼會自動失效，要重新整過。
4. 唔用嗰陣，喺同一頁撳「移除」。

⚠️ 注意：應用程式密碼可以用 IMAP 存取成個郵箱，唔係真正嘅「唯讀權限」。
呢個插件個 code 只係唯讀（`select(readonly=True)` + `BODY.PEEK`），但條密碼本身權限好大，
一定要只放喺伺服器。公司／學校帳戶、只用安全金鑰或開咗「進階保護」嘅帳戶可能搵唔到呢個選項。

官方說明：https://support.google.com/accounts/answer/185833

## 3. 放插件入伺服器

**源碼方式：** 將 `google_readonly.py` 抄去 `xiaozhi-server/plugins_func/functions/`。

**Docker 方式：** 將檔案放喺 `xiaozhi-server/plugins/google_readonly.py`，
喺 `docker-compose.yml` 嘅 `volumes:` 加一行：

```yaml
      - ./plugins/google_readonly.py:/opt/xiaozhi-esp32-server/plugins_func/functions/google_readonly.py
```

（伺服器會自動載入 `plugins_func/functions` 入面所有檔案；用 volume 掛入去係我哋嘅做法，唔係官方文件寫明。）

## 4. 改 `data/.config.yaml`

```yaml
selected_module:
  Intent: function_call
Intent:
  function_call:
    functions:            # 注意：呢個清單會成個取代預設清單，想保留其他插件要一齊列出
      - get_today_events
      - get_upcoming_events
      - get_unread_email_summary
plugins:
  google_readonly:
    ical_url: "https://calendar.google.com/calendar/ical/你的日曆/private-xxxxxxxx/basic.ics"
    gmail_user: "you@gmail.com"
    gmail_app_password: "xxxx xxxx xxxx xxxx"
    timezone: "Asia/Hong_Kong"
    max_emails: 5
    gmail_query: "in:inbox is:unread"   # 可選，Gmail 搜尋語法
```

然後重啟：`docker compose restart`（或者重新 `python app.py`）。
揀嘅 LLM 要支援 function calling（tools）。

## 5. 單獨測試（唔使開伺服器）

```bash
# 用 Google 公開嘅香港公眾假期日曆試
python3 google_readonly.py --ics "https://calendar.google.com/calendar/ical/en.hong_kong%23holiday%40group.v.calendar.google.com/public/basic.ics" --days 60

# 試自己日曆（私人網址唔好貼上網！）
python3 google_readonly.py --ics "你的私人網址" --days 7

# 試 Gmail
python3 google_readonly.py --gmail-user you@gmail.com --gmail-app-password "xxxx xxxx xxxx xxxx" --limit 3
```

## 限制

- 重複活動只支援簡單規則（每日／每週＋星期／每月／每年、INTERVAL、COUNT、UNTIL、EXDATE、單次修改）。
  複雜規則（例如「每月第二個星期二」）可能唔準。
- iCal 私人網址嘅更新可能有延遲（Google 冇公佈幾耐）。
- 郵件摘要每封最多約 100 字；最多讀 10 封。

## 私隱

- 日曆行程同郵件標題／摘要會傳去你揀嘅 AI 供應商（例如經 OpenRouter）去生成答案。
- 唔好用喺學生或者其他人嘅帳戶。
- 郵件內容可能有人刻意寫入「指示」（prompt injection）；插件已經提醒 AI 唔好跟，但唔可以完全保證。
