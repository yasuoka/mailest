mailest - search mail messages with Hyper Estrairer
===================================================

"mailest" provides a search backend and id index database for
[Mew](http://www.mew.org/) using
[Hyper Estraier](http://fallabs.com/hyperestraier/index.html).

- `V`, `k/` or other related commands for newly received/refiled mails
  become avaiable very soon.

- You don't need to start indexing explicitly.
  - "mailest" daemon (mailestd) does it automatically.  It monitors
    changes on the mail folders by kqueue or inotify.


Install
-------

Prerequirement:

- "hyperestraier" and "qdbm" installed
- "libevent" and "libiconv" are required


OpenBSD:

    % ftp https://github.com/yasuoka/mailest/archive/mailest-0.9.20.tar.gz
    % tar xzf mailest-0.9.20.tar.gz
    % cd mailest-mailest-0.9.20
    % make
    % sudo make install

Other:

    % wget https://github.com/yasuoka/mailest/archive/mailest-0.9.20.tar.gz
    % tar xzf mailest-0.9.20.tar.gz
    % cd mailest-mailest-0.9.20
    % ./configure
    % make
    % sudo make install

 On BSD, You may need to use "gmake" instead of "make" to use GNUmakefile


Version up
----------

Terminate the running mailest daemons before using the new version:

  - The mailest communication protocol between its server and its
    clients may be changed between the versions and the compatibility
    between the versions is not taken care of.
  - Use `mailestctl stop` or send a terminal signal (SIGTERM) to stop
    the mailest programs.


How to start
------------

+ For existing `mewest` user, backup and/or delete the database
  (`~/Mail/casket`)

  - Since "mailestd" can work with the old database, so OK to skip deleting
    `~/Mail/casket`, but remark that `V` will not work against the mails
    which are indexed by `mewest`.   To make `V` work against them, delete
    the database in advance.

+ Add the below line to `.mew.el` to let Mew use "Hyper Estrairer" for
  search.

      ((setq mew-search-method 'est)

+ Add the below lines to `~/.emacs` use use the "mailest" instead of
  existing "mewest".

      (defvar mew-prog-est        "mew-mailest")
      (defvar mew-prog-est-update "mew-mailest")
      (defvar mew-prog-cmew       "cmew-mailest")
      (defvar mew-prog-smew       "smew-mailest")
      (defvar mew-id-db-file      ".mailest.sock")

+ Try `km`, `kM`, `k/`, `k?`, `V` in Mew.  See
  http://mew.org/en/info/release/mew_6.html#dbsearch for search commands
  in Mew.


Usage
-----

- "mailest" daemon will start by the following operations on Mew
  - Search or virutal folder operations (`V`, `k/` and so on)
  - Making index operations (`km` or `kM`)
- After the daemon starts, it  monitors the changes on the folders
  which has index already.
- After `kM`, "mailest" will start monitoring for newly created folders
  as well.  Otherwise `km` is required for monitoring new folders.
- Other
  - to stop the daemon: `mailestctl stop`
  - to watch the behaviour of the daemon: `tail -f ~/Mail/mailestd.log`
  - to suspend/resume indexing `mailestctl suspend` or `mailestctl resume`
  - if you want to stop monitoring the folders, add `monitor disable`
    to `~/Mail/mailestd.conf`.

See [man pages](#man-pages) also.


ChangeLog
---------

0.9.21 (not yet)

  - Fix the way of referencing to libestdir.  Suggested by Hiroki Sato.
  - Fix BSD make.
  - Fix BSD make build on NetBSD.
  - Fix a bug in mailestd.conf(5).


0.9.20

  - Use the realpath always for folders not to treat the path with
    symbolic links and the realpath as different folders.  (Found
    by Yoshiaki Kasahara)
  - Improve log and comment a bit.


0.9.19

  - Fix some variables in BSD make not to have DESTDIR doublely.
  - Remove a debug log output in #ifdef MONITOR_INOTIFY.
  - Fix typos in log message.


0.9.18

  - Fix off-by-one in mailestctl.  It crashed by using "csearch"
    (mew-mailest).  (Found and debugged by Yoshiaki Kasahara)
  - Fix ./configure to select kqueue or inotify properly.
  - s/cascket/casket/ (Pointed out by Yoshiaki Kasahara)


0.9.17

  - Fix "update" related logs to show the entire path name.
    (diff from Hiroki Sato)


0.9.16

  - Fix "smew -p" to work.


0.9.15

  - Monitoring were not enabled on some systems (atleast on FreeBSD).
    Fixed autoconf to choice kqueue() or inotify() properly.  (Pointed
    out by Yoshiaki Kasahara)


0.9.14

  - Support "smew -p" and "smew -c" so that Mew '^' and '&' work.
    (pointed out by Yoshiaki Kasahara)
  - Fix and refine build on FreeBSD (diff from Hiroki Saito)
  - Fix build on the systems which need -liconv for iconv_open().
    (diff from Yoshiaki Kasahara)
  - Internal size for message-id may be too short.  Use 255 for it.
  - When "guess-parid" failed to find the parent message, add dummy
    x-mew-parid not to try to find the parent message ever.
  - Fixed: "guess-parid" is too slow since mailestd failed to create the
    index of "@title" on startup.
  - Fixed to create the index for "@title" properly.


0.9.13

  - Add "guess-parid" configration option to find the parent message
    for the messages which don't contain "In-Reply-To" or "Reference"
    header by guessing with "Subject" and "Date".  Add the following
    to `~/Mail/mailestd.conf`.

        guess-parid

0.9.12

  - Improve logging
  - Tweak log levels
  - Fix parsing "monitor" in mailestd.conf

0.9.11

  - Fix not to change current working directory.
  - Fix: invalid memory access in mailestd_db_smew().

0.9.10

  - Fix build on operating system which use kqueue for monitor.

0.9.9

  - Monitor the inodes the directories (by kevent and inotify) and start
    update automatically.
  - Fixed error in replace.h which is to redefine RB_FOREACH_SAFE.
  - Optimize database when many documents are put or deleted.  As the
    Hyper Estrairer's documet is recomended.
  - Unlimit the resource limit of the data size when process startup.

0.9.8

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

  - Fix: When the database doesn't exists, the database thread stopps
    forever.
  - Fix: When the database is error, the first update causes a lot
    of errors.

0.9.6

  -  Fix: cannot search non ASCII/UTF-8 mails
  -  Fix: mailestd exits abnormally sometimes when it is stopped during
     syncing DB.


0.9.5

  -  First "update" to huge database taked long time.  Fix it not to wait
     database cache updating before update starts.
  -  Fix the results of "update" to be passed correctly.
  -  Support build on FreeBSD and NetBSD

Man pages
---------

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

- When indexing huge amount of mails, smew takes very long time.  Find
  a way to workaround this.
- Delete the index phsically
- Automatically create a backup for the database when closing the
  writable DB connection.  Also recover the database automatically
  when it's broken.


Copyright
---------

Licenses other than "libestdraft" are BSD or OpenBSD license.  Only
"libestdraft" is derived from Hyper Estrairer's estcmd.c, it's LGPL.
See the source code to check the entire copyright.
