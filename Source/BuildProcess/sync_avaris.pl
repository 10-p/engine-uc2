#!/usr/bin/perl.exe

#############################################################
# directories/programs
$root_dir                               = 'f:\\Build_UC2';
$systemdir				= $root_dir.'\\System';
$ucc					= $root_dir.'\\System\\Ucc.exe';
$uccmasterlog				= $root_dir.'\\System\\Ucc.log';
$solution				= $root_dir.'\\Avaris.sln';		                 # Solution to compile in devenv
$xboxprojectfile			= $root_dir.'\\XboxLaunch\\Src\\XboxLaunch.vcproj';
$cspecpath				= 'f:\\Build_UC2';
$xbepath				= $root_dir.'\\XboxLaunch\\System\\default.xbe';
$linflag 				= 'linflag.txt';
$checkoutfilelistfile			= $root_dir.'\\BuildFileList.txt';			 # List of files to delete and build - included in P4
$scriptfilelistfile		= $root_dir.'\\ScriptFileList.txt';
$buildini				= $root_dir.'\\System\\Build.ini';
$compilelogdir				= $root_dir.'\\Logs\\';	                                 # Dir to dump log files into
$versionfile				= $root_dir.'\\System\\Localization\\int\\Core.int';

$last_script_pkg        = "UCMenu.u";                                            # last script package to be compiled, used to check if script build passed

#NOTES: Blat must be installed on the system running this perl script
#		To Install: Put Blat.exe in some folder and run:
#		-install <SMTP_SERVER> <SENDING EMAIL ADDRESS>
#		ie -install mail.scionstudios.com steve@scionstudios.com
$blat					= 'c:\\windows\\system32\\blat.exe';
$devenv					= 'c:\\program files\\microsoft visual studio .net 2003\\common7\\ide\\devenv.exe';

#Flags and files residing on the server
$output_base_dir		= '\\\\Build-Server\\BuildFlags\\';
#$output_base_dir		= 'C:\\Temp\\';
$buildflag				= $output_base_dir.'makebuild.txt';
$serverlogfile			= $output_base_dir.'UC_logs\\compile';
$linsavelogfile			= $output_base_dir.'UC_logs\\runlinsave.txt';
$servermasterlogfile	= $output_base_dir.'UC_logs\\uccmaster.log';
$serverversions			= $output_base_dir.'UC_versions\\';
$setupdir				= $output_base_dir.'Avaris-Full';

#$clientspec                             = 'Scion_Ray-Pentium4_Code';
#$clientspec			= 'UC2_Build_Machine';
$clientspec				= 'Build-Server_UC2';
#NOTES: For P4 functions (ie label) to work properly this owner must match the owner name of the clientspec
$owner					= 'warren_schultz';
$depot					= '//depot/UC2004/';
$setupini				= 'setupuc2004_full';

#TODO: Replace fromaddr with buildmachine address?
$announceaddr			= 'avaris-external@epicgames.com';
#$announceaddr			= 'avaris-coders@epicgames.com';
$failaddr				= 'avaris-coders@epicgames.com';
$fromaddr		        = 'BuildMachine@epicgames.com';
$failsubject			= "UC2004 BUILD FAILED";
$failmastersubject		= "UCC MASTER FAILED";
$mailserver             = "mercury.epicgames.net";

$dowipexbox			= 1;                                 # remove all XboxTextures\*.*, XboxMaps\*.*, etc.
$dosync				= 1;
$docheckout			= 1;                                 # perform actual file checkout? debug
$dobuild			= 1;
$doassets			= 0;
$domail				= 1;                                 # send emails? debug
$dosubmit			= 1;
$dorevert			= 1;
$waitforsubmit		= 1;
$incredibuild		= 1;

$dolinsave			= 0;
$buildpc			= 1;
$builduc			= 1;
$buildxbox			= 1;  # regular 'xbox release' version
$buildxboxls		= 0;  # linear save version
$buildxboxll		= 1;  # linear load version
$buildxboxfinal     = 1;  # whether to build the 'final release' version as well

$scriptcompileoptions		= ' -nobind -noh';
$BuildCfg                   = 'Release';
$BuildPrj                   = '!BuildEngine';
$XBuildCfg                  = 'XBox Release';
$XBuildCfgLL                = 'XBox Release LL';
$XBuildCfgLS		       	= 'XBox Release LS';
$XBuildCfgFinal	       	    = 'XBox Release LL Final';
$XBuildPrj                  = 'XBoxLaunch';

# changes categories, match each file to a specific category
%file_cat = ("uc"      =>    "Code",
             "huc"     =>    "Code",
             "cpp"     =>    "Code",
             "h"       =>    "Code",
             "vcproj"  =>    "Code",
             "sln"     =>    "Code",
             "ini"     =>    "Code",
             "int"     =>    "Code",
             "psh"     =>    "Code",
             "vsh"     =>    "Code",
             "xpu"     =>    "Code",
             "xvu"     =>    "Code",
             "pl"      =>    "Code",
             "bat"     =>    "Code",
             "ukx"     =>    "Art",
             "usx"     =>    "Art",
             "utx"     =>    "Art",
             "unr"     =>    "Art",
             "dds"     =>    "Art",
             "psd"     =>    "Art",
             "tga"     =>    "Art",
             "bmp"     =>    "Art",
             "pcx"     =>    "Art",
             "ase"     =>    "Art",
             "max"     =>    "Art",
             "raw"     =>    "Art",
             "xmv"     =>    "Art",
             "uax"     =>    "Sound",
             "xsb"     =>    "Sound",
             "xwb"     =>    "Sound",
             "xap"     =>    "Sound",
             "wav"     =>    "Sound",
             "wma"     =>    "Sound",
             "ogg"     =>    "Sound",
             "doc"     =>    "Design",
             "xls"     =>    "Design",
);
%changelist;
$default_change                = 0;       # did default.ini change?
$user_change                   = 0;       # did defaultuser.ini change?

@checkedoutfiles;
@scriptfiles;

# clock/unclock variables
$timer      = 0;
$timer_op   = "";

# see if any headers were updated
sub check_header_update {
   @updated = ();
   $header_cnt = 0;
   foreach $line (@_)
   {
      # check to see if we exported a header
      if ($line =~ /Autogenerating C\+\+ header/)
      {
         if ($line =~ /(\w+)Classes.h/)
         {
            push(@updated,$1);
            $header_cnt++;
         }
      }
      # check to see if the header was identical
      elsif ($line =~ /(\w+)Classes\.h because identical/)
      {
         $testpkg = $1;
         $index = 0;
         while ($index <= $#updated)
         {
            if ($updated[$index] eq $testpkg)
            {
               $updated[$index] = "";
               $header_cnt--;
            }
            $index++;
         }
      }
   }
   return $header_cnt;

}

#############################################################

# elapsed_time
# - returns a string describing the elapsed time between to time() calls
# - $_[0] should be the difference between the time() calls
sub elapsed_time {
   $seconds = $_[0];
   $minutes = $seconds / 60;
   $hours = int($minutes / 60);
   $minutes = int($minutes - ($hours * 60));
   $seconds = int($seconds - ($minutes * 60));
   return $hours." hrs ".$minutes." min ".$seconds." sec";
}

# clock
# - simple timer, call in conjunction with unclock to get a rough estimate of elapsed time
sub clock {
   if ($timer_op ne "") {
      print "Warn: new clock interrupting previous clock for $timer_op!\n";
   }
   $timer_op = $_[0];
   $timer = time;
   print "Starting [$timer_op] at ".localtime($timer)."\n";
}

# unclock
# - calculates the elapsed time since clock was called
sub unclock {
   if ($_[0] ne $timer_op) {
      print "Warn: unclock mismatch, $_[0] vs. $timer_op!\n";
   }
   else
   {
      print "Finished [$timer_op], elapsed: ".elapsed_time(time-$timer)."\n";
   }
   $timer_op = "";
}

sub setversion
{
	open( VERSIONFILE, $versionfile );
	chomp( @versionfiletext = <VERSIONFILE> );
	close( VERSIONFILE );

	open( VERSIONFILE, ">$versionfile" );

	# Opens up version .int file and updates the 4th version number
	foreach $line (@versionfiletext) {
		if( $line =~ /^BuildVersion=\"(\d+)\.(\d+)\.(\d+).(\d)(\d)(\d)(\d)\"/ ) {
			$v1 = $7 + 1;
			$v2 = $6;
			$v3 = $5;
			$v4 = $4;

			if( $v1 == 10 ) {
				$v1 = 0;
				$v2++;
			}

			if( $v2 == 10 ) {
				$v2 = 0;
				$v3++;
			}

			if( $v3 == 10 ) {
				$v3 = 0;
				$v4++;
			}

			if( $v4 == 10 ) {
				$v1 = "";
				$v2 = "";
				$v3 = "";
				$v4 = "ERROR: OVER FLOW VERSION NUMBERING"
			}

			$vernum  = "$v4$v3$v2$v1";
			$version = "$1.$2.$3.$vernum";

			print VERSIONFILE "BuildVersion=\"$version\"\n";
		}
		else {
			print VERSIONFILE "$line\n";
		}
	}

	close( VERSIONFILE );

	$announcesubject = "New version in P4: $version";
	$announcemastersubject = "New installer available: $version";
	$compilelog = $compilelogdir.$version.".log";
	$unrealeddllcompilelog = $compilelogdir.$version.".unrealeddll.log";
	$xboxcompilelog = $compilelogdir.$version.".xbox.log";
	$assetcompilelog = $compilelogdir.$version.".asset";
	$assetconsolelog = "$compilelogdir.$version.assetconsole.log";
}


#############################################################
sub getcheckoutlist
{
	open( CHECKOUTFILELIST, $checkoutfilelistfile );
	chomp( @checkoutfiles = <CHECKOUTFILELIST> );
	close( CHECKOUTFILELIST );

	open( CHECKOUTFILELIST, $scriptfilelistfile );
	chomp( @checkoutscriptfiles = <CHECKOUTFILELIST> );
	close( CHECKOUTFILELIST );
}

#############################################################
sub revertall
{
	if ( $docheckout == 1 && $dorevert == 1 )
	{
		system("p4 -c \"$clientspec\" revert -c default \/\/\.\.\.");
	}

	revertscript();
}

sub revertscript
{
	foreach $file (@scriptfiles)
	{
		system("p4 -c \"$clientspec\" revert -c default $file");
	}
}

#############################################################
sub checkoutall
{
	foreach $file (@checkoutfiles)
	{
		open( EDIT, "p4 -c \"$clientspec\" edit \"$cspecpath\\$file\" 2>&1 |");
		@edit = <EDIT>;
		close(EDIT);

		if( ! ($edit[0] =~ m/\-\ opened\ for\ edit$/) ) {
			if ( $domail == 1 ) {
				open( SMTP, "|\"$blat\" - -t $failaddr -f $fromaddr -s \"$failsubject\"" );
				print SMTP "Avaris build failure: Checkout failed.\n\n$edit[0]";
				close( SMTP );
			}
			print "Avaris build failure: Checkout failed.\n\n$edit[0]";
			return 0;
		}
		else
		{
			foreach $line (@edit) {
				if ($line =~ /(.+)\#/) {
					# add to checked out list
					push @checkedoutfiles,$1;
				}
			}
		}
	}
	# lock all the files opened for edit
	system("p4 -c $clientspec lock");

	return 1;
}

sub checkoutufiles
{
	foreach $file (@checkoutscriptfiles)
	{
		open( EDIT, "p4 -c \"$clientspec\" edit \"$cspecpath\\$file\" 2>&1 |");
		@edit = <EDIT>;
		close(EDIT);

		if( ! ($edit[0] =~ m/\-\ opened\ for\ edit$/) ) {
			if ( $domail == 1 ) {
				open( SMTP, "|\"$blat\" - -t $failaddr -f $fromaddr -s \"$failsubject\"" );
				print SMTP "Avaris build failure: Checkout failed.\n\n$edit[0]";
				close( SMTP );
			}
			print "Avaris build failure: Checkout failed.\n\n$edit[0]";
			return 0;
		}
		else
		{
			foreach $line (@edit) {
				if ($line =~ /(.+)\#/) {
					# add to checked out list
					push @scriptfiles,$1;
				}
			}
		}
	}
	# lock all the files opened for edit
	system("p4 -c $clientspec lock");

	return 1;
}

#############################################################
sub updatebuildini
{
	open( BUILDINI, ">$buildini" );
	print BUILDINI "[BuildVersion]\n";
	print BUILDINI "Label=$version\n";
	close(BUILDINI);
}

#############################################################
sub submitall
{
	# unlock the files for check in
	system("p4 -c $clientspec unlock");

	open( SUBMITCMD, "|p4 -c \"$clientspec\" submit -i" );

	print SUBMITCMD "Change:\tnew\n";
	print SUBMITCMD "Client:\t$clientspec\n";
	print SUBMITCMD "Status:\tnew\n";
	print SUBMITCMD "Description:\t$version\n";
	print SUBMITCMD "Files:\n";

	foreach $file (@checkedoutfiles)
	{
		#$f = $file;
		#$f =~ s#\\#/#g;	 #Replace every instance of backslash with forwardslash
		#print SUBMITCMD "\t$depot$f\t#edit\n";
		print SUBMITCMD "\t$file#edit\n";
	}

	return close(SUBMITCMD);
}

sub junk {
}

#############################################################
sub label
{
	# create the version label
	open( LABELCMD, "|p4 -c \"$clientspec\" label -i" );
	print LABELCMD "Label:\t$version\n";
	print LABELCMD "Owner:\t$owner\n";
	print LABELCMD "Description:\n\t$version\n";
	print LABELCMD "Options:\tunlocked\n";
	print LABELCMD "View:\n\t\"$depot...\"\n";
	close(LABELCMD);
}

#############################################################
# get changes since last build, put them in @changelist
sub getchanges
{
	# see how many builds to go back
	$getpreviousbuild = $_[0];

	@buildflagtext = "Request";

	# grab the last build change
	$foundlastbuild = 0;
	open( CHANGES, "p4 changes -c \"$clientspec\" \"$depot...\"|" );
	foreach $line (<CHANGES>)
	{
		if ($line =~ /^Change ([0-9]+)/)
		{
			$lastbuildchange = $1;
			# make sure this is a build version change
			if ($line =~ /([0-9][0-9])\.([0-9][0-9])\.([0-9][0-9])\.([0-9][0-9][0-9][0-9])/)
			{
				print "Last known build version: $1.$2.$3.$4\n";
				$lastbuild = $line;
				# if we've gone back far enough...
				if ($getpreviousbuild <= $foundlastbuild)
				{
					# stop searching
					$foundlastbuild++;
					last;
				}
				else
				{
					$foundlastbuild++;
				}
			}
		}
	}
	close(CHANGES);
	# make sure $lastbuildchange is valid
	if ($foundlastbuild == 0)
	{
		print "ERROR: failed to find last build version!\n";
		return;
	}
	# get a detailed description so that we can get the full date/time
	open ( DESCRIBE, "p4 describe $lastbuildchange|");
	@description = <DESCRIBE>;
	close(DeSCRIBE);
	# ugh? parse the date/time, ph34r my l33t regexp skills!
	$description[0] =~ /([0-9][0-9][0-9][0-9])\/([0-9][0-9])\/([0-9][0-9]) ([0-9][0-9]):([0-9][0-9]):([0-9][0-9])/;
	$changedate = "$1\/$2\/$3:$4:$5:".($6+1);  # inc by one to avoid the last build version

	# open up the changes for this depot
	open (CHANGES,"p4 changes \"$depot...\"|");
	foreach $change (<CHANGES>)
	{
		# check to see if we've reached the last build version
		if ($change =~ $lastbuild)
		{
			# skip to the end
			last;
		}
		else
		{
			# grab the change number
			if ($change =~ /Change ([0-9]+) on/)
			{
				$change_num = $1;
				# list of categories this change affects
				%belongs_in = ();
				# parse the description of this change
				open (DESCRIBE,"p4 describe $change_num|");
				$reading_desc = 1;    # are we reading the description of this change?
				$desc = "";
				foreach $line (<DESCRIBE>)
				{
					# if we're reading the desc, check to see if this line is the end
					if ($reading_desc == 1)
					{
						if ($line =~ /Affected files/)
						{
							# we've reached the end of the changes
							$reading_desc = 0;
						}
						else
						{
							# append to the list of changes, only if it contains something other than whitespace
							if ($line =~ /\w/)
							{
								$desc .= $line;
							}
						}
					}
					else
					{
						# check to see if this line contains a file ref
						if ($line =~ /\.(\w+)\#/)
						{
							$category = $file_cat{lc($1)};
							if ($category eq "")
							{
								$category = "Misc";
							}

							# mark this category as one we belong in
							$belongs_in{$category} += 1;    # some arbitrary value
							# check if it's a critical change
							if ($default_change == 0 && $line =~ /Default\.ini/)
							{
								$default_change = 1;
							}
							elsif ($user_change == 0 && $line =~ /DefaultUser\.ini/)
							{
								$user_change = 1;
							}
						}
					}
				}
				close (DESCRIBE);
				# for each category it belongs to, add the desc
				$best_cnt = 0;
				$best_cat = "Misc";
				foreach $cat (keys(%belongs_in))
				{
					if ($belongs_in{$cat} > $best_cnt)
					{
						$best_cnt = $belongs_in{$cat};
						$best_cat = $cat;
					}
				}
				$changelist{$best_cat} .= $desc."\n";
			}
		}
	}
	# all done!
	close (CHANGES);
}

#############################################################
# return 1 if change was from $depot, 0 if not
sub belongstodepot {
   $changenum = $_[0];
   $result = 0;
   open(DESCRIBE, "p4 describe $changenum|");
   foreach $line (<DESCRIBE>) {
      if ($line =~ /$depot/) {
         $result = 1;
         last;
      }
   }
   close(DESCRIBE);
   return $result;
}

#############################################################
sub announce
{
   if ($domail == 1) {
      $attempts = 0;
      $success = 0;
      while ($success == 0 && $attempts < 10) {
         print "Sending mail...\n";
         open( SMTP, "|\"$blat\" - -t $announceaddr -f $fromaddr -s \"$announcesubject\" -server $mailserver > output.txt" );
         print SMTP '@'."$version\n".$buildflagtext[0]."\n\nChanges since last version:\n\n";
         # if there was a critical change, mark in the build notes
         if ($default_change == 1) {
            print SMTP "NOTE: Please delete Avaris.ini, otherwise the game may not run correctly!\n\n";
         }
         if ($user_change == 1) {
            print SMTP "NOTE: Please delete User.ini, otherwise the game may not run correctly!\n\n";
         }

         if ($changelist{"Code"} ne "") {
            print SMTP "CODE CHANGES:\n\n";
            print SMTP $changelist{"Code"}."\n";
         }

         if ($changelist{"Art"} ne "") {
            print SMTP "ART CHANGES:\n\n";
            print SMTP $changelist{"Art"}."\n";
         }

         if ($changelist{"Design"} ne "") {
            print SMTP "DESIGN CHANGES:\n\n";
            print SMTP $changelist{"Design"}."\n";
         }

         if ($changelist{"Sound"} ne "") {
            print SMTP "SOUND CHANGES:\n\n";
            print SMTP $changelist{"Sound"}."\n";
         }

         if ($changelist{"Misc"} ne "") {
            print SMTP "MISC CHANGES:\n\n";
            print SMTP $changelist{"Misc"}."\n";
         }

         close(SMTP);

         # make sure we were able to send the mail
         print "Checking for success...\n";
         if (open(OUTPUT,"output.txt")) {
            $errors = 0;
            foreach $line (<OUTPUT>) {
               if ($line =~ m/Error:/i) {
                  chomp($line);
                  print "- found error [".$line."]\n";
                  $errors++;
               }
            }
            $success = $errors == 0 ? 1 : 0;
            close(OUTPUT);
         }
         else
         {
            print "- failed to capture output, assuming success.\n";
            $sucess = 0;
         }

         # if we failed, delay a few seconds before attempting again
         if ($success == 0) {
            sleep(15);
         }
         $attempts++;
      }
      # notify that we weren't able to send the mail
      if ($success == 0) {
         print "Failed to send mail after $attempts attempts!\n";
      }
   }
}

sub bypassfailure
{
	my $reason = $_[0];
	my $message = $_[1];

	announcegeneralfailure($reason);

	print "$message\n Type 1 and press enter to bypass this error and continue with the build.  Type anything else and press enter to begin reverting.";
	my $result = <STDIN>;
	if ( $result != 1 )
	{
		return 0;
	}

	return 1;
}

#############################################################
# look through the file for failures and errors.  if find none, succeeded.
# incredibuilds fail like this:
# 1 build system error(s), 20 build system warning(s)
sub checkbuildsuccess
{
	local ($locallogfile) = $_[0];
	my $successflag = 1;

	open( COMPILELOG, $locallogfile );

	while( <COMPILELOG> )
	{
		chomp;

		if ( /.*([0-9]+)\ failed/ )
		{
			if( $1 != 0 )
			{
				close(COMPILELOG);
				$successflag = 0;
			}
		}

		if ( /.*([0-9]+)\ error\(s\)/ )
		{
			if ( $1 != 0 )
			{
				close (COMPILELOG);
				$successflag = 0;
			}
		}

		if ( /.*([0-9]+)\ build system error\(s\)/ )
		{
			if ( $1 != 0 )
			{
				close (COMPILELOG);
				$successflag = 0;
			}
		}

	}
	close(COMPILELOG);

	if ($incredibuild && $successflag == 0)
	  {
	    announceincredpause();
	    print "You have the opportunity to recompile this library manually using VC.NET.  If you recompile, and want the build process to continue as if there had been no errors, type 1 and press enter.  If you want to stop the build process and revert all files, type anything else and press enter.\n";
	    my $result = <STDIN>;
	    if ( $result != 1 ) {
  	        return 0;
	      } else {
		return 1;
	      }
	  }

	return $successflag;
}

#############################################################
sub checklinsuccess
{
	my $result = 1;

	print "\nVerifying successful LIN generation...\n";
	system("echo Verifying successful LIN generation... >> $linsavelogfile");

	# verify that all map .lin files were generated correctly
	open(FILELIST, "dir /b $root_dir\\XBoxMaps\\*.unr|");
	while (<FILELIST>)
	{
		# ignore entry, since it's included in script.lin
		if ( !($_ =~ /Entry.unr/) )
		{
			#remove the trailing newline from the filename
			chomp;

			#replace the .unr extension with .lin extension
			s/.unr/.lin/i;
			$linfile = $root_dir."\\XboxMaps\\$_";
			if ( !(-e $linfile) )
			{
				print "ERROR: $_ wasn't generated!!\n";
				system("echo ERROR: $_ wasn't generated!! >> $linsavelogfile");
				$result = 0;
			}
		}
	}
	close(FILELIST);

	# check for the script.lin - assume that it hasn't been moved into the system
	# directory just yet
	$linfile = $root_dir."\\XboxMaps\\script.lin";
	if ( !(-e $linfile) )
	{
		print "ERROR: script.lin wasn't generated!! \n";
		system("echo ERROR: script.lin wasn't generated!! >> $linsavelogfile");
		$result = 0;
	}


	return $result;
}


#############################################################
sub announcefailure
{
	local ($localpart) = $_[0];
	local ($localfile) = $_[1];

	if ( $domail == 1 )
	{
		open( SMTP, "|\"$blat\" - -t $failaddr -f $fromaddr -s \"$failsubject\" -server $mailserver" );
		print SMTP "Avaris build failure: $localpart compile failed.\n\nSee $localfile for the build log.";
		close( SMTP );
	}
	print "Avaris build failure: $localpart compile failed.\n\nSee $localfile for the build log.";
}

sub announcegeneralfailure
{
	my $reason = $_[0];

	if ( $domail == 1 )
	{
		open( SMTP, "|\"$blat\" - -t $failaddr -f $fromaddr -s \"$failsubject\" -server $mailserver" );
		print SMTP "Avaris build failure: $reason.\n\nBuild has been paused.";
		close( SMTP );
	}

	print "Avaris build failure: $reason.\n\nBuild has been paused.";
}

sub announcesubmitfailure
{
	local ($localpart) = $_[0];
	if ( $domail == 1 )
	{
		open( SMTP, "|\"$blat\" - -t $failaddr -f $fromaddr -s \"$failsubject\" -server $mailserver" );
		print SMTP "Avaris build failure: Submit failed.\n";
		close( SMTP );
	}
	print "Avaris build failure: Submit failed.\n";
}

sub announceincredpause
{
	local ($localpart) = $_[0];
	if ( $domail == 1 )
	{
		open( SMTP, "|\"$blat\" - -t $failaddr -f $fromaddr -s \"$failsubject\" -server $mailserver" );
		print SMTP "Avaris Incredibuild has a problem! Build paused!\n";
		close( SMTP );
	}
	print "Avaris Incredibuild has a problem! Build paused!\n";
}

#############################################################
sub announcemasterfailure
{
   if ( $domail == 1 ) {
    open( SMTP, "|\"$blat\" - -t $failaddr -f $fromaddr -s \"$failmastersubject\" -server $mailserver" );
    print SMTP "Avaris ucc master failure.  Version in P4 still valid.\n\nSee $servermasterlogfile for the ucc master log.";
    close( SMTP );
   }
   print "Avaris ucc master failure.  Version in P4 still valid.\n\nSee $servermasterlogfile for the ucc master log.";
}

sub announcexboxfailure {
   if ($domail == 1) {
      open( SMTP, "|\"$blat\" - -t avaris-coders\@epicgames.com -f $fromaddr -s \"Build failed - Can't Reach Build Xbox\" -server $mailserver");
      print SMTP "Failed to connect to Xbox, MAKE SURE IT IS TURNED ON!\n";
      close( SMTP );
   }
   print("ERROR: unable to connect to xbox!\n");
}

#############################################################
sub announcemaster
{
   if ( $domail == 1 ) {
    open( SMTP, "|\"$blat\" - -t $announceaddr -f $fromaddr -s \"$announcemastersubject\" -server $mailserver" );
    print SMTP "$serverversions\\$version";
    close( SMTP );
   }
   print "$serverversions\\$version";
}

#############################################################
sub uccmaster
{
   open( UCCMASTERLOG, "$ucc master $setupini|" );
   while( <UCCMASTERLOG> )
   {
          chomp;
          if( /^Exiting\ due\ to\ error/ )
          {
                  system("copy $uccmasterlog $servermasterlogfile /y");
                  announcemasterfailure();
                  return 0;
          }
          elsif( /^appError/ )
          {
                  system("copy $uccmasterlog $servermasterlogfile /y");
                  announcemasterfailure();
                  return 0;
          }
   }

   system("mkdir $serverversions\\$version");
   system("xcopy $setupdir\\*.* $serverversions\\$version /s/e");

   announcemaster();
   return 1;
}

#############################################################

sub waitforlinsave
{
	print "Waiting for xbox linear save process...\n";
	system("echo Waiting for xbox linear save process... >> $linsavelogfile");
	while(1)
	{
		sleep(10);

		# parse result of xbdir for file not found
		open( LINTEST, "xbdir /b xd:\\$linflag|" );
		my $result = <LINTEST>;
		if ( $result =~ /File Not Found/ )
		{
			print "Xbox linear save process complete...\n";
			system("echo Xbox linear save process complete... >> $linsavelogfile");
			close ( LINTEST );
			return 1;
		}
#		else
#		{
#			if ( $result =~ // )
#			{
#				print "Xbox died...\n";
#				system("echo Xbox died...\n >> $linsavelogfile");
#				close(LINTEST);
#				return 0;
#			}
#		}
		close ( LINTEST );
		# if it's not there, wait
	}
}


#############################################################
sub dobuild
{
	$build_time = time;

	if ( $dowipexbox == 1 )
	{
	  clock "XBOXDELETE";
	  print "Deleting all Xbox* directories off of the Xbox.\n";
	  system("xbdel /f xE:\\Avaris\\XboxAnimations\\*.*");
	  system("xbdel /f xE:\\Avaris\\XboxMaps\\*.*");
	  system("xbdel /f xE:\\Avaris\\XboxStaticMeshes\\*.*");
	  system("xbdel /f xE:\\Avaris\\XboxVertexShaders\\*.*");
	  system("xbdel /f xE:\\Avaris\\XboxTextures\\*.*");
	  system("xbdel /f xE:\\Avaris\\System\\*.*");
	  print "Xbox file deletion complete.\n";
	  unclock "XBOXDELETE";
	}

	if ( $docheckout == 1 )
	{
		# retrieve checkout list from BuildFileList.txt
		getcheckoutlist();
	}

	if ( $docheckout == 1  && $dorevert == 1 )
	{
		# revert everything in the file list first so if a previous build failed to check in it doesn't break all future builds
		# necessary because the script can't detect failed submission
		foreach $file (@checkoutfiles)
		{
			system("p4 -c \"$clientspec\" revert \"$cspecpath\\$file\"");
		}
	}

	# sync
	if ($dosync == 1)
	{
		clock "SYNC";
		# to speed things up use line WITHOUT -f (partial sync, -f forces a full resynch)
		system("p4 -c \"$clientspec\" sync \"$depot...\"");
		unclock "SYNC";
	}

	if ($docheckout == 1)
	{
		clock "CHECKOUT";
		# mark checkout list files for edit in P4
		if( !checkoutall() )
		{
			revertall();
			return;
		}

		if ( !checkoutufiles() )
		{
			revertall();
			return;
		}
		unclock "CHECKOUT";
	}

	if ( $dosubmit == 1 )
	{
		clock "GET CHANGES";
		getchanges(0);
		unclock "GET CHANGES";
	}

	setversion;

	if ($dobuild == 1)
	{
		print "Compiling new build version: $version\n";

		# build
		system("del $serverlogfile.* /f");
		system("del $servermasterlogfile /f");
		system("del $systemdir\\avaris.ini /f");

		#build UCC

		if ( $buildpc == 1 )
		{
			print "START FULL COMPILE - BEFORE SCRIPT REBUILD $version\n";

			clock "BUILD CPP PASS #1";
			if ( $incredibuild == 1 )
			{
				print "USING INCREDIBUILD\n";
				system("BuildConsole \"$solution\" /rebuild /cfg=\"$BuildCfg\" /prj=\"$BuildPrj\" /log=\"$compilelog\" ");
			}
			else
			{
				system("\"$devenv\" \"$solution\" /rebuild \"$BuildCfg\" /out $compilelog");
			}
			unclock "BUILD CPP PASS #1";

			system("copy $compilelog $serverlogfile.cpp1.log /y");

			# check for build errors
			if( !checkbuildsuccess($compilelog) )
			{
				system("del $compilelog /f");
				revertall();
				announcefailure("PC Rebuild", "$serverlogfile.cpp1.log");
				return;
			}
			system("del $compilelog /f");
		}

		if ( $builduc == 1 )
		{
			clock "BUILD UCC";
			#rebuild/replace Classes.h files before full rebuild
			system("del $systemdir\\*.u /f");

			system("copy /Y $systemdir\\DefUserXbox.ini $systemdir\\User.ini");
			system("copy /Y $systemdir\\DefaultXbox.ini $systemdir\\Avaris.ini");

			system("$systemdir\\ucc make $scriptcompileoptions -log=$compilelog" );  # capps removed the replace-identical

			system("del $systemdir\\Avaris.ini /f");
			system("del $systemdir\\User.ini /f");

			# check for script build errors
			system("copy $compilelog $serverlogfile.uc.log /y");
			system("del $compilelog /f");
			if (!open(SCRIPT_TEST,"< $systemdir\\$last_script_pkg"))
			{
				revertall();
				announcefailure("SCRIPT FAILURE #1", "$serverlogfile.uc.log");
				return;
			}

			# strip the scripttext & cpptext from the .u files
#			system("$systemdir\\ucc editor.stripsource");
			$need_cpp_pass2 = check_header_update(<SCRIPT_TEST>);
			close(SCRIPT_TEST);
			unclock "BUILD UCC";
		}

		if ( $buildpc == 1 )
		{
			#full compile
			if ($need_cpp_pass2 != 0)
			{
				clock "BUILD CPP PASS #2";
				if ( $incredibuild == 1 )
				{
					system("BuildConsole \"$solution\" /cfg=\"$BuildCfg\" /prj=\"$BuildPrj\" /log=\"$compilelog\" ");
					# check for build errors again
					if( !checkbuildsuccess($compilelog) )
					{
						system("copy $compilelog $serverlogfile.cpp2.log /y");
						system("del $compilelog /f");
						revertall();
						announcefailure("PC after script", "$serverlogfile.cpp2.log");
						return;
					}
					system("del $systemdir\\*.u /f");
					system("copy /Y $systemdir\\DefUserXbox.ini $systemdir\\User.ini");
					system("copy /Y $systemdir\\DefaultXbox.ini $systemdir\\Avaris.ini");

					system("$systemdir\\ucc make $scriptcompileoptions -log=$compilelog" );  # capps removed the replace-identical

					system("del $systemdir\\Avaris.ini /f");
					system("del $systemdir\\User.ini /f");

					# check for script build errors
					if (!open(SCRIPT_TEST,"< $systemdir\\$last_script_pkg"))
					{
						system("copy $compilelog $serverlogfile.uc2.log /y");
						system("del $compilelog /f");
						revertall();
						announcefailure("SCRIPT FAILURE #2", "$serverlogfile.uc2.log");
						return;
					}
					close(SCRIPT_TEST);
				}
				else
				{
					system("\"$devenv\" \"$solution\" /build \"$BuildCfg\" /out $compilelog");
				}
				unclock "BUILD CPP PASS #2";
				system("copy $compilelog $serverlogfile.cpp2.log /y");
			}
			else
			{
				print "No headers were updated, skipping second build pass\n";
			}

			system("del $compilelog /f");
		}

		system("$systemdir\\ucc patchscript *.u -nosummary");
		revertscript();

		if ( $buildxbox == 1 )
		{
			# build XBox stuff
			print "START XBOX COMPILE $version\n";
			clock "BUILD XBOX";
			system("del \"$root_dir.\\$XBuildPrj\\Src\\$XBuildCfg\\*.*\" /q");
			if ( $incredibuild == 1 )
			{
				system("BuildConsole \"$solution\" /rebuild /cfg=\"$XBuildCfg\" /prj=\"$XBuildPrj\" /log=\"$xboxcompilelog\" ");
			}
			else
			{
				system("\"$devenv\" \"$solution\" /rebuild \"$XBuildCfg\" /project \"$xboxprojectfile\" /out $xboxcompilelog");
			}
			unclock "BUILD XBOX";

			if( !checkbuildsuccess($xboxcompilelog) )
			{
				system("copy $xboxcompilelog $serverlogfile.xbox.log /y");
				system("del $xboxcompilelog /f");
				revertall();
				announcefailure("Xbox Build", "$serverlogfile.xbox.log");
				return;
			}
		}

		# build XBox linear loading version, or linear saving version if doing linear save first
		if ( $dolinsave == 1 )
		{
			if ( $buildxboxls == 1 )
			{
				system("del \"$root_dir.\\$XBuildPrj\\Src\\$XBuildCfgLS\\*.*\" /q");
				print "START XBOX LINEAR SAVE COMPILE $version\n";
				clock "BUILD LINSAVE XBOX";
				if ( $incredibuild == 1 )
				{
					system("BuildConsole \"$solution\" /rebuild /cfg=\"$XBuildCfgLS\" /prj=\"$XBuildPrj\" /log=\"$xboxcompilelog\" ");
				}
				else
				{
					system("\"$devenv\" \"$solution\" /rebuild \"$XBuildCfgLS\" /project \"$xboxprojectfile\" /out $xboxcompilelog");
				}
				unclock "BUILD LINSAVE XBOX";
			}
		}
		else
		{
			if ( $buildxboxll == 1 )
			{
				system("del \"$root_dir.\\$XBuildPrj\\Src\\$XBuildCfgLL\\*.*\" /q");
				print "START XBOX LINEAR LOAD COMPILE $version\n";
				clock "BUILD LINLOAD XBOX";
				if ( $incredibuild == 1 )
				{
					system("BuildConsole \"$solution\" /rebuild /cfg=\"$XBuildCfgLL\" /prj=\"$XBuildPrj\" /log=\"$xboxcompilelog\" ");
				}
				else
				{
					system("\"$devenv\" \"$solution\" /rebuild \"$XBuildCfgLL\" /project \"$xboxprojectfile\" /out $xboxcompilelog");
				}
				unclock "BUILD LINLOAD XBOX";
			}
		}

		if ( $buildxboxls == 1 || $buildxboxll == 1 )
		{
			if( !checkbuildsuccess($xboxcompilelog) )
			{
				system("copy $xboxcompilelog $serverlogfile.xboxl1.log /y");
				system("del $xboxcompilelog /f");
				revertall();
				announcefailure("Xbox Linear Step 1 (build)", "$serverlogfile.xboxl1.log");
				return;
			}
		}

	}

	if ($doassets == 1)
	{
		clock "BUILD XBOX ASSETS";
		# build vertex shaders and RDF/XPR files
		system("$systemdir\\xboxassetconversion all -log=$assetcompilelog.log > $assetconsolelog");
		unclock "BUILD XBOX ASSETS";
	}

	# copy linear save files over to the xbox, and run the linear save program
	if ( $dobuild == 1 && $dolinsave == 1 )
	{
		system("echo bleh > linflag.txt");
		system("xbcp /f linflag.txt xe:\\Avaris\\$linflag > $linsavelogfile");
		system("del linflag.txt >> $linsavelogfile");
		system("del /q $root_dir\\XboxMaps\\*.lin");
		system("del /q $root_dir\\System\\*.lin");
		system("$root_dir\\sync_LS.bat $root_dir Avaris >> $linsavelogfile");  #copy files over

		# busy wait until done
		if ( !waitforlinsave() )
		{
			if ( bypassfailure("Xbox Linear Step 2 (building .LIN files)") != 1 )
			{
				revertall();
				return;
			}
		}

		# if done, copy all *.lin files to Xboxmaps dir on PC
		system("xbcp /f xe:\\Avaris\\System\\*.lin $root_dir\\XboxMaps >> $linsavelogfile");

		# verify that all .lin files were generated correctly
		if ( !checklinsuccess() )
		{
			if ( bypassfailure("Xbox Linear Step 2 (verify .LIN files)") != 1 )
			{
				revertall();
				return;
			}
		}

		# then move the script.lin into the System directory
		system("move /Y $root_dir\\XboxMaps\\script.lin $systemdir");

		# generate the TOC for the LL configuration
		system("$systemdir\\xboxassetconversion filesizes -log=$assetcompilelog.TOC.log >> $assetconsolelog");
		# build linear load.  note that functional programming is good!

		if ( $buildxboxll == 1 )
		{
			system("del \"$root_dir.\\$XBuildPrj\\Src\\$XBuildCfgLL\\*.*\" /q");
			print "START XBOX LINEAR LOAD COMPILE $version\n";
			clock "BUILD LINLOAD XBOX";
			if ( $incredibuild == 1 )
			{
				system("BuildConsole \"$solution\" /rebuild /cfg=\"$XBuildCfgLL\" /prj=\"$XBuildPrj\" /log=\"$xboxcompilelog\" ");
			}
			else
			{
				system("\"$devenv\" \"$solution\" /rebuild \"$XBuildCfgLL\" /project \"$xboxprojectfile\" /out $xboxcompilelog");
			}
			unclock "BUILD LINLOAD XBOX";
			if( !checkbuildsuccess($xboxcompilelog) )
			{
				system("copy $xboxcompilelog $serverlogfile.xboxl2.log /y");
				system("del $xboxcompilelog /f");
				revertall();
				announcefailure("Xbox Linear Step 3 (build LL after lin save)", "$serverlogfile.xboxl2.log");
				return;
			}
		}
	}


	if ($dobuild == 1 && $buildxboxll && $buildxboxfinal == 1 ) {
	  system("del \"$root_dir.\\$XBuildPrj\\Src\\$XBuildCfgFinal\\*.*\" /q");
	  print "START XBOX LINEAR LOAD FINAL COMPILE $version\n";
	  clock "BUILD XBOX FINAL";
	  if ( $incredibuild == 1 )
	    {
	      system("BuildConsole \"$solution\" /rebuild /cfg=\"$XBuildCfgFinal\" /prj=\"$XBuildPrj\" /log=\"$xboxcompilelog\" ");
	    }
	  else
	    {
	      system("\"$devenv\" \"$solution\" /rebuild \"$XBuildCfgFinal\" /project \"$xboxprojectfile\" /out $xboxcompilelog");
	    }
	  unclock "BUILD XBOX FINAL";
	  if( !checkbuildsuccess($xboxcompilelog) )
	    {
	      system("copy $xboxcompilelog $serverlogfile.xboxfinal.log /y");
	      system("del $xboxcompilelog /f");
	      revertall();
	      announcefailure("Xbox Linear Step 4 (build FINAL after lin load)", "$serverlogfile.xboxfinal.log");
	      return;
	    }
	}

	if ($dosubmit == 1)
	{

		# update build.ini
		updatebuildini();

		# submit
		print "START SUBMIT $version\n";
		clock "SUBMIT BUILD";
		my $submitresult = submitall();
		unclock "SUBMIT BUILD";

		if ( $submitresult != 1 )
		{
			announcesubmitfailure();
			if ( $waitforsubmit == 1 )
			{
				print "To allow the current build to finish, fix all problems preventing the submit from succeeding,\nmanually submit the build, then type 1 and press enter.\nType anything else and press enter to begin reverting all files:\n";
				my $result = <STDIN>;
				if ( $result != 1 )
				{
					revertall();
					return;
				}
			}
			else
			{
				revertall();
				return;
			}
		}

		# label
		clock "LABEL BUILD";
		label();
		unclock "LABEL BUILD";
	}

	# announce build success
	clock "ANNOUNCE";
	announce();
	unclock "ANNOUNCE";

	# master
	# Calls "ucc master" to build Setup executable/installer
	#uccmaster();

	print "BUILD COMPLETE $version\n";
	print "Total time: ".elapsed_time(time-$build_time)."\n";
}

#############################################################
# main

# ping the xbox, make sure it's alive
$error_level = system("xbdir xe:\\ > output.txt");
if ($error_level != 0)
{
   # send out an email bitching
   announcexboxfailure;
}
else
{
   if( !(-e $compilelogdir) )
   {
     system("mkdir $compilelogdir");
   }

   dobuild;

#   print "\n\n\nPaused .... ";
#   $garbage = <STDIN>;
}

