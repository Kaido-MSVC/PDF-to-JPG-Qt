#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#include "converter.h"
#include <QtWidgets> // 為了避免在無法編譯驗證的情況下漏標頭，先保留整包；穩定後可改成個別標頭
#include <QDesktopServices>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleHints>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

// 自動化測試用功能（--smoke / --batch / heartbeat / 測試密碼 / 截圖）。
// Debug 建置預設啟用；Release 需自行定義 PDF2JPG_TESTING 才會編入，避免隨正式版出貨。
#if defined(PDF2JPG_TESTING) || !defined(NDEBUG)
#define PDF2JPG_TEST_HOOKS 1
#endif

namespace {

QString formatDuration(qint64 seconds) {
    const qint64 h = seconds / 3600, m = (seconds % 3600) / 60, s = seconds % 60;
    if (h) return QString("%1 時 %2 分").arg(h).arg(m);
    if (m) return QString("%1 分 %2 秒").arg(m).arg(s);
    return QString("%1 秒").arg(s);
}

// 長時間批次期間避免系統睡眠（目前只有 Windows 實作）。
void blockSystemSleep(bool on) {
#ifdef Q_OS_WIN
    SetThreadExecutionState(on ? (ES_CONTINUOUS | ES_SYSTEM_REQUIRED) : ES_CONTINUOUS);
#else
    Q_UNUSED(on)
#endif
}

void applyTheme(int index) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    qApp->styleHints()->setColorScheme(index == 1 ? Qt::ColorScheme::Light
                                      : index == 2 ? Qt::ColorScheme::Dark : Qt::ColorScheme::Unknown);
#else
    Q_UNUSED(index) // Qt < 6.8 沒有 QStyleHints::setColorScheme
#endif
}

QString resourceText(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

// 顯示授權文字；第三方清單另提供開啟 licenses 資料夾（授權全文）的按鈕。
void showLicenseText(QWidget *parent, const QString &title, const QString &resource, bool markdown) {
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.resize(760, 600);
    auto *layout = new QVBoxLayout(&dialog);
    auto *view = new QTextBrowser;
    view->setOpenExternalLinks(true);
    if (markdown) view->setMarkdown(resourceText(resource));
    else view->setPlainText(resourceText(resource));
    layout->addWidget(view);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    if (markdown) {
        const QString folder = QCoreApplication::applicationDirPath() + "/licenses";
        QPushButton *openFolder = buttons->addButton("開啟授權全文資料夾", QDialogButtonBox::ActionRole);
        openFolder->setEnabled(QFileInfo(folder).isDir());
        QObject::connect(openFolder, &QPushButton::clicked, &dialog,
                         [folder] { QDesktopServices::openUrl(QUrl::fromLocalFile(folder)); });
    }
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

void showAbout(QWidget *parent) {
    QDialog dialog(parent);
    dialog.setWindowTitle("關於 PDF to JPG_QT");
    auto *layout = new QVBoxLayout(&dialog);
    auto *header = new QHBoxLayout;
    auto *icon = new QLabel;
    icon->setPixmap(QIcon(":/assets/app.ico").pixmap(64, 64));
    header->addWidget(icon, 0, Qt::AlignTop);
    auto *text = new QLabel(QString(
        "<h2>PDF to JPG_QT</h2>"
        "<p>版本 %1</p>"
        "<p>Copyright © 2026 Kaido-MSVC<br>以 MIT 授權釋出。</p>"
        "<p>使用 Qt %2（含 Qt PDF / PDFium），依 LGPLv3 動態連結。<br>"
        "第三方元件與授權全文見「第三方授權」及隨附的 licenses 資料夾。</p>")
        .arg(QCoreApplication::applicationVersion(), QString::fromLatin1(qVersion())));
    text->setTextFormat(Qt::RichText);
    text->setWordWrap(true);
    header->addWidget(text, 1);
    layout->addLayout(header);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    QPushButton *license = buttons->addButton("授權條款", QDialogButtonBox::ActionRole);
    QPushButton *thirdParty = buttons->addButton("第三方授權", QDialogButtonBox::ActionRole);
    QPushButton *aboutQt = buttons->addButton("關於 Qt", QDialogButtonBox::ActionRole);
    QObject::connect(license, &QPushButton::clicked, &dialog,
                     [&dialog] { showLicenseText(&dialog, "授權條款（MIT）", ":/LICENSE", false); });
    QObject::connect(thirdParty, &QPushButton::clicked, &dialog,
                     [&dialog] { showLicenseText(&dialog, "第三方授權", ":/THIRD_PARTY_NOTICES.md", true); });
    QObject::connect(aboutQt, &QPushButton::clicked, &dialog, [&dialog] { QMessageBox::aboutQt(&dialog); });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

#ifdef PDF2JPG_TEST_HOOKS
Settings settingsFromJson(const QJsonObject &o) {
    Settings s;
    for (const auto &v : o["files"].toArray()) s.files << v.toString();
    s.output = o["output"].toString();
    s.dpi = o["dpi"].toInt(200);
    s.quality = o["quality"].toInt(92);
    s.encoders = o["encoders"].toInt(2);
    s.memoryMiB = o["memoryMiB"].toInt(512);
    s.resume = o["resume"].toBool(s.resume);
    s.resumeFolder = o["resumeFolder"].toString();
    s.maxErrorsKept = o["maxErrorsKept"].toInt(s.maxErrorsKept);
    s.passwordRetries = o["passwordRetries"].toInt(s.passwordRetries);
    s.testPassword = o["password"].toString();
    return s;
}
#endif

} // namespace

class Window final : public QWidget {
    // --- 控制項 ---
    QListWidget *list = new QListWidget;
    QLineEdit *output = new QLineEdit;
    QComboBox *dpi = new QComboBox, *theme = new QComboBox;
    QSpinBox *quality = new QSpinBox, *encoders = new QSpinBox, *memory = new QSpinBox;
    QCheckBox *resume = new QCheckBox("中斷續傳（預設關閉；一次一個 PDF）");
    QLineEdit *resumeFolder = new QLineEdit;
    QPushButton *chooseBatch = new QPushButton("選擇既有批次…");
    QPushButton *start = new QPushButton("開始轉換"), *cancel = new QPushButton("取消"),
                *add = new QPushButton("加入 PDF"), *addDir = new QPushButton("加入資料夾"),
                *remove = new QPushButton("移除選取"), *clear = new QPushButton("清空"),
                *browse = new QPushButton("瀏覽…");
    QProgressBar *progress = new QProgressBar;
    QLabel *status = new QLabel("準備就緒");
    QPlainTextEdit *log = new QPlainTextEdit;

    // --- 執行狀態 ---
    Converter *worker = nullptr;
    QPointer<QInputDialog> passwordDialog;
    bool closeWhenDone = false;
    QString lastDir;

    // --- 節流：worker 的 signal 只更新這些值，由 uiTimer 統一刷新畫面 ---
    QTimer *uiTimer = new QTimer(this);
    QStringList pendingLog;
    qint64 pagesDone = 0, pagesTotal = 0;
    int currentFile = 0, fileCount = 0;
    bool progressDirty = false;
    QElapsedTimer runClock;
    QJsonObject lastResult;

#ifdef PDF2JPG_TEST_HOOKS
    QJsonObject test;
    int heartbeatTicks = 0;
#endif

public:
    Window() {
        buildUi();
        loadPrefs();
        connectSignals();
        setAcceptDrops(true);
        uiTimer->setInterval(100);
        connect(uiTimer, &QTimer::timeout, this, [this] { refreshUi(); });
    }

    void addFiles(const QStringList &paths) {
        QSet<QString> existing;
        for (int i = 0; i < list->count(); ++i) existing.insert(list->item(i)->data(Qt::UserRole).toString().toLower());
        auto addOne = [&](const QString &file) {
            const QFileInfo info(file);
            if (!info.isFile() || info.suffix().compare("pdf", Qt::CaseInsensitive) != 0) return;
            const QString canonical = info.canonicalFilePath();
            const QString path = canonical.isEmpty() ? info.absoluteFilePath() : canonical;
            if (existing.contains(path.toLower())) return;
            existing.insert(path.toLower());
            auto *item = new QListWidgetItem(info.fileName());
            item->setData(Qt::UserRole, path); // 實際路徑放在 UserRole，畫面只顯示檔名
            item->setToolTip(path);
            list->addItem(item);
        };
        for (const QString &p : paths) {
            if (QFileInfo(p).isDir()) { // 資料夾：遞迴加入其中所有 PDF
                QDirIterator it(p, QDir::Files, QDirIterator::Subdirectories);
                while (it.hasNext()) addOne(it.next());
            } else {
                addOne(p);
            }
        }
        status->setText(QString("清單共 %1 個 PDF").arg(list->count()));
    }

    void requestCancel() {
        if (!worker) return;
        worker->cancel();
        status->setText("取消中：等待目前渲染及正在寫入的頁面結束…");
        cancel->setEnabled(false);
        if (passwordDialog) passwordDialog->reject();
    }

#ifdef PDF2JPG_TEST_HOOKS
    void smoke(const QJsonObject &cfg) {
        test = cfg;
        addFiles(settingsFromJson(cfg).files);
        output->setText(cfg["output"].toString());
        dpi->setCurrentText(QString::number(cfg["dpi"].toInt(200)));
        quality->setValue(cfg["quality"].toInt(92));
        encoders->setValue(cfg["encoders"].toInt(2));
        memory->setValue(cfg["memoryMiB"].toInt(512));
        resume->setChecked(cfg["resume"].toBool());
        resumeFolder->setText(cfg["resumeFolder"].toString());
        theme->setCurrentIndex(cfg["dark"].toBool() ? 2 : 1);
        auto *heartbeat = new QTimer(this); // 只在測試模式量測 GUI 是否被卡住
        heartbeat->setInterval(20);
        connect(heartbeat, &QTimer::timeout, this, [this] { if (worker) ++heartbeatTicks; });
        heartbeat->start();
        QTimer::singleShot(200, this, [this] {
            Settings s = settingsFromUi();
            s.testPassword.clear(); // GUI 路徑要走密碼對話方塊
            if (!s.validate().isEmpty()) { qApp->exit(2); return; }
            startWorker(s);
            if (test.contains("closeMs")) QTimer::singleShot(test["closeMs"].toInt(), this, [this] { close(); });
            if (test.contains("cancelMs")) QTimer::singleShot(test["cancelMs"].toInt(), this, [this] { requestCancel(); });
        });
    }
#endif

protected:
    void closeEvent(QCloseEvent *e) override {
        if (worker) {
            if (qApp->isSavingSession()) { // 系統關機／登出：不能擋，取消並等待結束
                worker->cancel();
                if (passwordDialog) passwordDialog->reject();
                worker->wait();
                savePrefs();
                e->accept();
                return;
            }
            closeWhenDone = true;
            requestCancel();
            e->ignore();
            return;
        }
        savePrefs();
        e->accept();
    }
    void dragEnterEvent(QDragEnterEvent *e) override {
        if (!worker && e->mimeData()->hasUrls()) e->acceptProposedAction();
    }
    void dropEvent(QDropEvent *e) override {
        if (worker) return;
        QStringList paths;
        for (const QUrl &u : e->mimeData()->urls()) if (u.isLocalFile()) paths << u.toLocalFile();
        addFiles(paths);
    }

private:
    // ------------------------------------------------------------------ UI
    void buildUi() {
        setWindowTitle("PDF to JPG_QT");
        setWindowIcon(QIcon(":/assets/app.ico"));
        resize(850, 760);
        setMinimumSize(680, 620);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(24, 20, 24, 20);
        root->setSpacing(12);

        auto *top = new QHBoxLayout;
        auto *title = new QLabel("PDF → JPG");
        QFont font = title->font();
        font.setPointSize(22);
        font.setBold(true);
        title->setFont(font);
        top->addWidget(title);
        top->addStretch();
        theme->addItems({"跟隨系統", "淺色", "深色"});
#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
        theme->setVisible(false);
#endif
        top->addWidget(theme);
        auto *about = new QPushButton("關於");
        top->addWidget(about);
        connect(about, &QPushButton::clicked, this, [this] { showAbout(this); });
        root->addLayout(top);
        root->addWidget(new QLabel("批次轉換逐頁 JPG · 在本機處理 · 保留已完成頁面 · 不覆寫既有檔案"));

        auto *buttons = new QHBoxLayout;
        for (auto *b : {add, addDir, remove, clear}) buttons->addWidget(b);
        buttons->addStretch();
        root->addLayout(buttons);
        list->setSelectionMode(QAbstractItemView::ExtendedSelection);
        list->setTextElideMode(Qt::ElideMiddle);
        root->addWidget(list, 2);

        auto *outRow = new QHBoxLayout;
        outRow->addWidget(new QLabel("輸出資料夾"));
        outRow->addWidget(output);
        outRow->addWidget(browse);
        auto *open = new QPushButton("開啟輸出");
        outRow->addWidget(open);
        root->addLayout(outRow);
        connect(open, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(output->text())); });

        for (int d : kDpiChoices) dpi->addItem(QString::number(d));
        quality->setRange(1, 100);
        encoders->setRange(1, kMaxEncoders);
        memory->setRange(kMinMemoryMiB, kMaxMemoryMiB);
        memory->setSingleStep(128);
        memory->setSuffix(" MiB");
        auto *form = new QGridLayout;
        form->addWidget(new QLabel("DPI"), 0, 0);
        form->addWidget(dpi, 0, 1);
        form->addWidget(new QLabel("JPEG 品質"), 0, 2);
        form->addWidget(quality, 0, 3);
        form->addWidget(new QLabel("編碼執行緒"), 1, 0);
        form->addWidget(encoders, 1, 1);
        form->addWidget(new QLabel("影像記憶體預算"), 1, 2);
        form->addWidget(memory, 1, 3);
        form->addWidget(resume, 2, 0, 1, 4);
        form->addWidget(resumeFolder, 3, 0, 1, 3);
        form->addWidget(chooseBatch, 3, 3);
        resumeFolder->setPlaceholderText("選擇與目前版本相容的 PDF 輸出資料夾");
        resumeFolder->setReadOnly(true);
        resumeFolder->setEnabled(false);
        chooseBatch->setEnabled(false);
        root->addLayout(form);
        auto *note = new QLabel("預算涵蓋渲染、等候與編碼影像；PDF 解碼器等額外記憶體另計。600 DPI 建議至少 512 MiB。"
                                "續傳關閉時每次建立新資料夾；啟用時先驗證既有 JPG，衝突或紀錄不完整會停止並保留原檔。");
        note->setWordWrap(true);
        root->addWidget(note);

        root->addWidget(progress);
        root->addWidget(status);
        log->setReadOnly(true);
        log->setMaximumBlockCount(5000);
        root->addWidget(log, 1);

        auto *actions = new QHBoxLayout;
        auto *saveLog = new QPushButton("匯出畫面紀錄");
        actions->addWidget(saveLog);
        actions->addStretch();
        actions->addWidget(start);
        actions->addWidget(cancel);
        root->addLayout(actions);
        cancel->setEnabled(false);
        progress->setRange(0, 1000);

        connect(saveLog, &QPushButton::clicked, this, [this] {
            const QString p = QFileDialog::getSaveFileName(this, "匯出紀錄", lastDir, "文字 (*.txt)");
            if (p.isEmpty()) return;
            QSaveFile f(p);
            const QByteArray bytes = log->toPlainText().toUtf8();
            if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit())
                QMessageBox::warning(this, "寫入失敗", f.errorString());
        });
    }

    void connectSignals() {
        connect(theme, &QComboBox::currentIndexChanged, this, [](int i) { applyTheme(i); });
        connect(add, &QPushButton::clicked, this, [this] {
            const QStringList files = QFileDialog::getOpenFileNames(this, "選擇 PDF", lastDir, "PDF (*.pdf)");
            if (!files.isEmpty()) { lastDir = QFileInfo(files.first()).absolutePath(); addFiles(files); }
        });
        connect(addDir, &QPushButton::clicked, this, [this] {
            const QString dir = QFileDialog::getExistingDirectory(this, "選擇含有 PDF 的資料夾", lastDir);
            if (!dir.isEmpty()) { lastDir = dir; addFiles({dir}); }
        });
        connect(remove, &QPushButton::clicked, this, [this] { qDeleteAll(list->selectedItems()); });
        connect(clear, &QPushButton::clicked, list, &QListWidget::clear);
        connect(browse, &QPushButton::clicked, this, [this] {
            const QString p = QFileDialog::getExistingDirectory(this, "輸出資料夾", output->text());
            if (!p.isEmpty()) output->setText(p);
        });
        connect(start, &QPushButton::clicked, this, [this] { begin(); });
        connect(resume, &QCheckBox::toggled, this, [this](bool on) {
            resumeFolder->setEnabled(on); chooseBatch->setEnabled(on);
        });
        connect(chooseBatch, &QPushButton::clicked, this, [this] {
            const QString p = QFileDialog::getExistingDirectory(this, "選擇該 PDF 的既有批次資料夾", output->text());
            if (!p.isEmpty()) resumeFolder->setText(p);
        });
        connect(cancel, &QPushButton::clicked, this, [this] { requestCancel(); });
    }

    void setBusy(bool busy) {
        for (QWidget *w : QList<QWidget *>{add, addDir, remove, clear, browse, output, dpi, quality, encoders, memory, resume, start})
            w->setEnabled(!busy);
        resumeFolder->setEnabled(!busy && resume->isChecked());
        chooseBatch->setEnabled(!busy && resume->isChecked());
        cancel->setEnabled(busy);
    }

    // ------------------------------------------------------------ 偏好設定
    static QString defaultOutput() {
        const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        return QDir(pictures.isEmpty() ? QDir::homePath() : pictures).filePath("JPG輸出");
    }

    void loadPrefs() {
#ifdef PDF2JPG_TEST_HOOKS
        QSettings s(QDir(QCoreApplication::applicationDirPath()).filePath("test-settings.ini"), QSettings::IniFormat);
#else
        QSettings s;
#endif
        output->setText(s.value("output", defaultOutput()).toString());
        dpi->setCurrentText(s.value("dpi", 200).toString());
        quality->setValue(s.value("quality", 92).toInt());
        encoders->setValue(s.value("encoders", std::clamp(QThread::idealThreadCount() / 2, 1, 4)).toInt());
        memory->setValue(s.value("memoryMiB", 512).toInt());
        resume->setChecked(false);
        lastDir = s.value("lastDir").toString();
        theme->setCurrentIndex(std::clamp(s.value("theme", 0).toInt(), 0, 2));
        applyTheme(theme->currentIndex());
    }

    void savePrefs() const {
#ifdef PDF2JPG_TEST_HOOKS
        QSettings s(QDir(QCoreApplication::applicationDirPath()).filePath("test-settings.ini"), QSettings::IniFormat);
#else
        QSettings s;
#endif
        s.setValue("output", output->text().trimmed());
        s.setValue("dpi", dpi->currentText().toInt());
        s.setValue("quality", quality->value());
        s.setValue("encoders", encoders->value());
        s.setValue("memoryMiB", memory->value());
        s.setValue("lastDir", lastDir);
        s.setValue("theme", theme->currentIndex());
    }

    // ------------------------------------------------------------ 開始 / 結束
    Settings settingsFromUi() const {
        Settings s;
        for (int i = 0; i < list->count(); ++i) s.files << list->item(i)->data(Qt::UserRole).toString();
        s.output = output->text().trimmed();
        s.dpi = dpi->currentText().toInt();
        s.quality = quality->value();
        s.encoders = encoders->value();
        s.memoryMiB = memory->value();
        s.resume = resume->isChecked();
        s.resumeFolder = resumeFolder->text();
        return s;
    }

    void begin() {
        if (worker) return;
        const Settings s = settingsFromUi();
        const QString problem = s.validate();
        if (!problem.isEmpty()) { QMessageBox::information(this, "無法開始", problem); return; }
        startWorker(s);
    }

    void startWorker(const Settings &s) {
        savePrefs();
        setBusy(true);
        progress->setValue(0);
        log->clear();
        pendingLog.clear();
        pagesDone = pagesTotal = 0;
        currentFile = fileCount = 0;
        progressDirty = false;
        lastResult = {};
        status->setText("開始處理…");
        runClock.start();
        blockSystemSleep(true);
        uiTimer->start();

        worker = new Converter(s, this);
        connect(worker, &Converter::message, this, [this](const QString &text) { pendingLog << text; }, Qt::QueuedConnection);
        connect(worker, &Converter::progress, this, [this](qint64 done, qint64 total, int file, int files) {
            pagesDone = done; pagesTotal = total; currentFile = file; fileCount = files; progressDirty = true;
        }, Qt::QueuedConnection);
        connect(worker, &Converter::passwordNeeded, this,
                [this](const QString &source, bool retry) { promptPassword(source, retry); }, Qt::QueuedConnection);
        connect(worker, &Converter::batchFinished, this,
                [this](const QJsonObject &result) { lastResult = result; }, Qt::QueuedConnection);
        // batchFinished 先於 finished 由同一執行緒送出，queued 順序保證 lastResult 在 onFinished 前已就緒。
        connect(worker, &QThread::finished, this, [this] { onFinished(); }, Qt::QueuedConnection);
        worker->start();
    }

    void refreshUi() {
        if (!pendingLog.isEmpty()) {
            log->appendPlainText(pendingLog.join('\n'));
            pendingLog.clear();
        }
        if (!progressDirty) return;
        progressDirty = false;
        const double fraction = pagesTotal > 0 ? double(pagesDone) / double(pagesTotal) : 0.0;
        progress->setValue(int(1000.0 * std::clamp(fraction, 0.0, 1.0)));
        QString eta;
        if (pagesDone > 0 && pagesTotal > pagesDone) { // 以整體平均速度粗估，續傳略過的頁面會讓估計偏樂觀
            const double elapsed = double(runClock.elapsed()) / 1000.0;
            eta = QString(" · 約剩 %1").arg(formatDuration(qint64(elapsed * double(pagesTotal - pagesDone) / double(pagesDone))));
        }
        status->setText(QString("檔案 %1 / %2 · 已處理 %3 / %4 頁%5").arg(currentFile).arg(fileCount).arg(pagesDone).arg(pagesTotal).arg(eta));
    }

    void promptPassword(const QString &source, bool retry) {
        if (!worker) return;
        auto *dialog = new QInputDialog(this);
        dialog->setWindowTitle("PDF 開啟密碼");
        dialog->setLabelText(source + (retry ? "\n密碼不正確，請重新輸入；取消可略過此檔案。" : "\n輸入密碼；取消可略過此檔案。"));
        dialog->setTextEchoMode(QLineEdit::Password);
        passwordDialog = dialog;
        connect(dialog, &QDialog::finished, this, [this, dialog](int code) {
            if (worker) worker->answerPassword(dialog->textValue(), code == QDialog::Accepted);
            dialog->deleteLater();
        });
        status->setText("等待輸入 PDF 密碼…（輸入期間主視窗無法操作；取消輸入會略過此檔案）");
        dialog->open();
#ifdef PDF2JPG_TEST_HOOKS
        if (!test.isEmpty() && !test["waitPassword"].toBool()) {
            QTimer::singleShot(100, this, [this, guard = QPointer<QInputDialog>(dialog)] {
                if (!guard) return;
                if (test.contains("password")) { guard->setTextValue(test["password"].toString()); guard->accept(); }
                else guard->reject();
            });
        }
#endif
    }

    bool interactive() const {
#ifdef PDF2JPG_TEST_HOOKS
        return test.isEmpty();
#else
        return true;
#endif
    }

    void onFinished() {
        uiTimer->stop();
        progressDirty = true;
        refreshUi();
        QJsonObject result = lastResult;
        if (result.isEmpty()) result["fatal"] = "工作已結束，但未收到完整結果。";
        worker->wait();
        worker->deleteLater();
        worker = nullptr;
        blockSystemSleep(false);
        setBusy(false);

        int completed = 0, failed = 0, cancelled = 0, unstarted = 0;
        for (const auto &v : result["files"].toArray()) {
            const QString s = v.toObject()["status"].toString();
            completed += s == "completed"; failed += s == "failed"; cancelled += s == "cancelled"; unstarted += s == "not_started";
        }
        const qint64 savedTotal = qint64(result["saved"].toDouble()) + qint64(result["skipped"].toDouble());
        const QString fatal = result["fatal"].toString();
        status->setText(QString("%1 · 完成 %2，失敗 %3，取消 %4，未處理 %5 · 已存 %6 頁 · %7 秒")
                            .arg(result["cancelRequested"].toBool() ? "已取消" : "處理結束")
                            .arg(completed).arg(failed).arg(cancelled).arg(unstarted).arg(savedTotal)
                            .arg(result["elapsedMs"].toDouble() / 1000, 0, 'f', 2));
        const bool clean = failed == 0 && cancelled == 0 && unstarted == 0 && fatal.isEmpty();
        if (clean) progress->setValue(1000);

#ifdef PDF2JPG_TEST_HOOKS
        if (!test.isEmpty()) {
            QJsonObject report = result;
            report["guiHeartbeatTicks"] = heartbeatTicks;
            report["resumeChecked"] = resume->isChecked();
            report["resumeEnabled"] = resume->isEnabled();
            report["chooseBatchEnabled"] = chooseBatch->isEnabled();
            QFile f(test["report"].toString());
            if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(report).toJson());
            QTimer::singleShot(100, this, [this] { grab().save(test["screenshot"].toString()); qApp->quit(); });
        }
#endif
        if (closeWhenDone) { close(); return; }
        if (!isActiveWindow()) QApplication::alert(this);
        if (interactive() && (failed > 0 || !fatal.isEmpty())) {
            const QString logPath = result["logPath"].toString();
            auto *box = new QMessageBox(QMessageBox::Warning, "轉換完成，但有問題",
                fatal.isEmpty() ? QString("有 %1 個檔案失敗。詳細原因請見畫面紀錄或批次紀錄檔。").arg(failed) : fatal,
                QMessageBox::Ok, this);
            QPushButton *openLog = logPath.isEmpty() ? nullptr : box->addButton("開啟紀錄檔", QMessageBox::ActionRole);
            box->setAttribute(Qt::WA_DeleteOnClose);
            connect(box, &QMessageBox::buttonClicked, this, [openLog, logPath](QAbstractButton *b) {
                if (openLog && b == openLog) QDesktopServices::openUrl(QUrl::fromLocalFile(logPath));
            });
            box->open();
        }
    }
};

#ifdef PDF2JPG_TEST_HOOKS
static int runBatch(QApplication &app, const QJsonObject &cfg) {
    Converter worker(settingsFromJson(cfg));
    QJsonObject result;
    QObject::connect(&worker, &Converter::passwordNeeded, &app,
                     [&](const QString &, bool) { worker.answerPassword({}, false); }, Qt::QueuedConnection);
    QObject::connect(&worker, &Converter::batchFinished, &app,
                     [&](const QJsonObject &r) { result = r; }, Qt::QueuedConnection);
    QObject::connect(&worker, &QThread::finished, &app, [&] {
        worker.wait();
        if (result.isEmpty()) result["fatal"] = "工作已結束，但未收到完整結果。";
        QFile out(cfg["report"].toString());
        if (!out.open(QIODevice::WriteOnly)) { app.exit(3); return; }
        out.write(QJsonDocument(result).toJson());
        app.exit(result["fatal"].toString().isEmpty() ? 0 : 1);
    }, Qt::QueuedConnection);
    if (cfg.contains("cancelMs")) QTimer::singleShot(cfg["cancelMs"].toInt(), &app, [&] { worker.cancel(); });
    worker.start();
    return app.exec();
}
#endif

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    // 續傳須能完整解碼程式允許的最大 JPEG。單頁尺寸與批次影像預算仍各自檢查。
    QImageReader::setAllocationLimit(kJpegDecodeLimitMiB);
    app.setApplicationName("PDF to JPG_QT");
    app.setApplicationVersion(APP_VERSION_STR); // 由 CMakeLists.txt 的 project(VERSION) 注入
    app.setOrganizationName("PDF2JPG"); // QSettings 需要

#ifdef PDF2JPG_TEST_HOOKS
    const QStringList args = app.arguments();
    if (args.size() == 3 && (args[1] == "--batch" || args[1] == "--smoke")) {
        QFile file(args[2]);
        if (!file.open(QIODevice::ReadOnly)) return 2;
        QJsonParseError error;
        const auto json = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !json.isObject()) return 2;
        const QJsonObject cfg = json.object();
        if (args[1] == "--smoke") {
            Window w;
            w.show();
            w.smoke(cfg);
            return app.exec();
        }
        return runBatch(app, cfg);
    }
#endif

    Window window;
    window.show();
    return app.exec();
}
