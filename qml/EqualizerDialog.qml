import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Quince

// Ten bands from 31 Hz to 16 kHz, with presets.
Dialog {
    id: dialog

    readonly property AudioEffects effects: App.effects

    function label(frequency) {
        return frequency >= 1000 ? (frequency / 1000) + "k" : String(frequency)
    }

    title: qsTr("Equalizer")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(640, Overlay.overlay ? Overlay.overlay.width - 48 : 640)
    standardButtons: Dialog.Close
    onClosed: destroy()

    // title and button bar lie on the glass too
    Material.background: "transparent"
    background: Glass {
        radius: Style.radiusLarge
    }
    Overlay.modal: GlassScrim {}
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Style.normal }
            NumberAnimation { property: "scale"; from: 0.95; to: 1; duration: Style.slow; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Style.fast }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Switch {
                text: qsTr("On")
                checked: dialog.effects.equalizer
                onToggled: dialog.effects.equalizer = checked
            }
            Item {
                Layout.fillWidth: true
            }
            ComboBox {
                Layout.preferredWidth: 220
                model: dialog.effects.presets
                displayText: dialog.effects.presetName(dialog.effects.preset)
                currentIndex: dialog.effects.presets.indexOf(dialog.effects.preset)
                delegate: ItemDelegate {
                    required property string modelData
                    width: ListView.view.width
                    text: dialog.effects.presetName(modelData)
                }
                onActivated: index => dialog.effects.applyPreset(dialog.effects.presets[index])
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 240
            enabled: dialog.effects.equalizer
            opacity: enabled ? 1 : 0.5
            spacing: 0

            Repeater {
                model: dialog.effects.bandFrequencies

                ColumnLayout {
                    id: band
                    required property int index
                    required property int modelData
                    // equal shares of the width
                    Layout.preferredWidth: 1
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 4

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        readonly property real gain: dialog.effects.bands[band.index]
                        text: (gain > 0 ? "+" : "") + gain
                        color: Style.textDim
                        font.pixelSize: 11
                        font.features: { "tnum": 1 }
                    }
                    Slider {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.fillHeight: true
                        orientation: Qt.Vertical
                        from: -12
                        to: 12
                        stepSize: 0.5
                        wheelEnabled: true
                        value: dialog.effects.bands[band.index]
                        onMoved: dialog.effects.setBand(band.index, value)
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: dialog.label(band.modelData)
                        color: Style.textDim
                        font.pixelSize: 12
                    }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("Hz. Boosts are held below full scale by a limiter, so loud parts do not clip.")
            color: Style.textDim
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }
    }
}
