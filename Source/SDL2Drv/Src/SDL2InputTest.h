/*=============================================================================
	SDL2InputTest.h: the M10 input test channel (-inputtest=<file>).

	A test harness appends command lines to <file>; the client executes them as they
	arrive, once per tick. Pad commands drive a virtual game controller made with SDL's
	own virtual joystick API, so everything after SDL (SDL_GameController -> SDL2Pads ->
	engine keys -> GUI/game) is the real path. Commands:
	  pad attach | pad detach
	  pad button <sdl button name> <0|1>          a b x y back start leftshoulder dpup ...
	  pad axis <sdl axis name> <value>            leftx lefty rightx righty: -32768..32767 (SDL: y down = +)
	                                              lefttrigger righttrigger: 0..32767 as the controller reads it
	  probe <tag>                                 one "SDL2Input: probe <tag> ..." state line
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
	SDL_Joystick*	Virtual;
	SDL_JoystickID	VirtualID;
	INT				WatchLeft;
	INT				WatchIndex;
	FString			WatchTag;
	UBOOL Execute( USDL2Client* Client, const TCHAR* Line );
	void Probe( USDL2Client* Client, const TCHAR* Tag );
	void Watch( USDL2Client* Client );
	void Detach();
};
