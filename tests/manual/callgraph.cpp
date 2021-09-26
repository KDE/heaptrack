/*
    SPDX-FileCopyrightText: 2014-2017 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

void asdf()
{
    new char[1];
}

void foo()
{
    asdf();
    new char[12];
}

void bar()
{
    asdf();
    foo();
    new char[123];
}

void recurse(int i)
{
    new char[2];
    bar();
    if (i) {
        recurse(--i);
    }
}

int main()
{
    asdf();
    foo();
    bar();
    recurse(5);
    return 0;
}
