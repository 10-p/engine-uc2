#include "EnginePrivate.h"

IMPLEMENT_CLASS(UForceFeedbackWaveform);
IMPLEMENT_CLASS(UWaveformManager);

/* scion ======================================================================
 * UWaveformManager::UpdateWaveform
 * Author: jg
 * 
 * Updates which waveform sample to use
 *
 * input:	flDelta FLOAT The amount of time that has elapsed
 *
 * ============================================================================
 */
void UWaveformManager::UpdateWaveform(FLOAT flDelta)
{
	guard(UWaveformManager::UpdateWaveform);
	// Check to see if this waveform sample has expired or not
	if (ElapsedTime + flDelta < FFWaveform->Samples(CurrentSample).Duration)
	{
		ElapsedTime += flDelta;
	}
	else
	{
		// Figure out if there is a tiny bit of time that has overlapped
		// into the next sample
		ElapsedTime = (ElapsedTime + flDelta) -
			FFWaveform->Samples(CurrentSample).Duration;
		CurrentSample++;
		// Check for a completed waveform
		if (CurrentSample == FFWaveform->Samples.Num())
		{
			// If this is a non-looping waveform zero it out
			if (FFWaveform->bIsLooping == 0)
			{
				// This waveform is done playing
				FFWaveform = NULL;
			}
			// This waveform loops until stopped (vehicles most likely)
			else
			{
				CurrentSample = 0;
				ElapsedTime = 0;
			}
		}
	}
	unguard;
}

/* scion ======================================================================
 * UWaveformManager::GetRumbleState
 * Author: jg
 * 
 * Determines the amount of rumble to apply to the left and right motors of the
 * specified gamepad
 *
 * input:	flDelta FLOAT The amount of time that has elapsed
 *			LeftAmount WORD The amount to make the left motor spin
 *			RightAmount WORD The amount to make the right motor spin
 *
 * ============================================================================
 */
void UWaveformManager::GetRumbleState(FLOAT flDelta,
	unsigned short& LeftAmount,unsigned short& RightAmount)
{
	guard(UWaveformManager::GetRumbleState);
	LeftAmount = RightAmount = 0;
	// Only update time/sample if there is a current waveform playing
	if (FFWaveform != NULL && bIsPaused == 0)
	{
		// Update the current tick for this
		UpdateWaveform(flDelta);
		// Only calculate a new waveform amount if the waveform is still valid
		if (FFWaveform != NULL)
		{
			// Evaluate the left function
			LeftAmount = EvaluateFunction(
				FFWaveform->Samples(CurrentSample).LeftFunction,
				FFWaveform->Samples(CurrentSample).LeftAmplitude,flDelta,
				FFWaveform->Samples(CurrentSample).Duration);
			// And now the right
			RightAmount = EvaluateFunction(
				FFWaveform->Samples(CurrentSample).RightFunction,
				FFWaveform->Samples(CurrentSample).RightAmplitude,flDelta,
				FFWaveform->Samples(CurrentSample).Duration);
			// Scale the samples if the user requested it
			if (ScaleAllWaveformsBy != 1.f)
			{
				LeftAmount = (_WORD)((FLOAT)LeftAmount * ScaleAllWaveformsBy);
				RightAmount = (_WORD)((FLOAT)RightAmount * ScaleAllWaveformsBy);
			}
		}
	}
	unguard;
}

/* scion ======================================================================
 * UWaveformManager::EvaluateFunction
 * Author: jg
 * 
 * Figures out which function to call to evaluate the current rumble amount. It
 * internally handles the constant function type.
 *
 * input:	eFunc BYTE Which function to apply to the rumble
 *			byAmplitude BYTE The max value to apply for this function
 *			flDelta FLOAT The amount of time that has elapsed
 *			flDuration FLOAT The max time for this waveform
 *
 * ============================================================================
 */
unsigned short UWaveformManager::EvaluateFunction(BYTE eFunc,BYTE byAmplitude,
	FLOAT flDelta,FLOAT flDuration)
{
	guard(UWaveformManager::EvaluateFunction);
	// Defualt to no rumble in case of unknown type
	unsigned short wAmount = 0;
	// Determine which evaluator to use
	switch(eFunc)
	{
		case WF_Constant:
		{
			// Constant values represent percentage
			wAmount = byAmplitude * (65535 / 100);
			break;
		}
		case WF_LinearIncreasing:
		{
			// Calculate the value based upon elapsed time and the max in
			// byAmplitude. Goes from zero to byAmplitude.
			wAmount = (byAmplitude * (ElapsedTime / flDuration)) * (65535 / 100);
			break;
		}
		case WF_LinearDecreasing:
		{
			// Calculate the value based upon elapsed time and the max in
			// byAmplitude. Goes from byAmplitude to zero.
			wAmount = (byAmplitude * (1.f - (ElapsedTime / flDuration))) *
				(65535 / 100);
			break;
		}
		case WF_Sin0to90:
		{
			// Uses a sin(0..pi/2) function to go from zero to byAmplitude
			wAmount = (byAmplitude * (appSin(PI/2 * (flDelta / flDuration)))) *
				(65535 / 100);
			break;
		}
		case WF_Sin90to180:
		{
			// Uses a sin(pi/2..pi) function to go from byAmplitude to 0
			wAmount = (byAmplitude * (appSin(PI/2 + PI/2 *
				(flDelta / flDuration)))) * (65535 / 100);
			break;
		}
		case WF_Sin0to180:
		{
			// Uses a sin(0..pi) function to go from 0 to byAmplitude to 0
			wAmount = (byAmplitude * (appSin(PI * (flDelta / flDuration)))) *
				(65535 / 100);
			break;
		}
		case WF_Noise:
		{
			// Calculate a random value between 0 and byAmplitude
			wAmount = (byAmplitude * (65535 / 100)) * appFrand();
			break;
		}
	};
	return wAmount;
	unguard;
}
