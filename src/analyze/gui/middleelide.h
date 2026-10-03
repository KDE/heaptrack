#ifndef MIDDLEELIDE_H
#define MIDDLEELIDE_H

#include <QString>

class MiddleElide
{
public:
    MiddleElide();

    static QString elideAngleBracket(const QString& s);

private:
    static QString substituteAngleBrackets(const QString& s);
};

#endif // MIDDLEELIDE_H
