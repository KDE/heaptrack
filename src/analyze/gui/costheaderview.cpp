/*
 * Copyright 2020 Milian Wolff <mail@milianw.de>
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

#include "costheaderview.h"

#include <QDebug>
#include <QEvent>
#include <QMenu>
#include <QPainter>
#include <QScopedValueRollback>

#include <cmath>

CostHeaderView::CostHeaderView(QWidget* parent)
    : QHeaderView(Qt::Horizontal, parent)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
    setSectionsMovable(true);
    setFirstSectionMovable(false);
#endif
    setDefaultSectionSize(100);
    setStretchLastSection(false);
    connect(this, &QHeaderView::sectionCountChanged, this, [this]() { resizeColumns(false); });
    connect(this, &QHeaderView::sectionResized, this, [this](int index, int oldSize, int newSize) {
        if (m_isResizing)
            return;
        QScopedValueRollback<bool> guard(m_isResizing, true);
        if (index != 0) {
            // give/take space from first column
            resizeSection(0, sectionSize(0) - (newSize - oldSize));
        } else {
            // distribute space across all columns
            // use actual width as oldSize/newSize isn't reliable here
            const auto numSections = count();
            int usedWidth = 0;
            for (int i = 0; i < numSections; ++i)
                usedWidth += sectionSize(i);
            const auto diff = usedWidth - width();
            const auto numVisibleSections = numSections - hiddenSectionCount();
            if (numVisibleSections == 0)
                return;

            const auto diffPerSection = diff / numVisibleSections;
            const auto extraDiff = diff % numVisibleSections;
            for (int i = 1; i < numSections; ++i) {
                if (isSectionHidden(i)) {
                    continue;
                }
                auto newSize = sectionSize(i) - diffPerSection;
                if (i == numSections - 1)
                    newSize -= extraDiff;
                resizeSection(i, newSize);
            }
        }
    });

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QHeaderView::customContextMenuRequested, this, [this](const QPoint& pos) {
        const auto numSections = count();

        QMenu menu;
        auto resetSizes = menu.addAction(tr("Reset Column Sizes"));
        connect(resetSizes, &QAction::triggered, this, [this]() { resizeColumns(true); });

        if (numSections > 1) {
            auto* subMenu = menu.addMenu(tr("Visible Columns"));
            for (int i = 1; i < numSections; ++i) {
                auto* action = subMenu->addAction(model()->headerData(i, Qt::Horizontal).toString());
                action->setCheckable(true);
                action->setChecked(!isSectionHidden(i));
                connect(action, &QAction::toggled, this, [this, i](bool visible) { setSectionHidden(i, !visible); });
            }
        }

        menu.exec(mapToGlobal(pos));
    });
}

CostHeaderView::~CostHeaderView() = default;

void CostHeaderView::resizeEvent(QResizeEvent* event)
{
    QHeaderView::resizeEvent(event);
    resizeColumns(false);
}

void CostHeaderView::resizeColumns(bool reset)
{
    const auto numColumns = count();
    if (!numColumns) {
        return;
    }

    QScopedValueRollback<bool> guard(m_isResizing, true);
    auto availableWidth = width();
    const auto defaultSize = defaultSectionSize();

    for (int i = numColumns - 1; i >= 0; --i) {
        if (i == 0) {
            resizeSection(i, std::max(availableWidth, defaultSize));
        } else if (reset) {
            resizeSection(i, defaultSize);
        }
        if (!isSectionHidden(i)) {
            availableWidth -= sectionSize(i);
        }
    }
}
