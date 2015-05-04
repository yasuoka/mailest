mailest - search mail messages with Hyper Estrairer
===================================================

WIP

"mailest" is a "search" backend for [Mew](http://mew.org) using
[Hyper Estraier](fallabs.com/hyperestraier/index.html).  It can replace
the existing backend `mewest`.


Install
-------

On OpenBSD

    % ftp https://github.com/yasuoka/mailest/archive/mailest-0.9.3.tar.gz
    % make
    % sudo make install

On other BSD or Linux

    % wget https://github.com/yasuoka/mailest/archive/mailest-0.9.3.tar.gz
    % ./configure
    % gmake (or make)
    % sudo make install


Quick Usage
-----------

+ Add the below line to .mew.el to let Mew use "Hyper Estrairer" for
  search.

      ((setq mew-search-method 'est)

+ Add the below lines to ~/.emacs use use the "mailest" instead of
  existing "mewest".

      (defvar mew-prog-est        "mew-mailest")
      (defvar mew-prog-est-update "mew-mailest")

+ Use `km`, `kM`, `k/` or `k?` in Mew.  See
  http://mew.org/en/info/release/mew_6.html#dbsearch for search commands
  in Mew.


Documentations
--------------

Manpages:

- [mailestctl(1)](http://yasuoka.github.io/mailest/mailestctl.1.html)
- [mailestd(8)](http://yasuoka.github.io/mailest/mailestd.8.html)
- [mailestd.conf(5)](http://yasuoka.github.io/mailest/mailestd.conf.5.html)
- [mew-mailest(8)](http://yasuoka.github.io/mailest/mew-mailest.1.html)


Differences from `mewest`
-------------------------

Backup of the database

  "mailest" doesn't create a backup of the database before updating the
  database which `mewest` does.  Hyper estrairer databases break easyly
  if the program or the system is crashed during updating the database.


Todo
----

- write man pages


Copyright
---------

It's OpenBSD licence other than "libestdraft".  "libestdraft" is derived
from Hyper Estrairer's estcmd.c.  It's LGPL 2.1.  See estdraft.c to check
its entire copyright.
