#!/bin/sh
#
# Copyright (C) Shirish A Kalele 2000
#
# Builds a Samba package from the samba distribution. 
# By default, the package will be built to install samba in /usr/local
# Change the INSTALL_BASE variable to change this: will modify the pkginfo 
# and samba.server files to point to the new INSTALL_BASE
#
INSTALL_BASE=/usr/local

add_dynamic_entries() 
{
  # First build the codepages and append codepage entries to prototype
  echo "#\n# Codepages \n#"
  echo d none samba/lib/codepages 0755 root other

  CODEPAGELIST="437 737 850 852 861 932 866 949 950 936"
  # Check if make_smbcodepage exists
  if [ ! -f $DISTR_BASE/source/bin/make_smbcodepage ]; then
    echo "Could not find $DISTR_BASE/source/bin/make_smbcodepage to generate codepages.\n\
     Please create the binaries before packaging." >&2
    exit 1
  fi

  for p in $CODEPAGELIST; do
    $DISTR_BASE/source/bin/make_smbcodepage c $p $DISTR_BASE/source/codepages/codepage_def.$p $DISTR_BASE/source/codepages/codepage.$p
    echo f none samba/lib/codepages/codepage.$p=source/codepages/codepage.$p 0644 root other
  done

  # Add the binaries, docs and SWAT files

  echo "#\n# Binaries \n#"
  cd $DISTR_BASE/source/bin
  for binfile in *
  do
    if [ -f $binfile ]; then
      echo f none samba/bin/$binfile=source/bin/$binfile 0755 root other
    fi
  done
  echo "#\n# HTML documentation \n#"
  echo d none samba/docs/htmldocs 0755 root other
  cd $DISTR_BASE/docs/htmldocs
  for htmldoc in *
  do
    if [ -f $htmldoc ]; then
      echo f none samba/docs/htmldocs/$htmldoc=docs/htmldocs/$htmldoc 0644 root other
    fi
  done
  echo "#\n# Text Docs \n#"
  echo d none samba/docs/textdocs 0755 root other
  cd $DISTR_BASE/docs/textdocs
  for textdoc in *
  do 
    if [ -f $textdoc ]; then
      echo f none samba/docs/textdocs/$textdoc=docs/textdocs/$textdoc 0644 root other
    fi
  done
  echo "#\n# SWAT \n#"
  cd $DISTR_BASE
  list=`find swat -type d`
  for i in $list
  do
    echo "d none samba/$i 0755 root other"
  done
  list=`find swat -type f`
  for i in $list
  do
    echo "f none samba/$i=$i 0644 root other"
  done
  echo "#\n# HTML documentation for SWAT\n#"
  cd $DISTR_BASE/docs/htmldocs
  for htmldoc in *
  do
    if [ -f $htmldoc ]; then
      echo f none samba/swat/help/$htmldoc=docs/htmldocs/$htmldoc 0644 root other
    fi
  done

}

if [ $# = 0 ]
then
	# Try to guess the distribution base..
	CURR_DIR=`pwd`
	DISTR_BASE=`echo $CURR_DIR | sed 's|\(.*\)/packaging.*|\1|'`
	echo "Assuming Samba distribution is rooted at $DISTR_BASE.."
else
	DISTR_BASE=$1
fi

#
if [ ! -d $DISTR_BASE ]; then
	echo "Source build directory $DISTR_BASE does not exist."
	exit 1
fi

# Set up the prototype file from prototype.master
if [ -f prototype ]; then
	rm prototype
fi

# Setup version from version.h
VERSION=`sed 's/#define VERSION \"\(.*\)\"$/\1/' ../../source/include/version.h`
sed -e "s|__VERSION__|$VERSION|" -e "s|__ARCH__|`uname -p`|" -e "s|__BASEDIR__|$INSTALL_BASE|g" pkginfo.master >pkginfo

sed -e "s|__BASEDIR__|$INSTALL_BASE|g" inetd.conf.master >inetd.conf
sed -e "s|__BASEDIR__|$INSTALL_BASE|g" samba.server.master >samba.server

cp prototype.master prototype

# Add the dynamic part to the prototype file
(add_dynamic_entries >> prototype)

# Create the package
pkgmk -o -d /tmp -b $DISTR_BASE -f prototype
if [ $? = 0 ]
then
	pkgtrans /tmp samba.pkg samba
fi
echo The samba package is in /tmp
rm -f prototype
