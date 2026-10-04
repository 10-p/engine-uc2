!# perl

# UNDO CHANGELIST - This is a DANGEROUS program.  
# You must have the proper changelist number!

print "Enter Changelist Number:\n";

chomp( $listnum = <STDIN> );
$prevlistnum = $listnum - 1;

print "\n\n";
print "WARNING! WARNING! WARNING!\n";
print "This will undo all changes associated with changelist $listnum\n";
print "Are you sure you want to continue? (y/n)\n";

chomp( $reply = <STDIN> );
if( $reply eq "y" ) {
	open( CLDESCRIP, "p4 describe -s $listnum|");

	$list_flag = 0;
	while( <CLDESCRIP> ) {
		chomp;
		
		#Ignore everthing before the "Affeced files ..." Line
		if( $list_flag && $_ ) {
		
			#Gather files into 3 lists: Edited, Added, and Deleted Files
			if( /\.{3} (.*)#\d+ (.*)/ ) {
				$file = $1;
				$type = $2;
				
				if( $type eq "edit" || $type eq "integrate" ) {
					@edited_files = (@edited_files, $file);
				}
				elsif( $type eq "add" ) {
					@added_files = (@added_files, $file);
				}
				elsif( $type eq "delete" ) {
					@deleted_files = (@deleted_files, $file);
				}
			}
		}
		elsif( !$list_change && /Affected files .../ ) {
			$list_flag = 1;
		}
	}
	
	#List gathered files
	foreach $item (@edited_files)  { print "EDIT   FILE: $item\n"; }
	foreach $item (@added_files)   { print "ADD    FILE: $item\n"; }
	foreach $item (@deleted_files) { print "DELETE FILE: $item\n"; }
	
	#(1)  Syncs workspace to files at the state they were in before changelist was submitted.  	
	print	"\np4 sync \@$prevlistnum\n";
	system( "p4 sync \@$prevlistnum" );

	#(2)  Opens the edited file for edit.  
	if( @edited_files ) {
		print   "\np4 edit @edited_files\n";
		system( "p4 edit @edited_files" );
	}

	#(3)  Opens the deleted file for add.	
	if( @deleted_files ) {
		print	"\np4 add @deleted_files\n";
		system( "p4 add @deleted_files" );
	}

	#(4)  Syncs workspace to files at the state they were in as of changelist. 
	#	  The opened files are not modified. The file that is opened for edit is scheduled for resolve.  	
	print	"\np4 sync \@$listnum\n";
	system( "p4 sync \@$listnum" );

	#(5)  Resolves with "accept yours" so that its contents are untouched.  
	print	"\np4 resolve -ay\n";
	system( "p4 resolve -ay" );

	#(6)  Syncs workspace to head revisions. Opened files are not modifed, but they are scheduled for resolve again.  
	print	"\np4 sync\n";
	system( "p4 sync" );

	#(7)  Allows you to interactively resolve all changes that were made after changelist.  	
	print	"\np4 resolve\n";
	print	"YOU MUST INTERACTIVELY RESOLVE THESE FILES\n";
	system( "p4 resolve" );

	#(8)  Opens changelist's added file for delete.  	
	if( @added_files ) {
		print	"\np4 delete @added_files\n";
		system( "p4 delete @added_files" );
	}
	
	#(9)  Submits fixed files as newest versions.  
	print	"\np4 submit\n";
	system( "p4 submit" );

	close( CLDESCRIP );
}
