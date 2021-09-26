/*
    SPDX-FileCopyrightText: 2014-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

void foo()
{
    new char[1];
}

void bar()
{
    foo();
    new char[2];
}

void asdf()
{
    bar();
    new char[3];
}

void foobar()
{
    asdf();
    new char[5];
}

int main()
{
    asdf();
    new char[4];
    foobar();
    return 0;
}
