#!/bin/sh

git checkout dev
find -name "*~"  | xargs rm
tmp_dir=`mktemp -d "/tmp/FlashGraphR.XXXXXXX"`
cp -R * $tmp_dir

git checkout master
rm -R *
mv $tmp_dir/* .
