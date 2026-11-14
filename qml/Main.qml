import QtQuick
import QtQuick.Window

Window {
    id: root
    visible: backend.visible
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
    color: "transparent"
    title: "hyprtab"
    // Not fullscreen: a fuzzel-like floating panel (float+center via Hyprland window rules).
    // Fixed size so Hyprland's tiling cannot resize the panel before it floats.
    width: Math.min(920, (root.screen ? root.screen.width : 1920) - 80)
    height: Math.min(300, (root.screen ? root.screen.height : 1080) - 80)
    minimumWidth: width
    maximumWidth: width
    minimumHeight: height
    maximumHeight: height

    Component.onCompleted: backend.refresh()

    onVisibleChanged: {
        if (visible) {
            backend.refresh()
            root.requestActivate()
            listView.forceActiveFocus()
        }
    }

    // Keep the window list fresh while the switcher is open.
    Timer {
        interval: 500
        running: root.visible
        repeat: true
        onTriggered: backend.refresh()
    }

    // Hyprland may have resized us while tiled; re-assert the panel size
    // once the window has floated.
    Timer {
        interval: 400
        running: root.visible
        repeat: false
        onTriggered: {
            root.width = Math.min(920, (root.screen ? root.screen.width : 1920) - 80)
            root.height = Math.min(300, (root.screen ? root.screen.height : 1080) - 80)
        }
    }

    Rectangle {
        id: panel
        anchors.fill: parent
        radius: 16
        color: "#f21c2333"
        border.color: "#3a4a63"
        border.width: 1

        Column {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10

            Text {
                id: header
                text: "ウィンドウを切り替え (" + windowModel.rowCount + ")"
                color: "#e8ecf1"
                font.pixelSize: 16
                font.bold: true
            }

            ListView {
                id: listView
                width: parent.width
                height: parent.height - header.height - footer.height - parent.spacing * 2
                clip: true
                orientation: ListView.Horizontal
                model: windowModel
                currentIndex: backend.currentIndex
                highlightFollowsCurrentItem: true
                focus: true
                boundsBehavior: Flickable.StopAtBounds
                spacing: 8

                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Tab) {
                        backend.next()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Backtab) {
                        backend.prev()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        backend.commit()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Escape) {
                        backend.cancel()
                        event.accepted = true
                    }
                }
                Keys.onReleased: (event) => {
                    if (event.key === Qt.Key_Alt || event.key === Qt.Key_Meta) {
                        backend.commit()
                        event.accepted = true
                    }
                }

                delegate: Rectangle {
                    id: card
                    required property int index
                    required property string title
                    required property string className
                    required property string initialClass
                    required property string workspaceName
                    required property string monitorName
                    required property bool active

                    width: 168
                    height: ListView.view.height
                    radius: 12
                    color: card.ListView.isCurrentItem ? "#3a6ea5"
                                                       : (ma.containsMouse ? "#26ffffff" : "#14ffffff")
                    Behavior on color { ColorAnimation { duration: 80 } }

                    Column {
                        anchors.centerIn: parent
                        width: parent.width - 16
                        spacing: 6

                        Image {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 48
                            height: 48
                            source: "image://hypricon/" + card.className + "|" + card.initialClass
                            sourceSize: Qt.size(48, 48)
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                        }

                        Text {
                            text: card.title
                            color: "#e8ecf1"
                            font.pixelSize: 13
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                        }

                        Text {
                            text: card.className + " · ws " + card.workspaceName
                            color: "#9aa4b0"
                            font.pixelSize: 10
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                        }

                        Rectangle {
                            visible: card.active
                            width: 8
                            height: 8
                            radius: 4
                            color: "#4cc2ff"
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                    }

                    MouseArea {
                        id: ma
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            backend.select(card.index)
                            backend.commit()
                        }
                    }
                }
            }

            Text {
                id: footer
                text: "Tab / Shift+Tab: 選択移動    Enter: 決定    Esc: キャンセル    Altを離す: 決定"
                color: "#9aa4b0"
                font.pixelSize: 12
            }
        }
    }
}
