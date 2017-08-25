#! /bin/bash

cd ..
git commit -am "$1"
git push origin master 
cd /usr/include
ctags -R
cd /usr/include/redis/src
