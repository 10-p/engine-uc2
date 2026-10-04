#!/usr/bin/perl.exe
#author: superville

# Pulls section names off vars in loc files
use File::Find;

# File location vars
$root_dir	='C:\\UC2004Code\\';
$systemdir	= $root_dir.'System\\';
$locdir		= $systemdir.'Localization\\';
#$locdir	= $systemdir.'Test\\';
$debugfile	= $systemdir.'LocDebug.log';

# Debug vars
$runsilent	= 0;

sub printdebug
{
	print $_[0];
	print DEBUGFILE $_[0];
}

############################################################
# stripfile
sub stripfile
{
	# Open the file for reading
	unless( open( FILEHANDLE, $File::Find::name ) )
	{
		# print out warning if couldn't be opened
		printdebug "Error: Couldn't open file $File::Find::name for READING\n";
		next;
	}
	
	# Get all lines from the file
	chomp( @filetext = <FILEHANDLE> );
	close( FILEHANDLE );

	# Open the file for writing
	unless( open( FILEHANDLE, ">$File::Find::name" ) )
	{
		# print out warning if couldn't be opened
		printdebug "Error: Couldn't open file $File::Find::name for WRITING\n";
		next;
	}
	
	if( $runsilent == 0 )
	{
		printdebug "FileName: $File::Find::name\n";
	}
	
	$cursection = "";
	
	# For each line of the file
	foreach $fileline (@filetext)
	{
		if( $fileline =~ /^\[(\w+)\]/ )
		{
			$cursection = $1;
			
			if( $runsilent == 0 )
			{
				printdebug "Section: $cursection\n";
			}
		}
		
		# If we match the pattern of a var with a section name
		# UCPickup_Adrenaline_Super.PickupMessage="Super Adrenaline"
		# Arena_PanelMain.ArenaRulesItemBots.Items[7]=(Item="7")
		if( $fileline =~ /^$cursection\.(.*)=(.*)\s*$/ )
		{
			$printline = "$1=$2\n";
			
			# Strip the section name and print to the file
			print FILEHANDLE "$printline";
			
			if( $runsilent == 0 )
			{
				printdebug "Strip: $fileline\nTo:$printline\n";
			}
		}
		else
		{
			# Otherwise, just print it out to the file
			print FILEHANDLE "$fileline\n";
			
			if( $runsilent )
			{
				printdebug "Leave: $fileline\n";
			}
		}
	}
	
	# Close the file
	close( FILEHANDLE );
}

#############################################################
# main

open( DEBUGFILE, ">$debugfile" );

# Find each file in the loc directory structure
# and call &stripfile with each file name
find( \&stripfile, ($locdir) );

close( DEBUGFILE );

printdebug "\n\nDONE...\n";

while( 1 )
{
}
