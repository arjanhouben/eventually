#!/bin/bash

if [ "$(id -u)" == "0" ]; then
	echo "This script should not be run as root" 1>&2
	exit 1
fi

while [ true ]
do
	read -p "Would you like to install eventually just for $(whoami)? ( Y/n ) " choice
	choice=${choice:-y}
	case $choice in
		y)
			rootinstall=false
			break;
			;;
		n)
			rootinstall=true
			break;
			;;
		*)
			echo "please choose y/n"
	esac
done

echo "FKEF " $rootinstall

if [ $rootinstall == true ]
then
	instdir=/usr/local
	instcommand="sudo make install"
else
	instdir=~/.eventually
	mkdir -p $instdir
	grep -Fxq "export PATH=\$PATH:$instdir/bin" ~/.profile || echo "export PATH=\$PATH:$instdir/bin" >> ~/.profile
	. ~/.profile
	instcommand="make install"
fi

tmpdir=$(mktemp -d -t eventually)
srcdir=$(pwd)
cd $tmpdir
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${instdir} ${srcdir}
make
$instcommand
echo profit!
echo
echo eventually -h
eventually -h
