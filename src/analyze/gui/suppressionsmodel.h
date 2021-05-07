/*
 * Copyright 2021 Milian Wolff <milian.wolff@kdab.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef SUPPRESSIONSMODEL_H
#define SUPPRESSIONSMODEL_H

#include <QAbstractTableModel>
#include <QVector>

struct Suppression;
struct SummaryData;

class SuppressionsModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit SuppressionsModel(QObject* parent = nullptr);
    ~SuppressionsModel();

    void setSuppressions(const SummaryData& summaryData);

    enum class Columns
    {
        Matches,
        Leaked,
        Pattern,
        COLUMN_COUNT,
    };
    int columnCount(const QModelIndex& parent = {}) const override;
    int rowCount(const QModelIndex& parent = {}) const override;

    enum Roles
    {
        SortRole = Qt::UserRole,
        TotalCostRole,
    };

    QVariant headerData(int section, Qt::Orientation orientation = Qt::Horizontal,
                        int role = Qt::DisplayRole) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

private:
    QVector<Suppression> m_suppressions;
    qint64 m_totalAllocations = 0;
    qint64 m_totalLeaked = 0;
};

#endif // SUPPRESSIONSMODEL_H
