#!/usr/bin/perl.exe
#author: superville

# File location vars
$root_dir	='C:\\UC2004Code';
$systemdir	= $root_dir.'\\System';
$outputfile	= $systemdir.'\\AvarisDLO.log';
$debugfile	= $systemdir.'\\AvarisDLODebug.log';

# Comparison file vars
#$charpkgname	= 'UCCharacters';
$charpkgname	= 'UCInventory';
#$charpkgname	= 'UCGameplay';

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
#				"UCWeap_Ranged.uc",
			);
			
# Debug vars
$runsilent	= 0;

sub printdebug
{
	print $_[0];
	print DEBUGFILE $_[0];
}

######################################################################
###### main

if( $runsilent == 0 )
{
	printdebug "\n\nBUILD UNION LIST...\n\n";
}

open( DEBUGFILE, ">$debugfile" );

# For each file in the list
foreach $filename (@filelist)
{
	$fullfilename = $root_dir.'\\'.$charpkgname.'\\Classes\\'.$filename;
	
	# Open the file and grab the text
	unless( open( COMPFILE, $fullfilename ) )
	{
		printdebug "Error: Couldn't open file $fullfilename\n";
		next;
	}
	chomp( @compfiletext = <COMPFILE> );
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
	
	$bHadGeneral = 0;
	$bHadSpecific = 0;
	
	# For each line in the comparison file
	foreach $line (@compfiletext)
	{
		# If line matches general DLO defaultproperty entry
		if( $line =~ /(GeneralDLOList\(\d+\))=(.*)/ ) 
		{
			if( $bHadGeneral == 0 )
			{
				print COMPFILE "\tGeneralDLOList={(\n";
				
				$bHadGeneral = 1;
				
				print COMPFILE "\t\t$2";
			}
			else
			{
				print COMPFILE ",\n\t\t$2";
			}
			
			if( $runsilent == 0 )
			{
				printdebug "Print: $2\n";
			}
		}
		elsif( $bHadGeneral == 1 )
		{
			print COMPFILE "\n\t)}\n";
			$bHadGeneral = 0;
			print COMPFILE "$line\n";
		}
		elsif( $line =~ /(SpecificDLOList\(\d+\))=(.*)/ ) 
		{
			# Otherwise, if line matches specific DLO defaultproperty entry
			
			if( $bHadSpecific == 0 )
			{
				print COMPFILE "\tSpecificDLOList={(\n";
				
				$bHadSpecific = 1;
				
				print COMPFILE "\t\t$2";
			}
			else
			{
				print COMPFILE ",\n\t\t$2";
			}
			
			if( $runsilent == 0 )
			{
				printdebug "Print: $2\n";
			}
		}
		elsif( $bHadSpecific == 1 )
		{
			print COMPFILE "\n\t)}\n";
			$bHadSpecific = 0;
			print COMPFILE "$line\n";
		}
		else
		{
			print COMPFILE "$line\n";
		}
	}	
	
	close( COMPFILE );		
}

close( DEBUGFILE );