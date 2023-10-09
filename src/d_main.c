//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	DOOM main program (D_DoomMain) and game loop (D_DoomLoop),
//	plus functions to determine game mode (shareware, registered),
//	parse command line parameters, configure game parameters (turbo),
//	and call the startup functions.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "am_map.h"
#include "d_englsh.h"
#include "d_event.h"
#include "d_iwad.h"
#include "d_loop.h"
#include "d_main.h"
#include "d_mode.h"
#include "d_player.h"
#include "doomdef.h"
#include "doomstat.h"
#include "doomtype.h"
#include "f_wipe.h"
#include "g_game.h"
#include "hu_stuff.h"
#include "i_input.h"
#include "i_joystick.h"
#include "i_sound.h"
#include "i_system.h"
#include "i_timer.h"
#include "i_video.h"
#include "m_argv.h"
#include "m_config.h"
#include "m_controls.h"
#include "m_menu.h"
#include "m_misc.h"
#include "net_client.h"
#include "net_dedicated.h"
#include "net_query.h"
#include "p_saveg.h"
#include "p_setup.h"
#include "r_draw.h"
#include "r_main.h"
#include "r_state.h"
#include "s_sound.h"
#include "sounds.h"
#include "st_stuff.h"
#include "statdump.h"
#include "v_video.h"
#include "w_file.h"
#include "w_main.h"
#include "w_wad.h"
#include "wi_stuff.h"
#include "z_zone.h"

#include "ipu_host.h"
#include "ipu/ipu_interface.h"

//
// D-DoomLoop()
// Not a globally visible function,
//  just included for source reference,
//  called by D_DoomMain, never exits.
// Manages timing and IO,
//  calls all ?_Responder, ?_Ticker, and ?_Drawer,
//  calls I_GetTime, I_StartFrame, and I_StartTic
//
void D_DoomLoop(void);

// Location where savegames are stored

char *savegamedir;

// location of IWAD and WAD files

char *iwadfile;

boolean devparm;     // started game with -devparm
boolean nomonsters;  // checkparm of -nomonsters
boolean respawnparm; // checkparm of -respawn
boolean fastparm;    // checkparm of -fast

// extern int soundVolume;
// extern  int	sfxVolume;
// extern  int	musicVolume;

extern boolean inhelpscreens;

skill_t startskill;
int startepisode;
int startmap;
boolean autostart;
int startloadgame;

boolean advancedemo;

// Store demo, do not accept any inputs
boolean storedemo;

// If true, the main game loop has started.
boolean main_loop_started = false;

char wadfile[1024]; // primary wad file
char mapdir[1024];  // directory of development maps

void D_ConnectNetGame(void);
void D_CheckNetGame(void);

//
// D_ProcessEvents
// Send all the events of the given timestamp down the responder chain
//
void D_ProcessEvents(void) {
  event_t *ev;

  // IF STORE DEMO, DO NOT ACCEPT INPUT
  if (storedemo)
    return;


  // JOSEF: Buffer events to send to IPU
  G_Responder_MiscValues_t ev_buf;
  ev_buf.num_ev = 0;

  while ((ev = D_PopEvent()) != NULL) {
    if (M_Responder(ev))
      continue; // menu ate the event
    G_Responder(ev);
   
    if (ev_buf.num_ev < IPUMAXEVENTSPERTIC) {
      ev_buf.events[ev_buf.num_ev++] = *ev;
    } else {
      printf("WARNING: exceeding %d events per tic, keypresses may be dropped\n", IPUMAXEVENTSPERTIC);
    }
  }

  IPU_G_Responder(&ev_buf);
}

//
// D_Display
//  draw current display, possibly wiping it from the previous
//

// wipegamestate can be set to -1 to force a wipe on the next draw
gamestate_t wipegamestate = GS_DEMOSCREEN;
extern boolean setsizeneeded;
extern int showMessages;
void R_ExecuteSetViewSize(void);

void D_Display(void) {
  static boolean viewactivestate = false;
  static boolean menuactivestate = false;
  static boolean inhelpscreensstate = false;
  static boolean fullscreen = false;
  static gamestate_t oldgamestate = -1;
  static int borderdrawcount;
  int nowtime;
  int tics;
  int wipestart;
  int y;
  boolean done;
  boolean wipe;
  boolean redrawsbar;

  if (nodrawers)
    return; // for comparative timing / profiling

  redrawsbar = false;

  // change the view size if needed
  if (setsizeneeded) {
    R_ExecuteSetViewSize();
    oldgamestate = -1; // force background redraw
    borderdrawcount = 3;
  }

  // save the current screen if about to wipe
  if (gamestate != wipegamestate) {
    wipe = true;
    wipe_StartScreen(0, 0, SCREENWIDTH, SCREENHEIGHT);
  } else
    wipe = false;

  if (gamestate == GS_LEVEL && gametic)
    HU_Erase();

  // do buffered drawing
  switch (gamestate) {
  case GS_LEVEL:
    if (!gametic)
      break;

    if (automapactive)
      AM_Drawer();
    // IPU_AM_Drawer(); // JOSEF. Disabled while I split render tiles
    
    
    if (wipe || (viewheight != SCREENHEIGHT && fullscreen))
      redrawsbar = true;
    if (inhelpscreensstate && !inhelpscreens)
      redrawsbar = true; // just put away the help screen
    ST_Drawer(viewheight == SCREENHEIGHT, redrawsbar);
    fullscreen = viewheight == SCREENHEIGHT;
    break;

  case GS_INTERMISSION:
    WI_Drawer();
    break;

  case GS_DEMOSCREEN:
    D_PageDrawer();
    break;
  }

  // draw buffered stuff to screen
  I_UpdateNoBlit();

  // draw the view directly
  if (gamestate == GS_LEVEL && !automapactive && gametic)
    R_RenderPlayerView(&players[displayplayer]);


  if (gamestate == GS_LEVEL && gametic)
    HU_Drawer();

  // clean up border stuff
  if (gamestate != oldgamestate && gamestate != GS_LEVEL)
    I_SetPalette(W_CacheLumpName("PLAYPAL", PU_CACHE));

  // see if the border needs to be initially drawn
  if (gamestate == GS_LEVEL && oldgamestate != GS_LEVEL) {
    viewactivestate = false; // view was not active
    R_FillBackScreen();      // draw the pattern into the back screen
  }

  // see if the border needs to be updated to the screen
  if (gamestate == GS_LEVEL && !automapactive &&
      scaledviewwidth != SCREENWIDTH) {
    if (menuactive || menuactivestate || !viewactivestate)
      borderdrawcount = 3;
    if (borderdrawcount) {
      R_DrawViewBorder(); // erase old menu stuff
      borderdrawcount--;
    }
  }

  if (testcontrols) {
    // Box showing current mouse speed

    V_DrawMouseSpeedBox(testcontrols_mousespeed);
  }

  menuactivestate = menuactive;
  viewactivestate = viewactive;
  inhelpscreensstate = inhelpscreens;
  oldgamestate = wipegamestate = gamestate;

  // draw pause pic
  if (paused) {
    if (automapactive)
      y = 4;
    else
      y = viewwindowy + 4;
    V_DrawPatchDirect(viewwindowx + (scaledviewwidth - 68) / 2, y,
                      W_CacheLumpName("M_PAUSE", PU_CACHE));
  }

  // menus go directly to the screen
  M_Drawer();  // menu is drawn even on top of everything
  NetUpdate(); // send out any new accumulation

  // normal update
  if (!wipe) {
    I_FinishUpdate(); // page flip or blit buffer
    return;
  }

  // wipe update
  wipe_EndScreen(0, 0, SCREENWIDTH, SCREENHEIGHT);

  wipestart = I_GetTime() - 1;

  do {
    do {
      nowtime = I_GetTime();
      tics = nowtime - wipestart;
      I_Sleep(1);
    } while (tics <= 0);

    wipestart = nowtime;
    done = wipe_ScreenWipe(wipe_Melt, 0, 0, SCREENWIDTH, SCREENHEIGHT, tics);
    I_UpdateNoBlit();
    M_Drawer();       // menu is drawn even on top of wipes
    I_FinishUpdate(); // page flip or blit buffer
  } while (!done);
}

//
// Add configuration file variable bindings.
//

void D_BindVariables(void) {
  int i;

  M_ApplyPlatformDefaults();

  I_BindInputVariables();
  I_BindVideoVariables();
  I_BindJoystickVariables();
  I_BindSoundVariables();

  M_BindBaseControls();
  M_BindWeaponControls();
  M_BindMapControls();
  M_BindMenuControls();
  M_BindChatControls(MAXPLAYERS);

  key_multi_msgplayer[0] = HUSTR_KEYGREEN;
  key_multi_msgplayer[1] = HUSTR_KEYINDIGO;
  key_multi_msgplayer[2] = HUSTR_KEYBROWN;
  key_multi_msgplayer[3] = HUSTR_KEYRED;

  NET_BindVariables();

  M_BindIntVariable("mouse_sensitivity", &mouseSensitivity);
  M_BindIntVariable("sfx_volume", &sfxVolume);
  M_BindIntVariable("music_volume", &musicVolume);
  M_BindIntVariable("show_messages", &showMessages);
  M_BindIntVariable("screenblocks", &screenblocks);
  M_BindIntVariable("detaillevel", &detailLevel);
  M_BindIntVariable("snd_channels", &snd_channels);
  M_BindIntVariable("vanilla_savegame_limit", &vanilla_savegame_limit);
  M_BindIntVariable("vanilla_demo_limit", &vanilla_demo_limit);

  // Multiplayer chat macros

  for (i = 0; i < 10; ++i) {
    char buf[12];

    M_snprintf(buf, sizeof(buf), "chatmacro%i", i);
    M_BindStringVariable(buf, &chat_macros[i]);
  }
}

//
// D_GrabMouseCallback
//
// Called to determine whether to grab the mouse pointer
//

boolean D_GrabMouseCallback(void) {
  // Drone players don't need mouse focus

  if (drone)
    return false;

  // when menu is active or game is paused, release the mouse

  if (menuactive || paused)
    return false;

  // only grab mouse when playing levels (but not demos)

  return (gamestate == GS_LEVEL) && !demoplayback && !advancedemo;
}

//
//  D_DoomLoop
//
void D_DoomLoop(void) {
  if (gamevariant == bfgedition &&
      (demorecording || (gameaction == ga_playdemo) || netgame)) {
    printf(" WARNING: You are playing using one of the Doom Classic\n"
           " IWAD files shipped with the Doom 3: BFG Edition. These are\n"
           " known to be incompatible with the regular IWAD files and\n"
           " may cause demos and network games to get out of sync.\n");
  }

  if (demorecording)
    G_BeginRecording();

  main_loop_started = true;

  I_SetWindowTitle(gamedescription);
  I_GraphicsCheckCommandLine();
  I_SetGrabMouseCallback(D_GrabMouseCallback);
  I_InitGraphics();
  IPU_MiscSetup();

  TryRunTics();

  V_RestoreBuffer();
  R_ExecuteSetViewSize();

  D_StartGameLoop();

  if (testcontrols) {
    wipegamestate = gamestate;
  }

  while (1) {
    // frame syncronous IO operations
    I_StartFrame();

    TryRunTics(); // will run at least one tic

    S_UpdateSounds(players[consoleplayer].mo); // move positional sounds

    // Update display, next frame, with current state.
    if (screenvisible)
      D_Display();

    if (M_CheckParm("-onetictest") > 0)
      break;
  }
}

//
//  DEMO LOOP
//
int demosequence;
int pagetic;
char *pagename;

//
// D_PageTicker
// Handles timing for warped projection
//
void D_PageTicker(void) {
  if (--pagetic < 0)
    D_AdvanceDemo();
}

//
// D_PageDrawer
//
void D_PageDrawer(void) {
  V_DrawPatch(0, 0, W_CacheLumpName(pagename, PU_CACHE));
}

//
// D_AdvanceDemo
// Called after each demo or intro demosequence finishes
//
void D_AdvanceDemo(void) { advancedemo = true; }

//
// This cycles through the demo sequences.
// FIXME - version dependend demo numbers?
//
void D_DoAdvanceDemo(void) {
  players[consoleplayer].playerstate = PST_LIVE; // not reborn
  advancedemo = false;
  usergame = false; // no save / end game here
  paused = false;
  gameaction = ga_nothing;

  // The Ultimate Doom executable changed the demo sequence to add
  // a DEMO4 demo.  Final Doom was based on Ultimate, so also
  // includes this change; however, the Final Doom IWADs do not
  // include a DEMO4 lump, so the game bombs out with an error
  // when it reaches this point in the demo sequence.

  // However! There is an alternate version of Final Doom that
  // includes a fixed executable.

  if (gameversion == exe_ultimate || gameversion == exe_final)
    demosequence = (demosequence + 1) % 7;
  else
    demosequence = (demosequence + 1) % 6;

  switch (demosequence) {
  case 0:
    if (gamemode == commercial)
      pagetic = TICRATE * 11;
    else
      pagetic = 170;
    gamestate = GS_DEMOSCREEN;
    pagename = "TITLEPIC";
    if (gamemode == commercial)
      S_StartMusic(mus_dm2ttl);
    else
      S_StartMusic(mus_intro);
    break;
  case 1:
    G_DeferedPlayDemo("demo1");
    break;
  case 2:
    pagetic = 200;
    gamestate = GS_DEMOSCREEN;
    pagename = "CREDIT";
    break;
  case 3:
    G_DeferedPlayDemo("demo2");
    break;
  case 4:
    gamestate = GS_DEMOSCREEN;
    if (gamemode == commercial) {
      pagetic = TICRATE * 11;
      pagename = "TITLEPIC";
      S_StartMusic(mus_dm2ttl);
    } else {
      pagetic = 200;

      if (gameversion >= exe_ultimate)
        pagename = "CREDIT";
      else
        pagename = "HELP2";
    }
    break;
  case 5:
    G_DeferedPlayDemo("demo3");
    break;
  // THE DEFINITIVE DOOM Special Edition demo
  case 6:
    G_DeferedPlayDemo("demo4");
    break;
  }

  // The Doom 3: BFG Edition version of doom2.wad does not have a
  // TITLETPIC lump. Use INTERPIC instead as a workaround.
  if (gamevariant == bfgedition && !strcasecmp(pagename, "TITLEPIC") &&
      W_CheckNumForName("titlepic") < 0) {
    pagename = "INTERPIC";
  }
}

//
// D_StartTitle
//
void D_StartTitle(void) {
  gameaction = ga_nothing;
  demosequence = -1;
  D_AdvanceDemo();
}

/* JOSEF: Don't support Doom II
static void SetMissionForPackName(char *pack_name) {
  int i;
  static const struct {
    char *name;
    int mission;
  } packs[] = {
      {"doom2", doom2}, {"tnt", pack_tnt}, {"plutonia", pack_plut},
  };

  for (i = 0; i < arrlen(packs); ++i) {
    if (!strcasecmp(pack_name, packs[i].name)) {
      gamemission = packs[i].mission;
      return;
    }
  }

  printf("Valid mission packs are:\n");

  for (i = 0; i < arrlen(packs); ++i) {
    printf("\t%s\n", packs[i].name);
  }

  I_Error("Unknown mission pack name: %s", pack_name);
}
*/

//
// Find out what version of Doom is playing.
//

void D_IdentifyVersion(void) {
  // JOSEF : I'm not supporting other options
  gamemission = doom;
  gamemode = shareware;

  /*
  // gamemission is set up by the D_FindIWAD function.  But if
  // we specify '-iwad', we have to identify using
  // IdentifyIWADByName.  However, if the iwad does not match
  // any known IWAD name, we may have a dilemma.  Try to
  // identify by its contents.

  if (gamemission == none) {
    unsigned int i;

    for (i = 0; i < numlumps; ++i) {
      if (!strncasecmp(lumpinfo[i]->name, "MAP01", 8)) {
        gamemission = doom2;
        break;
      } else if (!strncasecmp(lumpinfo[i]->name, "E1M1", 8)) {
        gamemission = doom;
        break;
      }
    }

    if (gamemission == none) {
      // Still no idea.  I don't think this is going to work.

      I_Error("Unknown or invalid IWAD file.");
    }
  }

  // Make sure gamemode is set up correctly

  if (logical_gamemission == doom) {
    // Doom 1.  But which version?

    if (W_CheckNumForName("E4M1") > 0) {
      // Ultimate Doom

      gamemode = retail;
    } else if (W_CheckNumForName("E3M1") > 0) {
      gamemode = registered;
    } else {
      gamemode = shareware;
    }
  } else {
    int p;

    // Doom 2 of some kind.
    gamemode = commercial;

    // We can manually override the gamemission that we got from the
    // IWAD detection code. This allows us to eg. play Plutonia 2
    // with Freedoom and get the right level names.

    //!
    // @category compat
    // @arg <pack>
    //
    // Explicitly specify a Doom II "mission pack" to run as, instead of
    // detecting it based on the filename. Valid values are: "doom2",
    // "tnt" and "plutonia".
    //
    p = M_CheckParmWithArgs("-pack", 1);
    if (p > 0) {
      SetMissionForPackName(myargv[p + 1]);
    }
  }*/
}

// Set the gamedescription string

void D_SetGameDescription(void) { gamedescription = "Microdoom"; }

//      print title for every printed line
char title[128];

static boolean D_AddFile(char *filename) {
  wad_file_t *handle;

  printf(" adding %s\n", filename);
  handle = W_AddFile(filename);

  return handle != NULL;
}

static struct {
  char *description;
  char *cmdline;
  GameVersion_t version;
} gameversions[] = {
    {"Doom 1.666", "1.666", exe_doom_1_666},
    {"Doom 1.7/1.7a", "1.7", exe_doom_1_7},
    {"Doom 1.8", "1.8", exe_doom_1_8},
    {"Doom 1.9", "1.9", exe_doom_1_9},
    {"Hacx", "hacx", exe_hacx},
    {"Ultimate Doom", "ultimate", exe_ultimate},
    {"Final Doom", "final", exe_final},
    {"Final Doom (alt)", "final2", exe_final2},
    {"Chex Quest", "chex", exe_chex},
    {NULL, NULL, 0},
};

// Initialize the game version

static void InitGameVersion(void) {
  byte *demolump;
  char demolumpname[6];
  int demoversion;
  int p;
  int i;
  boolean status;

  //!
  // @arg <version>
  // @category compat
  //
  // Emulate a specific version of Doom.  Valid values are "1.666",
  // "1.7", "1.8", "1.9", "ultimate", "final", "final2", "hacx" and
  // "chex".
  //

  p = M_CheckParmWithArgs("-gameversion", 1);

  if (p) {
    for (i = 0; gameversions[i].description != NULL; ++i) {
      if (!strcmp(myargv[p + 1], gameversions[i].cmdline)) {
        gameversion = gameversions[i].version;
        break;
      }
    }

    if (gameversions[i].description == NULL) {
      printf("Supported game versions:\n");

      for (i = 0; gameversions[i].description != NULL; ++i) {
        printf("\t%s (%s)\n", gameversions[i].cmdline,
               gameversions[i].description);
      }

      I_Error("Unknown game version '%s'", myargv[p + 1]);
    }
  } else {
    // Determine automatically

    if (gamemission == pack_chex) {
      // chex.exe - identified by iwad filename

      gameversion = exe_chex;
    } else if (gamemission == pack_hacx) {
      // hacx.exe: identified by iwad filename

      gameversion = exe_hacx;
    } else if (gamemode == shareware || gamemode == registered ||
               (gamemode == commercial && gamemission == doom2)) {
      // original
      gameversion = exe_doom_1_9;

      // Detect version from demo lump
      for (i = 1; i <= 3; ++i) {
        M_snprintf(demolumpname, 6, "demo%i", i);
        if (W_CheckNumForName(demolumpname) > 0) {
          demolump = W_CacheLumpName(demolumpname, PU_STATIC);
          demoversion = demolump[0];
          W_ReleaseLumpName(demolumpname);
          status = true;
          switch (demoversion) {
          case 106:
            gameversion = exe_doom_1_666;
            break;
          case 107:
            gameversion = exe_doom_1_7;
            break;
          case 108:
            gameversion = exe_doom_1_8;
            break;
          case 109:
            gameversion = exe_doom_1_9;
            break;
          default:
            status = false;
            break;
          }
          if (status) {
            break;
          }
        }
      }
    } else if (gamemode == retail) {
      gameversion = exe_ultimate;
    } else if (gamemode == commercial) {
      // Final Doom: tnt or plutonia
      // Defaults to emulating the first Final Doom executable,
      // which has the crash in the demo loop; however, having
      // this as the default should mean that it plays back
      // most demos correctly.

      gameversion = exe_final;
    }
  }

  // The original exe does not support retail - 4th episode not supported

  if (gameversion < exe_ultimate && gamemode == retail) {
    gamemode = registered;
  }

  // EXEs prior to the Final Doom exes do not support Final Doom.

  if (gameversion < exe_final && gamemode == commercial &&
      (gamemission == pack_tnt || gamemission == pack_plut)) {
    gamemission = doom2;
  }
}

void PrintGameVersion(void) {
  int i;

  for (i = 0; gameversions[i].description != NULL; ++i) {
    if (gameversions[i].version == gameversion) {
      printf("Emulating the behavior of the "
             "'%s' executable.\n",
             gameversions[i].description);
      break;
    }
  }
}

static void G_CheckDemoStatusAtExit(void) { G_CheckDemoStatus(); }

//
// D_DoomMain
//
void D_DoomMain(void) {
  int p;
  char file[256];
  char demolumpname[9];
  // int numiwadlumps;

  // print banner

  printf("Z_Init: Init zone memory allocation daemon. \n");
  Z_Init();

  //!
  // @category net
  //
  // Start a dedicated server, routing packets but not participating
  // in the game itself.
  //

  if (M_CheckParm("-dedicated") > 0) {
    printf("Dedicated server mode.\n");
    NET_DedicatedServer();

    // Never returns
  }

  //!
  // @category net
  //
  // Query the Internet master server for a global list of active
  // servers.
  //

  if (M_CheckParm("-search")) {
    NET_MasterQuery();
    exit(0);
  }

  //!
  // @arg <address>
  // @category net
  //
  // Query the status of the server running on the given IP
  // address.
  //

  p = M_CheckParmWithArgs("-query", 1);

  if (p) {
    NET_QueryAddress(myargv[p + 1]);
    exit(0);
  }

  //!
  // @category net
  //
  // Search the local LAN for running servers.
  //

  if (M_CheckParm("-localsearch")) {
    NET_LANQuery();
    exit(0);
  }

  //!
  // @vanilla
  //
  // Disable monsters.
  //

  nomonsters = M_CheckParm("-nomonsters");

  //!
  // @vanilla
  //
  // Monsters respawn after being killed.
  //

  respawnparm = M_CheckParm("-respawn");

  //!
  // @vanilla
  //
  // Monsters move faster.
  //

  fastparm = M_CheckParm("-fast");

  //!
  // @vanilla
  //
  // Developer mode.  F1 saves a screenshot in the current working
  // directory.
  //

  devparm = M_CheckParm("-devparm");

  I_DisplayFPSDots(devparm);

  //!
  // @category net
  // @vanilla
  //
  // Start a deathmatch game.
  //

  if (M_CheckParm("-deathmatch"))
    deathmatch = 1;

  //!
  // @category net
  // @vanilla
  //
  // Start a deathmatch 2.0 game.  Weapons do not stay in place and
  // all items respawn after 30 seconds.
  //

  if (M_CheckParm("-altdeath"))
    deathmatch = 2;

  if (devparm)
    printf(D_DEVSTR);

  // find which dir to use for config files

  M_SetConfigDir(NULL);

  //!
  // @arg <x>
  // @vanilla
  //
  // Turbo mode.  The player's speed is multiplied by x%.  If unspecified,
  // x defaults to 200.  Values are rounded up to 10 and down to 400.
  //

  if ((p = M_CheckParm("-turbo"))) {
    int scale = 200;
    extern int forwardmove[2];
    extern int sidemove[2];

    if (p < myargc - 1)
      scale = atoi(myargv[p + 1]);
    if (scale < 10)
      scale = 10;
    if (scale > 400)
      scale = 400;
    printf("turbo scale: %i%%\n", scale);
    forwardmove[0] = forwardmove[0] * scale / 100;
    forwardmove[1] = forwardmove[1] * scale / 100;
    sidemove[0] = sidemove[0] * scale / 100;
    sidemove[1] = sidemove[1] * scale / 100;
  }

  // init subsystems
  printf("V_Init: allocate screens.\n");
  V_Init();

  // Load configuration files before initialising other subsystems.
  printf("M_LoadDefaults: Load system defaults.\n");
  M_SetConfigFilenames("default.cfg", "chocolate-doom.cfg");
  D_BindVariables();
  M_LoadDefaults();

  // Save configuration at exit.
  I_AtExit(M_SaveDefaults, false);

  // Find main IWAD file and load it.
  iwadfile = D_FindIWAD(IWAD_MASK_DOOM, &gamemission);

  // None found?

  if (iwadfile == NULL) {
    I_Error("Game mode indeterminate.  No IWAD file was found.  Try\n"
            "specifying one with the '-iwad' command line parameter.\n");
  }

  modifiedgame = false;

  printf("W_Init: Init WADfiles.\n");
  D_AddFile(iwadfile);
  // numiwadlumps = numlumps;

  W_CheckCorrectIWAD(doom);

  // Now that we've loaded the IWAD, we can figure out what gamemission
  // we're playing and which version of Vanilla Doom we need to emulate.
  D_IdentifyVersion();
  printf("Gamemode: %d\n", gamemode);
  InitGameVersion();

  // TODO: surely very related to init game version?
  //
  // Check which IWAD variant we are using.

  if (W_CheckNumForName("FREEDOOM") >= 0) {
    if (W_CheckNumForName("FREEDM") >= 0) {
      gamevariant = freedm;
    } else {
      gamevariant = freedoom;
    }
  } else if (W_CheckNumForName("DMENUPIC") >= 0) {
    gamevariant = bfgedition;
  }

  // Load PWAD files.
  modifiedgame = W_ParseCommandLine();

  // Debug:
  //    W_PrintDirectory();
  
  // TODO: the following demo stuff seems like a good candidate for extracting

  //!
  // @arg <demo>
  // @category demo
  // @vanilla
  //
  // Play back the demo named demo.lmp.
  //

  p = M_CheckParmWithArgs("-playdemo", 1);

  if (!p) {
    //!
    // @arg <demo>
    // @category demo
    // @vanilla
    //
    // Play back the demo named demo.lmp, determining the framerate
    // of the screen.
    //
    p = M_CheckParmWithArgs("-timedemo", 1);
  }

  if (p) {
    char *uc_filename = strdup(myargv[p + 1]);
    M_ForceUppercase(uc_filename);

    // With Vanilla you have to specify the file without extension,
    // but make that optional.
    if (M_StringEndsWith(uc_filename, ".LMP")) {
      M_StringCopy(file, myargv[p + 1], sizeof(file));
    } else {
      M_snprintf(file, sizeof(file), "%s.lmp", myargv[p + 1]);
    }

    free(uc_filename);

    if (D_AddFile(file)) {
      M_StringCopy(demolumpname, lumpinfo[numlumps - 1]->name,
                   sizeof(demolumpname));
    } else {
      // If file failed to load, still continue trying to play
      // the demo in the same way as Vanilla Doom.  This makes
      // tricks like "-playdemo demo1" possible.

      M_StringCopy(demolumpname, myargv[p + 1], sizeof(demolumpname));
    }

    printf("Playing demo %s.\n", file);
  }

  I_AtExit(G_CheckDemoStatusAtExit, true);

  // Generate the WAD hash table.  Speed things up a bit.
  W_GenerateHashTable();

  // Set the gamedescription string.
  D_SetGameDescription();

  savegamedir = M_GetSaveGameDir(D_SaveGameIWADName(gamemission));

  if (W_CheckNumForName("SS_START") >= 0 || W_CheckNumForName("FF_END") >= 0) {
    I_PrintDivider();
    printf(" WARNING: The loaded WAD file contains modified sprites or\n"
           " floor textures.  You may want to use the '-merge' command\n"
           " line option instead of '-file'.\n");
  }

  /*I_PrintStartupBanner(gamedescription);*/

  // Freedoom's IWADs are Boom-compatible, which means they usually
  // don't work in Vanilla (though FreeDM is okay). Show a warning
  // message and give a link to the website.
  /*if (gamevariant == freedoom) {*/
    /*printf(" WARNING: You are playing using one of the Freedoom IWAD\n"*/
           /*" files, which might not work in this port. See this page\n"*/
           /*" for more information on how to play using Freedoom:\n"*/
           /*"   https://www.chocolate-doom.org/wiki/index.php/Freedoom\n");*/
    /*I_PrintDivider();*/
  /*}*/

  printf("I_Init: Setting up machine state.\n");
  I_CheckIsScreensaver();
  printf("Checked screen saver\n");
  I_InitTimer();
  printf("inited timer\n");
  I_InitJoystick();
  printf("inited joystick \n");
  I_InitSound(true);
  printf("initted sound\n");
  I_InitMusic();
  printf("initted music\n");

  printf("NET_Init: Init network subsystem.\n");
  NET_Init();

  // Initial netgame startup. Connect to server etc.
  D_ConnectNetGame();

  // get skill / episode / map from parms
  startskill = sk_medium;
  startepisode = 1;
  startmap = 1;
  autostart = false;

  //!
  // @arg <skill>
  // @vanilla
  //
  // Set the game skill, 1-5 (1: easiest, 5: hardest).  A skill of
  // 0 disables all monsters.
  //

  p = M_CheckParmWithArgs("-skill", 1);

  if (p) {
    startskill = myargv[p + 1][0] - '1';
    autostart = true;
  }

  //!
  // @arg <n>
  // @vanilla
  //
  // Start playing on episode n (1-4)
  //

  p = M_CheckParmWithArgs("-episode", 1);

  if (p) {
    startepisode = myargv[p + 1][0] - '0';
    startmap = 1;
    autostart = true;
  }

  timelimit = 0;

  //!
  // @arg <n>
  // @category net
  // @vanilla
  //
  // For multiplayer games: exit each level after n minutes.
  //

  p = M_CheckParmWithArgs("-timer", 1);

  if (p) {
    timelimit = atoi(myargv[p + 1]);
  }

  //!
  // @category net
  // @vanilla
  //
  // Austin Virtual Gaming: end levels after 20 minutes.
  //

  p = M_CheckParm("-avg");

  if (p) {
    timelimit = 20;
  }

  //!
  // @arg [<x> <y> | <xy>]
  // @vanilla
  //
  // Start a game immediately, warping to ExMy (Doom 1) or MAPxy
  // (Doom 2)
  //

  p = M_CheckParmWithArgs("-warp", 1);

  if (p) {
    if (gamemode == commercial)
      startmap = atoi(myargv[p + 1]);
    else {
      startepisode = myargv[p + 1][0] - '0';

      if (p + 2 < myargc) {
        startmap = myargv[p + 2][0] - '0';
      } else {
        startmap = 1;
      }
    }
    autostart = true;
  }

  // Undocumented:
  // Invoked by setup to test the controls.

  p = M_CheckParm("-testcontrols");

  if (p > 0) {
    startepisode = 1;
    startmap = 1;
    autostart = true;
    testcontrols = true;
  }

  // Check for load game parameter
  // We do this here and save the slot number, so that the network code
  // can override it or send the load slot to other players.

  //!
  // @arg <s>
  // @vanilla
  //
  // Load the game in slot s.
  //

  p = M_CheckParmWithArgs("-loadgame", 1);

  if (p) {
    startloadgame = atoi(myargv[p + 1]);
  } else {
    // Not loading a game
    startloadgame = -1;
  }

  printf("IPU_Init: Init hardware... ");
  IPU_Init();

  printf("M_Init: Init miscellaneous info.\n");
  M_Init();

  printf("R_Init: Init DOOM refresh daemon - ");
  R_Init();

  printf("\nP_Init: Init Playloop state.\n");
  P_Init();

  printf("S_Init: Setting up sound.\n");
  S_Init(sfxVolume * 8, musicVolume * 8);

  printf("D_CheckNetGame: Checking network game status.\n");
  D_CheckNetGame();

  PrintGameVersion();

  printf("HU_Init: Setting up heads up display.\n");
  HU_Init();

  printf("ST_Init: Init status bar.\n");
  ST_Init();
  

  // If Doom II without a MAP01 lump, this is a store demo.
  // Moved this here so that MAP01 isn't constantly looked up
  // in the main loop.

  if (gamemode == commercial && W_CheckNumForName("map01") < 0)
    storedemo = true;

  if (M_CheckParmWithArgs("-statdump", 1)) {
    I_AtExit(StatDump, true);
    printf("External statistics registered.\n");
  }

  //!
  // @arg <x>
  // @category demo
  // @vanilla
  //
  // Record a demo named x.lmp.
  //

  p = M_CheckParmWithArgs("-record", 1);

  if (p) {
    G_RecordDemo(myargv[p + 1]);
    autostart = true;
  }

  p = M_CheckParmWithArgs("-playdemo", 1);
  if (p) {
    singledemo = true; // quit after one demo
    G_DeferedPlayDemo(demolumpname);
    D_DoomLoop(); // never returns
  }

  p = M_CheckParmWithArgs("-timedemo", 1);
  if (p) {
    G_TimeDemo(demolumpname);
    D_DoomLoop(); // never returns
  }

  if (startloadgame >= 0) {
    M_StringCopy(file, P_SaveGameFile(startloadgame), sizeof(file));
    G_LoadGame(file);
  }

  if (gameaction != ga_loadgame) {
    if (autostart || netgame)
      G_InitNew(startskill, startepisode, startmap);
    else
      D_StartTitle(); // start up intro loop
  }

  D_DoomLoop(); // never returns
}
