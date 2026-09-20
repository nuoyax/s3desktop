#include "ui/ObjectBrowser.h"

#include "ui/ObjectModel.h"
#include "ui/Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QStackedWidget>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

namespace us3 {

namespace {

/// A small tool button in the Fluent style: icon only, no border until hovered.
QToolButton *navButton(Theme::Glyph glyph, const QString &tip) {
    auto *b = new QToolButton;
    b->setObjectName(QStringLiteral("crumbNav"));
    b->setIcon(Theme::icon(glyph));
    b->setIconSize(QSize(Theme::iconSize, Theme::iconSize));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setCursor(Qt::ArrowCursor);
    return b;
}

QFrame *vSeparator() {
    auto *f = new QFrame;
    f->setFrameShape(QFrame::VLine);
    f->setObjectName(QStringLiteral("separator"));
    f->setFixedWidth(1);
    return f;
}

} // namespace

ObjectBrowser::ObjectBrowser(S3Client *client, QWidget *parent)
    : QWidget(parent), m_client(client) {
    m_model = new ObjectModel(this);
    buildUi();
}

void ObjectBrowser::buildUi() {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    outer->addWidget(buildBreadcrumb());

    // The table and the details pane share a row. A splitter would be the usual
    // choice, but the details pane is a fixed 260px inspector rather than a
    // resizable editor, so a plain layout keeps it from being dragged to a
    // useless width.
    auto *body = new QWidget;
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    m_table = new QTableView;
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->setWordWrap(false);
    m_table->setTextElideMode(Qt::ElideMiddle);
    m_table->setSortingEnabled(true);
    m_table->setCornerButtonEnabled(false);
    // Fixed row height plus uniform row heights is what makes the view cheap to
    // render with tens of thousands of rows: Qt can then compute which rows are
    // visible without asking the model for every one.
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_table->verticalHeader()->setDefaultSectionSize(Theme::rowHeight);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionsClickable(true);
    m_table->horizontalHeader()->setSectionResizeMode(ObjectModel::NameColumn,
                                                      QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ObjectModel::SizeColumn,
                                                      QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(ObjectModel::ModifiedColumn,
                                                      QHeaderView::Fixed);
    m_table->setColumnWidth(ObjectModel::SizeColumn, 110);
    m_table->setColumnWidth(ObjectModel::ModifiedColumn, 150);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);

    // The original lacked this and had no way to open an item from the
    // keyboard; Enter is the least-surprising binding.
    connect(m_table, &QTableView::doubleClicked, this, &ObjectBrowser::onTableActivated);
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this]() { emit selectionChanged(); });

    connect(m_table->horizontalHeader(), &QHeaderView::sortIndicatorChanged, this,
            [this](int column, Qt::SortOrder order) {
                m_model->sort(column, order);
                emit selectionChanged();
            });

    bodyLayout->addWidget(m_table, 1);
    bodyLayout->addWidget(buildDetailsPane());

    outer->addWidget(body, 1);
}

QWidget *ObjectBrowser::buildBreadcrumb() {
    auto *bar = new QWidget;
    bar->setObjectName(QStringLiteral("breadcrumb"));
    bar->setFixedHeight(34);

    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(6, 3, 8, 3);
    layout->setSpacing(2);

    m_back = navButton(Theme::Glyph::Back, QStringLiteral("Back (Alt+Left)"));
    m_back->setShortcut(QKeySequence(QStringLiteral("Alt+Left")));
    m_forward = navButton(Theme::Glyph::Forward, QStringLiteral("Forward (Alt+Right)"));
    m_forward->setShortcut(QKeySequence(QStringLiteral("Alt+Right")));
    m_up = navButton(Theme::Glyph::Up, QStringLiteral("Up one level (Alt+Up)"));
    m_up->setShortcut(QKeySequence(QStringLiteral("Alt+Up")));

    connect(m_back, &QToolButton::clicked, this, &ObjectBrowser::goBack);
    connect(m_forward, &QToolButton::clicked, this, &ObjectBrowser::goForward);
    connect(m_up, &QToolButton::clicked, this, &ObjectBrowser::navigateUp);

    layout->addWidget(m_back);
    layout->addWidget(m_forward);
    layout->addWidget(m_up);
    layout->addWidget(vSeparator());

    m_crumbHost = new QWidget;
    auto *crumbLayout = new QHBoxLayout(m_crumbHost);
    crumbLayout->setContentsMargins(4, 0, 0, 0);
    crumbLayout->setSpacing(1);
    layout->addWidget(m_crumbHost, 1);

    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("search"));
    m_search->setPlaceholderText(QStringLiteral("Search this bucket…"));
    m_search->setClearButtonEnabled(true);
    m_search->setFixedWidth(240);
    m_search->addAction(Theme::icon(Theme::Glyph::Search, Theme::textFaint()),
                        QLineEdit::LeadingPosition);
    connect(m_search, &QLineEdit::textChanged, this, &ObjectBrowser::searchChanged);
    layout->addWidget(m_search);

    return bar;
}

QWidget *ObjectBrowser::buildDetailsPane() {
    m_details = new QWidget;
    m_details->setObjectName(QStringLiteral("sidePanel"));
    m_details->setFixedWidth(268);

    auto *layout = new QVBoxLayout(m_details);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QLabel(QStringLiteral("Details"));
    header->setObjectName(QStringLiteral("sidePanelHeader"));
    layout->addWidget(header);

    auto *content = new QWidget;
    auto *grid = new QVBoxLayout(content);
    grid->setContentsMargins(12, 12, 12, 12);
    grid->setSpacing(10);

    auto addField = [&](const QString &caption, QLabel **valueLabel) {
        auto *captionLabel = new QLabel(caption);
        captionLabel->setObjectName(QStringLiteral("panelSection"));
        grid->addWidget(captionLabel);

        auto *value = new QLabel(QStringLiteral("—"));
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        grid->addWidget(value);
        *valueLabel = value;
    };

    addField(QStringLiteral("NAME"), &m_detailName);
    addField(QStringLiteral("SIZE"), &m_detailSize);
    addField(QStringLiteral("MODIFIED"), &m_detailModified);
    addField(QStringLiteral("ETAG"), &m_detailEtag);
    addField(QStringLiteral("STORAGE CLASS"), &m_detailClass);
    addField(QStringLiteral("KEY"), &m_detailKey);

    grid->addStretch(1);
    layout->addWidget(content, 1);

    return m_details;
}

void ObjectBrowser::rebuildBreadcrumb() {
    auto *layout = qobject_cast<QHBoxLayout *>(m_crumbHost->layout());
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }

    // The root is always offered, so there is a way back to the bucket list of
    // keys even from a deep prefix.
    auto addCrumb = [&](const QString &label, const QString &target, bool current) {
        if (layout->count() > 0) {
            auto *sep = new QLabel(QStringLiteral("›"));
            sep->setObjectName(QStringLiteral("crumbSep"));
            layout->addWidget(sep);
        }

        auto *button = new QToolButton;
        button->setObjectName(QStringLiteral("crumbPart"));
        button->setText(label);
        button->setProperty("current", current);
        button->setAutoRaise(true);
        button->setCursor(Qt::PointingHandCursor);
        if (!current) {
            connect(button, &QToolButton::clicked, this,
                    [this, target]() { navigateTo(target); });
        } else {
            button->setEnabled(false);
        }
        layout->addWidget(button);
    };

    addCrumb(QStringLiteral("Root"), QString(), m_prefix.isEmpty());

    const QStringList parts = m_prefix.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QString accumulated;
    for (int i = 0; i < parts.size(); ++i) {
        accumulated += parts.at(i) + QLatin1Char('/');
        addCrumb(parts.at(i), accumulated, i == parts.size() - 1);
    }

    layout->addStretch(1);

    m_back->setEnabled(canGoBack());
    m_forward->setEnabled(canGoForward());
    m_up->setEnabled(!m_prefix.isEmpty());
}

void ObjectBrowser::pushHistory(const QString &prefix) {
    // Truncate anything ahead of the cursor: navigating after going back is a
    // new branch, exactly as in a browser.
    while (m_history.size() > m_historyIndex + 1) {
        m_history.removeLast();
    }
    if (m_history.isEmpty() || m_history.last() != prefix) {
        m_history.append(prefix);
        m_historyIndex = m_history.size() - 1;
    }
}

void ObjectBrowser::navigateTo(const QString &prefix) {
    const QString normalised = (prefix == QLatin1String("all")) ? QString() : prefix;
    if (normalised == m_prefix) {
        return;
    }

    m_prefix = normalised;
    m_model->setPrefix(m_prefix);
    pushHistory(m_prefix);
    rebuildBreadcrumb();
    m_search->clear();
    emit loadRequested(m_prefix);
}

void ObjectBrowser::navigateUp() {
    if (m_prefix.isEmpty()) {
        return;
    }
    QString parent = m_prefix;
    if (parent.endsWith(QLatin1Char('/'))) {
        parent.chop(1);
    }
    const int slash = parent.lastIndexOf(QLatin1Char('/'));
    parent = slash < 0 ? QString() : parent.left(slash + 1);
    navigateTo(parent);
}

void ObjectBrowser::resetNavigation() {
    m_prefix.clear();
    m_history.clear();
    m_historyIndex = -1;
    m_model->setPrefix(QString());
    {
        QSignalBlocker blocker(m_search);
        m_search->clear();
    }
    m_model->setSearchText(QString());
    rebuildBreadcrumb();
}

void ObjectBrowser::goBack() {
    if (!canGoBack()) {
        return;
    }
    m_historyIndex -= 1;
    m_prefix = m_history.at(m_historyIndex);
    m_model->setPrefix(m_prefix);
    rebuildBreadcrumb();
    m_search->clear();
    emit loadRequested(m_prefix);
}

void ObjectBrowser::goForward() {
    if (!canGoForward()) {
        return;
    }
    m_historyIndex += 1;
    m_prefix = m_history.at(m_historyIndex);
    m_model->setPrefix(m_prefix);
    rebuildBreadcrumb();
    m_search->clear();
    emit loadRequested(m_prefix);
}

void ObjectBrowser::onTableActivated(const QModelIndex &index) {
    if (!index.isValid()) {
        return;
    }
    if (index.data(ObjectModel::IsFolderRole).toBool()) {
        const QString prefix = index.data(ObjectModel::KeyRole).toString();
        emit folderActivated(prefix);
        navigateTo(prefix);
        return;
    }
    emit objectActivated(index.data(ObjectModel::KeyRole).toString());
}

QStringList ObjectBrowser::selectedKeys() const {
    QStringList keys;
    const QModelIndexList rows = m_table->selectionModel()->selectedRows(ObjectModel::NameColumn);
    keys.reserve(rows.size());
    for (const QModelIndex &index : rows) {
        if (index.data(ObjectModel::IsFolderRole).toBool()) {
            continue; // a folder is not an object and cannot be downloaded
        }
        keys.append(index.data(ObjectModel::KeyRole).toString());
    }
    return keys;
}

QList<ObjectInfo> ObjectBrowser::selectedObjects() const {
    QList<ObjectInfo> objects;
    const QModelIndexList rows = m_table->selectionModel()->selectedRows(ObjectModel::NameColumn);
    objects.reserve(rows.size());
    for (const QModelIndex &index : rows) {
        ObjectInfo info;
        if (m_model->objectAt(index, &info)) {
            objects.append(info);
        }
    }
    return objects;
}

void ObjectBrowser::selectAll() {
    m_table->selectAll();
}

void ObjectBrowser::showDetailsFor(const QModelIndex &index) {
    ObjectInfo info;
    if (!m_model->objectAt(index, &info)) {
        const QString folder = index.isValid() ? index.data(ObjectModel::FolderNameRole).toString()
                                               : QString();
        m_detailName->setText(folder.isEmpty() ? QStringLiteral("—") : folder);
        m_detailSize->setText(folder.isEmpty() ? QStringLiteral("—")
                                               : QStringLiteral("folder"));
        m_detailModified->setText(QStringLiteral("—"));
        m_detailEtag->setText(QStringLiteral("—"));
        m_detailClass->setText(QStringLiteral("—"));
        m_detailKey->setText(folder.isEmpty() ? QStringLiteral("—")
                                              : index.data(ObjectModel::KeyRole).toString());
        return;
    }

    m_detailName->setText(info.key.section(QLatin1Char('/'), -1));
    m_detailSize->setText(QStringLiteral("%1  (%2 bytes)")
                              .arg(Theme::formatBytes(info.size))
                              .arg(info.size));
    m_detailModified->setText(info.lastModified.isValid()
                                  ? info.lastModified.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                                  : QStringLiteral("—"));
    const QString etag = info.etag;
    m_detailEtag->setText(etag.isEmpty()
                              ? QStringLiteral("—")
                              : QString(etag).remove(QLatin1Char('"')));
    m_detailClass->setText(info.storageClass.isEmpty() ? QStringLiteral("STANDARD")
                                                       : info.storageClass);
    m_detailKey->setText(info.key);
}

void ObjectBrowser::setDetailsVisible(bool on) {
    m_details->setVisible(on);
}

bool ObjectBrowser::detailsVisible() const {
    return m_details->isVisible();
}

void ObjectBrowser::setSearchText(const QString &text) {
    if (m_search->text() == text) {
        return;
    }
    QSignalBlocker blocker(m_search);
    m_search->setText(text);
    m_model->setSearchText(text);
}

} // namespace us3
