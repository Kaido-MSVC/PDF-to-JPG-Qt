#include "converter.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaEnum>
#include <QPainter>
#include <QPdfDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryFile>
#include <limits>
#include <exception>
#include <QStorageInfo>
#include <QUuid>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <functional>
#include <stdexcept>
#include <thread>
#include <vector>
#include <QCryptographicHash>
#include <QImageReader>
#include <memory>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

constexpr qint64 kMiB = 1024 * 1024;
QString ioError(const QString &path, const QString &detail) {
    return QString("%1：%2").arg(path, detail);
}
QString formatMiB(qint64 bytes) { return QString::number(bytes / kMiB + (bytes % kMiB != 0)) + " MiB"; }
QString prepareFolder(const QString &root, const QString &stem, QSet<QString> &usedNames, bool &created) {
    for (int i = 1; i <= 100000; ++i) {
        const QString name = stem + (i == 1 ? QString() : QString(" (%1)").arg(i));
        if (usedNames.contains(name.toLower())) continue;
        const QString path = QDir(root).filePath(name);
        if (QDir().mkdir(path)) {
            usedNames.insert(name.toLower()); created = true; return path;
        }
        if (!QFileInfo::exists(path)) throw std::runtime_error(ioError(path, "無法建立輸出資料夾").toStdString());
    }
    throw std::runtime_error("同名資料夾過多");
}

// ---------------------------------------------------------------------------
// 開始前的粗略掃描：頁數、預估輸出大小、最大單頁記憶體需求。
// ---------------------------------------------------------------------------
struct Scan {
    qint64 pages = -1;        // -1：無法取得（損毀或需要密碼）
    bool encrypted = false;
    qint64 estimateBytes = 0;
    qint64 maxCost = 0;
};

qint64 pageCost(const QSizeF &pt, int dpi, double *pixels = nullptr) {
    const double w = std::ceil(pt.width() * dpi / 72.0), h = std::ceil(pt.height() * dpi / 72.0);
    if (!std::isfinite(w) || !std::isfinite(h) || w < 1 || h < 1 || w > 100000 || h > 100000 || w * h > 80000000)
        throw std::runtime_error("頁面尺寸過大或無效，請降低 DPI");
    if (pixels) *pixels = w * h;
    return qint64(w) * qint64(h) * 12 + 8 * kMiB;
}

Scan scanPdf(const QString &path, int dpi, const std::atomic<bool> &stopped) {
    Scan scan;
    QPdfDocument doc;
    const auto error = doc.load(path);
    if (error == QPdfDocument::Error::IncorrectPassword) { scan.encrypted = true; return scan; }
    if (error != QPdfDocument::Error::None || doc.status() != QPdfDocument::Status::Ready) return scan;
    scan.pages = doc.pageCount();
    if (scan.pages <= 0) return scan;
    const int step = int(std::max<qint64>(1, scan.pages / 200)); // 最多抽樣約 200 頁
    double pixelSum = 0;
    int sampled = 0;
    for (int p = 0; p < scan.pages && !stopped; p += step) {
        double px = 0;
        try { scan.maxCost = std::max(scan.maxCost, pageCost(doc.pagePointSize(p), dpi, &px)); }
        catch (const std::runtime_error &) { continue; }
        pixelSum += px;
        ++sampled;
    }
    // 掃描檔 JPEG 約 0.3–0.5 byte/像素，僅供提醒用的粗估。
    if (sampled) scan.estimateBytes = qint64(pixelSum / sampled * double(scan.pages) * 0.4);
    return scan;
}

// ---------------------------------------------------------------------------
// 編碼佇列：渲染（呼叫端執行緒）與 JPEG 編碼（工作執行緒）以位元組預算限流。
// Reservations include the page being rendered, queued pages and active encoders.
// 12 bytes/pixel allows RGB32 plus a possible JPEG conversion copy and scratch margin.
// ---------------------------------------------------------------------------
#include "resume.h"

struct Job { QImage image; QString path; qint64 cost; QString batchKey; };

class EncodingQueue {
public:
    EncodingQueue(int count, qint64 bytes, int jpegQuality, std::atomic<bool> &cancel,
                  std::function<void(QString, bool)> cb)
        : budget(bytes), capacity(count * 2), quality(jpegQuality), cancelled(cancel), report(std::move(cb)) {
        try {
            for (int i = 0; i < count; ++i) {
#ifdef PDF2JPG_TESTING
                if (qEnvironmentVariableIntValue("PDF2JPG_FAIL_THREAD") == i + 1) throw std::runtime_error("injected thread creation failure");
#endif
                threads.emplace_back([this] { consume(); });
            }
        } catch (...) { shutdown(); throw; }
    }
    ~EncodingQueue() { shutdown(); }
    void shutdown() noexcept {
        { std::lock_guard lock(mutex); done = true; }
        changed.notify_all();
        for (auto &t : threads) if (t.joinable()) t.join();
    }
    class Reservation {
        EncodingQueue *queue; qint64 bytes;
    public:
        Reservation(EncodingQueue &q, qint64 n) noexcept : queue(&q), bytes(n) {}
        Reservation(const Reservation &) = delete;
        Reservation &operator=(const Reservation &) = delete;
        ~Reservation() { if (queue) queue->release(bytes); }
        void transfer() noexcept { queue = nullptr; }
    };
    bool reserve(qint64 bytes) {
        std::unique_lock lock(mutex);
        while (!cancelled && !failure && (used + bytes > budget || outstanding >= capacity))
            changed.wait_for(lock, std::chrono::milliseconds(50));
        if (failure) std::rethrow_exception(failure);
        if (cancelled) return false;
        used += bytes; ++outstanding;
        peak = std::max(peak, used); peakJobs = std::max(peakJobs, outstanding);
        return true;
    }
    void release(qint64 bytes) {
        { std::lock_guard lock(mutex); used -= bytes; --outstanding; }
        changed.notify_all();
    }
    void submit(Job job) {
        { std::lock_guard lock(mutex);
          if (failure) std::rethrow_exception(failure);
#ifdef PDF2JPG_TESTING
          if (qEnvironmentVariableIsSet("PDF2JPG_FAIL_SUBMIT")) throw std::bad_alloc();
#endif
          jobs.push_back(std::move(job)); }
        changed.notify_all();
    }
    void drain() {
        std::unique_lock lock(mutex);
        changed.wait(lock, [this] { return outstanding == 0; });
        if (failure) std::rethrow_exception(failure);
    }

    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Job> jobs;
    qint64 used = 0, peak = 0, budget;
    int outstanding = 0, peakJobs = 0, capacity;
    bool done = false;
    std::exception_ptr failure;
    int quality;
    std::atomic<bool> &cancelled;
    std::function<void(QString, bool)> report;
    std::vector<std::jthread> threads;

private:
    void consume() noexcept {
        try {
            for (;;) {
                Job job;
                {
                    std::unique_lock lock(mutex);
                    changed.wait(lock, [this] { return done || !jobs.empty(); });
                    if (jobs.empty()) return;
                    job = std::move(jobs.front()); jobs.pop_front();
                }
                Reservation reservation(*this, job.cost);
                try {
                    // Destroy image before releasing its reservation, including exception paths.
                    struct ClearImage { QImage &image; ~ClearImage() { image = QImage(); } } clear{job.image};
                    QString error;
                    bool saved = false;
                    try {
                        if (!cancelled) {
                            QTemporaryFile file(QDir(QFileInfo(job.path).absolutePath()).filePath(".pdf2jpg-XXXXXX.tmp"));
                            if (!file.open()) throw std::runtime_error(ioError(job.path, file.errorString()).toStdString());
                            QImageWriter writer(&file, "jpg");
                            writer.setQuality(quality); writer.setOptimizedWrite(false);
                            if (!writer.write(job.image) || !file.flush()) throw std::runtime_error(ioError(job.path, writer.errorString() + file.errorString()).toStdString());
    #ifdef PDF2JPG_TESTING
                            if (qEnvironmentVariableIsSet("PDF2JPG_FAIL_WRITE")) throw std::runtime_error("injected write failure");
    #endif
                            // QTemporaryFile::rename closes its native handle and uses a rename only.
                            // Existing destinations fail; unlike QFile it never falls back to copy/delete.
                            if (!file.rename(job.path))
                                throw std::runtime_error(ioError(job.path, file.errorString() + "（不覆寫既有檔案）").toStdString());
                            file.setAutoRemove(false);
#ifdef PDF2JPG_TESTING
                            if (qEnvironmentVariableIsSet("PDF2JPG_CRASH_AFTER_PUBLISH")) TerminateProcess(GetCurrentProcess(), 91);
#endif
                            if (!job.batchKey.isEmpty()) writeReceipt(job.path, job.image.size(), job.batchKey);
                            saved = true;
                        }
                    } catch (const std::exception &e) { error = QString::fromUtf8(e.what()); }
                    catch (...) { error = QStringLiteral("JPEG 編碼發生未知例外"); }
    #ifdef PDF2JPG_TESTING
                    if (qEnvironmentVariableIsSet("PDF2JPG_FAIL_CALLBACK")) throw std::bad_alloc();
    #endif
                    report(error, saved);
                } catch (...) { recordFailure(); return; }
            }
        } catch (...) { recordFailure(); }
    }
    void recordFailure() noexcept {
        // Publish failure before the active reservation is released: drain cannot miss it.
        { std::lock_guard lock(mutex);
          if (!failure) failure = std::current_exception();
          cancelled = true;
          for (auto &job : jobs) { used -= job.cost; --outstanding; }
          jobs.clear();
        }
        changed.notify_all();
    }
};

QString errorName(QPdfDocument::Error error) {
    return QString::fromLatin1(QMetaEnum::fromType<QPdfDocument::Error>().valueToKey(int(error)));
}

} // namespace

QString Settings::validate() const {
    if (resume && (files.size() != 1 || resumeFolder.trimmed().isEmpty()))
        return "續傳請只加入一個 PDF，並選擇其既有批次資料夾。";
    if (resume && (!QFileInfo(resumeFolder).isDir() || QFileInfo(resumeFolder).isSymLink() || QFileInfo(resumeFolder).isJunction()))
        return "續傳資料夾不存在或是連結。";
    if (files.isEmpty()) return "尚未加入 PDF。";
    if (output.trimmed().isEmpty()) return "尚未選擇輸出資料夾。";
    const QFileInfo out(output);
    if (out.exists() && !out.isDir()) return "輸出路徑不是資料夾：" + output;
    if (std::find(std::begin(kDpiChoices), std::end(kDpiChoices), dpi) == std::end(kDpiChoices))
        return "DPI 必須為 150、200、300 或 600。";
    if (quality < 1 || quality > 100) return "JPEG 品質必須為 1–100。";
    if (encoders < 1 || encoders > kMaxEncoders) return QString("編碼執行緒必須為 1–%1。").arg(kMaxEncoders);
    if (memoryMiB < kMinMemoryMiB || memoryMiB > kMaxMemoryMiB)
        return QString("影像記憶體預算必須為 %1–%2 MiB。").arg(kMinMemoryMiB).arg(kMaxMemoryMiB);
    if (maxErrorsKept < 0) return "錯誤保留筆數不可為負數。";
    if (passwordRetries < 1 || passwordRetries > kMaxPasswordRetries)
        return QString("密碼重試次數必須為 1–%1。").arg(kMaxPasswordRetries);
    return {};
}

QString safeStem(const QString &source) {
    QString s = QFileInfo(source).completeBaseName().normalized(QString::NormalizationForm_C);
    s.replace(QRegularExpression("[<>:\"/\\\\|?*\\x00-\\x1f]"), "_");
    // 以 UTF-16 單位截斷；60 字（中文最多約 180 bytes）加上 _p0001.jpg 仍在常見 255 bytes 檔名限制內。
    s = s.left(60);
    if (!s.isEmpty() && s.back().isHighSurrogate()) s.chop(1);
    while (s.endsWith('.') || s.endsWith(' ')) s.chop(1);
    const QString first = s.section('.', 0, 0).toUpper();
    static const QRegularExpression reserved("^(CON|PRN|AUX|NUL|CONIN\\$|CONOUT\\$|COM[0-9¹²³]|LPT[0-9¹²³])$");
    if (s.isEmpty()) s = "PDF";
    if (reserved.match(first).hasMatch()) s.prepend('_');
    return s;
}

Converter::Converter(Settings s, QObject *parent) : QThread(parent), settings(std::move(s)) {}

void Converter::cancel() {
    // 旗標必須在鎖內設定，否則 askPassword 剛檢查完條件、尚未進入等待時會漏掉喚醒。
    { std::lock_guard lock(passwordMutex); stopped = true; }
    passwordReady.notify_all();
}

void Converter::answerPassword(QString p, bool accepted) {
    {
        std::lock_guard lock(passwordMutex);
        suppliedPassword = std::move(p);
        passwordAccepted = accepted;
        answered = true;
    }
    passwordReady.notify_all();
}

QString Converter::askPassword(const QString &source, bool retry, bool &accepted) {
    if (!settings.testPassword.isEmpty()) { accepted = true; return settings.testPassword; }
    {
        std::lock_guard lock(passwordMutex);
        answered = false;
        passwordAccepted = false;
    }
    emit passwordNeeded(source, retry); // 不持鎖發送，避免直接連線的接收端造成死結
    std::unique_lock lock(passwordMutex);
    passwordReady.wait(lock, [this] { return answered || stopped.load(); });
    accepted = answered && passwordAccepted && !stopped.load();
    QString p = std::move(suppliedPassword);
    suppliedPassword.clear();
    return p;
}

void Converter::emitProgress(bool force) {
    std::lock_guard serial(progressMutex);
    const qint64 now = clock.elapsed();
    qint64 last = lastEmitMs.load();
    if (!force) {
        if (now - last < 100 || !lastEmitMs.compare_exchange_strong(last, now)) return;
    } else {
        lastEmitMs = now;
    }
    emit progress(pagesDone.load(), pagesTotal.load(), currentFile.load(), fileCount);
}

QJsonObject Converter::processFile(int f, qint64 &counted, QSet<QString> &usedNames) {
    const QString source = settings.files[f];
    int saved = 0, skipped = 0, failed = 0, dropped = 0, pages = 0;
    QString folder, status = "not_started";
    bool createdFolder = false;
    QJsonArray errors;
    std::mutex counts; // 保護 saved / skipped / failed / errors / dropped（編碼執行緒與本執行緒都會改）
    // 呼叫端必須已持有 counts。超過上限只計數，不再累積也不再送 message。
    auto addError = [&](const QString &text) {
        if (errors.size() < settings.maxErrorsKept) { errors.append(text); emit message(text); }
        else ++dropped;
    };
    currentFile = f + 1;

    if (!stopped) {
        try {
            emit message("讀取：" + source);
            NativeGuard sourceGuard(source, true); // Deny writes/deletion until rendering and hashing finish.
            const QString sourceHash = contentHash(source, &stopped);
            QPdfDocument doc; // Construct, load, render and destroy solely in this thread.
            auto loadError = doc.load(source);
            int attempts = 0;
            while (loadError == QPdfDocument::Error::IncorrectPassword && !stopped) {
                if (attempts >= settings.passwordRetries) throw std::runtime_error("密碼錯誤次數過多，已略過此檔案");
                bool accepted = false;
                QString password = askPassword(source, attempts > 0, accepted);
                if (!accepted) {
                    if (!stopped) throw std::runtime_error("已略過：未提供 PDF 密碼");
                    break;
                }
                doc.close();
                doc.setPassword(password);
                loadError = doc.load(source);
                password.fill(QChar(0)); // 只能盡量清除這份拷貝，不保證記憶體中不留痕跡
                password.clear();
                ++attempts;
            }
            if (!stopped) {
                if (loadError != QPdfDocument::Error::None || doc.status() != QPdfDocument::Status::Ready)
                    throw std::runtime_error(("PDF 開啟失敗：" + errorName(loadError)).toStdString());
                pages = doc.pageCount();
                if (pages <= 0) throw std::runtime_error("PDF 沒有頁面");
                if (counted == 0) { pagesTotal += pages; counted = pages; } // 預掃描時因密碼而未知的頁數
                const QString stem = safeStem(source);
                folder = settings.resume ? QFileInfo(settings.resumeFolder).absoluteFilePath()
                                         : prepareFolder(settings.output, stem, usedNames, createdFolder);
                NativeGuard batchGuard(QDir(folder).filePath(".pdf2jpg.lock"), false);
#ifdef PDF2JPG_TESTING
                if (qEnvironmentVariableIsSet("PDF2JPG_HOLD_LOCK")) QThread::msleep(1500);
#endif
                const auto identity = batchIdentity(settings, sourceHash, stem, pages);
                const auto batchKey = identityHash(identity);
                const QString manifest = QDir(folder).filePath(".pdf2jpg-batch.json");
                QSet<int> complete;
                if (settings.resume) {
                    if (readRecord(manifest) != identity)
                        throw std::runtime_error("批次身分不匹配（PDF 內容、檔名或輸出設定已變更）；保留原檔");
                    complete = validateExisting(folder, stem, doc, settings, batchKey, stopped);
                } else {
                    publishJson(manifest, identity);
                }
                {
                    EncodingQueue queue(settings.encoders, qint64(settings.memoryMiB) * kMiB, settings.quality, stopped,
                        [&](QString err, bool ok) {
                            {
                                std::lock_guard lock(counts);
                                if (ok) ++saved;
                                else if (!err.isEmpty()) { ++failed; addError(err); }
                            }
                            if (ok || !err.isEmpty()) ++pagesDone; // 被取消而放棄的頁面不計入
                            emitProgress(false);
                        });
                    for (int p = 0; p < pages && !stopped; ++p) {
                        try {
                            if (complete.contains(p)) {
                                { std::lock_guard lock(counts); ++skipped; }
                                ++pagesDone; emitProgress(false); continue;
                            }
                            const QString target = QDir(folder).filePath(
                                QString("%1_p%2.jpg").arg(stem).arg(p + 1, 4, 10, QChar('0')));
                            const QSizeF pt = doc.pagePointSize(p);
                            const double wd = std::ceil(pt.width() * settings.dpi / 72.0);
                            const double hd = std::ceil(pt.height() * settings.dpi / 72.0);
                            if (!std::isfinite(wd) || !std::isfinite(hd) || wd < 1 || hd < 1 || wd > 100000 || hd > 100000
                                || wd * hd > 80000000)
                                throw std::runtime_error("頁面尺寸過大，請降低 DPI");
                            const qint64 cost = pageCost(pt, settings.dpi);
                            if (cost > queue.budget)
                                throw std::runtime_error(QString("此頁至少需要 %1 影像記憶體預算（目前 %2），請降低 DPI 或提高預算")
                                                             .arg(formatMiB(cost), formatMiB(queue.budget)).toStdString());
                            if (!queue.reserve(cost)) break;
                            EncodingQueue::Reservation reservation(queue, cost);
                            // QPdfDocument::render 回傳帶透明度的影像；合成到白底 RGB32，避免 JPEG 透明區變黑。
                            QImage source32 = doc.render(p, QSize(int(wd), int(hd)));
                            if (source32.isNull()) throw std::runtime_error("PDF 頁面渲染失敗");
                            QImage image(source32.size(), QImage::Format_RGB32);
                            if (image.isNull()) throw std::runtime_error("記憶體不足，無法建立影像");
                            image.fill(Qt::white);
                            { QPainter painter(&image); painter.drawImage(0, 0, source32); }
                            source32 = QImage();
                            image.setDotsPerMeterX(qRound(settings.dpi / 0.0254));
                            image.setDotsPerMeterY(qRound(settings.dpi / 0.0254));
                            if (stopped) break;
                            queue.submit({std::move(image), target, cost, batchKey});
                            reservation.transfer();
                        } catch (const std::exception &e) {
                            { std::lock_guard lock(counts); ++failed; addError(QString("第 %1 頁：%2").arg(p + 1).arg(QString::fromUtf8(e.what()))); }
                            ++pagesDone;
                            emitProgress(false);
                        }
                    }
                    queue.drain();
                    peakBytes = std::max(peakBytes, queue.peak);
                    peakJobs = std::max(peakJobs, queue.peakJobs);
                }
            }
            status = (pages > 0 && saved + skipped == pages) ? "completed" : (stopped ? "cancelled" : "failed");
        } catch (const std::exception &e) {
            std::lock_guard lock(counts);
            addError(QString::fromUtf8(e.what()));
            status = stopped ? "cancelled" : "failed";
        } catch (...) {
            std::lock_guard lock(counts);
            addError("未預期的檔案處理例外");
            status = stopped ? "cancelled" : "failed";
        }
        // v1.1 identity is useful even when no page finished; never delete batch records.
        if (!folder.isEmpty() && createdFolder && saved + skipped == 0
            && !QFileInfo::exists(QDir(folder).filePath(".pdf2jpg-batch.json"))) {
            if (!QDir().rmdir(folder)) {
                std::lock_guard lock(counts);
                addError("無法移除空輸出資料夾（可能含殘留檔案）：" + folder);
            }
        }
        if (!stopped) { // 讓整批進度與總頁數對齊（例如開檔失敗的檔案，其預掃描頁數要補上）
            const qint64 handled = qint64(saved) + skipped + failed;
            if (handled < counted) pagesDone += counted - handled;
        }
    }
    totalSaved += saved;
    totalSkipped += skipped;
    emitProgress(true);
    const QString label = status == "completed" ? "完成" : status == "failed" ? "失敗"
                        : status == "cancelled" ? "已取消（保留完成頁面）" : "未處理";
    emit message(QString("%1：%2，已存 %3 / %4 頁%5").arg(QFileInfo(source).fileName(), label).arg(saved + skipped).arg(pages)
                     .arg(skipped ? QString("（續傳略過 %1 頁）").arg(skipped) : QString()));
    if (dropped) emit message(QString("%1：另有 %2 筆錯誤未逐筆列出").arg(QFileInfo(source).fileName()).arg(dropped));
    return QJsonObject{{"source", source}, {"folder", folder}, {"status", status}, {"pages", pages},
                       {"saved", saved}, {"skipped", skipped}, {"pageErrors", failed},
                       {"errors", errors}, {"errorsDropped", dropped}};
}

void Converter::run() {
    try {
        clock.start();
        pagesDone = 0; pagesTotal = 0; lastEmitMs = -1000; currentFile = 0;
        totalSaved = totalSkipped = peakBytes = 0; peakJobs = 0;
        QJsonArray files;
        QString fatal;
        try {
            const QString invalid = settings.validate();
            if (!invalid.isEmpty()) throw std::runtime_error(("無效設定：" + invalid).toStdString());
            if (!QDir().mkpath(settings.output)) throw std::runtime_error(ioError(settings.output, "無法建立輸出目錄").toStdString());
            fileCount = int(settings.files.size());

            // 開始前掃描：總頁數（供整批進度）、記憶體預算與磁碟空間提醒。
            std::vector<qint64> counted(size_t(fileCount), 0);
            qint64 estimateTotal = 0;
            emit message("掃描頁數…");
            for (int f = 0; f < fileCount && !stopped; ++f) {
                const Scan scan = scanPdf(settings.files[f], settings.dpi, stopped);
                const QString name = QFileInfo(settings.files[f]).fileName();
                if (scan.encrypted) emit message("需要密碼：" + name + "（處理時會詢問）");
                if (scan.pages > 0) {
                    counted[size_t(f)] = scan.pages;
                    pagesTotal += scan.pages;
                    estimateTotal = std::min(std::numeric_limits<qint64>::max() - scan.estimateBytes, estimateTotal) + scan.estimateBytes;
                    if (scan.maxCost > qint64(settings.memoryMiB) * kMiB)
                        emit message(QString("警告：%1 有頁面至少需要 %2 預算（目前 %3），這些頁面會失敗；請降低 DPI 或提高預算。")
                                         .arg(name, formatMiB(scan.maxCost), formatMiB(qint64(settings.memoryMiB) * kMiB)));
                }
            }
            const QStorageInfo storage(settings.output);
            if (storage.isValid() && estimateTotal > 0 && double(estimateTotal) * 1.1 > double(storage.bytesAvailable()))
                emit message(QString("警告：預估輸出約 %1，但磁碟可用空間只有 %2。").arg(formatMiB(estimateTotal), formatMiB(storage.bytesAvailable())));
            emitProgress(true);

            QSet<QString> usedNames;
            for (int f = 0; f < fileCount; ++f) files.append(processFile(f, counted[size_t(f)], usedNames));
        } catch (const std::exception &e) {
            fatal = QString::fromUtf8(e.what());
            emit message(fatal);
        } catch (...) {
            fatal = "未預期的工作例外";
            emit message(fatal);
        }

    #ifdef PDF2JPG_TESTING
        if (qEnvironmentVariableIsSet("PDF2JPG_FAIL_FINAL")) throw std::bad_alloc();
    #endif
        QJsonObject result{{"files", files}, {"saved", totalSaved}, {"skipped", totalSkipped},
                           {"pagesDone", pagesDone.load()}, {"pagesTotal", pagesTotal.load()},
                           {"cancelRequested", stopped.load()}, {"elapsedMs", clock.elapsed()},
                           {"peakImageReservationBytes", peakBytes}, {"peakOutstandingJobs", peakJobs},
                           {"encoders", settings.encoders}, {"dpi", settings.dpi}, {"quality", settings.quality},
                           {"resume", settings.resume}, {"fatal", fatal}};
        if (QDir(settings.output).exists()) {
            const QString log = QDir(settings.output).filePath("batch-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".json");
            const QByteArray bytes = QJsonDocument(result).toJson();
            QSaveFile file(log);
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
                result["logError"] = ioError(log, file.errorString());
                emit message(result["logError"].toString());
            } else {
                result["logPath"] = log;
                emit message("批次紀錄：" + log);
            }
        }
        emitProgress(true);
        emit batchFinished(result);
    } catch (...) {
        // Allocation or receiver failures must never escape QThread's entry point.
        stopped = true;
        try { emit batchFinished(QJsonObject{{"fatal", QStringLiteral("工作異常終止（記憶體不足或通知失敗）")}, {"cancelRequested", true}}); } catch (...) {}
    }
}
