#include "backend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>
#include <functional>

// ---------------------------------------------------------------------------
// WindowModel
// ---------------------------------------------------------------------------

WindowModel::WindowModel(QObject *parent) : QAbstractListModel(parent) {}

int WindowModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_wins.size());
}

QVariant WindowModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_wins.size())
        return {};
    const WindowInfo &w = m_wins[index.row()];
    switch (role) {
    case AddressRole: return w.address;
    case TitleRole: return w.title;
    case ClassNameRole: return w.className;
    case InitialClassRole: return w.initialClass;
    case WorkspaceNameRole: return w.workspaceName;
    case WorkspaceIdRole: return w.workspaceId;
    case MonitorNameRole: return w.monitorName;
    case ActiveRole: return w.active;
    }
    return {};
}

QHash<int, QByteArray> WindowModel::roleNames() const {
    return {
        {AddressRole, "address"},
        {TitleRole, "title"},
        {ClassNameRole, "className"},
        {InitialClassRole, "initialClass"},
        {WorkspaceNameRole, "workspaceName"},
        {WorkspaceIdRole, "workspaceId"},
        {MonitorNameRole, "monitorName"},
        {ActiveRole, "active"},
    };
}

void WindowModel::update(const QVector<WindowInfo> &wins) {
    if (wins.size() == m_wins.size()) {
        bool same = true;
        for (int i = 0; i < wins.size(); ++i) {
            if (wins[i].address != m_wins[i].address) {
                same = false;
                break;
            }
        }
        if (same) {
            // Only titles/flags may have changed: update in place, no reset.
            m_wins = wins;
            if (!m_wins.isEmpty())
                emit dataChanged(index(0), index(m_wins.size() - 1));
            return;
        }
    }
    beginResetModel();
    m_wins = wins;
    endResetModel();
}

const WindowInfo *WindowModel::at(int row) const {
    return (row >= 0 && row < m_wins.size()) ? &m_wins[row] : nullptr;
}

// ---------------------------------------------------------------------------
// IconProvider — resolves window class names to theme icons
// ---------------------------------------------------------------------------

IconProvider::IconProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}

// Looks up the Icon= entry of a .desktop file whose StartupWMClass (or file
// name) matches the window class.
static QString desktopFileIcon(const QString &className) {
    const QString cls = className.toLower();
    QStringList dirs;
    const QString xdg = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    if (!xdg.isEmpty())
        dirs << xdg;
    dirs << QStringLiteral("/usr/local/share/applications")
         << QStringLiteral("/usr/share/applications");

    for (const QString &dir : dirs) {
        const QDir d(dir);
        const QStringList entries = d.entryList({QStringLiteral("*.desktop")}, QDir::Files);
        for (const QString &e : entries) {
            QFile f(d.filePath(e));
            if (!f.open(QIODevice::ReadOnly))
                continue;
            const QByteArray content = f.readAll();
            QString iconValue;
            bool wmMatch = false;
            for (const QByteArray &line : content.split('\n')) {
                if (line.startsWith("Icon="))
                    iconValue = QString::fromUtf8(line.mid(5)).trimmed();
                else if (line.startsWith("StartupWMClass=")) {
                    const QString wm = QString::fromUtf8(line.mid(15)).trimmed().toLower();
                    if (wm == cls || wm.split(QLatin1Char(';')).contains(cls))
                        wmMatch = true;
                }
            }
            const bool nameMatch = QFileInfo(e).completeBaseName().toLower() == cls;
            if ((wmMatch || nameMatch) && !iconValue.isEmpty())
                return iconValue;
        }
    }
    return {};
}

// Case-insensitive file search inside icon theme dirs (Qt's theme lookup
// misses mixed-case names like org.gnome.Evince).
static QString findIconFile(const QString &name) {
    static QHash<QString, QString> cache;
    const auto hit = cache.constFind(name);
    if (hit != cache.constEnd())
        return hit.value();

    const QString lower = name.toLower();
    QStringList roots;
    roots << QStringLiteral("/usr/share/icons")
          << QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/icons");

    QString found;
    for (const QString &root : roots) {
        const QDir rootDir(root);
        if (!rootDir.exists())
            continue;
        const QStringList themes = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &theme : themes) {
            const QDir themeDir(rootDir.filePath(theme));
            QStringList appDirs{themeDir.filePath(QStringLiteral("scalable/apps")),
                                themeDir.filePath(QStringLiteral("symbolic/apps"))};
            const QStringList sizes = themeDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString &s : sizes)
                appDirs << themeDir.filePath(s + QStringLiteral("/apps"));
            for (const QString &dir : appDirs) {
                const QDir d(dir);
                if (!d.exists())
                    continue;
                const QStringList files = d.entryList(QDir::Files);
                for (const QString &file : files) {
                    const QFileInfo fi(file);
                    const QString suffix = fi.suffix().toLower();
                    if (fi.completeBaseName().toLower() == lower
                        && (suffix == QLatin1String("svg") || suffix == QLatin1String("png") || suffix == QLatin1String("xpm"))) {
                        found = d.filePath(file);
                        break;
                    }
                }
                if (!found.isEmpty())
                    break;
            }
            if (!found.isEmpty())
                break;
        }
        if (!found.isEmpty())
            break;
    }
    cache.insert(name, found);
    return found;
}

QString IconProvider::resolve(const QString &id) {
    static QHash<QString, QString> cache;
    const auto it = cache.constFind(id);
    if (it != cache.constEnd())
        return it.value();

    // NB: "|" would be percent-encoded (%7C) in the image:// URL, so use "__".
    const QStringList parts = id.split(QStringLiteral("__"));
    QString resolved;
    // 1. theme icon named after the window class
    for (const QString &p : parts) {
        if (p.isEmpty())
            continue;
        for (const QString &c : {p.toLower(), p}) {
            if (QIcon::hasThemeIcon(c)) {
                resolved = c;
                break;
            }
        }
        if (!resolved.isEmpty())
            break;
    }
    // 2. .desktop file Icon= entry matched by StartupWMClass / file name
    if (resolved.isEmpty()) {
        for (const QString &p : parts) {
            if (p.isEmpty())
                continue;
            QStringList names{p};
            const int dot = p.lastIndexOf(QLatin1Char('.'));
            if (dot > 0 && dot < p.size() - 1)
                names << p.mid(dot + 1); // "org.gnome.Evince" -> "Evince"
            for (const QString &name : names) {
                const QString iconValue = desktopFileIcon(name);
                if (iconValue.isEmpty())
                    continue;
                if (iconValue.startsWith(QLatin1Char('/'))) {
                    if (QFileInfo::exists(iconValue))
                        resolved = iconValue;
                } else if (QIcon::hasThemeIcon(iconValue)) {
                    resolved = iconValue;
                } else {
                    const QString file = findIconFile(iconValue);
                    if (!file.isEmpty())
                        resolved = file;
                }
                if (!resolved.isEmpty())
                    break;
            }
            if (!resolved.isEmpty())
                break;
        }
    }
    // 3. direct file search by class name
    if (resolved.isEmpty()) {
        for (const QString &p : parts) {
            if (p.isEmpty())
                continue;
            const QString file = findIconFile(p.toLower());
            if (!file.isEmpty()) {
                resolved = file;
                break;
            }
        }
    }
    // 4. fallbacks
    if (resolved.isEmpty())
        resolved = QStringLiteral("application-x-executable");

    cache.insert(id, resolved);
    return resolved;
}

QPixmap IconProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) {
    const QString resolved = resolve(id);
    const QIcon icon = resolved.startsWith(QLatin1Char('/'))
                           ? QIcon(resolved)
                           : QIcon::fromTheme(resolved, QIcon::fromTheme(QStringLiteral("application-x-executable")));

    const QSize sz = (requestedSize.isValid() && !requestedSize.isEmpty()) ? requestedSize : QSize(32, 32);
    if (size)
        *size = sz;
    return icon.pixmap(sz);
}

// ---------------------------------------------------------------------------
// Backend
// ---------------------------------------------------------------------------

static QByteArray hyprctl(const QStringList &args, int timeoutMs = 2000) {
    QProcess p;
    p.start(QStringLiteral("hyprctl"), args);
    if (!p.waitForStarted(1000))
        return {};
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(500);
        return {};
    }
    return p.readAllStandardOutput();
}

// Classic dispatcher syntax (Hyprland <= 0.56). Returns false on newer
// Lua-based hyprctl so the caller can fall back to the Lua form.
static bool tryClassicDispatch(const QString &dispatcher, const QString &arg = {}) {
    QProcess p;
    p.start(QStringLiteral("hyprctl"), arg.isEmpty()
                ? QStringList{QStringLiteral("dispatch"), dispatcher}
                : QStringList{QStringLiteral("dispatch"), dispatcher, arg});
    return p.waitForStarted(1000) && p.waitForFinished(2000) && p.exitCode() == 0;
}

static bool tryClassicFocus(const QString &selector) {
    return tryClassicDispatch(QStringLiteral("focuswindow"), selector);
}

static void focusWindowLua(const QString &luaExpr) {
    QProcess::startDetached(QStringLiteral("hyprctl"), {QStringLiteral("dispatch"), luaExpr});
}

Backend::Backend(QObject *parent) : QObject(parent) {}

void Backend::setWindow(QQuickWindow *w) { m_window = w; }

void Backend::applyScreen() {
    if (!m_window || m_activeMonitorName.isEmpty())
        return;
    const auto screens = QGuiApplication::screens();
    for (QScreen *s : screens) {
        if (s->name() == m_activeMonitorName) {
            m_window->setScreen(s);
            return;
        }
    }
}

// Grabs keyboard focus for the overlay itself (Wayland app_id "hyprtab").
static void focusSelf() {
    if (tryClassicFocus(QStringLiteral("class:hyprtab")))
        return;
    QProcess::startDetached(QStringLiteral("hyprctl"), {QStringLiteral("eval"),
        QStringLiteral("for _,w in ipairs(hl.get_windows()) do "
                       "if w.class==\"hyprtab\" then hl.dispatch(hl.dsp.focus({window=w})) end end")});
}

WindowModel *Backend::model() const {
    return const_cast<WindowModel *>(&m_model);
}

int Backend::currentIndex() const { return m_currentIndex; }

void Backend::setCurrentIndex(int index) {
    const int n = m_model.rowCount();
    if (n <= 0) {
        if (m_currentIndex != 0) {
            m_currentIndex = 0;
            emit currentIndexChanged();
        }
        return;
    }
    index = std::clamp(index, 0, n - 1);
    if (index == m_currentIndex)
        return;
    m_currentIndex = index;
    if (const WindowInfo *w = m_model.at(index))
        m_selectedAddress = w->address;
    emit currentIndexChanged();
}

bool Backend::visible() const { return m_visible; }

void Backend::setVisible(bool v) {
    if (m_visible == v)
        return;
    m_visible = v;
    if (v) {
        m_visibleSince.start();
    } else {
        if (m_focusTimer) {
            m_focusTimer->stop();
            m_focusTimer->deleteLater();
            m_focusTimer = nullptr;
        }
        if (m_altTimer)
            m_altTimer->stop();
    }
    emit visibleChanged();
}

QString Backend::activeMonitorName() const { return m_activeMonitorName; }

int Backend::windowCount() const { return m_model.rowCount(); }

void Backend::refresh() {
    if (m_busy)
        return;
    m_busy = true;
    const QByteArray clients = hyprctl({QStringLiteral("clients"), QStringLiteral("-j")});
    const QByteArray monitors = hyprctl({QStringLiteral("monitors"), QStringLiteral("-j")});
    const QByteArray workspaces = hyprctl({QStringLiteral("workspaces"), QStringLiteral("-j")});
    const QByteArray active = hyprctl({QStringLiteral("activewindow"), QStringLiteral("-j")});
    m_busy = false;

    QHash<int, QString> monitorNames;
    QString activeMonitor;
    const QJsonArray mons = QJsonDocument::fromJson(monitors).array();
    for (const QJsonValue &m : mons) {
        const QJsonObject o = m.toObject();
        const QString name = o.value(QStringLiteral("name")).toString();
        monitorNames.insert(o.value(QStringLiteral("id")).toInt(), name);
        if (o.value(QStringLiteral("focused")).toBool())
            activeMonitor = name;
    }

    QHash<int, QString> wsNames;
    const QJsonArray wss = QJsonDocument::fromJson(workspaces).array();
    for (const QJsonValue &w : wss) {
        const QJsonObject o = w.toObject();
        const int id = o.value(QStringLiteral("id")).toInt();
        QString name = o.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            name = QString::number(id);
        wsNames.insert(id, name);
    }

    const QString activeAddr = QJsonDocument::fromJson(active).object()
                                   .value(QStringLiteral("address")).toString();

    QVector<WindowInfo> wins;
    const QJsonArray arr = QJsonDocument::fromJson(clients).array();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        if (!o.value(QStringLiteral("mapped")).toBool())
            continue;
        if (o.value(QStringLiteral("hidden")).toBool())
            continue; // minimized
        WindowInfo w;
        w.address = o.value(QStringLiteral("address")).toString();
        w.title = o.value(QStringLiteral("title")).toString();
        w.className = o.value(QStringLiteral("class")).toString();
        w.initialClass = o.value(QStringLiteral("initialClass")).toString();
        if (w.className == QLatin1String("hyprtab")) {
            m_selfAddress = w.address;
            m_selfFloating = o.value(QStringLiteral("floating")).toBool();
            continue; // ourselves
        }
        const QJsonObject ws = o.value(QStringLiteral("workspace")).toObject();
        if (ws.value(QStringLiteral("type")).toString() == QLatin1String("special"))
            continue; // scratchpad
        w.workspaceId = ws.value(QStringLiteral("id")).toInt();
        w.workspaceName = wsNames.value(w.workspaceId, ws.value(QStringLiteral("name")).toString());
        w.monitorName = monitorNames.value(o.value(QStringLiteral("monitor")).toInt());
        w.active = (w.address == activeAddr);
        w.focusHistoryId = o.value(QStringLiteral("focusHistoryID")).toVariant().toLongLong();
        w.pid = o.value(QStringLiteral("pid")).toInt();
        wins.append(w);
    }

    // focusHistoryID: 0 is the currently focused window, ascending = MRU order.
    std::stable_sort(wins.begin(), wins.end(),
                     [](const WindowInfo &a, const WindowInfo &b) { return a.focusHistoryId < b.focusHistoryId; });

    const int oldCount = m_model.rowCount();
    m_model.update(wins);
    if (m_model.rowCount() != oldCount)
        emit windowCountChanged();

    // Keep the selection on the same window across refreshes.
    if (!m_selectedAddress.isEmpty()) {
        for (int i = 0; i < m_model.rowCount(); ++i) {
            if (m_model.at(i)->address == m_selectedAddress) {
                m_currentIndex = i;
                break;
            }
        }
    }
    setCurrentIndex(std::clamp(m_currentIndex, 0, std::max(0, m_model.rowCount() - 1)));

    if (activeMonitor != m_activeMonitorName) {
        m_activeMonitorName = activeMonitor;
        emit activeMonitorChanged();
    }
    applyScreen();

    // The panel is a normal floating window: if focus moved elsewhere
    // (e.g. the user clicked another window), close without switching.
    // While Alt is still held (mid Alt-Tab gesture), never auto-close.
    if (m_visible && m_visibleSince.isValid() && m_visibleSince.elapsed() > 1000
        && !activeAddr.isEmpty() && activeAddr != m_selfAddress
        && !(m_altDownAtShow && isAltDown())) {
        cancel();
    }
}

// Hyprland >= 0.57 only: hyprctl eval does not print return values, so the
// result is passed through a temp file. Returns false on older versions,
// which simply disables the quick Alt-release commit.
bool Backend::isAltDown() {
    const QString tmp = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                        + QStringLiteral("/hyprtab-alt-down");
    const QString code = QStringLiteral(
        "local f=io.open(\"%1\",\"w\") "
        "f:write(tostring(hl.is_key_down(\"Alt_L\") or hl.is_key_down(\"Alt_R\"))) "
        "f:close()").arg(tmp);
    QProcess p;
    p.start(QStringLiteral("hyprctl"), {QStringLiteral("eval"), code});
    if (!p.waitForStarted(1000) || !p.waitForFinished(2000) || p.exitCode() != 0)
        return false;
    QFile f(tmp);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return f.readAll().trimmed() == "true";
}

void Backend::show() {
    refresh();
    const int n = m_model.rowCount();
    if (n == 0) {
        setVisible(false);
        return;
    }
    // First Alt-Tab lands on the previously focused window (MRU index 1).
    setCurrentIndex(n > 1 ? 1 : 0);
    setVisible(true);
    // If the user releases Alt immediately after the first Tab, commit at once:
    // remember whether Alt was held when the panel appeared, then poll.
    m_altDownAtShow = isAltDown();
    if (!m_altTimer) {
        m_altTimer = new QTimer(this);
        m_altTimer->setInterval(200);
        connect(m_altTimer, &QTimer::timeout, this, [this] {
            if (m_visible && m_altDownAtShow && !isAltDown())
                commit();
        });
    }
    m_altTimer->start();
    // Make sure the overlay holds keyboard focus so key events reach QML.
    if (m_focusTimer) {
        m_focusTimer->stop();
        m_focusTimer->deleteLater();
    }
    m_focusTimer = new QTimer(this);
    m_focusTimer->setSingleShot(true);
    QTimer *t = m_focusTimer;
    connect(t, &QTimer::timeout, this, [this] {
        focusSelf();
        polishWindow();
    });
    t->start(300);
    // Re-run after QML re-asserted the panel size (window may have been
    // resized while tiled before floating).
    QTimer::singleShot(800, this, [this] { if (m_visible) polishWindow(); });
}

// After the panel is mapped: make it float + centered even without window rules.
void Backend::polishWindow() {
    refresh(); // picks up m_selfAddress / m_selfFloating now that we are mapped
    if (m_selfAddress.isEmpty())
        return;
    const QString w = QStringLiteral("hl.get_window(\"address:%1\")").arg(m_selfAddress);
    if (!m_selfFloating && !tryClassicDispatch(QStringLiteral("togglefloating")))
        focusWindowLua(QStringLiteral("hl.dsp.window.float({window=%1})").arg(w));
    if (!tryClassicDispatch(QStringLiteral("centerwindow")))
        focusWindowLua(QStringLiteral("hl.dsp.window.center({window=%1})").arg(w));
    // Size is handled by the windowrule (size 920 212) and the QML min/max
    // size re-assert; do not dispatch a resize here (it can visibly jump).
}

void Backend::hide() { setVisible(false); }

void Backend::next() {
    if (!m_visible) {
        show();
        return;
    }
    const int n = m_model.rowCount();
    if (n > 0)
        setCurrentIndex((m_currentIndex + 1) % n);
}

void Backend::prev() {
    if (!m_visible) {
        show();
        const int n = m_model.rowCount();
        if (n > 0)
            setCurrentIndex(n - 1);
        return;
    }
    const int n = m_model.rowCount();
    if (n > 0)
        setCurrentIndex((m_currentIndex - 1 + n) % n);
}

void Backend::select(int index) { setCurrentIndex(index); }

void Backend::activate(const QString &address) {
    if (address.isEmpty())
        return;
    if (tryClassicFocus(QStringLiteral("address:") + address))
        return;
    // Hyprland >= 0.57 (Lua-based hyprctl)
    focusWindowLua(QStringLiteral("hl.dsp.focus({window=hl.get_window(\"address:%1\")})").arg(address));
}

void Backend::commit() {
    if (const WindowInfo *w = m_model.at(m_currentIndex))
        activate(w->address);
    setVisible(false);
}

void Backend::cancel() { setVisible(false); }

QString Backend::handleCommand(const QString &line) {
    const QStringList parts = line.simplified().split(QLatin1Char(' '));
    if (parts.isEmpty() || parts.first().isEmpty())
        return QStringLiteral("error empty command\n");
    const QString cmd = parts.first();

    if (cmd == QStringLiteral("ping"))
        return QStringLiteral("pong\n");
    if (cmd == QStringLiteral("show")) { show(); return QStringLiteral("ok\n"); }
    if (cmd == QStringLiteral("hide")) { hide(); return QStringLiteral("ok\n"); }
    if (cmd == QStringLiteral("next")) { next(); return QStringLiteral("ok %1\n").arg(m_currentIndex); }
    if (cmd == QStringLiteral("prev")) { prev(); return QStringLiteral("ok %1\n").arg(m_currentIndex); }
    if (cmd == QStringLiteral("select") && parts.size() > 1) {
        select(parts.at(1).toInt());
        return QStringLiteral("ok %1\n").arg(m_currentIndex);
    }
    if (cmd == QStringLiteral("commit")) {
        QString addr;
        if (const WindowInfo *w = m_model.at(m_currentIndex))
            addr = w->address;
        commit();
        return addr + QLatin1Char('\n');
    }
    if (cmd == QStringLiteral("cancel")) { cancel(); return QStringLiteral("ok\n"); }
    if (cmd == QStringLiteral("refresh")) {
        refresh();
        return QStringLiteral("ok %1\n").arg(m_model.rowCount());
    }
    if (cmd == QStringLiteral("list")) {
        refresh();
        QString out;
        for (int i = 0; i < m_model.rowCount(); ++i) {
            const WindowInfo *w = m_model.at(i);
            out += QStringLiteral("%1\t%2\t%3\t%4\n").arg(i).arg(w->address, w->className, w->title);
        }
        return out;
    }
    if (cmd == QStringLiteral("activate") && parts.size() > 1) {
        activate(parts.at(1));
        return QStringLiteral("ok\n");
    }
    if (cmd == QStringLiteral("icon") && parts.size() > 1)
        return IconProvider::resolve(parts.at(1)) + QLatin1Char('\n');
    if (cmd == QStringLiteral("iconpix") && parts.size() > 1) {
        IconProvider prov;
        QSize sz;
        const QPixmap pm = prov.requestPixmap(parts.at(1), &sz, QSize(48, 48));
        return QStringLiteral("%1 null=%2 %3x%4\n")
            .arg(IconProvider::resolve(parts.at(1))).arg(pm.isNull()).arg(pm.width()).arg(pm.height());
    }
    if (cmd == QStringLiteral("images")) {
        QString out;
        const char *statusNames[] = {"Null", "Ready", "Loading", "Error"};
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *it) {
            const QByteArray cn = it->metaObject()->className();
            if (cn.contains("Image")) {
                const int st = it->property("status").toInt();
                out += QString::fromUtf8(cn) + " src=" + it->property("source").toString()
                       + " status=" + (st >= 0 && st <= 3 ? statusNames[st] : "?")
                       + " painted=" + it->property("paintedWidth").toString() + "x" + it->property("paintedHeight").toString()
                       + QLatin1Char('\n');
            }
            for (QQuickItem *c : it->childItems())
                walk(c);
        };
        if (m_window)
            walk(m_window->contentItem());
        return out.isEmpty() ? QStringLiteral("(no images)\n") : out;
    }
    if (cmd == QStringLiteral("rects")) {
        QString out;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *it) {
            const QByteArray cn = it->metaObject()->className();
            if (cn.contains("Rectangle")) {
                const double wd = it->property("width").toDouble();
                const double ht = it->property("height").toDouble();
                if (wd > 50 && wd < 500)
                    out += QStringLiteral("%1 %2x%3\n").arg(QString::fromUtf8(cn)).arg(wd).arg(ht);
            }
            for (QQuickItem *c : it->childItems())
                walk(c);
        };
        if (m_window)
            walk(m_window->contentItem());
        return out.isEmpty() ? QStringLiteral("(no rects)\n") : out;
    }
    if (cmd == QStringLiteral("screens")) {
        QString out = QStringLiteral("activeMonitor=") + m_activeMonitorName
                      + QStringLiteral(" self=") + m_selfAddress
                      + QStringLiteral(" visible=") + (m_visible ? "1" : "0") + QLatin1Char('\n');
        const auto screens = QGuiApplication::screens();
        for (const QScreen *s : screens)
            out += QStringLiteral("%1 %2,%3 %4x%5\n").arg(s->name()).arg(s->geometry().x())
                      .arg(s->geometry().y()).arg(s->geometry().width()).arg(s->geometry().height());
        return out;
    }
    return QStringLiteral("error unknown command\n");
}
