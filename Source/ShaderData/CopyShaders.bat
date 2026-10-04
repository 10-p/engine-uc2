@echo off

echo XBOX -- Copying Pixel Shaders
xbcp /y /f /t /r /d .\PixelShaders\*.xpu xe:\Avaris\ShaderData\PixelShaders\

echo XBOX -- Copying Vertex Shaders
xbcp /y /f /t /r /d .\VertexShaders\*.xvu xe:\Avaris\ShaderData\VertexShaders\



