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

#ifndef MLBOOKMARKMODEL_HPP
#define MLBOOKMARKMODEL_HPP

#include <QAbstractListModel>
#include <memory>

#include <vlc_common.h>
#include <vlc_media_library.h>
#include <vlc_player.h>
#include <vlc_threads.h>
#include <vlc_cxx_helpers.hpp>

#include "mlhelper.hpp"


class MediaLib;
class MLBookmarkModel : public QAbstractListModel
{
public:
    MLBookmarkModel( MediaLib* medialib, vlc_player_t* player, QObject* parent );
    virtual ~MLBookmarkModel();

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole ) const override;
    bool setData( const QModelIndex& index, const QVariant& value, int role = Qt::EditRole ) override;
    Qt::ItemFlags flags( const QModelIndex & ) const override;
    int rowCount( const QModelIndex& index = {} ) const override;
    int columnCount( const QModelIndex& index = {} ) const override;
    QModelIndex index( int row, int column,
                       const QModelIndex& parent = QModelIndex() ) const override;
    QModelIndex parent( const QModelIndex& ) const override;
    QVariant headerData( int section, Qt::Orientation orientation, int role ) const override;
    void sort( int column, Qt::SortOrder order ) override;

    void add();
    void remove( const QModelIndexList& indexes );
    void clear();
    void select( const QModelIndex& index );

private:
    static void onCurrentMediaChanged( vlc_player_t* player, input_item_t* media,
                                       void* data );
    static void onPlaybackStateChanged( vlc_player_t* player, vlc_player_state state,
                                        void* data );

    void updateMediaId(uint64_t revision, const QString mediaUri);

    enum RefreshOperation {
        MLBOOKMARKMODEL_REFRESH,
        MLBOOKMARKMODEL_CLEAR,
    };
    void refresh( RefreshOperation forceClear );
private:
    using BookmarkListPtr = ml_unique_ptr<vlc_ml_bookmark_list_t>;
    using InputItemPtr = std::unique_ptr<input_item_t, decltype(&input_item_Release)>;

    MediaLib* m_mediaLib = nullptr;
    vlc_player_t* m_player = nullptr;
    vlc_player_listener_id* m_listener = nullptr;

    // Assume to be only used from the GUI thread
    BookmarkListPtr m_bookmarks;
    uint64_t m_currentMediaId = 0;

    //avoid starting two beginReset simultaneously
    unsigned m_countPendingReset = 0;

    mutable vlc::threads::mutex m_mutex;
    uint64_t m_revision = 0;
    // current item & media id can be accessed by any thread and therefore
    // must be accessed with m_mutex held
    InputItemPtr m_currentItem;

    vlc_ml_sorting_criteria_t m_sort = VLC_ML_SORTING_INSERTIONDATE;
    bool m_desc = false;
};

#endif // MLBOOKMARKMODEL_HPP
