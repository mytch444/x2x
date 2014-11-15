x2x
===

x2x allows the keyboard, mouse on one X display to be used to control another X
display.

License
=======

x2x is under MIT/BSD license.

Call for help
=============

x2x has a long history. It was born in the times when life was hard, window
managers were simple and displays were rectangle. Several implicit assumptions
survived during the x2x lifetime, and now need reconsideration. As x2x is simple
(however, it has accumulated inner knowledge of X11 implementations), we are
looking for brave person who can read and understand whole x2x and will write
new x2x, adapted to the modern life of dynamically attached and detached
displays, complex data transferring through the X selections and Xinerama.

Don't Worry Fair Maiden!
========================

I am trying to answer your call for help. However due to my incompetence I am
basically going to purge all that I do not need and all that I do not understand.

So far I have cleaned up a bit, removed a lot (notibly window's support and
clipboard), and am now adding/fixing stuff. 

Changes Made So Far
===================

Simplified make. Probably over simplified.

Changed how it deals with mouse movements, I have changed it to move the mouse
to a certain position after each movement so that when dealing with mulitiple 
monitors on from's side the mouse movements are at the same speed on to. This 
also fixed problems with not being able to move the mouse to the bottom of to 
due to it hitting the bottom of a monitor on from.

Authors
=======

x2x was initially developed in DEC by David Chaiken.
Previous maintainer is Mikhail Gusarov.
This fork is maintained by Mytchel Hammond

