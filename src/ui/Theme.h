#pragma once

#include <QColor>
#include <QDateTime>
#include <QIcon>
#include <QString>

class QApplication;

namespace s3desktop {

/// The visual language of the application, in one place.
///
/// The original app used Fyne's default widgets, which on Windows render as a
/// flat Android-ish surface with a single accent colour. This build follows the
/// Windows 11 / Fluent conventions users already know from Explorer: a muted
/// neutral canvas, a compact single-line ribbon, 1px separators instead of
/// borders on every control, and a single accent reserved for selection and the
/// default button.
///
/// Icons are drawn at runtime rather than shipped as files. That keeps the
/// repository free of binary assets and lets every glyph inherit the current
/// text colour, which is what makes the ribbon look consistent in both the
/// enabled and disabled states.
namespace Theme {

// --- Palette ---------------------------------------------------------------

inline QColor canvas()     { return QColor(0xF3, 0xF3, 0xF3); }
inline QColor surface()    { return QColor(0xFC, 0xFC, 0xFC); }
inline QColor surfaceAlt() { return QColor(0xFA, 0xFA, 0xFA); }
inline QColor border()     { return QColor(0xE0, 0xE0, 0xE0); }
inline QColor borderSoft() { return QColor(0xEB, 0xEB, 0xEB); }
inline QColor text()       { return QColor(0x1B, 0x1B, 0x1B); }
inline QColor textMuted()  { return QColor(0x61, 0x61, 0x61); }
inline QColor textFaint()  { return QColor(0x8A, 0x8A, 0x8A); }
inline QColor accent()     { return QColor(0x00, 0x67, 0xC0); }
inline QColor accentHover(){ return QColor(0x1A, 0x77, 0xCC); }
inline QColor accentSoft() { return QColor(0xE5, 0xF1, 0xFB); }
inline QColor danger()     { return QColor(0xC4, 0x2B, 0x1C); }
inline QColor success()    { return QColor(0x0F, 0x7B, 0x0F); }
inline QColor warning()    { return QColor(0x9D, 0x5D, 0x00); }

// --- Metrics ---------------------------------------------------------------

/// Row height in the object table. Fluent's compact density: readable without
/// the airy 40px rows that make a 50k-object list feel endless.
constexpr int rowHeight = 26;
constexpr int ribbonHeight = 64;
constexpr int controlRadius = 4;
constexpr int iconSize = 16;

// --- Setup -----------------------------------------------------------------

/// Apply the application-wide font, palette and stylesheet.
void apply(QApplication &app);

/// Path or embedded name of the stylesheet, for logging.
QString styleSheetName();

// --- Icons -----------------------------------------------------------------

/// Names understood by icon(). Unknown names yield a null icon rather than a
/// placeholder, so a typo shows up as a missing glyph in review.
enum class Glyph {
    Upload, Download, Delete, Link, Refresh, Add, Edit, Copy,
    Folder, FolderOpen, File, Search, Settings, Bucket, Columns,
    SortAsc, SortDesc, Home, Up, Back, Forward, Close, Check, Warning,
    Info, Stop, Clear, Save, Plug, Clock, Size
};

/// A monochrome icon drawn at `px` logical pixels in `colour`.
QIcon icon(Glyph glyph, const QColor &colour = text(), int px = iconSize);

/// Human-readable byte count, decimal (1000-based) as the original displayed.
QString formatBytes(qint64 bytes);

/// Relative-ish timestamp: "just now", "5 min ago", "3 days ago", else a date.
/// Matches the original's intent while staying readable for old objects.
QString formatWhen(const QDateTime &when);

} // namespace Theme
} // namespace s3desktop
