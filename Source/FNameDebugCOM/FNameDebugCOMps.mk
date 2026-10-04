
FNameDebugCOMps.dll: dlldata.obj FNameDebugCOM_p.obj FNameDebugCOM_i.obj
	link /dll /out:FNameDebugCOMps.dll /def:FNameDebugCOMps.def /entry:DllMain dlldata.obj FNameDebugCOM_p.obj FNameDebugCOM_i.obj \
		kernel32.lib rpcndr.lib rpcns4.lib rpcrt4.lib oleaut32.lib uuid.lib \

.c.obj:
	cl /c /Ox /DWIN32 /D_WIN32_WINNT=0x0500 /DREGISTER_PROXY_DLL \
		$<

clean:
	@del FNameDebugCOMps.dll
	@del FNameDebugCOMps.lib
	@del FNameDebugCOMps.exp
	@del dlldata.obj
	@del FNameDebugCOM_p.obj
	@del FNameDebugCOM_i.obj
