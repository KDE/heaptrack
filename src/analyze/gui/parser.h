/*
    SPDX-FileCopyrightText: 2015-2020 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
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

    enum class StopAfter
    {
        Summary,
        BottomUp,
        SizeHistogram,
        TopDownAndCallerCallee,
        Finished,
    };

    void parse(const QString& path, const QString& diffBase, const FilterParameters& filterParameters,
               StopAfter stopAfter = StopAfter::Finished);
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
    void parseImpl(const QString& path, const QString& diffBase, const FilterParameters& filterParameters,
                   StopAfter stopAfter);

    QString m_path;
    std::shared_ptr<ParserData> m_data;
};

#endif // PARSER_H
