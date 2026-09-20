#include "ui/Theme.h"

#include <QApplication>
#include <QDateTime>
#include <QFont>
#include <QFontDatabase>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>

namespace s3desktop {
namespace Theme {

namespace {

// --- Stylesheet -------------------------------------------------------------

/// A hand-written sheet rather than a theme library: the surface count is small
/// enough that a dependency would cost more than it saves, and every rule here
/// is a decision about this application's density rather than a generic default.
const char *kStyleSheet = R"QSS(
QWidget {
    color: #1b1b1b;
    font-size: 12px;
}

QMainWindow, QDialog {
    background: #f3f3f3;
}

/* --- Menus --- */
QMenuBar {
    background: #fcfcfc;
    border-bottom: 1px solid #e0e0e0;
    padding: 1px 4px;
}
QMenuBar::item {
    padding: 4px 10px;
    background: transparent;
    border-radius: 4px;
}
QMenuBar::item:selected { background: #e9e9e9; }
QMenuBar::item:pressed  { background: #dfdfdf; }

QMenu {
    background: #fcfcfc;
    border: 1px solid #d6d6d6;
    border-radius: 6px;
    padding: 4px;
}
QMenu::item {
    padding: 5px 26px 5px 24px;
    border-radius: 4px;
}
QMenu::item:selected { background: #e5f1fb; color: #1b1b1b; }
QMenu::item:disabled { color: #8a8a8a; }
QMenu::separator { height: 1px; background: #ebebeb; margin: 4px 8px; }

/* --- Ribbon --- */
#ribbon {
    background: #fcfcfc;
    border-bottom: 1px solid #e0e0e0;
}
QToolButton#ribbonButton {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 4px;
    padding: 4px 8px 3px 8px;
    color: #1b1b1b;
    font-size: 11px;
}
QToolButton#ribbonButton:hover   { background: #f0f0f0; border-color: #e0e0e0; }
QToolButton#ribbonButton:pressed { background: #e5e5e5; }
QToolButton#ribbonButton:disabled { color: #a0a0a0; }
QToolButton#ribbonButton::menu-indicator { image: none; width: 0; }

QFrame#ribbonGroup {
    border-right: 1px solid #ebebeb;
}

/* --- Breadcrumb --- */
#breadcrumb {
    background: #fafafa;
    border-bottom: 1px solid #e0e0e0;
}
QToolButton#crumbNav {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 4px;
    padding: 2px;
}
QToolButton#crumbNav:hover:!disabled { background: #f0f0f0; border-color: #e0e0e0; }
QToolButton#crumbNav:pressed:!disabled { background: #e5e5e5; }
QToolButton#crumbNav:disabled { background: transparent; border-color: transparent; }
QToolButton#crumbPart {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 4px;
    padding: 3px 7px;
    color: #1b1b1b;
}
QToolButton#crumbPart:hover:!disabled { background: #f0f0f0; border-color: #e0e0e0; }
QToolButton#crumbPart[current="true"] { color: #616161; }
QLabel#crumbSep { color: #b8b8b8; }

/* --- Search box --- */
QLineEdit#search {
    background: #ffffff;
    border: 1px solid #d6d6d6;
    border-bottom: 1px solid #b8b8b8;
    border-radius: 4px;
    padding: 4px 8px 4px 26px;
    selection-background-color: #0067c0;
}
QLineEdit#search:focus {
    border-bottom: 2px solid #0067c0;
    padding-bottom: 3px;
}
QLineEdit, QComboBox, QSpinBox, QPlainTextEdit {
    background: #ffffff;
    border: 1px solid #d6d6d6;
    border-bottom: 1px solid #b8b8b8;
    border-radius: 4px;
    padding: 4px 6px;
    selection-background-color: #0067c0;
    selection-color: #ffffff;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QPlainTextEdit:focus {
    border-bottom: 2px solid #0067c0;
    padding-bottom: 3px;
}
QLineEdit:disabled, QComboBox:disabled {
    background: #f5f5f5;
    color: #a0a0a0;
}
QComboBox::drop-down { border: none; width: 20px; }
QComboBox QAbstractItemView {
    background: #fcfcfc;
    border: 1px solid #d6d6d6;
    selection-background-color: #e5f1fb;
    selection-color: #1b1b1b;
    outline: none;
}

/* --- Buttons --- */
QPushButton {
    background: #ffffff;
    border: 1px solid #d6d6d6;
    border-bottom: 1px solid #b8b8b8;
    border-radius: 4px;
    padding: 5px 16px;
    min-width: 72px;
}
QPushButton:hover   { background: #f7f7f7; }
QPushButton:pressed { background: #ededed; color: #616161; }
QPushButton:disabled { background: #f5f5f5; color: #a0a0a0; border-color: #e0e0e0; }
QPushButton:default {
    background: #0067c0;
    border: 1px solid #0067c0;
    color: #ffffff;
}
QPushButton:default:hover   { background: #1a77cc; }
QPushButton:default:pressed { background: #005ba4; color: #e0e0e0; }
QPushButton:default:disabled { background: #c9c9c9; border-color: #c9c9c9; color: #f5f5f5; }
QPushButton#link { border: none; background: transparent; color: #0067c0; min-width: 0; padding: 2px 4px; }
QPushButton#link:hover { text-decoration: underline; }

QToolButton {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 4px;
    padding: 3px;
}
QToolButton:hover   { background: #f0f0f0; border-color: #e0e0e0; }
QToolButton:pressed { background: #e5e5e5; }

/* --- Tables and trees --- */
QHeaderView::section {
    background: #fafafa;
    border: none;
    border-right: 1px solid #ebebeb;
    border-bottom: 1px solid #e0e0e0;
    padding: 5px 8px;
    font-weight: 600;
    color: #616161;
}
QHeaderView::section:hover { background: #f0f0f0; }

QTableView, QTreeView, QListView {
    background: #ffffff;
    border: none;
    outline: none;
    alternate-background-color: #fbfbfb;
    selection-background-color: #e5f1fb;
    selection-color: #1b1b1b;
}
QTableView::item, QTreeView::item {
    padding: 0px;
    border: none;
}
QTableView::item:selected, QTreeView::item:selected { background: #e5f1fb; color: #1b1b1b; }
QTableView::item:selected:!active, QTreeView::item:selected:!active { background: #f0f0f0; }

QTreeView::branch { background: transparent; }

/* --- Panels --- */
#sidePanel {
    background: #fafafa;
    border-left: 1px solid #e0e0e0;
}
#sidePanelHeader {
    background: #fafafa;
    border-bottom: 1px solid #ebebeb;
    padding: 6px 10px;
    font-weight: 600;
    color: #616161;
}
#panelSection {
    color: #616161;
    font-weight: 600;
    font-size: 11px;
}

/* --- Status bar --- */
QStatusBar {
    background: #fcfcfc;
    border-top: 1px solid #e0e0e0;
    color: #616161;
}
QStatusBar::item { border: none; }
QStatusBar QLabel { color: #616161; }

/* --- Progress --- */
QProgressBar {
    background: #ebebeb;
    border: none;
    border-radius: 3px;
    height: 6px;
    text-align: center;
    color: transparent;
}
QProgressBar::chunk { background: #0067c0; border-radius: 3px; }
QProgressBar#warn::chunk { background: #c42b1c; }

/* --- Tabs --- */
QTabWidget::pane { border: none; border-top: 1px solid #e0e0e0; background: #fcfcfc; }
QTabBar::tab {
    background: transparent;
    border: none;
    border-bottom: 2px solid transparent;
    padding: 6px 14px;
    color: #616161;
}
QTabBar::tab:selected { color: #1b1b1b; border-bottom-color: #0067c0; }
QTabBar::tab:hover:!selected { background: #f0f0f0; }

/* --- Scrollbars: thin, Fluent style --- */
QScrollBar:vertical {
    background: transparent;
    width: 12px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: #c6c6c6;
    border-radius: 3px;
    min-height: 28px;
    margin: 2px 3px;
}
QScrollBar::handle:vertical:hover { background: #a8a8a8; }
QScrollBar:horizontal {
    background: transparent;
    height: 12px;
    margin: 0;
}
QScrollBar::handle:horizontal {
    background: #c6c6c6;
    border-radius: 3px;
    min-width: 28px;
    margin: 3px 2px;
}
QScrollBar::handle:horizontal:hover { background: #a8a8a8; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* --- Splitter --- */
QSplitter::handle { background: #e0e0e0; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:hover { background: #0067c0; }

/* --- Group boxes --- */
QGroupBox {
    border: 1px solid #e0e0e0;
    border-radius: 6px;
    margin-top: 10px;
    padding-top: 8px;
    background: #ffffff;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 10px;
    padding: 0 4px;
    color: #616161;
    font-weight: 600;
}

/* --- Lists in dialogs --- */
QListWidget {
    background: #ffffff;
    border: 1px solid #e0e0e0;
    border-radius: 4px;
    outline: none;
}
QListWidget::item { padding: 5px 8px; border-radius: 4px; }
QListWidget::item:selected { background: #e5f1fb; color: #1b1b1b; }

QCheckBox { spacing: 6px; }
QCheckBox::indicator {
    width: 15px; height: 15px;
    border: 1px solid #8a8a8a;
    border-radius: 3px;
    background: #ffffff;
}
QCheckBox::indicator:checked {
    background: #0067c0;
    border-color: #0067c0;
}
QCheckBox::indicator:hover { border-color: #0067c0; }

QLabel#hint { color: #8a8a8a; font-size: 11px; }
QLabel#warnLabel { color: #9d5d00; }
QLabel#errLabel { color: #c42b1c; }
QFrame#separator { background: #ebebeb; }
)QSS";

// --- Icon drawing -----------------------------------------------------------

/// Stroke an icon in a 24x24 design space, scaled to the requested size.
/// Working in a fixed space keeps the glyphs the same weight at every size,
/// which hand-tuned pixel coordinates would not.
void strokePath(QPainter &p, const QPainterPath &path) {
    p.setPen(QPen(p.pen().color(), 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

QPainterPath pathOf(std::initializer_list<QPointF> pts, bool close = false) {
    QPainterPath path;
    bool first = true;
    for (const QPointF &pt : pts) {
        if (first) {
            path.moveTo(pt);
            first = false;
        } else {
            path.lineTo(pt);
        }
    }
    if (close) {
        path.closeSubpath();
    }
    return path;
}

void drawGlyph(QPainter &p, Glyph glyph) {
    switch (glyph) {
    case Glyph::Upload: {
        strokePath(p, pathOf({{12, 16}, {12, 5}}));
        strokePath(p, pathOf({{8, 9}, {12, 5}, {16, 9}}));
        strokePath(p, pathOf({{5, 15}, {5, 19}, {19, 19}, {19, 15}}));
        break;
    }
    case Glyph::Download: {
        strokePath(p, pathOf({{12, 5}, {12, 16}}));
        strokePath(p, pathOf({{8, 12}, {12, 16}, {16, 12}}));
        strokePath(p, pathOf({{5, 15}, {5, 19}, {19, 19}, {19, 15}}));
        break;
    }
    case Glyph::Delete: {
        strokePath(p, pathOf({{4, 7}, {20, 7}}));
        strokePath(p, pathOf({{6, 7}, {7, 20}, {17, 20}, {18, 7}}));
        strokePath(p, pathOf({{9, 7}, {9, 4}, {15, 4}, {15, 7}}));
        strokePath(p, pathOf({{10, 11}, {10, 16}}));
        strokePath(p, pathOf({{14, 11}, {14, 16}}));
        break;
    }
    case Glyph::Link: {
        strokePath(p, pathOf({{10, 14}, {14, 10}}));
        QPainterPath a;
        a.moveTo(8.5, 16.5);
        a.cubicTo(6, 19, 3, 18, 3, 15);
        p.save();
        p.translate(0, 0);
        {
            QPainterPath left;
            left.moveTo(9, 15);
            left.lineTo(6, 18);
            left.cubicTo(4, 20, 1.5, 17.5, 3.5, 15.5);
            left.lineTo(6.5, 12.5);
            left.cubicTo(8.2, 10.8, 10.2, 11.4, 11, 12.2);
            strokePath(p, left);

            QPainterPath right;
            right.moveTo(15, 9);
            right.lineTo(18, 6);
            right.cubicTo(20, 4, 22.5, 6.5, 20.5, 8.5);
            right.lineTo(17.5, 11.5);
            right.cubicTo(15.8, 13.2, 13.8, 12.6, 13, 11.8);
            strokePath(p, right);
        }
        p.restore();
        break;
    }
    case Glyph::Refresh: {
        QPainterPath arc;
        arc.arcMoveTo(QRectF(4, 4, 16, 16), 60);
        arc.arcTo(QRectF(4, 4, 16, 16), 60, 280);
        strokePath(p, arc);
        strokePath(p, pathOf({{13, 4}, {18, 4}, {18, 9}}));
        break;
    }
    case Glyph::Add: {
        strokePath(p, pathOf({{12, 5}, {12, 19}}));
        strokePath(p, pathOf({{5, 12}, {19, 12}}));
        break;
    }
    case Glyph::Edit: {
        strokePath(p, pathOf({{5, 19}, {6.5, 14.5}, {16, 5}, {19, 8}, {9.5, 17.5}, {5, 19}}, true));
        strokePath(p, pathOf({{14, 7}, {17, 10}}));
        break;
    }
    case Glyph::Copy: {
        strokePath(p, pathOf({{9, 3}, {20, 3}, {20, 14}, {17, 14}}));
        strokePath(p, pathOf({{4, 7}, {15, 7}, {15, 18}, {4, 18}}, true));
        break;
    }
    case Glyph::Folder: {
        strokePath(p, pathOf({{3, 6}, {10, 6}, {12, 8.5}, {21, 8.5}, {21, 19}, {3, 19}}, true));
        break;
    }
    case Glyph::FolderOpen: {
        strokePath(p, pathOf({{3, 6}, {10, 6}, {12, 8.5}, {20, 8.5}, {20, 12}}));
        strokePath(p, pathOf({{3, 19}, {5, 11}, {22, 11}, {19.5, 19}}, true));
        break;
    }
    case Glyph::File: {
        strokePath(p, pathOf({{6, 3}, {14, 3}, {19, 8}, {19, 21}, {6, 21}}, true));
        strokePath(p, pathOf({{14, 3}, {14, 8}, {19, 8}}));
        break;
    }
    case Glyph::Search: {
        QPainterPath circle;
        circle.addEllipse(QPointF(10.5, 10.5), 6.2, 6.2);
        strokePath(p, circle);
        strokePath(p, pathOf({{15.2, 15.2}, {20, 20}}));
        break;
    }
    case Glyph::Settings: {
        QPainterPath circle;
        circle.addEllipse(QPointF(12, 12), 3.1, 3.1);
        strokePath(p, circle);
        for (int i = 0; i < 8; ++i) {
            const double angle = i * M_PI / 4.0;
            const QPointF a(12 + std::cos(angle) * 6.4, 12 + std::sin(angle) * 6.4);
            const QPointF b(12 + std::cos(angle) * 9.0, 12 + std::sin(angle) * 9.0);
            strokePath(p, pathOf({{a.x(), a.y()}, {b.x(), b.y()}}));
        }
        break;
    }
    case Glyph::Bucket: {
        strokePath(p, pathOf({{4, 7}, {20, 7}}));
        strokePath(p, pathOf({{5.5, 7}, {7, 20}, {17, 20}, {18.5, 7}}));
        QPainterPath top;
        top.addEllipse(QPointF(12, 7), 8, 2.6);
        strokePath(p, top);
        break;
    }
    case Glyph::Columns: {
        strokePath(p, pathOf({{4, 5}, {20, 5}, {20, 19}, {4, 19}}, true));
        strokePath(p, pathOf({{9.5, 5}, {9.5, 19}}));
        strokePath(p, pathOf({{15, 5}, {15, 19}}));
        break;
    }
    case Glyph::SortAsc: {
        strokePath(p, pathOf({{5, 18}, {12, 7}, {19, 18}}));
        break;
    }
    case Glyph::SortDesc: {
        strokePath(p, pathOf({{5, 8}, {12, 19}, {19, 8}}));
        break;
    }
    case Glyph::Home: {
        strokePath(p, pathOf({{3, 11.5}, {12, 4}, {21, 11.5}}));
        strokePath(p, pathOf({{5.5, 10.5}, {5.5, 20}, {18.5, 20}, {18.5, 10.5}}));
        break;
    }
    case Glyph::Up: {
        strokePath(p, pathOf({{12, 19}, {12, 6}}));
        strokePath(p, pathOf({{6, 12}, {12, 6}, {18, 12}}));
        break;
    }
    case Glyph::Back: {
        strokePath(p, pathOf({{14, 5}, {7, 12}, {14, 19}}));
        break;
    }
    case Glyph::Forward: {
        strokePath(p, pathOf({{10, 5}, {17, 12}, {10, 19}}));
        break;
    }
    case Glyph::Close: {
        strokePath(p, pathOf({{6, 6}, {18, 18}}));
        strokePath(p, pathOf({{18, 6}, {6, 18}}));
        break;
    }
    case Glyph::Check: {
        strokePath(p, pathOf({{5, 12.5}, {10, 17.5}, {19, 7}}));
        break;
    }
    case Glyph::Warning: {
        strokePath(p, pathOf({{12, 4}, {21, 20}, {3, 20}}, true));
        strokePath(p, pathOf({{12, 10}, {12, 15}}));
        strokePath(p, pathOf({{12, 17.6}, {12, 17.7}}));
        break;
    }
    case Glyph::Info: {
        QPainterPath circle;
        circle.addEllipse(QPointF(12, 12), 8.2, 8.2);
        strokePath(p, circle);
        strokePath(p, pathOf({{12, 11}, {12, 16.5}}));
        strokePath(p, pathOf({{12, 7.8}, {12, 7.9}}));
        break;
    }
    case Glyph::Stop: {
        strokePath(p, pathOf({{6.5, 6.5}, {17.5, 6.5}, {17.5, 17.5}, {6.5, 17.5}}, true));
        break;
    }
    case Glyph::Clear: {
        strokePath(p, pathOf({{6, 6}, {18, 18}}));
        strokePath(p, pathOf({{18, 6}, {6, 18}}));
        break;
    }
    case Glyph::Save: {
        strokePath(p, pathOf({{4, 4}, {16.5, 4}, {20, 7.5}, {20, 20}, {4, 20}}, true));
        strokePath(p, pathOf({{8, 4}, {8, 10}, {16, 10}, {16, 4}}));
        strokePath(p, pathOf({{7.5, 20}, {7.5, 14}, {16.5, 14}, {16.5, 20}}));
        break;
    }
    case Glyph::Plug: {
        strokePath(p, pathOf({{9, 3}, {9, 8}}));
        strokePath(p, pathOf({{15, 3}, {15, 8}}));
        strokePath(p, pathOf({{6, 8}, {18, 8}, {17, 14}, {12, 18}, {7, 14}}, true));
        strokePath(p, pathOf({{12, 18}, {12, 21}}));
        break;
    }
    case Glyph::Clock: {
        QPainterPath circle;
        circle.addEllipse(QPointF(12, 12), 8.4, 8.4);
        strokePath(p, circle);
        strokePath(p, pathOf({{12, 7}, {12, 12.4}, {16, 14.4}}));
        break;
    }
    case Glyph::Size: {
        strokePath(p, pathOf({{4, 6}, {20, 6}}));
        strokePath(p, pathOf({{4, 12}, {15, 12}}));
        strokePath(p, pathOf({{4, 18}, {11, 18}}));
        break;
    }
    }
}

} // namespace

void apply(QApplication &app) {
    // Fluent's UI font on Windows, falling back to whatever the system offers.
    QFont font = QApplication::font();
    const QStringList preferred{QStringLiteral("Segoe UI Variable Text"),
                                QStringLiteral("Segoe UI"),
                                QStringLiteral("Microsoft YaHei UI")};
    const QStringList families = QFontDatabase::families();
    for (const QString &candidate : preferred) {
        if (families.contains(candidate)) {
            font.setFamily(candidate);
            break;
        }
    }
    font.setPointSizeF(9.0);
    font.setHintingPreference(QFont::PreferFullHinting);
    app.setFont(font);

    app.setStyleSheet(QString::fromUtf8(kStyleSheet));
}

QString styleSheetName() {
    return QStringLiteral("fluent.qss (embedded)");
}

QIcon icon(Glyph glyph, const QColor &colour, int px) {
    const int scale = 2; // draw at 2x and let Qt downscale: crisp on HiDPI
    QPixmap pm(px * scale, px * scale);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(colour);
    p.scale(px * scale / 24.0, px * scale / 24.0);
    drawGlyph(p, glyph);
    p.end();

    pm.setDevicePixelRatio(scale);
    return QIcon(pm);
}

QString formatBytes(qint64 bytes) {
    if (bytes < 0) {
        return QStringLiteral("—");
    }
    // Decimal units, matching the original's ByteCountSI so numbers read the
    // same as the size a provider reports in its own console.
    static const char *units[] = {"B", "kB", "MB", "GB", "TB", "PB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1000.0 && unit < 5) {
        value /= 1000.0;
        ++unit;
    }
    if (unit == 0) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', value < 10 ? 1 : 0).arg(QLatin1String(units[unit]));
}

QString formatWhen(const QDateTime &when) {
    if (!when.isValid()) {
        return QStringLiteral("—");
    }

    const QDateTime now = QDateTime::currentDateTime();
    const qint64 secs = when.secsTo(now);

    if (secs < 0) {
        return when.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    }
    if (secs < 60) {
        return QStringLiteral("just now");
    }
    if (secs < 3600) {
        const int m = static_cast<int>(secs / 60);
        return QStringLiteral("%1 min ago").arg(m);
    }
    if (secs < 86400 && when.date() == now.date()) {
        return QStringLiteral("%1:%2 today")
            .arg(when.time().hour(), 2, 10, QLatin1Char('0'))
            .arg(when.time().minute(), 2, 10, QLatin1Char('0'));
    }
    const qint64 days = when.date().daysTo(now.date());
    if (days < 7) {
        return days == 1 ? QStringLiteral("yesterday") : QStringLiteral("%1 days ago").arg(days);
    }
    if (when.date().year() == now.date().year()) {
        return when.toString(QStringLiteral("dd MMM HH:mm"));
    }
    return when.toString(QStringLiteral("yyyy-MM-dd"));
}

} // namespace Theme
} // namespace s3desktop
