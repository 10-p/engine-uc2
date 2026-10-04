#!/usr/bin/perl.exe
#author: superville
# App gets the union of 2 files Specific DLO arrays

$stripafter	  = 1;

# File location vars
$root_dir	='C:\\UC2004Code';
$systemdir	= $root_dir.'\\System';
$outputfile	= $systemdir.'\\AvarisDLO.log';
$debugfile	= $systemdir.'\\AvarisDLODebug.log';

# Sorting list vars
@texturelist	= ();
@materiallist	= ();
@skeletallist	= ();
@staticlist		= ();
@otherlist		= ();
@classlist		= ();
@orderlist		= ();

# Comparison file vars

$charpkgname	= 'UCInventory';
@sortlist		= ( 
#					"(Union)UCWeap_Ranged|UCWeap_RocketLauncher,UCWeap_FlakCannon,UCWeap_CanisterGun_Gren,UCWeap_RazorJack,UCWeap_SniperRifle,UCWeap_ShockRifle_New,UCWeap_CanisterGun_Bio,UCWeap_Stinger",
#					"(Union)UCWeap_Race|UCWeap_Race_Liandri,UCWeap_Race_Liandri_Devastation,UCWeap_Race_Nakhti,UCWeap_Race_Necris,UCWeap_Race_Raiden,UCWeap_Race_Skaarj,UCWeap_Race_Terran_Gorge,UCWeap_Race_Terran_Malcolm",
					"(Inter)UCWeap_Melee|UCWeap_Melee_Anubis,UCWeap_Melee_Apophis,UCWeap_Melee_Brock,UCWeap_Melee_Devastation,UCWeap_Melee_Gorge,UCWeap_Melee_Lauren,UCWeap_Melee_Malcolm,UCWeap_Melee_Raiden,UCWeap_Melee_Raptor,UCWeap_Melee_Sapphire,UCWeap_Melee_Selket,UCWeap_Melee_Sobek,UCWeap_Melee_Szalor,UCWeap_Melee_Torgr",
					"(Inter)UCWeap_Melee_Nakhti|UCWeap_Melee_Anubis,UCWeap_Melee_Apophis,UCWeap_Melee_Raiden,UCWeap_Melee_Selket,UCWeap_Melee_Sobek",
####					"(Inter)UCWeap_Melee_Sobek|UCWeap_Melee_Apophis",
					"(Inter)UCWeap_Melee_Necris|UCWeap_Melee_Brock,UCWeap_Melee_Lauren",
					"(Inter)UCWeap_Melee_Liandri|UCWeap_Melee_Devastation,UCWeap_Melee_Raptor",
					"(Inter)UCWeap_Melee_Skaarj|UCWeap_Melee_Szalor,UCWeap_Melee_Torgr",
					"(Inter)UCWeap_Melee_Terran|UCWeap_Melee_Malcolm,UCWeap_Melee_Sapphire",
				);
					
					
#$charpkgname	= 'UCCharacters';					
#@sortlist		= ( 
#					"(Inter)UCPawn|UCPawn_Human_Malcolm,UCPawn_Human_Sapphire,UCPawn_Jugg_Arclite,UCPawn_Jugg_Gorge,UCPawn_Jugg_Jackhammer,UCPawn_Liandri_Corrosion,UCPawn_Liandri_Devastation,UCPawn_Liandri_Raptor,UCPawn_Liandri_Syzygy,UCPawn_Nakhti_Anubis,UCPawn_Nakhti_Anubis_Tutorial,UCPawn_Nakhti_Apophis,UCPawn_Nakhti_Darius,UCPawn_Nakhti_Hyena,UCPawn_Nakhti_Memphis,UCPawn_Nakhti_Mirage,UCPawn_Nakhti_Nepthys,UCPawn_Nakhti_Ra,UCPawn_Nakhti_Selket,UCPawn_Nakhti_Sobek,UCPawn_Necris_Brock,UCPawn_Necris_Calypso,UCPawn_Necris_Judas,UCPawn_Necris_Lauren,UCPawn_Necris_Lilith,UCPawn_Necris_Selket,UCPawn_Skaarj_Garek,UCPawn_Skaarj_Korig,UCPawn_Skaarj_Kraag,UCPawn_Skaarj_Szalor,UCPawn_Skaarj_Torgr,UCPawn_Special_Raiden",
#					"(Inter)UCPawn_Human|UCPawn_Human_Malcolm,UCPawn_Human_Sapphire",
#					"(Inter)UCPawn_Jugg|UCPawn_Jugg_Arclite,UCPawn_Jugg_Gorge,UCPawn_Jugg_Jackhammer",
#					"(Inter)UCPawn_Liandri|UCPawn_Liandri_Corrosion,UCPawn_Liandri_Devastation,UCPawn_Liandri_Raptor,UCPawn_Liandri_Syzygy",
#					"(Inter)UCPawn_Nakhti|UCPawn_Nakhti_Anubis,UCPawn_Nakhti_Anubis_Tutorial,UCPawn_Nakhti_Apophis,UCPawn_Nakhti_Darius,UCPawn_Nakhti_Hyena,UCPawn_Nakhti_Memphis,UCPawn_Nakhti_Mirage,UCPawn_Nakhti_Nepthys,UCPawn_Nakhti_Ra,UCPawn_Nakhti_Selket,UCPawn_Nakhti_Sobek,UCPawn_Special_Raiden",
#					"(Inter)UCPawn_Necris|UCPawn_Necris_Brock,UCPawn_Necris_Calypso,UCPawn_Necris_Judas,UCPawn_Necris_Lauren,UCPawn_Necris_Lilith,UCPawn_Necris_Selket",
#					"(Inter)UCPawn_Skaarj|UCPawn_Skaarj_Garek,UCPawn_Skaarj_Korig,UCPawn_Skaarj_Kraag,UCPawn_Skaarj_Szalor,UCPawn_Skaarj_Torgr",
#				);

# Current index of file we are trying to compare				
$listidx	= 0;
@unionlist	= ();

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

# For each line of sort info
foreach $sortline (@sortlist)
{
	# Empty file list so we can refill it
	@filelist = ();
	$topfile  = "";
	$topline  = "";
	$listidx  = 0;
	
	@texturelist	= ();
	@materiallist	= ();
	@skeletallist	= ();
	@staticlist		= ();
	@otherlist		= ();
	@classlist		= ();
	@orderlist		= ();
	
	# $UNION
	# If false, get intersection (only keep ones in every file)
	# If true, get union (keep anything that shows up but only once)
	$union		= 0;	
	$intersect	= 0;
	
	if( $runsilent == 0 )
	{
		printdebug "\n\nFULL SORTLINE: $sortline\n";
	}
	
	# Grab the comparison type either Union or Inter(section)
	if( $sortline =~ /\((.*)\)(.*)/ )
	{
		$type = $1;
		$rest = $2;
		if( $type =~ /Union/i )
		{
			$union		= 1;
			$intersect	= 0;
		}
		else
		{
			$union		= 0;
			$intersect	= 1;
		}
		
		$sortline = $rest;
		
		if( $runsilent == 0 )
		{
			printdebug "SortType: $type\nRest: $sortline\n";
		}
	}
	else
	{
		if( $runsilent == 0 )
		{
			printdebug "Invalid Sort Line, SKIP (NO UNION/INTER)\n";
		}
		
		# If there is no top file to be found, just skip the line
		next;
	}
	
	# Grab the top file that this info should go into and remove it from the sort line
	#"UCWeap_Ranged|UCWeap_RocketLauncher,UCWeap_FlakCannon,UCWeap_CanisterGun_Gren,UCWeap_RazorJack",
	if( $sortline =~ /(.*)\|(.*)/ )
	{
		$topfile  = $1;
		$topline  = $2;
		$sortline = $2;
		
		if( $runsilent == 0 )
		{
			printdebug "TopFile: $1\nRest: $sortline\n";
		}
	}
	else
	{
		if( $runsilent == 0 )
		{
			printdebug "Invalid Sort Line, SKIP (NO DEST FILE)\n";
		}
		
		# If there is no top file to be found, just skip the line
		next;
	}
	
	# After top file has been pulled off, pull each item off the list and put it into the file list
	# until there are none left
	#"UCWeap_RocketLauncher,UCWeap_FlakCannon,UCWeap_CanisterGun_Gren,UCWeap_RazorJack",
	while( $sortline =~ /(.*),(.*)/ )
	{
		push( @filelist, $2 );
		$sortline = $1;
		
		if( $runsilent == 0 )
		{
			printdebug "File: $2\nRest: $sortline\n";
		}
	}
	
	# If there is something left in sortline, assume it is a file and push it onto the list
	if( $sortline ne "" )
	{
		if( $runsilent == 0 )
		{
			printdebug "File: $sortline\nRest: EMPTY\n";
		}
		
		push( @filelist, $sortline );
		$sortline = "";
	}

	# For each file in the list
	foreach $filename (@filelist)
	{
		$fullfilename = $root_dir.'\\'.$charpkgname.'\\Classes\\'.$filename.'.uc';
		
		# Open the file and grab the text
		unless( open( COMPFILE, $fullfilename ) )
		{
			printdebug "Error: Couldn't open file $fullfilename\n";
			next;
		}
		chomp( @compfiletext = <COMPFILE> );
		close( COMPFILE );
		
		if( $runsilent == 0 )
		{
			printdebug "\n\nFile: $fullfilename\n";
		}
		
		$bHadSpecific = 0;
		
		# If this is the first file we've opened
		if( $listidx == 0 )
		{
			# For each line in the comparison file
			foreach $line (@compfiletext)
			{
				# If line matches specific DLO defaultproperty entry
				#(PackageName=T_CharacterSkins,ObjectName=Temp_IronSpirit_Shd,ObjectClass=class'Material'),
				if( $line =~ /\((.*)ObjectClass=class'(\w+)'\)/ ) 
				{
					$addline = "($1ObjectClass=class'$2')";
					
					# Add the line to the union list
					push( @unionlist, $addline );
					$bHadSpecific = 1;
					
					if( $runsilent == 0 )
					{
						printdebug "First: $addline\n";
					}
				}
			}		
		}
		else
		{
			if( $intersect == 1 )
			{
				if( $runsilent == 0 )
				{
					printdebug "GET INTERSECTION\n";
				}
			
				@complist	= (@unionlist);
				@unionlist	= ();
				
				# For each line in the union list
				foreach $unionline (@complist)
				{
					$bFound = 0;
					
					# Go through each line in the script file
					foreach $fileline (@compfiletext)
					{
						# If compare file line matches specific DLO defaultproperty entry
						#(PackageName=T_CharacterSkins,ObjectName=Temp_IronSpirit_Shd,ObjectClass=class'Material'),
						if( $fileline =~ /\((.*)ObjectClass=class'(\w+)'\)/ ) 
						{
							$addline = "($1ObjectClass=class'$2')";
							$bHadSpecific = 1;
							
							if( $runsilent == 0 )
							{
#								printdebug "Check: $unionline\n";
#								printdebug "Against: $addline\n";
							}
												
							# If the union line and the compared line match
							if( $unionline eq $addline )
							{
								# Add the line back to the union list
								push( @unionlist, $unionline );
								$bFound = 1;
								
								if( $runsilent == 0 )
								{
									printdebug "Keep: $unionline\n";
								}
							}
							else 
							{
								if( $runsilent == 0 )
								{
#									printdebug "NoMatch:$addline\n";
								}
							}
						}
					}
					
					if( $bFound == 0 )
					{
						if( $runsilent == 0 )
						{
							printdebug "Discard: $unionline\n";
						}
					}
				}
				
				# If didn't find a specific dlo section
				if( $bHadSpecific == 0 )
				{
					# If we weren't o the first file
					if( $listidx > 0 )
					{
						# Just keep all the previous union entries
						@unionlist = (@complist);
					}
				}
				else
				{
					# Otherwise, update index
					$listidx++;
				}
			}
			else # $union == 1
			{
				if( $runsilent == 0 )
				{
					printdebug "GET UNION\n";
				}
				
				# Go through each line in the script file
				foreach $fileline (@compfiletext)
				{
					# If compare file line matches specific DLO defaultproperty entry
					#(PackageName=T_CharacterSkins,ObjectName=Temp_IronSpirit_Shd,ObjectClass=class'Material'),
					if( $fileline =~ /\((.*)ObjectClass=class'(\w+)'\)/ ) 
					{
						$addline = "($1ObjectClass=class'$2')";
						$bHadSpecific = 1;
						
						$bFound = 0;
							
						# For each line in the union list
						foreach $unionline (@unionlist)
						{
							if( $runsilent == 0 )
							{
#								printdebug "Check: $unionline\n";
#								printdebug "Against: $addline\n";
							}
												
							# If the union line and the compared line match
							if( $unionline eq $addline )
							{
								# Mark as found (won't be added later since we want a single copy of each for union)
								$bFound = 1;
								
								if( $runsilent == 0 )
								{
									printdebug "Toss: $unionline\n";
								}
							}
						}
						
						# If we didn't find the addline in the union list already
						if( $bFound == 0 )
						{
							# ADD IT
							push( @unionlist, $addline );
							
							if( $runsilent == 0 )
							{
								printdebug "Keep: $unionline\n";
							}
						}
					}
				}	
			}
		}
		
		# Update index of file list so we don't add stuff as "First:" file all the time
		$listidx++;
	}
	
	# RESORT BY OBJECT TYPE #
	# For each line in the whole list
	# Take the item and put it into the proper list by pushing it onto the end of the list
	foreach $typeline (@unionlist)
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



	# Open previous contents of the output file
	unless( open( PREVOUT, $outputfile ) )
	{
		printdebug "Error: Couldn't open file $outputfile\n";
		next;
	}
	chomp( @prevouttext = <PREVOUT> );
	close( PREVOUT );

	# Open output file for writing
	open( OUTFILE, ">$outputfile" );

	if( $runsilent == 0 )
	{
		printdebug "\n\nOUTPUT UNION TO $outputfile...\n\n";
	}

	foreach $prevline (@prevouttext)
	{
		print OUTFILE "$prevline\n";
	}
	print OUTFILE "\n\nPUT IN:$topfile\n";
	
	if( $union == 1 )
	{
		print OUTFILE "UNION OF: $topline\n";
	}
	else
	{
		print OUTFILE "INTERSECTION OF: $topline\n";
	}

	# Print the union list to the output file
	$listidx = 0;
	foreach $unionline (@orderlist)
	{
		if( $listidx == 0 )
		{
			print OUTFILE "\tGeneralDLOList={(\n";
			print OUTFILE "\t\t$unionline";
		}
		else
		{
			print OUTFILE ",\n\t\t$unionline";
		}
		
		$listidx++;
			
		if( $runsilent == 0 )
		{
			printdebug "Union: $unionline\n";
		}
	}

	if( $listidx > 0 )
	{
		print OUTFILE "\n\t)}\n";
	}

	# Close the output file
	close( OUTFILE );

	if( $stripafter == 1 )
	{
		if( $runsilent == 0 )
		{
			printdebug "\n\nSTRIP ALL UNIONS AND REORDER...\n\n";
		}

		foreach $filename (@filelist)
		{
			$fullfilename = $root_dir.'\\'.$charpkgname.'\\Classes\\'.$filename.'.uc';
			# Open the file and grab the text
			open( COMPFILE, "$fullfilename" );
			chomp( @compfiletext = <COMPFILE> );
			close( COMPFILE );
			
			# Reopen same file for writing
			unless( open( LOGFILE, ">$fullfilename" ) )
			{
				# Failed to open file for writing
				printdebug "Could not open $filename for writing\n";
				next;
			}
			
			if( $runsilent == 0 )
			{
				printdebug "\nStrip union specifics from $filename\n\n";
			}
			
			# For each line in the file
			$listidx = 0;
			foreach $fileline (@compfiletext)
			{
				# If compare file line matches specific DLO defaultproperty entry
				#(PackageName=T_CharacterSkins,ObjectName=Temp_IronSpirit_Shd,ObjectClass=class'Material'),
				if( $fileline =~ /\((.*)ObjectClass=class'(\w+)'\)/ ) 
				{
					$addline = "($1ObjectClass=class'$2')";
					$bFound	 = 0;
				
					# For each line in the union list
					foreach $unionline (@unionlist)
					{
						if( $runsilent == 0 )
						{
							#printdebug "Try Compare:\n$unionline\n$addline\n";
						}
						
						# See if the info matches
						if( $unionline eq $addline )
						{
							# If so, mark as found
							$bFound = 1;
							
							if( $runsilent == 0 )
							{
								printdebug "Strip: $addline\n";						
							}
						}
					}
					
					# If the info was unique (specific to this class)
					if( $bFound == 0 )
					{
						# Print it back into the file
						if( $listidx == 0 )
						{
							print LOGFILE "\t\t$addline";
						}
						else
						{
							print LOGFILE ",\n\t\t$addline";
						}
						
						# Update the index
						$listidx++;
						
						if( $runsilent == 0 )
						{
							printdebug "Unique: $addline\n";
						}
					}
				}
				else
				{
					# Otherwise, just print it back to file
					if( $listidx > 0 )
					{
						print LOGFILE "\n";
						$listidx = 0;
					}
					
					print LOGFILE "$fileline\n";
				}
			}
			
			close( LOGFILE );
		}
	}
}

printdebug "\n\nDONE...\n";

close( DEBUGFILE );

while( 1 )
{
}
