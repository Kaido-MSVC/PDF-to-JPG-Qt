#pragma once
// Private implementation: immutable batch identity and per-page completion receipts.

class NativeGuard {
    HANDLE handle = INVALID_HANDLE_VALUE;
public:
    NativeGuard(const QString &path, bool source) {
        QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
        if (!source && (QFileInfo(path).isSymLink() || (QFileInfo::exists(path) && !QFileInfo(path).isFile())))
            throw std::runtime_error("批次鎖路徑衝突");
        if (!native.startsWith("\\\\?\\")) native = native.startsWith("\\\\")
            ? "\\\\?\\UNC\\" + native.mid(2) : "\\\\?\\" + native;
        handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), GENERIC_READ | (source ? 0 : GENERIC_WRITE),
                             source ? FILE_SHARE_READ : 0, nullptr, source ? OPEN_EXISTING : OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            throw std::runtime_error(ioError(path, source ? "無法鎖定來源（可能正被修改）" : "批次正在使用或無法取得獨占鎖").toStdString());
    }
    NativeGuard(const NativeGuard &) = delete;
    NativeGuard &operator=(const NativeGuard &) = delete;
    ~NativeGuard() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};

QString contentHash(const QString &path, const std::atomic<bool> *cancel = nullptr) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error(ioError(path, file.errorString()).toStdString());
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        if (cancel && cancel->load()) throw std::runtime_error("已取消內容指紋檢查");
        const QByteArray bytes = file.read(1024 * 1024);
        if (bytes.isEmpty() && file.error() != QFileDevice::NoError)
            throw std::runtime_error(ioError(path, file.errorString()).toStdString());
        hash.addData(bytes);
    }
    return QString::fromLatin1(hash.result().toHex());
}

void publishJson(const QString &path, const QJsonObject &object) {
    QTemporaryFile file(QDir(QFileInfo(path).absolutePath()).filePath(".pdf2jpg-XXXXXX.tmp"));
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (!file.open() || file.write(bytes) != bytes.size() || !file.flush() || !file.rename(path))
        throw std::runtime_error(ioError(path, "紀錄發布失敗（不覆寫既有紀錄）").toStdString());
    file.setAutoRemove(false);
}

QJsonObject readRecord(const QString &path) {
    QFileInfo info(path);
    if (!info.isFile() || info.isSymLink() || info.size() > 65536)
        throw std::runtime_error(ioError(path, "紀錄遺失或無效；保留原檔").toStdString());
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error(ioError(path, file.errorString()).toStdString());
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        throw std::runtime_error(ioError(path, "紀錄不完整；保留原檔").toStdString());
    return doc.object();
}

QString pageName(const QString &stem, int page) {
    return QString("%1_p%2.jpg").arg(stem).arg(page + 1, 4, 10, QChar('0'));
}

QJsonObject batchIdentity(const Settings &s, const QString &hash, const QString &stem, int pages) {
    return {{"schema", 1}, {"pdfSha256", hash}, {"stem", stem}, {"pages", pages},
            {"dpi", s.dpi}, {"quality", s.quality}, {"qt", QString::fromLatin1(qVersion())},
            {"outputContract", "qt-pdf-default/ceil/rgb32-white/jpeg-optimized-false/dpm-v1"}};
}

QString identityHash(const QJsonObject &identity) {
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(identity).toJson(QJsonDocument::Compact),
                                                       QCryptographicHash::Sha256).toHex());
}

void writeReceipt(const QString &path, const QSize &size, const QString &batchKey) {
#ifdef PDF2JPG_TESTING
    if (qEnvironmentVariableIsSet("PDF2JPG_FAIL_RECEIPT")) throw std::runtime_error("injected receipt failure");
#endif
    publishJson(path + ".done.json", {{"schema", 1}, {"sha256", contentHash(path)},
                                     {"batch", batchKey}, {"name", QFileInfo(path).fileName()},
                                     {"width", size.width()}, {"height", size.height()}});
}

// Called before starting encoders: decoding remains within the existing per-page budget.
QSet<int> validateExisting(const QString &folder, const QString &stem, QPdfDocument &doc,
                          const Settings &s, const QString &batchKey, const std::atomic<bool> &cancel) {
    QSet<QString> allowed{".pdf2jpg-batch.json", ".pdf2jpg.lock"};
    QSet<int> complete;
    for (int p = 0; p < doc.pageCount(); ++p) {
        if (cancel) throw std::runtime_error("已取消續傳驗證");
        const QString name = pageName(stem, p), path = QDir(folder).filePath(name);
        allowed.insert(name); allowed.insert(name + ".done.json");
        const QFileInfo imageInfo(path), receiptInfo(path + ".done.json");
        if (!imageInfo.exists() && !receiptInfo.exists() && !imageInfo.isSymLink() && !receiptInfo.isSymLink()) continue;
        if (!imageInfo.isFile() || imageInfo.isSymLink())
            throw std::runtime_error(ioError(path, "已完成頁面遺失或路徑衝突").toStdString());
        const auto receipt = readRecord(receiptInfo.filePath());
        const auto pt = doc.pagePointSize(p);
        const auto cost = pageCost(pt, s.dpi);
        if (cost > qint64(s.memoryMiB) * kMiB) throw std::runtime_error("驗證 JPG 所需影像預算不足");
        const QSize expected(int(std::ceil(pt.width() * s.dpi / 72.0)), int(std::ceil(pt.height() * s.dpi / 72.0)));
        if (receipt["schema"].toInt() != 1 || receipt["batch"].toString() != batchKey || receipt["name"].toString() != name
            || receipt["width"].toInt() != expected.width()
            || receipt["height"].toInt() != expected.height() || receipt["sha256"].toString() != contentHash(path, &cancel))
            throw std::runtime_error(ioError(path, "JPG 憑證或尺寸不匹配；不覆寫原檔").toStdString());
        QImageReader reader(path, "jpg");
        if (reader.size() != expected || reader.read().size() != expected)
            throw std::runtime_error(ioError(path, "JPG 無法完整解碼或尺寸不匹配").toStdString());
        complete.insert(p);
    }
    for (const auto &info : QDir(folder).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
        if (!allowed.contains(info.fileName()) || !info.isFile() || info.isSymLink())
            throw std::runtime_error(ioError(info.filePath(), "批次含未知或中斷殘留檔案；不刪除原檔").toStdString());
    }
    return complete;
}
