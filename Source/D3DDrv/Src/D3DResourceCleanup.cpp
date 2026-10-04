#include "D3DDrv.h"

//
//	FD3DResource::~FD3DResource
//
FD3DResource::~FD3DResource()
{
	guard(FD3DResource::~FD3DResource);
	check(RenDev);
	try
	{
		//scion jg -- Break the link between engine/d3d
		if (RenderResource)
		{
			RenderResource->SetCacheId(INVALID_CACHEID,NULL);
		}
	}
	catch(...)
	{
	}
	RenderResource = NULL;
	FD3DResource* ResourcePtr = RenDev->ResourceList;
	// Remove this resource from the device resource list.
	if (ResourcePtr != this)
	{
		try
		{
			while(ResourcePtr && ResourcePtr->NextResource != this)
			{
				ResourcePtr = ResourcePtr->NextResource;
			}

			if(ResourcePtr)
			{
				ResourcePtr->NextResource = NextResource;
			}

			NextResource = NULL;
		}
		catch(...)
		{
		}
	}
	else
	{
		try
		{
			RenDev->ResourceList = NextResource;
			NextResource = NULL;
		}
		catch(...)
		{
		}
	}
	unguard;
}
