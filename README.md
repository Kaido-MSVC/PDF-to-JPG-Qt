# PDF to JPG_QT v1.1

Windows x64、C++20、Qt Widgets / Qt PDF、MSVC。啟動 `dist/PDF-to-JPG_QT.exe`；移動時請帶整個 dist 資料夾。

## 系統需求

- Windows 10/11 x64。
- 需先安裝 **Microsoft Visual C++ 可轉散發套件（x64）的最新版本**。即使電腦已安裝舊版，也請更新到最新版，否則程式可能無法啟動或異常結束。本程式不附帶這些執行階段 DLL。
- 下載：Microsoft 官方「最新支援的 Visual C++ 可轉散發套件」頁面
  https://learn.microsoft.com/zh-tw/cpp/windows/latest-supported-vc-redist ，x64 版的永久連結為
  https://aka.ms/vc14/vc_redist.x64.exe 。
- 若啟動時出現找不到 `vcruntime140.dll`、`msvcp140.dll`（或 `vcruntime140_1.dll`、`msvcp140_1.dll` 等）的錯誤，就是尚未安裝此套件，安裝後再啟動即可。

加入 PDF 或資料夾，選擇輸出路徑、150/200/300/600 DPI、JPEG 品質、1–8 條編碼執行緒與影像預算。預設 200 DPI、品質 92、512 MiB。偏好設定由 QSettings 保存。

續傳每次啟動預設關閉，不保存勾選狀態。關閉時，每份 PDF 建立新的來源名稱資料夾，重名自動加 `(2)`、`(3)`。

續傳操作：只加入一個 PDF，勾選「中斷續傳」，按「選擇既有批次…」選取該 PDF 上次由 v1.1 建立的輸出子資料夾，再按開始。多份 PDF 請逐一選取各自的批次資料夾續傳。設定需與上次相同；輸出資料夾欄位在續傳時僅供存放本次結果報告，JPG 寫回所選批次。

每個 v1.1 新批次都保存 `.pdf2jpg-batch.json` 身分紀錄；每頁成功發布後保存 `JPG檔名.done.json`。核對完整 PDF SHA-256、檔名、頁數、DPI、品質、Qt 版本與固定輸出選項；僅在 JPG 雜湊、憑證、完整解碼和預期尺寸均匹配時略過。執行緒數與影像預算不影響輸出身分，可以調整，但驗證仍需要足夠影像預算。

PDF 在處理期間以 Windows 分享模式禁止寫入／刪除；批次持有 Windows 獨占鎖。`.pdf2jpg.lock` 是保留的鎖檔，程序結束或崩潰後由作業系統釋放持有權，不用刪檔或依時間猜測是否過期。同批次同時使用時拒絕後來的程序。

身分不符、缺失／損壞 JPG 或憑證、未知檔案或殘留暫存檔會在輸出新頁面前拒絕續傳，保留原檔。JPG 與憑證都不存在才視為未完成頁面。若崩潰發生在 JPG 發布後、憑證發布前，會保守拒絕，不自動認領、刪除或覆寫該 JPG；可關閉續傳重新建立一批。v1.0 沒有身分與完成憑證，無法安全匯入續傳。

JPEG 在目的資料夾內寫入唯一暫存檔，成功後用 `QTemporaryFile::rename` 原子發布；目的檔已存在時失敗，不覆寫、不改成 copy/delete。失敗時清除自己的暫存檔，保留別人的檔案。

每頁輸出為白底 RGB JPEG，保留 DPI。密碼最多重試 3 次；取消密碼輸入略過此檔。取消批次或關閉主視窗時，停止安排新頁面並等待已開始的渲染與寫入收尾，保留已完成頁面。每批產生 `batch-UUID.json` 紀錄。開檔或頁面失敗會列出原因。

PDF 由單一背景執行緒渲染，JPEG 由多條工作執行緒編碼。影像預算包括渲染中、等候中及編碼中的影像，以 `寬×高×12+8 MiB` 保留額限流；同時工作數上限為編碼執行緒數的兩倍。單頁超出預算或 8,000 萬像素會拒絕。

## 建置與驗收

```powershell
.\build.ps1                  # 正式 Release：build、dist，沒有診斷命令與故障注入
.\build.ps1 -Testing         # 驗收 Release：build-test、dist-test，含 engine-tests
python .\verify.py          # 真實 PDF/JPEG、GUI 自動驗收、故障注入與壓力測試
python .\verify_deployment.py # 正式版離開 Qt PATH 的啟動與 DLL 來源檢查
```

本機 Qt 6.12.0 `D:/qt/6.12.0/msvc2022_64`、Visual Studio 2026 Community / MSVC x64。建置工具環境只在子程序內設定；未改全域 PATH 或系統設定。dist 不附帶 MSVC 執行階段 DLL（windeployqt 使用 `--no-compiler-runtime`），執行時使用系統安裝的 Visual C++ 可轉散發套件，見「系統需求」。

驗收使用本機既有 PyMuPDF 與 Pillow，不安裝套件；程式執行不需要 Python。測試設定使用執行檔旁的 INI，避免覆蓋正式版偏好。測試資料在 work；所有密碼為合成測試資料。

## 限制

- Qt PDF 渲染及 JPEG／磁碟 I/O 無法安全強制中斷，取消及關機仍需等待目前呼叫返回。
- 影像預算不是程序記憶體硬上限，PDFium、字型、Qt、解碼器與快取另計。
- 記憶體耗盡時會嘗試回報最小錯誤；若連回報所需的分配也失敗，GUI 會提示未收到結果。無法復原原生 PDFium 崩潰或系統強制終止。
- 加密 PDF 頁數在解密後加入總數，百分比可能降低；預掃描為抽樣，實際每頁仍會檢查尺寸。
- 大資料夾遞迴加入仍在 GUI 執行緒，可能短暫停頓。未擴充新功能或改成新架構。
- 本機 NTFS 與長路徑已測試；網路磁碟、其他檔案系統、真正磁碟耗盡與 Windows 登出未實測。

另可執行 `python tests/verify_resume.py` 重跑續傳針對性驗收。詳見 VERIFICATION.md。


## 授權

本程式以 [MIT 授權](LICENSE) 釋出，Copyright © 2026 Kaido-MSVC。程式圖示由 AI（ChatGPT）生成。

本程式使用 Qt 6.12.0 與 Qt PDF 6.140.0（隨 Qt 6.12.0 發佈；內含 PDFium），Qt 依 GNU LGPLv3 以動態連結方式使用；Qt DLL 隨程式放在 dist 資料夾，可自行替換為相容版本。Qt 原始碼可由 https://download.qt.io/ 或 https://code.qt.io/ 取得。

第三方元件清單、版本與授權見 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)，授權全文在 `licenses/`。程式內「關於」→「第三方授權」／「關於 Qt」也可查看。發佈的 dist 資料夾會一併附上 LICENSE、THIRD_PARTY_NOTICES.md 與 licenses/。
