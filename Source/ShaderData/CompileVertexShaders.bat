echo XBOX -- Compiling Vertex Shaders

pushd .\VertexShaders

FOR %%a IN (*.vsh) DO xsasm /Og /v /nologo %%a

popd

pause
