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
