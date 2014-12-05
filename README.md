x2x
===

x2x allows the keyboard, mouse on one X display to be used to control another X
display.

Don't expect stability with this fork at the moment. With time it will come but
for now it will be very unstable with many bugs. Tell me if you find any I will
probably know but may now.

License
=======

x2x is under MIT/BSD license.

Changes Made So Far
===================

Removed all support for MS Windows as it (in my eyes) overcomplicates and 
(again biast view) is not necessary. Plus I have no machine to test it on.

Simplified make. Probably over simplified.

Changed how mouse movements are dealt with, I have changed it to move the mouse
to a certain position after each movement so that when dealing with mulitiple 
monitors on from's side the mouse movements are at the same speed on to. This 
also fixed problems with not being able to move the mouse to the bottom of to 
due to it hitting the bottom of a monitor on from.

Further changed the way of dealing with events. Now a new window is created 
in the center of the display called the 'pad' that captures all events and 
keeps the mouse centered in it. 

Changed the trigger window to InputOutput so it could get VisibilityChange 
events. There were already functions for listening to this but Xorg states that
InputOnly windows do not recieve the events. So not sure what the hell is going
on here. Sadly it means that one pixel down the side of the screen is used but
means that the window is always on top. Not sure what to do with this. If you 
have a better idea please inform me.

Authors
=======

x2x was initially developed in DEC by David Chaiken.
Previous maintainer is Mikhail Gusarov.
This fork is maintained by Mytchel Hammond

