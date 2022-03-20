/*****************************************************************************
 * Copyright (C) 2021 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

import QtQuick          2.11
import QtQuick.Controls 2.4
import QtQuick.Layouts  1.11
import QtQml.Models     2.2

import org.videolan.medialib 0.1
import org.videolan.vlc 0.1

import "qrc:///widgets/" as Widgets
import "qrc:///main/"    as MainInterface
import "qrc:///util/"    as Util
import "qrc:///style/"

FocusScope {
    id: root

    // Properties

    readonly property int contentMargin: (_currentView.contentMargin) ? _currentView.contentMargin
                                                                      : 0

    // NOTE: Specify an optional header for the view.
    property Component header: undefined

    property Item headerItem: (_currentView) ? _currentView.headerItem : undefined

    readonly property int currentIndex: _currentView.currentIndex

    property int initialIndex: 0

    /* required */ property var model

    // NOTE: The ContextMenu depends on the model so we have to provide it too.
    /* required */ property var contextMenu

    property var sortModel: [
        { text: I18n.qtr("Alphabetic"), criteria: "title"    },
        { text: I18n.qtr("Duration"),   criteria: "duration" }
    ]

    // Aliases

    property alias dragItem: dragItem

    // Private

    property alias _currentView: view.currentItem

    // Events

    onModelChanged: resetFocus()

    onInitialIndexChanged: resetFocus()

    // Connections

    Connections {
        target: MainCtx

        onGridViewChanged: {
            if (MainCtx.gridView) view.replace(grid)
            else                  view.replace(list)
        }
    }

    Connections {
        target: model

        onCountChanged: {
            if (model.count === 0 || modelSelect.hasSelection)
                return;

            resetFocus();
        }
    }

    // Functions

    function setCurrentItemFocus(reason) {
        _currentView.setCurrentItemFocus(reason);
    }

    function resetFocus() {
        if (model.count === 0) return

        var initialIndex = root.initialIndex

        if (initialIndex >= model.count)
            initialIndex = 0

        modelSelect.select(model.index(initialIndex, 0), ItemSelectionModel.ClearAndSelect)

        if (_currentView)
            _currentView.positionViewAtIndex(initialIndex, ItemView.Contain)
    }

    function getLabel(model) {
        if (!model) return ""

        return [
            model.resolution_name || "",
            model.channel || ""
        ].filter(function(a) { return a !== "" })
    }

    // Events

    function onAction(indexes) {
        g_mainDisplay.showPlayer()

        MediaLib.addAndPlay(model.getIdsForIndexes(indexes))
    }

    function onDoubleClick(object) {
        g_mainDisplay.play(MediaLib, object.id)
    }

    function onLabelGrid(object) { return getLabel(object) }
    function onLabelList(object) { return getLabel(object) }

    // Private events

    function _onNavigationUp() {
        // NOTE: We are calling the header focus function when we have one.
        if (headerItem && headerItem.focus)
            headerItem.forceActiveFocus(Qt.TabFocusReason)
        else
            Navigation.defaultNavigationUp()
    }

    function _onNavigationCancel() {
        if (_currentView.currentIndex <= 0) {
            Navigation.defaultNavigationCancel()
        } else {
            _currentView.currentIndex = 0

            _currentView.positionViewAtIndex(0, ItemView.Contain)
        }
    }

    // Children

    Widgets.StackViewExt {
        id: view

        anchors.fill: parent

        focus: (model.count !== 0)

        initialItem: (MainCtx.gridView) ? grid : list
    }

    Widgets.MLDragItem {
        id: dragItem

        mlModel: root.model

        indexes: modelSelect.selectedIndexes

        coverRole: "thumbnail"
    }

    Util.SelectableDelegateModel {
        id: modelSelect

        model: root.model
    }

    Component {
        id: grid

        MainInterface.MainGridView {
            id: gridView

            // Properties

            property Item currentItem: Item{}

            // Settings

            cellWidth : VLCStyle.gridItem_video_width
            cellHeight: VLCStyle.gridItem_video_height

            model: root.model

            selectionDelegateModel: modelSelect

            headerDelegate: root.header

            activeFocusOnTab: true

            // Navigation

            Navigation.parentItem: root

            Navigation.upAction: _onNavigationUp

            // NOTE: cancelAction takes a function, we pass it directly.
            Navigation.cancelAction: root._onNavigationCancel

            // Events

            // NOTE: Define the initial position and selection. This is done on activeFocus rather
            //       than Component.onCompleted because modelSelect.selectedGroup update itself
            //       after this event.
            onActiveFocusChanged: {
                if (activeFocus == false || model.count === 0 || modelSelect.hasSelection)
                    return;

                modelSelect.select(model.index(0,0), ItemSelectionModel.ClearAndSelect);
            }

            onActionAtIndex: root.onAction(modelSelect.selectedIndexes)

            // Connections

            Connections {
                target: root.contextMenu

                onShowMediaInformation: gridView.switchExpandItem(index)
            }

            // Children

            Widgets.GridShadows {
                id: shadows

                coverWidth: VLCStyle.gridCover_video_width
                coverHeight: VLCStyle.gridCover_video_height
            }

            delegate: VideoGridItem {
                id: gridItem

                // properties required by ExpandGridView

                property var model: ({})
                property int index: -1

                // Settings

                opacity: (gridView.expandIndex !== -1
                          &&
                          gridView.expandIndex !== gridItem.index) ? 0.7 : 1

                labels: root.onLabelGrid(model)

                // FIXME: Sometimes MLBaseModel::getDataAt returns {} so we use 'isNew === true'.
                showNewIndicator: (model.isNew === true)

                dragItem: root.dragItem

                unselectedUnderlay: shadows.unselected
                selectedUnderlay: shadows.selected

                // Events

                onItemClicked: gridView.leftClickOnItem(modifier, index)

                onItemDoubleClicked: root.onDoubleClick(model)

                onContextMenuButtonClicked: {
                    gridView.rightClickOnItem(index);

                    root.contextMenu.popup(modelSelect.selectedIndexes, globalMousePos,
                                           { "information" : index });
                }

                // Animations

                Behavior on opacity { NumberAnimation { duration: VLCStyle.duration_faster } }
            }

            expandDelegate: VideoInfoExpandPanel {
                width: gridView.width

                x: 0

                model: root.model

                Navigation.parentItem: gridView

                Navigation.cancelAction: function() { gridView.retract() }
                Navigation.upAction    : function() { gridView.retract() }
                Navigation.downAction  : function() { gridView.retract() }

                onRetract: gridView.retract()
            }
        }
    }

    Component {
        id: list

        VideoListDisplay {
            id: listView

            readonly property real contentMargin: VLCStyle.margin_normal

            // Settings

            model: root.model

            selectionDelegateModel: modelSelect

            dragItem: root.dragItem

            header: root.header

            headerTopPadding: VLCStyle.margin_normal

            headerPositioning: ListView.InlineHeader

            activeFocusOnTab: true

            // Navigation

            Navigation.parentItem: root

            Navigation.upAction: _onNavigationUp

            //cancelAction takes a *function* pass it directly
            Navigation.cancelAction: root._onNavigationCancel

            // Events

            onActionForSelection: root.onAction(modelSelect.selectedIndexes)

            onItemDoubleClicked: root.onDoubleClick(model)

            onContextMenuButtonClicked: root.contextMenu.popup(modelSelect.selectedIndexes, globalMousePos)

            onRightClick: root.contextMenu.popup(modelSelect.selectedIndexes, globalMousePos)

            // Functions

            function onLabels(model) { return root.onLabelList(model); }
        }
    }

    EmptyLabel {
        anchors.fill: parent

        coverWidth : VLCStyle.dp(182, VLCStyle.scale)
        coverHeight: VLCStyle.dp(114, VLCStyle.scale)

        visible: (model.count === 0)

        focus: visible

        text: I18n.qtr("No video found\nPlease try adding sources, by going to the Network tab")

        cover: VLCStyle.noArtVideoCover

        Navigation.parentItem: root
    }
}
