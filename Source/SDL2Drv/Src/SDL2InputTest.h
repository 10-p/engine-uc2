/*=============================================================================
	SDL2InputTest.h: the M10 input test channel ([SDL2Drv.SDL2Client] InputTestFile=<file>, set with -ini:).

	A test harness appends command lines to <file>; the client executes them as they
	arrive, once per tick. Pad commands drive a virtual game controller made with SDL's
	own virtual joystick API, so everything after SDL (SDL_GameController -> SDL2Pads ->
	engine keys -> GUI/game) is the real path. Commands:
	  pad [<n>] attach | pad [<n>] detach          G-6: <n> = virtual pad 0..3 (default 0); SDL puts each
	                                              new pad in the first free port
	  pad [<n>] button <sdl button name> <0|1>    a b x y back start leftshoulder dpup ...
	  pad axis <sdl axis name> <value>            leftx lefty rightx righty: -32768..32767 (SDL: y down = +)
	                                              lefttrigger righttrigger: 0..32767 as the controller reads it
	  probe <tag>                                 one "SDL2Input: probe <tag> ..." state line
	  viewports <tag>                             G-6: one "SDL2Input: vps <tag> ..." line (screen, split
	                                              views), then one "SDL2Input: vp <tag> <i> ..." line per viewport
	                                              (rectangle, scales, flags, pad, player, pawn, HUD)
	  exec <console command>                      run on the first viewport (e.g. "shot")
	  log <text>                                  a marker line
	  mouse <dx> <dy>                             D-6: one relative mouse motion (SDL_MOUSEMOTION pushed into
	                                              SDL's queue for the first viewport's window, so the viewport's
	                                              own event handling onwards is the real path); one per tick
	  watch <ticks> [tag]                         D-6: one "SDL2Input: watch <tag> <i> ..." line per client tick
	                                              for <ticks> ticks (time, view rotation, camera, pawn)
	  place <i> <distance> <yaw degrees>          D-6 lock-on set-up: move the i-th other player's pawn (the
	                                              probe's "other" lines) to <distance> units from ours, at <yaw>
	                                              degrees from our view's yaw, at our height
	  aim <i>                                     D-6 lock-on set-up: turn our view to the i-th other pawn
	  keys <tag> <Key> [<Key>...]                 D-9: one "SDL2Input: keys <tag> <Key>="<binding>" ..." line
	                                              with the first viewport's bindings of those keys
	The probe also logs "probe4 <tag> caption=..." (D-9): the focused control's Caption.
	The test tools/dev/m10-controls.sh writes these and reads the log.
=============================================================================*/

class FSDL2InputTest
{
public:
	FSDL2InputTest( const TCHAR* InPath );
	~FSDL2InputTest();
	void Tick( USDL2Client* Client );

private:
	FILE*			File;
	ANSICHAR		Partial[1024];
	INT				PartialLen;
	enum { MAX_VIRTUAL = 4 };					// G-6: one per pad port
	SDL_Joystick*	Virtual[MAX_VIRTUAL];
	SDL_JoystickID	VirtualID[MAX_VIRTUAL];
	INT				WatchLeft;
	INT				WatchIndex;
	FString			WatchTag;
	UBOOL Execute( USDL2Client* Client, const TCHAR* Line );
	void Probe( USDL2Client* Client, const TCHAR* Tag );
	void ProbeViewports( USDL2Client* Client, const TCHAR* Tag );
	void Watch( USDL2Client* Client );
	void Detach( INT Pad );
};
