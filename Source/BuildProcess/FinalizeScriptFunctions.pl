#!/usr/bin/perl.exe
#author: superville

$logfilename		= 'UCC.log';
#$logfilename		= 'Test.log';

$gamedirectory		= 'C:\\UC2004Code';
$systemdirectory	= 'C:\\UC2004Code\\System\\';
$clientspec			= 'Judas_XBox';
$cspecpath			= 'C:\\UC2004Code';
#NOTES: For P4 functions (ie label) to work properly this owner must match the owner name of the clientspec
$owner				= 'Stephen_Superville';
$depot				= '//depot/UC2004/';

# Debug vars
$runsilent			= 0;
$debugfile			= $systemdirectory.'AvarisDebug.log';

sub printdebug
{
	print $_[0];
	print DEBUGFILE $_[0];
}

#############################################################
# main
open( DEBUGFILE, ">$debugfile" );


open( UCCLOG, "$systemdirectory\\$logfilename" );
chomp( @ucclogfiletext = <UCCLOG> );
close( UCCLOG );

foreach $logline (@ucclogfiletext)
{
	$path = "";
	
	# Gather package/class/state/function info
	# Format1: Log: - Package.Class.State.Function
	# Format2: Log: - Package.Class.Function
	# If pattern has a state in it, get info
	if( $logline =~ /^Log: - (\w+)\.(\w+)\.(\w+)\.(\w+)/ ) 
	{
		$package	= $1;
		$class		= $2;
		$state		= $3;
		$function	= $4;
	}
	elsif( $logline =~ /^Log: - (\w+)\.(\w+)\.(\w+)/ ) 
	{
		# Otherwise, if pattern has just class/func, get info
		$package	= $1;
		$class		= $2;
		$state		= undef;
		$function	= $3;
	}
	else 
	{
		# Otherwise skip the line b/c it has nothing we want
		next;
	}
	
	# Build path to script file
	$path = "$gamedirectory\\$package\\Classes\\$class.uc";
	
	#Check out of P4
	open( CHECKOUT, "p4 -c \"$clientspec\" edit \"$path\" 2>&1 |");
	@edit = <CHECKOUT>;
	close(CHECKOUT);
	
	# Open file for edit
	open( SCRIPTFILE, "$path" );
	chomp( @scriptfiletext = <SCRIPTFILE> );
	close( SCRIPTFILE );
	
	open( SCRIPTFILE, ">$path" );
	
	$curstate	= undef;
	$brackets	= 0;
	$checkstate = 0;
	
	if( $runsilent == 0 )
	{
		printdebug "\nTrying to find function $package.$class.$state.$function...\n";
	}
	
	$bFound = 0;
	
	foreach $scriptline (@scriptfiletext) 
	{
		# If a state name is defined
		if( defined($state) ) 
		{
			# Mark curstate if we found proper state and write the line unaltered
			if( $scriptline =~ /(.*?)(\bstate\b)(\s*)($state)(.+|.*?)/i ) 
			{
				$curstate = $state;
				print SCRIPTFILE "$scriptline\n";
				next;
			}
			elsif( $curstate ne $state ) 
			{
				# Otherwise, skip lines until we find correct state
				print SCRIPTFILE "$scriptline\n";
				next;
			}
		}
		else 
		{
			# Need to count parens to make sure we aren't in a state
			if( $scriptline =~ /(.*?)(\bstate\b)(\s*)(.*)(.+|.*?)/i ) 
			{
				$curstate = $4;
			}
			
			# If we are currently inside a state
			if( defined($curstate) ) 
			{
				# Count open brackets in this line
				$idx = index( $scriptline, "{", 0 );
				while( $idx > -1 ) 
				{
					$checkstate = 1;	# Mark flag to check for exit if we've found at least 1 bracket
					$brackets++;
					$idx = index( $scriptline, "{", $idx + 1 );
				}
				
				# Count close brackets in this line
				$idx = index( $scriptline, "}", 0 );
				while( $idx > -1 ) 
				{
					$brackets--;
					$idx = index( $scriptline, "}", $idx + 1 );
				}
			}		
		}
		
		# If we aren't looking for a state or we are in the state we are looking for and
		if( $bFound == 0 && ((!defined($state) && !defined($curstate)) || ($curstate eq $state)) ) 
		{
			# If line matches pattern (case insensitive)
			#function int GetIntStatByName(name StatName) { return -1; }
			if( $scriptline =~ /^(.*?)(\bfunction|event|delegate\b)(\s+)(.*?)\b$function\b(\s*?)\((.*)\)(.*)/i ) 
			{
				$start		= $1;
				$func		= "$2$3";
				$rettype	= $4;
				$param		= "$5($6)";	
				$rest		= $7;
				$fixed		= "$start"."final $func$rettype$function$param$rest";
				
				printdebug "1: $1\n2: $2\n3: $3\n4: $4\n5: $5\n6: $6\n7: $7\n\n";
				
				$bFound		= 1;
				
				print SCRIPTFILE "$fixed\n";
				
				if( $runsilent == 0 )
				{
					printdebug "Edit: $path\nLocation: $package.$class.$state.$function\nLine: $scriptline\nFixed: $fixed\n\n";
				}
			}
			else 
			{
				print SCRIPTFILE "$scriptline\n";
			}
		}
		else {
			print SCRIPTFILE "$scriptline\n";
		}
		
		# If we should be looking for exit and brackets has returned to zero
		if( $checkstate > 0 && $brackets == 0 ) {
			# Leave current state
			$curstate	= "";
			$checkstate = 0;
		}
	}	
	
	close( SCRIPTFILE );
	
}

close( DEBUGFILE );
