/*
    SPDX-FileCopyrightText: 2015-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef TREEPROXY_H
#define TREEPROXY_H

#include <QSortFilterProxyModel>

class TreeProxy final : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    explicit TreeProxy(int symbolRole, int resultDataRole, QObject* parent = nullptr);
    virtual ~TreeProxy();

public slots:
    void setFunctionFilter(const QString& functionFilter);
    void setModuleFilter(const QString& moduleFilter);

private:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;
    bool lessThan(const QModelIndex& source_left, const QModelIndex& source_right) const override;

    const int m_symbolRole;
    const int m_resultDataRole;

    QString m_functionFilter;
    QString m_moduleFilter;
};

#endif // TREEPROXY_H
