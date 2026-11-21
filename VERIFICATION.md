# PDF to JPG_QT v1.1 驗收

日期：2026-10-23（台北）。專案：D:/projects/PDF to JPG_QT。

## 結果與範圍

唯一新增使用者功能為可選中斷續傳，每次啟動預設關閉，不保存勾選。保留 Qt PDF 渲染、JPEG 編碼、GUI/worker 分離、取消、影像記憶體預算、原子發布及不覆寫保護。

新增 resume.h 與 tests/verify_resume.py；修改 converter.cpp/.h、main.cpp、CMakeLists.txt、app.rc 及說明文件。正式 Release 不含診斷命令、故障注入或測試密碼入口。

## v1.0 基線與備份

先唯讀檢查原始碼、建置腳本、測試紀錄、dist 與 build EXE。兩份 EXE 的 SHA-256 相同：CCD340AD5190A786C98FAD5AA54B023C9A707BD4D53D749132FF4B5F32228DC2。

備份根目錄全部原始碼、建置／說明檔、assets、tests 及完整 dist（EXE、Qt plugins、CRT 與授權文件）；附逐檔 SHA-256 清單。不包含可重建的 build 暫存物。

- 本聊天：outputs/v1.0-backup、outputs/v1.0-backup-sha256.json。
- 專案內：work/backup-v1.0-before-v1.1-20261023。

原始碼先複製到隔離工作目錄，以既有 MSVC/Qt 重建驗收版，原有 46 項整合檢查通過。原始 v1.0 完整部署以乾淨 PATH 啟動並核對 DLL 來源。

## 續傳規則

一份 PDF 的輸出子資料夾是一個可選取批次。只接受一個 PDF 續傳；多份 PDF 請逐一操作。關閉時仍建立新資料夾，自動避開重名。續傳時 JPG 寫回所選批次，輸出資料夾欄位用於本次結果報告。

來源 PDF 在雜湊、開啟、渲染期間透過 Windows 分享模式禁止寫入及刪除。批次 .pdf2jpg.lock 持有 Windows 獨占 handle；不依 PID／時間猜測鎖過期，程序死亡由系統釋放持有權。同時新建批次仍由原有 mkdir 排他避開名稱衝突。

不可變 .pdf2jpg-batch.json 保存 PDF 全內容 SHA-256、檔名 stem、頁數、DPI、JPEG 品質、Qt 版本及輸出合約（Qt PDF 預設 render、向上取整尺寸、白底 RGB32、optimizedWrite=false、DPI metadata）。執行緒／預算不影響輸出身分，可在足夠驗證預算下調整。

每頁 JPG 原子發布後才原子發布 .done.json，含批次身分雜湊、頁檔名、JPG SHA-256 與尺寸。續傳先驗證全部既有頁面，憑證、JPG 雜湊、完整 JPEG 解碼與預期尺寸均正確才略過。解碼在編碼佇列開始前逐頁執行，遵守單頁影像預算檢查。

缺失／損壞 JPG 或紀錄、身分不符、未知檔案、暫存殘留均拒絕續傳，不產生新頁面，不刪除或覆寫舊檔。JPG 及憑證都不存在才視為未完成。

## 實測

1. v1.1 續傳針對性驗收 46 項通過：正常新批次、全部略過、關閉續傳重跑、調整執行緒／預算、拒絕多 PDF；DPI／品質、相同大小與時間的 PDF 內容變更；損壞／截斷／錯尺寸 JPG、缺失 JPG／憑證、損壞紀錄、Qt／輸出合約／schema 不符、未知檔案與暫存、同名資料夾、缺頁重建；取消重啟並核對舊 JPG 雜湊；中日文及超過 290 字元來源／批次路徑；並行同批次拒絕、來源改寫遭拒、正常解鎖、強制終止持鎖程序後重啟；GUI 預設與續傳控制項。
2. 真實程序終止：驗收版在 JPG 發布後、憑證發布前終止，退出碼 91；下次拒絕孤立 JPG 且保留原檔。另從外部終止持鎖程序，確認系統釋放鎖。
3. 例外注入：worker 建立、submit、JPEG 寫入、callback、憑證發布、結果序列化均在 timeout 內收尾；無發布頁面的可恢復失敗可續傳；憑證失敗留下 JPG，重啟保守拒絕且不覆寫。
4. 既有 46 項整合回歸通過：各 DPI 尺寸／metadata／白底 RGB、密碼、損毀／遺失 PDF、超大頁與預算、重跑不覆寫、路徑失敗、GUI 深淺色／取消／密碼／關閉、worker/callback/submit/write/final 故障、搶先建立同名 JPG／資料夾、雙程序不同資料夾、160 頁合成壓力測試。
5. 引擎檢查通過：6 種無效尺寸、正常算術、部分 worker 建構失敗、100 次 callback bad_alloc；reservation 歸零、drain 可取得錯誤且無卡住。
6. GUI 截圖目視檢查：續傳勾選及既有批次欄位／按鈕可見。診斷 GUI 透過實際控制項取設定；未手動逐次點選 Windows 資料夾對話框。

JSON 摘要、設定、stderr 與 GUI 截圖保存於專案 work/v1.1-verification；本聊天 outputs/test-evidence 另保存精簡證據。使用合成 PDF，未安裝套件或改全域 PATH。

## 已知限制與未實測

- v1.0 沒有身分／完成憑證，不能安全匯入續傳。請關閉續傳建立新批次。
- 孤立 JPG、壞憑證或未知殘留會保守拒絕，沒有自動修復／清理功能。舊檔保留，可關閉續傳重新轉換。
- JPG 和憑證是兩次原子發布，非跨檔交易；中間中斷不當成功。flush/rename 不保證斷電時硬體持久性。真正斷電、磁碟耗盡、ACL 拒寫、網路磁碟、其他檔案系統及 Windows 登出／關機未實測。
- 鎖協調本程式，不防具有檔案權限的外部程式惡意改寫整份批次及憑證。
- 單次 Qt 渲染、JPEG、磁碟呼叫及 JPEG 驗證不可安全強制中止，取消仍等目前呼叫返回；PDF 指紋逐 MB 檢查取消。
- 影像預算不是程序總記憶體硬上限；PDFium／Qt／字型／快取另計。全面 OOM 或原生崩潰可能無完整結果。
- 未建立一萬頁資料；160 頁合成樣本不是普遍效能保證。完整指紋與 JPG 驗證會增加續傳啟動耗時。

契約參考：[Windows CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)、[Qt QTemporaryFile rename](https://doc.qt.io/qt-6/qtemporaryfile.html#rename)、[Qt QImageReader](https://doc.qt.io/qt-6/qimagereader.html#read)。

## 正式部署確認

已在原 Qt 專案使用既有 Visual Studio 2026 / MSVC x64 + Qt 6.12.0 成功編譯與部署。正式 EXE：D:/projects/PDF to JPG_QT/dist/PDF-to-JPG_QT.exe，檔案版本 1.1.0。

SHA-256：DCA6D2934E3560B98587C97BF753ED92C3641C35A1F2B756FF181A6431430E71。

正式版以乾淨 PATH 啟動，Qt/PDF/Windows plugin/MSVC CRT 均由 dist 載入；另核對正式原始碼與通過測試的隔離副本相同，正式二進位不含診斷／故障入口標記。功能驗收使用同原始碼的獨立驗收版；正式版做啟動與 DLL 來源檢查，未以正式 GUI 另手動重跑全部轉換情境。

D:/projects/PDF to JPG 原 Python 專案的 223 個檔案於前後逐一核對 SHA-256 及檔案數，無變更。

## 2026-10-30 變更

部署內容調整（commit 21b2049、1adddb5 及本次）：

- Windows SDK 的 DLL（d3dcompiler_47.dll、dxcompiler.dll、dxil.dll）與 MSVC 執行階段 DLL（vcruntime140*、msvcp140*、concrt140、vccorlib140）不再隨程式散布。windeployqt 改用 `--no-compiler-runtime`，並加上 `--no-system-d3d-compiler`、`--no-system-dxc-compiler`；build.ps1 不再複製 Microsoft.VC145.CRT，且每次部署前清空 dist。
- 執行階段改由使用者安裝 Microsoft 官方最新版 Visual C++ 可轉散發套件（x64），見 README「系統需求」。Windows 10/11 已內建 d3dcompiler_47.dll。
- 本檔第 15 行「完整 dist（…CRT…）」與第 63 行「MSVC CRT 均由 dist 載入」的描述已不適用：dist 不再含 CRT，正式版改由系統載入 msvcp140.dll 等執行階段。
- 本檔為開發紀錄，不再複製進 dist。

驗證方式與結果：

- `build.ps1` 建置與部署成功；`verify_deployment.py` 通過。該腳本以只含 System32 的 PATH 啟動正式版，確認 Qt/PDF/Windows plugin 由 dist 載入、msvcp140.dll 由系統載入；並檢查 dist 不含 Windows SDK DLL、MSVC 執行階段 DLL 與 VERIFICATION.md，授權檔無占位字且與 licenses/ 一致。
- 移除 Windows SDK DLL 後，以 Windows UI Automation 操作正式版 GUI（PATH 同樣只含 System32）：啟動、經檔案對話框加入 24 頁 PDF 並轉換（完成 1、失敗 0，輸出 24 張 JPG）、開啟「關於」與「第三方授權」視窗後關閉，程式結束碼 0。測試前後備份並還原 HKCU\Software\PDF2JPG 偏好設定。
- 當時 dist 仍附帶 MSVC 執行階段；改由系統載入後僅以 verify_deployment.py 驗證啟動。本機已安裝 VC++ 可轉散發套件，未模擬未安裝的情況。
# v1.1.1 本機驗收（2026-11-21）

以乾淨的 `master`、`f9cec133ff3996c3b85325c6f8b0b066c636bc60` 為基準，在 `work/v1.1.1-20261121-171831/` 的隔離副本建置及測試。測試環境為 Windows 11 25H2 x64（26200.9974）、Visual Studio 2026 18.12.1、MSVC 19.52.37029、Windows SDK 10.0.28000.0、CMake／CTest 4.5.0、Ninja 1.12.1。兩個組態的 Ninja 規則均使用正式 `-std:c++23`。CTest 另編譯並驗證 `_MSVC_LANG >= 202302L` 與 `std::to_underlying`。

## Qt 元件來源

Qt Maintenance Tool 的 `D:/qt/components.xml` 與 `D:/qt/InstallationLog.txt` 確認安裝元件 `extensions.qtpdf.6120.61400.win64_msvc2022_64`，於 2026-11-15 更新至建置 `202611110526`。SDK 根目錄為 `D:/qt/6.12.0/msvc2022_64`，SBOM 的 Qt PDF 來源提交為 `e081473af432e2b09d17caa3ca3bffd45992a0bd`。Qt 於 2026-11-13 發布此獨立版本。發布說明記載 Chromium 安全修補基準，本次依 DLL 與 SDK 來源驗收，個別 PDFium CVE 的適用性未確認。 [Qt 正式公告](https://lists.qt-project.org/pipermail/announce/2026-November/000691.html) · [Qt 6.140.0 發布說明](https://code.qt.io/cgit/qt/qtreleasenotes.git/about/qtwebengine/6.140.0/release-note.md)

| 正式封裝檔案 | 檔案版本 | SHA-256 | Authenticode |
|---|---|---|---|
| `Qt6Pdf.dll` | 6.140.0.0 | `bd0de9654ce7eb950e1c57f36e706da09e7d49da4cdd11a41934d53ab14398ea` | The Qt Company Oy，有效，2026-11-11 08:59:00 台灣時間 |
| `imageformats/qpdf.dll` | 6.140.0.0 | `ac2c04d4f59fd012568dac5fe1d3700b7975d68545111c47f7a781ec2aaba197` | The Qt Company Oy，有效，2026-11-11 09:01:54 台灣時間 |
| `Qt6Gui.dll` | 6.12.0.0 | `37c79b48b9cc2d9a03d21410c26fea683d236f9280f5c9aaab85d980759f2835` | 套件與 SDK 逐位元組相同 |
| `imageformats/qjpeg.dll` | 6.12.0.0 | `9e67dc18c9f67141f5da20646b63a5ee4a57f8cfdad6b1b3c4c5f70e49ace065` | 套件與 SDK 逐位元組相同 |

舊公開版 `Qt6Pdf.dll` 的 SHA-256 為 `aa0e0c688d5991b0b075d241d39901b15a641d3b9c4e4b1f82f4b391b5ed6447`。新版 CMake 快取與 Qt 工具路徑均在選定 SDK 下，部署的 23 個 Qt DLL 逐一與 SDK 核對。正式 EXE 啟動時將 PATH 設為 System32，Qt 由隨附封裝載入，MSVC 執行階段由 Windows 系統載入。授權檔與通知核對通過。

## 測試結果

| 測試 | 結果 | 隔離副本中的證據 |
|---|---|---|
| CMake Configure／Build，Release 及 `-Testing` | 兩組均通過 | `work/production-build.log`、`work/testing-build.log` |
| CTest | 1／1 通過 | `work/ctest.log` |
| PDF、JPG 與 GUI 整合 | 47 項通過，包含正常與異常 PDF、多頁、文字註解、可見矩形遮罩、底層文字對照、取消、輸出衝突與 160 頁既有測試 | `work/verify-20261121-173058/summary.json` |
| 續傳、記憶體與 Windows 路徑 | 52 項通過，包含 A3／600 DPI 轉檔與另啟程序續傳、低預算拒絕、舊格式、元件與輸出契約不符、JPEG 損毀與尺寸不符、Junction 及 Symbolic Link | `work/resume-20261121-173059/summary.json` |
| 實際不同 Qt PDF 建置 | 9 月 DLL 產生批次，11 月 DLL 安全拒絕續傳，原檔逐一雜湊相同。僅使用合成 PDF | `work/qt-pdf-build-comparison/summary.json` |
| 現有封裝保護 | 4 項通過，包含編譯失敗、`-Testing` 部署失敗，以及正式版與測試版兩種候選封裝移動失敗的回復 | `work/build-verification-20261121-172857/summary.json` |
| 正式封裝 | 乾淨 PATH 啟動、DLL 來源與雜湊、授權、版本及診斷入口檢查通過 | `work/release-deployment.json` |

A3 合成檔在 600 DPI 產生 7016 × 9922 的 RGB JPG。1,024 MiB 預算通過轉換與續傳，768 MiB 續傳安全拒絕。Qt 的解碼上限設定為 306 MiB，另有 8,000 萬像素及單頁預算檢查。新版使用第 2 版批次紀錄及含可見註解的輸出契約。第 1 版批次應另建輸出，已測試保留既有 JPG。

共用路徑方面，續傳會拒絕直接選取 Junction 或 Symbolic Link。以一般 NTFS 目錄和無共享模式持有合成鎖檔時，Windows 拒絕改名該批次及其父目錄。工作磁碟有繼承的 `Authenticated Users` 修改權限範例，可寫的共用路徑仍須仰賴目錄 ACL。未證實越界寫入，因此保留現有 I/O 架構。未測跨帳號並行改路徑、SMB、其他檔案系統、舊 Windows 或真正磁碟耗盡。註解轉換沒有安全遮蔽或資料移除保證。

## 本機封裝（來源聲明修正前）

- `release/PDF-to-JPG-Qt-1.1.1-Windows-x64-Portable.zip` 的 SHA-256 為 `de9ee5109bca21a541b871cc3f8446ca6c05c760f00d4c65f18682cd58e3c677`。解讀 ZIP 後，87 個檔案均與候選 `dist` 逐一比對相同。
- 正式 EXE 的 SHA-256 為 `0c386e42b70155a772d4f18cad797cebbaf484383a9bcaac064eee87800475b8`。`release/SHA256SUMS.txt` 與 `release/dist-file-sha256.json` 保存封裝及各檔案雜湊。
- 本機候選已通過約定的驗收。正式 v1.1.0 本機產物保留，GitHub Tag 與 Release 維持原狀。

## v1.1.1 正式候選的來源聲明驗收

發布前核對發現初次候選的 Qt PDF 原始碼聲明仍指向 `d505fc23640d2bd6345e6483eb73daf4e19f5829`。正式 SDK 的 `qtpdf-6.140.0.spdx` 記載 `e081473af432e2b09d17caa3ca3bffd45992a0bd`。更新聲明後，在 `work/v1.1.1-release-final/` 重新建置 Release 及 `-Testing`，重新驗證 Qt PDF DLL 與外掛的 6.140.0.0 版本、Qt 簽章與 SHA-256。原列 qtbase 提交與 SDK SBOM 一致。

- 從正式 EXE 核對 Qt 資源表與 `:/THIRD_PARTY_NOTICES.md` 內容。內嵌聲明、候選 `dist` 與原始碼副本三者逐位元組一致。
- CTest **1／1 通過**。正式 EXE 在 PATH 僅含 System32 時啟動成功，DLL 來源、23 個 Qt DLL 的 SHA-256、授權全文、版本與診斷入口檢查均通過。
- 將新 ZIP 解壓至獨立資料夾，逐一比對全部 **87 個檔案**。相對於修正前候選，只有 EXE 與第三方聲明的檔案雜湊變更。授權全文及 Qt DLL 完全相同。
- 程式邏輯未變更。47 項整合及 52 項續傳測試沿用前述驗收結果。各別 PDFium CVE 的適用性仍未確認。

| 產物 | SHA-256 |
|---|---|
| Portable ZIP | `3052b3b3e967c9bfcfa661736d244e0880aa577e7677f78e504472baca7fe33c` |
| SHA256SUMS.txt | `00f9ecbe9abdf826dfda2843ba6c73e359b4fd61022ab08b786648da4634c001` |
| 正式 EXE | `705e5edad3761900ecc98148c7806dc103cf3f403b9d94367c0d45197e1432ec` |
| EXE 內嵌聲明與隨附聲明 | `5d1742989cdb2fa526eb35cc8c98ee84438e6e74d6b3c6241da729584ed8da0c` |
