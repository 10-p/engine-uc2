echo XBOX -- Compiling Pixel Shaders

pushd .\PixelShaders

FOR %%a IN (*.psh) DO xsasm /nologo %%a

popd

