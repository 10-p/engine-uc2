#include "UCElements.h"

IMPLEMENT_PACKAGE(UCElements);

// Register things.
#define NAMES_ONLY
#define AUTOGENERATE_NAME(name) UCELEMENTS_API FName UCELEMENTS_##name;
#define AUTOGENERATE_FUNCTION(cls,idx,name) IMPLEMENT_FUNCTION(cls,idx,name)
#include "UCElementsClasses.h"
#undef AUTOGENERATE_FUNCTION
#undef AUTOGENERATE_NAME
#undef NAMES_ONLY

// Import natives
#define NATIVES_ONLY
#define NAMES_ONLY
#define AUTOGENERATE_NAME(name)
#define AUTOGENERATE_FUNCTION(cls,idx,name)
#include "UCElementsClasses.h"
#undef AUTOGENERATE_FUNCTION
#undef AUTOGENERATE_NAME
#undef NATIVES_ONLY
#undef NAMES_ONLY

void RegisterNamesUCElements()
{
	#define NAMES_ONLY
	#define AUTOGENERATE_NAME(name) extern UCELEMENTS_API FName UCELEMENTS_##name; UCELEMENTS_##name=FName(TEXT(#name),FNAME_Intrinsic);
	#define AUTOGENERATE_FUNCTION(cls,idx,name)
	#include "UCElementsClasses.h"
	#undef DECLARE_NAME
	#undef NAMES_ONLY
}

#if !__STATIC_LINK
	struct FUCElementsInitManager
    {
        FUCElementsInitManager()
        {
			RegisterNamesUCElements();
        }
    } UCElementsInitManager;
#endif

IMPLEMENT_CLASS(UCanvasElement);
IMPLEMENT_CLASS(UElement_Root);
IMPLEMENT_CLASS(UElementAction);
IMPLEMENT_CLASS(UElement_WeaponIcon_AmmoBar);
IMPLEMENT_CLASS(UElement_Text);

void UCanvasElement::RenderElement(UCanvas *C,FLOAT relativeDrawX,FLOAT relativeDrawY, FLOAT relativeScale, 
								   FMatrix& relativeRotation, FMatrix& TransformMat )
{
	guard(Element::RenderElement);

	if (!bHidden && C != NULL)
	{
		relativeScale *= DrawScale;

		// calc the relative draw position
		FLOAT newDrawX = DrawX * ScaleX + SafeZoneOffsetX + relativeDrawX;
		FLOAT newDrawY = DrawY * ScaleY + SafeZoneOffsetY + relativeDrawY;
		FLOAT newDrawW = DrawW * ScaleW;
		FLOAT newDrawH = DrawH * ScaleH;

		FRotationMatrix newRotation(Rotation);
		newRotation *= relativeRotation;
		FMatrix newTransformMat(TransformMat);

		if (Image != NULL)
		{
			// check our uvs
			if (DrawUL == -1.f)
			{
				DrawUL = Image->MaterialUSize();
			}
			if (DrawVL == -1.f)
			{
				DrawVL = Image->MaterialVSize();
			}

			// adjust our dimensions if necessary
			if (bUseParentDrawSize)
			{
				if (ParentElement != NULL)
				{
					newDrawW = ParentElement->DrawW * ScaleW;
					newDrawH = ParentElement->DrawH * ScaleH;
				}
			}
			else if ( bUseImageDimensions )
			{
				DrawW = Abs(DrawUL);
				DrawH = Abs(DrawVL);

				newDrawW = ScaleW * DrawW;
				newDrawH = ScaleH * DrawH;
			}

			// set the render color
			// use parent color if possible
			if (bUseParentDrawColor &&
				ParentElement != NULL)
			{
				C->Color = ParentElement->DrawColor;
			}
			else
			{
				C->Color = DrawColor;
			}
		}

		if ( bUseRotation || relativeScale != 1.f )
		{
			FVector current(newDrawX,newDrawY,0);
			FVector pos = current.TransformVectorBy(GMath.UnitCoords / Rotation);
			newDrawX = pos.X;
			newDrawY = pos.Y;

			FRotationMatrix& r = newRotation;
			FScaleMatrix relS(FVector(relativeScale,relativeScale,1.f));			

			static FLOAT staticDrawX = 0.f;
			static FLOAT staticDrawY = 0.f;
			if ( ParentElement == NULL )
			{
				staticDrawX = DrawX * ScaleX;
				staticDrawY = DrawY * ScaleY;
			}
			FTranslationMatrix offset(FVector(-staticDrawX,-staticDrawY,0.f));
			FTranslationMatrix postoffset(FVector(staticDrawX,staticDrawY,0.f));

			// scion sz - this matrix is used to manually transform quad verts
            // without having to the set the canvas matrix
			newTransformMat = r * offset * relS * postoffset;
		}

		newDrawX -= AnchorX * ScaleW;
		newDrawY -= AnchorY * ScaleH;

		if ( Image != NULL )
		{
			FPlane ElementColor( C->Color );
			ElementColor.W *= DrawAlpha;

			// draw the actual image (scaled);
			switch (DrawMethod)
			{
			case DrawTile:
				C->DrawTile(Image,
					newDrawX,newDrawY,
					newDrawW,newDrawH,
					DrawU,DrawV,
					DrawUL,DrawVL,
					0,
					ElementColor,
					0,0,
					&newTransformMat );
				break;
			case DrawStretched:
				C->DrawTileStretchedUV(Image,
					newDrawX,newDrawY,
					newDrawW,newDrawH,
					DrawU,DrawV,
					DrawUL,DrawVL,
					&newTransformMat );
				break;
			}
		}

		// iterate through any children and render them
		for (INT i = 0; i < ChildElements.Num(); i++)
		{
			if (ChildElements(i) != NULL)
			{
				ChildElements(i)->RenderElement(C,newDrawX,newDrawY,relativeScale,newRotation,newTransformMat);
			}
		}
	}
	unguardobj;
}

void UCanvasElement::execRenderElement(FFrame &Stack, RESULT_DECL)
{
	guardSlow(UCanvasElement::execRenderElement);

	P_GET_OBJECT(UCanvas,C);
	P_GET_FLOAT_OPTX(relativeDrawX,0.f);
	P_GET_FLOAT_OPTX(relativeDrawY,0.f);
	P_GET_FLOAT_OPTX(relativeScale,1.f);
	P_FINISH;

	FMatrix TransformMat;
	TransformMat.SetIdentity();
	RenderElement(C,relativeDrawX,relativeDrawY,relativeScale, 
		FMatrix::Identity, TransformMat );

	unguardexecSlow;
}

void UElement_WeaponIcon_AmmoBar::RenderElement(UCanvas *C,FLOAT relativeDrawX,FLOAT relativeDrawY, FLOAT relativeScale, 
												FMatrix& relativeRotation, FMatrix& TransformMat )
{
	guard(AmmoBar::RenderElement);
	if (Weapon != NULL && Weapon->bDeleteMe)
	{
		Weapon = NULL;
	}

	if ( !bHidden && C != NULL &&
		Weapon != NULL && Image != NULL )
	{
		FLOAT ammoPct =	Clamp<FLOAT>(Weapon->eventAmmoStatus(),0.f,1.f);
		if (ammoPct > 0.f)
		{
			// if the weapon doesn't have enough ammo to fire
			if (bCheckMinAmmo && Weapon->eventGetMinAmmoToFire() > ammoPct)
			{
				// draw the bar in red
				C->Color = FColor(255, 0, 0, DrawColor.A);
			}
			else
			{
				// otherwise use the default color
				C->Color = DrawColor;
			}

			FLOAT newDrawX = DrawX * ScaleX + SafeZoneOffsetX + relativeDrawX;
			FLOAT newDrawY = DrawY * ScaleY + SafeZoneOffsetY + relativeDrawY;
			FLOAT newDrawW = Max(DrawW * ammoPct, 2.f) * ScaleW;
			FLOAT newDrawH = DrawH * ScaleH;

			C->DrawTile( Image, newDrawX, newDrawY, newDrawW, newDrawH,
						DrawU, DrawV, DrawUL * ammoPct, DrawVL,	0, C->Color );
		}
	}
	unguardobj;
}

void UElement_Text::RenderElement(UCanvas *C,FLOAT relativeDrawX,FLOAT relativeDrawY, FLOAT relativeScale, 
								  FMatrix& relativeRotation, FMatrix& TransformMat )
{
	guard(UElement_Text::RenderElement);

	Super::RenderElement(C, relativeDrawX, relativeDrawY);

	if (!bHidden && Font)
	{
		C->CurX = (DrawX + TextOffsetX) * ScaleX + SafeZoneOffsetX + relativeDrawX;
		C->CurY = (DrawY + TextOffsetY) * ScaleY + SafeZoneOffsetY + relativeDrawY;
		C->Color = DrawColor;
		C->ClippedPrint(Font, TextScale * ScaleW, TextScale * ScaleH, false, *Text);
	}

	unguardobj;
}

/*==========================================================================================================
function	::ScriptIsActive
Desc:		Returns whether there are any scripts current active.

last modified by:	Ron Prestenback
==========================================================================================================*/
UBOOL UElement_Root::ScriptIsActive()
{
	guardSlow(UElement_Root::execScriptIsActive);

	UBOOL bResult = 0;
	if ( Scripts.IsValidIndex(ActiveScriptIdx) )
	{
		for (INT i = 0; i < Scripts(ActiveScriptIdx).Actions.Num() && !bResult; i++)
		{
			if (Scripts(ActiveScriptIdx).Actions(i)->bActive)
			{
				bResult = 1;
			}
		}
	}
	if (!bResult)
	{
		ActiveScriptIdx = -1;
	}

	return bResult;

	unguardSlow;
}

void UElement_Root::execScriptIsActive(FFrame& Stack, RESULT_DECL)
{
	P_FINISH;
	
	*(UBOOL*)Result = ScriptIsActive();
}

/*==========================================================================================================
function	::StartScript
Desc:		Begin executing the specified script.

input:		ScriptName
				the script to start

			bOverride
				specify true to kill any existing active scripts

effects:	Sets our ActiveScriptIdx to the index of the script (if found).
			Activates the scripts first action.

notes:		caution...this function sets pending actions according to the order that actions with bDelayActions
			is set - this means that the script's actions array should be sorted according to initdelay
last modified by:	Ron Prestenback
==========================================================================================================*/
void UElement_Root::StartScript(FName scriptName, UBOOL bOverride, UBOOL bSkipDeactivate)
{
	guard(UElement_Root::StartScript);

#ifndef FINAL_RELEASE
	if ( bDebugScripts )
	{
		debugf( TEXT("Starting script '%s' in %s bOverride: %s Active: %s"), *scriptName, GetName(),
				bOverride ? TEXT("True") : TEXT("False"), ScriptIsActive() ? TEXT("True") : TEXT("False") );
	}
#endif

	if (ScriptIsActive())
	{
		if (bOverride)
		{
			if ( !bSkipDeactivate )
			{
				// stop the previous script
				for (INT i = 0; i < Scripts(ActiveScriptIdx).Actions.Num(); i++)
				{
					if (Scripts(ActiveScriptIdx).Actions(i)->bActive)
					{
						Scripts(ActiveScriptIdx).Actions(i)->eventDeActivate();
					}
				}
			}
			ActiveScriptIdx = -1;
			// and nuke any queued scripts
			QueuedScripts.Empty();
		}
		else
		{
			// queue this script
			QueuedScripts.AddItem(scriptName);
		}
	}
	if (ActiveScriptIdx == -1)
	{
		UElement_Root* Default = GetClass()->GetDefaultObject<UElement_Root>();
		// search for the new script
		for (INT scriptIdx = 0; scriptIdx < Scripts.Num(); scriptIdx++)
		{
			if (Scripts(scriptIdx).ScriptName == scriptName)
			{
				ActiveScriptIdx = scriptIdx;
				// activate the actions
				UElementAction* pendingAction = NULL;
				INT cnt = 0;
				for (INT i = 0; i < Scripts(scriptIdx).Actions.Num(); i++)
				{
					UElementAction* action = Scripts(scriptIdx).Actions(i);
					UElementAction* DefaultAction = action->GetClass()->GetDefaultObject<UElementAction>();
					if (bDebugScripts)
					{
						action->InitDelay = (action->bTransient ? DefaultAction->InitDelay : Default->Scripts(scriptIdx).Actions(cnt)->InitDelay) * DebugSpeed;
						action->Duration = (action->bTransient ? DefaultAction->Duration : Default->Scripts(scriptIdx).Actions(cnt)->Duration) * DebugSpeed;
						action->bDebugActions = true;
					}
//					else
//					{
//						action->InitDelay = (action->bTransient ? DefaultAction->InitDelay : Default->Scripts(scriptIdx).Actions(cnt)->InitDelay);
//						action->Duration = (action->bTransient ? DefaultAction->Duration : Default->Scripts(scriptIdx).Actions(cnt)->Duration);
//						action->bDebugActions = false;
//					}

					if (!action->bTransient)
					{
						cnt++;
					}

					// set the pending action if necessary
					action->PendingAction = pendingAction;
					// check if this action should delay future actions
					if (action->bDelayActions)
					{
						// set this as the new pending action
						pendingAction = action;
					}
					action->eventActivate();
				}
				break;
			}
		}
	}

	unguard;
}

void UElement_Root::execStartScript(FFrame& Stack, RESULT_DECL)
{
	guardSlow(UElement_Root::execStartScript);

	P_GET_NAME(scriptName);
	P_GET_UBOOL_OPTX(bOverride, false);
	P_GET_UBOOL_OPTX(bSkipDeactivate,false);
	P_FINISH;

	StartScript(scriptName, bOverride, bSkipDeactivate);

	unguardexecSlow;
}

void UElement_Root::execTickScript(FFrame& Stack, RESULT_DECL)
{
	guard(UElement_Root::execTickScript);

	P_GET_FLOAT(DeltaTime);
	P_GET_INT_OPTX(ScriptIndex,ActiveScriptIdx);
	P_FINISH;

	// update our active script
	if (Scripts.IsValidIndex(ScriptIndex))
	{
		UBOOL bStillGoing = false;
		for (INT i = 0; i < Scripts(ScriptIndex).Actions.Num(); i++)
		{
			UElementAction* Action = Scripts(ScriptIndex).Actions(i);
			if (Action->bActive)
			{
				Action->eventTickAction(DeltaTime);
				if (!Action->bFinished)
				{
					bStillGoing = true;
				}
			}
		}

		if (!bStillGoing)
		{
			eventScriptFinished(Scripts(ScriptIndex).ScriptName);
		}
	}

	// check if we should fire a queued script
	if (ScriptIndex == ActiveScriptIdx && !ScriptIsActive() && QueuedScripts.Num() > 0)
	{
		StartScript(QueuedScripts(0), false, false);
		QueuedScripts.Remove(0, 1);
	}

	unguardexec;
}