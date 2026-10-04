/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * This file contains the native functions that are only accessible from C++.
 *
 * ============================================================================
 */

// Called by the viewport when updating the gamepad states
void GetRumbleState(FLOAT flDelta,unsigned short& LeftAmount,
	unsigned short& RightAmount);

// Determines which evaluation function to call and lets that function scale by
// time and amplitude
unsigned short EvaluateFunction(BYTE eFunc,BYTE byAmplitude,FLOAT flDelta,
	FLOAT flDuration);

// Update the currently playing waveform sample
void UpdateWaveform(FLOAT flDelta);