/*
 * This file is part of QtFM <https://qtfm.eu>
 *
 * Checksums of files: CRC-32, CRC-64, MD5, SHA-1, SHA-256 and SHA-512,
 * from the "Checksum" submenu. The files are read in another thread and
 * the results are shown in a dialog, where they can be copied and compared
 * with a known checksum.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "mainwindow.h"

#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDialog>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFile>
#include <QFontDatabase>

#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>

#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include <atomic>
#include <memory>

namespace {

enum Algorithm { Crc32 = 0, Crc64, Md5, Sha1, Sha256, Sha512 };

const char *algorithmNames[] = { "CRC-32", "CRC-64", "MD5", "SHA-1", "SHA-256", "SHA-512" };

//---------------------------------------------------------------------------
// CRC-32 (IEEE 802.3, as zlib, cksfv and 7-Zip) and CRC-64 (ECMA-182
// polynomial, reflected, as xz and 7-Zip: CRC-64/XZ)
//---------------------------------------------------------------------------
const quint32 *crc32Table()
{
    static quint32 table[256];
    static bool ready = false;
    if (!ready) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k) { c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; }
            table[i] = c;
        }
        ready = true;
    }
    return table;
}

const quint64 *crc64Table()
{
    static quint64 table[256];
    static bool ready = false;
    if (!ready) {
        for (quint64 i = 0; i < 256; ++i) {
            quint64 c = i;
            for (int k = 0; k < 8; ++k) { c = (c & 1) ? 0xC96C5795D7870F42ull ^ (c >> 1) : c >> 1; }
            table[i] = c;
        }
        ready = true;
    }
    return table;
}

/**
 * Computes the checksum of a file. Returns it as lowercase hex, or an empty
 * string and an error message. Stops (empty result) when canceled is set.
 */
QString checksumOfFile(const QString &path, int algorithm, const std::atomic_bool &canceled,
                       const std::function<void(qint64)> &progress, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return QString();
    }

    QCryptographicHash::Algorithm hashAlgorithm = QCryptographicHash::Md5;
    switch (algorithm) {
    case Sha1:   hashAlgorithm = QCryptographicHash::Sha1; break;
    case Sha256: hashAlgorithm = QCryptographicHash::Sha256; break;
    case Sha512: hashAlgorithm = QCryptographicHash::Sha512; break;
    default: break;
    }
    QCryptographicHash hash(hashAlgorithm);
    const quint32 *table32 = crc32Table();
    const quint64 *table64 = crc64Table();
    quint32 crc32 = 0xFFFFFFFFu;
    quint64 crc64 = 0xFFFFFFFFFFFFFFFFull;

    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    qint64 done = 0;
    while (true) {
        if (canceled) { return QString(); }
        const qint64 in = file.read(buffer.data(), buffer.size());
        if (in < 0) {
            *error = file.errorString();
            return QString();
        }
        if (in == 0) { break; }
        const uchar *data = reinterpret_cast<const uchar*>(buffer.constData());
        if (algorithm == Crc32) {
            for (qint64 i = 0; i < in; ++i) { crc32 = table32[(crc32 ^ data[i]) & 0xFF] ^ (crc32 >> 8); }
        } else if (algorithm == Crc64) {
            for (qint64 i = 0; i < in; ++i) { crc64 = table64[(crc64 ^ data[i]) & 0xFF] ^ (crc64 >> 8); }
        } else {
            hash.addData(QByteArrayView(buffer.constData(), in));
        }
        done += in;
        progress(done);
    }

    if (algorithm == Crc32) { return QString("%1").arg(crc32 ^ 0xFFFFFFFFu, 8, 16, QChar('0')); }
    if (algorithm == Crc64) { return QString("%1").arg(crc64 ^ 0xFFFFFFFFFFFFFFFFull, 16, 16, QChar('0')); }
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace

//---------------------------------------------------------------------------
/**
 * @brief Creates the "Checksum" submenu (one action per algorithm)
 */
QMenu *MainWindow::createChecksumMenu()
{
    QMenu *menu = new QMenu(tr("Checksum"), this);
    menu->setIcon(QIcon::fromTheme("security-high", QIcon::fromTheme("dialog-password")));
    for (int i = Crc32; i <= Sha512; ++i) {
        QAction *action = menu->addAction(QString::fromLatin1(algorithmNames[i]));
        action->setStatusTip(tr("Calculate the %1 checksum of the selected files")
                             .arg(QString::fromLatin1(algorithmNames[i])));
        connect(action, &QAction::triggered, this, [this, i]() { checksumFiles(i); });
    }
    return menu;
}

//---------------------------------------------------------------------------
/**
 * @brief Calculates a checksum of the selected files and shows the results
 * in a dialog (folders are skipped). The dialog is not modal: the files
 * are read in another thread and the window can be used meanwhile.
 * @param algorithm 0 CRC-32, 1 CRC-64, 2 MD5, 3 SHA-1, 4 SHA-256, 5 SHA-512
 */
void MainWindow::checksumFiles(int algorithm)
{
    // Selected files of the active pane
    QModelIndexList items = listSelectionModel->selectedRows(0);
    if (items.isEmpty()) { items = listSelectionModel->selectedIndexes(); }
    QStringList files;
    qint64 totalSize = 0;
    foreach (QModelIndex item, items) {
        const QModelIndex source = modelView->mapToSource(item);
        if (modelList->isDotDot(source)) { continue; }
        const QString path = modelList->filePath(source);
        const QFileInfo info(path); // a link counts as its target, as in sha256sum
        if (!info.isFile() || files.contains(path)) { continue; }
        files << path;
        totalSize += info.size();
    }
    if (files.isEmpty()) {
        status->showMessage(tr("Select one or more files to calculate their checksum."), 5000);
        return;
    }

    const QString name = QString::fromLatin1(algorithmNames[algorithm]);

    // Dialog
    QDialog *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("%1 checksum").arg(name));
    dialog->resize(720, 320);

    QLabel *info = new QLabel(dialog);
    info->setWordWrap(true);
    QPlainTextEdit *results = new QPlainTextEdit(dialog);
    results->setReadOnly(true);
    results->setLineWrapMode(QPlainTextEdit::NoWrap);
    results->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    QProgressBar *progressBar = new QProgressBar(dialog);
    progressBar->setRange(0, 1000);
    progressBar->setValue(0);

    QLineEdit *compareEdit = new QLineEdit(dialog);
    compareEdit->setPlaceholderText(tr("Paste a checksum here to compare it with the results"));
    compareEdit->setClearButtonEnabled(true);
    QLabel *compareResult = new QLabel(dialog);
    compareResult->setWordWrap(true);
    QHBoxLayout *compareLayout = new QHBoxLayout();
    compareLayout->addWidget(new QLabel(tr("Compare:"), dialog));
    compareLayout->addWidget(compareEdit, 1);

    QDialogButtonBox *buttons = new QDialogButtonBox(dialog);
    QPushButton *copyButton = buttons->addButton(tr("Copy"), QDialogButtonBox::ActionRole);
    copyButton->setIcon(QIcon::fromTheme("edit-copy"));
    copyButton->setEnabled(false);
    QPushButton *cancelButton = buttons->addButton(QDialogButtonBox::Cancel);
    QPushButton *closeButton = buttons->addButton(QDialogButtonBox::Close);
    closeButton->hide();

    QVBoxLayout *layout = new QVBoxLayout(dialog);
    layout->addWidget(info);
    layout->addWidget(results, 1);
    layout->addWidget(progressBar);
    layout->addLayout(compareLayout);
    layout->addWidget(compareResult);
    layout->addWidget(buttons);

    info->setText(files.count() == 1 ? files.first()
                                     : tr("%1 files, %2").arg(files.count())
                                       .arg(Common::formatSize(totalSize)));

    // Results, in the same order as the files: "<checksum>  <name>", the
    // format of md5sum/sha256sum (one file: only the checksum is copied)
    struct Result { QString checksum; QString error; };
    auto resultList = std::make_shared<QVector<Result>>(files.count());
    auto finishedCount = std::make_shared<int>(0);

    auto showResults = [=]() {
        QStringList lines;
        for (int i = 0; i < files.count(); ++i) {
            const Result &r = resultList->at(i);
            const QString fileName = files.count() == 1 ? QFileInfo(files.at(i)).fileName()
                                                        : files.at(i);
            if (!r.error.isEmpty()) { lines << tr("%1: error: %2").arg(fileName, r.error); }
            else if (!r.checksum.isEmpty()) { lines << QString("%1  %2").arg(r.checksum, fileName); }
        }
        results->setPlainText(lines.join("\n"));
    };

    auto compare = [=]() {
        const QString wanted = compareEdit->text().trimmed().toLower();
        if (wanted.isEmpty()) { compareResult->clear(); return; }
        for (int i = 0; i < files.count(); ++i) {
            if (resultList->at(i).checksum == wanted) {
                compareResult->setText(tr("<b style=\"color:#2a9d2a\">Match</b>: %1")
                                       .arg(files.at(i).toHtmlEscaped()));
                return;
            }
        }
        compareResult->setText(*finishedCount < files.count() ?
                               tr("No match yet (still calculating).") :
                               tr("<b style=\"color:#d03030\">No match</b>"));
    };
    connect(compareEdit, &QLineEdit::textChanged, dialog, compare);

    connect(copyButton, &QPushButton::clicked, dialog, [=]() {
        if (files.count() == 1) {
            QApplication::clipboard()->setText(resultList->at(0).checksum);
        } else {
            QApplication::clipboard()->setText(results->toPlainText());
        }
    });

    // Worker: reads the files in another thread. The dialog is told about
    // the progress and the results through queued calls, which are dropped
    // if the dialog is gone. Closing the dialog cancels the work.
    auto canceled = std::make_shared<std::atomic_bool>(false);
    auto lastReport = std::make_shared<std::atomic<qint64>>(0);
    QFutureWatcher<void> *watcher = new QFutureWatcher<void>(dialog);

    connect(cancelButton, &QPushButton::clicked, dialog, [=]() {
        canceled->store(true);
        dialog->close();
    });
    connect(closeButton, &QPushButton::clicked, dialog, &QDialog::close);
    connect(dialog, &QDialog::finished, dialog, [=]() {
        canceled->store(true);
        watcher->waitForFinished();
    });
    connect(watcher, &QFutureWatcher<void>::finished, dialog, [=]() {
        progressBar->hide();
        cancelButton->hide();
        closeButton->show();
        closeButton->setDefault(true);
        copyButton->setEnabled(true);
        compare();
    });

    watcher->setFuture(QtConcurrent::run([=]() {
        qint64 doneBefore = 0;
        for (int i = 0; i < files.count(); ++i) {
            if (canceled->load()) { return; }
            QString error;
            auto progress = [&](qint64 done) {
                // at most about 20 updates per second
                const qint64 now = QDateTime::currentMSecsSinceEpoch();
                if (now - lastReport->load() < 50) { return; }
                lastReport->store(now);
                const int value = totalSize > 0 ? int((doneBefore + done) * 1000 / totalSize) : 0;
                QMetaObject::invokeMethod(progressBar, [=]() { progressBar->setValue(value); },
                                          Qt::QueuedConnection);
            };
            const QString checksum = checksumOfFile(files.at(i), algorithm, *canceled, progress, &error);
            if (canceled->load()) { return; }
            doneBefore += QFileInfo(files.at(i)).size();
            QMetaObject::invokeMethod(results, [=]() {
                (*resultList)[i].checksum = checksum;
                (*resultList)[i].error = error;
                ++(*finishedCount);
                showResults();
                compare();
            }, Qt::QueuedConnection);
        }
    }));

    dialog->show();
}
