/*
    SPDX-FileCopyrightText: 2020 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#pragma once

#include <QHeaderView>

class CostHeaderView : public QHeaderView
{
    Q_OBJECT
public:
    explicit CostHeaderView(QWidget* parent = nullptr);
    ~CostHeaderView();

private:
    void resizeEvent(QResizeEvent* event) override;
    void resizeColumns(bool reset);

    bool m_isResizing = false;
};
