/*****************************************************************************
 * Copyright (C) 2022 VLC authors and VideoLAN
 *
 * Authors: Benjamin Arnaud <bunjee@omega.gg>
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

#include "control_list_filter.hpp"

// Player includes
#include "player_controller.hpp"
#include "control_list_model.hpp"

// Ctor / dtor

/* explicit */ ControlListFilter::ControlListFilter(QObject * parent)
    : QSortFilterProxyModel(parent) {}

// QAbstractProxyModel reimplementation

void ControlListFilter::setSourceModel(QAbstractItemModel * sourceModel) /* override */
{
    assert(sourceModel->inherits("ControlListModel"));

    QSortFilterProxyModel::setSourceModel(sourceModel);
}

// Protected QSortFilterProxyModel reimplementation

bool ControlListFilter::filterAcceptsRow(int source_row, const QModelIndex &) const /* override */
{
    QAbstractItemModel * model = sourceModel();

    if (model == nullptr || m_player == nullptr)
        return true;

    QVariant variant = model->data(model->index(source_row, 0), ControlListModel::ID_ROLE);

    if (variant.isValid() == false)
        return true;

    ControlListModel::ControlType type
        = static_cast<ControlListModel::ControlType> (variant.toInt());

    // NOTE: These controls are completely hidden when the current media does not support them.
    if (type == ControlListModel::TELETEXT_BUTTONS)
    {
        return m_player->isTeletextAvailable();
    }
    else if (type == ControlListModel::DVD_MENUS_BUTTON)
    {
        return m_player->hasMenu();
    }

    return true;
}

// Properties

PlayerController * ControlListFilter::player()
{
    return m_player;
}

void ControlListFilter::setPlayer(PlayerController * player)
{
    if (m_player == player) return;

    if (m_player)
        disconnect(m_player, nullptr, this, nullptr);

    m_player = player;

    connect(player, &PlayerController::teletextAvailableChanged, this, &ControlListFilter::invalidate);
    connect(player, &PlayerController::hasMenuChanged,           this, &ControlListFilter::invalidate);

    invalidate();

    emit playerChanged();
}
