/*
 * Copyright 2019 David Faure <david.faure@kdab.com>
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

#include "parser.h"

#include <QCoreApplication>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.count() <= 1) // first arg is the app name
        return 1;

    qRegisterMetaType<CallerCalleeResults>();

    Parser parser;
    QObject::connect(&parser, &Parser::finished,
                     &app, &QCoreApplication::quit);

    parser.parse(args.at(1), QString());

    return app.exec();
}
