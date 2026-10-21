import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import Quince

// Categories on the left, the chosen page of cards on the right. A search
// shows the matching rows of every page at once. Options that depend on a
// switch only show while it is on.
Dialog {
    id: dialog

    readonly property Settings settings: App.settings
    // key of the open category
    property string page: "playback"
    readonly property string query: searchField.text.trim().toLowerCase()
    readonly property bool searching: query !== ""
    readonly property bool narrow: width < 640
    readonly property int listWidth: narrow ? 64 : 200

    readonly property var pages: [
        {key: "playback", name: qsTr("Playback"), icon: "icons/play.svg"},
        {key: "sound", name: qsTr("Sound"), icon: "icons/volume.svg"},
        {key: "lyrics", name: qsTr("Lyrics"), icon: "icons/captions.svg"},
        {key: "look", name: qsTr("Look"), icon: "icons/palette.svg"},
        {key: "desktop", name: qsTr("Desktop"), icon: "icons/monitor.svg"},
        {key: "discord", name: qsTr("Discord"), icon: "icons/broadcast.svg"},
        {key: "downloads", name: qsTr("Downloads"), icon: "icons/download.svg"},
    ]

    function hit(text) {
        return searching && !!text && text.toLowerCase().indexOf(query) >= 0
    }
    function pageName(key) {
        const p = pages.find(p => p.key === key)
        return p ? p.name : ""
    }

    // A setting: title and hint on the left, its controls on the right (below
    // when wide). Lives in a Card.
    component SettingRow: Item {
        id: row
        property string title
        property string hint
        // a sub-option: only while its parent option is on
        property bool shown: true
        property bool sub: false
        property bool wide: false
        // a click anywhere on the row emits tapped (switch rows)
        property bool tappable: false
        signal tapped()
        default property alias controls: slot.data
        readonly property bool showing: shown && (!dialog.searching || dialog.hit(title) || dialog.hit(hint)
                                                  || (!!parent && parent.matchAll === true))
        readonly property bool first: !!parent && parent.firstShown === row

        Layout.fillWidth: true
        implicitHeight: Math.max(52, grid.implicitHeight + 20)
        visible: showing
        onShowingChanged: if (showing) appear.restart()

        NumberAnimation {
            id: appear
            target: row
            property: "opacity"
            from: 0
            to: 1
            duration: Style.normal
            easing.type: Easing.OutCubic
        }
        TapHandler {
            enabled: row.tappable && row.enabled
            onTapped: row.tapped()
        }
        HoverHandler {
            enabled: row.tappable
            cursorShape: Qt.PointingHandCursor
        }
        Rectangle {
            visible: !row.first
            x: 16
            width: row.width - 32
            height: 1
            color: Style.line
        }
        // ties a sub-option to the one above
        Rectangle {
            visible: row.sub
            x: 24
            y: 12
            width: 2
            height: row.height - 24
            radius: 1
            color: Qt.alpha(Style.accent, 0.45)
        }
        GridLayout {
            id: grid
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: row.sub ? 40 : 16
            anchors.rightMargin: 12
            columns: row.wide ? 1 : 2
            columnSpacing: 16
            rowSpacing: 8
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    Layout.fillWidth: true
                    text: row.title
                    wrapMode: Text.Wrap
                    font.pixelSize: 14
                    color: Style.text
                }
                Label {
                    Layout.fillWidth: true
                    visible: row.hint !== ""
                    text: row.hint
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
                    color: Style.textDim
                }
            }
            RowLayout {
                id: slot
                Layout.fillWidth: row.wide
                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                spacing: 8
            }
        }
    }
    component SwitchRow: SettingRow {
        id: switchRow
        property bool checked
        signal toggled(bool value)
        tappable: true
        onTapped: switchRow.toggled(!switchRow.checked)
        Switch {
            checked: switchRow.checked
            onToggled: switchRow.toggled(checked)
        }
    }
    // a box of options [{id, name}]
    component ChoiceRow: SettingRow {
        id: choiceRow
        property var options: []
        property var value
        property string textRole: "name"
        property string valueRole: "id"
        signal chosen(var value)
        ComboBox {
            Layout.preferredWidth: 220
            implicitHeight: 40
            topInset: 0
            bottomInset: 0
            textRole: choiceRow.textRole
            valueRole: choiceRow.valueRole
            model: choiceRow.options
            // count: the options may be set after the box was built
            currentIndex: count > 0 ? indexOfValue(choiceRow.value) : -1
            onActivated: choiceRow.chosen(currentValue)
        }
    }
    component SliderRow: SettingRow {
        id: sliderRow
        property real from: 0
        property real to: 100
        property real stepSize: 1
        property real value
        property var format: v => Math.round(v) + " %"
        signal moved(real value)
        Slider {
            id: slider
            Layout.preferredWidth: 180
            from: sliderRow.from
            to: sliderRow.to
            stepSize: sliderRow.stepSize
            value: sliderRow.value
            onMoved: sliderRow.moved(value)
        }
        Label {
            Layout.preferredWidth: 64
            text: sliderRow.format(slider.value)
            color: Style.textDim
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideRight
        }
    }
    component SpinRow: SettingRow {
        id: spinRow
        property int from: 0
        property int to: 100
        property int stepSize: 1
        property int value
        property bool editable: false
        signal modified(int value)
        SpinBox {
            implicitHeight: 40
            topInset: 0
            bottomInset: 0
            from: spinRow.from
            to: spinRow.to
            stepSize: spinRow.stepSize
            editable: spinRow.editable
            value: spinRow.value
            onValueModified: spinRow.modified(value)
        }
    }
    // Rows on a rounded surface, under a small title. Hides when none of its
    // rows shows; a search for its title or its page shows all of them.
    component Card: ColumnLayout {
        id: card
        property string title
        default property alias rows: box.data
        readonly property bool matchAll: dialog.hit(title) || (!!parent && dialog.hit(parent.pageTitle || ""))
        readonly property bool showing: box.firstShown !== null

        Layout.fillWidth: true
        spacing: 8
        visible: showing

        Label {
            Layout.leftMargin: 4
            visible: card.title !== ""
            text: card.title
            font.pixelSize: 12
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
            color: Style.textDim
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: box.implicitHeight
            radius: Style.radius
            color: Qt.alpha(Style.surfaceHigh, 0.55)
            border.width: 1
            border.color: Style.line
            ColumnLayout {
                id: box
                readonly property bool matchAll: card.matchAll
                // from the rows' own flags, not their visible (that follows the card)
                readonly property Item firstShown: {
                    const c = children
                    for (let i = 0; i < c.length; ++i)
                        if (c[i].showing === true)
                            return c[i]
                    return null
                }
                width: parent.width
                spacing: 0
            }
        }
    }
    // The cards of a category, with its name on top.
    component Page: ColumnLayout {
        id: pageItem
        property string key
        readonly property string pageTitle: dialog.pageName(key)
        readonly property bool showing: {
            const c = children
            for (let i = 0; i < c.length; ++i)
                if (c[i].showing === true)
                    return true
            return false
        }
        Layout.fillWidth: true
        spacing: 18
        visible: dialog.searching ? showing : dialog.page === key
        Label {
            text: pageItem.pageTitle
            font.pixelSize: 20
            font.weight: Font.DemiBold
            color: Style.text
        }
    }

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(880, Overlay.overlay ? Overlay.overlay.width - 48 : 880)
    height: Math.min(640, Overlay.overlay ? Overlay.overlay.height - 48 : 640)
    padding: 0
    onClosed: destroy()
    onOpened: searchField.forceActiveFocus()
    onPageChanged: {
        // also set while the dialog is built
        if (!scroller)
            return
        scroller.contentY = 0
        pageFade.restart()
    }
    Component.onCompleted: App.player.watchAudioDevices()

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
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Style.fast }
            NumberAnimation { property: "scale"; from: 1; to: 0.97; duration: Style.fast }
        }
    }

    header: Item {
        implicitHeight: 68
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 22
            anchors.rightMargin: 12
            spacing: 12
            Label {
                Layout.preferredWidth: dialog.narrow ? implicitWidth : dialog.listWidth - 34
                text: qsTr("Settings")
                font.pixelSize: 22
                font.weight: Font.DemiBold
                color: Style.text
            }
            TextField {
                id: searchField
                Layout.fillWidth: true
                Layout.maximumWidth: 380
                leftPadding: 38
                rightPadding: 12
                topPadding: 8
                bottomPadding: 8
                selectByMouse: true
                font.pixelSize: 14
                background: Rectangle {
                    implicitHeight: 38
                    radius: height / 2
                    color: searchField.activeFocus ? Style.surfaceHigh : Qt.alpha(Style.surfaceHigh, 0.7)
                    border.width: 1
                    border.color: searchField.activeFocus ? Qt.alpha(Style.accent, 0.6)
                                : searchField.hovered ? "#26ffffff" : Style.line
                    Behavior on color {
                        ColorAnimation { duration: Style.fast }
                    }
                }
                // the results start at the top
                onTextChanged: scroller.contentY = 0
                // the first Esc empties the field, the next one closes
                Keys.onEscapePressed: event => {
                    if (text !== "")
                        clear()
                    else
                        dialog.close()
                }
                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 13
                    source: "icons/search.svg"
                    sourceSize: Qt.size(18, 18)
                    opacity: 0.6
                }
                // Material's own placeholder floats up over the border when focused
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    x: searchField.leftPadding
                    text: qsTr("Search settings")
                    color: Style.textDim
                    visible: searchField.text.length === 0
                }
            }
            Item {
                Layout.fillWidth: true
            }
            ToolButton {
                icon.source: "icons/close.svg"
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Close")
                ToolTip.delay: 500
                onClicked: dialog.close()
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            // the buttons fill it; it must not take the room of the page
            Layout.fillWidth: false
            Layout.preferredWidth: dialog.listWidth
            Layout.fillHeight: true
            Layout.topMargin: 4
            spacing: 2
            opacity: dialog.searching ? 0.45 : 1
            Behavior on opacity {
                NumberAnimation { duration: Style.fast }
            }

            Repeater {
                model: dialog.pages
                ItemDelegate {
                    id: category
                    required property var modelData
                    readonly property bool selected: !dialog.searching && dialog.page === modelData.key
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    Layout.rightMargin: 10
                    implicitHeight: 42
                    text: modelData.name
                    icon.source: modelData.icon
                    icon.width: 22
                    icon.height: 22
                    // Material's 16 px on each side leave the icon only no room
                    leftPadding: dialog.narrow ? 0 : 16
                    rightPadding: dialog.narrow ? 0 : 16
                    display: dialog.narrow ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
                    font.pixelSize: 14
                    font.weight: selected ? Font.DemiBold : Font.Normal
                    icon.color: selected ? Style.accent : Style.text
                    ToolTip.visible: dialog.narrow && hovered
                    ToolTip.text: text
                    ToolTip.delay: 300
                    onClicked: {
                        searchField.clear()
                        dialog.page = modelData.key
                    }
                    background: Rectangle {
                        radius: Style.radius
                        color: category.selected ? Qt.alpha(Style.accent, 0.14)
                             : category.pressed ? Style.pressed
                             : category.hovered ? Style.hover : "transparent"
                        Behavior on color {
                            ColorAnimation { duration: Style.fast }
                        }
                        Rectangle {
                            width: 3
                            height: category.selected ? 18 : 0
                            radius: 2
                            anchors.verticalCenter: parent.verticalCenter
                            color: Style.accent
                            Behavior on height {
                                NumberAnimation { duration: Style.normal; easing.type: Easing.OutBack }
                            }
                        }
                    }
                }
            }
            Item {
                Layout.fillHeight: true
            }
        }

        Rectangle {
            Layout.fillHeight: true
            Layout.bottomMargin: 16
            width: 1
            color: Style.line
        }

        Flickable {
            id: scroller
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: column.implicitHeight + 40
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            ColumnLayout {
                id: column

                readonly property bool found: {
                    const c = children
                    for (let i = 0; i < c.length; ++i)
                        if (c[i].showing === true)
                            return true
                    return false
                }

                x: 24
                y: 4
                width: scroller.width - 48
                spacing: 18

                NumberAnimation {
                    id: pageFade
                    target: column
                    property: "opacity"
                    from: 0
                    to: 1
                    duration: Style.normal
                    easing.type: Easing.OutCubic
                }
                SmoothScroll {
                    flickable: scroller
                }

                Label {
                    Layout.topMargin: 40
                    Layout.fillWidth: true
                    visible: dialog.searching && !column.found
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Nothing found")
                    color: Style.textDim
                }

                Page {
                    key: "playback"
                    Card {
                        title: qsTr("Volume")
                        SwitchRow {
                            title: qsTr("Volume follows your hearing")
                            hint: qsTr("The slider works exponentially, so its middle sounds half as loud")
                            checked: dialog.settings.exponentialVolume
                            onToggled: value => dialog.settings.exponentialVolume = value
                        }
                        SwitchRow {
                            title: qsTr("Even out loudness")
                            hint: qsTr("Quiet and loud tracks play at the same level, like on YouTube")
                            checked: dialog.settings.normalizeLoudness
                            onToggled: value => dialog.settings.normalizeLoudness = value
                        }
                        SpinRow {
                            title: qsTr("Volume step")
                            hint: qsTr("Percent per wheel turn or key press")
                            from: 1
                            to: 10
                            value: dialog.settings.volumeStep
                            onModified: value => dialog.settings.volumeStep = value
                        }
                        ChoiceRow {
                            title: qsTr("Output")
                            options: App.player.audioDevices
                            textRole: "description"
                            valueRole: "name"
                            // the device list may arrive after the box was built
                            value: App.player.audioDevice
                            onChosen: value => dialog.settings.audioDevice = value
                        }
                    }
                    Card {
                        title: qsTr("Queue")
                        SwitchRow {
                            title: qsTr("Continue the last queue at start")
                            checked: dialog.settings.resumeSession
                            onToggled: value => dialog.settings.resumeSession = value
                        }
                        SwitchRow {
                            title: qsTr("Skip songs I disliked")
                            checked: dialog.settings.skipDisliked
                            onToggled: value => dialog.settings.skipDisliked = value
                        }
                        SwitchRow {
                            title: qsTr("Songs I pick start paused")
                            checked: dialog.settings.startPaused
                            onToggled: value => dialog.settings.startPaused = value
                        }
                    }
                    Card {
                        title: "SponsorBlock"
                        SwitchRow {
                            title: qsTr("Skip the parts of music videos without music")
                            hint: qsTr("Uses the segments SponsorBlock users marked")
                            checked: dialog.settings.sponsorBlock
                            onToggled: value => dialog.settings.sponsorBlock = value
                        }
                        SettingRow {
                            sub: true
                            wide: true
                            shown: dialog.settings.sponsorBlock
                            title: qsTr("Parts to skip")
                            Flow {
                                Layout.fillWidth: true
                                spacing: 8
                                Repeater {
                                    model: [
                                        {id: "music_offtopic", name: qsTr("Without music")},
                                        {id: "sponsor", name: qsTr("Sponsors")},
                                        {id: "selfpromo", name: qsTr("Self promotion")},
                                        {id: "interaction", name: qsTr("Subscribe reminders")},
                                        {id: "intro", name: qsTr("Intros")},
                                        {id: "outro", name: qsTr("Outros")},
                                    ]
                                    Button {
                                        id: chip
                                        required property var modelData
                                        readonly property bool chosen:
                                            dialog.settings.sponsorBlockCategories.indexOf(modelData.id) >= 0
                                        text: modelData.name
                                        Material.foreground: chosen ? Style.background : Style.text
                                        background: Rectangle {
                                            radius: height / 2
                                            color: chip.chosen ? Style.accent : chip.pressed ? Style.pressed
                                                 : chip.hovered ? Style.hover : "transparent"
                                            border.width: chip.chosen ? 0 : 1
                                            border.color: Style.line
                                            Behavior on color {
                                                ColorAnimation { duration: Style.fast }
                                            }
                                        }
                                        leftPadding: 14
                                        rightPadding: 14
                                        topInset: 0
                                        bottomInset: 0
                                        implicitHeight: 32
                                        // Material pads 14 above and below
                                        topPadding: 4
                                        bottomPadding: 4
                                        onClicked: {
                                            const list = dialog.settings.sponsorBlockCategories.filter(c => c !== modelData.id)
                                            if (!chosen)
                                                list.push(modelData.id)
                                            dialog.settings.sponsorBlockCategories = list
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Page {
                    key: "sound"
                    Card {
                        title: qsTr("Effects")
                        SettingRow {
                            title: qsTr("Equalizer")
                            tappable: true
                            onTapped: App.effects.equalizer = !App.effects.equalizer
                            Button {
                                text: App.effects.presetName(App.effects.preset)
                                flat: true
                                onClicked: App.requestDialog("equalizer")
                            }
                            Switch {
                                checked: App.effects.equalizer
                                onToggled: App.effects.equalizer = checked
                            }
                        }
                        SwitchRow {
                            title: qsTr("Compressor")
                            hint: qsTr("Quiet parts louder, loud parts quieter")
                            checked: App.effects.compressor
                            onToggled: value => App.effects.compressor = value
                        }
                        SwitchRow {
                            title: qsTr("Skip silences")
                            checked: App.effects.skipSilences
                            onToggled: value => App.effects.skipSilences = value
                        }
                        SwitchRow {
                            sub: true
                            shown: App.effects.skipSilences
                            title: qsTr("Only the silence at the start of a track")
                            checked: App.effects.skipSilencesAtStart
                            onToggled: value => App.effects.skipSilencesAtStart = value
                        }
                    }
                    Card {
                        title: qsTr("Transitions")
                        SliderRow {
                            title: qsTr("Crossfade")
                            hint: qsTr("Off plays albums gapless")
                            from: 0
                            to: 12
                            stepSize: 1
                            value: App.effects.crossfade
                            format: v => v > 0 ? qsTr("%1 s").arg(v) : qsTr("Off")
                            onMoved: value => App.effects.crossfade = value
                        }
                        SliderRow {
                            sub: true
                            shown: App.effects.crossfade > 0
                            title: qsTr("Fade in")
                            hint: qsTr("The next track comes up this fast")
                            from: 0
                            to: App.effects.crossfade
                            stepSize: 0.5
                            value: App.effects.crossfadeIn
                            format: v => v > 0 ? qsTr("%1 s").arg(v) : qsTr("As long")
                            onMoved: value => App.effects.crossfadeIn = value
                        }
                        SliderRow {
                            title: qsTr("Speed")
                            from: 0.5
                            to: 2
                            stepSize: 0.05
                            value: App.effects.speed
                            format: v => v.toFixed(2) + "×"
                            onMoved: value => App.effects.speed = value
                        }
                    }
                }

                Page {
                    key: "lyrics"
                    Card {
                        title: qsTr("Sources")
                        SwitchRow {
                            title: "LRCLIB"
                            hint: qsTr("Time-synced lyrics")
                            checked: dialog.settings.lyricsLrclib
                            onToggled: value => dialog.settings.lyricsLrclib = value
                        }
                        SwitchRow {
                            title: "Megalobiz"
                            hint: qsTr("Time-synced lyrics")
                            checked: dialog.settings.lyricsMegalobiz
                            onToggled: value => dialog.settings.lyricsMegalobiz = value
                        }
                        SwitchRow {
                            title: "Genius"
                            hint: qsTr("Plain lyrics, when no other source has any")
                            checked: dialog.settings.lyricsGenius
                            onToggled: value => dialog.settings.lyricsGenius = value
                        }
                        ChoiceRow {
                            title: qsTr("Ask first")
                            options: [
                                {id: "", name: qsTr("No preference")},
                                {id: "lrclib", name: "LRCLIB"},
                                {id: "megalobiz", name: "Megalobiz"},
                                {id: "youtube", name: "YouTube Music"},
                                {id: "genius", name: "Genius"},
                            ]
                            value: dialog.settings.lyricsPreferred
                            onChosen: value => dialog.settings.lyricsPreferred = value
                        }
                        SwitchRow {
                            title: qsTr("Inexact matches")
                            hint: qsTr("Show lyrics even if title or length differ a little")
                            checked: dialog.settings.lyricsInexact
                            onToggled: value => dialog.settings.lyricsInexact = value
                        }
                    }
                    Card {
                        title: qsTr("Display")
                        ChoiceRow {
                            title: qsTr("The sung line")
                            options: [
                                {id: "fancy", name: qsTr("Fancy: large, with a glow")},
                                {id: "scale", name: qsTr("Scale: grows")},
                                {id: "offset", name: qsTr("Offset: moves to the right")},
                                {id: "focus", name: qsTr("Focus: only it is bright")},
                            ]
                            value: dialog.settings.lyricsEffect
                            onChosen: value => dialog.settings.lyricsEffect = value
                        }
                        SwitchRow {
                            title: qsTr("Time codes next to the lines")
                            checked: dialog.settings.lyricsTimeCodes
                            onToggled: value => dialog.settings.lyricsTimeCodes = value
                        }
                        SettingRow {
                            title: qsTr("Mark for pauses")
                            hint: qsTr("Shown between lines while nothing is sung")
                            TextField {
                                Layout.preferredWidth: 80
                                implicitHeight: 40
                                horizontalAlignment: Text.AlignHCenter
                                text: dialog.settings.lyricsGapText
                                onEditingFinished: dialog.settings.lyricsGapText = text
                            }
                        }
                    }
                    Card {
                        title: qsTr("Other scripts")
                        SwitchRow {
                            title: qsTr("Latin letters under the lines")
                            hint: qsTr("For Japanese, Korean and Chinese lyrics")
                            checked: dialog.settings.lyricsRomanize
                            onToggled: value => dialog.settings.lyricsRomanize = value
                        }
                        ChoiceRow {
                            title: qsTr("Chinese characters")
                            options: [
                                {id: "", name: qsTr("As they come")},
                                {id: "hant", name: qsTr("Traditional")},
                                {id: "hans", name: qsTr("Simplified")},
                            ]
                            value: dialog.settings.lyricsChinese
                            onChosen: value => dialog.settings.lyricsChinese = value
                        }
                    }
                }

                Page {
                    key: "look"
                    Card {
                        title: qsTr("General")
                        ChoiceRow {
                            title: qsTr("Language")
                            options: [
                                {id: "", name: qsTr("As the system")},
                                {id: "en", name: "English"},
                                {id: "de", name: "Deutsch"},
                            ]
                            value: dialog.settings.language
                            onChosen: value => dialog.settings.language = value
                        }
                        ChoiceRow {
                            title: qsTr("Start page")
                            options: [
                                {id: "home", name: qsTr("Home")},
                                {id: "explore", name: qsTr("Explore")},
                                {id: "library", name: qsTr("Library")},
                                {id: "liked", name: qsTr("Liked songs")},
                            ]
                            value: dialog.settings.startPage
                            onChosen: value => dialog.settings.startPage = value
                        }
                        SwitchRow {
                            title: qsTr("Compact sidebar")
                            hint: qsTr("Icons only")
                            checked: dialog.settings.compactSidebar
                            onToggled: value => dialog.settings.compactSidebar = value
                        }
                    }
                    Card {
                        title: qsTr("Colors")
                        SwitchRow {
                            title: qsTr("Colors from the album cover")
                            checked: dialog.settings.albumColors
                            onToggled: value => dialog.settings.albumColors = value
                        }
                        SliderRow {
                            title: qsTr("Strength of the glow")
                            from: 0
                            to: 200
                            stepSize: 10
                            value: dialog.settings.glowStrength
                            onMoved: value => dialog.settings.glowStrength = value
                        }
                        SwitchRow {
                            title: qsTr("Progress bar in the album color")
                            checked: dialog.settings.coloredProgress
                            onToggled: value => dialog.settings.coloredProgress = value
                        }
                    }
                    Card {
                        title: qsTr("Clock")
                        SwitchRow {
                            title: qsTr("Clock in the top bar")
                            checked: dialog.settings.clock
                            onToggled: value => dialog.settings.clock = value
                        }
                        SettingRow {
                            sub: true
                            shown: dialog.settings.clock
                            title: qsTr("Format")
                            CheckBox {
                                text: qsTr("Seconds")
                                checked: dialog.settings.clockSeconds
                                onToggled: dialog.settings.clockSeconds = checked
                            }
                            ComboBox {
                                Layout.preferredWidth: 160
                                implicitHeight: 40
                                topInset: 0
                                bottomInset: 0
                                textRole: "name"
                                valueRole: "id"
                                model: [
                                    {id: "", name: qsTr("As the system")},
                                    {id: "24", name: qsTr("24 hours")},
                                    {id: "12", name: qsTr("12 hours")},
                                ]
                                currentIndex: count > 0 ? indexOfValue(dialog.settings.clockHours) : -1
                                onActivated: dialog.settings.clockHours = currentValue
                            }
                        }
                    }
                    Card {
                        title: qsTr("Window")
                        SwitchRow {
                            title: qsTr("See-through window")
                            hint: qsTr("For the desktop's blur, after a restart")
                            checked: dialog.settings.transparentWindow
                            onToggled: value => dialog.settings.transparentWindow = value
                        }
                        SliderRow {
                            sub: true
                            shown: dialog.settings.transparentWindow
                            title: qsTr("Window background")
                            from: 0
                            to: 100
                            stepSize: 5
                            value: dialog.settings.windowOpacity
                            onMoved: value => dialog.settings.windowOpacity = value
                        }
                    }
                    Card {
                        title: qsTr("Videos")
                        SliderRow {
                            title: qsTr("Ambient light")
                            hint: qsTr("The video's colors around it")
                            from: 0
                            to: 100
                            stepSize: 5
                            value: dialog.settings.ambientStrength
                            onMoved: value => dialog.settings.ambientStrength = value
                        }
                        SliderRow {
                            shown: dialog.settings.ambientStrength > 0
                            sub: true
                            title: qsTr("Reach of the ambient light")
                            from: 50
                            to: 200
                            stepSize: 10
                            value: dialog.settings.ambientSize
                            onMoved: value => dialog.settings.ambientSize = value
                        }
                    }
                }

                Page {
                    key: "desktop"
                    Card {
                        title: qsTr("Tray")
                        SwitchRow {
                            title: qsTr("Icon in the system tray")
                            checked: dialog.settings.tray
                            onToggled: value => dialog.settings.tray = value
                        }
                        SwitchRow {
                            sub: true
                            shown: dialog.settings.tray
                            title: qsTr("A click on the icon plays or pauses")
                            hint: qsTr("Otherwise it opens the window")
                            checked: dialog.settings.trayClickPlays
                            onToggled: value => dialog.settings.trayClickPlays = value
                        }
                        SwitchRow {
                            sub: true
                            shown: dialog.settings.tray
                            title: qsTr("Closing the window keeps playing")
                            hint: qsTr("Quince stays in the tray")
                            checked: dialog.settings.closeToTray
                            onToggled: value => dialog.settings.closeToTray = value
                        }
                    }
                    Card {
                        title: qsTr("Notifications")
                        SwitchRow {
                            title: qsTr("Notification when the track changes")
                            checked: dialog.settings.notifications
                            onToggled: value => dialog.settings.notifications = value
                        }
                        SwitchRow {
                            sub: true
                            shown: dialog.settings.notifications
                            title: qsTr("Also when playback goes on after a pause")
                            checked: dialog.settings.notifyOnResume
                            onToggled: value => dialog.settings.notifyOnResume = value
                        }
                        ChoiceRow {
                            sub: true
                            shown: dialog.settings.notifications && Qt.platform.os === "linux"
                            title: qsTr("Urgency")
                            options: [
                                {id: "low", name: qsTr("Low")},
                                {id: "normal", name: qsTr("Normal")},
                                {id: "critical", name: qsTr("Critical")},
                            ]
                            value: dialog.settings.notificationUrgency
                            onChosen: value => dialog.settings.notificationUrgency = value
                        }
                    }
                    Card {
                        title: qsTr("Shortcuts")
                        SwitchRow {
                            shown: Qt.platform.os === "linux"
                            title: qsTr("System-wide shortcuts")
                            hint: qsTr("Through the desktop portal, after a restart")
                            checked: dialog.settings.globalShortcuts
                            onToggled: value => dialog.settings.globalShortcuts = value
                        }
                    }
                }

                Page {
                    key: "discord"
                    Card {
                        SwitchRow {
                            title: qsTr("Show what I listen to in Discord")
                            checked: dialog.settings.discord
                            onToggled: value => dialog.settings.discord = value
                        }
                    }
                    Card {
                        title: qsTr("Status")
                        ChoiceRow {
                            shown: dialog.settings.discord
                            title: qsTr("The member list shows")
                            options: [
                                {id: "app", name: qsTr("The app's name")},
                                {id: "artist", name: qsTr("The artist")},
                                {id: "title", name: qsTr("The title")},
                            ]
                            value: dialog.settings.discordShows
                            onChosen: value => dialog.settings.discordShows = value
                        }
                        SwitchRow {
                            shown: dialog.settings.discord
                            title: qsTr("Show the time left")
                            checked: !dialog.settings.discordHideTimeLeft
                            onToggled: value => dialog.settings.discordHideTimeLeft = !value
                        }
                        SwitchRow {
                            shown: dialog.settings.discord
                            title: qsTr("Button \"Listen on YouTube Music\"")
                            checked: dialog.settings.discordButton
                            onToggled: value => dialog.settings.discordButton = value
                        }
                        SpinRow {
                            shown: dialog.settings.discord
                            title: qsTr("Clear the status after a pause")
                            hint: qsTr("Minutes, 0 never")
                            from: 0
                            to: 120
                            value: dialog.settings.discordPauseMinutes
                            onModified: value => dialog.settings.discordPauseMinutes = value
                        }
                    }
                    Card {
                        title: qsTr("Application")
                        SettingRow {
                            shown: dialog.settings.discord
                            wide: true
                            title: qsTr("Application ID")
                            hint: qsTr("Empty uses Pear Desktop's Discord application. For your own, create one at "
                                       + "discord.com/developers/applications and paste its Application ID.")
                            TextField {
                                Layout.fillWidth: true
                                implicitHeight: 40
                                text: dialog.settings.discordAppId
                                onEditingFinished: dialog.settings.discordAppId = text
                            }
                        }
                    }
                }

                Page {
                    key: "downloads"
                    Card {
                        title: qsTr("Files")
                        SettingRow {
                            wide: true
                            title: qsTr("Folder")
                            TextField {
                                Layout.fillWidth: true
                                implicitHeight: 40
                                text: dialog.settings.downloadFolder
                                onEditingFinished: dialog.settings.downloadFolder = text
                            }
                            Button {
                                text: qsTr("Choose")
                                flat: true
                                onClicked: folderDialog.open()
                            }
                        }
                        ChoiceRow {
                            title: qsTr("Format")
                            options: [
                                {id: "opus", name: qsTr("Opus (best quality)")},
                                {id: "m4a", name: qsTr("M4A / AAC")},
                                {id: "mp3", name: qsTr("MP3 (converted)")},
                            ]
                            hint: dialog.settings.downloadFormat === "mp3" ? qsTr("Converted from what YouTube sends")
                                                                         : qsTr("As YouTube sends it, not converted")
                            value: dialog.settings.downloadFormat
                            onChosen: value => dialog.settings.downloadFormat = value
                        }
                        ChoiceRow {
                            sub: true
                            shown: dialog.settings.downloadFormat === "mp3"
                            title: qsTr("MP3 bit rate")
                            options: [
                                {id: 128, name: "128 kbit/s"},
                                {id: 192, name: "192 kbit/s"},
                                {id: 256, name: "256 kbit/s"},
                                {id: 320, name: "320 kbit/s"},
                            ]
                            value: dialog.settings.downloadMp3Kbps
                            onChosen: value => dialog.settings.downloadMp3Kbps = value
                        }
                        SwitchRow {
                            title: qsTr("Skip tracks that are already in the folder")
                            checked: dialog.settings.downloadSkipExisting
                            onToggled: value => dialog.settings.downloadSkipExisting = value
                        }
                        SpinRow {
                            title: qsTr("Tracks per playlist at most")
                            hint: qsTr("0 downloads all")
                            from: 0
                            to: 5000
                            stepSize: 10
                            editable: true
                            value: dialog.settings.downloadPlaylistMax
                            onModified: value => dialog.settings.downloadPlaylistMax = value
                        }
                    }
                }
            }
        }
    }

    FolderDialog {
        id: folderDialog
        currentFolder: "file://" + dialog.settings.downloadFolder
        onAccepted: dialog.settings.downloadFolder = decodeURIComponent(selectedFolder.toString().replace("file://", ""))
    }
}
