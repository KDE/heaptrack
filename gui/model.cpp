/*
 * Copyright 2015 Milian Wolff <mail@milianw.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "model.h"

Model::Model(QObject* parent)
{

}

Model::~Model()
{
}

QVariant Model::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole || section < 0 || section >= NUM_COLUMNS) {
        return QVariant();
    }
    switch (static_cast<Columns>(section)) {
        case LocationColumn:
            return tr("Location");
        case AllocationsColumn:
            return tr("Allocations");
        case PeakColumn:
            return tr("Peak");
        case LeakedColumn:
            return tr("Leaked");
        case AllocatedColumn:
            return tr("Allocated");
        case NUM_COLUMNS:
            break;
    }
    return QVariant();
}

QVariant Model::data(const QModelIndex& index, int role) const
{
    return QVariant();
}

QModelIndex Model::index(int row, int column, const QModelIndex& parent) const
{
    return QModelIndex();
}

QModelIndex Model::parent(const QModelIndex& child) const
{
    return QModelIndex();
}

int Model::rowCount(const QModelIndex& parent) const
{
    return 0;
}

int Model::columnCount(const QModelIndex& parent) const
{
    return NUM_COLUMNS;
}
