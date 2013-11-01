The enclosed quality hash implementations are not my own, and
my thanks to their authors for all the work they did to implement them.

* CityHash comes from Google (https://code.google.com/p/cityhash/) under
a BSD licence. See its documentation.

* 4-SHA256 is derived, with many improvements including an ARM NEON
implementation by me, from https://github.com/wereHamster/sha256-sse by
Tomas Carnecky. Its licence (confirmed with Tomas) for all files is the
Common Development and Distribution License (CDDL) from OpenSolaris
(sha256-ref.c is directly from there).

* SpookyHash comes from http://burtleburtle.net/bob/hash/spooky.html by
Bob Jenkins. It is public domain.
