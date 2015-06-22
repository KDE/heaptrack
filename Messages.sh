#! /usr/bin/env bash
subdirs="gui"
rcfiles="`find $subdirs -name \*.rc`"
uifiles="`find $subdirs -name \*.ui`"
kcfgfiles="`find $subdirs -name \*.kcfg`"
if [[ "$rcfiles" != "" ]] ; then
    $EXTRACTRC $rcfiles >> rc.cpp || exit 11
fi
if [[ "$uifiles" != "" ]] ; then
    $EXTRACTRC $uifiles >> rc.cpp || exit 12
fi
if [[ "$kcfgfiles" != "" ]] ; then
    $EXTRACTRC $kcfgfiles >> rc.cpp || exit 13
fi
$XGETTEXT -kaliasLocal `find $subdirs -name \*.cc -o -name \*.cpp -o -name \*.h | grep -v '/tests/'` rc.cpp -o $podir/heaptrack.pot
rm -f rc.cpp
