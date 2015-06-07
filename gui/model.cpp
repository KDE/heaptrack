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

#include <sstream>

using namespace std;

namespace {
QString generateSummary(const AccumulatedTraceData& data)
{
    stringstream stream;
    const double totalTimeS = 0.001 * data.totalTime;
    stream << "<qt>"
           << "<strong>total runtime</strong>: " << fixed << totalTimeS << "s.<br/>"
           << "<strong>bytes allocated in total</strong> (ignoring deallocations): " << formatBytes(data.totalAllocated)
             << " (" << formatBytes(data.totalAllocated / totalTimeS) << "/s)<br/>"
           << "<strong>calls to allocation functions</strong>: " << data.totalAllocations
             << " (" << size_t(data.totalAllocations / totalTimeS) << "/s)<br/>"
           << "<strong>peak heap memory consumption</strong>: " << formatBytes(data.peak) << "<br/>"
           << "<strong>total memory leaked</strong>: " << formatBytes(data.leaked) << "<br/>";
    stream << "</qt>";
    return QString::fromStdString(stream.str());
}
}

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
    if (index.parent().isValid()
        || index.row() < 0 || index.row() > m_data.mergedAllocations.size()
        || index.column() < 0 || index.column() > NUM_COLUMNS)
    {
        return QVariant();
    }

    const auto& allocation = m_data.mergedAllocations[index.row()];
    if (role == Qt::DisplayRole) {
        switch (static_cast<Columns>(index.column())) {
        case AllocationsColumn:
            return static_cast<quint64>(allocation.allocations);
        case PeakColumn:
            return static_cast<quint64>(allocation.peak);
        case LeakedColumn:
            return static_cast<quint64>(allocation.leaked);
        case AllocatedColumn:
            return static_cast<quint64>(allocation.allocated);
        case LocationColumn:
            return QVariant();
        case NUM_COLUMNS:
            break;
        }
    }
    return QVariant();
}

QModelIndex Model::index(int row, int column, const QModelIndex& /*parent*/) const
{
    return createIndex(row, column);
}

QModelIndex Model::parent(const QModelIndex& /*child*/) const
{
    return QModelIndex();
}

int Model::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_data.mergedAllocations.size();
}

int Model::columnCount(const QModelIndex& /*parent*/) const
{
    return NUM_COLUMNS;
}

void Model::loadFile(const QString& file)
{
    beginResetModel();
    m_data.read(file.toStdString());
    endResetModel();
    emit dataReady(generateSummary(m_data));
}
