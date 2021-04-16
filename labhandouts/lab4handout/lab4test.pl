#!/usr/bin/perl
use File::Spec;

$drivetests = "../../base/drivetest.pl";

print "\nLab 4 Testing Starting\n\n";

$tool = File::Spec->rel2abs( "./address_translation" ) ;

$tbegin = time();
#invoke test script in this directory
open TB, "$drivetests $tool 2>&1 |";
while(<TB>) {
  print $_;
}

$ttotal = time() - $tbegin;

print "\n\nLab 4 Testing Complete in $ttotal seconds\n\n";
