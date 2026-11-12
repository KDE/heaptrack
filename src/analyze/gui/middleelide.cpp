#include "middleelide.h"
#include <QChar>

MiddleElide::MiddleElide()
{

}

QString MiddleElide::elideAngleBracket(const QString& s)
{
   return substituteAngleBrackets(s);
}

QString MiddleElide::substituteAngleBrackets(const QString& s)
{
    static QChar startBracket = QChar(u'<');
    static QChar stopBracket  = QChar(u'>');

    int level = 0;
    QString result;
    for (int i=0, n = s.length(); i < n; i++)  {
        QChar currentChar = s[i];
        if (currentChar == QChar(startBracket) && level == 0) {
            result += QChar(startBracket);
            level++;
        }
        else if (currentChar == QChar(startBracket) && level > 0) {
            level++;
        }
        else if (currentChar == QChar(stopBracket) && level == 1) {
            result += QString::fromUtf8("...>");
            level--;
        }
        else if (currentChar == QChar(stopBracket) && level > 1) {
            level--;
        }
        else if (level == 0) {
            result += currentChar;
        }
    }
    return result;
}
