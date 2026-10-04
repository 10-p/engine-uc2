#!/usr/bin/perl.exe
#author: superville

# Analyzes dlo lists for files in given folders and 
# prints out counts and locations for redundant info
#use File::Find;

# File location vars
$root_dir	='C:\\UC2004Code\\';
$systemdir	= $root_dir.'System\\';
$outputfile	= $systemdir.'AvarisDLO.log';
$debugfile	= $systemdir.'AvarisDLODebug.log';
$mindup		= 2;

@filelist = (	"UCCharacters\\Classes\\UCHands_Human_Female.uc",
				"UCCharacters\\Classes\\UCHands_Human_Malcolm.uc",
				"UCCharacters\\Classes\\UCHands_Jugg_Male.uc",
				"UCCharacters\\Classes\\UCHands_Jugg_Male_Arclite.uc",
				"UCCharacters\\Classes\\UCHands_Jugg_Male_Jackhammer.uc",
				"UCCharacters\\Classes\\UCHands_Liandri.uc",
				"UCCharacters\\Classes\\UCHands_Liandri_Devastation.uc",
				"UCCharacters\\Classes\\UCHands_Liandri_Syzygy.uc",
				"UCCharacters\\Classes\\UCHands_Nakhti_Female.uc",
				"UCCharacters\\Classes\\UCHands_Nakhti_Male.uc",
				"UCCharacters\\Classes\\UCHands_Nakhti_Male_Anubis.uc",
				"UCCharacters\\Classes\\UCHands_Nakhti_Male_Apophis.uc",
				"UCCharacters\\Classes\\UCHands_Nakhti_Male_Sobek.uc",
				"UCCharacters\\Classes\\UCHands_Necris_Female.uc",
				"UCCharacters\\Classes\\UCHands_Necris_Male.uc",
				"UCCharacters\\Classes\\UCHands_Skaarj.uc",
				"UCCharacters\\Classes\\UCHands_Skaarj_Garek.uc",
				"UCCharacters\\Classes\\UCHands_Skaarj_Kraag.uc",
				"UCCharacters\\Classes\\UCHands_Skaarj_Torgr.uc",
				"UCCharacters\\Classes\\UCHands_Special_Raiden.uc",
				"UCCharacters\\Classes\\UCPawn_Ambient_Nali.uc",
				"UCCharacters\\Classes\\UCPawn_Human.uc",
				"UCCharacters\\Classes\\UCPawn_Human_Malcolm.uc",
				"UCCharacters\\Classes\\UCPawn_Human_Sapphire.uc",
				"UCCharacters\\Classes\\UCPawn_Jugg.uc",
				"UCCharacters\\Classes\\UCPawn_Jugg_Arclite.uc",
				"UCCharacters\\Classes\\UCPawn_Jugg_Gorge.uc",
				"UCCharacters\\Classes\\UCPawn_Jugg_Jackhammer.uc",
				"UCCharacters\\Classes\\UCPawn_Liandri.uc",
				"UCCharacters\\Classes\\UCPawn_Liandri_Corrosion.uc",
				"UCCharacters\\Classes\\UCPawn_Liandri_Devastation.uc",
				"UCCharacters\\Classes\\UCPawn_Liandri_Raptor.uc",
				"UCCharacters\\Classes\\UCPawn_Liandri_SecurityBotVOne.uc",
				"UCCharacters\\Classes\\UCPawn_Liandri_Syzygy.uc",
				"UCCharacters\\Classes\\UCPawn_Nakhti.uc",
				"UCCharacters\\Classes\\UCPawn_Nakhti_Anubis.uc",
				"UCCharacters\\Classes\\UCPawn_Nakhti_Anubis_Tutorial.uc",
				"UCCharacters\\Classes\\UCPawn_Nakhti_Apophis.uc",
				"UCCharacters\\Classes\\UCPawn_Nakhti_Darius.uc",
				"UCCharacters\\Classes\\UCPawn_Nakhti_Hyena.uc",
				"UCCharacters\\Classes\\UCPawn_Nakhti_Memphis.uc",
				"UCCharacters\\Classes\\UCPawn_Nakhti_Mirage.uc",
				"UCCharacters\\Classes\\UCPawn_Nakhti_Nepthys.uc",
				"UCCharacters\\Classes\\UCPawn_Nakhti_Ra.uc",
				"UCCharacters\\Classes\\UCPawn_Nakhti_Selket.uc",
				"UCCharacters\\Classes\\UCPawn_Nakhti_Sobek.uc",
				"UCCharacters\\Classes\\UCPawn_Nakhti_Sobek_Tutorial.uc",
				"UCCharacters\\Classes\\UCPawn_Necris.uc",
				"UCCharacters\\Classes\\UCPawn_Necris_Brock.uc",
				"UCCharacters\\Classes\\UCPawn_Necris_Calypso.uc",
				"UCCharacters\\Classes\\UCPawn_Necris_Judas.uc",
				"UCCharacters\\Classes\\UCPawn_Necris_Lauren.uc",
				"UCCharacters\\Classes\\UCPawn_Necris_Lilith.uc",
				"UCCharacters\\Classes\\UCPawn_Necris_Selket.uc",
				"UCCharacters\\Classes\\UCPawn_Skaarj.uc",
				"UCCharacters\\Classes\\UCPawn_Skaarj_Garek.uc",
				"UCCharacters\\Classes\\UCPawn_Skaarj_Korig.uc",
				"UCCharacters\\Classes\\UCPawn_Skaarj_Kraag.uc",
				"UCCharacters\\Classes\\UCPawn_Skaarj_Szalor.uc",
				"UCCharacters\\Classes\\UCPawn_Skaarj_Torgr.uc",
				"UCCharacters\\Classes\\UCPawn_Special_Raiden.uc",
				"UCCharacters\\Classes\\UCPawn_Tutorial_Dummy.uc",
				"UCGameplay\\Classes\\UCPawn.uc",
				"UCGameplay\\Classes\\UCWeap_Melee.uc",
				"UCGameplay\\Classes\\UCWeap_Melee_Jugg.uc",
				"UCGameplay\\Classes\\UCWeap_Melee_Liandri.uc",
				"UCGameplay\\Classes\\UCWeap_Melee_Nakhti.uc",
				"UCGameplay\\Classes\\UCWeap_Melee_Necris.uc",
				"UCGameplay\\Classes\\UCWeap_Melee_Skaarj.uc",
				"UCGameplay\\Classes\\UCWeap_Melee_Terran.uc",
				"UCGameplay\\Classes\\UCWeap_Race.uc",
				"UCGameplay\\Classes\\UCWeap_Ranged.uc",
				"UCInventory\\Classes\\UCWeap_CanisterGun_Bio.uc",
				"UCInventory\\Classes\\UCWeap_CanisterGun_Gren.uc",
				"UCInventory\\Classes\\UCWeap_FlakCannon.uc",
				"UCInventory\\Classes\\UCWeap_Melee_Anubis.uc",
				"UCInventory\\Classes\\UCWeap_Melee_Apophis.uc",
				"UCInventory\\Classes\\UCWeap_Melee_Brock.uc",
				"UCInventory\\Classes\\UCWeap_Melee_Devastation.uc",
				"UCInventory\\Classes\\UCWeap_Melee_Lauren.uc",
				"UCInventory\\Classes\\UCWeap_Melee_Malcolm.uc",
				"UCInventory\\Classes\\UCWeap_Melee_Raiden.uc",
				"UCInventory\\Classes\\UCWeap_Melee_Raptorv",
				"UCInventory\\Classes\\UCWeap_Melee_Sapphire.uc",
				"UCInventory\\Classes\\UCWeap_Melee_Selket.uc",
				"UCInventory\\Classes\\UCWeap_Melee_Sobek.uc",
				"UCInventory\\Classes\\UCWeap_Melee_Szalor.uc",
				"UCInventory\\Classes\\UCWeap_Melee_Torgr.uc",
				"UCInventory\\Classes\\UCWeap_Race_Liandri.uc",
				"UCInventory\\Classes\\UCWeap_Race_Liandri_Devastation.uc",
				"UCInventory\\Classes\\UCWeap_Race_Nakhti.uc",
				"UCInventory\\Classes\\UCWeap_Race_Necris.uc",
				"UCInventory\\Classes\\UCWeap_Race_Raiden.uc",
				"UCInventory\\Classes\\UCWeap_Race_Skaarj.uc",
				"UCInventory\\Classes\\UCWeap_Race_Terran_Gorge.uc",
				"UCInventory\\Classes\\UCWeap_Race_Terran_Malcolm.uc",
				"UCInventory\\Classes\\UCWeap_Razorjack.uc",
				"UCInventory\\Classes\\UCWeap_ShockRifle_New.uc",
				"UCInventory\\Classes\\UCWeap_SniperRifle.uc",
				"UCInventory\\Classes\\UCWeap_Stinger.uc",
				"UCMutators\\Classes\\UCWeap_Instagib.uc"
			);

#@duphash holds	info on each entry in the format
#Key: PackageName.ObjectName.ObjectClass
#Value: Count|Location1,Location2,...
%duphash = ();
			
# Debug vars
$runsilent	= 0;

sub printdebug
{
	print $_[0];
	print DEBUGFILE $_[0];
}
			
#############################################################
# main

open( DEBUGFILE, ">$debugfile" );

# For each file in the list
foreach $filename (@filelist)
{
	$fullfilename = $root_dir.$filename;
	
	# Open the file for reading
	unless( open( ANALYZEFILE, $fullfilename ) )
	{
		# print out warning if couldn't be opened
		printdebug "Error: Couldn't open file $fullfilename\n";
		next;
	}
	
	# Get all lines from the file
	chomp( @filetext = <ANALYZEFILE> );
	close( ANALYZEFILE );
	
	if( $runsilent == 0 )
	{
		printdebug "\n\nFile: $fullfilename\n";
	}
	
	# For each line in the file
	foreach $fileline (@filetext)
	{
		if( $runsilent == 0 )
		{
#			printdebug "Try: $fileline\n";
		}
		
		# If the line matches the format
		# (PackageName=K_Weapons,ObjectName=SniperRifle,ObjectClass=class'MeshAnimation')
		if( $fileline =~ /\(PackageName=(\w+),ObjectName=(\w+),ObjectClass=class'(\w+)'\)/ ) 
		{
			if( $runsilent == 0 )
			{
				printdebug "Match Line: $fileline\n";
			}
		
			#Build the key to be found in the hash
			#Key: PackageName.ObjectName.ObjectClass
			$key = "$1.$2.$3";
			
			# Try to find it in the hash
			$curvalue = $duphash{$key};
			
			if( $runsilent == 0 )
			{
				printdebug "Key: $key CurVal: $curvalue\n";
			}
			
			# If it is not found
			if( !defined($curvalue) )
			{
				# Add the key to the hash with a count of 1 and the current file in the location list
				#Value: Count|Location1,Location2,...
				$duphash{$key} = "(1|$filename)";
				
				if( $runsilent == 0 )
				{
					printdebug "Not Found... Add: $key => $duphash{$key}\n";
				}
			}
			else
			{
				# Otherwise...
				# Break apart the value
				if( $curvalue =~ /\((\d+)\|(.+)\)/ )
				{
					# Get the count and list of locations
					$count = $1;
					$place = $2;
					
					if( $runsilent == 0 )
					{
						printdebug "Count: $count\nPlace: $place\n";
					}
					
					# Increment the count and add the current file to the location list
					$count++;
					$loclist = "$place,$filename";
					
					# Set new value for the key
					$duphash{$key} = "($count|$loclist)";
					
					if( $runsilent == 0 )
					{
						printdebug "Found... Key: $key NewValue: $duphash{$key}\n";
					}
				}
				else
				{
					printdebug "Invalid Value: $curvalue for Key: $key\n";
				}
			}
			
				
		}				
	}
	
	close( ANALYZEFILE );
}

# Open the output file for writing
# Open output file for writing
open( OUTFILE, ">$outputfile" );

if( $runsilent == 0 )
{
	printdebug "\n\nOUTPUT TO $outputfile...\n\n";
}

# For each entry into the hash
while( ($k, $v) = each %duphash )
{
	if( $runsilent == 0 )
	{
		print "Pair $k => $v\n";
	}

	# Break apart the value
	if( $v =~ /\((\d+)\|(.*)\)/ )
	{
		$count	  = $1;
		$sortline = $2;
		@places	  = ();
		
		if( $count >= $mindup )
		{
			while( $sortline =~ /(.*),(.*)/ )
			{
				push( @places, $2 );
				$sortline = $1;
			}
			
			# If there is something left in sortline, assume it is a file and push it onto the list
			if( $sortline ne "" )
			{
				push( @places, $sortline );
				$sortline = "";
			}

			# Print out the key : count
			print OUTFILE "$count : $k\n";
			
			# Print out the location list
			foreach $place (@places)
			{
				print OUTFILE "$place\n";
			}
			
			print OUTFILE "\n";
		}
	}
	else
	{
		printdebug "Invalid Value: $curvalue for Key: $key\n";
	}
}

printdebug "\n\nDONE...\n";

close( OUTFILE );
close( DEBUGFILE );

while( 1 )
{
}
