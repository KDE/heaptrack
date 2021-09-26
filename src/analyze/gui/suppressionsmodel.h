/*
    SPDX-FileCopyrightText: 2021 Milian Wolff <milian.wolff@kdab.com>

    SPDX-License-Identifier: LGPL-2.1-or-later
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
