/*
 * x2x: Uses the XTEST extension to forward mouse movements and
 * keystrokes from a window on one display to another display.  Useful
 * for desks with multiple keyboards.
 *
 * Copyright (c) 1997
 * Digital Equipment Corporation.  All rights reserved.
 *
 * By downloading, installing, using, modifying or distributing this
 * software, you agree to the following:
 *
 * 1. CONDITIONS. Subject to the following conditions, you may download,
 * install, use, modify and distribute this software in source and binary
 * forms:
 *
 * a) Any source code, binary code and associated documentation
 * (including the online manual) used, modified or distributed must
 * reproduce and retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *
 * b) No right is granted to use any trade name, trademark or logo of
 * Digital Equipment Corporation.  Neither the "Digital Equipment
 * Corporation" name nor any trademark or logo of Digital Equipment
 * Corporation may be used to endorse or promote products derived from
 * this software without the prior written permission of Digital
 * Equipment Corporation.
 *
 * 2.  DISCLAIMER.  THIS SOFTWARE IS PROVIDED BY DIGITAL "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.IN NO EVENT SHALL DIGITAL BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Modified on 3 Oct 1998 by Charles Briscoe-Smith:
 *   added options -north and -south
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/Xatom.h> /* for selection */
#include <X11/Xos.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <X11/extensions/dpms.h>

#define DEBUG

#ifndef MIN
#define MIN(x,y) (((x) < (y)) ? (x) : (y))
#endif
#ifndef MAX
#define MAX(x,y) (((x) > (y)) ? (x) : (y))
#endif

#define UTF8_STRING "UTF8_STRING"

/**********
 * definitions for edge
 **********/
#define EDGE_NONE   0 /* don't transfer between edges of screens */
#define EDGE_NORTH  1 /* from display is on the north side of to display */
#define EDGE_SOUTH  2 /* from display is on the south side of to display */
#define EDGE_EAST   3 /* from display is on the east side of to display */
#define EDGE_WEST   4 /* from display is on the west side of to display */

#define X2X_DISCONNECTED    0
#define X2X_AWAIT_RELEASE   1
#define X2X_CONNECTED       2
#define X2X_CONN_RELEASE    3

/**********
 * functions
 **********/
static void    Usage();
static void    ParseCommandLine();
static Display *OpenAndCheckDisplay();
static Bool    CheckTestExtension();
static void    DoX2X();
static void    InitDpyInfo();
static void    DoConnect();
static void    DoDisconnect();
static void    RegisterEventHandlers();

static Bool    ProcessEvent();
static Bool    ProcessMotionNotify();
static Bool    ProcessExpose();
static Bool    ProcessEnterNotify();
static Bool    ProcessButtonPress();
static Bool    ProcessButtonRelease();
static Bool    ProcessKeyEvent();

static Bool    ProcessConfigureNotify();
static Bool    ProcessClientMessage();
static Bool    ProcessSelectionRequest();
static void    SendPing();
static Bool    ProcessPropertyNotify();
static Bool    ProcessSelectionNotify();
static Bool    ProcessSelectionClear();
static Bool    ProcessVisibility();
static Bool    ProcessMapping();
static void    RefreshPointerMapping();

/**********
 * stuff for selection forwarding
 **********/
typedef struct _dpyxtra {
    Display *otherDpy;
    int  sState;
    Atom pingAtom;
    Bool pingInProg;
    Window propWin;
} DPYXTRA, *PDPYXTRA;

#define N_BUTTONS   20

#define MAX_BUTTONMAPEVENTS 20

#define GETDPYXTRA(DPY,PDPYINFO)\
    (((DPY) == (PDPYINFO)->fromDpy) ?\
     &((PDPYINFO)->fromDpyXtra) : &((PDPYINFO)->toDpyXtra))

/* values for sState */
#define SELSTATE_ON     0
#define SELSTATE_OFF    1
#define SELSTATE_WAIT   2

/* special values for translated coordinates */
#define COORD_INCR     -1
#define COORD_DECR     -2
#define SPECIAL_COORD(COORD) (((COORD) < 0) ? (COORD) : 0)

/* max unreasonable coordinates before accepting it */
#define MAX_UNREASONABLES 10

/**********
 * display information
 **********/
typedef struct {
    /* stuff on "from" display */
    Display *fromDpy;
    Atom    fromDpyUtf8String;
    Atom    fromDpyTargets;
    Window  root;
    Window  trigger;
    Atom    wmpAtom, wmdwAtom;
    Cursor  grabCursor;
    int     width, height;
    Bool    vertical;
    int     lastFromCoord;
    int     unreasonableDelta;

    /* stuff on "to" display */
    Display *toDpy;
    Atom    toDpyUtf8String;
    Atom    toDpyTargets;
    Window  selWin;
    unsigned int inverseMap[N_BUTTONS + 1]; /* inverse of button mapping */

    /* state of connection */
    int     mode;                        /* connection */
    int     eventMask;                /* trigger */

    /* coordinate conversion stuff */
    int     toScreen;
    int     nScreens;
    /*
    short   **xTables; // precalculated conversion tables 
    short   **yTables;
    */
    int     fromConnCoord; // location of cursor after conn/disc ops 
    int     toDiscCoord;
//    int     fromIncrCoord; // location of cursor after incr/decr ops 
//    int     fromDecrCoord;
    
    int x, y;
    int toWidth, toHeight;
    int xcenter, ycenter;

    /* selection forwarding info */
    DPYXTRA fromDpyXtra;
    DPYXTRA toDpyXtra;
    Display *sDpy;
    XSelectionRequestEvent sEv;
    Time    sTime;

} DPYINFO, *PDPYINFO;

/* shadow displays */
typedef struct _shadow {
    struct _shadow *pNext;
    char    *name;
    Display *dpy;
} SHADOW, *PSHADOW;

typedef int  (*HANDLER)(); /* event handler function */

/**********
 * top-level variables
 **********/
static char    *programStr = "x2x";
static char    *fromDpyName = NULL;
static char    *toDpyName   = NULL;
static Bool    waitDpy      = False;
static Bool    doMouse      = True;
static int     doEdge       = EDGE_NONE;
static Bool    doAutoUp     = True;
static Bool    doResurface  = True;
static PSHADOW shadows      = NULL;
static int     triggerw     = 2;
static Bool    doPointerMap = True;
static Bool    doCapsLkHack = False;
static Bool    doDpmsMouse  = True;
static int     logicalOffset= 0;
static int     nButtons     = 0;
static KeySym  buttonmap[N_BUTTONS + 1][MAX_BUTTONMAPEVENTS + 1];

#if debug
#define debug printf
#else
void debug(const char* fmt, ...)
{
}
#endif

/**********
 * main
 **********/
int main(argc, argv)
    int  argc;
    char **argv;
{
    printf("in main\n");

    Display *fromDpy;
    PSHADOW pShadow;

    XrmInitialize();
    ParseCommandLine(argc, argv);

    fromDpyName = XDisplayName(fromDpyName);

    toDpyName   = XDisplayName(toDpyName);
    if (!strcasecmp(toDpyName, fromDpyName)) {
        fprintf(stderr, "%s: display names are both %s\n", 
                programStr, toDpyName);
        exit(1);
    }

    /* no OS independent way to stop Xlib from complaining via stderr,
       but can always pipe stdout/stderr to /dev/null */
    /* convert to real name: */
    /* ... qualifies this while in WIN_2_X case with an X source */
    while ((fromDpy = XOpenDisplay(fromDpyName)) == NULL) {
        printf("why?\n");
        if (!waitDpy) {
            fprintf(stderr, "%s - error: can not open display %s\n",
                    programStr, fromDpyName);
            exit(2);
        } /* END if */
        sleep(10);
    } /* END while fromDpy */

    printf("shadow?\n");
    /* toDpy is always the first shadow */
    pShadow = (PSHADOW)malloc(sizeof(SHADOW));
    pShadow->name = toDpyName;
    /* link into the global list */
    pShadow->pNext = shadows;
    shadows = pShadow;

    /* initialize all of the shadows, including the toDpy */
    for (pShadow = shadows; pShadow; pShadow = pShadow->pNext)
        if (!(pShadow->dpy = OpenAndCheckDisplay(pShadow->name)))
            exit(3);

    /* run the x2x loop */
    DoX2X(fromDpy, shadows->dpy);

    /* shut down gracefully */

    XCloseDisplay(fromDpy);

    for (pShadow = shadows; pShadow; pShadow = pShadow->pNext)
        XCloseDisplay(pShadow->dpy);
    exit(0);

} /* END main */

static Display *OpenAndCheckDisplay(name)
    char *name;
{
    Display *openDpy;

    /* convert to real name: */
    name = XDisplayName(name);
    while ((openDpy = XOpenDisplay(name)) == NULL) {
        if (!waitDpy) {
            fprintf(stderr, "%s - error: can not open display %s\n",
                    programStr, name);
            return NULL;
        } /* END if */
        sleep(10);
    } /* END while openDpy */

    if (!CheckTestExtension(openDpy)) {
        fprintf(stderr,
                "%s - error: display %s does not support the test extension\n",
                programStr, name);
        return NULL;
    }
    return (openDpy);

} /* END OpenAndCheckDisplay */

/**********
 * use standard X functions to parse the command line
 **********/
static void ParseCommandLine(argc, argv)
    int  argc;
    char **argv;
{
    printf("Parsing command line\n");

    int     arg;
    PSHADOW pShadow;
    //  extern  char *lawyerese;
    KeySym  keysym;
    int     button;
    int     eventno;
    char    *keyname, *argptr;

    debug("programStr = %s\n", programStr);

    /* Clear button map */
    for (button = 0; button <= N_BUTTONS; button++)
        buttonmap[button][0] = NoSymbol;

    for (arg = 1; arg < argc; ++arg) {
        if (!strcasecmp(argv[arg], "-from")) {
            if (++arg >= argc) Usage();
            fromDpyName = argv[arg];

            debug("fromDpyName = %s\n", fromDpyName);
        } else if (!strcasecmp(argv[arg], "-to")) {
            if (++arg >= argc) Usage();
            toDpyName = argv[arg];

            debug("toDpyName = %s\n", toDpyName);
        } else if (!strcasecmp(argv[arg], "-wait")) {
            waitDpy = True;

            debug("will wait for displays\n");
        } else if (!strcasecmp(argv[arg], "-nomouse")) {
            doMouse = False;

            debug("will not capture mouse (eek!)\n");
        } else if (!strcasecmp(argv[arg], "-nopointermap")) {
            doPointerMap = False;

            debug("will not do pointer mapping\n");
        } else if (!strcasecmp(argv[arg], "-north")) {
            doEdge = EDGE_NORTH;

            debug("\"from\" is on the north side of \"to\"\n");
        } else if (!strcasecmp(argv[arg], "-south")) {
            doEdge = EDGE_SOUTH;

            debug("\"from\" is on the south side of \"to\"\n");
        } else if (!strcasecmp(argv[arg], "-east")) {
            doEdge = EDGE_EAST;

            debug("\"from\" is on the east side of \"to\"\n");
        } else if (!strcasecmp(argv[arg], "-west")) {
            doEdge = EDGE_WEST;

            debug("\"from\" is on the west side of \"to\"\n");
        } else if (!strcasecmp(argv[arg], "-noautoup")) {
            doAutoUp = False;

            debug("will not automatically lift keys and buttons\n");
       } else if (!strcasecmp(argv[arg], "-capslockhack")) {
            doCapsLkHack = True;

            debug("behavior of CapsLock will be hacked\n");
        } else if (!strcasecmp(argv[arg], "-nodpmsmouse")) {
            doDpmsMouse = False;

            debug("mouse movement wakes monitor\n");
        } else if (!strcasecmp(argv[arg], "-offset")) {
            if (++arg >= argc) Usage();
            logicalOffset = atoi(argv[arg]);

            debug("logicalOffset %d\n", logicalOffset);
        } else if (!strcasecmp(argv[arg], "-nocapslockhack")) {
            doCapsLkHack = False;

            debug("behavior of CapsLock will not be hacked\n");
       } else if (!strcasecmp(argv[arg], "-buttonmap")) {
            if (++arg >= argc) Usage();
            button = atoi(argv[arg]);

            if ((button < 1) || (button > N_BUTTONS))
                printf("x2x: warning: invalid button %d\n", button);
            else if (++arg >= argc)
                Usage();
            else
            {
                debug("will map button %d to keysyms '%s'\n", 
                        button, argv[arg]);

                argptr  = argv[arg];
                eventno = 0;
                while ((keyname = strtok(argptr, " \t\n\r")) != NULL)
                {
                    if ((keysym = XStringToKeysym(keyname)) == NoSymbol)
                        printf("x2x: warning: can't translate %s\n", keyname);
                    else if (eventno + 1 >= MAX_BUTTONMAPEVENTS)
                        printf("x2x: warning: too many keys mapped to button %d\n",
                                button);
                    else
                        buttonmap[button][eventno++] = keysym;
                    argptr = NULL;
                }
                buttonmap[button][eventno] = NoSymbol;
            }
        } else if (!strcasecmp(argv[arg], "-noresurface")) {
            doResurface = False;

            debug("will not resurface the trigger window when obscured\n");
        } else if (!strcasecmp(argv[arg], "-shadow")) {
            if (++arg >= argc) Usage();
            pShadow = (PSHADOW)malloc(sizeof(SHADOW));
            pShadow->name = argv[arg];

            /* into the global list of shadows */
            pShadow->pNext = shadows;
            shadows = pShadow;

        } else if (!strcasecmp(argv[arg], "-triggerw")) {
            if (++arg >= argc) Usage();
            triggerw = atoi(argv[arg]);
        } else if (!strcasecmp(argv[arg], "-copyright")) {
            //      printf(lawyerese);
            printf("this makes an error so I commented it out\n");
        } else {
            Usage();
        } /* END if... */
    } /* END for */

    printf("Finished parsing command line\n");

} /* END ParseCommandLine */

static void Usage()
{
    printf("Usage: x2x [-to <DISPLAY> | -from <DISPLAY>] options...\n");
    printf("       -copyright\n");
    printf("       -geometry <GEOMETRY>\n");
    printf("       -wait\n");
    printf("       -north\n");
    printf("       -south\n");
    printf("       -east\n");
    printf("       -west\n");
    printf("       -nosel\n");
    printf("       -noautoup\n");
    printf("       -noresurface\n");
    printf("       -capslockhack\n");
    printf("       -nocapslockhack\n");
    printf("       -shadow <DISPLAY>\n");
    printf("       -buttonmap <button#> \"<keysym> ...\"\n");
    exit(4);

} /* END Usage */

/**********
 * call the library to check for the test extension
 **********/
static Bool CheckTestExtension(dpy)
    Display  *dpy;
{
    int eventb, errorb;
    int vmajor, vminor;

    return (XTestQueryExtension(dpy, &eventb, &errorb, &vmajor, &vminor));

} /* END CheckTestExtension */

static void DoX2X(fromDpy, toDpy)
    Display *fromDpy;
    Display *toDpy;
{

    printf("Starting loop\n");
    
    DPYINFO   dpyInfo;
    int       nfds;
    fd_set    fdset;
    Bool      fromPending;
    int       fromConn, toConn;

    printf("putting displays in dpyInfo\n");
    /* set up displays */
    dpyInfo.fromDpy = fromDpy;
    dpyInfo.toDpy = toDpy;
    printf("initdpyinfo\n");
    InitDpyInfo(&dpyInfo);
    RegisterEventHandlers(&dpyInfo);

    /* set up for select */
    fromConn = XConnectionNumber(fromDpy);

    toConn   = XConnectionNumber(toDpy);
    nfds = (fromConn > toConn ? fromConn : toConn) + 1;

    while (True) { /* FOREVER */
        if ((fromPending = XPending(fromDpy)))
            if (ProcessEvent(fromDpy, &dpyInfo)) /* done! */
                break;

        if (XPending(toDpy)) {
            if (ProcessEvent(toDpy, &dpyInfo)) /* done! */
                break;
        } else if (!fromPending) {
            FD_ZERO(&fdset);
            FD_SET(fromConn, &fdset);
            FD_SET(toConn, &fdset);
            select(nfds, &fdset, NULL, NULL, NULL);
        }

    } /* END FOREVER */

} /* END DoX2X() */

static void InitDpyInfo(pDpyInfo)
    PDPYINFO pDpyInfo;
{
    Display   *fromDpy, *toDpy;
    Screen    *fromScreen, *toScreen;
    long      black, white;
    int       fromHeight, fromWidth, toHeight, toWidth;
    Pixmap    nullPixmap;
    XColor    dummyColor;
    Window    root, trigger, rret, toRoot, propWin;
    short     *xTable, *yTable; /* short: what about dimensions > 2^15? */
    int       *heights, *widths;
    int       counter;
    int       nScreens, screenNum;
    int       xoff, yoff; /* window offsets */
    unsigned int width, height; /* window width, height */
    int       geomMask;                /* mask returned by parse */
    int       gravMask;
    int       gravity;
    int       xret, yret, wret, hret, bret, dret;
    XSetWindowAttributes xswa;
    XSizeHints *xsh;
    int       eventMask;
    char      *windowName;
    PSHADOW   pShadow;
    int       triggerLoc;
    Bool      vertical;

    /* cache commonly used variables */
    fromDpy = pDpyInfo->fromDpy;
    toDpy   = pDpyInfo->toDpy;
    pDpyInfo->toDpyXtra.propWin = (Window) 0;

    fromScreen = XDefaultScreenOfDisplay(fromDpy);
    toScreen   = XDefaultScreenOfDisplay(toDpy);
    black      = XBlackPixelOfScreen(fromScreen);
    white      = XWhitePixelOfScreen(fromScreen);
    fromHeight = XHeightOfScreen(fromScreen);
    fromWidth  = XWidthOfScreen(fromScreen);
    toHeight   = XHeightOfScreen(toScreen);
    toWidth    = XWidthOfScreen(toScreen);
    toRoot     = XDefaultRootWindow(toDpy);

    /* values also in dpyinfo */
    root       = pDpyInfo->root      = XDefaultRootWindow(fromDpy);
    nScreens   = pDpyInfo->nScreens  = XScreenCount(toDpy);
    vertical   = pDpyInfo->vertical = (doEdge == EDGE_NORTH
            || doEdge == EDGE_SOUTH);
    pDpyInfo->fromDpyUtf8String = XInternAtom(fromDpy, UTF8_STRING, False);
    pDpyInfo->fromDpyTargets = XInternAtom(fromDpy, "TARGETS", False);

    pDpyInfo->toDpyUtf8String = XInternAtom(toDpy, UTF8_STRING, False);
    pDpyInfo->toDpyTargets = XInternAtom(toDpy, "TARGETS", False);

    /* other dpyinfo values */
    pDpyInfo->mode        = X2X_DISCONNECTED;
    pDpyInfo->unreasonableDelta = toWidth / 2;//(vertical ? toHeight : toWidth) / 2;

    /* window init structures */
    xswa.override_redirect = True;
    xsh = XAllocSizeHints();
    eventMask = KeyPressMask | KeyReleaseMask;

    // cursor locations for moving between screens 
    //pDpyInfo->fromIncrCoord = triggerw;
    //pDpyInfo->fromDecrCoord = (vertical ? fromHeight : fromWidth) - triggerw - 1;
    if (doEdge) { // edge triggers x2x 

        nullPixmap = XCreatePixmap(fromDpy, root, 1, 1, 1);
        eventMask |= EnterWindowMask;
        pDpyInfo->grabCursor =
            None;
            // For debuging this will be disabled. Put it back it to hide 
            // the cursor on to.
            //XCreatePixmapCursor(fromDpy, nullPixmap, nullPixmap,
            //        &dummyColor, &dummyColor, 0, 0);
        // trigger window location 
        if (doEdge == EDGE_NORTH) {
            triggerLoc = 0;
            pDpyInfo->fromConnCoord = triggerw;
            pDpyInfo->toDiscCoord = toHeight + triggerw;
        } else if (doEdge == EDGE_SOUTH) {
            triggerLoc = fromHeight - triggerw;
            pDpyInfo->fromConnCoord = fromHeight - triggerw;
            pDpyInfo->toDiscCoord = -triggerw;
        } else if (doEdge == EDGE_EAST) {
            triggerLoc = fromWidth - triggerw;
            pDpyInfo->fromConnCoord = fromWidth - triggerw;
            pDpyInfo->toDiscCoord = -triggerw;
        } else /* doEdge == EDGE_WEST */ {
            triggerLoc = 0;
            pDpyInfo->fromConnCoord = triggerw;
            pDpyInfo->toDiscCoord = toWidth + triggerw;
        } /* END if doEdge == ... */

        xswa.background_pixel = black;

        /* fromWidth - 1 doesn't seem to work for some reason */
        /* Use triggerw offsets so that if an x2x is running
           along the left edge and along the north edge, both with
           -resurface, we don't get a feedback loop of them each
           fighting to be on top.
           --09/27/99 Greg J. Badros <gjb@cs.washington.edu> */
        /* also, make it an InputOnly window so I don't lose
           screen real estate --09/29/99 gjb */
        trigger = pDpyInfo->trigger =
            XCreateWindow(fromDpy, root,
                    triggerLoc,
                    triggerw,
                    triggerw,
                    fromHeight - (2*triggerw),
                    0, 0, InputOnly, 0,
                    CWOverrideRedirect, &xswa);
    }

    /* size hints stuff: */
    xsh->x           = xoff;
    xsh->y           = yoff;
    xsh->base_width  = width;
    xsh->base_height = height;
    xsh->win_gravity = gravity;
    xsh->flags       = (PPosition|PBaseSize|PWinGravity);
    XSetWMNormalHints(fromDpy, trigger, xsh);

    windowName = (char *)malloc(strlen(programStr) + strlen(toDpyName) + 2);
    sprintf(windowName, "%s %s", programStr, toDpyName);

    XStoreName(fromDpy, trigger, windowName);
    XSetIconName(fromDpy, trigger, windowName);

    /* register for WM_DELETE_WINDOW protocol */
    pDpyInfo->wmpAtom = XInternAtom(fromDpy, "WM_PROTOCOLS", True);
    pDpyInfo->wmdwAtom = XInternAtom(fromDpy, "WM_DELETE_WINDOW", True);
    XSetWMProtocols(fromDpy, trigger, &(pDpyInfo->wmdwAtom), 1);

    XFree((char *) xsh);

    free(windowName);

    pDpyInfo->toWidth = toWidth;
    pDpyInfo->toHeight = toHeight;
    pDpyInfo->x = toWidth / 2;
    pDpyInfo->y = toHeight / 2;
    pDpyInfo->xcenter = triggerw / 2 + 100;
    pDpyInfo->ycenter = (fromHeight - (2 * triggerw)) / 2 + 100;
    
    // conversion stuff 
    pDpyInfo->toScreen = (doEdge == EDGE_WEST || doEdge == EDGE_NORTH)
        ? (nScreens - 1) : 0;

    // always create propWin for events from toDpy 
    propWin = XCreateWindow(toDpy, toRoot, 0, 0, 1, 1, 0, 0, InputOutput,
            CopyFromParent, 0, NULL);
    pDpyInfo->toDpyXtra.propWin = propWin;
    debug("Create window %x on todpy\n", (unsigned int)propWin);
    /* initialize pointer mapping */
    RefreshPointerMapping(toDpy, pDpyInfo);

    if (doResurface) /* get visibility events */
        eventMask |= VisibilityChangeMask;

    XSelectInput(fromDpy, trigger, eventMask);
    pDpyInfo->eventMask = eventMask; /* save for future munging */
    XMapRaised(fromDpy, trigger);
    
    printf("test grab control of shadows\n");
    for (pShadow = shadows; pShadow; pShadow = pShadow->pNext)
        XTestGrabControl(pShadow->dpy, True); /* impervious to grabs! */

} /* END InitDpyInfo */

static void DoConnect(pDpyInfo)
    PDPYINFO pDpyInfo;
{
    printf("doconnect\n");

    Display *fromDpy = pDpyInfo->fromDpy;
    Window  trigger = pDpyInfo->trigger;

    PSHADOW   pShadow;

    printf("going through shadows\n");
    for (pShadow = shadows; pShadow; pShadow = pShadow->pNext) {
        if (doDpmsMouse)
        {
            DPMSForceLevel(pShadow->dpy, DPMSModeOn);
        }
        XFlush(pShadow->dpy);
    }

    debug("connecting\n");
    pDpyInfo->mode = X2X_CONNECTED;

    printf("grabbing pointer and keyboard on trigger\n");

    XGrabPointer(fromDpy, trigger, True,
            PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
            GrabModeAsync, GrabModeAsync,
            None, pDpyInfo->grabCursor, CurrentTime);
    XGrabKeyboard(fromDpy, trigger, True,
            GrabModeAsync, GrabModeAsync,
            CurrentTime);
    
    XSelectInput(fromDpy, trigger, pDpyInfo->eventMask | PointerMotionMask);
    XFlush(fromDpy);
   
    printf("...connected\n");
} /* END DoConnect */

static void DoDisconnect(pDpyInfo)
    PDPYINFO pDpyInfo;
{
    printf("dodisconnect\n");
    Display *fromDpy = pDpyInfo->fromDpy;
    PDPYXTRA pDpyXtra;

    debug("disconnecting\n");
    pDpyInfo->mode = X2X_DISCONNECTED;

    printf("unmapping\n");
    printf("ungrabbing\n");
    XUngrabKeyboard(fromDpy, CurrentTime);
    XUngrabPointer(fromDpy, CurrentTime);
    XSelectInput(fromDpy, pDpyInfo->trigger, pDpyInfo->eventMask);

    XFlush(fromDpy);

    printf("disconnected\n");
} /* END DoDisconnect */

static void RegisterEventHandlers(pDpyInfo)
    PDPYINFO pDpyInfo;
{
    printf("RegiserEventHandlers.\n");
    Display *fromDpy = pDpyInfo->fromDpy;
    Window  trigger = pDpyInfo->trigger;
    Display *toDpy;
    Window  propWin;

#define XSAVECONTEXT(A, B, C, D) XSaveContext(A, B, C, (XPointer)(D))

    printf("Xsavecontext\n");

    XSAVECONTEXT(fromDpy, trigger, MotionNotify,    ProcessMotionNotify);
    XSAVECONTEXT(fromDpy, trigger, Expose,          ProcessExpose);
    XSAVECONTEXT(fromDpy, trigger, EnterNotify,     ProcessEnterNotify);
    XSAVECONTEXT(fromDpy, trigger, ButtonPress,     ProcessButtonPress);
    XSAVECONTEXT(fromDpy, trigger, ButtonRelease,   ProcessButtonRelease);
    XSAVECONTEXT(fromDpy, trigger, KeyPress,        ProcessKeyEvent);
    XSAVECONTEXT(fromDpy, trigger, KeyRelease,      ProcessKeyEvent);
    XSAVECONTEXT(fromDpy, trigger, ConfigureNotify, ProcessConfigureNotify);
    XSAVECONTEXT(fromDpy, trigger, ClientMessage,   ProcessClientMessage);
    XSAVECONTEXT(fromDpy, trigger, ClientMessage,   ProcessClientMessage);
    XSAVECONTEXT(fromDpy, trigger, ClientMessage,   ProcessClientMessage);
    XSAVECONTEXT(fromDpy, None,    MappingNotify,   ProcessMapping);


    if (doResurface)
        XSAVECONTEXT(fromDpy, trigger, VisibilityNotify, ProcessVisibility);

    toDpy = pDpyInfo->toDpy;
    propWin = pDpyInfo->toDpyXtra.propWin;
    XSAVECONTEXT(toDpy, None, MappingNotify, ProcessMapping);

} /* END RegisterEventHandlers */

static Bool ProcessEvent(dpy, pDpyInfo)
    Display  *dpy;
    PDPYINFO pDpyInfo;
{
    XEvent    ev;
    XAnyEvent *pEv = (XAnyEvent *)&ev;
    HANDLER   handler;

#define XFINDCONTEXT(A, B, C, D) XFindContext(A, B, C, (XPointer *)(D))

    XNextEvent(dpy, &ev);
    handler = 0;
    if ((!XFINDCONTEXT(dpy, pEv->window, pEv->type, &handler)) ||
            (!XFINDCONTEXT(dpy, None, pEv->type, &handler))) {
        /* have handler */
        return ((*handler)(dpy, pDpyInfo, &ev));
    } else {
        debug("no handler for window 0x%x, event type %d\n",
                (unsigned int)pEv->window, pEv->type);
    } /* END if/else */

    return False;

} /* END ProcessEvent */

static Bool ProcessMotionNotify(unused, pDpyInfo, pEv)
    Display  *unused;
    PDPYINFO pDpyInfo;
    XMotionEvent *pEv; /* caution: might be pseudo-event!!! */
{
    /* Note: ProcessMotionNotify is sometimes called from inside x2x to
     *       simulate a motion event.  Any new references to pEv fields
     *       must be checked carefully!
     */

    int         toScreenNum;
    PSHADOW     pShadow;
    Display     *fromDpy;
    Bool        bAbortedDisconnect;
    int         delta;
    int         dx, dy;
    XMotionEvent xmev;

    toScreenNum = pDpyInfo->toScreen;

    dx = pEv->x - pDpyInfo->xcenter;
    dy = pEv->y - pDpyInfo->ycenter;

    pDpyInfo->x += dx;
    pDpyInfo->y += dy;

    printf("%i,%i %i,%i\n", dx, dy, pEv->x, pEv->y);

    if (dx == 0 && dy == 0)
        return False;

    if (pEv->same_screen) {
        delta = dx + dy;
        if (delta < 0) delta = -delta;
        if (delta > pDpyInfo->unreasonableDelta) {
            printf("unreasable\n");
            return False;
        }
    }

    if (pDpyInfo->vertical ? pDpyInfo->y : pDpyInfo->x 
            - pDpyInfo->toDiscCoord >= 0) {
        printf("disconnect\n");
        DoDisconnect(pDpyInfo);
        if (pDpyInfo->vertical)
            XWarpPointer(pDpyInfo->fromDpy, None, pDpyInfo->root, 0, 0, 0, 0,
                    pDpyInfo->x, pDpyInfo->fromConnCoord);
        else
            XWarpPointer(pDpyInfo->fromDpy, None, pDpyInfo->root, 0, 0, 0, 0,
                    pDpyInfo->fromConnCoord, pDpyInfo->y);
        return False;
    }
 
    if (pDpyInfo->x >= pDpyInfo->toWidth) {
       pDpyInfo->x = pDpyInfo->toWidth - 1;
    } else if (pDpyInfo->x <= 0) {
        pDpyInfo->x = 0;
    }

    if (pDpyInfo->y >= pDpyInfo->toHeight) {
        pDpyInfo->y = pDpyInfo->toHeight - 1;
    } else if (pDpyInfo->y <= 0) {
        pDpyInfo->y = 0;
    }

    for (pShadow = shadows; pShadow; pShadow = pShadow->pNext) {
        if (doDpmsMouse) {
            DPMSForceLevel(pShadow->dpy, DPMSModeOn);
        }

        XTestFakeMotionEvent(pShadow->dpy, toScreenNum,
                pDpyInfo->x, pDpyInfo->y,
//                vert?pDpyInfo->xTables[toScreenNum][pEv->x_root]:toCoord,
//                vert?toCoord:pDpyInfo->yTables[toScreenNum][pEv->y_root],
                0);
        XFlush(pShadow->dpy);
    } /* END for */

    XWarpPointer(pDpyInfo->fromDpy, None, pDpyInfo->trigger, 0, 0, 0, 0,
            pDpyInfo->xcenter, pDpyInfo->ycenter);
    
    return False;

} /* END ProcessMotionNotify */

static Bool ProcessExpose(dpy, pDpyInfo, pEv)
    Display  *dpy;
    PDPYINFO pDpyInfo;
    XExposeEvent *pEv;
{
    printf("processExpose\n");

    XClearWindow(pDpyInfo->fromDpy, pDpyInfo->trigger);

    return False;

} /* END ProcessExpose */

static Bool ProcessEnterNotify(dpy, pDpyInfo, pEv)
    Display  *dpy;
    PDPYINFO pDpyInfo;
    XCrossingEvent *pEv;
{
    printf("processEnterNotify\n");

    Display *fromDpy = pDpyInfo->fromDpy;
    XMotionEvent xmev;

    if ((pEv->mode == NotifyNormal) &&
            (pDpyInfo->mode == X2X_DISCONNECTED) && (dpy == pDpyInfo->fromDpy)) {
        DoConnect(pDpyInfo);
           
        if (pDpyInfo->vertical) {
            pDpyInfo->x = pEv->x;
            pDpyInfo->y = pDpyInfo->toDiscCoord > 0 ? pDpyInfo->toDiscCoord - 1 : 0;
        } else {
            pDpyInfo->x = pDpyInfo->toDiscCoord > 0 ? pDpyInfo->toDiscCoord - 1 : 0;
            pDpyInfo->y = pEv->y;
        }

        XWarpPointer(fromDpy, None, pDpyInfo->root, 0, 0, 0, 0,
                pDpyInfo->xcenter, pDpyInfo->ycenter);
 
        //        xmev.x_root = pDpyInfo->lastFromCoord = pDpyInfo->fromConnCoord;
//        xmev.y_root = pEv->y_root;
//        xmev.same_screen = True;
//        ProcessMotionNotify(NULL, pDpyInfo, &xmev);
    }  /* END if NotifyNormal... */
    return False;

} /* END ProcessEnterNotify */

static Bool ProcessButtonPress(dpy, pDpyInfo, pEv)
    Display  *dpy;
    PDPYINFO pDpyInfo;
    XButtonEvent *pEv;
{
    printf("processButtonPress\n");

    int state;
    PSHADOW   pShadow;
    unsigned int toButton;

    KeySym  keysym;
    KeyCode keycode;
    int     eventno;

    switch (pDpyInfo->mode) {
        case X2X_DISCONNECTED:
            pDpyInfo->mode = X2X_AWAIT_RELEASE;
            debug("awaiting button release before connecting\n");
            break;
        case X2X_CONNECTED:
            debug("Got button %d, max is %d (%d)\n", pEv->button, N_BUTTONS, nButtons);
            if ((pEv->button <= N_BUTTONS) &&
                    (buttonmap[pEv->button][0] != NoSymbol))
            {
                debug("Mapped!\n");
                for (pShadow = shadows; pShadow; pShadow = pShadow->pNext)
                {
                    debug("Button %d is mapped, sending keys: ", pEv->button);
                    for (eventno = 0;
                            (keysym = buttonmap[pEv->button][eventno]) != NoSymbol;
                            eventno++)
                    {
                        if ((keycode = XKeysymToKeycode(pShadow->dpy, keysym))) {
                            XTestFakeKeyEvent(pShadow->dpy, keycode, True, 0);
                            XTestFakeKeyEvent(pShadow->dpy, keycode, False, 0);
                            XFlush(pShadow->dpy);
                            debug(" (0x%04X)", keycode);
                        }
                        else
                            debug(" (no code)");
                    }
                    debug("\n");
                }
            } else if (pEv->button <= nButtons) {
                toButton = pDpyInfo->inverseMap[pEv->button];
                for (pShadow = shadows; pShadow; pShadow = pShadow->pNext) {
                    XTestFakeButtonEvent(pShadow->dpy, toButton, True, 0);
                    debug("from button %d down, to button %d down\n", pEv->button,toButton);
                    XFlush(pShadow->dpy);
                } /* END for */
            }
            if (doEdge) break;

            /* check if more than one button pressed */
            state = pEv->state;
            switch (pEv->button) {
                case Button1: state &= ~Button1Mask; break;
                case Button2: state &= ~Button2Mask; break;
                case Button3: state &= ~Button3Mask; break;
                case Button4: state &= ~Button4Mask; break;
                case Button5: state &= ~Button5Mask; break;
                default:
                              debug("unknown button %d\n", pEv->button);
                              break;
            } /* END switch button */
            if (state) { /* then more than one button pressed */
                debug("awaiting button release before disconnecting\n");
                pDpyInfo->mode = X2X_CONN_RELEASE;
            }
            break;
    } /* END switch mode */
    return False;
} /* END ProcessButtonPress */

static Bool ProcessButtonRelease(dpy, pDpyInfo, pEv)
    Display  *dpy;
    PDPYINFO pDpyInfo;
    XButtonEvent *pEv;
{
    printf("process button release\n");
    int state;
    PSHADOW   pShadow;
    XMotionEvent xmev;
    unsigned int toButton;

    if ((pDpyInfo->mode == X2X_CONNECTED) ||
            (pDpyInfo->mode == X2X_CONN_RELEASE)) {
        if ((pEv->button <= nButtons) &&
                (buttonmap[pEv->button][0] == NoSymbol))
            // Do not process button release if it was mapped to keys
        {
            printf("do not process button release if it was mapped to key? da fuck\n");

            toButton = pDpyInfo->inverseMap[pEv->button];
            for (pShadow = shadows; pShadow; pShadow = pShadow->pNext) {
                XTestFakeButtonEvent(pShadow->dpy, toButton, False, 0);
                debug("from button %d up, to button %d up\n", pEv->button, toButton);
                XFlush(pShadow->dpy);
            } /* END for */
        }
    } /* END if */

    if (doEdge) return False;
    if ((pDpyInfo->mode == X2X_AWAIT_RELEASE) ||
            (pDpyInfo->mode == X2X_CONN_RELEASE)) {
        printf("disconnecting because buttons were all up? \n");
        /* make sure that all buttons are released */
        state = pEv->state;
        switch (pEv->button) {
            case Button1: state &= ~Button1Mask; break;
            case Button2: state &= ~Button2Mask; break;
            case Button3: state &= ~Button3Mask; break;
            case Button4: state &= ~Button4Mask; break;
            case Button5: state &= ~Button5Mask; break;
            default:
                          debug("unknown button %d\n", pEv->button);
                          break;
        } /* END switch button */
        if (!state) { /* all buttons up: time to (dis)connect */
            if (pDpyInfo->mode == X2X_AWAIT_RELEASE) { /* connect */
                DoConnect(pDpyInfo);
                xmev.x_root = pDpyInfo->lastFromCoord = pEv->x_root;
                xmev.y_root = pEv->y_root;
                xmev.same_screen = True;
                ProcessMotionNotify(NULL, pDpyInfo, &xmev);
            } else { /* disconnect */
                DoDisconnect(pDpyInfo);
            } /* END if mode */
        } /* END if !state */
    } /* END if mode */
    return False;

} /* END ProcessButtonRelease */

static Bool ProcessKeyEvent(dpy, pDpyInfo, pEv)
    Display  *dpy;
    PDPYINFO pDpyInfo;
    XKeyEvent *pEv;
{
    
    printf("process key event\n");

    KeyCode   keycode;
    KeySym    keysym;
    PSHADOW   pShadow;
    Bool      bPress;
    Bool      DoFakeShift = False;
    KeyCode   toShiftCode;

    keysym = XKeycodeToKeysym(pDpyInfo->fromDpy, pEv->keycode, 0);
    bPress = (pEv->type == KeyPress);

    /* If CapsLock is on, we need to do some funny business to make sure the */
    /* "to" display does the right thing */
    if(doCapsLkHack && (pEv->state & 0x2))
    {
        /* Throw away any explicit shift events (they're faked as neccessary) */
        if((keysym == XK_Shift_L) || (keysym == XK_Shift_R)) return False;

        /* If the shift key is pressed, do the shift, unless the keysym */
        /* is an alpha key, in which case we invert the shift logic */
        DoFakeShift = (pEv->state & 0x1);
        if(((keysym >= XK_A) && (keysym <= XK_Z)) ||
                ((keysym >= XK_a) && (keysym <= XK_z)))
            DoFakeShift = !DoFakeShift;
    }

    for (pShadow = shadows; pShadow; pShadow = pShadow->pNext) {
        toShiftCode = XKeysymToKeycode(pShadow->dpy, XK_Shift_L);
        if ((keycode = XKeysymToKeycode(pShadow->dpy, keysym))) {
            if(DoFakeShift) XTestFakeKeyEvent(pShadow->dpy, toShiftCode, True, 0);
            XTestFakeKeyEvent(pShadow->dpy, keycode, bPress, 0);
            if(DoFakeShift) XTestFakeKeyEvent(pShadow->dpy, toShiftCode, False, 0);
            XFlush(pShadow->dpy);
        } /* END if */
    } /* END for */

    return False;

} /* END ProcessKeyEvent */

static Bool ProcessConfigureNotify(dpy, pDpyInfo, pEv)
    Display  *dpy;
    PDPYINFO pDpyInfo;
    XConfigureEvent *pEv;
{
    printf("process configure notify?\n");

    return False;

} /* END ProcessConfigureNotify */

static Bool ProcessClientMessage(dpy, pDpyInfo, pEv)
    Display  *dpy;
    PDPYINFO pDpyInfo;
    XClientMessageEvent *pEv;
{
    printf("process client message\n");

    /* terminate if atoms match! */
    return ((pEv->message_type == pDpyInfo->wmpAtom) &&
            (pEv->data.l[0]    == pDpyInfo->wmdwAtom));

} /* END ProcessClientMessage */

static Bool ProcessSelectionRequest(dpy, pDpyInfo, pEv)
    Display *dpy;
    PDPYINFO pDpyInfo;
    XSelectionRequestEvent *pEv;
{
    printf("process selectino request?\n");

    PDPYXTRA pDpyXtra = GETDPYXTRA(dpy, pDpyInfo);
    Display *otherDpy;
    Atom utf8string;
    Atom targets;
    Atom data[10];
    int n = 0;

    if (dpy == pDpyInfo->fromDpy) {
        utf8string = pDpyInfo->fromDpyUtf8String;
        targets = pDpyInfo->fromDpyTargets;
    } else {
        utf8string = pDpyInfo->toDpyUtf8String;
        targets = pDpyInfo->toDpyTargets;
    }

    debug("selection request\n");

    if ((pDpyXtra->sState != SELSTATE_ON) ||
            (pEv->selection != XA_PRIMARY) ||
            (pEv->target > XA_LAST_PREDEFINED && pEv->target != utf8string && pEv->target != targets)) { /* bad request, punt request */
        pEv->property = None;
    } else if (pEv->target == targets) {
        // send targets supported -> UTF8_STRING, STRING, TARGETS
        n = 0;
        data[n++] = utf8string;
        data[n++] = XA_STRING;
        data[n++] = targets;
        XChangeProperty(dpy, pEv->requestor, pEv->property, XA_ATOM, 32, PropModeReplace, (unsigned char *) data, n);
    } else {
        otherDpy = pDpyXtra->otherDpy;
        SendPing(otherDpy, GETDPYXTRA(otherDpy, pDpyInfo)); /* get started */
        if (pDpyInfo->sDpy) {
            /* nuke the old one */
            pDpyInfo->sEv.property = None;
        } /* END if InProg */
        pDpyInfo->sDpy = otherDpy;
        pDpyInfo->sEv = *pEv;
    } /* END if relaySel */
    return False;

} /* END ProcessSelectionRequest */

static void SendPing(dpy, pDpyXtra)
    Display *dpy;
    PDPYXTRA pDpyXtra;
{
    printf("send ping? why?\n");

    if (!(pDpyXtra->pingInProg)) {
        XChangeProperty(dpy, pDpyXtra->propWin, pDpyXtra->pingAtom, XA_PRIMARY,
                8, PropModeAppend, NULL, 0);
        pDpyXtra->pingInProg = True;
    } /* END if */
} /* END SendPing */

static Bool ProcessPropertyNotify(dpy, pDpyInfo, pEv)
    Display *dpy;
    PDPYINFO pDpyInfo;
    XPropertyEvent *pEv;
{
    printf("process property notify\n");

    PDPYXTRA pDpyXtra = GETDPYXTRA(dpy, pDpyInfo);

    debug("property notify\n");

    if (pEv->atom == pDpyXtra->pingAtom) { /* acking a ping */
        pDpyXtra->pingInProg = False;
        if (pDpyXtra->sState == SELSTATE_WAIT) {
            pDpyXtra->sState = SELSTATE_ON;
            XSetSelectionOwner(dpy, XA_PRIMARY, pDpyXtra->propWin, pEv->time);
        } else if (dpy == pDpyInfo->sDpy) {
            if (pDpyInfo->sTime == pEv->time) {
                /* oops, need to ensure uniqueness */
                SendPing(dpy, pDpyXtra); /* try for another time stamp */
            } else {
                pDpyInfo->sTime = pEv->time;
                XConvertSelection(dpy, pDpyInfo->sEv.selection, pDpyInfo->sEv.target,
                        XA_PRIMARY, pDpyXtra->propWin, pEv->time);
            } /* END if ... ensure uniqueness */
        } /* END if sState... */
    } /* END if ping */
    return False;

} /* END ProcessPropertyNotify */

static Bool ProcessSelectionClear(dpy, pDpyInfo, pEv)
    Display *dpy;
    PDPYINFO pDpyInfo;
    XSelectionClearEvent *pEv;
{
    printf("processselectionclear?\n");

    Display  *otherDpy;
    PDPYXTRA pDpyXtra, pOtherXtra;

    debug("selection clear\n");

    if (pEv->selection == XA_PRIMARY) {
        /* track primary selection */
        pDpyXtra = GETDPYXTRA(dpy, pDpyInfo);
        pDpyXtra->sState = SELSTATE_OFF;
        otherDpy = pDpyXtra->otherDpy;
        pOtherXtra = GETDPYXTRA(otherDpy, pDpyInfo);
        pOtherXtra->sState = SELSTATE_WAIT;
        SendPing(otherDpy, pOtherXtra);
        if (pDpyInfo->sDpy) { /* nuke the selection in progress */
            pDpyInfo->sEv.property = None;
            pDpyInfo->sDpy = NULL;
        } /* END if nuke */
    } /* END if primary */
    return False;

} /* END ProcessSelectionClear */

/**********
 * process a visibility event
 **********/
static Bool ProcessVisibility(dpy, pDpyInfo, pEv)
    Display          *dpy;
    PDPYINFO         pDpyInfo;
    XVisibilityEvent *pEv;
{
    printf("process visablility?\n");

    /* might want to qualify, based on other messages.  otherwise,
       this code might cause a loop if two windows decide to fight
       it out for the top of the stack */
    if (pEv->state != VisibilityUnobscured)
        XRaiseWindow(dpy, pEv->window);

    return False;

} /* END ProcessVisibility */

/**********
 * process a keyboard mapping event
 **********/
static Bool ProcessMapping(dpy, pDpyInfo, pEv)
    Display             *dpy;
    PDPYINFO            pDpyInfo;
    XMappingEvent       *pEv;
{
    debug("process mapping\n");

    switch (pEv->request) {
        case MappingModifier:
        case MappingKeyboard:
            XRefreshKeyboardMapping(pEv);
            break;
        case MappingPointer:
            RefreshPointerMapping(dpy, pDpyInfo);
            break;
    } /* END switch */

    return False;

} /* END ProcessMapping */

static void RefreshPointerMapping(dpy, pDpyInfo)
    Display             *dpy;
    PDPYINFO            pDpyInfo;
{
    printf("refreshPointerMapping?\n");

    unsigned int buttCtr;
    unsigned char buttonMap[N_BUTTONS];

    if (dpy == pDpyInfo->toDpy) { /* only care about toDpy */
        /* straightforward mapping */
        for (buttCtr = 1; buttCtr <= N_BUTTONS; ++buttCtr) {
            pDpyInfo->inverseMap[buttCtr] = buttCtr;
        } /* END for */

        nButtons = MIN(N_BUTTONS, XGetPointerMapping(dpy, buttonMap, N_BUTTONS));
        debug("got button mapping: %d items\n", nButtons);

        if (doPointerMap) {
            for (buttCtr = 0; buttCtr < nButtons; ++buttCtr) {
                debug("button %d -> %d\n", buttCtr + 1, buttonMap[buttCtr]);
                if (buttonMap[buttCtr] <= N_BUTTONS)
                    pDpyInfo->inverseMap[buttonMap[buttCtr]] = buttCtr + 1;
            } /* END for */
        } /* END if */
    } /* END if toDpy */

} /* END RefreshPointerMapping */

