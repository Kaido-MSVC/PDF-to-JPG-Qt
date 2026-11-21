// Private engine checks: include the implementation to exercise its internal queue/math.
#include "../converter.cpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstdio>
#include <utility>

static_assert(_MSVC_LANG >= 202302L);
static_assert(std::to_underlying(QPdfDocumentRenderOptions::RenderFlag::Annotations) == 1);
static void check(bool ok) { if (!ok) throw std::runtime_error("engine assertion failed"); }
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        int rejected = 0;
        const double inf = std::numeric_limits<double>::infinity();
        const double nan = std::numeric_limits<double>::quiet_NaN();
        for (QSizeF size : {QSizeF(0,1), QSizeF(-1,1), QSizeF(inf,1), QSizeF(nan,1), QSizeF(1e300,1e300), QSizeF(1e10,1e10)}) {
            try { pageGeometry(size, 600); } catch (const std::runtime_error &) { ++rejected; }
        }
        check(rejected == 6);
        const auto page = pageGeometry(QSizeF(72,72),600);
        check(page.size == QSize(600,600) && page.bytes == 600LL*600*12+8*kMiB);
        check(kJpegDecodeLimitMiB == 306);
        check(pageName("draft%1", 0) == "draft%1_p0001.jpg");
        std::atomic<bool> cancelled{false};
        qputenv("PDF2JPG_FAIL_THREAD", "2");
        bool threw=false;
        try { EncodingQueue q(4,128*kMiB,92,cancelled,[](QString,bool){}); } catch (...) { threw=true; }
        qunsetenv("PDF2JPG_FAIL_THREAD"); check(threw);
        QTemporaryDir dir(QDir::currentPath()+"/engine-test-XXXXXX"); check(dir.isValid());
        for (int i=0; i<100; ++i) {
            cancelled=false;
            EncodingQueue q(4,128*kMiB,92,cancelled,[](QString,bool){throw std::bad_alloc();});
            const qint64 cost=8*kMiB;
            check(q.reserve(cost));
            { EncodingQueue::Reservation r(q,cost); }
            check(q.used==0 && q.outstanding==0);
            check(q.reserve(cost));
            { EncodingQueue::Reservation r(q,cost);
              QImage im(16,16,QImage::Format_RGB32); im.fill(Qt::white);
              q.submit({im,dir.filePath(QString::number(i)+".jpg"),cost,{}}); r.transfer(); }
            bool failed=false;
            try { q.drain(); } catch (...) { failed=true; }
            q.shutdown();
            check(failed && q.failure && q.used==0 && q.outstanding==0);
        }
        std::puts("PASS: 6 invalid dimensions, valid arithmetic, partial construction, 100 callback failures, reservation/drain invariants");
        return 0;
    } catch (const std::exception &e) { std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
