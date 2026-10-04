@echo off

xbreboot

:DOPING
echo Pinging Xbox...
xbdir xe:\
echo Error level is %ERRORLEVEL%
IF %ERRORLEVEL% == 1 goto DOPING

xbdel /r /h /f /s xp:\*.*
xbdel /r /h /f /s xr:\*.*
xbdel /r /h /f /s xq:\*.*
xbdel /r /h /f /s xe:\%2\System\*.lin

xbcp /y /f /e /d /t %1\System\*.raw xe:\%2\
xbcp /y /f /e /d /t %1\System\xbox.bin xe:\%2\xbox.bin
xbcp /y /f /e /t %1\System\DefaultLS.xbe xe:\%2\

xbcp /y /f /t /r /d /e %1\Downloader\media\ xe:\%2\media\
xbcp /y /f /t /r /d /e %1\Movies\ xe:\%2\Movies\
xbcp /y /f /t /r /d /e %1\Startup\*.xpr xe:\%2\Startup\
xbcp /y /f /t /r /d /e %1\Textures\ xe:\%2\Textures\
xbcp /y /f /t /r /d /e %1\Sounds\ xe:\%2\Sounds\
xbcp /y /f /t /r /d /e %1\XBoxAnimations\ xe:\%2\XBoxAnimations\
xbcp /y /f /t /r /d /e %1\XBoxMaps\*.unr xe:\%2\XBoxMaps\
xbcp /y /f /t /r /d /e %1\XBoxStaticMeshes\ xe:\%2\XBoxStaticMeshes\
xbcp /y /f /t /r /d /e %1\XBoxTextures\*.xpr xe:\%2\XBoxTextures\
xbcp /y /f /t /r /d /e %1\XBoxTextures\*.utx xe:\%2\XBoxTextures\
xbcp /y /f /t /r /d /e %1\XBoxVertexShaders\ xe:\%2\XBoxVertexShaders\

xbcp /y /f /t /r /d /e %1\System\*.ini xe:\%2\System\
xbcp /y /f /t /d /e %1\System\*.u xe:\%2\System\

xbcp /y /f /t /r /s /d /e %1\System\Localization\*.* xe:\%2\System\Localization\

xbdel /f xe:\%2\System\Avaris.ini
xbdel /f xe:\%2\System\User.ini
xbdel /f xe:\%2\System\Default.ini
xbdel /f xe:\%2\System\DefUser.ini
xbdel /f xe:\%2\Sounds\*.uax
xbcp /y /f /e %1\System\DefaultXbox.ini xe:\%2\System\Default.ini
xbcp /y /f /e %1\System\DefUserXbox.ini xe:\%2\System\DefUser.ini

xbreboot xe:\%2\DefaultLS.xbe
