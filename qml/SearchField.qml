import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import Quince

// The search box with suggestions while typing.
TextField {
    id: field

    signal searchRequested(string query)

    function submit(query) {
        popup.close()
        if (query.trim().length > 0)
            field.searchRequested(query.trim())
    }

    leftPadding: 44
    rightPadding: 16
    topPadding: 10
    bottomPadding: 10
    selectByMouse: true
    font.pixelSize: 14

    background: Rectangle {
        implicitHeight: 42
        radius: height / 2
        color: field.activeFocus ? Style.surfaceHigh : Qt.alpha(Style.surfaceHigh, 0.7)
        border.width: 1
        border.color: field.activeFocus ? Qt.alpha(Style.accent, 0.6) : field.hovered ? "#26ffffff" : Style.line
        Behavior on color {
            ColorAnimation { duration: Style.fast }
        }
        Behavior on border.color {
            ColorAnimation { duration: Style.fast }
        }
    }
    onAccepted: submit(text)
    onTextEdited: {
        if (text.trim().length > 0)
            popup.open()
        else
            popup.close()
    }
    Keys.onEscapePressed: popup.close()

    Image {
        anchors.verticalCenter: parent.verticalCenter
        x: 16
        source: "icons/search.svg"
        sourceSize: Qt.size(20, 20)
        opacity: 0.6
    }

    // Material's own placeholder floats up over the border when focused
    Label {
        anchors.verticalCenter: parent.verticalCenter
        x: field.leftPadding
        text: qsTr("Search songs, albums, artists")
        color: Style.textDim
        visible: field.text.length === 0
    }

    SearchSuggestions {
        id: suggestions
        input: field.text
    }

    Popup {
        id: popup

        y: field.height + 4
        width: field.width
        height: Math.min(column.implicitHeight + 16, 520)
        padding: 8
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        visible: false

        background: Glass {
            radius: 14
        }

        Flickable {
            anchors.fill: parent
            contentHeight: column.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: column
                width: parent.width

                Repeater {
                    model: suggestions.queries
                    ItemDelegate {
                        required property var modelData
                        width: column.width
                        text: modelData.text
                        icon.source: "icons/search.svg"
                        onClicked: {
                            field.text = modelData.query
                            field.submit(modelData.query)
                        }
                    }
                }

                Rectangle {
                    width: column.width
                    height: 1
                    color: "#1affffff"
                    visible: suggestions.queries.length > 0 && suggestions.items.length > 0
                }

                Repeater {
                    model: suggestions.items
                    ItemRow {
                        required property var modelData
                        width: column.width
                        item: modelData
                        onActivated: {
                            popup.close()
                            App.open(modelData)
                        }
                    }
                }
            }
        }
    }
}
