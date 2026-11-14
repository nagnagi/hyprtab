#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QObject>
#include <QElapsedTimer>
#include <QPixmap>
#include <QQmlEngine>
#include <QTimer>

class QQuickWindow;
#include <QQuickImageProvider>
#include <QString>
#include <QVector>

struct WindowInfo {
    QString address;
    QString title;
    QString className;
    QString initialClass;
    QString workspaceName;
    int workspaceId = 0;
    QString monitorName;
    bool active = false;
    long long focusHistoryId = 0;
    int pid = 0;
};

class WindowModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        AddressRole = Qt::UserRole + 1,
        TitleRole,
        ClassNameRole,
        InitialClassRole,
        WorkspaceNameRole,
        WorkspaceIdRole,
        MonitorNameRole,
        ActiveRole,
    };

    explicit WindowModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setWindows(const QVector<WindowInfo> &wins);
    const WindowInfo *at(int row) const;

private:
    QVector<WindowInfo> m_wins;
};

class IconProvider : public QQuickImageProvider {
public:
    IconProvider();
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;
};

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(WindowModel *model READ model CONSTANT)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY visibleChanged)
    Q_PROPERTY(QString activeMonitorName READ activeMonitorName NOTIFY activeMonitorChanged)
    Q_PROPERTY(int windowCount READ windowCount NOTIFY windowCountChanged)

public:
    explicit Backend(QObject *parent = nullptr);

    void setWindow(QQuickWindow *w);

    WindowModel *model() const;
    int currentIndex() const;
    void setCurrentIndex(int index);
    bool visible() const;
    void setVisible(bool v);
    QString activeMonitorName() const;
    int windowCount() const;

    Q_INVOKABLE void show();
    Q_INVOKABLE void hide();
    Q_INVOKABLE void next();
    Q_INVOKABLE void prev();
    Q_INVOKABLE void select(int index);
    Q_INVOKABLE void commit();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void activate(const QString &address);

    // Handles one CLI command line received over the local socket.
    QString handleCommand(const QString &line);

signals:
    void currentIndexChanged();
    void visibleChanged();
    void activeMonitorChanged();
    void windowCountChanged();

private:
    void applyScreen();
    void polishWindow();

    WindowModel m_model;
    QQuickWindow *m_window = nullptr;
    QTimer *m_focusTimer = nullptr;
    QElapsedTimer m_visibleSince;
    int m_currentIndex = 0;
    bool m_visible = false;
    bool m_busy = false;
    QString m_activeMonitorName;
    QString m_selectedAddress;
    QString m_selfAddress;
    bool m_selfFloating = false;
};
