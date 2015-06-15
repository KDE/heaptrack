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

#ifndef CHARTMODEL_H
#define CHARTMODEL_H

#include <QAbstractTableModel>
#include <QVector>

struct ChartRow
{
    quint64 timeStamp;
    quint64 leaked;
    quint64 allocations;
};

Q_DECLARE_TYPEINFO(ChartRow, Q_MOVABLE_TYPE);
using ChartData = QVector<ChartRow>;
Q_DECLARE_METATYPE(ChartData)

class ChartModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit ChartModel(QObject* parent = nullptr);
    virtual ~ChartModel();

    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;

public slots:
    void resetData(const ChartData& data);

private:
    ChartData m_data;
};

#endif // CHARTMODEL_H
