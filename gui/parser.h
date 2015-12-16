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

#ifndef PARSER_H
#define PARSER_H

#include <QObject>

#include "treemodel.h"
#include "chartmodel.h"
#include "histogrammodel.h"

class Parser : public QObject
{
    Q_OBJECT
public:
    explicit Parser(QObject* parent = nullptr);
    virtual ~Parser();

public slots:
    void parse(const QString& path);

signals:
    void progressMessageAvailable(const QString& progress);
    void summaryAvailable(const QString& summary);
    void bottomUpDataAvailable(const TreeData& data);
    void topDownDataAvailable(const TreeData& data);
    void consumedChartDataAvailable(const ChartData& data);
    void allocationsChartDataAvailable(const ChartData& data);
    void allocatedChartDataAvailable(const ChartData& data);
    void temporaryChartDataAvailable(const ChartData& data);
    void sizeHistogramDataAvailable(const HistogramData& data);
    void finished();
};

#endif // PARSER_H
