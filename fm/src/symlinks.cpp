/*
 * This file is part of QtFM <https://qtfm.eu>
 *
 * Creating and editing symbolic links: "New symbolic link..." and
 * "Edit symbolic link...", both through symLinkDialog().
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "mainwindow.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRandomGenerator>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include <stdio.h>
#include <unistd.h>

//---------------------------------------------------------------------------
/**
 * @brief Real path of a folder (symbolic links resolved), or the path
 * itself when it cannot be resolved
 */
static QString realFolder(const QString &folder)
{
    const QString real = QFileInfo(folder).canonicalFilePath();
    return real.isEmpty() ? QDir::cleanPath(folder) : real;
}

//---------------------------------------------------------------------------
/**
 * @brief Absolute form of a link target. A relative target is resolved
 * from the real folder of the link, as the system does.
 */
static QString absoluteTarget(const QString &target, const QString &folder)
{
    if (target.isEmpty() || QDir::isAbsolutePath(target)) { return target; }
    return QDir::cleanPath(realFolder(folder) + "/" + target);
}

//---------------------------------------------------------------------------
/**
 * @brief Relative form of a link target, from the real folder of the link
 */
static QString relativeTarget(const QString &target, const QString &folder)
{
    if (target.isEmpty() || !QDir::isAbsolutePath(target)) { return target; }
    return QDir(realFolder(folder)).relativeFilePath(QDir::cleanPath(target));
}

//---------------------------------------------------------------------------
/**
 * @brief Dialog to create a symbolic link, or to change the name and the
 * target of an existing one
 *
 * The target is written as it will be stored in the link: an absolute path,
 * or a path relative to the folder of the link ("Relative path" converts
 * between the two). A target that does not exist is allowed (the link is
 * then broken), with a notice.
 *
 * @param folder folder of the link
 * @param linkPath the link to edit, or empty to create a new one
 * @param initialTarget target proposed for a new link (may be empty)
 * @param createdPath set to the path of the link created or edited
 * @return true if a link was created or changed
 */
bool MainWindow::symLinkDialog(const QString &folder, const QString &linkPath,
                               const QString &initialTarget, QString *createdPath)
{
    const bool editing = !linkPath.isEmpty();
    const QString oldName = editing ? QFileInfo(linkPath).fileName() : QString();
    const QString oldTarget = editing ? Common::linkTarget(QFileInfo(linkPath)) : QString();

    QDialog dialog(this);
    dialog.setWindowTitle(editing ? tr("Edit symbolic link") : tr("New symbolic link"));
    dialog.setModal(true);
    dialog.setMinimumWidth(520);

    QLabel *folderLabel = new QLabel(folder, &dialog);
    folderLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    folderLabel->setWordWrap(true);

    QLineEdit *nameEdit = new QLineEdit(&dialog);
    QLineEdit *targetEdit = new QLineEdit(&dialog);
    targetEdit->setPlaceholderText(tr("Path of the file or folder the link points to"));
    QPushButton *browseFile = new QPushButton(tr("File..."), &dialog);
    QPushButton *browseFolder = new QPushButton(tr("Folder..."), &dialog);
    QHBoxLayout *targetLayout = new QHBoxLayout();
    targetLayout->addWidget(targetEdit, 1);
    targetLayout->addWidget(browseFile);
    targetLayout->addWidget(browseFolder);

    QCheckBox *relativeCheck = new QCheckBox(tr("Relative path (from the folder of the link)"), &dialog);
    QLabel *targetInfo = new QLabel(&dialog);
    targetInfo->setWordWrap(true);
    QLabel *message = new QLabel(&dialog);
    message->setWordWrap(true);

    QFormLayout *form = new QFormLayout();
    form->addRow(tr("Folder:"), folderLabel);
    if (editing) {
        QLabel *current = new QLabel(oldTarget, &dialog);
        current->setTextInteractionFlags(Qt::TextSelectableByMouse);
        current->setWordWrap(true);
        form->addRow(tr("Current target:"), current);
    }
    form->addRow(tr("Name:"), nameEdit);
    form->addRow(tr("Target:"), targetLayout);
    form->addRow(QString(), relativeCheck);
    form->addRow(QString(), targetInfo);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                                     Qt::Horizontal, &dialog);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(message);
    layout->addWidget(buttons);

    // Initial values
    QString target;
    if (editing) {
        nameEdit->setText(oldName);
        target = oldTarget;
        relativeCheck->setChecked(!QDir::isAbsolutePath(oldTarget));
    } else {
        const bool relative = settings->value("symlinkRelative", false).toBool();
        relativeCheck->setChecked(relative);
        target = relative ? relativeTarget(initialTarget, folder) : initialTarget;
        // proposed name: "Link to <target>", or "New link", made unique
        const QString targetName = QFileInfo(initialTarget).fileName();
        const QString name = targetName.isEmpty() ? tr("New link")
                                                  : tr("Link to %1").arg(targetName);
        auto taken = [&folder](const QString &candidate) {
            const QString path = folder + "/" + candidate;
            return QFileInfo::exists(path) || QFileInfo(path).isSymLink();
        };
        QString candidate = name;
        for (int num = 2; taken(candidate); ++num) {
            candidate = QString("%1 (%2)").arg(name).arg(num);
        }
        nameEdit->setText(candidate);
    }
    targetEdit->setText(target);

    // Whether the target exists, as the link will see it
    auto updateTargetInfo = [&]() {
        const QString text = targetEdit->text().trimmed();
        if (text.isEmpty()) { targetInfo->clear(); return; }
        const QFileInfo info(absoluteTarget(text, folder));
        if (info.exists()) {
            targetInfo->setText(info.isDir() ? tr("The target is a folder.")
                                             : tr("The target is a file."));
        } else {
            targetInfo->setText(tr("The target does not exist: the link will be broken."));
        }
    };
    connect(targetEdit, &QLineEdit::textChanged, &dialog, updateTargetInfo);
    connect(targetEdit, &QLineEdit::textChanged, message, &QLabel::clear);
    connect(nameEdit, &QLineEdit::textChanged, message, &QLabel::clear);
    updateTargetInfo();

    // Converts the target between absolute and relative
    connect(relativeCheck, &QCheckBox::toggled, &dialog, [&](bool relative) {
        const QString text = targetEdit->text().trimmed();
        if (text.isEmpty()) { return; }
        const QString absolute = absoluteTarget(text, folder);
        targetEdit->setText(relative ? relativeTarget(absolute, folder) : absolute);
    });

    // Browse for the target
    auto browse = [&](bool forFolder) {
        QString start = absoluteTarget(targetEdit->text().trimmed(), folder);
        if (start.isEmpty()) { start = folder; }
        const QString chosen = forFolder ?
                    QFileDialog::getExistingDirectory(&dialog, tr("Select the target folder"), start) :
                    QFileDialog::getOpenFileName(&dialog, tr("Select the target file"), start);
        if (chosen.isEmpty()) { return; }
        targetEdit->setText(relativeCheck->isChecked() ? relativeTarget(chosen, folder) : chosen);
    };
    connect(browseFile, &QPushButton::clicked, &dialog, [&]() { browse(false); });
    connect(browseFolder, &QPushButton::clicked, &dialog, [&]() { browse(true); });

    // Validation
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString name = nameEdit->text();
        const QString text = targetEdit->text().trimmed();
        QString error;
        if (name.isEmpty()) {
            error = tr("The name cannot be empty.");
        } else if (name.contains('/')) {
            error = tr("The name cannot contain \"/\".");
        } else if (name == "." || name == "..") {
            error = tr("\"%1\" is not a valid name.").arg(name);
        } else if (name != oldName && (QFileInfo::exists(folder + "/" + name)
                                       || QFileInfo(folder + "/" + name).isSymLink())) {
            error = tr("There is already an item named \"%1\" in this folder.").arg(name);
        } else if (text.isEmpty()) {
            error = tr("The target cannot be empty.");
        } else if (QDir::cleanPath(absoluteTarget(text, folder))
                   == QDir::cleanPath(realFolder(folder) + "/" + name)) {
            error = tr("A link cannot point to itself.");
        }
        if (!error.isEmpty()) {
            message->setText(error);
            return;
        }
        if (editing && name == oldName && text == oldTarget) { dialog.reject(); return; } // nothing to do
        dialog.accept();
    });

    nameEdit->setFocus();
    nameEdit->selectAll();
    if (dialog.exec() != QDialog::Accepted) { return false; }

    if (!editing) { settings->setValue("symlinkRelative", relativeCheck->isChecked()); }

    const QString name = nameEdit->text();
    const QString text = targetEdit->text().trimmed();
    const QString newPath = folder + "/" + name;
    const QByteArray targetBytes = QFile::encodeName(text);
    bool ok = false;

    if (!editing) {
        ok = ::symlink(targetBytes.constData(), QFile::encodeName(newPath).constData()) == 0;
    } else {
        // The new link is created under a temporary name and moved over
        // the old one (or to its new name): the link is never missing, and
        // an error leaves the old link as it was
        QString temp;
        do {
            temp = QString("%1/.qtfm-link-%2").arg(folder)
                    .arg(QRandomGenerator::global()->generate(), 8, 16, QChar('0'));
        } while (QFileInfo::exists(temp) || QFileInfo(temp).isSymLink());
        const QByteArray tempBytes = QFile::encodeName(temp);
        if (::symlink(targetBytes.constData(), tempBytes.constData()) == 0) {
            ok = ::rename(tempBytes.constData(), QFile::encodeName(newPath).constData()) == 0;
            if (!ok) { ::unlink(tempBytes.constData()); }
            else if (newPath != linkPath) { ::unlink(QFile::encodeName(linkPath).constData()); }
        }
    }

    if (!ok) {
        QMessageBox::warning(this, editing ? tr("Edit symbolic link") : tr("New symbolic link"),
                             tr("Could not create the symbolic link \"%1\". You may not have the proper permissions.")
                             .arg(newPath));
        return false;
    }
    if (createdPath) { *createdPath = newPath; }
    return true;
}

//---------------------------------------------------------------------------
/**
 * @brief Creates a symbolic link in the current folder. With one item
 * selected (or the context menu of an item), the link points to it.
 */
void MainWindow::newSymLink()
{
    const QString folder = pathEdit->itemText(0);
    if (!QFileInfo(folder).isWritable()) {
        status->showMessage(tr("The current directory is not writable, unable to create a symbolic link."));
        return;
    }

    // target proposed: the selected item, if there is exactly one
    QString target;
    if (focusWidget() == list || focusWidget() == detailTree) {
        QModelIndexList items = listSelectionModel->selectedRows(0);
        if (items.isEmpty()) { items = listSelectionModel->selectedIndexes(); }
        if (items.count() == 1) {
            const QModelIndex source = modelView->mapToSource(items.first());
            if (!modelList->isDotDot(source)) { target = modelList->filePath(source); }
        }
    }

    QString created;
    if (!symLinkDialog(folder, QString(), target, &created)) { return; }
    status->showMessage(tr("Symbolic link created: %1").arg(created), 5000);

    // select it once the model has seen it (file system notification)
    QTimer::singleShot(300, this, [this, created]() {
        selectListItem(modelList->index(created));
    });
}

//---------------------------------------------------------------------------
/**
 * @brief Changes the name and the target of the selected symbolic link
 */
void MainWindow::editSymLink()
{
    QModelIndexList items = listSelectionModel->selectedRows(0);
    if (items.isEmpty()) { items = listSelectionModel->selectedIndexes(); }
    QString path;
    if (items.count() == 1) { path = modelList->filePath(modelView->mapToSource(items.first())); }
    if (path.isEmpty() || !QFileInfo(path).isSymLink()) {
        status->showMessage(tr("Select one symbolic link to edit it."), 5000);
        return;
    }

    const QString folder = QFileInfo(path).absolutePath();
    if (!QFileInfo(folder).isWritable()) {
        status->showMessage(tr("The folder is not writable, unable to change the symbolic link."));
        return;
    }

    QString changed;
    if (!symLinkDialog(folder, path, QString(), &changed)) { return; }
    status->showMessage(tr("Symbolic link changed: %1").arg(changed), 5000);

    // the model keeps the information of the entry: read it again (the
    // file system notification only reports new and removed names)
    modelList->refreshItem(changed);
    QTimer::singleShot(300, this, [this, changed]() {
        modelList->refreshItem(changed);
        selectListItem(modelList->index(changed));
        listSelectionChanged(QItemSelection(), QItemSelection());
    });
}
