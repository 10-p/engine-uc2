#!/usr/bin/perl.exe
#author: superville

# File location vars
$root_dir	='C:\\UC2004Code';
$systemdir	= $root_dir.'\\System';
$outputfile	= $systemdir.'\\AvarisDLO.log';
$debugfile	= $systemdir.'\\AvarisDLODebug.log';

# Comparison file vars
#$charpkgname	= 'UCCharacters';
#$charpkgname	= 'UCInventory';
$charpkgname	= 'UCGameplay';

@filelist		= ( 
#UCCHARACTERS
#					"UCPawn_Human.uc",
#					"UCPawn_Human_Malcolm.uc",
#					"UCPawn_Human_Sapphire.uc",
#					"UCPawn_Jugg.uc",
#					"UCPawn_Jugg_Arclite.uc",
#					"UCPawn_Jugg_Gorge.uc",
#					"UCPawn_Jugg_Jackhammer.uc",
#					"UCPawn_Liandri.uc",
#					"UCPawn_Liandri_Corrosion.uc",
#					"UCPawn_Liandri_Devastation.uc",
#					"UCPawn_Liandri_Raptor.uc",
#					"UCPawn_Liandri_Syzygy.uc",
#					"UCPawn_Nakhti.uc",
#					"UCPawn_Nakhti_Anubis.uc",
#					"UCPawn_Nakhti_Anubis_Tutorial.uc",
#					"UCPawn_Nakhti_Apophis.uc",
#					"UCPawn_Nakhti_Darius.uc",
#					"UCPawn_Nakhti_Hyena.uc",
#					"UCPawn_Nakhti_Memphis.uc",
#					"UCPawn_Nakhti_Mirage.uc",
#					"UCPawn_Nakhti_Nepthys.uc",
#					"UCPawn_Nakhti_Ra.uc",
#					"UCPawn_Nakhti_Selket.uc",
#					"UCPawn_Nakhti_Sobek.uc",
#					"UCPawn_Nakhti_Sobek_Tutorial.uc",
#					"UCPawn_Necris.uc",
#					"UCPawn_Necris_Brock.uc",
#					"UCPawn_Necris_Calypso.uc",
#					"UCPawn_Necris_Judas.uc",
#					"UCPawn_Necris_Lauren.uc",
#					"UCPawn_Necris_Lilith.uc",
#					"UCPawn_Necris_Selket.uc",
#					"UCPawn_Skaarj.uc",
#					"UCPawn_Skaarj_Garek.uc",
#					"UCPawn_Skaarj_Korig.uc",
#					"UCPawn_Skaarj_Kraag.uc",
#					"UCPawn_Skaarj_Szalor.uc",
#					"UCPawn_Skaarj_Torgr.uc",
#					"UCPawn_Special_Raiden.uc"

#UCINVENTORY
#				"UCWeap_CanisterGun.uc",
#				"UCWeap_CanisterGun_Bio.uc",
#				"UCWeap_CanisterGun_Gren.uc",
#				"UCWeap_FlakCannon.uc",
#				"UCWeap_RazorJack.uc",
#				"UCWeap_RocketLauncher.uc",
#				"UCWeap_ShockRifle_New.uc",
#				"UCWeap_SniperRifle.uc",
#				"UCWeap_Stinger.uc",
#
#				"UCWeap_Race_Liandri.uc",				
#				"UCWeap_Race_Liandri_Devastation.uc",
#				"UCWeap_Race_Nakhti.uc",
#				"UCWeap_Race_Necris.uc",
#				"UCWeap_Race_Raiden.uc",
#				"UCWeap_Race_Skaarj.uc",
#				"UCWeap_Race_Terran.uc",
#				"UCWeap_Race_Terran_Gorge.uc",
#				"UCWeap_Race_Terran_Malcolm.uc",
#
#				"UCWeap_Melee_Anubis.uc",
#				"UCWeap_Melee_Apophis.uc",
#				"UCWeap_Melee_Arclite.uc",
#				"UCWeap_Melee_Brock.uc",
#				"UCWeap_Melee_Devastation.uc",
#				"UCWeap_Melee_Gorge.uc",
#				"UCWeap_Melee_Lauren.uc",
#				"UCWeap_Melee_Malcolm.uc",
#				"UCWeap_Melee_Raiden.uc",
#				"UCWeap_Melee_Raptor.uc",
#				"UCWeap_Melee_Sapphire.uc",
#				"UCWeap_Melee_Selket.uc",
#				"UCWeap_Melee_Sobek.uc",
#				"UCWeap_Melee_Szalor.uc",
#				"UCWeap_Melee_Torgr.uc"
#
#UCGAMEPLAY
#				"UCPawn.uc",
#				"UCWeapon.uc",
#				"UCWeap_Melee.uc",
#				"UCWeap_Race.uc",
#				"UCWeap_Ranged.uc"
			);
			

@materiallist = ("TexPanner",
				 "TexCoordSource",
				 "TexEnvMap",
				 "TexMatrix",
				 "TexModifier",
				 "TexOscillator",
				 "TexOscillatorTriggered",
				 "TexPanner",
				 "TexPannerTriggered",
				 "TexRotator",
				 "TexScaler",
				 "Texture",
				 "PSAdditiveDistortion",
				 "PSBackBufferEffect",
				 "PSBlurMaterial",
				 "PSColorRemapMaterial",
				 "PSDepthOfFieldMaterial",
				 "PSDiffuseOpacity",
				 "PSDiffuseOpacitySpecularMask",
				 "PSDistortionBaseMaterial",
				 "PSDistortionMaterial",
				 "PSDualDiffuse",
				 "PSEmissiveConstColor",
				 "PSEmissiveShader",
				 "PSEmissiveSkinShader",
				 "PSEmissiveSpecular",
				 "PSLuminanceTexIndexMaterial",
				 "PSModulatedDistortionMaterial",
				 "PSMotionBlurMaterial",
				 "PSPlayerCamMaterial",
				 "PSRefractionMaterial",
				 "PSShenuSkin",
				 "PSSkinShader",
				 "PSSkinShaderOpacity",
				 "PSSubtractiveDistortion",
				 "PSTeamShader",
				 "PSTeamSkinShader",
				 "PSTeamSkinShaderOpacity",
				 "PSUseTexIndexMaterial",
				 "Combiner",
				 "Modifier",
				 "RenderedMaterial",
				 "FinalBlend",
				 "ColorModifier",
				 "MaterialSequence",
				 "MaterialSwitch",
				 "OpacityModifier",
				 "ContactEffectsModifier",
				 "Cubemap",
				 "Shader",
				 "ConstantColor",
				 "ConstantMaterial",
				 "FadeColor",
				 "BitmapMaterial",
				 "MultiPassMaterial",
				 "ParticleMaterial",
				 "PixelShaderMaterial",
				 "ProjectorMaterial",
				 "TerrainMaterial",
				 "VertexColor",
				 "MovieMaterial",
				 "ScriptedTexture",
				 "ShadowBitmapMaterial",
				 "ProjectorMultiMaterial"
				 );
				 
# Debug vars
$runsilent	= 0;

sub printdebug
{
	print $_[0];
	print DEBUGFILE $_[0];
}
				 
#################################################################
## isMaterial
sub isMaterial
{
	foreach $item (@materiallist)
	{
		if( $item eq $_[0] )
		{
			return 1;
		}
	}
	
	0;
}

######################################################################
###### main

open( DEBUGFILE, ">$debugfile" );

foreach $filename (@filelist)
{
	$fullfilename = $root_dir.'\\'.$charpkgname.'\\Classes\\'.$filename;
	
	# Open the file and grab the text
	unless( open( COMPFILE, $fullfilename ) )
	{
		printdebug "Error: Couldn't open file $fullfilename\n";
		next;
	}
	chomp( @filetext = <COMPFILE> );
	close( COMPFILE );
	
	# Open the file and grab the text
	unless( open( COMPFILE, ">$fullfilename" ) )
	{
		printdebug "Error: Couldn't open file $fullfilename\n";
		next;
	}
	
	if( $runsilent == 0 )
	{
		printdebug "\n\nFile: $fullfilename\n";
	}
	
	# For each line in the comparison file
	foreach $line (@filetext)
	{
		# If line matches DLO defaultproperty entry
		#(PackageName=T_Crosshair,ObjectName=CH_Misc_06,ObjectClass=class'Texture'),
		if( $line =~ /(.*)ObjectClass=class'(\w+)'(.*)/ ) 
		{
			$bChanged = 0;
			$type = $2;
			if( isMaterial( $type ) == 1 )
			{
				$type = "Material";
				$bChanged = 1;
			}
			
			$old   = "$1ObjectClass=class'$2'$3";
			$fixed = "$1ObjectClass=class'$type'$3";
			
			print COMPFILE "$fixed\n";
		
			if( $runsilent == 0 && $bChanged == 1 )
			{
				printdebug "Changed:\n$old\n$fixed\n";
			}
		}
		else
		{
			print COMPFILE "$line\n";
		}
	}		
	
	close( COMPFILE );
}

close( DEBUGFILE );