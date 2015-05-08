mailest - search mail messages with Hyper Estrairer
===================================================

"mailest" is a "search" backend for [Mew](http://mew.org) using
[Hyper Estraier](fallabs.com/hyperestraier/index.html).  It can replace
the existing backend `mewest`.


Install
-------

Prerequirement:

- "hyperestraier" and "qdbm" installed
- "libevent" and "libiconv" are required


OpenBSD:

    % ftp https://github.com/yasuoka/mailest/archive/mailest-0.9.7.tar.gz
    % tar xzf mailest-0.9.7.tar.gz
    % cd mailest-mailest-0.9.7
    % make
    % sudo make install

Other:

    % wget https://github.com/yasuoka/mailest/archive/mailest-0.9.7.tar.gz
    % tar xzf mailest-0.9.7.tar.gz
    % cd mailest-mailest-0.9.7
    % ./configure
    % make
    % sudo make install

 On BSD, You may need to use "gmake" instead of "make" to use GNUmakefile


Quick Usage
-----------

+ Add the below line to .mew.el to let Mew use "Hyper Estrairer" for
  search.

      ((setq mew-search-method 'est)

+ Add the below lines to ~/.emacs use use the "mailest" instead of
  existing "mewest".

      (defvar mew-prog-est        "mew-mailest")
      (defvar mew-prog-est-update "mew-mailest")
      (defvar mew-prog-cmew       "cmew-mailest")
      (defvar mew-prog-smew       "smew-mailest")
      (defvar mew-id-db-file      ".mailest.sock")

+ Use `km`, `kM`, `k/` or `k?` in Mew.  See
  http://mew.org/en/info/release/mew_6.html#dbsearch for search commands
  in Mew.


ChangeLog
---------

0.9.9

- 2015-05-08: yasuoka
  - Optimize database when many documents are put or deleted.  As the
    Hyper Estrairer's documet is recomended.


0.9.8

- 2015-05-08: yasuoka
  - Add 'smew' functionality.  Add `smew` command to mailestctl(1).
    To use 'smew' of 'mailestd', add below lines:

        (defvar mew-prog-cmew       "cmew-mailest")
        (defvar mew-prog-smew       "smew-mailest")
        (defvar mew-id-db-file      ".mailest.sock")

    and delete the existing database and create the database again.
  - Made the 'message-id' attribute indexed.
  -  Create `x-mew-parid` indexed attribute for each message.
  - Add `message-id` command to mailestdctl(1).

0.9.7

-  2015-05-06: yasuoka
  - Fix: When the database doesn't exists, the database thread stopps
    forever.
  - Fix: When the database is error, the first update causes a lot
    of errors.

0.9.6

-  2015-05-06: yasuoka
  -  Fix: cannot search non ASCII/UTF-8 mails
  -  Fix: mailestd exits abnormally sometimes when it is stopped during
     syncing DB.


0.9.5

-  2015-05-06: yasuoka
  -  First "update" to huge database taked long time.  Fix it not to wait
     database cache updating before update starts.
  -  Fix the results of "update" to be passed correctly.
  -  Support build on FreeBSD and NetBSD

Man page
--------

- [mailestctl(1)](http://yasuoka.github.io/mailest/mailestctl.1.html)
- [mailestd(8)](http://yasuoka.github.io/mailest/mailestd.8.html)
- [mailestd.conf(5)](http://yasuoka.github.io/mailest/mailestd.conf.5.html)
- [mew-mailest(8)](http://yasuoka.github.io/mailest/mew-mailest.1.html)


Differences from `mewest`
-------------------------

Backup of the database

  "mailest" doesn't create a backup of the database before updating the
  database which `mewest` does.  Hyper Estrairer databases break easyly
  if the program or the system crashes during updating the database.


TODO
----

- Monitor the inodes the directories (by kevent and inotify) and start
  update automatically.
- Delete the index phsically
- Automatically create a backup for the database when closing the
  writable DB connection.  Also recover the database automatically
  when it's broken.
- Greate a tool which add x-mew-parid attribute for mails which doesn't
  have any In-Reply-To or Reference header by guessing with Subject and
  Date.


Copyright
---------

Licenses other than "libestdraft" are BSD or OpenBSD license.  Only
"libestdraft" is derived from Hyper Estrairer's estcmd.c, it's LGPL.
See the source code to check the entire copyright.
