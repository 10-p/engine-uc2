#!/usr/bin/perl.exe
#author: superville

# File location vars
$root_dir	='C:\\UC2004Code';
$systemdir	= $root_dir.'\\System';
$logfile	= $systemdir.'\\Avaris.log';
#$logfile	= $systemdir.'\\ucc.log';
$outputfile	= $systemdir.'\\AvarisDLO.log';

# Block creation vars
$listname	= 'SpecificDLOList';
#$listname	= 'NonTeamGameDLOList';
#$listname	= 'TeamGameDLOList';

#excludelist used to ignore ObjectClass of types in list
@excludelist = ("Package", 
				"Struct",
				"Const",
				"State",
				"Enum",
				"TextBuffer",
				"ForceFeedbackWaveform",
				"ActionAttachCameraToBone",
				"ActionMoveCamera",
				"ActionPause",
				"CanvasElement",
				"ElementAction",
				"ElementAction_Alpha",
				"ElementAction_AnchorTranslation",
				"ElementAction_CallScript",
				"ElementAction_ChangeImage",
				"ElementAction_Color",
				"ElementAction_Flip",
				"ElementAction_Hide",
				"Element_NestedElement",
				"ElementAction_Rotation",
				"ElementAction_Scale",
				"ElementAction_Translation",
				"ElementAction_Translation_Overshoot",
				"LabelPair",
				"GUILabel",
				"GUIImage",
				"ScoreboardGroup_General",
				"ScoreboardGroup_Special",
				"ScoreboardGroup_TeamSpecials",
				"ScoreboardGroup_Weapon",
				"SubActionCameraEffect",
				"SubActionCameraShake",
				"SubActionDepthOfField",
				"SubActionExec",
				"SubActionFade",
				"SubActionFOV",
				"SubActionFSAA",
				"SubActionGameSpeed",
				"SubActionHDTV",
				"SubActionOrientation",
				"SubActionRecordMovie",
				"SubActionRumble",
				"SubActionSceneSpeed",
				"SubActionSpawnActor",
				"SubActionTrigger",
				"AnimNotify_Script",
				"BeamEmitter",
				"SparkEmitter",
				"SpriteEmitter",
				"MeshEmitter",
				"UCTrailEmitter",
				"UCRibbonEmitter");
				
@castlist =		("TexPanner",
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
				 

######################################
##### Obsolete block code
#$infoname	= 'Info';
#$indexname	= 'Idx';
######################################

# Flags/Counters
$markerfound	= 0;
$processcount	= 0;

# List Vars
@wholelist		= ();	# Raw list parsed from log
@orderlist		= ();	# Reordered list to print out to output file

@texturelist	= ();
@materiallist	= ();
@skeletallist	= ();
@staticlist		= ();
@otherlist		= ();
@classlist		= ();

#Debug Flags
$runsilent		= 1;

################################################################
## isExcluded 
sub isExcluded
{
	foreach $item (@excludelist)
	{
		if( $item eq $_[0] )
		{
			return 1;
		}
	}
	
	0;
}

#################################################################
## isMaterial
sub isMaterial
{
	foreach $item (@castlist)
	{
		if( $item eq $_[0] )
		{
			return 1;
		}
	}
	
	0;
}

#############################################################
# main

# Open the log file and grab all the lines then close
open( LOGFILE, $logfile );
chomp( @logfiletext = <LOGFILE> );
close( LOGFILE );

# Open output file for writing
open( LOGFILE, ">$outputfile" );

# Go through each line in the log file
foreach $line (@logfiletext)
{
	# If we haven't found the start of tracking yet
	if( $markerfound == 0 )
	{
		# Check to see if the line matches the start of tracking loads
		if( !($line =~ /since tracking was enabled:/) )
		{
			if( $runsilent == 0 )
			{
				print "No Marker: $line\n";
			}
				
			# If not, Skip to next line
			next;
		}
	}
	
	# If we found the tracking marker but flag isn't set
	# Set it and skip to next line to start processing
	if( $markerfound == 0 )
	{
		print "Found Marker!\n";
		$markerfound = 1;
		next;
	}
	
	#See if line is of proper type (2 types)
	##Log: StaticMesh S_FX.Particles.FX_Part_BioSphere
	##Log: Class UCCharacters.UCCombo_Heal
	if( $line =~ /Log: (\w+) (\w+)\.(\w+)\.(\w+)/ )
	{
		# Make sure the type isn't in the exclude lists
		if( isExcluded( $1 ) == 0 )
		{
			#Create block (example below)
			##SpecificDLOList(128)=(PackageName=S_FX,ObjectName=FX_Part_BioSphere,ObjectClass=class'StaticMesh')
			
			$type = $1;
			if( isMaterial( $type ) == 1 )
			{
				$type = "Material";
			}
			
			$itemtext = "(PackageName=$2,ObjectName=$4,ObjectClass=class\'$type\')";
	
			@wholelist = (@wholelist,$itemtext);

			if( $runsilent == 0 )
			{
				print "Process: $1 $2.$4\n";
			}
		}
		elsif( $runsilent == 0 )
		{
			print "Exclude Type: $1\n";
		}
	}
	elsif( $line =~ /Log: (\w+) (\w+)\.(\w+)/ )
	{
		if( isExcluded( $1 ) == 0 )
		{
			$type = $1;
			if( isMaterial( $type ) == 1 )
			{
				$type = "Material";
			}
			
			$itemtext = "(PackageName=$2,ObjectName=$3,ObjectClass=class\'$type\')";
			@wholelist = (@wholelist,$itemtext);

			if( $runsilent == 0 )
			{
				print "Process: $1 $2.$3\n";
			}
		}
		elsif( $runsilent == 0 )
		{
			print "Exclude Type: $1\n";
		}
	}
	else
	{
		if( $runsilent == 0 )
		{
			print "Skip Line: $line\n";
		}
	}
}

# Order of log file is reverse of dependency, so flip the list
@wholelist = reverse(@wholelist);

# For each line in the whole list
# Take the item and put it into the proper list by pushing it onto the end of the list
foreach $typeline (@wholelist)
{
	if( $typeline =~ /'Texture'/i )
	{
		push (@texturelist, $typeline);
	}
	elsif( $typeline =~ /'Material'/i )
	{
		push (@materiallist, $typeline);
	}
	elsif( $typeline =~ /'SkeletalMesh'/i )
	{
		push (@skeletallist, $typeline);
	}
	elsif( $typeline =~ /'StaticMesh'/i )
	{
		push (@staticlist, $typeline);
	}
	elsif( $typeline =~ /'Class'/i )
	{
		push (@classlist, $typeline);
	}
	else
	{
		push (@otherlist, $typeline);
	}
}

# Compile the order list
@orderlist = (@texturelist, @materiallist, @otherlist, @skeletallist, @staticlist, @classlist);

# Print to the output file with list name and commas seperating the items
foreach $orderitem (@orderlist)
{
	if( $processcount == 0 )
	{
		print LOGFILE "\t$listname={(\n";
		print LOGFILE "\t\t$orderitem";
	}
	else
	{
		print LOGFILE ",\n\t\t$orderitem";
	}
	#Update the counter
	$processcount++;
}

# Close the array in the output file
if( $processcount > 0 )
{
	print LOGFILE "\n\t)}\n";
}

# Close the output file handle
close( LOGFILE );

print "DONE...\n";

while( 1 )
{
}