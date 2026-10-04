#!/usr/bin/perl.exe

$buildflag				= '\\\\build-server\\BuildFlags\\makeUC2build.txt';
$buildscript                            = 'f:\\Build_UC2\\BuildProcess\\sync_avaris.pl';

print "\n\n\nHEY YOU!!!!\n";
print "\nMAKE SURE THE XBOX IS ON!!!!!!!!\n";

while(1) {

  print "Checking for buildflag...\n";
  if( -e $buildflag ) {
    print "Doing a build!\n";
    system( "$buildscript" );
    system( "del $buildflag /f" );
  }
  sleep(10);

}

