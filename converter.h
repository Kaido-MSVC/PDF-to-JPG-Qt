#pragma once
#include <QElapsedTimer>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThread>
#include <atomic>
#include <condition_variable>
#include <mutex>

// UI 與引擎共用的範圍常數（SpinBox / ComboBox 請直接引用，避免兩邊不一致）。
inline constexpr int kDpiChoices[] = {150, 200, 300, 600};
inline constexpr int kMinMemoryMiB = 128;
inline constexpr int kMaxMemoryMiB = 2048;
inline constexpr int kMaxEncoders = 8;
inline constexpr int kMaxPasswordRetries = 10;

struct Settings {
    QStringList files;
    QString output;
    int dpi = 200, quality = 92, encoders = 2, memoryMiB = 512;
    bool resume = false;       // 每次啟動預設關閉
    QString resumeFolder;     // 明確選擇單一 PDF 的既有批次資料夾
    int maxErrorsKept = 200;   // 每個檔案最多保留/顯示的錯誤筆數，超過只計數
    int passwordRetries = 3;   // 同一個 PDF 最多詢問密碼的次數
    // Only supplied by the explicit diagnostic batch entry point; never logged.
    QString testPassword;

    // 回傳錯誤訊息；空字串代表設定有效。UI 在按下開始前就可呼叫。
    QString validate() const;
};

class Converter final : public QThread {
    Q_OBJECT
public:
    explicit Converter(Settings s, QObject *parent = nullptr);
    ~Converter() override { cancel(); wait(); }
    void cancel();
    void answerPassword(QString password, bool accepted);
signals:
    void message(QString text);
    // 以「總頁數」計算的整批進度；已節流（約每 100 ms 最多一次，檔案結束時強制送出）。
    void progress(qint64 pagesDone, qint64 pagesTotal, int fileIndex, int files);
    // retry 為 true 代表上一次輸入的密碼不正確。
    void passwordNeeded(QString source, bool retry);
    // run() 結尾送出一次，值傳遞，避免與 UI 執行緒共用資料。
    void batchFinished(QJsonObject result);
protected:
    void run() override;
private:
    QJsonObject processFile(int index, qint64 &counted, QSet<QString> &usedNames);
    void emitProgress(bool force);
    QString askPassword(const QString &source, bool retry, bool &accepted);

    Settings settings;
    std::atomic<bool> stopped{false};

    // 以下成員只在 worker 執行緒（或其編碼執行緒，且為 atomic）使用
    std::mutex progressMutex;
    QElapsedTimer clock;
    std::atomic<qint64> pagesDone{0}, pagesTotal{0}, lastEmitMs{-1000};
    std::atomic<int> currentFile{0};
    int fileCount = 0;
    qint64 totalSaved = 0, totalSkipped = 0, peakBytes = 0;
    int peakJobs = 0;

    std::mutex passwordMutex;
    std::condition_variable passwordReady;
    // guarded by passwordMutex
    bool answered = false, passwordAccepted = false;
    QString suppliedPassword;
};
QString safeStem(const QString &source);
