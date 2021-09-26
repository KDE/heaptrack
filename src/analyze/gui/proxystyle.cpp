/*
    SPDX-FileCopyrightText: 2020 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "proxystyle.h"

#include <QWidget>

int ProxyStyle::styleHint(QStyle::StyleHint hint, const QStyleOption* option, const QWidget* widget,
                          QStyleHintReturn* returnData) const
{
    if (hint == QStyle::SH_RubberBand_Mask
        && widget->metaObject()->className() == QByteArrayLiteral("ChartRubberBand")) {
        return 0;
    }
    return QProxyStyle::styleHint(hint, option, widget, returnData);
}
