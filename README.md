# PDF to JPG_QT v1.1.1 — Windows x64

離線 PDF 批次轉 JPG 桌面工具，採 C++23、Qt Widgets / Qt PDF、MSVC。作者與發布者：**Kaido-MSVC**。

Qt 版支援單份 PDF 中斷續傳，需要 Visual C++ x64 執行階段，專案程式碼採 MIT。另有 [PDF to JPG Python 版](https://github.com/Kaido-MSVC/PDF-to-JPG-Python/releases/tag/v1.0.0)，提供單一 EXE，專案採 AGPL-3.0-or-later。

## 下載與使用

正式下載見 [GitHub Release v1.1.1](https://github.com/Kaido-MSVC/PDF-to-JPG-Qt/releases/tag/v1.1.1)。

- [免安裝版 Portable ZIP](https://github.com/Kaido-MSVC/PDF-to-JPG-Qt/releases/download/v1.1.1/PDF-to-JPG-Qt-1.1.1-Windows-x64-Portable.zip)。請完整解壓後執行 `PDF-to-JPG-Qt-1.1.1-Portable/PDF-to-JPG_QT.exe`。
- [SHA256SUMS.txt](https://github.com/Kaido-MSVC/PDF-to-JPG-Qt/releases/download/v1.1.1/SHA256SUMS.txt) 提供正式 ZIP 的 SHA-256。
- 自行建置後執行 `dist/PDF-to-JPG_QT.exe`。已發布版本的對應原始碼見各版本的 Release。

請保留完整資料夾、Qt DLL、外掛與授權文件，不要只複製 EXE。Qt 已隨附；Microsoft Visual C++ 可轉散發套件需另行安裝，見下方系統需求。執行檔未數位簽章。


## 系統需求

- Windows 10 版本 1809 以上，或 Windows 11（x64）。此為 Qt 6.12 官方支援的最低版本（https://doc.qt.io/qt-6/supported-platforms.html ）。
- 需先安裝 **Microsoft Visual C++ 可轉散發套件（x64）的最新版本**。即使電腦已安裝舊版，也請更新到最新版，否則程式可能無法啟動或異常結束。本程式不附帶這些執行階段 DLL。
- 下載：Microsoft 官方「最新支援的 Visual C++ 可轉散發套件」頁面
  https://learn.microsoft.com/zh-tw/cpp/windows/latest-supported-vc-redist ，x64 版的永久連結為
  https://aka.ms/vc14/vc_redist.x64.exe 。
- 若啟動時出現找不到 `vcruntime140.dll`、`msvcp140.dll`（或 `vcruntime140_1.dll`、`msvcp140_1.dll` 等）的錯誤，就是尚未安裝此套件，安裝後再啟動即可。

加入 PDF 或資料夾，選擇輸出路徑、150/200/300/600 DPI、JPEG 品質、1–8 條編碼執行緒與影像預算。預設 200 DPI、品質 92、512 MiB。偏好設定由 QSettings 保存。

續傳每次啟動預設關閉，不保存勾選狀態。關閉時，每份 PDF 建立新的來源名稱資料夾，重名自動加 `(2)`、`(3)`。

續傳時只加入一個 PDF，勾選「中斷續傳」，再選取該 PDF 先前由 v1.1.1 建立的輸出子資料夾。多份 PDF 請逐一選取各自的資料夾。JPG 寫回所選批次，輸出資料夾欄位供存放本次結果報告。

每個新批次保存第 2 版 `.pdf2jpg-batch.json` 身分紀錄，每頁成功發布後保存 `JPG檔名.done.json`。續傳會核對完整 PDF SHA-256、檔名、頁數、DPI、品質、Qt 版本、實際載入的 Qt PDF DLL SHA-256 及含註解的輸出契約。既有 JPG 必須符合雜湊、憑證、完整解碼和預期尺寸才會略過。執行緒數與影像預算可調整，驗證仍須有足夠影像預算。A3／600 DPI 建議使用至少 1,024 MiB。

PDF 在處理期間以 Windows 分享模式禁止寫入／刪除；批次持有 Windows 獨占鎖。`.pdf2jpg.lock` 是保留的鎖檔，程序結束或崩潰後由作業系統釋放持有權，不用刪檔或依時間猜測是否過期。同批次同時使用時拒絕後來的程序。

身分不符、缺失或損壞的 JPG 或憑證、未知檔案及殘留暫存檔，都會使程式在輸出新頁面前拒絕續傳並保留原檔。JPG 與憑證都不存在才視為未完成頁面。如果 v1.0 或 v1.1.0 留有舊批次，或新版批次在發布 JPG 後、保存憑證前中斷，請關閉續傳並建立新批次。

JPEG 在目的資料夾內寫入唯一暫存檔，成功後用 `QTemporaryFile::rename` 原子發布；目的檔已存在時失敗，不覆寫、不改成 copy/delete。失敗時清除自己的暫存檔，保留別人的檔案。

每頁輸出為白底 RGB JPEG，保留 DPI 及 Qt PDF 能繪製的可見頁面註解。需要保護敏感資訊時，請先在 PDF 編輯器套用正式遮蔽，並檢查輸出。轉成 JPG 無法保證遮蔽或刪除敏感資料。密碼最多重試 3 次，取消密碼輸入會略過該檔。取消批次或關閉主視窗時，停止安排新頁面，等待已開始的渲染與寫入收尾並保留完成頁面。每批產生 `batch-UUID.json` 紀錄。

PDF 由單一背景執行緒渲染，JPEG 由多條工作執行緒編碼。影像預算包括渲染中、等候中及編碼中的影像，以 `寬×高×12+8 MiB` 保留額限流。同時工作數上限為編碼執行緒數的兩倍。轉換與續傳驗證都會拒絕超過單頁尺寸、8,000 萬像素或影像預算的頁面。Qt JPEG 解碼配置上限設定為 306 MiB。

## 建置與驗收

```powershell
.\build.ps1                  # 正式 Release：獨立建置目錄、dist
.\build.ps1 -Testing         # 驗收 Release：獨立建置目錄、dist-test，含 engine-tests
ctest --test-dir "<腳本輸出的測試版建置目錄>" --output-on-failure
python .\verify.py          # 真實 PDF/JPEG、GUI 自動驗收、故障注入與壓力測試
python .\verify_deployment.py # 正式版離開 Qt PATH 的啟動與 DLL 來源檢查
.\tests\verify_build.ps1     # 在隔離副本驗證建置失敗與封裝回復
```

建置使用 CMake 4.5 以上、Visual Studio 2026 的 MSVC 14.52 x64，以及 `D:/qt/6.12.0/msvc2022_64` 的 Qt 6.12.0 和獨立發布的 Qt PDF 6.140.0。每次建置在 `work/` 內建立新快取與部署資料夾，逐一比對部署 DLL 與 SDK，驗證後才替換目的地。上一份封裝保留在該次工作目錄內。建置工具環境只在子程序內設定。MSVC 執行階段由系統安裝的 Visual C++ 可轉散發套件提供。

驗收使用本機既有 PyMuPDF 與 Pillow，不安裝套件；程式執行不需要 Python。測試設定使用執行檔旁的 INI，避免覆蓋正式版偏好。測試資料在 work；所有密碼為合成測試資料。

## 限制

- Qt PDF 渲染及 JPEG／磁碟 I/O 無法安全強制中斷，取消及關機仍需等待目前呼叫返回。
- 影像預算不是程序記憶體硬上限，PDFium、字型、Qt、解碼器與快取另計。
- 記憶體耗盡時會嘗試回報最小錯誤；若連回報所需的分配也失敗，GUI 會提示未收到結果。無法復原原生 PDFium 崩潰或系統強制終止。
- 加密 PDF 頁數在解密後加入總數，百分比可能降低；預掃描為抽樣，實際每頁仍會檢查尺寸。
- 大資料夾遞迴加入仍在 GUI 執行緒，可能短暫停頓。未擴充新功能或改成新架構。
- 建議使用只有自己可寫的輸出目錄。批次鎖協調同一批次的程式實例，共用位置仍須仰賴目錄權限。網路磁碟、其他檔案系統、真正磁碟耗盡與 Windows 登出未實測。

另可執行 `python tests/verify_resume.py` 重跑續傳針對性驗收。詳見 VERIFICATION.md。


## 授權

本程式以 [MIT 授權](LICENSE) 釋出，Copyright © 2026 Kaido-MSVC。程式圖示由 AI（ChatGPT）生成。

本程式使用 Qt 6.12.0 與獨立發布的 Qt PDF 6.140.0（內含 PDFium），Qt 依 GNU LGPLv3 以動態連結方式使用。Qt DLL 隨程式放在 dist 資料夾，可自行替換為相容版本。對應提交見第三方聲明。 [Qt 6.12.0 原始碼](https://download.qt.io/official_releases/qt/6.12/6.12.0/) · [Qt WebEngine／PDFium 6.140.0 原始碼](https://download.qt.io/official_releases/qtwebengine/6.140/6.140.0/src/) · [第三方聲明](THIRD_PARTY_NOTICES.md)

第三方元件清單、版本與授權見 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)，授權全文在 `licenses/`。程式內「關於」→「第三方授權」／「關於 Qt」也可查看。發佈的 dist 資料夾會一併附上 LICENSE、THIRD_PARTY_NOTICES.md 與 licenses/。

## v1.1.1 發布確認

正式封裝使用 Qt PDF 6.140.0 的 11 月建置。來源提交、EXE 內嵌聲明、部署檔案與回歸結果見 [VERIFICATION.md](VERIFICATION.md)。


## v1.1.0 公開發布核對

沿用已通過完整審查與最後增量驗收的 `5dc28b3` 程式及 dist，本次僅更新 README 的正式下載與發布說明，未修改功能、授權文件或 dist，未重新編譯或重跑已通過的整套測試。

Portable ZIP 與原 dist 的 85 個檔案逐一核對 SHA-256 相同。正式 EXE 與 build EXE 的 SHA-256 均為 `e930a96e4d1cfd6a1a855d0734728203b723cbb393dc1790eb09281e51f12ae4`。發布後另核對遠端 commit/tag 與公開附件下載雜湊。

[VERIFICATION.md](VERIFICATION.md) 保存各版驗收紀錄，其中較早的 EXE 雜湊與部署描述反映當時狀態。公開附件以 Release 與 SHA256SUMS.txt 為準。未另做乾淨 Windows 虛擬機器或全版本 Windows 相容性測試。
