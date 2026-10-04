#!/usr/bin/perl.exe
#author: superville

# File location vars
$root_dir	='C:\\UC2004Code';
$systemdir	= $root_dir.'\\System';
$logfile	= $systemdir.'\\AvarisDLO.log';
$outputfile	= $systemdir.'\\AvarisDLO.log';

# Block creation vars
$listname1	= 'GeneralDLOList';
$listname2	= 'SpecificDLOList';

$counter1 = 0;
$counter2 = 0;

@newlist1 = ();
@newlist2 = ();


# Open the log file and grab all the lines then close
open( LOGFILE, $logfile );
chomp( @logfiletext = <LOGFILE> );
close( LOGFILE );

# Open output file for writing
open( LOGFILE, ">$outputfile" );

# Go through each line in the log file
foreach $line (@logfiletext)
{
	#Match line to one of two cases
	#GeneralDLOList(1154)=(PackageName="X",ObjectName="Y",ObjectClass=class'Z')
	#SpecificDLOList(1154)=(PackageName="X",ObjectName="Y",ObjectClass=class'Z')
	if( $line =~ /GeneralDLOList\((\d+)\)=(.*)/ )
	{
		#If match, insert updated index line into list
		@newlist1 = (@newlist1, "\tGeneralDLOList($counter1)=$2\n");
		$counter1++;
	}
	if( $line =~ /SpecificDLOList\((\d+)\)=(.*)/ )
	{
		@newlist2 = (@newlist2, "\tSpecificDLOList($counter2)=$2\n");
		$counter2++;
	}
}

#Print each list in order
foreach $line (@newlist1)
{
	print LOGFILE "$line";
}
foreach $line (@newlist2)
{
	print LOGFILE "$line";
}

close( LOGFILE );

print "DONE...\n";

while( 1 )
{
}