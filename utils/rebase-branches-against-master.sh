#!/bin/bash

git checkout master
for n in $(git branch | grep -v "* master"); do echo $n; git checkout $n && git rebase master; done

# git co <br> && git reset --soft <commit id> && rm <conflict> && git a && git ci -m "add candidate" && git rebase master
