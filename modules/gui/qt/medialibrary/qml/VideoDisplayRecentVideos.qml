/*****************************************************************************
 * Copyright (C) 2019 VLC authors and VideoLAN
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
import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQml.Models 2.2

import org.videolan.medialib 0.1
import org.videolan.vlc 0.1

import "qrc:///widgets/" as Widgets
import "qrc:///util/" as Util
import "qrc:///util/Helpers.js" as Helpers
import "qrc:///style/"

FocusScope {
    id: root

    property alias leftPadding: recentVideosColumn.leftPadding
    property alias rightPadding: recentVideosColumn.rightPadding

    // Settings

    implicitHeight: recentVideosColumn.height

    focus: listView.count > 0

    // Functions

    function _actionAtIndex(index) {
        g_mainDisplay.showPlayer()
        MediaLib.addAndPlay( model.getIdForIndexes(index), [":restore-playback-pos=2"] )
    }

    // Childs

    VideoContextMenu {
        id: contextMenu

        model: listView.model
    }

    Column {
        id: recentVideosColumn

        width: root.width

        spacing: VLCStyle.margin_xsmall

        topPadding: VLCStyle.margin_normal

        bottomPadding: VLCStyle.margin_normal

        Widgets.SubtitleLabel {
            text: I18n.qtr("Continue Watching")

            visible: listView.visible
        }

        Widgets.KeyNavigableListView {
            id: listView

            width: root.width - root.leftPadding - root.rightPadding

            implicitHeight: VLCStyle.gridItem_video_height + VLCStyle.gridItemSelectedBorder
                            +
                            VLCStyle.margin_xlarge

            spacing: VLCStyle.column_margin_width

            // NOTE: Sometimes, we want items to be visible on the sides.
            displayMarginBeginning: root.leftPadding
            displayMarginEnd: root.leftPadding

            // NOTE: We want navigation buttons to be centered on the item cover.
            buttonMargin: VLCStyle.margin_xsmall + VLCStyle.gridCover_video_height / 2 - buttonLeft.height / 2

            orientation: ListView.Horizontal

            focus: true

            fadeColor: VLCStyle.colors.bg

            Navigation.parentItem: root

            visible: listView.count > 0

            model: MLRecentsVideoModel {
                ml: MediaLib
            }

            header: Item {
                width: VLCStyle.margin_normal
            }

            delegate: VideoGridItem {
                id: gridItem

                x: selectedBorderWidth
                y: selectedBorderWidth

                pictureWidth: VLCStyle.gridCover_video_width
                pictureHeight: VLCStyle.gridCover_video_height

                selected: activeFocus

                focus: true

                unselectedUnderlay: shadows.unselected
                selectedUnderlay: shadows.selected

                onItemDoubleClicked: gridItem.play()

                onItemClicked: {
                    listView.currentIndex = index
                    this.forceActiveFocus(Qt.MouseFocusReason)
                }

                // NOTE: contextMenu.popup wants a list of indexes.
                onContextMenuButtonClicked: {
                    contextMenu.popup([root.model.index(index, 0)],
                                      globalMousePos,
                                      { "player-options": [":restore-playback-pos=2"] })
                }

                dragItem: Widgets.DragItem {
                    coverRole: "thumbnail"

                    indexes: [index]

                    onRequestData: {
                        setData(identifier, [model])
                    }

                    function getSelectedInputItem(cb) {
                        return MediaLib.mlInputItem([model.id], cb)
                    }
                }

                Behavior on opacity {
                    NumberAnimation {
                        duration: VLCStyle.duration_faster
                    }
                }

                function play() {
                    if (model.id !== undefined) {
                        g_mainDisplay.showPlayer()
                        MediaLib.addAndPlay( model.id, [":restore-playback-pos=2"] )
                    }
                }
            }

            footer: Item {
                width: VLCStyle.margin_normal
            }

            onActionAtIndex: root._actionAtIndex(index)

            Widgets.GridShadows {
                id: shadows

                coverWidth: VLCStyle.gridCover_video_width
                coverHeight: VLCStyle.gridCover_video_height
            }
        }

        Widgets.SubtitleLabel {
            text: I18n.qtr("Videos")
        }
    }
}
