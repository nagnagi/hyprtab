#include "backend.h"

#include <QGuiApplication>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>

#include <algorithm>

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

void WindowModel::setWindows(const QVector<WindowInfo> &wins) {
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

QPixmap IconProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) {
    const QStringList parts = id.split(QLatin1Char('|'));
    QStringList candidates;
    for (const QString &p : parts) {
        if (p.isEmpty())
            continue;
        candidates << p.toLower() << p;
    }
    candidates << QStringLiteral("application-x-executable")
               << QStringLiteral("window")
               << QStringLiteral("preferences-system");

    QIcon icon;
    for (const QString &c : candidates) {
        if (QIcon::hasThemeIcon(c)) {
            icon = QIcon::fromTheme(c);
            break;
        }
    }
    if (icon.isNull())
        icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));

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
    if (v)
        m_visibleSince.start();
    else if (m_focusTimer) {
        m_focusTimer->stop();
        m_focusTimer->deleteLater();
        m_focusTimer = nullptr;
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
    m_model.setWindows(wins);
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
    if (m_visible && m_visibleSince.isValid() && m_visibleSince.elapsed() > 1000
        && !activeAddr.isEmpty() && activeAddr != m_selfAddress) {
        cancel();
    }
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
    // Re-assert the panel size (tiling may have resized it before floating).
    if (!tryClassicDispatch(QStringLiteral("resizeactive"), QStringLiteral("920 300")))
        focusWindowLua(QStringLiteral("hl.dsp.window.resize({window=%1, x=920, y=300})").arg(w));
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
