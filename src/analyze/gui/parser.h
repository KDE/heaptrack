/*
 * Copyright 2015-2020 Milian Wolff <mail@milianw.de>
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

#ifndef PARSER_H
#define PARSER_H

#include <QObject>

#include "../filterparameters.h"
#include "callercalleemodel.h"
#include "chartmodel.h"
#include "histogrammodel.h"
#include "treemodel.h"

#include <memory>

struct ParserData;

class Parser : public QObject
{
    Q_OBJECT
public:
    explicit Parser(QObject* parent = nullptr);
    virtual ~Parser();

    bool isFiltered() const;

public slots:
    void parse(const QString& path, const QString& diffBase);
    void reparse(const FilterParameters& filterParameters);

signals:
    void progressMessageAvailable(const QString& progress);
    void progress(const int progress);
    void summaryAvailable(const SummaryData& summary);
    void bottomUpDataAvailable(const TreeData& data);
    void topDownDataAvailable(const TreeData& data);
    void callerCalleeDataAvailable(const CallerCalleeResults& data);
    void consumedChartDataAvailable(const ChartData& data);
    void allocationsChartDataAvailable(const ChartData& data);
    void temporaryChartDataAvailable(const ChartData& data);
    void sizeHistogramDataAvailable(const HistogramData& data);
    void finished();
    void failedToOpen(const QString& path);

private:
    void parseImpl(const QString& path, const QString& diffBase, const FilterParameters& filterParameters);

    QString m_path;
    std::shared_ptr<ParserData> m_data;
};

#endif // PARSER_H
