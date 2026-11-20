/*
 * This file is part of QtFM <https://qtfm.eu>
 *
 * Split view: the file area can be divided in 2, 3 or 4 panes. Each pane
 * has its own file views, tabs, filter bar and state (see FilePane).
 *
 * The rest of the main window works on one pane, the active one: the
 * members list, detailTree, stackWidget, tabs, modelView, listSelectionModel,
 * filterBar, filterEdit, filterDirsCheck and mainLayout point to the widgets
 * of the active pane (bindPane), and the address bar, the folder tree and the
 * status bar follow it, like they follow the current tab.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "mainwindow.h"

#include <QHeaderView>
#include <QShortcut>
#include <QStorageInfo>
#include <QTimer>
#include <QToolButton>

//---------------------------------------------------------------------------
/**
 * @brief Creates a pane: filter bar, file views, tabs and their connections
 *
 * The signals of the widgets of a pane make it the active one before they
 * are handled, as the slots work on the active pane.
 */
FilePane *MainWindow::createPane()
{
    FilePane *pane = new FilePane;
    pane->view = 0;
    pane->configured = false;

    pane->widget = new QWidget();
    QVBoxLayout *paneLayout = new QVBoxLayout(pane->widget);
    paneLayout->setSpacing(0);
    paneLayout->setContentsMargins(0,0,0,0);

    // Header: folder and totals of the pane (only with a split view)
    pane->header = new QLabel(pane->widget);
    pane->header->setTextFormat(Qt::PlainText);
    pane->header->setAutoFillBackground(true);
    pane->header->setContentsMargins(4,2,4,2);
    // a long path must not keep the pane from getting narrower
    pane->header->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    pane->header->hide();
    paneLayout->addWidget(pane->header);

    // Filter bar
    pane->filterBar = new QWidget(pane->widget);
    QHBoxLayout *filterLayout = new QHBoxLayout(pane->filterBar);
    filterLayout->setContentsMargins(4,2,4,2);
    filterLayout->setSpacing(6);
    pane->filterEdit = new QLineEdit(pane->filterBar);
    pane->filterEdit->setClearButtonEnabled(true);
    pane->filterEdit->setPlaceholderText(tr("Name or pattern, e.g. *.pdf"));
    pane->filterDirsCheck = new QCheckBox(tr("Filter folders too"), pane->filterBar);
    pane->filterDirsCheck->setChecked(settings->value("filterFolders", false).toBool());
    QToolButton *filterClose = new QToolButton(pane->filterBar);
    filterClose->setIcon(QIcon::fromTheme("window-close", QIcon::fromTheme("dialog-close")));
    filterClose->setToolTip(tr("Close the filter (Esc)"));
    filterClose->setAutoRaise(true);
    filterLayout->addWidget(new QLabel(tr("Filter:"), pane->filterBar));
    filterLayout->addWidget(pane->filterEdit, 1);
    filterLayout->addWidget(pane->filterDirsCheck);
    filterLayout->addWidget(filterClose);
    pane->filterBar->hide();
    paneLayout->addWidget(pane->filterBar);

    connect(pane->filterEdit, &QLineEdit::textChanged, this, [this, pane]() {
        setActivePane(pane);
        if (pane == activePane) { applyFilter(); }
    });
    connect(pane->filterDirsCheck, &QCheckBox::toggled, this, [this, pane](bool checked) {
        settings->setValue("filterFolders", checked);
        setActivePane(pane);
        if (pane == activePane) { applyFilter(); }
    });
    connect(filterClose, &QToolButton::clicked, this, [this, pane]() {
        setActivePane(pane);
        if (pane == activePane) { closeFilterBar(); }
    });
    QShortcut *filterEscape = new QShortcut(QKeySequence(Qt::Key_Escape), pane->filterBar);
    filterEscape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(filterEscape, &QShortcut::activated, this, [this, pane]() {
        setActivePane(pane);
        if (pane == activePane) { closeFilterBar(); }
    });

    // File views and tabs
    QWidget *main = new QWidget(pane->widget);
    pane->layout = new QVBoxLayout(main);
    pane->layout->setSpacing(0);
    pane->layout->setContentsMargins(0,0,0,0);

    pane->stack = new QStackedWidget(main);
    QWidget *page = new QWidget(pane->stack);
    QHBoxLayout *hl1 = new QHBoxLayout(page);
    hl1->setSpacing(0);
    hl1->setContentsMargins(0,0,0,0);
    pane->list = new DfmQListView(page);
    hl1->addWidget(pane->list);
    pane->stack->addWidget(page);

    QWidget *page2 = new QWidget(pane->stack);
    hl1 = new QHBoxLayout(page2);
    hl1->setSpacing(0);
    hl1->setContentsMargins(0,0,0,0);
    pane->detailTree = new DfmQTreeView(page2);
    hl1->addWidget(pane->detailTree);
    pane->stack->addWidget(page2);

    pane->tabs = new tabBar(modelList->folderIcons);
    pane->tabs->setDrawBase(0);
    pane->tabs->setExpanding(0);

    pane->layout->addWidget(pane->stack);
    pane->layout->addWidget(pane->tabs);
    paneLayout->addWidget(main, 1);

    // Model: every pane sorts and filters the file system model on its own
    pane->model = new viewsSortProxyModel();
    pane->model->setSourceModel(modelList);
    pane->model->setSortCaseSensitivity(Qt::CaseInsensitive);

    pane->list->setWrapping(true);
    pane->list->setWordWrap(true);
    pane->list->setModel(pane->model);
    pane->list->setTextElideMode(Qt::ElideNone);
    pane->list->setResizeMode(QListView::Adjust);
    pane->list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    pane->list->setSelectionRectVisible(true);
    // files are renamed in a dialog (renameDialog), not in place
    pane->list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    pane->selection = pane->list->selectionModel();

    pane->detailTree->setRootIsDecorated(true);
    pane->detailTree->setItemsExpandable(true);
    pane->detailTree->setUniformRowHeights(true);
    pane->detailTree->setAlternatingRowColors(true);
    pane->detailTree->setModel(pane->model);
    pane->detailTree->setSelectionModel(pane->selection);
    pane->detailTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    pane->detailTree->setDragDropMode(QAbstractItemView::DragDrop);
    pane->detailTree->setDefaultDropAction(Qt::MoveAction);
    pane->detailTree->setDropIndicatorShown(true);
    pane->detailTree->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // A reset of the model (Refresh) makes the views forget their folder.
    // Connected after setModel(), so the views are reset first.
    connect(pane->model, &QAbstractItemModel::modelReset, this, [this]() {
        if (paneRootsPending) { return; }
        paneRootsPending = true;
        QTimer::singleShot(0, this, SLOT(restorePaneRoots()));
    });

    // Mouse clicks in the views
    auto clicked = [this, pane](const QModelIndex &index) {
        if (!uiReady) { return; }
        setActivePane(pane);
        if (pane != activePane) { return; }
        if (singleClickMode == 1) { listItemClicked(index); }
        else if (singleClickMode == 2) { listDoubleClicked(index); }
    };
    auto activated = [this, pane](const QModelIndex &index) {
        if (!uiReady) { return; }
        setActivePane(pane);
        if (pane == activePane) { listDoubleClicked(index); }
    };
    auto pressed = [this, pane](const QModelIndex &index) {
        if (!uiReady) { return; }
        setActivePane(pane);
        if (pane == activePane) { listItemPressed(index); }
    };
    connect(pane->list, &QAbstractItemView::clicked, this, clicked);
    connect(pane->detailTree, &QAbstractItemView::clicked, this, clicked);
    connect(pane->list, &QAbstractItemView::activated, this, activated);
    connect(pane->detailTree, &QAbstractItemView::activated, this, activated);
    connect(pane->list, &QAbstractItemView::pressed, this, pressed);
    connect(pane->detailTree, &QAbstractItemView::pressed, this, pressed);

    // Selection: only the one of the active pane is shown in the status bar
    // (the selection of another pane can change when its files are removed)
    connect(pane->selection, &QItemSelectionModel::selectionChanged, this,
            [this, pane](const QItemSelection &selected, const QItemSelection &deselected) {
        if (!uiReady || pane != activePane) { return; }
        listSelectionChanged(selected, deselected);
    });

    // Tabs
    connect(pane->tabs, &QTabBar::currentChanged, this, [this, pane](int index) {
        if (!uiReady) { return; }
        setActivePane(pane);
        if (pane == activePane) { tabChanged(index); }
    });
    connect(pane->tabs, &tabBar::dragDropTab, this,
            [this](const QMimeData *data, QString newPath, QStringList cutList) {
        if (!uiReady) { return; }
        pasteLauncher(data, newPath, cutList);
    });

    // A pane created later (the window is set up): same settings as the
    // active pane
    if (uiReady) {
        if (tabsOnTopAct->isChecked()) {
            pane->layout->setDirection(QBoxLayout::BottomToTop);
            pane->tabs->setShape(QTabBar::RoundedNorth);
        }
        pane->detailTree->setIconSize(QSize(zoomDetail, zoomDetail));
        pane->detailTree->header()->restoreState(detailTree->header()->saveState());
        pane->detailTree->setSortingEnabled(1);
        pane->model->setFilterRegularExpression(modelView->filterRegularExpression());
        pane->model->sort(currentSortColumn, currentSortOrder);
    }

    return pane;
}

//---------------------------------------------------------------------------
/**
 * @brief Makes the members of the main window point to the widgets of a pane
 */
void MainWindow::bindPane(FilePane *pane)
{
    filterBar = pane->filterBar;
    filterEdit = pane->filterEdit;
    filterDirsCheck = pane->filterDirsCheck;
    mainLayout = pane->layout;
    stackWidget = pane->stack;
    list = pane->list;
    detailTree = pane->detailTree;
    tabs = pane->tabs;
    modelView = pane->model;
    listSelectionModel = pane->selection;
}

//---------------------------------------------------------------------------
/**
 * @brief Keeps in a pane the state the main window holds for the active one
 */
void MainWindow::savePaneState(FilePane *pane)
{
    pane->history.clear();
    for (int i = 0; i < pathEdit->count(); ++i) { pane->history << pathEdit->itemText(i); }
    if (pane->path.isEmpty()) { pane->path = pathEdit->itemText(0); }
    pane->forwardHistory = forwardHistory;
    pane->curIndex = curIndex;
    pane->view = currentView;
    pane->statusSize = folderStatusSize;
    pane->statusDate = folderStatusDate;
}

//---------------------------------------------------------------------------
/**
 * @brief Returns the pane the given widget is in, if any
 */
FilePane *MainWindow::paneOf(QObject *object) const
{
    if (!object || !object->isWidgetType()) { return nullptr; }
    QWidget *widget = static_cast<QWidget*>(object);
    while (widget) {
        foreach (FilePane *pane, panes) {
            if (pane->widget == widget) { return pane; }
        }
        widget = widget->parentWidget();
    }
    return nullptr;
}

//---------------------------------------------------------------------------
/**
 * @brief Makes a pane the active one
 *
 * The address bar with its history, the view mode actions, the folder tree
 * and the status bar change to those of the pane, like when changing tabs.
 * The selection of the pane is kept.
 */
void MainWindow::setActivePane(FilePane *pane)
{
    if (!uiReady || !pane || pane == activePane || switchingPane || !panes.contains(pane)) { return; }


    if (activePane) { savePaneState(activePane); }
    activePane = pane;
    bindPane(pane);

    // Folder, history and view mode: those of the current tab, if any
    QString path = pane->path;
    QStringList history = pane->history;
    int type = pane->view;
    if (tabs->count()) {
        const int index = tabs->currentIndex();
        if (!tabs->tabData(index).toString().isEmpty()) { path = tabs->tabData(index).toString(); }
        if (tabs->getHistory(index)) { history = *tabs->getHistory(index); }
        type = tabs->getType(index);
    }
    // the folder can have been removed meanwhile
    while (!QFileInfo(path).isDir() && path != "/" && !path.isEmpty()) {
        path = QFileInfo(path).absolutePath();
    }
    if (path.isEmpty()) { path = "/"; }
    if (history.isEmpty() || history.first() != path) { history.prepend(path); }

    pathEdit->clear();
    pathEdit->addItems(history);
    forwardHistory = pane->forwardHistory;
    folderStatusSize = pane->statusSize;
    folderStatusDate = pane->statusDate;
    curIndex = QFileInfo(path);
    backIndex = QModelIndex();

    // View mode. The views of a pane keep their mode: they are only set up
    // the first time, when the mode differs (tab changed) or after a zoom.
    modelList->loadFolder(path);
    detailAct->setChecked(type == 2);
    iconAct->setChecked(type == 1);
    if (!pane->configured || pane->view != type) {
        toggleDetails();
        pane->configured = true;
    } else {
        currentView = type;
    }
    pane->view = currentView;

    // Folder tree, window title, root folder of the model and totals
    switchingPane = true;
    const QModelIndex treeIndex = modelTree->mapFromSource(modelList->index(path));
    if (tree->currentIndex() == treeIndex) { treeSelectionChanged(treeIndex, QModelIndex()); }
    else { tree->setCurrentIndex(treeIndex); }
    switchingPane = false;

    // status bar: the selection of this pane, or its folder
    listSelectionChanged(QItemSelection(), QItemSelection());
    updatePaneHeaders();
}

//---------------------------------------------------------------------------
/**
 * @brief Sets the number of panes of the file area (1 to 4)
 *
 * New panes show the folder of the active pane. The panes removed are the
 * last ones.
 */
void MainWindow::setSplitCount(int count)
{
    count = qBound(1, count, 4);

    if (count < panes.count()) {
        if (panes.indexOf(activePane) >= count) { setActivePane(panes.first()); }
        while (panes.count() > count) {
            FilePane *pane = panes.takeLast();
            delete pane->widget; // the views, before their model
            delete pane->model;
            delete pane;
        }
    }

    QList<FilePane*> created;
    FilePane *current = activePane;
    const QString path = pathEdit->itemText(0);
    while (panes.count() < count) {
        FilePane *pane = createPane();
        pane->path = path;
        pane->history = QStringList(path);
        pane->view = currentView;
        panes.append(pane);
        created.append(pane);
    }

    arrangePanes();

    // Activating a new pane sets up its views and loads its folder
    foreach (FilePane *pane, created) { setActivePane(pane); }
    setActivePane(current);

    updateShownFolders();
    updatePaneHeaders();

    if (splitActs[count - 1]) { splitActs[count - 1]->setChecked(true); }
    settings->setValue("splitViews", count);

    if (currentView == 2) { detailTree->setFocus(); }
    else { list->setFocus(); }
}

//---------------------------------------------------------------------------
/**
 * @brief Places the panes: two side by side; with three or four, two on
 * top and the others below (one pane below takes the whole width)
 */
void MainWindow::arrangePanes()
{
    const int count = panes.count();
    int top = 0;
    int bottom = 0;
    for (int i = 0; i < count; ++i) {
        const bool below = (count >= 3 && i >= 2);

        // addWidget() moves a widget that is already in a splitter
        if (below) { paneBottom->addWidget(panes.at(i)->widget); ++bottom; }
        else { paneTop->addWidget(panes.at(i)->widget); ++top; }
        panes.at(i)->widget->show();
    }
    paneBottom->setVisible(bottom > 0);

    // same size for all
    QList<int> sizes;
    for (int i = 0; i < top; ++i) { sizes << 1000; }
    paneTop->setSizes(sizes);
    sizes.clear();
    for (int i = 0; i < bottom; ++i) { sizes << 1000; }
    if (bottom > 0) {
        paneBottom->setSizes(sizes);
        paneRows->setSizes(QList<int>() << 1000 << 1000);
    }
}

//---------------------------------------------------------------------------
/**
 * @brief Updates the headers of the panes: folder and totals, with the
 * active pane highlighted. They are only shown with a split view.
 */
void MainWindow::updatePaneHeaders()
{
    const bool split = panes.count() > 1;
    foreach (FilePane *pane, panes) {
        pane->header->setVisible(split);
        if (!split) { continue; }

        QString info = pane->statusSize.trimmed();
        if (!pane->statusDate.isEmpty()) {
            if (!info.isEmpty()) { info += ", "; }
            info += pane->statusDate;
        }
        QString text = pane->path;
        if (!info.isEmpty()) { text += QString("    (%1)").arg(info); }
        pane->header->setText(text);
        pane->header->setToolTip(text);

        const bool active = (pane == activePane);
        QFont font = this->font();
        font.setBold(active);
        pane->header->setFont(font);
        QPalette colors = palette();
        colors.setColor(QPalette::Window,
                        palette().color(active ? QPalette::Highlight : QPalette::Window));
        colors.setColor(QPalette::WindowText,
                        palette().color(active ? QPalette::HighlightedText : QPalette::WindowText));
        pane->header->setPalette(colors);
    }
}

//---------------------------------------------------------------------------
/**
 * @brief Tells the model which folders the panes show (they keep their "..")
 */
void MainWindow::updateShownFolders()
{
    QStringList folders;
    foreach (FilePane *pane, panes) {
        if (!pane->path.isEmpty()) { folders << pane->path; }
    }
    modelList->setShownFolders(folders);
}

//---------------------------------------------------------------------------
/**
 * @brief After a reset of the model, shows again the folders of the panes
 * that are not the active one (the active one is handled by refresh())
 */
void MainWindow::restorePaneRoots()
{
    paneRootsPending = false;
    foreach (FilePane *pane, panes) {
        if (pane == activePane || pane->path.isEmpty()) { continue; }
        modelList->loadFolder(pane->path);
        const QModelIndex root = pane->model->mapFromSource(modelList->index(pane->path));
        pane->list->setRootIndex(root);
        pane->detailTree->setRootIndex(root);
    }
    updateShownFolders();
}

//---------------------------------------------------------------------------
/**
 * @brief Items of a folder (without ".." and, unless they are shown, hidden
 * files) and its totals as text: size of the files and free space
 */
void MainWindow::folderTotals(const QString &path, QModelIndexList &items, QString &total)
{
    qint64 bytes = 0;
    const bool includeHidden = hiddenAct->isChecked();
    const QModelIndex folder = modelList->index(path);

    for (int x = 0; x < modelList->rowCount(folder); ++x) {
        const QModelIndex item = modelList->index(x, 0, folder);
        if (modelList->isDotDot(item)) { continue; } // not an item of the folder
        if (!includeHidden && modelList->fileInfo(item).isHidden()) { continue; }
        items.append(item);
        bytes += modelList->size(item);
    }

    total = bytes ? Common::formatSize(bytes) : QString();

    // free space of the file system of this folder
    QStorageInfo storage(path);
    if (storage.isValid() && storage.isReady()) {
        const QString freeSpace = tr("%1 free").arg(Common::formatSize(storage.bytesAvailable()));
        total = total.isEmpty() ? freeSpace : QString("%1, %2").arg(total, freeSpace);
    }
}

//---------------------------------------------------------------------------
/**
 * @brief Split view actions of the View menu
 */
void MainWindow::splitViewTriggered(QAction *action)
{
    for (int i = 0; i < 4; ++i) {
        if (splitActs[i] == action) { setSplitCount(i + 1); }
    }
}
