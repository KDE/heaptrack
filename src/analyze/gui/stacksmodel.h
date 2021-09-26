/*
    SPDX-FileCopyrightText: 2015-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef STACKSMODEL_H
#define STACKSMODEL_H

#include <QAbstractListModel>

class StacksModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit StacksModel(QObject* parent = nullptr);
    ~StacksModel();

    void setStackIndex(int index);
    void fillFromIndex(const QModelIndex& leaf);
    void clear();

    int rowCount(const QModelIndex& parent) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

signals:
    void stacksFound(int stacks);

private:
    QVector<QVector<QModelIndex>> m_data;
    int m_stackIndex = 0;
};

#endif // STACKSMODEL_H
